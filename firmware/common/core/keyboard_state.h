#pragma once

#include <stdbool.h>
#include <stdint.h>

#define KEYBOARD_USAGE_COUNT       256u
#define KEYBOARD_USAGE_WORD_COUNT  (KEYBOARD_USAGE_COUNT / 32u)
#define KEYBOARD_BOOT_REPORT_SIZE  8u
#define KEYBOARD_BOOT_KEY_COUNT    6u

typedef struct {
    uint32_t usages[KEYBOARD_USAGE_WORD_COUNT];
    uint8_t modifiers;
    uint8_t errors;
} keyboard_state_t;

typedef struct {
    uint8_t bytes[KEYBOARD_BOOT_REPORT_SIZE];
} keyboard_boot_report_t;

void keyboard_state_clear(keyboard_state_t *state);
bool keyboard_state_equal(const keyboard_state_t *left,
                          const keyboard_state_t *right);
bool keyboard_state_empty(const keyboard_state_t *state);
bool keyboard_state_set_usage(keyboard_state_t *state, uint16_t usage,
                              bool pressed);
void keyboard_state_merge(keyboard_state_t *destination,
                          const keyboard_state_t *source);
bool keyboard_state_has_new_press(const keyboard_state_t *before,
                                  const keyboard_state_t *after);
void keyboard_state_filter_for_boot_report(keyboard_state_t *state,
                                           uint8_t maximum_usage,
                                           bool retain_source_errors);
void keyboard_state_to_boot_report(const keyboard_state_t *state,
                                   uint8_t maximum_usage,
                                   bool forward_source_errors,
                                   keyboard_boot_report_t *report);
