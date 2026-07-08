#include "sdrjo/dsp/demod.hpp"
#include <cmath>

namespace sdrjo::dsp {

FmDemodulator::FmDemodulator(double sampleRateHz, double deviationHz)
{
    // Normalizza in modo che una deviazione di picco dia ±1.0 in uscita.
    gain_ = float(sampleRateHz / (2.0 * M_PI * deviationHz));
}

void FmDemodulator::process(const cfloat* in, size_t n, float* out)
{
    for (size_t i = 0; i < n; i++) {
        cfloat d = in[i] * std::conj(prev_);
        prev_ = in[i];
        out[i] = std::atan2(d.imag(), d.real()) * gain_;
    }
}

AmDemodulator::AmDemodulator(double dcAlpha) : dcAlpha_(dcAlpha) {}

void AmDemodulator::process(const cfloat* in, size_t n, float* out)
{
    for (size_t i = 0; i < n; i++) {
        float mag = std::abs(in[i]);
        dc_ = float(dcAlpha_) * dc_ + (1.0f - float(dcAlpha_)) * mag;
        out[i] = mag - dc_;
    }
}

Agc::Agc(float attack, float release, float targetLevel)
    : attack_(attack), release_(release), target_(targetLevel) {}

void Agc::process(float* samples, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float mag = std::fabs(samples[i]);
        float rate = (mag > envelope_) ? attack_ : release_;
        envelope_ += rate * (mag - envelope_);
        if (envelope_ < 1e-6f) envelope_ = 1e-6f;
        samples[i] *= target_ / envelope_;
    }
}

} // namespace sdrjo::dsp
