#pragma once
//
// Decodificatore CW (Morse) da audio: inviluppo -> soglia adattiva ->
// classificazione punto/linea con stima automatica della velocita' (WPM).
//
#include <functional>
#include <string>

namespace sdrjo::morse {

class CwDecoder {
public:
    // onText viene chiamata a ogni carattere decodificato.
    CwDecoder(double sampleRateHz, std::function<void(char)> onText = {});

    // Elabora audio mono float (il tono CW puo' essere a qualsiasi pitch:
    // si lavora sull'inviluppo, non sulla frequenza).
    void processAudio(const float* samples, size_t n);

    // Chiude l'eventuale carattere in sospeso (fine trasmissione).
    void flush();

    // Testo accumulato dall'inizio (comodo per test e UI).
    const std::string& text() const { return text_; }

    // Stima corrente della velocita' in parole al minuto.
    double wpm() const { return (dotMs_ > 0) ? 1200.0 / dotMs_ : 0.0; }

private:
    void onStateChange(bool mark, double durationMs);
    void endLetter();

    double sampleRate_;
    std::function<void(char)> onText_;
    std::string text_;

    // Inviluppo e soglia.
    float envelope_ = 0.0f;
    float envAlpha_;
    float peak_ = 1e-3f;
    bool keyDown_ = false;
    size_t runSamples_ = 0;

    // Temporizzazione adattiva.
    double dotMs_ = 60.0; // 20 WPM iniziali
    std::string currentSymbol_;
    bool wordGapEmitted_ = true;
};

} // namespace sdrjo::morse
