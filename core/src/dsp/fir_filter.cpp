#include "sdrjo/dsp/fir_filter.hpp"
#include <cassert>
#include <cmath>

namespace sdrjo::dsp {

std::vector<float> designLowPass(double sampleRateHz, double cutoffHz, int numTaps)
{
    assert(numTaps % 2 == 1 && "numTaps deve essere dispari");
    std::vector<float> taps(numTaps);
    const double fc = cutoffHz / sampleRateHz; // frequenza normalizzata
    const int m = numTaps - 1;
    double sum = 0.0;
    for (int i = 0; i < numTaps; i++) {
        double x = i - m / 2.0;
        double sinc = (x == 0.0) ? 2.0 * fc
                                 : std::sin(2.0 * M_PI * fc * x) / (M_PI * x);
        double hamming = 0.54 - 0.46 * std::cos(2.0 * M_PI * i / m);
        taps[i] = float(sinc * hamming);
        sum += taps[i];
    }
    // Normalizza a guadagno unitario in DC.
    for (auto& t : taps) t = float(t / sum);
    return taps;
}

// --------------------------------------------------------------------------

FirFilter::FirFilter(std::vector<float> taps)
    : taps_(std::move(taps)), history_(taps_.size(), cfloat(0, 0)) {}

void FirFilter::process(const cfloat* in, size_t n, cfloat* out)
{
    const size_t nt = taps_.size();
    for (size_t i = 0; i < n; i++) {
        history_[histPos_] = in[i];
        cfloat acc(0, 0);
        size_t idx = histPos_;
        for (size_t t = 0; t < nt; t++) {
            acc += history_[idx] * taps_[t];
            idx = (idx == 0) ? nt - 1 : idx - 1;
        }
        out[i] = acc;
        histPos_ = (histPos_ + 1) % nt;
    }
}

// --------------------------------------------------------------------------

FirDecimator::FirDecimator(std::vector<float> taps, unsigned decimation)
    : taps_(std::move(taps)), history_(taps_.size() - 1, cfloat(0, 0)),
      decim_(decimation)
{
    assert(decimation >= 1);
}

size_t FirDecimator::process(const cfloat* in, size_t n, cfloat* out)
{
    const size_t nt = taps_.size();
    size_t written = 0;

    for (size_t i = 0; i < n; i++) {
        if (phase_ == 0) {
            // Convoluzione centrata sul campione corrente, guardando indietro:
            // i campioni piu' vecchi di 'in' stanno in history_.
            cfloat acc(0, 0);
            for (size_t t = 0; t < nt; t++) {
                ptrdiff_t src = ptrdiff_t(i) - ptrdiff_t(t);
                cfloat s = (src >= 0) ? in[src]
                                      : history_[history_.size() + src];
                acc += s * taps_[t];
            }
            out[written++] = acc;
        }
        phase_ = (phase_ + 1) % decim_;
    }

    // Aggiorna la coda con gli ultimi nt-1 campioni del chunk.
    const size_t keep = history_.size();
    if (n >= keep) {
        for (size_t i = 0; i < keep; i++)
            history_[i] = in[n - keep + i];
    } else {
        // Chunk piu' corto della coda: fai scorrere.
        std::vector<cfloat> merged(history_.begin(), history_.end());
        merged.insert(merged.end(), in, in + n);
        for (size_t i = 0; i < keep; i++)
            history_[i] = merged[merged.size() - keep + i];
    }
    return written;
}

// --------------------------------------------------------------------------

FrequencyShifter::FrequencyShifter(double sampleRateHz, double shiftHz)
    : sampleRate_(sampleRateHz)
{
    setShift(shiftHz);
}

void FrequencyShifter::setShift(double shiftHz)
{
    phaseInc_ = 2.0 * M_PI * shiftHz / sampleRate_;
}

void FrequencyShifter::process(const cfloat* in, size_t n, cfloat* out)
{
    for (size_t i = 0; i < n; i++) {
        out[i] = in[i] * cfloat(float(std::cos(phase_)), float(std::sin(phase_)));
        phase_ += phaseInc_;
        if (phase_ > 2.0 * M_PI)  phase_ -= 2.0 * M_PI;
        if (phase_ < -2.0 * M_PI) phase_ += 2.0 * M_PI;
    }
}

} // namespace sdrjo::dsp
