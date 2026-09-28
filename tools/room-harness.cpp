// Drives the REAL ROOM -- src/room.cpp compiled as-is against libRack -- and
// checks it against its firmware: a tail that FB lengthens and SIZE stretches,
// the dry path, and the expo-table bug (LP at the very top silences the wet
// signal) with its optional fix.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/room-harness.cpp -o /tmp/room-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/room-harness
#include "../src/room.cpp"
#include <cstdio>
#include <string>
#include <vector>
#include <complex>
rack::plugin::Plugin* pluginInstance = nullptr;

static const float SR = 48000.f;
static int fails = 0;
static void check(bool ok, const std::string& what) {
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}
struct Rig {
	Room m;
	Module::ProcessArgs args;
	Rig(float fb, float size, float lp = 0.8f) {
		args.sampleRate = SR; args.sampleTime = 1.f / SR; args.frame = 0;
		for (auto& i : m.inputs) i.channels = 1;
		m.params[Room::FB_PARAM].setValue(fb);
		m.params[Room::SIZE_PARAM].setValue(size);
		m.params[Room::LP_PARAM].setValue(lp);
		m.params[Room::MIX_PARAM].setValue(1.f);
		run(3.f);                                   // SIZE's smoothing is slow (0.001 a tick)
	}
	std::vector<float> run(float s, float burst = 0.f) {
		std::vector<float> y;
		for (int i = 0, n = (int)(s * SR); i < n; i++) {
			m.inputs[Room::IN_INPUT].setVoltage(i < (int)(burst * SR) ? ((i / 20) % 2 ? 4.f : -4.f) : 0.f);
			m.process(args);
			y.push_back(m.outputs[Room::OUT_OUTPUT].getVoltage());
		}
		return y;
	}
};
// time for the tail to fall 40 dB below its peak
static float decay40(const std::vector<float>& y) {
	float pk = 0; size_t ipk = 0;
	for (size_t i = 0; i < y.size(); i++) if (std::fabs(y[i]) > pk) { pk = std::fabs(y[i]); ipk = i; }
	size_t last = ipk;
	for (size_t i = ipk; i < y.size(); i++) if (std::fabs(y[i]) > pk * 0.01f) last = i;
	return (last - ipk) / SR;
}

// THD+N of a sine: the energy more than 30 Hz from it, against the total
static double thdn(const std::vector<float>& y, double f0) {
	int N = 8192; double tot = 0, off = 0;
	for (int k = 1; k < N / 2; k++) {
		std::complex<double> s = 0;
		for (int t = 0; t < N; t++) s += (double)y[y.size() - N + t] * (0.5 - 0.5 * std::cos(2 * M_PI * t / N)) * std::polar(1.0, -2 * M_PI * k * t / N);
		double p = std::norm(s); tot += p; if (std::fabs(k * SR / N - f0) > 30) off += p;
	}
	return 10 * std::log10(off / tot);
}

int main() {
	rack::random::init();
	printf("== level and cleanliness ==\n");
	{   // A 5 V sine is ordinary VCV audio. With 2OPFM's front end it filled the
		// ADC and its reverb sat on the +-2047 clamp 58% of the time, and the
		// crude rate conversion held the whole module to -32 dB.
		Rig r(0.5f, 0.6f);
		std::vector<float> y; float pk = 0;
		for (int i = 0; i < (int)(3 * SR); i++) {
			r.m.inputs[Room::IN_INPUT].setVoltage(5.f * std::sin(2 * M_PI * 440.0 * i / SR));
			r.m.process(r.args);
			if (i > 2 * SR) { y.push_back(r.m.outputs[Room::OUT_OUTPUT].getVoltage()); pk = std::max(pk, std::fabs(y.back())); }
		}
		double d = thdn(y, 440);
		printf("  5 V sine, defaults, wet: peak %.2f V, THD+N %.1f dB\n", pk, d);
		check(pk < 9.9f, "a 5 V signal's reverb stays off the firmware's clamp");
		check(d < -45.0, "THD+N better than -45 dB (the firmware alone is ~-50)");
	}
	printf("== the tail ==\n");
	float lo, hi, small;
	{ Rig r(0.3f, 0.6f); lo = decay40(r.run(8.f, 0.05f)); }
	{ Rig r(0.85f, 0.6f); hi = decay40(r.run(8.f, 0.05f)); }
	{ Rig r(0.85f, 0.15f); small = decay40(r.run(8.f, 0.05f)); }
	printf("  -40 dB after: FB 0.3 %.2f s, FB 0.85 %.2f s; FB 0.85 at SIZE 0.15 %.2f s\n", lo, hi, small);
	check(hi > 1.5f * lo, "more FB, a longer tail");
	check(small < hi, "a smaller ROOM decays sooner (shorter delays, more trips through the filters)");
	printf("== the firmware's table ==\n");
	{
		Rig bug(0.6f, 0.6f, 1.f);
		auto y = bug.run(2.f, 0.05f);
		float pk = 0; for (float v : y) pk = std::max(pk, std::fabs(v));
		Rig fixed(0.6f, 0.6f, 1.f);
		fixed.m.fw.fixTable = true; fixed.run(1.f);
		auto z = fixed.run(2.f, 0.05f);
		float pz = 0; for (float v : z) pz = std::max(pz, std::fabs(v));
		printf("  LP fully up: wet peak %.4f V as shipped, %.3f V with the table completed\n", pk, pz);
		check(pk < 0.01f, "as shipped, LP at the very top silences the reverb");
		check(pz > 0.2f, "with the table completed, it does not");
	}
	printf("== dry ==\n");
	{
		Rig r(0.5f, 0.5f);
		r.m.params[Room::MIX_PARAM].setValue(0.f);
		r.m.inputs[Room::IN_INPUT].setVoltage(1.5f);
		r.m.process(r.args);
		check(r.m.outputs[Room::OUT_OUTPUT].getVoltage() == 1.5f, "DRY passes the input untouched");
	}
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
