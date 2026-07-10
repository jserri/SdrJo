#pragma once
//
// Decoder del protocollo Nexus-TH (sensori temperatura/umidita' cinesi
// molto diffusi, 433.92 MHz): PPM con impulso ~500 us e pausa corta (0)
// o lunga (1); 36 bit per trama, ripetuta ~12 volte.
//
//  bit  0-7   id del sensore (cambia a ogni cambio batteria)
//  bit  8     batteria ok
//  bit  9     costante
//  bit 10-11  canale (1-3 sul selettore)
//  bit 12-23  temperatura in decimi di grado, complemento a due
//  bit 24-27  costante 1111
//  bit 28-35  umidita' %
//
#include "ook_detector.hpp"

#include <cstdint>
#include <functional>

namespace sdrjo::sensors {

struct NexusReading {
    uint8_t id = 0;
    bool batteryOk = false;
    int channel = 0;      // 1-3
    double tempC = 0.0;
    int humidity = 0;     // %
};

class NexusDecoder {
public:
    using ReadingCallback = std::function<void(const NexusReading&)>;

    explicit NexusDecoder(ReadingCallback cb) : cb_(std::move(cb)) {}

    // Da collegare all'OokDetector.
    void onPulse(const Pulse& p);

private:
    void flush();

    ReadingCallback cb_;
    uint64_t bits_ = 0;
    int nBits_ = 0;
};

} // namespace sdrjo::sensors
