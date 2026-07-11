#pragma once
//
// Decoder RTTY (radiotelescriventi): FSK Baudot a 45.45 baud con shift
// di 170 Hz, i toni standard dei radioamatori (mark 2125 / space 2295).
// Da collegare all'audio demodulato in USB, come si fa con fldigi.
//
// Catena: due risonatori (Goertzel scorrevole) su mark e space ->
// confronto degli inviluppi -> campionamento UART (1 start, 5 dati LSB
// per primo, stop) -> tabella Baudot ITA2 con registri LTRS/FIGS.
//
#include <cstddef>
#include <functional>
#include <string>

namespace sdrjo::dsp {

class RttyDecoder {
public:
    using CharCallback = std::function<void(char)>;

    // markHz/spaceHz: toni audio; baud tipicamente 45.45 (amatoriale).
    RttyDecoder(double sampleRate, CharCallback cb, double markHz = 2125.0,
                double spaceHz = 2295.0, double baud = 45.45);

    void processAudio(const float* samples, size_t n);

    // true = scambia mark e space (segnale ricevuto "al contrario",
    // capita spesso con LSB/USB invertite).
    void setReverse(bool on) { reverse_ = on; }

    void reset();

private:
    // Risonatore a banda stretta: potenza del tono inseguita nel tempo.
    struct Tone {
        float coeffRe, coeffIm; // oscillatore complesso e^{-j2πf/fs}
        float re = 0, im = 0;
        float alpha;            // smorzamento (larghezza di banda)
        float env = 0;
        void configure(double freqHz, double rate, double bwHz);
        void step(float x);
    };

    CharCallback cb_;
    double rate_;
    double samplesPerBit_;
    Tone mark_, space_;
    bool reverse_ = false;

    // Stato UART.
    int state_ = 0;          // 0 = attesa start, 1 = dentro il carattere
    double sampleCount_ = 0; // campioni dall'inizio del carattere
    int bitIndex_ = 0;
    int bits_ = 0;
    bool figs_ = false;
    bool lastTone_ = true;   // true = mark
};

} // namespace sdrjo::dsp
