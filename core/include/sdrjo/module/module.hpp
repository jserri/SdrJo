#pragma once
//
// API dei moduli plugin di SdrJo.
//
// Ogni modulo (ADS-B, Morse, Meteor LRPT, ...) e' una libreria dinamica
// (.dll su Windows, .so su Linux) che esporta una factory C:
//
//     extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module();
//     extern "C" SDRJO_MODULE_EXPORT uint32_t        sdrjo_module_abi();
//
// L'host (la GUI o la CLI) carica il modulo, gli fornisce un "tap" sul
// flusso IQ alla frequenza/banda richiesta e ne disegna l'interfaccia.
//
#include "../dsp/types.hpp"
#include <cstdint>
#include <string>

#if defined(_WIN32)
  #define SDRJO_MODULE_EXPORT __declspec(dllexport)
#else
  #define SDRJO_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace sdrjo {

// Versione dell'ABI: da incrementare a ogni modifica incompatibile
// di questa interfaccia. L'host rifiuta i moduli con ABI diversa.
constexpr uint32_t kModuleAbiVersion = 2;

struct ModuleInfo {
    std::string name;        // es. "ADS-B"
    std::string version;     // es. "0.1.0"
    std::string description; // una riga per l'elenco moduli
    // Richiesta di sintonia del modulo (0 = usa la sintonia corrente del VFO).
    double preferredFreqHz = 0.0;
    // Sample rate IQ che il modulo vuole ricevere (l'host converte).
    double requiredSampleRateHz = 0.0;
};

// Servizi che l'host mette a disposizione del modulo.
class IModuleHost {
public:
    virtual ~IModuleHost() = default;

    // Log verso la console/finestra log dell'host.
    virtual void log(const std::string& moduleName, const std::string& text) = 0;

    // Riproduce audio demodulato (mono, float, sampleRate dichiarato).
    virtual void playAudio(const float* samples, size_t n, double sampleRateHz) = 0;

    // Chiede all'host di sintonizzare l'hardware (se l'utente lo permette).
    virtual bool requestTune(double freqHz, double sampleRateHz) = 0;
};

// Interfaccia implementata da ogni modulo.
class IModule {
public:
    virtual ~IModule() = default;

    virtual ModuleInfo info() const = 0;

    virtual void start(IModuleHost& host) = 0;
    virtual void stop() = 0;

    // Riceve campioni IQ gia' centrati e ricampionati secondo ModuleInfo.
    // Chiamato dal thread DSP: non bloccare.
    virtual void processIq(const cfloat* samples, size_t n) = 0;

    // Hook per la GUI (chiamato nel thread di rendering ImGui).
    virtual void drawUi() {}

    // Stato corrente in JSON per il Cockpit web: coppie chiave/valore da
    // mostrare nella card del modulo, es. {"Aerei":"12","Messaggi":"4813"}.
    // Chiamato da un thread diverso da processIq: proteggere lo stato.
    virtual std::string statusJson() const { return "{}"; }

    // Porta dell'eventuale interfaccia web dedicata (0 = nessuna),
    // es. la mappa voli del modulo ADS-B.
    virtual uint16_t webPort() const { return 0; }
};

} // namespace sdrjo

// Firma delle funzioni esportate dai moduli.
using SdrjoCreateModuleFn = sdrjo::IModule* (*)();
using SdrjoModuleAbiFn    = uint32_t (*)();
