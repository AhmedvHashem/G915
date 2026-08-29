# G915 Pico 2 W Bridge

Firmware that turns a Raspberry Pi Pico 2 W into a Bluetooth-to-USB bridge for
the Logitech G915 keyboard. The Pico connects to the keyboard over Bluetooth
Low Energy HID and exposes it to the host computer as a USB boot keyboard.

## Quick start

1. Copy `firmware/g915_pico2w_bridge/g915_pico2w_bridge.uf2` to a Pico 2 W in
   BOOTSEL mode.
2. Hold the G915 Bluetooth button for three seconds until it flashes rapidly.
3. Type `123456` on the keyboard and press Enter to complete pairing.

The Pico remembers the keyboard and reconnects automatically. Its LED blinks
while disconnected and stays on when the bridge is ready.

## Build

Install the Raspberry Pi Pico SDK and Ninja, then run:

```powershell
cd firmware/g915_pico2w_bridge
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```

The generated UF2 file will be in the `build` directory. See the
[firmware README](firmware/g915_pico2w_bridge/README.md) for UART diagnostics
and additional technical details.

## Repository layout

- `firmware/g915_pico2w_bridge` — main BLE HID to USB HID bridge firmware.
- `firmware/g915_pico2w_blink` — basic Pico 2 W LED test firmware.
