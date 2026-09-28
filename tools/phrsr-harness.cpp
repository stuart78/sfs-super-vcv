// Drives the REAL PHRSR -- src/phrsr.cpp compiled as-is against libRack -- and
// checks it against what its firmware does: the internal clock's rate, live
// recording, loop lengths set with STEPS, the external clock, the output
// stage, and the patch round trip.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/phrsr-harness.cpp -o /tmp/phrsr-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/phrsr-harness
#include "../src/phrsr.cpp"
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
	Phrsr m;
	Module::ProcessArgs args;
	Rig() {
		args.sampleRate = SR; args.sampleTime = 1.f / SR; args.frame = 0;
		for (auto& i : m.inputs) i.channels = 1;
		for (auto& o : m.outputs) o.channels = 1;
	}
	void set(int p, float v) { m.params[p].setValue(v); }
	void run(float s) { for (int i = 0, n = (int)(s * SR); i < n; i++) { m.process(args); args.frame++; } }
	// one external clock pulse: 5 ms high, then low for the rest of `period`
	void pulse(float period) {
		m.inputs[Phrsr::CLK_INPUT].setVoltage(5.f); run(0.005f);
		m.inputs[Phrsr::CLK_INPUT].setVoltage(0.f); run(period - 0.005f);
	}
	float a() { return m.outputs[Phrsr::A_OUTPUT].getVoltage(); }
	float b() { return m.outputs[Phrsr::B_OUTPUT].getVoltage(); }
};

static float counts_to_v(int c) { return 2.f * 3.3f * c / 4095.f; }

