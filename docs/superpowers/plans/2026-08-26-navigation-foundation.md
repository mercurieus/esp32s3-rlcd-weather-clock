# Navigation Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the clock correct UTC-based timekeeping and working two-button navigation, with one shared top bar, a focus frame, and transient button hints.

**Architecture:** The RTC switches to storing UTC with the timezone applied only at display time, which also fixes DST. A dependency-free navigation state machine sits between a debouncing button layer and the screens. Shared overlay (top bar, focus frame, hint overlay) moves onto `lv_layer_top()`, which floats above whichever screen is loaded, deleting the duplicated top bar added in ccffbf5.

**Tech Stack:** ESP-IDF 5.5.5, LVGL 9.5, FreeRTOS, picolibc, Waveshare ESP32-S3-RLCD-4.2.

**Spec:** `docs/superpowers/specs/2026-08-26-widget-navigation-and-settings-design.md`

**Covers spec milestones 1-3.** Milestones 4-7 (Vista Cal+Clock redraw, weather API extension, settings/NVS, chimes) get their own plans once this lands, because they build on interfaces this plan creates.

## Global Constraints

- **1-bit panel.** `Lvgl_FlushCallback` thresholds RGB565 at `0x7fff`. No grey exists. Every stroke must be pure black (`0x000000`) and at least 2 px wide.
- **`components/ui/CMakeLists.txt` uses an explicit `SRCS` list, not a glob.** It
  used to be `file(GLOB_RECURSE ui_srcs "*.c")`, which silently misses any new
  `.c` file added without also touching `CMakeLists.txt` itself (a plain glob's
  result is cached at configure time). `CONFIGURE_DEPENDS` looks like the fix
  but ESP-IDF's component-discovery step runs in CMake script mode, where
  `CONFIGURE_DEPENDS` is rejected outright - confirmed by trying it. When a
  later task (or milestone) adds a new file under `components/ui/`, add it to
  this list by hand.
- **Refresh only when something changed.** `LV_DISPLAY_RENDER_MODE_FULL`: each `Lvgl_Refresh()` is a full 400x300 render, a 120000-iteration threshold loop, a 15 KB SPI write and a 50 ms delay.
- **LVGL heap is 64 KB** (`CONFIG_LV_MEM_SIZE_KILOBYTES=64`, built-in allocator). Watch object count.
- **LVGL timers do not run on their own.** `lv_timer_handler()` is only called from `Lvgl_Refresh()`. Anything time-based must be driven from the one-second task tick.
- **Buttons:** Select = `GPIO_NUM_18`, OK = `GPIO_NUM_0`. Active low, internal pull-up.
- **All LVGL calls must hold the lock:** `Lvgl_lock(-1)` / `Lvgl_unlock()`.
- **Screen coordinates:** 400x300. Shared overlay owns y 0..36. Screens own y 38..300.

## Build environment

This machine has no host compiler and ESP-IDF is installed via the VS Code extension, so `export.ps1` does not work. Use this PowerShell prelude for every build:

```powershell
$env:IDF_TOOLS_PATH = "C:\Espressif"
$env:IDF_PATH = "C:\esp\v5.5.5\esp-idf"
$env:ESP_ROM_ELF_DIR = "C:\Espressif\tools\esp-rom-elfs\20241011\"
$env:IDF_PYTHON_ENV_PATH = "C:\Espressif\tools\python\v5.5.5\venv"
$env:IDF_PYTHON_CHECK_CONSTRAINTS = "no"
$env:PATH = "C:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;" +
            "C:\Espressif\tools\cmake\3.30.2\bin;C:\Espressif\tools\ninja\1.12.1\;" +
            "C:\Espressif\tools\ccache\4.12.1\ccache-4.12.1-windows-x86_64;" +
            "C:\Espressif\tools\python\v5.5.5\venv\Scripts;C:\esp\v5.5.5\esp-idf\tools;$env:PATH"
$py = "C:\Espressif\tools\python\v5.5.5\venv\Scripts\python.exe"
```

Then from `firmware/`:

- Build: `& $py "$env:IDF_PATH\tools\idf.py" build`
- Flash and watch serial: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

**There is no host C compiler**, so the test cycle is flash-and-read-serial. Tasks that can be checked without hardware also get a Python model check, which runs instantly and catches design errors before a flash cycle.

## File structure

| File | Responsibility |
|---|---|
| `components/selftest/selftest.[ch]` | Boot-time assertion harness, serial summary |
| `components/selftest/Kconfig` | `CLOCK_SELFTEST` build flag |
| `components/clock/clock_time.[ch]` | UTC to local conversion, timezone application |
| `components/clock/wifi_sync.c` | Modified: NTP returns UTC, not local |
| `components/ui/btn_event.h` | Shared `btn_event_t` enum, zero dependencies |
| `components/input/buttons.[ch]` | Debounce, long-press classification, wake sources |
| `components/ui/nav.[ch]` | Navigation state machine, pure C |
| `components/ui/overlay.[ch]` | Top bar, focus frame, hint overlay on `lv_layer_top()` |
| `components/clock/clock_task.c` | Modified: drives buttons -> nav -> screens |
| `tools/verify_nav.py` | Host-side invariant check of the transition table |

---

### Task 1: Self-test harness

The project has no test framework and no host compiler. This task builds the smallest thing that gives every later task a real red/green cycle: assertions that run at boot and print a summary over serial. Deliberately not Unity — Unity's `RUN_TEST` requires `setUp`/`tearDown` whose provision differs by ESP-IDF configuration, and 40 lines of our own has no integration risk.

**Files:**
- Create: `firmware/components/selftest/selftest.h`
- Create: `firmware/components/selftest/selftest.c`
- Create: `firmware/components/selftest/Kconfig`
- Create: `firmware/components/selftest/CMakeLists.txt`
- Modify: `firmware/main/main.cpp`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `void Selftest_Run(void)`; macros `SELFTEST_CHECK(cond)`, `SELFTEST_CHECK_INT(actual, expected)`, `SELFTEST_CHECK_STR(actual, expected)`; and the registration point `selftest_run_all()` that later tasks append their suites to.

- [ ] **Step 1: Write the harness header**

`firmware/components/selftest/selftest.h`:

```c
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Runs every registered self-test suite and logs a summary.
   Compiles to an empty function unless CONFIG_CLOCK_SELFTEST is set. */
void Selftest_Run(void);

/* Used by suite implementations. Not for application code. */
void selftest_check(bool ok, const char *expr, const char *file, int line);
void selftest_check_int(long actual, long expected, const char *expr,
                        const char *file, int line);
void selftest_check_str(const char *actual, const char *expected,
                        const char *expr, const char *file, int line);

#define SELFTEST_CHECK(cond) \
    selftest_check((cond), #cond, __FILE__, __LINE__)
#define SELFTEST_CHECK_INT(actual, expected) \
    selftest_check_int((long)(actual), (long)(expected), #actual, __FILE__, __LINE__)
#define SELFTEST_CHECK_STR(actual, expected) \
    selftest_check_str((actual), (expected), #actual, __FILE__, __LINE__)

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Write the harness implementation with one deliberately failing test**

`firmware/components/selftest/selftest.c`:

```c
#include "selftest.h"

#include "esp_log.h"
#include "sdkconfig.h"

#include <string.h>

#if CONFIG_CLOCK_SELFTEST

static const char *TAG = "Selftest";

static int s_checks;
static int s_failures;

void selftest_check(bool ok, const char *expr, const char *file, int line)
{
    s_checks++;
    if (!ok) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s", file, line, expr);
    }
}

void selftest_check_int(long actual, long expected, const char *expr,
                        const char *file, int line)
{
    s_checks++;
    if (actual != expected) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s was %ld, expected %ld",
                 file, line, expr, actual, expected);
    }
}

void selftest_check_str(const char *actual, const char *expected,
                        const char *expr, const char *file, int line)
{
    s_checks++;
    if (actual == NULL || expected == NULL || strcmp(actual, expected) != 0) {
        s_failures++;
        ESP_LOGE(TAG, "FAIL %s:%d  %s was \"%s\", expected \"%s\"",
                 file, line, expr,
                 actual ? actual : "(null)", expected ? expected : "(null)");
    }
}

