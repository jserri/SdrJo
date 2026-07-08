#include "sdrjo/adsb/mode_s.hpp"

#include <cmath>
#include <cstring>

namespace sdrjo::adsb {

uint32_t crc24(const uint8_t* data, size_t lenBytes)
{
    constexpr uint32_t kPoly = 0xFFF409;
    uint32_t crc = 0;
    for (size_t i = 0; i < lenBytes; i++) {
        crc ^= uint32_t(data[i]) << 16;
        for (int b = 0; b < 8; b++) {
            crc <<= 1;
            if (crc & 0x1000000) crc ^= kPoly;
        }
    }
    return crc & 0xFFFFFF;
}

// Alfabeto a 6 bit dei callsign (Annex 10): '#' = carattere non valido.
static const char kCallsignCharset[65] =
    "#ABCDEFGHIJKLMNOPQRSTUVWXYZ##### ###############0123456789######";

static void decodeCallsign(const uint8_t* me, ModeSMessage& out)
{
    // 8 caratteri da 6 bit nei byte ME[1..6] (bit 41..88 del messaggio).
    uint64_t bits = 0;
    for (int i = 1; i <= 6; i++) bits = (bits << 8) | me[i];
    char cs[9];
    for (int i = 0; i < 8; i++) {
        int idx = int((bits >> (42 - 6 * i)) & 0x3F);
        cs[i] = kCallsignCharset[idx];
    }
    cs[8] = '\0';
    // Rimuovi gli spazi finali.
    for (int i = 7; i >= 0 && (cs[i] == ' ' || cs[i] == '#'); i--) cs[i] = '\0';
    out.callsign = cs;
    out.hasCallsign = true;
}

static void decodeAirbornePosition(const uint8_t* msg, ModeSMessage& out)
{
    // Altitudine barometrica: 12 bit (bit 41..52 del messaggio).
    int alt12 = ((msg[5] << 4) | (msg[6] >> 4)) & 0xFFF;
    if (alt12 != 0) {
        if (alt12 & 0x10) { // Q-bit: passo di 25 ft
            int n = ((alt12 & 0xFE0) >> 1) | (alt12 & 0x0F);
            out.altitudeFt = n * 25 - 1000;
            out.hasAltitude = true;
        }
        // Q=0 (passo 100 ft, codifica Gillham) non gestito in questa versione.
    }

    out.cprOdd = (msg[6] >> 2) & 1;
    int lat17 = ((msg[6] & 0x03) << 15) | (msg[7] << 7) | (msg[8] >> 1);
    int lon17 = ((msg[8] & 0x01) << 16) | (msg[9] << 8) | msg[10];
    out.cprLat = double(lat17) / 131072.0;
    out.cprLon = double(lon17) / 131072.0;
    out.hasCprPosition = true;
}

static void decodeVelocity(const uint8_t* msg, ModeSMessage& out)
{
    int subtype = msg[4] & 0x07;
    if (subtype != 1 && subtype != 2) return; // solo ground speed per ora

    int ewSign = (msg[5] >> 2) & 1;
    int ewVel = ((msg[5] & 0x03) << 8) | msg[6];
    int nsSign = (msg[7] >> 7) & 1;
    int nsVel = ((msg[7] & 0x7F) << 3) | (msg[8] >> 5);
    if (ewVel == 0 || nsVel == 0) return; // velocita' non disponibile

    double vew = double(ewVel - 1) * (ewSign ? -1.0 : 1.0);
    double vns = double(nsVel - 1) * (nsSign ? -1.0 : 1.0);
    if (subtype == 2) { vew *= 4.0; vns *= 4.0; } // supersonico

    out.groundSpeedKt = std::sqrt(vew * vew + vns * vns);
    out.trackDeg = std::atan2(vew, vns) * 180.0 / M_PI;
    if (out.trackDeg < 0) out.trackDeg += 360.0;

    int vrSign = (msg[8] >> 3) & 1;
    int vr = ((msg[8] & 0x07) << 6) | (msg[9] >> 2);
    if (vr != 0) out.verticalRateFpm = (vr - 1) * 64 * (vrSign ? -1 : 1);

    out.hasVelocity = true;
}

bool decode(const uint8_t* frame, size_t lenBytes, ModeSMessage& out)
{
    if (lenBytes != 7 && lenBytes != 14) return false;

    out = ModeSMessage{};
    std::memcpy(out.raw, frame, lenBytes);
    out.lenBytes = int(lenBytes);
    out.df = frame[0] >> 3;

    switch (out.df) {
    case 17: // ADS-B Extended Squitter
    case 18: // TIS-B / ADS-R
        if (lenBytes != 14) return false;
        out.crcOk = (crc24(frame, 14) == 0);
        if (!out.crcOk) return false;
        out.icao = (uint32_t(frame[1]) << 16) | (uint32_t(frame[2]) << 8) | frame[3];
        out.typeCode = frame[4] >> 3;

        if (out.typeCode >= 1 && out.typeCode <= 4)
            decodeCallsign(frame + 4, out);
        else if (out.typeCode >= 9 && out.typeCode <= 18)
            decodeAirbornePosition(frame, out);
        else if (out.typeCode == 19)
            decodeVelocity(frame, out);
        return true;

    case 11: // All-call reply: PI = CRC, con interrogatore II=0 il resto e' 0.
        if (lenBytes != 7) return false;
        out.crcOk = (crc24(frame, 7) == 0);
        out.icao = (uint32_t(frame[1]) << 16) | (uint32_t(frame[2]) << 8) | frame[3];
        return out.crcOk;

    default:
        // Gli altri DF (4/5/20/21...) hanno l'ICAO nel campo parita':
        // servono euristiche/whitelist, rimandate a una versione futura.
        return false;
    }
}

bool decodeHex(const std::string& hex, ModeSMessage& out)
{
    if (hex.size() != 14 && hex.size() != 28) return false;
    uint8_t buf[14];
    for (size_t i = 0; i < hex.size() / 2; i++) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        int hi = nib(hex[2 * i]), lo = nib(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        buf[i] = uint8_t((hi << 4) | lo);
    }
    return decode(buf, hex.size() / 2, out);
}

int cprNL(double latDeg)
{
    if (latDeg == 0.0) return 59;
    double alat = std::fabs(latDeg);
    if (alat >= 87.0) return (alat > 87.0) ? 1 : 2;

    constexpr double kNz = 15.0;
    double a = 1.0 - std::cos(M_PI / (2.0 * kNz));
    double b = std::cos(M_PI / 180.0 * alat);
    double nl = 2.0 * M_PI / std::acos(1.0 - a / (b * b));
    return int(std::floor(nl));
}

static double positiveMod(double a, double b)
{
    double r = std::fmod(a, b);
    return (r < 0) ? r + b : r;
}

std::optional<Position> cprGlobalDecode(double latCprEven, double lonCprEven,
                                        double latCprOdd, double lonCprOdd,
                                        bool lastIsOdd)
{
    constexpr double kDLatEven = 360.0 / 60.0;
    constexpr double kDLatOdd  = 360.0 / 59.0;

    // Indice di zona di latitudine.
    double j = std::floor(59.0 * latCprEven - 60.0 * latCprOdd + 0.5);

    double latEven = kDLatEven * (positiveMod(j, 60.0) + latCprEven);
    double latOdd  = kDLatOdd  * (positiveMod(j, 59.0) + latCprOdd);
    if (latEven >= 270.0) latEven -= 360.0;
    if (latOdd  >= 270.0) latOdd  -= 360.0;

    // I due messaggi devono cadere nella stessa zona di longitudine.
    if (cprNL(latEven) != cprNL(latOdd)) return std::nullopt;

    double lat = lastIsOdd ? latOdd : latEven;
    if (lat < -90.0 || lat > 90.0) return std::nullopt;

    int nl = cprNL(lat);
    double m = std::floor(lonCprEven * (nl - 1) - lonCprOdd * nl + 0.5);

    double lon;
    if (lastIsOdd) {
        int ni = std::max(nl - 1, 1);
        lon = (360.0 / ni) * (positiveMod(m, ni) + lonCprOdd);
    } else {
        int ni = std::max(nl, 1);
        lon = (360.0 / ni) * (positiveMod(m, ni) + lonCprEven);
    }
    if (lon >= 180.0) lon -= 360.0;

    return Position{lat, lon};
}

} // namespace sdrjo::adsb
