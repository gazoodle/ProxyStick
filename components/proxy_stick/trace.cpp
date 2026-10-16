#include "trace.h"

#ifdef USE_PROXY_STICK_TRACE

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <vector>

#include "esp_rtc_time.h"
#include "esp_system.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"
#include "tusb.h"

namespace esphome::proxy_stick::trace {
namespace {

static const char *const TAG = "proxy_stick.trace";
constexpr size_t PER_BOOT = 256;
constexpr size_t CHUNK = 32;  // Entries per flash write: a flush rewrites only the chunks that changed.
constexpr size_t CHUNKS = PER_BOOT / CHUNK;
constexpr uint32_t BOOTS = 16;
constexpr uint32_t FLUSH_MS = 250;  // At most this much is lost when the power goes, bar urgent events.
// Early in a boot, a heartbeat every HEARTBEAT_MS, written at once: when a host cuts the power, the
// trace says to within that when the stick died. Later the heartbeat stops, sparing the flash.
constexpr uint32_t HEARTBEAT_MS = 100;
constexpr uint32_t HEARTBEAT_UNTIL_MS = 15000;
constexpr size_t DUMP_LINES_PER_STEP = 8;

struct Entry {
  uint32_t ms;
  uint8_t kind;
  uint8_t code;
  uint16_t count;
  uint8_t data[8];
};
static_assert(sizeof(Entry) == 16, "the stored format");

std::array<Entry, PER_BOOT> entries;
size_t used{0};
uint8_t dirty{0};  // A bit per chunk.
uint32_t boot{0};
nvs_handle_t nvs{0};
SemaphoreHandle_t lock{nullptr};
TaskHandle_t flusher{nullptr};

void key(char (&out)[16], uint32_t slot, size_t chunk) {
  std::snprintf(out, sizeof(out), "s%02" PRIu32 "c%u", slot, static_cast<unsigned>(chunk));
}

void flush() {
  std::array<Entry, CHUNK> copy;
  for (size_t chunk = 0; chunk < CHUNKS; chunk++) {
    xSemaphoreTake(lock, portMAX_DELAY);
    const bool changed = dirty & (1u << chunk);
    const size_t count = std::min(CHUNK, used > chunk * CHUNK ? used - chunk * CHUNK : 0);
    if (changed)
      std::copy_n(entries.begin() + chunk * CHUNK, count, copy.begin());
    dirty &= ~(1u << chunk);
    xSemaphoreGive(lock);
    if (!changed || count == 0)
      continue;
    char name[16];
    key(name, boot % BOOTS, chunk);
    nvs_set_blob(nvs, name, copy.data(), count * sizeof(Entry));
  }
  nvs_commit(nvs);
}

void flush_task(void *) {
  while (true) {
    const bool early = millis() < HEARTBEAT_UNTIL_MS;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(early ? HEARTBEAT_MS : FLUSH_MS));
    if (early) {
      uint8_t sample[5];
      const uint32_t now = millis();
      std::memcpy(sample, &now, sizeof(now));
      sample[4] = (tud_connected() ? USB_SPOKEN_TO : 0) | (tud_mounted() ? USB_CONFIGURED : 0) |
                  (tud_suspended() ? USB_SUSPENDED : 0);
      record(ALIVE, 0, sample, sizeof(sample));
    }
    flush();
  }
}

const char *reset_reason(uint8_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:
      return "power on";
    case ESP_RST_SW:
      return "software";
    case ESP_RST_PANIC:
      return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
      return "watchdog";
    case ESP_RST_BROWNOUT:
      return "brownout";
    case ESP_RST_USB:
      return "USB";
    default:
      return "other";
  }
}

const char *bus_event(uint8_t event) {
  static const char *const NAMES[] = {"attached (configured)", "detached", "suspended", "resumed"};
  return event < 4 ? NAMES[event] : "?";
}

const char *milestone_name(uint8_t milestone) {
  static const char *const NAMES[] = {"network up",     "event stream open", "event stream lost",
                                      "medium ready",   "medium removed",    "USB connect",
                                      "USB disconnect", "setup done (WiFi radio started)",
                                      "USB-Serial/JTAG released (handover gap starts)"};
  return milestone < 9 ? NAMES[milestone] : "?";
}

uint32_t word(const uint8_t *data) {
  uint32_t value;
  std::memcpy(&value, data, sizeof(value));
  return value;
}

