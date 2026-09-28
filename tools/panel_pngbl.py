#!/usr/bin/env python3
"""PNGBL's PROVISIONAL panel. The design (unreleased) has no panel art at all,
only a schematic and a pick-and-place file (PNGBL_V2_XY.csv), so this is a
stand-in in the house style: the black faceplate and the gold frame and notch,
copied from EG's art, with the lettering and tick rings drawn at runtime by the
widget. Replace it with real art when there is some.

Positions from the pick-and-place (board mm; columns 16.88 / 24.50 / 32.12 are
VCV's 7.62 / 15.24 / 22.86 once centred; rows snapped to the house grid, the
footprints' origins sitting a fixed offset from their shafts):
   KHZ (15.24, 18.53)   RES (15.24, 49.01)   CV amount (15.24, 64.25): large knobs
   CV (7.62, 79.49)     INPUT (22.86, 79.49)
   PING (7.62, 94.73)   BAND (22.86, 94.73)   LED (22.86, 87.11)
   HI (7.62, 109.97)    LOW (22.86, 109.97)
"""
import copy, os, sys
import xml.etree.ElementTree as ET
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel, NS

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/EG/eg.svg"))
g = p.groups["Graphics"]
shapes = [e for e in g.iter() if e.tag in (NS + "path", NS + "polygon", NS + "polyline", NS + "rect")]
frame = copy.deepcopy(shapes[6])                    # EG's frame and notch (27.71 x 121.39 mm)
for c in list(g):
    g.remove(c)
g.append(frame)
for gid in ("Holes", "Front_Window"):
    if gid in p.groups:
        for parent in p.root.iter():
            if p.groups[gid] in list(parent):
                parent.remove(p.groups[gid])
                break
out = os.path.join(root, "design/PNGBL/panel-provisional.svg")
p.tree.write(out, xml_declaration=True, encoding="UTF-8")
q = Panel(out)
off = q.build_panel(os.path.join(root, "res/pngbl.svg"), keep=[], hp=6)
print(f"res/pngbl.svg: provisional panel, add {off:.2f} mm to its x positions")
