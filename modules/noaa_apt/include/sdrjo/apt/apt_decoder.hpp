#pragma once
//
// Decoder APT dei satelliti NOAA (15/18/19): dal segnale audio demodulato
// FM (multiplex con sottoportante AM a 2400 Hz) all'immagine.
//
// Formato APT: 2 linee/s, 2080 pixel/linea (word rate 4160 Hz), ogni linea
// contiene sync A + canale video A + sync B + canale video B (telemetria).
//
#include <sdrjo/dsp/fir_filter.hpp>
#include <sdrjo/dsp/resampler.hpp>

#include <cstdint>
#include <vector>

namespace sdrjo::apt {

constexpr int kPixelsPerLine = 2080;
constexpr double kPixelRate = 4160.0;
constexpr int kSyncLength = 39;

class AptDecoder {
public:
    // audioRate: sample rate dell'audio FM-demodulato in ingresso.
    explicit AptDecoder(double audioRate);

    // Elabora un blocco di audio; le righe complete si accumulano in image().
    void processAudio(const float* samples, size_t n);

    // Immagine ricevuta finora: rows() x kPixelsPerLine, 8 bit.
    const std::vector<uint8_t>& image() const { return image_; }
    int rows() const { return int(image_.size() / kPixelsPerLine); }

    // Quante righe hanno agganciato il sync (qualita' della ricezione).
    int syncedRows() const { return syncedRows_; }

private:
    void processPixels();

    double audioRate_;
    dsp::FrequencyShifter shifter_;   // -2400 Hz
    dsp::FirFilter lowpass_;          // inviluppo del video
    dsp::LinearResampler resampler_;  // -> 4160 pixel/s

    std::vector<float> pixels_;       // pixel normalizzandi in coda
    std::vector<uint8_t> image_;
    int syncedRows_ = 0;

    // Normalizzazione adattiva del contrasto.
    float levelLow_ = 0.0f, levelHigh_ = 1e-3f;
};

} // namespace sdrjo::apt
