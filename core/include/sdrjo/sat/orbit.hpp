#pragma once
//
// Propagatore orbitale semplificato (Kepler + perturbazioni secolari J2)
// per la predizione dei passaggi dei satelliti LEO (NOAA, Meteor, ISS).
//
// NON e' SGP4: con TLE freschi (pochi giorni) l'errore sui tempi di
// passaggio e' di pochi secondi, piu' che sufficiente per AOS/LOS e
// Doppler. Rinnovare i TLE spesso.
//
#include "tle.hpp"

#include <vector>

namespace sdrjo::sat {

struct AzEl {
    double azDeg = 0;         // azimut (0 = nord, 90 = est)
    double elDeg = -90;       // elevazione sull'orizzonte
    double rangeKm = 0;       // distanza osservatore-satellite
    double rangeRateKmS = 0;  // derivata della distanza (+ = si allontana)
};

struct SatPass {
    double aosUnix = 0;  // Acquisition Of Signal (elevazione > soglia)
    double tcaUnix = 0;  // Time of Closest Approach (elevazione massima)
    double losUnix = 0;  // Loss Of Signal
    double maxElDeg = 0;
    double aosAzDeg = 0;
    double losAzDeg = 0;
};

// Tempo siderale di Greenwich in gradi (per la rotazione ECI -> ECEF).
double gmstDeg(double unixTime);

class OrbitPropagator {
public:
    explicit OrbitPropagator(const Tle& tle);

    // Posizione (km) e velocita' (km/s) in coordinate ECI al tempo dato.
    void positionEci(double unixTime, double r[3], double v[3]) const;

    // Azimut/elevazione/distanza visti da un osservatore a terra.
    AzEl observe(double unixTime, double latDeg, double lonDeg,
                 double altM = 0.0) const;

    // Passaggi sopra minElDeg nelle prossime "hours" ore (passo 30 s,
    // AOS/LOS raffinati al secondo).
    std::vector<SatPass> findPasses(double startUnix, double hours,
                                    double latDeg, double lonDeg,
                                    double minElDeg = 5.0) const;

    // Offset Doppler in Hz per una portante freqHz (positivo in avvicinamento).
    double dopplerHz(double unixTime, double latDeg, double lonDeg,
                     double freqHz) const;

    // Semiasse maggiore (km) e quota media (km): utili per i test.
    double semiMajorAxisKm() const { return a_; }
    double raanRateDegPerDay() const;

private:
    double epoch_;   // unix
    double incl_;    // rad
    double raan0_;   // rad
    double ecc_;
    double argp0_;   // rad
    double m0_;      // rad
    double n_;       // moto medio effettivo, rad/s
    double a_;       // km
    double raanDot_; // rad/s
    double argpDot_; // rad/s
};

} // namespace sdrjo::sat
