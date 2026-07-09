#pragma once
//
// Filtri audio per migliorare la ricezione (header-only):
// passa-alto (toglie rombo/hum) e passa-basso (toglie fruscio) a un polo,
// concatenati in una catena configurabile a runtime.
//
#include <cmath>
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
    }

    void reset()
    {
        hpPrevX_ = hpPrevY_ = lpState_ = 0.0f;
    }

private:
    bool hpOn_ = false, lpOn_ = false;
    float hpAlpha_ = 1.0f, hpPrevX_ = 0.0f, hpPrevY_ = 0.0f;
    float lpAlpha_ = 1.0f, lpState_ = 0.0f;
};

} // namespace sdrjo::dsp
