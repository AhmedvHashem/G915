#pragma once

#define APP_STATUS_LED_BLINK_INTERVAL_MS 500u

#define APP_USB_KEY_USAGE_MAX            0x65u
#define APP_USB_FORWARD_SOURCE_ERRORS    0
#define APP_USB_REMOTE_WAKE_DELAY_MS      5u
// Pulse both Shift modifiers once when the USB device remounts. Windows can
// otherwise retain Shift-down if a reset interrupts its release report.
#define APP_USB_STARTUP_MODIFIER_RECOVERY_MASK 0x22u

#define APP_USB_HOST_DP_PIN              16u
#define APP_USB_HOST_DM_PIN              17u
#define APP_USB_HOST_5V_ENABLE_PIN       18u
#define APP_USB_HOST_RHPORT              1u
#define APP_USB_DEVICE_RHPORT            0u
