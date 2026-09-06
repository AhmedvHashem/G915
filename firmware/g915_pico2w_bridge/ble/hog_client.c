#define BTSTACK_FILE__ "ble/hog_client.c"

#include "ble/hog_client.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "app_config.h"
#include "ble/hid_keyboard_decoder.h"
#include "btstack.h"
#include "btstack_tlv.h"

#define APPROVED_PEER_TLV_TAG                                      \
    ((((uint32_t)'H') << 24) | (((uint32_t)'G') << 16) |          \
     (((uint32_t)'P') << 8) | (uint32_t)'R')
#define APPROVED_PEER_RECORD_VERSION 1u
#define APPROVED_PEER_RECORD_SIZE    8u

typedef struct {
    bd_addr_t address;
    bd_addr_type_t address_type;
} peer_identity_t;

static hog_client_callbacks_t client_callbacks;
static hog_client_state_t client_state = HOG_CLIENT_STARTING;
static uint32_t source_epoch = 1u;

static bool initialized;
static bool hci_working;
static bool approved_peer_valid;
static bool target_peer_valid;
static bool target_is_approved;
static bool source_session_active;
static bool link_secure;

static peer_identity_t approved_peer;
static peer_identity_t target_peer;
static hci_con_handle_t connection_handle = HCI_CON_HANDLE_INVALID;
static uint16_t hids_cid;
static uint32_t retry_delay_ms = APP_BLE_RETRY_INITIAL_MS;

static uint8_t hid_descriptor_storage[APP_HID_DESCRIPTOR_STORAGE_SIZE];
static hid_keyboard_decoder_t keyboard_decoder;
static btstack_timer_source_t state_timer;
static btstack_packet_callback_registration_t hci_event_registration;
static btstack_packet_callback_registration_t sm_event_registration;
static const btstack_tlv_t *tlv_impl;
static void *tlv_context;

static void start_scan(void);
static void retry_current_policy(void);
static void schedule_backoff(uint8_t status);

static void transition_to(hog_client_state_t state, uint8_t status) {
    client_state = state;
    if (client_callbacks.status_changed != NULL) {
        client_callbacks.status_changed(state, status,
                                        client_callbacks.context);
    }
}

static void cancel_state_timer(void) {
    (void)btstack_run_loop_remove_timer(&state_timer);
}

static void arm_state_timer(uint32_t timeout_ms) {
    cancel_state_timer();
    btstack_run_loop_set_timer(&state_timer, timeout_ms);
    btstack_run_loop_add_timer(&state_timer);
}

static bool address_type_valid(bd_addr_type_t address_type) {
    return address_type == BD_ADDR_TYPE_LE_PUBLIC ||
           address_type == BD_ADDR_TYPE_LE_RANDOM ||
           address_type == BD_ADDR_TYPE_LE_PUBLIC_IDENTITY ||
           address_type == BD_ADDR_TYPE_LE_RANDOM_IDENTITY;
}

static uint8_t base_address_type(bd_addr_type_t address_type) {
    return ((uint8_t)address_type) & 1u;
}

static bool peer_equal(const peer_identity_t *left,
                       const peer_identity_t *right) {
    return base_address_type(left->address_type) ==
               base_address_type(right->address_type) &&
           memcmp(left->address, right->address, sizeof(bd_addr_t)) == 0;
}

static void encode_peer_record(const peer_identity_t *peer,
                               uint8_t record[APPROVED_PEER_RECORD_SIZE]) {
    record[0] = APPROVED_PEER_RECORD_VERSION;
    record[1] = (uint8_t)peer->address_type;
    memcpy(&record[2], peer->address, sizeof(bd_addr_t));
}

static bool decode_peer_record(
    const uint8_t record[APPROVED_PEER_RECORD_SIZE],
    peer_identity_t *peer) {
    const bd_addr_type_t address_type = (bd_addr_type_t)record[1];
    if (record[0] != APPROVED_PEER_RECORD_VERSION ||
        !address_type_valid(address_type)) {
        return false;
    }
    peer->address_type = address_type;
    memcpy(peer->address, &record[2], sizeof(bd_addr_t));
    return true;
}

