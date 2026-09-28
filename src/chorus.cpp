#include "plugin.hpp"
#include "parts.hpp"
#include "chorus_tables.hpp"
#include <cmath>
#include <cstdint>

// =============================================================================
// CHORUS — the Super Synthesis modulated delay (REV1), ported from its firmware
// (design/CHORUS/firmware/Core: chorus.c, c_filters.c, lerp.c,
// dynamic_smooth.c and the TIM2/TIM3 handlers in stm32g4xx_it.c).
//
// TIMING. HSI 16 MHz x 12 / 4 = 48 MHz. TIM2 (period 960) runs the audio at
// 49.95 kHz; TIM3 (period 6400) the controls at 7.5 kHz. The filters and the
// smoother are written for 53333 / 53000 Hz, so their corners sit ~6% off
// where their constants say: kept.
//
// AUDIO (chorus_tick). The input is high-passed at 150 Hz and low-passed at
// 8 kHz (Chamberlin SVFs), padded x0.7, and written with the feedback into a
// 15,000-sample int16 delay line. It is read, interpolated, at
//     in - lfo x (1 - delay) - delay x 14999
// so DELAY runs from a few samples to 300 ms (15,000 / 49.95 kHz: the product
// page's "~300 ms"), and the LFO's depth shrinks as the delay grows. The read
// clips at +-2047 and returns as feedback, INVERTED, times FB.
//
// CONTROLS (chorus_control_tick). RATE and AMT go through a squared table to
// an LFO of 0..75 (nominal, 0..37 Hz at this ISR) and a depth of 0..14999
// samples; FB is linear; DELAY plus the CV input (offset by a trimmed "magic"
// 1900 counts) is heavily smoothed. THE TABLE IS SHORT: 1021 of its 1024
// entries are written, so RATE or AMT in the last 0.3% of travel reads zero.
// That is the shipped firmware, and it is kept.
//
// THE ANALOG SIDE IS ASSUMED. There is no CHORUS schematic in the repo. The
// firmware holds DAC2 at mid-scale as a reference, exactly as the 2OPFM does
// for its difference-amplifier output, so the 2OPFM's front end is assumed:
// 409.6 counts/V in, 4.87 mV/count out (x6.04 on 3.3 V / 4096), both
// non-inverting overall. The product page adds what the firmware cannot see:
// BAL "a simple crossfader from dry to wet" (taken as linear), and the CV
// "through its associated attenuverter" before the ADC.
// =============================================================================

struct ChorusFirmware {
	static constexpr double FS = 48e6 / 961.0;      // TIM2
	static constexpr double FS_CTL = 48e6 / 6401.0; // TIM3
	static constexpr float SAMPLE_RATE = 53333.f;    // main.h, for the SVFs
	static constexpr float DYN_RATE = 53000.f;       // dynamic_smooth.h
	static const int LEN = 15000;                    // CHORUS_BUFFER_LENGTH
	static constexpr float MAGIC_CV_OFFSET = 1900.f;

	struct Svf { float low = 0, high = 0, band = 0, d1 = 0, d2 = 0, a = 0, q = 1; };
	static void svfInit(Svf& s, float f, float q) {
		s.a = std::min(6.28f * f / SAMPLE_RATE, 1.f);
		s.q = 1.f / std::max(q, 0.5f);
	}
	static void svfTick(Svf& s, float in) {
		s.low = s.d2 + s.a * s.d1;
		s.high = in - s.low - s.q * s.d1;
		s.band = s.a * s.high + s.d1;
		s.d1 = s.band;
		s.d2 = s.low;
	}
	struct Smooth { float g0 = 0, sense = 0, low1 = 0, low2 = 0; };
	static void smoothInit(Smooth& s, float base, float sens) {
		float gc = std::tan(3.14f * (base / DYN_RATE));
		s.g0 = 2.f * gc / (1.f + gc);
		s.sense = sens * 4.f;
	}
	static float smoothTick(Smooth& s, float in) {
		float l1z = s.low1, l2z = s.low2;
		float g = std::min(s.g0 + s.sense * std::fabs(l2z - l1z), 1.f);
		s.low1 = l1z + g * (in - l1z);
		s.low2 = l2z + g * (s.low1 - l2z);
		return s.low2;
	}

