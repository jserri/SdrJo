#pragma once
//
// Tabella degli aerei visti: accumula i messaggi decodificati per ICAO,
// combina le coppie CPR even/odd in posizioni reali e scarta i contatti
// non piu' aggiornati.
//
#include "mode_s.hpp"

#include <chrono>
#include <map>
#include <vector>

namespace sdrjo::adsb {

struct Aircraft {
    uint32_t icao = 0;
    std::string callsign;
    bool hasPosition = false;
    double latDeg = 0.0, lonDeg = 0.0;
    // Scia: ultime posizioni note, per disegnare la traiettoria sulla mappa.
    std::vector<Position> trail;
    int altitudeFt = 0;
    bool hasAltitude = false;
    double groundSpeedKt = 0.0;
    double trackDeg = 0.0;
    int verticalRateFpm = 0;
    bool hasVelocity = false;
    uint32_t messageCount = 0;
    std::chrono::steady_clock::time_point lastSeen;

    // Stato CPR in attesa della coppia.
    bool hasCprEven = false, hasCprOdd = false;
    double cprLatEven = 0, cprLonEven = 0, cprLatOdd = 0, cprLonOdd = 0;
    std::chrono::steady_clock::time_point cprEvenTime, cprOddTime;
};

class AircraftTracker {
public:
    // Integra un messaggio decodificato nella tabella.
    void update(const ModeSMessage& msg);

    // Elenco dei contatti attivi (visti negli ultimi maxAgeS secondi).
    std::vector<Aircraft> activeAircraft(double maxAgeS = 60.0) const;

    size_t totalMessages() const { return totalMessages_; }

private:
    std::map<uint32_t, Aircraft> aircraft_;
    size_t totalMessages_ = 0;
};

} // namespace sdrjo::adsb