/* Proves the harness reports failures rather than silently passing.
   Removed in step 5 once it has done its job. */
static void test_harness_reports_failure(void)
{
    SELFTEST_CHECK_INT(1 + 1, 3);
}

void Selftest_Run(void)
{
    s_checks = 0;
    s_failures = 0;

    ESP_LOGI(TAG, "--- self-test start ---");
    test_harness_reports_failure();
    ESP_LOGI(TAG, "SELFTEST COMPLETE: %d check(s), %d failure(s)",
             s_checks, s_failures);
}

#else  /* !CONFIG_CLOCK_SELFTEST */

void Selftest_Run(void) {}
void selftest_check(bool ok, const char *e, const char *f, int l)
{ (void)ok; (void)e; (void)f; (void)l; }
void selftest_check_int(long a, long b, const char *e, const char *f, int l)
{ (void)a; (void)b; (void)e; (void)f; (void)l; }
void selftest_check_str(const char *a, const char *b, const char *e,
                        const char *f, int l)
{ (void)a; (void)b; (void)e; (void)f; (void)l; }

#endif
```

- [ ] **Step 3: Add the build flag and component registration**

`firmware/components/selftest/Kconfig`:

```
menu "Clock self-test"

config CLOCK_SELFTEST
    bool "Run on-device self tests at boot"
    default y
    help
        Runs assertion suites at boot, logs a summary over serial, then
        continues into the normal application. Turn this off for release
        builds.

endmenu
```

`firmware/components/selftest/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "selftest.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES log
)
```

- [ ] **Step 4: Call it from app_main and verify the harness catches a failure**

In `firmware/main/main.cpp`, add `#include "selftest.h"` alongside the other includes, and call `Selftest_Run();` as the first statement inside `app_main()`, before `RlcdPort.RLCD_Init()`.

In `firmware/main/CMakeLists.txt`, add the dependency:

```cmake
idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "./"
    PRIV_REQUIRES selftest)
```

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`
Expected serial output — the harness must **report the failure**, proving it is not silently passing:

```
Selftest: --- self-test start ---
Selftest: FAIL .../selftest.c:63  1 + 1 was 2, expected 3
Selftest: SELFTEST COMPLETE: 1 check(s), 1 failure(s)
```

- [ ] **Step 5: Turn the failing test green**

Change the assertion in `test_harness_reports_failure` to `SELFTEST_CHECK_INT(1 + 1, 2)` and rename the function to `test_harness_runs`.

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`
Expected: `SELFTEST COMPLETE: 1 check(s), 0 failure(s)`

- [ ] **Step 6: Commit**

```bash
git add firmware/components/selftest firmware/main/main.cpp firmware/main/CMakeLists.txt
git commit -m "test: add on-device self-test harness

No host compiler is available, so assertions run at boot and report over
serial. Deliberately not Unity: RUN_TEST needs setUp/tearDown whose
provision varies by IDF configuration."
```

---

### Task 2: UTC to local time conversion

**Files:**
- Create: `firmware/components/clock/clock_time.h`
- Create: `firmware/components/clock/clock_time.c`
- Create: `firmware/components/clock/clock_time_test.c`
- Modify: `firmware/components/clock/CMakeLists.txt`
- Modify: `firmware/components/selftest/selftest.c`

**Interfaces:**
- Consumes: `SELFTEST_CHECK_INT` from Task 1.
- Produces:
  - `void ClockTime_SetTimezone(const char *tz)`
  - `time_t ClockTime_UtcToEpoch(const struct tm *utc)` — does not modify `*utc`
  - `void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out)`
  - `void ClockTime_RunTests(void)` — called by `Selftest_Run`

- [ ] **Step 1: Write the failing tests**

The expected values below are derived from the POSIX rule `EET-2EEST,M3.5.0/3,M10.5.0/4`. Standard EET is UTC+2, summer EEST is UTC+3. DST starts on the last Sunday of March at 03:00 local standard time and ends on the last Sunday of October at 04:00 local summer time — both of which are **01:00 UTC**. In 2026 the last Sunday of March is the 29th and the last Sunday of October is the 25th.

`firmware/components/clock/clock_time_test.c`:

```c
#include "clock_time.h"
#include "selftest.h"

#include <string.h>

#define TZ_KYIV "EET-2EEST,M3.5.0/3,M10.5.0/4"

static struct tm make_utc(int year, int mon, int mday, int hour, int min)
{
    struct tm t;
    memset(&t, 0, sizeof(t));
    t.tm_year = year - 1900;
    t.tm_mon  = mon - 1;
    t.tm_mday = mday;
    t.tm_hour = hour;
    t.tm_min  = min;
    return t;
}

static void check_local(const char *tz, struct tm utc,
                        int want_hour, int want_min, int want_mday)
{
    struct tm local;
    ClockTime_SetTimezone(tz);
    ClockTime_UtcToLocal(&utc, &local);
    SELFTEST_CHECK_INT(local.tm_hour, want_hour);
    SELFTEST_CHECK_INT(local.tm_min,  want_min);
    SELFTEST_CHECK_INT(local.tm_mday, want_mday);
}

void ClockTime_RunTests(void)
{
    /* Winter: EET, UTC+2 */
    check_local(TZ_KYIV, make_utc(2026, 1, 15, 12, 0), 14, 0, 15);

    /* Summer: EEST, UTC+3 */
    check_local(TZ_KYIV, make_utc(2026, 7, 15, 12, 0), 15, 0, 15);

    /* One minute before the spring transition: still EET */
    check_local(TZ_KYIV, make_utc(2026, 3, 29, 0, 59), 2, 59, 29);

    /* At the spring transition: local jumps 03:00 -> 04:00 */
    check_local(TZ_KYIV, make_utc(2026, 3, 29, 1, 0), 4, 0, 29);

    /* One minute before the autumn transition: still EEST */
    check_local(TZ_KYIV, make_utc(2026, 10, 25, 0, 59), 3, 59, 25);

    /* At the autumn transition: local falls back 04:00 -> 03:00 */
    check_local(TZ_KYIV, make_utc(2026, 10, 25, 1, 0), 3, 0, 25);

    /* UTC passes through unchanged */
    check_local("UTC0", make_utc(2026, 8, 26, 19, 3), 19, 3, 26);

    /* Conversion must not modify its input */
    struct tm utc = make_utc(2026, 8, 26, 19, 3);
    struct tm before = utc;
    (void)ClockTime_UtcToEpoch(&utc);
    SELFTEST_CHECK_INT(memcmp(&utc, &before, sizeof(utc)), 0);

    /* Epoch of a known instant: 2026-08-26 19:03:00 UTC */
    SELFTEST_CHECK_INT(ClockTime_UtcToEpoch(&before), 1787770980L);
}
```

Register it in `firmware/components/selftest/selftest.c` — but **not** via
`#include "clock_time.h"` and not via adding `clock` to selftest's
`PRIV_REQUIRES`. `clock` requires `main` (for `user_config.h`) and `main`
requires `selftest`, so `selftest -> clock` closes a cycle:
`selftest -> clock -> main -> selftest`. ESP-IDF's component build rejects
that.

Instead, forward-declare the function and call it — the prototype only has
to match; ESP-IDF links every component's objects into one binary regardless
of the declared `REQUIRES` graph, so this resolves at link time with no
header needed:

```c
/* Forward-declared rather than pulled in via "clock_time.h": see the
   comment above test_harness_runs for why. Defined in
   components/clock/clock_time_test.c. */
extern void ClockTime_RunTests(void);
```

Add that near the top of `selftest.c`, inside the `#if CONFIG_CLOCK_SELFTEST`
block, and call `ClockTime_RunTests();` inside `Selftest_Run()` immediately
after `test_harness_runs();`. `firmware/components/selftest/CMakeLists.txt`
does not change — it stays `PRIV_REQUIRES log`.

This problem is specific to `clock`: it is the only component that needs
`main` for a header. A later task's test suite (Task 5's `nav.c`, for
instance) can very likely just add its own component to `selftest`'s
`PRIV_REQUIRES` the straightforward way — `ui` does not require `main`, so
`selftest -> ui` is not cyclic. Check the target component's own
`PRIV_REQUIRES`/`REQUIRES` for `main` before assuming the straightforward
path works; if it is there, use the forward-declare pattern instead.

