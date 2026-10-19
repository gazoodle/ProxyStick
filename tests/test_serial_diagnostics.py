"""Check the real serial recorder without building firmware or requiring UART hardware."""

import subprocess
from pathlib import Path


def test_serial_recorder_on_host(tmp_path):
    root = Path(__file__).resolve().parents[1]
    for directory in ("esphome/core", "driver", "freertos", "hal"):
        (tmp_path / directory).mkdir(parents=True)
    (tmp_path / "esphome/core/defines.h").write_text("")
    (tmp_path / "esphome/core/log.h").write_text(
        "#define ESP_LOGE(...) ((void)0)\n#define ESP_LOGI(...) ((void)0)\n"
    )
    (tmp_path / "freertos/FreeRTOS.h").write_text(
        "#pragma once\nusing portMUX_TYPE=int; using SemaphoreHandle_t=void*;\n"
        "constexpr int pdTRUE=1;\n#define portMUX_INITIALIZER_UNLOCKED 0\n"
        "bool xPortInIsrContext();\n"
        "#define portENTER_CRITICAL(p) ((void)(p))\n#define portEXIT_CRITICAL(p) ((void)(p))\n"
    )
    (tmp_path / "freertos/semphr.h").write_text(
        '#include "FreeRTOS.h"\nSemaphoreHandle_t xSemaphoreCreateMutex();\n'
        "int xSemaphoreTake(SemaphoreHandle_t,unsigned); int xSemaphoreGive(SemaphoreHandle_t);\n"
    )
    (tmp_path / "driver/uart.h").write_text(
        "#pragma once\n#include <cstddef>\nusing uart_port_t=int; using esp_err_t=int;\n"
        "constexpr int UART_NUM_1=1,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,UART_STOP_BITS_1=1,"
        "UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0,UART_PIN_NO_CHANGE=-1,ESP_OK=0;\n"
        "struct uart_config_t { int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk; };\n"
        "int uart_param_config(int,const uart_config_t*); int uart_set_pin(int,int,int,int,int);\n"
        "int uart_driver_install(int,int,int,int,void*,int); int uart_driver_delete(int);\n"
        "int uart_disable_rx_intr(int); int uart_get_tx_buffer_free_size(int,size_t*);\n"
        "int uart_write_bytes(int,const void*,size_t);\n"
        "const char* esp_err_to_name(int);\n"
    )
    (tmp_path / "esp_random.h").write_text("#include <cstdint>\nuint32_t esp_random();\n")
    (tmp_path / "esp_timer.h").write_text("#include <cstdint>\nint64_t esp_timer_get_time();\n")
    executable = tmp_path / "serial-check"
    result = subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-DUSE_PROXY_STICK_DEBUG",
            "-DUSE_PROXY_STICK_DEBUG_SERIAL",
            f"-I{tmp_path}",
            str(Path(__file__).with_name("serial_diagnostics_check.cpp")),
            str(root / "components/proxy_stick/diagnostics_serial.cpp"),
            "-o",
            str(executable),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    subprocess.run([str(executable)], check=True, capture_output=True)
    # Serial-only mode must not pull in the HTTP uploader or its ring.
    for name in ("diagnostics.cpp", "diagnostics_serial.cpp"):
        serial = name == "diagnostics.cpp"
        output = tmp_path / f"{name}.o"
        subprocess.run(
            [
                "c++",
                "-std=c++17",
                f"-I{tmp_path}",
                *(["-DUSE_PROXY_STICK_DEBUG", "-DUSE_PROXY_STICK_DEBUG_SERIAL"] if serial else []),
                "-c",
                str(root / "components/proxy_stick" / name),
                "-o",
                str(output),
            ],
            check=True,
            capture_output=True,
        )
        symbols = subprocess.run(["nm", str(output)], check=True, capture_output=True, text=True).stdout
        assert "upload_worker" not in symbols and "ring" not in symbols


def test_tinyusb_config_extension_reaches_c_translation_units(tmp_path):
    root = Path(__file__).resolve().parents[1]
    header = root / "components/proxy_stick/proxy_stick_tusb_config.h"
    (tmp_path / "tusb_config.h").write_text(
        "#pragma once\n#define ORIGINAL_BUFFER_SIZE 4096\n"
        "#define CFG_TUSB_DEBUG_PRINTF wrong_sink\n#define CFG_TUD_LOG_LEVEL 3\n"
    )
    source = tmp_path / "config.c"
    source.write_text(
        "#include CFG_TUSB_CONFIG_FILE\n"
        "#if ORIGINAL_BUFFER_SIZE != 4096 || CFG_TUD_LOG_LEVEL != 2 || CFG_TUD_MSC_LOG_LEVEL != 2\n"
        "#error configuration changed\n#endif\n"
        "int proxy_stick_tusb_printf(const char *format, ...) { return format[0] == 120 ? 0 : 1; }\n"
        'int main(void) { return CFG_TUSB_DEBUG_PRINTF("x"); }\n'
    )
    executable = tmp_path / "config-check"
    result = subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Werror",
            f"-I{tmp_path}",
            f'-DCFG_TUSB_CONFIG_FILE="{header}"',
            str(source),
            "-o",
            str(executable),
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    subprocess.run([str(executable)], check=True)
