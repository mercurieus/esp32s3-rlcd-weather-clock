# Open leads

## Free internal DRAM falls ~1.8 KB per *failed* Wi-Fi connect

Observed 2026-09-12 on hardware, five consecutive failed bring-ups with the
SSID pointed at a non-existent AP (three connect attempts each, then give up):

| cycle | free internal before sync |
|-------|---------------------------|
| 1     | 144 943 |
| 2     | 144 799 |
| 3     | 142 963 |
| 4     | 141 147 |
| 5     | 139 255 |

Deltas: -144, -1836, -1816, -1892. The per-cycle `delta` printed by the sync
logging was 0 every time, so whatever this is happens outside the window that
figure measures.

**Why it matters.** This is the one path none of the stress testing ever
exercised - `wifi_stress.c` only ever connects successfully. A flaky access
point would walk the heap down exactly the way the `ESP_ERR_NO_MEM` abort in
`phy_track_pll_init` implies, and that abort is still unexplained.

**Why it is not yet a leak.** It was never given a settle window. Misreading
precisely this kind of monotonic decline is what produced the phantom "208-byte
PCB leak per fetch" (see `34702d3`): normal resources still in flight look
identical to leaked ones if the measurement ends before they are released.

**How to measure it properly**, using the method that works:

1. Point `CONFIG_CLOCK_WIFI_SSID` at a non-existent AP.
2. Run failed bring-ups back to back, then wait past 2 x `CONFIG_LWIP_TCP_MSL`
   (120 s) before reading anything.
3. Stop the heap trace *after* the settle, never before it.
4. Ground-truth against the subsystem's own bookkeeping rather than the heap
   trace - `dump_tcp_pcbs()` in `wifi_stress.c` for lwIP.
5. Compare totals at two very different cycle counts. One-time setup shows the
   same byte total at 4 cycles and at 15; accumulation does not.

Production now records this on its own: `AllocWatch_Snapshot()` stores free,
largest block and low-water mark before every bring-up, and it survives a
reboot, so a board that dies in the field reports its own heap on next boot.

## Smaller

- Trend arrows for indoor temp and humidity - space reserved at x68 and x159,
  needs reading history stored first.
- Google Calendar events on the calendar screen, possibly replacing the
  four-day forecast block.
- Dithered ghost digits for adjacent-month calendar cells.
- `tools/__pycache__/` is untracked noise; add to `.gitignore`.
