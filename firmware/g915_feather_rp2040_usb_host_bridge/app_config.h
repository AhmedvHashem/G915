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

// Poll the receiver's keyboard endpoint every this many frames regardless of
// the bInterval it declares. bInterval is the longest interval a host may
// leave between polls, so polling faster is allowed; the receiver simply NAKs
// frames with nothing new. 0 honours the declared interval. Diagnostics report
// the declared value as binterval.
#define APP_USB_HOST_POLL_INTERVAL_FRAMES 1u

// Frame phase lock. The PIO USB host frame (and so the receiver poll at its
// start) is steered to begin this many microseconds before the console's own
// SOF, so a report is armed on the console-facing endpoint just before the
// console polls for it. The receiver poll, its completion, the hop to core 0,
// and arming the endpoint take roughly 100 to 150 us; the rest is margin for
// timing jitter. 0 disables steering and the frame runs at exactly 1 ms.
#define APP_SOF_PHASE_LEAD_US            250u
// Largest change to one frame period while steering. Devices detect suspend
// after 3 ms without bus activity and do not otherwise depend on exact SOF
// spacing, but the step is kept small so the receiver never sees a frame that
// is noticeably short or long.
#define APP_SOF_PHASE_MAX_STEP_US        2u
#define APP_SOF_PHASE_LOCK_TOLERANCE_US  20u
// Without a console SOF for this long the frame period reverts to nominal.
#define APP_SOF_PHASE_REFERENCE_MAX_AGE_US 20000u
