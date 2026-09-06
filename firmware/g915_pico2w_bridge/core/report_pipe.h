#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "keyboard_state.h"

#define REPORT_PIPE_CAPACITY 16u

typedef struct {
    keyboard_state_t state;
    uint32_t epoch;
    uint32_t sequence;
} report_pipe_item_t;

typedef struct {
    report_pipe_item_t items[REPORT_PIPE_CAPACITY];
    size_t read_index;
    size_t count;
    keyboard_state_t latest;
    uint32_t epoch;
    uint32_t next_sequence;
    uint32_t overflow_count;
    uint32_t stale_report_count;
    bool latest_valid;
    bool active;
    bool wake_pending;
} report_pipe_t;

void report_pipe_init(report_pipe_t *pipe);
void report_pipe_set_active(report_pipe_t *pipe, bool active);
void report_pipe_publish(report_pipe_t *pipe, const keyboard_state_t *state,
                         uint32_t epoch);
void report_pipe_source_reset(report_pipe_t *pipe, uint32_t epoch);
void report_pipe_request_resync(report_pipe_t *pipe);
void report_pipe_request_modifier_recovery(report_pipe_t *pipe,
                                           uint8_t modifier_mask);
bool report_pipe_pop(report_pipe_t *pipe, report_pipe_item_t *item);
bool report_pipe_acknowledge(report_pipe_t *pipe,
                             const report_pipe_item_t *item);
bool report_pipe_wake_request_pending(const report_pipe_t *pipe);
bool report_pipe_take_wake_request(report_pipe_t *pipe);
const keyboard_state_t *report_pipe_latest(const report_pipe_t *pipe);
