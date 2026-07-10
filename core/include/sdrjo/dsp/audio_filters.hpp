#pragma once
//
// Filtri audio per migliorare la ricezione (header-only):
// passa-alto (toglie rombo/hum) e passa-basso (toglie fruscio) a un polo,
// piu' un notch biquad per scavare i fischi (eterodine), concatenati in
// una catena configurabile a runtime.
//
#include <cmath>
#include <complex>
#include <cstddef>

namespace sdrjo::dsp {

class AudioFilterChain {
public:
    // highPassHz / lowPassHz = 0 disattivano il rispettivo filtro.
    void configure(double sampleRate, double highPassHz, double lowPassHz)
    {
        hpOn_ = highPassHz > 0.0;
        lpOn_ = lowPassHz > 0.0;
        if (hpOn_)
            hpAlpha_ = float(std::exp(-2.0 * 3.14159265358979323846 *
                                      highPassHz / sampleRate));
        if (lpOn_)
            lpAlpha_ = float(1.0 - std::exp(-2.0 * 3.14159265358979323846 *
                                            lowPassHz / sampleRate));
    }

    // Notch (biquad, formule RBJ): intaglio stretto centrato su notchHz.
    // notchHz = 0 lo disattiva. q tipico 20-40: piu' alto = piu' stretto.
    void configureNotch(double sampleRate, double notchHz, double q = 30.0)
    {
        notchOn_ = notchHz > 0.0 && notchHz < sampleRate / 2.0;
        if (!notchOn_) return;
        double w = 2.0 * 3.14159265358979323846 * notchHz / sampleRate;
        double cw = std::cos(w);
        double alpha = std::sin(w) / (2.0 * q);
        double a0 = 1.0 + alpha;
        nb0_ = float(1.0 / a0);
        nb1_ = float(-2.0 * cw / a0);
        nb2_ = float(1.0 / a0);
        na1_ = float(-2.0 * cw / a0);
        na2_ = float((1.0 - alpha) / a0);
    }

    void process(float* x, size_t n)
    {
        if (hpOn_) {
            for (size_t i = 0; i < n; i++) {
                // y[n] = a*(y[n-1] + x[n] - x[n-1])
                float y = hpAlpha_ * (hpPrevY_ + x[i] - hpPrevX_);
                hpPrevX_ = x[i];
                hpPrevY_ = y;
                x[i] = y;
            }
        }
        if (lpOn_) {
            for (size_t i = 0; i < n; i++) {
                lpState_ += lpAlpha_ * (x[i] - lpState_);
                x[i] = lpState_;
            }
        }
        if (notchOn_) {
            // Forma diretta II trasposta.
            for (size_t i = 0; i < n; i++) {
                float y = nb0_ * x[i] + nz1_;
                nz1_ = nb1_ * x[i] - na1_ * y + nz2_;
                nz2_ = nb2_ * x[i] - na2_ * y;
                x[i] = y;
            }
        }
    }

    void reset()
    {
        hpPrevX_ = hpPrevY_ = lpState_ = 0.0f;
        nz1_ = nz2_ = 0.0f;
    }

private:
    bool hpOn_ = false, lpOn_ = false, notchOn_ = false;
    float hpAlpha_ = 1.0f, hpPrevX_ = 0.0f, hpPrevY_ = 0.0f;
    float lpAlpha_ = 1.0f, lpState_ = 0.0f;
    float nb0_ = 1, nb1_ = 0, nb2_ = 0, na1_ = 0, na2_ = 0;
    float nz1_ = 0, nz2_ = 0;
};

// Noise blanker sul canale IQ: i campioni la cui ampiezza supera di
// "soglia" volte la media inseguita vengono riportati al livello medio.
// Efficace contro i disturbi impulsivi (accensioni, recinti elettrici).
class NoiseBlanker {
public:
    void configure(bool on, float threshold)
    {
        on_ = on;
        k_ = threshold < 1.5f ? 1.5f : threshold;
    }

    template <typename Complex>
    void processInPlace(Complex* x, size_t n)
    {
        if (!on_) return;
        for (size_t i = 0; i < n; i++) {
            float m = std::abs(x[i]);
            if (avg_ > 1e-12f && m > k_ * avg_) {
                // Impulso: schiacciato al livello di soglia. Nella media
                // pesa 10 volte meno, cosi' non si alza la soglia da solo
                // ma la media puo' comunque riagganciarsi al segnale.
                x[i] *= (k_ * avg_) / m;
                avg_ += 0.001f * (m - avg_);
            } else {
                avg_ += 0.01f * (m - avg_);
            }
        }
    }

    void reset() { avg_ = 0.0f; }

private:
    bool on_ = false;
    float k_ = 4.0f;
    float avg_ = 0.0f;
};

} // namespace sdrjo::dsp
