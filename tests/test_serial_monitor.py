"""Host-only collector checks: forwarding, overload reporting and setup/cleanup failures."""

import subprocess
from pathlib import Path


def test_serial_collector_on_host(tmp_path):
    root = Path(__file__).resolve().parents[1]
    for directory in ("esphome/core", "driver", "freertos"):
        (tmp_path / directory).mkdir(parents=True)
    (tmp_path / "esphome/core/component.h").write_text(
        "#pragma once\nnamespace esphome { class Component { public: bool failed=false; "
        "virtual void setup() {} virtual void dump_config() {} virtual void on_shutdown() {} "
        "void mark_failed() { failed=true; } }; }\n"
    )
    (tmp_path / "esphome/core/log.h").write_text(
        "void log_message(const char*,const char*);\n"
        "#define ESP_LOGE(t,f,...) log_message(t,f)\n"
        "#define ESP_LOGW(t,f,...) log_message(t,f)\n"
        "#define ESP_LOGI(t,f,...) log_message(t,f)\n"
        "#define ESP_LOGCONFIG(t,f,...) log_message(t,f)\n"
    )
    (tmp_path / "freertos/FreeRTOS.h").write_text(
        "#pragma once\nusing QueueHandle_t=void*; using TaskHandle_t=void*; using SemaphoreHandle_t=void*;\n"
        "constexpr unsigned portMAX_DELAY=~0u; constexpr int pdTRUE=1,pdPASS=1;\n"
        "#define pdMS_TO_TICKS(x) (x)\n"
        "int xQueueReceive(QueueHandle_t,void*,unsigned); int xQueueReset(QueueHandle_t);\n"
        "int xQueueSend(QueueHandle_t,const void*,unsigned);\n"
    )
    (tmp_path / "freertos/task.h").write_text(
        '#include "FreeRTOS.h"\n'
        "int xTaskCreate(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*);\n"
        "void vTaskDelete(TaskHandle_t);\n"
    )
    (tmp_path / "freertos/semphr.h").write_text(
        '#include "FreeRTOS.h"\n'
        "SemaphoreHandle_t xSemaphoreCreateBinary(); int xSemaphoreGive(SemaphoreHandle_t);\n"
        "int xSemaphoreTake(SemaphoreHandle_t,unsigned); void vSemaphoreDelete(SemaphoreHandle_t);\n"
    )
    (tmp_path / "driver/uart.h").write_text(
        '#pragma once\n#include <cstddef>\n#include <cstdint>\n#include "freertos/FreeRTOS.h"\n'
        "using uart_port_t=int; using esp_err_t=int;\n"
        "constexpr int UART_NUM_0=0,UART_NUM_1=1,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,"
        "UART_STOP_BITS_1=1,UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=0,UART_PIN_NO_CHANGE=-1,ESP_OK=0;\n"
        "enum { UART_DATA,UART_FIFO_OVF,UART_BUFFER_FULL,UART_FRAME_ERR,UART_PARITY_ERR,UART_BREAK };\n"
        "struct uart_event_t { int type; size_t size; };\n"
        "struct uart_config_t { int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk; };\n"
        "const char* esp_err_to_name(int); int uart_param_config(int,const uart_config_t*);\n"
        "int uart_set_pin(int,int,int,int,int); int uart_driver_install(int,int,int,int,QueueHandle_t*,int);\n"
        "int uart_driver_delete(int); int uart_read_bytes(int,void*,uint32_t,unsigned);\n"
        "int uart_write_bytes(int,const void*,size_t); int uart_flush_input(int);\n"
    )
    executable = tmp_path / "collector-check"
    result = subprocess.run(
        [
            "c++",
            "-std=c++17",
            "-Wall",
            "-Wextra",
            "-Werror",
            f"-I{tmp_path}",
            str(Path(__file__).with_name("serial_monitor_check.cpp")),
            str(root / "components/serial_monitor/serial_monitor.cpp"),
            "-o",
            str(executable),
        ],
        check=False,
        capture_output=True,
        text=True,
    )
    assert result.returncode == 0, result.stderr
    result = subprocess.run([str(executable)], check=False, capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
