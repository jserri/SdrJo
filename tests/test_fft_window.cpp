//
// Test delle finestre FFT: la Blackman-Harris deve avere un "tappeto"
// di dispersione (spectral leakage) molto piu' basso della Hann attorno
// a un tono che cade tra due bin, senza spostare il picco.
//
#include <sdrjo/dsp/fft.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int main()
{
    const size_t n = 4096;
    // Tono a meta' strada tra due bin: caso peggiore per il leakage.
    const double bin = 300.5;
    std::vector<sdrjo::cfloat> iq(n);
    for (size_t i = 0; i < n; i++) {
        double ph = 2.0 * M_PI * bin * double(i) / double(n);
        iq[i] = sdrjo::cfloat(float(std::cos(ph)), float(std::sin(ph)));
    }

    std::vector<float> hann(n), bh(n);
    sdrjo::dsp::powerSpectrumDb(iq.data(), n, hann.data(),
                                sdrjo::dsp::FftWindow::Hann);
    sdrjo::dsp::powerSpectrumDb(iq.data(), n, bh.data(),
                                sdrjo::dsp::FftWindow::BlackmanHarris);

    // Picco nello stesso punto (bin positivo: n/2 + 300 o 301).
    auto peakOf = [&](const std::vector<float>& s) {
        size_t p = 0;
        for (size_t i = 1; i < n; i++)
            if (s[i] > s[p]) p = i;
        return p;
    };
    size_t pH = peakOf(hann), pB = peakOf(bh);
    CHECK(pH >= n / 2 + 299 && pH <= n / 2 + 302);
    CHECK(pB >= n / 2 + 299 && pB <= n / 2 + 302);

    // Leakage a 30 bin dal picco: la BH deve stare parecchio piu' in
    // basso della Hann (in teoria -92 dB contro -31 dB di primo lobo).
    float lkH = hann[pH + 30], lkB = bh[pB + 30];
    std::printf("leakage a +30 bin: Hann %.1f dB, Blackman-Harris %.1f dB\n",
                lkH, lkB);
    CHECK(lkB < lkH - 20.0f);

    // Il picco non deve perdere piu' di qualche dB tra le due finestre
    // (guadagni coerenti di finestra a parte).
    CHECK(std::fabs(hann[pH] - bh[pB]) < 6.0f);

    // La chiamata senza parametro resta identica alla Hann esplicita.
    std::vector<float> dflt(n);
    sdrjo::dsp::powerSpectrumDb(iq.data(), n, dflt.data());
    CHECK(std::fabs(dflt[pH] - hann[pH]) < 1e-4f);

    if (failures == 0) std::printf("test_fft_window: OK\n");
    return failures == 0 ? 0 : 1;
}
