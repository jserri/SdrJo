#include "sdrjo/adsb/aircraft_tracker.hpp"

namespace sdrjo::adsb {

void AircraftTracker::update(const ModeSMessage& msg)
{
    if (!msg.crcOk || msg.icao == 0) return;

    auto now = std::chrono::steady_clock::now();
    Aircraft& ac = aircraft_[msg.icao];
    ac.icao = msg.icao;
    ac.lastSeen = now;
    ac.messageCount++;
    totalMessages_++;

    if (msg.hasCallsign) ac.callsign = msg.callsign;

    if (msg.hasAltitude) {
        ac.altitudeFt = msg.altitudeFt;
        ac.hasAltitude = true;
    }

    if (msg.hasVelocity) {
        ac.groundSpeedKt = msg.groundSpeedKt;
        ac.trackDeg = msg.trackDeg;
        ac.verticalRateFpm = msg.verticalRateFpm;
        ac.hasVelocity = true;
    }

    if (msg.hasCprPosition) {
        if (msg.cprOdd) {
            ac.cprLatOdd = msg.cprLat;
            ac.cprLonOdd = msg.cprLon;
            ac.cprOddTime = now;
            ac.hasCprOdd = true;
        } else {
            ac.cprLatEven = msg.cprLat;
            ac.cprLonEven = msg.cprLon;
            ac.cprEvenTime = now;
            ac.hasCprEven = true;
        }

        // La coppia even/odd va usata solo se ravvicinata (< 10 s),
        // altrimenti l'aereo puo' aver cambiato zona CPR.
        if (ac.hasCprEven && ac.hasCprOdd) {
            auto dt = (ac.cprEvenTime > ac.cprOddTime)
                          ? ac.cprEvenTime - ac.cprOddTime
                          : ac.cprOddTime - ac.cprEvenTime;
            if (dt < std::chrono::seconds(10)) {
                auto pos = cprGlobalDecode(ac.cprLatEven, ac.cprLonEven,
                                           ac.cprLatOdd, ac.cprLonOdd,
                                           msg.cprOdd);
                if (pos) {
                    ac.latDeg = pos->latDeg;
                    ac.lonDeg = pos->lonDeg;
                    ac.hasPosition = true;
                }
            }
        }
    }
}

std::vector<Aircraft> AircraftTracker::activeAircraft(double maxAgeS) const
{
    std::vector<Aircraft> out;
    auto now = std::chrono::steady_clock::now();
    for (const auto& [icao, ac] : aircraft_) {
        auto age = std::chrono::duration<double>(now - ac.lastSeen).count();
        if (age <= maxAgeS) out.push_back(ac);
    }
    return out;
}

} // namespace sdrjo::adsb
