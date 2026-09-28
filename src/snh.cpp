#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// S&H — three chained sample-and-holds, an unreleased Super Synthesis design
// (2022), modelled from its schematic (design/SNH/schematic.pdf) and BOM.
//
// ONE CHANNEL. IN is buffered (U7.4) into a J112 JFET switch and a 470n
// polyester cap, buffered again (U7.1) to OUT. The trigger is compared against
// a 0.58 V reference (12 V x 5.1k / 105.1k), the comparator's swing
// differentiated (10n into 10k/10k: half the step, decaying over 0.2 ms), and
// that compared again: the JFET conducts for ~0.58 ms after each RISING edge,
// long enough (Ron ~50 ohm x 470n = 24 us) to acquire fully.
//
// THE CHAIN. Channel N's IN is normalled from channel N-1's OUT, and its
// trigger from channel N-1's differentiated edge, INVERTED (U4): so with one
// trigger patched, the first channel samples on its rising edge, the second
// on its FALLING edge, the third ~0.58 ms after that. The panel draws the chain
// upwards: the first channel is the bottom one.
//
// The BOM has no LEDs although the panel draws three; here they follow each
// output. Not modelled: hold droop (a J112 and a polyester cap barely leak).
// =============================================================================

static const int SH_N = 3;

struct Snh : Module {
	enum ParamId { PARAMS_LEN };
	enum InputId { ENUMS(IN_INPUT, SH_N), ENUMS(TRIG_INPUT, SH_N), INPUTS_LEN };
	enum OutputId { ENUMS(OUT_OUTPUT, SH_N), OUTPUTS_LEN };
	enum LightId { ENUMS(OUT_LIGHT, SH_N), LIGHTS_LEN };

	static constexpr float REF = 12.f * 5.1f / 105.1f;
	static constexpr float SWING = 10.5f;             // TL074 into +-12 V

	struct Ch {
		float comp = -SWING;   // U7.3's output
		float node = 0.f;      // the differentiated edge (T_OUT)
		float hold = 0.f;      // C2
	} ch[SH_N];

	Snh() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		const char* ord[SH_N] = {"1 (bottom)", "2 (middle)", "3 (top)"};
		for (int n = 0; n < SH_N; n++) {
			configInput(IN_INPUT + n, std::string("Input ") + ord[n] + (n ? ": normalled from the output below" : ""));
			configInput(TRIG_INPUT + n, std::string("Trigger ") + ord[n] + (n ? ": normalled to the falling edge of the trigger below" : ": rising edge samples"));
			configOutput(OUT_OUTPUT + n, std::string("Output ") + ord[n]);
			configLight(OUT_LIGHT + n, std::string("Output ") + ord[n]);
		}
	}

	void process(const ProcessArgs& args) override {
		const float dt = args.sampleTime;
		const float kEdge = std::exp(-dt / 0.2e-3f);      // 10n x (10k + 10k)
		const float kAcq = 1.f - std::exp(-dt / 24e-6f);  // Ron x 470n
		float prevOut = 0.f, prevNode = 0.f;
		for (int n = 0; n < SH_N; n++) {
			Ch& c = ch[n];
			float in = inputs[IN_INPUT + n].isConnected() ? inputs[IN_INPUT + n].getVoltage() : (n ? prevOut : 0.f);
			float trig = inputs[TRIG_INPUT + n].isConnected() ? inputs[TRIG_INPUT + n].getVoltage() : (n ? -prevNode : 0.f);
			float comp = trig > REF ? SWING : -SWING;
			c.node = kEdge * (c.node + 0.5f * (comp - c.comp));
			c.comp = comp;
			if (c.node > REF)                              // U7.2 opens the JFET
				c.hold += (clamp(in, -SWING, SWING) - c.hold) * kAcq;
			outputs[OUT_OUTPUT + n].setVoltage(c.hold);
			lights[OUT_LIGHT + n].setBrightnessSmooth(clamp(c.hold / 5.f, 0.f, 1.f), args.sampleTime);
			prevOut = c.hold;
			prevNode = c.node;
		}
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* h = json_array();
		for (int n = 0; n < SH_N; n++) json_array_append_new(h, json_real(ch[n].hold));
		json_object_set_new(root, "held", h);
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* h = json_object_get(root, "held"))
			for (int n = 0; n < SH_N; n++) ch[n].hold = (float)json_number_value(json_array_get(h, n));
	}
};

// The art's positions (tools/panel_snh.py) plus 0.24 mm in x.
struct SnhWidget : ModuleWidget {
	SnhWidget(Snh* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/snh.svg")));
		const float xl = 7.62f, xr = 22.86f;
		struct P { Vec in, trig, out, led; };
		const P pos[SH_N] = {
			{Vec(xl, 109.97f), Vec(xr, 94.73f), Vec(xl, 79.49f), Vec(xl, 87.11f)},
			{Vec(xr, 79.49f), Vec(xl, 64.25f), Vec(xr, 49.01f), Vec(xr, 56.63f)},
			{Vec(xl, 49.01f), Vec(xr, 33.77f), Vec(xl, 18.53f), Vec(xl, 26.15f)},
		};
		for (int n = 0; n < SH_N; n++) {
			addInput(createInputCentered<super::JackIn>(mm2px(pos[n].in), module, Snh::IN_INPUT + n));
			addInput(createInputCentered<super::JackIn>(mm2px(pos[n].trig), module, Snh::TRIG_INPUT + n));
			addOutput(createOutputCentered<super::JackOut>(mm2px(pos[n].out), module, Snh::OUT_OUTPUT + n));
			addChild(createLightCentered<super::Led>(mm2px(pos[n].led), module, Snh::OUT_LIGHT + n));
		}
	}
};

Model* modelSnh = createModel<Snh, SnhWidget>("SH");
