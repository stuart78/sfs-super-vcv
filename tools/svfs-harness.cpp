// Drives the REAL SVFs -- src/svfs.cpp compiled as-is against libRack -- and
// checks it against the schematic's arithmetic: 1V/oct tracking in
// self-oscillation, the FREQ range, where RES starts to oscillate, the zener
// limit on the oscillation, pass-band gains, and the attenuverter's zero.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/svfs-harness.cpp -o /tmp/svfs-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/svfs-harness
#include "../src/svfs.cpp"
#include <cstdio>
#include <string>
#include <vector>
rack::plugin::Plugin* pluginInstance = nullptr;

static const float SR = 48000.f;
static int fails = 0;
static void check(bool ok, const std::string& what) {
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}

struct Rig {
	Svfs m;
	Module::ProcessArgs args;
	Rig() {
		args.sampleRate = SR; args.sampleTime = 1.f / SR; args.frame = 0;
		for (auto& i : m.inputs) i.channels = 1;
		for (auto& o : m.outputs) o.channels = 1;
	}
	void set(int p, float v) { m.params[p].setValue(v); }
	std::vector<float> run(float s, int out, float in = 0.f) {
		std::vector<float> y;
		for (int i = 0, n = (int)(s * SR); i < n; i++) {
			m.inputs[Svfs::IN_A_INPUT].setVoltage(in);
			m.process(args);
			y.push_back(m.outputs[out].getVoltage());
		}
		return y;
	}
};

static float pitch(const std::vector<float>& x, size_t a) {
	double first = -1, last = -1; int n = 0;
	for (size_t i = a + 1; i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) {
			double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
			if (first < 0) first = t;
			last = t; n++;
		}
	return n > 1 ? (float)((n - 1) * SR / (last - first)) : 0.f;
}
static float peak(const std::vector<float>& x, size_t a) {
	float p = 0.f;
	for (size_t i = a; i < x.size(); i++) p = std::max(p, std::fabs(x[i]));
	return p;
}

