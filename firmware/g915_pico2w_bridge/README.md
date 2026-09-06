# G915 Bridge for Raspberry Pi Pico 2 W

The G915 connects to the Pico 2 W over Bluetooth LE HID Report Protocol. The
firmware decodes the standard keyboard fields from the G915 report map and
exposes one USB boot-keyboard interface. Consumer/media and Logitech vendor
reports are not forwarded, so they do not interrupt ordinary keyboard input.

## Architecture

The bridge uses one cooperative foreground loop:

```text
CYW43/BTstack poll -> BLE HID client -> HID decoder -> canonical keyboard state
                                                    -> report pipe
TinyUSB task       -> USB keyboard adapter <--------/
```

`cyw43_arch_poll()`, `tud_task()`, and the USB adapter task all run from
`main()`. This keeps BLE callbacks, state publication, and USB servicing in one
execution context.

The main components are:

- `ble/hog_client.c` — an explicit BLE lifecycle state machine covering
  startup, discovery, connection, security, HID setup, ready, cancellation,
  disconnection, and bounded retry/backoff paths. Events are accepted only for
  the active connection and HID client context.
- `ble/hid_keyboard_decoder.c` — validates HID report lengths, decodes
  keyboard fields, and combines keyboard fragments into a typed canonical
  state.
- `../common/core/keyboard_state.c` — represents modifiers, key usages, and source error
  usages independently of both BTstack and TinyUSB. It converts that state to
  the six-key USB boot protocol and emits rollover if more than six supported
  keys are held.
- `../common/core/report_pipe.c` — orders state changes, suppresses duplicates, and uses
  connection epochs plus release/resynchronization barriers so stale reports
  are not replayed after a source reset or lifecycle transition.
- `../common/usb/usb_keyboard.c` — owns USB mount, unmount, suspend, resume, HID idle,
  protocol, output LED, and transfer-completion behavior. It sends one report
  at a time and resynchronizes the host after USB lifecycle changes.

When USB is unavailable or suspended, the report pipe retains the latest
canonical state instead of accumulating an unbounded history. If the host
enabled USB remote wake, one fresh key press can request wake during a suspend;
the normal release/current-state resynchronization follows after resume.

## Pairing, security, and recovery

On a new or forgotten bridge:

1. Flash `g915_pico2w_bridge.uf2`.
2. Hold the G915 Bluetooth button for three seconds until it flashes rapidly.
3. Type the fixed passkey `123456` on the G915 and press Enter.

Pairing requests bonding, man-in-the-middle protection, and LE Secure
Connections. After a successful secure HID setup, the approved keyboard record
is stored in Pico flash. Subsequent boots reconnect to that approved address;
transient reconnect or setup failures use capped backoff rather than approving
an arbitrary nearby device.

To intentionally replace the keyboard, connect a 115200-baud serial terminal
to UART0 (GP0/TX and GP1/RX) and send:

```text
forget
```

Terminate the command with Enter. The firmware removes its approved-keyboard
record and returns to pairing mode.

The onboard LED blinks in every non-ready BLE state and stays solid while the
HID client is ready. UART diagnostics use the same 115200-baud UART0 connection.

## Build

Install the Raspberry Pi Pico SDK, CMake, and Ninja, and set `PICO_SDK_PATH`.
The checked-in presets build for `pico2_w`:

```powershell
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

Use `pico2w-debug` in both commands for a debug build. The release UF2 is
written to `build/g915_pico2w_bridge.uf2`.

The equivalent direct configuration is:

```powershell
cmake -S . -B build -G Ninja -DPICO_BOARD=pico2_w -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Native tests

The keyboard-state and report-pipe modules are shared with the Feather bridge
and live in `firmware/common`. Configuring this directory's `tests` project
builds those core tests together with the BLE decoder tests, which use
BTstack's real HID parser and therefore need `PICO_SDK_PATH`:

```powershell
cmake -S tests -B tests/build -G Ninja
cmake --build tests/build
ctest --test-dir tests/build --output-on-failure
```

Without a Pico SDK the decoder tests are skipped and only the core tests run.
