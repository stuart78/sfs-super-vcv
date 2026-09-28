#!/usr/bin/env python3
"""S&H's panel (unreleased design), from design/SNH/SNH_REV1_PCB_PANEL.svg.

Measured (mm, art space). Each channel is drawn as a FET: TRIG on the gate, IN
at the source, OUT and its LED at the drain. The dashed arrows run upwards:
the schematic's first channel is the panel's BOTTOM one.
   first  (bottom): IN 9 (7.38, 109.97)  TRIG 10 (22.62, 94.73)  OUT 8 (7.38, 79.49)  LED 7 (7.38, 87.11)
   second (middle): IN 4 (22.62, 79.49)  TRIG 11 (7.38, 64.25)   OUT 2 (22.62, 49.01) LED 0 (22.62, 56.63)
   third  (top):    IN 5 (7.38, 49.01)   TRIG 6 (22.62, 33.77)   OUT 3 (7.38, 18.53)  LED 1 (7.38, 26.15)
The red rings on the output jacks (#e9282a) are guides and go with the jacks.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/SNH/SNH_REV1_PCB_PANEL.svg"))
off = p.build_panel(os.path.join(root, "res/snh.svg"), keep=[0, 1, 7], hp=6, drop_stroke="#e9282a", drop_groups=["Holes"])
print(f"res/snh.svg: art centred, add {off:.2f} mm to its x positions")
