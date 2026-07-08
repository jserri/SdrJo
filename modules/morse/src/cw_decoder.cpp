#include "sdrjo/morse/cw_decoder.hpp"
#include "sdrjo/morse/morse_table.hpp"

#include <cmath>

namespace sdrjo::morse {

CwDecoder::CwDecoder(double sampleRateHz, std::function<void(char)> onText)
    : sampleRate_(sampleRateHz), onText_(std::move(onText))
{
    // Inviluppo con costante di tempo ~5 ms: abbastanza veloce per punti
    // a 40 WPM (30 ms), abbastanza lenta da riempire i cicli del tono.
    envAlpha_ = float(1.0 - std::exp(-1.0 / (sampleRate_ * 0.005)));
}

void CwDecoder::processAudio(const float* samples, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        float mag = std::fabs(samples[i]);
        envelope_ += envAlpha_ * (mag - envelope_);

        // Il picco segue l'inviluppo in salita e decade lentamente:
        // fornisce il riferimento per la soglia adattiva.
        if (envelope_ > peak_) peak_ = envelope_;
        else peak_ *= 1.0f - float(1.0 / (sampleRate_ * 2.0)); // decadimento ~2 s

        // Isteresi: accende al 55% del picco, spegne al 35%.
        bool newState = keyDown_;
        if (!keyDown_ && envelope_ > 0.55f * peak_) newState = true;
        if (keyDown_ && envelope_ < 0.35f * peak_) newState = false;

        if (newState != keyDown_) {
            double durMs = 1000.0 * double(runSamples_) / sampleRate_;
            onStateChange(keyDown_, durMs);
            keyDown_ = newState;
            runSamples_ = 0;
        } else {
            runSamples_++;
            // Silenzio molto lungo: chiudi lettera e parola.
            if (!keyDown_ && runSamples_ > size_t(sampleRate_ * 0.010 * dotMs_)) {
                // (10 * dotMs in campioni) -> fine parola sicura
                if (!currentSymbol_.empty()) endLetter();
                if (!wordGapEmitted_ && !text_.empty()) {
                    text_ += ' ';
                    if (onText_) onText_(' ');
                    wordGapEmitted_ = true;
                }
            }
        }
    }
}

void CwDecoder::onStateChange(bool wasMark, double durationMs)
{
    if (durationMs < 0.3 * dotMs_) return; // glitch/rumore: ignora

    if (wasMark) {
        // Classifica punto o linea e aggiorna la stima del punto.
        if (durationMs < 2.0 * dotMs_) {
            currentSymbol_ += '.';
            dotMs_ = 0.8 * dotMs_ + 0.2 * durationMs;
        } else {
            currentSymbol_ += '-';
            dotMs_ = 0.8 * dotMs_ + 0.2 * (durationMs / 3.0);
        }
        wordGapEmitted_ = false;
    } else {
        // Spazio: dentro la lettera (~1 punto), tra lettere (~3), tra parole (~7).
        if (durationMs > 5.0 * dotMs_) {
            endLetter();
            if (!wordGapEmitted_ && !text_.empty()) {
                text_ += ' ';
                if (onText_) onText_(' ');
                wordGapEmitted_ = true;
            }
        } else if (durationMs > 2.0 * dotMs_) {
            endLetter();
        }
    }
}

void CwDecoder::endLetter()
{
    if (currentSymbol_.empty()) return;
    char c = symbolToChar(currentSymbol_);
    if (c == '\0') c = '_'; // sequenza sconosciuta
    text_ += c;
    if (onText_) onText_(c);
    currentSymbol_.clear();
}

void CwDecoder::flush()
{
    if (keyDown_) {
        double durMs = 1000.0 * double(runSamples_) / sampleRate_;
        onStateChange(true, durMs);
        keyDown_ = false;
        runSamples_ = 0;
    }
    endLetter();
}

} // namespace sdrjo::morse
