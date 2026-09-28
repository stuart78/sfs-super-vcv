#!/usr/bin/env python3
"""A panel from its gerbers, for modules whose only panel art is the PCB (CHORUS).

Super Synthesis panels are PCBs: the gold art is exposed copper, so the top
copper layer (.GTL) is the artwork, drawn as filled regions (G36/G37), and the
board outline layer (.GKO) traces the panel and every hole as closed contours.
This reads both (RS-274X, as DipTrace writes them: linear segments, dark and
clear polarity) into an SVG in the house form -- the panels' black, the art in
their gold -- and lists the holes, whose centres and diameters are the
component positions.

    python3 tools/gerber_panel.py GTL GKO out.svg
"""
import math
import re
import sys

GOLD = "#efcb8f"
BLACK = "#1e1e1e"


def parse(path):
    """Contours from a gerber: [(polarity, [(x, y), ...]), ...] in inches.
    Regions are G36..G37; in the outline layer every D2-started run is one."""
    text = open(path).read()
    fmt = re.search(r"%FSLAX(\d)(\d)Y(\d)(\d)\*%", text)
    dec = int(fmt.group(2))
    scale = 10 ** dec
    inch = "%MOIN*%" in text
    unit = 1.0 if inch else 1 / 25.4
    x = y = 0.0
    polarity = "D"
    contours, cur, in_region = [], [], False
    for raw in re.split(r"\*", text):
        tok = raw.replace("%", "").strip()        # a "%" can precede a newline: strip after
        if not tok:
            continue
        if tok.startswith("LPD"):
            polarity = "D"; continue
        if tok.startswith("LPC"):
            polarity = "C"; continue
        if tok.startswith("G36"):
            in_region, cur = True, []
            continue
        if tok.startswith("G37"):
            if len(cur) > 2:
                contours.append((polarity, cur))
            in_region, cur = False, []
            continue
        m = re.match(r"(?:G0?1)?(?:X(-?\d+))?(?:Y(-?\d+))?D0?([123])$", tok)
        if not m:
            continue
        if m.group(1) is not None:
            x = int(m.group(1)) / scale * unit
        if m.group(2) is not None:
            y = int(m.group(2)) / scale * unit
        d = m.group(3)
        if d == "2":                       # move: a new contour starts
            if len(cur) > 2:
                contours.append((polarity, cur))
            cur = [(x, y)]
        elif d == "1":
            cur.append((x, y))
    if len(cur) > 2:
        contours.append((polarity, cur))
    return contours