void describe(const Entry &entry, char (&out)[96]) {
  const uint8_t *d = entry.data;
  switch (entry.kind) {
    case BOOT:
      std::snprintf(out, sizeof(out), "BOOT %" PRIu32 ", reset reason %u (%s), %" PRIu32 " ms since power-on",
                    word(d), entry.code, reset_reason(entry.code), word(d + 4));
      return;
    case BUS:
      std::snprintf(out, sizeof(out), "USB bus %s", bus_event(entry.code));
      return;
    case TEST_UNIT_READY:
      std::snprintf(out, sizeof(out), "TEST UNIT READY -> %s", entry.code ? "ready" : "not ready (no medium)");
      return;
    case INQUIRY:
      std::snprintf(out, sizeof(out), "INQUIRY");
      return;
    case CAPACITY:
      std::snprintf(out, sizeof(out), "READ CAPACITY -> %" PRIu32 " blocks", word(d));
      return;
    case START_STOP:
      std::snprintf(out, sizeof(out), "START STOP UNIT start=%u eject=%u power=%u", entry.code & 1,
                    (entry.code >> 1) & 1, d[0]);
      return;
    case PREVENT_REMOVAL:
      std::snprintf(out, sizeof(out), "PREVENT ALLOW MEDIUM REMOVAL prohibit=%u", entry.code);
      return;
    case WRITE_PROTECT:
      std::snprintf(out, sizeof(out), "write-protect query (MODE SENSE 6) -> protected");
      return;
    case READ:
      std::snprintf(out, sizeof(out), "READ 10, LBA %" PRIu32 "..%" PRIu32, word(d), word(d + 4));
      return;
    case READ_FAILED:
      std::snprintf(out, sizeof(out), "READ 10 FAILED at LBA %" PRIu32, word(d));
      return;
    case WRITE:
      std::snprintf(out, sizeof(out), "WRITE 10 -> refused (write protected)");
      return;
    case REJECTED:
      std::snprintf(out, sizeof(out), "REJECTED opcode 0x%02X [%02X %02X %02X %02X %02X %02X %02X %02X]",
                    entry.code, d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
      return;
    case MILESTONE:
      std::snprintf(out, sizeof(out), "-- %s", milestone_name(entry.code));
      return;
    case FULL:
      std::snprintf(out, sizeof(out), "(trace full; nothing more recorded this boot)");
      return;
    case ALIVE:
      std::snprintf(out, sizeof(out), "alive, last seen at %" PRIu32 " ms; USB: %s%s%s", word(d),
                    d[4] & USB_SPOKEN_TO ? "spoken to" : "host silent", d[4] & USB_CONFIGURED ? ", configured" : "",
                    d[4] & USB_SUSPENDED ? ", suspended" : "");
      return;
    default:
      std::snprintf(out, sizeof(out), "kind %u code %u", entry.kind, entry.code);
  }
}

// The dump, a few lines per loop: the stored boots oldest first, each read back from flash.
struct Dump {
  bool active{false};
  std::vector<std::pair<uint32_t, uint32_t>> boots;  // (boot number, slot)
  size_t boot_index{0};
  size_t chunk{0};
  std::vector<Entry> chunk_entries;
  size_t entry{0};
} dump;

bool load_chunk(uint32_t slot, size_t chunk, std::vector<Entry> &out) {
  char name[16];
  key(name, slot, chunk);
  size_t size = 0;
  if (nvs_get_blob(nvs, name, nullptr, &size) != ESP_OK || size == 0)
    return false;
  out.resize(size / sizeof(Entry));
  return nvs_get_blob(nvs, name, out.data(), &size) == ESP_OK;
}

}  // namespace

void begin() {
  lock = xSemaphoreCreateMutex();
  if (lock == nullptr || nvs_open("ps_trace", NVS_READWRITE, &nvs) != ESP_OK) {
    ESP_LOGE(TAG, "Unable to open the trace's flash storage; no USB trace this boot");
    lock = nullptr;
    return;
  }
  nvs_get_u32(nvs, "boot", &boot);
  boot++;
  nvs_set_u32(nvs, "boot", boot);
  for (size_t chunk = 0; chunk < CHUNKS; chunk++) {
    char name[16];
    key(name, boot % BOOTS, chunk);
    nvs_erase_key(nvs, name);
  }
  nvs_commit(nvs);
  if (xTaskCreate(flush_task, "proxy_stick_trace", 3072, nullptr, 2, &flusher) != pdPASS) {
    ESP_LOGE(TAG, "Unable to start the trace's flush task; no USB trace this boot");
    lock = nullptr;
    return;
  }
  // The RTC counter starts at power-on, unlike millis(), which starts with the app: their difference is how
  // long the ROM and bootloader took, during which a host already sees the chip's USB-Serial/JTAG device.
  uint32_t boot_data[2] = {boot, static_cast<uint32_t>(esp_rtc_get_time_us() / 1000)};
  record(BOOT, esp_reset_reason(), boot_data, sizeof(boot_data), true);
  ESP_LOGI(TAG, "USB trace recording boot %" PRIu32, boot);
}

