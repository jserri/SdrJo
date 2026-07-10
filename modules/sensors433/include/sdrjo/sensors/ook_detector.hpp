#pragma once
//
// Rilevatore OOK (On-Off Keying) per i sensori ISM a 433/868 MHz:
// inviluppo del segnale, soglia adattiva e misura della durata di
// impulsi e pause in microsecondi (stile rtl_433).
//
#include <sdrjo/dsp/types.hpp>

#include <cstddef>
#include <functional>

namespace sdrjo::sensors {

struct Pulse {
    double highUs; // durata dell'impulso (portante accesa)
    double gapUs;  // pausa che lo segue (portante spenta)
};

class OokDetector {
public:
    static constexpr double kSampleRateHz = 250000.0;

    using PulseCallback = std::function<void(const Pulse&)>;

    explicit OokDetector(PulseCallback cb, double sampleRate = kSampleRateHz);

    void processIq(const cfloat* samples, size_t n);

private:
    PulseCallback cb_;
    double rate_;
    float envAvg_ = 0.0f;   // media dell'inviluppo (rumore + segnale)
    float envPeak_ = 0.0f;  // picco inseguito (livello "acceso")
    bool high_ = false;
    size_t highLen_ = 0;
    size_t gapLen_ = 0;
};

} // namespace sdrjo::sensors
