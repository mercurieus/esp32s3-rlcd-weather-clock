# Cal+Clock Vista redraw and accurate SPI transfer wait

Date: 2026-08-29
Status: approved for planning

## Context

This is milestone 4 of
`docs/superpowers/specs/2026-08-26-widget-navigation-and-settings-design.md`,
written out and refined into its own spec the same way milestones 1-3 became
`docs/superpowers/plans/2026-08-26-navigation-foundation.md`. It formalizes
the visual decisions made in a mockup session and captured in
`TEMP-visual-design-notes.md` (delete that file once this spec is committed —
its content is folded in below, with one section explicitly superseded, see
"Nav model" below), resolves the one question that session left open, and
adds a small, unrelated fix to the SPI transfer path found worth doing at the
same time since milestone 4 is what makes it matter (a live second hand,
ticking every second, is the first thing on this device that benefits from
not padding every refresh with a guessed delay).

Two things surfaced during brainstorming are explicitly **out of scope** here
and noted for their own future spec:

- **Windowed/partial SPI refresh.** Still deferred, as in the parent spec's
  non-goals. The transfer-wait fix below is an orthogonal, lower-risk
  improvement chosen instead — it does not reduce bytes transferred, only
  removes a padded guess, and touches none of the panel's undocumented
  column/row addressing.
- **A "weather station" screen** (live current conditions, hourly forecast,
  an analog clock, styled after a classic weather station instrument). A real
  idea, but it is a new top-level screen with its own API surface
  (`hourly=...`, `current_weather=true` — both beyond what milestone 5
  already plans) and its own visual language. Noted on the roadmap; not
  designed here.

## Goals

- Redraw the existing Cal+Clock screen after the Windows Vista/7 taskbar
  clock flyout: plain analog dial with a working second hand, month
  calendar grid, metal-styled header/bezel/buttons via dithering.
- Working month browsing via the `<`/`>` arrows, replacing the parent
  spec's per-day focus (see "Month browsing" below for why).
- Replace the SPI flush path's blind `vTaskDelay(50)` with an accurate wait
  on the transfer's actual completion.

## Non-goals

- Anything in the "out of scope" list above.
- Any change to `nav.c` (see "Nav model" below).
- Double-buffering `DispBuffer` for a truly non-blocking transfer. Considered
  during brainstorming and rejected for now as more complexity than this
  milestone's payoff justifies — see "SPI transfer wait" below. Left as a
  future option if the accurate-wait version ever proves insufficient.

## Nav model (resolved)

Main and Cal+Clock remain two equal, symmetric top-level screens, cycled by a
short Select press, exactly as implemented in the navigation-foundation plan.
**No `nav.c` change.**

This explicitly supersedes `TEMP-visual-design-notes.md`'s "Calendar
drill-down, not buttons-on-a-tile" section in full: the idea of Main gaining
a sixth focus tile that opens Cal+Clock as a Detail overlay is rejected.
There is no overview/opened distinction — Cal+Clock is drawn the same way
however you arrive at it. The round `< >` month buttons described there as
living "only in the opened view" simply don't apply to that framing; see
"Dial and grid" below for where month browsing actually lives.

## SPI transfer wait

**Current state.** `Lvgl_FlushCallback` (`main/main.cpp:17`) writes the
dirty pixels into `DispBuffer` via `RLCD_SetPixel`, calls
`RlcdPort.RLCD_Display()`, which sends the whole buffer through
`esp_lcd_panel_io_tx_color()` — itself asynchronous, queuing onto SPI/DMA and
returning quickly — then immediately calls `lv_disp_flush_ready()`.
`Lvgl_Refresh()` (`components/app_bsp/lvgl_bsp.cpp:37`) then unconditionally
sleeps 50 ms before returning to the caller, on every single refresh. That
delay is not just a wasted guess: `DispBuffer` is a single, persistent
full-frame buffer (`components/port_bsp/display_bsp.cpp`), not
double-buffered, so the delay is what currently prevents the next refresh's
`RLCD_SetPixel` calls from mutating a buffer whose previous contents are
still being DMA'd out — a real, if accidental, safety mechanism.

**Change.** Register `on_color_trans_done` in `esp_lcd_panel_io_spi_config_t`
(`components/port_bsp/display_bsp.cpp:28`), backed by a counting or binary
FreeRTOS semaphore signalled from that callback. `RLCD_Display()` waits on it
before returning, rather than the caller sleeping a fixed 50 ms afterward.
Concretely:

- `DisplayPort` gains a `SemaphoreHandle_t xfer_done_` member: a **binary**
  semaphore (only "is a transfer outstanding" matters, never a count),
  created in the constructor and given once immediately so the first call
  doesn't block forever waiting for a transfer that never started.
- `io_config.on_color_trans_done` is set to a small static trampoline that
  gives the semaphore from `user_ctx` (the `DisplayPort*`).
