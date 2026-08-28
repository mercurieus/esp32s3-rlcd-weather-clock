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
