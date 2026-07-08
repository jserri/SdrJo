#pragma once
//
// Demodulatore QPSK per LRPT Meteor-M (72k simboli/s, SDR a 137 MHz).
//
// STATO: scheletro funzionante nella struttura ma con anelli ancora da
// tarare su registrazioni reali. La catena e':
//   1. filtro RRC (roll-off 0.6 per LRPT)
//   2. Costas loop a 4 fasi per agganciare la portante
//   3. recupero di clock (Gardner) per campionare al centro del simbolo
//   4. uscita: soft bit I/Q (0..255) da passare al Viterbi
//
#include <sdrjo/dsp/types.hpp>
#include <sdrjo/dsp/fir_filter.hpp>

#include <functional>
#include <vector>

namespace sdrjo::lrpt {

class QpskDemodulator {
public:
    // symbolRate: 72000 per i Meteor-M N2-x in modalita' standard
    // (80000 per la modalita' estesa usata a volte da M2-3/M2-4).
    QpskDemodulator(double sampleRateHz, double symbolRate = 72000.0);

    // Callback con coppie di soft bit (I poi Q) per ogni simbolo.
    void setSymbolCallback(std::function<void(uint8_t softI, uint8_t softQ)> cb);

    void processIq(const sdrjo::cfloat* samples, size_t n);

    double frequencyErrorHz() const { return freqErrorHz_; }
    bool locked() const { return locked_; }

private:
    double sampleRate_;
    double symbolRate_;
    double samplesPerSymbol_;

    // Costas loop
    double carrierPhase_ = 0.0;
    double carrierFreq_ = 0.0; // rad/campione
    double costasAlpha_, costasBeta_;
    double freqErrorHz_ = 0.0;
    bool locked_ = false;

    // Clock recovery (Gardner)
    double symbolClock_ = 0.0;
    sdrjo::cfloat prevSample_{0, 0};
    sdrjo::cfloat midSample_{0, 0};

    sdrjo::dsp::FirFilter rrc_;
    std::function<void(uint8_t, uint8_t)> callback_;

    static std::vector<float> designRrc(double sampleRate, double symbolRate,
                                        double rolloff, int numTaps);
};

} // namespace sdrjo::lrpt
