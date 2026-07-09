#include "sdrjo/dsp/wfm_stereo.hpp"

#include <cmath>

namespace sdrjo::dsp {

WfmStereoDemodulator::WfmStereoDemodulator(double iqRate, double audioRate)
    : iqRate_(iqRate),
      decim_(unsigned(iqRate / audioRate)),
      fm_(iqRate, 75000.0),
      pilotLpf_(designLowPass(iqRate, 800.0, 301)),
      audioLpf_(designLowPass(iqRate, 15000.0, 101), unsigned(iqRate / audioRate)),
      phaseInc19_(2.0 * M_PI * 19000.0 / iqRate)
{
    deemAlpha_ = float(1.0 - std::exp(-1.0 / (audioRate * 50e-6)));
}

size_t WfmStereoDemodulator::process(const cfloat* iq, size_t n,
                                     std::vector<float>& left,
                                     std::vector<float>& right)
{
    mpx_.resize(n);
    fm_.process(iq, n, mpx_.data());

    // Pilota: mix a -19 kHz e passa-basso stretto -> fasore lento z.
    pilotIn_.resize(n);
    pilotOut_.resize(n);
    sumDiff_.resize(n);
    double ph = phase19_;
    for (size_t i = 0; i < n; i++) {
        pilotIn_[i] = mpx_[i] * cfloat(float(std::cos(-ph)), float(std::sin(-ph)));
        ph += phaseInc19_;
        if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
    pilotLpf_.process(pilotIn_.data(), n, pilotOut_.data());

    // Somma (L+R) nel canale reale, differenza (L-R) in quello immaginario:
    // un solo filtro decimante li porta entrambi a banda audio.
    ph = phase19_;
    for (size_t i = 0; i < n; i++) {
        cfloat z = pilotOut_[i];
        float mag = std::abs(z);
        pilotLevel_ += 0.0005f * (mag - pilotLevel_);

        float diff = 0.0f;
        if (mag > 1e-4f) {
            // Sottoportante ricostruita: cos(2*fase del pilota).
            cfloat u = z / mag;
            cfloat u2 = u * u;
            float c38 = float(u2.real() * std::cos(2.0 * ph) -
                              u2.imag() * std::sin(2.0 * ph));
            diff = 2.0f * mpx_[i] * c38;
        }
        sumDiff_[i] = cfloat(mpx_[i], diff);
        ph += phaseInc19_;
        if (ph > 2.0 * M_PI) ph -= 2.0 * M_PI;
    }
    phase19_ = ph;

    audio_.resize(n / decim_ + 2);
    size_t m = audioLpf_.process(sumDiff_.data(), n, audio_.data());

    bool stereo = stereoLocked();
    for (size_t i = 0; i < m; i++) {
        float sum = audio_[i].real();
        float diff = stereo ? audio_[i].imag() : 0.0f;
        float l = sum + diff;
        float r = sum - diff;
        // Deenfasi 50 us.
        deemL_ += deemAlpha_ * (l - deemL_);
        deemR_ += deemAlpha_ * (r - deemR_);
        left.push_back(deemL_);
        right.push_back(deemR_);
    }
    return m;
}

} // namespace sdrjo::dsp
