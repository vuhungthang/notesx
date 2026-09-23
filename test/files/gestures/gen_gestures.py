#!/usr/bin/env python3
"""Generate the Plan 008 gesture fixture corpus.

The corpus is committed data (test/files/gestures/), not generated at test time, so a reviewer can
read exactly what the recognizers are held to. This script is how it was drawn, kept beside it so
the corpus can be extended deliberately. Run: python3 gen_gestures.py <dest-dir>
"""
import math
import os
import sys


def circle(cx, cy, r, n=64, start=0.0, sweep=2 * math.pi, overshoot=0.0, wobble=0.0):
    pts = []
    total = n if overshoot == 0.0 else n + 1
    for i in range(total):
        frac = (i / (total - 1)) if total > 1 else 0.0
        ang = start + sweep * frac + overshoot
        rr = r * (1.0 + wobble * math.sin(3.0 * ang))
        pts.append((cx + rr * math.cos(ang), cy + rr * math.sin(ang)))
    return pts


def spiral(cx, cy, r0, r1, turns=2.5, n=120):
    pts = []
    for i in range(n):
        frac = i / (n - 1)
        ang = 2 * math.pi * turns * frac
        r = r0 + (r1 - r0) * frac
        pts.append((cx + r * math.cos(ang), cy + r * math.sin(ang)))
    return pts


def line(x0, y0, x1, y1, n=32):
    return [(x0 + (x1 - x0) * i / (n - 1), y0 + (y1 - y0) * i / (n - 1)) for i in range(n)]


def scrub(cx, cy, width, height, passes, n_per_pass=24):
    """A tight back-and-forth scrub: many reversals packed into a small span."""
    pts = []
    for p in range(passes):
        y = cy - height / 2 + height * p / (passes - 1)
        xs = [cx - width / 2, cx + width / 2] if p % 2 == 0 else [cx + width / 2, cx - width / 2]
        for i in range(n_per_pass):
            pts.append((xs[0] + (xs[1] - xs[0]) * i / (n_per_pass - 1), y))
    return pts


def zigzag(x0, y0, width, height, turns, n_per_turn=20):
    pts = []
    for t in range(turns):
        sgn = 1 if t % 2 == 0 else -1
        for i in range(n_per_turn):
            f = i / (n_per_turn - 1)
            pts.append((x0 + width * f, y0 + (0 if sgn > 0 else height) + (height if sgn > 0 else -height) * f * 0))
    # simpler: alternate corners
    pts = []
    for corner in range(turns + 1):
        pts.append((x0 + width * (corner % 2), y0))
    return pts


def translate(pts, dx, dy):
    return [(x + dx, y + dy) for (x, y) in pts]


def scale(pts, f):
    return [(x * f, y * f) for (x, y) in pts]


def thin(pts, keep):
    return pts[::keep] + [pts[-1]]


def write_fixture(dest, name, recognizer, expect, pts, min_confidence=0.5, times=None):
    os.makedirs(dest, exist_ok=True)
    path = os.path.join(dest, name + ".txt")
    with open(path, "w") as fh:
        fh.write("# name: %s\n" % name)
        fh.write("# recognizer: %s\n" % recognizer)
        fh.write("# expect: %s\n" % expect)
        if expect == "match":
            fh.write("# min-confidence: %.3f\n" % min_confidence)
        fh.write("# points: %d\n" % len(pts))
        for i, (x, y) in enumerate(pts):
            if times is not None:
                fh.write("%.4f %.4f %.4f\n" % (x, y, times[i]))
            else:
                fh.write("%.4f %.4f\n" % (x, y))


