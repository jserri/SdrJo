#pragma once
//
// Mini server HTTP integrato (senza dipendenze, Windows/Linux) usato per
// servire le interfacce web dei moduli, come la mappa ADS-B.
// Pensato per uso locale: di default ascolta solo su 127.0.0.1.
//
#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdrjo {

class HttpServer {
public:
    // Il gestore restituisce il corpo della risposta a ogni richiesta GET;
    // riceve la query string ("freq=100000000&mode=NFM", vuota se assente).
    using Handler = std::function<std::string(const std::string& query)>;

    // Scrittore per le risposte in streaming (audio, eventi):
    // write() ritorna false quando il client si disconnette.
    class StreamWriter {
    public:
        StreamWriter(intptr_t fd, std::atomic<bool>& serverRunning)
            : fd_(fd), running_(serverRunning) {}
        bool write(const void* data, size_t len);
        bool alive() const;

    private:
        intptr_t fd_;
        std::atomic<bool>& running_;
        bool dead_ = false;
    };
    using StreamHandler = std::function<void(StreamWriter&)>;

    HttpServer();
    ~HttpServer();

    // Registra un percorso ("/", "/data/aircraft.json", ...).
    void route(const std::string& path, const std::string& contentType,
               Handler handler);

    // Rotta in streaming: il gestore gira in un thread dedicato e scrive
    // sul client finche' vuole (es. /api/audio.wav). Header inviato subito.
    void streamRoute(const std::string& path, const std::string& contentType,
                     StreamHandler handler);

    // Protegge TUTTE le rotte con HTTP Basic Auth (password vuota = off).
    // Nota: Basic Auth su HTTP in chiaro va bene in LAN; per l'accesso da
    // internet usare una VPN (vedi README, sezione accesso remoto).
    void setAuth(const std::string& username, const std::string& password);

    // Avvia il server. port = 0 sceglie una porta libera (vedi port()).
    // bindAll = true ascolta su tutte le interfacce (visibile in LAN).
    bool start(uint16_t port, bool bindAll = false);
    void stop();

    bool isRunning() const { return running_.load(); }
    uint16_t port() const { return port_; }

private:
    struct Route {
        std::string contentType;
        Handler handler;
    };

    void acceptLoop();
    void handleClient(intptr_t clientFd);

    struct StreamRoute {
        std::string contentType;
        StreamHandler handler;
    };

    std::map<std::string, Route> routes_;
    std::map<std::string, StreamRoute> streamRoutes_;
    std::string authToken_; // base64(user:pass); vuoto = nessuna auth

    std::mutex streamMutex_;
    std::vector<intptr_t> streamSockets_;
    std::vector<std::thread> streamThreads_;
    intptr_t listenFd_ = -1;
    uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace sdrjo
