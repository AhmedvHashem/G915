#include "usb_host_keyboard.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "hardware/sync.h"
#include "hardware/timer.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pio_usb.h"
#include "tusb.h"
#include "class/hid/hid.h"
#include "host/hcd.h"

#include "app_config.h"
#include "boot_keyboard_host.h"
#include "sof_phase_lock.h"

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
    // When core 1 observed this report, for the delivery-latency measurement.
    uint32_t timestamp_us;
    uint16_t vid;
    uint16_t pid;
    uint8_t protocol;
} source_event_t;

static queue_t source_events;
static uint8_t keyboard_dev_addr;
static uint8_t keyboard_itf_num;
static uint32_t source_epoch;
static keyboard_state_t last_host_state;
static bool last_host_state_valid;
static usb_host_keyboard_status_t host_status = USB_HOST_KEYBOARD_WAITING;
static usb_host_keyboard_diagnostics_t diagnostics;

// Everything above is written only by core 0 from the source-event queue.
// The values below are written only by core 1, on the PIO USB host loop and
// its callbacks, and read by core 0 in usb_host_keyboard_get_diagnostics().
// Aligned word accesses cannot tear, and a lost update would only skew a
// statistic, so these stay off the event queue and out of the report path.
static volatile bool host_device_configured;
static volatile uint32_t min_report_frame_gap;
static uint32_t last_report_frame;
static bool last_report_frame_valid;

static uint32_t observe_report_frame(void) {
    const uint32_t now_us = time_us_32();
    const uint32_t frame = pio_usb_host_get_frame_number();
    if (last_report_frame_valid) {
        // The receiver NAKs frames with no key change, so consecutive reports
        // are normally far apart. The smallest gap ever seen is the effective
        // polling interval of the endpoint.
        const uint32_t gap = frame - last_report_frame;
        if (gap > 0 &&
            (min_report_frame_gap == 0 || gap < min_report_frame_gap)) {
            min_report_frame_gap = gap;
        }
    }
    last_report_frame = frame;
    last_report_frame_valid = true;
    return now_us;
}

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
    keyboard_itf_num = 0;
    source_epoch = 0;
    keyboard_state_clear(&last_host_state);
    last_host_state_valid = false;
    host_status = USB_HOST_KEYBOARD_WAITING;
    diagnostics = (usb_host_keyboard_diagnostics_t){
        .status = USB_HOST_KEYBOARD_WAITING,
    };
    host_device_configured = false;
    min_report_frame_gap = 0;
    last_report_frame = 0;
    last_report_frame_valid = false;
}

