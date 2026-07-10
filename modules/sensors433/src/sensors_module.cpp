//
// Modulo plugin Sensori 433 MHz: riceve IQ a 250 kS/s centrato su
// 433.92 MHz e decodifica i sensori meteo OOK (protocollo Nexus-TH),
// stile rtl_433. Le letture finiscono nel log e nel Cockpit.
//
#include <sdrjo/module/module.hpp>
#include "sdrjo/sensors/nexus_decoder.hpp"
#include "sdrjo/sensors/ook_detector.hpp"

#include <cstdio>
#include <map>
#include <mutex>
#include <string>

namespace sdrjo::sensors {

class SensorsModule : public IModule {
public:
    SensorsModule()
        : nexus_([this](const NexusReading& r) { onReading(r); }),
          detector_([this](const Pulse& p) { nexus_.onPulse(p); })
    {}

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "Sensori 433";
        mi.version = "0.1.0";
        mi.description =
            "Sensori meteo/temperatura OOK a 433.92 MHz (Nexus-TH)";
        mi.preferredFreqHz = 433.92e6;
        mi.requiredSampleRateHz = OokDetector::kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override
    {
        host_ = &host;
        host.log("Sensori", "in ascolto su 433.92 MHz (protocollo Nexus)");
    }

    void stop() override { host_ = nullptr; }

    void processIq(const cfloat* samples, size_t n) override
    {
        detector_.processIq(samples, n);
    }

    std::string statusJson() const override
    {
        std::lock_guard<std::mutex> lk(mutex_);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "{\"Sensori\":\"%zu\",\"Letture\":\"%zu\","
                      "\"Ultima\":\"%s\"}",
                      sensors_.size(), readings_, last_.c_str());
        return buf;
    }

private:
    void onReading(const NexusReading& r)
    {
        char line[96];
        std::snprintf(line, sizeof(line),
                      "id %u ch %d: %.1f gradi C, %d%% umidita'%s", r.id,
                      r.channel, r.tempC, r.humidity,
                      r.batteryOk ? "" : " (batteria scarica)");
        {
            std::lock_guard<std::mutex> lk(mutex_);
            // Anti-doppione: ogni trasmissione ripete la trama ~12 volte.
            uint32_t key = (uint32_t(r.id) << 2) | uint32_t(r.channel);
            std::string val = line;
            if (sensors_[key] == val) return;
            sensors_[key] = val;
            readings_++;
            last_ = val;
        }
        if (host_) host_->log("Sensori", line);
    }

    IModuleHost* host_ = nullptr;
    NexusDecoder nexus_;
    OokDetector detector_;
    mutable std::mutex mutex_;
    std::map<uint32_t, std::string> sensors_;
    size_t readings_ = 0;
    std::string last_ = "-";
};

} // namespace sdrjo::sensors

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::sensors::SensorsModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
