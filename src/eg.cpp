#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// EG — the Super Synthesis attack/decay envelope (REV1), modelled from its
// schematic (design/EG/schematic.pdf) and BOM.
//
// THE CYCLE. A rising edge at TRIG (an NPN inverter, then a 1n differentiator
// into a PNP: a ~0.1 ms pulse) SETs a CD4013 flip-flop. While it is set, an
// LM13700 OTA charges C2 (47n) towards the flip-flop's output, 12 V, at
//     dV/dt = (Iabc / 47n) * tanh(0.0426 * (Q - V))
// (both OTA inputs see 220R / 100k of their source: Q on +, the buffered
// envelope on -). The envelope itself is wired to the flip-flop's RESET, so it
// resets the flip-flop when it crosses the 4013's input threshold, half its
// 12 V supply: that is the ~6 V peak the product page gives. With Q at 0 it
// decays the same way towards 0, near-exponentially at these levels.
//
// TIMES. Two MOSFETs, driven by Q and /Q, choose whether ATTACK or DECAY feeds
// the exponential converter. Each pot spans +-5 V; ATTACK goes through an
// inverting stage with a +1.39 V offset (360k from +5 V), DECAY straight
// through, and they are wired in opposite senses so clockwise is longer for
// both. Either then reaches U6.1 through 31k, the CV through 30k, and U6.1's
// 1.8k feedback sets Q2's base; Q2/Q3 turn that into the OTA's bias current,
//     Iabc = Iref * exp(-Vb / VT),   Iref ~ Q2's emitter current, 2.2M to -12 V
// capped near 1.08 mA by R19 (10k) against the bias pin two diodes above -12 V.
// Iref ASSUMES MATCHED Q2/Q3: it is the one constant here the schematic cannot
// give, and it scales every time together. From it, DECAY runs ~3 ms to ~15 s,
// ATTACK ~0.75 ms at its fast end -- and, because of that +1.39 V offset, its
// last quarter runs into minutes. That is what the circuit as drawn does; on a
// real unit, leakage may cap the slow end.
//
// CV. The jack feeds a 10k pot to ground, with 47k/47k also dividing it to the
// wiper, into U5.2 as 2 x wiper - CV: an attenuverter, zero at the centre, and
// then 30k into the converter: about x10 per volt, on attack and decay alike.
//
// RE. The button clocks a second flip-flop (toggle); its Q lights D5 behind the
// panel's translucent "RE" window, and through Q8 disables Q7. Q7 otherwise
// shunts every trigger while the envelope is above ~0.65 V: one-shot mode
// waits for the cycle to finish; RE lets a trigger restart the attack from
// wherever the envelope is.
//
// EOC. A comparator against 0.12 V (12 V x 1k / 101k), differentiated: a pulse,
// ~5 ms per the product page, as the envelope falls back to 0.
// =============================================================================

struct Eg : Module {
	enum ParamId { CV_PARAM, ATTACK_PARAM, DECAY_PARAM, RE_PARAM, PARAMS_LEN };
	enum InputId { CV_INPUT, TRIG_INPUT, INPUTS_LEN };
	enum OutputId { EOC_OUTPUT, OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { OUT_LIGHT, RE_LIGHT, LIGHTS_LEN };

	static constexpr float C2 = 47e-9f;
	static constexpr float VT = 0.02585f;
	static constexpr float IMAX = 1.08e-3f;
	static constexpr float RESET_V = 6.f;       // CD4013 input threshold at VDD 12 V
	static constexpr float QHIGH = 12.f;
	static constexpr float BLOCK_V = 0.65f;     // Q7 turns on
	static constexpr float EOC_V = 0.119f;
	static const int SUB = 4;

	float v = 0.f;          // C2, the envelope
	bool q = false;         // flip-flop A
	bool re = false;        // flip-flop B: retrigger mode
	float setTime = 0.f;    // the trigger's SET pulse
	float eocTime = 0.f;
	bool below = true;      // EOC comparator
	dsp::SchmittTrigger trig, button;

