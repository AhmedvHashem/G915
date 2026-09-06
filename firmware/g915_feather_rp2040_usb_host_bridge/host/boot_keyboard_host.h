#pragma once

#include <stdbool.h>
#include <stdint.h>

// Minimal TinyUSB host class driver for one HID boot-protocol keyboard
// interface. It replaces TinyUSB's generic HID host class for this bridge.
//
// - It claims only the first interface with class HID, subclass Boot, and
//   protocol Keyboard, and opens only that interface's interrupt IN endpoint.
//   Every other interface of the device stays unclaimed, so TinyUSB skips it
//   and issues no control transfers for it.
// - Its configuration sequence is SET_IDLE(0) then SET_PROTOCOL(Boot), the
//   same two requests TinyUSB's HID host issues for a boot keyboard. It never
//   requests the HID report descriptor, which the G915 LIGHTSPEED receiver
//   intermittently stalls through Pico-PIO-USB.
// - It re-arms the IN endpoint itself, inside the same transfer callback that
//   delivers a report, so a re-arm can never be forgotten or delayed behind
//   application work.
// - It polls the endpoint every APP_USB_HOST_POLL_INTERVAL_FRAMES frames when
//   that is non-zero, regardless of the bInterval the interface declares.
//
// Every callback below runs on the core that services tuh_task(). The driver
// registers itself through TinyUSB's usbh_app_driver_get_cb() hook and needs
// no explicit initialisation.

#define BOOT_KEYBOARD_REPORT_SIZE 8u

// Invoked for every HID-class interface the enumerator offers, whether or not
// this driver claimed it. `protocol` is the interface's bInterfaceProtocol.
void boot_keyboard_host_interface_cb(uint8_t dev_addr, uint8_t itf_num,
                                     uint8_t protocol, bool claimed);

// The claimed interface finished its configuration and its IN endpoint has
// been armed.
void boot_keyboard_host_mount_cb(uint8_t dev_addr, uint8_t itf_num);

// The device owning a mounted interface was removed. Only follows a mount.
void boot_keyboard_host_umount_cb(uint8_t dev_addr, uint8_t itf_num);

// One report arrived on the claimed interface. `report` is valid only for the
// duration of the call; the buffer is re-armed as soon as the callback returns.
void boot_keyboard_host_report_cb(uint8_t dev_addr, uint8_t itf_num,
                                  const uint8_t *report, uint16_t length);

// Number of failed attempts to re-arm the IN endpoint. Readable from any core.
uint32_t boot_keyboard_host_arm_failure_count(void);

// True while a keyboard interface is claimed and fully configured.
bool boot_keyboard_host_mounted(void);

// bInterval the claimed interface declared for its IN endpoint, in frames.
// Zero while nothing is claimed. Readable from any core.
uint8_t boot_keyboard_host_declared_interval(void);

// Interval actually used to poll that endpoint, in frames. Readable from any
// core.
uint8_t boot_keyboard_host_poll_interval(void);
