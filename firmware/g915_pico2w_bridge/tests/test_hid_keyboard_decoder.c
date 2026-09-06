#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "ble/hid_keyboard_decoder.h"

// btstack_hid_parser.c uses this utility, while pulling in the complete
// btstack_util.c would add unrelated platform and logging dependencies.
uint32_t btstack_min(uint32_t left, uint32_t right) {
    return left < right ? left : right;
}

static const uint8_t keyboard_without_report_id[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xa1, 0x01,        // Collection (Application)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (None)
    0x29, 0x65,        //   Usage Maximum (Keyboard Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xc0,              // End Collection
};

static const uint8_t composite_descriptor[] = {
    // Keyboard fragment, report ID 1.
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xa1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (None)
    0x29, 0x65,        //   Usage Maximum (Keyboard Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xc0,              // End Collection

    // Independently owned keyboard fragment, report ID 2.
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xa1, 0x01,        // Collection (Application)
    0x85, 0x02,        //   Report ID (2)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (None)
    0x29, 0x65,        //   Usage Maximum (Keyboard Application)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x75, 0x08,        //   Report Size (8)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x00,        //   Input (Data, Array, Absolute)
    0xc0,              // End Collection

    // Consumer report, report ID 3.
    0x05, 0x0c,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xa1, 0x01,        // Collection (Application)
    0x85, 0x03,        //   Report ID (3)
    0x09, 0xe9,        //   Usage (Volume Increment)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x75, 0x07,        //   Report Size (7)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Constant, Variable, Absolute)
    0xc0,              // End Collection
};

static bool has_usage(const keyboard_state_t *state, uint16_t usage) {
    const uint32_t mask = UINT32_C(1) << (usage % 32u);
    return (state->usages[usage / 32u] & mask) != 0u;
}

static hid_keyboard_decode_result_t decode(
    hid_keyboard_decoder_t *decoder, uint8_t report_id,
    const uint8_t *descriptor, uint16_t descriptor_len,
    const uint8_t *report, uint16_t report_len,
    const keyboard_state_t **state) {
    return hid_keyboard_decoder_decode(
        decoder, 0u, report_id, descriptor, descriptor_len,
        report, report_len, state);
}

static void test_no_report_id_hids_prefix(void) {
    hid_keyboard_decoder_t decoder;
    hid_keyboard_decoder_init(&decoder);

    // HIDS prepends zero when the descriptor does not declare report IDs.
    // The parser must see only the payload byte (Keyboard A, usage 0x04).
    const uint8_t report[] = {0x00, 0x04};
    const keyboard_state_t *state = NULL;
    assert(decode(&decoder, 0u, keyboard_without_report_id,
                  sizeof(keyboard_without_report_id), report,
                  sizeof(report), &state) == HID_KEYBOARD_DECODE_CHANGED);
    assert(state == &decoder.state);
    assert(has_usage(state, 0x04u));
}

static void test_report_id_and_length_validation(void) {
    hid_keyboard_decoder_t decoder;
    hid_keyboard_decoder_init(&decoder);
    const keyboard_state_t *state = NULL;

    const uint8_t unknown_id[] = {0x04, 0x04};
    assert(decode(&decoder, 4u, composite_descriptor,
                  sizeof(composite_descriptor), unknown_id,
                  sizeof(unknown_id), &state) == HID_KEYBOARD_DECODE_INVALID);
    assert(state == NULL);

    const uint8_t mismatched_prefix[] = {0x02, 0x04};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), mismatched_prefix,
                  sizeof(mismatched_prefix), &state) ==
           HID_KEYBOARD_DECODE_INVALID);

    const uint8_t too_short[] = {0x01};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), too_short,
                  sizeof(too_short), &state) == HID_KEYBOARD_DECODE_INVALID);

    const uint8_t too_long[] = {0x01, 0x04, 0x00};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), too_long,
                  sizeof(too_long), &state) == HID_KEYBOARD_DECODE_INVALID);
    assert(keyboard_state_empty(&decoder.state));
}

static void test_consumer_report_does_not_clear_keyboard(void) {
    hid_keyboard_decoder_t decoder;
    hid_keyboard_decoder_init(&decoder);
    const keyboard_state_t *state = NULL;

    const uint8_t keyboard_press[] = {0x01, 0x04};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), keyboard_press,
                  sizeof(keyboard_press), &state) ==
           HID_KEYBOARD_DECODE_CHANGED);
    assert(has_usage(&decoder.state, 0x04u));

    const uint8_t consumer_press[] = {0x03, 0x01};
    state = &decoder.state;
    assert(decode(&decoder, 3u, composite_descriptor,
                  sizeof(composite_descriptor), consumer_press,
                  sizeof(consumer_press), &state) ==
           HID_KEYBOARD_DECODE_IGNORED);
    assert(state == NULL);
    assert(has_usage(&decoder.state, 0x04u));
}

static void test_fragment_union_and_independent_release(void) {
    hid_keyboard_decoder_t decoder;
    hid_keyboard_decoder_init(&decoder);
    const keyboard_state_t *state = NULL;

    const uint8_t first_press[] = {0x01, 0x04};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), first_press,
                  sizeof(first_press), &state) ==
           HID_KEYBOARD_DECODE_CHANGED);
    assert(has_usage(state, 0x04u));

    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), first_press,
                  sizeof(first_press), &state) ==
           HID_KEYBOARD_DECODE_UNCHANGED);
    assert(state == &decoder.state);

    const uint8_t second_press[] = {0x02, 0x05};
    assert(decode(&decoder, 2u, composite_descriptor,
                  sizeof(composite_descriptor), second_press,
                  sizeof(second_press), &state) ==
           HID_KEYBOARD_DECODE_CHANGED);
    assert(has_usage(state, 0x04u));
    assert(has_usage(state, 0x05u));

    const uint8_t first_release[] = {0x01, 0x00};
    assert(decode(&decoder, 1u, composite_descriptor,
                  sizeof(composite_descriptor), first_release,
                  sizeof(first_release), &state) ==
           HID_KEYBOARD_DECODE_CHANGED);
    assert(!has_usage(state, 0x04u));
    assert(has_usage(state, 0x05u));

    const uint8_t second_release[] = {0x02, 0x00};
    assert(decode(&decoder, 2u, composite_descriptor,
                  sizeof(composite_descriptor), second_release,
                  sizeof(second_release), &state) ==
           HID_KEYBOARD_DECODE_CHANGED);
    assert(keyboard_state_empty(state));
}

int main(void) {
    test_no_report_id_hids_prefix();
    test_report_id_and_length_validation();
    test_consumer_report_does_not_clear_keyboard();
    test_fragment_union_and_independent_release();
    puts("HID keyboard decoder tests passed");
    return 0;
}
