#include "plugin.hpp"
#include "parts.hpp"
#include "fwrate.hpp"
#include "chorus_tables.hpp"    // ROOM's float_expo_table is the same table, byte for byte
#include <cmath>
#include <cstdint>

// =============================================================================
// ROOM — the Super Synthesis reverb (REV1), ported from its firmware
// (design/ROOM/firmware/Core: sandbox.c, Lib/shared_delays.c, Lib/lfo.c,
// Lib/c_filters.c, and the TIM2/TIM3 handlers in stm32g4xx_it.c).
//
// TIMING. HSI 16 MHz x 14 / 4 = 56 MHz. TIM2 (period 1700) runs the audio at
// 32.92 kHz; TIM3 (period 8000) the controls at 7.0 kHz. The delay maths is
// written for 32 kHz (ms_scale 32 samples/ms), so every delay runs ~3% short;
// the LFOs' PHINC_SCALE is the 53.3 kHz one, so they run at 0.62x their
// nominal 2.2 and 1.1 Hz. Both kept.
//
// THE REVERB (sandbox_tick). Four input allpasses (7.13, 6.451, 32.1, 24.88
// ms), then the feedback, then three loop allpasses (42, 128.4, 164 ms; the
// first and third modulated by the LFOs, by +-0.5% of SIZE), then a Chamberlin
// HP and LP set by the HP and LP knobs; out, and back as feedback x FB,
// INVERTED. Every allpass has gain 0.5 and every delay is its time x SIZE.
// All seven share ONE 15,000-sample int16 buffer: a single index walks
// backwards one sample a tick, and each allpass writes at the running write
// point and hands the next one the point `delay` samples older.
//
// THE TABLE BUG. SIZE, HP and LP all go through the firmware's
// float_expo_table, declared [1024] with 1021 entries written. So in the last
// ~0.3% of travel LP reads 0 (the loop's low-pass closes: silence), HP reads 0
// (no high-pass) and SIZE reads 0 (every delay collapses). That is the shipped
// firmware and it is the default; the context menu can complete the table.
//
// THE ANALOG SIDE IS ASSUMED, as for CHORUS: no ROOM schematic in the repo.
// The firmware holds DAC2 at mid-scale as its output reference; the front end
// is taken as a Eurorack effect's: +-10 V fills the ADC (204.8 counts/V), and
// the output stage undoes it, so the wet path is unity gain. (The first port
// used 2OPFM's 409.6 counts/V and x6.04 out: +-5 V then filled the ADC with no
// headroom and the wet path was 6 dB hot, so a 5 V signal's reverb sat on the
// firmware's +-2047 clamp 58% of the time.) The rate conversion is
// super::FirmwareRate (src/fwrate.hpp). The DRY/WET slot is a horizontal
// slider mixing the input with the DAC's output. The panel's LED follows the
// output.
// =============================================================================

struct RoomFirmware {
	static constexpr double FS = 56e6 / 1701.0;     // TIM2
	static constexpr double FS_CTL = 56e6 / 8001.0; // TIM3
	static constexpr float SAMPLE_RATE = 32000.f;
	static constexpr float PHINC_SCALE = 80531.1401f;
	static const int BUF = 15000;                   // SHARED_BUF_SIZE

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

	// shared_delay_t
	int16_t buf[BUF] = {};
	uint16_t bufsize = BUF;
	int16_t write = 0, index = 0;
	float msScale = SAMPLE_RATE / 1000.f;

	struct Lfo { uint16_t raw = 0; float out = 0.f; uint32_t inc = 0, phase = 0; } lfos[4];
	float hp = 0.f, lp = 0.f, size = 0.f, fbPot = 0.f;
	float feedback = 0.f;
	Svf fbLow, fbHigh;
	bool fixTable = false;

	uint16_t adc[4] = {0, 0, 0, 0};                 // HP, SIZE, LP, FB: ADC1, 10-bit
	double ctlAcc = 0.0;

