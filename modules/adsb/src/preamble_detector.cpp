#include "sdrjo/adsb/preamble_detector.hpp"
#include "sdrjo/adsb/mode_s.hpp"

#include <cmath>

namespace sdrjo::adsb {

// Preambolo: impulsi da 0.5 us a 0, 1.0, 3.5, 4.5 us dall'inizio.
// A 2 campioni/us: campioni 0, 2, 7, 9 alti; il resto basso.
static constexpr int kPreambleSamples = 16;   // 8 us
static constexpr int kLongFrameBits = 112;
static constexpr int kMaxFrameSamples = kPreambleSamples + kLongFrameBits * 2;

PreambleDetector::PreambleDetector(FrameCallback cb) : callback_(std::move(cb)) {}

void PreambleDetector::processIq(const cfloat* samples, size_t n)
{
    std::vector<float> mag(n);
    for (size_t i = 0; i < n; i++) mag[i] = std::abs(samples[i]);
    processMagnitude(mag.data(), n);
}

void PreambleDetector::processMagnitude(const float* mag, size_t n)
{
    buf_.insert(buf_.end(), mag, mag + n);
    scan();
    // Conserva la coda che potrebbe contenere un frame a cavallo dei chunk.
    if (buf_.size() > size_t(kMaxFrameSamples)) {
        size_t drop = buf_.size() - kMaxFrameSamples;
        buf_.erase(buf_.begin(), buf_.begin() + ptrdiff_t(drop));
        scanPos_ = (scanPos_ > drop) ? scanPos_ - drop : 0;
    }
}

void PreambleDetector::scan()
{
    if (buf_.size() < size_t(kMaxFrameSamples)) return;
    const float* m = buf_.data();
    const size_t end = buf_.size() - kMaxFrameSamples;

    size_t j = scanPos_;
    for (; j <= end; j++) {
        // Forma del preambolo (stile dump1090).
        if (!(m[j]     > m[j + 1] &&
              m[j + 1] < m[j + 2] &&
              m[j + 2] > m[j + 3] &&
              m[j + 3] < m[j]     &&
              m[j + 4] < m[j]     &&
              m[j + 5] < m[j]     &&
              m[j + 6] < m[j]     &&
              m[j + 7] > m[j + 8] &&
              m[j + 8] < m[j + 9] &&
              m[j + 9] > m[j + 6]))
            continue;

        // Il livello alto medio deve superare nettamente lo spazio tra
        // il preambolo e i dati (campioni 10..13 devono restare bassi).
        float high = (m[j] + m[j + 2] + m[j + 7] + m[j + 9]) / 6.0f;
        if (m[j + 10] >= high || m[j + 11] >= high ||
            m[j + 12] >= high || m[j + 13] >= high)
            continue;

        // Demodula 112 bit PPM: bit=1 se la prima meta' e' piu' alta.
        uint8_t frame[14] = {0};
        const float* d = m + j + kPreambleSamples;
        for (int b = 0; b < kLongFrameBits; b++) {
            if (d[2 * b] > d[2 * b + 1])
                frame[b / 8] |= uint8_t(0x80 >> (b % 8));
        }

        int df = frame[0] >> 3;
        size_t lenBytes = (df >= 16) ? 14 : 7;

        ModeSMessage msg;
        if (decode(frame, lenBytes, msg) && callback_) {
            callback_(frame, lenBytes);
            // Salta il frame appena decodificato.
            j += kPreambleSamples + lenBytes * 8 * 2 - 1;
        }
    }
    scanPos_ = j;
}

} // namespace sdrjo::adsb
