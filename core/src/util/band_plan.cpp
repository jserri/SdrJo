#include "sdrjo/util/band_plan.hpp"

namespace sdrjo {

const std::vector<Band>& bandPlan()
{
    static const std::vector<Band> plan = {
        {148.5e3, 283.5e3, "Onde lunghe", "bc"},
        {526.5e3, 1606.5e3, "Onde medie", "bc"},
        {1.81e6, 2.0e6, "160m radioamatori", "ham"},
        {3.5e6, 3.8e6, "80m radioamatori", "ham"},
        {5.9e6, 6.2e6, "49m broadcast", "bc"},
        {7.0e6, 7.2e6, "40m radioamatori", "ham"},
        {7.2e6, 7.45e6, "41m broadcast", "bc"},
        {9.4e6, 9.9e6, "31m broadcast", "bc"},
        {10.1e6, 10.15e6, "30m radioamatori", "ham"},
        {11.6e6, 12.1e6, "25m broadcast", "bc"},
        {13.57e6, 13.87e6, "22m broadcast", "bc"},
        {14.0e6, 14.35e6, "20m radioamatori", "ham"},
        {15.1e6, 15.83e6, "19m broadcast", "bc"},
        {17.48e6, 17.9e6, "16m broadcast", "bc"},
        {18.068e6, 18.168e6, "17m radioamatori", "ham"},
        {21.0e6, 21.45e6, "15m radioamatori", "ham"},
        {21.45e6, 21.85e6, "13m broadcast", "bc"},
        {24.89e6, 24.99e6, "12m radioamatori", "ham"},
        {25.67e6, 26.1e6, "11m broadcast", "bc"},
        {26.965e6, 27.405e6, "CB 27 MHz", "cb"},
        {28.0e6, 29.7e6, "10m radioamatori", "ham"},
        {50.0e6, 52.0e6, "6m radioamatori", "ham"},
        {87.5e6, 108.0e6, "FM broadcast", "bc"},
        {108.0e6, 118.0e6, "VOR/ILS radionav.", "nav"},
        {118.0e6, 137.0e6, "Banda aerea (AM)", "aero"},
        {137.0e6, 138.0e6, "Satelliti meteo", "sat"},
        {144.0e6, 146.0e6, "2m radioamatori", "ham"},
        {156.0e6, 162.05e6, "VHF marino", "marine"},
        {430.0e6, 440.0e6, "70cm radioamatori", "ham"},
        {433.05e6, 434.79e6, "ISM 433", "ism"},
        {446.0e6, 446.2e6, "PMR446", "pmr"},
        {868.0e6, 870.0e6, "ISM 868", "ism"},
        {1087.0e6, 1093.0e6, "ADS-B 1090", "aero"},
        {1240.0e6, 1300.0e6, "23cm radioamatori", "ham"},
        {1525.0e6, 1559.0e6, "Inmarsat L-band", "sat"},
        {1573.4e6, 1577.4e6, "GPS L1", "nav"},
    };
    return plan;
}

std::vector<Band> bandsInRange(double lowHz, double highHz)
{
    std::vector<Band> out;
    for (const auto& b : bandPlan()) {
        if (b.highHz > lowHz && b.lowHz < highHz) out.push_back(b);
        if (b.lowHz > highHz) break; // il piano e' ordinato
    }
    return out;
}

} // namespace sdrjo
