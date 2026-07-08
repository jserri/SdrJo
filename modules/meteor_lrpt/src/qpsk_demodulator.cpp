#include "sdrjo/lrpt/qpsk_demodulator.hpp"

#include <algorithm>
#include <cmath>

namespace sdrjo::lrpt {

std::vector<float> QpskDemodulator::designRrc(double sampleRate, double symbolRate,
                                              double rolloff, int numTaps)
{
    // Radice di coseno rialzato (root raised cosine).
    std::vector<float> taps(numTaps);
    const double ts = sampleRate / symbolRate; // campioni per simbolo
    const int mid = numTaps / 2;
    double sum = 0.0;
    for (int i = 0; i < numTaps; i++) {
        double t = double(i - mid) / ts;
        double v;
        if (std::fabs(t) < 1e-9) {
            v = 1.0 - rolloff + 4.0 * rolloff / M_PI;
        } else if (std::fabs(std::fabs(4.0 * rolloff * t) - 1.0) < 1e-6) {
            v = rolloff / std::sqrt(2.0) *
                ((1.0 + 2.0 / M_PI) * std::sin(M_PI / (4.0 * rolloff)) +
                 (1.0 - 2.0 / M_PI) * std::cos(M_PI / (4.0 * rolloff)));
        } else {
            double num = std::sin(M_PI * t * (1.0 - rolloff)) +
                         4.0 * rolloff * t * std::cos(M_PI * t * (1.0 + rolloff));
            double den = M_PI * t * (1.0 - std::pow(4.0 * rolloff * t, 2.0));
            v = num / den;
        }
        taps[i] = float(v);
        sum += v;
    }
    for (auto& t : taps) t = float(t / sum);
    return taps;
}

QpskDemodulator::QpskDemodulator(double sampleRateHz, double symbolRate)
    : sampleRate_(sampleRateHz),
      symbolRate_(symbolRate),
      samplesPerSymbol_(sampleRateHz / symbolRate),
      rrc_(designRrc(sampleRateHz, symbolRate, 0.6, 63))
{
    // Larghezza di banda dell'anello di Costas: ~1% del symbol rate.
    double bw = 2.0 * M_PI * symbolRate * 0.01 / sampleRateHz;
    double damping = 0.707;
    double denom = 1.0 + 2.0 * damping * bw + bw * bw;
    costasAlpha_ = 4.0 * damping * bw / denom;
    costasBeta_ = 4.0 * bw * bw / denom;
}

void QpskDemodulator::setSymbolCallback(std::function<void(uint8_t, uint8_t)> cb)
{
    callback_ = std::move(cb);
}

void QpskDemodulator::processIq(const cfloat* samples, size_t n)
{
    std::vector<cfloat> filtered(n);
    rrc_.process(samples, n, filtered.data());

    for (size_t i = 0; i < n; i++) {
        // Correzione di portante.
        cfloat lo(float(std::cos(-carrierPhase_)), float(std::sin(-carrierPhase_)));
        cfloat s = filtered[i] * lo;

        // Errore di fase QPSK (decision-directed).
        float err = float(std::copysign(1.0f, s.real()) * s.imag() -
                          std::copysign(1.0f, s.imag()) * s.real());
        carrierFreq_ += costasBeta_ * err;
        carrierPhase_ += carrierFreq_ + costasAlpha_ * err;
        if (carrierPhase_ > 2.0 * M_PI) carrierPhase_ -= 2.0 * M_PI;
        if (carrierPhase_ < -2.0 * M_PI) carrierPhase_ += 2.0 * M_PI;
        freqErrorHz_ = carrierFreq_ * sampleRate_ / (2.0 * M_PI);
        locked_ = std::fabs(err) < 0.3f;

        // Recupero di clock stile Gardner semplificato: campiona il simbolo
        // quando il contatore attraversa lo zero, tenendo il campione di meta'.
        symbolClock_ += 1.0;
        if (symbolClock_ >= samplesPerSymbol_) {
            symbolClock_ -= samplesPerSymbol_;

            // Errore di Gardner con il campione centrale.
            float gerr = (s.real() - prevSample_.real()) * midSample_.real() +
                         (s.imag() - prevSample_.imag()) * midSample_.imag();
            symbolClock_ -= std::clamp(double(gerr) * 0.01, -0.5, 0.5);

            if (callback_) {
                auto toSoft = [](float v) -> uint8_t {
                    float x = v * 127.0f / 0.5f + 128.0f;
                    return uint8_t(std::clamp(x, 0.0f, 255.0f));
                };
                callback_(toSoft(s.real()), toSoft(s.imag()));
            }
            prevSample_ = s;
        } else if (symbolClock_ >= samplesPerSymbol_ / 2.0 &&
                   symbolClock_ - 1.0 < samplesPerSymbol_ / 2.0) {
            midSample_ = s;
        }
    }
}

} // namespace sdrjo::lrpt
