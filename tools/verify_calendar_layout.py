#!/usr/bin/env python3
"""Checks the UI's geometry against real glyph widths parsed from LVGL's
compiled font sources, rather than against a sample string someone eyeballed.

This exists because eyeballing failed. The Main-screen date was sized around
"Fri 21" - 41px, comfortably inside its 43px column - and shipped. The real
worst case is "Wed 24" at 63px: the layout never fit any wide date, and it was
only caught by re-measuring for an unrelated change. Every check here takes the
maximum over the whole value set, never one example.

Host-side and standalone, same convention as tools/verify_nav.py:

    python tools/verify_calendar_layout.py

Exit code 0 and "all geometry checks pass" when everything fits.
"""
import math
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
FONT_DIR = REPO_ROOT / "managed_components" / "lvgl__lvgl" / "src" / "font"
UI_DIR = REPO_ROOT / "components" / "ui"

ADV_W_RE = re.compile(r"\.adv_w\s*=\s*(-?\d+)")
CMAP_ENTRY_RE = re.compile(
    r"\.range_start\s*=\s*(\d+),\s*\.range_length\s*=\s*(\d+),\s*"
    r"\.glyph_id_start\s*=\s*(\d+),\s*\.unicode_list\s*=\s*(\w+)"
)
LINE_HEIGHT_RE = re.compile(r"\.line_height\s*=\s*(\d+)")


class Font:
    """Advance widths and line height parsed out of a compiled LVGL font.

    adv_w is 1/16 px fixed point. LVGL's own glyph lookup rounds with
    (adv_w + 8) >> 4, so that is what is used here - measuring in fractional
    pixels would disagree with what the renderer actually advances by.
    """

    def __init__(self, path: Path):
        text = path.read_text(encoding="utf-8", errors="replace")
        self.name = path.stem

        glyphs = re.search(r"glyph_dsc\[\]\s*=\s*\{(.*?)\n\};", text, re.DOTALL)
        if not glyphs:
            raise ValueError(f"{path.name}: no glyph_dsc[] table")
        adv = [int(m.group(1)) for m in ADV_W_RE.finditer(glyphs.group(1))]

        cmaps = re.search(r"cmaps\[\]\s*=\s*\{(.*?)\n\};", text, re.DOTALL)
        if not cmaps:
            raise ValueError(f"{path.name}: no cmaps[] table")

        self.widths: dict[str, int] = {}
        for m in CMAP_ENTRY_RE.finditer(cmaps.group(1)):
            start, length, gid0 = int(m.group(1)), int(m.group(2)), int(m.group(3))
            list_name = m.group(4)

            if list_name == "NULL":
                # Contiguous range: codepoint start+i maps to glyph gid0+i.
                for off in range(length):
                    gid = gid0 + off
                    if gid < len(adv):
                        self.widths[chr(start + off)] = (adv[gid] + 8) >> 4
                continue

            # Sparse range: a lookup table of offsets from range_start, in
            # order. This is where the degree sign and the LV_SYMBOL_* glyphs
            # live, and skipping it silently loses every non-ASCII character
            # the UI actually uses.
            lst = re.search(rf"{list_name}\[\]\s*=\s*\{{(.*?)\}};", text, re.DOTALL)
            if not lst:
                continue
            offsets = [int(x, 16) for x in re.findall(r"0x[0-9a-fA-F]+", lst.group(1))]
            for i, off in enumerate(offsets):
                gid = gid0 + i
                if gid < len(adv):
                    self.widths[chr(start + off)] = (adv[gid] + 8) >> 4

        lh = LINE_HEIGHT_RE.search(text)
        self.line_height = int(lh.group(1)) if lh else 0

    def width(self, text: str) -> int:
        missing = [c for c in text if c not in self.widths]
        if missing:
            raise KeyError(f"{self.name} has no glyph for {missing!r}")
        return sum(self.widths[c] for c in text)

    def widest(self, candidates) -> tuple[int, str]:
        best = max(candidates, key=self.width)
        return self.width(best), best


def font(size: int) -> Font:
    path = FONT_DIR / f"lv_font_montserrat_{size}.c"
    if not path.exists():
        raise FileNotFoundError(
            f"{path} missing - check CONFIG_LV_FONT_MONTSERRAT_{size} is enabled "
            f"and the component has been fetched"
        )
    return Font(path)


