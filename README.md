# G915 Pico 2 W Bridge

Firmware that turns a Raspberry Pi Pico 2 W into a Bluetooth LE HID-to-USB
bridge for the Logitech G915 keyboard. The Pico connects to the keyboard over
Bluetooth, normalizes its keyboard reports, and exposes one USB boot-keyboard
interface to the host. Consumer/media and Logitech vendor reports are not
forwarded over USB.

## Quick start

1. Build the release firmware (see below).
2. Copy `firmware/g915_pico2w_bridge/build/g915_pico2w_bridge.uf2` to a Pico
   2 W in BOOTSEL mode.
3. On a new or forgotten bridge, hold the G915 Bluetooth button for three
   seconds until it flashes rapidly.
4. Type `123456` on the keyboard and press Enter to complete pairing.

After successful setup, the bridge remembers the approved keyboard and tries
to reconnect to that device on later boots. The onboard LED blinks until the
BLE HID client is ready and then stays on.

To remove the approved keyboard and return to pairing mode, send the exact
lowercase command `forget`, followed by Enter, to UART0 at 115200 baud (GP0/TX,
GP1/RX).

## Build and test

Install the Raspberry Pi Pico SDK, CMake, and Ninja, and set `PICO_SDK_PATH`.
Then run:

```powershell
cd firmware/g915_pico2w_bridge
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

The generated UF2 is written to `firmware/g915_pico2w_bridge/build`. A
`pico2w-debug` configure/build preset is also available.

The platform-independent keyboard-state and report-pipeline tests can run on
the development machine:

```powershell
cd firmware/g915_pico2w_bridge
cmake -S tests -B tests/build -G Ninja
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

See the [firmware README](firmware/g915_pico2w_bridge/README.md) for the
architecture, USB lifecycle behavior, security model, and a direct CMake build
command.

## Repository layout

- `firmware/g915_pico2w_bridge` — BLE HID-to-USB HID bridge firmware.
- `firmware/g915_pico2w_blink` — basic Pico 2 W LED test firmware.