- [ ] **Step 2: Verify the epoch constant before trusting the test**

The value in the test must be independently confirmed, otherwise a wrong test
bakes in a wrong implementation. (This step exists for exactly this reason:
the value that was first drafted here by hand — `1787857380` — was wrong by
one full day, 86400s. Running the command below is what catches that, not
eyeballing the arithmetic.)

Run:

```bash
python -c "import calendar,datetime; print(calendar.timegm(datetime.datetime(2026,8,26,19,3,0).timetuple()))"
```

Expected: `1787770980`. If your test file has a different constant, correct
it to the printed value before continuing.

- [ ] **Step 3: Run the tests to verify they fail**

Add `"clock_time.c" "clock_time_test.c"` to the `SRCS` list in `firmware/components/clock/CMakeLists.txt` but do not create the files yet.

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: FAIL — CMake reports the missing source files. This confirms the build is actually compiling the new sources rather than ignoring them.

- [ ] **Step 4: Write the implementation**

`firmware/components/clock/clock_time.h`:

```c
#pragma once

#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Applies a POSIX TZ string to every later local-time conversion. */
void ClockTime_SetTimezone(const char *tz);

/* Converts UTC calendar time to a Unix epoch. Leaves *utc untouched. */
time_t ClockTime_UtcToEpoch(const struct tm *utc);

/* Converts UTC calendar time to local time in the active timezone. */
void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out);

/* Self-test suite. Empty unless CONFIG_CLOCK_SELFTEST. */
void ClockTime_RunTests(void);

#ifdef __cplusplus
}
#endif
```

`firmware/components/clock/clock_time.c`:

```c
#include "clock_time.h"

#include <stdlib.h>

void ClockTime_SetTimezone(const char *tz)
{
    setenv("TZ", tz, 1);
    tzset();
}

time_t ClockTime_UtcToEpoch(const struct tm *utc)
{
    /* timegm normalises its argument, so work on a copy. */
    struct tm copy = *utc;
    copy.tm_isdst = 0;
    return timegm(&copy);
}

void ClockTime_UtcToLocal(const struct tm *utc, struct tm *local_out)
{
    time_t epoch = ClockTime_UtcToEpoch(utc);
    localtime_r(&epoch, local_out);
}
```

Guard the test file so release builds drop it — wrap the entire body of `clock_time_test.c` in `#if CONFIG_CLOCK_SELFTEST` / `#else` with an empty `void ClockTime_RunTests(void) {}` / `#endif`, and add `#include "sdkconfig.h"` at the top.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`
Expected: `SELFTEST COMPLETE: 24 check(s), 0 failure(s)`
(23 from this suite - seven `check_local` calls of three checks each, plus the
two direct checks - and one from the Task 1 harness test.)

If the two transition-boundary cases fail while the winter and summer cases pass, the TZ string is being parsed but the rule offsets are wrong — recheck that the rule uses `/3` and `/4` and not the default `/2`.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/clock/clock_time.h firmware/components/clock/clock_time.c \
        firmware/components/clock/clock_time_test.c firmware/components/clock/CMakeLists.txt \
        firmware/components/selftest
git commit -m "feat: add UTC to local time conversion with DST tests

Covers both 2026 transition boundaries for EET/EEST, which is what the
current local-time RTC gets wrong."
```

---

### Task 3: RTC stores UTC

Fixes the bug where the clock stays an hour wrong for up to 12 hours after a DST transition, and makes a runtime timezone setting possible.

**Files:**
- Modify: `firmware/components/clock/wifi_sync.c:143` and `:60-61`
- Modify: `firmware/components/clock/wifi_sync.h`
- Modify: `firmware/components/clock/clock_task.c`

**Interfaces:**
- Consumes: `ClockTime_SetTimezone`, `ClockTime_UtcToLocal` from Task 2.
- Produces: `WifiSync_SyncTimeOnce` now writes **UTC** into `out_time`. `clock_task` holds `now_utc` and derives `now` for display.

- [ ] **Step 1: Make NTP return UTC**

In `firmware/components/clock/wifi_sync.c`, replace `localtime_r(&now, out_time);` at line 143 with:

```c
            gmtime_r(&now, out_time);
```

Update the log line immediately below it to say `"Time fetched (UTC): ..."` so serial output is not misleading.

Delete these two lines from `WifiSync_Init` (around line 60), because timezone is no longer this module's concern:

```c
    setenv("TZ", CLOCK_TIMEZONE, 1);
    tzset();
```

In `firmware/components/clock/wifi_sync.h`, document the contract change above the declaration:

```c
/* On success out_time receives the current time in UTC, not local time.
   Use ClockTime_UtcToLocal to render it. */
bool WifiSync_SyncTimeOnce(struct tm *out_time, const char *ssid, const char *password, uint32_t timeout_ms,
                            WifiSync_ConnectedCallback on_connected, void *ctx);
```

- [ ] **Step 2: Apply the timezone at startup and convert for display**

In `firmware/components/clock/clock_task.c`, add near the other includes:

```c
#include "clock_time.h"
```

At the top of `clock_task()`, before `WifiSync_Init()`, apply the compile-time timezone:

```c
    ClockTime_SetTimezone(CLOCK_TIMEZONE);
```

Rename the startup variable so the units are unmistakable. Replace:

```c
    struct tm t = {0};
```

with:

```c
    struct tm t_utc = {0};
    struct tm t = {0};
```

and after the successful `WifiSync_SyncTimeOnce(&t_utc, ...)` call, replace `Pcf85063_SetTime(&t);` with:

```c
    Pcf85063_SetTime(&t_utc);          /* the RTC now holds UTC */
    ClockTime_UtcToLocal(&t_utc, &t);  /* everything displayed is local */
```

Update the `WifiSync_SyncTimeOnce` call itself to pass `&t_utc` instead of `&t`.

- [ ] **Step 3: Convert on every read in the main loop**

In the `for (;;)` loop, replace:

```c
        struct tm now;
        if (Pcf85063_GetTime(&now) != ESP_OK) {
```

with:

```c
        struct tm now_utc;
        struct tm now;
        if (Pcf85063_GetTime(&now_utc) != ESP_OK) {
```

and immediately after the error check, add:

```c
        ClockTime_UtcToLocal(&now_utc, &now);
```

The rest of the loop keeps using `now` and needs no further change.

Inside the scheduled-sync block, replace:

```c
                struct tm ntp_t;
                if (WifiSync_SyncTimeOnce(&ntp_t, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
                    Pcf85063_SetTime(&ntp_t);
                    now = ntp_t;
                    update_sync_label(&now);
                }
```

with:

```c
                struct tm ntp_utc;
                if (WifiSync_SyncTimeOnce(&ntp_utc, CLOCK_WIFI_SSID, CLOCK_WIFI_PASS, 15000, wifi_connected_cb, s_weather)) {
                    Pcf85063_SetTime(&ntp_utc);
                    ClockTime_UtcToLocal(&ntp_utc, &now);
                    update_sync_label(&now);
                }
```

- [ ] **Step 4: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean, no warnings.

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

Expected, in order:
1. `Time fetched (UTC): HH:MM:SS ...` where `HH` is **two or three hours behind** your wall clock (two in winter, three in summer).
2. The display shows your correct **local** wall-clock time.

If the display shows UTC, `ClockTime_UtcToLocal` is not being applied on the read path. If the display is correct but the log shows local time, step 1 did not take effect.

- [ ] **Step 5: Verify the RTC survives a reset**

Press the reset button (do not reflash) and watch the serial log. Because Wi-Fi sync runs at boot the clock will resync, so to test the RTC path specifically, temporarily set `CLOCK_WIFI_SSID` to a nonexistent network, flash, and confirm the boot path reports no Wi-Fi. Then restore the real SSID.

Expected: the failure path is reached and reported, confirming the RTC read path is exercised independently of NTP.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/clock/wifi_sync.c firmware/components/clock/wifi_sync.h \
        firmware/components/clock/clock_task.c
git commit -m "fix: store UTC in the RTC, apply timezone only for display

