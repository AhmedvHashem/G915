/*
 * G915 BLE HID Report Protocol host.
 *
 * The keyboard-facing side uses the complete HOGP report map so consumer and
 * vendor reports cannot disturb the standard keyboard subscription. Only
 * Keyboard/Keypad usages are converted to the USB boot-keyboard report used by
 * the computer-facing side of the bridge.
 */

#define BTSTACK_FILE__ "hog_report_host.c"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <btstack_tlv.h>

#include "btstack_config.h"
#include "btstack.h"
#include "bridge.h"

#define TLV_TAG_HOGD ((((uint32_t)'H') << 24) | (((uint32_t)'O') << 16) | \
                      (((uint32_t)'G') << 8) | 'D')

// Interval values are in 1.25 ms units; scan values are in 0.625 ms units.
#define BLE_CONNECT_SCAN_INTERVAL 48
#define BLE_CONNECT_SCAN_WINDOW   48
#define BLE_CONNECTION_INTERVAL   6
#define BLE_CONNECTION_LATENCY    0
#define BLE_SUPERVISION_TIMEOUT   0x0048

#define HID_DESCRIPTOR_STORAGE_SIZE 2048
#define USB_BOOT_KEY_COUNT 6
#define USB_KEY_USAGE_MIN  0x04
#define USB_KEY_USAGE_MAX  0x65
#define HID_MODIFIER_MIN   0xe0
#define HID_MODIFIER_MAX   0xe7

typedef struct {
    bd_addr_t addr;
    bd_addr_type_t addr_type;
} le_device_addr_t;

static enum {
    W4_WORKING,
    W4_HID_DEVICE_FOUND,
    W4_CONNECTED,
    W4_ENCRYPTED,
    W4_HID_CLIENT_CONNECTED,
    READY,
    W4_TIMEOUT_THEN_SCAN,
    W4_TIMEOUT_THEN_RECONNECT,
} app_state;

static le_device_addr_t remote_device;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static uint16_t hids_cid;
static uint8_t hid_descriptor_storage[HID_DESCRIPTOR_STORAGE_SIZE];

static btstack_timer_source_t connection_timer;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;
static const btstack_tlv_t *btstack_tlv_singleton_impl;
static void *btstack_tlv_singleton_context;

static void hog_start_scan(void);

static void print_connection_parameters(const char *event_name,
                                        uint16_t interval, uint16_t latency) {
    printf("%s: %u.%02u ms interval, latency %u\n", event_name,
           interval * 125 / 100, 25 * (interval & 3), latency);
}

