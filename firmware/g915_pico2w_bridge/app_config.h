#pragma once

#include <stdint.h>

// Product policy. Low-level stack capacities remain in btstack_config.h and
// tusb_config.h.
#define APP_STATUS_LED_BLINK_INTERVAL_MS 500u

#define APP_USB_KEY_USAGE_MAX            0x65u
#define APP_USB_FORWARD_SOURCE_ERRORS    0
#define APP_USB_REMOTE_WAKE_DELAY_MS      5u

#define APP_PAIRING_PASSKEY              123456u
#define APP_HID_DESCRIPTOR_STORAGE_SIZE  2048u
#define APP_HID_MAX_REPORT_FRAGMENTS     8u

// BLE interval values are in 1.25 ms units. Scan values are in 0.625 ms
// units, and the supervision timeout is in 10 ms units.
#define APP_BLE_SCAN_INTERVAL            48u
#define APP_BLE_SCAN_WINDOW              48u
#define APP_BLE_CONNECTION_INTERVAL      6u
#define APP_BLE_CONNECTION_LATENCY       0u
#define APP_BLE_SUPERVISION_TIMEOUT      0x0048u

#define APP_BLE_CONNECT_TIMEOUT_MS       10000u
#define APP_BLE_SECURITY_TIMEOUT_MS      30000u
#define APP_BLE_HIDS_TIMEOUT_MS          15000u
#define APP_BLE_RETRY_INITIAL_MS         100u
#define APP_BLE_RETRY_MAX_MS             5000u
