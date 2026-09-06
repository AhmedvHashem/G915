#pragma once

#include <stdint.h>

#include "core/report_pipe.h"

// Delivery timing for reports sent to the USB host.
//
// The source stamps each state change with the time it observed it. This
// adapter records when it armed the report on the interrupt endpoint and when
// the host acknowledged it, and reads the host's 11-bit USB frame number at
// both points. Items the pipe synthesises carry no source stamp and contribute
// only to the frame counts.
#define USB_KEYBOARD_LATENCY_BUCKET_US    100u
// The last bucket collects everything at or above its lower bound.
#define USB_KEYBOARD_LATENCY_BUCKET_COUNT 32u

typedef struct {
    // Source stamp to host acknowledgement.
    uint32_t count;
    uint32_t min_us;
    uint32_t max_us;
    uint64_t total_us;
    uint32_t histogram[USB_KEYBOARD_LATENCY_BUCKET_COUNT];
    // The same interval split at the moment the endpoint was armed. Before
    // that moment is the hop from the source into this adapter; after it is
    // the wait for the host's next poll of the endpoint.
    uint64_t arm_total_us;
    uint32_t arm_max_us;
    uint64_t poll_total_us;
    uint32_t poll_max_us;
    // Host USB frames elapsed between arming and acknowledgement, for every
    // report: index 0 is the same frame, index 3 is three or more.
    uint32_t frames_waited[4];
} usb_keyboard_latency_t;

typedef struct {
    uint32_t source_us;       // 0 for synthesised items.
    uint32_t armed_us;
    uint32_t completed_us;
    uint16_t armed_frame;     // Host frame number when the endpoint was armed.
    uint16_t completed_frame;
} usb_keyboard_delivery_t;

// Invoked from the USB task context for every acknowledged report.
typedef void (*usb_keyboard_delivery_observer_t)(
    const usb_keyboard_delivery_t *delivery, void *context);

void usb_keyboard_init(report_pipe_t *pipe);
void usb_keyboard_task(void);
uint8_t usb_keyboard_led_state(void);
void usb_keyboard_get_latency(usb_keyboard_latency_t *result);
void usb_keyboard_reset_latency(void);
void usb_keyboard_set_delivery_observer(
    usb_keyboard_delivery_observer_t observer, void *context);
