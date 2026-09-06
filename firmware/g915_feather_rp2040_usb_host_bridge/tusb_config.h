#pragma once

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the Pico SDK
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                    OPT_OS_PICO
#endif
#if BRIDGE_USB_CDC_DIAGNOSTICS
#define CFG_TUSB_DEBUG_PRINTF          bridge_debug_printf
#define CFG_TUD_LOG_LEVEL              0
#ifndef BRIDGE_USB_HOST_LOG_LEVEL
#define BRIDGE_USB_HOST_LOG_LEVEL      0
#endif
#define CFG_TUH_LOG_LEVEL              BRIDGE_USB_HOST_LOG_LEVEL
#endif

// Native RP2040 USB is the PS5-facing HID device. PIO USB is the receiver-
// facing host controller on TinyUSB root port 1.
#define CFG_TUD_ENABLED                1
#define CFG_TUD_MAX_SPEED              OPT_MODE_DEFAULT_SPEED
#define CFG_TUH_ENABLED                1
#define CFG_TUH_MAX_SPEED              OPT_MODE_DEFAULT_SPEED
#define CFG_TUH_RPI_PIO_USB            1
#define CFG_TUSB_RHPORT0_MODE          (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_RHPORT1_MODE          (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

#define CFG_TUSB_MEM_ALIGN             __attribute__((aligned(4)))
#define CFG_TUD_MEM_ALIGN              __attribute__((aligned(4)))
#define CFG_TUH_MEM_ALIGN              __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE         64
#define CFG_TUD_HID                    1
#define CFG_TUD_CDC                    BRIDGE_USB_CDC_DIAGNOSTICS
#define CFG_TUD_MSC                    0
#define CFG_TUD_MIDI                   0
#define CFG_TUD_VENDOR                 0
#define CFG_TUD_HID_EP_BUFSIZE         16
#if BRIDGE_USB_CDC_DIAGNOSTICS
#define CFG_TUD_CDC_RX_BUFSIZE         64
// Large enough for the multi-line diagnostics report emitted once a second.
#define CFG_TUD_CDC_TX_BUFSIZE         1024
#define CFG_TUD_CDC_EP_BUFSIZE         64
#endif

// A G915 LIGHTSPEED receiver exposes three HID interfaces behind a long
// configuration descriptor. TinyUSB's generic HID host class is disabled; the
// application-defined driver in host/boot_keyboard_host.c claims the single
// boot-keyboard interface and leaves the others unconfigured.
#define CFG_TUH_ENUMERATION_BUFSIZE    512
#define CFG_TUH_DEVICE_MAX             1
#define CFG_TUH_HUB                    0
#define CFG_TUH_HID                    0
