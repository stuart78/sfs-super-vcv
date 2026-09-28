#!/usr/bin/env python3
"""VCAs' panel, from design/VCAs/VCAs_REV2_PANEL.svg (60.60 mm, 12HP).

Four columns at x = 7.44 / 22.68 / 37.92 / 53.16 mm (art space); per column:
   OUT jack y 18.53 (8, 11, 14, 17)    LED y 26.15 (20-23, drawn amber: kept)
   CV jack  y 33.77                    IN jack y 49.01
   IN attenuverter, small knob, y 64.25 (24-27)
   level slider: track y 92.34 (0, 2, 4, 6: kept), handles (1, 3, 5, 7: widgets)
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/VCAs/VCAs_REV2_PANEL.svg"))
off = p.build_panel(os.path.join(root, "res/vcas.svg"), keep=[0, 2, 4, 6, 20, 21, 22, 23], hp=12)
print(f"res/vcas.svg: art centred, add {off:.2f} mm to its x positions")
