#include "sdrjo/sensors/ook_detector.hpp"

#include <cmath>

namespace sdrjo::sensors {

OokDetector::OokDetector(PulseCallback cb, double sampleRate)
    : cb_(std::move(cb)), rate_(sampleRate)
{
}

void OokDetector::processIq(const cfloat* samples, size_t n)
{
    // Soglia adattiva a meta' strada tra rumore medio e picco inseguito:
    // regge sia i sensori vicini (forti) sia quelli in giardino (deboli).
    const double usPerSample = 1e6 / rate_;
    const size_t maxGap = size_t(rate_ * 0.01); // 10 ms: fine trasmissione

    for (size_t i = 0; i < n; i++) {
        float mag = std::abs(samples[i]);
        envAvg_ += 0.0005f * (mag - envAvg_);
        if (mag > envPeak_) envPeak_ += 0.05f * (mag - envPeak_);
        else envPeak_ += 0.00005f * (mag - envPeak_);

        float thr = 0.5f * (envAvg_ + envPeak_);
        bool on = envPeak_ > 3.0f * envAvg_ && mag > thr;

        if (on) {
            if (!high_ && highLen_ > 0) {
                // Fine della pausa: chiudi l'impulso precedente.
                cb_({highLen_ * usPerSample, gapLen_ * usPerSample});
                highLen_ = 0;
                gapLen_ = 0;
            }
            high_ = true;
            highLen_++;
        } else {
            if (high_) {
                high_ = false;
                gapLen_ = 0;
            }
            if (highLen_ > 0) {
                gapLen_++;
                if (gapLen_ >= maxGap) {
                    // Trasmissione finita: emetti l'ultimo impulso.
                    cb_({highLen_ * usPerSample, gapLen_ * usPerSample});
                    highLen_ = 0;
                    gapLen_ = 0;
                }
            }
        }
    }
}

} // namespace sdrjo::sensors
