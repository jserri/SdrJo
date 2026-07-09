//
// Modulo plugin NOAA APT: immagini meteo analogiche dei NOAA 15/18/19.
// Frequenze: NOAA 15 = 137.620, NOAA 18 = 137.9125, NOAA 19 = 137.100 MHz.
//
#include <sdrjo/module/module.hpp>
#include <sdrjo/dsp/demod.hpp>
#include <sdrjo/util/bmp_writer.hpp>
#include "sdrjo/apt/apt_decoder.hpp"

#include <mutex>
#include <string>
#include <vector>

namespace sdrjo::apt {

class AptModule : public IModule {
public:
    static constexpr double kSampleRateHz = 48000.0;

    AptModule() : fm_(kSampleRateHz, 17000.0), decoder_(kSampleRateHz) {}

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "NOAA APT";
        mi.version = "0.1.0";
        mi.description = "Immagini meteo dai satelliti NOAA 15/18/19 (APT su 137 MHz)";
        mi.preferredFreqHz = 137.1e6; // NOAA 19
        mi.requiredSampleRateHz = kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override
    {
        host_ = &host;
        host.log("APT", "in attesa del passaggio del satellite...");
    }

    void stop() override
    {
        saveImage();
        host_ = nullptr;
    }

    void processIq(const cfloat* samples, size_t n) override
    {
        audio_.resize(n);
        fm_.process(samples, n, audio_.data());

        std::lock_guard<std::mutex> lk(mutex_);
        int prevRows = decoder_.rows();
        decoder_.processAudio(audio_.data(), n);

        // Salva l'immagine parziale ogni 50 righe nuove (25 s di passaggio).
        if (decoder_.rows() / 50 != prevRows / 50) saveImage();
    }

    // Percorso dell'ultima immagine salvata (per GUI/cockpit).
    std::string lastImagePath() const
    {
        std::lock_guard<std::mutex> lk(mutex_);
        return lastImagePath_;
    }

    std::string statusJson() const override
    {
        std::lock_guard<std::mutex> lk(mutex_);
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "{\"Righe\":\"%d\",\"Con sync\":\"%d\","
                      "\"Immagine\":\"%s\"}",
                      decoder_.rows(), decoder_.syncedRows(),
                      lastImagePath_.empty() ? "-" : lastImagePath_.c_str());
        return buf;
    }

private:
    void saveImage()
    {
        if (decoder_.rows() < 10) return;
        std::string path = "apt_ricezione.bmp";
        if (writeGrayscaleBmp(path, decoder_.image().data(), kPixelsPerLine,
                              decoder_.rows())) {
            lastImagePath_ = path;
            if (host_) {
                host_->log("APT", "immagine aggiornata: " + path + " (" +
                                      std::to_string(decoder_.rows()) +
                                      " righe)");
            }
        }
    }

    IModuleHost* host_ = nullptr;
    dsp::FmDemodulator fm_;
    AptDecoder decoder_;
    std::vector<float> audio_;
    std::string lastImagePath_;
    mutable std::mutex mutex_;
};

} // namespace sdrjo::apt

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::apt::AptModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
