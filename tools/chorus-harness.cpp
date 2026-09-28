// Drives the REAL CHORUS -- src/chorus.cpp compiled as-is against libRack -- and
// checks it against its firmware: the delay range (300 ms at full DELAY), the
// inverted feedback, the LFO depth shrinking with the delay, BAL, and the
// short expo table's zero at the very top of RATE.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/chorus-harness.cpp -o /tmp/chorus-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/chorus-harness
#include "../src/chorus.cpp"
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
	Chorus m;
	Module::ProcessArgs args;
	Rig() { args.sampleRate = SR; args.sampleTime = 1.f / SR; args.frame = 0; for (auto& i : m.inputs) i.channels = 1; }
	void set(int p, float v) { m.params[p].setValue(v); }
	std::vector<float> run(float s, float impulseAt = -1.f) {
		std::vector<float> y;
		for (int i = 0, n = (int)(s * SR); i < n; i++) {
			m.inputs[Chorus::IN_INPUT].setVoltage(i >= (int)(impulseAt * SR) && i < (int)(impulseAt * SR) + 48 && impulseAt >= 0 ? 4.f : 0.f);
			m.process(args);
			y.push_back(m.outputs[Chorus::OUT_OUTPUT].getVoltage());
		}
		return y;
	}
};
// the echoes: onset times (the wet path is AC-coupled, so find the first swing)
static std::vector<std::pair<float, float>> echoes(const std::vector<float>& y, float from) {
	std::vector<std::pair<float, float>> e;
	int gap = 0;
	for (size_t i = (size_t)(from * SR); i < y.size(); i++) {
		if (std::fabs(y[i]) > 0.3f && gap > (int)(0.02f * SR)) { e.push_back({i / SR, y[i]}); gap = 0; }
		gap = std::fabs(y[i]) > 0.3f ? 0 : gap + 1;
	}
	return e;
}

int main() {
	rack::random::init();
	printf("== delay range ==\n");
	{
		Rig r;
		r.set(Chorus::BAL_PARAM, 1.f); r.set(Chorus::AMT_PARAM, 0.f); r.set(Chorus::DELAY_PARAM, 1.f);
		r.run(1.f);
		auto y = r.run(1.f, 0.1f);
		auto e = echoes(y, 0.1f);
		float d = e.empty() ? 0.f : e[0].first - 0.1f;
		printf("  DELAY full: echo after %.1f ms (15,000 samples at %.0f Hz = %.1f ms)\n", d * 1e3, ChorusFirmware::FS, 15000.0 / ChorusFirmware::FS * 1e3);
		check(std::fabs(d - 0.3f) < 0.01f, "the full delay is ~300 ms, as the product page says");
	}
	printf("== inverted feedback ==\n");
	{
		Rig r;
		r.set(Chorus::BAL_PARAM, 1.f); r.set(Chorus::AMT_PARAM, 0.f); r.set(Chorus::DELAY_PARAM, 0.3f); r.set(Chorus::FB_PARAM, 0.7f);
		r.run(1.f);
		auto y = r.run(1.5f, 0.05f);
		auto e = echoes(y, 0.05f);
		std::string s;
		for (size_t i = 0; i < e.size() && i < 4; i++) s += (e[i].second > 0 ? "+" : "-");
		printf("  echo polarities: %s\n", s.c_str());
		check(e.size() >= 3 && e[0].second * e[1].second < 0 && e[1].second * e[2].second < 0, "successive echoes alternate: the feedback is inverted");
	}
	printf("== the LFO ==\n");
	{
		Rig a; a.set(Chorus::AMT_PARAM, 0.8f); a.set(Chorus::DELAY_PARAM, 0.f); a.run(0.5f);
		Rig b; b.set(Chorus::AMT_PARAM, 0.8f); b.set(Chorus::DELAY_PARAM, 0.9f); b.run(0.5f);
		printf("  depth at DELAY 0: %.0f samples of swing; at DELAY 0.9: %.0f\n", a.m.fw.amt * (1.f - a.m.fw.delayFilt), b.m.fw.amt * (1.f - b.m.fw.delayFilt));
		check(b.m.fw.amt * (1.f - b.m.fw.delayFilt) < 0.2f * a.m.fw.amt * (1.f - a.m.fw.delayFilt), "the modulation depth shrinks as the delay grows");
		Rig t; t.set(Chorus::RATE_PARAM, 1.f); t.run(0.5f);
		Rig u; u.set(Chorus::RATE_PARAM, 0.99f); u.run(0.5f);
		printf("  RATE 0.99: %.2f; RATE 1.00: %.4f (nominal units)\n", u.m.fw.rate, t.m.fw.rate);
		check(t.m.fw.rate < 0.01f && u.m.fw.rate > 60.f, "the firmware's short table: the very top of RATE is zero");
	}
	printf("== BAL ==\n");
	{
		Rig r; r.set(Chorus::BAL_PARAM, 0.f);
		r.m.inputs[Chorus::IN_INPUT].setVoltage(2.5f);
		r.m.process(r.args);
		check(r.m.outputs[Chorus::OUT_OUTPUT].getVoltage() == 2.5f, "BAL fully dry passes the input untouched");
	}
	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
