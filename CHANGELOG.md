# Changelog

Notable changes to this project. Format loosely follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Dates are when the work landed on the branch, not when it was released.

## [Unreleased] — the `calendar-screen-rework` branch

99 commits. The display and reliability work is finished and
hardware-verified. The reboot that ran through most of this branch's history is
found and fixed — see **Fixed**, first entry — and nothing is left under
[Known issues](#known-issues). One display path remains
[Unverified](#unverified) because reproducing it needs a real power cycle.

### Added

**Calendar + clock screen.** A Monday-first month grid with today boxed and
adjacent-month days rendered a size down, an analogue dial beside it, the
four-day forecast underneath and a quote strip at the foot. Arrow buttons page
through months. One label per day cell rather than one per column, because a
single `lv_label` cannot mix fonts and the adjacent-month days need a smaller
one.

**Analogue dial** with hour, minute and second hands, 12 solid hour ticks and 60
minor ticks. The minor ticks are dithered to a mid-grey in screen space, since
the panel has no grey of its own.

**Five-slot top bar** — indoor temperature, humidity, current weather, link
status, battery — living on `lv_layer_top()`, so it is drawn over every screen
rather than belonging to any one of them.

**Date on the clock face**, in the gap between the colon's two dots: the one
hole the big digits leave. Stacked weekday over day number.

**Current outdoor conditions**, not just the forecast high — `current_weather=true`
on the Open-Meteo request plus a `WeatherNow` field. Weather now runs on its own
cycle (`CONFIG_WEATHER_REFRESH_MIN`, 30 minutes) instead of riding the
twice-daily time sync.

**Battery fuel gauge** with the voltage beside it, and a charging bolt shown
*instead of* the fill — on a 1-bit panel a glyph drawn over a solid black fill
is invisible.

**Link status indicator** with four states: a tick when the last sync worked and
the radio is resting, a warning when the data has gone stale, a refresh while
associating, and an aerial only once an address is actually held.

**Button navigation** — a two-button input layer with long-press detection, a
navigation state machine, and a focus frame and hint overlay.

**Diagnostics.** Core dumps written to flash, so an intermittent panic can be
read afterwards. A live status line carrying last sync, uptime to the minute,
reset reason (with the raw ROM code, since IDF collapses both USB causes into
one) and heap. Per-sync internal-heap logging. `alloc_watch`, which records the
size and caps of a failed allocation plus a heap snapshot taken before every
Wi-Fi bring-up, in `RTC_NOINIT` so both survive the reboot.

**Tests and tooling.** An on-device self-test harness. `tools/verify_calendar_layout.py`,
which measures layouts against the real LVGL font tables — parsing `adv_w` out of
the generated font C files, including the sparse ranges where `°` and the
`LV_SYMBOL_*` glyphs live — and tests the widest value each field can hold.
`wifi_stress.c`, a Kconfig-gated bring-up/fetch stress test with a walker over
lwIP's own PCB lists. `tools/render_screen_mockup.py`, which draws both screens
as SVG from the layout constants themselves.

### Changed

**All configuration moved into Kconfig** under **Weather Clock**.
`clock_config.h` and `weather_config.h` are gone, and `sdkconfig` is no longer
tracked, because it holds the Wi-Fi password and the location.

**Stays awake while a USB host is attached.** Light sleep suspends the USB
Serial/JTAG peripheral, which made the port vanish; this turned 206 consecutive
flash failures into first-try success and made serial usable as a diagnostic
channel again. On battery it sleeps as before.

**TLS buffers are allocated per transfer**, raising the measured memory floor by
about 16.7 KB.

**Wi-Fi bring-up is declined below a heap floor** (`CONFIG_CLOCK_MIN_HEAP_FOR_WIFI`,
57344 B) rather than risking an abort deep inside the PHY. Measured free before
bring-up is ~145 KB, so the floor has 2.5x of headroom.

**The repository was flattened**, `firmware/` into the root.

**The README's screenshots are generated, not photographed.** The old photo
predated two redesigns and still showed the date on the top bar. Both screens
are now drawn from the layout constants, so they cannot quietly go stale.

### Fixed

**The clock tick leaked 32 bytes a second, and that was the reboot.**
`ClockTime_UtcToEpoch` took a UTC epoch out of newlib the obvious way —
`setenv("TZ","UTC0")`, `mktime`, `setenv` back — and newlib reallocates the
environment entry whenever the replacement value is longer, leaking the old
buffer. The main loop converts once a second: 2160 B/minute, measured flat over
fourteen minutes. That is ~130 KB an hour, so the heap emptied in about an hour
and then sat at the floor; at the next Wi-Fi bring-up a ~60 byte
`MALLOC_CAP_INTERNAL` allocation failed inside `phy_track_pll_init`, where IDF's
`ESP_ERROR_CHECK` aborts and reboots the board.

Replaced with `days_from_civil` — pure arithmetic, no libc state, no
allocation. Not by preference: this toolchain has no `timegm` in its headers or
its `libc.a`, and picolibc ships it only under `test/`. Checked against
reference `timegm` before flashing, including the value the self-test pins, a
leap day and the 2038 boundary. Verified after: thirteen consecutive minutes at
+0 bytes.

Every earlier hunt missed it because they all tested the fetch path, which is
clean. The leak was in the per-minute UI tick — the surface `wifi_stress.c`
never touched. The `alloc_watch` guard added earlier in this branch is what made
it findable: it converted the reboot into a stale reading, so the evidence
survived.

**A failed sync no longer bricks the clock.** It used to spin forever on
"Please reset the device" — a flat access point, a slow DHCP lease or one
low-memory moment left a dead-looking board waiting for a button this hardware
does not have. It now carries on and retries, and the retry rides the existing
weather slot: forcing it on its own turned a missed sync into a Wi-Fi attempt
every single minute.

**It no longer states a time it does not have.** This build has no backup cell
on the PCF85063, so the RTC survives a reset but not an unplug, after which the
chip returns a well-formed *wrong* time. Bit 7 of the seconds register is the
oscillator-stop flag that says so, and `Pcf85063_GetTime()` masks it off to read
the seconds — so it needed reading separately. A cold boot with no network shows
`--:--` until a sync lands.

**A failed weather refresh no longer reboots the device.** `ESP_ERROR_CHECK`
aborts, which is defensible at first-boot bring-up and wrong on anything that
runs repeatedly: a failed refresh should cost a stale reading, not an uptime.

**The RTC stores UTC**, with the timezone applied only for display.

**One connect attempt per bring-up.** `WifiSync` was calling `esp_wifi_connect()`
while the `STA_START` handler was already doing so.

**Display timing**: wait for the actual SPI transfer rather than a padded delay;
batch the flush callback's send to once per refresh pass; restore the
post-refresh settling delay, which was the real cause of the vertical roll.

**Layout fixes** found by measuring rather than by eye: top-bar readings sized
for their widest values; the face date sized for the widest date rather than a
lucky one (`Fri 21` fitted, `Wed 24` did not); the restart diagnostic made
actually visible, having been drawn off the bottom of a 300px panel.

### Removed

**Windowed partial refresh.** Added, reverted, restored and reverted again —
the vertical roll it was meant to fix turned out to be the missing settling
delay. The spec records the clean re-test rather than pretending the detour did
not happen.

### Known issues

None outstanding. The `ESP_ERR_NO_MEM` abort inside `phy_track_pll_init` that
this branch carried as unexplained was found and fixed on 2026-09-12 — see
**Fixed: the clock tick leaked 32 bytes a second** below.

### Unverified

The `--:--` path is logically exercised but has not been reproduced on hardware:
raising the oscillator-stop flag needs a real power cycle, and USB keeps the
board powered.