- `RLCD_Display()` takes the semaphore (blocking) as its **first** step —
  this waits for the *previous* transfer to finish, guaranteeing `DispBuffer`
  is safe to have been mutated by the caller's `RLCD_SetPixel` calls that
  already happened before this call — then sends the new buffer.
- `Lvgl_Refresh()` drops its `vTaskDelay(50)` entirely.

This still blocks the caller — `DispBuffer` being single-buffered means it
must — but for the transfer's actual duration (≈24 ms at the current 10 MHz
SPI clock for the full 30000-byte buffer) rather than a padded 50 ms guess,
and it is now a guarantee instead of a hope. True non-blocking would need
double-buffering `DispBuffer` and tracking render-pass boundaries across
possibly multiple `PARTIAL`-mode flush calls per refresh — rejected above as
more complexity than this milestone's payoff justifies.

**Cross-checked against the sibling `esp32s3-rlcd-smart-clock` project**,
which shares this exact driver (`DisplayPort`, same `RLCD_Display()`, same
`esp_lcd_panel_io_tx_color`, same 10 MHz `pclk_hz`) with no LVGL involved —
its own animation loop (`main/display_animations.cpp:74`) calls
`RLCD_Display()` and waits only 5 ms, not 50, for the same 30000-byte
buffer. Neither project has a completion callback; that 5 ms is as much a
guess as our 50 ms, just a tighter one, and is exposed to the same
single-buffer race in principle even if it hasn't produced visible
artifacts there in practice. It's useful evidence that the true transfer
time is well under 50 ms, but it is not a pattern worth copying — it
confirms the direction (replace the guess with a real wait), not a number
to borrow.

**Testing.** Build and flash; confirm on hardware that the Main and
Cal+Clock screens still render correctly and nothing regresses (this is a
timing-only change, so "looks identical, feels identical or snappier" is the
bar — no new automated test is meaningful here).

## Dial and grid

**Dial:** plain Vista style — ticks only, no numerals, no chapter ring, no
outlined hand polygons (the engraved/skeuomorphic treatment explored earlier
was rejected and must not be resurrected). Geometry:

| Element | Spec |
|---|---|
| Dial position | x 238..388, y 62..212 |
| Major ticks | 12, 3 px wide, 10 px long |
| Minor ticks | 60 (not 12 — see below), 2 px wide, 4 px long |
| Hour hand | 5 px wide, 34 px long |
| Minute hand | 3 px wide, 52 px long |
| Second hand | 2 px wide, 58 px long |
| Hub | 9x9 px square |
| Colour | all pure black — no `0x888888` on this panel; that grey is what made the parent spec's original second hand thresholded invisible |

Minor ticks are 60, not the 12 the parent spec originally specified: at
r=58 adjacent 60-tick spacing is ~6.3 px, wide enough for a 2 px tick to
survive the 1-bit threshold; a 1 px tick does not, which is what produced
speckle noise when this was first tried.

**Grid:** unchanged from the parent spec — 7 fixed-width centred columns,
header as line 0, `CAL_COL_X0 = 6`, `CAL_COL_W = 30`, `CAL_ROW_H = 20`, pitch
from `text_line_space` (not `pad_row`). Leading/trailing days render in
montserrat_12 against the current month's montserrat_14, reading as
subordinate without needing a second colour this panel doesn't have.

**Month browsing:** as the parent spec already specifies — advancing focus
past the last day of the displayed month browses into the next month at day
1 (and symmetrically backward). The `< >` glyphs next to the month/year
header are a static visual affordance for this, not separate focusable
elements — no additional nav.c or focus-count change.

## Metal via dithering

Two techniques, both native to 1-bit displays, applied to structure only —
**never the dial face**, which stays the plain style specified in "Dial and
grid" above:

- **Ordered dithering** (Bayer 4x4 matrix): a real per-pixel grey gradient,
  thresholded through the matrix *before* LVGL sees it — a plain grey fill
  collapses to solid under the panel's hard threshold, so the gradient must
  already be dithered pixels by the time it's drawn.
- **Bevel pairs** for relief: a light edge on one side, dark on the other,
  two 1 px lines per edge.
- **Hard rule:** never dither behind text — it destroys glyphs at this
  panel's pitch. Anything carrying type sits on plain white.

Applied to: the header rail (dithered band, dark lower lip, bright ridge
about a third of the way down, dark lower lip again — a "rolled rail" rather
than a flat bar), the dial's bezel, and the month `< >` buttons (shaded via
hemisphere-normal-against-light-vector, not a flat gradient fill). The
shared top bar's four readings (temp, humidity, date, battery) sink into the
rail as recessed white plaques — dark edge top-left, light edge
bottom-right — which is what keeps their text off the dither per the hard
rule above.

