# Windowed Partial Refresh Design

## Context

`DisplayPort::RLCD_Display()` (`firmware/components/port_bsp/display_bsp.cpp`) always
ships the entire 15000-byte packed panel buffer over SPI, addressed to the panel's
full fixed column/row window, regardless of how small the actual dirty rect was.
This was a known, documented limitation (see the comment above `RLCD_Display()`)
carried since before this session.

During execution of the Cal+Clock Vista redraw plan
(`docs/superpowers/plans/2026-08-29-calclock-vista-redraw.md`), Task 1 fixed a real
DMA race by adding an accurate SPI transfer-done wait
(`DisplayPort::RLCD_WaitTransferDone()`), then a follow-up fix batched multiple
LVGL dirty-rect flushes into a single send per refresh pass. Both fixes are
hardware-verified and committed. Neither one caused the following symptom, but
removing the DMA race made it visible where it had previously been masked by
undefined racy behavior:

Large-area screen updates (switching between Main and Cal+Clock, the hint overlay
showing/hiding) show a slow, visible top-to-bottom "roll" - roughly 50px/second,
so a full 300px-tall change takes around 6 seconds to visually settle. A tiny
change (the once-a-second colon blink) shows no such roll, even though it goes
through the exact same full-panel SPI send.

Two hypotheses were tested and hardware-refuted before this design:

- **SPI transfer speed.** Ruled out by the numbers alone: `DisplayLen` is 15000
  bytes at a 10MHz SPI clock, ≈12ms to transmit - three orders of magnitude too
  fast to be the visible cause.
- **FRCTRL (0xB2) frame-rate register.** The panel's `RLCD_Init()` ends by
  switching to LPM (Low Power Mode, ~1Hz) instead of staying in HPM (High Power
  Mode, ~25.5Hz) - a plausible cause, since 1Hz means roughly a second per frame.
  Hardware-tested and refuted: switching to HPM produced no measurable change
  (~50px/sec roll in both modes). Reverted (commit `e65354b`) - HPM has a real
  battery cost with zero visible benefit.

