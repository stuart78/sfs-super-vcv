# CLAUDE.md

**SFS Super** (working name, slug `SFSSuper`): the Super Synthesis Eurorack
library (Chris McDowell, CC0, github.com/supersynthesis/eurorack), ported to VCV
Rack. A sister plugin to Signal Function Set (`~/code/SignalFunctionSet`), whose
conventions it follows. **The plan, the order and each module's status are in
`docs/porting-plan.md`**; update it as modules land.

## Build

- `make` builds `plugin.dylib`. **`make dev` is the local install**: it packages
  a copy with every `"hidden": true` flipped off and copies it into Rack's plugin
  folder (Rack unpacks it at the next launch). Plain `make install` keeps the
  flags, and Rack shows hidden modules nowhere, so after it nothing new appears.
  `RACK_DIR` defaults to `../Rack-SDK`.
- `make dist && ./tools/screenshots.sh` renders every module into
  `screenshots/` (the README's images) through Rack's own `-t`, in a throwaway
  user dir; Rack must be closed and the screen unlocked. It draws with
  `module == NULL`, the Library's thumbnail path.
- `./tools/static-check.sh` runs cppcheck and clang-tidy as the VCV Library
  does (exit status = findings). Run it before every release.
- Releases: bump `plugin.json`, tag `vX.Y.Z`; `.github/workflows/release.yml`
  builds all four platforms and attaches them (this plugin is distributed from
  GitHub, so unlike Signal Function Set its releases carry binaries).

## Rules carried over from Signal Function Set

- **New modules start `"hidden": true`**, and are unhidden only after discussion.
- **Params, inputs, outputs and lights serialise by index**: append, never
  insert; retire in place.
- **No screws** on any panel, and no mounting-hole slots (the hardware art has
  both; take them out when adapting a panel).
- **Panels are the hardware art**, with the drawn parts cut out as widget
  graphics (see `docs/porting-plan.md`, *Panels*). The art carries its own
  lettering, so no runtime `PanelLabels`.
- **Verify with the real code**: a `tools/<module>-harness.cpp` compiles the
  module against libRack; digital modules are checked against their firmware.
- `src/panel-style.hpp`, `src/fastmath.hpp` and `src/membrane.hpp` are copies of
  Signal Function Set's, not links, so neither plugin can break the other.

## Modules

1. **2OPFM** (slug `2OPFM`, `src/twoopfm.cpp`, hidden). Moved from Signal
   Function Set, where it was the unreleased "2OP", on 2026-09-26. The 2OPFM
   panel with its drawn jacks, knobs and slider handles as the widgets
   (the parts kit was first cut from it); context menu picks the firmware: the stock firmware
   emulated (its own 39.975 kHz ISR and 5 kHz envelope tick, verbatim tables,
   zero-order-hold DAC), 2OPFM+, Drum, CZ, Formant, Bell. The same DSP runs on
   the hardware from `~/code/2opfm-alt` (`App/Src/alt.c`), checked sample for
   sample against `tools/2opfm-harness.cpp`: **change one, change both**.
2. **PHRSR** (slug `PHRSR`, `src/phrsr.cpp`, hidden). The firmware's TIM3
   handler and `step_increment()` transcribed with their integer types, run at
   the chip's 1422.2 Hz (64 MHz / 45001); keeps its quirks (the uint16 pot
   smoothing stalls up to 49 counts short when rising; an external clock edge
   during a high internal clock double-steps; the clock out is set one
   interrupt late on purpose). Outputs 0-6.6 V through the schematic's x2
   stages, A/B with the 1n feedback cap (half the jump at once, the rest at
   1.6 kHz). A = the LEFT button's sequence, which the firmware writes to the
   net the schematic calls BOUT. Buttons LATCH in VCV (a mouse cannot hold REC
   while turning DC). Sequences saved in the patch. `tools/phrsr-harness.cpp`.

3. **SVFs** (slug `SVFs`, `src/svfs.cpp`, hidden, 12HP). Two 2164 state-variable
   filters modelled from the REV3 schematic and BOM: summer HP = IN - LP - 0.1BP
   + 3.1V+, damping k = 3.1a - 0.1 (the 1M makes it negative at full RES: self-
   oscillation), a from the 100k RES pot with 10k on its wiper; back-to-back 4.7 V
   zeners limit oscillation near 11 Vpp EXCEPT at the very top of RES, where the
   wiper grounds U5.1+ and the rails limit it (both are tested). fc = 24.1 kHz x
   2^(-Vc/0.1987) (-33 mV/dB; the 1k+150k 0.1% input is exactly 1V/oct); FREQ
   sweeps 19.7 octaves; Vc >= 0 (D6/D8), so never above 24.1 kHz; attenuverter
   +-2.96 oct/V. TPT SVF, 2x oversampled, damping from the previous sample's BP.
   A -120 dB noise floor starts self-oscillation, as the hardware's own noise
   does. **The zener knee must be exactly zero below threshold**: a smooth max
   that merely approaches zero leaked 1 mV, which is enough damping at the
   microvolts oscillation starts from to stop it. `tools/svfs-harness.cpp`.