static void refresh_tlv_instance(void) {
    btstack_tlv_get_instance(&tlv_impl, &tlv_context);
}

static void load_approved_peer(void) {
    approved_peer_valid = false;
    refresh_tlv_instance();
    if (tlv_impl == NULL) return;

    uint8_t record[APPROVED_PEER_RECORD_SIZE];
    const int record_len = tlv_impl->get_tag(
        tlv_context, APPROVED_PEER_TLV_TAG, record, sizeof(record));
    if (record_len == (int)sizeof(record) &&
        decode_peer_record(record, &approved_peer)) {
        approved_peer_valid = true;
        return;
    }

    if (record_len != 0) {
        // Remove records from pre-versioned or incompatible layouts.
        tlv_impl->delete_tag(tlv_context, APPROVED_PEER_TLV_TAG);
    }
}

static bool store_approved_peer_if_changed(const peer_identity_t *peer) {
    refresh_tlv_instance();
    if (tlv_impl == NULL) return false;

    uint8_t wanted[APPROVED_PEER_RECORD_SIZE];
    uint8_t stored[APPROVED_PEER_RECORD_SIZE];
    encode_peer_record(peer, wanted);
    const int stored_len = tlv_impl->get_tag(
        tlv_context, APPROVED_PEER_TLV_TAG, stored, sizeof(stored));
    if (stored_len == (int)sizeof(stored) &&
        memcmp(stored, wanted, sizeof(wanted)) == 0) {
        approved_peer = *peer;
        approved_peer_valid = true;
        return true;
    }

    if (tlv_impl->store_tag(tlv_context, APPROVED_PEER_TLV_TAG,
                            wanted, sizeof(wanted)) != 0) {
        return false;
    }
    approved_peer = *peer;
    approved_peer_valid = true;
    return true;
}

static void delete_approved_peer_record(void) {
    refresh_tlv_instance();
    if (tlv_impl != NULL) {
        tlv_impl->delete_tag(tlv_context, APPROVED_PEER_TLV_TAG);
    }
}

static bool peer_identity_for_connection(peer_identity_t *peer) {
    if (connection_handle == HCI_CON_HANDLE_INVALID) return false;
    const int device_index = sm_le_device_index(connection_handle);
    if (device_index < 0) return false;

    int address_type;
    sm_key_t irk;
    le_device_db_info(device_index, &address_type, peer->address, irk);
    peer->address_type = (bd_addr_type_t)address_type;
    return address_type_valid(peer->address_type);
}

static bool authenticated_sc_peer_for_connection(peer_identity_t *peer) {
    if (connection_handle == HCI_CON_HANDLE_INVALID) return false;

    sm_key_t active_ltk;
    if (sm_get_ltk(connection_handle, active_ltk) != ERROR_CODE_SUCCESS) {
        return false;
    }
    const int device_index = sm_le_device_index(connection_handle);
    if (device_index < 0) return false;

    int address_type;
    sm_key_t irk;
    le_device_db_info(device_index, &address_type, peer->address, irk);
    peer->address_type = (bd_addr_type_t)address_type;
    if (!address_type_valid(peer->address_type)) return false;

    uint16_t ediv;
    uint8_t rand[8];
    sm_key_t ltk;
    int key_size;
    int authenticated;
    int authorized;
    int secure_connection;
    le_device_db_encryption_get(device_index, &ediv, rand, ltk, &key_size,
                                &authenticated, &authorized,
                                &secure_connection);
    (void)ediv;
    (void)rand;
    (void)authorized;
    return key_size == 16 && authenticated != 0 && secure_connection != 0 &&
           memcmp(active_ltk, ltk, sizeof(sm_key_t)) == 0;
}

static void advance_epoch(void) {
    ++source_epoch;
    if (source_epoch == 0u) ++source_epoch;
}

static void end_source_session(void) {
    if (!source_session_active) return;
    source_session_active = false;
    link_secure = false;
    hid_keyboard_decoder_reset(&keyboard_decoder);
    advance_epoch();
    if (client_callbacks.source_reset != NULL) {
        client_callbacks.source_reset(source_epoch, client_callbacks.context);
    }
}

