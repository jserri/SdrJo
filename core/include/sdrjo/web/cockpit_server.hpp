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
#include "../util/ring_buffer.hpp"

#include <functional>
#include <memory>
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
    // Cambio di demodulatore dal web ("WFM stereo", "NFM", "AM", ...).
    using ModeHandler = std::function<bool(const std::string& mode)>;

    CockpitServer();

    void setStatusProvider(StatusProvider p) { status_ = std::move(p); }
    void setSpectrumProvider(SpectrumProvider p) { spectrum_ = std::move(p); }
    void setTuneHandler(TuneHandler h) { tune_ = std::move(h); }
    void setModeHandler(ModeHandler h) { mode_ = std::move(h); }
    void setDeviceInfo(const std::string& name, double freqHz, double rateHz);

    // Stato del ricevitore d'ascolto, mostrato e comandato dal browser.
    void setVfoInfo(double freqHz, const std::string& mode);

    // Audio demodulato dal host (mono, 48 kHz): distribuito a tutti i
    // client collegati a /api/audio.wav, ognuno con la propria coda.
    void pushAudio(const float* mono, size_t n);

    // Password per l'accesso (utente "sdrjo"); vuota = nessuna protezione.
    void setPassword(const std::string& password)
    {
        server_.setAuth("sdrjo", password);
    }

    bool start(uint16_t port = kDefaultPort, bool bindAll = false);
    void stop();
    uint16_t port() const { return server_.port(); }

private:
    std::string statusJson();
    std::string spectrumJson();

    std::string controlJson(const std::string& query);
    void audioStream(HttpServer::StreamWriter& w);

    HttpServer server_;
    StatusProvider status_;
    SpectrumProvider spectrum_;
    TuneHandler tune_;
    ModeHandler mode_;

    std::mutex devMutex_;
    std::string deviceName_ = "nessuna sorgente";
    double freqHz_ = 0.0;
    double rateHz_ = 0.0;
    double vfoHz_ = 0.0;
    std::string vfoMode_ = "Spento";

    // Code audio per-client (16 bit, 48 kHz mono).
    std::mutex audioMutex_;
    std::vector<std::shared_ptr<RingBuffer<int16_t>>> audioClients_;
};

// Pagina HTML del cockpit (autonoma, nessuna dipendenza esterna).
const char* cockpitPageHtml();

} // namespace sdrjo
