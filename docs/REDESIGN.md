# Redesign list

This file lists what to change now that the port matches the original firmware. Items are
grouped by area. Each one says whether it is a fix, a change in behaviour, or an upstream ZMK
feature to adopt. Strike items out or move them to "Done" as they land.

## Trackpad

The original firmware never configured the ADBS-A320 sensor. From reset the sensor runs with its
optical finger navigation (OFN) engine off. That gives a fixed 500 CPI with no speed switching,
no noise gating and no finger detection. This explains both complaints: fine movement is coarse
and jerky, and fast movement does not travel far. The datasheet's power-up sequence (page 15)
turns these features on, and no ZitaoTech firmware ever ran it.

The sensor also has no power-on self reset. It expects its NRST pin to be toggled every time it
gets power. The original firmware never did this, and we do not know whether NRST is wired to the
MCU. This is the likely cause of the trackpad sometimes being dead after boot until a power
cycle.

1. **Done (commit `761f85a`), needs a hardware test.** The driver now follows the datasheet
   bring-up. It waits 100 ms after power-up, soft-resets the sensor, checks the product ID
   (`0x83` and `0x7c`), and enables speed switching, assert/deassert and finger detection. It reads
   the configuration back to confirm it. While running, it checks the product ID once per second.
   If the sensor stops answering, it resets it again. After 5 failed attempts it power-cycles the
   sensor through `EXT_POWER`, but only when ext power is meant to be on, so the T key still works.
   What to check:
   - The trackpad comes up on every boot. Try about 10 boots, using both the power switch and
     `&sys_reset`.
   - Fine movement is smooth.
   - A fast swipe crosses the screen in 1–2 drags. Speed switching now runs the sensor between
     250 and 1000 CPI.
   - Pointer speed may now be too high, because of the 1.5× scaler on top. If so, drop
     `<&zip_xy_scaler 3 2>` to `1 1` in the keymap.
   - Scroll mode may now scroll faster. Its curve was tuned for 500 CPI.
   - If the trackpad is still dead on some boots, flash `nix run .#flash-debug`. Then read the log
     with `tio /dev/ttyACM0` and look for `input_a320` lines.
   **Follow-up, tuned from serial traces (`b32ee0d`, `959f1b3`, `2c29cd1`):**
   - Thumb swipes peak at 4–8 ips. That is below the sensor's default speed-switching
     thresholds (8–10 ips), so the sensor rarely left 500 CPI. The thresholds are now 2/3/4 ips
     and the top step is 1250 CPI.
   - Polling runs every 4 ms while the finger is moving. The sensor makes a new sample about
     every 6 ms, so polling faster gains nothing.
   - Saturated samples are kept instead of dropped.
   - Small samples with a high shutter value are dropped as liftoff twitches, unless they continue
     a fast stroke.
   - The fastest swipes still saturate at 127 counts per frame. That is about 17 ips, above the
     sensor's rated 15 ips, so it is a hardware limit.
   Result reported on hardware: 2–3 swipes to cross the screen, fewer twitches.
2. **Pointer acceleration in firmware, if still needed after item 1.** Upstream ZMK has no
   acceleration processor. Community modules exist (forks of `zmk-pointing-acceleration`), but they
   need vetting. The OS adds its own acceleration on top: libinput uses its "adaptive" profile for
   mice by default. Decide this after testing item 1.
3. **Scroll mode trigger (behaviour change).** Scroll mode currently depends on the host's caps lock
   LED. That means:
   - scrolling turns caps lock on, so you cannot type while scrolling;
   - it only works on hosts that send LED reports (iOS needs a settings change);
   - the mode sits in host state, not in the keyboard.
   Replace it with a keyboard-side mode, using the upstream `zip_temp_layer` or a layer override on
   `&trackpad_listener`. Options: hold a key to scroll, tap a key to toggle, or both. Then drop the
   custom `indicator_scroll` processor, or keep it as an option.
4. **Retune the scroll curve** once items 1 and 3 settle. Upstream also supports smooth
   (high-resolution) scrolling through `CONFIG_ZMK_POINTING_SMOOTH_SCROLLING`.
5. **Precision mode:** a layer override that scales the pointer down while a key is held. This is
   an upstream feature.
6. **Polling rate and the motion pin.** The driver polls every 10 ms. The A320 has a MOTION pin that
   could raise an interrupt, but the original board files do not list one. We could find it by
   probing the free nRF pins while moving a finger. An interrupt would allow lower latency and less
   power use at the same time.

## Power management

**These figures are estimates.** I worked them out from the datasheets and from what the code does.
Nothing has been measured. To get real numbers, put a current meter (a Nordic PPK2, a µCurrent or
similar) in series with the battery. The BL-5B is rated at about 890 mAh.

