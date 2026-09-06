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
    // Smallest frame gap ever observed between two keyboard reports, which is
    // the interrupt polling interval of the receiver in milliseconds. Zero
    // until at least two reports have arrived.
    uint32_t min_report_frame_gap;
    // Reports that could not be re-armed on the receiver IN endpoint.
    uint32_t report_arm_failure_count;
} usb_host_keyboard_diagnostics_t;

void usb_host_keyboard_init(void);
void usb_host_keyboard_core1(void);
void usb_host_keyboard_task(report_pipe_t *pipe);
usb_host_keyboard_status_t usb_host_keyboard_status(void);
void usb_host_keyboard_get_diagnostics(usb_host_keyboard_diagnostics_t *result);
