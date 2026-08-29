#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "pico/util/queue.h"
#include "tusb.h"

#include "bridge.h"

int btstack_main(int argc, const char *argv[]);

#define STATUS_LED_BLINK_INTERVAL_MS 500

typedef struct {
    uint8_t bytes[8];
} keyboard_report_t;

static queue_t report_queue;
static volatile bool bluetooth_ready;
static bool led_state;

static void queue_latest_report(const uint8_t bytes[8]) {
    keyboard_report_t report;
    memcpy(report.bytes, bytes, sizeof(report.bytes));
    report.bytes[1] = 0;

    if (queue_try_add(&report_queue, &report)) return;

    // Reports are complete state snapshots. If USB is stalled, retain the
    // newest state so a release report cannot be lost permanently.
    keyboard_report_t discarded;
    queue_try_remove(&report_queue, &discarded);
    queue_try_add(&report_queue, &report);
}

void bridge_keyboard_report(const uint8_t *report, size_t length) {
    if (length < 8) return;
    queue_latest_report(report);
}

void bridge_keyboard_connected(void) {
    bluetooth_ready = true;
}

void bridge_keyboard_disconnected(void) {
    static const uint8_t released[8] = {0};
    bluetooth_ready = false;
    queue_latest_report(released);
}

static void service_status_led(void) {
    bool wanted = bluetooth_ready;
    if (!wanted) {
        uint64_t elapsed_ms = to_ms_since_boot(get_absolute_time());
        wanted = ((elapsed_ms / STATUS_LED_BLINK_INTERVAL_MS) & 1u) != 0;
    }

    if (wanted == led_state) return;
    led_state = wanted;
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, wanted);
}

static void service_usb_keyboard(void) {
    if (!tud_mounted() || !tud_hid_ready()) return;

    keyboard_report_t report;
    if (!queue_try_remove(&report_queue, &report)) return;

    static const uint8_t released[8] = {0};
    if (tud_suspended() && memcmp(report.bytes, released, 8) != 0) {
        tud_remote_wakeup();
    }
    tud_hid_report(0, report.bytes, sizeof(report.bytes));
}

int main(void) {
    stdio_init_all();
    queue_init(&report_queue, sizeof(keyboard_report_t), 16);

    if (cyw43_arch_init() != PICO_OK) {
        panic("CYW43439 initialization failed");
    }
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, false);

    tusb_init();
    btstack_main(0, NULL);

    while (true) {
        tud_task();
        service_usb_keyboard();
        service_status_led();
        tight_loop_contents();
    }
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)requested_length;
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t buffer_size) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)buffer_size;
}