| State | Original firmware | This port today | Achievable |
|---|---|---|---|
| Awake, idle, connected, LEDs off | ~0.5–1.5 mA | ~0.1–0.3 mA | ~0.05–0.1 mA |
| Left on overnight | same as idle (never sleeps) | same as idle (never sleeps) | ~5–20 µA in deep sleep |
| Rough standby on 890 mAh | 3–8 weeks | 4–12 months | sleep: years; idle: 1+ year |

Where the current goes:

1. **Empty mouse reports (fixed by the port).** The original sent a mouse report every 10 ms even
   with no motion. Over BLE this keeps the radio transmitting on every connection event and defeats
   peripheral latency. This was the largest drain. The port only sends reports when there is
   motion.
2. **No deep sleep (inherited).** `CONFIG_ZMK_SLEEP` is off, so the keyboard stays awake until you
   use the slide switch. Upstream sleep cuts ext power (and with it the trackpad and probably the
   backlights) and enters System OFF. To do: enable `CONFIG_ZMK_SLEEP` with an idle timeout, and add
   `wakeup-source` to the kscan node so a key press wakes it. The trackpad cannot wake the keyboard
   unless we find the motion pin (trackpad item 6).
3. **Trackpad polling at 100 Hz (inherited).** Each poll wakes the CPU for several I²C transfers,
   which I estimate at 50–150 µA on average. The A320 drops into its own rest modes when idle (Rest1
   0.4 mA, then Rest2 0.1 mA after 9.5 s, then Rest3 0.04 mA after about 10 min). Traces show
   that it still reaches Rest1 and Rest2 while the firmware polls it every 10 ms (the Observation
   register reads `0x5f` and then `0x9f`). So polling does not hold it in Run mode (3.1 mA). To
   do: adaptive polling, fast while moving and slow (or interrupt-driven) when idle.
4. **Indicator LEDs ignore idle (inherited).** A layer left toggled on with `&tog` keeps the
   big-button LED lit or animating. Caps lock left on (scroll mode) keeps the trackpad LED at 100%.
   Each lit LED costs its own current plus about 0.5 mA for the PWM clock. (Zephyr stops the PWM
   peripheral when all channels are at 0%, so LEDs that are off cost nothing.) To do: turn the
   indicators off when the keyboard goes idle, like the keyboard backlight already does.
5. **+8 dBm TX power (inherited).** The board forces the maximum transmit power. That roughly
   doubles radio current per packet compared with 0 dBm. For a keyboard used within a metre or two,
   0 dBm should be plenty. To do: drop the default to 0 dBm and keep +8 dBm as an option.
6. **Unknowns:** the board's regulator, charger quiescent current, and what `EXT_POWER` actually
   switches. These need the schematic or a measurement.

## Weird things inherited from the original

- **Scroll mode tied to host caps lock.** See trackpad item 3.
- **Key labels do not match functions.** The key labelled `alt` is Shift, left `aA` is Ctrl, `0` is
  Alt, and `$` types `;`.
- **Shift ghosts with X and B.** This is a matrix limit (no diodes). The original shipped
  sticky-Shift as a separate firmware build. Use `&sk LSHFT` everywhere.
- **`&tog` layers.** SYM pressed inside UPPER turns SYM on underneath UPPER instead of switching to
  it. Options are `&to`, momentary hold, tap-toggle with hold-momentary, or an upstream tri-layer
  (conditional layers).
- **`KP_EQUAL` on UPPER** is the keypad `=`. Some hosts (Android, iOS) ignore it. The firmware
  fork's own keymap used `EQUAL`.
- **SYM `(`/`)` keys:** tap gives `(`, hold gives `[`. `<` and `>` sit on U and I in UPPER instead of
  on the paren keys.
- **Double-tap the trackpad in UPPER to clear the BT bond.** This is easy to trigger by accident,
  and the 400 ms tap-dance window also delays a single click on that layer.
- **Space in UPPER is a 500 ms hold-tap for the USB/BLE toggle.** Space is delayed on that layer.
- **Ext power state is saved in settings.** If T turns the trackpad off, it stays off across
  reboots. This is probably where the README's "press T if the trackpad doesn't work" advice comes
  from. Consider not saving it, or showing the state on an LED.
- **No physical layout in the devicetree.** This blocks ZMK Studio and generated keymap images.

## Upstream ZMK features worth adopting

- **ZMK Studio:** live keymap editing over USB or BLE, no reflashing. Needs a physical layout node
  (`zmk,physical-layout`) and `enableZmkStudio = true` in the flake.
- **Physical layout and keymap-drawer:** generate the layer images from the keymap, replacing the
  hand-drawn PNGs.
- **Deep sleep** (`CONFIG_ZMK_SLEEP`) and ext-power cutoff while asleep. See Power item 2.
- **Input processors:** temp layer (auto mouse layer), scroll mode on a layer, precision scaling,
  and mouse buttons as behaviours.
- **Smooth scrolling** (`CONFIG_ZMK_POINTING_SMOOTH_SCROLLING`).
- **Caps word** (`&caps_word`): a capitalisation mode that turns itself off, better than caps lock
  for typing and independent of the scroll mode.
