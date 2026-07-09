//
// Test dei blocchi DSP avanzati: ricampionatore razionale, VFO,
// WFM stereo, SSB, DC blocker e stima PPM.
//
#include <sdrjo/dsp/resampler.hpp>
#include <sdrjo/dsp/vfo.hpp>
#include <sdrjo/dsp/wfm_stereo.hpp>
#include <sdrjo/dsp/ssb.hpp>
#include <sdrjo/dsp/correction.hpp>
#include <sdrjo/dsp/fft.hpp>
#include "test_util.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

using namespace sdrjo;
using namespace sdrjo::dsp;

static std::vector<cfloat> tone(double freq, double rate, size_t n,
                                float amp = 1.0f)
{
    std::vector<cfloat> out(n);
    for (size_t i = 0; i < n; i++) {
        double ph = 2.0 * M_PI * freq * double(i) / rate;
        out[i] = amp * cfloat(float(std::cos(ph)), float(std::sin(ph)));
    }
    return out;
}

// Frequenza media stimata dalla rotazione di fase per campione.
static double meanFreq(const std::vector<cfloat>& x, double rate, size_t skip)
{
    double acc = 0.0;
    size_t count = 0;
    for (size_t i = std::max<size_t>(skip, 1); i < x.size(); i++) {
        cfloat d = x[i] * std::conj(x[i - 1]);
        if (std::abs(d) < 1e-6f) continue;
        acc += std::atan2(d.imag(), d.real());
        count++;
    }
    return count ? acc / double(count) * rate / (2.0 * M_PI) : 0.0;
}

static double rms(const std::vector<float>& x, size_t skip)
{
    double acc = 0.0;
    for (size_t i = skip; i < x.size(); i++) acc += double(x[i]) * x[i];
    return std::sqrt(acc / double(x.size() - skip));
}

