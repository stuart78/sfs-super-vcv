#!/usr/bin/env python3
"""EG's panel, from design/EG/eg.svg.

The export draws no parts: its "Holes" layer is the drill circles (mm, art space):
   EOC out (7.38, 18.53)    OUT (22.62, 18.53)    OUT LED (22.62, 26.15)
   CV in   (7.38, 33.77)    TRIG (22.62, 33.77)
   CV attenuverter (7.38, 49.01), 6.5 mm: the small knob
   RE button (22.62, 49.01), 3 mm: the tall red tact switch
   ATTACK (15.00, 79.49)    DECAY (15.00, 109.97), the large knobs
"Front_Window" is the translucent RE window the retrigger LED (D5) lights from
behind; it stays in the panel as drawn (unlit) and is cut again, lit, for the
widget that shows it on.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/EG/eg.svg"))
p.cut_group("Front_Window", os.path.join(root, "res/eg-window-lit.svg"), recolour_fill="#ff5a3c")
off = p.build_panel(os.path.join(root, "res/eg.svg"), keep=[], hp=6, drop_groups=["Holes"])
print(f"res/eg.svg: art centred, add {off:.2f} mm to its x positions")
