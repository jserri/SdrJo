#include "sdrjo/source/file_source.hpp"

#include <chrono>
#include <cstdio>
#include <vector>

namespace sdrjo {

FileSource::FileSource(std::string path, double sampleRateHz, bool throttle)
    : path_(std::move(path)), rateHz_(sampleRateHz), throttle_(throttle) {}

FileSource::~FileSource() { stop(); }

bool FileSource::start(IqCallback cb)
{
    if (running_.load()) return false;
    callback_ = std::move(cb);
    running_.store(true);
    worker_ = std::thread(&FileSource::workerLoop, this);
    return true;
}

void FileSource::stop()
{
    running_.store(false);
    // Join incondizionato: il worker puo' essere gia' terminato da solo
    // (fine del file) ma il thread resta joinable finche' non lo si unisce.
    if (worker_.joinable()) worker_.join();
}

void FileSource::workerLoop()
{
    FILE* f = std::fopen(path_.c_str(), "rb");
    if (!f) { running_.store(false); return; }

    constexpr size_t kChunkSamples = 16384;
    std::vector<uint8_t> raw(kChunkSamples * 2);
    std::vector<cfloat> conv(kChunkSamples);

    using clock = std::chrono::steady_clock;
    auto next = clock::now();

    while (running_.load()) {
        size_t rd = std::fread(raw.data(), 2, kChunkSamples, f);
        if (rd == 0) break; // fine file
        convertU8Iq(raw.data(), rd, conv.data());
        if (callback_) callback_(conv.data(), rd);

        if (throttle_) {
            next += std::chrono::nanoseconds(int64_t(1e9 * double(rd) / rateHz_));
            std::this_thread::sleep_until(next);
        }
    }
    std::fclose(f);
    running_.store(false);
}

} // namespace sdrjo
