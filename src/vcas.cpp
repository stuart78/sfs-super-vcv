#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// VCAs — the Super Synthesis quad linear VCA and mixer (REV2), modelled from its
// schematic (design/VCAs/schematic.pdf) and BOM.
//
// PER CHANNEL. IN goes through an attenuverter (the small knob: a 10k trimmer
// from IN to ground, 100k/100k also dividing IN onto its wiper, into U6.3 as
// 2 x wiper - IN), then 30k into a V2164 cell, whose output current a 30k
// transimpedance stage (U6.1) turns back into a voltage: unity at 0 dB.
//
// LINEAR GAIN. The control amp U6.2 integrates the sum of CV/100k, the slider
// (0..5 V)/100k and a 10M to -5 V, against the output current of a second
// 2164 cell fed a fixed -5 V/100k. It settles where that cell's gain balances
// them, and the audio cell shares its control voltage, so
//     G = (slider + CV) / 5 V - 0.01
// linear, not exponential. D7/D8 keep the control voltage above 0, so the gain
// tops out at unity (0 dB), and the 10M makes sure the bottom is fully off.
//
// THE CHAIN (the panel's arrows). Each OUT jack's switch contact carries the
// channel on, while unpatched, through an inverter and 30k into the next
// channel's transimpedance node: an unpatched output is summed into the next,
// 1 -> 2 -> 3 -> 4, so four unpatched channels mix at OUT 4.
//
// The LED follows the channel's output, lighting on one polarity. TL074 outputs
// are soft-limited near +-10.5 V. Not modelled: 2164 noise and distortion.
// =============================================================================

static const int VCA_N = 4;

struct Vcas : Module {
	enum ParamId { ENUMS(LEVEL_PARAM, VCA_N), ENUMS(IN_PARAM, VCA_N), PARAMS_LEN };
	enum InputId { ENUMS(CV_INPUT, VCA_N), ENUMS(IN_INPUT, VCA_N), INPUTS_LEN };
	enum OutputId { ENUMS(OUT_OUTPUT, VCA_N), OUTPUTS_LEN };
	enum LightId { ENUMS(OUT_LIGHT, VCA_N), LIGHTS_LEN };

	Vcas() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int n = 0; n < VCA_N; n++) {
			std::string c = " " + std::to_string(n + 1);
			configParam(LEVEL_PARAM + n, 0.f, 1.f, 0.f, "Level" + c, " V", 0.f, 5.f);
			configParam(IN_PARAM + n, 0.f, 1.f, 1.f, "Input" + c + " (attenuverter)", "%", 0.f, 200.f, -100.f);
			configInput(CV_INPUT + n, "Gain CV" + c + " (linear: +5 V adds unity)");
			configInput(IN_INPUT + n, "Input" + c);
			configOutput(OUT_OUTPUT + n, "Output" + c + (n < VCA_N - 1 ? " (unpatched, it is summed into the next)" : " (and every unpatched channel before it)"));
			configLight(OUT_LIGHT + n, "Output" + c);
		}
	}

	// U6.3: 2 x wiper - IN, the 10k trimmer's wiper loaded by 100k/100k
	static float attenuvert(float x, float k) {
		float p = clamp(k, 0.f, 1.f);
		float rp = p * (1.f - p) * 10e3f, rd = 50e3f;
		float m = rp < 1.f ? p * x : (p * x / rp + 0.5f * x / rd) / (1.f / rp + 1.f / rd);
		return 2.f * m - x;
	}

	static float gain(float slider, float cv) {
		return clamp((slider * 5.f + cv) / 5.f - 0.01f, 0.f, 1.f);
	}

	static float rail(float v) {
		float a = std::fabs(v);
		if (a <= 9.f) return v;
		return std::copysign(9.f + 1.5f * std::tanh((a - 9.f) / 1.5f), v);
	}

	void process(const ProcessArgs& args) override {
		float carry = 0.f;
		for (int n = 0; n < VCA_N; n++) {
			float a = attenuvert(inputs[IN_INPUT + n].getVoltage(), params[IN_PARAM + n].getValue());
			float g = gain(params[LEVEL_PARAM + n].getValue(), inputs[CV_INPUT + n].getVoltage());
			float y = rail(g * a + carry);
			outputs[OUT_OUTPUT + n].setVoltage(y);
			// an unpatched output's switch carries it into the next channel
			carry = outputs[OUT_OUTPUT + n].isConnected() ? 0.f : y;
			lights[OUT_LIGHT + n].setBrightnessSmooth(clamp(y / 5.f, 0.f, 1.f), args.sampleTime);
		}
	}
};

// The art's columns (tools/panel_vcas.py) plus 0.18 mm in x.
struct VcasWidget : ModuleWidget {
	VcasWidget(Vcas* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/vcas.svg")));
		for (int n = 0; n < VCA_N; n++) {
			float x = 7.62f + 15.24f * n;
			addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(x, 18.53f)), module, Vcas::OUT_OUTPUT + n));
			addChild(createLightCentered<super::LedAmber>(mm2px(Vec(x, 26.15f)), module, Vcas::OUT_LIGHT + n));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(x, 33.77f)), module, Vcas::CV_INPUT + n));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(x, 49.01f)), module, Vcas::IN_INPUT + n));
			addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(x, 64.25f)), module, Vcas::IN_PARAM + n));
			addParam(createParamCentered<super::Slider>(mm2px(Vec(x, 92.34f)), module, Vcas::LEVEL_PARAM + n));
		}
	}
};

Model* modelVcas = createModel<Vcas, VcasWidget>("VCAs");