	int16_t buf[LEN] = {};
	uint16_t in = 0;
	float feedback = 0.f, bufferModulation = 0.f;
	float lfoOut = 0.f;
	uint32_t lfoPhase = 0;
	float rate = 0.f, amt = 0.f, fb = 0.f, delay = 0.f, delayFilt = 0.f;
	Svf hp, lp;
	Smooth smooth;
	// what the ADCs read: AMT, DELAY, CV, RATE, FB (12-bit), and the audio (ADC2)
	uint16_t adc[5] = {0, 0, 1900, 0, 0};
	int audioIn = 0;
	float dac = 2048.f;
	double acc = 0.0, ctlAcc = 0.0;

	ChorusFirmware() {
		smoothInit(smooth, 0.05f, 0.5f);
		svfInit(lp, 8000.f, 0.707f);
		svfInit(hp, 150.f, 1.5f);
	}

	static float lerp(const int16_t* b, float pos) {
		if (pos < 0.f) pos += LEN;
		if (pos > LEN - 1) pos -= LEN;
		uint16_t i = (uint16_t)pos;
		float f = pos - i;
		int16_t a = b[i];
		int16_t n = i < LEN - 1 ? b[i + 1] : b[i + 1 - LEN];
		return (n - a) * f + a;
	}

	float tick(float x) {
		svfTick(hp, x);
		svfTick(lp, hp.high);
		float sig = lp.low * 0.7f;
		in++;
		if (in > LEN - 1) in -= LEN;
		buf[in] = (int16_t)(sig + feedback);
		// lfo_tick: a unipolar sine, 0..amplitude
		lfoPhase += (uint32_t)(42949.6710f * rate);
		lfoOut = (((CH_SINE10[lfoPhase >> 22] - 512) * 0.001953125f) * 0.5f + 0.5f) * amt;
		bufferModulation += (lfoOut - bufferModulation) * 0.03f;
		delayFilt = smoothTick(smooth, delay);
		float out = lerp(buf, in - bufferModulation * (1.f - delayFilt) - delayFilt * (LEN - 1));
		out = clamp(out, -2047.f, 2047.f);
		feedback = out * (fb * -1.f);
		return out;
	}

	void control() {
		uint16_t r = adc[3] >> 2, a = adc[0] >> 2, f = adc[4] >> 2, d = adc[1] >> 2;
		rate += (CH_EXPO[r] * 75.f - rate) * 0.03f;
		amt += (CH_EXPO[a] * (LEN - 1) - amt) * 0.03f;
		fb += ((f / 1024.f) - fb) * 0.03f;
		float di = clamp(d + (adc[2] - MAGIC_CV_OFFSET) * 0.75f, 0.f, 1023.f);
		delay += ((di / 1024.f) - delay) * 0.5f;
	}

	void run(float sr) {
		acc += FS / sr;
		while (acc >= 1.0) {
			acc -= 1.0;
			dac = (float)(uint32_t)(tick((float)(audioIn - 2048)) + 2048.f);
			ctlAcc += FS_CTL / FS;
			if (ctlAcc >= 1.0) { ctlAcc -= 1.0; control(); }
		}
	}
};

struct Chorus : Module {
	enum ParamId { CV_PARAM, BAL_PARAM, FB_PARAM, RATE_PARAM, AMT_PARAM, DELAY_PARAM, PARAMS_LEN };
	enum InputId { IN_INPUT, CV_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { OUT_LIGHT, LFO_LIGHT, LIGHTS_LEN };

	ChorusFirmware fw;

