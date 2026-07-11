# SdrJo

Applicazione SDR per Windows in stile **SDR++**, con architettura a **moduli
plugin** per aggiungere decoder specializzati (aerei ADS-B, Morse, satelliti
meteo Meteor-M, ...). Progettata per la **RTL-SDR Blog V4** ma compatibile con
qualsiasi chiavetta RTL2832U.

## Struttura

```
core/                 Libreria base: DSP (FFT, FIR, demod FM/AM), API moduli,
                      sorgenti campioni (RTL-SDR, file IQ), server web/TCP,
                      Cockpit web
modules/adsb/         Aerei su 1090 MHz: mappa web + uscita SBS (porta 30003)
modules/rds/          Nome stazione e RadioText dalle radio FM (RDS)
modules/noaa_apt/     Immagini meteo NOAA 15/18/19 (APT su 137 MHz)
modules/meteor_lrpt/  Immagini meteo Meteor-M N2-3 / N2-4 (LRPT su 137 MHz)
modules/morse/        Decodifica telegrafia CW con stima automatica dei WPM
app/gui/              GUI nativa Dear ImGui + Cockpit web integrato
app/cli/              Strumento a riga di comando per provare i decoder
tests/                Test automatici (ctest)
docs/                 Architettura e roadmap
```

## Il Cockpit

La plancia di SdrJo e' **web-based**: l'app serve su `http://localhost:8750`
una dashboard moderna con spettro + waterfall live, il frequenzimetro e le
card di stato di tutti i moduli — consultabile anche da tablet o telefono
sulla rete locale. La GUI nativa (ImGui) resta per il controllo del
dispositivo e usa lo stesso tema scuro.

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
| ADS-B: mappa web dei voli (stile SDRAngel/tar1090) | ✅ implementata e testata |
| ADS-B: uscita SBS/BaseStation porta 30003 (VRS, PlanePlotter...) | ✅ implementata e testata |
| RDS: PI, PTY, nome stazione (PS) e RadioText | ✅ implementato e testato (anche end-to-end dal multiplex) |
| NOAA APT: sync, righe immagine, salvataggio BMP | ✅ implementato e testato |
| Cockpit web (dashboard moderna, senza dipendenze) | ✅ implementato e testato |
| Morse: decoder adattivo (velocità e pitch qualsiasi) | ✅ implementato e testato |
| CW/RTTY/PSK31: decoder testi con AFC, squelch e waterfall audio | ✅ implementati e testati |
| TETRA: **rivelatore di sola attività** (nessuna decodifica/decifratura) | ✅ implementato e testato |
| Meteor LRPT: Viterbi CCSDS, derandomizer, sync CADU | ✅ implementato e testato |
| Meteor LRPT: QPSK (Costas + Gardner) | 🔧 scheletro, da tarare su registrazioni reali |
| Meteor LRPT: Reed-Solomon + immagine JPEG | 📋 da fare (vedi roadmap) |
| GUI (spettro + waterfall + moduli + registratore + replay) | ✅ compila ed e' stata avviata su Linux; da provare su Windows |
| VFO multipli / canalizzazione per modulo | ✅ implementati e testati |
| WFM stereo + SSB + ricampionatore razionale + DC/PPM | ✅ implementati e testati |
| Uscita audio (miniaudio) | ✅ implementata (backend nullo se offline) |
| Registrazione IQ + replay | ✅ implementati e testati |
| Installer (CPack NSIS/ZIP) | ✅ configurato |

### Nota su TETRA (rilevamento, non intercettazione)

SdrJo include un **rivelatore di attività TETRA**: misura soltanto se sul
canale (25 kHz) è presente un portante digitale largo tipo TETRA sopra il
rumore (SNR e larghezza occupata). Serve alla caccia agli impianti e alla
mappatura dello spettro.

**Non c'è — e non ci sarà — alcuna decodifica o decifratura.** La voce
TETRA usa ACELP quasi sempre cifrato (TEA1/2/3) e intercettarne il
contenuto è illegale in Italia/UE. Il rivelatore si ferma alla
presenza/assenza del segnale, senza mai toccarne il contenuto.

## Compilazione su Windows (32 e 64 bit)