static void clear_connection(void) {
    connection_handle = HCI_CON_HANDLE_INVALID;
    hids_cid = 0u;
    target_peer_valid = false;
    target_is_approved = false;
    link_secure = false;
}

static bool advertising_report_contains_hid(const uint8_t *packet,
                                            uint16_t size) {
    if (size < 12u) return false;
    const uint8_t data_len =
        gap_event_advertising_report_get_data_length(packet);
    if ((uint16_t)data_len > (uint16_t)(size - 12u)) return false;
    return ad_data_contains_uuid16(
        data_len, gap_event_advertising_report_get_data(packet),
        ORG_BLUETOOTH_SERVICE_HUMAN_INTERFACE_DEVICE);
}

static void connect_to_peer(const peer_identity_t *peer) {
    cancel_state_timer();
    target_peer = *peer;
    target_peer_valid = true;
    target_is_approved =
        approved_peer_valid && peer_equal(peer, &approved_peer);
    connection_handle = HCI_CON_HANDLE_INVALID;
    hids_cid = 0u;
    link_secure = false;

    transition_to(HOG_CLIENT_CONNECTING, ERROR_CODE_SUCCESS);
    const uint8_t status = gap_connect(target_peer.address,
                                       target_peer.address_type);
    if (status != ERROR_CODE_SUCCESS) {
        schedule_backoff(status);
        return;
    }
    arm_state_timer(APP_BLE_CONNECT_TIMEOUT_MS);
}

static void start_scan(void) {
    cancel_state_timer();
    target_peer_valid = false;
    target_is_approved = false;
    transition_to(HOG_CLIENT_UNPROVISIONED, ERROR_CODE_SUCCESS);
    gap_set_scan_parameters(0u, APP_BLE_SCAN_INTERVAL,
                            APP_BLE_SCAN_WINDOW);
    transition_to(HOG_CLIENT_SCANNING, ERROR_CODE_SUCCESS);
    gap_start_scan();
}

static void retry_current_policy(void) {
    if (!hci_working) {
        transition_to(HOG_CLIENT_STARTING, ERROR_CODE_COMMAND_DISALLOWED);
        return;
    }
    if (approved_peer_valid) {
        connect_to_peer(&approved_peer);
    } else {
        start_scan();
    }
}

static void schedule_backoff(uint8_t status) {
    cancel_state_timer();
    transition_to(HOG_CLIENT_BACKOFF, status);
    arm_state_timer(retry_delay_ms);
    if (retry_delay_ms < APP_BLE_RETRY_MAX_MS) {
        uint32_t next_delay = retry_delay_ms * 2u;
        if (next_delay < retry_delay_ms ||
            next_delay > APP_BLE_RETRY_MAX_MS) {
            next_delay = APP_BLE_RETRY_MAX_MS;
        }
        retry_delay_ms = next_delay;
    }
}

static void request_disconnect(uint8_t status) {
    cancel_state_timer();
    end_source_session();
    transition_to(HOG_CLIENT_DISCONNECTING, status);
    if (connection_handle == HCI_CON_HANDLE_INVALID) {
        clear_connection();
        schedule_backoff(status);
        return;
    }
    const uint8_t disconnect_status = gap_disconnect(connection_handle);
    if (disconnect_status == ERROR_CODE_UNKNOWN_CONNECTION_IDENTIFIER) {
        clear_connection();
        schedule_backoff(disconnect_status);
    }
}

static void track_and_disconnect_unexpected(hci_con_handle_t handle,
                                            uint8_t status) {
    if (connection_handle != HCI_CON_HANDLE_INVALID) {
        // A legitimate session already owns the global handle. The stray
        // link is still closed immediately, but must not replace it.
        (void)gap_disconnect(handle);
        return;
    }
    cancel_state_timer();
    connection_handle = handle;
    source_session_active = true;
    request_disconnect(status);
}

static void start_hids_setup(void);

