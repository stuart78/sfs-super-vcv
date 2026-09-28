#include "plugin.hpp"
#include "parts.hpp"
#include <cmath>

// =============================================================================
// SVFs — the Super Synthesis dual state-variable filter (REV3), modelled from
// its schematic (design/SVFs/schematic.pdf) with the values in its BOM.
//
// Each channel is a two-integrator loop on a V2164 VCA. Stage by stage:
//
// THE SUMMER (U5.1, TL074). IN is inverted (U5.2) and summed with LP and BP
// into U5.1's inverting input, feedback 100k, so
//     HP = IN - LP - 0.1 BP + 3.1 V+
// where 0.1 is the 1M from BP, and 3.1 is the non-inverting gain,
// 1 + 100k / (100k || 100k || 1M). V+ is the RES pot's share of BP.
//
// THE INTEGRATORS. HP drives a 2164 cell through 30k; the cell's output
// current integrates on 220p (U5.4), giving BP; BP does the same through the
// second cell into LP. Each inverts, so with BPs the conventional band-pass,
// the node the schematic calls BP is -BPs, and the loop's damping is
//     k = 3.1 a - 0.1
// with a = V+ / BP. That -0.1 is deliberate: with RES fully up k goes
// negative and the filter oscillates.
//
// RESONANCE. The RES pot (100k) runs from BP to ground with its wiper on U5.1+
// and 10k from wiper to ground, so a = (Rb || 10k) / (Ra + Rb || 10k): 1 with
// the wiper at BP (k = 3, a Q of 1/3), falling towards 0 at ground. Back-to-back
// 4.7 V zeners from BP to U5.1+ conduct once BP leads V+ by about 5.4 V (4.7 +
// a diode drop), pulling V+ after BP and so raising the damping: that is the
// amplitude limiter, and why self-oscillation settles near 11 Vpp. Except at
// the very top of RES: there the wiper sits on ground, U5.1+ with it, the
// zeners have nothing to pull against, and the op-amp rails do the limiting.
//
// FREQUENCY. The 2164 is -33 mV/dB, so its gain is 2^(-Vc / 0.1987 V), and with
// 30k and 220p the cutoff is 24.1 kHz at Vc = 0. Vc is built by U1.2 (30k 0.1%
// feedback) from currents: the 1V/oct jack through 1k + 150k 0.1% (30/151 of a
// volt is exactly one octave at -33 mV/dB), the FREQ pot (100k to the -10 V
// LM4040 through 1k, wiper through 75k), and the CV attenuverter. D6/D8 make
// U1.2 a limiter: Vc cannot go below 0, so the cutoff cannot pass 24.1 kHz.
//
// THE ATTENUVERTER. The CV jack goes through 51k into the wiper of a 100k pot
// whose ends are two virtual grounds, U1.2's directly and U1.1's, which
// inverts; the current splits by the wiper's position, so it is zero at the
// centre and up to +-30k/51k = +-2.96 octaves per volt at the ends.
//
// Simulated as a zero-delay-feedback (TPT) SVF, 2x oversampled, with the
// damping from the zener/pot network evaluated on the previous sample's BP --
// the one place the loop is not solved implicitly. Op-amp outputs (TL074 on
// +-12 V) are soft-limited near +-10.5 V. Not modelled: 2164 temperature drift,
// the 10p/560p compensation (well above audio), op-amp slew. The circuit's
// noise is only a -120 dB floor, there to start self-oscillation.
// =============================================================================

struct SvfChannel {
	float s1 = 0.f, s2 = 0.f;   // TPT states: band-pass (conventional sign), low-pass
	float bpNode = 0.f;          // the schematic's BP, for the next sample's damping
	float hp = 0.f, bp = 0.f, lp = 0.f;

	// Op-amp output near a +-12 V rail: linear to 9 V, bending to 10.5 V.
	static float rail(float v) {
		float a = std::fabs(v);
		if (a <= 9.f) return v;
		float over = a - 9.f;
		return std::copysign(9.f + 1.5f * std::tanh(over / 1.5f), v);
	}

	// V+ from the RES network, given the schematic's BP.
	static float vPlus(float bpNode, float alpha, float rth) {
		float v = alpha * bpNode;
		float e = bpNode - v;
		// zener (4.7 V) + diode, about 5.4 V, with a softened knee;
		// ~100 ohm dynamic resistance against the network's Thevenin resistance
		// The knee is exactly zero below it: a smooth max that only approaches
		// zero leaks a millivolt at any level, and at the microvolts a
		// self-oscillation starts from that is enough damping to stop it.
		const float vz = 5.4f, w = 0.15f, rz = 100.f;
		float ex = std::fabs(e) - vz;
		float over = ex <= -w ? 0.f : ex >= w ? ex : (ex + w) * (ex + w) / (4.f * w);
		return v + std::copysign(over * rth / (rth + rz), e);
	}