	RoomFirmware() {
		svfInit(fbLow, 2500.f, 0.8f);
		svfInit(fbHigh, 250.f, 0.8f);
	}

	float expo(uint16_t i) const {
		if (i < 1021 || !fixTable) return CH_EXPO[i];
		return ((i + 1) / 1024.f) * ((i + 1) / 1024.f);   // the entries the file never wrote
	}

	// C's float -> int16 on the M4: saturate to int32, keep the low 16 bits
	static int16_t toInt16(float v) {
		double c = std::max(-2147483648.0, std::min(2147483647.0, (double)v));
		return (int16_t)(uint16_t)(uint32_t)(int32_t)c;
	}

	void delayTick() {
		write = index;
		index--;
		if (index < 0) index += bufsize;
	}

	float allpass(float delay, float in, float fb, float mod) {
		delay *= msScale;
		float frac = delay * mod;
		uint16_t ip = (uint16_t)frac;
		float fp = frac - ip;
		if (frac > bufsize || frac < 0.f) return 0.f;
		uint16_t j = (uint16_t)(write + ip);
		uint16_t k = (uint16_t)(write + delay);
		if (j > bufsize - 1) j -= bufsize;
		if (k > bufsize - 1) k -= bufsize;
		int16_t a = buf[j];
		int16_t n = j < bufsize - 1 ? buf[j + 1] : buf[(j + 1) - bufsize];
		float delayed = a + (n - a) * fp;
		in -= delayed * fb;
		buf[write] = toInt16(in);
		write = (int16_t)k;
		return in * fb + delayed;
	}

	void lfoTick(Lfo& l, float f) {
		l.inc = (uint32_t)(PHINC_SCALE * f);
		l.phase += l.inc;
		l.raw = CH_SINE10[((l.phase >> 22) + 1) & 1023];
		l.out = (l.raw - 512) * 0.001953125f;
	}

	float tick(float input) {
		static const float inputTimes[4] = {7.13f, 6.451f, 32.1f, 24.88f};
		static const float loopTimes[3] = {42.0f, 128.4f, 164.0f};
		lfoTick(lfos[0], 2.2f);
		lfoTick(lfos[1], 1.1f);
		lfoTick(lfos[2], 5.4f);
		lfoTick(lfos[3], 4.1f);
		float lfoAmt = size * 0.005f;
		delayTick();
		float x = input;
		for (int i = 0; i < 4; i++) x = allpass(inputTimes[i], x, 0.5f, size);
		float loop = x + feedback;
		loop = allpass(loopTimes[0], loop, 0.5f, size - lfos[0].out * lfoAmt);
		loop = allpass(loopTimes[1], loop, 0.5f, size);
		loop = allpass(loopTimes[2], loop, 0.5f, size - lfos[1].out * lfoAmt);
		svfTick(fbHigh, loop);
		svfTick(fbLow, fbHigh.high);
		float out = fbLow.low;
		feedback = out * fbPot;
		return clamp(out, -2047.f, 2047.f);
	}

	void control() {
		hp += (expo(adc[0]) - hp) * 0.03f;
		lp += (expo(adc[2]) * 1.05f - lp) * 0.03f;
		size += (expo((uint16_t)(adc[1] * 0.9f) + 102) - size) * 0.001f;
		fbPot += ((adc[3] / -1024.f) - fbPot) * 0.03f;
		if (lp > 0.9999f) lp = 0.9999f;
		fbHigh.a = hp * 0.7f;
		fbLow.a = lp;
	}

	// One TIM2 interrupt: an ADC count in, the DAC count out.
	float interrupt(int adcIn) {
		float dac = (float)(uint32_t)(tick((float)(adcIn - 2047)) + 2047.f);
		ctlAcc += FS_CTL / FS;
		if (ctlAcc >= 1.0) { ctlAcc -= 1.0; control(); }
		return dac;
	}
};

struct Room : Module {
	enum ParamId { FB_PARAM, LP_PARAM, HP_PARAM, SIZE_PARAM, MIX_PARAM, PARAMS_LEN };
	enum InputId { IN_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { OUT_LIGHT, LIGHTS_LEN };

