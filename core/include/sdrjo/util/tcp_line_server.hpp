#pragma once
//
// Server TCP "push" a righe di testo: i client si collegano e ricevono il
// flusso (usato per l'uscita SBS/BaseStation del modulo ADS-B, porta 30003,
// compatibile con Virtual Radar Server, dump1090 & co.).
//
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace sdrjo {

class TcpLineServer {
public:
    TcpLineServer();
    ~TcpLineServer();

    // port = 0 sceglie una porta libera. bindAll = visibile in LAN.
    bool start(uint16_t port, bool bindAll = false);
    void stop();

    // Invia una riga (aggiunge "\r\n") a tutti i client collegati.
    void broadcast(const std::string& line);

    bool isRunning() const { return running_.load(); }
    uint16_t port() const { return port_; }
    size_t clientCount() const;

private:
    void acceptLoop();

    intptr_t listenFd_ = -1;
    uint16_t port_ = 0;
    std::thread worker_;
    std::atomic<bool> running_{false};

    mutable std::mutex clientsMutex_;
    std::vector<intptr_t> clients_;
};

} // namespace sdrjo