	void process(float in, float g, float alpha, float rth) {
		// damping from last sample's BP; at small signal it is exactly 3.1 a - 0.1
		float k;
		if (std::fabs(bpNode) > 1e-3f) k = 3.1f * vPlus(bpNode, alpha, rth) / bpNode - 0.1f;
		else k = 3.1f * alpha - 0.1f;
		float h = (in - (k + g) * s1 - s2) / (1.f + g * (k + g));
		float b = g * h + s1;
		s1 = g * h + b;
		float l = g * b + s2;
		s2 = g * b + l;
		// the integrators' outputs cannot pass the rails
		s1 = clamp(s1, -11.f, 11.f);
		s2 = clamp(s2, -11.f, 11.f);
		hp = rail(h);
		bp = rail(-b);          // the schematic's BP node is the inverted band-pass
		lp = rail(l);
		bpNode = bp;
	}
};

struct Svfs : Module {
	// per channel: CV amount, RES, FREQ
	enum ParamId { CV_A_PARAM, RES_A_PARAM, FREQ_A_PARAM, CV_B_PARAM, RES_B_PARAM, FREQ_B_PARAM, PARAMS_LEN };
	enum InputId { IN_A_INPUT, VOCT_A_INPUT, CV_A_INPUT, IN_B_INPUT, VOCT_B_INPUT, CV_B_INPUT, INPUTS_LEN };
	enum OutputId { BP_A_OUTPUT, LP_A_OUTPUT, HP_A_OUTPUT, BP_B_OUTPUT, LP_B_OUTPUT, HP_B_OUTPUT, OUTPUTS_LEN };
	enum LightId { A_LIGHT, B_LIGHT, LIGHTS_LEN };

	static const int OS = 2;
	SvfChannel ch[2];
	// The circuit's own noise, about -120 dB: inaudible, and what starts a
	// resonant filter oscillating. A model with none sits at exactly zero
	// forever with RES fully up, which the hardware never does.
	uint32_t noiseState = 0x9E3779B9u;
	float noise() {
		noiseState ^= noiseState << 13; noiseState ^= noiseState >> 17; noiseState ^= noiseState << 5;
		return ((noiseState >> 8) * (1.f / 16777216.f) - 0.5f) * 2e-6f;
	}

	Svfs() {
		config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
		for (int c = 0; c < 2; c++) {
			std::string n = c ? " B" : " A";
			int o = c * 3;
			configParam(CV_A_PARAM + o, 0.f, 1.f, 0.5f, "CV amount" + n + " (attenuverter: centre is none)", " oct/V", 0.f, 5.92f, -2.96f);
			configParam(RES_A_PARAM + o, 0.f, 1.f, 0.5f, "Resonance" + n);
			configParam(FREQ_A_PARAM + o, 0.f, 1.f, 0.5f, "Frequency" + n);
			configInput(IN_A_INPUT + o, "Audio" + n);
			configInput(VOCT_A_INPUT + o, "Frequency CV" + n + " (1V/oct)");
			configInput(CV_A_INPUT + o, "Frequency CV" + n + " (through the attenuverter)");
			configOutput(BP_A_OUTPUT + o, "Band-pass" + n);
			configOutput(LP_A_OUTPUT + o, "Low-pass" + n);
			configOutput(HP_A_OUTPUT + o, "High-pass" + n);
			configLight(A_LIGHT + c, "Low-pass" + n);
		}
	}

	// The 2164's control voltage: U1.2's summing node, from the three sources.
	static float controlVoltage(float voct, float freqK, float cv, float cvK) {
		// FREQ: 100k from ground to -10 V (LM4040) through 1k; x from the ground end
		float x = clamp(1.f - freqK, 0.f, 1.f);
		float rg = x * 100e3f, rr = (1.f - x) * 100e3f + 1e3f;
		float vth = -10.f * rg / (rg + rr);
		float rth = rg * rr / (rg + rr);
		float iFreq = vth / (rth + 75e3f);
		// 1V/oct: 1k + 150k, 0.1%
		float iVoct = voct / 151e3f;
		// attenuverter: 51k into a 100k pot between two virtual grounds
		float p = clamp(cvK, 0.f, 1.f);
		float rm = p * 100e3f, rp = (1.f - p) * 100e3f;
		float par = (rm + rp) > 0.f ? rm * rp / (rm + rp) : 0.f;
		float iCv = cv / (51e3f + par);
		float iPlus = iCv * p, iMinus = iCv * (1.f - p);
		// U1.1 inverts the minus share into U1.2's node through 100k/100k
		float i = iVoct + iFreq + iPlus - iMinus;
		return std::max(0.f, -30e3f * i);       // D6/D8: Vc does not go below 0
	}

