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

Fatti (4a tornata):

- ✅ **Notch** biquad (click sullo Spettro audio: si aggancia da solo al
  fischio) e **noise blanker** con soglia regolabile.
- ✅ **Spettro audio** 0-6 kHz del canale demodulato nella sidebar.
- ✅ Tracce **media** e **max hold** sullo spettro RF.
- ✅ **Scanner delle memorie**: si ferma dove lo squelch apre, riprende
  dopo N secondi di silenzio, registrazione automatica su WAV.
- ✅ **Satelliti**: TLE da tle.txt, passaggi 24h (Kepler+J2, per LEO con
  TLE freschi) e **inseguimento Doppler** automatico del downlink.
- ✅ Nuovo modulo **Sensori 433 MHz** (OOK, protocollo Nexus-TH:
  temperatura/umidita' stile rtl_433).
- ✅ Replay dei file IQ **in loop** (prima si fermava a fine file).

Fatti (5a tornata):

- ✅ **Audio a bassa latenza nel Cockpit**: WebSocket + IMA ADPCM (4:1,
  blocchi da 20 ms), token nuovo a ogni avvio consegnato da `/api/wsinfo`;
  fallback automatico al WAV se il browser/rete non regge. ~100 ms contro
  i ~2 s del WAV.
- ✅ Controlli **stile SDR#** a fianco dello spettro: levette verticali
  **Zoom / Contrasto / Range / Offset** (il contrasto e' una gamma sulla
  palette del waterfall).
- ✅ Demodulatore a **radio button** (un click, niente tendina).
- ✅ **Larghezza in kHz mostrata** mentre trascini i bordi della banda
  (e passando il mouse sopra la banda), come SDR#.
- ✅ **Decoder di testi** integrati (stile fldigi): **CW/Morse** (con tono
  regolabile) e **RTTY** Baudot 45.45 (mark 2125 / shift 170, invertibile)
  sul canale d'ascolto, testo scorrevole nella sidebar.

Fatti (6a tornata, rifiniture d'uso):

- ✅ La sintonia **non ricentra piu'** la vista a ogni passo (via il
  microlag); la vista segue solo quando il VFO sta per uscire dai bordi.
- ✅ Il **dial** e il campo **Frequenza (MHz)** ricentrano davvero
  l'hardware: lo spettro segue subito (prima restava sulla vecchia).
- ✅ **Rotellina sullo spettro = cambia frequenza** (a passi di snap);
  lo zoom resta sulla leva laterale (o Ctrl+rotellina).
- ✅ **Volume** fino a 1.5x per i segnali deboli.
- ✅ **Tooltip di aiuto** concisi sui controlli principali.
- ✅ **S-meter compatto** nell'angolo alto-destra dello spettro (quello
  analogico resta nella sidebar).
- ✅ Audio web **meno a scatti**: jitter buffer piu' ampio (~0.22 s) con
  recupero degli underrun.