	Eg() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CV_PARAM, 0.f, 1.f, 0.5f, "CV amount (attenuverter: centre is none)", "%", 0.f, 200.f, -100.f);
		configParam(ATTACK_PARAM, 0.f, 1.f, 0.3f, "Attack");
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configButton(RE_PARAM, "RE: re-trigger (lit) or one-shot");
		configInput(CV_INPUT, "Length CV (attack and decay together)");
		configInput(TRIG_INPUT, "Trigger (rising edge)");
		configOutput(EOC_OUTPUT, "End of cycle (a ~5 ms trigger as the envelope returns to 0)");
		configOutput(OUT_OUTPUT, "Envelope (0 to ~6 V)");
		configLight(OUT_LIGHT, "Envelope");
		configLight(RE_LIGHT, "Re-trigger");
	}

	void onReset() override { v = 0.f; q = false; re = false; eocTime = 0.f; }

	// U5.2's output: 2 x wiper - CV, the wiper loaded by the 47k/47k divider
	static float cvBuffer(float vj, float k) {
		float p = clamp(k, 0.f, 1.f);
		float rp = p * (1.f - p) * 10e3f, rd = 23.5e3f;
		float m = rp < 1.f ? p * vj : (p * vj / rp + 0.5f * vj / rd) / (1.f / rp + 1.f / rd);
		return 2.f * m - vj;
	}

	// Q2's base, from whichever pot the MOSFETs let through, and the CV
	static float baseVoltage(bool attack, float attackK, float decayK, float cvOut) {
		float i;
		if (attack) {
			float va = -5.f + 10.f * attackK;               // CW: +5 V
			i = -(va + 5.f * 100.f / 360.f) / 31e3f;         // U6.2 inverts, with R1's offset
		} else {
			float vd = 5.f - 10.f * decayK;                 // wired the other way: CW is -5 V
			i = vd / 31e3f;
		}
		i += cvOut / 30e3f;
		return -1.8e3f * i;
	}

	static float biasCurrent(float vb) {
		float iref = (vb + 12.f - 0.65f) / 2.2e6f;          // Q2's emitter current
		float iexp = iref * std::exp(clamp(-vb / VT, -60.f, 60.f));
		return iexp * IMAX / (iexp + IMAX);                  // R19 caps it
	}

	void process(const ProcessArgs& args) override {
		if (button.process(params[RE_PARAM].getValue()))
			re = !re;

		if (trig.process(inputs[TRIG_INPUT].getVoltage(), 0.5f, 0.8f)) {
			// Q7 shunts the SET pulse while the envelope is up, unless RE
			if (re || v < BLOCK_V)
				setTime = 1e-4f;
		}

		float cvOut = cvBuffer(inputs[CV_INPUT].getVoltage(), params[CV_PARAM].getValue());
		float dt = args.sampleTime / SUB;
		for (int s = 0; s < SUB; s++) {
			if (setTime > 0.f) { q = true; setTime -= dt; }
			else if (v >= RESET_V) q = false;
			float vb = baseVoltage(q, params[ATTACK_PARAM].getValue(), params[DECAY_PARAM].getValue(), cvOut);
			float iabc = biasCurrent(vb);
			float target = q ? QHIGH : 0.f;
			v += dt * iabc / C2 * std::tanh(0.04255f * (target - v));
			v = clamp(v, 0.f, 11.f);
		}

		bool nowBelow = v < EOC_V;
		if (nowBelow && !below) eocTime = 0.005f;
		below = nowBelow;
		if (eocTime > 0.f) eocTime -= args.sampleTime;

		outputs[OUT_OUTPUT].setVoltage(v);
		outputs[EOC_OUTPUT].setVoltage(eocTime > 0.f ? 8.6f : 0.f);   // 10.5 V through 220R into 1k
		lights[OUT_LIGHT].setBrightness(v / RESET_V);
		lights[RE_LIGHT].setBrightness(re ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "re", json_boolean(re));
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "re")) re = json_boolean_value(j);
	}
};

// The translucent RE window, lit from behind by D5: the window's own glyphs
// (res/eg-window-lit.svg, cut from the panel at full panel size) drawn over the
// unlit ones at the light's brightness, with a soft glow round them.
struct EgWindow : widget::Widget {
	Eg* module = nullptr;
	std::shared_ptr<window::Svg> svg;
	EgWindow() : svg(Svg::load(asset::plugin(pluginInstance, "res/eg-window-lit.svg"))) {}
	void drawLayer(const DrawArgs& args, int layer) override {
		if (layer != 1 || !svg || !svg->handle) return;
		float b = module ? module->lights[Eg::RE_LIGHT].getBrightness() : 1.f;
		if (b <= 0.f) return;
		NVGcontext* vg = args.vg;
		// the glow, centred on the window (art 23.0, 56.4 mm)
		Vec c = mm2px(Vec(23.0f, 56.4f));
		NVGpaint glow = nvgRadialGradient(vg, c.x, c.y, mm2px(1.f), mm2px(5.f),
		                                  nvgRGBAf(1.f, 0.35f, 0.24f, 0.35f * b), nvgRGBAf(1.f, 0.35f, 0.24f, 0.f));
		nvgBeginPath(vg);
		nvgRect(vg, c.x - mm2px(6.f), c.y - mm2px(6.f), mm2px(12.f), mm2px(12.f));
		nvgFillPaint(vg, glow);
		nvgFill(vg);
		nvgSave(vg);
		nvgGlobalAlpha(vg, b);
		window::svgDraw(vg, svg->handle);
		nvgRestore(vg);
	}
};

struct TactRed : app::SvgSwitch {
	TactRed() {
		momentary = true;
		addFrame(super::part("tact-red.svg"));
		addFrame(super::part("tact-red-pressed.svg"));
		shadow->opacity = 0.f;
	}
};

// The drill positions (tools/panel_eg.py) plus 0.24 mm in x.
struct EgWidget : ModuleWidget {
	EgWidget(Eg* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/eg.svg")));

		// the lit window sits where the art put it: the art's origin is 0.24 mm in
		EgWindow* w = new EgWindow;
		w->module = module;
		w->box.pos = mm2px(Vec(0.24f, 0.f));
		w->box.size = mm2px(Vec(30.f, 128.5f));
		addChild(w);

		const float xl = 7.62f, xr = 22.86f, xm = 15.24f;
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xl, 18.53f)), module, Eg::EOC_OUTPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 18.53f)), module, Eg::OUT_OUTPUT));
		addChild(createWidgetCentered<super::LedBody>(mm2px(Vec(xr, 26.15f))));   // the art drills, but draws no LED
		addChild(createLightCentered<super::Led>(mm2px(Vec(xr, 26.15f)), module, Eg::OUT_LIGHT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 33.77f)), module, Eg::CV_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 33.77f)), module, Eg::TRIG_INPUT));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xl, 49.01f)), module, Eg::CV_PARAM));
		addParam(createParamCentered<TactRed>(mm2px(Vec(xr, 49.01f)), module, Eg::RE_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 79.49f)), module, Eg::ATTACK_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 109.97f)), module, Eg::DECAY_PARAM));
	}
};

Model* modelEg = createModel<Eg, EgWidget>("EG");
