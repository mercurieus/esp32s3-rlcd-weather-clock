# Widget navigation, Vista-style Cal+Clock, and on-device settings

Date: 2026-08-26
Status: approved for planning

## Context

The firmware currently has two screens (Main, Cal+Clock) toggled by a single
button, and no way to change anything without reflashing. This design adds
two-button navigation with focus and detail views, an on-device settings menu
backed by NVS, and redraws the Cal+Clock screen after the Windows Vista/7
taskbar clock flyout.

## Goals

- Two-button navigation: cycle screens, focus elements within a screen, open
  a detail view for the focused element.
- On-device settings for display preferences, timezone, sync schedule, plus
  read-only device info and a few actions.
- Cal+Clock redrawn as the Vista/7 flyout, with the second hand restored.
- Button hints as a transient overlay, not permanent screen furniture.

## Non-goals

- Wi-Fi AP + web configuration portal. Deferred; Wi-Fi credentials stay
  compile-time in `clock_config.h`.
- Text entry of any kind. Two buttons cannot do it usefully.
- Partial-panel refresh. `RLCD_Display()` hardcodes a full-screen window;
  changing that is a separate driver change, tracked as a follow-up.

## Hardware constraints

These shape every decision below and are easy to forget.

- **400 x 300, 1 bit.** `Lvgl_FlushCallback` in `main/main.cpp` thresholds
  RGB565 at `0x7fff`. There is no grey. Any stroke thinner than 2 px is
  anti-aliased into the threshold and breaks up.
- **Full-screen render on every refresh.** `LV_DISPLAY_RENDER_MODE_FULL` plus
  a 120000-iteration threshold loop plus a 15 KB SPI write plus a 50 ms
  `vTaskDelay`. Refresh only when something changed.
