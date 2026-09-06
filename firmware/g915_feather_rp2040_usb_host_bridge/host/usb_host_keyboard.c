#include "usb_host_keyboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pio_usb.h"
#include "tusb.h"
#include "host/hcd.h"

#include "app_config.h"

#define SOURCE_EVENT_QUEUE_DEPTH 16u

_Static_assert(APP_USB_HOST_DM_PIN == APP_USB_HOST_DP_PIN + 1u,
               "Pico-PIO-USB DPDM pinout requires D- immediately after D+");

typedef enum {
    SOURCE_EVENT_RESET = 0,
    SOURCE_EVENT_STATE,
    SOURCE_EVENT_READY,
    SOURCE_EVENT_WAITING,
    SOURCE_EVENT_ATTACHED,
    SOURCE_EVENT_CONFIGURED,
    SOURCE_EVENT_HID_INTERFACE,
} source_event_kind_t;

typedef struct {
    source_event_kind_t kind;
    keyboard_state_t state;
    uint32_t epoch;
    uint16_t vid;
    uint16_t pid;
    uint8_t protocol;
} source_event_t;

static queue_t source_events;
static uint8_t keyboard_dev_addr;
static uint8_t keyboard_instance;
static uint32_t source_epoch;
static keyboard_state_t last_host_state;
static bool last_host_state_valid;
static usb_host_keyboard_status_t host_status = USB_HOST_KEYBOARD_WAITING;
static usb_host_keyboard_diagnostics_t diagnostics;

static void queue_event(const source_event_t *event) {
    if (queue_try_add(&source_events, event)) return;

    // Reports contain the complete keyboard state. If core 0 was briefly busy,
    // discard the oldest snapshot and keep the newest one rather than blocking
    // the time-sensitive PIO USB host loop.
    source_event_t discarded;
    (void)queue_try_remove(&source_events, &discarded);
    (void)queue_try_add(&source_events, event);
}

static void queue_status(source_event_kind_t kind) {
    const source_event_t event = {
        .kind = kind,
        .epoch = source_epoch,
    };
    queue_event(&event);
}

static void reset_source(source_event_kind_t status) {
    ++source_epoch;
    keyboard_state_clear(&last_host_state);
    last_host_state_valid = false;

    const source_event_t reset = {
        .kind = SOURCE_EVENT_RESET,
        .epoch = source_epoch,
    };
    queue_event(&reset);
    queue_status(status);
}

static bool decode_boot_report(const uint8_t *report, uint16_t length,
                               keyboard_state_t *state) {
    if (report == NULL || length != sizeof(hid_keyboard_report_t)) return false;

    const hid_keyboard_report_t *keyboard =
        (const hid_keyboard_report_t *)report;
    keyboard_state_clear(state);
    state->modifiers = keyboard->modifier;

    for (size_t i = 0; i < sizeof(keyboard->keycode); ++i) {
        (void)keyboard_state_set_usage(state, keyboard->keycode[i], true);
    }
    keyboard_state_filter_for_boot_report(
        state, APP_USB_KEY_USAGE_MAX, APP_USB_FORWARD_SOURCE_ERRORS != 0);
    return true;
}

void usb_host_keyboard_init(void) {
    queue_init(&source_events, sizeof(source_event_t), SOURCE_EVENT_QUEUE_DEPTH);
    keyboard_dev_addr = 0;
    keyboard_instance = 0;
    source_epoch = 0;
    keyboard_state_clear(&last_host_state);
    last_host_state_valid = false;
    host_status = USB_HOST_KEYBOARD_WAITING;
    diagnostics = (usb_host_keyboard_diagnostics_t){
        .status = USB_HOST_KEYBOARD_WAITING,
    };
}

void usb_host_keyboard_core1(void) {
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pin_dp = APP_USB_HOST_DP_PIN;
    pio_config.pinout = PIO_USB_PINOUT_DPDM;
    if (!tuh_configure(APP_USB_HOST_RHPORT,
                       TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_config)) {
        panic("PIO USB host configuration failed");
    }

    const tusb_rhport_init_t host_init = {
        .role = TUSB_ROLE_HOST,
        .speed = TUSB_SPEED_AUTO,
    };
    if (!tusb_init(APP_USB_HOST_RHPORT, &host_init)) {
        panic("TinyUSB host initialization failed");
    }

    while (true) {
        tuh_task();
        // The G915 receiver is a multi-interface full-speed device. Leaving a
        // short interval between host-task passes avoids back-to-back control
        // transactions that make this receiver restart enumeration, while
        // remaining far below its 1 ms interrupt polling interval.
        sleep_us(100);
        tight_loop_contents();
    }
}

