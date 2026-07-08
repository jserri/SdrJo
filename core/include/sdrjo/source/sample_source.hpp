#pragma once
//
// Interfaccia comune per le sorgenti di campioni IQ:
//  - RtlSdrSource : chiavetta RTL-SDR reale (V3, V4, generiche R820T/R828D)
//  - FileSource   : file IQ registrato (replay, test, sviluppo senza hardware)
//
#include "../dsp/types.hpp"
#include <functional>
#include <string>

namespace sdrjo {

// Callback invocata dal thread di acquisizione a ogni blocco di campioni.
using IqCallback = std::function<void(const cfloat* samples, size_t n)>;

class ISampleSource {
public:
    virtual ~ISampleSource() = default;

    virtual std::string name() const = 0;

    virtual bool setCenterFrequency(double hz) = 0;
    virtual double centerFrequency() const = 0;

    virtual bool setSampleRate(double hz) = 0;
    virtual double sampleRate() const = 0;

    // gainDb < 0 = AGC hardware attivo.
    virtual bool setGain(double gainDb) = 0;

    virtual bool start(IqCallback cb) = 0;
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;
};

} // namespace sdrjo
