#include "sdrjo/adsb/adsb_server.hpp"

#include <sdrjo/util/geo.hpp>

#include <cstdarg>
#include <cstdio>

namespace sdrjo::adsb {

static void appendf(std::string& out, const char* fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = std::vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (n > 0) out.append(buf, size_t(n));
}

std::string aircraftToJson(const AircraftTracker& tracker,
                           const std::optional<Position>& antenna)
{
    auto now = std::chrono::steady_clock::now();
    auto list = tracker.activeAircraft(60.0);

    std::string j;
    j.reserve(4096);
    j += "{";
    if (antenna) {
        appendf(j, "\"antenna\":{\"lat\":%.6f,\"lon\":%.6f},",
                antenna->latDeg, antenna->lonDeg);
    } else {
        j += "\"antenna\":null,";
    }
    appendf(j, "\"messages\":%zu,\"aircraft\":[", tracker.totalMessages());

    bool first = true;
    for (const auto& ac : list) {
        if (!first) j += ",";
        first = false;

        double seen = std::chrono::duration<double>(now - ac.lastSeen).count();
        appendf(j, "{\"icao\":\"%06X\",\"callsign\":\"%s\",\"msgs\":%u,"
                   "\"seen\":%.1f", ac.icao, ac.callsign.c_str(),
                ac.messageCount, seen);

        if (ac.hasPosition) {
            appendf(j, ",\"lat\":%.6f,\"lon\":%.6f", ac.latDeg, ac.lonDeg);
            if (antenna) {
                appendf(j, ",\"distKm\":%.1f,\"bearingDeg\":%.0f",
                        geo::haversineKm(antenna->latDeg, antenna->lonDeg,
                                         ac.latDeg, ac.lonDeg),
                        geo::bearingDeg(antenna->latDeg, antenna->lonDeg,
                                        ac.latDeg, ac.lonDeg));
            }
            if (ac.trail.size() >= 2) {
                j += ",\"trail\":[";
                for (size_t i = 0; i < ac.trail.size(); i++) {
                    if (i) j += ",";
                    appendf(j, "[%.5f,%.5f]", ac.trail[i].latDeg,
                            ac.trail[i].lonDeg);
                }
                j += "]";
            }
        }
        if (ac.hasAltitude) appendf(j, ",\"altFt\":%d", ac.altitudeFt);
        if (ac.hasVelocity) {
            appendf(j, ",\"gsKt\":%.0f,\"trackDeg\":%.0f,\"vrFpm\":%d",
                    ac.groundSpeedKt, ac.trackDeg, ac.verticalRateFpm);
        }
        j += "}";
    }
    j += "]}";
    return j;
}

// --------------------------------------------------------------------------

AdsbWebServer::AdsbWebServer(AircraftTracker& tracker, std::mutex& trackerMutex)
    : tracker_(tracker), mutex_(trackerMutex)
{
    server_.route("/", "text/html; charset=utf-8",
                  [] { return std::string(mapPageHtml()); });
    server_.route("/data/aircraft.json", "application/json", [this] {
        std::lock_guard<std::mutex> lk(mutex_);
        return aircraftToJson(tracker_, antenna_);
    });
}

void AdsbWebServer::setAntennaPosition(double latDeg, double lonDeg)
{
    std::lock_guard<std::mutex> lk(mutex_);
    antenna_ = Position{latDeg, lonDeg};
}

bool AdsbWebServer::start(uint16_t port, bool bindAll)
{
    return server_.start(port, bindAll);
}

void AdsbWebServer::stop() { server_.stop(); }

} // namespace sdrjo::adsb