static void security_complete(uint8_t status) {
    if (status != ERROR_CODE_SUCCESS) {
        request_disconnect(status);
        return;
    }
    peer_identity_t identity;
    if (!authenticated_sc_peer_for_connection(&identity)) {
        request_disconnect(ERROR_CODE_AUTHENTICATION_FAILURE);
        return;
    }
    if (target_is_approved && !peer_equal(&identity, &approved_peer)) {
        request_disconnect(ERROR_CODE_AUTHENTICATION_FAILURE);
        return;
    }
    link_secure = true;
    target_peer = identity;
    target_peer_valid = true;
    start_hids_setup();
}

static void handle_hids_event(uint8_t packet_type, uint16_t channel,
                              uint8_t *packet, uint16_t size) {
    (void)packet_type;
    (void)channel;
    if (packet == NULL || size < 3u ||
        hci_event_packet_get_type(packet) != HCI_EVENT_GATTSERVICE_META) {
        return;
    }

    const uint8_t subevent =
        hci_event_gattservice_meta_get_subevent_code(packet);
    switch (subevent) {
        case GATTSERVICE_SUBEVENT_HID_SERVICE_CONNECTED: {
            if (size < 8u) return;
            const uint16_t event_cid =
                gattservice_subevent_hid_service_connected_get_hids_cid(
                    packet);
            if (client_state != HOG_CLIENT_HIDS_SETUP || hids_cid == 0u ||
                event_cid != hids_cid) {
                return;
            }
            cancel_state_timer();
            const uint8_t status =
                gattservice_subevent_hid_service_connected_get_status(packet);
            if (status != ERROR_CODE_SUCCESS ||
                gattservice_subevent_hid_service_connected_get_num_instances(
                    packet) == 0u) {
                hids_cid = 0u;
                request_disconnect(status != ERROR_CODE_SUCCESS
                                       ? status
                                       : ERROR_CODE_UNSUPPORTED_FEATURE_OR_PARAMETER_VALUE);
                return;
            }
            if (!link_secure) {
                request_disconnect(ERROR_CODE_AUTHENTICATION_FAILURE);
                return;
            }

            if (!approved_peer_valid && target_peer_valid) {
                if (!store_approved_peer_if_changed(&target_peer)) {
                    request_disconnect(ERROR_CODE_MEMORY_CAPACITY_EXCEEDED);
                    return;
                }
            }
            retry_delay_ms = APP_BLE_RETRY_INITIAL_MS;
            hid_keyboard_decoder_reset(&keyboard_decoder);
            transition_to(HOG_CLIENT_READY, ERROR_CODE_SUCCESS);
            return;
        }

        case GATTSERVICE_SUBEVENT_HID_SERVICE_DISCONNECTED: {
            if (size < 5u) return;
            const uint16_t event_cid =
                gattservice_subevent_hid_service_disconnected_get_hids_cid(
                    packet);
            if (hids_cid == 0u || event_cid != hids_cid) return;
            hids_cid = 0u;
            end_source_session();
            // HIDS emits this synchronously while dispatching the underlying
            // HCI disconnection event. The HCI handler completes recovery.
            transition_to(HOG_CLIENT_DISCONNECTING,
                          ERROR_CODE_REMOTE_USER_TERMINATED_CONNECTION);
            return;
        }

        case GATTSERVICE_SUBEVENT_HID_REPORT: {
            if (size < 10u || client_state != HOG_CLIENT_READY ||
                hids_cid == 0u || !link_secure ||
                connection_handle == HCI_CON_HANDLE_INVALID) {
                return;
            }
            const uint16_t event_cid =
                gattservice_subevent_hid_report_get_hids_cid(packet);
            if (event_cid != hids_cid) return;

            const uint16_t report_len =
                gattservice_subevent_hid_report_get_report_len(packet);
            if (report_len == 0u || report_len > (uint16_t)(size - 9u)) {
                return;
            }
            const uint8_t service_index =
                gattservice_subevent_hid_report_get_service_index(packet);
            const uint8_t report_id =
                gattservice_subevent_hid_report_get_report_id(packet);
            const uint8_t *descriptor =
                hids_host_descriptor_storage_get_descriptor_data(
                    hids_cid, service_index);
            const uint16_t descriptor_len =
                hids_host_descriptor_storage_get_descriptor_len(
                    hids_cid, service_index);
            const keyboard_state_t *state;
            const hid_keyboard_decode_result_t result =
                hid_keyboard_decoder_decode(
                    &keyboard_decoder, service_index, report_id, descriptor,
                    descriptor_len,
                    gattservice_subevent_hid_report_get_report(packet),
                    report_len, &state);
            if (result == HID_KEYBOARD_DECODE_CHANGED &&
                client_callbacks.keyboard_state_changed != NULL) {
                client_callbacks.keyboard_state_changed(
                    state, source_epoch, client_callbacks.context);
            }
            return;
        }

        default:
            return;
    }
}

