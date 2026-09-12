#!/usr/bin/env python3
"""Draw a simulation of the main clock face as an SVG.

Not a screenshot. Every coordinate is read out of the UI source at run time, so
the mockup follows the firmware rather than drifting away from it the way a
photograph does - the photo this replaced still showed the date on the top bar,
two redesigns later.

What it cannot reproduce is the panel's own character. The real display is
1-bit with no grey, so strokes are hard-edged and diagonals dither, and type is
rendered here by whatever the viewer has installed rather than by the embedded
Saira and Montserrat. Glyph shapes are close, not exact.

    python tools/render_screen_mockup.py [-o img/screen-simulated.svg]
"""

import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
UI = ROOT / "components" / "ui"

W, H = 400, 300

# What the mockup shows. Chosen to exercise the awkward cases rather than to
# flatter: a negative outdoor temperature, a two-digit indoor reading with a
# decimal, and a weekday/day pair among the widest the face can hold.
SAMPLE = {
    "indoor": "23.4°",
    "humidity": "48%",
    "outdoor": "-4°",
    "battery": "4.14",
    "battery_pct": 82,
    "hh": "19",
    "mm": "03",
    "weekday": "We",
    "day": "24",
    "status": "Sync 24.09 05:00  Up 3d07h12m  R:power-on/01  H 141/104k",
    "days": [
        ("24/9 We", "12/4"),
        ("25/9 Th", "14/6"),
        ("26/9 Fr", "11/3"),
        ("27/9 Sa", "9/1"),
    ],
}

SANS = "Montserrat,Segoe UI,Roboto,Helvetica,Arial,sans-serif"
COND = "Saira Condensed,Arial Narrow,Helvetica Neue,Impact,sans-serif"

# Montserrat ships with LVGL; these metrics are stable and small enough to
# state here rather than hunt for inside managed_components.
MONT = {12: (15, 3), 16: (18, 3), 20: (22, 4)}


def read(path):
    return path.read_text(encoding="utf-8", errors="replace")


def rail_pos(src, var):
    """x, y of an overlay_label(top, x, y, ...) call, resolving RAIL_TEXT_Y."""
    m = re.search(var + r"\s*=\s*overlay_label\(top,\s*(-?\d+),\s*([A-Za-z_0-9]+)", src)
    if not m:
        raise SystemExit("overlay.c: no position for " + var)
    y = m.group(2)
    if not y.lstrip("-").isdigit():
        ym = re.search(r"#define\s+" + y + r"\s+(-?\d+)", src)
        if not ym:
            raise SystemExit("overlay.c: cannot resolve " + y)
        y = ym.group(1)
    return int(m.group(1)), int(y)


def obj_pos(src, name):
    """x, y of the lv_obj_set_pos that follows objects.<name> = obj."""
    m = re.search(r"objects\." + name + r"\s*=\s*obj;\s*lv_obj_set_pos\(obj,\s*(-?\d+),\s*(-?\d+)\)", src)
    if not m:
        raise SystemExit("screens.c: no position for objects." + name)
    return int(m.group(1)), int(m.group(2))


def frame(src, nth):
    calls = re.findall(r"overlay_frame\(top,\s*(\d+),\s*(\d+),\s*(\d+),\s*(\d+)\)", src)
    return tuple(int(v) for v in calls[nth])


def define(src, name):
    m = re.search(r"#define\s+" + name + r"\s+(\d+)", src)
    if not m:
        raise SystemExit("overlay.c: no #define " + name)
    return int(m.group(1))


def metrics(font_file):
    src = read(UI / font_file)
    lh = int(re.search(r"\.line_height\s*=\s*(\d+)", src).group(1))
    bl = int(re.search(r"\.base_line\s*=\s*(\d+)", src).group(1))
    return lh, bl


def esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def text(x, y_top, size, s, family=SANS, anchor="start", box=None, length=None):
    """LVGL places a label by its top-left corner; SVG places text on its
    baseline, so every y here has to be shifted by the font's ascent."""
    lh, bl = MONT.get(size, (int(size * 1.2), int(size * 0.2)))
    baseline = y_top + (lh - bl)
    if box is not None:
        if anchor == "middle":
            x = x + box / 2
        elif anchor == "end":
            x = x + box
    fit = ''
    if length is not None:
        fit = ' textLength="%d" lengthAdjust="spacingAndGlyphs"' % length
    return ('<text x="%s" y="%s" font-family="%s" font-size="%d" text-anchor="%s"%s '
            'fill="#000">%s</text>' % (x, baseline, family, size, anchor, fit, esc(s)))


def cloud(cx, cy, scale=1.0):
    """Stands in for the weather bitmaps, which are C arrays rather than files."""
    r1, r2, w, h = 5 * scale, 6 * scale, 18 * scale, 5 * scale
    return ('<g fill="none" stroke="#000" stroke-width="2">'
            '<circle cx="%s" cy="%s" r="%s"/><circle cx="%s" cy="%s" r="%s"/>'
            '<rect x="%s" y="%s" width="%s" height="%s" rx="%s"/></g>'
            % (cx - 4 * scale, cy, r1, cx + 3 * scale, cy - 2 * scale, r2,
               cx - w / 2, cy + 2 * scale, w, h, h / 2))


