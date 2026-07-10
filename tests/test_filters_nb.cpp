//
// Test del notch audio e del noise blanker.
//
#include <sdrjo/dsp/audio_filters.hpp>
#include <sdrjo/dsp/types.hpp>

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

// Ampiezza RMS di un tono a freqHz dopo il filtro (regime, primo secondo
// scartato).
static double toneRmsThrough(sdrjo::dsp::AudioFilterChain& chain,
                             double freqHz, double rate)
{
    const size_t n = size_t(rate * 2);
    std::vector<float> x(n);
    for (size_t i = 0; i < n; i++)
        x[i] = std::sin(2.0 * M_PI * freqHz * double(i) / rate);
    chain.process(x.data(), n);
    double acc = 0;
    for (size_t i = n / 2; i < n; i++) acc += double(x[i]) * double(x[i]);
    return std::sqrt(acc / double(n / 2)); // RMS pieno = 0.707
}

int main()
{
    const double rate = 48000.0;

    // Notch a 1 kHz: il tono a 1 kHz sparisce, quello a 300 Hz passa.
    {
        sdrjo::dsp::AudioFilterChain c;
        c.configureNotch(rate, 1000.0, 30.0);
        double atNotch = toneRmsThrough(c, 1000.0, rate);
        c.reset();
        double away = toneRmsThrough(c, 300.0, rate);
        std::printf("notch: 1 kHz rms=%.4f, 300 Hz rms=%.4f\n", atNotch,
                    away);
        CHECK(atNotch < 0.03);  // > 27 dB di attenuazione
        CHECK(away > 0.65);     // < 1 dB di perdita
    }

    // Notch spento (0 Hz): non tocca nulla.
    {
        sdrjo::dsp::AudioFilterChain c;
        c.configureNotch(rate, 0.0);
        double rms = toneRmsThrough(c, 1000.0, rate);
        CHECK(std::fabs(rms - 0.7071) < 0.01);
    }

    // Noise blanker: i picchi impulsivi vengono schiacciati, il segnale
    // continuo resta com'e'.
    {
        sdrjo::dsp::NoiseBlanker nb;
        nb.configure(true, 4.0f);
        const size_t n = 48000;
        std::vector<sdrjo::cfloat> x(n);
        for (size_t i = 0; i < n; i++)
            x[i] = sdrjo::cfloat(0.1f * std::cos(0.01f * float(i)),
                                 0.1f * std::sin(0.01f * float(i)));
        // Impulsi 60x ogni 4800 campioni nella seconda meta'.
        for (size_t i = n / 2; i < n; i += 4800) x[i] *= 60.0f;

        std::vector<sdrjo::cfloat> y = x;
        nb.processInPlace(y.data(), n);

        // I campioni impulsivi devono scendere di almeno 10x.
        for (size_t i = n / 2; i < n; i += 4800) {
            CHECK(std::abs(y[i]) < std::abs(x[i]) / 10.0f);
        }
        // Il segnale normale non deve essere alterato in modo udibile.
        double err = 0, ref = 0;
        for (size_t i = n / 2 + 100; i < n / 2 + 4700; i++) {
            err += std::norm(y[i] - x[i]);
            ref += std::norm(x[i]);
        }
        CHECK(err / ref < 0.01);
    }

    // Noise blanker spento: passthrough esatto.
    {
        sdrjo::dsp::NoiseBlanker nb;
        nb.configure(false, 4.0f);
        std::vector<sdrjo::cfloat> x = {{1e3f, 0}, {2e3f, 1e3f}};
        auto y = x;
        nb.processInPlace(y.data(), y.size());
        CHECK(y[0] == x[0] && y[1] == x[1]);
    }

    if (failures == 0) std::printf("test_filters_nb: OK\n");
    return failures == 0 ? 0 : 1;
}
