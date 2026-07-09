//
// Modulo plugin ADS-B: riceve IQ a 2.0 MS/s centrato su 1090 MHz,
// rileva i frame Mode S e mantiene la tabella degli aerei.
//
#include <sdrjo/module/module.hpp>
#include "sdrjo/adsb/adsb_server.hpp"
#include "sdrjo/adsb/aircraft_tracker.hpp"
#include "sdrjo/adsb/preamble_detector.hpp"

#include <cstdio>
#include <memory>
#include <mutex>

namespace sdrjo::adsb {

class AdsbModule : public IModule {
public:
    AdsbModule()
        : detector_([this](const uint8_t* frame, size_t len) {
              onFrame(frame, len);
          })
    {}

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "ADS-B";
        mi.version = "0.1.0";
        mi.description = "Monitoraggio aerei su 1090 MHz (Mode S / Extended Squitter)";
        mi.preferredFreqHz = 1090e6;
        mi.requiredSampleRateHz = PreambleDetector::kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override
    {
        host_ = &host;
        host.requestTune(1090e6, PreambleDetector::kSampleRateHz);
        host.log("ADS-B", "in ascolto su 1090 MHz");

        // Avvia la mappa web dei voli (stile SDRAngel / tar1090).
        web_ = std::make_unique<AdsbWebServer>(tracker_, mutex_);
        if (web_->start()) {
            host.log("ADS-B", "mappa voli su http://localhost:" +
                                  std::to_string(web_->port()));
        } else {
            host.log("ADS-B", "porta web occupata: mappa non disponibile");
            web_.reset();
        }
    }

    void stop() override
    {
        if (web_) web_->stop();
        web_.reset();
        host_ = nullptr;
    }

    void processIq(const cfloat* samples, size_t n) override
    {
        detector_.processIq(samples, n);
    }

    void drawUi() override
    {
        // La tabella aerei viene disegnata dall'host GUI leggendo
        // activeAircraft(); qui non serve altro per la versione CLI.
    }

    AircraftTracker& tracker() { return tracker_; }

private:
    void onFrame(const uint8_t* frame, size_t len)
    {
        ModeSMessage msg;
        if (!decode(frame, len, msg)) return;

        std::lock_guard<std::mutex> lk(mutex_);
        tracker_.update(msg);

        if (host_ && msg.hasCallsign) {
            char line[64];
            std::snprintf(line, sizeof(line), "ICAO %06X volo %s",
                          msg.icao, msg.callsign.c_str());
            host_->log("ADS-B", line);
        }
    }

    IModuleHost* host_ = nullptr;
    PreambleDetector detector_;
    AircraftTracker tracker_;
    std::mutex mutex_;
    std::unique_ptr<AdsbWebServer> web_;
};

} // namespace sdrjo::adsb

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::adsb::AdsbModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