Pcf85063_SetTime was being handed local time, so DST transitions did not
take effect until the next NTP sync - up to 12 hours late. It also made a
runtime timezone setting impossible."
```

---

### Task 4: Two-button input layer

**Files:**
- Create: `firmware/components/ui/btn_event.h`
- Create: `firmware/components/input/buttons.h`
- Create: `firmware/components/input/buttons.c`
- Create: `firmware/components/input/CMakeLists.txt`
- Modify: `firmware/main/user_config.h`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - `btn_event_t` enum: `BTN_EV_NONE`, `BTN_EV_SELECT_SHORT`, `BTN_EV_SELECT_LONG`, `BTN_EV_OK_SHORT`, `BTN_EV_OK_LONG`
  - `void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin)`
  - `void Buttons_EnableWakeup(void)`
  - `btn_event_t Buttons_ReadEvent(void)`

- [ ] **Step 1: Define the shared event type**

`btn_event_t` lives in its own header so that `nav` (Task 5) can consume it without depending on the GPIO layer.

`firmware/components/ui/btn_event.h`:

```c
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BTN_EV_NONE = 0,
    BTN_EV_SELECT_SHORT,
    BTN_EV_SELECT_LONG,
    BTN_EV_OK_SHORT,
    BTN_EV_OK_LONG,
} btn_event_t;

#ifdef __cplusplus
}
#endif
```

- [ ] **Step 2: Add the pins**

In `firmware/main/user_config.h`, replace the `KEY_BUTTON_PIN` line with:

```c
#define BTN_OK_PIN          GPIO_NUM_0    /* BOOT  */
#define BTN_SELECT_PIN      GPIO_NUM_18   /* KEY   */
```

Both values are confirmed against the esp32s3-rlcd-smart-clock project on this same board, which uses `ButtonInput s_buttons(GPIO_NUM_18, GPIO_NUM_0)` with select first.

- [ ] **Step 3: Write the button layer**

`firmware/components/input/buttons.h`:

```c
#pragma once

#include "btn_event.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures both pins as inputs with internal pull-ups. Active low. */
void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin);

/* Arms both pins as light-sleep wake sources. Call once after Init. */
void Buttons_EnableWakeup(void);

/* Call after waking with ESP_SLEEP_WAKEUP_GPIO. Debounces, classifies the
   press, then blocks until release so one push cannot emit twice.
   Returns BTN_EV_NONE if no button was actually down. */
btn_event_t Buttons_ReadEvent(void);

#ifdef __cplusplus
}
#endif
```

`firmware/components/input/buttons.c`:

```c
#include "buttons.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_sleep.h"

#define BTN_DEBOUNCE_MS 20
#define BTN_LONG_MS     600
#define BTN_POLL_MS     20

static gpio_num_t s_select = GPIO_NUM_NC;
static gpio_num_t s_ok     = GPIO_NUM_NC;

void Buttons_Init(gpio_num_t select_pin, gpio_num_t ok_pin)
{
    s_select = select_pin;
    s_ok     = ok_pin;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << select_pin) | (1ULL << ok_pin),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
}

void Buttons_EnableWakeup(void)
{
    gpio_wakeup_enable(s_select, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(s_ok, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
}

btn_event_t Buttons_ReadEvent(void)
{
    vTaskDelay(pdMS_TO_TICKS(BTN_DEBOUNCE_MS));

    bool select_down = gpio_get_level(s_select) == 0;
    bool ok_down     = gpio_get_level(s_ok) == 0;
    if (!select_down && !ok_down) {
        return BTN_EV_NONE;   /* bounce or a wake from something else */
    }

    /* If somehow both are down, Select wins. */
    const bool       is_select = select_down;
    const gpio_num_t pin       = is_select ? s_select : s_ok;

    int held_ms = BTN_DEBOUNCE_MS;
    while (gpio_get_level(pin) == 0 && held_ms < BTN_LONG_MS) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
        held_ms += BTN_POLL_MS;
    }

    const bool is_long = held_ms >= BTN_LONG_MS;

    /* Drain the remainder of the press. */
    while (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(BTN_POLL_MS));
    }

    if (is_select) {
        return is_long ? BTN_EV_SELECT_LONG : BTN_EV_SELECT_SHORT;
    }
    return is_long ? BTN_EV_OK_LONG : BTN_EV_OK_SHORT;
}
```

`firmware/components/input/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "buttons.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES driver esp_hw_support
    REQUIRES ui
)
```

`REQUIRES ui` is what makes `btn_event.h` visible, since it lives in the `ui` component's include directory.

- [ ] **Step 4: Verify both buttons on hardware**

This is the first time GPIO18 is exercised on this board, so prove it before building navigation on top of it. Temporarily add to `clock_task()`, immediately after `configure_key_button_wakeup()` is replaced (Task 8 does the full wiring — for now add a throwaway probe loop at the top of `clock_task`):

```c
    Buttons_Init(BTN_SELECT_PIN, BTN_OK_PIN);
    for (int i = 0; i < 200; i++) {
        ESP_LOGI(TAG, "probe select=%d ok=%d",
                 gpio_get_level(BTN_SELECT_PIN), gpio_get_level(BTN_OK_PIN));
        vTaskDelay(pdMS_TO_TICKS(100));
    }
```

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

Press each of the two non-power buttons in turn. Expected: `select` reads 0 while one is held and `ok` reads 0 while the other is held; both read 1 at rest.

**If neither line ever drops to 0 for one of the buttons, stop and report it** — the Select pin is wrong and every later task depends on it. Delete the probe loop once confirmed.

- [ ] **Step 5: Commit**

```bash
git add firmware/components/input firmware/components/ui/btn_event.h firmware/main/user_config.h
git commit -m "feat: add two-button input layer with long-press detection

Select on GPIO18, OK on GPIO0, both confirmed against the smart-clock
project on this board."
```

---

### Task 5: Navigation state machine

The one piece with no hardware dependency, so it gets both on-device assertions and a host-side exhaustive model check.

**Files:**
- Create: `firmware/components/ui/nav.h`
- Create: `firmware/components/ui/nav.c`
- Create: `firmware/components/ui/nav_test.c`
- Create: `tools/verify_nav.py`
- Modify: `firmware/components/selftest/selftest.c`

**Interfaces:**
- Consumes: `btn_event_t` from Task 4.
- Produces:
  - `nav_mode_t` enum: `NAV_SCREEN`, `NAV_FOCUS`, `NAV_DETAIL`, `NAV_SETTINGS`
  - `NAV_SCREEN_MAIN` = 0, `NAV_SCREEN_CALCLOCK` = 1, `NAV_SCREEN_COUNT` = 2
  - `nav_state_t` with fields `mode`, `screen`, `focus`, `settings_row`, `settings_rows`, `focus_count`, `focus_wrapped`
  - `void nav_init(nav_state_t *st, nav_focus_count_fn count_fn, uint8_t settings_rows)`
  - `bool nav_handle(nav_state_t *st, btn_event_t ev)` — returns true when the caller must redraw
  - `void Nav_RunTests(void)`

- [ ] **Step 1: Write the failing tests**

`firmware/components/ui/nav_test.c`:

```c
#include "nav.h"
#include "selftest.h"
#include "sdkconfig.h"

#if CONFIG_CLOCK_SELFTEST

/* Main has 5 focusable elements, Cal+Clock has none until milestone 4. */
static uint8_t stub_focus_count(uint8_t screen)
{
    return screen == NAV_SCREEN_MAIN ? 5 : 0;
}