Current best understanding: the roll is the ST7305 panel's own liquid-crystal
optical settling time (a physical property of "ultra-low-power a-Si TFT" reflective
panels, per the datasheet's own framing) - not an SPI clock-rate or addressing-window
limit on its own. It behaves like a raster-style settling scan whose visible
duration scales with how much of the panel changes state, which is exactly why a
tiny colon-blink change is imperceptible on the same underlying mechanism that
makes a full-screen change take ~6 seconds.

A windowed-refresh spike (recorded in
`.superpowers/sdd/2026-08-29-calclock-vista-redraw/progress.md`) confirmed this
mechanism can be exploited: constraining the SPI write to a smaller physical
window measurably speeds up the visible settle time for that window, without
requiring any change to the frame-rate register. This spec designs the real,
production implementation of that windowing.

## Goals

- Replace `RLCD_Display()`'s unconditional full-panel send, in the LVGL flush
  path, with a windowed send sized to the actual dirty area whenever that's
  worth doing.
- No visible regression: a windowed send must never corrupt panel content
  outside its intended area, and must never miss real dirty pixels (over-scan
  is fine, under-scan is not).
- Keep `RLCD_Display()` itself unchanged and available as the full-panel
  fallback - both for the 5+ existing call sites unrelated to LVGL (init,
  color-clear, etc.) and for the new auto-dispatch's own fallback path.

## Non-goals

- Re-deriving the CASET/RASET addressing itself - already hardware-confirmed
  (see below), not re-investigated here.
- Windowing the non-LVGL call sites (`RLCD_Init()`'s startup clear, etc.) -
  those aren't in a hot path and stay as full sends.
- Any change to the FRCTRL frame-rate register - tested, refuted, not revisited.
- Double-buffering `DispBuffer` - out of scope, unrelated to this problem.

## Confirmed hardware facts (inputs to this design)

Hardware-verified during the spike, not assumptions:

- **CASET (0x2A) = vertical (Y) axis.** Unit = 12 physical rows (3 of
  `InitLandscapeLUT()`'s `block_y` groups of 4 rows each). Full range is the
  already-in-use `0x12`-`0x2A` (18-42 = 25 units, covering the 300px height).
  Increases **downward**: CASET's low end (18-20) produced a block at the top
  of the screen, its high end (40-42) at the bottom.
- **RASET (0x2B) = horizontal (X) axis.** Unit = 2 physical columns, matching
  `byte_x` in `InitLandscapeLUT()` exactly. Full range is the already-in-use
  `0x00`-`0xC7` (0-199 = 200 units, covering the 400px width).
- **Payload size = CASET_units × RASET_units × 3 bytes.** Confirmed against
  `25 × 200 × 3 = 15000 = DisplayLen` for the full-panel case. This 3-byte
  factor corresponds exactly to `InitLandscapeLUT()`'s 3 `block_y` groups per
  CASET unit (12 rows ÷ 4 rows/group = 3).
- **An undersized RAMWR payload does not fail gracefully.** Hardware-confirmed:
  sending fewer bytes than the addressed window requires left visible garbage
  on the panel, not a clean partial fill. Getting the length exactly right is
  a correctness requirement, not an optimization.
- **`DispBuffer`'s existing layout is windowing-friendly.** It's `byte_x`-major
  (200 groups of 2 physical columns) with `block_y` (75 groups of 4 rows,
  Y-inverted) as the contiguous inner index - so for one fixed `byte_x`, a
  `block_y` sub-range is already a contiguous run in memory. No buffer
  restructure needed.

## Architecture

### New method: `RLCD_DisplayWindow`

```cpp
void DisplayPort::RLCD_DisplayWindow(int x1, int y1, int x2, int y2);
```

Takes a screen-space rectangle (inclusive, `0 <= x1 <= x2 <= 399`,
`0 <= y1 <= y2 <= 299`), rounds it to valid CASET/RASET bounds (see Rounding
below), extracts the corresponding bytes out of `DispBuffer`, and sends them
windowed. `RLCD_Display()` is unchanged and remains the full-panel path, used
directly by every non-LVGL call site and as `RLCD_DisplayWindow`'s own
fallback for near-full-screen windows.

A single private helper computes the CASET/RASET/length triple from the
rounded screen rect:

```cpp
struct RlcdWindow { uint8_t caset_xs, caset_xe, raset_ys, raset_ye; int len; };
RlcdWindow RLCD_ComputeWindow(int x1, int y1, int x2, int y2);
```

Both the payload-extraction loop and the CASET/RASET command bytes are driven
from one `RlcdWindow` value - they cannot drift apart the way the spike's own
bug did (an unmultiplied length computed separately from the command bytes).

### Rounding

**X (RASET, 2px granularity, no inversion):**

```cpp
uint8_t rs = x1 >> 1;       // round down
uint8_t re = x2 >> 1;       // x2 inclusive, integer division already floors correctly
```

**Y (CASET, 12px granularity, Y-inverted, and CASET *decreases* as screen-Y
increases):**

CASET's low end (18-20) hardware-measured at the *top* of the screen, its
high end (40-42) at the *bottom*. Since `block_y` (via `inv_y = height-1-y`)
is *largest* at the top and smallest at the bottom, CASET must be a
*decreasing* function of `block_y`: `caset = 42 - block_y/3` (verified against
both the full-panel case and a top-band example by hand below).

```cpp
int inv_y1 = height_ - 1 - y1;
int inv_y2 = height_ - 1 - y2;
int by_a = inv_y1 >> 2;
int by_b = inv_y2 >> 2;
int by_lo = std::min(by_a, by_b);
int by_hi = std::max(by_a, by_b);
int g_lo = by_lo / 3;    // group index, ascending with block_y
int g_hi = by_hi / 3;
uint8_t caset_xs = 42 - g_hi;    // larger block_y group -> smaller CASET
uint8_t caset_xe = 42 - g_lo;    // smaller block_y group -> larger CASET
```

Integer division on `g_lo`/`g_hi` already expands the window to its enclosing
12px-aligned band (a group is 3 raw `block_y` units): it can grow by up to
11px vertically, never shrinks, so it never under-scans real dirty pixels.
`RlcdWindow.len = (caset_xe - caset_xs + 1) * (re - rs + 1) * 3`.

Verified by hand: full panel (`y1=0,y2=299`) gives `by_lo=0,by_hi=74`,
`g_lo=0,g_hi=24`, `caset_xs=42-24=18`, `caset_xe=42-0=42` - matches the
existing hardcoded full-panel range exactly. A top-band rect (`y1=0,y2=33`,
the top ~34 rows) gives `by_a=74,by_b=66`, `g_lo=22,g_hi=24`,
`caset_xs=42-24=18`, `caset_xe=42-22=20` - correctly the *low* CASET end,
matching the hardware-confirmed "low CASET = top" direction.

### Buffer extraction

`DispBuffer`'s own `block_y` index always runs 0-74 ascending regardless of
what CASET means visually, so the extraction has to convert back from CASET
to that raw ascending index - using `caset_xe` (the *larger* CASET value) to
find the *start* of the contiguous run, since larger CASET corresponds to
*smaller* `block_y`/group index:

The payload is written into a **persistent scratch buffer** (`WindowBuffer`,
allocated once at full `DisplayLen` size, alongside `DispBuffer` in the
constructor) rather than a fresh `malloc`/`free` per call. `RLCD_Sendbuffera()`
queues an async DMA send and returns immediately (this is exactly the bug the
spike's own `RLCD_TestWindow` diagnostic hit: freeing a buffer right after
queuing it races the still-in-flight transfer). A persistent buffer sidesteps
this the same way `DispBuffer` itself already is safe: the *next* batch's
existing `RLCD_WaitTransferDone()` (Task 1, called before that batch's first
pixel write) guarantees any previous transfer - including one reading from
`WindowBuffer` - has completed before either buffer is touched again. No new
wait or malloc/free churn needed inside the windowed send itself.

```cpp
int H4 = height_ >> 2;   // 75, already the stride InitLandscapeLUT() uses
int cursor = 0;
int by_start = (42 - caset_xe) * 3;             // = g_lo * 3
int run_len  = (caset_xe - caset_xs + 1) * 3;
for (int bx = rs; bx <= re; bx++) {
    memcpy(WindowBuffer + cursor, &DispBuffer[bx * H4 + by_start], run_len);
    cursor += run_len;
}
```

Verified by hand against the same two cases: full panel gives
`by_start=(42-42)*3=0`, `run_len=(42-18+1)*3=75` - the entire per-`byte_x`
span, correct. The top-band rect gives `by_start=(42-20)*3=66`,
`run_len=(20-18+1)*3=9` - `block_y` 66-74, the topmost 9-row-group span,
correct.

One `memcpy` per `byte_x` column in range - `(re - rs + 1)` copies, each of a
contiguous run already sitting in `DispBuffer`.

### Fallback threshold

If the rounded window's CASET span covers more than ~80% of the panel's
height (`caset_xe - caset_xs + 1 >= 20` of the 25 total units), skip
windowing and call the existing `RLCD_Display()` instead - the copy and
addressing overhead isn't worth it when the window is nearly the full panel
anyway. This threshold is expressed in CASET (vertical) units specifically,
since vertical span is what the spike measured as the lever that speeds up
settling; a window's horizontal extent doesn't independently affect this
decision.

### Batch integration (`Lvgl_FlushCallback`, `firmware/main/main.cpp`)

Mirrors Task 1's existing `s_batch_in_progress` pattern with a per-batch union
rectangle:

```cpp
static bool s_batch_in_progress = false;
static int s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2;

if (!s_batch_in_progress) {
    // first flush of the batch: initialize the union rect from area
    s_batch_x1 = area->x1; s_batch_y1 = area->y1;
    s_batch_x2 = area->x2; s_batch_y2 = area->y2;
    s_batch_in_progress = true;
} else {
    // later flushes: widen the union rect
    s_batch_x1 = min(s_batch_x1, area->x1);  s_batch_y1 = min(s_batch_y1, area->y1);
    s_batch_x2 = max(s_batch_x2, area->x2);  s_batch_y2 = max(s_batch_y2, area->y2);
}

// on the last flush, instead of RlcdPort.RLCD_Display():
RlcdPort.RLCD_DisplayAuto(s_batch_x1, s_batch_y1, s_batch_x2, s_batch_y2);
s_batch_in_progress = false;
```

`RLCD_DisplayAuto(x1,y1,x2,y2)` is a thin new `DisplayPort` method: applies the
rounding, checks the fallback threshold, and calls either
`RLCD_DisplayWindow()` or `RLCD_Display()`. This keeps `Lvgl_FlushCallback`
itself simple - it just tracks the union rect and hands it off once per batch,
same "exactly one send per batch" shape Task 1 already established.

## Error handling / safety

- Window coordinates are clamped to `[0,399]×[0,299]` before rounding -
  defensive against any future caller passing out-of-range LVGL coordinates.
- The single `RLCD_ComputeWindow()` helper is the only place that computes
  CASET/RASET/length - both `RLCD_DisplayWindow()`'s command bytes and its
  payload-extraction loop read from the same `RlcdWindow` value, eliminating
  the class of bug the spike hit (an under-sized payload computed separately
  from the addressed window).
- `RLCD_DisplayWindow()` writes into the persistent `WindowBuffer` (not a
  fresh per-call allocation - see Buffer extraction above), so it needs no
  internal wait of its own: it follows the same async-send discipline as
  `RLCD_Display()` already does with `DispBuffer`. The caller
  (`RLCD_DisplayAuto`, itself called from the batch's last flush) relies on
  `Lvgl_FlushCallback`'s existing `RLCD_WaitTransferDone()`/`RLCD_Display()`
  pairing - no new semaphore logic needed, since there is still exactly one
  send per batch.

## Testing

- **Unit test (no hardware):** verify `RLCD_ComputeWindow()`'s rounding and
  length math against known values - the full-panel case
  `RLCD_ComputeWindow(0, 0, 399, 299)` must produce exactly
  `{caset_xs=18, caset_xe=42, raset_ys=0, raset_ye=199, len=15000}`, matching
  today's hardcoded `RLCD_Display()` constants. A handful of arbitrary rects
  (e.g. the top-bar region, a single forecast tile) checked by hand against
  the rounding rules above.
- **Hardware verification:** flash, exercise Main↔Cal+Clock screen switches
  and the hint overlay show/hide (the two symptoms that motivated this work),
  confirm no visible corruption and a visibly faster settle than the ~50px/sec
  baseline. Also re-verify the untouched cases (colon blink, focus frame)
  still look identical - this change must be invisible except for being
  faster.

## Roadmap note

Not in scope here, but worth recording: this design's 80%-height fallback
threshold means a full screen switch (Main to Cal+Clock and back) will likely
still fall back to a full-panel send today, since both screens differ across
nearly their whole height. The more valuable win from this work is on
same-screen compound changes that touch a bounded vertical band - the hint
overlay (a fixed-height band near the bottom), the top bar (a fixed-height
band at the top), and future Cal+Clock month-browsing arrows (Task 4 of the
paused Vista redraw plan). Screen-switch roll may need its own follow-up
(e.g. an actual crossfade-free "wipe" using two or three sequential windowed
sends) if it's still bothersome after this lands - not designed here.
