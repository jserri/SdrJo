#pragma once
//
// Filtri FIR con progetto a finestra (sinc + Hamming) e versione decimante,
// il mattone base per portare il segnale dalla banda della RTL-SDR
// (es. 2.4 MS/s) alla banda del singolo modulo (es. 12.5 kHz per NFM).
//
#include "types.hpp"
#include <vector>

namespace sdrjo::dsp {

// Progetta un passa-basso FIR: cutoffHz relativo a sampleRateHz, numTaps dispari.
std::vector<float> designLowPass(double sampleRateHz, double cutoffHz, int numTaps);

// FIR complesso con stato interno (streaming, chunk dopo chunk).
class FirFilter {
public:
    explicit FirFilter(std::vector<float> taps);
    void process(const cfloat* in, size_t n, cfloat* out);

private:
    std::vector<float> taps_;
    std::vector<cfloat> history_;
    size_t histPos_ = 0;
};

// FIR + decimazione per un fattore intero (calcola solo le uscite necessarie).
class FirDecimator {
public:
    FirDecimator(std::vector<float> taps, unsigned decimation);

    // Restituisce il numero di campioni scritti in out
    // (out deve poter contenere almeno n / decimation + 1 campioni).
    size_t process(const cfloat* in, size_t n, cfloat* out);

    unsigned decimation() const { return decim_; }

private:
    std::vector<float> taps_;
    std::vector<cfloat> history_; // coda del chunk precedente, lunga numTaps-1
    unsigned decim_;
    unsigned phase_ = 0;
};

// Oscillatore + mixer per traslare in frequenza (porta il segnale voluto a DC).
class FrequencyShifter {
public:
    FrequencyShifter(double sampleRateHz, double shiftHz);
    void setShift(double shiftHz);
    void process(const cfloat* in, size_t n, cfloat* out);

private:
    double sampleRate_;
    double phase_ = 0.0;
    double phaseInc_ = 0.0;
};

} // namespace sdrjo::dsp
