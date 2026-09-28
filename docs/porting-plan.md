# Porting plan

The Super Synthesis library (Chris McDowell, CC0:
github.com/supersynthesis/eurorack), ported to VCV Rack, one module at a time,
each on its own hardware panel art. Every module starts `"hidden": true` and is
unhidden only after it has been played and discussed.

`design/<MODULE>/` holds each module's sources: the `.ai` panel and its
Illustrator SVG export, `schematic.pdf`, and for the digital modules `firmware/`
(the module's own code, without ST's HAL).

## The order

The library's own order (its README), then the two firmware modules it leaves
off that list, then the unreleased designs.

| # | Module | Kind | Sources | Status |
|---|---|---|---|---|
| 1 | **2OPFM** | digital (G431) | firmware, schematic, panel | **done**: stock emulated + 5 alternative firmwares; `~/code/2opfm-alt` runs them on the hardware |
| 2 | **PHRSR** | digital (F334) | firmware (650 lines), schematic, panel | **done**: firmware transcribed at its 1422 Hz interrupt, output stages from the schematic, latching buttons; `tools/phrsr-harness.cpp` |
| 3 | **SVFs** | analog, 12HP | schematic, panel | **done**: 2164 SVF modelled stage by stage (summer, integrators, RES network with zener limiter, CV summing); `tools/svfs-harness.cpp` |
| 4 | **EG** | analog | schematic, panel (parts drawn as placement circles) | **done**: OTA + 4013 cycle, expo converter, one-shot/RE with the lit RE window, EOC; `tools/eg-harness.cpp` |
| 5 | **VCAs** | analog, 12HP | schematic, panel | **done**: linear 2164 VCAs (a 2164 in the control loop), attenuverted inputs, the unpatched-output chain; `tools/vcas-harness.cpp` |
| 6 | **SCANNER** | analog | schematic, panel | **done** (render pending): scan sum, D7 tents, 5x windows off a 1-4 V ladder; `tools/scanner-harness.cpp` |
| 7 | **TVCA** | analog | schematic, panel | **done** (render pending): OTA tanh VCA, DIST divider, linear INIT+CV current; `tools/tvca-harness.cpp` |
| 8 | **CHORUS** | digital (G431, 49.95 kHz) | firmware; panel from gerbers | **done** (render pending): firmware at its own ISR rates; panel rebuilt from the PCB by `tools/gerber_panel.py`; analog I/O assumed from 2OPFM; `tools/chorus-harness.cpp` |
| 9 | **ROOM** | digital (G431, 32.9 kHz) | firmware, panel + gerbers | **done** (render pending): 7 allpasses in one shared buffer; positions from the gerbers (the DRY/WET "gap" is a horizontal slider slot); table-bug fix in the menu; `tools/room-harness.cpp` |
| 10 | **OTAVCAs** | analog, 12HP, unreleased | schematic, panel | **done** (render pending): two saturating LM13700 VCAs, linear Iabc, 1 into 2; `tools/otavcas-harness.cpp` |
| 11 | **S&H** (slug `SH`) | analog, unreleased | schematic, panel | **done** (render pending): three JFET S&Hs, input and inverted-edge trigger normalled up the chain; `tools/snh-harness.cpp` |
| 12 | **PNGBL** | analog, unreleased | schematic + pick-and-place only | **done** (render pending): OTA SVF with ping; PROVISIONAL panel (EG's frame, runtime lettering) until there is art; `tools/pngbl-harness.cpp` |

## How each kind is ported

**Digital modules** are ported from their firmware, not re-imagined. The 2OPFM
method: the firmware's own code runs at its own interrupt rates, with its own
tables, integer maths and quirks (float-to-integer saturation, table-index
wraps), behind the front end the schematic describes (inverting CV stages,
DAC-to-output gain), and a zero-order-hold DAC. Where a firmware only makes
sense on the chip (DMA, the ADC mux), the ADC values are synthesised from
voltages instead. Check it by compiling the module against libRack in a
`tools/<module>-harness.cpp`, and wherever possible against the firmware's C
compiled for the host.

**Analog modules** are modelled from their schematics: the transfer function of
each stage (2164 VCAs, OTAs, tanh stages, integrators, comparators), component
values included, so ranges and curves come from the circuit rather than from
taste. State each modelling simplification in the source, next to the stage it
simplifies.

## Panels

Each module wears its real panel, as 2OPFM does: the drawn jacks, knobs,
sliders, buttons and LEDs are cut out of the art and become the widgets' own
graphics; tracks and LED off-states stay in the panel; the art's lettering
means no runtime labels. Physical panels are 30.00 mm (6HP) and 60.60 mm (12HP)
against VCV's 30.48 and 60.96, so the art is centred in the VCV width at true
scale, never stretched.

Removed for VCV, as on 2OPFM: **mounting-hole slots** and **screws** (TVCA and
VCAs draw them). Every panel has the slots.

**Export from Illustrator, not the converted files.** The `.ai` files are PDFs
underneath, and `design/*/panel-source.svg` were converted from them with
pdftocairo: good for reading positions (a browser renders them correctly), but
they are full of clip paths and masks, which Rack's renderer (NanoSVG) ignores,
so a clipped gradient would draw as a full rectangle. An Illustrator
*Export As > SVG* with outlined text, as the 2OPFM panel was, has none.

## The parts kit

The panels share one vocabulary, so the widgets do too: `src/parts.hpp` over
`res/parts/`, cut from the art by `tools/panelkit.py` (each module has a
`tools/panel_<module>.py` recording the measured element indices and centres).

| Part | Widget | Cut from |
|---|---|---|
| Hex jack, grey (input) | `super::JackIn` | 2OPFM |
| Hex jack, red (output) | `super::JackOut` | 2OPFM |
| Small black knob, ±150° over 11 dots | `super::KnobSmall` | 2OPFM |
| Large grey knurled knob, ±150° | `super::KnobLarge` | PHRSR (pointer rotated to 12 o'clock) |
| Slider handle on a drawn track | `super::Slider` | 2OPFM |
| 9 mm grey button, pressed frame | `super::Button` | PHRSR |
| Red LED over its drawn off-state | `super::Led` | 2OPFM |
| LED body (off state) | `super::LedBody` | PHRSR, for panels that drill an LED hole but draw none (EG) |
| Red tact switch (tall, TL1105) | `TactRed` in eg.cpp | drawn: the art only drills a 3 mm hole |
| Amber LED | `super::LedAmber` | VCAs (its art draws them amber) |
| Horizontal slider in a slot | `super::SliderH` | ROOM (2OPFM's handle turned; the part draws the track) |

**Buttons latch where a mouse needs them to.** PHRSR's are held on the
hardware while turning a knob or pressing another button; one mouse cannot, so
there they toggle (`momentary = false`). The firmware sees the same held states.

## Before release

CC0 covers the code and the art. It does not cover the **Super Synthesis name
or the product names**, and the VCV Library generally expects a hardware
maker's blessing for recreations. **Chris McDowell gave his permission on
2026-09-28**; cite it when submitting to the Library.
The working name "SFS Super" (slug `SFSSuper`) is provisional; the slug is
permanent only from the first release.
