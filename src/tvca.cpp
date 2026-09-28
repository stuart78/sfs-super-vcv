#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// TVCA — the Super Synthesis two-input tanh VCA (REV3), modelled from its
// schematic (design/TVCA/schematic.pdf) and BOM.
//
// MIX. Each input has a level trimmer (10k, jack to ground), summed by U2.1
// through 100k against 100k.
//
// DIST. The mix feeds the LM13700's + input through a divider: 100k, then the
// DIST slider (10k) with its wiper to the OTA, then 1k to ground. So the OTA
// sees f x mix, f = (1k + d x 10k) / 111k: 0.009 with DIST down to 0.099 up.
// Even down, 5 V arrives as 45 mV, 0.87 of 2VT: clean at 1 V, a hot signal
// compressed ~2 dB. Up, 0.5 V: deep into the OTA's tanh.
//
// GAIN. INIT (a slider across the CJ431's 5 V) and CV, each through 100k, drive
// U6.1 with Q1 as a linear voltage-to-current converter into 10k:
//     Iabc = (INIT + CV) / 10k,   never negative (D4)
// and the OTA's output current, Iabc x tanh(f x mix / 2VT), becomes a voltage
// in U2.2's 10k transimpedance stage. With DIST down and INIT up that is about
// unity gain; with DIST up, a saturated tanh at a level set by INIT and CV.
//
// The LED follows the output, one polarity. TL072 outputs soft-limit near
// +-10.5 V. Not modelled: the OTA's output resistance and offsets.
// =============================================================================

struct Tvca : Module {
	enum ParamId { INA_PARAM, INB_PARAM, DIST_PARAM, INIT_PARAM, PARAMS_LEN };
	enum InputId { INA_INPUT, INB_INPUT, CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { OUT_LIGHT, LIGHTS_LEN };

	Tvca() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(INA_PARAM, 0.f, 1.f, 1.f, "Input A level", "%", 0.f, 100.f);
		configParam(INB_PARAM, 0.f, 1.f, 1.f, "Input B level", "%", 0.f, 100.f);
		configParam(DIST_PARAM, 0.f, 1.f, 0.f, "Distortion (drive into the OTA's tanh)", "%", 0.f, 100.f);
		configParam(INIT_PARAM, 0.f, 1.f, 0.f, "Initial gain", " V", 0.f, 5.f);
		configInput(INA_INPUT, "Input A");
		configInput(INB_INPUT, "Input B");
		configInput(CV_INPUT, "Gain CV (linear: adds to INIT)");
		configOutput(OUT_OUTPUT, "Output");
		configLight(OUT_LIGHT, "Output");
	}

	static float rail(float v) {
		float a = std::fabs(v);
		if (a <= 9.f) return v;
		return std::copysign(9.f + 1.5f * std::tanh((a - 9.f) / 1.5f), v);
	}

	static float transfer(float mix, float dist, float init, float cv) {
		float f = (1e3f + clamp(dist, 0.f, 1.f) * 10e3f) / 111e3f;
		float iabc = std::max(0.f, (clamp(init, 0.f, 1.f) * 5.f + cv) / 10e3f);
		iabc = std::min(iabc, 2e-3f);                      // the LM13700's own limit
		return rail(10e3f * iabc * std::tanh(f * mix / (2.f * 0.02585f)));
	}

	void process(const ProcessArgs& args) override {
		float mix = inputs[INA_INPUT].getVoltage() * params[INA_PARAM].getValue()
		          + inputs[INB_INPUT].getVoltage() * params[INB_PARAM].getValue();
		float y = transfer(mix, params[DIST_PARAM].getValue(), params[INIT_PARAM].getValue(),
		                   inputs[CV_INPUT].getVoltage());
		outputs[OUT_OUTPUT].setVoltage(y);
		lights[OUT_LIGHT].setBrightnessSmooth(clamp(y / 5.f, 0.f, 1.f), args.sampleTime);
	}
};

// The art's positions (tools/panel_tvca.py) plus 0.24 mm in x.
struct TvcaWidget : ModuleWidget {
	TvcaWidget(Tvca* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/tvca.svg")));
		const float xl = 7.62f, xr = 22.86f;
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 18.53f)), module, Tvca::OUT_OUTPUT));
		addChild(createLightCentered<super::Led>(mm2px(Vec(xr, 26.15f)), module, Tvca::OUT_LIGHT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 33.77f)), module, Tvca::CV_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 49.01f)), module, Tvca::INA_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 49.01f)), module, Tvca::INB_INPUT));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xl, 64.25f)), module, Tvca::INA_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 64.25f)), module, Tvca::INB_PARAM));
		addParam(createParamCentered<super::Slider>(mm2px(Vec(xl, 92.34f)), module, Tvca::DIST_PARAM));
		addParam(createParamCentered<super::Slider>(mm2px(Vec(xr, 92.34f)), module, Tvca::INIT_PARAM));
	}
};

Model* modelTvca = createModel<Tvca, TvcaWidget>("TVCA");
