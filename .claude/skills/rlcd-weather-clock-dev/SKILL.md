---
name: rlcd-weather-clock-dev
description: Use when building, flashing, debugging, or designing UI for the esp32s3-rlcd-weather-clock (Waveshare ESP32-S3-RLCD-4.2, ST7305 1-bit panel, ESP-IDF 5.5.5, LVGL 9.5) - including COM port trouble, "device not functioning", missing serial output, layout that looks wrong on the panel, and any change to components/ui or the sync path.
---

# ESP32-S3 RLCD Weather Clock

Develop from measured facts. On this board the two things that most often waste a
cycle are **the panel is 1-bit** and **the serial port is not a reliable channel**.

## Flash without destroying something

**The port is not the board.** This machine has several `VID_303A&PID_1001`
devices plus stale ghost entries from earlier replugs. A live port with the right
VID proves "an Espressif device", not "the clock". Flashing the wrong one
overwrites its bootloader, partition table and app irreversibly - this has
already happened once here.

```powershell
. C:\Espressif\tools\Microsoft.v5.5.5.PowerShell_profile.ps1   # eim-managed; export.ps1 points at a venv that does not exist
idf.py -p COM8 flash
```

- **The clock is COM8.** If it fails, retry once, then ask for a replug. **Never
  scan for another port and use it.**
- Identify by MAC before trusting any other port:
  `python -m esptool -p COMn read_mac --after no_reset` (read-only; will not
  disturb whatever runs on the other ports). Record this board's MAC here the
  first time it is read, then match on it rather than on a port number.
- Invoke esptool as `python -m esptool` - the console script has been named both
  `esptool.py` and `esptool` across IDF versions; the module name has not moved.

### Two different COM8 failures, two different fixes

| Error | Meaning | Fix |
|---|---|---|
| `PermissionError(13, ..., 31)` "device attached to the system is not functioning" | The board's USB CDC dropped - almost always light sleep (below) | Download mode, or catch an awake window |
| `PermissionError(13, 'Access is denied.', None, 5)` | **Another program holds the port** - a VS Code serial monitor, a stray `idf.py monitor` | Close it |

Confusing these wastes replugs on a problem that is a monitor left open.

### Download mode is the reliable path

**This board has no RESET button.** Buttons are, left to right, **PWR, KEY, BOOT**.

1. **Long-press PWR** to power off.
2. **Hold BOOT.**
3. **Short-press PWR**, keep BOOT held about a second, release.

The screen stays blank - correct, that is the ROM loader. It runs before any
application, so it is immune to the sleep loop and connects when normal flashing
will not. The port may re-enumerate under a different number afterwards.

GPIO0 low at reset selects the ROM bootloader; GPIO46 must stay floating or low
or the downloader is never reached.

## Serial logging does not work on this firmware

