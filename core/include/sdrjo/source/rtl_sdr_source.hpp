#pragma once
//
// Sorgente RTL-SDR basata su librtlsdr.
//
// NOTA per RTL-SDR Blog V4: la V4 monta il tuner R828D e un upconverter
// interno per le HF; e' PIENAMENTE supportata solo dal driver del fork
// "rtl-sdr-blog" (https://github.com/rtlsdrblog/rtl-sdr-blog).
// Su Windows: installare il driver WinUSB con Zadig e mettere le DLL del
// fork accanto all'eseguibile. Con la librtlsdr "vanilla" vecchia la V4
// sintonizza male sotto i 28 MHz e con offset di frequenza.
//
#include "sample_source.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

#if defined(SDRJO_HAVE_RTLSDR)

struct rtlsdr_dev; // forward (da rtl-sdr.h)

namespace sdrjo {

class RtlSdrSource : public ISampleSource {
public:
    struct DeviceDesc {
        uint32_t index;
        std::string name;
        std::string serial;
    };
    static std::vector<DeviceDesc> enumerate();

    explicit RtlSdrSource(uint32_t deviceIndex = 0);
    ~RtlSdrSource() override;

    std::string name() const override;

    bool setCenterFrequency(double hz) override;
    double centerFrequency() const override { return freqHz_; }

    bool setSampleRate(double hz) override;
    double sampleRate() const override { return rateHz_; }

    bool setGain(double gainDb) override;

    // Correzione PPM del quarzo (le V4 hanno TCXO: di solito 0).
    bool setPpmCorrection(int ppm);

    // Bias-T per alimentare LNA esterni (utile per LRPT/ADS-B).
    // Richiede librtlsdr recente (fork rtl-sdr-blog o >= 0.8).
    bool setBiasTee(bool on);

    bool start(IqCallback cb) override;
    void stop() override;
    bool isRunning() const override { return running_.load(); }

private:
    void workerLoop();

    rtlsdr_dev* dev_ = nullptr;
    double freqHz_ = 100e6;
    double rateHz_ = 2.4e6;
    IqCallback callback_;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace sdrjo

#endif // SDRJO_HAVE_RTLSDR