Fatti (7a tornata, correzioni d'uso):

- ✅ Cambio frequenza (dial/campo/modulo) ora risintonizza l'hardware
  SUBITO (senza limitatore) e azzera cattura+waterfall: lo spettro segue
  all'istante, niente piu' "resta sulla vecchia frequenza".
- ✅ Levette Zoom/Contrasto/Range/Offset e S-meter verticale in una
  finestra dedicata a destra: non stanno piu' sopra il waterfall, quindi
  Range/Offset tornano cliccabili (era un bug).
- ✅ Drag dal righello reso solido (pilotato dallo stato del pulsante,
  non solo dall'hover): non "salta" piu' sullo spettro.
- ✅ La riga sottile di puntamento sparisce al click/drag (niente piu'
  sovraimpressione).
- ✅ Bottone Mute istantaneo accanto al volume; tolto "Spento" dai modi;
  la radio parte in AM.

Fatti (8a tornata):

- ✅ Cambio frequenza: al retune il thread DSP **svuota l'anello IQ**
  (buttando il backlog della vecchia banda), cosi' lo spettro passa
  subito alla nuova frequenza invece di smaltire i campioni vecchi.
- ✅ Il waterfall **non si azzera** piu' al cambio banda: scorre
  naturalmente mostrando la nuova frequenza dall'alto (come SDR#).
- ✅ Waterfall piu' **nitido in zoom**: texture a 8192 colonne.
- ✅ **Controllo larghezza di banda** (slider log + preset CW/SSB/AM/NFM)
  con minimo 100 Hz: filtri stretti per il CW su HF (piu' tap sotto 1 kHz).

Fatti (9a tornata):

- ✅ **Interruttore per ogni modulo**: i decoder partono spenti e si
  accendono uno alla volta. Un modulo spento non riceve IQ (ne' il suo
  VFO viene elaborato), quindi non spende CPU: l'app resta leggera e
  l'anello IQ non si riempie. UI con stato colorato (verde/grigio),
  avviso "fuori banda" e dettagli visibili solo quando il modulo e'
  acceso.

Fatti (10a tornata, check-up UI/UX):

- ✅ **Etichette non piu' troncate** nella sidebar: nome sopra il campo,
  widget a piena larghezza.
- ✅ **Barra di stato** in fondo: sorgente, sample rate, VFO/modo/banda,
  moduli attivi e **riempimento anello IQ** (spia della salute del PC).
- ✅ **Bande rapide**: un click imposta frequenza+modo+larghezza (FM,
  aereo, PMR, CB, 2m, 70cm, 40m, 20m, onde medie).
- ✅ **Scorciatoie tastiera**: spazio = mute, frecce = sintonia a snap,
  M = ciclo modo, F = zoom 1x.
- ✅ **Persistenza sessione**: freq/modo/banda/volume/snap salvati in
  sdrjo.cfg; **avvio massimizzato**.
- ✅ Tolti i controlli **duplicati** (Zoom/Range), tooltip spettro
  compatto (aiuto sul "(?)"), niente piu' sovrapposizione dB/righello,
  finestra "Vista" allargata (titolo intero, slider piu' afferrabili).

Fatti (11a tornata, CW/RTTY fldigi-like + bookmark):

- ✅ **CW**: velocita' automatica o **WPM manuale** (meglio sui deboli),
  tono regolabile; riga "CW" sullo Spettro audio come mira di taratura.
- ✅ **RTTY**: **baud** (45.45/50/75), **shift** (170/425/850), **mark**
  regolabile e inversione; due righe "M"/"S" sullo Spettro audio da
  allineare ai picchi ruotando la sintonia, come fldigi.
- ✅ **Bookmark**: le frequenze salvate che cadono nella vista appaiono
  come tacche viola con nome in cima allo spettro.
- ✅ **Scala interfaccia** (font) regolabile e salvata (monitor 4K).

Fatti (12a tornata, PSK31 + squelch decoder + S-meter in barra di stato):

- ✅ **PSK31 (BPSK31)**: nuovo decoder nella catena del decoder testi.
  NCO a banda base + integrate-and-dump con aggancio del tempo sui minimi
  d'ampiezza + decisione BPSK differenziale + Varicode. Tono regolabile,
  riga "PSK" sullo Spettro audio e **costellazione** di taratura (due lobi
  opposti = agganciato). Test di andata/ritorno con e senza rumore.
- ✅ **Squelch dei decoder**: soglia d'ampiezza (RMS) che blocca CW/RTTY/
  PSK31 quando non c'e' segnale, cosi' non compare piu' testo casuale sul
  solo rumore. Barra "segnale/silenzio" + valore salvato in configurazione.
- ✅ **Indicatore di taratura RTTY**: barre livello **Mark/Space** da
  pareggiare ruotando la sintonia (oltre alle righe M/S sullo spettro).
- ✅ **S-meter spostato**: la barretta verticale (poco leggibile) nella
  colonna "Vista" e' ora una **barra orizzontale in fondo all'app**, a
  destra, staccata dall'indicatore buffer IQ.

Fatti (13a tornata, SSB come SDR#/SDR++ + waterfall audio + rifiniture):

- ✅ **Passband SSB asimmetrico**: in USB la banda utile e' evidenziata
  *sopra* il marker [f, f+bw], in LSB *sotto* [f-bw, f]; il marker resta
  sulla portante soppressa (etichetta "USB"/"LSB"). Cosi', come su SDR#/
  SDR++, si mette il marker sul bordo del segnale e non al centro. Il
  trascinamento cambia solo il bordo della banda utile. AM/NFM/WFM
  restano simmetrici.
- ✅ **Waterfall audio** nel pannello Spettro audio (stile fldigi): scorre
  nel tempo e rende evidenti i toni CW/PSK e i due binari RTTY.
- ✅ **Click-to-tune**: con un decoder attivo, click sullo spettro/waterfall
  audio sintonizza il tono (CW/PSK) o il mark (RTTY) agganciandosi al picco;
  click destro piazza il notch.
- ✅ **LED d'aggancio PSK31** (rivelatore a portante quadrata) + etichette
  sopra gli slider del decoder (niente piu' testo tagliato).

Fatti (14a tornata, AFC + snap al picco + rivelatore TETRA):

- ✅ **AFC dei decoder**: opzione che centra da sola il tono CW/PSK sul
  picco piu' vicino dell'audio (scan Goertzel stretto, nudge lento), come
  l'aggancio automatico di fldigi.
- ✅ **Snap al picco** sullo spettro RF: al click di sintonia (opzione
  attivabile) aggancia il segnale piu' forte li' vicino; comodo su AM/FM
  con portante. Preferenza salvata in configurazione.
- ✅ **Rivelatore di attivita' TETRA**: misura *solo la presenza* di un
  portante digitale largo ~canale (25 kHz) sopra il rumore (SNR + larghezza
  occupata + LED). **Nessuna decodifica ne' decifratura** (la voce e'
  cifrata e intercettarla e' illegale). Classe DSP dedicata + test sintetico
  (portante rilevato, rumore e CW respinti).

Fatti (15a tornata, fix sintonia vista + snap fini):

- ✅ **Fix salto vista/righello**: mescolando la sintonia col click sullo
  spettro (VFO) e col frequenzimetro/campo MHz (ricentro hardware) la
  vista non "saltava" piu' sulla frequenza scelta e il righello sembrava
  tornare indietro. Ora ogni sintonia esplicita (dial, campo MHz, banda)
  **ricentra la vista sulla frequenza sintonizzata**; il click fine dentro
  lo span resta senza salti (solo VFO).
- ✅ **Snap fini per onde corte**: aggiunti passi **10/50/100/500 Hz** e
  2.5 kHz (per SSB/CW in HF) oltre a quelli medi.

Fatti (16a tornata, rifiniture UI):

- ✅ **Barra di stato racchiusa bene**: il contenuto sfondava l'altezza e
  il bordo inferiore spariva; ora la barra e' piu' alta col padding giusto
  e il riquadro e' chiuso su tutti e quattro i lati.
- ✅ **Posizione antenna in un sottomenu**: lat/lon e "Rileva dalla rete"
  spostati dalla sezione Dispositivo a un menu a tendina dedicato,
  richiudibile (lo si imposta una volta e si lascia chiuso).

Fatti (17a tornata, sintonia col frequenzimetro come SDR#):

- ✅ **Frequenzimetro/campo MHz stile SDR#**: se la frequenza scelta e' gia'
  nello span ricevuto muove SOLO il marker del VFO (i segnali a schermo
  restano fermi e il marker ci scorre sopra: si VEDE che hai cambiato);
  esce dalla chiavetta solo se vai fuori span. Prima ricentrava sempre
  l'hardware e a piena banda righello+spettro scorrevano insieme facendo
  sembrare che non cambiasse nulla.
- ✅ **Offset anti-DC piccolo**: ridotto da ~240 kHz a ~12-110 kHz (scala
  con la larghezza). Il DC blocker gia' scava la riga della DC sul flusso
  grezzo, quindi basta poco: cosi' il segnale sintonizzato resta al centro
  dello spettro invece che fisso a ~60%.

Fatti (18a tornata, diagnostica sintonia hardware):

- ✅ **Righello onesto**: `freqMHz` (che pilota il righello) ora segue la
  frequenza REALMENTE riletta dalla chiavetta (`rtlsdr_get_center_freq`),
  non quella chiesta. Prima si aggiornava comunque: se la sintonia non
  andava a segno il righello si spostava mentre lo spettro no.
- ✅ **Ritenta la sintonia**: se `set_center_freq` fallisce o la chiavetta
  non si e' mossa, il comando viene ripetuto una volta.
- ✅ **Log di sintonia**: ogni cambio registra "chiesto X | rc | chiavetta
  legge Y | offset VFO" (o "VFO dentro span, HW fermo"). Il pannello Log
  ora manda a capo le righe e ha un pulsante "Copia negli appunti" per
  passare i dati. Serve a capire se, su hardware reale, il comando di
  sintonia raggiunge davvero la V4.

Prossimi:

- SGP4 completo al posto di Kepler+J2 (precisione con TLE vecchi).
- Scarico automatico dei TLE (serve HTTPS) e piu' protocolli 433 MHz
  (Oregon Scientific, TFA, TPMS...).
- Altri modi digitali: FT8/FT4 (serve sincronizzazione tempo), SSTV.
- VFO con offset regolabile per modulo dalla GUI (oggi centrati).
- Registratore pianificato (parte da solo al passaggio del satellite).
- MSIX per il Microsoft Store.
