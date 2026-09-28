#include "plugin.hpp"
#include "parts.hpp"
#include "panel-style.hpp"
#include <cmath>

// =============================================================================
// PNGBL — a pingable OTA state-variable filter, an unreleased Super Synthesis
// design ("OTA SVF", 2021), modelled from its schematic
// (design/PNGBL/schematic.pdf) and BOM.
//
// It is SVFs' loop with LM13700 integrators:
//
// SUMMER (U5.4). INPUT, the PING pulse, LP (each 100k) and BP (1M) into an
// inverting summer with 100k feedback, and the RES network on its + input:
//     HP = -(IN + PING) - LP - 0.1 BP + 3.1 V+
// RES (100k from BP to ground, 10k on the wiper) and back-to-back 5.1 V zeners
// from BP to V+ behave as in SVFs: damping k = 3.1a - 0.1, self-oscillation at
// the top of the knob, the zeners (~5.7 V with the diode drop) limiting it.
//
// INTEGRATORS. Each is an LM13700 fed through 100k into 220R, integrating on
// 220p in an inverting buffer, so the loop sees
//     d(out)/dt = -(Iabc / 220p) tanh(0.0425 x in)
// fc = Iabc x 3.07e7 Hz per amp, the tanh a gentle saturation on hot signals.
//
// FREQUENCY. KHZ (+-5 V through 75k) and the CV (75k into an attenuverter:
// a 100k pot, 47k/47k, U2.2 inverting one share, zero at the centre, up to
// 1/75k per volt) sum into U2.1 with 2k feedback, driving the same NPN/PNP
// exponential converter as EG: Iabc = Iref e^(-Vb/VT), Iref ~ Q1's emitter
// current (200k to -5 V; matched Q1/Q2 ASSUMED), split between the two OTAs by
// 8.2k each. ~330 Hz at the centre of KHZ, ~2 Hz to ~58 kHz across it, the CV
// up to ~1.5 octaves per volt.
//
// PING. A rising edge at PING (NPN inverter, 100k/100k collector divider: a
// 6 V fall) is differentiated by 100n and passed by D2 into the summer through
// 100k: a pulse of ~4 V decaying over ~15 ms, less a diode drop. That kick is
// what rings the filter. A falling edge is clamped away by D3.
//
// The LED follows BP, one polarity. Outputs HI, BAND, LOW through 1k. As in
// SVFs: TPT SVF, 2x oversampled, nonlinear damping and the OTA tanh taken from
// the previous sample, a -120 dB noise floor to start self-oscillation.
//
// THE PANEL IS PROVISIONAL: the design has none. See tools/panel_pngbl.py.
// =============================================================================

struct Pngbl : Module {
	enum ParamId { KHZ_PARAM, RES_PARAM, CV_PARAM, PARAMS_LEN };
	enum InputId { CV_INPUT, IN_INPUT, PING_INPUT, INPUTS_LEN };
	enum OutputId { HI_OUTPUT, BAND_OUTPUT, LOW_OUTPUT, OUTPUTS_LEN };
	enum LightId { BAND_LIGHT, LIGHTS_LEN };

	static const int OS = 2;
	float s1 = 0.f, s2 = 0.f, bpNode = 0.f, hpPrev = 0.f;
	float ping = 0.f;               // the differentiated node, volts below 0
	dsp::SchmittTrigger pingTrig;
	uint32_t noiseState = 0x9E3779B9u;

