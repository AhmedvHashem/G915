#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "host/frame_phase.h"

#define PERIOD 1000u

static frame_phase_config_t config_with_lead(uint32_t lead_us) {
    frame_phase_config_t config = {
        .period_us = PERIOD,
        .lead_us = lead_us,
        .max_step_us = 2,
        .lock_tolerance_us = 5,
        .reference_max_age_us = 20000,
    };
    return config;
}

static void test_controller_without_reference_is_nominal(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(250);
    frame_phase_controller_init(&controller, &config);

    assert(frame_phase_controller_next_start(&controller, 5000, false, 0,
                                             5000) == 6000);
    assert(!controller.locked);
    assert(controller.adjustment_count == 0);
}

static void test_controller_stale_reference_is_nominal(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(250);
    frame_phase_controller_init(&controller, &config);

    // Reference is 30 ms old at the time of the decision.
    assert(frame_phase_controller_next_start(&controller, 100000, true, 70000,
                                             100000) == 101000);
    assert(!controller.locked);
}

static void test_controller_aligned_reference_needs_no_step(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(250);
    frame_phase_controller_init(&controller, &config);

    // Frames start at 10000, 11000, ... The reference grid is 250 us after
    // each start: 10250, 11250, ... Any grid instant must give error 0.
    assert(frame_phase_controller_next_start(&controller, 10000, true, 10250,
                                             10990) == 11000);
    assert(controller.last_error_us == 0);
    assert(controller.locked);
    assert(frame_phase_controller_next_start(&controller, 11000, true, 7250,
                                             11990) == 12000);
    assert(controller.last_error_us == 0);
    assert(controller.adjustment_count == 0);
}

static void test_controller_step_is_clamped_and_converges(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(250);
    frame_phase_controller_init(&controller, &config);

    // Reference grid at 10550 + k*1000, so the desired starts are 10300 +
    // k*1000: our frames must move 300 us later.
    uint32_t start = 10000;
    uint32_t next = frame_phase_controller_next_start(&controller, start, true,
                                                      10550, start + 990);
    assert(next == start + PERIOD + 2);
    assert(controller.last_error_us == 300);
    assert(!controller.locked);

    // Keep the reference fresh on its own grid while we converge.
    uint32_t reference = 10550;
    int frames = 1;
    start = next;
    while (!controller.locked && frames < 1000) {
        reference += PERIOD;
        next = frame_phase_controller_next_start(&controller, start, true,
                                                 reference, start + 990);
        assert((int32_t)(next - start) >= (int32_t)PERIOD - 2);
        assert((int32_t)(next - start) <= (int32_t)PERIOD + 2);
        start = next;
        ++frames;
    }
    assert(controller.locked);
    // The error shrinks by 2 us per frame from 300. Lock is declared on the
    // frame that measures 4 us, the 148th correction, after the first one
    // above: 149 frames in total.
    assert(frames == 149);
    assert(controller.last_error_us == 4);

    // Once locked the phase is exactly held: frame start == reference - lead.
    reference += PERIOD;
    next = frame_phase_controller_next_start(&controller, start, true,
                                             reference, start + 990);
    assert(next == reference + PERIOD - 250);
}

static void test_controller_takes_shorter_direction(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(0);
    frame_phase_controller_init(&controller, &config);

    // Desired start 20990 versus nominal 21000: 10 us early is closer than
    // 990 us late.
    frame_phase_controller_next_start(&controller, 20000, true, 20990, 20995);
    assert(controller.last_error_us == -10);

    // Desired start 21010 versus nominal 21000: 10 us late.
    frame_phase_controller_next_start(&controller, 20000, true, 21010 - 1000,
                                      20995);
    assert(controller.last_error_us == 10);

    // Exactly half a period away resolves to the negative side, never both.
    // The reference must lie in the past of the decision time to be used.
    frame_phase_controller_next_start(&controller, 20000, true, 21500, 21505);
    assert(controller.last_error_us == -500);
}