void usb_host_keyboard_task(report_pipe_t *pipe) {
    source_event_t event;
    while (queue_try_remove(&source_events, &event)) {
        switch (event.kind) {
            case SOURCE_EVENT_RESET:
                report_pipe_source_reset(pipe, event.epoch);
                break;
            case SOURCE_EVENT_STATE:
                report_pipe_publish(pipe, &event.state, event.epoch);
                break;
            case SOURCE_EVENT_READY:
                host_status = USB_HOST_KEYBOARD_READY;
                diagnostics.status = USB_HOST_KEYBOARD_READY;
                break;
            case SOURCE_EVENT_WAITING:
                host_status = USB_HOST_KEYBOARD_WAITING;
                diagnostics = (usb_host_keyboard_diagnostics_t){
                    .status = USB_HOST_KEYBOARD_WAITING,
                };
                break;
            case SOURCE_EVENT_ATTACHED:
                host_status = USB_HOST_KEYBOARD_ATTACHED;
                diagnostics = (usb_host_keyboard_diagnostics_t){
                    .status = USB_HOST_KEYBOARD_ATTACHED,
                };
                break;
            case SOURCE_EVENT_CONFIGURED:
                if (host_status != USB_HOST_KEYBOARD_READY) {
                    host_status = USB_HOST_KEYBOARD_CONFIGURED;
                    diagnostics.status = USB_HOST_KEYBOARD_CONFIGURED;
                }
                diagnostics.vid = event.vid;
                diagnostics.pid = event.pid;
                break;
            case SOURCE_EVENT_HID_INTERFACE:
                diagnostics.vid = event.vid;
                diagnostics.pid = event.pid;
                ++diagnostics.hid_interface_count;
                if (event.protocol == HID_ITF_PROTOCOL_KEYBOARD) {
                    ++diagnostics.keyboard_interface_count;
                }
                break;
        }
    }
}

usb_host_keyboard_status_t usb_host_keyboard_status(void) {
    return host_status;
}

void usb_host_keyboard_get_diagnostics(usb_host_keyboard_diagnostics_t *result) {
    *result = diagnostics;
}

void tuh_event_hook_cb(uint8_t rhport, uint32_t event_id, bool in_isr) {
    (void)in_isr;
    if (rhport != APP_USB_HOST_RHPORT) return;

    if (event_id == HCD_EVENT_DEVICE_ATTACH) {
        queue_status(SOURCE_EVENT_ATTACHED);
    } else if (event_id == HCD_EVENT_DEVICE_REMOVE) {
        // The HID unmount callback performs the keyboard-state reset when a
        // keyboard had reached READY. This event also covers removal during
        // enumeration, before any class callback exists.
        if (keyboard_dev_addr == 0) {
            queue_status(SOURCE_EVENT_WAITING);
        }
    }
}

void tuh_mount_cb(uint8_t dev_addr) {
    uint16_t vid = 0;
    uint16_t pid = 0;
    (void)tuh_vid_pid_get(dev_addr, &vid, &pid);
    const source_event_t event = {
        .kind = SOURCE_EVENT_CONFIGURED,
        .epoch = source_epoch,
        .vid = vid,
        .pid = pid,
    };
    queue_event(&event);
}

void tuh_umount_cb(uint8_t dev_addr) {
    (void)dev_addr;
    if (keyboard_dev_addr == 0) queue_status(SOURCE_EVENT_WAITING);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance,
                      const uint8_t *report_descriptor,
                      uint16_t descriptor_length) {
    (void)report_descriptor;
    (void)descriptor_length;

    const uint8_t protocol = tuh_hid_interface_protocol(dev_addr, instance);
    uint16_t vid = 0;
    uint16_t pid = 0;
    (void)tuh_vid_pid_get(dev_addr, &vid, &pid);
    const source_event_t hid_event = {
        .kind = SOURCE_EVENT_HID_INTERFACE,
        .epoch = source_epoch,
        .vid = vid,
        .pid = pid,
        .protocol = protocol,
    };
    queue_event(&hid_event);
    if (protocol != HID_ITF_PROTOCOL_KEYBOARD || keyboard_dev_addr != 0) return;

    keyboard_dev_addr = dev_addr;
    keyboard_instance = instance;
    reset_source(SOURCE_EVENT_READY);

    printf("USB keyboard mounted: %04x:%04x addr %u interface %u\n",
           vid, pid, dev_addr, instance);

    if (!tuh_hid_receive_report(dev_addr, instance)) {
        printf("Could not queue first keyboard report\n");
    }
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    if (dev_addr != keyboard_dev_addr || instance != keyboard_instance) return;

    keyboard_dev_addr = 0;
    keyboard_instance = 0;
    reset_source(SOURCE_EVENT_WAITING);
    printf("USB keyboard removed\n");
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance,
                                const uint8_t *report, uint16_t length) {
    if (dev_addr != keyboard_dev_addr || instance != keyboard_instance) return;

    keyboard_state_t state;
    if (decode_boot_report(report, length, &state) &&
        (!last_host_state_valid ||
         !keyboard_state_equal(&last_host_state, &state))) {
        last_host_state = state;
        last_host_state_valid = true;
        const source_event_t event = {
            .kind = SOURCE_EVENT_STATE,
            .state = state,
            .epoch = source_epoch,
        };
        queue_event(&event);
    }

    if (!tuh_hid_receive_report(dev_addr, instance)) {
        printf("Could not requeue keyboard report\n");
    }
}
