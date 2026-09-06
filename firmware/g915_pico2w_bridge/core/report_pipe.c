#include "report_pipe.h"

#include <string.h>

static void clear_items(report_pipe_t *pipe) {
    pipe->read_index = 0;
    pipe->count = 0;
}

static void append_item(report_pipe_t *pipe, const keyboard_state_t *state,
                        uint32_t timestamp_us) {
    const size_t write_index =
        (pipe->read_index + pipe->count) % REPORT_PIPE_CAPACITY;
    pipe->items[write_index].state = *state;
    pipe->items[write_index].epoch = pipe->epoch;
    pipe->items[write_index].sequence = pipe->next_sequence++;
    pipe->items[write_index].timestamp_us = timestamp_us;
    ++pipe->count;
}

static void enqueue_resync(report_pipe_t *pipe) {
    keyboard_state_t released;
    keyboard_state_clear(&released);

    clear_items(pipe);
    append_item(pipe, &released, 0);
    if (pipe->latest_valid && !keyboard_state_empty(&pipe->latest)) {
        append_item(pipe, &pipe->latest, 0);
    }
}

void report_pipe_init(report_pipe_t *pipe) {
    memset(pipe, 0, sizeof(*pipe));
    keyboard_state_clear(&pipe->latest);
    pipe->latest_valid = true;
    pipe->next_sequence = 1;
}

void report_pipe_set_active(report_pipe_t *pipe, bool active) {
    if (pipe->active == active) return;
    pipe->active = active;
    clear_items(pipe);
    if (active) enqueue_resync(pipe);
}

void report_pipe_publish(report_pipe_t *pipe, const keyboard_state_t *state,
                         uint32_t epoch) {
    report_pipe_publish_at(pipe, state, epoch, 0);
}

void report_pipe_publish_at(report_pipe_t *pipe, const keyboard_state_t *state,
                            uint32_t epoch, uint32_t timestamp_us) {
    if (epoch < pipe->epoch) {
        ++pipe->stale_report_count;
        return;
    }

    if (epoch > pipe->epoch) {
        pipe->epoch = epoch;
        keyboard_state_clear(&pipe->latest);
        pipe->latest_valid = true;
        pipe->wake_pending = false;
        clear_items(pipe);
        if (pipe->active) enqueue_resync(pipe);
    }

    if (pipe->latest_valid && keyboard_state_equal(&pipe->latest, state)) {
        return;
    }

    if (keyboard_state_has_new_press(&pipe->latest, state)) {
        pipe->wake_pending = true;
    }
    pipe->latest = *state;
    pipe->latest_valid = true;

    if (!pipe->active) return;
    if (pipe->count == REPORT_PIPE_CAPACITY) {
        ++pipe->overflow_count;
        enqueue_resync(pipe);
        return;
    }
    append_item(pipe, state, timestamp_us);
}

void report_pipe_source_reset(report_pipe_t *pipe, uint32_t epoch) {
    if (epoch < pipe->epoch) {
        ++pipe->stale_report_count;
        return;
    }
    pipe->epoch = epoch;
    keyboard_state_clear(&pipe->latest);
    pipe->latest_valid = true;
    pipe->wake_pending = false;
    clear_items(pipe);
    if (pipe->active) enqueue_resync(pipe);
}

void report_pipe_request_resync(report_pipe_t *pipe) {
    if (pipe->active) enqueue_resync(pipe);
}

void report_pipe_request_modifier_recovery(report_pipe_t *pipe,
                                           uint8_t modifier_mask) {
    if (!pipe->active || modifier_mask == 0) return;

    keyboard_state_t pressed;
    keyboard_state_t released;
    keyboard_state_clear(&pressed);
    keyboard_state_clear(&released);
    pressed.modifiers = modifier_mask;

    // Force the destination OS to observe a real modifier transition. This
    // clears a key-down state retained when the previous USB session vanished
    // during a reset or firmware update.
    clear_items(pipe);
    append_item(pipe, &pressed, 0);
    append_item(pipe, &released, 0);
    if (pipe->latest_valid && !keyboard_state_empty(&pipe->latest)) {
        append_item(pipe, &pipe->latest, 0);
    }
}

bool report_pipe_pop(report_pipe_t *pipe, report_pipe_item_t *item) {
    if (pipe->count == 0) return false;
    *item = pipe->items[pipe->read_index];
    pipe->read_index = (pipe->read_index + 1u) % REPORT_PIPE_CAPACITY;
    --pipe->count;
    return true;
}

bool report_pipe_acknowledge(report_pipe_t *pipe,
                             const report_pipe_item_t *item) {
    if (!pipe->wake_pending || pipe->count != 0 || !pipe->latest_valid ||
        item->epoch != pipe->epoch ||
        !keyboard_state_equal(&item->state, &pipe->latest)) {
        return false;
    }
    pipe->wake_pending = false;
    return true;
}

bool report_pipe_wake_request_pending(const report_pipe_t *pipe) {
    return pipe->wake_pending;
}

bool report_pipe_take_wake_request(report_pipe_t *pipe) {
    const bool pending = pipe->wake_pending;
    pipe->wake_pending = false;
    return pending;
}

const keyboard_state_t *report_pipe_latest(const report_pipe_t *pipe) {
    return pipe->latest_valid ? &pipe->latest : NULL;
}