static bool adv_event_contains_hid_service(const uint8_t *packet) {
    const uint8_t *ad_data = gap_event_advertising_report_get_data(packet);
    uint8_t ad_len = gap_event_advertising_report_get_data_length(packet);
    return ad_data_contains_uuid16(
        ad_len, ad_data, ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
}

static void hog_connection_timeout(btstack_timer_source_t *timer) {
    UNUSED(timer);
    printf("Connection timeout; returning to scan\n");
    gap_connect_cancel();
    hog_start_scan();
}

static void hog_connect(void) {
    btstack_run_loop_remove_timer(&connection_timer);
    btstack_run_loop_set_timer(&connection_timer, 10000);
    btstack_run_loop_set_timer_handler(&connection_timer,
                                       &hog_connection_timeout);
    btstack_run_loop_add_timer(&connection_timer);
    connection_handle = HCI_CON_HANDLE_INVALID;
    app_state = W4_CONNECTED;
    gap_connect(remote_device.addr, remote_device.addr_type);
}

static void hog_start_scan(void) {
    printf("Scanning for LE HID devices...\n");
    app_state = W4_HID_DEVICE_FOUND;
    gap_set_scan_parameters(0, BLE_CONNECT_SCAN_INTERVAL,
                            BLE_CONNECT_SCAN_WINDOW);
    gap_start_scan();
}

static void hog_start_connect(void) {
    btstack_tlv_get_instance(&btstack_tlv_singleton_impl,
                             &btstack_tlv_singleton_context);
    if (btstack_tlv_singleton_impl != NULL) {
        int len = btstack_tlv_singleton_impl->get_tag(
            btstack_tlv_singleton_context, TLV_TAG_HOGD,
            (uint8_t *)&remote_device, sizeof(remote_device));
        if (len == sizeof(remote_device)) {
            printf("Bonded; connecting to %s address %s...\n",
                   remote_device.addr_type == 0 ? "public" : "random",
                   bd_addr_to_str(remote_device.addr));
            hog_connect();
            return;
        }
    }
    hog_start_scan();
}

static void hog_reconnect_timeout(btstack_timer_source_t *timer) {
    UNUSED(timer);
    if (app_state == W4_TIMEOUT_THEN_RECONNECT) {
        hog_connect();
    } else if (app_state == W4_TIMEOUT_THEN_SCAN) {
        hog_start_scan();
    }
}

static void schedule_connection_retry(bool reconnect_known_device) {
    app_state = reconnect_known_device ? W4_TIMEOUT_THEN_RECONNECT
                                       : W4_TIMEOUT_THEN_SCAN;
    btstack_run_loop_remove_timer(&connection_timer);
    btstack_run_loop_set_timer(&connection_timer, 100);
    btstack_run_loop_set_timer_handler(&connection_timer,
                                       &hog_reconnect_timeout);
    btstack_run_loop_add_timer(&connection_timer);
}

static void disconnect_after_setup_error(uint8_t status) {
    printf("HID setup failed: 0x%02x\n", status);
    if (connection_handle != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(connection_handle);
    } else {
        schedule_connection_retry(false);
    }
}

static bool add_key_once(uint8_t keys[USB_BOOT_KEY_COUNT],
                         uint8_t *key_count, uint8_t usage) {
    for (uint8_t i = 0; i < *key_count; ++i) {
        if (keys[i] == usage) return true;
    }
    if (*key_count >= USB_BOOT_KEY_COUNT) return false;
    keys[(*key_count)++] = usage;
    return true;
}

static void handle_hid_input_report(uint8_t service_index,
                                    const uint8_t *report,
                                    uint16_t report_len) {
    if (report_len == 0) return;

    const uint8_t *descriptor =
        hids_host_descriptor_storage_get_descriptor_data(hids_cid,
                                                          service_index);
    uint16_t descriptor_len =
        hids_host_descriptor_storage_get_descriptor_len(hids_cid,
                                                         service_index);
    if (descriptor == NULL || descriptor_len == 0) return;

    btstack_hid_parser_t parser;
    btstack_hid_parser_init(&parser, descriptor, descriptor_len,
                            HID_REPORT_TYPE_INPUT, report, report_len);

    uint8_t usb_report[8] = {0};
    uint8_t key_count = 0;
    bool contains_keyboard_fields = false;

    while (btstack_hid_parser_has_more(&parser)) {
        uint16_t usage_page;
        uint16_t usage;
        int32_t value;
        btstack_hid_parser_get_field(&parser, &usage_page, &usage, &value);

        if (usage_page != HID_USAGE_PAGE_KEYBOARD) continue;
        contains_keyboard_fields = true;

        if (usage >= HID_MODIFIER_MIN && usage <= HID_MODIFIER_MAX) {
            if (value != 0) usb_report[0] |= 1u << (usage - HID_MODIFIER_MIN);
            continue;
        }
        if (value == 0 || usage < USB_KEY_USAGE_MIN ||
            usage > USB_KEY_USAGE_MAX) {
            continue;
        }
        add_key_once(&usb_report[2], &key_count, (uint8_t)usage);
    }

    // Consumer/media and Logitech vendor reports never clear keyboard state.
    // A report containing Keyboard/Keypad fields, including an all-zero key
    // release, becomes a complete USB keyboard state snapshot.
    if (contains_keyboard_fields) {
        bridge_keyboard_report(usb_report, sizeof(usb_report));
    }
}

static void handle_hids_event(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    if (hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) return;

    switch (hci_event_gattservice_meta_get_subevent_code(packet)) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                disconnect_after_setup_error(status);
                break;
            }

            printf("HID Report Protocol ready; %u service(s)\n",
                   gattservice_subevent_hid_service_connected_get_num_instances(
                       packet));
            if (btstack_tlv_singleton_impl != NULL) {
                btstack_tlv_singleton_impl->store_tag(
                    btstack_tlv_singleton_context, TLV_TAG_HOGD,
                    (const uint8_t *)&remote_device, sizeof(remote_device));
            }
            app_state = READY;
            bridge_keyboard_connected();
            break;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED:
            hids_cid = 0;
            break;

        case GATTSERVICE_SUBEVENT_HID_REPORT:
            handle_hid_input_report(
                gattservice_subevent_hid_report_get_service_index(packet),
                gattservice_subevent_hid_report_get_report(packet),
                gattservice_subevent_hid_report_get_report_len(packet));
            break;

        default:
            break;
    }
}

static void start_hids_client(void) {
    if (app_state != W4_ENCRYPTED) return;
    app_state = W4_HID_CLIENT_CONNECTED;
    uint8_t status = hids_host_connect(connection_handle, handle_hids_event,
                                       HID_PROTOCOL_MODE_REPORT, &hids_cid);
    if (status != ERROR_CODE_SUCCESS) disconnect_after_setup_error(status);
}

