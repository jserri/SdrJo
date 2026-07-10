#include "sdrjo/util/wav_writer.hpp"

#include <algorithm>
#include <cstring>
#include <vector>

namespace sdrjo {

static void putU32(uint8_t* p, uint32_t v)
{
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
    p[2] = uint8_t(v >> 16);
    p[3] = uint8_t(v >> 24);
}

static void putU16(uint8_t* p, uint16_t v)
{
    p[0] = uint8_t(v);
    p[1] = uint8_t(v >> 8);
}

bool WavWriter::start(const std::string& path, int sampleRateHz, int channels)
{
    stop();
    if (sampleRateHz <= 0 || channels <= 0) return false;
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    path_ = path;
    rate_ = sampleRateHz;
    channels_ = channels;
    frames_ = 0;

    // Header provvisorio (dimensioni 0): sistemato in stop().
    uint8_t h[44] = {0};
    std::memcpy(h, "RIFF", 4);
    std::memcpy(h + 8, "WAVE", 4);
    std::memcpy(h + 12, "fmt ", 4);
    putU32(h + 16, 16);                    // dimensione blocco fmt
    putU16(h + 20, 1);                     // PCM
    putU16(h + 22, uint16_t(channels));
    putU32(h + 24, uint32_t(sampleRateHz));
    putU32(h + 28, uint32_t(sampleRateHz * channels * 2)); // byte/s
    putU16(h + 32, uint16_t(channels * 2)); // block align
    putU16(h + 34, 16);                    // bit per campione
    std::memcpy(h + 36, "data", 4);
    if (std::fwrite(h, 1, sizeof(h), file_) != sizeof(h)) {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }
    return true;
}

void WavWriter::write(const float* samples, size_t n)
{
    if (!file_ || n == 0) return;
    static thread_local std::vector<int16_t> buf;
    buf.resize(n);
    for (size_t i = 0; i < n; i++) {
        float v = std::clamp(samples[i], -1.0f, 1.0f);
        buf[i] = int16_t(v * 32767.0f);
    }
    std::fwrite(buf.data(), sizeof(int16_t), n, file_);
    frames_ += n / size_t(channels_);
}

void WavWriter::stop()
{
    if (!file_) return;
    uint32_t dataBytes = uint32_t(frames_ * uint64_t(channels_) * 2);
    uint8_t sz[4];
    putU32(sz, 36 + dataBytes);
    std::fseek(file_, 4, SEEK_SET);
    std::fwrite(sz, 1, 4, file_);
    putU32(sz, dataBytes);
    std::fseek(file_, 40, SEEK_SET);
    std::fwrite(sz, 1, 4, file_);
    std::fclose(file_);
    file_ = nullptr;
}

} // namespace sdrjo
