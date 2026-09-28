#!/usr/bin/env python3
"""PHRSR's panel and its new parts, from design/PHRSR/PHRSR_REV7_PANEL.svg.

Element indices and centres were measured from the export (getBBox, mm in
the art's own 30.00 mm space):
   0 button REC right (22.62, 49.01)   1 button STEPS (22.62, 64.25)
   2 button REC left  (7.38, 49.01)    3 LED B, second drawing (22.62, 26.15)
   4 jack A out (7.38, 18.53)          5 jack B out (22.62, 18.53)
   6 jack CLK out (22.62, 33.77)       7 jack CLK in (7.38, 33.77)
   8 knob DC (15.00, 79.48), pointer drawn at -30 deg
   9 knob RATE (15.00, 109.97), pointer at +60 deg
  10 LED B (22.62, 26.15)   11 LED A (7.38, 26.15)   12 LED CLK (22.62, 41.39)
Jacks and LEDs are the shared parts (cut from 2OPFM); this cuts the large
knob and the button, which PHRSR introduces.
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/PHRSR/PHRSR_REV7_PANEL.svg"))
# the large knob, pointer brought to 12 o'clock
p.cut(8, os.path.join(root, "res/parts/knob-large.svg"), (15.00, 79.48), (11.92, 11.92), rotate=30)
# the button, and the same button pressed: its gradient turned round, so the
# light falls on the other side, as it does on a cap pushed into the panel
p.cut(2, os.path.join(root, "res/parts/button.svg"), (7.38, 49.01), (9.00, 9.00))
p.cut(2, os.path.join(root, "res/parts/button-pressed.svg"), (7.38, 49.01), (9.00, 9.00),
      recolour={"linear-gradient": ["#5a5a5a", "#9a9a9a"]})
# the LED body (its off state), for panels that drill a hole but draw no LED (EG)
p.cut(11, os.path.join(root, "res/parts/led-off.svg"), (7.38, 26.15), (3.10, 3.10))
off = p.build_panel(os.path.join(root, "res/phrsr.svg"), keep=[3, 10, 11, 12], hp=6)
print(f"res/phrsr.svg: art centred, add {off:.2f} mm to its x positions")
