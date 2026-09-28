#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// SCANNER — the Super Synthesis 4-way crossfader, CV half (REV2), modelled from
// its schematic (design/SCANNER/schematic.pdf) and BOM.
//
// THE SCAN VOLTAGE. U1.1 sums the SCAN knob (100k across 0..5 V, LM4040), the
// direct CV jack and one share of the attenuverted CV jack, each through 86.6k
// against 100k; U1.2 inverts that and adds the attenuverter's other share, so
//     scan = 1.155 x (knob + CV + attenuverter x CV')
// where the attenuverter is the SVFs trick: 86.6k into a 100k pot's wiper,
// its two ends on the two amps' virtual grounds, zero at the centre.
//
// THE WINDOWS. A 5 V ladder (5 x 100k) gives the channels offsets of 1, 2, 3
// and 4 V. Each channel:
//   - U2.1 is 2 x V+ - scan, with V+ = scan clamped by D7 to one diode drop
//     above the channel's offset: a tent, rising with the scan until the
//     clamp, then falling;
//   - U2.3 takes 5 x (tent - the previous channel's offset) (20k/100k,
//     20k/100k): each window opens where the channel below sits;
//   - D6 passes only its positive part to the jack.
// So each output is a triangle ~7 V high and ~3.2 V of scan wide, peaking a
// diode drop above its offset, overlapping its neighbours. The LED follows it.
// =============================================================================

static const int SC_N = 4;

struct Scanner : Module {
	enum ParamId { ATTEN_PARAM, SCAN_PARAM, PARAMS_LEN };
	enum InputId { CVA_INPUT, CVB_INPUT, INPUTS_LEN };
	enum OutputId { ENUMS(OUT_OUTPUT, SC_N), OUTPUTS_LEN };
	enum LightId { ENUMS(OUT_LIGHT, SC_N), LIGHTS_LEN };

	Scanner() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(ATTEN_PARAM, 0.f, 1.f, 0.5f, "CV amount (attenuverter: centre is none)", "%", 0.f, 200.f, -100.f);
		configParam(SCAN_PARAM, 0.f, 1.f, 0.f, "Scan", " V", 0.f, 5.f * 100.f / 86.6f);
		configInput(CVA_INPUT, "Scan CV (through the attenuverter)");
		configInput(CVB_INPUT, "Scan CV");
		for (int n = 0; n < SC_N; n++) {
			configOutput(OUT_OUTPUT + n, "Window " + std::to_string(n + 1) + " (peaks as the scan passes " + std::to_string(n + 1) + " V)");
			configLight(OUT_LIGHT + n, "Window " + std::to_string(n + 1));
		}
	}

	// A junction's knee: max(0, x), rounded over +-w and exactly flat outside
	// it (a smooth max that only approaches zero leaks, as SVFs found).
	static float knee(float x, float w) {
		if (x <= -w) return 0.f;
		if (x >= w) return x;
		return (x + w) * (x + w) / (4.f * w);
	}

	static float scanVoltage(float knob, float cvDirect, float cvAtten, float attenK) {
		float p = clamp(attenK, 0.f, 1.f);
		float rm = p * 100e3f, rq = (1.f - p) * 100e3f;
		float par = (rm + rq) > 0.f ? rm * rq / (rm + rq) : 0.f;
		float i = cvAtten / (86.6e3f + par);
		// the share into U1.1's node is inverted twice (positive), U1.2's once
		float att = i * (p - (1.f - p)) * 100e3f;
		return (knob * 5.f + cvDirect) * (100.f / 86.6f) + att;
	}

	static float window(int n, float scan) {
		const float vd = 0.5f;                   // 1N4148 at ~10-80 uA
		float offset = n + 1.f, last = (float)n;
		float vp = scan - knee(scan - (offset + vd), 0.05f);     // D7 clamps V+
		float tent = 2.f * vp - scan;
		float u23 = clamp(5.f * (tent - last), -10.5f, 10.5f);
		// D6 into 100k: forward only, a drop, a soft knee
		return knee(u23 - vd, 0.05f);
	}

	void process(const ProcessArgs& args) override {
		float scan = scanVoltage(params[SCAN_PARAM].getValue(), inputs[CVB_INPUT].getVoltage(),
		                         inputs[CVA_INPUT].getVoltage(), params[ATTEN_PARAM].getValue());
		for (int n = 0; n < SC_N; n++) {
			float v = window(n, scan);
			outputs[OUT_OUTPUT + n].setVoltage(v);
			lights[OUT_LIGHT + n].setBrightnessSmooth(clamp(v / 7.f, 0.f, 1.f), args.sampleTime);
		}
	}
};

// Outputs in reading order: 1 top left, 2 top right, 3 and 4 below. The art's
// positions (tools/panel_scanner.py) plus 0.24 mm in x.
struct ScannerWidget : ModuleWidget {
	ScannerWidget(Scanner* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/scanner.svg")));
		const float xl = 7.62f, xr = 22.86f, xm = 15.24f;
		const Vec jack[SC_N] = {Vec(xl, 18.53f), Vec(xr, 18.53f), Vec(xl, 33.77f), Vec(xr, 33.77f)};
		for (int n = 0; n < SC_N; n++) {
			addOutput(createOutputCentered<super::JackOut>(mm2px(jack[n]), module, Scanner::OUT_OUTPUT + n));
			addChild(createLightCentered<super::Led>(mm2px(jack[n].plus(Vec(0.f, 7.62f))), module, Scanner::OUT_LIGHT + n));
		}
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 49.01f)), module, Scanner::CVA_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 49.01f)), module, Scanner::CVB_INPUT));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 79.49f)), module, Scanner::ATTEN_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 109.96f)), module, Scanner::SCAN_PARAM));
	}
};

Model* modelScanner = createModel<Scanner, ScannerWidget>("SCANNER");
