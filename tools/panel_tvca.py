#!/usr/bin/env python3
"""TVCA's panel, from design/TVCA/TVCA_REV3_PANEL.svg.

Measured (mm, art space):
   OUT 7 (22.62, 18.53)   LED 0 (22.62, 26.15)   CV 8 (22.62, 33.77)
   IN A 10 (7.38, 49.01)   IN B 9 (22.62, 49.01)
   IN level knobs 6 (7.38, 64.25), 5 (22.62, 64.25): small knobs
   sliders: DIST track 2 (7.38, 92.34), INIT track 1 (22.62, 92.34) (kept);
            handles 3, 4 (widgets)
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/TVCA/TVCA_REV3_PANEL.svg"))
off = p.build_panel(os.path.join(root, "res/tvca.svg"), keep=[0, 1, 2], hp=6)
print(f"res/tvca.svg: art centred, add {off:.2f} mm to its x positions")
