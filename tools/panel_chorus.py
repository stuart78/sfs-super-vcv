#!/usr/bin/env python3
"""CHORUS's panel. Its only panel art is the PCB: design/CHORUS/gerbers/, turned
into design/CHORUS/panel-from-gerber.svg by tools/gerber_panel.py (the copper
art in gold on black; the outline's holes as a "Holes" layer). Holes (mm):
   IN (7.38, 18.53)   OUT (22.62, 18.53)   LED (22.62, 26.15)
   CV (7.38, 33.77), with its attenuverter (7.38, 49.01)
   BAL (22.62, 33.77)   FB (22.62, 49.01)            small knobs
   LFO LED (7.38, 71.87)   RATE (7.38, 79.49)   AMT (22.62, 79.49)
   DELAY (15.00, 109.97), the large knob
"""
import os, subprocess, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
g = os.path.join(root, "design/CHORUS/gerbers/chorus_REV1_PANEL")
src = os.path.join(root, "design/CHORUS/panel-from-gerber.svg")
subprocess.run([sys.executable, os.path.join(root, "tools/gerber_panel.py"), g + ".GTL", g + ".GKO", src], check=True,
               stdout=subprocess.DEVNULL)
p = Panel(src)
off = p.build_panel(os.path.join(root, "res/chorus.svg"), keep=[], hp=6, drop_groups=["Holes"])
print(f"res/chorus.svg: art centred, add {off:.2f} mm to its x positions")
