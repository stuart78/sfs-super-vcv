// Drives the REAL PNGBL -- src/pngbl.cpp compiled as-is against libRack -- and
// checks the schematic's behaviour: the KHZ range round ~330 Hz, a ping that
// rings the filter and dies away, self-oscillation at the top of RES, and the
// attenuverter's zero.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/pngbl-harness.cpp -o /tmp/pngbl-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/pngbl-harness
#include "../src/pngbl.cpp"
#include <cstdio>
#include <string>
#include <vector>
rack::plugin::Plugin* pluginInstance = nullptr;
static const float SR = 48000.f;
static int fails = 0;
static void check(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; }
static float pitch(const std::vector<float>& x, size_t a, size_t b) {
	double first = -1, last = -1; int n = 0;
	for (size_t i = a + 1; i < b && i < x.size(); i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) { double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]); if (first < 0) first = t; last = t; n++; }
	return n > 1 ? (float)((n - 1) * SR / (last - first)) : 0.f;
}
struct Rig {
	Pngbl m; Module::ProcessArgs a;
	Rig() { a.sampleRate = SR; a.sampleTime = 1.f / SR; a.frame = 0; for (auto& i : m.inputs) i.channels = 1; for (int i = 0; i < 4; i++) m.process(a); }
	std::vector<float> run(float s, int out, float pingAt = -1.f) {
		std::vector<float> y;
		for (int i = 0, n = (int)(s * SR); i < n; i++) {
			m.inputs[Pngbl::PING_INPUT].setVoltage(pingAt >= 0 && i >= (int)(pingAt * SR) && i < (int)(pingAt * SR) + 240 ? 5.f : 0.f);
			m.process(a);
			y.push_back(m.outputs[out].getVoltage());
		}
		return y;
	}
};
int main() {
	rack::random::init();
	float lo = Pngbl::bias(0.f, 0.f, 0.5f) * 3.07e7f, mid = Pngbl::bias(0.5f, 0.f, 0.5f) * 3.07e7f, hi = Pngbl::bias(1.f, 0.f, 0.5f) * 3.07e7f;
	printf("  KHZ: %.1f Hz, %.0f Hz, %.0f Hz across its travel\n", lo, mid, hi);
	check(mid > 250.f && mid < 420.f && lo < 5.f && hi > 20000.f, "KHZ spans ~2 Hz to past 20 kHz round ~330 Hz");
	check(Pngbl::bias(0.5f, 3.f, 0.5f) == Pngbl::bias(0.5f, 0.f, 0.5f), "the CV attenuverter's centre passes nothing");
	printf("  1 V of CV at full attenuverter: x%.2f\n", Pngbl::bias(0.5f, 1.f, 1.f) / Pngbl::bias(0.5f, 0.f, 0.5f));
	{
		Rig r; r.m.params[Pngbl::RES_PARAM].setValue(0.85f);
		auto y = r.run(1.f, Pngbl::BAND_OUTPUT, 0.1f);
		float pk = 0, late = 0;
		for (size_t i = 4800; i < 9600; i++) pk = std::max(pk, std::fabs(y[i]));
		for (size_t i = 38400; i < 48000; i++) late = std::max(late, std::fabs(y[i]));
		float f = pitch(y, 4900, 14000);
		printf("  ping at RES 0.85: rings at %.0f Hz, peak %.2f V, %.4f V 700 ms later\n", f, pk, late);
		check(pk > 0.5f && late < 0.05f * pk, "a ping rings the filter and it dies away");
		check(std::fabs(f - mid) < 0.15f * mid, "at the filter's frequency");
	}
	{
		Rig r; r.m.params[Pngbl::RES_PARAM].setValue(0.97f);
		auto y = r.run(3.f, Pngbl::BAND_OUTPUT);
		float pk = 0; for (size_t i = 96000; i < y.size(); i++) pk = std::max(pk, std::fabs(y[i]));
		printf("  RES 0.97: self-oscillates at %.0f Hz, BP peak %.2f V\n", pitch(y, 96000, y.size()), pk);
		check(pk > 4.f && pk < 8.f, "it self-oscillates, the zeners holding it near 12 Vpp");
	}
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
