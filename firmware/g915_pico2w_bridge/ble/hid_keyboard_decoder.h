#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "app_config.h"
#include "core/keyboard_state.h"

typedef enum {
    HID_KEYBOARD_DECODE_IGNORED,
    HID_KEYBOARD_DECODE_INVALID,
    HID_KEYBOARD_DECODE_UNCHANGED,
    HID_KEYBOARD_DECODE_CHANGED,
} hid_keyboard_decode_result_t;

typedef struct {
    bool occupied;
    uint8_t service_index;
    uint8_t report_id;
    keyboard_state_t state;
} hid_keyboard_fragment_t;

typedef struct {
    hid_keyboard_fragment_t fragments[APP_HID_MAX_REPORT_FRAGMENTS];
    keyboard_state_t state;
} hid_keyboard_decoder_t;

void hid_keyboard_decoder_init(hid_keyboard_decoder_t *decoder);
void hid_keyboard_decoder_reset(hid_keyboard_decoder_t *decoder);

// BTstack HIDS report events include a synthetic report-ID byte at report[0].
// A valid keyboard report replaces the complete state of its
// (service_index, report_id) fragment. The decoder returns their union in
// state so independent keyboard fragments cannot release each other's keys.
hid_keyboard_decode_result_t hid_keyboard_decoder_decode(
    hid_keyboard_decoder_t *decoder, uint8_t service_index, uint8_t report_id,
    const uint8_t *descriptor, uint16_t descriptor_len,
    const uint8_t *report, uint16_t report_len,
    const keyboard_state_t **state);