static void start_hids_setup(void) {
    if (client_state != HOG_CLIENT_SECURING ||
        connection_handle == HCI_CON_HANDLE_INVALID || !link_secure) {
        return;
    }
    cancel_state_timer();
    hids_cid = 0u;
    transition_to(HOG_CLIENT_HIDS_SETUP, ERROR_CODE_SUCCESS);
    const uint8_t status = hids_host_connect(
        connection_handle, handle_hids_event, HID_PROTOCOL_MODE_REPORT,
        &hids_cid);
    if (status != ERROR_CODE_SUCCESS) {
        hids_cid = 0u;
        request_disconnect(status);
        return;
    }
    arm_state_timer(APP_BLE_HIDS_TIMEOUT_MS);
}

static void handle_connection_complete(const uint8_t *packet,
                                       uint16_t size) {
    if (size < 6u) return;
    const uint8_t status =
        gap_subevent_le_connection_complete_get_status(packet);

    if (status != ERROR_CODE_SUCCESS) {
        if (client_state == HOG_CLIENT_CONNECTING ||
            client_state == HOG_CLIENT_CANCELING_CONNECT) {
            cancel_state_timer();
            clear_connection();
            schedule_backoff(status);
        }
        return;
    }

    const hci_con_handle_t event_handle =
        gap_subevent_le_connection_complete_get_connection_handle(packet);
    if (client_state != HOG_CLIENT_CONNECTING &&
        client_state != HOG_CLIENT_CANCELING_CONNECT) {
        // Completion from an earlier canceled attempt. Never let it become an
        // untracked input source.
        track_and_disconnect_unexpected(event_handle,
                                        ERROR_CODE_CONNECTION_TIMEOUT);
        return;
    }

    peer_identity_t event_peer;
    if (size < 14u) {
        track_and_disconnect_unexpected(
            event_handle, ERROR_CODE_INVALID_HCI_COMMAND_PARAMETERS);
        return;
    }
    event_peer.address_type =
        (bd_addr_type_t)
            gap_subevent_le_connection_complete_get_peer_address_type(packet);
    gap_subevent_le_connection_complete_get_peer_address(packet,
                                                         event_peer.address);

    if (!target_peer_valid || !address_type_valid(event_peer.address_type) ||
        !peer_equal(&target_peer, &event_peer)) {
        track_and_disconnect_unexpected(event_handle,
                                        ERROR_CODE_AUTHENTICATION_FAILURE);
        return;
    }

    cancel_state_timer();
    connection_handle = event_handle;
    source_session_active = true;
    hid_keyboard_decoder_reset(&keyboard_decoder);

    if (client_state == HOG_CLIENT_CANCELING_CONNECT) {
        // The controller completed after cancel won the race. Track the handle
        // until its disconnection event so it cannot be orphaned.
        request_disconnect(ERROR_CODE_CONNECTION_TIMEOUT);
        return;
    }

    transition_to(HOG_CLIENT_SECURING, ERROR_CODE_SUCCESS);
    sm_request_pairing(connection_handle);
    arm_state_timer(APP_BLE_SECURITY_TIMEOUT_MS);
}

static bool security_stage_active(void) {
    return client_state == HOG_CLIENT_SECURING ||
           client_state == HOG_CLIENT_HIDS_SETUP ||
           client_state == HOG_CLIENT_READY;
}

