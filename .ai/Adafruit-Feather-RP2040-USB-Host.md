# Adafruit Feather RP2040 USB Host — G915 USB Bridge

## Status

**Supported alternative; implementation not currently present or validated in
this repository.**

This design uses the G915 Lightspeed receiver instead of Bluetooth. The Feather
has the two USB roles required by a wired HID proxy: its USB-A connector hosts
the receiver through PIO USB, while its native USB-C connector presents a clean
keyboard to the PC or PS5.

The validated reference behavior and USB report format come from the
[Raspberry Pi Pico 2 W](Raspberry-Pi-Pico-2-W.md) implementation.

Other supported hardware plan:

- [Pro Micro](Pro-Micro.md)

## Architecture

```text
G915 ──Lightspeed──▶ Logitech receiver ──USB-A──▶ PIO USB host
                                                        │
                                                        │ 8-byte boot report
                                                        ▼
                                               RP2040 report queue
                                                        │
                                                        ▼
                                   native USB-C device ──▶ PC or PS5
```

The receiver remains physically attached to the Feather while the bridge is in
use. This path preserves the G915's low-latency Lightspeed link but does not
provide the Pico plan's Bluetooth switching convenience.

## Hardware

Use Adafruit product 5723, **Feather RP2040 with USB Type A Host**.

| Item | Value |
|---|---|
| MCU | RP2040 with 8 MB flash |
| Console-facing port | USB-C, native USB device |
| Receiver-facing port | USB-A, PIO USB host |
| Host D+ | GPIO 16 |
| Host D- | GPIO 17 |
| Host 5 V enable | GPIO 18 using the board definition's active state |
| Required CPU clock | 120 MHz or 240 MHz |
| Status output | onboard red LED; USB host power-good LED |

No external shield or soldering should be required. Use a short USB cable or
extension so the Feather and receiver do not hang directly from the console.

## Toolchain

The simplest implementation path is Arduino with the Earle Philhower RP2040
core:

1. Install the Raspberry Pi Pico/RP2040/RP2350 Arduino core.
2. Install Adafruit TinyUSB Library.
3. Install Pico PIO USB.
4. Select **Adafruit Feather RP2040 USB Host**.
5. Select **120 MHz** CPU speed.
6. Select **Adafruit TinyUSB** as the USB stack.

The 120/240 MHz clock requirement is functional, not cosmetic. PIO USB timing
will not work correctly at the normal 133 MHz RP2040 clock.

## Planned repository layout

Create the implementation under:

```text
firmware/feather_rp2040_usb_host/
  feather_rp2040_usb_host.ino
  README.md
```

Do not revive an unrelated historical Bluetooth project. This firmware should
contain only the receiver-host and USB-keyboard-device bridge.

## Firmware requirements

### USB host side

- Enable the USB-A 5 V supply using the board-defined enable pin and polarity.
- Run Pico PIO USB on the documented host pins.
- Continuously service the TinyUSB host task.
- Enumerate the Logitech receiver's composite interfaces.
- Select only the HID boot-keyboard interface for version 1.
- Request boot protocol explicitly.
- Re-arm report reception after every callback.
- Ignore HID++ vendor interfaces until the keyboard bridge is stable.

### Relay

Forward complete eight-byte keyboard states:

```text
byte 0     modifier bitmap
byte 1     reserved; force to zero
bytes 2-7  six boot-keyboard key usages
```

Use a bounded multicore-safe queue. The host callback should only normalize and
enqueue reports. The native USB-device context should own all calls that send
reports to the PC or PS5. On overflow, preserve the newest complete state so a
release is not lost.

Send an all-zero report when the receiver is removed or the keyboard interface
is released. Never synthesize releases from a short inactivity timeout because
a legitimately held key may generate no new report.

### USB device side

Match the proven Pico output unless testing identifies a reason to differ:

- one HID interface;
- boot subclass and keyboard protocol;
- no report ID;
- eight-byte input reports;
- 2 ms interrupt polling;
- no USB CDC, mass-storage, MIDI, or vendor interface;
- `VID 0xCAFE`, `PID 0x4004` for the project build.

Keeping the console-facing device HID-only avoids exposing the receiver's
Logitech composite interfaces and keeps behavior consistent across supported
boards.

## Bring-up plan

1. **Board and clock:** flash a minimal build at 120 MHz and confirm the
   onboard LED works.
2. **Host power:** enable USB-A 5 V and confirm the power-good LED.
3. **Receiver enumeration:** log the receiver VID/PID and every interface on a
   PC debug build.
4. **Keyboard reports:** confirm repeated key down and release reports; a
   one-key-only result usually means receive was not re-armed.
5. **USB device:** present a synthetic boot keyboard to a PC and send `a` down
   followed by an all-zero release.
6. **Full relay:** type from the G915 through the Feather into a PC.
7. **Lifecycle:** remove/reinsert the receiver, power-cycle the keyboard, and
   verify no stuck keys.
8. **PS5:** move the USB-C cable to the console and test in a text field.
9. **Soak:** test modifiers, six simultaneous keys, rapid typing, and at least
   one hour of operation.

Do not debug console compatibility until the complete bridge works reliably on
a PC.

## Field diagnostics

Recommended LED states:

| Pattern | Meaning |
|---|---|
| fast blink | unsupported clock or fatal initialization error |
| off | host active, no keyboard interface |
| solid | receiver keyboard interface active |
| brief pulse | report received, for debug builds only |

Use a separate PC debug build for serial logs. The production console build
should disable USB CDC so it stays a pure keyboard.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No USB-A power-good LED | wrong board selection or 5 V enable not asserted |
| Power is present but receiver never mounts | wrong CPU clock or PIO USB configuration |
| Receiver mounts only as protocol 0 interfaces | composite selection or report-descriptor parsing is required |
| Exactly one key arrives | missing host receive re-arm |
| Short or prefixed reports | boot protocol was not applied, or the selected report includes an ID |
| Keys remain held after receiver removal | missing zero report in unmount handling |
| PC works but PS5 does not | compare descriptors and identity with the proven Pico implementation |

## Limitations

- The Lightspeed receiver is captive in the bridge while it is used.
- Version 1 targets standard boot-keyboard keys and six-key rollover.
- Media keys, G-keys, and Logitech HID++ are deferred.
- PIO USB and two USB stacks make this firmware more timing-sensitive than the
  Pico Bluetooth implementation.
- The design depends on the receiver exposing or accepting a boot-keyboard
  interface. Otherwise its report descriptor must be parsed.

## Completion criteria

This device becomes validated when all of the following are recorded:

- exact Arduino core and library versions;
- successful receiver enumeration;
- complete down/release forwarding on a PC;
- safe receiver removal without stuck keys;
- pure HID-only USB descriptors;
- successful PS5 typing;
- a reproducible source build and preserved UF2.

## References

- Adafruit Feather RP2040 USB Host product 5723 documentation
- Adafruit TinyUSB Arduino examples
- Pico PIO USB project
- Adafruit HID remapper examples
