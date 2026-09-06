#include "keyboard_state.h"

#include <string.h>

#define HID_USAGE_ERROR_ROLLOVER 0x01u
#define HID_USAGE_ERROR_MIN      0x01u
#define HID_USAGE_ERROR_MAX      0x03u
#define HID_USAGE_MODIFIER_MIN   0xe0u
#define HID_USAGE_MODIFIER_MAX   0xe7u

void keyboard_state_clear(keyboard_state_t *state) {
    memset(state, 0, sizeof(*state));
}

bool keyboard_state_equal(const keyboard_state_t *left,
                          const keyboard_state_t *right) {
    return left->modifiers == right->modifiers &&
           left->errors == right->errors &&
           memcmp(left->usages, right->usages, sizeof(left->usages)) == 0;
}

bool keyboard_state_empty(const keyboard_state_t *state) {
    if (state->modifiers != 0 || state->errors != 0) return false;
    for (size_t i = 0; i < KEYBOARD_USAGE_WORD_COUNT; ++i) {
        if (state->usages[i] != 0) return false;
    }
    return true;
}

bool keyboard_state_set_usage(keyboard_state_t *state, uint16_t usage,
                              bool pressed) {
    if (usage == 0) return true;

    if (usage >= HID_USAGE_ERROR_MIN && usage <= HID_USAGE_ERROR_MAX) {
        const uint8_t mask = (uint8_t)(1u << (usage - HID_USAGE_ERROR_MIN));
        if (pressed) {
            state->errors |= mask;
        } else {
            state->errors &= (uint8_t)~mask;
        }
        return true;
    }

    if (usage >= HID_USAGE_MODIFIER_MIN && usage <= HID_USAGE_MODIFIER_MAX) {
        const uint8_t mask = (uint8_t)(1u << (usage - HID_USAGE_MODIFIER_MIN));
        if (pressed) {
            state->modifiers |= mask;
        } else {
            state->modifiers &= (uint8_t)~mask;
        }
        return true;
    }

    if (usage >= KEYBOARD_USAGE_COUNT) return false;
    const size_t word = usage / 32u;
    const uint32_t mask = 1u << (usage % 32u);
    if (pressed) {
        state->usages[word] |= mask;
    } else {
        state->usages[word] &= ~mask;
    }
    return true;
}

void keyboard_state_merge(keyboard_state_t *destination,
                          const keyboard_state_t *source) {
    destination->modifiers |= source->modifiers;
    destination->errors |= source->errors;
    for (size_t i = 0; i < KEYBOARD_USAGE_WORD_COUNT; ++i) {
        destination->usages[i] |= source->usages[i];
    }
}

bool keyboard_state_has_new_press(const keyboard_state_t *before,
                                  const keyboard_state_t *after) {
    if ((after->modifiers & (uint8_t)~before->modifiers) != 0) return true;
    if ((after->errors & (uint8_t)~before->errors) != 0) return true;
    for (size_t i = 0; i < KEYBOARD_USAGE_WORD_COUNT; ++i) {
        if ((after->usages[i] & ~before->usages[i]) != 0) return true;
    }
    return false;
}

void keyboard_state_filter_for_boot_report(keyboard_state_t *state,
                                           uint8_t maximum_usage,
                                           bool retain_source_errors) {
    if (!retain_source_errors) state->errors = 0;

    for (uint16_t usage = (uint16_t)maximum_usage + 1u;
         usage < KEYBOARD_USAGE_COUNT; ++usage) {
        const size_t word = usage / 32u;
        state->usages[word] &= ~(1u << (usage % 32u));
    }
}

void keyboard_state_to_boot_report(const keyboard_state_t *state,
                                   uint8_t maximum_usage,
                                   bool forward_source_errors,
                                   keyboard_boot_report_t *report) {
    memset(report, 0, sizeof(*report));
    report->bytes[0] = state->modifiers;

    size_t key_count = 0;
    for (uint16_t usage = 0x04u; usage <= maximum_usage; ++usage) {
        if (usage >= HID_USAGE_MODIFIER_MIN &&
            usage <= HID_USAGE_MODIFIER_MAX) {
            continue;
        }
        const uint32_t mask = 1u << (usage % 32u);
        if ((state->usages[usage / 32u] & mask) == 0) continue;

        if (key_count < KEYBOARD_BOOT_KEY_COUNT) {
            report->bytes[2u + key_count] = (uint8_t)usage;
        }
        ++key_count;
    }

    if (key_count > KEYBOARD_BOOT_KEY_COUNT ||
        (forward_source_errors && state->errors != 0)) {
        for (size_t i = 0; i < KEYBOARD_BOOT_KEY_COUNT; ++i) {
            report->bytes[2u + i] = HID_USAGE_ERROR_ROLLOVER;
        }
    }
}
