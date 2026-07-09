#include "sdrjo/adsb/sbs_output.hpp"

#include <cstdio>
#include <ctime>

namespace sdrjo::adsb {

// Data e ora correnti nei due campi richiesti dal formato SBS.
static void nowFields(char (&dateOut)[24], char (&timeOut)[24])
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::snprintf(dateOut, sizeof(dateOut), "%04u/%02u/%02u",
                  unsigned(tm.tm_year + 1900) % 10000u,
                  unsigned(tm.tm_mon + 1) % 100u, unsigned(tm.tm_mday) % 100u);
    std::snprintf(timeOut, sizeof(timeOut), "%02u:%02u:%02u.000",
                  unsigned(tm.tm_hour) % 100u, unsigned(tm.tm_min) % 100u,
                  unsigned(tm.tm_sec) % 100u);
}

// MSG,{tipo},{sess},{acid},{hex},{flightid},{data},{ora},{data},{ora},
//     {callsign},{alt},{gs},{track},{lat},{lon},{vr},{squawk},{alert},
//     {emergency},{spi},{onground}
std::vector<std::string> toSbsLines(const ModeSMessage& msg,
                                    const Position* resolved, int altitudeFt)
{
    std::vector<std::string> out;
    if (!msg.crcOk || msg.icao == 0) return out;

    char date[24], time[24];
    nowFields(date, time);

    char line[256];
    auto prefix = [&](int type) {
        return std::snprintf(line, sizeof(line),
                             "MSG,%d,1,1,%06X,1,%s,%s,%s,%s", type, msg.icao,
                             date, time, date, time);
    };

    if (msg.hasCallsign) {
        int n = prefix(1);
        std::snprintf(line + n, sizeof(line) - n, ",%s,,,,,,,,,,",
                      msg.callsign.c_str());
        out.push_back(line);
    }
    if (msg.hasCprPosition && resolved) {
        int n = prefix(3);
        std::snprintf(line + n, sizeof(line) - n, ",,%d,,,%.5f,%.5f,,,0,0,0,0",
                      altitudeFt, resolved->latDeg, resolved->lonDeg);
        out.push_back(line);
    }
    if (msg.hasVelocity) {
        int n = prefix(4);
        std::snprintf(line + n, sizeof(line) - n, ",,,%.1f,%.1f,,,%d,,,,",
                      msg.groundSpeedKt, msg.trackDeg, msg.verticalRateFpm);
        out.push_back(line);
    }
    return out;
}

} // namespace sdrjo::adsb