void usb_host_keyboard_core1(void) {
    pio_usb_configuration_t pio_config = PIO_USB_DEFAULT_CONFIG;
    pio_config.pin_dp = APP_USB_HOST_DP_PIN;
    pio_config.pinout = PIO_USB_PINOUT_DPDM;
    // The 1 ms frame is generated by sof_phase_lock from a hardware alarm on
    // this core instead of Pico-PIO-USB's own repeating timer, so its phase
    // can be steered against the console's SOF.
    pio_config.skip_alarm_pool = true;
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
    sof_phase_lock_start_frames();

    while (true) {
        tuh_task();
        if (host_device_configured) {
            // Every host event originates in an interrupt on this core: the
            // frame alarm posts completions at the end of each frame, and
            // core 0's queue operations signal an event too. Waiting for an
            // event therefore resumes tuh_task() exactly when there is work.
            // An exception return also sets the event register, so an
            // interrupt landing between tuh_task() and this wait is not lost.
            // sleep_us() would instead register an alarm on the default alarm
            // pool, whose interrupt is serviced by core 0, and interrupt the
            // console-facing core tens of thousands of times a second.
            __wfe();
        } else {
            // Enumeration pacing. The G915 receiver is a multi-interface
            // full-speed device that restarts enumeration when it sees
            // back-to-back control transactions, so keep a real gap between
            // host-task passes until every interface is configured. A busy
            // wait gives the same gap without the alarm-pool interrupt.
            busy_wait_us(100);
        }
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
                report_pipe_publish_at(pipe, &event.state, event.epoch,
                                       event.timestamp_us);
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
    result->declared_poll_interval = boot_keyboard_host_declared_interval();
    result->poll_interval = boot_keyboard_host_poll_interval();
    result->min_report_frame_gap = min_report_frame_gap;
    result->report_arm_failure_count = boot_keyboard_host_arm_failure_count();
}

//--------------------------------------------------------------------+
// TinyUSB host stack callbacks (core 1)
//--------------------------------------------------------------------+

void tuh_event_hook_cb(uint8_t rhport, uint32_t event_id, bool in_isr) {
    (void)in_isr;
    if (rhport != APP_USB_HOST_RHPORT) return;

    if (event_id == HCD_EVENT_DEVICE_ATTACH) {
        // A fresh enumeration is about to start, so pace core 1 conservatively
        // again and hold the frame period at nominal until the device reports
        // itself fully configured.
        host_device_configured = false;
        sof_phase_lock_set_steering(false);
        queue_status(SOURCE_EVENT_ATTACHED);
    } else if (event_id == HCD_EVENT_DEVICE_REMOVE) {
        host_device_configured = false;
        sof_phase_lock_set_steering(false);
        // The driver's unmount callback performs the keyboard-state reset when
        // a keyboard had reached READY. This event also covers removal during
        // enumeration, before the driver has mounted anything.
        if (keyboard_dev_addr == 0) {
            queue_status(SOURCE_EVENT_WAITING);
        }
    }
}

void tuh_mount_cb(uint8_t dev_addr) {
    host_device_configured = true;
    sof_phase_lock_set_steering(true);
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
    host_device_configured = false;
    sof_phase_lock_set_steering(false);
    if (keyboard_dev_addr == 0) queue_status(SOURCE_EVENT_WAITING);
}

//--------------------------------------------------------------------+
// Boot-keyboard host class driver callbacks (core 1)
//--------------------------------------------------------------------+

void boot_keyboard_host_interface_cb(uint8_t dev_addr, uint8_t itf_num,
                                     uint8_t protocol, bool claimed) {
    (void)itf_num;
    (void)claimed;
    uint16_t vid = 0;
    uint16_t pid = 0;
    (void)tuh_vid_pid_get(dev_addr, &vid, &pid);
    const source_event_t event = {
        .kind = SOURCE_EVENT_HID_INTERFACE,
        .epoch = source_epoch,
        .vid = vid,
        .pid = pid,
        .protocol = protocol,
    };
    queue_event(&event);
}

void boot_keyboard_host_mount_cb(uint8_t dev_addr, uint8_t itf_num) {
    keyboard_dev_addr = dev_addr;
    keyboard_itf_num = itf_num;
    last_report_frame_valid = false;
    reset_source(SOURCE_EVENT_READY);

    // The driver armed the endpoint before this callback. Logging comes last:
    // stdio over UART blocks until the FIFO drains, which would otherwise
    // stall core 1 for milliseconds inside the enumeration window this
    // receiver is most sensitive to.
    uint16_t vid = 0;
    uint16_t pid = 0;
    (void)tuh_vid_pid_get(dev_addr, &vid, &pid);
    printf("USB keyboard mounted: %04x:%04x addr %u interface %u\n",
           vid, pid, dev_addr, itf_num);
}

void boot_keyboard_host_umount_cb(uint8_t dev_addr, uint8_t itf_num) {
    if (dev_addr != keyboard_dev_addr || itf_num != keyboard_itf_num) return;

    keyboard_dev_addr = 0;
    keyboard_itf_num = 0;
    last_report_frame_valid = false;
    reset_source(SOURCE_EVENT_WAITING);
    printf("USB keyboard removed\n");
}

void boot_keyboard_host_report_cb(uint8_t dev_addr, uint8_t itf_num,
                                  const uint8_t *report, uint16_t length) {
    if (dev_addr != keyboard_dev_addr || itf_num != keyboard_itf_num) return;

    const uint32_t received_us = observe_report_frame();

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
            .timestamp_us = received_us,
        };
        queue_event(&event);
    }
    // No logging on this path: stdio over UART blocks for milliseconds. The
    // driver re-arms the endpoint as soon as this callback returns.
}
