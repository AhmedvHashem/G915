#include "ble/hid_keyboard_decoder.h"

#include <stddef.h>
#include <string.h>

#include "btstack.h"

static hid_keyboard_fragment_t *find_fragment(
    hid_keyboard_decoder_t *decoder, uint8_t service_index,
    uint8_t report_id) {
    for (size_t i = 0; i < APP_HID_MAX_REPORT_FRAGMENTS; ++i) {
        hid_keyboard_fragment_t *fragment = &decoder->fragments[i];
        if (fragment->occupied && fragment->service_index == service_index &&
            fragment->report_id == report_id) {
            return fragment;
        }
    }
    return NULL;
}

static hid_keyboard_fragment_t *allocate_fragment(
    hid_keyboard_decoder_t *decoder, uint8_t service_index,
    uint8_t report_id) {
    hid_keyboard_fragment_t *fragment =
        find_fragment(decoder, service_index, report_id);
    if (fragment != NULL) return fragment;

    for (size_t i = 0; i < APP_HID_MAX_REPORT_FRAGMENTS; ++i) {
        fragment = &decoder->fragments[i];
        if (fragment->occupied) continue;
        fragment->occupied = true;
        fragment->service_index = service_index;
        fragment->report_id = report_id;
        keyboard_state_clear(&fragment->state);
        return fragment;
    }
    return NULL;
}

static void merge_fragments(hid_keyboard_decoder_t *decoder,
                            keyboard_state_t *merged) {
    keyboard_state_clear(merged);
    for (size_t i = 0; i < APP_HID_MAX_REPORT_FRAGMENTS; ++i) {
        const hid_keyboard_fragment_t *fragment = &decoder->fragments[i];
        if (fragment->occupied) {
            keyboard_state_merge(merged, &fragment->state);
        }
    }
}

void hid_keyboard_decoder_init(hid_keyboard_decoder_t *decoder) {
    hid_keyboard_decoder_reset(decoder);
}

void hid_keyboard_decoder_reset(hid_keyboard_decoder_t *decoder) {
    if (decoder == NULL) return;
    memset(decoder, 0, sizeof(*decoder));
}

hid_keyboard_decode_result_t hid_keyboard_decoder_decode(
    hid_keyboard_decoder_t *decoder, uint8_t service_index, uint8_t report_id,
    const uint8_t *descriptor, uint16_t descriptor_len,
    const uint8_t *report, uint16_t report_len,
    const keyboard_state_t **state) {
    if (state != NULL) *state = NULL;
    if (decoder == NULL || descriptor == NULL || descriptor_len == 0u ||
        report == NULL || report_len == 0u) {
        return HID_KEYBOARD_DECODE_INVALID;
    }

    const bool report_ids_declared =
        btstack_hid_report_id_declared(descriptor, descriptor_len);
    const uint16_t descriptor_report_id =
        report_ids_declared ? report_id : HID_REPORT_ID_UNDEFINED;

    if (report_ids_declared &&
        btstack_hid_report_id_valid(report_id, descriptor, descriptor_len) !=
            HID_REPORT_ID_VALID) {
        return HID_KEYBOARD_DECODE_INVALID;
    }
    if (!report_ids_declared && report_id != 0u) {
        return HID_KEYBOARD_DECODE_INVALID;
    }

    const int payload_len = btstack_hid_get_report_size_for_id(
        descriptor_report_id, HID_REPORT_TYPE_INPUT, descriptor,
        descriptor_len);
    if (payload_len <= 0 || payload_len > UINT16_MAX - 1 ||
        report_len != (uint16_t)(payload_len + 1) ||
        report[0] != report_id) {
        return HID_KEYBOARD_DECODE_INVALID;
    }

    // The BTstack HID parser needs the ID byte only when the report map
    // declares report IDs. HIDS adds it in both cases.
    const uint8_t *parser_report = report_ids_declared ? report : &report[1];
    const uint16_t parser_report_len =
        report_ids_declared ? report_len : (uint16_t)(report_len - 1u);

    btstack_hid_parser_t parser;
    btstack_hid_parser_init(&parser, descriptor, descriptor_len,
                            HID_REPORT_TYPE_INPUT, parser_report,
                            parser_report_len);

    keyboard_state_t fragment_state;
    keyboard_state_clear(&fragment_state);
    bool contains_keyboard_fields = false;

    while (btstack_hid_parser_has_more(&parser)) {
        uint16_t usage_page;
        uint16_t usage;
        int32_t value;
        btstack_hid_parser_get_field(&parser, &usage_page, &usage, &value);

        if (usage_page != HID_USAGE_PAGE_KEYBOARD) continue;
        contains_keyboard_fields = true;
        if (value != 0) {
            (void)keyboard_state_set_usage(&fragment_state, usage, true);
        }
    }

    // Consumer, mouse, and vendor reports do not own keyboard state and must
    // never release it.
    if (!contains_keyboard_fields) return HID_KEYBOARD_DECODE_IGNORED;

    hid_keyboard_fragment_t *fragment =
        allocate_fragment(decoder, service_index, report_id);
    if (fragment == NULL) return HID_KEYBOARD_DECODE_INVALID;
    fragment->state = fragment_state;

    keyboard_state_t merged;
    merge_fragments(decoder, &merged);
    if (keyboard_state_equal(&decoder->state, &merged)) {
        if (state != NULL) *state = &decoder->state;
        return HID_KEYBOARD_DECODE_UNCHANGED;
    }

    decoder->state = merged;
    if (state != NULL) *state = &decoder->state;
    return HID_KEYBOARD_DECODE_CHANGED;
}
