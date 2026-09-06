# G915 PS5 Bridge for Adafruit Feather RP2040 USB Host

This firmware bridges the Logitech G915 LIGHTSPEED USB receiver to a PS5 as a
plain USB boot keyboard. Plug the receiver into the Feather's USB-A host port
and connect the Feather's USB-C port to the PS5.

The RP2040 native USB controller presents the keyboard to the PS5. The USB-A
port uses Pico-PIO-USB on GPIO16 (D+), GPIO17 (D-), and GPIO18 (5 V enable).
The PIO host stack runs on core 1; core 0 owns the PS5-facing device stack and
the ordered report pipeline, which is shared with the Pico 2 W bridge and lives
in `firmware/common`.

Only the receiver's boot-keyboard interface is forwarded. Mouse, media, and
Logitech HID++ vendor interfaces are intentionally ignored so they cannot
interrupt ordinary keyboard input. The resulting PS5-facing interface uses the
same six-key rollover behavior as the Pico 2 W bridge.

## Host class driver

TinyUSB's generic HID host class is disabled (`CFG_TUH_HID 0`). The receiver
is handled by `host/boot_keyboard_host.c`, a small application-defined TinyUSB
host class driver registered through `usbh_app_driver_get_cb()`:

- It claims only the first interface with class HID, subclass Boot, and
  protocol Keyboard, and opens only that interface's interrupt IN endpoint.
  The receiver's other interfaces stay unclaimed, so TinyUSB skips them and
  issues no control transfers for them.
- Its configuration sequence is `SET_IDLE(0)` followed by `SET_PROTOCOL(Boot)`,
  the same two requests TinyUSB's HID host sends a boot keyboard. It never
  requests the HID report descriptor, which this receiver intermittently
  stalls through Pico-PIO-USB. Earlier versions patched TinyUSB's `hid_host.c`
  at configure time to skip that request; the driver makes the patch
  unnecessary.
- It re-arms the IN endpoint inside its own transfer callback, immediately
  after handing the report to `host/usb_host_keyboard.c`, so a re-arm can never
  be forgotten or delayed behind application work.

`host/usb_host_keyboard.c` decodes each boot report into the shared keyboard
state, drops duplicates, and hands changes to core 0 through a multicore
queue. Core 0 publishes them into the report pipe and the shared USB keyboard
adapter sends them to the PS5.

## Latency design

Two USB frame clocks sit between a key press and the PS5. The bridge polls the
receiver once per frame of its own PIO USB host, and the PS5 polls the bridge
once per frame of its bus. Both frames are 1 ms, full-speed USB allows nothing
faster, and the two clocks are unrelated. Everything below exists to stop a
report from waiting for either clock longer than necessary.

- **Event-driven host core.** Core 1 sleeps with a wait-for-event instruction
  between passes of the TinyUSB host task once the receiver is configured.
  Every host event originates in an interrupt on that core, so the task resumes
  exactly when there is work. The earlier microsecond sleep went through the
  SDK's default alarm pool, whose interrupt is serviced by core 0, and
  interrupted the PS5-facing core tens of thousands of times a second. During
  enumeration a busy wait keeps the 100 µs gap this receiver needs between
  host-task passes.
- **Frame phase lock.** `host/sof_phase_lock.c` generates the PIO USB frame
  from a hardware alarm on core 1 instead of Pico-PIO-USB's own timer, and
  steers its phase so the receiver poll at the start of each frame lands
  `APP_SOF_PHASE_LEAD_US` before the PS5's SOF. The PS5's SOF is timestamped
  inside the USB device interrupt on core 0 through TinyUSB's event hook;
  `host/frame_phase.c` holds the pure arithmetic and is unit tested. The frame
  period never changes by more than `APP_SOF_PHASE_MAX_STEP_US` per frame, and
  steering is off during enumeration and whenever no PS5 SOF has been seen for
  `APP_SOF_PHASE_REFERENCE_MAX_AGE_US`. With the default 250 µs lead a report
  is armed on the PS5-facing endpoint roughly 100 µs before the PS5 polls it,
  instead of anywhere from 0 to 1 ms before. Set the lead to 0 to run the
  frame at exactly 1 ms with no steering.
- **Receiver polling interval.** `APP_USB_HOST_POLL_INTERVAL_FRAMES` polls the
  receiver's keyboard endpoint every frame regardless of the `bInterval` it
  declares. `bInterval` is the longest gap a host may leave between polls, so
  this is permitted; the receiver NAKs frames with nothing new. Set it to 0 to
  honour the declared interval.

The `feather-diagnostic` build reports, once a second: the receiver's
declared and effective polling interval, per-report latency from the receiver
poll to the PS5's acknowledgement split at the moment the endpoint was armed,
how many PS5 frames each report waited, a 100 µs histogram of the total, the
phase lock state and error, and where within the PS5's frame its poll arrives.
Tune the lead down until `poll_avg` stops falling or `frames_waited` starts
showing reports that missed a frame.

## Build

Install Pico SDK 2.3.0 or newer, CMake, Ninja, Git, and the Arm GNU toolchain,
then set `PICO_SDK_PATH`. Configure and build with:

```powershell
cmake --preset feather-release
cmake --build --preset feather-release
```

The first configure downloads the pinned Pico-PIO-USB dependency into the
ignored build directory. The output is:

```text
build/g915_feather_rp2040_usb_host_bridge.uf2
```

For a debug build, use `feather-debug` for both commands. UART diagnostics are
available at 115200 baud on GPIO0 (TX) and GPIO1 (RX); USB CDC is disabled so
the USB-C interface remains a single keyboard.

The `feather-diagnostic` preset adds a USB CDC interface that prints the
report described under "Latency design" once per second. It enumerates with a
different PID so the PS5 build is never confused with it.

The frame phase arithmetic has host-side tests that need only a C compiler:

```powershell
cmake -S tests -B tests/build -G Ninja
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

They run the shared core tests from `firmware/common` as well.

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
