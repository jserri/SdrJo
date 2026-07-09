#pragma once
//
// Piano delle bande (Regione 1 / Italia): usato dallo spettro della GUI
// e dal Cockpit per mostrare "cosa si sta ascoltando" (40m, FM, banda
// aerea, ...).
//
#include <string>
#include <vector>

namespace sdrjo {

struct Band {
    double lowHz;
    double highHz;
    const char* name;
    // Categoria per il colore: "ham", "bc" (broadcast), "aero", "marine",
    // "sat", "ism", "cb", "pmr", "nav".
    const char* category;
};

// Piano completo, ordinato per frequenza crescente.
const std::vector<Band>& bandPlan();

// Bande che intersecano [lowHz, highHz].
std::vector<Band> bandsInRange(double lowHz, double highHz);

} // namespace sdrjo