- **Conditional layers** (tri-layer: SYM + aA gives UPPER), **combos**, **mod-morph** (for
  example Shift+`$` → `$`), and **`&key_repeat`**.
- **Safer BT controls:** `&bt BT_DISC`, and clearing bonds through a hold or combo instead of a
  double-tap.
- **USB logging snippet** for debugging. Already added as `nix run .#flash-debug`.

## Done

- Trackpad: datasheet bring-up, speed switching, ID check and auto-recovery (`761f85a`). Tested
  on hardware: comes up on every boot, fine movement much better.
- Trackpad: speed thresholds, polling while moving, and liftoff filter tuned from traces
  (`b32ee0d`..`2c29cd1`).
- Batch 1, typing and layers. Every key now does what its label says: `alt` = Alt, both `aA` =
  Shift, `$` = `$`. `0` = Ctrl. `SYM` = the legends layer. `alt` + either `aA` = UPPER. Mods and
  layers are sticky (tap = next key, hold = normal, 2 s timeout). Double-tap `aA` = caps word.
  Double-tap SYM or the UPPER combo = lock. Inside a locked layer its own key returns to BASE.
  `< >` moved to T/Y on UPPER, which pushed the ext power toggle from T to U. `KP_EQUAL` became
  `EQUAL`. To test:
  - [ ] Tap `aA`, then a letter: one capital. Hold `aA`: normal Shift.
  - [x] Double-tap `aA`: caps word, which ends at a space. (Needed a 300 ms window, `921c807`.)
  - [ ] Tap `0`, then C: Ctrl+C. Tap `alt`, then Tab: Alt+Tab. Hold `alt` and press Tab
    repeatedly: cycles windows.
  - [ ] Tap SYM, then W: `1`, then back to BASE. Hold SYM and type `123`. Double-tap SYM: locked
    (big buttons steady). SYM again: BASE.
  - [ ] Press `alt` + `aA` together: UPPER toggles on (big buttons breathing). Combo again: BASE.
    Hold the combo: UPPER while held. (The first build double-tapped to lock, which could not
    work reliably through a combo: UPPER fell back to one-shot and timed out after 2 s.)
  - [ ] UPPER on, press SYM: switches to SYM. Locked SYM, press `alt` + `aA`: UPPER on top.
  - [ ] Alt+Shift (for example the Android language switch): tap `alt`, then tap `aA`, without the
    combo firing.
  - [ ] `$` types `$`. On UPPER, T/Y = `< >`, U = trackpad power, P = `=`.
- Batch 2, trackpad modes. Scroll mode is now a keyboard-side SCROLL layer (layer 3) instead of
  the host's caps lock. Tap Call to toggle it, or hold Call to scroll while held. Caps lock moved
  to UPPER-Q. The trackpad backlight shows scroll mode, and the big-button layer light ignores
  it. A new per-axis scroll curve replaces the caps-lock processor: low gain up to a knee, then
  high gain, so it no longer depends on event order or a 30 ms tick. Smooth (high-resolution)
  scrolling is on (`CONFIG_ZMK_POINTING_SMOOTH_SCROLLING` in `bb9900.conf`). Turn it off for Mac
  or iOS hosts, which would otherwise scroll about 16x too fast. Precision mode and pointer
  acceleration were skipped. To test:
  - [x] Tap Call: trackpad backlight on, trackpad scrolls. Tap Call again: back to the pointer.
  - [x] Hold Call and swipe: scrolls while held, pointer again on release.
  - [x] The big-button light does not change when scroll mode turns on.
  - [x] Scroll mode works together with SYM and UPPER, and keys behave as normal in scroll mode.
  - [x] Scrolling is smooth (pixel steps) on this Linux machine. Slow swipes are controllable;
    fast swipes do not fling the page.
  - [x] Horizontal scroll goes the same way as vertical, like a wheel (first build had it reversed).
  - [x] UPPER-Q toggles caps lock.
  Follow-ups: horizontal direction fixed (`676a08e`); scroll distance cut by about 30% (`aeb9879`).
- Batch 3, power:
  - Deep sleep after 30 min without use (`CONFIG_ZMK_SLEEP`). A key press wakes it. Ext power
    and the trackpad are cut while asleep. ZMK never sleeps while USB power is present.
  - Big-button and trackpad lights turn off with the 30 s idle timeout.
  - The trackpad polls every 25 ms after 1 s without motion, 10 ms within 1 s, and 4 ms while
    moving.
  - TX power stays at +8 dBm, by choice.
  To test:
  - [ ] On battery, leave it untouched past the sleep timeout: everything goes dark and BLE
    disconnects. A key press wakes it. It reconnects within a few seconds, and the trackpad works
    after the wake.
  - [ ] Lock UPPER or turn on scroll mode, wait 30 s: both lights go off. Press a key or touch the
    trackpad: they come back.
  - [ ] After a few seconds' pause, the first trackpad movement does not feel laggy.
