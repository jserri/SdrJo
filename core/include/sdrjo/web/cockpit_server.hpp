#pragma once
//
// SdrJo Cockpit: la plancia web dell'applicazione.
//
// Una dashboard moderna servita dall'app stessa (http://localhost:8750),
// utilizzabile da qualsiasi browser, anche da tablet/telefono in LAN:
//  - spettro + waterfall live disegnati su canvas
//  - card dei moduli con lo stato in tempo reale (statusJson)
//  - controllo di sintonia e collegamenti alle UI dedicate dei moduli
//
#include "../util/http_server.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace sdrjo {

class CockpitServer {
public:
    static constexpr uint16_t kDefaultPort = 8750;

    struct ModuleStatus {
        std::string name;
        std::string description;
        std::string statusJson; // oggetto {"chiave":"valore",...}
        uint16_t webPort = 0;   // UI dedicata (0 = nessuna)
    };

    // I provider vengono chiamati dal thread HTTP a ogni richiesta.
    using StatusProvider = std::function<std::vector<ModuleStatus>()>;
    // Ritorna lo spettro corrente in dB (vuoto = nessuna sorgente attiva).
    using SpectrumProvider = std::function<std::vector<float>()>;
    // Richiesta di sintonia dall'interfaccia web (Hz); ritorna successo.
    using TuneHandler = std::function<bool(double freqHz)>;

    CockpitServer();

    void setStatusProvider(StatusProvider p) { status_ = std::move(p); }
    void setSpectrumProvider(SpectrumProvider p) { spectrum_ = std::move(p); }
    void setTuneHandler(TuneHandler h) { tune_ = std::move(h); }
    void setDeviceInfo(const std::string& name, double freqHz, double rateHz);

    bool start(uint16_t port = kDefaultPort, bool bindAll = false);
    void stop();
    uint16_t port() const { return server_.port(); }

private:
    std::string statusJson();
    std::string spectrumJson();

    HttpServer server_;
    StatusProvider status_;
    SpectrumProvider spectrum_;
    TuneHandler tune_;

    std::mutex devMutex_;
    std::string deviceName_ = "nessuna sorgente";
    double freqHz_ = 0.0;
    double rateHz_ = 0.0;
};

// Pagina HTML del cockpit (autonoma, nessuna dipendenza esterna).
const char* cockpitPageHtml();

} // namespace sdrjo