static void packet_handler(uint8_t packet_type, uint16_t channel,
                           uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                btstack_assert(app_state == W4_WORKING);
                hog_start_connect();
            }
            break;

        case GAP_EVENT_ADVERTISING_REPORT:
            if (app_state != W4_HID_DEVICE_FOUND ||
                !adv_event_contains_hid_service(packet)) {
                break;
            }
            gap_stop_scan();
            gap_event_advertising_report_get_address(packet,
                                                       remote_device.addr);
            remote_device.addr_type =
                gap_event_advertising_report_get_address_type(packet);
            printf("Found; connecting to %s address %s...\n",
                   remote_device.addr_type == 0 ? "public" : "random",
                   bd_addr_to_str(remote_device.addr));
            hog_connect();
            break;

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            bool was_ready = app_state == READY;
            bridge_keyboard_disconnected();
            connection_handle = HCI_CON_HANDLE_INVALID;
            hids_cid = 0;
            printf(was_ready ? "Disconnected; reconnecting...\n"
                             : "Disconnected during setup; scanning...\n");
            schedule_connection_retry(was_ready);
            break;
        }

        case HCI_EVENT_META_GAP: {
            if (hci_event_gap_meta_get_subevent_code(packet) !=
                    GAP_SUBEVENT_LE_CONNECTION_COMPLETE ||
                app_state != W4_CONNECTED) {
                break;
            }
            btstack_run_loop_remove_timer(&connection_timer);
            uint8_t status =
                gap_subevent_le_connection_complete_get_status(packet);
            if (status != ERROR_CODE_SUCCESS) {
                printf("BLE connection failed: 0x%02x\n", status);
                schedule_connection_retry(false);
                break;
            }
            connection_handle =
                gap_subevent_le_connection_complete_get_connection_handle(
                    packet);
            print_connection_parameters(
                "BLE connection established",
                gap_subevent_le_connection_complete_get_conn_interval(packet),
                gap_subevent_le_connection_complete_get_conn_latency(packet));
            app_state = W4_ENCRYPTED;
            sm_request_pairing(connection_handle);
            break;
        }

        case HCI_EVENT_LE_META:
            if (hci_event_le_meta_get_subevent_code(packet) !=
                HCI_SUBEVENT_LE_CONNECTION_UPDATE_COMPLETE) {
                break;
            }
            if (hci_subevent_le_connection_update_complete_get_status(packet) !=
                ERROR_CODE_SUCCESS) {
                printf("BLE connection update failed: 0x%02x\n",
                       hci_subevent_le_connection_update_complete_get_status(
                           packet));
                break;
            }
            print_connection_parameters(
                "BLE connection updated",
                hci_subevent_le_connection_update_complete_get_conn_interval(
                    packet),
                hci_subevent_le_connection_update_complete_get_conn_latency(
                    packet));
            break;

        default:
            break;
    }
}

static void sm_packet_handler(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    bool pairing_succeeded = false;
    bool pairing_failed = false;

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            sm_just_works_confirm(
                sm_event_just_works_request_get_handle(packet));
            break;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            printf("Confirming numeric comparison: %" PRIu32 "\n",
                   sm_event_numeric_comparison_request_get_passkey(packet));
            sm_numeric_comparison_confirm(
                sm_event_numeric_comparison_request_get_handle(packet));
            break;

        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            printf("Display passkey: %" PRIu32 "\n",
                   sm_event_passkey_display_number_get_passkey(packet));
            break;

        case SM_EVENT_PAIRING_COMPLETE:
            if (sm_event_pairing_complete_get_status(packet) ==
                ERROR_CODE_SUCCESS) {
                printf("Pairing complete\n");
                pairing_succeeded = true;
            } else {
                printf("Pairing failed: status 0x%02x, reason 0x%02x\n",
                       sm_event_pairing_complete_get_status(packet),
                       sm_event_pairing_complete_get_reason(packet));
                pairing_failed = true;
            }
            break;

        case SM_EVENT_REENCRYPTION_COMPLETE:
            printf("Re-encryption complete\n");
            pairing_succeeded = true;
            break;

        default:
            break;
    }

    if (pairing_succeeded) start_hids_client();
    if (pairing_failed && connection_handle != HCI_CON_HANDLE_INVALID) {
        gap_disconnect(connection_handle);
    }
}

int btstack_main(int argc, const char *argv[]);
int btstack_main(int argc, const char *argv[]) {
    (void)argc;
    (void)argv;

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_use_fixed_passkey_in_display_role(123456);
    sm_set_authentication_requirements(SM_AUTHREQ_SECURE_CONNECTION |
                                       SM_AUTHREQ_BONDING);

    gatt_client_init();
    hids_host_init(hid_descriptor_storage, sizeof(hid_descriptor_storage));

    gap_set_connection_parameters(
        BLE_CONNECT_SCAN_INTERVAL, BLE_CONNECT_SCAN_WINDOW,
        BLE_CONNECTION_INTERVAL, BLE_CONNECTION_INTERVAL,
        BLE_CONNECTION_LATENCY, BLE_SUPERVISION_TIMEOUT, 0, 0);

    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);
    sm_event_callback_registration.callback = &sm_packet_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    setvbuf(stdout, NULL, _IONBF, 0);
    app_state = W4_WORKING;
    hci_power_control(HCI_POWER_ON);
    return 0;
}
