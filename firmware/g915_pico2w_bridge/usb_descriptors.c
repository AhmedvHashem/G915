#include <string.h>

#include "pico/unique_id.h"
#include "tusb.h"

#define USB_VID 0xCAFE
#define USB_PID 0x4004

tusb_desc_device_t const device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0110,
    .bDeviceClass = 0,
    .bDeviceSubClass = 0,
    .bDeviceProtocol = 0,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&device_descriptor;
}

uint8_t const hid_report_descriptor[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()
};

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) {
    (void)instance;
    return hid_report_descriptor;
}

enum {
    ITF_NUM_KEYBOARD,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_KEYBOARD   0x81

uint8_t const configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(hid_report_descriptor), EPNUM_KEYBOARD, 8, 1),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

static const char *const string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "G915 Bridge",
    "Bluetooth Boot Keyboard",
    NULL,
};

static uint16_t descriptor_buffer[33];

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;
    size_t count;
    char serial[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

    if (index == 0) {
        memcpy(&descriptor_buffer[1], string_descriptors[0], 2);
        count = 1;
    } else if (index == 3) {
        pico_get_unique_board_id_string(serial, sizeof(serial));
        count = strlen(serial);
        for (size_t i = 0; i < count; ++i) descriptor_buffer[1 + i] = serial[i];
    } else {
        if (index >= sizeof(string_descriptors) / sizeof(string_descriptors[0])) return NULL;
        const char *value = string_descriptors[index];
        count = strlen(value);
        if (count > 32) count = 32;
        for (size_t i = 0; i < count; ++i) descriptor_buffer[1 + i] = value[i];
    }

    descriptor_buffer[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * count + 2));
    return descriptor_buffer;
}

