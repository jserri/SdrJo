#pragma once
//
// Tipi base per il DSP di SdrJo.
//
#include <complex>
#include <cstdint>
#include <vector>

namespace sdrjo {

// Campione IQ in virgola mobile, normalizzato in [-1, +1].
using cfloat = std::complex<float>;

// Converte un buffer di campioni IQ a 8 bit senza segno (formato nativo
// della RTL-SDR: 127.5 = zero) in campioni complessi normalizzati.
inline void convertU8Iq(const uint8_t* in, size_t numSamples, cfloat* out)
{
    for (size_t i = 0; i < numSamples; i++) {
        out[i] = cfloat((float(in[2 * i])     - 127.5f) / 127.5f,
                        (float(in[2 * i + 1]) - 127.5f) / 127.5f);
    }
}

} // namespace sdrjo