static void handle_encryption_change(hci_con_handle_t handle, uint8_t status,
                                     uint8_t enabled, uint8_t key_size) {
    if (!security_stage_active() || handle != connection_handle) return;
    if (status == ERROR_CODE_SUCCESS && enabled != 0u &&
        (key_size == 0u || key_size == 16u)) {
        return;
    }
    link_secure = false;
    request_disconnect(status != ERROR_CODE_SUCCESS
                           ? status
                           : ERROR_CODE_AUTHENTICATION_FAILURE);
}

static void handle_hci_event(uint8_t packet_type, uint16_t channel,
                             uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || packet == NULL || size == 0u) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case BTSTACK_EVENT_STATE:
            if (size < 3u) return;
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                if (hci_working) return;
                hci_working = true;
                load_approved_peer();
                retry_current_policy();
            } else if (hci_working) {
                hci_working = false;
                cancel_state_timer();
                end_source_session();
                clear_connection();
                transition_to(HOG_CLIENT_STARTING,
                              ERROR_CODE_HARDWARE_FAILURE);
            }
            return;

        case GAP_EVENT_ADVERTISING_REPORT: {
            if (client_state != HOG_CLIENT_SCANNING ||
                approved_peer_valid ||
                !advertising_report_contains_hid(packet, size)) {
                return;
            }
            peer_identity_t candidate;
            candidate.address_type =
                (bd_addr_type_t)
                    gap_event_advertising_report_get_address_type(packet);
            if (!address_type_valid(candidate.address_type)) return;
            gap_event_advertising_report_get_address(packet,
                                                     candidate.address);
            gap_stop_scan();
            connect_to_peer(&candidate);
            return;
        }

        case HCI_EVENT_META_GAP:
            if (size >= 3u &&
                hci_event_gap_meta_get_subevent_code(packet) ==
                    GAP_SUBEVENT_LE_CONNECTION_COMPLETE) {
                handle_connection_complete(packet, size);
            }
            return;

        case HCI_EVENT_DISCONNECTION_COMPLETE: {
            if (size < 6u) return;
            const hci_con_handle_t event_handle =
                hci_event_disconnection_complete_get_connection_handle(
                    packet);
            if (connection_handle == HCI_CON_HANDLE_INVALID ||
                event_handle != connection_handle) {
                return;
            }
            const uint8_t reason =
                hci_event_disconnection_complete_get_reason(packet);
            cancel_state_timer();
            end_source_session();
            clear_connection();
            schedule_backoff(reason);
            return;
        }

        case HCI_EVENT_ENCRYPTION_CHANGE:
            if (size >= 6u) {
                handle_encryption_change(
                    hci_event_encryption_change_get_connection_handle(packet),
                    hci_event_encryption_change_get_status(packet),
                    hci_event_encryption_change_get_encryption_enabled(packet),
                    0u);
            }
            return;

        case HCI_EVENT_ENCRYPTION_CHANGE_V2:
            if (size >= 7u) {
                handle_encryption_change(
                    hci_event_encryption_change_v2_get_connection_handle(
                        packet),
                    hci_event_encryption_change_v2_get_status(packet),
                    hci_event_encryption_change_v2_get_encryption_enabled(
                        packet),
                    hci_event_encryption_change_v2_get_encryption_key_size(
                        packet));
            }
            return;

        case HCI_EVENT_ENCRYPTION_KEY_REFRESH_COMPLETE:
            if (size >= 5u && security_stage_active() &&
                hci_event_encryption_key_refresh_complete_get_handle(packet) ==
                    connection_handle &&
                hci_event_encryption_key_refresh_complete_get_status(packet) !=
                    ERROR_CODE_SUCCESS) {
                link_secure = false;
                request_disconnect(
                    hci_event_encryption_key_refresh_complete_get_status(
                        packet));
            }
            return;

        default:
            return;
    }
}

static bool sm_event_matches_current(hci_con_handle_t handle) {
    return client_state == HOG_CLIENT_SECURING &&
           connection_handle != HCI_CON_HANDLE_INVALID &&
           handle == connection_handle;
}

