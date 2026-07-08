# Roadmap e idee per i moduli

Tutte le idee qui sotto sono realizzabili con la sola **RTL-SDR Blog V4**
(24 MHz – 1.7 GHz + HF via upconverter interno), al più con un'antenna adatta
e per alcune un LNA alimentato dal bias-T integrato.

## In lavorazione

- **ADS-B (1090 MHz)** — funzionante a livello decoder. Prossimi passi:
  mappa degli aerei nella GUI, correzione errori a 1 bit sul CRC, uscita in
  formato SBS/BaseStation per alimentare altri programmi.
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
| **RDS su FM broadcast** | 87.5–108 MHz | nome stazione e radiotesto; ottimo secondo modulo "digitale", segnale fortissimo |
| **NOAA APT** | 137.62 / 137.9125 / 137.1 MHz | immagini meteo analogiche dei NOAA 15/18/19: molto più semplice di LRPT (FM + AM 2400 Hz), riusa il ricevitore a 137 MHz |
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

- Uscita audio multipiattaforma (miniaudio) e demodulatori WFM stereo/SSB.
- VFO multipli: più moduli in ascolto contemporaneamente nella stessa banda.
- Ricampionatore razionale (per rate non divisori interi, es. 288k da 2.4M = /8.33).
- Correzione automatica del DC spike e dell'offset PPM.
- Pannello di registrazione IQ con replay integrato nella GUI.
- Pacchetto di installazione Windows (NSIS/MSIX) con driver e moduli inclusi.