void record(Kind kind, uint8_t code, const void *data, size_t size, bool urgent) {
  if (lock == nullptr)
    return;
  Entry entry{millis(), kind, code, 1, {}};
  std::memcpy(entry.data, data, std::min(size, sizeof(entry.data)));

  xSemaphoreTake(lock, portMAX_DELAY);
  Entry *last = used > 0 ? &entries[used - 1] : nullptr;
  if (last != nullptr && last->kind == kind && kind == ALIVE && last->data[4] == entry.data[4]) {
    std::memcpy(last->data, entry.data, 4);  // One entry per run of heartbeats in one state: the latest time.
    last->count += last->count < UINT16_MAX;
    dirty |= 1u << ((used - 1) / CHUNK);
  } else if (last != nullptr && last->kind == kind && kind == READ) {
    // Consecutive reads become one entry: the first LBA stays, the last moves on.
    std::memcpy(last->data + 4, entry.data, 4);
    last->count += last->count < UINT16_MAX;
    dirty |= 1u << ((used - 1) / CHUNK);
  } else if (last != nullptr && last->kind == kind && last->code == code &&
             std::memcmp(last->data, entry.data, sizeof(entry.data)) == 0) {
    last->count += last->count < UINT16_MAX;
    dirty |= 1u << ((used - 1) / CHUNK);
  } else if (used < PER_BOOT) {
    if (used == PER_BOOT - 1)
      entry = Entry{millis(), FULL, 0, 1, {}};
    entries[used++] = entry;
    dirty |= 1u << ((used - 1) / CHUNK);
  }
  xSemaphoreGive(lock);

  if (urgent && flusher != nullptr)
    xTaskNotifyGive(flusher);
}

void start_dump() {
  if (lock == nullptr) {
    ESP_LOGW(TAG, "The USB trace is not recording; nothing to dump");
    return;
  }
  flush();
  dump = Dump{};
  for (uint32_t slot = 0; slot < BOOTS; slot++) {
    std::vector<Entry> first;
    if (load_chunk(slot, 0, first) && !first.empty() && first[0].kind == BOOT)
      dump.boots.emplace_back(word(first[0].data), slot);
  }
  std::sort(dump.boots.begin(), dump.boots.end());
  dump.active = true;
  ESP_LOGI(TAG, "USB trace: %u stored boots, oldest first; times are ms since that boot",
           static_cast<unsigned>(dump.boots.size()));
}

bool dump_step() {
  if (!dump.active)
    return false;
  for (size_t line = 0; line < DUMP_LINES_PER_STEP; line++) {
    if (dump.boot_index >= dump.boots.size()) {
      ESP_LOGI(TAG, "USB trace: end");
      dump.active = false;
      return false;
    }
    if (dump.entry >= dump.chunk_entries.size()) {
      const uint32_t slot = dump.boots[dump.boot_index].second;
      if (dump.chunk >= CHUNKS || !load_chunk(slot, dump.chunk, dump.chunk_entries)) {
        dump.boot_index++;
        dump.chunk = 0;
        dump.chunk_entries.clear();
        dump.entry = 0;
        continue;
      }
      dump.chunk++;
      dump.entry = 0;
      continue;
    }
    const Entry &entry = dump.chunk_entries[dump.entry++];
    char text[96];
    describe(entry, text);
    if (entry.count > 1)
      ESP_LOGI(TAG, "%8" PRIu32 " ms  %s  (x%u)", entry.ms, text, entry.count);
    else
      ESP_LOGI(TAG, "%8" PRIu32 " ms  %s", entry.ms, text);
  }
  return true;
}

}  // namespace esphome::proxy_stick::trace

#endif  // USE_PROXY_STICK_TRACE