- **LVGL built-in allocator, 64 KB pool** (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`).
  Object count matters. The Info page exposes free LVGL heap so this stays
  visible.
- **Light sleep between ticks.** The task wakes once a second or on a GPIO
  edge. Anything time-based must be driven from that tick; LVGL timers do not
  run on their own because `lv_timer_handler()` is only called from
  `Lvgl_Refresh()`.
- **Two usable GPIO buttons.** Select = `GPIO_NUM_18`, OK = `GPIO_NUM_0`.
  Confirmed, not assumed: the esp32s3-rlcd-smart-clock project on this same
  board does `ButtonInput s_buttons(GPIO_NUM_18, GPIO_NUM_0)`. The third case
  cutout is the PMU power button and is not software-readable.
- **ES8311 output codec on the shared I2C bus**, I2S on MCLK `GPIO_NUM_16`,
  BCLK `GPIO_NUM_9`, WS `GPIO_NUM_45`, DOUT `GPIO_NUM_8`, speaker PA enable
  `GPIO_NUM_46`. The speaker itself plugs into a header and may not be fitted.

## Architecture

New components, each with one job:

| Unit | Responsibility | Depends on |
|---|---|---|
| `components/input/buttons.[ch]` | Debounce, classify short/long press, expose events | driver/gpio, esp_sleep |
| `components/ui/nav.[ch]` | Navigation state machine. Pure C, no LVGL, no ESP-IDF | nothing |
| `components/ui/overlay.[ch]` | Top bar + hint overlay + focus frame on `lv_layer_top()` | lvgl |
| `components/settings/settings.[ch]` | Typed settings struct, NVS load/save, TZ table | nvs_flash |
| `components/ui/screen_settings.[ch]` | Settings list rendering and row actions | lvgl, settings |

`nav.c` having no dependencies is deliberate: it is the piece most likely to
harbour bugs and the only piece that can be tested away from hardware.

## Input layer

`user_config.h` gains:

```c
#define BTN_OK_PIN      GPIO_NUM_0    /* BOOT, confirmed working */
#define BTN_SELECT_PIN  GPIO_NUM_18   /* KEY - confirmed against sibling board */
```

Both configured input, pull-up, active low, with
`gpio_wakeup_enable(pin, GPIO_INTR_LOW_LEVEL)`.

On wake with cause `ESP_SLEEP_WAKEUP_GPIO`, poll both pins; whichever reads
low is the pressed one. Debounce 20 ms, then poll at 20 ms until release or
`BTN_LONG_MS` (600). A press held past 600 ms emits its LONG event
immediately, then waits for release without emitting again.

Emits exactly one of: `BTN_EV_SELECT_SHORT`, `BTN_EV_SELECT_LONG`,
`BTN_EV_OK_SHORT`, `BTN_EV_OK_LONG`.

This replaces the inline busy-wait currently in `clock_task`.

## Navigation state machine

```c
typedef enum { NAV_SCREEN, NAV_FOCUS, NAV_DETAIL, NAV_SETTINGS } nav_mode_t;
```

| Mode | Select short | Select long | OK short | OK long |
|---|---|---|---|---|
| `SCREEN` | next screen | — | enter `FOCUS` (index 0) | `SETTINGS` |
| `FOCUS` | next element (wraps) | back to `SCREEN` | open `DETAIL` | `SETTINGS` |
| `DETAIL` | — | back to `FOCUS` | back to `FOCUS` | `SETTINGS` |
| `SETTINGS` | next row (wraps) | exit to `SCREEN` | cycle value / run action | — |

Screen cycle: **Main -> Cal+Clock -> Main**. Settings is modal and not in the
cycle. Detail is a transient overlay, not a screen.

Focusable elements:

- **Main**: `[indoor sensor] [day1] [day2] [day3] [day4]` — 5 elements.
- **Cal+Clock**: the days of the displayed month, 1..N. Advancing past N
  browses to the next month at day 1; retreating before 1 browses to the
  previous month at its last day. This is what makes the `< >` month arrows
  meaningful.

`nav_handle(nav_state_t *, btn_event_t)` returns a `nav_action_t` describing
what the caller must redraw. The state machine itself touches no widgets.

## Shared overlay on `lv_layer_top()`

`lv_layer_top()` floats above whichever screen is loaded and survives
`lv_screen_load()`. Moving the top bar there **removes the duplication added
in ccffbf5**: `calendar_update_top_bar()` and the calendar's second widget set
are deleted, and one set of temp/humidity/date/battery widgets serves every
screen.

The focus frame also lives here, positioned in absolute screen coordinates, so
one frame object can outline an element on any screen — including the top bar
itself, which is how Main's `[indoor sensor]` element gets highlighted.

Screens shrink to y 38..300. Main barely moves: it already had a rule at y=34,
so deleting its own top-bar widgets leaves the rest in place.

### Hint overlay

A hidden box on `lv_layer_top()`, roughly 320x40 centred near the bottom,
showing the current mode's button labels:

```
   Select: next day        OK: details        hold OK: menu
```

Shown for 5 s at boot and 2.5 s after any button event. **Auto-hide is driven
from the one-second task tick, not an LVGL timer** — `lv_timer_handler()` only
runs inside `Lvgl_Refresh()`, so a timer would never fire while idle. The task
holds `hint_until_ms` and hides the overlay on the first tick past it.

Cost: one extra refresh roughly 2.5 s after each button press.

## Cal+Clock screen

```
+--------------------------------------------------+  y
|  25.6C 63%   19.07.2026              [4.14]      |  0..36   shared top bar
|==================================================|  34..36  rule
|            Wednesday, 26 August 2026             |  38..56  montserrat_16
|   <  August 2026  >            .-''''''-.        |  58..74  montserrat_14
|   Mo Tu We Th Fr Sa Su       .'    |     '.      |
|   28 29 30 31  1  2  3      /      |       \     |  76..216 grid, 7 x 20
|    4  5  6  7  8  9 10     |       o----    |    |  62..212 dial, 150 sq
|   11 12 13 14 15 16 17     |      /         |    |
|   18 19 20 21 22 23 24      \    /         /     |
|   25[26]27 28 29 30 31       '.          .'      |
|    1  2  3  4  5  6  7         '-......-'        |
|                                  19:03:42        |  214..232 montserrat_16
|--------------------------------------------------|  236     rule
| (~)Su  (/)Mo  (~)Tu  (~)We                       |  240..276 weather row
| 25/15  23/13  20/14  22/10                       |
|--------------------------------------------------|
| Sunrise 05:52  Sunset 20:31  Day 14h39m  Moon 68%|  280..295 montserrat_12
+--------------------------------------------------+
```

Geometry constants (all derived, none eyeballed):

- Grid columns keep the current scheme: 7 fixed-width centred labels, header
  as line 0, `CAL_COL_X0 = 6`, `CAL_COL_W = 30`, `CAL_ROW_H = 20`, pitch from
  `text_line_space`, not `pad_row`.
- Dial `x 238..388`, `y 62..212`.
- Today box and focus frame both derive from the column constants, so they
  cannot drift off a cell.

### Leading and trailing days

Vista greys them. There is no grey on this panel. They render in
**montserrat_12 against the month's montserrat_14**, which reads as
subordinate without needing a second colour.

### Dial

- 12 major ticks, 3 px wide, 10 px long.
- 48 minor ticks, 2 px wide, 4 px long. At r=68 adjacent ticks are ~7 px
  apart, so 2 px survives the threshold. 1 px does not — that is what made the
  old minor ticks noise.
- Hour hand 5 px / 34 long, minute 3 px / 52, **second 2 px / 58**, all pure
  black. The old `0x888888` second hand thresholded to white, which is why it
  was invisible.
- 9x9 square hub.

### Day detail overlay

OK on a focused day opens: weekday, ISO week number, day of year, days from
today, and — if the day falls inside the 4-day forecast — its icon, high/low,
rain probability and wind.

### Sun and moon

Sunrise, sunset and day length come from Open-Meteo. Moon phase is computed
locally, no network:

```
phase = frac((jd - 2451550.1) / 29.530588853)     /* ref new moon 2000-01-06 */
illum = (1 - cos(2*pi*phase)) / 2
```

Named across eight buckets (new, waxing crescent, first quarter, waxing
gibbous, full, waning gibbous, last quarter, waning crescent).

## Weather API extension

The existing single request gains three daily fields at no extra network cost:

```
&daily=weathercode,temperature_2m_max,temperature_2m_min,
       sunrise,sunset,precipitation_probability_max,wind_speed_10m_max
```

`WeatherDay` gains `sunrise_min`, `sunset_min` (minutes past local midnight),
`precip_prob`, `wind_max`. `timezone=auto` is already set, so the returned sun
times are local.

## Settings

One scrolling list on its own screen. Select moves the row, OK cycles the
value or runs the action, long Select exits.

**Display** — time 24h/12h · date format `DD.MM.YYYY` / `YYYY-MM-DD` /
`MM/DD/YYYY` · °C/°F · show seconds on/off · week starts Monday/Sunday

**Time** — timezone from a preset POSIX-TZ table · sync hour 1 (0..23) ·
sync hour 2 (0..23)

**Actions** — sync weather now · sync time now · reboot

**Info** (read-only) — firmware version · uptime · battery mV and % · free
LVGL heap, internal heap, PSRAM · SSID and RSSI · last NTP sync · RTC state

### NVS schema

```c
#define SETTINGS_VERSION 1

typedef struct {
    uint8_t version;
    uint8_t time_24h;         /* 0/1                                    */
    uint8_t date_fmt;         /* 0 DD.MM.YYYY, 1 YYYY-MM-DD, 2 MM/DD/YY */
    uint8_t temp_f;           /* 0/1                                    */
    uint8_t show_seconds;     /* 0/1                                    */
    uint8_t week_start_sun;   /* 0/1                                    */
    uint8_t tz_index;         /* index into TZ_TABLE                    */
    uint8_t sync_hour_1;      /* 0..23                                  */
    uint8_t sync_hour_2;      /* 0..23                                  */
    uint8_t reserved[7];      /* room to grow without a version bump    */
} settings_t;
```

Namespace `clock`, key `settings`, stored as a blob. Missing or mismatched
version loads defaults and writes them back. Defaults match today's
compile-time behaviour: 24h, `DD.MM.YYYY`, °C, seconds on, Monday, EET,
05:00 / 15:00.

The TZ table holds roughly sixteen labelled POSIX strings (UTC, London,
Berlin, Kyiv, Moscow, Dubai, India, Bangkok, China, Japan, Sydney, New York,
Chicago, Denver, Los Angeles). Default is the current
`EET-2EEST,M3.5.0/3,M10.5.0/4`.

## Chimes

An old-clock chime, synthesised rather than sampled. Nothing is stored in
flash: a struck bell is a handful of inharmonic partials with exponential
decay, which is roughly twenty lines of arithmetic.

### Timbre

Each strike sums five partials at the classic bell ratios against the
strike note, with higher partials decaying faster:

| Partial | Ratio | Relative gain | Decay |
|---|---|---|---|
| hum | 0.5 | 0.30 | slowest |
| fundamental (prime) | 1.0 | 1.00 | slow |
| tierce | 1.2 | 0.50 | medium |
| quint | 1.5 | 0.25 | fast |
| nominal | 2.0 | 0.60 | fast |

Output at 16 kHz mono, matching the sibling project's codec configuration.
That is ample: the highest partial of the highest bell is under 900 Hz.

### What plays

Westminster quarters, in E major, on the four bells
E4 329.63, F#4 369.99, G#4 415.30, B3 246.94:

| Time | Phrase |
|---|---|
| :15 | G# F# E B |
| :30 | E G# F# B, B F# G# E |
| :45 | E G# F# B, B F# G# E, G# E F# B |
| :00 | G# F# E B, B E F# G#, B F# G# E, G# E F# B, then the hour strike |

**The hour strike is what makes each hour sound different**: a deeper bell
(E3, 164.81 Hz) struck once at one o'clock through twelve times at twelve,
roughly two seconds apart. This is what a grandfather clock does, and it is
the classic way an hour is audibly identifiable without looking.

Twelve o'clock therefore runs about 45 seconds. Playback lives on its own
low-priority `chime_task` fed by a queue, so the one-second display tick and
the second hand keep running throughout. Audio is generated in chunks and
streamed to `esp_codec_dev_write`; a full phrase is never buffered whole.

The PA on `GPIO_NUM_46` is enabled only for the duration of a phrase.

### Settings rows

- **Chimes** — Off / Hour strike only / Westminster + strike. Default **Off**,
  because a speaker may not be fitted and an unexpected 45-second chime is a
  poor first-boot experience.
- **Quarter chimes** — Off / On. Only meaningful with Westminster selected.
- **Quiet hours** — Off / 22:00-08:00 / 23:00-07:00 / custom start and end.
  Default 22:00-08:00. Nothing chimes inside the window.
- **Volume** — 0 to 100 in steps of 10, applied through the codec.
- **Test chime** — an action row that plays a single strike, so the speaker
  can be verified without waiting for an hour boundary.

`settings_t` gains `chime_mode`, `chime_quarters`, `quiet_start`, `quiet_end`
and `volume`, taken from the existing `reserved[7]` bytes, so the NVS version
byte does not need to change.

### Scheduling

The existing one-second tick already knows the minute boundary. The chime
scheduler fires when the minute changes to 00, 15, 30 or 45, is enabled for
that quarter, and falls outside quiet hours. It queues a request and returns
immediately.

A missed boundary is never made up: if the device was asleep, resyncing or
mid-chime, the request is dropped rather than played late.

## RTC stores UTC

`wifi_sync.c:60-61` sets `TZ` then calls `localtime_r`, so `Pcf85063_SetTime()`
writes **local** time to the RTC.

Two consequences: a runtime timezone setting would not take effect until the
next NTP sync, up to 12 hours later; and — independent of this work — **at a
DST boundary the clock stays an hour wrong until 05:00 or 15:00.**

Change: the RTC stores UTC. `Pcf85063_GetTime()` yields UTC, which is
converted to a `time_t` and passed through `localtime_r()` for display, with
`TZ` set from `settings.tz_index` at boot and whenever the setting changes.

Migration: devices flashed with this build have a local-time RTC until their
first NTP sync, after which they are correct. Worst case is one wrong-by-offset
period on first boot after update; acceptable and self-healing.

This lands as its own commit, before any navigation work, so timekeeping can
be verified in isolation.

## Refresh and power

| State | Refresh rate |
|---|---|
| Main active | 1/s (colon blink, unchanged) |
| Cal+Clock active | 1/s (second hand) |
| Settings active | on input only |
| Detail overlay | on input only |
| Any state | one extra refresh when a hint expires |

The second hand costs exactly what Main's colon blink already costs, so it is
not a regression — but it only ticks while Cal+Clock is the active screen.

## Testing

No host C compiler is available on this machine (only the Espressif
cross-toolchain), so:

- **Nav state machine** — `nav.c` stays dependency-free so it can be host
  tested later. For now, the transition table is mirrored in a Python model
  and checked exhaustively over every (mode, event, screen, focus index)
  combination against invariants: mode always valid, focus index always in
  range, every mode reachable, every mode can return to `SCREEN`, Settings
  always reachable and always exitable.
- **Layout geometry** — asserted against real font metrics parsed from the
  LVGL font tables, not eyeballed. This is the method that caught the column
  alignment and quote-wrapping problems in ccffbf5.
- **Calendar month browsing** — extends the existing 12-year harness to cover
  crossing month and year boundaries in both directions.
- **Moon phase** — checked against known new and full moon dates.
- **Settings persistence** — round-tripped on device via the Info page across
  a reboot.

## Milestones

Each builds, flashes, and is verifiable on its own.

1. RTC to UTC, timezone applied for display only.
2. Input layer + nav state machine + hint overlay. Select cycles Main and
   Cal+Clock; long OK opens a stub Settings screen. Also logs GPIO transitions
   so the Select pin can be confirmed.
3. Overlay onto `lv_layer_top()`; delete the duplicated top bars.
4. Cal+Clock Vista redraw: dial with second hand, month browsing, day detail.
5. Weather API extension and the sun/moon strip.
6. Settings screen and NVS.
7. Chimes: codec bring-up, bell synthesis, schedule, chime settings rows.

## Risks

- **A speaker may not be fitted.** The board only provides a header. Chimes
  default to off, the ES8311 is probed over I2C at boot, and if it does not
  answer the chime rows render as "no audio hardware" rather than failing.
- **Chime playback is long.** Westminster on the hour plus twelve strikes runs
  around 45 s. It must not block the display tick, hence the dedicated task.
- **LVGL 64 KB heap.** Settings rows, the hint overlay and the detail view all
  add objects. The Info page reports free LVGL heap so pressure is visible;
  if it becomes tight, the Settings list is the natural candidate for building
  rows on demand rather than up front.
- **`lv_layer_top()` persistence across `lv_screen_load()`** is assumed and
  must be confirmed early in milestone 3, since the whole overlay design rests
  on it.
- **Hint overlay refresh cost** adds one full-panel write per button press.
  Measurable, but worth watching on battery.
