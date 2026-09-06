#include "usb_keyboard.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "app_config.h"
#include "pico/stdlib.h"
#include "tusb.h"

static report_pipe_t *keyboard_pipe;
static report_pipe_item_t pending_item;
static report_pipe_item_t in_flight_item;
static bool pending_valid;
static bool report_in_flight;
static bool usb_mounted;
static bool usb_suspended;
static bool remote_wakeup_enabled;
static bool wake_requested_this_suspend;
static uint8_t host_leds;
static uint8_t idle_rate;
static uint32_t last_report_ms;
static uint32_t suspend_started_ms;

static uint32_t now_ms(void) {
    return to_ms_since_boot(get_absolute_time());
}

static void encode_state(const keyboard_state_t *state,
                         keyboard_boot_report_t *report) {
    keyboard_state_to_boot_report(state, APP_USB_KEY_USAGE_MAX,
                                  APP_USB_FORWARD_SOURCE_ERRORS != 0, report);
}

void usb_keyboard_init(report_pipe_t *pipe) {
    keyboard_pipe = pipe;
    pending_valid = false;
    report_in_flight = false;
    usb_mounted = false;
    usb_suspended = false;
    remote_wakeup_enabled = false;
    wake_requested_this_suspend = false;
    host_leds = 0;
    idle_rate = 0;
    last_report_ms = now_ms();
    suspend_started_ms = 0;
    report_pipe_set_active(pipe, false);
}

void usb_keyboard_task(void) {
    if (keyboard_pipe == NULL) return;

    // TinyUSB clears its mounted flag on a bus reset without invoking
    // tud_umount_cb(). Detect that transition so a newly configured host gets
    // a release/current-state resynchronization instead of an old queue.
    if (usb_mounted && !tud_mounted()) {
        usb_mounted = false;
        usb_suspended = false;
        remote_wakeup_enabled = false;
        wake_requested_this_suspend = false;
        pending_valid = false;
        report_in_flight = false;
        host_leds = 0;
        idle_rate = 0;
        report_pipe_set_active(keyboard_pipe, false);
        (void)report_pipe_take_wake_request(keyboard_pipe);
    }
    if (!usb_mounted) return;

    if (usb_suspended) {
        const bool wake_delay_elapsed =
            (uint32_t)(now_ms() - suspend_started_ms) >=
            APP_USB_REMOTE_WAKE_DELAY_MS;
        if (report_pipe_wake_request_pending(keyboard_pipe) &&
            remote_wakeup_enabled && !wake_requested_this_suspend &&
            wake_delay_elapsed && tud_remote_wakeup()) {
            wake_requested_this_suspend = true;
        }
        return;
    }

    if (report_in_flight) return;
    if (!tud_hid_ready()) return;

    if (!pending_valid) {
        if (!report_pipe_pop(keyboard_pipe, &pending_item)) {
            const uint32_t idle_period_ms = (uint32_t)idle_rate * 4u;
            if (idle_period_ms == 0 ||
                (uint32_t)(now_ms() - last_report_ms) < idle_period_ms) {
                return;
            }

            const keyboard_state_t *latest =
                report_pipe_latest(keyboard_pipe);
            if (latest == NULL) return;
            pending_item.state = *latest;
            pending_item.epoch = keyboard_pipe->epoch;
            pending_item.sequence = 0;
        }
        pending_valid = true;
    }

    keyboard_boot_report_t report;
    encode_state(&pending_item.state, &report);
    if (!tud_hid_report(0, report.bytes, sizeof(report.bytes))) {
        // A reset must not be stranded in the pipe behind a report that was
        // dequeued but never accepted by TinyUSB.
        pending_valid = false;
        report_pipe_request_resync(keyboard_pipe);
        return;
    }

    in_flight_item = pending_item;
    pending_valid = false;
    report_in_flight = true;
}

uint8_t usb_keyboard_led_state(void) {
    return host_leds;
}

