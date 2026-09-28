// Drives the REAL TVCA -- src/tvca.cpp compiled as-is against libRack -- and
// checks the schematic's behaviour: near-unity clean gain with DIST down and
// INIT up, linear gain in INIT + CV, and DIST pushing it into a tanh whose
// level INIT sets.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/tvca-harness.cpp -o /tmp/tvca-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/tvca-harness
#include "../src/tvca.cpp"
#include <cstdio>
#include <string>
rack::plugin::Plugin* pluginInstance = nullptr;

static int fails = 0;
static void check(bool ok, const std::string& what) {
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}

int main() {
	float g1 = Tvca::transfer(1.f, 0.f, 1.f, 0.f), g5 = Tvca::transfer(5.f, 0.f, 1.f, 0.f) / 5.f;
	printf("  DIST down, INIT up: 1 V -> %.3f V, 5 V -> %.3f V (gain %.3f)\n", g1, g5 * 5.f, g5);
	check(g1 > 0.8f && g1 < 0.95f, "about unity with DIST down and INIT up (10k x 500 uA x f / 2VT = 0.87)");
	// even at the bottom of DIST the divider leaves 45 mV per 5 V at the OTA, 0.87
	// of 2VT: a hot signal is compressed ~2 dB. That is the circuit, not a fault.
	check(g5 / g1 > 0.75f && g5 / g1 < 0.85f, "DIST down is clean at 1 V and gently compressed at 5 V (~2 dB)");
	float half = Tvca::transfer(1.f, 0.f, 0.5f, 0.f), cv = Tvca::transfer(1.f, 0.f, 0.f, 2.5f);
	printf("  INIT half: %.3f V; INIT down + 2.5 V CV: %.3f V; INIT down alone: %.3f V\n", half, cv, Tvca::transfer(1.f, 0.f, 0.f, 0.f));
	check(std::fabs(half - g1 / 2.f) < 1e-3f && std::fabs(cv - half) < 1e-3f, "gain is linear in INIT + CV");
	check(Tvca::transfer(1.f, 0.f, 0.f, 0.f) == 0.f && Tvca::transfer(1.f, 0.f, 0.f, -3.f) == 0.f, "closed at zero, and a negative sum stays closed (D4)");
	float d1 = Tvca::transfer(1.f, 1.f, 1.f, 0.f), d5 = Tvca::transfer(5.f, 1.f, 1.f, 0.f);
	printf("  DIST up: 1 V -> %.3f V, 5 V -> %.3f V\n", d1, d5);
	check(d5 < 5.01f && d5 > 4.9f && d1 > 4.f, "DIST up saturates toward 10k x Iabc = 5 V: a tanh");
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
