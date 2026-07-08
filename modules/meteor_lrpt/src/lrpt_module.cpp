//
// Modulo plugin Meteor-M LRPT: immagini meteo digitali su 137 MHz.
//
// Frequenze utili (verificare lo stato attuale dei satelliti):
//   Meteor-M N2-3: 137.9 MHz  (a volte spento o su 80k simboli/s)
//   Meteor-M N2-4: 137.1 MHz
//
// Catena implementata:  QPSK -> Viterbi -> sync/derandom CADU.
// TODO: Reed-Solomon (255,223), demultiplex VCDU->MPDU, decodifica MCU
//       JPEG e composizione dell'immagine multibanda (vedi docs/ROADMAP.md).
//
#include <sdrjo/module/module.hpp>
#include "sdrjo/lrpt/qpsk_demodulator.hpp"
#include "sdrjo/lrpt/viterbi27.hpp"
#include "sdrjo/lrpt/ccsds.hpp"

#include <cstdio>

namespace sdrjo::lrpt {

class LrptModule : public IModule {
public:
    static constexpr double kSampleRateHz = 288000.0; // 4 campioni/simbolo

    LrptModule() : demod_(kSampleRateHz, 72000.0)
    {
        demod_.setSymbolCallback([this](uint8_t i, uint8_t q) {
            softBits_.push_back(i);
            softBits_.push_back(q);
            if (softBits_.size() >= kViterbiChunk) drainSoftBits();
        });
    }

    ModuleInfo info() const override
    {
        ModuleInfo mi;
        mi.name = "Meteor LRPT";
        mi.version = "0.1.0";
        mi.description = "Immagini meteo dai satelliti Meteor-M N2-3/N2-4 (137 MHz)";
        mi.preferredFreqHz = 137.1e6; // Meteor-M N2-4
        mi.requiredSampleRateHz = kSampleRateHz;
        return mi;
    }

    void start(IModuleHost& host) override
    {
        host_ = &host;
        host.requestTune(mi_.preferredFreqHz > 0 ? mi_.preferredFreqHz : 137.1e6,
                         kSampleRateHz);
        host.log("LRPT", "in attesa del passaggio del satellite...");
    }

    void stop() override { host_ = nullptr; }

    void processIq(const cfloat* samples, size_t n) override
    {
        demod_.processIq(samples, n);
    }

    size_t caduCount() const { return caduCount_; }

private:
    static constexpr size_t kViterbiChunk = kCaduBytes * 8 * 2 * 4;

    void drainSoftBits()
    {
        // Viterbi sul blocco accumulato...
        auto bits = Viterbi27::decodeSoft(softBits_);
        softBits_.clear();

        // ...poi ricerca dei CADU nel flusso decodificato.
        bitStream_.insert(bitStream_.end(), bits.begin(), bits.end());
        bool inverted = false;
        while (auto off = findSync(bitStream_, inverted)) {
            if (bitStream_.size() - *off < kCaduBytes * 8) break; // CADU incompleto
            auto cadu = packBits(bitStream_, *off, kCaduBytes * 8);
            if (inverted)
                for (auto& b : cadu) b = uint8_t(~b);
            derandomize(cadu.data() + 4, kCaduDataBytes);
            caduCount_++;
            if (host_ && caduCount_ % 100 == 1) {
                char line[64];
                std::snprintf(line, sizeof(line), "%zu CADU ricevuti", caduCount_);
                host_->log("LRPT", line);
            }
            // TODO: passare il CADU a Reed-Solomon + assembler immagine.
            bitStream_.erase(bitStream_.begin(),
                             bitStream_.begin() + ptrdiff_t(*off + kCaduBytes * 8));
        }
        // Evita crescita illimitata se non c'e' segnale.
        if (bitStream_.size() > kCaduBytes * 8 * 4)
            bitStream_.erase(bitStream_.begin(),
                             bitStream_.end() - ptrdiff_t(kCaduBytes * 8));
    }

    IModuleHost* host_ = nullptr;
    ModuleInfo mi_;
    QpskDemodulator demod_;
    std::vector<uint8_t> softBits_;
    std::vector<uint8_t> bitStream_;
    size_t caduCount_ = 0;
};

} // namespace sdrjo::lrpt

extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{
    return new sdrjo::lrpt::LrptModule();
}

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{
    return sdrjo::kModuleAbiVersion;
}
