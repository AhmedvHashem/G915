# Use a Logitech G915 keyboard with a PS5

This project turns a small circuit board into an adapter between a Logitech
G915 keyboard and a PlayStation 5.

The adapter receives your keystrokes from the G915 and presents them to the
PS5 as a normal wired USB keyboard. No software is installed on the console.

> [!NOTE]
> The adapter only provides keyboard input. It does not add keyboard support
> to games that do not already support it, and it does not forward mouse,
> media-key, or other Logitech-specific features.

## Choose your adapter

There are two versions. You only need to build one.

| Version | What you need | How the G915 connects | Best for |
| --- | --- | --- | --- |
| **Adafruit Feather RP2040 USB Host** | The Feather board and your G915 LIGHTSPEED USB receiver | LIGHTSPEED receiver | The easiest setup and lowest delay |
| **Raspberry Pi Pico 2 W** | A Pico 2 W board | Bluetooth | Using the keyboard without its receiver |

The **Feather version is recommended** when you have the LIGHTSPEED receiver.
USB reports arrive about every 1 millisecond; Bluetooth has a minimum interval
of 7.5 milliseconds.

## What you need before starting

- A Logitech G915 keyboard.
- One of the two boards listed above.
- A USB data cable for the board. A charge-only cable will not work.
- The matching `.uf2` firmware file.
- A computer for copying the firmware to the board.

The `.uf2` file is the program for the adapter. If someone has provided a
ready-made file, you can go directly to the instructions below. Otherwise,
see [Building the firmware](#building-the-firmware).

## Recommended: Feather and LIGHTSPEED receiver

### Install the firmware

1. Connect the Feather's USB-C port to your computer.
2. Hold **BOOT**, briefly press **RESET**, and then release **BOOT**.
3. A removable drive named `RPI-RP2` should appear.
4. Copy `g915_feather_rp2040_usb_host_bridge.uf2` onto that drive.
5. The drive disappears automatically when installation finishes. This is
   normal.

### Connect it to the PS5

1. Make sure the keyboard and LIGHTSPEED receiver are already paired. If they
   are not, pair them with Logitech software on a computer first.
2. Plug the LIGHTSPEED receiver into the Feather's USB-A port.
3. Connect the Feather's USB-C port to the PS5.
4. Press the **LIGHTSPEED** button on the G915.

The Feather's red light blinks while it waits for the receiver and stays on
when the keyboard is ready.

## Alternative: Pico 2 W over Bluetooth

### Install the firmware

1. Disconnect the Pico 2 W from USB.
2. Hold its **BOOTSEL** button while connecting it to your computer, then
   release the button.
3. A removable drive named `RPI-RP2` should appear.
4. Copy `g915_pico2w_bridge.uf2` onto that drive.
5. The drive disappears automatically when installation finishes. This is
   normal.

### Pair and connect it

1. Connect the Pico 2 W to the PS5 with its USB cable.
2. Hold the G915's **Bluetooth** button for about three seconds, until it
   flashes quickly.
3. Type `123456` on the G915 and press **Enter** to finish pairing.

The Pico's light blinks while connecting and stays on when the keyboard is
ready. It remembers the approved keyboard and reconnects automatically after
the first successful pairing.

Replacing the paired keyboard requires a serial connection to the Pico. See
the [Pico 2 W technical guide](firmware/g915_pico2w_bridge/README.md#pairing-security-and-recovery)
for those recovery steps.

## Quick troubleshooting

- **No `RPI-RP2` drive appears:** Try another USB cable; many cables provide
  power but cannot transfer data. Repeat the button sequence for your board.
- **The status light keeps blinking:** Check that the G915 is using the same
  connection mode as the adapter. For the Feather, also check that the
  receiver works and is already paired with the keyboard.
- **The PS5 sees no keyboard:** Try another PS5 USB port and reconnect the
  adapter. Remember that individual games decide whether keyboard input is
  supported.
- **Some special keys do nothing:** This project intentionally forwards only
  ordinary keyboard keys. Media controls and Logitech-specific functions are
  not included.

## Building the firmware

This section is for developers or anyone who does not have a ready-made `.uf2`
file. Install Raspberry Pi Pico SDK 2.3.0 or newer, CMake, Ninja, Git, and the
Arm GNU toolchain, and set `PICO_SDK_PATH`.

### Feather build

From the repository root:

```powershell
cd firmware/g915_feather_rp2040_usb_host_bridge
cmake --preset feather-release
cmake --build --preset feather-release
```

The finished file is:

```text
firmware/g915_feather_rp2040_usb_host_bridge/build/g915_feather_rp2040_usb_host_bridge.uf2
```

See the [Feather technical guide](firmware/g915_feather_rp2040_usb_host_bridge/README.md)
for architecture, diagnostics, wiring details, and tests.

### Pico 2 W build

From the repository root:

```powershell
cd firmware/g915_pico2w_bridge
cmake --preset pico2w-release
cmake --build --preset pico2w-release
```

The finished file is:

```text
firmware/g915_pico2w_bridge/build/g915_pico2w_bridge.uf2
```

See the [Pico 2 W technical guide](firmware/g915_pico2w_bridge/README.md) for
architecture, security behavior, diagnostics, and tests.

## Repository layout

- `firmware/g915_feather_rp2040_usb_host_bridge` — the recommended
  LIGHTSPEED receiver version.
- `firmware/g915_pico2w_bridge` — the Bluetooth version.
- `firmware/common` — keyboard behavior shared by both versions.
- `firmware/g915_pico2w_blink` — a simple board-light test for developers.
