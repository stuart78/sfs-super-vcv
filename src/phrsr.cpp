#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>
#include <cstdint>

// =============================================================================
// PHRSR — the Super Synthesis phrase recorder (REV7), ported from its firmware.
//
// Two 16-step sequences, A and B, on one clock. Hold a REC button and the DC
// knob is written into that sequence at every step; hold STEPS and press a REC
// button to start a loop at the current step, and it grows by one step for
// every step you keep holding. The clock is an internal oscillator (RATE) or
// the CLK input, which also resets the oscillator's phase.
//
// The firmware (design/PHRSR/firmware/Core/Src/main.c) runs in one timer
// interrupt: TIM3, period 45000 at 64 MHz (HSI / 2 x 16; system_stm32f30x.c),
// so 1422.2 Hz. It is transcribed here as it is, integer types and all,
// because several of its behaviours live in those types:
//   - the pot smoothing writes a float back into a uint16, so it truncates:
//     a pot moving DOWN converges, a pot moving UP stalls up to 49 counts short
//   - an external clock edge that lands while the internal clock bit is high
//     steps twice, since resetting the phase makes that bit fall
//   - the clock output is set one interrupt late, on purpose (see the comment
//     the firmware carries, kept below)
//
// The analog side is from the REV7 schematic: each DAC (0..3.3 V) feeds a
// non-inverting x2 stage, so the outputs swing 0..6.6 V. The A and B stages
// have 1n across their 100k feedback, which makes them 1 + 1/(1 + s*100us):
// a step jumps halfway at once and glides the rest at 1.6 kHz (the "slow
// filter" the firmware's comment regrets). The clock stage has no cap. Each
// LED is an op-amp current source off its DAC, so its brightness is linear in
// the output. CLK in is an NPN inverter, tripping near 0.65 V.
//
// JACK NAMING: the firmware writes seq_a (the LEFT button's) to DAC1 CH2,
// which the schematic routes to the net it calls BOUT. The panel's left jack
// is taken to be the left button's sequence, so A = left throughout here.
//
// VCV: the three buttons LATCH, and light while they are on. A mouse cannot hold REC while turning DC, or
// hold STEPS while pressing REC, and those are the instrument. The firmware
// only ever sees held states and their edges, so a latch drives the same code.
// The sequences are kept in the patch (the hardware forgets them at power-off).
// =============================================================================

static const int PH_NUM_STEPS = 16;          // #define NUM_STEPS
static const int PH_RATE_MULTIPLIER = 1;
static const uint32_t PH_RATE_OFFSET = 16000;
static const int PH_CLOCK_SHIFT = 5;
static const double PH_EXPONENT = 1.9;

struct PhrsrFirmware {
	static constexpr double FS = 64e6 / 45001.0;    // TIM3: 1422.2 Hz

	// ── the firmware's globals, with their types ─────────────────────────────
	uint16_t seq_a[PH_NUM_STEPS] = {}, seq_b[PH_NUM_STEPS] = {};
	uint16_t rate_pot_val = 0, DC_pot_val = 0;
	uint32_t seq_phase_accumulator = 0, seq_phase_increment = 0;
	uint16_t seq_index = 0, seq_a_index = 0, seq_b_index = 0;
	uint16_t seq_a_start = 0, seq_b_start = 0;
	uint16_t seq_a_count = 0, seq_b_count = 0;
	uint16_t seq_a_length = PH_NUM_STEPS, seq_b_length = PH_NUM_STEPS;
	uint8_t left_button_state = 1, right_button_state = 1, steps_button_state = 1,
	        clk_in_state = 1, last_clk_in_state = 1, last_steps_button_state = 1,
	        last_left_button_state = 1, last_right_button_state = 1;
	uint8_t steps_a_flag = 0, steps_b_flag = 0;
	uint8_t clk_out = 0, last_clk_out = 0;
	int8_t delayed_clk = 0;

	// ── the hardware around it ───────────────────────────────────────────────
	uint16_t ADC[2] = {0, 0};                       // [0] DC pot (PB0), [1] RATE pot (PB1)
	uint8_t pin_left = 1, pin_right = 1, pin_steps = 1, pin_clk = 1;   // pulled up; 0 = active
	uint16_t dac1_ch1 = 0, dac1_ch2 = 0, dac2_ch1 = 0;
	double acc = 0.0;

