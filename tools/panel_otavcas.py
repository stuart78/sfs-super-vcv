#!/usr/bin/env python3
"""OTAVCAs' panel (unreleased design), from design/OTAVCAs/OTAVCA_x2_V1.svg (12HP).

Measured (mm, art space), channel 1 | channel 2:
   OUT 2 (22.68, 18.53)   | 5 (53.16, 18.53)     LED 9 (22.68, 26.15) | 8 (53.16, 26.15)
   CV  4 (7.44, 33.77)    | 7 (37.92, 33.77)     IN 3 (22.68, 33.77)  | 6 (53.16, 33.77)
   LVL 1 (22.68, 49.01)   | 0 (53.16, 49.01)     small knobs
   CV attenuverter 11 (15.06, 79.49) | 12 (45.54, 79.49)   GAIN 10 (15.06, 109.97) | 13 (45.54, 109.97)
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/OTAVCAs/OTAVCA_x2_V1.svg"))
off = p.build_panel(os.path.join(root, "res/otavcas.svg"), keep=[8, 9], hp=12)
print(f"res/otavcas.svg: art centred, add {off:.2f} mm to its x positions")
