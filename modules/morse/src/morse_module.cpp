//
// Modulo plugin Morse/CW: demodula AM/CW il canale sintonizzato e
// decodifica il testo, mostrando anche la stima WPM.
//
#include <sdrjo/module/module.hpp>
#include "sdrjo/morse/cw_decoder.hpp"

#include <cmath>
#include <mutex>
#include <vector>

namespace sdrjo::morse {

class MorseModule : public IModule {
public:
    static constexpr double kSampleRateHz = 12500.0; // canale stretto CW

    MorseModule()
        : decoder_(kSampleRateHz, [this](char c) { onChar(c); })
    {}

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "Morse (CW)";
        mi.version = "0.1.0";
        mi.description = "Decodifica telegrafia CW con stima automatica della velocita'";
        mi.requiredSampleRateHz = kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override { host_ = &host; }
    void stop() override { host_ = nullptr; }

    void processIq(const cfloat* samples, size_t n) override
    {
        // L'inviluppo del segnale IQ centrato sul tono CW e' direttamente
        // la chiave on/off: passa il modulo del campione al decoder
        // (senza rimozione DC: per l'OOK il livello assoluto e' il dato).
        magBuf_.resize(n);
        for (size_t i = 0; i < n; i++) magBuf_[i] = std::abs(samples[i]);
        std::lock_guard<std::mutex> lk(mutex_);
        decoder_.processAudio(magBuf_.data(), n);
    }

    const std::string& text() const { return decoder_.text(); }

    std::string statusJson() const override
    {
        std::lock_guard<std::mutex> lk(mutex_);
        // Ultimi 40 caratteri decodificati.
        std::string tail = decoder_.text();
        if (tail.size() > 40) tail = tail.substr(tail.size() - 40);
        std::string clean;
        for (char c : tail)
            if (uint8_t(c) >= 0x20 && c != '"' && c != '\\') clean += c;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "{\"Testo\":\"%s\",\"WPM\":\"%.0f\"}",
                      clean.c_str(), decoder_.wpm());
        return buf;
    }

private:
    void onChar(char c)
    {
        if (host_) host_->log("Morse", std::string(1, c));
    }

    IModuleHost* host_ = nullptr;
    CwDecoder decoder_;
    std::vector<float> magBuf_;
    mutable std::mutex mutex_;
};

} // namespace sdrjo::morse

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::morse::MorseModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
