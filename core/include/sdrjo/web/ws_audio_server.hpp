#pragma once
//
// Streaming audio a bassa latenza per il Cockpit: WebSocket (RFC 6455)
// con blocchi IMA ADPCM da 20 ms (4:1 rispetto al PCM del WAV).
//
// Sicurezza: ogni connessione deve presentare ?token=... nel handshake;
// il token viene generato a caso a ogni avvio e consegnato SOLO dalla
// pagina del Cockpit (che a sua volta puo' essere protetta da password).
//
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdrjo {

class WsAudioServer {
public:
    static constexpr uint16_t kDefaultPort = 8751;

    ~WsAudioServer();

    // bindAll = false ascolta solo su localhost.
    bool start(uint16_t port = kDefaultPort, bool bindAll = false);
    void stop();
    bool isRunning() const { return running_.load(); }
    uint16_t port() const { return port_; }

    // Token richiesto nel query string del handshake (?token=...).
    void setToken(const std::string& token);
    std::string token() const;

    // Audio mono 48 kHz dal thread DSP: accumulato in blocchi da 960
    // campioni (20 ms), codificato una volta e inviato a tutti i client.
    void pushAudio(const float* mono, size_t n);

    size_t clientCount() const;

private:
    struct Client;
    void acceptLoop();
    void clientLoop(std::shared_ptr<Client> c);

    std::atomic<bool> running_{false};
    uint16_t port_ = 0;
    intptr_t listenFd_ = -1;
    std::thread acceptThread_;

    mutable std::mutex mutex_; // protegge token_ e clients_
    std::string token_;
    std::vector<std::shared_ptr<Client>> clients_;

    std::vector<float> pending_; // campioni in attesa del blocco pieno
};

} // namespace sdrjo
