#pragma once

#include <stdint.h>

#include "core/report_pipe.h"

typedef enum {
    USB_HOST_KEYBOARD_WAITING = 0,
    USB_HOST_KEYBOARD_ATTACHED,
    USB_HOST_KEYBOARD_CONFIGURED,
    USB_HOST_KEYBOARD_READY,
} usb_host_keyboard_status_t;

typedef struct {
    usb_host_keyboard_status_t status;
    uint16_t vid;
    uint16_t pid;
    uint8_t hid_interface_count;
    uint8_t keyboard_interface_count;
    // bInterval the receiver declared for its keyboard endpoint, in frames,
    // and the interval this bridge actually polls it at. Zero until claimed.
    uint8_t declared_poll_interval;
    uint8_t poll_interval;
    // Smallest frame gap ever observed between two keyboard reports. With
    // the receiver NAKing idle frames this converges on the effective
    // polling interval in frames. Zero until at least two reports arrived.
    uint32_t min_report_frame_gap;
    // Reports that could not be re-armed on the receiver IN endpoint.
    uint32_t report_arm_failure_count;
} usb_host_keyboard_diagnostics_t;

void usb_host_keyboard_init(void);
void usb_host_keyboard_core1(void);
void usb_host_keyboard_task(report_pipe_t *pipe);
usb_host_keyboard_status_t usb_host_keyboard_status(void);
void usb_host_keyboard_get_diagnostics(usb_host_keyboard_diagnostics_t *result);
