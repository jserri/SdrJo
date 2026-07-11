#pragma once
//
// Codec IMA ADPCM (4 bit/campione, compressione 4:1) per lo streaming
// audio del Cockpit via WebSocket: banda e latenza molto piu' basse del
// WAV PCM, senza dipendenze esterne.
//
// Ogni blocco e' autonomo: inizia con lo stato del predittore, cosi' un
// client puo' agganciarsi al flusso in qualsiasi momento.
//
//   blocco = [predictor int16 LE][index uint8][riservato uint8]
//            [n/2 byte di nibble ADPCM, primo campione nel nibble basso]
//
#include <cstddef>
#include <cstdint>
#include <vector>

namespace sdrjo {

// Codifica n campioni float [-1,1] in un blocco ADPCM (n deve essere
// pari). Ritorna 4 + n/2 byte.
std::vector<uint8_t> adpcmEncodeBlock(const float* samples, size_t n);

// Decodifica un blocco prodotto da adpcmEncodeBlock. Vuoto se malformato.
std::vector<float> adpcmDecodeBlock(const uint8_t* block, size_t bytes);

} // namespace sdrjo
