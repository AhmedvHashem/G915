# G915 PS5 Bridge for Adafruit Feather RP2040 USB Host

This firmware bridges the Logitech G915 LIGHTSPEED USB receiver to a PS5 as a
plain USB boot keyboard. Plug the receiver into the Feather's USB-A host port
and connect the Feather's USB-C port to the PS5.

The RP2040 native USB controller presents the keyboard to the PS5. The USB-A
port uses Pico-PIO-USB on GPIO16 (D+), GPIO17 (D-), and GPIO18 (5 V enable).
The PIO host stack runs on core 1; core 0 owns the PS5-facing device stack and
the existing ordered report pipeline.

Only the receiver's boot-keyboard interface is forwarded. Mouse, media, and
Logitech HID++ vendor interfaces are intentionally ignored so they cannot
interrupt ordinary keyboard input. The resulting PS5-facing interface uses the
same six-key rollover behavior as the Pico 2 W bridge.

## Build

Install Pico SDK 2.3.0 or newer, CMake, Ninja, Git, and the Arm GNU toolchain,
then set `PICO_SDK_PATH`. Configure and build with:

```powershell
cmake --preset feather-release
cmake --build --preset feather-release
```

The first configure downloads the pinned Pico-PIO-USB 0.7.2 dependency into
the ignored build directory. The output is:

```text
build/g915_feather_rp2040_usb_host_bridge.uf2
```

For a debug build, use `feather-debug` for both commands. UART diagnostics are
available at 115200 baud on GPIO0 (TX) and GPIO1 (RX); USB CDC is disabled so
the USB-C interface remains a single keyboard.

## Flash and use

1. Hold **BOOT**, tap **RESET**, then release **BOOT**.
2. Copy `g915_feather_rp2040_usb_host_bridge.uf2` to the `RPI-RP2` drive.
3. Plug the G915 LIGHTSPEED receiver into USB-A.
4. Connect USB-C to the PS5 and select LIGHTSPEED mode on the G915.

The red onboard LED blinks while waiting for the receiver's keyboard interface
and stays solid when it is ready. USB-A 5 V is enabled automatically.

The firmware does not pair or configure the LIGHTSPEED receiver. Pair the
keyboard and receiver with Logitech software first if they are not already
paired.

