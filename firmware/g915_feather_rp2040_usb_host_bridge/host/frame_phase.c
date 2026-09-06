#include "frame_phase.h"

#include <stdlib.h>

#define USB_FRAME_NUMBER_MASK 0x7ffu

// Late arrivals pull the model toward them by this fraction per observation.
// It is small enough that interrupt-latency jitter barely moves the estimate
// and large enough to follow any realistic crystal mismatch, which changes the
// true SOF instant by well under a microsecond per frame.
#define TRACKER_PULL_DIVISOR 16

//--------------------------------------------------------------------+
// SOF tracker
//--------------------------------------------------------------------+

void frame_phase_tracker_init(frame_phase_tracker_t *tracker,
                              uint32_t period_us) {
    tracker->period_us = period_us;
    frame_phase_tracker_reset(tracker);
}

void frame_phase_tracker_reset(frame_phase_tracker_t *tracker) {
    tracker->valid = false;
    tracker->model_us = 0;
    tracker->last_frame = 0;
    tracker->observations = 0;
}

void frame_phase_tracker_observe(frame_phase_tracker_t *tracker,
                                 uint32_t observed_us, uint16_t frame_number) {
    const uint16_t frame = frame_number & USB_FRAME_NUMBER_MASK;
    ++tracker->observations;

    if (!tracker->valid) {
        tracker->valid = true;
        tracker->model_us = observed_us;
        tracker->last_frame = frame;
        return;
    }

    uint32_t elapsed_frames =
        (uint32_t)(frame - tracker->last_frame) & USB_FRAME_NUMBER_MASK;
    if (elapsed_frames == 0) elapsed_frames = 1;
    tracker->last_frame = frame;

    const uint32_t predicted =
        tracker->model_us + elapsed_frames * tracker->period_us;
    const int32_t residual = (int32_t)(observed_us - predicted);

    if (residual < 0) {
        // Earlier than predicted: the truth is at least this early.
        tracker->model_us = observed_us;
    } else if (residual > (int32_t)(tracker->period_us / 2u)) {
        // Far later than any interrupt latency. The bus paused or resumed, so
        // the old grid is meaningless; restart from this observation.
        tracker->model_us = observed_us;
    } else {
        tracker->model_us =
            predicted + (uint32_t)(residual / TRACKER_PULL_DIVISOR);
    }
}

bool frame_phase_tracker_reference(const frame_phase_tracker_t *tracker,
                                   uint32_t *reference_us) {
    if (!tracker->valid) return false;
    *reference_us = tracker->model_us;
    return true;
}

//--------------------------------------------------------------------+
// Frame controller
//--------------------------------------------------------------------+

void frame_phase_controller_init(frame_phase_controller_t *controller,
                                 const frame_phase_config_t *config) {
    controller->config = *config;
    controller->last_error_us = 0;
    controller->locked = false;
    controller->adjustment_count = 0;
}

// Reduces a signed offset to the equivalent offset within one period, in
// [-period/2, period/2), so the shorter direction is always chosen.
static int32_t wrap_to_period(int32_t offset, uint32_t period_us) {
    const int32_t period = (int32_t)period_us;
    int32_t wrapped = offset % period;
    if (wrapped < -period / 2) {
        wrapped += period;
    } else if (wrapped >= period / 2) {
        wrapped -= period;
    }
    return wrapped;
}

uint32_t frame_phase_controller_next_start(frame_phase_controller_t *controller,
                                           uint32_t frame_start_us,
                                           bool reference_valid,
                                           uint32_t reference_us,
                                           uint32_t now_us) {
    const frame_phase_config_t *config = &controller->config;
    const uint32_t nominal = frame_start_us + config->period_us;

    if (!reference_valid ||
        (uint32_t)(now_us - reference_us) > config->reference_max_age_us) {
        controller->locked = false;
        return nominal;
    }

    // Any instant on the reference grid is as good as another, so the desired
    // start is compared with the nominal one modulo the period.
    const uint32_t desired = reference_us - config->lead_us;
    const int32_t error =
        wrap_to_period((int32_t)(desired - nominal), config->period_us);
    controller->last_error_us = error;
    controller->locked = (uint32_t)abs(error) <= config->lock_tolerance_us;

    int32_t step = error;
    const int32_t max_step = (int32_t)config->max_step_us;
    if (step > max_step) step = max_step;
    if (step < -max_step) step = -max_step;
    if (step != 0) ++controller->adjustment_count;

    return nominal + (uint32_t)step;
}