	// RES: the pot's share of BP at U5.1+, and the network's Thevenin resistance
	static void resonance(float resK, float& alpha, float& rth) {
		float y = clamp(1.f - resK, 0.f, 1.f);            // wiper from the ground end
		float rb = y * 100e3f, ra = (1.f - y) * 100e3f;
		float rp = rb > 0.f ? rb * 10e3f / (rb + 10e3f) : 0.f;
		alpha = (ra + rp) > 0.f ? rp / (ra + rp) : 1.f;
		rth = (ra + rp) > 0.f ? ra * rp / (ra + rp) : 0.f;
	}

	void process(const ProcessArgs& args) override {
		float fs = args.sampleRate * OS;
		for (int c = 0; c < 2; c++) {
			int o = c * 3;
			float vc = controlVoltage(inputs[VOCT_A_INPUT + o].getVoltage(), params[FREQ_A_PARAM + o].getValue(),
			                          inputs[CV_A_INPUT + o].getVoltage(), params[CV_A_PARAM + o].getValue());
			float fc = 24114.f * std::exp2(-vc / 0.19868f);
			fc = clamp(fc, 0.005f, 0.45f * fs);
			float g = std::tan((float)M_PI * fc / fs);
			float alpha, rth;
			resonance(params[RES_A_PARAM + o].getValue(), alpha, rth);
			float in = inputs[IN_A_INPUT + o].getVoltage() + noise();
			float hp = 0.f, bp = 0.f, lp = 0.f;
			for (int k = 0; k < OS; k++) {
				ch[c].process(in, g, alpha, rth);
				hp += ch[c].hp; bp += ch[c].bp; lp += ch[c].lp;
			}
			outputs[HP_A_OUTPUT + o].setVoltage(hp / OS);
			outputs[BP_A_OUTPUT + o].setVoltage(bp / OS);
			outputs[LP_A_OUTPUT + o].setVoltage(lp / OS);
			// U2's LED driver off LP: lights on one polarity
			lights[A_LIGHT + c].setBrightnessSmooth(clamp(lp / OS / 8.f, 0.f, 1.f), args.sampleTime);
		}
	}
};

// The art's positions (tools/panel_svfs.py) plus 0.18 mm in x: the 60.60 mm
// panel centred in VCV's 60.96. Channel B is channel A 30.48 mm to the right.
struct SvfsWidget : ModuleWidget {
	SvfsWidget(Svfs* module) {
		setModule(module);
		setPanel(createPanel(asset::plugin(pluginInstance, "res/svfs.svg")));
		for (int c = 0; c < 2; c++) {
			float dx = c * 30.48f;
			int o = c * 3;
			float xl = 7.62f + dx, xr = 22.86f + dx, xm = 15.24f + dx;
			addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xl, 18.53f)), module, Svfs::BP_A_OUTPUT + o));
			addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 18.53f)), module, Svfs::LP_A_OUTPUT + o));
			addChild(createLightCentered<super::Led>(mm2px(Vec(xm, 26.15f)), module, Svfs::A_LIGHT + c));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 33.77f)), module, Svfs::IN_A_INPUT + o));
			addOutput(createOutputCentered<super::JackOut>(mm2px(Vec(xr, 33.77f)), module, Svfs::HP_A_OUTPUT + o));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(xl, 49.01f)), module, Svfs::VOCT_A_INPUT + o));
			addInput(createInputCentered<super::JackIn>(mm2px(Vec(xr, 49.01f)), module, Svfs::CV_A_INPUT + o));
			addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 64.25f)), module, Svfs::CV_A_PARAM + o));
			addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 79.48f)), module, Svfs::RES_A_PARAM + o));
			addParam(createParamCentered<super::KnobLarge>(mm2px(Vec(xm, 109.96f)), module, Svfs::FREQ_A_PARAM + o));
		}
	}
};

Model* modelSvfs = createModel<Svfs, SvfsWidget>("SVFs");
