//
// Test delle utility geografiche (haversine e rilevamento).
//
#include <sdrjo/util/geo.hpp>
#include "test_util.hpp"

using namespace sdrjo::geo;

int main()
{
    // 1 grado di longitudine all'equatore = 111.195 km (R = 6371 km).
    CHECK_NEAR(haversineKm(0, 0, 0, 1), 111.195, 0.01);
    CHECK_NEAR(haversineKm(0, 0, 1, 0), 111.195, 0.01);
    CHECK_NEAR(haversineKm(42, 12, 42, 12), 0.0, 1e-9);

    // Malpensa (45.63, 8.72) - Fiumicino (41.80, 12.24): ~516 km.
    double d = haversineKm(45.63, 8.72, 41.80, 12.24);
    CHECK(d > 500 && d < 530);

    // Rilevamenti cardinali.
    CHECK_NEAR(bearingDeg(0, 0, 0, 1), 90.0, 0.01);    // est
    CHECK_NEAR(bearingDeg(0, 0, 1, 0), 0.0, 0.01);     // nord
    CHECK_NEAR(bearingDeg(0, 0, 0, -1), 270.0, 0.01);  // ovest
    CHECK_NEAR(bearingDeg(0, 0, -1, 0), 180.0, 0.01);  // sud

    return testResult("test_geo");
}
