#pragma once
//
// Demodulatore FM broadcast stereo: FM -> multiplex -> pilota 19 kHz ->
// ricostruzione della sottoportante 38 kHz -> L/R con deenfasi.
//
#include "demod.hpp"
#include "fir_filter.hpp"

#include <vector>

namespace sdrjo::dsp {

class WfmStereoDemodulator {
public:
    // iqRate: rate del canale IQ centrato sulla stazione (>= 192 kHz);
    // audioRate deve dividere iqRate (240k/48k -> decimazione 5).
    WfmStereoDemodulator(double iqRate = 240000.0, double audioRate = 48000.0);

    // Appende audio a left/right; ritorna i campioni per canale aggiunti.
    size_t process(const cfloat* iq, size_t n, std::vector<float>& left,
                   std::vector<float>& right);

    // true se il pilota a 19 kHz e' presente (trasmissione stereo).
    bool stereoLocked() const { return pilotLevel_ > 0.01f; }
    float pilotLevel() const { return pilotLevel_; }

private:
    double iqRate_;
    unsigned decim_;

    FmDemodulator fm_;
    FirFilter pilotLpf_;      // passa-basso sul pilota mixato a DC
    FirDecimator audioLpf_;   // 15 kHz + decimazione (somma e differenza insieme)

    double phase19_ = 0.0;    // fase del mixer a 19 kHz
    double phaseInc19_;
    float pilotLevel_ = 0.0f;

    // Deenfasi 50 us (Europa).
    float deemAlpha_;
    float deemL_ = 0.0f, deemR_ = 0.0f;

    std::vector<float> mpx_;
    std::vector<cfloat> work_, pilotIn_, pilotOut_, sumDiff_, audio_;
};

} // namespace sdrjo::dsp
