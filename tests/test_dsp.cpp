//
// Test dei blocchi DSP del core: FFT, filtro FIR, demodulatore FM.
//
#include <sdrjo/dsp/fft.hpp>
#include <sdrjo/dsp/fir_filter.hpp>
#include <sdrjo/dsp/demod.hpp>
#include "test_util.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace sdrjo;
using namespace sdrjo::dsp;

int main()
{
    // --- FFT: un tono a bin 32 deve avere il picco nel bin giusto --------
    {
        const size_t n = 256;
        std::vector<cfloat> x(n);
        for (size_t i = 0; i < n; i++) {
            double ph = 2.0 * M_PI * 32.0 * double(i) / double(n);
            x[i] = cfloat(float(std::cos(ph)), float(std::sin(ph)));
        }
        auto copy = x;
        fft(copy.data(), n);
        size_t peak = 0;
        for (size_t i = 1; i < n; i++)
            if (std::abs(copy[i]) > std::abs(copy[peak])) peak = i;
        CHECK(peak == 32);

        // ifft(fft(x)) == x
        ifft(copy.data(), n);
        double maxErr = 0;
        for (size_t i = 0; i < n; i++)
            maxErr = std::max(maxErr, double(std::abs(copy[i] - x[i])));
        CHECK(maxErr < 1e-4);
    }

    // --- FIR passa-basso: attenua un tono oltre il cutoff -----------------
    {
        const double rate = 48000.0;
        auto taps = designLowPass(rate, 4000.0, 101);
        FirFilter lp(taps);

        auto power = [&](double freq) {
            FirFilter f(taps);
            const size_t n = 4096;
            std::vector<cfloat> in(n), out(n);
            for (size_t i = 0; i < n; i++) {
                double ph = 2.0 * M_PI * freq * double(i) / rate;
                in[i] = cfloat(float(std::cos(ph)), float(std::sin(ph)));
            }
            f.process(in.data(), n, out.data());
            double p = 0;
            for (size_t i = n / 2; i < n; i++) p += std::norm(out[i]);
            return p / double(n / 2);
        };

        double inBand = power(1000.0);
        double outBand = power(12000.0);
        CHECK(inBand > 0.9);            // passa quasi tutto
        CHECK(outBand < inBand * 1e-3); // almeno 30 dB di attenuazione
    }

    // --- Decimatore: stessa risposta, 1/4 dei campioni --------------------
    {
        auto taps = designLowPass(48000.0, 5000.0, 63);
        FirDecimator dec(taps, 4);
        std::vector<cfloat> in(1024, cfloat(1.0f, 0.0f));
        std::vector<cfloat> out(300);
        size_t n1 = dec.process(in.data(), 512, out.data());
        size_t n2 = dec.process(in.data() + 512, 512, out.data() + n1);
        CHECK(n1 + n2 == 256);
        // A regime il DC passa con guadagno 1.
        CHECK_NEAR(out[n1 + n2 - 1].real(), 1.0, 0.01);
    }

    // --- Demodulatore FM: tono modulato -> sinusoide in uscita ------------
    {
        const double rate = 240000.0, dev = 75000.0, audioFreq = 1000.0;
        FmDemodulator fm(rate, dev);
        const size_t n = 4096;
        std::vector<cfloat> iq(n);
        double phase = 0.0;
        for (size_t i = 0; i < n; i++) {
            double t = double(i) / rate;
            double instFreq = dev * std::sin(2.0 * M_PI * audioFreq * t);
            phase += 2.0 * M_PI * instFreq / rate;
            iq[i] = cfloat(float(std::cos(phase)), float(std::sin(phase)));
        }
        std::vector<float> audio(n);
        fm.process(iq.data(), n, audio.data());

        // Ampiezza di picco ~1.0 (deviazione piena), frequenza ~1 kHz.
        float peak = 0;
        for (size_t i = 100; i < n; i++) peak = std::max(peak, std::fabs(audio[i]));
        CHECK_NEAR(peak, 1.0, 0.05);
    }

    return testResult("test_dsp");
}