static void reject_pairing_method(hci_con_handle_t handle) {
    if (!sm_event_matches_current(handle)) return;
    sm_bonding_decline(handle);
    request_disconnect(ERROR_CODE_AUTHENTICATION_FAILURE);
}

static void handle_sm_event(uint8_t packet_type, uint16_t channel,
                            uint8_t *packet, uint16_t size) {
    (void)channel;
    if (packet_type != HCI_EVENT_PACKET || packet == NULL || size < 4u) {
        return;
    }

    switch (hci_event_packet_get_type(packet)) {
        case SM_EVENT_JUST_WORKS_REQUEST:
            reject_pairing_method(
                sm_event_just_works_request_get_handle(packet));
            return;

        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            reject_pairing_method(
                sm_event_numeric_comparison_request_get_handle(packet));
            return;

        case SM_EVENT_PASSKEY_INPUT_NUMBER:
            // This bridge is deliberately DisplayOnly: the fixed number is
            // entered on the keyboard during first pairing.
            reject_pairing_method(
                sm_event_passkey_input_number_get_handle(packet));
            return;

        case SM_EVENT_PASSKEY_DISPLAY_NUMBER: {
            const hci_con_handle_t handle =
                sm_event_passkey_display_number_get_handle(packet);
            if (!sm_event_matches_current(handle)) return;
            const uint32_t passkey =
                sm_event_passkey_display_number_get_passkey(packet);
            if (passkey != APP_PAIRING_PASSKEY) {
                request_disconnect(ERROR_CODE_AUTHENTICATION_FAILURE);
                return;
            }
            if (passkey != APP_PAIRING_PASSKEY) {
                reject_pairing_method(handle);
                return;
            }
            printf("BLE pairing passkey: %06" PRIu32
                   " (enter it on the keyboard)\n",
                   passkey);
            return;
        }

        case SM_EVENT_PAIRING_COMPLETE: {
            const hci_con_handle_t handle =
                sm_event_pairing_complete_get_handle(packet);
            if (!sm_event_matches_current(handle)) return;
            security_complete(
                sm_event_pairing_complete_get_status(packet));
            return;
        }

        case SM_EVENT_REENCRYPTION_COMPLETE: {
            const hci_con_handle_t handle =
                sm_event_reencryption_complete_get_handle(packet);
            if (!sm_event_matches_current(handle)) return;
            // A completion event is not success by itself; status and the
            // resulting authenticated security level are both mandatory.
            security_complete(
                sm_event_reencryption_complete_get_status(packet));
            return;
        }

        default:
            return;
    }
}

static void handle_state_timer(btstack_timer_source_t *timer) {
    (void)timer;
    switch (client_state) {
        case HOG_CLIENT_CONNECTING: {
            transition_to(HOG_CLIENT_CANCELING_CONNECT,
                          ERROR_CODE_CONNECTION_TIMEOUT);
            const uint8_t status = gap_connect_cancel();
            if (client_state != HOG_CLIENT_CANCELING_CONNECT) {
                // Cancellation can synchronously emit its failed connection
                // completion event.
                return;
            }
            if (status != ERROR_CODE_SUCCESS) {
                schedule_backoff(status);
            } else {
                arm_state_timer(APP_BLE_CONNECT_TIMEOUT_MS);
            }
            return;
        }

        case HOG_CLIENT_CANCELING_CONNECT:
            // A later successful completion is caught globally and
            // disconnected even after recovery has moved on.
            clear_connection();
            schedule_backoff(ERROR_CODE_CONNECTION_TIMEOUT);
            return;

        case HOG_CLIENT_SECURING:
            request_disconnect(ERROR_CODE_CONNECTION_TIMEOUT);
            return;

        case HOG_CLIENT_HIDS_SETUP:
            if (hids_cid != 0u) {
                (void)hids_host_disconnect(hids_cid);
                hids_cid = 0u;
            }
            request_disconnect(ERROR_CODE_CONNECTION_TIMEOUT);
            return;

        case HOG_CLIENT_BACKOFF:
            retry_current_policy();
            return;

        default:
            return;
    }
}

