#pragma once

#include <stdint.h>

#include "core/report_pipe.h"

// Delivery latency, measured from the timestamp a source stamped onto a report
// to the moment the USB host acknowledged that report. Items the pipe
// synthesises carry no timestamp and are excluded.
typedef struct {
    uint32_t min_us;
    uint32_t max_us;
    uint64_t total_us;
    uint32_t count;
} usb_keyboard_latency_t;

void usb_keyboard_init(report_pipe_t *pipe);
void usb_keyboard_task(void);
uint8_t usb_keyboard_led_state(void);
void usb_keyboard_get_latency(usb_keyboard_latency_t *result);
void usb_keyboard_reset_latency(void);