4. **EG** (slug `EG`, `src/eg.cpp`, hidden). The REV1 attack/decay: TRIG sets a
   CD4013; an LM13700 charges 47n towards its 12 V Q at (Iabc/C) tanh(0.0426(Q-V))
   until the envelope itself crosses the 4013's reset threshold (6 V: the
   product page's "~6 V" peak), then decays the same way to 0. Iabc from an
   expo converter: Iref (Q2's emitter current, ASSUMED matched Q2/Q3 -- the one
   constant the schematic cannot give) x exp(-Vb/VT), capped ~1.08 mA. As drawn,
   ATTACK carries a +1.39 V offset (360k from +5 V), so its range is skewed slow:
   ~0.75 ms fast end, ~3.5 s mid, minutes at the top; DECAY ~4 ms to ~2 min to
   0.12 V. CV is an attenuverter (U5.2 = 2 x wiper - CV), ~x10 per volt on both.
   RE (red tact, toggles flip-flop B) lights the panel's translucent "RE" window
   (the `Front_Window` layer, cut lit as res/eg-window-lit.svg and drawn over at
   the light's brightness); off = one-shot (Q7 shunts triggers above ~0.65 V).
   EOC: a 5 ms, ~8.6 V pulse as the envelope falls below 0.12 V. The panel draws
   no parts, only drill `Holes` (dropped from the build); `tools/eg-harness.cpp`.

5. **VCAs** (slug `VCAs`, `src/vcas.cpp`, hidden, 12HP). Four linear VCAs from
   the REV2 schematic: a second 2164 cell in the control amp's feedback, fed
   -5 V/100k, makes G = (slider 0-5 V + CV)/5 - 0.01, clamped 0..1 (D7/D8 hold
   Vc >= 0: unity is the ceiling). IN through an attenuverter (2 x wiper - IN).
   An UNPATCHED OUT is summed (inverter + 30k) into the next channel's
   transimpedance node, 1 -> 2 -> 3 -> 4: `isConnected()` decides it.
   `tools/vcas-harness.cpp`.

6. **SCANNER** (slug `SCANNER`, `src/scanner.cpp`, hidden). The REV2 CV
   crossfader: scan = 1.155 x (knob 0-5 V + direct CV + attenuverted CV); four
   channels off a 1/2/3/4 V ladder, each U2.1 = 2V+ - scan with D7 clamping V+
   a drop above the offset (a tent), U2.3 = 5 x (tent - previous offset), D6
   forward only: ~7 V triangles 3 V wide peaking at 1.5/2.5/3.5/4.5 V of scan,
   overlapping. Outputs numbered in reading order (the schematic does not say
   which jack is which). Diode knees exactly flat outside. `tools/scanner-harness.cpp`.

7. **TVCA** (slug `TVCA`, `src/tvca.cpp`, hidden). The REV3 two-input tanh VCA:
   inputs through level trimmers into a summer; DIST is a 100k/10k/1k divider
   into the LM13700 (f 0.009..0.099); Iabc = (INIT 0-5 V + CV)/10k, linear,
   never negative (D4); out = 10k x Iabc x tanh(f x mix / 2VT). Even with DIST
   down a 5 V signal is compressed ~2 dB (45 mV at the OTA): the circuit's own
   colour, tested as such. `tools/tvca-harness.cpp`.

8. **CHORUS** (slug `CHORUS`, `src/chorus.cpp` + `src/chorus_tables.hpp`,
   hidden). The firmware's chorus_tick/chorus_control_tick at its own rates
   (48 MHz: 49.95 kHz audio, 7.5 kHz control; filters written for 53333 Hz, so
   ~6% off: kept): HP 150 / LP 8k, x0.7, 15,000-sample int16 line read at
   in - lfo(1 - delay) - delay x 14999 (300 ms max, matching the product page),
   INVERTED feedback x FB. The firmware's float_expo_table is declared [1024]
   with 1021 written: RATE/AMT at the very top read 0 (kept, tested). No
   schematic: the audio front end is ASSUMED to be a Eurorack effect's (+-10 V
   fills the ADC, 204.8 counts/V, unity wet gain); BAL (linear dry/wet) and the
   CV attenuverter are analog, per the product page. **Rate conversion is
   `src/fwrate.hpp`** (shared with ROOM): band-limit, interpolate to each tick,
   interpolate the DAC, reconstruction filter. The first port sampled the
   nearest host sample and held the DAC, and used 2OPFM's 409.6 counts/V in and
   x6.04 out: a 5 V signal filled the ADC, ROOM's reverb sat on its +-2047
   clamp 58% of the time, and the jitter alone held both modules near -33 dB
   THD+N against the firmware's own -50. Panel from the PCB gerbers by
   `tools/gerber_panel.py` (copper = the gold art; the outline's contours = the
   holes). `tools/chorus-harness.cpp`.