def build():
    overlay = read(UI / "overlay.c")
    screens = read(UI / "screens.c")

    temp_x, rail_y = rail_pos(overlay, "s_temp")
    hum_x, _ = rail_pos(overlay, "s_hum")
    out_x, _ = rail_pos(overlay, "s_outdoor")
    sync_x, _ = rail_pos(overlay, "s_sync")

    m = re.search(r"s_battery_volts\s*=\s*overlay_label\(top,\s*(\d+),\s*(\d+),\s*(\d+)", overlay)
    volt_x, volt_y, volt_w = (int(g) for g in m.groups())

    m = re.search(r"lv_obj_set_pos\(s_weather_icon,\s*(\d+),\s*(\d+)\)", overlay)
    wicon_x, wicon_y = int(m.group(1)), int(m.group(2))

    body = frame(overlay, 0)
    nub = frame(overlay, 1)
    fill = (define(overlay, "BATT_FILL_X"), define(overlay, "BATT_FILL_Y"),
            define(overlay, "BATT_FILL_W"), define(overlay, "BATT_FILL_H"))

    hh1 = obj_pos(screens, "clock_hh1")
    hh2 = obj_pos(screens, "clock_hh2")
    mm1 = obj_pos(screens, "clock_mm1")
    mm2 = obj_pos(screens, "clock_mm2")
    date_x, date_y = obj_pos(screens, "clock_date")
    status = obj_pos(screens, "sync")
    rule_y = obj_pos(screens, "obj4")[1]
    big_lh, big_bl = metrics("ui_font_saira_condensed_bold200.c")

    o = []
    a = o.append
    a('<svg xmlns="http://www.w3.org/2000/svg" viewBox="-14 -14 %d %d" width="%d" '
      'height="%d" role="img" aria-label="Simulated clock face reading %s:%s">'
      % (W + 28, H + 28, (W + 28) * 2, (H + 28) * 2, SAMPLE["hh"], SAMPLE["mm"]))
    a('<title>ESP32-S3 RLCD weather clock - main screen (simulated)</title>')
    a('<rect x="-14" y="-14" width="%d" height="%d" rx="10" fill="#2b2b2b"/>' % (W + 28, H + 28))
    # a reflective LCD is a shade off white, never paper white
    a('<rect x="0" y="0" width="%d" height="%d" fill="#eceae4"/>' % (W, H))
    a('<g shape-rendering="crispEdges">')

    # ---- top bar --------------------------------------------------------
    a(text(temp_x, rail_y, 20, SAMPLE["indoor"]))
    a(text(hum_x, rail_y, 20, SAMPLE["humidity"]))
    a(cloud(wicon_x + 12, wicon_y + 13))
    a(text(out_x, rail_y, 20, SAMPLE["outdoor"]))

    # the aerial, which appears only while the link is genuinely up
    sx, sy = sync_x + 3, rail_y + 19
    a('<g fill="none" stroke="#000" stroke-width="2">'
      '<path d="M%d %d a13 13 0 0 1 18 0"/><path d="M%d %d a8 8 0 0 1 10 0"/></g>'
      '<circle cx="%d" cy="%d" r="2.5" fill="#000"/>'
      % (sx, sy - 12, sx + 4, sy - 7, sx + 9, sy - 1))

    a(text(volt_x, volt_y, 16, SAMPLE["battery"], anchor="end", box=volt_w))
    a('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#000" stroke-width="2"/>'
      % body)
    a('<rect x="%d" y="%d" width="%d" height="%d" fill="none" stroke="#000" stroke-width="2"/>'
      % nub)
    a('<rect x="%d" y="%d" width="%d" height="%d" fill="#000"/>'
      % (fill[0], fill[1], round(fill[2] * SAMPLE["battery_pct"] / 100.0), fill[3]))

    # ---- face -----------------------------------------------------------
    baseline = hh1[1] + (big_lh - big_bl)
    digits = ((hh1, SAMPLE["hh"][0]), (hh2, SAMPLE["hh"][1]),
              (mm1, SAMPLE["mm"][0]), (mm2, SAMPLE["mm"][1]))
    for (x, _y), ch in digits:
        # textLength pins each digit to the 90px column the firmware gives it.
        # Without it the mockup depends on the viewer having a condensed face
        # installed, and on anything else the digits sprawl across the colon -
        # showing a layout the device never produces.
        a('<text x="%d" y="%d" font-family="%s" font-size="200" font-weight="700" '
          'textLength="78" lengthAdjust="spacingAndGlyphs" text-anchor="middle" '
          'fill="#000">%s</text>' % (x + 45, baseline, COND, ch))

    # The colon as its two dots. Decoded from the font bitmap: ink rows 0..21
    # and 80..101 within a glyph box starting at y68, giving a 58px gap - which
    # is the hole the date sits in.
    a('<rect x="188" y="68" width="23" height="22" fill="#000"/>')
    a('<rect x="188" y="148" width="23" height="22" fill="#000"/>')
    a(text(date_x, date_y, 16, SAMPLE["weekday"], anchor="middle", box=48))
    a(text(date_x, date_y + 18, 16, SAMPLE["day"], anchor="middle", box=48))

    a('<line x1="0" y1="%d" x2="%d" y2="%d" stroke="#000" stroke-width="2"/>'
      % (rule_y, W, rule_y))

    # ---- four-day forecast ----------------------------------------------
    for i, day in enumerate(SAMPLE["days"]):
        col = i * 100 + 4
        a(text(col, 193, 20, day[0]))
        a(cloud(col + 36, 233, scale=1.45))
        a(text(col, 261, 20, day[1]))

    a(text(status[0], status[1], 12, SAMPLE["status"], length=394))
    a("</g></svg>")
    return "\n".join(o) + "\n"


def main():
    ap = argparse.ArgumentParser(description="Render the simulated clock face.")
    ap.add_argument("-o", "--out", default=str(ROOT / "img" / "screen-simulated.svg"))
    args = ap.parse_args()
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(build(), encoding="utf-8")
    print("wrote " + str(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
