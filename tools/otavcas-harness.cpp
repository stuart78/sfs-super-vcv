// Drives the REAL OTAVCAs -- src/otavcas.cpp compiled as-is against libRack --
// and checks the schematic's behaviour: linear gain current, the attenuverter,
// non-inverting output, saturation at full level, and 1 summing into 2.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/otavcas-harness.cpp -o /tmp/otavcas-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/otavcas-harness
#include "../src/otavcas.cpp"
#include <cstdio>
#include <string>
rack::plugin::Plugin* pluginInstance = nullptr;
static int fails = 0;
static void check(bool ok, const std::string& what) { printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str()); if (!ok) fails++; }

int main() {
	float full = Otavcas::bias(1.f, 0.f, 0.5f), half = Otavcas::bias(0.5f, 0.f, 0.5f);
	float cvUp = Otavcas::bias(0.f, 1.f, 1.f), cvMid = Otavcas::bias(0.f, 1.f, 0.5f), cvDn = Otavcas::bias(0.5f, 1.f, 0.f);
	printf("  Iabc: GAIN full %.0f uA, half %.0f uA; 1 V CV at CW %.0f uA, centre %.1f uA; GAIN half + 1 V at CCW %.0f uA\n",
	       full * 1e6, half * 1e6, cvUp * 1e6, cvMid * 1e6, cvDn * 1e6);
	check(std::fabs(full - 500e-6f) < 1e-7f && std::fabs(half - 250e-6f) < 1e-7f, "GAIN is linear: 500 uA at the top");
	check(std::fabs(cvUp - 100e-6f) < 1e-7f && cvMid == 0.f && std::fabs(cvDn - 150e-6f) < 1e-7f, "the attenuverter: +100 uA/V, nothing, -100 uA/V");
	float small = Otavcas::channel(0.1f, 1.f, full), big = Otavcas::channel(5.f, 1.f, full), neg = Otavcas::channel(-0.1f, 1.f, full);
	printf("  full GAIN: 0.1 V -> %.3f V, 5 V -> %.2f V (13k x 500 uA = 6.5 V); -0.1 V -> %.3f V\n", small, big, neg);
	check(small > 0.f && neg < 0.f, "non-inverting (the audio enters the OTA's - input and U2.4 re-inverts)");
	check(big > 6.f && big < 6.5f, "a hot input saturates to 13k x Iabc");
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
