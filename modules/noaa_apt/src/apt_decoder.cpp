#include "sdrjo/apt/apt_decoder.hpp"

#include <algorithm>
#include <cmath>

namespace sdrjo::apt {

// Pattern del sync A: 7 cicli di onda quadra a 1040 Hz (4 pixel per ciclo)
// preceduti e seguiti da livello basso.
static std::vector<float> syncPattern()
{
    std::vector<float> p(kSyncLength, -1.0f);
    for (int c = 0; c < 7; c++) {
        p[4 + c * 4] = 1.0f;
        p[4 + c * 4 + 1] = 1.0f;
    }
    return p;
}

AptDecoder::AptDecoder(double audioRate)
    : audioRate_(audioRate),
      shifter_(audioRate, -2400.0),
      lowpass_(dsp::designLowPass(audioRate, 2000.0, 63)),
      resampler_(audioRate, kPixelRate)
{
}

void AptDecoder::processAudio(const float* samples, size_t n)
{
    // Inviluppo coerente della sottoportante AM a 2400 Hz.
    std::vector<cfloat> cplx(n), mixed(n), filtered(n);
    for (size_t i = 0; i < n; i++) cplx[i] = cfloat(samples[i], 0.0f);
    shifter_.process(cplx.data(), n, mixed.data());
    lowpass_.process(mixed.data(), n, filtered.data());

    std::vector<float> env(n);
    for (size_t i = 0; i < n; i++) env[i] = 2.0f * std::abs(filtered[i]);

    resampler_.process(env.data(), n, pixels_);
    processPixels();
}

void AptDecoder::processPixels()
{
    const auto pattern = syncPattern();

    // Servono almeno due righe nel buffer per cercare il sync in una riga
    // intera e poi estrarne una completa.
    while (pixels_.size() >= size_t(kPixelsPerLine * 2)) {
        // Aggiorna i livelli per la normalizzazione (percentile approssimato).
        float lo = pixels_[0], hi = pixels_[0];
        for (int i = 0; i < kPixelsPerLine; i++) {
            lo = std::min(lo, pixels_[i]);
            hi = std::max(hi, pixels_[i]);
        }
        levelLow_ += 0.2f * (lo - levelLow_);
        levelHigh_ += 0.2f * (hi - levelHigh_);
        float mid = 0.5f * (levelLow_ + levelHigh_);
        float span = std::max(1e-6f, levelHigh_ - levelLow_);

        // Correlazione con il pattern di sync su una riga intera.
        int bestPos = 0;
        float bestCorr = -1e9f;
        for (int pos = 0; pos <= kPixelsPerLine; pos++) {
            float corr = 0;
            for (int k = 0; k < kSyncLength; k++)
                corr += (pixels_[pos + k] - mid) * pattern[k];
            if (corr > bestCorr) { bestCorr = corr; bestPos = pos; }
        }

        // Correlazione normalizzata: > 0.35 = sync affidabile.
        float quality = bestCorr / (0.5f * span * kSyncLength);
        int start = 0;
        if (quality > 0.35f) {
            start = bestPos;
            syncedRows_++;
        }
        if (pixels_.size() < size_t(start + kPixelsPerLine)) return;

        // Estrai la riga normalizzata a 8 bit.
        size_t base = image_.size();
        image_.resize(base + kPixelsPerLine);
        for (int i = 0; i < kPixelsPerLine; i++) {
            float v = (pixels_[start + i] - levelLow_) / span;
            image_[base + i] = uint8_t(std::clamp(v * 255.0f, 0.0f, 255.0f));
        }
        pixels_.erase(pixels_.begin(), pixels_.begin() + start + kPixelsPerLine);
    }
}

} // namespace sdrjo::apt
