#include "sdrjo/util/iq_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <vector>

namespace sdrjo {

bool IqRecorder::start(const std::string& path, double centerFreqHz,
                       double sampleRateHz)
{
    stop();
    file_ = std::fopen(path.c_str(), "wb");
    if (!file_) return false;
    path_ = path;
    rateHz_ = sampleRateHz;
    bytesWritten_ = 0;

    // Metadati leggibili di fianco alla registrazione.
    if (FILE* meta = std::fopen((path + ".txt").c_str(), "w")) {
        std::time_t t = std::time(nullptr);
        char when[64] = {0};
        std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S",
                      std::localtime(&t));
        std::fprintf(meta,
                     "formato: u8 IQ interleaved (compatibile rtl_sdr)\n"
                     "frequenza_hz: %.0f\nsample_rate_hz: %.0f\ndata: %s\n",
                     centerFreqHz, sampleRateHz, when);
        std::fclose(meta);
    }
    return true;
}

void IqRecorder::stop()
{
    if (file_) {
        std::fclose(file_);
        file_ = nullptr;
    }
}

void IqRecorder::write(const cfloat* samples, size_t n)
{
    if (!file_) return;
    std::vector<uint8_t> raw(n * 2);
    for (size_t i = 0; i < n; i++) {
        float re = std::clamp(samples[i].real(), -1.0f, 1.0f);
        float im = std::clamp(samples[i].imag(), -1.0f, 1.0f);
        raw[2 * i] = uint8_t(std::lround(re * 127.0f + 127.5f));
        raw[2 * i + 1] = uint8_t(std::lround(im * 127.0f + 127.5f));
    }
    bytesWritten_ += std::fwrite(raw.data(), 1, raw.size(), file_);
}

} // namespace sdrjo
