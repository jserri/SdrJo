//
// Test del decoder Mode S con i vettori noti di "The 1090 MHz Riddle".
//
#include <sdrjo/adsb/mode_s.hpp>
#include "test_util.hpp"

using namespace sdrjo::adsb;

int main()
{
    // --- Identificazione (TC=4): volo KLM1023, ICAO 4840D6 -------------
    {
        ModeSMessage m;
        CHECK(decodeHex("8D4840D6202CC371C32CE0576098", m));
        CHECK(m.df == 17);
        CHECK(m.crcOk);
        CHECK(m.icao == 0x4840D6);
        CHECK(m.typeCode == 4);
        CHECK(m.hasCallsign);
        CHECK(m.callsign == "KLM1023");
    }

    // --- CRC: un bit corrotto deve far fallire la decodifica -----------
    {
        ModeSMessage m;
        CHECK(!decodeHex("8D4840D6202CC371C32CE0576099", m));
    }

    // --- Posizione airborne (TC=11): coppia even/odd --------------------
    {
        ModeSMessage even, odd;
        CHECK(decodeHex("8D40621D58C382D690C8AC2863A7", even));
        CHECK(decodeHex("8D40621D58C386435CC412692AD6", odd));

        CHECK(even.icao == 0x40621D);
        CHECK(even.hasAltitude);
        CHECK(even.altitudeFt == 38000);
        CHECK(even.hasCprPosition && !even.cprOdd);
        CHECK(odd.hasCprPosition && odd.cprOdd);

        // Valori CPR grezzi attesi (dal libro).
        CHECK_NEAR(even.cprLat * 131072.0, 93000.0, 0.5);
        CHECK_NEAR(even.cprLon * 131072.0, 51372.0, 0.5);
        CHECK_NEAR(odd.cprLat * 131072.0, 74158.0, 0.5);
        CHECK_NEAR(odd.cprLon * 131072.0, 50194.0, 0.5);

        auto pos = cprGlobalDecode(even.cprLat, even.cprLon,
                                   odd.cprLat, odd.cprLon,
                                   /*lastIsOdd=*/false);
        CHECK(pos.has_value());
        if (pos) {
            CHECK_NEAR(pos->latDeg, 52.2572, 0.001);
            CHECK_NEAR(pos->lonDeg, 3.91937, 0.01);
        }
    }

    // --- Velocita' (TC=19 subtype 1) ------------------------------------
    {
        ModeSMessage m;
        CHECK(decodeHex("8D485020994409940838175B284F", m));
        CHECK(m.icao == 0x485020);
        CHECK(m.typeCode == 19);
        CHECK(m.hasVelocity);
        CHECK_NEAR(m.groundSpeedKt, 159.20, 0.1);
        CHECK_NEAR(m.trackDeg, 182.88, 0.1);
        CHECK(m.verticalRateFpm == -832);
    }

    // --- Funzione NL del CPR --------------------------------------------
    {
        CHECK(cprNL(0.0) == 59);
        CHECK(cprNL(52.2572) == 36);
        CHECK(cprNL(87.5) == 1);
        CHECK(cprNL(-52.2572) == 36); // simmetrica
    }

    return testResult("test_mode_s");
}
