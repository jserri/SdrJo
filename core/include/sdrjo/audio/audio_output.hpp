#pragma once
//
// Uscita audio multipiattaforma basata su miniaudio (WASAPI su Windows,
// ALSA/PulseAudio su Linux, CoreAudio su macOS).
//
// miniaudio.h viene scaricata automaticamente da CMake alla configurazione;
// se non e' disponibile (build offline) l'oggetto esiste comunque ma
// isActive() resta false e write() scarta i campioni: nessun #ifdef
// nel codice chiamante.
//
#include <sdrjo/util/ring_buffer.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace sdrjo {

class AudioOutput {
public:
    AudioOutput();
    ~AudioOutput();

    // Elenco delle schede/uscite audio disponibili (vuoto senza backend).
    static std::vector<std::string> listDevices();

    // Apre l'uscita audio. deviceIndex: indice in listDevices(),
    // -1 = dispositivo predefinito di sistema. channels: 1 o 2.
    bool start(double sampleRate, int channels, int deviceIndex = -1);
    void stop();

    bool isActive() const;
    std::string backendName() const;

    // Campioni float interleaved [-1,1]; non blocca (scarta se il buffer
    // e' pieno: meglio un click che bloccare il thread DSP).
    void write(const float* samples, size_t count);

    // Mono di comodo (duplica sul canale destro se l'uscita e' stereo).
    void writeMono(const float* samples, size_t count);

    // Pubblico solo perche' la callback C del backend deve accedervi.
    struct Impl;

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace sdrjo
