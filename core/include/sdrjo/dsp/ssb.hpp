#pragma once
//
// Demodulatore SSB (USB/LSB) a rilevatore di prodotto: il canale IQ va
// centrato sulla frequenza della portante soppressa.
//
#include "fir_filter.hpp"

#include <vector>

namespace sdrjo::dsp {

class SsbDemodulator {
public:
    // sampleRate: rate del canale IQ (l'audio esce allo stesso rate).
    SsbDemodulator(double sampleRate, bool upperSideband,
                   double bandwidthHz = 2700.0, float gain = 2.0f);

    void setSideband(bool upperSideband);

    // Appende audio reale a out (stesso rate dell'ingresso).
    size_t process(const cfloat* iq, size_t n, std::vector<float>& out);

private:
    double sampleRate_;
    double bandwidthHz_;
    bool usb_;
    float gain_;

    // Sposta la banda laterale voluta a cavallo di DC, filtra, ritorna.
    FrequencyShifter shiftDown_;
    FrequencyShifter shiftUp_;
    FirFilter lpf_;

    std::vector<cfloat> a_, b_;
};

} // namespace sdrjo::dsp
