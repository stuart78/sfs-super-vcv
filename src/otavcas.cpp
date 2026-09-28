#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// OTAVCAs — a pair of LM13700 VCAs, an unreleased Super Synthesis design
// (2022), modelled from its schematic (design/OTAVCAs/schematic.pdf) and BOM.
//
// GAIN. The GAIN pot (100k across 0..5 V) and the CV, through an attenuverter
// (100k into the wiper of a 100k pot whose ends are two virtual grounds, one
// of them inverted: zero at the centre), sum into U2.2, which with Q1 is a
// linear voltage-to-current converter: Iabc = 10 x (the node's current), so
// GAIN fully up is 500 uA and a volt of CV at a full attenuverter adds 100 uA.
// Never negative (D4).
//
// AUDIO. IN through a level trimmer (LVL), then 51k into 1.5k at the OTA's
// inverting input: 0.0286 per volt, so at full LVL a 5 V signal is 143 mV at
// the OTA, nearly three times 2VT -- these saturate readily. U2.4 turns the
// OTA's current back into a voltage through 13k, re-inverting, so the output
// is non-inverting: 13k x Iabc x tanh(0.0286 x LVL x IN / 2VT).
//
// MIX. Channel 1's OUT, while unpatched, is inverted (U3) and summed through
// 13k into channel 2's transimpedance node: the panel's dashed arrow. (The
// board also takes a MIX_IN from a neighbouring card; not modelled.)
// LEDs follow each output, one polarity.
// =============================================================================

struct Otavcas : Module {
	enum ParamId { LVL1_PARAM, CV1_PARAM, GAIN1_PARAM, LVL2_PARAM, CV2_PARAM, GAIN2_PARAM, PARAMS_LEN };
	enum InputId { IN1_INPUT, CV1_INPUT, IN2_INPUT, CV2_INPUT, INPUTS_LEN };
	enum OutputId { OUT1_OUTPUT, OUT2_OUTPUT, OUTPUTS_LEN };
	enum LightId { OUT1_LIGHT, OUT2_LIGHT, LIGHTS_LEN };

	Otavcas() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < 2; c++) {
			std::string n = " " + std::to_string(c + 1);
			configParam(LVL1_PARAM + 3 * c, 0.f, 1.f, 0.5f, "Input level" + n, "%", 0.f, 100.f);
			configParam(CV1_PARAM + 3 * c, 0.f, 1.f, 0.5f, "CV amount" + n + " (attenuverter: centre is none)", "%", 0.f, 200.f, -100.f);
			configParam(GAIN1_PARAM + 3 * c, 0.f, 1.f, 0.f, "Gain" + n, " V", 0.f, 5.f);
			configInput(IN1_INPUT + 2 * c, "Audio" + n);
			configInput(CV1_INPUT + 2 * c, "Gain CV" + n + " (through the attenuverter)");
			configOutput(OUT1_OUTPUT + c, c == 0 ? "Output 1 (unpatched, it is summed into 2)" : "Output 2 (and 1, while OUT 1 is unpatched)");
			configLight(OUT1_LIGHT + c, "Output" + n);
		}
	}

	static float bias(float gainK, float cv, float cvK) {
		float p = clamp(cvK, 0.f, 1.f);
		float rm = p * 100e3f, rq = (1.f - p) * 100e3f;
		float par = (rm + rq) > 0.f ? rm * rq / (rm + rq) : 0.f;
		float icv = cv / (100e3f + par) * (2.f * p - 1.f);
		float i = clamp(gainK, 0.f, 1.f) * 5.f / 100e3f + icv;
		return clamp(10.f * i, 0.f, 2e-3f);
	}

	static float rail(float v) {
		float a = std::fabs(v);
		if (a <= 9.f) return v;
		return std::copysign(9.f + 1.5f * std::tanh((a - 9.f) / 1.5f), v);
	}

	static float channel(float in, float lvl, float iabc) {
		float vin = in * clamp(lvl, 0.f, 1.f) * (1.5f / 52.5f);
		return 13e3f * iabc * std::tanh(vin / (2.f * 0.02585f));
	}

	void process(const ProcessArgs& args) override {
		float y[2];
		for (int c = 0; c < 2; c++) {
			float iabc = bias(params[GAIN1_PARAM + 3 * c].getValue(), inputs[CV1_INPUT + 2 * c].getVoltage(), params[CV1_PARAM + 3 * c].getValue());
			y[c] = channel(inputs[IN1_INPUT + 2 * c].getVoltage(), params[LVL1_PARAM + 3 * c].getValue(), iabc);
		}
		y[0] = rail(y[0]);
		if (!outputs[OUT1_OUTPUT].isConnected()) y[1] += y[0];
		y[1] = rail(y[1]);
		for (int c = 0; c < 2; c++) {
			outputs[OUT1_OUTPUT + c].setVoltage(y[c]);
			lights[OUT1_LIGHT + c].setBrightnessSmooth(clamp(y[c] / 5.f, 0.f, 1.f), args.sampleTime);
		}
	}
};

// The art's positions (tools/panel_otavcas.py) plus 0.18 mm in x; channel 2 is
// channel 1 30.48 mm to the right.
struct OtavcasWidget : ModuleWidget {
	OtavcasWidget(Otavcas* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/otavcas.svg")));
		for (int c = 0; c < 2; c++) {
			float dx = 30.48f * c;
			addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(22.86f + dx, 18.53f)), module, Otavcas::OUT1_OUTPUT + c));
			addChild(createLightCentered<super::Led>(mm2px(Vec(22.86f + dx, 26.15f)), module, Otavcas::OUT1_LIGHT + c));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(7.62f + dx, 33.77f)), module, Otavcas::CV1_INPUT + 2 * c));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(22.86f + dx, 33.77f)), module, Otavcas::IN1_INPUT + 2 * c));
			addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(22.86f + dx, 49.01f)), module, Otavcas::LVL1_PARAM + 3 * c));
			addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(15.24f + dx, 79.49f)), module, Otavcas::CV1_PARAM + 3 * c));
			addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(15.24f + dx, 109.97f)), module, Otavcas::GAIN1_PARAM + 3 * c));
		}
	}
};

Model* modelOtavcas = createModel<Otavcas, OtavcasWidget>("OTAVCAs");