**Hardware verification required before committing to this.** The mockup's
dither assumes the panel renders isolated single pixels cleanly; if adjacent
dithered pixels bleed together the checker pattern collapses to flat grey
and the whole technique needs rethinking. This is the first implementation
step (see "Implementation notes" below) — flash a standalone test pattern
(the tone-ramp swatches from the mockup) and confirm on the actual screen
before writing any of the bezel/rail code for real.

Exact pixel geometry for the metal treatment (dither matrix tiling, bevel
positions, rail band y-ranges) was worked out live in the mockup as
JavaScript, not yet transcribed into tables here — the implementation plan
must derive and record these the same way the rest of this document does,
not eyeball them from the mockup.

## Month browsing (revises the parent spec)

The parent spec gave Cal+Clock per-day focus (1..N days) with wrap-forward
into the next month and wrap-backward into the previous, plus a day-detail
overlay (weekday, ISO week, day of year, days from today, weather). Dropped
during brainstorming: per-day focus isn't needed, only month browsing is —
and `nav.c` has no backward-navigation event at all (`SELECT_SHORT` only
ever moves focus forward, wrapping), so per-day "retreat before day 1" was
never actually reachable through the two-button vocabulary as originally
worded. **This whole day-focus and day-detail design is replaced** by:

Cal+Clock gains exactly two focusables: the `<` and `>` month arrows
(`nav_focus_count(NAV_SCREEN_CALCLOCK) = 2`, index 0 = previous, index 1 =
next — up from 0 today). Select cycles focus between them, exactly as
`nav.c` already does for Main's five. OK on a focused arrow shifts the
*displayed* month in that direction and redraws the grid.

`nav.c` still has no notion of "perform an action, don't open a view" — OK
on a focused element unconditionally enters `NAV_DETAIL`. Cal+Clock uses the
same escape hatch `NAV_SETTINGS` already relies on ("caller acts on the row,
then redraws"): `clock_task.c` recognizes `mode == NAV_DETAIL && screen ==
NAV_SCREEN_CALCLOCK`, performs the month shift, and immediately sets
`s_nav.mode` back to `NAV_FOCUS` in the same handler — the user never sees a
detail view, an OK press on `<`/`>` just feels like it directly changes the
month, and repeated presses browse further. No `nav.c` change.

**Displayed vs. real "today."** A new pair of statics,
`s_cal_disp_year`/`s_cal_disp_month`, tracks which month is on screen,
independent of the RTC's real date. It resets to the real current month
every time Cal+Clock is loaded (leaving it, then coming back later, always
starts back at the current month rather than wherever browsing left off —
predictable over clever). The "today" highlight is shown only when the
displayed month equals the real current month; otherwise it's hidden. A
real-world midnight rollover while the display is on a browsed-away month
does not snap the view back — only the highlight's visibility, computed
fresh each redraw, reacts to it.

There is no day-detail overlay in this milestone. If per-day detail is
wanted later, it needs its own design — including how to reach a specific
day without a backward nav event — not assumed here.

## Roadmap note

Added to the backlog, not designed here: a new top-level screen showing live
current outdoor conditions, an hourly forecast, and an analog clock styled
after a classic weather station instrument. Needs its own brainstorm
(new API surface, new screen in `nav.c`, new visual language) when its turn
comes.

## Implementation notes

Suggested step order for the implementation plan:

1. **SPI transfer wait fix** — self-contained, no visual dependency, lowest
   risk. Verify on hardware that nothing regressed.
2. **Dither hardware verification** — flash the tone-ramp test pattern,
   confirm pixels don't bleed, *before* writing any bezel/rail code.
3. **Dial and second hand** — geometry above, verify on hardware (the second
   hand not going invisible, in particular, is the whole point of this
   milestone per the parent spec).
4. **Grid, month browsing (plain `<`/`>` hit-boxes, no styling yet),
   leading/trailing day styling.**
5. **Header rail, bezel, and the `<`/`>` buttons' final dithered/bevelled
   look** — only once step 2 has confirmed the technique holds up; the
   arrows' focus/action logic from step 4 is untouched, only their paint.
6. **Geometry test coverage** — assert against real font metrics as the
   parent spec's testing section describes, not eyeballed.

## Testing

- Layout geometry asserted against real font metrics parsed from LVGL's
  font tables (the method that caught the column alignment and
  quote-wrapping problems in `ccffbf5`), not eyeballed.
- Dial tick/hand geometry checked for 1-bit-threshold survival the same way
  the minor-tick width was derived above (spacing at radius vs. stroke
  width), not just visually.
- SPI transfer wait: hardware smoke test only, as noted above — no
  meaningful automated test for a timing-only change.
- Dither hardware verification: the tone-ramp test pattern step above is
  itself the test; its result (bleed or no bleed) gates whether step 5 in
  "Implementation notes" proceeds as designed or needs rethinking.
