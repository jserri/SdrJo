#pragma once
//
// Decodifica dei messaggi Mode S / ADS-B (1090 MHz Extended Squitter).
// Riferimento: "The 1090 Megahertz Riddle" di Junzi Sun (TU Delft).
//
#include <cstdint>
#include <optional>
#include <string>

namespace sdrjo::adsb {

// CRC-24 Mode S (polinomio 0xFFF409). Per un frame valido il resto
// calcolato sull'intero messaggio (parita' inclusa) e' zero.
uint32_t crc24(const uint8_t* data, size_t lenBytes);

struct Position {
    double latDeg;
    double lonDeg;
};

struct ModeSMessage {
    uint8_t raw[14] = {0};
    int lenBytes = 0;          // 7 (squitter corto) o 14 (esteso)
    int df = -1;               // Downlink Format
    uint32_t icao = 0;         // indirizzo a 24 bit
    bool crcOk = false;

    // Campi DF17/DF18 (Extended Squitter)
    int typeCode = -1;

    bool hasCallsign = false;
    std::string callsign;

    bool hasAltitude = false;
    int altitudeFt = 0;

    bool hasVelocity = false;
    double groundSpeedKt = 0.0;
    double trackDeg = 0.0;
    int verticalRateFpm = 0;

    // Posizione CPR grezza (serve una coppia even/odd per il decode globale).
    bool hasCprPosition = false;
    bool cprOdd = false;
    double cprLat = 0.0;       // normalizzata [0,1)
    double cprLon = 0.0;
};

// Decodifica un frame binario (7 o 14 byte). Restituisce false se la
// lunghezza non e' valida o il CRC fallisce per i formati verificabili.
bool decode(const uint8_t* frame, size_t lenBytes, ModeSMessage& out);

// Comodita': decodifica da stringa esadecimale ("8D4840D6202CC371C32CE0576098").
bool decodeHex(const std::string& hex, ModeSMessage& out);

// Decodifica globale CPR airborne da una coppia even/odd.
// lastIsOdd indica quale dei due messaggi e' il piu' recente.
std::optional<Position> cprGlobalDecode(double latCprEven, double lonCprEven,
                                        double latCprOdd, double lonCprOdd,
                                        bool lastIsOdd);

// Numero di zone di longitudine alla latitudine data (funzione NL del CPR).
int cprNL(double latDeg);

} // namespace sdrjo::adsb
