# Open leads

## Closed 2026-09-12: free DRAM falling per *failed* Wi-Fi connect

Recorded here as suggestive but unconfirmed: free internal DRAM fell ~1.8 KB
per failed bring-up across five cycles (-1836, -1816, -1892 B). It read like a
leak in the failed-connect path, which was the one path `wifi_stress.c` never
exercised.

**It was not a separate leak.** It was the per-tick `setenv` leak in
`ClockTime_UtcToEpoch`, fixed in `dbf339d`, seen through a minute that happened
to contain a failed connect. At the measured 2160 B/minute — 36 B/s — those
three figures are 51, 50 and 53 seconds of ticks. A failed connect is three
attempts at a 15 s timeout plus teardown, and the 1 Hz loop is blocked
throughout, so a minute containing one holds fewer than sixty conversions. That
is why the number sat *below* the steady rate rather than above it.

Worth keeping as a worked example: a number measured in the wrong units of time
looks like a new phenomenon. The decline was real, the rate was real, and the
attribution was wrong.

Verified after the fix: thirteen consecutive minutes at +0 bytes.

## Open

- **Trend arrows** for indoor temp and humidity — space reserved at x68 and
  x159, needs reading history stored first.
- **Google Calendar events** on the calendar screen, possibly replacing the
  four-day forecast block.
- **Dithered ghost digits** for adjacent-month calendar cells.
- **The `--:--` path is unverified on hardware.** Raising the PCF85063's
  oscillator-stop flag needs a real power cycle, and USB keeps the board
  powered. The logic is exercised; the display is not.
- **Consider hoisting the UTC conversion out of the per-second path.** Seconds
  are timezone-invariant, so the dial's second hand can take `tm_sec` straight
  from the chip and the date/hour/minute conversion need only run when the
  minute rolls. Worth nothing measurable now that the call is allocation-free
  and about 50 ns, so this is tidiness rather than performance.

## Method notes worth keeping

Two measurement mistakes cost most of a day between them, and both are the same
shape — trusting a number without checking what it was actually measuring.

1. **`heap_trace_stop()` before a settle** reports every `TIME_WAIT` socket as a
   leak, with a convincing call stack, at any settle length. If the number does
   not move when you change the measurement, suspect the measurement. See
   `34702d3`.
2. **Ground-truth a heap claim against the subsystem's own bookkeeping.** lwIP
   keeps `tcp_active_pcbs` / `tcp_tw_pcbs` / `tcp_bound_pcbs`; walking them
   settled in one run what three heap traces got wrong.
3. **A backtrace cannot cross a thread boundary.** `netconn_new` posts a message
   and lwIP's `tcpip` thread allocates, so the stack ends there no matter how
   large `CONFIG_HEAP_TRACING_STACK_DEPTH` is.
4. **Dumping a few hundred trace records over USB serial trips the interrupt
   watchdog.** Keep the window short and expect the reboot.
