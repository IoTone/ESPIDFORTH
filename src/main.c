/*
 * ESPIDFORTH - ESP32FORTH Port to ESP-IDF
 * Phase 2 of MagNET Hive AI prototype
 *
 * Boots Forth interpreter and provides a serial REPL.
 * Reports memory stats at startup.
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "forth_core.h"
#include "forth_version.h"

/*
 * Console backend.
 *
 * ESP-IDF compiles the USB-serial-JTAG driver only for targets whose soc_caps
 * advertise the peripheral, but installs its header unconditionally — so on
 * the classic ESP32 this file used to compile and then fail at link with
 * "undefined reference to usb_serial_jtag_write_bytes". Pick the backend from
 * the console the project is actually configured for, and fall back to UART,
 * which every target has.
 */
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#else
#include "driver/uart.h"

/* An unset ESP_CONSOLE_UART_NUM is -1 (the USB-console case); UART0 otherwise. */
#if defined(CONFIG_ESP_CONSOLE_UART_NUM) && CONFIG_ESP_CONSOLE_UART_NUM >= 0
#define CONSOLE_UART_NUM CONFIG_ESP_CONSOLE_UART_NUM
#else
#define CONSOLE_UART_NUM 0
#endif

#if defined(CONFIG_ESP_CONSOLE_UART_BAUDRATE)
#define CONSOLE_UART_BAUD CONFIG_ESP_CONSOLE_UART_BAUDRATE
#else
#define CONSOLE_UART_BAUD 115200
#endif
#endif /* CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG */

#define CONSOLE_RX_BUF 256
#define CONSOLE_TX_BUF 256

/* Default Forth dictionary heap size (100 KB) */
#define FORTH_HEAP_SIZE (100 * 1024)

/* ----- Console backend: raw byte I/O, bypassing VFS line buffering ----- */

static void console_install(void) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = {
        .tx_buffer_size = CONSOLE_TX_BUF,
        .rx_buffer_size = CONSOLE_RX_BUF,
    };
    usb_serial_jtag_driver_install(&cfg);
#else
    const uart_config_t cfg = {
        .baud_rate  = CONSOLE_UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(CONSOLE_UART_NUM, CONSOLE_RX_BUF, CONSOLE_TX_BUF, 0, NULL, 0);
    uart_param_config(CONSOLE_UART_NUM, &cfg);
#endif
}

static int console_write(const uint8_t *buf, size_t len, TickType_t ticks) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_write_bytes(buf, len, ticks);
#else
    (void)ticks;  /* uart_write_bytes blocks until the bytes are queued */
    return uart_write_bytes(CONSOLE_UART_NUM, buf, len);
#endif
}

static int console_read(uint8_t *buf, size_t len, TickType_t ticks) {
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_read_bytes(buf, len, ticks);
#else
    return uart_read_bytes(CONSOLE_UART_NUM, buf, len, ticks);
#endif
}

/* ----- Console helpers ----- */

static void console_print(const char *s) {
    console_write((const uint8_t *)s, strlen(s), pdMS_TO_TICKS(500));
}

/* snprintf + console_print helper */
static void console_printf(const char *fmt, ...) {
    char buf[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    console_print(buf);
}

static void print_memory_stats(const char *label) {
    console_printf("\r\n=== Memory Stats: %s ===\r\n", label);
    console_printf("  Free heap (internal): %lu bytes\r\n",
                   (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    console_printf("  Largest free block:   %lu bytes\r\n",
                   (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
    console_printf("  Min free ever:        %lu bytes\r\n",
                   (unsigned long)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
#if CONFIG_SPIRAM
    console_printf("  Free PSRAM:           %lu bytes\r\n",
                   (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    console_printf("  Largest PSRAM block:  %lu bytes\r\n",
                   (unsigned long)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
#endif
    console_print("===========================\r\n\r\n");
}

/* ----- Character I/O callbacks handed to the Forth REPL ----- */

static int console_getchar(void) {
    uint8_t c;
    int n = console_read(&c, 1, pdMS_TO_TICKS(10));
    if (n <= 0) return -1;
    return c;
}

static void console_putchar(int c) {
    uint8_t ch = (uint8_t)c;
    console_write(&ch, 1, pdMS_TO_TICKS(100));
}


void app_main(void) {
    /* Set up the console driver for raw char I/O */
    console_install();

    /* Small delay to let a USB host enumerate before sending */
    vTaskDelay(pdMS_TO_TICKS(500));

    console_print("\r\n\r\n");
    console_print("============================================\r\n");
    console_printf("  ESPIDFORTH v%s\r\n", ESPIDFORTH_VERSION_STRING);
    console_printf("  Build: %s %s\r\n", ESPIDFORTH_BUILD_DATE, ESPIDFORTH_BUILD_TIME);
    console_print("  Phase 2: MagNET Hive AI Prototype\r\n");
    console_print("============================================\r\n");

    print_memory_stats("Before Forth init");

    /* Determine heap size: use more on PSRAM-equipped targets */
    int heap_size = FORTH_HEAP_SIZE;
#if CONFIG_SPIRAM
    heap_size = 512 * 1024;  /* 512 KB when PSRAM available */
    console_printf("PSRAM detected, using %d KB Forth heap\r\n", heap_size / 1024);
#else
    console_printf("No PSRAM, using %d KB Forth heap\r\n", heap_size / 1024);
#endif

    console_print("Initializing Forth engine...\r\n");
    int rc = forth_init(heap_size);
    if (rc != 0) {
        console_printf("Failed to initialize Forth engine (rc=%d)\r\n", rc);
        return;
    }
    console_print("Forth engine initialized.\r\n");

    print_memory_stats("After Forth init");

    /* Run REPL directly in app_main (blocks forever) */
    forth_repl(console_getchar, console_putchar);

    /* Only reached if user types 'bye' */
    forth_deinit();
}
