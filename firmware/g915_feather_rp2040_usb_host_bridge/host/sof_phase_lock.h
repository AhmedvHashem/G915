#pragma once

#include <stdbool.h>
#include <stdint.h>

// Generates the Pico-PIO-USB host frame from a hardware alarm on core 1 and
// steers its phase so the receiver poll at the start of every frame lands a
// fixed lead ahead of the console's SOF. frame_phase.h holds the arithmetic;
// app_config.h holds the tuning constants.
//
// Core assignment: frame generation and steering run on core 1, where the PIO
// USB host lives. The console's SOF is observed on core 0 in the TinyUSB
// device interrupt. The two sides share a few aligned words that one core
// writes and the other reads. A stale read is harmless: the reference is a
// point on a 1 ms grid and steering moves at most a couple of microseconds
// per frame.

typedef struct {
    bool steering_enabled;    // Adjustments allowed: lead configured and receiver configured.
    bool reference_valid;     // A console SOF has been observed.
    uint32_t reference_us;    // Estimated instant of the latest console SOF.
    bool locked;              // |error_us| within the lock tolerance.
    int32_t error_us;         // Last phase error; positive means the frame must move later.
    uint32_t adjustment_count;
    uint32_t missed_frames;   // Alarm targets that were already in the past when set.
    uint32_t sof_observations;
} sof_phase_lock_status_t;

// Core 1. Claims a hardware alarm and starts generating frames. Call after the
// TinyUSB host root port is initialised with skip_alarm_pool set, so that
// pio_usb_host_frame() has a configured PIO to drive.
void sof_phase_lock_start_frames(void);

// Core 1. Allow or forbid phase adjustments. While forbidden the frame period
// is exactly nominal, which is how enumeration runs.
void sof_phase_lock_set_steering(bool enabled);

// Core 0, interrupt context. The console issued a SOF carrying frame_number,
// observed at observed_us.
void sof_phase_lock_observe_console_sof(uint32_t observed_us,
                                        uint16_t frame_number);

// Core 0, interrupt context. The console bus reset, suspended, or went away;
// forget its SOF grid so the next SOF starts a fresh estimate.
void sof_phase_lock_forget_console(void);

// Any core. Snapshot for diagnostics.
void sof_phase_lock_get_status(sof_phase_lock_status_t *status);
