#!/usr/bin/env python3
"""ROOM's panel, from design/ROOM/room_rev1.svg (30.20 mm wide).

The export draws no parts, only rings and lettering; the positions come from the
panel's gerbers (design/ROOM/gerbers, read by tools/gerber_panel.py into
design/ROOM/panel-from-gerber.svg), shifted 0.04 mm to the export's frame
(its rings sit at 7.54 / 22.78 / 15.16 where the gerber drills 7.58 / 22.82 / 15.20):
   IN (7.54, 18.53)   OUT (22.78, 18.53)   LED (22.78, 26.15)
   DRY/WET: a 20.3 x 2 mm horizontal slot centred (15.10, 33.79)
   FB (7.54, 48.97)   LP (22.78, 64.21)   HP (22.78, 79.45): small knobs
   SIZE (15.16, 109.97): large knob
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/ROOM/room_rev1.svg"))
off = p.build_panel(os.path.join(root, "res/room.svg"), keep=[], hp=6)
print(f"res/room.svg: art centred, add {off:.2f} mm to its x positions")
