#include "sof_phase_lock.h"

#include <stdbool.h>
#include <stdint.h>

#include "hardware/timer.h"
#include "pico/platform.h"
#include "pico/time.h"
#include "pio_usb.h"

#include "app_config.h"
#include "frame_phase.h"

#define USB_FRAME_PERIOD_US 1000u

// Written by core 0 in the USB device interrupt, read by core 1 in the frame
// alarm interrupt. The tracker itself is touched only by core 0.
static frame_phase_tracker_t console_sof;
static volatile uint32_t console_reference_us;
static volatile bool console_reference_valid;

// Core 1 only, except steering_enabled, which core 1 both reads and writes
// from task and interrupt context.
static frame_phase_controller_t controller;
static int frame_alarm = -1;
static uint64_t next_frame_time_us;
static volatile bool steering_enabled;
static volatile uint32_t missed_frames;

static void __not_in_flash_func(frame_alarm_handler)(uint alarm_num) {
    (void)alarm_num;
    // The frame handler sends SOF, runs the receiver poll, and posts any
    // completion to TinyUSB before returning, so nothing below delays it.
    pio_usb_host_frame();

    const uint32_t frame_start = (uint32_t)next_frame_time_us;
    uint32_t next_start;
    if (steering_enabled) {
        next_start = frame_phase_controller_next_start(
            &controller, frame_start, console_reference_valid,
            console_reference_us, time_us_32());
    } else {
        controller.locked = false;
        next_start = frame_start + USB_FRAME_PERIOD_US;
    }
    // The controller works on 32-bit times; carry its signed change into the
    // 64-bit schedule.
    next_frame_time_us +=
        (uint64_t)(int64_t)(int32_t)(next_start - frame_start);

    // A target already in the past is not armed. That can only happen if this
    // handler overran a whole frame, so skip ahead rather than stall the bus.
    while (hardware_alarm_set_target((uint)frame_alarm,
                                     from_us_since_boot(next_frame_time_us))) {
        next_frame_time_us += USB_FRAME_PERIOD_US;
        ++missed_frames;
    }
}

void sof_phase_lock_start_frames(void) {
    const frame_phase_config_t config = {
        .period_us = USB_FRAME_PERIOD_US,
        .lead_us = APP_SOF_PHASE_LEAD_US,
        .max_step_us = APP_SOF_PHASE_MAX_STEP_US,
        .lock_tolerance_us = APP_SOF_PHASE_LOCK_TOLERANCE_US,
        .reference_max_age_us = APP_SOF_PHASE_REFERENCE_MAX_AGE_US,
    };
    frame_phase_controller_init(&controller, &config);
    frame_phase_tracker_init(&console_sof, USB_FRAME_PERIOD_US);
    console_reference_valid = false;
    steering_enabled = false;
    missed_frames = 0;

    // Binds the alarm interrupt to the calling core, which must be core 1 so
    // the frame handler shares a core with the rest of the PIO USB host.
    frame_alarm = hardware_alarm_claim_unused(true);
    hardware_alarm_set_callback((uint)frame_alarm, frame_alarm_handler);
    next_frame_time_us = time_us_64() + USB_FRAME_PERIOD_US;
    while (hardware_alarm_set_target((uint)frame_alarm,
                                     from_us_since_boot(next_frame_time_us))) {
        next_frame_time_us += USB_FRAME_PERIOD_US;
    }
}

void sof_phase_lock_set_steering(bool enabled) {
    steering_enabled = enabled && APP_SOF_PHASE_LEAD_US > 0u;
}

void __not_in_flash_func(sof_phase_lock_observe_console_sof)(
    uint32_t observed_us, uint16_t frame_number) {
    frame_phase_tracker_observe(&console_sof, observed_us, frame_number);
    uint32_t reference;
    if (frame_phase_tracker_reference(&console_sof, &reference)) {
        console_reference_us = reference;
        console_reference_valid = true;
    }
}

void sof_phase_lock_forget_console(void) {
    console_reference_valid = false;
    frame_phase_tracker_reset(&console_sof);
}

void sof_phase_lock_get_status(sof_phase_lock_status_t *status) {
    status->steering_enabled = steering_enabled;
    status->reference_valid = console_reference_valid;
    status->reference_us = console_reference_us;
    status->locked = controller.locked;
    status->error_us = controller.last_error_us;
    status->adjustment_count = controller.adjustment_count;
    status->missed_frames = missed_frames;
    status->sof_observations = console_sof.observations;
}
