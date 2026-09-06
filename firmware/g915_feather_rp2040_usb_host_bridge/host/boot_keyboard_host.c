#include "boot_keyboard_host.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "tusb.h"
#include "class/hid/hid.h"
#include "host/usbh_pvt.h"

#include "app_config.h"

typedef enum {
    CONFIG_STAGE_SET_PROTOCOL = 1,
    CONFIG_STAGE_COMPLETE,
} config_stage_t;

typedef struct {
    uint8_t dev_addr;  // Zero while no interface is claimed.
    uint8_t itf_num;
    uint8_t ep_in;
    uint16_t ep_in_size;
    bool configured;
} boot_keyboard_itf_t;

static boot_keyboard_itf_t keyboard;
// Written on the host core, read by the diagnostics on the other core. An
// aligned word cannot tear, and a lost update only skews a statistic.
static volatile uint32_t arm_failure_count;
static volatile uint8_t declared_interval;
static volatile uint8_t poll_interval;

// The interface may declare a wMaxPacketSize larger than a boot report. Size
// the buffer for the largest full-speed interrupt packet so the transfer is
// requested at the endpoint's own packet size, as TinyUSB's HID host does.
CFG_TUH_MEM_SECTION CFG_TUH_MEM_ALIGN static uint8_t report_buffer[64];

static void process_set_config(tuh_xfer_t *xfer);

static bool arm_receive(void) {
    if (keyboard.dev_addr == 0) return false;
    if (!usbh_edpt_claim(keyboard.dev_addr, keyboard.ep_in)) return false;

    uint16_t length = keyboard.ep_in_size;
    if (length > sizeof(report_buffer)) length = (uint16_t)sizeof(report_buffer);
    if (!usbh_edpt_xfer(keyboard.dev_addr, keyboard.ep_in, report_buffer,
                        length)) {
        (void)usbh_edpt_release(keyboard.dev_addr, keyboard.ep_in);
        return false;
    }
    return true;
}

static bool send_class_request(uint8_t request_code, uint16_t value,
                               config_stage_t next_stage) {
    const tusb_control_request_t request = {
        .bmRequestType_bit = {
            .recipient = TUSB_REQ_RCPT_INTERFACE,
            .type = TUSB_REQ_TYPE_CLASS,
            .direction = TUSB_DIR_OUT,
        },
        .bRequest = request_code,
        .wValue = tu_htole16(value),
        .wIndex = tu_htole16((uint16_t)keyboard.itf_num),
        .wLength = 0,
    };
    tuh_xfer_t xfer = {
        .daddr = keyboard.dev_addr,
        .ep_addr = 0,
        .setup = &request,
        .buffer = NULL,
        .complete_cb = process_set_config,
        .user_data = (uintptr_t)next_stage,
    };
    // TinyUSB copies the request and the transfer descriptor before returning.
    return tuh_control_xfer(&xfer);
}

static void complete_configuration(void) {
    keyboard.configured = true;
    // Arm before announcing the mount, so the first poll after configuration
    // already has a buffer waiting and the application never needs to arm.
    if (!arm_receive()) ++arm_failure_count;
    boot_keyboard_host_mount_cb(keyboard.dev_addr, keyboard.itf_num);
    usbh_driver_set_config_complete(keyboard.dev_addr, keyboard.itf_num);
}

static void process_set_config(tuh_xfer_t *xfer) {
    if (xfer->daddr != keyboard.dev_addr) return;

    // A stall is an acceptable answer to both SET_IDLE and SET_PROTOCOL, the
    // same leniency TinyUSB's HID host applies, so the result is not checked.
    switch ((config_stage_t)xfer->user_data) {
        case CONFIG_STAGE_SET_PROTOCOL:
            if (send_class_request(HID_REQ_CONTROL_SET_PROTOCOL,
                                   HID_PROTOCOL_BOOT, CONFIG_STAGE_COMPLETE)) {
                break;
            }
            // The request could not even be issued. Finish configuration
            // anyway so the enumerator is not left waiting on this interface.
            complete_configuration();
            break;
        case CONFIG_STAGE_COMPLETE:
            complete_configuration();
            break;
    }
}

static bool driver_init(void) {
    memset(&keyboard, 0, sizeof(keyboard));
    arm_failure_count = 0;
    declared_interval = 0;
    poll_interval = 0;
    return true;
}

static bool driver_deinit(void) {
    memset(&keyboard, 0, sizeof(keyboard));
    return true;
}

