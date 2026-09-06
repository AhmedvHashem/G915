#include <assert.h>
#include <stdio.h>

#include "core/keyboard_state.h"
#include "core/report_pipe.h"

static keyboard_state_t state_with_key(uint16_t usage) {
    keyboard_state_t state;
    keyboard_state_clear(&state);
    assert(keyboard_state_set_usage(&state, usage, true));
    return state;
}

static void test_keyboard_boot_encoding(void) {
    keyboard_state_t state;
    keyboard_state_clear(&state);
    assert(keyboard_state_empty(&state));

    assert(keyboard_state_set_usage(&state, 0xe1, true));
    assert(keyboard_state_set_usage(&state, 0x04, true));
    assert(keyboard_state_set_usage(&state, 0x05, true));

    keyboard_boot_report_t report;
    keyboard_state_to_boot_report(&state, 0x65, false, &report);
    assert(report.bytes[0] == 0x02);
    assert(report.bytes[2] == 0x04);
    assert(report.bytes[3] == 0x05);
    assert(report.bytes[4] == 0);

    assert(keyboard_state_set_usage(&state, 0x66, true));
    keyboard_state_to_boot_report(&state, 0x65, false, &report);
    assert(report.bytes[4] == 0);
    keyboard_state_to_boot_report(&state, 0x66, false, &report);
    assert(report.bytes[4] == 0x66);
}

static void test_rollover_and_source_errors(void) {
    keyboard_state_t state;
    keyboard_state_clear(&state);
    for (uint16_t usage = 0x04; usage < 0x0b; ++usage) {
        assert(keyboard_state_set_usage(&state, usage, true));
    }

    keyboard_boot_report_t report;
    keyboard_state_to_boot_report(&state, 0x65, false, &report);
    for (size_t i = 2; i < KEYBOARD_BOOT_REPORT_SIZE; ++i) {
        assert(report.bytes[i] == 0x01);
    }

    keyboard_state_clear(&state);
    assert(keyboard_state_set_usage(&state, 0x01, true));
    keyboard_state_to_boot_report(&state, 0x65, false, &report);
    assert(report.bytes[2] == 0);
    keyboard_state_to_boot_report(&state, 0x65, true, &report);
    for (size_t i = 2; i < KEYBOARD_BOOT_REPORT_SIZE; ++i) {
        assert(report.bytes[i] == 0x01);
    }
}

static void test_usb_policy_filter(void) {
    keyboard_state_t state;
    keyboard_state_clear(&state);
    assert(keyboard_state_set_usage(&state, 0x04, true));
    assert(keyboard_state_set_usage(&state, 0x66, true));
    assert(keyboard_state_set_usage(&state, 0xe1, true));
    assert(keyboard_state_set_usage(&state, 0x01, true));

    keyboard_state_filter_for_boot_report(&state, 0x65, false);
    keyboard_boot_report_t report;
    keyboard_state_to_boot_report(&state, 0xff, true, &report);
    assert(report.bytes[0] == 0x02);
    assert(report.bytes[2] == 0x04);
    assert(report.bytes[3] == 0);
}

static void test_state_merge_and_new_press(void) {
    keyboard_state_t left = state_with_key(0x04);
    keyboard_state_t right = state_with_key(0x05);
    assert(keyboard_state_has_new_press(&left, &right));
    keyboard_state_merge(&left, &right);

    keyboard_boot_report_t report;
    keyboard_state_to_boot_report(&left, 0x65, false, &report);
    assert(report.bytes[2] == 0x04);
    assert(report.bytes[3] == 0x05);
    assert(!keyboard_state_has_new_press(&left, &right));
}

static void drain(report_pipe_t *pipe) {
    report_pipe_item_t item;
    while (report_pipe_pop(pipe, &item)) {
    }
}

static void test_inactive_coalescing_and_mount_resync(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);

    keyboard_state_t pressed = state_with_key(0x04);
    keyboard_state_t released;
    keyboard_state_clear(&released);
    report_pipe_publish(&pipe, &pressed, 1);
    report_pipe_publish(&pipe, &released, 1);

    report_pipe_item_t item;
    assert(!report_pipe_pop(&pipe, &item));
    assert(report_pipe_latest(&pipe) != NULL);
    assert(keyboard_state_empty(report_pipe_latest(&pipe)));

    report_pipe_set_active(&pipe, true);
    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_empty(&item.state));
    assert(!report_pipe_pop(&pipe, &item));
}