void Nav_RunTests(void)
{
    nav_state_t st;

    /* Select cycles screens and wraps */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_SHORT), 1);
    SELFTEST_CHECK_INT(st.screen, NAV_SCREEN_CALCLOCK);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.screen, NAV_SCREEN_MAIN);

    /* OK enters focus mode on a screen that has focusables */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_SHORT), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);
    SELFTEST_CHECK_INT(st.focus, 0);

    /* OK does nothing on a screen with no focusables */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_SELECT_SHORT);              /* to Cal+Clock */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_SHORT), 0);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* Focus advances, wraps, and reports the wrap */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_OK_SHORT);
    for (int i = 0; i < 4; i++) {
        nav_handle(&st, BTN_EV_SELECT_SHORT);
    }
    SELFTEST_CHECK_INT(st.focus, 4);
    SELFTEST_CHECK_INT(st.focus_wrapped, 0);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.focus, 0);
    SELFTEST_CHECK_INT(st.focus_wrapped, 1);

    /* Long Select backs out of focus mode */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_LONG), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* OK opens detail, both back gestures return to focus */
    nav_init(&st, stub_focus_count, 4);
    nav_handle(&st, BTN_EV_OK_SHORT);
    nav_handle(&st, BTN_EV_OK_SHORT);
    SELFTEST_CHECK_INT(st.mode, NAV_DETAIL);
    nav_handle(&st, BTN_EV_OK_SHORT);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);
    nav_handle(&st, BTN_EV_OK_SHORT);
    nav_handle(&st, BTN_EV_SELECT_LONG);
    SELFTEST_CHECK_INT(st.mode, NAV_FOCUS);

    /* Long OK reaches settings from every mode */
    const nav_mode_t from[] = { NAV_SCREEN, NAV_FOCUS, NAV_DETAIL };
    for (unsigned i = 0; i < sizeof(from) / sizeof(from[0]); i++) {
        nav_init(&st, stub_focus_count, 4);
        st.mode = from[i];
        SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_LONG), 1);
        SELFTEST_CHECK_INT(st.mode, NAV_SETTINGS);
        SELFTEST_CHECK_INT(st.settings_row, 0);
    }

    /* Long OK inside settings is a no-op, not a re-entry */
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_OK_LONG), 0);
    SELFTEST_CHECK_INT(st.mode, NAV_SETTINGS);

    /* Settings rows wrap, and long Select always escapes */
    nav_init(&st, stub_focus_count, 3);
    nav_handle(&st, BTN_EV_OK_LONG);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.settings_row, 2);
    nav_handle(&st, BTN_EV_SELECT_SHORT);
    SELFTEST_CHECK_INT(st.settings_row, 0);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_SELECT_LONG), 1);
    SELFTEST_CHECK_INT(st.mode, NAV_SCREEN);

    /* BTN_EV_NONE never changes anything */
    nav_init(&st, stub_focus_count, 4);
    SELFTEST_CHECK_INT(nav_handle(&st, BTN_EV_NONE), 0);
}

#else
void Nav_RunTests(void) {}
#endif
```

Register it: add `#include "nav.h"` and `Nav_RunTests();` to `Selftest_Run()` in `firmware/components/selftest/selftest.c`, and add `ui` to that component's `PRIV_REQUIRES`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: FAIL — `nav.h: No such file or directory`.

- [ ] **Step 3: Write the implementation**

`firmware/components/ui/nav.h`:

```c
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "btn_event.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NAV_SCREEN = 0,
    NAV_FOCUS,
    NAV_DETAIL,
    NAV_SETTINGS,
} nav_mode_t;

#define NAV_SCREEN_MAIN     0
#define NAV_SCREEN_CALCLOCK 1
#define NAV_SCREEN_COUNT    2

/* Returns how many focusable elements a screen has. Supplied by the caller
   so this module needs no knowledge of any screen. */
typedef uint8_t (*nav_focus_count_fn)(uint8_t screen);

typedef struct {
    nav_mode_t         mode;
    uint8_t            screen;
    uint8_t            focus;
    uint8_t            settings_row;
    uint8_t            settings_rows;
    bool               focus_wrapped;  /* focus just wrapped past the end   */
    nav_focus_count_fn focus_count;
} nav_state_t;

void nav_init(nav_state_t *st, nav_focus_count_fn count_fn, uint8_t settings_rows);

/* Applies one button event. Returns true when the caller must redraw. */
bool nav_handle(nav_state_t *st, btn_event_t ev);

void Nav_RunTests(void);

#ifdef __cplusplus
}
#endif
```

`firmware/components/ui/nav.c`:

```c
#include "nav.h"

void nav_init(nav_state_t *st, nav_focus_count_fn count_fn, uint8_t settings_rows)
{
    st->mode          = NAV_SCREEN;
    st->screen        = NAV_SCREEN_MAIN;
    st->focus         = 0;
    st->settings_row  = 0;
    st->settings_rows = settings_rows;
    st->focus_wrapped = false;
    st->focus_count   = count_fn;
}

bool nav_handle(nav_state_t *st, btn_event_t ev)
{
    if (ev == BTN_EV_NONE) {
        return false;
    }

    st->focus_wrapped = false;

    /* Long OK opens settings from anywhere but settings itself. */
    if (ev == BTN_EV_OK_LONG) {
        if (st->mode == NAV_SETTINGS) {
            return false;
        }
        st->mode         = NAV_SETTINGS;
        st->settings_row = 0;
        return true;
    }

    switch (st->mode) {
    case NAV_SCREEN:
        if (ev == BTN_EV_SELECT_SHORT) {
            st->screen = (uint8_t)((st->screen + 1) % NAV_SCREEN_COUNT);
            st->focus  = 0;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            if (st->focus_count(st->screen) == 0) {
                return false;
            }
            st->mode  = NAV_FOCUS;
            st->focus = 0;
            return true;
        }
        return false;

    case NAV_FOCUS: {
        const uint8_t n = st->focus_count(st->screen);
        if (ev == BTN_EV_SELECT_SHORT) {
            if (n == 0) {
                return false;
            }
            const uint8_t next = (uint8_t)((st->focus + 1) % n);
            st->focus_wrapped = (next < st->focus);
            st->focus         = next;
            return true;
        }
        if (ev == BTN_EV_SELECT_LONG) {
            st->mode = NAV_SCREEN;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            st->mode = NAV_DETAIL;
            return true;
        }
        return false;
    }

    case NAV_DETAIL:
        if (ev == BTN_EV_SELECT_LONG || ev == BTN_EV_OK_SHORT) {
            st->mode = NAV_FOCUS;
            return true;
        }
        return false;

    case NAV_SETTINGS:
        if (ev == BTN_EV_SELECT_SHORT) {
            if (st->settings_rows == 0) {
                return false;
            }
            st->settings_row =
                (uint8_t)((st->settings_row + 1) % st->settings_rows);
            return true;
        }
        if (ev == BTN_EV_SELECT_LONG) {
            st->mode = NAV_SCREEN;
            return true;
        }
        if (ev == BTN_EV_OK_SHORT) {
            return true;   /* caller acts on the row, then redraws */
        }
        return false;
    }

    return false;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`
Expected: `0 failure(s)`, with the total check count risen by 34.

- [ ] **Step 5: Add the exhaustive host-side model check**

The on-device tests cover the paths that matter; this catches states no hand-written case thought to visit, without a flash cycle.

`tools/verify_nav.py`:

```python
"""Exhaustive invariant check of the nav transition table in
firmware/components/ui/nav.c. Mirrors the C logic; if you change one,
change both. Run: python tools/verify_nav.py"""

import itertools
import sys

SCREEN, FOCUS, DETAIL, SETTINGS = range(4)
MODES = [SCREEN, FOCUS, DETAIL, SETTINGS]
NAMES = {SCREEN: "SCREEN", FOCUS: "FOCUS", DETAIL: "DETAIL", SETTINGS: "SETTINGS"}

NONE, SEL_S, SEL_L, OK_S, OK_L = range(5)
EVENTS = [NONE, SEL_S, SEL_L, OK_S, OK_L]

SCREEN_COUNT = 2
SETTINGS_ROWS = 4
FOCUS_COUNTS = {0: 5, 1: 0}          # Main has 5, Cal+Clock has none


def handle(st, ev):
    """Mirror of nav_handle. Returns (new_state, changed)."""
    st = dict(st)
    if ev == NONE:
        return st, False
    st["wrapped"] = False
    if ev == OK_L:
        if st["mode"] == SETTINGS:
            return st, False
        st["mode"], st["row"] = SETTINGS, 0
        return st, True

    m = st["mode"]
    if m == SCREEN:
        if ev == SEL_S:
            st["screen"] = (st["screen"] + 1) % SCREEN_COUNT
            st["focus"] = 0
            return st, True
        if ev == OK_S:
            if FOCUS_COUNTS[st["screen"]] == 0:
                return st, False
            st["mode"], st["focus"] = FOCUS, 0
            return st, True
        return st, False

    if m == FOCUS:
        n = FOCUS_COUNTS[st["screen"]]
        if ev == SEL_S:
            if n == 0:
                return st, False
            nxt = (st["focus"] + 1) % n
            st["wrapped"] = nxt < st["focus"]
            st["focus"] = nxt
            return st, True
        if ev == SEL_L:
            st["mode"] = SCREEN
            return st, True
        if ev == OK_S:
            st["mode"] = DETAIL
            return st, True
        return st, False

    if m == DETAIL:
        if ev in (SEL_L, OK_S):
            st["mode"] = FOCUS
            return st, True
        return st, False

    if m == SETTINGS:
        if ev == SEL_S:
            st["row"] = (st["row"] + 1) % SETTINGS_ROWS
            return st, True
        if ev == SEL_L:
            st["mode"] = SCREEN
            return st, True
        if ev == OK_S:
            return st, True
        return st, False

    raise AssertionError("unreachable mode %r" % m)


def reachable_states():
    start = {"mode": SCREEN, "screen": 0, "focus": 0, "row": 0, "wrapped": False}
    seen, queue = set(), [start]
    while queue:
        st = queue.pop()
        key = tuple(sorted(st.items()))
        if key in seen:
            continue
        seen.add(key)
        for ev in EVENTS:
            nxt, _ = handle(st, ev)
            queue.append(nxt)
    return [dict(k) for k in seen]


def main():
    errors = []
    states = reachable_states()

    for st in states:
        # invariant: mode always valid
        if st["mode"] not in MODES:
            errors.append("invalid mode %r" % st["mode"])
        # invariant: indices always in range
        if not 0 <= st["screen"] < SCREEN_COUNT:
            errors.append("screen out of range: %r" % st)
        limit = max(1, FOCUS_COUNTS[st["screen"]])
        if not 0 <= st["focus"] < limit:
            errors.append("focus out of range: %r" % st)
        if not 0 <= st["row"] < SETTINGS_ROWS:
            errors.append("settings row out of range: %r" % st)
        # invariant: no dead ends - SCREEN is always reachable again
        if not escapes_to_screen(st):
            errors.append("cannot return to SCREEN from %r" % st)
        # invariant: settings always reachable
        if not reaches_settings(st):
            errors.append("cannot reach SETTINGS from %r" % st)

    modes_seen = {st["mode"] for st in states}
    for m in MODES:
        if m not in modes_seen:
            errors.append("mode %s is unreachable" % NAMES[m])

    print("reachable states: %d" % len(states))
    if errors:
        for e in sorted(set(errors)):
            print("FAIL:", e)
        return 1
    print("all invariants hold")
    return 0


def _search(start, predicate, depth=6):
    queue, seen = [(start, 0)], set()
    while queue:
        st, d = queue.pop()
        key = tuple(sorted(st.items()))
        if key in seen or d > depth:
            continue
        seen.add(key)
        if predicate(st):
            return True
        for ev in EVENTS:
            nxt, _ = handle(st, ev)
            queue.append((nxt, d + 1))
    return False


def escapes_to_screen(st):
    return _search(st, lambda s: s["mode"] == SCREEN)


def reaches_settings(st):
    return _search(st, lambda s: s["mode"] == SETTINGS)


if __name__ == "__main__":
    sys.exit(main())
```

Run: `python tools/verify_nav.py`
Expected:

```
reachable states: <N>
all invariants hold
```

If any invariant fails, fix `nav.c` **and** the mirror in this file, then rerun both this and the on-device suite.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/ui/nav.h firmware/components/ui/nav.c \
        firmware/components/ui/nav_test.c firmware/components/selftest tools/verify_nav.py
git commit -m "feat: add navigation state machine

Pure C with no LVGL or ESP-IDF dependency, so it is covered by on-device
assertions plus an exhaustive host-side invariant check."
```

---

### Task 6: Shared top bar on lv_layer_top

Deletes the duplicated top bar added in ccffbf5. One widget set now serves every screen.

**Files:**
- Create: `firmware/components/ui/overlay.h`
- Create: `firmware/components/ui/overlay.c`
- Modify: `firmware/components/ui/screens.c`
- Modify: `firmware/components/ui/screens.h`
- Modify: `firmware/components/ui/ui.c`
- Modify: `firmware/components/clock/clock_task.c`

**Interfaces:**
- Consumes: nothing from earlier tasks.
- Produces:
  - `void Overlay_Create(void)`
  - `void Overlay_SetTopBar(const char *temp, const char *hum, const char *date, const char *battery)`

- [ ] **Step 1: Confirm the assumption the whole design rests on**

`lv_layer_top()` is assumed to persist across `lv_screen_load()`. Prove it before building on it. Add to `ui_init()` in `firmware/components/ui/ui.c`, temporarily, after `create_screens()`:

```c
    lv_obj_t *probe = lv_label_create(lv_layer_top());
    lv_obj_set_pos(probe, 150, 150);
    lv_obj_set_style_text_color(probe, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(probe, "LAYER TOP OK");
```

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

Expected: `LAYER TOP OK` is visible on the init screen, still visible after the main screen loads, and still visible after pressing the button to reach the calendar.

**If it disappears on a screen change, stop and report it** — sections C and the whole overlay design in the spec need revisiting. Delete the probe once confirmed.

- [ ] **Step 2: Write the overlay component**

`firmware/components/ui/overlay.h`:

```c
#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Builds the shared overlay on lv_layer_top(), which floats above whichever
   screen is loaded. Call once, after create_screens(). */
void Overlay_Create(void);

/* Updates the top bar. Strings are copied. */
void Overlay_SetTopBar(const char *temp, const char *hum,
                      const char *date, const char *battery);

#ifdef __cplusplus
}
#endif
```

`firmware/components/ui/overlay.c`:

```c
#include "overlay.h"

#include "fonts.h"

static lv_obj_t *s_temp;
static lv_obj_t *s_hum;
static lv_obj_t *s_date;
static lv_obj_t *s_battery;

/* A solid 2 px block, not an lv_line: hairlines dither away on a 1 bit
   panel. */
static void overlay_rule(lv_obj_t *parent, int x, int y, int w)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, 2);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
}

