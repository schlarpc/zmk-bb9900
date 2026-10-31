# Hardware test checklist

This checklist confirms that this port behaves like the original ZitaoTech firmware
(`bb9900_US_layout_2024.03.07.uf2`, built from `ZitaoTech/zmk@bbkeyboard_tp`).
The keymap comes from `ZitaoTech/zmk-config_9900`, the version that uses `&tog` for layers.

Layers use the keymap's names: **BASE** (0), **SYM** (1) and **UPPER** (2). The original README calls
them Layer 1, 2 and 3. SYM is toggled by the SYM key; UPPER by the right-hand aA key. In this keymap the
left-hand aA key is Ctrl, the "0" key is Alt and the key labelled "alt" (left of Z) is Shift.

Mark each item `[x]` when it passes. Write a note next to any item that fails.

## 0. Before you flash

- [x] Write down which devices are paired now, and in which BT slots (0–3).
- [x] Keep `bb9900_US_layout_2024.03.07.uf2` from the original repo nearby so you can roll back.

## 1. Flash and boot

- [x] Connect USB. Enter UPPER (right aA key), then press `$` (left of Enter). The `NICENANO` drive appears.
- [x] Run `nix run .#flash`. It finds the device and copies `zmk.uf2`.
- [ ] The keyboard reboots. The keyboard backlight comes on at about 40%.
- [ ] The big button backlight is off, because BASE is active.
- [ ] The trackpad backlight is off. If caps lock is on at the host, it lights after the host sends its LED report.

## 2. Bluetooth

- [ ] Existing pairings still work after the flash. The storage partition did not move, so they should survive. If they do not, record that and re-pair.
- [ ] UPPER: big buttons 1–4 select BT slots 0–3.
- [ ] UPPER: double-tap the trackpad. This clears the current slot (`&td0`, `BT_CLR`). A new device can then pair.
- [ ] UPPER: a single tap on the trackpad is a left click.
- [ ] Pair with a phone and a computer, and switch between them.
- [ ] Out of range: turn off device 1. The keyboard falls back to device 2.

## 3. USB and output selection

- [ ] With only USB connected and no BLE host, typing goes over USB.
- [ ] UPPER: hold Space for more than 500 ms. Output toggles between USB and BLE.
- [ ] UPPER: tap Space. A space is typed.
- [ ] Wait more than 10 s after toggling output, then power-cycle. The selection is remembered.

## 4. Keys and layers

- [p] BASE: every letter key, Backspace, Enter, Shift, Ctrl, Alt, Space. -> $ types ; 
- [x] Call key = caps lock. Blackberry key = GUI. Back key: tap = K_BACK, hold = Esc. End-call key = Tab.
- [x] Shoulder keys = left click and right click.
- [x] Trackpad press = left click.
- [x] SYM toggles SYM on and off (`&tog`). The big button backlight is steady while SYM is on.
- [x] Right aA toggles UPPER on and off. The big button backlight breathes, slowly fading between about 20% and 100%. -> pressing sym while in layer 2 doesn't return to layer 1. intentional? you refer to aA as entering both layer 2 and 3?
- [x] SYM: numbers, symbols, volume up and down on the shoulders, mute, PrintScreen, F5, F12. 
- [p] SYM: `(`/`)` keys tap = `(`/`)`; hold = `[`/`]`. -> no, reversed
- [x] UPPER: arrow keys (W/A/S/D positions), `< > | =`, `\ & { } ^`. -> works but frankly i expected <> on parens keys, why not
- [x] UPPER: shoulders scroll up and down (`&msc`).
- [x] UPPER: backlight down / toggle / up on V / B / N. Steps are 10%.
- [x] UPPER: R = `&sys_reset`. The keyboard reboots.
- [ ] Ghosting: hold Shift (the key labelled "alt", left of Z) and press X or B. The original gave no output here; this is a hardware matrix limit. Record what happens. -> is shift aA?

## 5. Trackpad: pointer mode

- [x] The cursor moves in the same direction as your finger on all four axes. The axes are not swapped or mirrored.
- [x] Speed feels like the original firmware (1.5× raw sensor counts).
- [x] A fast swipe never makes the cursor jump backwards. The original firmware could wrap at high speed; this port should not.
- [x] With the finger still, the cursor stays still. No drift.
- [x] UPPER: T = `&ext_power EP_TOG`. The trackpad stops working. Press T again and it comes back without a reboot.

## 6. Trackpad: scroll mode (caps lock)

- [x] Turn caps lock on. The trackpad backlight lights at 100%.
- [x] A vertical swipe scrolls. Finger up scrolls content the same way as the original firmware.
- [x] A slow swipe scrolls one line at a time. A fast swipe scrolls several lines at once.
- [x] A horizontal swipe scrolls sideways, in the same direction for slow and fast swipes. (The original firmware reversed slow horizontal scrolling; see the deviations below.)
- [x] Scroll speed feels like the original firmware.
- [x] Turn caps lock off. The trackpad backlight turns off and pointer mode returns at once.
- [ ] With scroll lock on and caps lock off (if the host supports it), the trackpad also scrolls. The trackpad backlight stays off.
- [ ] iOS: after following the README's AssistiveTouch and caps-lock-language-switch steps, scrolling works.

## 7. Backlight and power

- [x] After 30 s with no key press, the keyboard backlight turns off. A key press turns it back on.
- [x] The big button and trackpad indicators keep working while the keyboard backlight is idle-off.
- [x] Charging LED behaviour is unchanged (it is hardware-driven).
- [x] Battery level is reported to the host. Check it in the phone or OS Bluetooth settings.
- [ ] Rough battery drain over a day or two compared with the original firmware. The trackpad no longer sends empty reports 100 times a second, so drain should be the same or better.

## Known deviations from the original firmware

These differences are deliberate. Note anything else that differs.

1. **Horizontal scroll direction.** In the original, slow horizontal scrolling went the opposite way from fast horizontal scrolling. This was a C operator-precedence bug. In this port both go the same way.
2. **Scroll dead zones.** In the original, vertical motion of exactly 2 or 20 counts produced no scroll at all, because of gaps in the curve. Those values now fall into the neighbouring steps.
3. **Pointer overflow.** The original multiplied the motion by 1.5 into an 8-bit value. That value wrapped above about 85 counts, so the cursor jumped backwards. This port uses ZMK's 16-bit pipeline.
4. **Sensor overflow.** On a motion overflow the original never read the delta registers. This port reads and discards them, so the sensor cannot get stuck reporting overflow. This may fix the "trackpad stops working until I press T" problem from the README's troubleshooting section.
5. **Scroll mode condition.** The original used a hard-coded list of indicator values (2, 3, 4, 7). It skipped "caps + scroll" and "num + scroll". This port uses "caps lock or scroll lock is on". The caps lock LED likewise lights whenever caps lock is set.
6. **Empty reports.** The original sent a mouse report every 10 ms, even with no motion. This port sends reports only when there is motion.
7. **Indicator latency.** The original polled layer and caps state every 50–100 ms. This port is event-driven, so the LEDs change at once.
8. **Removed the stray `blue_led` GPIO node on P0.10**, copied from the nice!nano board. It clashed with the trackpad backlight PWM on the same pin.
9. **Debug logging** (`CONFIG_LOG`, sensor debug level) is no longer built in by default.
