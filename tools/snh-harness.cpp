// Drives the REAL S&H -- src/snh.cpp compiled as-is against libRack -- and
// checks the schematic's behaviour: sample on the rising edge and hold; the
// chain's normalling (second on the falling edge, third just after it).
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/snh-harness.cpp -o /tmp/snh-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/snh-harness
#include "../src/snh.cpp"
#include <cstdio>
#include <string>
rack::plugin::Plugin* pluginInstance = nullptr;
static const float SR = 48000.f;
static int fails = 0;
static void check(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; }

int main() {
	Snh m;
	Module::ProcessArgs a; a.sampleRate = SR; a.sampleTime = 1.f / SR; a.frame = 0;
	m.inputs[Snh::IN_INPUT].channels = 1;
	m.inputs[Snh::TRIG_INPUT].channels = 1;
	// a slow ramp in; one trigger, 20 ms long, at 100 ms
	int n = (int)(0.3f * SR), t0 = (int)(0.1f * SR), t1 = (int)(0.12f * SR);
	float at1 = 0, at2 = 0, at3 = 0; int e1 = -1, e2 = -1, e3 = -1;
	float o[3] = {0, 0, 0};
	for (int i = 0; i < n; i++) {
		float ramp = i / SR * 20.f;                 // 20 V/s
		m.inputs[Snh::IN_INPUT].setVoltage(ramp);
		m.inputs[Snh::TRIG_INPUT].setVoltage(i >= t0 && i < t1 ? 5.f : 0.f);
		m.process(a);
		float v[3] = {m.outputs[Snh::OUT_OUTPUT + 0].getVoltage(), m.outputs[Snh::OUT_OUTPUT + 1].getVoltage(), m.outputs[Snh::OUT_OUTPUT + 2].getVoltage()};
		if (e1 < 0 && v[0] != o[0]) { e1 = i; at1 = ramp; }
		if (e2 < 0 && v[1] != o[1]) { e2 = i; at2 = ramp; }
		if (e3 < 0 && v[2] != o[2]) { e3 = i; at3 = ramp; }
		for (int k = 0; k < 3; k++) o[k] = v[k];
	}
	printf("  first samples at %.2f ms, second %.2f ms, third %.2f ms (trigger rises at 100, falls at 120)\n", e1 / SR * 1e3, e2 / SR * 1e3, e3 / SR * 1e3);
	printf("  held: %.3f, %.3f, %.3f V (the ramp was %.2f V at the rise)\n", o[0], o[1], o[2], t0 / SR * 20.f);
	check(std::fabs(e1 / SR - 0.1f) < 0.001f, "the first channel samples on the rising edge");
	check(std::fabs(o[0] - 2.0f) < 0.02f, "and holds the input from then (~0.6 ms of tracking)");
	check(std::fabs(e2 / SR - 0.12f) < 0.001f, "the second samples on the falling edge, normalled");
	check(std::fabs(o[1] - o[0]) < 1e-4f, "copying the first's output (its input is normalled from it)");
	check(e3 > e2 && (e3 - e2) / SR < 0.002f && std::fabs(o[2] - o[1]) < 1e-4f, "the third follows just after the second");
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
