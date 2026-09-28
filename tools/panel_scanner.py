#!/usr/bin/env python3
"""SCANNER's panel, from design/SCANNER/SCANNER_REV2_PANEL.svg.

Measured (mm, art space):
   outputs  2 (7.38, 18.53)  1 (22.62, 18.53)  6 (7.38, 33.77)  0 (22.62, 33.77)
   LEDs     5 (7.38, 26.15)  8 (22.62, 26.15)  7 (7.38, 41.39)  9 (22.62, 41.39)
   CV in    3 (7.38, 49.01): through the -/+ attenuverter;  4 (22.62, 49.01): direct
   knobs   10 attenuverter (15.00, 79.49), 11 SCAN (15.00, 109.96)
"""
import os, sys
sys.path.insert(0, os.path.dirname(__file__))
from panelkit import Panel

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
p = Panel(os.path.join(root, "design/SCANNER/SCANNER_REV2_PANEL.svg"))
off = p.build_panel(os.path.join(root, "res/scanner.svg"), keep=[5, 7, 8, 9], hp=6)
print(f"res/scanner.svg: art centred, add {off:.2f} mm to its x positions")
