#include <stdbool.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "tusb.h"

#include "app_config.h"
#include "core/report_pipe.h"
#include "host/usb_host_keyboard.h"
#include "usb/usb_keyboard.h"

static report_pipe_t report_pipe;
static bool led_state;

#if BRIDGE_USB_CDC_DIAGNOSTICS
#define DIAGNOSTIC_LOG_QUEUE_DEPTH 64u
#define DIAGNOSTIC_LOG_MESSAGE_SIZE 128u

typedef struct {
    char text[DIAGNOSTIC_LOG_MESSAGE_SIZE];
} diagnostic_log_message_t;

static queue_t diagnostic_log_queue;

static void diagnostic_log_init(void) {
    queue_init(&diagnostic_log_queue, sizeof(diagnostic_log_message_t),
               DIAGNOSTIC_LOG_QUEUE_DEPTH);
}

int bridge_debug_printf(const char *format, ...) {
    diagnostic_log_message_t message = {0};
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(message.text, sizeof(message.text), format, args);
    va_end(args);

    if (!queue_try_add(&diagnostic_log_queue, &message)) {
        diagnostic_log_message_t discarded;
        (void)queue_try_remove(&diagnostic_log_queue, &discarded);
        (void)queue_try_add(&diagnostic_log_queue, &message);
    }
    return length;
}
#endif

#if BRIDGE_USB_CDC_DIAGNOSTICS
static void service_usb_diagnostics(void) {
    static uint32_t last_report_ms;
    const uint32_t now = to_ms_since_boot(get_absolute_time());
    if (!tud_cdc_connected()) return;

    diagnostic_log_message_t message;
    while (queue_try_remove(&diagnostic_log_queue, &message)) {
        tud_cdc_write(message.text, (uint32_t)strnlen(
            message.text, sizeof(message.text)));
    }

    if ((uint32_t)(now - last_report_ms) < 1000u) {
        tud_cdc_write_flush();
        return;
    }
    last_report_ms = now;

    usb_host_keyboard_diagnostics_t diagnostics;
    usb_host_keyboard_get_diagnostics(&diagnostics);
    char line[192];
    const int length = snprintf(
        line, sizeof(line),
        "host_stage=%u vid=%04x pid=%04x hid_interfaces=%u keyboards=%u "
        "poll_frames=%u arm_failures=%u\r\n",
        (unsigned)diagnostics.status, diagnostics.vid, diagnostics.pid,
        diagnostics.hid_interface_count, diagnostics.keyboard_interface_count,
        (unsigned)diagnostics.min_report_frame_gap,
        (unsigned)diagnostics.report_arm_failure_count);
    if (length > 0) {
        tud_cdc_write(line, (uint32_t)length);
        tud_cdc_write_flush();
    }
}
#endif

static void service_status_led(void) {
    bool wanted = usb_host_keyboard_status() == USB_HOST_KEYBOARD_READY;
    if (!wanted) {
        const uint64_t elapsed_ms = to_ms_since_boot(get_absolute_time());
        wanted = ((elapsed_ms / APP_STATUS_LED_BLINK_INTERVAL_MS) & 1u) != 0;
    }

    if (wanted == led_state) return;
    led_state = wanted;
    gpio_put(PICO_DEFAULT_LED_PIN, wanted);
}

int main(void) {
    // Pico-PIO-USB requires an integer multiple of 12 MHz. 120 MHz is its
    // documented RP2040 operating clock and remains valid for native USB.
    if (!set_sys_clock_khz(120000, true)) {
        panic("Could not set the 120 MHz PIO USB clock");
    }

#if BRIDGE_USB_CDC_DIAGNOSTICS
    diagnostic_log_init();
#endif
    stdio_init_all();
    setvbuf(stdout, NULL, _IONBF, 0);

    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, false);

    gpio_init(APP_USB_HOST_5V_ENABLE_PIN);
    gpio_set_dir(APP_USB_HOST_5V_ENABLE_PIN, GPIO_OUT);
    // Force every receiver through a real disconnect before host startup.
    gpio_put(APP_USB_HOST_5V_ENABLE_PIN, false);
    sleep_ms(20);
    gpio_put(APP_USB_HOST_5V_ENABLE_PIN, true);
    sleep_ms(100);

    report_pipe_init(&report_pipe);
    usb_keyboard_init(&report_pipe);
    usb_host_keyboard_init();

    const tusb_rhport_init_t device_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(APP_USB_DEVICE_RHPORT, &device_init)) {
        panic("TinyUSB device initialization failed");
    }

    multicore_launch_core1(usb_host_keyboard_core1);
    printf("G915 Feather RP2040 USB Host bridge started\n");

    while (true) {
        tud_task();
        usb_host_keyboard_task(&report_pipe);
        usb_keyboard_task();
#if BRIDGE_USB_CDC_DIAGNOSTICS
        service_usb_diagnostics();
#endif
        service_status_led();
        tight_loop_contents();
    }
}
