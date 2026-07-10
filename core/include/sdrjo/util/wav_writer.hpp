#pragma once
//
// Scrittura di file WAV PCM 16 bit (per lo scanner e le registrazioni
// audio). Le dimensioni nell'header vengono sistemate alla chiusura.
//
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

namespace sdrjo {

class WavWriter {
public:
    ~WavWriter() { stop(); }

    bool start(const std::string& path, int sampleRateHz, int channels = 1);

    // Campioni float [-1, 1] interleaved: convertiti e accodati.
    void write(const float* samples, size_t n);

    void stop();

    bool isOpen() const { return file_ != nullptr; }
    const std::string& path() const { return path_; }
    double secondsWritten() const
    {
        return (rate_ > 0 && channels_ > 0)
                   ? double(frames_) / double(rate_)
                   : 0.0;
    }

private:
    FILE* file_ = nullptr;
    std::string path_;
    int rate_ = 0;
    int channels_ = 0;
    uint64_t frames_ = 0; // frame = un campione per canale
};

} // namespace sdrjo