static bool open_keyboard_interface(uint8_t dev_addr,
                                    const tusb_desc_interface_t *itf_desc,
                                    uint16_t max_len) {
    // Interface descriptor, HID class descriptor, then endpoint descriptors.
    const uint8_t *p_desc = (const uint8_t *)itf_desc;
    const uint8_t *const end = p_desc + max_len;

    p_desc = tu_desc_next(p_desc);
    TU_VERIFY(p_desc < end && tu_desc_type(p_desc) == HID_DESC_TYPE_HID);
    p_desc = tu_desc_next(p_desc);

    uint8_t ep_in = 0;
    uint16_t ep_in_size = 0;
    for (uint8_t i = 0; i < itf_desc->bNumEndpoints && p_desc < end; ++i) {
        TU_VERIFY(tu_desc_type(p_desc) == TUSB_DESC_ENDPOINT);
        const tusb_desc_endpoint_t *ep = (const tusb_desc_endpoint_t *)p_desc;
        if (ep_in == 0 && tu_edpt_dir(ep->bEndpointAddress) == TUSB_DIR_IN &&
            ep->bmAttributes.xfer == TUSB_XFER_INTERRUPT) {
            // bInterval is the longest gap a host may leave between polls, so
            // polling more often is permitted. The host controller takes its
            // polling interval from the descriptor it is handed, so hand it a
            // copy with the interval this bridge wants.
            tusb_desc_endpoint_t polled = *ep;
            declared_interval = ep->bInterval;
#if APP_USB_HOST_POLL_INTERVAL_FRAMES > 0
            polled.bInterval = APP_USB_HOST_POLL_INTERVAL_FRAMES;
#endif
            poll_interval = polled.bInterval;
            TU_ASSERT(tuh_edpt_open(dev_addr, &polled));
            ep_in = ep->bEndpointAddress;
            ep_in_size = tu_edpt_packet_size(ep);
        }
        p_desc = tu_desc_next(p_desc);
    }
    TU_VERIFY(ep_in != 0);

    keyboard.dev_addr = dev_addr;
    keyboard.itf_num = itf_desc->bInterfaceNumber;
    keyboard.ep_in = ep_in;
    keyboard.ep_in_size = ep_in_size;
    keyboard.configured = false;
    return true;
}

static bool driver_open(uint8_t rhport, uint8_t dev_addr,
                        const tusb_desc_interface_t *itf_desc,
                        uint16_t max_len) {
    (void)rhport;
    if (itf_desc->bInterfaceClass != TUSB_CLASS_HID) return false;

    const bool is_boot_keyboard =
        itf_desc->bInterfaceSubClass == HID_SUBCLASS_BOOT &&
        itf_desc->bInterfaceProtocol == HID_ITF_PROTOCOL_KEYBOARD;
    bool claimed = false;
    if (is_boot_keyboard && keyboard.dev_addr == 0) {
        claimed = open_keyboard_interface(dev_addr, itf_desc, max_len);
    }

    boot_keyboard_host_interface_cb(dev_addr, itf_desc->bInterfaceNumber,
                                    itf_desc->bInterfaceProtocol, claimed);
    return claimed;
}

static bool driver_set_config(uint8_t dev_addr, uint8_t itf_num) {
    TU_VERIFY(dev_addr == keyboard.dev_addr && itf_num == keyboard.itf_num);

    // Idle rate 0: the device reports only on change. Stall is acceptable.
    if (!send_class_request(HID_REQ_CONTROL_SET_IDLE, 0,
                            CONFIG_STAGE_SET_PROTOCOL)) {
        complete_configuration();
    }
    return true;
}

static bool driver_xfer_cb(uint8_t dev_addr, uint8_t ep_addr,
                           xfer_result_t result, uint32_t xferred_bytes) {
    if (dev_addr != keyboard.dev_addr || ep_addr != keyboard.ep_in) return true;

    if (result == XFER_RESULT_SUCCESS && xferred_bytes > 0) {
        boot_keyboard_host_report_cb(dev_addr, keyboard.itf_num, report_buffer,
                                     (uint16_t)xferred_bytes);
    }

    // Re-arm on every completion, including a failed one, so a transient
    // error never leaves the endpoint idle. Nothing may run between the
    // report callback and this point: the buffer is reused immediately.
    if (keyboard.configured && !arm_receive()) ++arm_failure_count;
    return true;
}

static void driver_close(uint8_t dev_addr) {
    if (dev_addr != keyboard.dev_addr) return;

    const uint8_t itf_num = keyboard.itf_num;
    const bool was_mounted = keyboard.configured;
    memset(&keyboard, 0, sizeof(keyboard));
    declared_interval = 0;
    poll_interval = 0;
    if (was_mounted) boot_keyboard_host_umount_cb(dev_addr, itf_num);
}

uint32_t boot_keyboard_host_arm_failure_count(void) {
    return arm_failure_count;
}

bool boot_keyboard_host_mounted(void) {
    return keyboard.configured;
}

uint8_t boot_keyboard_host_declared_interval(void) {
    return declared_interval;
}

uint8_t boot_keyboard_host_poll_interval(void) {
    return poll_interval;
}

static const usbh_class_driver_t boot_keyboard_driver = {
    .name = "BOOTKBD",
    .init = driver_init,
    .deinit = driver_deinit,
    .open = driver_open,
    .set_config = driver_set_config,
    .xfer_cb = driver_xfer_cb,
    .close = driver_close,
};

const usbh_class_driver_t *usbh_app_driver_get_cb(uint8_t *driver_count) {
    *driver_count = 1;
    return &boot_keyboard_driver;
}
