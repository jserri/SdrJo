#pragma once
//
// Demodulatori analogici di base (FM, AM) e AGC: servono sia all'ascolto
// generico stile SDR++ sia come primo stadio di vari moduli digitali
// (es. il Morse usa AM/CW, gli APT NOAA usano FM+sottoportante AM).
//
#include "types.hpp"
#include <cstddef>

namespace sdrjo::dsp {

// Demodulatore FM a quadratura: out[i] = arg(in[i] * conj(in[i-1])) * gain.
class FmDemodulator {
public:
    // deviationHz: deviazione di picco attesa (75k per WFM, ~3.5k per NFM).
    FmDemodulator(double sampleRateHz, double deviationHz);
    void process(const cfloat* in, size_t n, float* out);

private:
    cfloat prev_{1.0f, 0.0f};
    float gain_;
};

// Demodulatore AM a inviluppo, con rimozione della componente DC.
class AmDemodulator {
public:
    explicit AmDemodulator(double dcAlpha = 0.999);
    void process(const cfloat* in, size_t n, float* out);

private:
    double dcAlpha_;
    float dc_ = 0.0f;
};

// AGC semplice con attacco/rilascio separati.
class Agc {
public:
    Agc(float attack = 0.02f, float release = 0.0005f, float targetLevel = 0.5f);
    void process(float* samples, size_t n);

private:
    float attack_, release_, target_;
    float envelope_ = 1e-3f;
};

} // namespace sdrjo::dsp
