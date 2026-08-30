# Raspberry Pi Pico 2 W — G915 Bluetooth-to-USB Bridge

## Status

**Primary supported device. Implemented and validated on real hardware.**

The Logitech G915 pairs with the Pico 2 W over Bluetooth LE HID. The Pico uses
the keyboard's full HID Report Protocol and report map, then forwards only
standard keyboard usages through its native USB controller. The PC or PS5
continues to see a simple USB boot keyboard; unsupported, consumer/media, and
Logitech vendor reports are consumed but not forwarded.

Working firmware and source:

- `../firmware/g915_pico2w_bridge/`
- `../firmware/g915_pico2w_bridge/g915_pico2w_bridge.uf2`

Other supported hardware plans:

- [Adafruit Feather RP2040 USB Host](Adafruit-Feather-RP2040-USB-Host.md)
- [Pro Micro](Pro-Micro.md)

## Architecture

```text
G915 ──Bluetooth LE HOGP──▶ CYW43439 / BTstack
                                      │
                                      │ full HID reports + report map
                                      ▼
                             keyboard report parser
                                      │
                                      │ 8-byte USB boot state
                                      ▼
                              RP2350 report queue
                                      │
                                      ▼
                         TinyUSB native USB device ──▶ PC or PS5
```

Bluetooth and USB use separate hardware paths. The native USB controller stays
in device mode; no USB host port, PIO USB, receiver, shield, or soldering is
required.

## Proven behavior

- The Pico discovers and pairs with the G915 as a BLE HID keyboard.
- Pairing uses a fixed passkey: `123456`.
- The bond is stored in Pico flash and reconnects on later boots.
- Windows enumerates the Pico as one HID keyboard and one USB input device.
- USB identity is `VID 0xCAFE`, `PID 0x4004`.
- USB is HID-only; no CDC serial interface is exposed to the host.
- Normal key presses, modifiers, releases, and simultaneous boot-keyboard keys
  are forwarded.
- A zero report is sent on Bluetooth disconnect to prevent stuck keys.
- The onboard wireless LED is off while disconnected and solid when the
  keyboard is ready.

## Hardware

- Raspberry Pi Pico 2 W.
- Micro-USB data cable from Pico to PC or PS5.
- Logitech G915 with Bluetooth support.
- Optional 3.3 V UART adapter for diagnostics.

UART diagnostics are available at 115200 baud:

| Pico pin | Function |
|---|---|
| GP0 | UART0 TX |
| GP1 | UART0 RX |
| GND | UART ground |

Do not connect a 5 V UART signal to the Pico GPIO pins.

## Firmware design

The source is a standalone Pico SDK C project.

| File | Responsibility |
|---|---|
| `CMakeLists.txt` | Pico 2 W, BTstack, CYW43, TinyUSB, and UART build configuration |
| `hog_report_host.c` | BLE scanning, pairing, bonding, reconnect, full HOGP report parsing |
| `main.c` | report queue, USB forwarding, disconnect release, and status LED |
| `bridge.h` | boundary between Bluetooth and USB translation units |
| `usb_descriptors.c` | single-interface boot-keyboard USB descriptors and identity |
| `tusb_config.h` | HID-only TinyUSB device configuration |
| `btstack_config*.h` | BTstack memory and feature configuration |

BTstack and TinyUSB are kept in separate translation units because both stacks
define overlapping HID names. The BLE host subscribes to all input reports and
uses the G915 report map to extract Keyboard/Keypad usages. Consumer and vendor
reports are ignored without changing the last keyboard state. Parsed keyboard
snapshots enter the USB queue, which retains each report until the HID endpoint
accepts it. If the queue fills, the oldest queued state is discarded so the
newest key state, especially a release, is retained.

## Build

Requirements:

- Raspberry Pi Pico SDK 2.3.0 or a compatible later release.
- ARM GNU toolchain.
- CMake and Ninja.
- Pico SDK host tools such as `pioasm` and `picotool`.

From `firmware/g915_pico2w_bridge`:

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```

The output is `build/g915_pico2w_bridge.uf2`. If CMake attempts to rebuild host
tools with an unsuitable compiler, pass the installed `pioasm_DIR` and
`picotool_DIR` explicitly.

## Flash

1. Disconnect the Pico.
2. Hold **BOOTSEL** while connecting its USB cable.
3. Release BOOTSEL when the `RP2350` drive appears.
4. Copy `g915_pico2w_bridge.uf2` to that drive.
5. The Pico reboots automatically as a USB keyboard.

The production build has no USB serial interface. To reflash it later, use the
BOOTSEL button; the absence of a COM port is expected.

## First pairing

1. Power the flashed Pico from the PC or PS5.
2. Hold the G915 Bluetooth button for about three seconds until it flashes
   rapidly.
3. Type `123456` on the G915 itself.
4. Press Enter.
5. Wait for the Pico LED to become solid.

If another computer or phone captures the keyboard first, disable Bluetooth on
that device and repeat the pairing sequence.

## Validation checklist

Perform tests in this order:

1. Windows or Linux enumerates one boot-keyboard interface.
2. The Pico LED becomes solid after G915 pairing.
3. A text editor receives letters, Shift combinations, Ctrl combinations,
   arrows, function keys, Backspace, and Enter.
4. Holding and releasing a key never leaves it stuck.
5. Turning the keyboard off while a key is held releases all keys.
6. Power cycling the Pico reconnects without pairing again.
7. Moving the Pico cable to the PS5 preserves normal typing.

## Troubleshooting

| Symptom | Action |
|---|---|
| Pico LED remains off | Put the G915 into pairing mode again and type `123456`, then Enter |
| Keyboard reconnects elsewhere | Disable Bluetooth on the previous host during pairing |
| Windows shows no USB keyboard | Try a known data cable and another USB port, then reflash with BOOTSEL |
| Keyboard connects but no keys arrive | Rebuild the release image and confirm the G915 is on its Bluetooth channel |
| A key remains held after a disconnect | Treat as a firmware regression in the disconnect zero-report path |
| Need logs | Attach a 3.3 V UART adapter to GP0/GP1/GND at 115200 baud |

## Current limitations

- Version 2 parses the G915's full BLE HID report map but exposes only the
  standard keyboard fields over USB.
- The USB boot protocol provides six ordinary key slots plus modifier bits.
- Consumer/media reports and Logitech G-key vendor reports are not forwarded.
- Host lock-LED output is accepted by USB but is not yet forwarded to the G915.
- Pairing uses a fixed passkey for a predictable headless setup.

## Maintenance roadmap

1. Keep the current UF2 as the known-good recovery image.
2. Add a controlled method to clear the stored bond without reflashing.
3. Add a pairing timeout/status blink pattern.
4. Forward consumer-control reports only after the keyboard path remains a
   separate, PS5-compatible boot interface.
5. Forward Caps Lock and Num Lock output reports if the G915 accepts them over
   HOGP.
6. Re-test pairing, reconnect, disconnect releases, and PS5 enumeration after
   every BTstack or Pico SDK upgrade.

## References

- Raspberry Pi Pico SDK BTstack `hog_host_demo`
- TinyUSB HID device keyboard examples
- `../firmware/g915_pico2w_bridge/README.md`
