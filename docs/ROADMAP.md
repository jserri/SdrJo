# Roadmap e idee per i moduli

Tutte le idee qui sotto sono realizzabili con la sola **RTL-SDR Blog V4**
(24 MHz – 1.7 GHz + HF via upconverter interno), al più con un'antenna adatta
e per alcune un LNA alimentato dal bias-T integrato.

## Fatti di recente

- **RDS su FM broadcast** — decoder completo (checkword, sync blocchi,
  gruppi 0A/2A: PS e RadioText) testato end-to-end dal multiplex a 57 kHz.
  Prossimi passi: lista AF, CT (orologio), TMC.
- **NOAA APT** — inviluppo della sottoportante 2400 Hz, aggancio del sync A,
  righe immagine e salvataggio BMP. Prossimi passi: correzione Doppler,
  falso colore, telemetria per la calibrazione.
- **Cockpit web** — la dashboard di SdrJo (spettro/waterfall live e card dei
  moduli). Prossimi passi: click-to-tune sullo spettro, websocket al posto
  del polling, immagini APT/LRPT inline.

## In lavorazione

- **ADS-B (1090 MHz)** — decoder, **mappa web dei voli** e **uscita
  SBS/BaseStation (porta 30003)** funzionanti. Prossimi passi: correzione
  errori a 1 bit sul CRC, database ICAO offline, grafico polare della
  portata dell'antenna.
- **Morse (CW)** — funzionante. Prossimi passi: filtro audio stretto attorno
  al tono con Goertzel, waterfall audio nella GUI.
- **Meteor-M LRPT (137.1 / 137.9 MHz)** — Viterbi, derandomizer e sync CADU
  pronti e testati. Mancano: taratura QPSK su registrazioni reali,
  Reed-Solomon (255,223), deinterleaving, decodifica MCU JPEG e composizione
  dell'immagine RGB. Riferimenti utili: progetto *meteor_decoder* (Pascal),
  *SatDump* e il blog di Lucas Teske sulla catena LRPT.

## Idee per nuovi moduli (dalla più semplice alla più ambiziosa)

| Modulo | Frequenza | Note |
|---|---|---|
| **Sensori ISM 433/868 MHz** | 433.92 / 868 MHz | stazioni meteo, sensori porta, TPMS delle gomme — stile `rtl_433`; decoder OOK/FSK generico a profili |
| **POCSAG / cercapersone** | ~466 MHz (varia per paese) | FSK 512–2400 bps, decoder semplice e didattico |
| **AIS navale** | 161.975 / 162.025 MHz | posizione delle navi, GMSK 9600; ottimo se vivi vicino alla costa |
| **ACARS** | 131.550 / 131.725 MHz | messaggi di testo degli aerei di linea, MSK 2400: complementare all'ADS-B |
| **Radiosonde (RS41)** | 400–406 MHz | tracking dei palloni meteo lanciati 2 volte al giorno; GFSK + Reed-Solomon, community molto attiva (radiosonde_auto_rx) |
| **APRS / AX.25** | 144.800 MHz (EU) | pacchetti dei radioamatori, AFSK 1200; con la mappa diventa spettacolare |
| **RTTY / PSK31 / FT8-lite** | HF (la V4 arriva in HF!) | decoder dei modi digitali radioamatoriali sulle onde corte |
| **DAB+** | 174–240 MHz | radio digitale: ambizioso (OFDM) ma ben documentato (progetto welle.io) |
| **SSTV dalla ISS** | 145.800 MHz | immagini dalla Stazione Spaziale durante gli eventi ARISS |
| **Predizione passaggi satellite** | — | non un decoder ma un servizio: TLE + SGP4, correzione Doppler automatica per LRPT/APT/ISS |
| **Scanner + registratore** | qualsiasi | scansione di un elenco di frequenze con squelch, registrazione automatica |
| **Inmarsat STD-C EGC** | 1541.45 MHz | messaggi marittimi dal satellite geostazionario: serve patch antenna + LNA (bias-T della V4) |

## Miglioramenti al core

Fatti:

- ✅ Uscita audio multipiattaforma (miniaudio, scaricata automaticamente) e
  demodulatori **WFM stereo** (pilota 19 kHz) e **SSB** (USB/LSB), testati.
- ✅ **VFO multipli**: ogni modulo riceve il proprio canale (shift + filtro +
  decimazione) dal flusso largo; ascolto FM contemporaneo ai decoder.
- ✅ Ricampionatore **razionale polifase** (L/M automatico, es. 2.4M -> 288k).
- ✅ **DC blocker** sul flusso IQ e stima dell'offset **PPM** da una portante
  nota (`dsp::estimatePpm`).
- ✅ **Registrazione IQ** con metadati e **replay** integrato nella GUI.
- ✅ Pacchetto di installazione con CPack: `cpack -G NSIS` (installer) o
  `cpack -G ZIP` (portabile); include i moduli e, se presente la cartella
  `driver/`, anche le DLL della chiavetta.

Fatti (2a tornata):

- ✅ Click-to-tune, rotellina con **snap interval** selezionabile, **zoom**
  (Ctrl+rotellina o slider) con waterfall ritagliato sulla vista.
- ✅ **Larghezza canale trascinabile** dai bordi della banda sul grafico.
- ✅ **Squelch** con soglia, isteresi e indicatore di livello.
- ✅ **Filtri audio** passa-alto/passa-basso sul canale di ascolto.
- ✅ **Frequency manager** persistente (frequenze.csv accanto all'exe).
- ✅ Pannello Audio: scelta scheda di uscita e frequenza audio.

Fatti (3a tornata):

- ✅ Rotellina = **zoom** sullo spettro (Ctrl+rotellina o rotellina sul
  righello = passi di snap); click sintonizza sul punto **premuto**.
- ✅ **Righello delle frequenze** sotto lo spettro, cliccabile e
  trascinabile; sintonia anche con click/trascina sul waterfall.
- ✅ **Linea del VFO sul waterfall** allineata al marker dello spettro.
- ✅ Frequenze/Moduli/Log dentro la sidebar: spettro+waterfall a tutta
  altezza.
- ✅ Retune hardware con **offset anti-DC** (il segnale ascoltato non
  finisce piu' sulla riga della DC al centro) e checkbox **AGC RTL**.
- ✅ DLL della chiavetta caricate da `driver/` accanto all'eseguibile.
- ✅ Posizione stazione **rilevata via IP** (ip-api.com) con un click.

Prossimi:

- Notch regolabile e noise blanker sul canale di ascolto.
- Max-hold / media sullo spettro e spettro audio (per CW/SSB).
- Scanner di memorie con squelch e registrazione automatica.
- Cockpit: audio Opus/WebSocket per latenza e banda migliori (oggi WAV).
- VFO con offset regolabile per modulo dalla GUI (oggi centrati).
- Predizione passaggi satellite (TLE + SGP4) con correzione Doppler.
- MSIX per il Microsoft Store.
