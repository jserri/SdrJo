#pragma once
//
// Decoder (e codificatore per test) del modo digitale FT8.
//
// FT8 e' un modo amatoriale a scambio breve, cicli di 15 s, 8-FSK con
// spaziatura 6.25 Hz, 79 simboli (58 dati + 3 array di sincronismo Costas
// 7x7), FEC LDPC(174,91) piu' CRC a 14 bit e messaggi impacchettati in
// 77 bit. Qui implementiamo la ricezione: sincronizzazione sullo spettro,
// demodulazione soft, decodifica LDPC a propagazione di credenza e
// spacchettamento dei messaggi piu' comuni.
//
// NOTA sulle costanti: le tabelle LDPC/Costas/Gray sono COSTANTI DEL
// PROTOCOLLO FT8 (fissate dalla specifica, identiche in ft8_lib MIT e in
// WSJT-X). Gli algoritmi sono scritti da zero per SdrJo.
//
#include <cstdint>
#include <string>
#include <vector>

namespace sdrjo::dsp::ft8 {

// Un messaggio FT8 decodificato in una finestra da 15 s.
struct Decode {
    double freqHz = 0.0;   // frequenza audio del segnale
    double dtSec = 0.0;    // scarto temporale rispetto all'inizio ideale
    float snrDb = 0.0f;    // stima grossolana del rapporto S/N (dB)
    float sync = 0.0f;     // qualita' di sincronismo (per ordinare i decode)
    std::string message;   // testo, es. "CQ IZ0ABC JN61"
};

// --- Livello messaggi -------------------------------------------------------

// Impacchetta un messaggio (es. "CQ IZ0ABC JN61") in 77 bit.
// Ritorna false se il messaggio non e' rappresentabile.
bool pack77(const std::string& message, uint8_t bits77[77]);

// Spacchetta 77 bit in testo. Ritorna false se il tipo non e' supportato.
bool unpack77(const uint8_t bits77[77], std::string& out);

// --- Livello codice (CRC + LDPC) -------------------------------------------

// 77 bit di payload -> 174 bit di codeword (aggiunge CRC-14 e parita' LDPC).
void encode174(const uint8_t bits77[77], uint8_t codeword174[174]);

// Decoder LDPC a propagazione di credenza: 174 LLR -> 77 bit di payload.
// Convenzione: LLR positivo = bit 1. Ritorna false se non converge o se il
// CRC non torna.
bool bpDecode(const float llr174[174], uint8_t bits77[77], int maxIter = 30);

// --- Modem ------------------------------------------------------------------

// Sequenza dei 79 toni (0..7) per un payload di 77 bit.
void tonesFromBits(const uint8_t bits77[77], int tones[79]);

// Genera l'audio (reale) di un messaggio FT8 come 8-FSK a fase continua,
// utile per i test e per riferimento. sampleRate in Hz, tono base f0 in Hz.
// Vuoto se il messaggio non e' valido.
std::vector<float> encodeAudio(const std::string& message, double f0Hz,
                               double sampleRate = 12000.0);

// --- Decodifica di una finestra --------------------------------------------

// Decodifica una finestra audio (idealmente ~15 s) e ritorna i messaggi
// trovati, ordinati per qualita' di sincronismo decrescente. L'audio puo'
// essere a qualunque sampleRate (viene ricampionato internamente).
std::vector<Decode> decodeAudio(const float* audio, size_t n,
                                double sampleRate, double freqMin = 200.0,
                                double freqMax = 3000.0);

} // namespace sdrjo::dsp::ft8
