# G915 Bridge for Raspberry Pi Pico 2 W

The G915 connects to the Pico 2 W over Bluetooth LE HID. The Pico exposes one
USB boot-keyboard interface and forwards each eight-byte boot report unchanged.

## First pairing

1. Flash `g915_pico2w_bridge.uf2`.
2. Hold the G915 Bluetooth button for three seconds until it flashes rapidly.
3. Type `123456` on the G915 and press Enter.

The bond and keyboard address are stored in Pico flash. Later boots reconnect
automatically when the keyboard is switched to Bluetooth.

The onboard LED blinks while disconnected and stays solid while ready. USB is
HID-only. Optional diagnostics are on UART0, GP0/TX and GP1/RX, at 115200 baud.
The bridge requests a 7.5 ms Bluetooth connection interval with zero peripheral
latency. UART diagnostics report the initial interval and any later
connection-parameter update requested by the keyboard.

## Build

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w
cmake --build build
```
