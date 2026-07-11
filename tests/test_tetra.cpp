//
// Test del rivelatore di ATTIVITA' TETRA (solo presenza, niente decodifica).
// Verifica che: (a) un portante digitale largo ~canale sopra il rumore sia
// rilevato, (b) il solo rumore no, (c) un fischio stretto (CW) no.
//
#include <sdrjo/dsp/tetra_activity.hpp>

#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

namespace {
const double kRate = 48000.0;

// Rumore complesso a banda limitata (bordi ripidi, come un portante
// digitale che riempie il canale): passa-basso a una polo ripetuto 4
// volte per una caduta netta oltre la banda utile.
std::vector<sdrjo::cfloat> bandLimitedCarrier(size_t n, float amp, float cut)
{
    std::mt19937 rng(3);
    std::normal_distribution<float> g(0.0f, 1.0f);
    float a = float(1.0 - std::exp(-2.0 * M_PI * cut / kRate));
    sdrjo::cfloat s1(0, 0), s2(0, 0), s3(0, 0), s4(0, 0);
    std::vector<sdrjo::cfloat> out(n);
    for (size_t i = 0; i < n; i++) {
        sdrjo::cfloat x(g(rng), g(rng));
        s1 += a * (x - s1);
        s2 += a * (s1 - s2);
        s3 += a * (s2 - s3);
        s4 += a * (s3 - s4);
        out[i] = amp * s4;
    }
    return out;
}

std::vector<sdrjo::cfloat> whiteNoise(size_t n, float amp)
{
    std::mt19937 rng(9);
    std::normal_distribution<float> g(0.0f, amp);
    std::vector<sdrjo::cfloat> out(n);
    for (size_t i = 0; i < n; i++) out[i] = sdrjo::cfloat(g(rng), g(rng));
    return out;
}

// Somma di due segnali della stessa lunghezza.
void addInto(std::vector<sdrjo::cfloat>& a, const std::vector<sdrjo::cfloat>& b)
{
    for (size_t i = 0; i < a.size(); i++) a[i] += b[i];
}
} // namespace

int main()
{
    const size_t N = 48000; // 1 secondo

    // (a) Portante digitale largo (cut ~9 kHz) e forte sopra un po' di rumore.
    {
        auto sig = bandLimitedCarrier(N, 6.0f, 9000.0);
        addInto(sig, whiteNoise(N, 0.05f));
        sdrjo::dsp::TetraActivityDetector det(kRate);
        det.processIq(sig.data(), sig.size());
        std::printf("portante: attivo=%d snr=%.1f occ=%.1f kHz\n",
                    det.active(), det.snrDb(), det.occupiedKHz());
        CHECK(det.active());
        CHECK(det.snrDb() > 6.0f);
    }

    // (b) Solo rumore: nessuna attivita'.
    {
        auto sig = whiteNoise(N, 1.0f);
        sdrjo::dsp::TetraActivityDetector det(kRate);
        det.processIq(sig.data(), sig.size());
        std::printf("rumore:   attivo=%d snr=%.1f\n", det.active(), det.snrDb());
        CHECK(!det.active());
    }

    // (c) Fischio stretto (CW a 1 kHz) forte: banda troppo stretta, non e'
    // un portante largo -> non deve essere scambiato per TETRA.
    {
        std::vector<sdrjo::cfloat> sig(N);
        double ph = 0.0;
        for (size_t i = 0; i < N; i++) {
            ph += 2.0 * M_PI * 1000.0 / kRate;
            sig[i] = sdrjo::cfloat(3.0f * float(std::cos(ph)),
                                   3.0f * float(std::sin(ph)));
        }
        addInto(sig, whiteNoise(N, 0.05f));
        sdrjo::dsp::TetraActivityDetector det(kRate);
        det.processIq(sig.data(), sig.size());
        std::printf("CW:       attivo=%d snr=%.1f occ=%.1f kHz\n",
                    det.active(), det.snrDb(), det.occupiedKHz());
        CHECK(!det.active());
    }

    if (failures == 0) std::printf("test_tetra: OK\n");
    return failures == 0 ? 0 : 1;
}