static lv_obj_t *overlay_frame(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_pad_all(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(o, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(o, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(o, 255, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(o, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    return o;
}

static lv_obj_t *overlay_label(lv_obj_t *parent, int x, int y, int width,
                              lv_text_align_t align, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_pos(l, x, y);
    lv_obj_set_size(l, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_26, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(l, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    if (width > 0) {
        lv_obj_set_style_min_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_max_width(l, width, LV_PART_MAIN | LV_STATE_DEFAULT);
        lv_obj_set_style_text_align(l, align, LV_PART_MAIN | LV_STATE_DEFAULT);
    }
    lv_label_set_text_static(l, text);
    return l;
}

void Overlay_Create(void)
{
    lv_obj_t *top = lv_layer_top();

    /* The layer itself must not paint, or it would hide every screen. */
    lv_obj_set_style_bg_opa(top, LV_OPA_TRANSP, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(top, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_remove_flag(top, LV_OBJ_FLAG_SCROLLABLE);

    /* An opaque white band so screen content cannot show through the bar. */
    lv_obj_t *band = lv_obj_create(top);
    lv_obj_remove_flag(band, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(band, 0, 0);
    lv_obj_set_size(band, 400, 34);
    lv_obj_set_style_pad_all(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(band, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(band, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(band, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);

    overlay_frame(top, 329, 5, 64, 26);   /* battery body */
    overlay_frame(top, 391, 10, 7, 16);   /* battery nub  */

    s_temp    = overlay_label(top, 2, 3, 0, LV_TEXT_ALIGN_LEFT, "0.0");
    s_hum     = overlay_label(top, 89, 3, 0, LV_TEXT_ALIGN_LEFT, "0%");
    s_date    = overlay_label(top, 163, 3, 0, LV_TEXT_ALIGN_LEFT, "01.01.2000");
    s_battery = overlay_label(top, 332, 3, 58, LV_TEXT_ALIGN_CENTER, "0.00");

    overlay_rule(top, 0, 34, 400);
}

void Overlay_SetTopBar(const char *temp, const char *hum,
                      const char *date, const char *battery)
{
    if (!s_temp) {
        return;
    }
    lv_label_set_text(s_temp, temp);
    lv_label_set_text(s_hum, hum);
    lv_label_set_text(s_date, date);
    lv_label_set_text(s_battery, battery);
}
```

- [ ] **Step 3: Delete both duplicated top bars**

In `firmware/components/ui/screens.c`:

- Delete the whole `create_calendar_top_bar` function and its call from `create_screen_calendar`.
- Delete `calendar_update_top_bar` and the four `s_cal_bar_*` statics.
- Delete the `cal_add_rule(parent, 0, 34, 400)` that belonged to that bar.
- In `create_screen_main`, delete the blocks that create `objects.obj0`, `objects.obj1`, `objects.temp`, `objects.hum`, `objects.date`, `objects.battery` and `objects.obj3` (the rule at y=34).

In `firmware/components/ui/screens.h`, delete `obj0`, `obj1`, `temp`, `hum`, `date`, `battery` and `obj3` from `objects_t`, and delete the `calendar_update_top_bar` declaration.

**Do not delete `objects.obj2`** (the blinking colon) or `objects.obj4` (the rule at y=187) — both are still used.

In `firmware/components/ui/ui.c`, add `#include "overlay.h"` and call `Overlay_Create();` in `ui_init()` immediately after `create_screens()`.

- [ ] **Step 4: Point clock_task at the shared bar**

In `firmware/components/clock/clock_task.c`, add `#include "overlay.h"`, then inside `update_labels` replace these five lines:

```c
        lv_label_set_text(objects.temp, temp_buf);
        lv_label_set_text(objects.hum, hum_buf);
        lv_label_set_text(objects.date, date_buf);
        lv_label_set_text(objects.battery, batt_buf);

        /* the calendar screen carries its own copy of the top bar */
        calendar_update_top_bar(temp_buf, hum_buf, date_buf, batt_buf);
```

with a single call:

```c
        Overlay_SetTopBar(temp_buf, hum_buf, date_buf, batt_buf);
```

- [ ] **Step 5: Build and verify on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean. Any `objects.temp`-style error means a reference was missed in step 3.

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

Expected:
1. The top bar shows on the main screen exactly as before.
2. Pressing the button to the calendar screen keeps the **same** bar, still updating.
3. Free LVGL heap has gone up, not down — six widgets were deleted and one
   set added.

To observe point 3, add this to the end of `Overlay_Create()`:

```c
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    LV_LOG_USER("LVGL heap: %u free of %u", (unsigned)mon.free_size, (unsigned)mon.total_size);
```

Note the figure before this task and after it. Keep the line — the Info page
in the settings plan reports the same number.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/ui firmware/components/clock/clock_task.c
git commit -m "refactor: move the top bar to lv_layer_top

lv_layer_top floats above whichever screen is loaded, so one widget set
now serves every screen. Removes the duplicate bar added in ccffbf5."
```

---

### Task 7: Focus frame and hint overlay

**Files:**
- Modify: `firmware/components/ui/overlay.h`
- Modify: `firmware/components/ui/overlay.c`

**Interfaces:**
- Consumes: `Overlay_Create` from Task 6.
- Produces:
  - `void Overlay_ShowFocus(const lv_area_t *area)` — `NULL` hides the frame
  - `void Overlay_ShowHint(const char *text, uint32_t now_ms, uint32_t duration_ms)`
  - `bool Overlay_TickHint(uint32_t now_ms)` — returns true when it hid the hint and the caller must redraw

- [ ] **Step 1: Extend the header**

Append to `firmware/components/ui/overlay.h`, before the `#ifdef __cplusplus` closing block:

```c
/* Draws the focus frame around an absolute screen rectangle.
   Pass NULL to hide it. */
void Overlay_ShowFocus(const lv_area_t *area);

/* Shows the hint overlay for duration_ms. The text is copied. */
void Overlay_ShowHint(const char *text, uint32_t now_ms, uint32_t duration_ms);

/* Call once per task tick. Hides an expired hint and returns true when it
   did, meaning the caller must refresh. LVGL timers cannot do this: they
   only run inside Lvgl_Refresh(), which only runs when something is dirty. */
bool Overlay_TickHint(uint32_t now_ms);
```

- [ ] **Step 2: Implement**

Add to `firmware/components/ui/overlay.c`, alongside the existing statics:

```c
static lv_obj_t *s_focus;
static lv_obj_t *s_hint_box;
static lv_obj_t *s_hint_label;
static uint32_t  s_hint_until_ms;
static bool      s_hint_visible;
```

Add these two blocks to the end of `Overlay_Create()`:

```c
    /* Focus frame. 3 px so it survives the 1 bit threshold, and hidden
       until a focusable element is selected. */
    s_focus = overlay_frame(top, 0, 0, 10, 10);
    lv_obj_set_style_border_width(s_focus, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_focus, LV_OBJ_FLAG_HIDDEN);

    /* Hint overlay: white box with a black outline, bottom centre. */
    s_hint_box = lv_obj_create(top);
    lv_obj_remove_flag(s_hint_box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_pos(s_hint_box, 40, 252);
    lv_obj_set_size(s_hint_box, 320, 40);
    lv_obj_set_style_pad_all(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_radius(s_hint_box, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(s_hint_box, lv_color_hex(0xffffff), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(s_hint_box, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(s_hint_box, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(s_hint_box, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_add_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);

    s_hint_label = lv_label_create(s_hint_box);
    lv_obj_set_pos(s_hint_label, 4, 4);
    lv_obj_set_size(s_hint_label, 308, 32);
    lv_label_set_long_mode(s_hint_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(s_hint_label, &lv_font_montserrat_12, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_color(s_hint_label, lv_color_hex(0x000000), LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_obj_set_style_text_align(s_hint_label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN | LV_STATE_DEFAULT);
    lv_label_set_text_static(s_hint_label, "");
```

Append the three public functions:

```c
void Overlay_ShowFocus(const lv_area_t *area)
{
    if (!s_focus) {
        return;
    }
    if (area == NULL) {
        lv_obj_add_flag(s_focus, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_set_pos(s_focus, area->x1, area->y1);
    lv_obj_set_size(s_focus,
                    area->x2 - area->x1 + 1,
                    area->y2 - area->y1 + 1);
    lv_obj_remove_flag(s_focus, LV_OBJ_FLAG_HIDDEN);
}

void Overlay_ShowHint(const char *text, uint32_t now_ms, uint32_t duration_ms)
{
    if (!s_hint_box) {
        return;
    }
    lv_label_set_text(s_hint_label, text);
    lv_obj_remove_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);
    s_hint_visible  = true;
    s_hint_until_ms = now_ms + duration_ms;
}

bool Overlay_TickHint(uint32_t now_ms)
{
    if (!s_hint_visible) {
        return false;
    }
    /* Unsigned difference, so this stays correct across the 49-day wrap. */
    if ((int32_t)(now_ms - s_hint_until_ms) < 0) {
        return false;
    }
    lv_obj_add_flag(s_hint_box, LV_OBJ_FLAG_HIDDEN);
    s_hint_visible = false;
    return true;
}
```

- [ ] **Step 3: Build and verify the geometry does not collide**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean.

The hint box occupies y 252..292. On the main screen that overlaps the forecast row (192..282) and the sync label (285..297). That is intentional — it is an overlay — but confirm it is drawn **above** the screen content and not behind it. Task 8 exercises it; there is nothing to see on hardware yet.

- [ ] **Step 4: Commit**

```bash
git add firmware/components/ui/overlay.h firmware/components/ui/overlay.c
git commit -m "feat: add focus frame and hint overlay to shared overlay

Hint expiry is driven from the caller's tick, not an LVGL timer, because
lv_timer_handler only runs inside Lvgl_Refresh."
```

---

### Task 8: Wire navigation into the clock task

**Files:**
- Modify: `firmware/components/clock/clock_task.c`
- Modify: `firmware/components/clock/CMakeLists.txt`

**Interfaces:**
- Consumes: `Buttons_*` (Task 4), `nav_*` (Task 5), `Overlay_*` (Tasks 6 and 7).
- Produces: nothing further.

- [ ] **Step 1: Add the dependencies and includes**

In `firmware/components/clock/CMakeLists.txt`, add `input` to `PRIV_REQUIRES`.

In `firmware/components/clock/clock_task.c`, add:

```c
#include "buttons.h"
#include "nav.h"
#include "overlay.h"
#include "esp_timer.h"
```

(`esp_timer.h` is already included; do not add it twice.)

- [ ] **Step 2: Add the navigation state and its helpers**

Add near the other file statics, replacing `s_on_calendar_screen`:

```c
#define HINT_BOOT_MS   5000
#define HINT_EVENT_MS  2500

static nav_state_t s_nav;

/* Main has five focusables; Cal+Clock gets its own in milestone 4. */
static uint8_t nav_focus_count(uint8_t screen)
{
    return screen == NAV_SCREEN_MAIN ? 5 : 0;
}

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static const char *hint_for_mode(const nav_state_t *st)
{
    switch (st->mode) {
    case NAV_SCREEN:
        return st->screen == NAV_SCREEN_MAIN
            ? "Select: calendar     OK: pick a tile     hold OK: settings"
            : "Select: clock        OK: pick a day      hold OK: settings";
    case NAV_FOCUS:
        return "Select: next    OK: details    hold Select: back";
    case NAV_DETAIL:
        return "OK: back        hold Select: back       hold OK: settings";
    case NAV_SETTINGS:
        return "Select: next row     OK: change     hold Select: exit";
    }
    return "";
}

/* Absolute screen rectangles for Main's focusable elements. Index 0 is the
   indoor sensor in the shared top bar; 1..4 are the forecast days. */
static void main_focus_area(uint8_t index, lv_area_t *out)
{
    if (index == 0) {
        out->x1 = 0;   out->y1 = 0;   out->x2 = 158; out->y2 = 33;
        return;
    }
    const int col = (index - 1) * 100;
    out->x1 = col;       out->y1 = 190;
    out->x2 = col + 99;  out->y2 = 283;
}

/* 0xFF means "unknown", which forces the next call to load. */
static uint8_t s_loaded_screen = 0xFF;

static void apply_nav_visuals(void)
{
    /* Only load on an actual screen change. Focus moves must not reload the
       screen, or every Select press would rebuild and flicker it. */
    if (s_loaded_screen != s_nav.screen) {
        loadScreen(s_nav.screen == NAV_SCREEN_MAIN
                       ? SCREEN_ID_MAIN
                       : SCREEN_ID_CALENDAR);
        s_loaded_screen = s_nav.screen;
    }

    if (s_nav.mode == NAV_FOCUS && s_nav.screen == NAV_SCREEN_MAIN) {
        lv_area_t a;
        main_focus_area(s_nav.focus, &a);
        Overlay_ShowFocus(&a);
    } else {
        Overlay_ShowFocus(NULL);
    }
}
```

Because `show_battery_warning()` loads `SCREEN_ID_INIT` behind nav's back, it
must invalidate that cache or the recovery path will believe the right screen
is already loaded. Add as the last line inside its `Lvgl_lock` block:

```c
        s_loaded_screen = 0xFF;
```

- [ ] **Step 3: Replace button handling in the main loop**

Delete `configure_key_button_wakeup()` entirely and its call site. In its place, before the `for (;;)` loop, add:

```c
    Buttons_Init(BTN_SELECT_PIN, BTN_OK_PIN);
    Buttons_EnableWakeup();
    nav_init(&s_nav, nav_focus_count, 1);

    if (Lvgl_lock(-1)) {
        Overlay_ShowHint(hint_for_mode(&s_nav), now_ms(), HINT_BOOT_MS);
        Lvgl_Refresh();
        Lvgl_unlock();
    }
```

`settings_rows` is 1 for now — the Settings screen arrives in its own plan; until then long-press OK puts nav into `NAV_SETTINGS` and the hint says so, which is enough to verify the wiring.

Replace the whole `if (button_pressed && !s_battery_warning_active) { ... }` block with:

```c
        if (button_pressed && !s_battery_warning_active) {
            const btn_event_t ev = Buttons_ReadEvent();
            if (ev != BTN_EV_NONE) {
                const bool changed = nav_handle(&s_nav, ev);
                if (Lvgl_lock(-1)) {
                    if (changed) {
                        apply_nav_visuals();
                        if (s_nav.mode == NAV_SCREEN &&
                            s_nav.screen == NAV_SCREEN_CALCLOCK) {
                            update_calendar_display(&now);
                            update_clock_hands(now.tm_hour, now.tm_min);
                            s_last_cal_day = now.tm_mday;
                            s_last_cal_mon = now.tm_mon;
                            s_last_cal_min = now.tm_min;
                        }
                    }
                    Overlay_ShowHint(hint_for_mode(&s_nav), now_ms(), HINT_EVENT_MS);
                    Lvgl_Refresh();
                    Lvgl_unlock();
                }
            }
        }
```

- [ ] **Step 4: Replace the screen test and add hint expiry to the tick**

Replace every remaining `s_on_calendar_screen` test with `s_nav.screen == NAV_SCREEN_CALCLOCK`. In the per-second dirty block, that is the `else if (s_on_calendar_screen)` branch.

Then extend the same block so an expired hint triggers a redraw:

```c
        if (Lvgl_lock(-1)) {
            bool dirty = Overlay_TickHint(now_ms());

            if (s_battery_warning_active) {
                /* the notice is static - nothing to redraw */
            } else if (s_nav.screen == NAV_SCREEN_CALCLOCK) {
```

The rest of that block is unchanged: it already ORs into `dirty` and refreshes only when set.

In the battery-recovery path, replace:

```c
                        loadScreen(s_on_calendar_screen ? SCREEN_ID_CALENDAR : SCREEN_ID_MAIN);
```

with:

```c
                        apply_nav_visuals();
```

- [ ] **Step 5: Build and verify the whole loop on hardware**

Run: `& $py "$env:IDF_PATH\tools\idf.py" build`
Expected: builds clean. Any remaining `s_on_calendar_screen` reference is a missed edit.

Run: `& $py "$env:IDF_PATH\tools\idf.py" -p COM<N> flash monitor`

Walk through each of these and confirm:

1. At boot the hint overlay appears for about 5 seconds, then disappears on its own.
2. Short-press **Select**: the screen switches to Cal+Clock. Again: back to Main.
3. Short-press **OK** on Main: the focus frame appears around the top-left sensor area.
4. Short-press **Select** repeatedly: the frame steps through the four forecast days, then wraps back to the sensor.
5. **Hold Select**: the frame disappears and you are back to plain screen mode.
6. Short-press **OK** twice: the hint changes to the `NAV_DETAIL` text. There is no detail view yet, which is expected.
7. **Hold OK**: the hint changes to the settings text. Hold Select to escape.
8. Short-press **OK** on Cal+Clock: nothing happens, because it has no focusables until milestone 4.
9. Leave it alone for a minute: the top bar still updates and the clock still ticks.

- [ ] **Step 6: Commit**

```bash
git add firmware/components/clock/clock_task.c firmware/components/clock/CMakeLists.txt
git commit -m "feat: drive screens from the navigation state machine

Select cycles screens, OK enters focus mode, long OK reaches settings.
Replaces the single-button screen toggle."
```

---

## Definition of done

- `python tools/verify_nav.py` prints `all invariants hold`.
- A `CONFIG_CLOCK_SELFTEST=y` build reports `0 failure(s)` over serial.
- All nine manual checks in Task 8 step 5 pass on hardware.
- The displayed time is correct local time while the serial log shows UTC.
- Only one top bar exists in the codebase; `calendar_update_top_bar` is gone.

## Follow-on plans

Written once this lands, because each builds on interfaces created here:

1. **Cal+Clock Vista redraw** (spec milestones 4-5) — dial with second hand, month browsing via `focus_wrapped`, day detail view, weather API extension, sun and moon strip.
2. **Settings and NVS** (milestone 6) — replaces the `settings_rows = 1` stub.
3. **Chimes** (milestone 7) — ES8311 bring-up and synthesised Westminster.