	Pngbl() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KHZ_PARAM, 0.f, 1.f, 0.5f, "Frequency");
		configParam(RES_PARAM, 0.f, 1.f, 0.5f, "Resonance");
		configParam(CV_PARAM, 0.f, 1.f, 0.5f, "CV amount (attenuverter: centre is none)", "%", 0.f, 200.f, -100.f);
		configInput(CV_INPUT, "Frequency CV (through the attenuverter)");
		configInput(IN_INPUT, "Audio");
		configInput(PING_INPUT, "Ping (a rising edge rings the filter)");
		configOutput(HI_OUTPUT, "High-pass");
		configOutput(BAND_OUTPUT, "Band-pass");
		configOutput(LOW_OUTPUT, "Low-pass");
		configLight(BAND_LIGHT, "Band-pass");
	}

	float noise() {
		noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
		return ((noiseState >> 8) * (1.f / 16777216.f) - 0.5f) * 2e-6f;
	}

	static float rail(float v) {
		float a = std::fabs(v);
		if (a <= 9.f) return v;
		return std::copysign(9.f + 1.5f * std::tanh((a - 9.f) / 1.5f), v);
	}

	// The OTAs' shared bias: KHZ and the attenuverted CV through U2.1 (2k) to the converter.
	static float bias(float khzK, float cv, float cvK) {
		float vk = -5.f + 10.f * clamp(khzK, 0.f, 1.f);
		float p = clamp(cvK, 0.f, 1.f);
		auto par = [](float a, float b) { return (a + b) > 0.f ? a * b / (a + b) : 0.f; };
		float rtop = par((1.f - p) * 100e3f, 47e3f), rbot = par(p * 100e3f, 47e3f);
		float i = cv / (75e3f + par(rtop, rbot));
		float net = (rtop + rbot) > 0.f ? i * (rbot - rtop) / (rtop + rbot) : 0.f;
		float vb = -2e3f * (vk / 75e3f + net);
		float iref = (vb - 0.65f + 5.f) / 200e3f;
		float ic = iref * std::exp(clamp(-vb / 0.02585f, -60.f, 60.f));
		return std::min(ic / 2.f, 2e-3f);
	}

	static void resonance(float resK, float& alpha, float& rth) {
		float y = clamp(1.f - resK, 0.f, 1.f);
		float rb = y * 100e3f, ra = (1.f - y) * 100e3f;
		float rp = rb > 0.f ? rb * 10e3f / (rb + 10e3f) : 0.f;
		alpha = (ra + rp) > 0.f ? rp / (ra + rp) : 1.f;
		rth = (ra + rp) > 0.f ? ra * rp / (ra + rp) : 0.f;
	}

	static float vPlus(float bp, float alpha, float rth) {
		float v = alpha * bp, e = bp - v;
		const float vz = 5.7f, w = 0.15f, rz = 100.f;
		float ex = std::fabs(e) - vz;
		float over = ex <= -w ? 0.f : ex >= w ? ex : (ex + w) * (ex + w) / (4.f * w);
		return v + std::copysign(over * rth / (rth + rz), e);
	}

	// tanh(x)/x of an OTA's input, 0.0425 per volt of the node driving it
	static float otaGain(float v) {
		float x = 0.0425f * v;
		return std::fabs(x) < 1e-4f ? 1.f : std::tanh(x) / x;
	}

	void process(const ProcessArgs& args) override {
		float fs = args.sampleRate * OS;
		float iabc = bias(params[KHZ_PARAM].getValue(), inputs[CV_INPUT].getVoltage(), params[CV_PARAM].getValue());
		float fc = clamp(iabc * 3.07e7f, 0.5f, 0.45f * fs);
		float alpha, rth;
		resonance(params[RES_PARAM].getValue(), alpha, rth);

		// PING: a rising edge drops the node ~4 V; it drains over ~15 ms
		if (pingTrig.process(inputs[PING_INPUT].getVoltage(), 0.5f, 0.8f))
			ping = -4.f;
		float pingIn = ping < -0.6f ? ping + 0.6f : 0.f;      // through D2
		ping *= std::exp(-args.sampleTime / 15e-3f);

		float x = -(inputs[IN_INPUT].getVoltage() + pingIn) + noise();
		float hp = 0.f, bp = 0.f, lp = 0.f;
		for (int o = 0; o < OS; o++) {
			float g = std::tan((float)M_PI * fc / fs);
			float g1 = g * otaGain(hpPrev), g2 = g * otaGain(bpNode);
			float k = std::fabs(bpNode) > 1e-3f ? 3.1f * vPlus(bpNode, alpha, rth) / bpNode - 0.1f : 3.1f * alpha - 0.1f;
			float h = (x - (k + g2) * s1 - s2) / (1.f + g1 * (k + g2));
			float b = g1 * h + s1;
			s1 = clamp(g1 * h + b, -11.f, 11.f);
			float l = g2 * b + s2;
			s2 = clamp(g2 * b + l, -11.f, 11.f);
			hpPrev = rail(h);
			bpNode = rail(-b);           // the integrators invert: the node is -BP
			hp += hpPrev; bp += bpNode; lp += rail(l);
		}
		outputs[HI_OUTPUT].setVoltage(hp / OS);
		outputs[BAND_OUTPUT].setVoltage(bp / OS);
		outputs[LOW_OUTPUT].setVoltage(lp / OS);
		lights[BAND_LIGHT].setBrightnessSmooth(clamp(bp / OS / 5.f, 0.f, 1.f), args.sampleTime);
	}
};

