#pragma once
// Running a firmware at its own interrupt rate inside Rack's sample rate, for
// the effects (CHORUS, ROOM), whose audio passes THROUGH the firmware.
//
// The first version picked whichever host sample was current at each firmware
// tick and held the firmware's DAC value until the next one. Both are crude:
// the input had nothing above the firmware's Nyquist removed, and a staircase
// read at 48 kHz from a 32.9 kHz update lands at an irregular point in each
// step, which is timing jitter. Measured on ROOM with a 440 Hz sine, the
// firmware alone at its own rate was -53 dB THD+N (its 12-bit maths) and the
// module in Rack -32 dB: the conversion, not the reverb, was the noise.
//
// So, as the hardware's analog filtering would: a 4th-order Butterworth
// low-pass before the ADC, the input interpolated to each tick's exact time,
// the output interpolated between the last two ticks (one tick of delay), and
// the same low-pass after, as the DAC's reconstruction filter.
#include "plugin.hpp"

namespace super {

struct FirmwareRate {
	double fs = 48000.0;          // the firmware's tick rate
	double acc = 0.0;             // ticks since the last one, 0..1
	float hostSr = 0.f;
	float xPrev = 0.f;            // the previous host input, after the filter
	float yPrev = 0.f, yLast = 0.f;
	dsp::TBiquadFilter<float> inLp[2], outLp[2];

	void setRates(double firmwareFs, float sr) {
		fs = firmwareFs;
		if (sr == hostSr) return;
		hostSr = sr;
		// 0.45 of whichever Nyquist is lower
		float fc = (float)std::min(0.45 * fs, 0.45 * sr) / sr;
		const float q[2] = {0.5412f, 1.3066f};
		for (int i = 0; i < 2; i++) {
			inLp[i].setParameters(dsp::TBiquadFilter<float>::LOWPASS, fc, q[i], 1.f);
			outLp[i].setParameters(dsp::TBiquadFilter<float>::LOWPASS, fc, q[i], 1.f);
		}
	}

	// One host sample: `in` is the host-rate input; `tick(x)` runs one
	// firmware interrupt on input x (host-rate units) and returns its output.
	// Returns the host-rate output.
	template <typename Tick>
	float process(float in, Tick tick) {
		float x = inLp[1].process(inLp[0].process(in));
		double step = fs / hostSr;
		acc += step;
		while (acc >= 1.0) {
			acc -= 1.0;
			// this tick fell `acc` ticks before the current host sample
			float back = (float)(acc / step);
			float xi = x + (xPrev - x) * back;
			yPrev = yLast;
			yLast = tick(xi);
		}
		xPrev = x;
		float y = yPrev + (yLast - yPrev) * (float)acc;
		return outLp[1].process(outLp[0].process(y));
	}
};

}  // namespace super
