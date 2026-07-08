# SdrJo

Applicazione SDR per Windows in stile **SDR++**, con architettura a **moduli
plugin** per aggiungere decoder specializzati (aerei ADS-B, Morse, satelliti
meteo Meteor-M, ...). Progettata per la **RTL-SDR Blog V4** ma compatibile con
qualsiasi chiavetta RTL2832U.

## Struttura

```
core/                 Libreria base: DSP (FFT, FIR, demod FM/AM), API moduli,
                      sorgenti campioni (RTL-SDR, file IQ)
modules/adsb/         Monitoraggio aerei su 1090 MHz (Mode S / ADS-B)
modules/morse/        Decodifica telegrafia CW con stima automatica dei WPM
modules/meteor_lrpt/  Immagini meteo Meteor-M N2-3 / N2-4 (LRPT su 137 MHz)
app/gui/              GUI Dear ImGui: spettro, waterfall, gestione moduli
app/cli/              Strumento a riga di comando per provare i decoder
tests/                Test automatici (ctest)
docs/                 Architettura e roadmap
```

Ogni modulo è una libreria dinamica (`.dll` su Windows) caricata a runtime
dalla cartella `modules/` accanto all'eseguibile: per aggiungere un decoder
non serve toccare l'app principale.

## Stato attuale

| Componente | Stato |
|---|---|
| Core DSP (FFT, FIR, decimazione, FM/AM, AGC) | ✅ implementato e testato |
| API moduli plugin + loader dinamico | ✅ implementato |
| Sorgente RTL-SDR (V3/V4) e da file IQ | ✅ implementato (V4: vedi nota driver) |
| ADS-B: preambolo, CRC, callsign, posizione CPR, velocità | ✅ implementato e testato |
| Morse: decoder adattivo (velocità e pitch qualsiasi) | ✅ implementato e testato |
| Meteor LRPT: Viterbi CCSDS, derandomizer, sync CADU | ✅ implementato e testato |
| Meteor LRPT: QPSK (Costas + Gardner) | 🔧 scheletro, da tarare su registrazioni reali |
| Meteor LRPT: Reed-Solomon + immagine JPEG | 📋 da fare (vedi roadmap) |
| GUI (spettro + waterfall + moduli) | 🔧 scritta, da provare su Windows |
| Uscita audio | 📋 da fare |

## Compilazione su Windows

Prerequisiti: [CMake](https://cmake.org), Visual Studio 2022 (o MSYS2/MinGW) e Git.

```bat
git clone https://github.com/jserri/SdrJo.git
cd SdrJo
cmake -B build -DSDRJO_BUILD_GUI=ON
cmake --build build --config Release
```

La GUI scarica da sola Dear ImGui e GLFW (serve la connessione a internet la
prima volta). Per la sorgente hardware serve **librtlsdr**: il modo più
semplice è `vcpkg install rtlsdr` oppure copiare le DLL del driver
[rtl-sdr-blog](https://github.com/rtlsdrblog/rtl-sdr-blog/releases) accanto
all'eseguibile.

### Nota importante per la RTL-SDR Blog V4

La V4 (tuner R828D) è pienamente supportata **solo dal driver del fork
rtl-sdr-blog**: con la librtlsdr "vanilla" vecchia sintonizza con offset ed è
sorda sotto i 28 MHz. Quindi:

1. installare il driver USB **WinUSB** con [Zadig](https://zadig.akeo.ie)
   (seleziona "Bulk-In Interface 0" della chiavetta);
2. usare le DLL compilate del fork
   [rtl-sdr-blog](https://github.com/rtlsdrblog/rtl-sdr-blog) (≥ 2023);
3. per HF (onde corte) basta sintonizzare sotto i 28 MHz: l'upconverter
   interno della V4 viene gestito automaticamente dal driver.

Il bias-T della V4 (per alimentare un LNA esterno, molto utile per LRPT e
ADS-B) si attiva da `RtlSdrSource::setBiasTee(true)`.

## Compilazione su Linux (sviluppo/test)

```sh
cmake -B build && cmake --build build -j4
ctest --test-dir build            # esegue i test
```

## Prova rapida senza hardware

```sh
# Decodifica frame ADS-B noti:
sdrjo-cli adsb-hex 8D4840D6202CC371C32CE0576098
#   -> DF17 ICAO 4840D6 TC4  volo KLM1023

# Decodifica una registrazione IQ di 1090 MHz (rtl_sdr -f 1090000000 -s 2000000 cattura.bin):
sdrjo-cli adsb-iq cattura.bin

# Decodifica CW da file IQ:
sdrjo-cli morse-iq cattura.bin 12500
```

## Documentazione

- [docs/ARCHITETTURA.md](docs/ARCHITETTURA.md): come sono organizzati core,
  moduli e flusso dei campioni; come scrivere un nuovo modulo.
- [docs/ROADMAP.md](docs/ROADMAP.md): idee per i prossimi moduli (NOAA APT,
  radiosonde, sensori 433 MHz, POCSAG, AIS, ACARS, ...) e priorità.
