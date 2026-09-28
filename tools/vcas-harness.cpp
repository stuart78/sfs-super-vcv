// Drives the REAL VCAs -- src/vcas.cpp compiled as-is against libRack -- and
// checks the schematic's behaviour: the linear gain law and its unity ceiling,
// the input attenuverter, and the chain that sums unpatched outputs onward.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/vcas-harness.cpp -o /tmp/vcas-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/vcas-harness
#include "../src/vcas.cpp"
#include <cstdio>
#include <string>
rack::plugin::Plugin* pluginInstance = nullptr;

static int fails = 0;
static void check(bool ok, const std::string& what) {
	printf("  %s  %s\n", ok ? "ok  " : "FAIL", what.c_str());
	if (!ok) fails++;
}
struct Rig {
	Vcas m;
	Module::ProcessArgs args;
	Rig() { args.sampleRate = 48000; args.sampleTime = 1.f / 48000; args.frame = 0; for (auto& i : m.inputs) i.channels = 1; }
	void patch(int n, bool on) { m.outputs[Vcas::OUT_OUTPUT + n].channels = on ? 1 : 0; }
	float out(int n) { m.process(args); return m.outputs[Vcas::OUT_OUTPUT + n].getVoltage(); }
};

int main() {
	printf("== the gain law ==\n");
	{
		Rig r; r.patch(0, true);
		r.m.inputs[Vcas::IN_INPUT].setVoltage(4.f);
		float g[5];
		for (int i = 0; i <= 4; i++) { r.m.params[Vcas::LEVEL_PARAM].setValue(i / 4.f); g[i] = r.out(0) / 4.f; }
		printf("  slider 0, 1/4, 1/2, 3/4, 1: gain %.3f %.3f %.3f %.3f %.3f\n", g[0], g[1], g[2], g[3], g[4]);
		check(g[0] == 0.f, "the slider at the bottom is fully off (the 10M's offset)");
		check(std::fabs(g[2] - 0.49f) < 0.002f && std::fabs(g[4] - 0.99f) < 0.002f, "gain is linear in the slider: (slider + CV) / 5 V");
		r.m.params[Vcas::LEVEL_PARAM].setValue(1.f);
		r.m.inputs[Vcas::CV_INPUT].setVoltage(5.f);
		float top = r.out(0) / 4.f;
		r.m.params[Vcas::LEVEL_PARAM].setValue(0.f);
		r.m.inputs[Vcas::CV_INPUT].setVoltage(2.5f);
		float cv = r.out(0) / 4.f;
		printf("  slider up + 5 V CV: %.3f;  2.5 V CV alone: %.3f\n", top, cv);
		check(top == 1.f, "gain stops at unity (D8 holds the control voltage above 0)");
		check(std::fabs(cv - 0.49f) < 0.002f, "CV adds linearly: 2.5 V is half");
	}
	printf("== the input attenuverter ==\n");
	{
		float cw = Vcas::attenuvert(3.f, 1.f), mid = Vcas::attenuvert(3.f, 0.5f), ccw = Vcas::attenuvert(3.f, 0.f);
		printf("  3 V in: CW %.3f, centre %.4f, CCW %.3f\n", cw, mid, ccw);
		check(std::fabs(cw - 3.f) < 1e-4f && std::fabs(mid) < 1e-4f && std::fabs(ccw + 3.f) < 1e-4f, "+IN, nothing, -IN");
	}
	printf("== the chain: unpatched outputs sum onward ==\n");
	{
		Rig r;
		for (int n = 0; n < 4; n++) { r.m.params[Vcas::LEVEL_PARAM + n].setValue(1.f); r.m.inputs[Vcas::IN_INPUT + n].setVoltage(0.5f * (1 + n)); }   // sums to 5 V: clear of the rails
		r.patch(3, true);
		float all = r.out(3);
		r.patch(1, true);
		float split = r.out(3), two = r.out(1);
		printf("  OUT 4 alone patched: %.3f V; with OUT 2 patched too: OUT 2 %.3f V, OUT 4 %.3f V\n", all, two, split);
		check(std::fabs(all - 0.99f * 0.5f * (1 + 2 + 3 + 4)) < 0.01f, "four unpatched-before channels mix at OUT 4");
		check(std::fabs(two - 0.99f * 0.5f * (1 + 2)) < 0.01f && std::fabs(split - 0.99f * 0.5f * (3 + 4)) < 0.01f,
		      "patching OUT 2 takes 1+2 there and leaves 3+4 at OUT 4");
	}
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