	Chorus() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(CV_PARAM, 0.f, 1.f, 0.5f, "CV amount (attenuverter: centre is none)", "%", 0.f, 200.f, -100.f);
		configParam(BAL_PARAM, 0.f, 1.f, 0.5f, "Balance (dry to wet)", "% wet", 0.f, 100.f);
		configParam(FB_PARAM, 0.f, 1.f, 0.f, "Feedback (inverted)", "%", 0.f, 100.f);
		configParam(RATE_PARAM, 0.f, 1.f, 0.3f, "LFO rate");
		configParam(AMT_PARAM, 0.f, 1.f, 0.3f, "LFO amount");
		configParam(DELAY_PARAM, 0.f, 1.f, 0.05f, "Delay", " ms", 0.f, 300.f);
		configInput(IN_INPUT, "Audio");
		configInput(CV_INPUT, "Delay CV (through the attenuverter)");
		configOutput(OUT_OUTPUT, "Audio");
		configLight(OUT_LIGHT, "Output");
		configLight(LFO_LIGHT, "LFO");
	}

	void onReset() override { fw = ChorusFirmware(); }

	void process(const ProcessArgs& args) override {
		auto pot = [](float k) { return (uint16_t)clamp((int)std::lround(k * 4095.f), 0, 4095); };
		fw.adc[0] = pot(params[AMT_PARAM].getValue());
		fw.adc[1] = pot(params[DELAY_PARAM].getValue());
		fw.adc[3] = pot(params[RATE_PARAM].getValue());
		fw.adc[4] = pot(params[FB_PARAM].getValue());
		// the CV through its attenuverter, then the (assumed) input stage
		float att = params[CV_PARAM].getValue() * 2.f - 1.f;
		fw.adc[2] = (uint16_t)clamp((int)std::lround(ChorusFirmware::MAGIC_CV_OFFSET
		              + 409.6f * att * inputs[CV_INPUT].getVoltage()), 0, 4095);
		float dry = inputs[IN_INPUT].getVoltage();
		fw.audioIn = clamp((int)std::lround(2048.f + 409.6f * dry), 0, 4095);
		fw.run(args.sampleRate);
		float wet = (fw.dac - 2047.f) * (3.3f / 4096.f * 6.04f);
		float b = params[BAL_PARAM].getValue();
		float y = dry * (1.f - b) + wet * b;
		outputs[OUT_OUTPUT].setVoltage(y);
		lights[OUT_LIGHT].setBrightnessSmooth(clamp(y / 5.f, 0.f, 1.f), args.sampleTime);
		// TIM1 PWM, period 4096, duty = the LFO's output
		lights[LFO_LIGHT].setBrightness(clamp(fw.lfoOut / 4096.f, 0.f, 1.f));
	}
};

// The gerber panel's holes (tools/panel_chorus.py) plus 0.24 mm in x. The art
// drills the LEDs but draws none, so each gets the kit's LED body.
struct ChorusWidget : ModuleWidget {
	ChorusWidget(Chorus* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/chorus.svg")));
		const float xl = 7.62f, xr = 22.86f, xm = 15.24f;
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 18.53f)), module, Chorus::IN_INPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 18.53f)), module, Chorus::OUT_OUTPUT));
		addChild(createWidgetCentered<super::LedBody>(mm2px(Vec(xr, 26.15f))));
		addChild(createLightCentered<super::Led>(mm2px(Vec(xr, 26.15f)), module, Chorus::OUT_LIGHT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 33.77f)), module, Chorus::CV_INPUT));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 33.77f)), module, Chorus::BAL_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xl, 49.01f)), module, Chorus::CV_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 49.01f)), module, Chorus::FB_PARAM));
		addChild(createWidgetCentered<super::LedBody>(mm2px(Vec(xl, 71.87f))));
		addChild(createLightCentered<super::Led>(mm2px(Vec(xl, 71.87f)), module, Chorus::LFO_LIGHT));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xl, 79.49f)), module, Chorus::RATE_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 79.49f)), module, Chorus::AMT_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 109.97f)), module, Chorus::DELAY_PARAM));
	}
};

Model* modelChorus = createModel<Chorus, ChorusWidget>("CHORUS");
