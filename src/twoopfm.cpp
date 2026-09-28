#include "plugin.hpp"
#include "panel-style.hpp"   // fonts only: the art carries its own labels
#include "fastmath.hpp"
#include "membrane.hpp"
#include "parts.hpp"
#include <cmath>
#include <cstdint>

// =============================================================================
// 2OPFM — the Super Synthesis 2OPFM, and a firmware bench for it.
//
// Moved here from Signal Function Set (where it was the hidden module "2OP",
// slug TwoOp, never released) on 2026-09-26. The firmware port that runs on the
// real module lives in ~/code/2opfm-alt; its App/Src/alt.c is this file's DSP in
// plain C, checked sample for sample against tools/2opfm-harness.cpp. Change
// one, change both.
//
// The 2OPFM is a 2-operator FM voice on an STM32G431 (Chris McDowell, CC0,
// github.com/supersynthesis/eurorack). Its panel is small and fixed: a kHz pot,
// a RATIO pot, FM and DECAY sliders, CV jacks for kHz / FM / DECAY, a digital
// TRIG input, one 12-bit DAC out, and a V/OCT jumper. This module is that panel
// and nothing else, so an alternative firmware can be designed and heard here
// before it goes anywhere near the chip. The context menu picks the firmware.
//
// Every mode is written the way it would be written for the G431: float DSP,
// fixed small tables, no allocation, controls resolved at a control rate (every
// 16 samples, about the stock firmware's 5 kHz TIM3), and nothing that needs
// more than a few KB of RAM.
//
//   STOCK      the shipped firmware, emulated: its own 39.98 kHz ISR, the
//              integer sine table with no interpolation, the integer phase
//              offset for FM, the ADC scalings, and a zero-order-hold DAC.
//   2OPFM+     the same instrument done carefully: calibrated V/OCT, fractional
//              phase modulation, RATIO sticky on the integers, and modulator
//              self-feedback rising in the top quarter of the FM slider.
//   DRUM       a modal membrane (Kit's Bessel modes) morphing to a free bar.
//   CZ         Casio-style phase distortion; FM is the DCW.
//   FORMANT    FOF formant voice; RATIO is the vowel.
//   BELL       Carillon's 17-partial bell morphing to a bar.
//
// What stays the same across modes, because it is the hardware: TRIG starts
// the envelope, DECAY at the very top of its slider holds the voice open (the
// stock firmware's drone), and the modulator/brightness gets env^2 so the tone
// darkens before it fades.
// =============================================================================

enum TwoOpMode { MODE_STOCK, MODE_PLUS, MODE_DRUM, MODE_CZ, MODE_FORMANT, MODE_BELL, TWOOP_MODES };
static const char* const TWOOP_MODE_NAMES[TWOOP_MODES] = {
	"Stock 2OPFM (emulated)", "2OPFM+", "Modal drum", "Phase distortion (CZ)", "Formant", "Bell"};
static const char* const TWOOP_MODE_SHORT[TWOOP_MODES] = {"STOCK", "2OPFM+", "DRUM", "CZ", "FORMANT", "BELL"};
// What each control means in each mode, for the tooltips: kHz, RATIO, FM, DECAY.
static const char* const TWOOP_PARAM_NAMES[TWOOP_MODES][4] = {
	{"kHz", "Ratio", "FM", "Decay (top: drone)"},
	{"Pitch", "Ratio (sticky on integers)", "FM index (top quarter adds feedback)", "Decay (top: drone)"},
	{"Pitch", "Material (membrane to bar)", "Strike (centre to edge, soft to hard)", "Decay"},
	{"Pitch", "Waveform (saw, square, pulse, reso saw/tri/trap)", "DCW", "Decay (top: drone)"},
	{"Pitch", "Vowel (a e i o u)", "Formant shift (x0.5 to x2)", "Decay (top: drone)"},
	{"Pitch", "Shape (bell to bar)", "Brightness", "Ring"},
};