static void test_ordering_duplicates_and_reset_barrier(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);
    report_pipe_set_active(&pipe, true);
    report_pipe_source_reset(&pipe, 4);
    drain(&pipe);

    keyboard_state_t first = state_with_key(0x04);
    keyboard_state_t second = state_with_key(0x05);
    report_pipe_publish(&pipe, &first, 4);
    report_pipe_publish(&pipe, &first, 4);
    report_pipe_publish(&pipe, &second, 4);

    report_pipe_item_t item;
    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_equal(&item.state, &first));
    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_equal(&item.state, &second));
    assert(!report_pipe_pop(&pipe, &item));

    report_pipe_publish(&pipe, &first, 4);
    report_pipe_source_reset(&pipe, 5);
    assert(report_pipe_pop(&pipe, &item));
    assert(item.epoch == 5);
    assert(keyboard_state_empty(&item.state));
    assert(!report_pipe_pop(&pipe, &item));

    report_pipe_publish(&pipe, &first, 4);
    assert(pipe.stale_report_count == 1);
    assert(!report_pipe_pop(&pipe, &item));
}

static void test_overflow_resynchronizes(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);
    report_pipe_set_active(&pipe, true);
    report_pipe_source_reset(&pipe, 1);
    drain(&pipe);

    keyboard_state_t latest;
    for (uint16_t i = 0; i <= REPORT_PIPE_CAPACITY; ++i) {
        latest = state_with_key((uint16_t)(0x04 + i));
        report_pipe_publish(&pipe, &latest, 1);
    }
    assert(pipe.overflow_count == 1);

    report_pipe_item_t item;
    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_empty(&item.state));
    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_equal(&item.state, &latest));
    assert(!report_pipe_pop(&pipe, &item));
}

static void test_wake_latch(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);

    keyboard_state_t pressed = state_with_key(0x04);
    keyboard_state_t released;
    keyboard_state_clear(&released);
    report_pipe_publish(&pipe, &pressed, 1);
    assert(report_pipe_take_wake_request(&pipe));
    assert(!report_pipe_take_wake_request(&pipe));
    report_pipe_publish(&pipe, &released, 1);
    assert(!report_pipe_take_wake_request(&pipe));
}

static void test_wake_survives_suspend_until_current_ack(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);
    report_pipe_set_active(&pipe, true);
    report_pipe_source_reset(&pipe, 1);
    drain(&pipe);

    keyboard_state_t pressed = state_with_key(0x04);
    keyboard_state_t released;
    keyboard_state_clear(&released);
    report_pipe_publish(&pipe, &pressed, 1);
    report_pipe_publish(&pipe, &released, 1);
    assert(report_pipe_wake_request_pending(&pipe));

    report_pipe_item_t item;
    assert(report_pipe_pop(&pipe, &item));
    assert(!report_pipe_acknowledge(&pipe, &item));
    assert(report_pipe_pop(&pipe, &item));
    assert(report_pipe_acknowledge(&pipe, &item));
    assert(!report_pipe_wake_request_pending(&pipe));

    report_pipe_publish(&pipe, &pressed, 1);
    assert(report_pipe_wake_request_pending(&pipe));
    report_pipe_set_active(&pipe, false);
    assert(report_pipe_wake_request_pending(&pipe));
}

static void test_modifier_recovery_pulses_then_restores_latest(void) {
    report_pipe_t pipe;
    report_pipe_init(&pipe);

    keyboard_state_t latest = state_with_key(0x04);
    latest.modifiers = 0x02;
    report_pipe_publish(&pipe, &latest, 1);
    report_pipe_set_active(&pipe, true);
    report_pipe_request_modifier_recovery(&pipe, 0x22);

    report_pipe_item_t item;
    assert(report_pipe_pop(&pipe, &item));
    keyboard_state_t recovery;
    keyboard_state_clear(&recovery);
    recovery.modifiers = 0x22;
    assert(keyboard_state_equal(&item.state, &recovery));

    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_empty(&item.state));

    assert(report_pipe_pop(&pipe, &item));
    assert(keyboard_state_equal(&item.state, &latest));
    assert(!report_pipe_pop(&pipe, &item));
}

int main(void) {
    test_keyboard_boot_encoding();
    test_rollover_and_source_errors();
    test_usb_policy_filter();
    test_state_merge_and_new_press();
    test_inactive_coalescing_and_mount_resync();
    test_ordering_duplicates_and_reset_barrier();
    test_overflow_resynchronizes();
    test_wake_latch();
    test_wake_survives_suspend_until_current_ack();
    test_modifier_recovery_pulses_then_restores_latest();
    puts("core tests passed");
    return 0;
}