	static constexpr float COUNTS_PER_V = 204.8f;   // assumed: +-10 V full scale
	RoomFirmware fw;
	super::FirmwareRate rate;

	Room() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(FB_PARAM, 0.f, 1.f, 0.5f, "Feedback (inverted)", "%", 0.f, 100.f);
		configParam(LP_PARAM, 0.f, 1.f, 0.8f, "Low-pass in the loop (the very top is the firmware's silence: see the menu)");
		configParam(HP_PARAM, 0.f, 1.f, 0.1f, "High-pass in the loop");
		configParam(SIZE_PARAM, 0.f, 1.f, 0.6f, "Size");
		configParam(MIX_PARAM, 0.f, 1.f, 0.5f, "Dry / wet", "% wet", 0.f, 100.f);
		configInput(IN_INPUT, "Audio");
		configOutput(OUT_OUTPUT, "Audio");
		configLight(OUT_LIGHT, "Output");
	}

	void onReset() override {
		bool fix = fw.fixTable;
		fw = RoomFirmware();
		fw.fixTable = fix;
		rate = super::FirmwareRate();
	}

	void process(const ProcessArgs& args) override {
		auto pot10 = [](float k) { return (uint16_t)clamp((int)std::lround(k * 1023.f), 0, 1023); };
		fw.adc[0] = pot10(params[HP_PARAM].getValue());
		fw.adc[1] = pot10(params[SIZE_PARAM].getValue());
		fw.adc[2] = pot10(params[LP_PARAM].getValue());
		fw.adc[3] = pot10(params[FB_PARAM].getValue());
		float dry = inputs[IN_INPUT].getVoltage();
		rate.setRates(RoomFirmware::FS, args.sampleRate);
		float wet = rate.process(dry, [&](float x) {
			int adc = clamp((int)std::lround(2047.f + COUNTS_PER_V * x), 0, 4095);
			return (fw.interrupt(adc) - 2047.f) / COUNTS_PER_V;
		});
		float m = params[MIX_PARAM].getValue();
		float y = dry * (1.f - m) + wet * m;
		outputs[OUT_OUTPUT].setVoltage(y);
		lights[OUT_LIGHT].setBrightnessSmooth(clamp(y / 5.f, 0.f, 1.f), args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "fixTable", json_boolean(fw.fixTable));
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "fixTable")) fw.fixTable = json_boolean_value(j);
	}
};

// The gerber's holes in the export's frame (tools/panel_room.py) plus 0.14 mm:
// the 30.20 mm art centred in VCV's 30.48.
struct RoomWidget : ModuleWidget {
	RoomWidget(Room* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/room.svg")));
		const float dx = 0.14f;
		const float xl = 7.54f + dx, xr = 22.78f + dx, xm = 15.16f + dx;
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 18.53f)), module, Room::IN_INPUT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 18.53f)), module, Room::OUT_OUTPUT));
		addChild(createWidgetCentered<super::LedBody>(mm2px(Vec(xr, 26.15f))));
		addChild(createLightCentered<super::Led>(mm2px(Vec(xr, 26.15f)), module, Room::OUT_LIGHT));
		addParam(createParamCentered<super::SliderH>(mm2px(Vec(15.10f + dx, 33.79f)), module, Room::MIX_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xl, 48.97f)), module, Room::FB_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 64.21f)), module, Room::LP_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(xr, 79.45f)), module, Room::HP_PARAM));
		addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 109.97f)), module, Room::SIZE_PARAM));
	}

	void appendContextMenu(Menu* menu) override {
		Room* m = dynamic_cast<Room*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("Complete the firmware's expo table", "", &m->fw.fixTable));
		menu->addChild(createMenuLabel("Off, as shipped: LP, HP and SIZE read 0 at the very top of their travel"));
	}
};

Model* modelRoom = createModel<Room, RoomWidget>("ROOM");
