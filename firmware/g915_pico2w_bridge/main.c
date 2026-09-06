#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/cyw43_arch.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "app_config.h"
#include "ble/hog_client.h"
#include "core/report_pipe.h"
#include "usb/usb_keyboard.h"

static report_pipe_t report_pipe;
static hog_client_state_t bluetooth_state = HOG_CLIENT_STARTING;
static bool led_state;

static void handle_keyboard_state(const keyboard_state_t *state,
                                  uint32_t epoch, void *context) {
    keyboard_state_t host_state = *state;
    keyboard_state_filter_for_boot_report(
        &host_state, APP_USB_KEY_USAGE_MAX,
        APP_USB_FORWARD_SOURCE_ERRORS != 0);
    report_pipe_publish((report_pipe_t *)context, &host_state, epoch);
}

static void handle_source_reset(uint32_t epoch, void *context) {
    report_pipe_source_reset((report_pipe_t *)context, epoch);
}

static void handle_bluetooth_status(hog_client_state_t state, uint8_t status,
                                    void *context) {
    (void)context;
    bluetooth_state = state;
    if (status != 0) {
        printf("BLE state %u, status 0x%02x\n", (unsigned)state, status);
    }
}

static void service_status_led(void) {
    bool wanted = bluetooth_state == HOG_CLIENT_READY;
    if (!wanted) {
        const uint64_t elapsed_ms =
            to_ms_since_boot(get_absolute_time());
        wanted = ((elapsed_ms / APP_STATUS_LED_BLINK_INTERVAL_MS) & 1u) != 0;
    }

    if (wanted == led_state) return;
    led_state = wanted;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, wanted);
}

static void service_uart_commands(void) {
    static const char forget_command[] = "forget";
    static char line[sizeof(forget_command)];
    static size_t length;
    static bool line_too_long;

    const int value = getchar_timeout_us(0);
    if (value == PICO_ERROR_TIMEOUT) return;

    if (value == '\r' || value == '\n') {
        if (!line_too_long && length == sizeof(forget_command) - 1u &&
            memcmp(line, forget_command, sizeof(forget_command) - 1u) == 0) {
            printf("Forgetting approved keyboard and returning to pairing\n");
            hog_client_forget_peer();
        }
        length = 0;
        line_too_long = false;
        return;
    }

    if (length < sizeof(forget_command) - 1u) {
        line[length++] = (char)value;
    } else {
        line_too_long = true;
    }
}

int main(void) {
    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 0);

    if (cyw43_arch_init() != PICO_OK) {
        panic("CYW43439 initialization failed");
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    report_pipe_init(&report_pipe);
    usb_keyboard_init(&report_pipe);
    tusb_init();

    const hog_client_callbacks_t callbacks = {
        .keyboard_state_changed = handle_keyboard_state,
        .source_reset = handle_source_reset,
        .status_changed = handle_bluetooth_status,
        .context = &report_pipe,
    };
    hog_client_init(&callbacks);

    while (true) {
        // The polling CYW43 integration deliberately keeps BTstack callbacks
        // in this foreground loop with TinyUSB and the report pipeline.
        tud_task();
        usb_keyboard_task();
        cyw43_arch_poll();
        service_uart_commands();
        service_status_led();
        tight_loop_contents();
    }
}