	void step_increment() {
		seq_index++;
		seq_a_index++;
		seq_b_index++;
		seq_a_count++;
		seq_b_count++;

		seq_a_index &= PH_NUM_STEPS - 1;
		seq_b_index &= PH_NUM_STEPS - 1;

		if (seq_a_length >= PH_NUM_STEPS) seq_a_length = PH_NUM_STEPS;
		if (seq_b_length >= PH_NUM_STEPS) seq_b_length = PH_NUM_STEPS;

		if (steps_a_flag == 1) {
			seq_a_length++;
			if (seq_a_length >= PH_NUM_STEPS) seq_a_length = PH_NUM_STEPS;
		} else if (seq_a_count >= seq_a_length) {
			if (seq_a_length != PH_NUM_STEPS) {
				seq_a_index = seq_a_start;
				seq_a_count = 0;
			}
		}

		if (steps_b_flag == 1) {
			seq_b_length++;
			if (seq_b_length >= PH_NUM_STEPS) seq_b_length = PH_NUM_STEPS;
		} else if (seq_b_count >= seq_b_length) {
			if (seq_b_length != PH_NUM_STEPS) {
				seq_b_index = seq_b_start;
				seq_b_count = 0;
			}
		}

		if (steps_a_flag == 0 && left_button_state == 0) seq_a[seq_a_index] = DC_pot_val;
		if (steps_b_flag == 0 && right_button_state == 0) seq_b[seq_b_index] = DC_pot_val;

		dac1_ch1 = seq_b[seq_b_index];
		dac1_ch2 = seq_a[seq_a_index];
	}

	// TIM3_IRQHandler
	void tick() {
		left_button_state = pin_left;
		right_button_state = pin_right;
		steps_button_state = pin_steps;
		clk_in_state = pin_clk;

		// uint16 += float: truncated back into the uint16
		rate_pot_val = (uint16_t)(rate_pot_val + ((int)ADC[1] - (int)rate_pot_val) * 0.02f);
		DC_pot_val = (uint16_t)(DC_pot_val + ((int)ADC[0] - (int)DC_pot_val) * 0.02f);

		// "unorthodox clock output here. in order to delay the actual clock
		// output by one sample, we set a high clock to 2, and a low clock to -2.
		// at the end of this handler, we decrement or increment, and compare
		// against +/-1 for the actual dac output of the clock. this is to fix the
		// accidentally slow filter on the main dac outputs, causing them to
		// always be a touch behind the clock output. woopsies!" -- the firmware
		if (clk_in_state == 0) {
			if (last_clk_in_state == 1) {
				seq_phase_accumulator = 0;
				step_increment();
				delayed_clk = 2;
			}
		} else if (last_clk_in_state == 0) {
			delayed_clk = -2;
		}

		if (steps_button_state == 0) {
			if (left_button_state == 0) {
				if (last_left_button_state == 1) {
					steps_a_flag = 1;
					seq_a_length = 1;
					seq_a_start = seq_a_index;
					seq_a_count = 0;
				}
			} else if (last_left_button_state == 0) {
				steps_a_flag = 0;
			}
			if (right_button_state == 0) {
				if (last_right_button_state == 1) {
					steps_b_flag = 1;
					seq_b_length = 1;
					seq_b_start = seq_b_index;
					seq_b_count = 0;
				}
			} else if (last_right_button_state == 0) {
				steps_b_flag = 0;
			}
		} else if (last_steps_button_state == 0) {
			if (left_button_state == 0) steps_a_flag = 0;
			if (right_button_state == 0) steps_b_flag = 0;
		}

		clk_out = (seq_phase_accumulator >> (32 - PH_CLOCK_SHIFT)) & 1;
		seq_phase_increment = (uint32_t)(std::pow((double)rate_pot_val, PH_EXPONENT) + PH_RATE_OFFSET);
		seq_phase_accumulator += seq_phase_increment * PH_RATE_MULTIPLIER;

		if (clk_out == 0) {
			if (last_clk_out == 1) {
				step_increment();
				delayed_clk = 2;
			}
		} else if (last_clk_out == 0) {
			delayed_clk = -2;
		}

		last_clk_in_state = clk_in_state;
		last_clk_out = clk_out;
		last_steps_button_state = steps_button_state;
		last_left_button_state = left_button_state;
		last_right_button_state = right_button_state;

		if (delayed_clk > 0) {
			if (delayed_clk == 1) dac2_ch1 = 4095;
			delayed_clk--;
		}
		if (delayed_clk < 0) {
			if (delayed_clk == -1) dac2_ch1 = 0;
			delayed_clk++;
		}
	}

