#pragma once
//
// VFO: estrae un canale a banda stretta dal flusso largo dell'hardware.
//
//   IQ largo (es. 2.4 MS/s) -> shift in frequenza -> filtro+decimazione
//   intera -> (eventuale ricampionamento razionale) -> IQ del canale
//   al rate richiesto dal modulo (es. 240 kHz per l'FM, 48 kHz per l'APT).
//
// Piu' VFO in parallelo = piu' moduli in ascolto nella stessa banda.
//
#include "fir_filter.hpp"
#include "resampler.hpp"

#include <memory>
#include <vector>

namespace sdrjo::dsp {

class Vfo {
public:
    // offsetHz: frequenza del canale rispetto al centro banda.
    Vfo(double inRate, double outRate, double offsetHz);

    void setOffset(double offsetHz) { shifter_.setShift(-offsetHz); }
    double outputRate() const { return actualOutRate_; }

    // Appende i campioni del canale a out; ritorna quanti ne ha aggiunti.
    size_t process(const cfloat* in, size_t n, std::vector<cfloat>& out);

private:
    FrequencyShifter shifter_;
    std::unique_ptr<FirDecimator> decimator_;      // nullo se decimazione = 1
    std::unique_ptr<RationalResampler> resampler_; // nullo se rapporto intero
    double actualOutRate_;

    std::vector<cfloat> shifted_, decimated_;
};

} // namespace sdrjo::dsp
