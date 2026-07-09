//
// Modulo plugin RDS: sintonizza una stazione FM broadcast e mostra
// nome (PS), RadioText, PI e PTY.
//
#include <sdrjo/module/module.hpp>
#include <sdrjo/dsp/demod.hpp>
#include "sdrjo/rds/rds_blocks.hpp"
#include "sdrjo/rds/rds_demod.hpp"

#include <cstdio>
#include <mutex>
#include <vector>

namespace sdrjo::rds {

class RdsModule : public IModule {
public:
    static constexpr double kSampleRateHz = 240000.0;

    RdsModule()
        : fm_(kSampleRateHz, 75000.0),
          demod_(kSampleRateHz, [this](uint8_t bit) { decoder_.pushBit(bit); })
    {}

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "RDS";
        mi.version = "0.1.0";
        mi.description = "Nome stazione e RadioText dalle radio FM (RDS a 57 kHz)";
        mi.requiredSampleRateHz = kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override { host_ = &host; }
    void stop() override { host_ = nullptr; }

    void processIq(const cfloat* samples, size_t n) override
    {
        mpx_.resize(n);
        fm_.process(samples, n, mpx_.data());

        std::lock_guard<std::mutex> lk(mutex_);
        auto prevPs = std::string(decoder_.station().ps);
        demod_.processMultiplex(mpx_.data(), n);

        auto ps = std::string(decoder_.station().ps);
        if (host_ && !ps.empty() && ps != prevPs && ps.find('\0') == std::string::npos) {
            host_->log("RDS", "stazione: " + ps);
        }
    }

    StationInfo stationSnapshot() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return decoder_.station();
    }

    std::string statusJson() const override
    {
        auto st = stationSnapshot();
        auto clean = [](const char* s, size_t max) {
            std::string out;
            for (size_t i = 0; i < max && s[i]; i++)
                if (uint8_t(s[i]) >= 0x20 && s[i] != '"' && s[i] != '\\')
                    out += s[i];
            return out;
        };
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                      "{\"Stazione\":\"%s\",\"PI\":\"%04X\",\"PTY\":\"%d\","
                      "\"RadioText\":\"%s\",\"Gruppi\":\"%u\"}",
                      clean(st.ps, 8).c_str(), st.pi, st.pty,
                      clean(st.radioText, 64).c_str(), st.groupCount);
        return buf;
    }

private:
    IModuleHost* host_ = nullptr;
    dsp::FmDemodulator fm_;
    RdsDemodulator demod_;
    RdsDecoder decoder_;
    std::vector<float> mpx_;
    mutable std::mutex mutex_;
};

} // namespace sdrjo::rds

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::rds::RdsModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
