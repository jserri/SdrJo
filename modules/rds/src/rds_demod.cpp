#include "sdrjo/rds/rds_demod.hpp"

#include <cmath>

namespace sdrjo::rds {

RdsDemodulator::RdsDemodulator(double sampleRateHz,
                               std::function<void(uint8_t)> onBit)
    : sampleRate_(sampleRateHz),
      // Porta la banda base RDS a ~8-16 kHz di sample rate.
      decim_(unsigned(sampleRateHz / 12000.0)),
      basebandRate_(sampleRateHz / decim_),
      samplesPerSymbol_(basebandRate_ / kSymbolRate),
      shifter_(sampleRateHz, -kSubcarrierHz),
      decimator_(dsp::designLowPass(sampleRateHz, 2400.0, 129), decim_),
      onBit_(std::move(onBit))
{
    // Anello di Costas: banda ~1.5% del symbol rate.
    double bw = 2.0 * M_PI * kSymbolRate * 0.015 / basebandRate_;
    double damping = 0.707;
    double denom = 1.0 + 2.0 * damping * bw + bw * bw;
    alpha_ = 4.0 * damping * bw / denom;
    beta_ = 4.0 * bw * bw / denom;
}

void RdsDemodulator::processMultiplex(const float* samples, size_t n)
{
    // Reale -> complesso centrato sulla sottoportante.
    mixBuf_.resize(n);
    std::vector<cfloat> real(n);
    for (size_t i = 0; i < n; i++) real[i] = cfloat(samples[i], 0.0f);
    shifter_.process(real.data(), n, mixBuf_.data());

    bbBuf_.resize(n / decim_ + 2);
    size_t m = decimator_.process(mixBuf_.data(), n, bbBuf_.data());

    for (size_t i = 0; i < m; i++) {
        // Correzione di fase residua (Costas BPSK).
        cfloat lo(float(std::cos(-phase_)), float(std::sin(-phase_)));
        cfloat s = bbBuf_[i] * lo;

        float err = s.real() * s.imag(); // errore BPSK classico
        freq_ += beta_ * err;
        phase_ += freq_ + alpha_ * err;
        if (phase_ > 2 * M_PI) phase_ -= 2 * M_PI;
        if (phase_ < -2 * M_PI) phase_ += 2 * M_PI;
        locked_ = std::fabs(err) < 0.1f;

        // Campionamento dei simboli con recupero di clock early-late
        // semplificato (zero crossing del segnale bifase).
        float cur = s.real();
        clock_ += 1.0;
        if ((prevSample_ < 0) != (cur < 0)) {
            // Transizione: il clock ideale e' a meta' simbolo da qui.
            double target = samplesPerSymbol_ / 2.0;
            if (clock_ > target) clock_ -= 0.1;
            else clock_ += 0.1;
        }
        if (clock_ >= samplesPerSymbol_) {
            clock_ -= samplesPerSymbol_;
            onSymbol(cur);
        }
        prevSample_ = cur;
    }
}

void RdsDemodulator::onSymbol(float sym)
{
    symbolPair_.push_back(sym);
    if (int(symbolPair_.size()) < 2) return;

    float s0 = symbolPair_[0], s1 = symbolPair_[1];
    symbolPair_.clear();

    // In un simbolo bifase valido le due meta' hanno segno opposto.
    // Se non succede spesso, l'accoppiamento e' sfasato di un simbolo.
    if ((s0 < 0) == (s1 < 0)) {
        if (++pairSlipVotes_ > 16) {
            pairSlipVotes_ = 0;
            symbolPair_.push_back(s1); // scivola di un simbolo
        }
        // Coppia dubbia: decidi comunque con la meta' piu' forte.
    } else if (pairSlipVotes_ > 0) {
        pairSlipVotes_--;
    }

    uint8_t encoded = (s0 - s1) > 0 ? 1 : 0;
    uint8_t bit = encoded ^ prevEncodedBit_; // decodifica differenziale
    prevEncodedBit_ = encoded;
    if (onBit_) onBit_(bit);
}

} // namespace sdrjo::rds
