// Drives the REAL SCANNER -- src/scanner.cpp compiled as-is against libRack --
// and sweeps the scan voltage: four windows, each peaking a diode drop above
// its offset, at zero outside, in order; the knob's range; the attenuverter.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/scanner-harness.cpp -o /tmp/scanner-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/scanner-harness
#include "../src/scanner.cpp"
#include <cstdio>
#include <string>
rack::plugin::Plugin* pluginInstance = nullptr;

static int fails = 0;
static void check(bool ok, const std::string& what) {
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}

int main() {
	printf("== the windows, over the scan ==\n");
	float peakAt[SC_N] = {}, peak[SC_N] = {}, openAt[SC_N], closeAt[SC_N];
	for (int n = 0; n < SC_N; n++) { openAt[n] = -1; closeAt[n] = -1; }
	for (float s = 0.f; s <= 7.f; s += 0.001f)
		for (int n = 0; n < SC_N; n++) {
			float v = Scanner::window(n, s);
			if (v > peak[n]) { peak[n] = v; peakAt[n] = s; }
			if (v > 0.f && openAt[n] < 0) openAt[n] = s;
			if (v > 0.f) closeAt[n] = s;
		}
	for (int n = 0; n < SC_N; n++)
		printf("  window %d: opens %.2f V, peaks %.2f V at %.2f V, closes %.2f V\n", n + 1, openAt[n], peak[n], peakAt[n], closeAt[n]);
	bool ordered = true, tall = true, spaced = true;
	for (int n = 0; n < SC_N; n++) {
		tall &= peak[n] > 6.7f && peak[n] < 7.2f;
		spaced &= std::fabs(peakAt[n] - (n + 1.5f)) < 0.05f;
		if (n) ordered &= peakAt[n] > peakAt[n - 1];
	}
	check(ordered && spaced, "the peaks sit a diode drop above 1, 2, 3, 4 V, in order");
	check(tall, "each window peaks near 7 V (5 x 1.5 V, less D6's drop)");
	check(Scanner::window(0, 0.f) == 0.f && Scanner::window(3, 1.f) == 0.f, "outside its window a channel is exactly 0 V");
	check(Scanner::window(0, peakAt[0]) > 0.f && Scanner::window(1, peakAt[0]) > 0.f, "neighbours overlap: a crossfade, not a switch");

	printf("== the scan voltage ==\n");
	{
		float top = Scanner::scanVoltage(1.f, 0.f, 0.f, 0.5f);
		float cw = Scanner::scanVoltage(0.f, 0.f, 1.f, 1.f), mid = Scanner::scanVoltage(0.f, 0.f, 1.f, 0.5f), ccw = Scanner::scanVoltage(0.f, 0.f, 1.f, 0.f);
		printf("  knob fully up: %.2f V; 1 V at the attenuverter: CW %+.3f, centre %+.4f, CCW %+.3f\n", top, cw, mid, ccw);
		check(std::fabs(top - 5.774f) < 0.01f, "the knob alone scans 0 to 5.77 V: past the last window's peak");
		check(std::fabs(mid) < 1e-4f && cw > 1.1f && ccw < -1.1f, "the attenuverter is zero at the centre, +-1.155 at the ends");
		check(std::fabs(Scanner::scanVoltage(0.f, 1.f, 0.f, 0.5f) - 1.155f) < 0.001f, "the direct CV adds 1.155 V per volt");
	}
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
