#include "sdrjo/dsp/fft.hpp"
#include <cmath>
#include <vector>

namespace sdrjo::dsp {

static void fftInternal(cfloat* data, size_t n, bool inverse)
{
    // Permutazione bit-reversal
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(data[i], data[j]);
    }

    const float sign = inverse ? 1.0f : -1.0f;
    for (size_t len = 2; len <= n; len <<= 1) {
        float ang = sign * 2.0f * float(M_PI) / float(len);
        cfloat wlen(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cfloat w(1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; k++) {
                cfloat u = data[i + k];
                cfloat v = data[i + k + len / 2] * w;
                data[i + k] = u + v;
                data[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }

    if (inverse) {
        for (size_t i = 0; i < n; i++) data[i] /= float(n);
    }
}

void fft(cfloat* data, size_t n)  { fftInternal(data, n, false); }
void ifft(cfloat* data, size_t n) { fftInternal(data, n, true); }

void hannWindow(float* out, size_t n)
{
    for (size_t i = 0; i < n; i++)
        out[i] = 0.5f - 0.5f * std::cos(2.0f * float(M_PI) * float(i) / float(n - 1));
}

void powerSpectrumDb(const cfloat* in, size_t n, float* out)
{
    // Buffer e finestra riutilizzati tra le chiamate (niente allocazioni
    // ne' ricalcolo del coseno a ogni FFT: questa funzione gira ~30 volte
    // al secondo anche a 65536 punti).
    static thread_local std::vector<cfloat> work;
    static thread_local std::vector<float> win;
    work.resize(n);
    if (win.size() != n) {
        win.resize(n);
        hannWindow(win.data(), n);
    }
    for (size_t i = 0; i < n; i++) work[i] = in[i] * win[i];

    fft(work.data(), n);

    // Shift: la DC finisce al centro del grafico.
    for (size_t i = 0; i < n; i++) {
        size_t src = (i + n / 2) % n;
        float p = std::norm(work[src]) / float(n * n);
        out[i] = 10.0f * std::log10(p + 1e-20f);
    }
}

} // namespace sdrjo::dsp