static void test_controller_survives_counter_wrap(void) {
    frame_phase_controller_t controller;
    const frame_phase_config_t config = config_with_lead(250);
    frame_phase_controller_init(&controller, &config);

    const uint32_t start = 0xfffffe00u;  // 512 us before the wrap.
    const uint32_t nominal = start + PERIOD;  // Wraps to 0x1e8.
    const uint32_t reference = nominal + 250 + 6;  // 6 us late.
    const uint32_t next = frame_phase_controller_next_start(
        &controller, start, true, reference, reference + 44);
    assert(controller.last_error_us == 6);
    assert(next == nominal + 2);
}

static void test_tracker_snaps_early_and_follows_slowly(void) {
    frame_phase_tracker_t tracker;
    frame_phase_tracker_init(&tracker, PERIOD);
    uint32_t reference;
    assert(!frame_phase_tracker_reference(&tracker, &reference));

    // True SOF instants at 5000 + k*1000, observed with latencies that vary
    // between 8 and 30 us. With matching periods the estimate must settle
    // within the minimum latency of the truth and never earlier than it.
    const uint32_t latencies[] = {30, 25, 8, 20, 12, 9, 28, 8, 15, 10};
    uint32_t truth = 5000;
    for (uint16_t frame = 0; frame < 200; ++frame, truth += PERIOD) {
        const uint32_t latency =
            latencies[frame % (sizeof(latencies) / sizeof(latencies[0]))];
        frame_phase_tracker_observe(&tracker, truth + latency, frame);
    }
    assert(frame_phase_tracker_reference(&tracker, &reference));
    const int32_t model_error = (int32_t)(reference - (truth - PERIOD));
    assert(model_error >= 0);
    assert(model_error <= 10);
    assert(tracker.observations == 200);
}

static void test_tracker_follows_period_mismatch(void) {
    frame_phase_tracker_t tracker;
    frame_phase_tracker_init(&tracker, PERIOD);

    // The remote frame is 0.5 us long per 1000: 500 ppm, far worse than any
    // real crystal. Constant 10 us latency. Use a fixed-point truth in ns.
    uint64_t truth_ns = 5000000;
    uint32_t last_truth_us = 0;
    for (uint16_t frame = 0; frame < 4000; ++frame, truth_ns += 1000500) {
        last_truth_us = (uint32_t)(truth_ns / 1000);
        frame_phase_tracker_observe(&tracker, last_truth_us + 10,
                                    (uint16_t)(frame & 0x7ff));
    }
    uint32_t reference;
    assert(frame_phase_tracker_reference(&tracker, &reference));
    const int32_t model_error = (int32_t)(reference - last_truth_us);
    // The 2 ms of drift over the run must not accumulate. The slow pull only
    // acts once the residual reaches 16 us, so with the remote period longer
    // than nominal the estimate settles a few microseconds ahead of the truth
    // rather than the latency behind it; that is the conservative direction
    // for the frame lead.
    assert(model_error >= -16);
    assert(model_error <= 30);
}

static void test_tracker_resynchronises_after_gap(void) {
    frame_phase_tracker_t tracker;
    frame_phase_tracker_init(&tracker, PERIOD);

    frame_phase_tracker_observe(&tracker, 1000, 0);
    frame_phase_tracker_observe(&tracker, 2010, 1);
    // The bus was suspended; frames resume 3 seconds later with a new phase.
    frame_phase_tracker_observe(&tracker, 3002400, 2);
    uint32_t reference;
    assert(frame_phase_tracker_reference(&tracker, &reference));
    assert(reference == 3002400);

    // Missing observations are bridged by the frame number.
    frame_phase_tracker_observe(&tracker, 3005405, 5);
    assert(frame_phase_tracker_reference(&tracker, &reference));
    assert(reference == 3005400 + 5 / 16);
}

int main(void) {
    test_controller_without_reference_is_nominal();
    test_controller_stale_reference_is_nominal();
    test_controller_aligned_reference_needs_no_step();
    test_controller_step_is_clamped_and_converges();
    test_controller_takes_shorter_direction();
    test_controller_survives_counter_wrap();
    test_tracker_snaps_early_and_follows_slowly();
    test_tracker_follows_period_mismatch();
    test_tracker_resynchronises_after_gap();
    puts("frame phase tests passed");
    return 0;
}
