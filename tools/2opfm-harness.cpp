// Drives the REAL 2OPFM -- src/twoopfm.cpp compiled as-is against libRack -- and
// checks every firmware: that it speaks on a trigger, stays inside the 2OPFM's
// output swing, never goes NaN, and tracks 1V/oct where it claims to. With a
// directory argument it also writes one listening WAV per firmware.
//
//   clang++ -std=c++11 -O2 -DARCH_MAC -I ../Rack-SDK/include -I ../Rack-SDK/dep/include \
//     tools/2opfm-harness.cpp -o /tmp/2opfm-harness \
//     -L"/Applications/VCV Rack 2 Pro.app/Contents/Resources" -lRack
//   DYLD_LIBRARY_PATH="/Applications/VCV Rack 2 Pro.app/Contents/Resources" /tmp/2opfm-harness [wavdir]
#include "../src/twoopfm.cpp"
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
	TwoOp m;
	Module::ProcessArgs args;
	Rig(int mode) {
		args.sampleRate = SR;
		args.sampleTime = 1.f / SR;
		args.frame = 0;
		for (auto& i : m.inputs) i.channels = 1;
		m.pendingMode = mode;
	}
	void set(int p, float v) { m.params[p].setValue(v); }
	std::vector<float> run(float seconds, float trigEvery, float khzV = 0.f) {
		std::vector<float> out;
		int n = (int)(seconds * SR), every = (int)(trigEvery * SR);
		m.inputs[TwoOp::KHZ_INPUT].setVoltage(khzV);
		for (int i = 0; i < n; i++) {
			bool hi = every > 0 && (i % every) < 48;   // 1 ms trigger
			m.inputs[TwoOp::TRIG_INPUT].setVoltage(hi ? 10.f : 0.f);
			m.process(args);
			args.frame++;
			out.push_back(m.outputs[TwoOp::OUT_OUTPUT].getVoltage());
		}
		return out;
	}
};

// Frequency by rising zero crossings, interpolated, over [a, b).
static float pitch(const std::vector<float>& x, int a, int b) {
	double first = -1, last = -1; int n = 0;
	for (int i = a + 1; i < b; i++)
		if (x[i - 1] < 0.f && x[i] >= 0.f) {
			double t = i - 1 + x[i - 1] / (x[i - 1] - x[i]);
			if (first < 0) first = t;
			last = t; n++;
		}
	return n > 1 ? (float)((n - 1) * SR / (last - first)) : 0.f;
}

static void writeWav(const std::string& path, const std::vector<float>& x) {
	FILE* f = fopen(path.c_str(), "wb");
	if (!f) return;
	uint32_t n = x.size(), bytes = n * 2, sr = 48000, br = sr * 2, sz = 36 + bytes;
	uint16_t pcm = 1, ch = 1, ba = 2, bits = 16;
	fwrite("RIFF", 1, 4, f); fwrite(&sz, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
	uint32_t fmtLen = 16; fwrite(&fmtLen, 4, 1, f);
	fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
	fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
	fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
	for (float v : x) { int16_t s = (int16_t)clamp(v / 10.f * 32767.f, -32767.f, 32767.f); fwrite(&s, 2, 1, f); }
	fclose(f);
}

int main(int argc, char** argv) {
	rack::random::init();
	std::string wavDir = argc > 1 ? argv[1] : "";

	printf("== every firmware speaks, stays in range, stays finite ==\n");
	for (int mode = 0; mode < TWOOP_MODES; mode++) {
		Rig r(mode);
		auto x = r.run(4.f, 1.f);
		float peak = 0.f; double e = 0.0; bool finite = true;
		for (size_t i = 0; i < x.size(); i++) {
			if (!std::isfinite(x[i])) finite = false;
			peak = std::max(peak, std::fabs(x[i]));
			if (i >= 48000 && i < 48000 + 24000) e += x[i] * x[i];
		}
		float rms = std::sqrt(e / 24000.0);
		printf("  %-24s peak %5.2f V   rms(0.5 s after a trigger) %5.2f V\n", TWOOP_MODE_NAMES[mode], peak, rms);
		check(finite, std::string(TWOOP_MODE_NAMES[mode]) + ": finite");
		// the stock attack adds 0.1 a tick until it passes 0.95, so the
		// envelope peaks near 1.05 and so does the output -- the chip does this
		float lim = mode == MODE_STOCK ? 5.6f : 5.01f;
		check(peak <= lim, std::string(TWOOP_MODE_NAMES[mode]) + ": within the output swing");
		check(rms > 0.2f, std::string(TWOOP_MODE_NAMES[mode]) + ": audible after a trigger");
		if (!wavDir.empty()) {
			std::string name = TWOOP_MODE_SHORT[mode];
			for (char& c : name) if (c == '+') c = 'P';
			// a little melody: C, E-flat, G, C an octave up, one note a second
			std::vector<float> all;
			Rig w(mode);
			const float notes[4] = {0.f, 3.f / 12.f, 7.f / 12.f, 1.f};
			for (float v : notes) { auto part = w.run(1.f, 1.f, v - 1.f); all.insert(all.end(), part.begin(), part.end()); }
			// "twoop-" is what ~/code/2opfm-alt/host compares against; its own
			// renders are "2opfm-", so the two can share a directory
			writeWav(wavDir + "/twoop-" + name + ".wav", all);
		}
	}

	printf("== 1V/oct tracking with FM at zero (a plain sine) ==\n");
	for (int mode : {MODE_STOCK, MODE_PLUS}) {
		float f[3];
		for (int k = 0; k < 3; k++) {
			Rig r(mode);
			r.set(TwoOp::FM_PARAM, 0.f);
			r.set(TwoOp::DECAY_PARAM, 1.f);            // drone: hold the voice open
			r.set(TwoOp::KHZ_PARAM, mode == MODE_STOCK ? 0.7f : 0.35f);
			auto x = r.run(1.5f, 0.f, (float)k);
			f[k] = pitch(x, 48000, 72000);
		}
		float c1 = 1200.f * std::log2(f[1] / f[0] / 2.f), c2 = 1200.f * std::log2(f[2] / f[1] / 2.f);
		printf("  %-24s %.2f Hz, %.2f Hz, %.2f Hz  (octave errors %+.1f, %+.1f cents)\n",
		       TWOOP_MODE_NAMES[mode], f[0], f[1], f[2], c1, c2);
		// the stock firmware's pitch comes from an integer index into a
		// 59-steps-per-octave table: it can only be within ~10 cents
		float tol = mode == MODE_STOCK ? 25.f : 1.f;
		check(std::fabs(c1) < tol && std::fabs(c2) < tol, std::string(TWOOP_MODE_NAMES[mode]) + ": octaves in tune");
	}

	printf("== CZ at DCW 0 is a plain cosine at the knob pitch ==\n");
	for (int mode : {MODE_CZ}) {
		Rig r(mode);
		r.set(TwoOp::FM_PARAM, 0.f);
		r.set(TwoOp::DECAY_PARAM, 1.f);
		r.set(TwoOp::RATIO_PARAM, 0.f);
		auto x = r.run(1.f, 0.5f, 0.f);
		float f = pitch(x, 24000, 48000);
		printf("  %-24s %.2f Hz at C4\n", TWOOP_MODE_NAMES[mode], f);
		check(std::fabs(1200.f * std::log2(f / 261.63f)) < 2.f, "CZ: DCW 0 is a C4 cosine");
	}

	printf("\n%s (%d failure%s)\n", fails ? "FAILED" : "all passed", fails, fails == 1 ? "" : "s");
	return fails ? 1 : 0;
}