def main(gtl, gko, out):
    art = parse(gtl)
    outline = parse(gko)
    # the panel is the largest contour of the outline; the rest are holes
    def bbox(c):
        xs = [p[0] for p in c]; ys = [p[1] for p in c]
        return min(xs), min(ys), max(xs), max(ys)
    # The panel is the outline's rectangle that is a 3U panel tall (128.5 mm):
    # a fabrication set with a border carries a larger one round it, and the
    # biggest contour is then not the panel.
    rects = [c for _, c in outline if len(c) <= 6]
    def err(c):
        x0, y0, x1, y1 = bbox(c)
        return abs((y1 - y0) * 25.4 - 128.5)
    frame = min(rects, key=err) if rects else max((c for _, c in outline),
                                                  key=lambda c: (bbox(c)[2] - bbox(c)[0]) * (bbox(c)[3] - bbox(c)[1]))
    bx0, by0, bx1, by1 = bbox(frame)
    w_mm, h_mm = (bx1 - bx0) * 25.4, (by1 - by0) * 25.4
    U = 2.834646                      # 72 dpi units per mm, as the Illustrator exports
    def pt(p):                        # gerber is y-up from the board's corner
        return ((p[0] - bx0) * 25.4 * U, (by1 - p[1]) * 25.4 * U)
    holes = []
    for _, c in outline:
        if len(c) <= 6:
            continue                  # rectangles: the frame, not holes
        x0, y0, x1, y1 = bbox(c)
        if not (bx0 <= x0 and x1 <= bx1 and by0 <= y0 and y1 <= by1):
            continue
        cx, cy = ((x0 + x1) / 2 - bx0) * 25.4, (by1 - (y0 + y1) / 2) * 25.4
        holes.append((round(cx, 2), round(cy, 2), round((x1 - x0) * 25.4, 2), round((y1 - y0) * 25.4, 2)))
    holes.sort(key=lambda h: (h[1], h[0]))

    def path(c):
        return "M" + "L".join(f"{a:.3f},{b:.3f}" for a, b in map(pt, c)) + "Z"
    def inside(c):
        x0, y0, x1, y1 = bbox(c)
        return x0 >= bx0 - 1e-4 and x1 <= bx1 + 1e-4 and y0 >= by0 - 1e-4 and y1 <= by1 + 1e-4
    art = [(pol, c) for pol, c in art if inside(c)]
    # One <path> per region, each keyhole split into its rings. A region with
    # a hole (the frame, the counter of an O) is ONE gerber contour that cuts
    # in to the hole, runs round it and cuts back out. Inkscape fills that
    # with even-odd; Rack's renderer decides holes per subpath, by whether a
    # subpath lies inside another of the SAME path, so a keyhole never reads as
    # a hole there, and with every region merged into one path each letter
    # inside the frame counted as a hole in it and the frame's inside filled.
    def rings(c):
        key = lambda q: (round(q[0], 6), round(q[1], 6))
        pts = list(c)
        if len(pts) > 1 and key(pts[0]) == key(pts[-1]):
            pts.pop()
        out, stack, seen = [], [], {}
        for q in pts:
            k = key(q)
            if k in seen:
                i = seen[k]
                out.append(stack[i:])
                for r in stack[i + 1:]:
                    seen.pop(key(r), None)
                stack = stack[:i + 1]
            else:
                seen[k] = len(stack)
                stack.append(q)
        out.append(stack)
        def area(r):
            return abs(sum(r[j][0] * r[j - 1][1] - r[j - 1][0] * r[j][1] for j in range(len(r)))) / 2
        return [r for r in out if len(r) >= 3 and area(r) > 1e-9]
    def region(c, fill):
        d = "".join(path(r) for r in rings(c))
        return f'<path d="{d}" fill="{fill}" fill-rule="evenodd"/>'
    dark = "".join(region(c, GOLD) for pol, c in art if pol == "D")
    clear = "".join(region(c, BLACK) for pol, c in art if pol == "C")
    W, H = w_mm * U, h_mm * U
    svg = [f'<?xml version="1.0" encoding="UTF-8"?>',
           f'<svg xmlns="http://www.w3.org/2000/svg" width="{w_mm:.2f}mm" height="{h_mm:.2f}mm" viewBox="0 0 {W:.2f} {H:.2f}">',
           f'  <g id="Panel"><rect x="0" y="0" width="{W:.2f}" height="{H:.2f}" fill="{BLACK}"/></g>',
           f'  <g id="Graphics">{dark}']
    if clear:
        svg.append(f'    {clear}')
    svg.append('  </g>')
    svg.append('  <g id="Holes">' + "".join(
        f'<circle cx="{h[0] * U:.2f}" cy="{h[1] * U:.2f}" r="{h[2] * U / 2:.2f}" fill="none" stroke="#e9282a" stroke-width="0.25"/>'
        for h in holes) + '</g>')
    svg.append('</svg>')
    open(out, "w").write("\n".join(svg) + "\n")
    print(f"{out}: {w_mm:.2f} x {h_mm:.2f} mm, {len(art)} art regions ({sum(1 for p, _ in art if p == 'C')} clear), {len(holes)} holes")
    for h in holes:
        print(f"  hole ({h[0]:6.2f}, {h[1]:6.2f})  {h[2]:.2f} x {h[3]:.2f} mm")


if __name__ == "__main__":
    main(*sys.argv[1:4])