void hog_client_init(const hog_client_callbacks_t *callbacks) {
    if (callbacks != NULL) {
        client_callbacks = *callbacks;
    } else {
        memset(&client_callbacks, 0, sizeof(client_callbacks));
    }

    if (initialized) {
        transition_to(client_state, ERROR_CODE_SUCCESS);
        return;
    }
    initialized = true;
    client_state = HOG_CLIENT_STARTING;
    source_epoch = 1u;
    retry_delay_ms = APP_BLE_RETRY_INITIAL_MS;
    hid_keyboard_decoder_init(&keyboard_decoder);
    btstack_run_loop_set_timer_handler(&state_timer, handle_state_timer);
    transition_to(HOG_CLIENT_STARTING, ERROR_CODE_SUCCESS);

    l2cap_init();
    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_use_fixed_passkey_in_display_role(APP_PAIRING_PASSKEY);
    sm_set_secure_connections_only_mode(true);
    sm_set_authentication_requirements(
        SM_AUTHREQ_SECURE_CONNECTION | SM_AUTHREQ_MITM_PROTECTION |
        SM_AUTHREQ_BONDING);
    sm_allow_ltk_reconstruction_without_le_device_db_entry(0);

    gatt_client_init();
    gatt_client_set_required_security_level(LEVEL_3);
    hids_host_init(hid_descriptor_storage,
                   sizeof(hid_descriptor_storage));

    gap_set_connection_parameters(
        APP_BLE_SCAN_INTERVAL, APP_BLE_SCAN_WINDOW,
        APP_BLE_CONNECTION_INTERVAL, APP_BLE_CONNECTION_INTERVAL,
        APP_BLE_CONNECTION_LATENCY, APP_BLE_SUPERVISION_TIMEOUT, 0u, 0u);

    hci_event_registration.callback = handle_hci_event;
    hci_add_event_handler(&hci_event_registration);
    sm_event_registration.callback = handle_sm_event;
    sm_add_event_handler(&sm_event_registration);

    hci_power_control(HCI_POWER_ON);
}

void hog_client_forget_peer(void) {
    if (!initialized) return;

    peer_identity_t old_approved = approved_peer;
    const bool had_approved = approved_peer_valid;
    approved_peer_valid = false;
    delete_approved_peer_record();
    if (had_approved) {
        gap_delete_bonding(old_approved.address_type,
                           old_approved.address);
    }

    peer_identity_t active_identity;
    if (peer_identity_for_connection(&active_identity) &&
        (!had_approved || !peer_equal(&old_approved, &active_identity))) {
        gap_delete_bonding(active_identity.address_type,
                           active_identity.address);
    }

    retry_delay_ms = APP_BLE_RETRY_INITIAL_MS;
    switch (client_state) {
        case HOG_CLIENT_STARTING:
            return;

        case HOG_CLIENT_SCANNING:
            gap_stop_scan();
            start_scan();
            return;

        case HOG_CLIENT_CONNECTING:
            cancel_state_timer();
            transition_to(HOG_CLIENT_CANCELING_CONNECT,
                          ERROR_CODE_SUCCESS);
            const uint8_t status = gap_connect_cancel();
            if (client_state != HOG_CLIENT_CANCELING_CONNECT) return;
            if (status != ERROR_CODE_SUCCESS) {
                schedule_backoff(status);
            } else {
                arm_state_timer(APP_BLE_CONNECT_TIMEOUT_MS);
            }
            return;

        case HOG_CLIENT_CANCELING_CONNECT:
            return;

        case HOG_CLIENT_SECURING:
        case HOG_CLIENT_HIDS_SETUP:
        case HOG_CLIENT_READY:
        case HOG_CLIENT_DISCONNECTING:
            request_disconnect(ERROR_CODE_CONNECTION_TERMINATED_BY_LOCAL_HOST);
            return;

        case HOG_CLIENT_BACKOFF:
        case HOG_CLIENT_UNPROVISIONED:
            start_scan();
            return;
    }
}

hog_client_state_t hog_client_state(void) {
    return client_state;
}

uint32_t hog_client_epoch(void) {
    return source_epoch;
}