int main() {
	rack::random::init();

	printf("== the control law ==\n");
	{
		float vc0 = Svfs::controlVoltage(0.f, 1.f, 0.f, 0.5f);
		float vc1 = Svfs::controlVoltage(-1.f, 1.f, 0.f, 0.5f);
		float lo = Svfs::controlVoltage(0.f, 0.f, 0.f, 0.5f);
		printf("  Vc: FREQ max %.4f V, one volt down %.4f V (one octave is 0.1987 V); FREQ min %.3f V\n", vc0, vc1, lo);
		check(vc0 == 0.f, "FREQ fully up is Vc 0: the top of the range, 24.1 kHz");
		check(std::fabs(vc1 - 0.19868f) < 0.0005f, "1 V into the 1V/oct jack is exactly one octave at -33 mV/dB");
		printf("  FREQ spans %.1f octaves, down to %.3f Hz\n", lo / 0.19868f, 24114.f * std::exp2(-lo / 0.19868f));
		check(lo / 0.19868f > 18.f, "FREQ sweeps the 2164's range from 24 kHz to sub-audio");
		float up = Svfs::controlVoltage(0.f, 0.5f, 1.f, 1.f), mid = Svfs::controlVoltage(0.f, 0.5f, 1.f, 0.5f),
		      dn = Svfs::controlVoltage(0.f, 0.5f, 1.f, 0.f), none = Svfs::controlVoltage(0.f, 0.5f, 0.f, 0.5f);
		printf("  attenuverter, 1 V in: CW %+.3f oct, centre %+.4f, CCW %+.3f\n",
		       (none - up) / 0.19868f, (none - mid) / 0.19868f, (none - dn) / 0.19868f);
		check(std::fabs(mid - none) < 1e-4f, "the attenuverter's centre passes nothing");
		check(std::fabs((none - up) / 0.19868f - 2.96f) < 0.05f && std::fabs((none - dn) / 0.19868f + 2.96f) < 0.05f,
		      "its ends are +-2.96 octaves per volt (30k / 51k)");
	}

	printf("== resonance: Q from the pot network ==\n");
	{
		float a, r;
		Svfs::resonance(0.f, a, r);  float q0 = 1.f / (3.1f * a - 0.1f);
		Svfs::resonance(0.5f, a, r); float q5 = 1.f / (3.1f * a - 0.1f);
		float onset = -1;
		for (float k = 0.f; k <= 1.f; k += 0.001f) { Svfs::resonance(k, a, r); if (3.1f * a - 0.1f <= 0.f) { onset = k; break; } }
		printf("  Q %.2f at RES 0, %.2f at RES 0.5; self-oscillation from RES %.3f\n", q0, q5, onset);
		check(std::fabs(q0 - 1.f / 3.f) < 0.01f, "RES fully down is a Q of 1/3 (the wiper at BP: k = 3)");
		check(onset > 0.9f && onset < 1.f, "self-oscillation arrives only at the top of the knob");
	}

	printf("== self-oscillation: 1V/oct and the zener limit ==\n");
	{
		float f[3], amp = 0.f;
		for (int v = 0; v < 3; v++) {
			Rig r;
			r.set(Svfs::RES_A_PARAM, 1.f);
			r.set(Svfs::FREQ_A_PARAM, 0.62f);
			r.m.inputs[Svfs::VOCT_A_INPUT].setVoltage((float)v);
			auto y = r.run(1.5f, Svfs::BP_A_OUTPUT);
			f[v] = pitch(y, 48000);
			if (v == 0) amp = peak(y, 48000);
		}
		float c1 = 1200.f * std::log2(f[1] / f[0] / 2.f), c2 = 1200.f * std::log2(f[2] / f[1] / 2.f);
		printf("  %.2f Hz, %.2f Hz, %.2f Hz (octave errors %+.1f, %+.1f cents); BP peak %.2f V\n", f[0], f[1], f[2], c1, c2, amp);
		check(f[0] > 20.f, "it oscillates with RES fully up");
		check(std::fabs(c1) < 5.f && std::fabs(c2) < 5.f, "1V/oct tracks within 5 cents over two octaves");
		// RES fully up puts the pot's wiper on ground, so U5.1+ is grounded and
		// the zeners have nothing to pull against: there the op-amp rails limit it
		check(amp > 8.5f && amp < 11.f, "at the very top of RES the rails limit it (the wiper grounds U5.1+)");
		Rig z;
		z.set(Svfs::RES_A_PARAM, 0.97f);
		z.set(Svfs::FREQ_A_PARAM, 0.62f);
		auto y = z.run(3.f, Svfs::BP_A_OUTPUT);
		float za = peak(y, 96000);
		printf("  RES 0.97: BP peak %.2f V at %.1f Hz\n", za, pitch(y, 96000));
		check(za > 4.5f && za < 7.f, "just below the top, the zeners hold it near 11 Vpp");
	}

	printf("== pass bands ==\n");
	{
		Rig r;
		r.set(Svfs::RES_A_PARAM, 0.f);
		r.set(Svfs::FREQ_A_PARAM, 0.8f);
		auto lp = r.run(0.5f, Svfs::LP_A_OUTPUT, 2.f);
		float hpDc = r.m.outputs[Svfs::HP_A_OUTPUT].getVoltage();
		printf("  2 V DC in: LP %.3f V, HP %.4f V\n", lp.back(), hpDc);
		check(std::fabs(lp.back() - 2.f) < 0.01f, "LP passes DC at unity");
		check(std::fabs(hpDc) < 0.01f, "HP blocks DC");
		// silence in, silence out with RES down
		Rig q;
		auto y = q.run(0.5f, Svfs::BP_A_OUTPUT);
		check(peak(y, 0) < 1e-4f, "silent with no input and RES down");
	}

	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
