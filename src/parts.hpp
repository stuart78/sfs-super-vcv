#pragma once
// The parts kit: the Super Synthesis panels' own drawn jacks, knobs, buttons,
// sliders and LEDs, cut out of the art (res/parts/, by tools/panelkit.py) and
// used as the widgets' graphics, so each module looks the way its panel is
// drawn. The panel keeps what does not move: slider tracks, LED off-states,
// tick rings and lettering.
#include "plugin.hpp"

namespace super {

static inline std::shared_ptr<window::Svg> part(const char* name) {
	return Svg::load(asset::plugin(pluginInstance, std::string("res/parts/") + name));
}

// Hex-nut jacks: grey for inputs, red for outputs, as on the hardware.
struct JackIn : app::SvgPort {
	JackIn() { setSvg(part("jack-in.svg")); shadow->opacity = 0.f; }
};
struct JackOut : app::SvgPort {
	JackOut() { setSvg(part("jack-out.svg")); shadow->opacity = 0.f; }
};

// Knobs turn -150..+150 degrees, over the panels' rings of eleven dots whose
// hollow one, bottom left, is the minimum. The cut-outs have their pointer at
// 12 o'clock, which is the middle of the travel.
struct KnobSmall : app::SvgKnob {       // 6.1 mm black, white pointer (2OPFM, TVCA, VCAs)
	KnobSmall() {
		minAngle = -5.f * M_PI / 6.f;
		maxAngle = 5.f * M_PI / 6.f;
		setSvg(part("knob-small.svg"));
		shadow->opacity = 0.f;
	}
};
struct KnobLarge : app::SvgKnob {       // 11.9 mm grey, knurled (PHRSR, SVFs, SCANNER, OTAVCAs)
	KnobLarge() {
		minAngle = -5.f * M_PI / 6.f;
		maxAngle = 5.f * M_PI / 6.f;
		setSvg(part("knob-large.svg"));
		shadow->opacity = 0.f;
	}
};

// A 9 mm grey cap. Pressed, its gradient turns round, as the light does on a
// cap pushed into the panel. Momentary: the hardware's buttons are.
struct Button : app::SvgSwitch {
	Button() {
		momentary = true;
		addFrame(part("button.svg"));
		addFrame(part("button-pressed.svg"));
		shadow->opacity = 0.f;
	}
};

// The light inside an illuminated button: a glow over the cap in the LED red,
// strongest in the middle and translucent, so the cap's own shading still
// reads through it. Transparent when off, so the drawn cap is all there is.
struct ButtonGlow : app::ModuleLightWidget {
	ButtonGlow() {
		bgColor = nvgRGBA(0, 0, 0, 0);
		borderColor = nvgRGBA(0, 0, 0, 0);
		addBaseColor(nvgRGB(0xf0, 0x62, 0x4a));
		box.size = mm2px(Vec(8.4f, 8.4f));
	}
	void drawLight(const DrawArgs& args) override {
		if (color.a <= 0.f) return;
		float r = box.size.x / 2.f;
		NVGcolor inner = color, outer = color;
		inner.a *= 0.95f;
		outer.a *= 0.5f;
		nvgBeginPath(args.vg);
		nvgCircle(args.vg, r, r, r);
		nvgFillPaint(args.vg, nvgRadialGradient(args.vg, r, r, 0.f, r, inner, outer));
		nvgFill(args.vg);
	}
};

// An illuminated latching button: a click toggles the value and the cap lights
// while it is on; the cap itself springs back, as a real latching switch does.
// Construct with createLightParamCentered.
struct LitLatch : componentlibrary::LightButton<Button, ButtonGlow> {
	LitLatch() {
		momentary = false;
		latch = true;
	}
};

// A slider handle riding a track drawn in the panel. The widget is an
// invisible box a little wider than the handle (slider-hit.svg), 27.31 mm
// long over a 25.31 mm track, so the handle is easy to grab; its centre
// travels to within 2.46 mm of each end of the track, where 2OPFM's art parks
// its DECAY handle.
struct Slider : app::SvgSlider {
	Slider() {
		setBackgroundSvg(part("slider-hit.svg"));
		setHandleSvg(part("slider-handle.svg"));
		setHandlePosCentered(mm2px(Vec(1.8f, 23.85f)), mm2px(Vec(1.8f, 3.46f)));
		// With no module (the browser thumbnail) nothing ever positions the
		// handle and it sits at the top. Start it mid-travel; a real value
		// moves it in onChange.
		handle->box.pos = minHandlePos.plus(maxHandlePos).div(2.f);
	}
};

// A horizontal slider in a drilled slot (ROOM's DRY/WET): the track is drawn
// by the part, since the art only drills the 20.3 x 2 mm slot; the handle is
// 2OPFM's, turned. Left is the minimum.
struct SliderH : app::SvgSlider {
	SliderH() {
		horizontal = true;
		setBackgroundSvg(part("slider-h-track.svg"));
		setHandleSvg(part("slider-h-handle.svg"));
		setHandlePosCentered(mm2px(Vec(3.46f, 2.f)), mm2px(Vec(18.84f, 2.f)));
		handle->box.pos = minHandlePos.plus(maxHandlePos).div(2.f);
	}
};

// An LED with no background and no border: the panel's drawn LED is the off
// state, and only the light and its halo are drawn over it. The hardware
// drives them from op-amp current sources, so brightness is linear in what
// they show.
struct Led : app::ModuleLightWidget {
	Led() {
		bgColor = nvgRGBA(0, 0, 0, 0);
		borderColor = nvgRGBA(0, 0, 0, 0);
		addBaseColor(nvgRGB(0xf0, 0x62, 0x4a));
		box.size = mm2px(Vec(3.1f, 3.1f));
	}
};

// An amber LED, for the panels that draw theirs amber (VCAs).
struct LedAmber : Led {
	LedAmber() { baseColors[0] = nvgRGB(0xff, 0xa2, 0x3a); }
};

// The LED body, drawn: for panels that drill a hole for an LED but draw none
// (EG). Add it before the light, so the light draws over it.
struct LedBody : widget::SvgWidget {
	LedBody() { setSvg(part("led-off.svg")); }
};

}  // namespace super