Prerequisiti: [CMake](https://cmake.org), Visual Studio (Community va bene) e
Git. La GUI scarica da sola Dear ImGui e GLFW alla prima configurazione.

```bat
git clone https://github.com/jserri/SdrJo.git
cd SdrJo

:: build a 64 bit
cmake -B build64 -A x64
cmake --build build64 --config Release

:: build a 32 bit
cmake -B build32 -A Win32
cmake --build build32 --config Release
```

> Se usi il **Developer Command Prompt** (generatore NMake), l'architettura
> segue il prompt: "x64 Native Tools" produce 64 bit, quello base x86
> produce 32 bit. In quel caso ometti `-A`.

Al termine trovi tutto gia' al suo posto in `buildXX\bin\`:
`sdrjo.exe`, `sdrjo-cli.exe` e la cartella `modules\` con i plugin.

### Collegare la chiavetta (RTL-SDR V4 inclusa)

**librtlsdr non serve per compilare**: viene caricata a runtime. Per far
riconoscere la chiavetta:

1. installa il driver USB **WinUSB** con [Zadig](https://zadig.akeo.ie):
   collega la chiavetta, `Options > List All Devices`, seleziona
   "Bulk-In, Interface (Interface 0)" e premi *Install Driver*;
2. scarica le DLL del driver dal fork
   [rtl-sdr-blog releases](https://github.com/rtlsdrblog/rtl-sdr-blog/releases)
   (obbligatorio per la **V4**; la versione vanilla la sintonizza male);
3. copia `rtlsdr.dll` **della stessa architettura dell'app** (x64 con la
   build a 64 bit, x86 con quella a 32) in `build\bin\` accanto a
   `sdrjo.exe`, insieme alla `libusb-1.0.dll` fornita nello stesso zip.

Se la DLL manca o e' dell'architettura sbagliata, il pannello Dispositivo
di SdrJo lo dice chiaramente (niente crash, niente build da rifare).

Per le HF (onde corte) con la V4 basta sintonizzare sotto i 28 MHz:
l'upconverter interno e' gestito dal driver. Il bias-T per alimentare un
LNA esterno si attiva con `RtlSdrSource::setBiasTee(true)`.

### Problemi comuni di compilazione

- **`M_PI: identificatore non dichiarato`** / **`std::min non trovato`**:
  risolti nel progetto (definizioni MSVC globali) — aggiorna all'ultima
  versione del repo e riconfigura da zero (cancella la cartella build).
- **`_WinMain@16 non risolto`**: risolto, la GUI usa l'entry point
  standard `main()` anche in modalita' finestra.

## Creare l'installer / pacchetto portabile

```bat
cmake --build build64 --config Release
cd build64
cpack -G ZIP            :: SdrJo-x.y.z.zip portabile
cpack -G NSIS           :: installer .exe (serve NSIS: nsis.sourceforge.io)
```

Se prima di impacchettare crei una cartella `driver\` nella radice del
progetto con dentro `rtlsdr.dll` e `libusb-1.0.dll`, finiscono nel pacchetto.

## Accesso remoto (usare la radio fuori casa)

Il Cockpit e' un **ricevitore web completo** stile OpenWebRX:

- **click sullo spettro** dal browser = sintonia del VFO (marker arancione);
- **chips del demodulatore** (WFM stereo / NFM / AM / USB / LSB) per
  accendere e cambiare l'ascolto da remoto;
- pulsante **"Ascolta"**: l'audio demodulato arriva nel browser come
  stream WAV senza fine (48 kHz mono, ~96 kB/s) — funziona su telefono e
  tablet, piu' client contemporaneamente (ognuno ha la sua coda);
  dopo 30 s di silenzio continuo lo stream si chiude da solo.

L'esposizione oltre il PC locale si attiva dal pannello
**Audio > Accesso remoto**: spunta "Esponi il Cockpit in LAN", imposta una
**password** (obbligatoria) e premi Applica. Da quel momento
`http://IP-del-PC:8750` e' raggiungibile dagli altri dispositivi della rete
di casa (utente `sdrjo` + la tua password, HTTP Basic Auth).

**Per l'accesso da fuori casa la strada sicura e' una VPN**, non l'apertura
della porta sul router:

1. **Tailscale** (consigliato, gratuito per uso personale): installalo sul
   PC con SdrJo e sul telefono/portatile; i due dispositivi si vedono su una
   rete privata cifrata (WireGuard) senza toccare il router. Da fuori apri
   `http://100.x.y.z:8750` con l'IP Tailscale del PC.
2. **WireGuard/OpenVPN sul router**, se il tuo router lo supporta: stesso
   principio, gestito da te.
3. **Da evitare**: il port-forwarding diretto della 8750 su internet. La
   password Basic Auth viaggia su HTTP in chiaro: fuori dalla LAN va sempre
   incapsulata in una VPN o dietro un reverse proxy HTTPS (es. Caddy).

Nota bene: la chiavetta e' solo in ricezione, quindi il rischio e' limitato
all'accesso a cio' che ricevi e ai controlli dell'app — ma la password e la
VPN restano il minimo indispensabile.

## Compilazione su Linux (sviluppo/test)

```sh
cmake -B build && cmake --build build -j4
ctest --test-dir build            # esegue i test
```

## Mappa dei voli (stile SDRAngel)

Il modulo ADS-B include un'interfaccia web con **mappa scura Leaflet, icone
degli aerei orientate con la rotta, scie, cerchi di portata attorno
all'antenna** e tabella dei voli con quota, velocità, distanza e rilevamento.
Si apre su `http://localhost:8757` e viene avviata automaticamente dal modulo
nella GUI, oppure dalla CLI:

```sh
# In diretta con la RTL-SDR (lat/lon = posizione della tua antenna):
rtl_sdr -f 1090000000 -s 2000000 - | sdrjo-cli adsb-serve - 45.4642 9.1900

# Oppure replay di una registrazione:
sdrjo-cli adsb-serve cattura.bin 45.4642 9.1900
```

La pagina usa Leaflet e i tile da CDN: serve internet nel browser; senza
rete la tabella dei voli continua comunque a funzionare.

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
