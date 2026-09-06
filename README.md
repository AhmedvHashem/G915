# G915 PS5 Bridges

Firmware that turns either a Raspberry Pi Pico 2 W or an Adafruit Feather
RP2040 USB Host into a Logitech G915-to-USB keyboard bridge for PS5. The Pico
2 W receives the keyboard over Bluetooth LE; the Feather receives it through
the G915 LIGHTSPEED USB receiver. Both expose one USB boot-keyboard interface
to the console. The Feather path is the low-latency one: BLE cannot go below a
7.5 ms connection interval, while LIGHTSPEED delivers reports every
millisecond.

## Pico 2 W quick start

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

The keyboard-state and report-pipeline modules are shared by both bridges and
have platform-independent tests that run on the development machine:

```powershell
cd firmware/common
cmake -S tests -B tests/build -G Ninja
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

Each bridge's own `tests` directory runs these and adds its own: the Pico 2 W
bridge adds the BLE decoder tests when `PICO_SDK_PATH` is set, and the Feather
bridge adds its frame phase lock tests.

See the [firmware README](firmware/g915_pico2w_bridge/README.md) for the
architecture, USB lifecycle behavior, security model, and a direct CMake build
command.

## Feather RP2040 USB Host quick start

Build the Feather firmware, flash its UF2, plug the paired G915 LIGHTSPEED
receiver into USB-A, then connect USB-C to the PS5:

```powershell
cd firmware/g915_feather_rp2040_usb_host_bridge
cmake --preset feather-release
cmake --build --preset feather-release
```

The ready-to-flash file is
`firmware/g915_feather_rp2040_usb_host_bridge/build/g915_feather_rp2040_usb_host_bridge.uf2`.
The red LED blinks while waiting for the receiver and stays solid after its
keyboard interface mounts. See the [Feather firmware README](firmware/g915_feather_rp2040_usb_host_bridge/README.md)
for architecture, flashing, and wiring details.

## Repository layout

- `firmware/common` — keyboard state, report pipe, and the USB keyboard
  adapter shared by both bridges, with their host-side tests.
- `firmware/g915_pico2w_bridge` — BLE HID-to-USB HID bridge firmware.
- `firmware/g915_feather_rp2040_usb_host_bridge` — LIGHTSPEED receiver
  USB-host-to-USB-HID bridge firmware for the Adafruit Feather board.
- `firmware/g915_pico2w_blink` — basic Pico 2 W LED test firmware.
