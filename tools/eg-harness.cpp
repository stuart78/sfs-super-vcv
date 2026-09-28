// Drives the REAL EG -- src/eg.cpp compiled as-is against libRack -- and checks
// the cycle the schematic describes: the ~6 V peak, attack and decay times
// against the OTA/converter arithmetic, one-shot versus RE, EOC, the CV
// attenuverter, and that the times move the right way on every control.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/eg-harness.cpp -o /tmp/eg-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/eg-harness
#include "../src/eg.cpp"
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
	Eg m;
	Module::ProcessArgs args;
	Rig() {
		args.sampleRate = SR; args.sampleTime = 1.f / SR; args.frame = 0;
		for (auto& i : m.inputs) i.channels = 1;
		for (auto& o : m.outputs) o.channels = 1;
		// Rack's trigger detectors ignore an input already high on their first
		// sample, so start low, as a cable does
		for (int i = 0; i < 4; i++) step();
	}
	void step(float trig = 0.f) { m.inputs[Eg::TRIG_INPUT].setVoltage(trig); m.process(args); }
	void run(float s) { for (int i = 0, n = (int)(s * SR); i < n; i++) step(); }
	void fire() { for (int i = 0; i < 48; i++) step(5.f); }
	float out() { return m.outputs[Eg::OUT_OUTPUT].getVoltage(); }
	// attack: trigger to peak; decay: peak to below 0.12 V (EOC)
	void cycle(float& atk, float& dec, float& peak, float limit = 60.f) {
		int n = 0; peak = 0.f; int ipk = 0;
		fire();
		for (n = 0; n < (int)(limit * SR); n++) {
			step();
			float v = out();
			if (v > peak) { peak = v; ipk = n; }
			if (n > ipk + 10 && v < 0.119f) break;
		}
		atk = (ipk + 48) / SR; dec = (n - ipk) / SR;
	}
};

int main() {
	rack::random::init();

	printf("== one cycle ==\n");
	{
		Rig r;
		r.m.params[Eg::ATTACK_PARAM].setValue(0.3f);
		r.m.params[Eg::DECAY_PARAM].setValue(0.5f);
		float a, d, pk;
		r.cycle(a, d, pk);
		printf("  peak %.2f V, attack %.2f ms, decay (to 0.12 V) %.1f ms\n", pk, a * 1e3, d * 1e3);
		check(pk > 5.9f && pk < 6.3f, "the peak is the 4013's reset threshold, ~6 V");
		// decay: near-exponential from 6 V with tau = 47n / (0.04255 Iabc)
		float iabc = Eg::biasCurrent(Eg::baseVoltage(false, 0.3f, 0.5f, 0.f));
		float tau = 47e-9f / (0.04255f * iabc);
		float want = tau * std::log(6.f / 0.119f);
		printf("  decay: Iabc %.2f uA, tau %.1f ms: to 0.12 V in %.1f ms by the formula\n", iabc * 1e6, tau * 1e3, want * 1e3);
		check(std::fabs(d - want) < 0.08f * want, "the decay follows the OTA's small-signal time constant");
	}

	printf("== ranges ==\n");
	{
		float a0, d0, a1, d1, pk;
		{ Rig r; r.m.params[Eg::ATTACK_PARAM].setValue(0.f); r.m.params[Eg::DECAY_PARAM].setValue(0.f); r.cycle(a0, d0, pk); }
		{ Rig r; r.m.params[Eg::ATTACK_PARAM].setValue(0.5f); r.m.params[Eg::DECAY_PARAM].setValue(1.f); r.cycle(a1, d1, pk, 120.f); }
		printf("  fastest: attack %.2f ms, decay %.1f ms;  ATTACK 0.5 %.1f ms, DECAY max %.1f s\n", a0 * 1e3, d0 * 1e3, a1 * 1e3, d1);
		check(a0 < 0.002f, "attack's fast end is about a millisecond");
		check(d0 < 0.02f && d1 > 20.f, "decay spans milliseconds to tens of seconds");
		float slow = Eg::biasCurrent(Eg::baseVoltage(true, 1.f, 0.f, 0.f));
		printf("  ATTACK fully up: Iabc %.2g A, which is minutes (the +1.39 V offset in the converter)\n", slow);
		check(slow < 1e-9f, "as drawn, the top of ATTACK is extremely slow");
	}

	printf("== one-shot and RE ==\n");
	{
		Rig r;
		r.m.params[Eg::DECAY_PARAM].setValue(0.6f);
		r.fire(); r.run(0.01f);
		float mid = 0; int blocked = 0;
		r.run(0.05f); mid = r.out();
		r.fire(); r.run(0.001f);
		blocked = r.out() <= mid + 0.01f;
		check(blocked, "one-shot: a trigger during the decay is ignored");
		// press RE, then a trigger well into the decay restarts the attack
		r.m.params[Eg::RE_PARAM].setValue(1.f); r.step(); r.m.params[Eg::RE_PARAM].setValue(0.f); r.step();
		check(r.m.re, "the RE button toggles re-trigger on");
		while (r.out() > 2.5f) r.step();          // halfway down
		float before = r.out();
		r.fire(); r.run(0.02f);        // this ATTACK is ~40 ms to the top
		check(r.out() > before + 0.5f, "re-trigger: a trigger mid-decay attacks again from where it is");
		check(r.m.lights[Eg::RE_LIGHT].getBrightness() > 0.5f, "and the RE window is lit");
	}

	printf("== EOC ==\n");
	{
		Rig r;
		r.m.params[Eg::DECAY_PARAM].setValue(0.3f);
		r.fire();
		int high = 0, first = -1;
		for (int n = 0; n < (int)(2.f * SR); n++) {
			r.step();
			if (r.m.outputs[Eg::EOC_OUTPUT].getVoltage() > 4.f) { high++; if (first < 0) first = n; }
		}
		printf("  EOC high for %.2f ms, starting %.1f ms after the trigger\n", high / SR * 1e3, first / SR * 1e3);
		check(std::fabs(high / SR - 0.005f) < 0.0005f, "one ~5 ms pulse at the end of the cycle");
	}

	printf("== CV: an attenuverter on the length ==\n");
	{
		float c = Eg::cvBuffer(3.f, 0.5f), cw = Eg::cvBuffer(3.f, 1.f), ccw = Eg::cvBuffer(3.f, 0.f);
		printf("  3 V in: CW %.2f V, centre %.3f V, CCW %.2f V out of U5.2\n", cw, c, ccw);
		check(std::fabs(c) < 1e-3f && std::fabs(cw - 3.f) < 1e-3f && std::fabs(ccw + 3.f) < 1e-3f, "+V at CW, nothing at centre, -V at CCW");
		float i0 = Eg::biasCurrent(Eg::baseVoltage(false, 0.f, 0.5f, 0.f));
		float i1 = Eg::biasCurrent(Eg::baseVoltage(false, 0.f, 0.5f, 1.f));
		printf("  +1 V through it (CW): x%.1f faster\n", i1 / i0);
		check(i1 / i0 > 5.f, "positive CV shortens the envelope, strongly");
	}

	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