int main()
{
    // --- rationalApprox ------------------------------------------------------
    {
        auto [l1, m1] = rationalApprox(1.5);
        CHECK(l1 == 3 && m1 == 2);
        auto [l2, m2] = rationalApprox(288000.0 / 300000.0); // 0.96 = 24/25
        CHECK(l2 == 24 && m2 == 25);
    }

    // --- RationalResampler: tono a 1 kHz da 48k a 72k (L=3, M=2) ------------
    {
        RationalResampler rr(3, 2);
        auto in = tone(1000.0, 48000.0, 9600);
        std::vector<cfloat> out;
        // Elabora a pezzi per esercitare lo stato streaming.
        for (size_t off = 0; off < in.size(); off += 1000) {
            size_t n = std::min<size_t>(1000, in.size() - off);
            rr.process(in.data() + off, n, out);
        }
        CHECK(out.size() == in.size() * 3 / 2);
        CHECK_NEAR(meanFreq(out, 72000.0, 200), 1000.0, 2.0);

        // Ampiezza preservata (a regime).
        double amp = 0;
        for (size_t i = 500; i < out.size(); i++)
            amp = std::max(amp, double(std::abs(out[i])));
        CHECK_NEAR(amp, 1.0, 0.1);
    }

    // --- Vfo: due canali estratti dalla stessa banda -------------------------
    {
        const double wideRate = 96000.0;
        const size_t n = 48000;
        // Due portanti: +10 kHz e -20 kHz.
        auto t1 = tone(10000.0, wideRate, n, 0.7f);
        auto t2 = tone(-20000.0, wideRate, n, 0.7f);
        std::vector<cfloat> wide(n);
        for (size_t i = 0; i < n; i++) wide[i] = t1[i] + t2[i];

        Vfo vfoA(wideRate, 12000.0, 10000.0);
        Vfo vfoB(wideRate, 12000.0, -20000.0);
        std::vector<cfloat> chA, chB;
        vfoA.process(wide.data(), n, chA);
        vfoB.process(wide.data(), n, chB);

        CHECK_NEAR(double(chA.size()), n / 8.0, 8.0);
        // Ogni canale deve contenere il suo tono, portato a DC.
        CHECK_NEAR(meanFreq(chA, vfoA.outputRate(), 200), 0.0, 30.0);
        CHECK_NEAR(meanFreq(chB, vfoB.outputRate(), 200), 0.0, 30.0);
        double pA = 0, pB = 0;
        for (size_t i = 200; i < chA.size(); i++) pA += std::norm(chA[i]);
        for (size_t i = 200; i < chB.size(); i++) pB += std::norm(chB[i]);
        CHECK(pA / double(chA.size() - 200) > 0.2); // ~0.49 attesi
        CHECK(pB / double(chB.size() - 200) > 0.2);

        // Rapporto non intero: 2.4M -> 288k (divisione per 8.333).
        Vfo vfoC(2.4e6, 288000.0, 0.0);
        CHECK_NEAR(vfoC.outputRate(), 288000.0, 1.0);
        std::vector<cfloat> chC;
        auto dc = std::vector<cfloat>(24000, cfloat(0.5f, 0.0f));
        vfoC.process(dc.data(), dc.size(), chC);
        CHECK_NEAR(double(chC.size()), 24000.0 / (2.4e6 / 288000.0), 12.0);
    }

    // --- WFM stereo: tono a sinistra, silenzio a destra ----------------------
    {
        const double rate = 240000.0;
        const size_t n = size_t(rate); // 1 secondo
        std::vector<cfloat> iq(n);
        double phase = 0.0;
        for (size_t i = 0; i < n; i++) {
            double t = double(i) / rate;
            double l = std::sin(2.0 * M_PI * 800.0 * t);
            double r = 0.0;
            double p19 = 2.0 * M_PI * 19000.0 * t;
            double mpx = 0.45 * (l + r) / 2.0 + 0.09 * std::cos(p19) +
                         0.45 * std::cos(2.0 * p19) * (l - r) / 2.0;
            phase += 2.0 * M_PI * 75000.0 * mpx / rate;
            iq[i] = cfloat(float(std::cos(phase)), float(std::sin(phase)));
        }

        WfmStereoDemodulator demod(rate, 48000.0);
        std::vector<float> left, right;
        demod.process(iq.data(), n, left, right);

        CHECK(demod.stereoLocked());
        CHECK(left.size() == n / 5);
        // Separazione: il tono deve stare quasi tutto a sinistra.
        size_t skip = left.size() / 2; // lascia assestare pilota e deenfasi
        double rmsL = rms(left, skip), rmsR = rms(right, skip);
        CHECK(rmsL > 0.05);
        CHECK(rmsL / std::max(rmsR, 1e-9) > 4.0); // > 12 dB di separazione
    }

    // --- SSB ------------------------------------------------------------------
    {
        const double rate = 12000.0;
        // USB: tono audio a 1.2 kHz = riga a +1200 Hz.
        auto usbSig = tone(1200.0, rate, 12000, 0.5f);
        SsbDemodulator usb(rate, true);
        std::vector<float> audio;
        usb.process(usbSig.data(), usbSig.size(), audio);
        CHECK_NEAR(rms(audio, 2000), 0.5 * 2.0 / std::sqrt(2.0), 0.08);

        // Un segnale LSB (riga a -800 Hz) deve essere rigettato dal demod USB.
        auto lsbSig = tone(-800.0, rate, 12000, 0.5f);
        std::vector<float> rejected;
        usb.process(lsbSig.data(), lsbSig.size(), rejected);
        // (occhio: process appende; usa un demod nuovo)
        SsbDemodulator usb2(rate, true);
        rejected.clear();
        usb2.process(lsbSig.data(), lsbSig.size(), rejected);
        CHECK(rms(rejected, 2000) < 0.05);

        // E dev'essere decodificato dal demod LSB.
        SsbDemodulator lsb(rate, false);
        std::vector<float> audioLsb;
        lsb.process(lsbSig.data(), lsbSig.size(), audioLsb);
        CHECK_NEAR(rms(audioLsb, 2000), 0.5 * 2.0 / std::sqrt(2.0), 0.08);
    }

    // --- DcBlocker -------------------------------------------------------------
    {
        DcBlocker dc(1e-3);
        auto sig = tone(1000.0, 48000.0, 48000, 0.5f);
        for (auto& s : sig) s += cfloat(0.2f, -0.1f); // offset DC
        std::vector<cfloat> out(sig.size());
        dc.process(sig.data(), sig.size(), out.data());
        // A regime la media deve essere ~0 ma il tono intatto.
        cfloat mean(0, 0);
        double power = 0;
        size_t skip = 24000;
        for (size_t i = skip; i < out.size(); i++) {
            mean += out[i];
            power += std::norm(out[i]);
        }
        mean /= float(out.size() - skip);
        CHECK(std::abs(mean) < 0.01f);
        CHECK_NEAR(power / double(out.size() - skip), 0.25, 0.03);
    }

    // --- estimatePpm -------------------------------------------------------------
    {
        const double rate = 2.4e6, center = 100e6;
        const size_t n = 4096;
        // Portante attesa a 100 MHz ma ricevuta con +20 ppm (=+2 kHz).
        auto sig = tone(2000.0, rate, n, 0.8f);
        // Aggiungi rumore leggero.
        for (auto& s : sig) s += cfloat(0.01f * float(std::rand() % 100 - 50) / 50.0f,
                                        0.01f * float(std::rand() % 100 - 50) / 50.0f);
        std::vector<float> spec(n);
        powerSpectrumDb(sig.data(), n, spec.data());
        double ppm = estimatePpm(spec.data(), n, rate, center, center);
        CHECK_NEAR(ppm, 20.0, 3.0);

        // Solo rumore: nessuna stima.
        std::vector<cfloat> noise(n);
        for (auto& s : noise) s = cfloat(0.01f * float(std::rand() % 100 - 50) / 50.0f,
                                         0.01f * float(std::rand() % 100 - 50) / 50.0f);
        powerSpectrumDb(noise.data(), n, spec.data());
        CHECK(estimatePpm(spec.data(), n, rate, center, center) == 0.0);
    }

    return testResult("test_dsp_advanced");
}
