# Roadmap and open leads

The design work lives in `docs/superpowers/specs/`; this file is the index of
what is left, not a replacement for it. Anything marked **specced** has a
written design already — read it before starting, it carries decisions that are
not obvious from the code.

## Planned milestones

From `docs/superpowers/specs/2026-08-26-widget-navigation-and-settings-design.md`.
Milestones 1–4 are shipped: RTC stores UTC, input layer and nav state machine,
shared overlay on `lv_layer_top()`, and the Cal+Clock Vista redraw with the
dial, month browsing and the digital readout on the face.

### 5. Weather API extension, sun and moon — **specced** (§"Sun and moon", §"Weather API extension")

Three more daily fields at **no extra network cost** — the request is already
being made:

```
&daily=weathercode,temperature_2m_max,temperature_2m_min,
       sunrise,sunset,precipitation_probability_max,wind_speed_10m_max
```

`WeatherDay` gains `sunrise_min`, `sunset_min` (minutes past local midnight),
`precip_prob`, `wind_max`. `timezone=auto` is already set, so the returned sun
times are local.

Moon phase is computed locally, no network:

```
phase = frac((jd - 2451550.1) / 29.530588853)    /* ref new moon 2000-01-06 */
illum = (1 - cos(2*pi*phase)) / 2
```

Named across eight buckets. Currently `weather.c` fetches only the three
original fields and `WeatherDay` has no sun/moon members at all.

### 6. Settings screen and NVS — **specced** (§"Settings", §"NVS schema")

`NAV_SETTINGS` exists in the nav state machine and is covered by `nav_test.c`,
but there is no screen behind it. One scrolling list: Select moves the row, OK
cycles the value or runs the action, long Select exits.

- **Display** — 24h/12h · date format (Weekday `Fri 26 Aug` default / `DD.MM.YYYY`
  / `YYYY-MM-DD` / `MM/DD/YYYY`) · °C/°F · seconds on/off · week starts Mon/Sun
- **Time** — timezone from a preset POSIX-TZ table · sync hour 1 · sync hour 2
- **Actions** — sync weather now · sync time now · reboot
- **Info** (read-only) — firmware version · uptime · battery mV and % · free
  LVGL heap, internal heap, PSRAM · SSID and RSSI · last NTP sync · RTC state

Note the spec predates the Kconfig migration: several of these now also exist
as build-time options under **Weather Clock**. Decide per row whether NVS
overrides Kconfig or replaces it — the spec does not answer that.

The 3-letter month table the Weekday format needs does not exist anywhere in
the firmware yet; `WEEKDAY_NAMES` in `clock_task.c` is the closest thing.

### 7. Chimes — **specced** (§"Chimes", §"Timbre", §"What plays", §"Scheduling")

Synthesised Westminster quarters plus the hour strike, through the ES8311
codec. Nothing exists in the tree: no `chime`, no `es8311`, no audio task.

Risks the spec already calls out, worth re-reading before starting:

- **A speaker may not be fitted** — the board only provides a header. Chimes
  default to off, the ES8311 is probed over I2C at boot, and the rows render as
  "no audio hardware" rather than failing if it does not answer.
- **Playback is long** — Westminster on the hour plus twelve strikes runs about
  45 s, so it needs its own task and must never block the display tick.

## Backlog, not yet designed

- **Web interface.** Discussed but never specced — there is no design document
  for it. Needs its own brainstorm: what it serves (settings? readings? OTA?),
  whether the radio stays up for it or it is on-demand, and what that does to
  the power budget, which currently assumes Wi-Fi is down except for a few
  seconds a day.
- **A weather-station screen.** From the Cal+Clock spec's roadmap note: a new
  top-level screen with live current conditions, an hourly forecast, and an
  analogue clock styled after a classic instrument. New API surface, new screen
  in `nav.c`, new visual language — its own brainstorm when its turn comes.
- **The Cal+Clock bottom strip.** The four-day forecast row at `CAL_FC_Y` is
  the obvious place for Google Calendar events, or for the sun/moon strip from
  milestone 5. Those two want the same space, so decide between them rather
  than discovering the collision mid-implementation.
- **Trend arrows** for indoor temperature and humidity — space is already
  reserved at x68 and x159, but nothing stores previous readings, so reading
  history has to be plumbed first.
- **Dithered ghost digits** for adjacent-month calendar cells. They currently
  render a font size down, which works but is not what the spec pictured.

## Unverified

- **The `--:--` path.** A cold boot with no network shows dashes instead of a
  confident wrong time. The logic is exercised, the display is not: raising the
  PCF85063's oscillator-stop flag needs a real power cycle, and USB keeps the
  board powered.

## Closed 2026-09-12: free DRAM falling per *failed* Wi-Fi connect

Kept as a worked example. This was recorded as a real lead — free internal DRAM
fell ~1.8 KB per failed bring-up across five cycles (-1836, -1816, -1892 B) —
and it read like a leak in the failed-connect path, the one path
`wifi_stress.c` never exercised.

It was not a separate leak. It was the per-tick `setenv` leak in
`ClockTime_UtcToEpoch`, fixed in `80d61b1`, seen through a minute that happened
to contain a failed connect. At the measured 2160 B/minute — 36 B/s — those
three figures are 51, 50 and 53 seconds of ticks. A failed connect blocks the
1 Hz loop for three 15 s attempts plus teardown, so such a minute holds fewer
than sixty conversions. That is why it read *below* the steady rate, and so did
not look like the same thing.

The decline was real, the rate was real, the attribution was wrong.

## Method notes worth keeping

Two measurement mistakes cost most of a day between them, and both are the same
shape: trusting a number without checking what it was actually measuring.

1. **`heap_trace_stop()` before a settle** reports every `TIME_WAIT` socket as a
   leak, with a convincing call stack, at any settle length. If the number does
   not move when you change the measurement, suspect the measurement. See
   `34702d3` on the branch history.
2. **Ground-truth a heap claim against the subsystem's own bookkeeping.** lwIP
   keeps `tcp_active_pcbs` / `tcp_tw_pcbs` / `tcp_bound_pcbs`; walking them
   settled in one run what three heap traces got wrong.
3. **A backtrace cannot cross a thread boundary.** `netconn_new` posts a message
   and lwIP's `tcpip` thread allocates, so the stack ends there no matter how
   large `CONFIG_HEAP_TRACING_STACK_DEPTH` is.
4. **Dumping a few hundred trace records over USB serial trips the interrupt
   watchdog.** Keep the window short and expect the reboot.
5. **The per-minute heap line in `clock_task.c` is the canary.** It made a
   2160 B/minute leak visible in one capture. Leave it in.