def constant(source: Path, name: str) -> int:
    """Reads a #define from the C source, so these checks cannot drift away
    from the geometry they claim to verify."""
    m = re.search(rf"^#define\s+{name}\s+(\d+)", source.read_text(encoding="utf-8",
                                                                  errors="replace"),
                  re.MULTILINE)
    if not m:
        raise ValueError(f"{source.name}: no #define {name}")
    return int(m.group(1))


DAYS_2 = [f"{d}" for d in range(1, 32)]
WEEKDAYS_2 = ["Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"]
MONTHS = ["January", "February", "March", "April", "May", "June", "July",
          "August", "September", "October", "November", "December"]


def check_calendar_grid(errors):
    """Day cells must fit their column at both sizes the grid uses:
    montserrat_14 for the displayed month, montserrat_12 for the leading and
    trailing days of the neighbouring ones."""
    screens = UI_DIR / "screens.c"
    col_w = constant(screens, "CAL_COL_W")
    f14, f12 = font(14), font(12)

    for f, what in ((f14, "in-month day"), (f12, "adjacent-month day")):
        w, sample = f.widest(DAYS_2)
        if w > col_w:
            errors.append(f"{what} {sample!r} is {w}px at {f.name}, over CAL_COL_W {col_w}px")

    w, sample = f14.widest(WEEKDAYS_2)
    if w > col_w:
        errors.append(f"weekday header {sample!r} is {w}px, over CAL_COL_W {col_w}px")


def check_calendar_title(errors):
    """The month/year header sits between the two month arrows."""
    screens = UI_DIR / "screens.c"
    col_w = constant(screens, "CAL_COL_W")
    arrow_w = constant(screens, "CAL_ARROW_W")
    available = col_w * 7 - 2 * arrow_w

    f16 = font(16)
    titles = [f"{m} {y}" for m in MONTHS for y in (2026, 2038)]
    w, sample = f16.widest(titles)
    if w > available:
        errors.append(
            f"calendar title {sample!r} is {w}px at montserrat_16, over the "
            f"{available}px between the arrows"
        )


def check_face_date(errors):
    """The Main-screen date lives in the gap between the colon's two dots.

    This is the check that would have caught the shipped bug: the column is
    43px, "Fri 21" is 41px, and "Wed 24" is 63px. Stacked over two lines the
    widest line is just the day number, which is what makes it fit.
    """
    f16 = font(16)
    COLUMN_W = 43           # the ':' advance box, x175..218
    INK_CHANNEL_W = 49      # actual free space between digit ink, x172..221

    one_line = [f"{d} {n}" for d in ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"]
                for n in DAYS_2]
    w_one, sample_one = f16.widest(one_line)
    if w_one <= COLUMN_W:
        errors.append(
            f"one-line date {sample_one!r} now measures {w_one}px, inside the "
            f"{COLUMN_W}px column - the stacked layout may no longer be needed, "
            f"but check before changing it"
        )

    stacked = WEEKDAYS_2 + DAYS_2
    w_two, sample_two = f16.widest(stacked)
    if w_two > COLUMN_W:
        errors.append(
            f"stacked date line {sample_two!r} is {w_two}px at montserrat_16, "
            f"over the {COLUMN_W}px colon column"
        )
    if w_two > INK_CHANNEL_W:
        errors.append(
            f"stacked date line {sample_two!r} at {w_two}px would collide with "
            f"the clock digits, which leave only {INK_CHANNEL_W}px free"
        )

    two_lines = 2 * f16.line_height
    DOT_GAP_H = 58          # between the colon's dots, y90..147
    if two_lines > DOT_GAP_H:
        errors.append(
            f"two montserrat_16 lines are {two_lines}px, over the {DOT_GAP_H}px "
            f"gap between the colon's dots"
        )


