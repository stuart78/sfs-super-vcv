#!/usr/bin/env python3
"""SVFs' panel, from design/SVFs/SVFs_REV3_PANEL.svg (60.60 mm, 12HP).

Measured element centres (mm, the art's own space), channel A | channel B:
   BP out   2 (7.44, 18.53)   |  8 (37.92, 18.53)
   LP out   1 (22.68, 18.53)  |  7 (53.16, 18.53)
   LED     18 (15.06, 26.15)  | 19 (45.54, 26.15)
   IN       3 (7.44, 33.77)   |  9 (37.92, 33.77)
   HP out   0 (22.68, 33.77)  |  6 (53.16, 33.77)
   CV 1V/oct 4 (7.44, 49.01)  | 10 (37.92, 49.01)
   CV atten 5 (22.68, 49.01)  | 11 (53.16, 49.01)
   CV knob 12 (15.06, 64.25)  | 15 (45.54, 64.25)
   RES     13 (15.06, 79.48)  | 16 (45.54, 79.49)
   FREQ    14 (15.06, 109.96) | 17 (45.54, 109.96)
Every part is already in the kit; this only builds the panel. The red rings
round the output jacks (#eb2427) are guides and go with the jacks.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/SVFs/SVFs_REV3_PANEL.svg"))
off = p.build_panel(os.path.join(root, "res/svfs.svg"), keep=[18, 19], hp=12, drop_stroke="#eb2427")
print(f"res/svfs.svg: art centred, add {off:.2f} mm to its x positions")