	// Run the interrupt at its own rate against the host's. The DACs hold
	// between interrupts, as the chip's do.
	void run(float sr) {
		acc += FS / sr;
		while (acc >= 1.0) {
			acc -= 1.0;
			tick();
		}
	}
};

struct Phrsr : Module {
	enum ParamId { DC_PARAM, RATE_PARAM, REC_A_PARAM, REC_B_PARAM, STEPS_PARAM, PARAMS_LEN };
	enum InputId { CLK_INPUT, INPUTS_LEN };
	enum OutputId { A_OUTPUT, B_OUTPUT, CLK_OUTPUT, OUTPUTS_LEN };
	// the button lights were appended: lights, like everything else, serialise by index
	enum LightId { A_LIGHT, B_LIGHT, CLK_LIGHT, REC_A_LIGHT, REC_B_LIGHT, STEPS_LIGHT, LIGHTS_LEN };

	PhrsrFirmware fw;
	float lpA = 0.f, lpB = 0.f;    // the 1n across each output stage's feedback

	Phrsr() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(DC_PARAM, 0.f, 1.f, 0.5f, "DC (the voltage REC writes)", " V", 0.f, 6.6f);
		configParam(RATE_PARAM, 0.f, 1.f, 0.5f, "Rate (internal clock; with CLK patched, the clock's fallback)");
		configSwitch(REC_A_PARAM, 0.f, 1.f, 0.f, "REC A (on: DC is written at every step; with STEPS on: sets A's loop)", {"off", "on"});
		configSwitch(REC_B_PARAM, 0.f, 1.f, 0.f, "REC B (on: DC is written at every step; with STEPS on: sets B's loop)", {"off", "on"});
		configSwitch(STEPS_PARAM, 0.f, 1.f, 0.f, "Steps (on, then REC on for as many steps as the loop should be, then REC off)", {"off", "on"});
		configInput(CLK_INPUT, "Clock (steps on the rising edge, and resets the internal clock)");
		configOutput(A_OUTPUT, "Sequence A (0-6.6 V)");
		configOutput(B_OUTPUT, "Sequence B (0-6.6 V)");
		configOutput(CLK_OUTPUT, "Clock (0 / 6.6 V)");
		configLight(A_LIGHT, "Sequence A");
		configLight(B_LIGHT, "Sequence B");
		configLight(CLK_LIGHT, "Clock");
		configLight(REC_A_LIGHT, "REC A on");
		configLight(REC_B_LIGHT, "REC B on");
		configLight(STEPS_LIGHT, "Steps on");
	}

	void onReset() override { fw = PhrsrFirmware(); lpA = lpB = 0.f; }

	void process(const ProcessArgs& args) override {
		auto adc = [](float k) { return (uint16_t)clamp((int)std::lround(k * 4095.f), 0, 4095); };
		fw.ADC[0] = adc(params[DC_PARAM].getValue());
		fw.ADC[1] = adc(params[RATE_PARAM].getValue());
		fw.pin_left = params[REC_A_PARAM].getValue() > 0.5f ? 0 : 1;
		fw.pin_right = params[REC_B_PARAM].getValue() > 0.5f ? 0 : 1;
		fw.pin_steps = params[STEPS_PARAM].getValue() > 0.5f ? 0 : 1;
		fw.pin_clk = inputs[CLK_INPUT].getVoltage() > 0.65f ? 0 : 1;   // NPN: a high jack pulls the pin low
		fw.run(args.sampleRate);

		// DAC counts to volts at the DAC, then the output stages
		const float k = 3.3f / 4095.f;
		float va = fw.dac1_ch2 * k, vb = fw.dac1_ch1 * k, vc = fw.dac2_ch1 * k;
		float a = 1.f - std::exp(-args.sampleTime / 100e-6f);     // 100k x 1n
		lpA += (va - lpA) * a;
		lpB += (vb - lpB) * a;
		outputs[A_OUTPUT].setVoltage(va + lpA);
		outputs[B_OUTPUT].setVoltage(vb + lpB);
		outputs[CLK_OUTPUT].setVoltage(2.f * vc);
		lights[A_LIGHT].setBrightness(va / 3.3f);
		lights[B_LIGHT].setBrightness(vb / 3.3f);
		lights[CLK_LIGHT].setBrightness(vc / 3.3f);
		lights[REC_A_LIGHT].setBrightness(fw.pin_left == 0 ? 1.f : 0.f);
		lights[REC_B_LIGHT].setBrightness(fw.pin_right == 0 ? 1.f : 0.f);
		lights[STEPS_LIGHT].setBrightness(fw.pin_steps == 0 ? 1.f : 0.f);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_t* a = json_array();
		json_t* b = json_array();
		for (int i = 0; i < PH_NUM_STEPS; i++) {
			json_array_append_new(a, json_integer(fw.seq_a[i]));
			json_array_append_new(b, json_integer(fw.seq_b[i]));
		}
		json_object_set_new(root, "seqA", a);
		json_object_set_new(root, "seqB", b);
		json_object_set_new(root, "lenA", json_integer(fw.seq_a_length));
		json_object_set_new(root, "lenB", json_integer(fw.seq_b_length));
		json_object_set_new(root, "startA", json_integer(fw.seq_a_start));
		json_object_set_new(root, "startB", json_integer(fw.seq_b_start));
		return root;
	}
	void dataFromJson(json_t* root) override {
		json_t* a = json_object_get(root, "seqA");
		json_t* b = json_object_get(root, "seqB");
		for (int i = 0; i < PH_NUM_STEPS; i++) {
			if (a) fw.seq_a[i] = (uint16_t)clamp((int)json_integer_value(json_array_get(a, i)), 0, 4095);
			if (b) fw.seq_b[i] = (uint16_t)clamp((int)json_integer_value(json_array_get(b, i)), 0, 4095);
		}
		auto num = [&](const char* k, uint16_t& v, int lo, int hi) {
			if (json_t* j = json_object_get(root, k)) v = (uint16_t)clamp((int)json_integer_value(j), lo, hi);
		};
		num("lenA", fw.seq_a_length, 1, PH_NUM_STEPS);
		num("lenB", fw.seq_b_length, 1, PH_NUM_STEPS);
		num("startA", fw.seq_a_start, 0, PH_NUM_STEPS - 1);
		num("startB", fw.seq_b_start, 0, PH_NUM_STEPS - 1);
		fw.seq_a_index = fw.seq_a_start;
		fw.seq_b_index = fw.seq_b_start;
	}
};