void tud_mount_cb(void) {
    usb_mounted = true;
    usb_suspended = false;
    remote_wakeup_enabled = false;
    wake_requested_this_suspend = false;
    pending_valid = false;
    report_in_flight = false;
    last_report_ms = now_ms();
    if (keyboard_pipe != NULL) {
        (void)report_pipe_take_wake_request(keyboard_pipe);
        report_pipe_set_active(keyboard_pipe, true);
    }
}

void tud_umount_cb(void) {
    usb_mounted = false;
    usb_suspended = false;
    remote_wakeup_enabled = false;
    wake_requested_this_suspend = false;
    pending_valid = false;
    report_in_flight = false;
    host_leds = 0;
    idle_rate = 0;
    if (keyboard_pipe != NULL) {
        report_pipe_set_active(keyboard_pipe, false);
        (void)report_pipe_take_wake_request(keyboard_pipe);
    }
}

void tud_suspend_cb(bool remote_wakeup_en) {
    usb_suspended = true;
    remote_wakeup_enabled = remote_wakeup_en;
    wake_requested_this_suspend = false;
    pending_valid = false;
    report_in_flight = false;
    suspend_started_ms = now_ms();
    if (keyboard_pipe != NULL) report_pipe_set_active(keyboard_pipe, false);
}

void tud_resume_cb(void) {
    usb_suspended = false;
    remote_wakeup_enabled = false;
    wake_requested_this_suspend = false;
    pending_valid = false;
    report_in_flight = false;
    last_report_ms = now_ms();
    if (keyboard_pipe != NULL) report_pipe_set_active(keyboard_pipe, true);
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t requested_length) {
    (void)instance;
    if (report_id != 0 || keyboard_pipe == NULL) return 0;

    if (report_type == HID_REPORT_TYPE_INPUT) {
        const keyboard_state_t *latest = report_pipe_latest(keyboard_pipe);
        if (latest == NULL) return 0;
        keyboard_boot_report_t report;
        encode_state(latest, &report);
        const uint16_t length =
            requested_length < sizeof(report.bytes)
                ? requested_length
                : (uint16_t)sizeof(report.bytes);
        memcpy(buffer, report.bytes, length);
        return length;
    }

    if (report_type == HID_REPORT_TYPE_OUTPUT && requested_length > 0) {
        buffer[0] = host_leds;
        return 1;
    }
    return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           const uint8_t *buffer, uint16_t buffer_size) {
    (void)instance;
    if (report_id == 0 && report_type == HID_REPORT_TYPE_OUTPUT &&
        buffer_size > 0) {
        host_leds = buffer[0] & 0x1fu;
    }
}

void tud_hid_set_protocol_cb(uint8_t instance, uint8_t protocol) {
    (void)instance;
    (void)protocol;
    pending_valid = false;
    if (keyboard_pipe != NULL) report_pipe_request_resync(keyboard_pipe);
}

bool tud_hid_set_idle_cb(uint8_t instance, uint8_t new_idle_rate) {
    (void)instance;
    idle_rate = new_idle_rate;
    last_report_ms = now_ms();
    return true;
}

void tud_hid_report_complete_cb(uint8_t instance, const uint8_t *report,
                                uint16_t length) {
    (void)instance;
    (void)report;
    (void)length;
    if (keyboard_pipe != NULL && report_in_flight) {
        (void)report_pipe_acknowledge(keyboard_pipe, &in_flight_item);
    }
    report_in_flight = false;
    last_report_ms = now_ms();
}

void tud_hid_report_failed_cb(uint8_t instance,
                              hid_report_type_t report_type,
                              const uint8_t *report,
                              uint16_t transferred_bytes) {
    (void)instance;
    (void)report_type;
    (void)report;
    (void)transferred_bytes;
    report_in_flight = false;
    pending_valid = false;
    if (keyboard_pipe != NULL && usb_mounted && !usb_suspended) {
        report_pipe_request_resync(keyboard_pipe);
    }
}
