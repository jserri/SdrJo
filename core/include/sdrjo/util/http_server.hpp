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
#include <string>
#include <thread>

namespace sdrjo {

class HttpServer {
public:
    // Il gestore restituisce il corpo della risposta a ogni richiesta GET.
    using Handler = std::function<std::string()>;

    HttpServer();
    ~HttpServer();

    // Registra un percorso ("/", "/data/aircraft.json", ...).
    void route(const std::string& path, const std::string& contentType,
               Handler handler);

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

    std::map<std::string, Route> routes_;
    std::string authToken_; // base64(user:pass); vuoto = nessuna auth
    intptr_t listenFd_ = -1;
    uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> running_{false};
};

} // namespace sdrjo