9. **ROOM** (slug `ROOM`, `src/room.cpp`, hidden). The firmware's sandbox_tick
   at 32.92 kHz (56 MHz / 1701; delay maths written for 32 kHz, LFOs for
   53.3 kHz: kept): 4 input + 3 loop allpasses (gain 0.5, times x SIZE, two
   LFO-modulated) all in ONE shared 15,000-sample int16 buffer (a walking write
   point), HP/LP in the loop, inverted feedback x FB. Uses CHORUS's expo table
   (identical): LP at the very top closes the loop's low-pass and SILENCES the
   wet signal, HP/SIZE read 0 too -- kept as shipped, with "Complete the
   firmware's expo table" in the menu. The export draws no parts; positions come
   from its gerbers (the gap between DRY and WET is a 20.3 mm horizontal slider
   slot, `super::SliderH`). Analog I/O assumed as CHORUS's, through the same
   `src/fwrate.hpp`. FB near the top is the firmware's own: 16-bit rounding
   recirculates as grit from ~0.9, 0.98 nearly holds, and at the very top the
   loop gain passes 1 and it runs away to full scale. `tools/room-harness.cpp`
   (checks a 5 V sine stays off the clamp and under -45 dB THD+N).

10. **OTAVCAs** (slug `OTAVCAs`, `src/otavcas.cpp`, hidden, 12HP; unreleased
    design). Two LM13700 VCAs: Iabc = 10 x (GAIN 0-5 V/100k + attenuverted
    CV/100k), 500 uA at full GAIN, never negative; audio through LVL and a
    51k/1.5k divider into the OTA's - input (0.0286/V: saturates readily), 13k
    transimpedance, non-inverting overall; OUT 1 unpatched sums into OUT 2.
    `tools/otavcas-harness.cpp`.

11. **S&H** (slug `SH` -- "&" is not a slug character -- `src/snh.cpp`, hidden;
    unreleased design). Three JFET sample-and-holds: trigger compared at 0.58 V,
    differentiated (10n into 10k/10k, 0.2 ms), gating the J112 for ~0.58 ms per
    RISING edge. IN normalled from the channel below's OUT, TRIG from its
    differentiated edge INVERTED: one trigger samples channel 1 on the rise,
    2 on the fall, 3 ~0.58 ms later. The panel draws the chain upwards, so the
    first channel is the BOTTOM one. The BOM has no LEDs; the drawn ones follow
    the outputs. `tools/snh-harness.cpp`.

12. **PNGBL** (slug `PNGBL`, `src/pngbl.cpp`, hidden; unreleased "OTA SVF").
    SVFs' loop on LM13700 integrators (fc = Iabc x 3.07e7, OTA tanh from the
    previous sample), 5.1 V zeners, PING = a rising edge differentiated into a
    ~4 V, 15 ms kick into the summer. Bias from EG's converter topology (Iref
    through 200k to -5 V, split between the OTAs): ~334 Hz centre, 2 Hz-56 kHz.
    The design has NO panel: res/pngbl.svg is provisional (EG's frame via
    tools/panel_pngbl.py), lettering and rings drawn at runtime (PngblMarks),
    positions from the pick-and-place file. `tools/pngbl-harness.cpp`.

## Shared code

- `tools/gerber_panel.py`: a panel from its PCB gerbers (copper layer = the art,
  outline contours = holes), for modules with no panel art (CHORUS).
- `src/parts.hpp` + `res/parts/`: the parts kit (jacks, knobs, button, slider,
  LED), cut from the panels by `tools/panelkit.py`; `tools/panel_<module>.py`
  records each panel's measured element indices and rebuilds its `res/` files.

