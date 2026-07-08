# Architettura di SdrJo

## Flusso dei campioni

```
RTL-SDR (o file IQ)                     u8 interleaved I/Q
        │  thread USB
        ▼
convertU8Iq()                           cfloat normalizzati
        ▼
RingBuffer<cfloat>                      lock-free, thread USB -> thread DSP
        ▼
┌────────────────────┐
│ Host (GUI o CLI)   │── FFT ──► spettro + waterfall
│                    │
│  per ogni modulo:  │── FrequencyShifter ──► FirDecimator ──► processIq()
└────────────────────┘   (porta il canale     (al sample rate
                          del modulo a DC)     chiesto dal modulo)
```

L'hardware gira a un sample rate "largo" (tipicamente 2.4 MS/s); ogni modulo
dichiara in `ModuleInfo::requiredSampleRateHz` la banda che vuole e l'host
gliela prepara con mixer + filtro + decimazione. Così più moduli possono
lavorare in parallelo su canali diversi dentro la stessa banda catturata.

## API dei moduli (core/include/sdrjo/module/module.hpp)

Un modulo implementa `sdrjo::IModule`:

| Metodo | Ruolo |
|---|---|
| `info()` | nome, descrizione, frequenza preferita, sample rate richiesto |
| `start(host)` / `stop()` | ciclo di vita; `host` offre log, audio, richiesta di sintonia |
| `processIq(samples, n)` | riceve i campioni (thread DSP: non bloccare) |
| `drawUi()` | disegna i controlli ImGui (thread di rendering) |

e la esporta con:

```cpp
extern "C" SDRJO_MODULE_EXPORT sdrjo::IModule* sdrjo_create_module()
{ return new MioModulo(); }

extern "C" SDRJO_MODULE_EXPORT uint32_t sdrjo_module_abi()
{ return sdrjo::kModuleAbiVersion; }
```

`ModuleLoader::loadDirectory("modules")` carica tutte le `.dll`/`.so` della
cartella, verificando la versione ABI.

### Scrivere un nuovo modulo in 4 passi

1. crea `modules/miomodulo/` con un `CMakeLists.txt` che produce una libreria
   `MODULE` linkata a `sdrjo-core` (guarda `modules/morse/` come esempio
   minimo);
2. implementa `IModule` — di solito: un demodulatore nel `processIq()` e lo
   stato da mostrare in `drawUi()`;
3. separa la logica di decodifica in una libreria statica testabile
   (come `adsb-decoder`) e aggiungi un test in `tests/`;
4. aggiungi la sottocartella al `CMakeLists.txt` di radice.

## Scelte progettuali

- **C++17, dipendenze minime**: il core non dipende da nulla; GUI (ImGui,
  GLFW) e librtlsdr sono opzionali e isolate. Tutto il DSP è testabile su
  qualunque piattaforma senza hardware.
- **Decoder come librerie statiche + wrapper plugin**: la stessa logica è
  usata dai test, dalla CLI e dal modulo dinamico.
- **Test con vettori noti**: il decoder Mode S è verificato contro gli esempi
  di "The 1090 MHz Riddle" (callsign, CPR, velocità), il Viterbi con
  round-trip + errori iniettati, il CW con audio sintetizzato.

## Cosa manca per la parità con SDR++ (in ordine di utilità)

1. uscita audio (miniaudio è una singola header, ottimo candidato);
2. VFO multipli con drag sul waterfall;
3. registratore IQ/audio;
4. bookmark di frequenze e scanner;
5. server di rete stile rtl_tcp / SpyServer.
