#pragma once
//
// Strati CCSDS del downlink LRPT Meteor-M: ricerca del sync word,
// derandomizzazione e struttura dei CADU.
//
// Catena completa di ricezione (72k simboli/s QPSK su 137.1/137.9 MHz):
//   IQ -> filtro RRC -> Costas loop (recupero portante) -> recupero clock
//      -> soft bit -> Viterbi r=1/2 k=7 -> [questo file] sync + derandom
//      -> Reed-Solomon (255,223) x4 interleaved -> pacchetti CVCDU
//      -> MCU JPEG -> immagine multibanda.
//
#include <cstdint>
#include <optional>
#include <vector>

namespace sdrjo::lrpt {

// Attached Sync Marker CCSDS dei CADU.
constexpr uint32_t kSyncWord = 0x1ACFFC1D;

// Dimensioni CADU: 4 byte ASM + 1020 byte dati randomizzati.
constexpr size_t kCaduBytes = 1024;
constexpr size_t kCaduDataBytes = 1020;

// Genera la sequenza PN CCSDS (polinomio x^8+x^7+x^5+x^3+1, stato iniziale
// tutto a 1) usata per (de)randomizzare i 1020 byte dati del CADU.
std::vector<uint8_t> pnSequence(size_t numBytes);

// De-randomizza in place i dati di un CADU (esclude il sync a 4 byte).
void derandomize(uint8_t* data, size_t numBytes);

// Cerca il sync word in un flusso di bit (MSB first). Restituisce l'offset
// in bit del primo sync trovato, e in inverted=true se il flusso e'
// invertito di fase (tipico ambiguita' BPSK/QPSK).
std::optional<size_t> findSync(const std::vector<uint8_t>& bits, bool& inverted);

// Impacchetta un vettore di bit (0/1, MSB first) in byte.
std::vector<uint8_t> packBits(const std::vector<uint8_t>& bits, size_t startBit,
                              size_t numBits);

} // namespace sdrjo::lrpt
