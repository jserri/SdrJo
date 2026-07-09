#pragma once
//
// Correzioni del front-end RTL-SDR:
//  - DcBlocker: rimuove lo "spike" DC al centro dello spettro
//  - estimatePpm: stima l'errore del quarzo da una portante nota
//
#include "types.hpp"

#include <cstddef>

namespace sdrjo::dsp {

// Notch a 0 Hz: sottrae la media complessa inseguita con una costante
// di tempo lunga (non tocca i segnali veri, solo l'offset del ricevitore).
class DcBlocker {
public:
    explicit DcBlocker(double alpha = 1e-4) : alpha_(float(alpha)) {}

    void process(const cfloat* in, size_t n, cfloat* out)
    {
        for (size_t i = 0; i < n; i++) {
            dc_ += alpha_ * (in[i] - dc_);
            out[i] = in[i] - dc_;
        }
    }

    void processInPlace(cfloat* buf, size_t n) { process(buf, n, buf); }

    cfloat currentDc() const { return dc_; }

private:
    float alpha_;
    cfloat dc_{0.0f, 0.0f};
};

// Stima l'errore in PPM del quarzo osservando una portante di frequenza
// nota (es. una radio FM forte o un pilota DVB-T).
//
// specDb:        spettro di potenza con DC al centro (come powerSpectrumDb)
// sampleRate:    rate del flusso IQ
// centerFreqHz:  frequenza di sintonia
// expectedHz:    frequenza vera della portante osservata
// searchHz:      semi-ampiezza della finestra di ricerca attorno all'atteso
//
// Ritorna i PPM da passare a setPpmCorrection (0 se non trova un picco
// abbastanza forte, cioe' almeno 6 dB sopra la mediana della finestra).
double estimatePpm(const float* specDb, size_t n, double sampleRate,
                   double centerFreqHz, double expectedHz,
                   double searchHz = 10e3);

} // namespace sdrjo::dsp
