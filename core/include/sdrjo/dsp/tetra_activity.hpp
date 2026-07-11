#pragma once
//
// Rivelatore di ATTIVITA' TETRA (solo rilevamento, NESSUNA decodifica).
//
// TETRA usa π/4-DQPSK a 18 ksym/s in canali da 25 kHz: la portante di
// base (downlink) e' continua e riempie il canale in modo abbastanza
// piatto, con bordi ripidi (filtro a coseno rialzato). Questo rivelatore
// misura solo *se* c'e' un portante digitale largo ~canale sopra il
// rumore: utile per la caccia agli impianti e la mappatura dello spettro.
//
// NON demodula, NON decodifica e NON decifra: la voce TETRA e' quasi
// sempre cifrata (TEA1/2/3) e la sua intercettazione e' illegale. Qui ci
// si ferma alla presenza/assenza del segnale.
//
// Il canale IQ va centrato sulla frequenza sospetta (come per l'ascolto).
//
#include "types.hpp"

#include <cstddef>
#include <vector>

namespace sdrjo::dsp {

class TetraActivityDetector {
public:
    explicit TetraActivityDetector(double sampleRate);

    void processIq(const cfloat* iq, size_t n);
    void reset();

    // Esiti (aggiornati a ogni blocco FFT).
    bool active() const { return active_; }
    float snrDb() const { return snrDb_; }          // stacco banda/rumore
    float occupiedKHz() const { return occKHz_; }    // larghezza occupata stimata
    float level() const { return level_; }           // 0..1 per una barra

private:
    void analyze();

    double rate_;
    size_t fftN_;
    std::vector<cfloat> buf_;
    std::vector<float> spec_;
    size_t pos_ = 0;

    float snrDb_ = 0.0f;
    float occKHz_ = 0.0f;
    float level_ = 0.0f;
    bool active_ = false;
    int hold_ = 0; // blocchi di "tenuta" dopo l'ultimo rilevamento
};

} // namespace sdrjo::dsp
