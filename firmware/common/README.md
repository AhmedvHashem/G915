# Shared bridge modules

Code used by both the Pico 2 W and the Feather RP2040 bridges.

- `core/keyboard_state.c` — modifiers, key usages, and source error usages,
  independent of BTstack and TinyUSB. Converts that state to the six-key USB
  boot protocol and emits rollover when more than six supported keys are held.
- `core/report_pipe.c` — orders state changes, suppresses duplicates, and uses
  source epochs plus release/resynchronisation barriers so stale reports are
  never replayed after a source reset or a USB lifecycle transition. Items can
  carry a source timestamp for delivery-latency measurement.
- `usb/usb_keyboard.c` — owns USB mount, unmount, suspend, resume, HID idle,
  protocol, output LED, and transfer-completion behaviour for the console-facing
  boot keyboard. Sends one report at a time and resynchronises the host after
  lifecycle changes. Records delivery timing for every report: source stamp to
  host acknowledgement as min, average, max, and a 100 µs histogram; the same
  interval split at the moment the endpoint was armed; and how many host USB
  frames elapsed between arming and acknowledgement. An optional observer
  receives each delivery for bridge-specific measurements.

`CMakeLists.txt` exposes these as the `g915_bridge_common` INTERFACE library.
Each firmware adds it with `add_subdirectory` and provides its own
`app_config.h` and `tusb_config.h`, which the shared sources include.

## Tests

The core tests need only a host C compiler:

```powershell
cd firmware/common
cmake -S tests -B tests/build -G Ninja
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

They cover modifiers and usage filtering, six-key rollover, state merging,
duplicate suppression, stale epochs, source-reset barriers, overflow recovery,
USB activation resynchronisation, the remote-wake latch, and modifier
recovery.
