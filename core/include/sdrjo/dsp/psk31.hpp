#pragma once
//
// Decoder BPSK31 (PSK31): il modo digitale HF piu' diffuso tra i
// radioamatori. 31.25 baud, BPSK differenziale su un tono audio, testo
// codificato in Varicode. Da collegare all'audio demodulato in USB.
//
// Catena: NCO che porta il tono a banda base -> passa-basso -> integrate-
// and-dump per simbolo con aggancio del tempo sui minimi d'ampiezza ->
// decisione BPSK differenziale (inversione di fase = 0, fase costante = 1)
// -> Varicode (i caratteri sono separati da due "0" consecutivi).
//
#include "types.hpp"

#include <cstddef>
#include <functional>
#include <string>

namespace sdrjo::dsp {

class Psk31Decoder {
public:
    using CharCallback = std::function<void(char)>;

    Psk31Decoder(double sampleRate, CharCallback cb, double toneHz = 1000.0);

    void processAudio(const float* samples, size_t n);

    void setTone(double toneHz);
    void reset();

    // Fase dell'ultimo simbolo (per l'oscilloscopio di taratura): la
    // costellazione BPSK ben agganciata mostra due lobi opposti.
    cfloat lastSymbol() const { return lastSym_; }

private:
    void onSymbol(cfloat sym);

    CharCallback cb_;
    double rate_;
    double toneHz_;

    // NCO + filtro a banda base.
    double ncoPhase_ = 0.0;
    double ncoStep_ = 0.0;
    cfloat lpf_{0, 0};
    float lpfAlpha_;

    // Integrate-and-dump per simbolo + aggancio del tempo di simbolo:
    // le inversioni di fase (BPSK) fanno un "buco" d'ampiezza al confine
    // del simbolo; teniamo il confine sui minimi cosi' campioniamo al
    // centro.
    int samplesPerSym_ = 1;
    int symPos_ = 0;      // posizione nel simbolo corrente
    cfloat acc_{0, 0};    // integratore del simbolo
    float envMin_ = 1e9f; // minimo d'inviluppo nel simbolo
    int envMinPos_ = 0;   // dove cade il minimo
    int syncPhase_ = 0;   // correzione lenta del confine

    // Decisione differenziale.
    cfloat prevSym_{1, 0};
    cfloat lastSym_{0, 0};

    // Varicode: i caratteri sono separati da due "0" consecutivi.
    std::string code_;    // bit del carattere corrente
    int lastBit_ = 1;
};

} // namespace sdrjo::dsp