int main() {
	rack::random::init();

	printf("== the pot smoothing truncates, as the firmware's uint16 does ==\n");
	{
		Rig r;
		r.set(Phrsr::RATE_PARAM, 0.5f);            // ADC 2048
		r.run(2.f);
		int up = r.m.fw.rate_pot_val;
		r.set(Phrsr::RATE_PARAM, 1.f); r.run(2.f);
		r.set(Phrsr::RATE_PARAM, 0.5f); r.run(2.f);
		int down = r.m.fw.rate_pot_val;
		printf("  rising to 2048 settles at %d, falling to it settles at %d\n", up, down);
		check(up >= 1999 && up < 2048, "a rising pot stalls within 49 counts below");
		check(down == 2048, "a falling pot reaches its value");
	}

	printf("== the internal clock: 2^28 of phase per step at pow(RATE, 1.9) + 16000 a tick ==\n");
	for (float k : {0.25f, 0.6f, 1.0f}) {
		Rig r;
		r.set(Phrsr::RATE_PARAM, k);
		r.run(3.f);                                 // let the pot settle
		double inc = std::pow((double)r.m.fw.rate_pot_val, 1.9) + 16000.0;
		double want = PhrsrFirmware::FS * inc / 268435456.0;
		// count rising edges of CLK out over a window
		int edges = 0; bool last = false;
		float T = std::max(4.f, (float)(40.0 / want));
		for (int i = 0, n = (int)(T * SR); i < n; i++) {
			r.m.process(r.args);
			bool hi = r.m.outputs[Phrsr::CLK_OUTPUT].getVoltage() > 3.f;
			if (hi && !last) edges++;
			last = hi;
		}
		double got = edges / T;
		printf("  RATE %.2f: %.3f steps/s, firmware says %.3f\n", k, got, want);
		check(std::fabs(got - want) <= std::max(1.0 / T, want * 0.03), "step rate matches the firmware's");
	}
	{
		double slowest = PhrsrFirmware::FS * 16000.0 / 268435456.0;
		printf("  (slowest step: every %.1f s; fastest: %.1f Hz)\n", 1.0 / slowest,
		       PhrsrFirmware::FS * (std::pow(4095.0, 1.9) + 16000) / 268435456.0);
	}

	printf("== recording: REC on writes DC at every step ==\n");
	{
		Rig r;
		r.set(Phrsr::RATE_PARAM, 0.f);              // internal clock at its slowest: the external one drives
		std::vector<int> want(16);
		r.set(Phrsr::REC_A_PARAM, 1.f);
		for (int s = 0; s < 16; s++) {
			float dc = (s + 1) / 17.f;
			r.set(Phrsr::DC_PARAM, dc);
			r.run(0.3f);                            // DC settles (the smoothing's tau is ~35 ms)
			r.pulse(0.1f);
			want[r.m.fw.seq_a_index] = r.m.fw.seq_a[r.m.fw.seq_a_index];
		}
		r.set(Phrsr::REC_A_PARAM, 0.f);
		bool distinct = true;
		for (int i = 1; i < 16; i++) distinct &= r.m.fw.seq_a[i] != r.m.fw.seq_a[i - 1];
		check(distinct, "sixteen steps recorded, each its own value");
		bool bUntouched = true;
		for (int i = 0; i < 16; i++) bUntouched &= r.m.fw.seq_b[i] == 0;
		check(bUntouched, "B is untouched while only A records");
		// play back: every step's output is its recorded value through the x2 stage
		bool match = true;
		for (int s = 0; s < 16; s++) {
			r.pulse(0.05f);
			int idx = r.m.fw.seq_a_index;
			match &= std::fabs(r.a() - counts_to_v(r.m.fw.seq_a[idx])) < 0.01f;
		}
		check(match, "A plays its steps back at 2 x 3.3 V / 4095 per count");
	}

	printf("== STEPS + REC sets a loop, one step per step held ==\n");
	{
		Rig r;
		r.set(Phrsr::RATE_PARAM, 0.f);
		// a full 16-step ramp to loop over
		r.set(Phrsr::REC_A_PARAM, 1.f);
		for (int s = 0; s < 16; s++) { r.set(Phrsr::DC_PARAM, (s + 1) / 17.f); r.run(0.3f); r.pulse(0.1f); }
		r.set(Phrsr::REC_A_PARAM, 0.f); r.run(0.05f);
		int start = r.m.fw.seq_a_index;
		// STEPS on, REC on, 3 more steps, REC off, STEPS off: a 4-step loop from here
		r.set(Phrsr::STEPS_PARAM, 1.f); r.run(0.05f);
		r.set(Phrsr::REC_A_PARAM, 1.f); r.run(0.05f);
		for (int s = 0; s < 3; s++) r.pulse(0.1f);
		r.set(Phrsr::REC_A_PARAM, 0.f); r.run(0.05f);
		r.set(Phrsr::STEPS_PARAM, 0.f); r.run(0.05f);
		printf("  loop length %d from step %d\n", r.m.fw.seq_a_length, r.m.fw.seq_a_start);
		check(r.m.fw.seq_a_length == 4 && r.m.fw.seq_a_start == start, "held for 3 steps after the press: a 4-step loop");
		std::vector<int> seen;
		for (int s = 0; s < 12; s++) { r.pulse(0.05f); seen.push_back(r.m.fw.seq_a_index); }
		bool loops = true;
		for (int s = 4; s < 12; s++) loops &= seen[s] == seen[s - 4];
		std::string path;
		for (int v : seen) path += std::to_string(v) + " ";
		printf("  steps visited: %s\n", path.c_str());
		check(loops, "A cycles through those 4 steps");
		check(r.m.fw.seq_b_length == 16, "B keeps its full length");
		bool noWrite = true;
		for (int i = 0; i < 16; i++) noWrite &= r.m.fw.seq_a[i] != 0;
		check(noWrite, "setting the loop did not record over it");
	}

	printf("== the external clock, and the output stage ==\n");
	{
		Rig r;
		r.set(Phrsr::RATE_PARAM, 0.f);
		r.set(Phrsr::REC_B_PARAM, 1.f);
		r.set(Phrsr::DC_PARAM, 1.f); r.run(0.5f);
		int before = r.m.fw.seq_b_index;
		r.pulse(0.1f);
		check(r.m.fw.seq_b_index == ((before + 1) & 15), "a CLK rising edge steps the sequence");
		r.set(Phrsr::REC_B_PARAM, 0.f);
		// the output stage is 1 + 1/(1 + s * 100us): a step jumps halfway at once
		r.m.inputs[Phrsr::CLK_INPUT].setVoltage(0.f);
		r.run(0.05f);
		float settled = r.b();
		int n = 0;
		// find the recorded step: one more step lands on 0 V (B was only written once)
		r.m.inputs[Phrsr::CLK_INPUT].setVoltage(5.f);
		float jump = 0.f;
		for (int i = 0; i < 480; i++) {
			r.m.process(r.args);
			float v = r.b();
			if (std::fabs(v - settled) > 0.01f && !n) { n = i; jump = v; }
		}
		float end = r.b();
		printf("  B: %.3f V -> first moved sample %.3f V -> %.3f V after 10 ms\n", settled, jump, end);
		check(std::fabs((jump - end) - (settled - end) * 0.5f) < 0.15f * std::fabs(settled - end) + 0.01f,
		      "a change jumps about halfway at once, then glides");
	}

	printf("== the patch keeps the phrases ==\n");
	{
		Rig r;
		r.set(Phrsr::RATE_PARAM, 0.f);
		r.set(Phrsr::REC_A_PARAM, 1.f);
		for (int s = 0; s < 5; s++) { r.set(Phrsr::DC_PARAM, s / 5.f + 0.1f); r.run(0.3f); r.pulse(0.1f); }
		r.set(Phrsr::REC_A_PARAM, 0.f);
		json_t* j = r.m.dataToJson();
		Rig r2;
		r2.m.dataFromJson(j);
		json_decref(j);
		bool same = true;
		for (int i = 0; i < 16; i++) same &= r2.m.fw.seq_a[i] == r.m.fw.seq_a[i];
		check(same, "sequence A survives dataToJson / dataFromJson");
	}

	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