def main():
    dest_root = sys.argv[1] if len(sys.argv) > 1 else "test/files/gestures"
    cdir = os.path.join(dest_root, "circle")
    sdir = os.path.join(dest_root, "scribble")

    # ---- circle positives: a deliberate large circled gesture, in several shapes -------------
    base = circle(400.0, 400.0, 120.0)
    write_fixture(cdir, "positive-large-circle", "circle", "match", base)
    write_fixture(cdir, "positive-translated", "circle", "match", translate(base, 350.0, -260.0))
    write_fixture(cdir, "positive-scaled-down", "circle", "match", scale(base, 0.75))
    write_fixture(cdir, "positive-scaled-up", "circle", "match", scale(base, 1.6))
    write_fixture(cdir, "positive-sparse-sampling", "circle", "match", thin(base, 4))
    write_fixture(cdir, "positive-dense-sampling", "circle", "match", circle(400.0, 400.0, 120.0, n=240))
    write_fixture(cdir, "positive-reversed-direction", "circle", "match",
                  circle(400.0, 400.0, 120.0, start=0.0, sweep=-2 * math.pi))
    write_fixture(cdir, "positive-wobbly-hand", "circle", "match",
                  circle(400.0, 400.0, 120.0, wobble=0.10))
    write_fixture(cdir, "positive-slight-overshoot", "circle", "match",
                  circle(400.0, 400.0, 120.0, overshoot=0.35))

    # ---- circle negatives: ordinary marks that must stay ink ---------------------------------
    write_fixture(cdir, "negative-letter-O", "circle", "nomatch", circle(200.0, 200.0, 12.0, n=40))
    write_fixture(cdir, "negative-letter-a", "circle", "nomatch",
                  circle(200.0, 200.0, 11.0, n=36) + line(200.0, 211.0, 200.0, 180.0, n=16))
    write_fixture(cdir, "negative-letter-e", "circle", "nomatch",
                  circle(200.0, 200.0, 11.0, start=0.6, sweep=1.9 * math.pi, n=36) +
                  line(189.0, 200.0, 211.0, 200.0, n=12))
    write_fixture(cdir, "negative-equation-zero", "circle", "nomatch", circle(560.0, 300.0, 9.0, n=32))
    write_fixture(cdir, "negative-large-letter-O", "circle", "nomatch", circle(200.0, 200.0, 30.0, n=48))
    write_fixture(cdir, "negative-spiral", "circle", "nomatch", spiral(400.0, 400.0, 12.0, 90.0))
    write_fixture(cdir, "negative-incomplete-loop", "circle", "nomatch",
                  circle(400.0, 400.0, 120.0, start=0.0, sweep=1.55 * math.pi))
    write_fixture(cdir, "negative-flat-ellipse", "circle", "nomatch",
                  [(400.0 + 140.0 * math.cos(2 * math.pi * i / 90), 400.0 + 45.0 * math.sin(2 * math.pi * i / 90))
                   for i in range(91)])
    write_fixture(cdir, "negative-straight-line", "circle", "nomatch", line(100.0, 100.0, 500.0, 500.0, n=64))
    write_fixture(cdir, "negative-arc", "circle", "nomatch",
                  circle(400.0, 400.0, 120.0, start=0.0, sweep=math.pi))
    write_fixture(cdir, "negative-shading-loop", "circle", "nomatch",
                  scrub(400.0, 400.0, 160.0, 110.0, 8, n_per_pass=20))

    # ---- scribble positives: a dense local scrub -------------------------------------------------
    write_fixture(sdir, "positive-dense-scrub", "scribble", "match", scrub(300.0, 300.0, 100.0, 96.0, 16))
    write_fixture(sdir, "positive-dense-scrub-translated", "scribble", "match",
                  scrub(700.0, 500.0, 110.0, 100.0, 17))
    write_fixture(sdir, "positive-dense-scrub-scaled", "scribble", "match",
                  scrub(300.0, 300.0, 80.0, 76.0, 15, n_per_pass=28))
    write_fixture(sdir, "positive-scrub-vertical", "scribble", "match",
                  [(y, x) for (x, y) in scrub(300.0, 300.0, 100.0, 96.0, 16)])

    # ---- scribble negatives: ordinary marks that must stay ink ----------------------------------
    shading = []
    for i in range(6):
        shading += line(200.0, 200.0 + 20.0 * i, 500.0, 200.0 + 20.0 * i, n=30)
    write_fixture(sdir, "negative-shading", "scribble", "nomatch", shading)

    hatching = []
    for i in range(8):
        hatching += line(200.0 + 18.0 * i, 200.0, 200.0 + 18.0 * i + 160.0, 360.0, n=28)
    write_fixture(sdir, "negative-hatching", "scribble", "nomatch", hatching)

    write_fixture(sdir, "negative-crossout-X", "scribble", "nomatch",
                  line(180.0, 180.0, 260.0, 260.0, n=30) + line(260.0, 180.0, 180.0, 260.0, n=30))
    write_fixture(sdir, "negative-crossout-zigzag", "scribble", "nomatch",
                  zigzag(180.0, 200.0, 120.0, 60.0, 5))
    write_fixture(sdir, "negative-equation", "scribble", "nomatch",
                  line(300.0, 300.0, 340.0, 300.0, n=10) + line(390.0, 285.0, 420.0, 315.0, n=12) +
                  line(300.0, 360.0, 340.0, 360.0, n=10))
    write_fixture(sdir, "negative-dense-handwriting", "scribble", "nomatch",
                  [(200.0 + 6.0 * i, 200.0 + 8.0 * math.sin(i * 0.9)) for i in range(80)])
    write_fixture(sdir, "negative-small-circle", "scribble", "nomatch", circle(300.0, 300.0, 18.0, n=40))
    write_fixture(sdir, "negative-short-flick", "scribble", "nomatch",
                  [(200.0, 200.0), (230.0, 205.0), (205.0, 210.0), (235.0, 214.0)])

    print("fixtures written under", dest_root)


if __name__ == "__main__":
    main()
