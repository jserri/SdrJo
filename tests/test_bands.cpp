//
// Test del piano di banda usato dalle guide sullo spettro.
//
#include <sdrjo/util/band_plan.hpp>
#include "test_util.hpp"

#include <string>

using namespace sdrjo;

int main()
{
    // Piano ordinato e coerente.
    const auto& plan = bandPlan();
    CHECK(plan.size() > 20);
    for (size_t i = 0; i < plan.size(); i++) {
        CHECK(plan[i].lowHz < plan[i].highHz);
        if (i > 0) CHECK(plan[i].lowHz >= plan[i - 1].lowHz);
    }

    // Lo span della banda FM deve contenere "FM broadcast".
    bool foundFm = false, foundAero = false;
    for (const auto& b : bandsInRange(98.8e6, 101.2e6))
        if (std::string(b.name) == "FM broadcast") foundFm = true;
    CHECK(foundFm);

    // 1090 MHz: banda ADS-B.
    for (const auto& b : bandsInRange(1089e6, 1091e6))
        if (std::string(b.name) == "ADS-B 1090") foundAero = true;
    CHECK(foundAero);

    // 40m dei radioamatori.
    auto r40 = bandsInRange(7.05e6, 7.1e6);
    CHECK(!r40.empty());
    CHECK(std::string(r40[0].category) == "ham");

    // Fuori da ogni banda: vuoto.
    CHECK(bandsInRange(2.2e9, 2.3e9).empty());

    return testResult("test_bands");
}
