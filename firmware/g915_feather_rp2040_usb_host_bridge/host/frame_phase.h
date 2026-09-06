#pragma once

#include <stdbool.h>
#include <stdint.h>

// Pure timing logic for aligning the locally generated USB host frame with the
// USB frame of another bus. No hardware access: every time value is a 32-bit
// microsecond counter that is allowed to wrap.
//
// The bridge polls the receiver once per frame of its own PIO USB host, and
// the console polls the bridge once per frame of the console's bus. The two
// frame clocks are unrelated, so without alignment a report waits anywhere
// from zero to a full frame for the console's next poll. Placing the local
// frame a fixed lead ahead of the console's SOF makes that wait short and
// predictable.

//--------------------------------------------------------------------+
// SOF tracker
//--------------------------------------------------------------------+

// Estimates the instant of the most recent console SOF from timestamps taken
// when each SOF interrupt is observed. Observation is always late by the
// interrupt latency, so the tracker snaps to earlier arrivals and follows later
// ones only slowly: the earliest observations lie closest to the truth, while
// consistent lateness reveals a period mismatch that must be tracked.
typedef struct {
    uint32_t period_us;
    bool valid;
    uint32_t model_us;      // Estimate of the most recent SOF instant.
    uint16_t last_frame;    // 11-bit USB frame number carried by that SOF.
    uint32_t observations;
} frame_phase_tracker_t;

void frame_phase_tracker_init(frame_phase_tracker_t *tracker,
                              uint32_t period_us);
void frame_phase_tracker_reset(frame_phase_tracker_t *tracker);
// frame_number is the 11-bit frame number carried by the SOF packet.
void frame_phase_tracker_observe(frame_phase_tracker_t *tracker,
                                 uint32_t observed_us, uint16_t frame_number);
// Returns false until at least one SOF has been observed.
bool frame_phase_tracker_reference(const frame_phase_tracker_t *tracker,
                                   uint32_t *reference_us);

//--------------------------------------------------------------------+
// Frame controller
//--------------------------------------------------------------------+

typedef struct {
    uint32_t period_us;             // Nominal frame period, 1000 for USB.
    uint32_t lead_us;               // Local frame start precedes the reference by this much.
    uint32_t max_step_us;           // Largest change to one frame period.
    uint32_t lock_tolerance_us;     // |error| at or below this counts as locked.
    uint32_t reference_max_age_us;  // Older references are ignored.
} frame_phase_config_t;

typedef struct {
    frame_phase_config_t config;
    int32_t last_error_us;
    bool locked;
    uint32_t adjustment_count;
} frame_phase_controller_t;

void frame_phase_controller_init(frame_phase_controller_t *controller,
                                 const frame_phase_config_t *config);

// Returns the start time of the frame that follows one started at
// frame_start_us. When the reference is unavailable or older than
// reference_max_age_us the result is exactly one nominal period later.
// Otherwise the period is shortened or lengthened by at most max_step_us to
// move the frame start toward lead_us before the reference instant, always in
// the direction that needs the smaller total change.
uint32_t frame_phase_controller_next_start(frame_phase_controller_t *controller,
                                           uint32_t frame_start_us,
                                           bool reference_valid,
                                           uint32_t reference_us,
                                           uint32_t now_us);