// Positions are the art's (design/PHRSR/PHRSR_REV7_PANEL.svg, measured by
// tools/panel_phrsr.py) plus 0.24 mm in x: the 30.00 mm panel is centred in
// VCV's 30.48. The art carries its own lettering.
static const float PH_XL = 7.62f, PH_XR = 22.86f, PH_XM = 15.24f;

struct PhrsrWidget : ModuleWidget {
	PhrsrWidget(Phrsr* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/phrsr.svg")));

		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(PH_XL, 18.53f)), module, Phrsr::A_OUTPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(PH_XR, 18.53f)), module, Phrsr::B_OUTPUT));
		addChild(createLightCentered<super::Led>(mm2px(Vec(PH_XL, 26.15f)), module, Phrsr::A_LIGHT));
		addChild(createLightCentered<super::Led>(mm2px(Vec(PH_XR, 26.15f)), module, Phrsr::B_LIGHT));

		addInput(createInputCentered<super::JackIn>(mm2px(Vec(PH_XL, 33.77f)), module, Phrsr::CLK_INPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(PH_XR, 33.77f)), module, Phrsr::CLK_OUTPUT));
		addChild(createLightCentered<super::Led>(mm2px(Vec(PH_XR, 41.39f)), module, Phrsr::CLK_LIGHT));

		// latching and illuminated: lit while on (see the header for why they latch)
		addParam(createLightParamCentered<super::LitLatch>(mm2px(Vec(PH_XL, 49.01f)), module, Phrsr::REC_A_PARAM, Phrsr::REC_A_LIGHT));
		addParam(createLightParamCentered<super::LitLatch>(mm2px(Vec(PH_XR, 49.01f)), module, Phrsr::REC_B_PARAM, Phrsr::REC_B_LIGHT));
		addParam(createLightParamCentered<super::LitLatch>(mm2px(Vec(PH_XR, 64.25f)), module, Phrsr::STEPS_PARAM, Phrsr::STEPS_LIGHT));

		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(PH_XM, 79.48f)), module, Phrsr::DC_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(PH_XM, 109.97f)), module, Phrsr::RATE_PARAM));
	}
};

Model* modelPhrsr = createModel<Phrsr, PhrsrWidget>("PHRSR");