`esp_light_sleep_start()` suspends the USB Serial/JTAG peripheral, and the clock
task sleeps for most of every second. The console is USB Serial/JTAG **only**
(`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, `CONFIG_ESP_CONSOLE_UART_NUM=-1`), so
there is no UART fallback: while the application runs, the host sees the device
appear and vanish continuously.

**Consequences, all of which have cost time here:**

- A 40-minute background capture of COM8 recorded zero bytes and hundreds of
  "port lost" lines. That is the expected result, not a broken script.
- `ESP_LOGx` is effectively write-only in normal operation. Do not plan a
  diagnosis around reading the log.
- **Put diagnostics on the panel.** The settings screen carries the last
  `esp_reset_reason()` for exactly this reason - it is the only channel that
  survives.
- The chronic port flakiness is this, not a failing cable or board.

**If a real log is ever needed**, UART0 is on TX 43 / RX 44 and a UART does not
vanish from the host when the chip sleeps. Switch the console to it and attach a
USB-serial adapter. See [references/board-hardware.md](references/board-hardware.md).

## The panel is 1-bit - design for it

`Lvgl_FlushCallback` thresholds RGB565 at `0x7fff`. There is no grey. 400x300
landscape.

- **Strokes are pure black and at least 2px.** A 1px or anti-aliased line
  survives horizontally and vertically and breaks into speckle on diagonals -
  radial clock ticks at ~30-55 degrees had to become dithered dots.
- **Grey exists only as Bayer 4x4 dithering** - `Dither_Threshold(x, y, grey)` in
  `components/ui/dither.h`. Always pass **screen-space** coordinates so adjacent
  dithered elements tile against each other instead of visibly seaming.
- **Never dither behind text.** Reduce the grey until the band under a reading is
  a sparse, mostly-white stipple.
- **`text_opa` gives no grey text.** Below roughly 65% it thresholds to invisible,
  above it to solid black.
- **Text outline is unavailable.** `LV_STYLE_TEXT_OUTLINE_STROKE_*` needs FreeType
  vector fonts; `CONFIG_LV_USE_FREETYPE` is off, so it silently does nothing to
  bitmap fonts. The real fix for a weak stroke is a heavier font.
- The panel has **no backlight and no touch**. Never add a touch driver.

## Size layouts for the widest value

Measure from the compiled font's `glyph_dsc[].adv_w` (1/16px fixed point, indexed
through `cmaps[]`), enumerate every value the slot can hold, and lay out for the
maximum. **Make the placeholder text in `screens.c` that worst case**, so the next
reader sees the constraint rather than a flattering example.

This is not theoretical: a date slot was sized around `Fri 21` (41px) and shipped.
The real worst case is `Wed 24` at 63px - the layout never fit, and only a
re-measurement for an unrelated change caught it.

**Corollary:** glyphs can overflow their own layout boxes. The clock digits use
`ui_font_saira_condensed_bold200`, whose widest glyph (`8`) is `adv_w` 1574 =
98.4px inside a 90px column. Free space between the digit groups is x172..221, not
the x175..218 the box positions imply. Never infer available space from
`lv_obj_set_pos` and width alone.

When a column is tight horizontally but loose vertically, **stack** rather than
shrink - it let the face date go back up from montserrat_14 to _16.

## LVGL 9.5 on this project

- `lv_canvas_set_px` must run **outside** `lv_canvas_init_layer` /
  `lv_canvas_finish_layer`; `lv_draw_*` need the layer.
- **Canvas children always render on top of the canvas bitmap.** Anything that
  must appear *under* canvas-drawn content has to be drawn as layer primitives,
  not added as a child - this forced the digital readout off `lv_label` and onto
  `lv_draw_rect`/`lv_draw_label` so the hands sweep over it.
- LVGL heap is `CONFIG_LV_MEM_SIZE_KILOBYTES=64`. Canvas buffers come from SPIRAM
  via `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`. Per-object canvases do not
  scale - 42 calendar cells would exhaust the heap.
- Mixed font sizes on one row align on the **baseline**, not the top edge. The top
  bar's 20pt row has `line_height` 22 / `base_line` 4, so its baseline is y21, and
  montserrat_16 (18/3) meets it at y6.

## Board contract

Verified against `main/user_config.h`; do not reuse for similarly-named Touch-LCD
boards. Full peripheral pin map, I2C addresses, backup/restore commands and the
pinned Waveshare factory image hash are in
[references/board-hardware.md](references/board-hardware.md).

- ESP32-S3-WROOM-1-N16R8; 16MB Flash QIO; 8MB Octal PSRAM at 80MHz.
- Display SPI: SCK 11, MOSI 12, DC 5, CS 40, RST 41, TE 6. No MISO.
- Shared I2C: SDA 13, SCL 14.
- Buttons active-low: **KEY = GPIO18** (`BTN_SELECT_PIN`), **BOOT = GPIO0**
  (`BTN_OK_PIN`). GPIO0 is the download strap - sampled at reset, not while
  running, so a long press is safe, but it must stay input-with-pull-up and must
  never be driven early enough to block ROM download recovery.
- Battery sense: GPIO4 / ADC1 channel 3 behind a 3x divider, with
  `BATTERY_CALIBRATION_FACTOR` in `battery.c` tuned per unit against a multimeter.
  Treat anything below ~2500mV as "no battery connected", not a real low charge.

## Config traps

- **`sdkconfig.defaults` is read only when `sdkconfig` does not exist.** Adding an
  option changes nothing in an existing tree, the build still succeeds, and a
  feature behind `#if CONFIG_...` is silently compiled out. After editing it, grep
  the generated `sdkconfig` to confirm the value landed.
- This repo carries **five permanently modified files** that must stay unstaged:
  `components/clock/clock_config.h`, `components/clock/weather_config.h`,
  `dependencies.lock`, `sdkconfig`, `sdkconfig.defaults`. Stage by explicit path.
  Never `git add -A`.

## Verify on the panel, not on the compiler

A clean build proves nothing about this device. Flashing is the controller's job;
**judging the panel is the user's** - stroke weight, contrast in real light,
ghosting and legibility at distance are properties of a reflective display and
only a person looking at it settles them.

Hand over a specific checklist, not "does it look right". Name the worst case to
look at: a time with an `8` beside the colon, the widest date, a full battery.

## Debugging the sync path

- `ESP_ERROR_CHECK` **aborts**, which reboots the board. That is defensible for
  first-boot bring-up where a failure means the device is unusable anyway, and
  wrong on anything that runs repeatedly - a failed refresh should cost a stale
  reading, not an uptime.
- WiFi comes up only on a schedule (`SYNC_HOUR_1`/`SYNC_HOUR_2` for time,
  `WEATHER_REFRESH_MIN` for weather), so **link state is not a useful indicator** -
  it would read disconnected almost always. Show data staleness instead.
- A reboot and a brownout look identical from the outside. `esp_reset_reason()`
  separates them and they need opposite fixes; get it before proposing either.

## Never burn eFuses

**Do not run `espefuse` in write mode. Do not enable Secure Boot or Flash
Encryption.** eFuses are one-time-programmable - no reflash undoes a burned bit.

This is the only line between "recoverable" and "bricked". The ESP32-S3 ROM
bootloader is in silicon and this board uses native USB Serial/JTAG, so a corrupt
partition table, a half-written app, or blank Flash still enumerates and still
accepts esptool. Burning `DIS_DOWNLOAD_MODE`, `DIS_USB_SERIAL_JTAG`,
`DIS_USB_SERIAL_JTAG_DOWNLOAD_MODE` or `DIS_PAD_JTAG` closes that door forever.

`espefuse summary` is read-only and safe. Any `burn-*` subcommand is off limits
unless the user asks in those words and understands it is irreversible.

## Recovery ladder

Work it in order; do not escalate straight to an erase. Treat "bricked" claims
sceptically.

1. `otadata` corrupt - the ROM bootloader falls back to `factory` unaided.
2. App broken, USB enumerating - reflash from the host.
3. USB not enumerating - download mode (PWR off, hold BOOT, PWR on).
4. Only then restore a full verified dump from offset `0x0`.

Only this board's own dump restores its NVS and calibration; Waveshare's factory
image restores the demo and nothing of yours. Commands and the pinned image hash
are in [references/board-hardware.md](references/board-hardware.md). **Take a
16 MB backup before the first write** - none exists for this unit yet.

The RTC holder charges its cell: **ML1220 or compatible rechargeable only, never
CR1220.**

`erase-flash` is not a fix for connection trouble. Never use esptool `--force`
against unknown Secure Boot or Flash Encryption state.
