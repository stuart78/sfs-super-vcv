#!/usr/bin/env python3
"""Turn a Super Synthesis panel export into a VCV panel plus widget parts.

The exports (Illustrator, one per module in design/<MODULE>/) have three
layers: Panel (the background), Graphics (lettering, rings, lines) and
Elements (the drawn jacks, knobs, buttons, LEDs, slider handles). Each drawn
part becomes a widget graphic of its own (cut out with `cut`), is removed
from the panel (`build_panel`), and the art is centred in VCV's HP width at
true scale: hardware panels are 30.00 / 60.60 mm, VCV's 30.48 / 60.96.

Element indices and centres come from measuring the export (a browser's
getBBox is exact); see a module's `tools/panel_<module>.py` for the numbers.

    from panelkit import Panel
    p = Panel("design/PHRSR/PHRSR_REV7_PANEL.svg")
    p.cut(8, "res/parts/knob-large.svg", centre_mm=(15.0, 79.48), size_mm=(11.92, 11.92), rotate=30)
    p.build_panel("res/phrsr.svg", keep=[3, 10, 11, 12], hp=6)
"""
import copy
import re
import xml.etree.ElementTree as ET

SVG = "http://www.w3.org/2000/svg"
XL = "http://www.w3.org/1999/xlink"
ET.register_namespace("", SVG)
ET.register_namespace("xlink", XL)
NS = "{%s}" % SVG
HP_MM = 5.08


class Panel:
    def __init__(self, path):
        self.path = path
        self.tree = ET.parse(path)
        self.root = self.tree.getroot()
        vb = [float(v) for v in self.root.get("viewBox").split()]
        self.vb_w, self.vb_h = vb[2], vb[3]
        self.width_mm = float(re.match(r"([0-9.]+)mm", self.root.get("width")).group(1))
        self.u = self.vb_w / self.width_mm                 # art units per mm
        self.defs = self.root.find(NS + "defs")
        self.groups = {g.get("id"): g for g in self.root.iter(NS + "g") if g.get("id")}
        # EG's export has no Elements layer: its parts are drill circles in "Holes"
        self.elements = list(self.groups["Elements"]) if "Elements" in self.groups else []

    def cut(self, index, out, centre_mm, size_mm, rotate=0.0, drop_stroke=None, recolour=None):
        """Write element `index` as its own SVG, `size_mm` round `centre_mm`
        (the art's own coordinates). `rotate` turns it about its centre, in
        degrees, e.g. to bring a knob's pointer to 12 o'clock. `drop_stroke`
        removes children stroked in that colour (leftover guides).
        `recolour` maps gradient ids to replacements, for a pressed button."""
        node = copy.deepcopy(self.elements[index])
        if drop_stroke:
            for parent in node.iter():
                for c in list(parent):
                    if c.get("stroke", "").lower() == drop_stroke.lower():
                        parent.remove(c)
        cx, cy = centre_mm[0] * self.u, centre_mm[1] * self.u
        w, h = size_mm[0] * self.u, size_mm[1] * self.u
        svg = ET.Element(NS + "svg", {
            "width": f"{size_mm[0]:.4f}mm", "height": f"{size_mm[1]:.4f}mm",
            "viewBox": f"{cx - w / 2:.4f} {cy - h / 2:.4f} {w:.4f} {h:.4f}"})
        defs = copy.deepcopy(self.defs) if self.defs is not None else None
        if defs is not None and recolour:
            for grad in defs:
                if grad.get("id") in recolour:
                    for stop, colour in zip(grad.findall(NS + "stop"), recolour[grad.get("id")]):
                        stop.set("stop-color", colour)
        if defs is not None:
            svg.append(defs)
        if rotate:
            g = ET.SubElement(svg, NS + "g", {"transform": f"rotate({rotate:.4f} {cx:.4f} {cy:.4f})"})
            g.append(node)
        else:
            svg.append(node)
        ET.ElementTree(svg).write(out, xml_declaration=True, encoding="UTF-8")
        return out

    def cut_group(self, group_id, out, recolour_fill=None):
        """Write a whole layer as its own SVG, same size and viewBox as the
        panel (so it sits exactly where it was when overlaid), optionally with
        every fill replaced: e.g. a printed window drawn lit."""
        node = copy.deepcopy(self.groups[group_id])
        if recolour_fill:
            for e in node.iter():
                if e.get("fill") not in (None, "none"):
                    e.set("fill", recolour_fill)
        svg = ET.Element(NS + "svg", {"width": f"{self.width_mm:.4f}mm", "height": self.root.get("height"),
                                     "viewBox": self.root.get("viewBox")})
        svg.append(node)
        ET.ElementTree(svg).write(out, xml_declaration=True, encoding="UTF-8")
        return out

    def build_panel(self, out, keep, hp, drop_stroke=None, drop_groups=()):
        """Write the VCV panel: Elements reduced to `keep`, the art centred in
        `hp` HP at true scale, the background widened to fill it."""
        tree = copy.deepcopy(self.tree)
        root = tree.getroot()
        groups = {g.get("id"): g for g in root.iter(NS + "g") if g.get("id")}
        if "Elements" in groups:
            elems = groups["Elements"]
            for i, c in enumerate(list(elems)):
                if i not in keep:
                    elems.remove(c)
        for gid in drop_groups:          # e.g. drill-hole guides
            g = groups.get(gid)
            if g is not None:
                for parent in root.iter():
                    if g in list(parent):
                        parent.remove(g)
                        break
        if drop_stroke:
            for parent in root.iter():
                for c in list(parent):
                    if c.get("stroke", "").lower() == drop_stroke.lower():
                        parent.remove(c)
        target = hp * HP_MM
        pad = (target - self.width_mm) / 2 * self.u
        root.set("width", f"{target:.2f}mm")
        root.set("viewBox", f"{-pad:.4f} 0 {self.vb_w + 2 * pad:.4f} {self.vb_h:.4f}")
        # the background is the Panel layer's first shape: make it fill the new width
        bg = groups["Panel"][0]
        tag = bg.tag.replace(NS, "")
        if tag == "rect":
            bg.set("x", f"{-pad:.4f}")
            bg.set("width", f"{self.vb_w + 2 * pad:.4f}")
        else:
            fill = bg.get("fill")
            new = ET.Element(NS + "rect", {"x": f"{-pad:.4f}", "y": "0",
                                           "width": f"{self.vb_w + 2 * pad:.4f}",
                                           "height": f"{self.vb_h:.4f}", "fill": fill})
            groups["Panel"].remove(bg)
            groups["Panel"].insert(0, new)
        tree.write(out, xml_declaration=True, encoding="UTF-8")
        return pad / self.u        # the x offset, in mm, to add to art positions
