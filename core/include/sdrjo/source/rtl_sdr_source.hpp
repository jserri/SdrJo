#pragma once
//
// Sorgente RTL-SDR con caricamento di librtlsdr A RUNTIME (come SDR#):
// nessuna dipendenza in fase di build. All'avvio si cerca:
//   - Windows: rtlsdr.dll / librtlsdr.dll accanto all'eseguibile o nel PATH
//   - Linux:   librtlsdr.so.2 / .so.0 / .so
//
// NOTA per RTL-SDR Blog V4: la V4 monta il tuner R828D ed e' PIENAMENTE
// supportata solo dalle DLL del fork "rtl-sdr-blog"
// (https://github.com/rtlsdrblog/rtl-sdr-blog/releases). Scaricare la
// versione x86 o x64 corrispondente alla build dell'app. Su Windows va
// prima installato il driver WinUSB con Zadig. Con la librtlsdr "vanilla"
// vecchia la V4 sintonizza con offset ed e' sorda sotto i 28 MHz.
//
#include "sample_source.hpp"

#include <atomic>
#include <string>
#include <thread>
#include <vector>

struct rtlsdr_dev; // opaco, definito dentro librtlsdr

namespace sdrjo {

class RtlSdrSource : public ISampleSource {
public:
    // true se librtlsdr e' stata trovata e caricata correttamente.
    static bool available();

    // Messaggio d'aiuto quando available() == false (quale file manca).
    static std::string libraryHint();

    struct DeviceDesc {
        uint32_t index;
        std::string name;
        std::string serial;
    };
    // Elenco chiavette collegate (vuoto se libreria assente).
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
    // Ritorna false se la DLL caricata non espone la funzione.
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
