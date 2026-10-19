#include <cassert>
#include "../components/proxy_stick/diagnostic_command.h"

using esphome::proxy_stick::diagnostics::CommandSummary;

int main() {
  CommandSummary summary;
  summary.start(100, 512);
  for (unsigned i = 0; i < 8; i++)
    summary.buffer(512 + i * 8192, 8192, i == 3 ? 138406 : 200, true, true);
  assert(summary.offset == 512 && summary.bytes == 65536);
  assert(summary.callbacks == 8 && summary.max_source_us == 138406);
  assert(summary.delayed == 8 && summary.flags == 0);
  summary.buffer(512 + 65536, 512, 500, false, false);
  assert(summary.bytes == 65536 && summary.callbacks == 9 && (summary.flags & 1));
  summary.buffer(0, 512, 100, true, false);
  assert(summary.flags & 2);
  summary.reported_idle = true;
  summary.start(300, 1024);
  assert(summary.bytes == 0 && summary.callbacks == 0 && summary.max_source_us == 0);
  assert(summary.delayed == 0 && summary.flags == 0 && !summary.reported_idle);
  assert(summary.started_us == 300 && summary.offset == 1024 && summary.active);
}