def check_top_bar(errors):
    """Top-bar slots must not run into each other at their widest values.

    Slot x positions are read from overlay.c so this cannot silently drift.
    """
    overlay = (UI_DIR / "overlay.c").read_text(encoding="utf-8", errors="replace")

    def slot_x(var: str) -> int:
        m = re.search(rf"{var}\s*=\s*overlay_label\(top,\s*(\d+)", overlay)
        if not m:
            raise ValueError(f"overlay.c: could not find x for {var}")
        return int(m.group(1))

    f20 = font(20)
    temps = [f"{s}{w}.{f}°" for s in ("", "-") for w in (0, 88) for f in (0, 8)]
    hums = [f"{h}%" for h in (0, 100)]
    outdoor = [f"{s}{v}°" for s in ("", "-") for v in (0, 25, 88)]

    slots = [
        ("indoor temp", slot_x("s_temp"), f20.widest(temps), slot_x("s_temp_trend")),
        ("humidity", slot_x("s_hum"), f20.widest(hums), slot_x("s_hum_trend")),
        ("outdoor temp", slot_x("s_outdoor"), f20.widest(outdoor), slot_x("s_sync")),
    ]
    for name, x, (w, sample), next_x in slots:
        if x + w > next_x:
            errors.append(
                f"top bar {name} {sample!r} runs from x{x} to x{x + w}, past the "
                f"next slot at x{next_x}"
            )

    # battery voltage is montserrat_16 in a fixed-width right-aligned box
    f16 = font(16)
    volts = [f"{v // 100}.{v % 100:02d}" for v in range(300, 421)]
    w, sample = f16.widest(volts)
    m = re.search(r"s_battery_volts\s*=\s*overlay_label\(top,\s*(\d+),\s*\d+,\s*(\d+)", overlay)
    if not m:
        errors.append("overlay.c: could not find the battery voltage slot")
    else:
        box_w = int(m.group(2))
        if w > box_w:
            errors.append(
                f"battery voltage {sample!r} is {w}px at montserrat_16, over its "
                f"{box_w}px box"
            )


def check_status_line(errors):
    """The bottom status line is one line in a 400px label. It carries the
    reset reason, which is why it is measured against the longest reason
    string rather than a typical one."""
    f12 = font(12)
    SCREEN_W = 400
    reasons = ["power-on", "ext-pin", "sw-restart", "PANIC", "INT-WDT", "TASK-WDT",
               "WDT", "deepsleep", "BROWNOUT", "sdio", "USB-host", "jtag",
               "efuse-err", "PWR-GLITCH", "CPU-LOCKUP", "unknown"]
    # Any reason can carry a trailing "*", which clock_task.c appends when the
    # previous run recorded a failed allocation. It is one character, and this
    # line had 11px of slack before it existed, so it is measured, not assumed.
    samples = [
        f"Sync 30.09 23:59  Up 88d23h59m  R:{r}{star}/17  H 188/188k"
        for r in reasons for star in ("", "*")
    ]
    w, sample = f12.widest(samples)
    if w > SCREEN_W:
        errors.append(
            f"status line {sample!r} is {w}px at montserrat_12, over the "
            f"{SCREEN_W}px panel width"
        )


def check_dial(errors):
    """The dial's minor markers are 3x3 dithered blocks near the rim.

    Task 3 established on hardware that thin radial *lines* break into speckle
    at diagonal angles on this 1-bit panel, which is why these are axis-aligned
    blocks instead. What still has to hold is that neighbouring blocks do not
    touch - once they do, the ring reads as a solid band rather than as ticks.
    """
    screens = UI_DIR / "screens.c"
    radius = constant(screens, "CLOCK_RADIUS") - 4      # MINOR_DOT_RADIUS
    size = constant(screens, "MINOR_DOT_SIZE")
    COUNT = 48                                          # draw_minor_dots()

    spacing = 2 * math.pi * radius / COUNT
    if spacing <= size:
        errors.append(
            f"{COUNT} dots of {size}px at r={radius} are {spacing:.2f}px apart - "
            f"they touch, and the ring will read as a solid band"
        )

    canvas = constant(screens, "CLOCK_SIZE")
    centre = constant(screens, "CLOCK_CENTER")
    bezel_outer = constant(screens, "CLOCK_RADIUS") + 8  # draw_clock_bezel() band
    if centre + bezel_outer > canvas:
        errors.append(
            f"bezel reaches r={bezel_outer} from centre {centre}, outside the "
            f"{canvas}px canvas - it will be clipped at the cardinals"
        )


def main() -> int:
    errors: list[str] = []
    for check in (check_calendar_grid, check_calendar_title, check_face_date,
                  check_top_bar, check_status_line, check_dial):
        try:
            check(errors)
        except Exception as exc:                        # noqa: BLE001
            errors.append(f"{check.__name__} could not run: {exc}")

    if errors:
        for e in errors:
            print(f"FAIL: {e}")
        print(f"\n{len(errors)} geometry check(s) failed")
        return 1

    print("all geometry checks pass")
    return 0


if __name__ == "__main__":
    sys.exit(main())
