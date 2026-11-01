# zmk-bb9900

[ZMK](https://zmk.dev) firmware for the [ZitaoTech BB9900 USB/BLE keyboard](https://github.com/ZitaoTech/BB9900-USB_BLE_Keyboard), built with Nix.

The original firmware runs on a 2024 fork of ZMK that is no longer maintained. This repo runs
on upstream ZMK instead. All the hardware-specific code lives in a small Zephyr module in this
repo:

| Path | What it is |
|---|---|
| `boards/zitaotech/bb9900/` | Board definition (Zephyr HWMv2, target `bb9900//zmk`) |
| `drivers/input/input_a320.c` | ADBS-A320 trackpad, as a Zephyr input device |
| `src/input_processor_scroll_curve.c` | Turns trackpad motion into scrolling on the scroll mode layer |
| `src/layer_indicator.c` | Big button backlight shows the active layer |
| `src/trackpad_indicator.c` | Trackpad backlight shows scroll mode |
| `config/` | Your keymap, Kconfig overrides and the west manifest that pins ZMK |

## Usage

```sh
nix build            # firmware ends up in ./result/zmk.uf2
nix run .#flash      # waits for the bootloader drive, then copies the firmware
```

To enter the bootloader, connect USB, go to layer 3 (aA key) and press `$` (left of Enter).

Edit `config/bb9900.keymap` for your keymap and `config/bb9900.conf` for backlight and indicator
settings. To tune the trackpad, override `&trackpad_listener` in the keymap; there is a commented
example at the bottom of the keymap file.

## Updating ZMK

`config/west.yml` pins ZMK to a commit. The `# main` comment tells the updater which branch to
follow.

```sh
nix run .#update     # bumps west.yml to the branch head and fixes zephyrDepsHash
nix build
```

## Testing

See [docs/TESTING.md](docs/TESTING.md) for the hardware checklist and the known deviations from
the original firmware.
