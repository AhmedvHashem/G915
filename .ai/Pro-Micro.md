# Pro Micro — G915 USB Bridge

## Status

**Supported low-cost alternative; not implemented or electrically validated in
this repository.**

This design requires an ATmega32U4 Pro Micro and a MAX3421E Mini USB Host
Shield. The shield hosts the G915 Lightspeed receiver over USB, while the Pro
Micro's native USB controller presents a boot keyboard to the PC or PS5.

Related plans:

- [Raspberry Pi Pico 2 W](Raspberry-Pi-Pico-2-W.md) — implemented reference
- [Adafruit Feather RP2040 USB Host](Adafruit-Feather-RP2040-USB-Host.md) — integrated receiver-host alternative

## Architecture

```text
G915 ──Lightspeed──▶ Logitech receiver ──USB-A──▶ MAX3421E shield
                                                        │
                                                        │ SPI
                                                        ▼
                                                 ATmega32U4
                                                        │ native USB device
                                                        ▼
                                               Pro Micro ──▶ PC or PS5
```

The MAX3421E provides the USB host controller that the ATmega32U4 lacks. The
ATmega32U4 native USB peripheral remains dedicated to the console-facing HID
device.

## Hardware

- ATmega32U4 Pro Micro, **5 V / 16 MHz**, with a data-capable USB connector.
- MAX3421E Mini USB Host Shield with USB-A socket.
- Logitech G915 Lightspeed receiver.
- Data cable to the PC or PS5.
- Wire, soldering equipment, and a multimeter.

The Mini Host Shield is normally shaped for a Pro Mini, not a Pro Micro. Expect
point-to-point wiring rather than direct stacking.

## Mandatory electrical inspection

Mini Host Shield clones vary. Confirm these facts for the exact board before
connecting it:

| Check | Required result |
|---|---|
| MAX3421E supply | approximately 3.3 V, never 5 V |
| SPI inputs | 5 V tolerant through onboard level shifting, or externally shifted |
| USB-A VBUS | approximately 5 V after any required solder jumper is closed |
| Ground | shield, Pro Micro, receiver, and USB grounds are common |
| Power input | matches the labels and schematic of the exact clone |

Do not infer voltage tolerance from connector shape or seller photos. Some
boards accept 5 V and regulate/shift it; others are native 3.3 V designs.

Before inserting the Logitech receiver:

1. Check for a short between power and ground with power disconnected.
2. Power the Pro Micro from a PC.
3. Measure the MAX3421E supply.
4. Measure USB-A VBUS.
5. Verify that no signal exceeds the shield's input limits.

## Wiring

This is the expected logical wiring for conventional boards; the markings on
the actual boards take precedence.

| Mini Host Shield | Pro Micro | Function |
|---|---|---|
| MOSI | pin 16 / MOSI | SPI controller to MAX3421E |
| MISO | pin 14 / MISO | SPI MAX3421E to controller |
| SCK | pin 15 / SCK | SPI clock |
| SS | pin 10 | MAX3421E chip select |
| INT | pin 9 | MAX3421E interrupt |
| RESET | RST | recommended reset connection |
| GND | GND | common ground |
| shield power | verified board-specific input | shield and USB host power |

If the library or selected shield revision uses different SS or INT pins,
change both the wiring and firmware configuration together.

## Toolchain and dependencies

Start with the Arduino AVR toolchain:

- board definition for an ATmega32U4 Pro Micro;
- **5 V / 16 MHz** selection;
- USB Host Shield Library 2.0 for MAX3421E;
- a controllable ATmega32U4 boot-keyboard implementation.

NicoHood HID Project is suitable for initial USB-device experiments. The final
build may need a small custom HID class or LUFA so it can expose a single
boot-keyboard interface without CDC or report IDs.

Never leave this in production firmware:

```cpp
while (!Serial) { }
```

The PS5 will not open a USB serial terminal, so waiting for one can prevent the
bridge from starting.

## Planned repository layout

Create the implementation under:

```text
firmware/pro_micro/
  pro_micro.ino
  README.md
```

Keep board-specific USB descriptors close to the firmware instead of linking
to removed historical files.

## Host-side proof

Prove the shield before writing relay code:

1. Run the USB Host Shield Library `board_qc` example.
2. Confirm SPI communication and the MAX3421E oscillator test pass.
3. Run `USB_desc` with the Lightspeed receiver attached.
4. Save the device, configuration, interface, and endpoint descriptors.
5. Run `USBHIDBootKbd` and verify ordinary key down and release events.

If the receiver enumerates but the boot-keyboard example cannot bind, use
`HIDUniversal` with a parser that selects the keyboard report from the
receiver's composite layout. Do not consume the HID++ vendor interfaces in
version 1.

## Device-side proof

With the receiver removed, make the Pro Micro act as a simple keyboard to a PC:

1. Enumerate as a keyboard.
2. Send `00 00 04 00 00 00 00 00` for `a` down.
3. Send eight zero bytes for release.
4. Verify exactly one character and no stuck key.
5. Inspect the USB descriptor from the host.

The final desired USB device matches the proven Pico behavior:

- one HID boot-keyboard interface;
- no report ID;
- eight-byte input report;
- no CDC serial interface;
- no unrelated USB interfaces;
- project identity `VID 0xCAFE`, `PID 0x4004` unless compatibility testing
  requires a change.

## Relay design

Do not translate reports through ASCII or `Keyboard.write()`. That would lose
releases, modifiers, non-printable keys, and simultaneous key state.

Normalize and forward the full boot-keyboard report:

```text
byte 0     modifier bitmap
byte 1     reserved; force to zero
bytes 2-7  six key usages
```

Use a custom `HIDReportParser` on the host side. Remove a report-ID prefix only
when the selected receiver report actually includes one. Require an eight-byte
body, store it in a small pending slot, and send it when the native USB IN
endpoint is ready.

The cooperative main loop must call `Usb.Task()` frequently. Avoid dynamic
allocation, large buffers, and verbose strings: the ATmega32U4 has only 2.5 KiB
of SRAM.

Send an all-zero report when the receiver is removed, its configuration is
released, or the host controller restarts. Do not use an inactivity timeout to
release keys.

## Bring-up plan

1. Validate voltages and VBUS with a multimeter.
2. Pass the MAX3421E `board_qc` test.
3. Enumerate the receiver with `USB_desc`.
4. Receive keys with `USBHIDBootKbd` or a selected `HIDUniversal` parser.
5. Send a synthetic keyboard report from the Pro Micro to a PC.
6. Relay the G915 through both halves into a PC.
7. Test receiver removal, keyboard power-off, and Lightspeed switching without
   stuck keys.
8. Build a HID-only production image.
9. Test typing on the PS5.
10. Soak test rapid typing, modifiers, six held keys, sleep, and reconnect.

Do not diagnose the PS5 until step 6 works reliably on a PC.

## Diagnostics

Use a separate serial-enabled PC debug build. The production build should use
the onboard LED and remain HID-only.

| LED pattern | Meaning |
|---|---|
| slow blink | MAX3421E initialization failure |
| off | host running without a selected keyboard interface |
| solid | receiver keyboard interface active |
| fast blink | malformed report or fatal USB error |

Useful debug output includes receiver VID/PID, interface number, subclass,
protocol, endpoint, selected report length, and release/re-enumeration events.

## Limitations

- The receiver is captive in the bridge.
- Shield quality, power design, and level shifting vary by clone.
- ATmega32U4 memory and debugging headroom are limited.
- Version 1 supports standard boot-keyboard keys and six-key rollover.
- Media keys and Logitech G-keys remain out of scope until the main keyboard
  path is proven.
- Full descriptor and VID/PID control may require LUFA or Arduino core changes.

## Completion criteria

This device becomes validated only after recording:

- the exact Pro Micro and shield revisions;
- measured MAX3421E and USB-A voltages;
- final wiring and SS/INT assignments;
- successful receiver enumeration and key reports;
- pure HID-only console-facing descriptors;
- safe disconnect behavior;
- successful PS5 typing;
- reproducible source and a preserved firmware image.

## References

- USB Host Shield Library 2.0
- USB Host Shield `USBHIDBootKbd`, `HIDUniversal`, `USB_desc`, and `board_qc` examples
- NicoHood HID Project
- LUFA ATmega32U4 keyboard device examples