// The provisional panel's lettering and tick rings, in the panels' gold, since
// the design has no art (tools/panel_pngbl.py). Drawn, not SVG: Rack ignores
// <text>, and there is no outlined art to cut them from.
struct PngblMarks : widget::Widget {
	std::shared_ptr<Font> font;
	void draw(const DrawArgs& args) override {
		if (!font || font->handle < 0) font = sfs::panelFontBold();
		NVGcontext* vg = args.vg;
		const NVGcolor gold = nvgRGB(0xef, 0xcb, 0x8f);
		// rings of eleven dots round each large knob, the hollow one the minimum
		auto ring = [&](float cx, float cy, bool centreHollow) {
			for (int i = 0; i < 11; i++) {
				float a = (-150.f + 30.f * i) * (float)M_PI / 180.f;
				Vec p = mm2px(Vec(cx + 7.62f * std::sin(a), cy - 7.62f * std::cos(a)));
				bool hollow = centreHollow ? i == 5 : i == 0;
				nvgBeginPath(vg);
				nvgCircle(vg, p.x, p.y, mm2px(0.4f));
				if (hollow) { nvgStrokeColor(vg, gold); nvgStrokeWidth(vg, mm2px(0.15f)); nvgStroke(vg); }
				else { nvgFillColor(vg, gold); nvgFill(vg); }
			}
		};
		ring(15.24f, 18.53f, false);
		ring(15.24f, 49.01f, false);
		ring(15.24f, 64.25f, true);     // the attenuverter: its zero is the middle
		if (!font || font->handle < 0) return;
		nvgFontFaceId(vg, font->handle);
		nvgFillColor(vg, gold);
		nvgTextAlign(vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
		auto text = [&](float x, float y, const char* s, float mm = 3.1f) {
			nvgFontSize(vg, mm2px(mm));
			nvgText(vg, mm2px(x), mm2px(y), s, NULL);
		};
		text(15.24f, 28.0f, "KHZ");
		text(15.24f, 40.0f, "RES");
		text(7.62f, 72.9f, "CV");
		text(22.86f, 72.9f, "IN");
		text(7.62f, 102.3f, "PING");
		text(22.86f, 102.3f, "BAND");
		text(7.62f, 117.5f, "HI");
		text(22.86f, 117.5f, "LOW");
		nvgTextAlign(vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);
		text(28.6f, 125.5f, "PNGBL", 3.8f);
	}
};

struct PngblWidget : ModuleWidget {
	PngblWidget(Pngbl* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/pngbl.svg")));
		PngblMarks* marks = new PngblMarks;
		marks->box.size = box.size;
		addChild(marks);
		const float xl = 7.62f, xr = 22.86f, xm = 15.24f;
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 18.53f)), module, Pngbl::KHZ_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 49.01f)), module, Pngbl::RES_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 64.25f)), module, Pngbl::CV_PARAM));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 79.49f)), module, Pngbl::CV_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 79.49f)), module, Pngbl::IN_INPUT));
		addChild(createWidgetCentered<super::LedBody>(mm2px(Vec(xr, 87.11f))));
		addChild(createLightCentered<super::Led>(mm2px(Vec(xr, 87.11f)), module, Pngbl::BAND_LIGHT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 94.73f)), module, Pngbl::PING_INPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 94.73f)), module, Pngbl::BAND_OUTPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xl, 109.97f)), module, Pngbl::HI_OUTPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 109.97f)), module, Pngbl::LOW_OUTPUT));
	}
};

Model* modelPngbl = createModel<Pngbl, PngblWidget>("PNGBL");