// ── the stock firmware, emulated ────────────────────────────────────────────
// Transcribed from App/Src/2op_main.c, dynamic_smooth.c and the TIM2/TIM3
// handlers in stm32g4xx_it.c. The clocks: HSI 16 MHz x8 /2 = 64 MHz; TIM2
// period 1600 is the audio ISR, TIM3 period 12800 runs the envelope.
static const uint16_t STOCK_EXPO[1024] = {
	65535, 64771, 64017, 63271, 62534, 61805, 61085, 60374, 59670, 58975, 58288, 57609, 56938, 56274, 55619, 54971,
	54330, 53697, 53072, 52454, 51842, 51238, 50642, 50052, 49468, 48892, 48322, 47760, 47203, 46653, 46110, 45572,
	45042, 44517, 43998, 43486, 42979, 42478, 41983, 41494, 41011, 40533, 40061, 39594, 39133, 38677, 38226, 37781,
	37341, 36906, 36476, 36051, 35631, 35216, 34805, 34400, 33999, 33603, 33212, 32825, 32442, 32064, 31691, 31321,
	30957, 30596, 30239, 29887, 29539, 29195, 28855, 28519, 28186, 27858, 27533, 27213, 26896, 26582, 26272, 25966,
	25664, 25365, 25069, 24777, 24489, 24203, 23921, 23643, 23367, 23095, 22826, 22560, 22297, 22037, 21781, 21527,
	21276, 21028, 20783, 20541, 20302, 20065, 19832, 19600, 19372, 19146, 18923, 18703, 18485, 18270, 18057, 17846,
	17639, 17433, 17230, 17029, 16831, 16635, 16441, 16249, 16060, 15873, 15688, 15505, 15325, 15146, 14970, 14795,
	14623, 14452, 14284, 14118, 13953, 13791, 13630, 13471, 13314, 13159, 13006, 12854, 12705, 12557, 12410, 12266,
	12123, 11982, 11842, 11704, 11568, 11433, 11300, 11168, 11038, 10909, 10782, 10657, 10532, 10410, 10288, 10169,
	10050, 9933, 9817, 9703, 9590, 9478, 9368, 9259, 9151, 9044, 8939, 8835, 8732, 8630, 8529, 8430,
	8332, 8235, 8139, 8044, 7950, 7858, 7766, 7676, 7586, 7498, 7410, 7324, 7239, 7154, 7071, 6989,
	6907, 6827, 6747, 6669, 6591, 6514, 6438, 6363, 6289, 6216, 6144, 6072, 6001, 5931, 5862, 5794,
	5726, 5660, 5594, 5529, 5464, 5400, 5338, 5275, 5214, 5153, 5093, 5034, 4975, 4917, 4860, 4803,
	4747, 4692, 4637, 4583, 4530, 4477, 4425, 4373, 4323, 4272, 4222, 4173, 4125, 4077, 4029, 3982,
	3936, 3890, 3845, 3800, 3755, 3712, 3668, 3626, 3583, 3542, 3500, 3460, 3419, 3380, 3340, 3301,
	3263, 3225, 3187, 3150, 3113, 3077, 3041, 3006, 2971, 2936, 2902, 2868, 2835, 2802, 2769, 2737,
	2705, 2673, 2642, 2612, 2581, 2551, 2521, 2492, 2463, 2434, 2406, 2378, 2350, 2323, 2296, 2269,
	2242, 2216, 2191, 2165, 2140, 2115, 2090, 2066, 2042, 2018, 1995, 1971, 1948, 1926, 1903, 1881,
	1859, 1837, 1816, 1795, 1774, 1753, 1733, 1713, 1693, 1673, 1654, 1634, 1615, 1596, 1578, 1559,
	1541, 1523, 1506, 1488, 1471, 1454, 1437, 1420, 1403, 1387, 1371, 1355, 1339, 1323, 1308, 1293,
	1278, 1263, 1248, 1234, 1219, 1205, 1191, 1177, 1163, 1150, 1136, 1123, 1110, 1097, 1084, 1072,
	1059, 1047, 1035, 1023, 1011, 999, 987, 976, 964, 953, 942, 931, 920, 910, 899, 889,
	878, 868, 858, 848, 838, 828, 819, 809, 800, 790, 781, 772, 763, 754, 745, 737,
	728, 720, 711, 703, 695, 687, 679, 671, 663, 655, 648, 640, 633, 625, 618, 611,
	604, 597, 590, 583, 576, 569, 563, 556, 550, 543, 537, 531, 524, 518, 512, 506,
	500, 495, 489, 483, 477, 472, 466, 461, 456, 450, 445, 440, 435, 430, 425, 420,
	415, 410, 405, 400, 396, 391, 387, 382, 378, 373, 369, 365, 360, 356, 352, 348,
	344, 340, 336, 332, 328, 324, 321, 317, 313, 309, 306, 302, 299, 295, 292, 288,
	285, 282, 278, 275, 272, 269, 266, 263, 260, 257, 254, 251, 248, 245, 242, 239,
	236, 234, 231, 228, 226, 223, 220, 218, 215, 213, 210, 208, 205, 203, 201, 198,
	196, 194, 191, 189, 187, 185, 183, 181, 178, 176, 174, 172, 170, 168, 166, 164,
	162, 161, 159, 157, 155, 153, 151, 150, 148, 146, 144, 143, 141, 139, 138, 136,
	135, 133, 132, 130, 129, 127, 126, 124, 123, 121, 120, 118, 117, 116, 114, 113,
	112, 110, 109, 108, 107, 105, 104, 103, 102, 100, 99, 98, 97, 96, 95, 94,
	93, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 79, 78,
	77, 76, 75, 74, 73, 72, 72, 71, 70, 69, 68, 67, 67, 66, 65, 64,
	64, 63, 62, 61, 61, 60, 59, 59, 58, 57, 57, 56, 55, 55, 54, 53,
	53, 52, 52, 51, 50, 50, 49, 49, 48, 47, 47, 46, 46, 45, 45, 44,
	44, 43, 43, 42, 42, 41, 41, 40, 40, 39, 39, 38, 38, 38, 37, 37,
	36, 36, 35, 35, 35, 34, 34, 33, 33, 33, 32, 32, 31, 31, 31, 30,
	30, 30, 29, 29, 29, 28, 28, 28, 27, 27, 27, 26, 26, 26, 26, 25,
	25, 25, 24, 24, 24, 23, 23, 23, 23, 22, 22, 22, 22, 21, 21, 21,
	21, 20, 20, 20, 20, 19, 19, 19, 19, 19, 18, 18, 18, 18, 18, 17,
	17, 17, 17, 17, 16, 16, 16, 16, 16, 15, 15, 15, 15, 15, 15, 14,
	14, 14, 14, 14, 14, 13, 13, 13, 13, 13, 13, 12, 12, 12, 12, 12,
	12, 12, 11, 11, 11, 11, 11, 11, 11, 11, 10, 10, 10, 10, 10, 10,
	10, 10, 10, 9, 9, 9, 9, 9, 9, 9, 9, 9, 8, 8, 8, 8,
	8, 8, 8, 8, 8, 8, 8, 7, 7, 7, 7, 7, 7, 7, 7, 7,
	7, 7, 7, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6,
	6, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
	5, 5, 5, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,
	4, 4, 4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3,
	3, 3, 3, 3, 3, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// big_sine_wave.h regenerates exactly as floor(32768 + 32767.5 sin): checked
// against all 1024 entries of the shipped header.
struct StockSine {
	uint16_t t[1024];
	StockSine() {
		for (int i = 0; i < 1024; i++)
			t[i] = (uint16_t)std::floor(32768.0 + 32767.5 * std::sin(2.0 * M_PI * i / 1024.0));
	}
};
static const StockSine& stockSine() { static StockSine s; return s; }

struct StockFirmware {
	static constexpr double CLK = 64e6;
	static constexpr double FS = CLK / 1601.0;        // TIM2: the audio ISR, 39.975 kHz
	static constexpr double FS_CTL = CLK / 12801.0;   // TIM3: envelope + parameters, 5 kHz
	// ADC indices, from main.h
	enum { RATIO_POT, FM_SLIDER, DECAY_SLIDER, KHZ_POT, DECAY_JACK };
	enum { KHZ_JACK, FM_JACK };
	enum Stage { ATTACK, DECAY, RELEASE, OFF, RETRIG };
	static constexpr float VOCT_SCALE = 0.147299349f, WIDE_SCALE = 0.25f;

	// soft_adsr, trimmed to the fields run_linear_a_expo_r reads
	struct Env {
		float output = 1.f, rate = 0.f, rate_offset = 0.f, attack_offset = 0.f,
		      release_offset = 0.f, cutoff = 0.f, target = 1.f, attack = 0.f, release = 0.f;
		int stage = OFF;
		char one_shot = 0;
	} env;
	struct Op {
		int32_t output = 0, modulation = 0;
		float frequency = 440.f;
		uint32_t phase_increment = 0, phase_index = 0;
	} op1, op2;
	// dynamic_smooth, initialised as main.c does: (0.05, 0.5)
	float g0 = 0.f, sense = 2.f, low1 = 0.f, low2 = 0.f;

	float ENV_OFFSET = 0.f, ENV_AMT = 0.f, khz_pot = 0.f, filtered_khz_in = 0.f,
	      khz_in = 0.f, freqSum = 0.f, fm_ratio = 0.f, khz_in_scaling = WIDE_SCALE;
	uint8_t last_trig = 0;
	int32_t out = 0;

	// What the ADCs read. ADC1 is sampled by TIM3, ADC2 by TIM2, both by DMA.
	uint16_t adc1_10b[5] = {0, 0, 0, 0, 495};
	uint16_t adc2[2] = {2048, 1792};
	bool trigPin = false, voctJumper = true;

	double acc = 0.0, ctlAcc = 0.0;

	StockFirmware() {
		float gc = .00000712018209f;
		g0 = 2.f * gc / (1.f + gc);
	}

	float dynSmooth(float in) {
		float low1z = low1, low2z = low2;
		float bandz = low2z - low1z;
		float g = std::min(g0 + sense * std::fabs(bandz), 1.f);
		low1 = low1z + g * (in - low1z);
		low2 = low2z + g * (low1 - low2z);
		return low2;
	}
	// A float converted to uint32 on the M4 saturates (vcvt.u32.f32); in C it
	// is undefined, so say what the chip does.
	static uint32_t satU32(float x) {
		if (!(x > 0.f)) return 0u;
		if (x >= 4294967295.f) return 0xFFFFFFFFu;
		return (uint32_t)x;
	}

	void runOp(Op& op, float envelope) {
		const uint16_t* sine = stockSine().t;
		op.phase_index += op.phase_increment;
		uint32_t idx = ((op.phase_index >> 22) + (uint32_t)(int32_t)(op.modulation * ENV_AMT)) & 1023u;
		op.output = (int32_t)(((int32_t)sine[idx] - 32768) * envelope);
	}

	// main_2OP_loop(): TIM2
	void audioTick() {
		uint8_t trig = trigPin ? 1 : 0;
		if (trig && !last_trig) { env.stage = ATTACK; env.one_shot = 0; }
		last_trig = trig;

		ENV_OFFSET += (((((960 - (adc2[FM_JACK] >> 2)) - 512)) / 512.0f) - ENV_OFFSET) * 0.01f;
		ENV_AMT = (adc1_10b[FM_SLIDER] / 1600.0f) + ENV_OFFSET;
		if (ENV_AMT < 0.0f) ENV_AMT = 0.0f;

		khz_pot += ((1023 - adc1_10b[KHZ_POT]) - khz_pot) * 0.05f;
		filtered_khz_in += ((adc2[KHZ_JACK] - 2048) - filtered_khz_in) * 0.05f;
		khz_in = dynSmooth(filtered_khz_in) * khz_in_scaling;     // khz_correction ~ 1 at 3.3 V
		freqSum += ((khz_in + khz_pot) - freqSum) * 0.08f;
		fm_ratio += ((adc1_10b[RATIO_POT] / 2047.0f) - fm_ratio) * 0.01f;

		// (uint16_t)(freqSum) & 1023: saturates at 0 below, WRAPS above 1023
		op1.frequency = STOCK_EXPO[(uint16_t)satU32(freqSum) & 1023];
		op1.phase_increment = satU32(42949.6710f * op1.frequency);
		op2.frequency = op1.frequency * (fm_ratio * 32.0f);
		op2.phase_increment = satU32(42949.6710f * op2.frequency);

		if (adc1_10b[DECAY_SLIDER] > 1000) env.output = 1.0f;

		runOp(op1, env.output);
		runOp(op2, env.output * env.output);
		op1.modulation = op2.output >> 3;

		out = op1.output >> 5;
		if (out > 2047) out = 2047;
		if (out < -2047) out = -2047;
	}

	// run_linear_a_expo_r() then set_adsr_parameters(): TIM3
	void controlTick() {
		Env& a = env;
		if (a.output < 0.01f) a.one_shot = 0;
		if (a.stage == ATTACK) {
			a.output += 0.1f;                         // SMOOTH_ATTACK_INCREMENT
			a.rate = a.attack + a.attack_offset;
			if (a.output > 0.95f) a.stage = RELEASE;
		} else {
			if (a.stage == RELEASE) {
				a.rate = a.release + a.release_offset;
				a.target = 0.0f;
			}
			a.cutoff = (a.rate + a.rate_offset) * 0.1f;
			if (a.cutoff > 0.99f) a.cutoff = 0.99f;
			if (a.cutoff < 0.0f) a.cutoff = 0.f;
			a.output += (a.target - a.output) * a.cutoff;
		}

		a.rate_offset = 0.0005f;
		a.attack = 7.0f;
		int16_t jin = (int16_t)adc1_10b[DECAY_SLIDER] - ((int16_t)adc1_10b[DECAY_JACK] - 495);
		if (jin < 0) jin = 0;
		if (jin > 1023) jin = 1023;
		a.release = (STOCK_EXPO[jin] + 100.0f) / 100000.0f;

		khz_in_scaling = voctJumper ? VOCT_SCALE : WIDE_SCALE;
	}

	// One host sample. The ISRs run at their own rates and the DAC holds its
	// last value in between, exactly as the chip's DAC does.
	float step(float sr) {
		acc += FS / sr;
		while (acc >= 1.0) {
			acc -= 1.0;
			audioTick();
			ctlAcc += FS_CTL / FS;
			if (ctlAcc >= 1.0) { ctlAcc -= 1.0; controlTick(); }
		}
		// OP1.output >> 5 peaks at 1023, half the DAC: the analog stage is
		// assumed to scale that to the usual 5 V. (The clip at 2047 is never reached.)
		return out / 1023.f * 5.f;
	}

	// The front end, from voltages to ADC counts. The input stages invert, and
	// the offsets are the ones the firmware subtracts: 495 on DECAY, 448 on FM
	// (960 - 512), 2048 on kHz. The kHz slope is set so VOCT_SCALE is one octave
	// per volt through STOCK_EXPO (59.1 steps per octave).
	void readAdcs(float khzK, float ratioK, float fmK, float decayK,
	              float khzV, float fmV, float decayV, bool trigHigh) {
		auto c10 = [](float x) { return (uint16_t)clamp((int)std::lround(x), 0, 1023); };
		adc1_10b[RATIO_POT]    = c10(ratioK * 1023.f);
		adc1_10b[FM_SLIDER]    = c10(fmK * 1023.f);
		adc1_10b[DECAY_SLIDER] = c10(decayK * 1023.f);
		adc1_10b[KHZ_POT]      = c10(khzK * 1023.f);
		adc1_10b[DECAY_JACK]   = c10(495.f - 99.f * decayV);
		adc2[KHZ_JACK] = (uint16_t)clamp((int)std::lround(2048.f - 401.2f * khzV), 0, 4095);
		adc2[FM_JACK]  = (uint16_t)clamp((int)std::lround(4.f * (448.f - 88.f * fmV)), 0, 4095);
		trigPin = trigHigh;
	}
	float envLevel() const { return env.output; }
};

// ── shared pieces for the new firmwares ─────────────────────────────────────

// Linear 2 ms attack (the stock firmware's, which is a good one), exponential
// decay to silence. `drone` holds it open, slewing up rather than jumping.
struct AmpEnv {
	float v = 0.f;
	bool attack = false;
	void trig() { attack = true; }
	float step(float atkInc, float decCoef, bool drone) {
		if (attack) {
			v += atkInc;
			if (v >= 1.f) { v = 1.f; attack = false; }
		} else if (drone) {
			v += (1.f - v) * atkInc;
		} else {
			v *= decCoef;
		}
		return v;
	}
};

// A bank of rotating resonators: struck by a raised-cosine force pulse (a
// contact of T seconds excites almost nothing above 2/T), and a retrigger adds
// to what is ringing rather than clearing it.
template <int N>
struct Modal {
	float re[N] = {}, im[N] = {}, c[N] = {}, s[N] = {}, w[N] = {};
	int n = N;
	float force = 0.f, t = 0.f, dur = 0.001f;
	void set(int k, float hz, float t60, float gain, float sr) {
		if (hz >= 0.45f * sr || hz <= 0.f) { c[k] = s[k] = 0.f; w[k] = 0.f; return; }
		float r = std::exp(-6.9078f / (std::max(t60, 0.005f) * sr));
		float sw, cw;
		sfs::modeSinCos(2.f * (float)M_PI * hz / sr, sw, cw);
		c[k] = r * cw; s[k] = r * sw; w[k] = gain;
	}
	void strike(float amp, float contact) { force = amp; t = 0.f; dur = contact; }
	float step(float sr) {
		float f = 0.f;
		if (force > 0.f) {
			float u = t / dur;
			if (u >= 1.f) force = 0.f;
			else f = force * (0.5f - 0.5f * SFS_COS2PI(u)) * 2.f / (dur * sr);
			t += 1.f / sr;
		}
		float y = 0.f;
		for (int k = 0; k < N; k++) {
			float x = re[k], z = im[k] + f * w[k];
			re[k] = c[k] * x - s[k] * z;
			im[k] = s[k] * x + c[k] * z;
			y += re[k];
		}
		return y;
	}
	void clear() { for (int k = 0; k < N; k++) re[k] = im[k] = 0.f; force = 0.f; }
};

// Free-free beam partials, (beta_n L / 4.730)^2.
static float barRatio(int k) {
	static const float bl[5] = {4.7300f, 7.8532f, 10.9956f, 14.1372f, 17.2788f};
	float b = k < 5 ? bl[k] : (2 * k + 3) * (float)M_PI * 0.5f;
	return (b / 4.7300f) * (b / 4.7300f);
}

// Carillon's bell and bar, copied rather than shared: firmware is a snapshot,
// and these seventeen numbers are what would be burned into it.
static const int BELL_N = 17;
static const float BELL_RATIO[BELL_N] = {0.500f, 0.4991f, 1.000f, 0.9985f, 1.200f, 1.2033f, 1.498f,
	2.000f, 2.0035f, 2.515f, 2.667f, 3.000f, 4.02f, 5.02f, 5.45f, 6.03f, 8.15f};
static const float BELL_DB[BELL_N] = {-10.f, -13.f, -8.f, -10.f, -4.f, -6.f, -18.f,
	0.f, -3.f, -14.f, -15.f, -6.f, -9.f, -14.f, -16.f, -15.f, -20.f};
static const float BELL_T60[BELL_N] = {30.f, 30.f, 20.f, 20.f, 15.f, 15.f, 8.f,
	10.f, 10.f, 5.f, 5.f, 5.f, 3.f, 2.f, 1.6f, 1.3f, 0.8f};
static const float BAR_RATIO[BELL_N] = {1.f, 1.0015f, 1.f, 1.002f, 2.756f, 2.760f, 5.404f,
	5.404f, 5.41f, 8.933f, 8.94f, 13.34f, 13.35f, 18.64f, 19.f, 24.8f, 31.f};
static const float BAR_DB[BELL_N] = {-20.f, -24.f, 0.f, -6.f, -4.f, -8.f, -8.f,
	-12.f, -14.f, -12.f, -16.f, -16.f, -20.f, -22.f, -24.f, -26.f, -30.f};
static const float BAR_T60[BELL_N] = {2.5f, 2.5f, 4.f, 4.f, 2.2f, 2.2f, 1.3f,
	1.3f, 1.3f, 0.8f, 0.8f, 0.45f, 0.45f, 0.3f, 0.3f, 0.2f, 0.15f};

// Vowel formants F1-F4 (adult male), from Intone's table.
static const float VOWEL_F[5][4] = {
	{730.f, 1090.f, 2440.f, 3400.f},   // a
	{530.f, 1840.f, 2480.f, 3400.f},   // e
	{270.f, 2290.f, 3010.f, 3400.f},   // i
	{570.f,  840.f, 2410.f, 3400.f},   // o
	{300.f,  870.f, 2240.f, 3400.f},   // u
};
static const float FORMANT_AMP[4] = {1.f, 0.5f, 0.25f, 0.12f};
static const float FORMANT_BW[4]  = {80.f, 90.f, 120.f, 130.f};

// One FOF grain: a sine at the formant, a raised-cosine attack, an exponential
// tail whose rate is the formant's bandwidth. The sine and the tail are one
// decaying rotation (z *= r e^iw, the modal resonators' trick): on the
// 2OPFM's M4 a grain has to cost a handful of multiplies, since up to 32 of
// them sound at once and two polynomial sines each ran over the sample budget.
struct Grain {
	bool on = false;
	float zr = 0.f, zi = 0.f, cr = 1.f, ci = 0.f, aph = 0.f, daph = 0.f, amp = 0.f;
	float tick() {
		float a = aph < 0.5f ? 0.5f - 0.5f * SFS_COS2PI(aph) : 1.f;
		float y = amp * a * zi;
		float r = zr * cr - zi * ci;
		zi = zr * ci + zi * cr;
		zr = r;
		aph += daph;
		if (zr * zr + zi * zi < 1e-8f && aph >= 0.5f) on = false;   // below -80 dB
		return y;
	}
};

// ── the module ──────────────────────────────────────────────────────────────

struct TwoOp : Module {
	enum ParamId { KHZ_PARAM, RATIO_PARAM, FM_PARAM, DECAY_PARAM, PARAMS_LEN };
	enum InputId { KHZ_INPUT, FM_INPUT, DECAY_INPUT, TRIG_INPUT, INPUTS_LEN };
	enum OutputId { OUT_OUTPUT, OUTPUTS_LEN };
	enum LightId { ENV_LIGHT, LIGHTS_LEN };

	int mode = MODE_STOCK;
	bool voct = true;          // the V/OCT jumper: fitted = 1V/oct, off = wide
	int pendingMode = -1;      // set from the menu, taken by process()

	StockFirmware stock;
	dsp::SchmittTrigger trig;
	AmpEnv env;
	int ctl = 0;
	static const int CTL_DIV = 16;

	// resolved at control rate
	float f0 = 261.63f, atkInc = 0.f, decCoef = 0.f;
	bool drone = false;
	float fmAmt = 0.f, ratioK = 0.f, decayK = 0.f;

	// 2OPFM+
	float phC = 0.f, phM = 0.f, fb1 = 0.f, fb2 = 0.f, ratio = 1.f;
	// DRUM and BELL
	Modal<16> drum;
	Modal<BELL_N> bell;
	float drumContact = 0.001f, drumNorm = 1.f, bellContact = 0.001f;
	// CZ
	float phCz = 0.f, dcX = 0.f, dcY = 0.f;
	// FORMANT
	static const int GRAINS = 8;
	Grain grain[4][GRAINS];
	int grainNext[4] = {};
	float phF = 0.f, formantHz[4] = {}, rotR[4] = {1.f, 1.f, 1.f, 1.f}, rotI[4] = {};

	TwoOp() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		configParam(KHZ_PARAM, 0.f, 1.f, 0.5f, "kHz");
		configParam(RATIO_PARAM, 0.f, 1.f, 0.0625f, "Ratio");
		configParam(FM_PARAM, 0.f, 1.f, 0.3f, "FM");
		configParam(DECAY_PARAM, 0.f, 1.f, 0.5f, "Decay");
		configInput(KHZ_INPUT, "kHz CV (1V/oct with the V/OCT jumper, wider without)");
		configInput(FM_INPUT, "FM CV");
		configInput(DECAY_INPUT, "Decay CV");
		configInput(TRIG_INPUT, "Trigger");
		configOutput(OUT_OUTPUT, "Audio");
		configLight(ENV_LIGHT, "Envelope");
		applyNames();
	}

	void applyNames() {
		for (int i = 0; i < PARAMS_LEN; i++)
			paramQuantities[i]->name = TWOOP_PARAM_NAMES[mode][i];
	}

	void resetVoices() {
		stock = StockFirmware();
		env = AmpEnv();
		phC = phM = fb1 = fb2 = 0.f;
		drum.clear();
		bell.clear();
		phCz = dcX = dcY = 0.f;
		for (auto& row : grain) for (Grain& g : row) g.on = false;
		phF = 0.f;
		ctl = 0;
	}

	void onReset() override { pendingMode = MODE_STOCK; voct = true; }

	// ── control rate ────────────────────────────────────────────────────────
	void control(float sr) {
		float k = params[KHZ_PARAM].getValue();
		float cv = inputs[KHZ_INPUT].getVoltage() * (voct ? 1.f : 1.697f);
		f0 = clamp(261.63f * std::exp2((k - 0.5f) * 8.f + cv), 4.f, sr * 0.45f);
		ratioK = params[RATIO_PARAM].getValue();
		fmAmt  = clamp(params[FM_PARAM].getValue() + inputs[FM_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		decayK = clamp(params[DECAY_PARAM].getValue() + inputs[DECAY_INPUT].getVoltage() * 0.2f, 0.f, 1.f);
		drone  = params[DECAY_PARAM].getValue() > 1000.f / 1023.f;     // the slider, as the stock firmware reads it
		float t60 = 0.02f * std::pow(400.f, decayK);                   // 20 ms .. 8 s
		decCoef = std::exp(-6.9078f / (t60 * sr));
		atkInc  = 1.f / (0.002f * sr);

		switch (mode) {
			case MODE_PLUS: {
				// sticky on the integers (and on 1/2 below them): the middle 60% of
				// each step is flat, the edges glide to the next
				float r = 0.5f + ratioK * 15.5f;
				float n = std::floor(r + 0.5f), d = r - n;
				float a = std::fabs(d);
				float g = a < 0.3f ? 0.f : (a - 0.3f) / 0.2f * 0.5f;
				ratio = std::max(0.5f, n + (d < 0.f ? -g : g));
				break;
			}
			case MODE_DRUM: {
				float m = ratioK;
				float u = 0.05f + 0.85f * fmAmt;              // strike radius / distance from centre
				float t60d = 0.05f * std::pow(120.f, decayK);  // 50 ms .. 6 s
				float damp = 0.35f * (1.f - m) + 0.06f * m;    // a skin loses its highs, a bar keeps them
				const sfs::MembraneShapes& sh = sfs::membraneShapes();
				float norm = 0.f;
				for (int j = 0; j < 16; j++) {
					float mem = sfs::MEMBRANE_MODES[j].j / sfs::MEMBRANE_MODES[0].j;
					float rr = mem * std::pow(barRatio(j) / mem, m);
					float memShape = sh.at(j, u);
					float bar = std::cos(j * (float)M_PI * 0.5f + (j + 1) * (float)M_PI * 0.5f * u);
					bar += (1.f - bar) * u * u * u;
					float g = (1.f - m) * memShape + m * bar;
					drum.set(j, f0 * rr, t60d / (1.f + damp * (rr - 1.f)), g, sr);
					norm += g * g;
				}
				drumNorm = 1.f / std::sqrt(std::max(norm, 0.05f));
				drumContact = 0.004f * std::pow(0.3f / 4.f, fmAmt);   // 4 ms soft .. 0.3 ms hard
				break;
			}
			case MODE_BELL: {
				float shapeK = ratioK, brightK = fmAmt;
				float ring = 0.03f * std::pow(4.f / 0.03f, decayK);
				float sizeT = clamp(std::sqrt(261.6f / f0), 0.3f, 3.f);   // a bigger bell rings longer
				float tilt = (brightK - 0.5f) * 2.f;
				for (int j = 0; j < BELL_N; j++) {
					float rr = BELL_RATIO[j] * std::pow(BAR_RATIO[j] / BELL_RATIO[j], shapeK);
					float db = BELL_DB[j] + (BAR_DB[j] - BELL_DB[j]) * shapeK;
					float amp = std::pow(10.f, db / 20.f) * std::pow(std::max(rr, 0.5f) / 2.f, 0.5f * shapeK + tilt);
					float t = BELL_T60[j] * std::pow(BAR_T60[j] / BELL_T60[j], shapeK) * ring * sizeT;
					bell.set(j, f0 * rr, t, amp, sr);
				}
				bellContact = clamp(0.0008f * 261.6f / std::max(f0, 40.f) * (2.4f - 2.2f * brightK) * (1.f - 0.85f * shapeK),
				                    0.0001f, 0.006f);
				break;
			}
			case MODE_FORMANT: {
				float v = ratioK * 4.f;
				int a = std::min((int)v, 3);
				float fr = v - a;
				float shift = std::exp2((fmAmt - 0.5f) * 2.f);
				for (int j = 0; j < 4; j++) {
					float lf = std::log2(VOWEL_F[a][j]) * (1.f - fr) + std::log2(VOWEL_F[a + 1][j]) * fr;
					formantHz[j] = std::min(std::exp2(lf) * (j < 3 ? shift : 1.f), sr * 0.45f);
					// each period's grain: one sample of rotation at the formant, and
					// the bandwidth's decay folded in
					float dec = std::exp(-(float)M_PI * FORMANT_BW[j] / sr), sw, cw;
					sfs::modeSinCos(2.f * (float)M_PI * formantHz[j] / sr, sw, cw);
					rotR[j] = dec * cw; rotI[j] = dec * sw;
				}
				break;
			}
			default: break;
		}
	}

	// ── the voices ──────────────────────────────────────────────────────────
	float plus(float sr, bool fired) {
		if (fired) env.trig();
		float e = env.step(atkInc, decCoef, drone);
		// index 0..12 rad, squared so the bottom of the slider has resolution;
		// the top quarter feeds the modulator back into itself as well
		float I = 12.f * fmAmt * fmAmt;
		float fb = clamp((fmAmt - 0.75f) * 4.f, 0.f, 1.f) * 1.3f;
		float m = SFS_SIN2PI(phM + fb * 0.5f * (fb1 + fb2) * 0.15915494f);
		fb2 = fb1; fb1 = m;
		float y = SFS_SIN2PI(phC + I * e * e * m * 0.15915494f);
		phC += f0 / sr;          phC -= std::floor(phC);
		phM += f0 * ratio / sr;  phM -= std::floor(phM);
		return 5.f * y * e;
	}

	float drumVoice(float sr, bool fired) {
		if (fired) drum.strike(1.f, drumContact);
		env.v = std::min(1.f, drum.force > 0.f ? 1.f : env.v * 0.9995f);
		return 5.f * SFS_TANH(1.4f * drum.step(sr) * drumNorm);
	}

	float bellVoice(float sr, bool fired) {
		if (fired) bell.strike(1.f, bellContact);
		env.v = std::min(1.f, bell.force > 0.f ? 1.f : env.v * 0.9998f);
		return 5.f * SFS_TANH(0.4f * bell.step(sr));
	}

	// Casio phase distortion. d is the DCW: 0 is a pure cosine in every wave.
	static float czWave(int w, float ph, float d) {
		float p;
		switch (w) {
			case 0: {   // saw
				float m = 0.5f - 0.49f * d;
				p = ph < m ? 0.5f * ph / m : 0.5f + 0.5f * (ph - m) / (1.f - m);
				return SFS_COS2PI(p);
			}
			case 1: {   // square
				float h = ph < 0.5f ? 0.f : 1.f;
				float t = std::min((ph * 2.f - h) / (1.f - 0.98f * d), 1.f);
				return SFS_COS2PI(0.5f * (h + t));
			}
			case 2:     // pulse
				return SFS_COS2PI(std::min(ph / (1.f - 0.95f * d), 1.f));
			default: {  // resonance: a sine at R x f0, reset every period, in a window
				float win = w == 3 ? 1.f - ph : w == 4 ? 1.f - std::fabs(2.f * ph - 1.f)
				                                      : std::min(1.f, 2.f * (1.f - ph));
				float R = 1.f + 15.f * d;
				return 1.f - win * (1.f - SFS_COS2PI(R * ph));
			}
		}
	}
	float cz(float sr, bool fired) {
		if (fired) env.trig();
		float e = env.step(atkInc, decCoef, drone);
		float d = clamp(fmAmt * e * e, 0.f, 1.f);
		float pos = ratioK * 5.f;
		int a = std::min((int)pos, 5);
		float fr = pos - a;
		float b = clamp((fr - 0.4f) / 0.2f, 0.f, 1.f);   // a plateau per wave, a short crossfade between
		b = b * b * (3.f - 2.f * b);
		float y = czWave(a, phCz, d);
		if (b > 0.f && a < 5) y += (czWave(a + 1, phCz, d) - y) * b;
		phCz += f0 / sr; phCz -= std::floor(phCz);
		// the resonance waves sit off centre; a 10 Hz highpass takes the DC out
		float R = 1.f - 2.f * (float)M_PI * 10.f / sr;
		float hp = y - dcX + R * dcY;
		dcX = y; dcY = hp;
		return 5.f * hp * e;
	}

	float formant(float sr, bool fired) {
		if (fired) env.trig();
		float e = env.step(atkInc, decCoef, drone);
		phF += f0 / sr;
		if (phF >= 1.f) {
			phF -= std::floor(phF);
			// one grain per formant per glottal period
			float atk = std::min(0.001f, 0.5f / f0);
			for (int j = 0; j < 4; j++) {
				Grain& g = grain[j][grainNext[j]];
				grainNext[j] = (grainNext[j] + 1) % GRAINS;
				g.on = true; g.zr = 1.f; g.zi = 0.f; g.aph = 0.f;
				g.cr = rotR[j]; g.ci = rotI[j];
				g.daph = 0.5f / (atk * sr);
				g.amp = FORMANT_AMP[j];
			}
		}
		float y = 0.f;
		for (auto& row : grain) for (Grain& g : row) if (g.on) y += g.tick();
		// heavy grain overlap at high pitch piles up; keep the level even
		float gain = 1.f / (1.f + f0 * (1.f / 150.f));
		return 5.f * SFS_TANH(y * gain * 2.4f) * e;
	}

	void process(const ProcessArgs& args) override {
		if (pendingMode >= 0) {
			mode = pendingMode;
			pendingMode = -1;
			resetVoices();
			applyNames();
		}
		float sr = args.sampleRate;
		float trigV = inputs[TRIG_INPUT].getVoltage();
		bool fired = trig.process(trigV, 0.5f, 1.f);

		float y = 0.f;
		if (mode == MODE_STOCK) {
			stock.voctJumper = voct;
			stock.readAdcs(params[KHZ_PARAM].getValue(), params[RATIO_PARAM].getValue(),
			               params[FM_PARAM].getValue(), params[DECAY_PARAM].getValue(),
			               inputs[KHZ_INPUT].getVoltage(), inputs[FM_INPUT].getVoltage(),
			               inputs[DECAY_INPUT].getVoltage(), trigV > 1.f);
			y = stock.step(sr);
			env.v = clamp(stock.envLevel(), 0.f, 1.f);
		} else {
			if (ctl-- <= 0) { ctl = CTL_DIV - 1; control(sr); }
			switch (mode) {
				case MODE_PLUS:    y = plus(sr, fired); break;
				case MODE_DRUM:    y = drumVoice(sr, fired); break;
				case MODE_CZ:      y = cz(sr, fired); break;
				case MODE_FORMANT: y = formant(sr, fired); break;
				case MODE_BELL:    y = bellVoice(sr, fired); break;
				default: break;
			}
		}
		outputs[OUT_OUTPUT].setVoltage(y);
		lights[ENV_LIGHT].setBrightnessSmooth(env.v, args.sampleTime);
	}

	json_t* dataToJson() override {
		json_t* root = json_object();
		json_object_set_new(root, "mode", json_integer(pendingMode >= 0 ? pendingMode : mode));
		json_object_set_new(root, "voct", json_boolean(voct));
		return root;
	}
	void dataFromJson(json_t* root) override {
		if (json_t* j = json_object_get(root, "mode"))
			pendingMode = clamp((int)json_integer_value(j), 0, TWOOP_MODES - 1);
		if (json_t* j = json_object_get(root, "voct")) voct = json_boolean_value(j);
	}
};


// ── widget ──────────────────────────────────────────────────────────────────

// The panel is the hardware's own art (Super Synthesis 2OPFM, REV5), drawn at
// 30.00 mm and centred in VCV's 30.48 mm 6HP, so every position below is the
// art's plus 0.24 mm in x. The art carries its own lettering: NO runtime
// PanelLabels, or every label prints twice. Its jacks, knobs and slider
// handles are the widgets' own graphics (res/parts/, cut from this art),
// so the controls look exactly as drawn; the tracks and the LED's off state
// stay in the panel. The art's screw was left out: no screws on any panel.
static const float TO_XL = 7.62f, TO_XR = 22.86f;
static const float TO_Y_OUT = 18.53f;     // LED, OP1 out
static const float TO_Y_J1  = 33.77f;     // DECAY CV, TRIG
static const float TO_Y_J2  = 49.01f;     // kHz CV, FM CV
static const float TO_Y_KNOB = 64.25f;    // kHz, RATIO
static const float TO_Y_SLIDE = 92.34f;   // DECAY, FM tracks (centres)

// The drawn parts are the shared kit (src/parts.hpp, res/parts/), which was
// first cut from this panel.

// The firmware in force, bottom left in the art's gold, opposite its 2OPFM
// mark: the panel cannot say it otherwise.
struct TwoOpModeLabel : Widget {
	TwoOp* module = nullptr;
	std::shared_ptr<Font> font;
	void draw(const DrawArgs& args) override {
		if (!font || font->handle < 0) font = sfs::panelFontBold();
		if (!font || font->handle < 0) return;
		int m = module ? (module->pendingMode >= 0 ? module->pendingMode : module->mode) : MODE_STOCK;
		NVGcontext* vg = args.vg;
		nvgFontFaceId(vg, font->handle);
		nvgFontSize(vg, mm2px(sfs::TYPE_NOTE));
		nvgTextLetterSpacing(vg, mm2px(0.1f));
		nvgFillColor(vg, nvgRGB(0xef, 0xcb, 0x8f));
		nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
		nvgText(vg, 0.f, box.size.y / 2.f, TWOOP_MODE_SHORT[m], NULL);
	}
};

struct TwoOpWidget : ModuleWidget {
	TwoOpWidget(TwoOp* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/2opfm.svg")));

		TwoOpModeLabel* ml = new TwoOpModeLabel();
		ml->module = module;
		ml->box.pos = mm2px(Vec(1.8f, 123.5f));
		ml->box.size = mm2px(Vec(14.f, 4.f));
		addChild(ml);

		addChild(createLightCentered<super::Led>(mm2px(Vec(TO_XL, TO_Y_OUT)), module, TwoOp::ENV_LIGHT));
		addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(TO_XR, TO_Y_OUT)), module, TwoOp::OUT_OUTPUT));

		addInput(createInputCentered<super::JackIn>(mm2px(Vec(TO_XL, TO_Y_J1)), module, TwoOp::DECAY_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(TO_XR, TO_Y_J1)), module, TwoOp::TRIG_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(TO_XL, TO_Y_J2)), module, TwoOp::KHZ_INPUT));
		addInput(createInputCentered<super::JackIn>(mm2px(Vec(TO_XR, TO_Y_J2)), module, TwoOp::FM_INPUT));

		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(TO_XL, TO_Y_KNOB)), module, TwoOp::KHZ_PARAM));
		addParam(createParamCentered<super::KnobSmall>(mm2px(Vec(TO_XR, TO_Y_KNOB)), module, TwoOp::RATIO_PARAM));

		addParam(createParamCentered<super::Slider>(mm2px(Vec(TO_XL, TO_Y_SLIDE)), module, TwoOp::DECAY_PARAM));
		addParam(createParamCentered<super::Slider>(mm2px(Vec(TO_XR, TO_Y_SLIDE)), module, TwoOp::FM_PARAM));
	}

	void appendContextMenu(Menu* menu) override {
		TwoOp* m = dynamic_cast<TwoOp*>(module);
		if (!m) return;
		menu->addChild(new MenuSeparator);
		menu->addChild(createMenuLabel("Firmware"));
		for (int i = 0; i < TWOOP_MODES; i++) {
			menu->addChild(createCheckMenuItem(TWOOP_MODE_NAMES[i], "",
				[=]() { return (m->pendingMode >= 0 ? m->pendingMode : m->mode) == i; },
				[=]() { m->pendingMode = i; }));
		}
		menu->addChild(new MenuSeparator);
		menu->addChild(createBoolPtrMenuItem("V/OCT jumper fitted (off: wide kHz CV)", "", &m->voct));
	}
};

Model* modelTwoOpFM = createModel<TwoOp, TwoOpWidget>("2OPFM");
