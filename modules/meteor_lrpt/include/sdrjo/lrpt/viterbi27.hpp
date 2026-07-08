#pragma once
//
// Codifica/decodifica convoluzionale CCSDS k=7 rate 1/2 (Viterbi),
// usata dal downlink LRPT dei Meteor-M (e da moltissimi altri satelliti).
// Polinomi generatori: G1 = 0171 ottale, G2 = 0133 ottale.
//
#include <cstdint>
#include <vector>

namespace sdrjo::lrpt {

class Viterbi27 {
public:
    static constexpr int kConstraint = 7;
    static constexpr int kNumStates = 64;
    static constexpr uint8_t kPolyG1 = 0x79; // 0171 ottale
    static constexpr uint8_t kPolyG2 = 0x5B; // 0133 ottale

    // Codifica: per ogni bit in ingresso produce 2 bit (prima G1 poi G2).
    // Lo stato iniziale e' zero; non viene aggiunto flushing automatico.
    static std::vector<uint8_t> encode(const std::vector<uint8_t>& bits);

    // Decodifica hard-decision: symbols contiene coppie di bit (0/1),
    // symbols.size() deve essere pari. Restituisce i bit stimati.
    static std::vector<uint8_t> decodeHard(const std::vector<uint8_t>& symbols);

    // Decodifica soft-decision: ogni simbolo e' un byte 0..255 dove
    // 0 = sicuramente '0', 255 = sicuramente '1' (formato tipico dei
    // demodulatori QPSK per LRPT).
    static std::vector<uint8_t> decodeSoft(const std::vector<uint8_t>& softSymbols);
};

} // namespace sdrjo::lrpt
