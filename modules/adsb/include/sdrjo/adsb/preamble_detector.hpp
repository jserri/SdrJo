#pragma once
//
// Rilevamento del preambolo Mode S e demodulazione PPM dal vettore delle
// ampiezze, a 2.0 MS/s (2 campioni per microsecondo, come dump1090).
//
#include <cstdint>
#include <functional>
#include <vector>

#include <sdrjo/dsp/types.hpp>

namespace sdrjo::adsb {

// Callback per ogni frame demodulato che supera il CRC.
using FrameCallback = std::function<void(const uint8_t* frame, size_t lenBytes)>;

class PreambleDetector {
public:
    static constexpr double kSampleRateHz = 2.0e6;

    explicit PreambleDetector(FrameCallback cb);

    // Elabora un blocco di campioni IQ (gia' a 2.0 MS/s, centrati a 1090 MHz).
    void processIq(const sdrjo::cfloat* samples, size_t n);

    // Elabora direttamente un vettore di ampiezze (per test e replay).
    void processMagnitude(const float* mag, size_t n);

private:
    void scan();

    FrameCallback callback_;
    std::vector<float> buf_;   // ampiezze accumulate tra un chunk e l'altro
    size_t scanPos_ = 0;       // posizioni < scanPos_ gia' esaminate
};

} // namespace sdrjo::adsb
