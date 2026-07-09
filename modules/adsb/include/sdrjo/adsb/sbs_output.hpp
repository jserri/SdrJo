#pragma once
//
// Uscita in formato SBS-1/BaseStation (porta 30003): il formato CSV testuale
// capito da Virtual Radar Server, PlanePlotter, readsb e simili.
//
#include "mode_s.hpp"

#include <sdrjo/util/tcp_line_server.hpp>

namespace sdrjo::adsb {

// Converte un messaggio decodificato nelle righe SBS corrispondenti
// (MSG,1 identificazione; MSG,3 posizione+quota; MSG,4 velocita').
// resolved: posizione gia' risolta dal tracker (per la MSG,3), se nota.
std::vector<std::string> toSbsLines(const ModeSMessage& msg,
                                    const Position* resolved = nullptr,
                                    int altitudeFt = 0);

class SbsOutput {
public:
    static constexpr uint16_t kDefaultPort = 30003;

    // bindAll = true per collegare programmi da altri PC della LAN.
    bool start(uint16_t port = kDefaultPort, bool bindAll = false)
    {
        return server_.start(port, bindAll);
    }
    void stop() { server_.stop(); }

    void publish(const ModeSMessage& msg, const Position* resolved = nullptr,
                 int altitudeFt = 0)
    {
        if (!server_.isRunning() || server_.clientCount() == 0) return;
        for (const auto& line : toSbsLines(msg, resolved, altitudeFt))
            server_.broadcast(line);
    }

    uint16_t port() const { return server_.port(); }
    size_t clientCount() const { return server_.clientCount(); }

private:
    TcpLineServer server_;
};

} // namespace sdrjo::adsb
