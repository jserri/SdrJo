//
// Test del parser TLE e del propagatore orbitale.
//
// Il TLE di prova e' costruito ad arte (orbita sun-sincrona tipo NOAA):
// le verifiche sono di natura fisica: quota, velocita', precessione del
// nodo da J2, numero e durata dei passaggi, segno e ampiezza del Doppler.
//
#include <sdrjo/sat/orbit.hpp>
#include <sdrjo/sat/tle.hpp>

#include <cmath>
#include <cstdio>
#include <string>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

// Somma di controllo calcolata in modo indipendente dal parser.
static char checksumChar(const std::string& l)
{
    int s = 0;
    for (char c : l) {
        if (c >= '0' && c <= '9') s += c - '0';
        if (c == '-') s += 1;
    }
    return char('0' + s % 10);
}

static std::string padTo68(std::string s)
{
    s.resize(68, ' ');
    return s;
}

int main()
{
    // TLE sintetico: sole-sincrono a ~850 km (i=98.7, n=14.1234 giri/di'),
    // epoca 2026 giorno 100.0 (10 aprile 2026 00:00 UTC).
    std::string l1 = padTo68(
        "1 33591U 09005A   26100.00000000  .00000123  00000-0  12345-4 0  9");
    std::string l2 = padTo68(
        "2 33591  98.7000 123.4560 0012345  45.0000 315.0000 14.12345678 1234");
    l1 += checksumChar(l1);
    l2 += checksumChar(l2);

    sdrjo::sat::Tle tle;
    CHECK(sdrjo::sat::parseTle("NOAA TEST", l1, l2, tle));
    CHECK(tle.satNum == 33591);
    CHECK(std::fabs(tle.inclDeg - 98.7) < 1e-9);
    CHECK(std::fabs(tle.raanDeg - 123.456) < 1e-9);
    CHECK(std::fabs(tle.ecc - 0.0012345) < 1e-12);
    CHECK(std::fabs(tle.meanMotion - 14.12345678) < 1e-9);
    // Epoca: 2026-01-01 = 1767225600, +99 giorni.
    CHECK(std::fabs(tle.epochUnix - (1767225600.0 + 99.0 * 86400.0)) < 1.0);

    // Checksum sbagliato -> rifiutato.
    {
        std::string bad = l1;
        bad[68] = char('0' + (bad[68] - '0' + 1) % 10);
        sdrjo::sat::Tle t;
        CHECK(!sdrjo::sat::parseTle("X", bad, l2, t));
    }

    // Testo a 3 righe (con CRLF) -> 1 satellite.
    {
        auto v = sdrjo::sat::parseTleText("NOAA TEST\r\n" + l1 + "\r\n" + l2 +
                                          "\r\n");
        CHECK(v.size() == 1);
        CHECK(!v.empty() && v[0].name == "NOAA TEST");
    }

    // GMST a J2000 (2000-01-01 12:00 UTC): valore noto 280.4606 gradi.
    CHECK(std::fabs(sdrjo::sat::gmstDeg(946728000.0) - 280.4606) < 0.01);

    sdrjo::sat::OrbitPropagator orb(tle);

    // Quota e velocita' da orbita LEO a ~850 km.
    CHECK(orb.semiMajorAxisKm() > 7190 && orb.semiMajorAxisKm() < 7280);
    {
        double r[3], v[3];
        orb.positionEci(tle.epochUnix + 1234.0, r, v);
        double rm = std::sqrt(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]);
        double vm = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        CHECK(rm > 7150 && rm < 7320);
        CHECK(vm > 7.3 && vm < 7.6);
    }

    // Precessione del nodo da J2: un'orbita cosi' e' quasi sun-sincrona,
    // circa +0.99 gradi/giorno.
    std::printf("raan rate = %.3f gradi/giorno\n", orb.raanRateDegPerDay());
    CHECK(orb.raanRateDegPerDay() > 0.85 && orb.raanRateDegPerDay() < 1.15);

    // Passaggi in 24 ore visti dal nord Italia.
    const double lat = 45.0, lon = 9.0;
    auto passes = orb.findPasses(tle.epochUnix, 24.0, lat, lon, 5.0);
    std::printf("passaggi in 24h: %zu\n", passes.size());
    CHECK(passes.size() >= 2 && passes.size() <= 10);
    for (const auto& p : passes) {
        double durMin = (p.losUnix - p.aosUnix) / 60.0;
        CHECK(durMin > 2.0 && durMin < 18.0);
        CHECK(p.maxElDeg >= 5.0 && p.maxElDeg <= 90.0);
        CHECK(p.tcaUnix > p.aosUnix && p.tcaUnix < p.losUnix);
        // Alla soglia l'elevazione e' ~5 gradi (raffinamento al secondo).
        auto a = orb.observe(p.aosUnix, lat, lon);
        CHECK(std::fabs(a.elDeg - 5.0) < 0.5);
    }

    // Doppler su 137.1 MHz: si avvicina prima del TCA (offset positivo),
    // si allontana dopo (negativo), ampiezza fisica (< 4 kHz).
    if (!passes.empty()) {
        const auto& p = passes[0];
        double dAos = orb.dopplerHz(p.aosUnix + 5, lat, lon, 137.1e6);
        double dLos = orb.dopplerHz(p.losUnix - 5, lat, lon, 137.1e6);
        double dTca = orb.dopplerHz(p.tcaUnix, lat, lon, 137.1e6);
        std::printf("doppler: AOS %+.0f Hz, TCA %+.0f Hz, LOS %+.0f Hz\n",
                    dAos, dTca, dLos);
        CHECK(dAos > 500 && dAos < 4000);
        CHECK(dLos < -500 && dLos > -4000);
        CHECK(std::fabs(dTca) < std::fabs(dAos));
    }

    if (failures == 0) std::printf("test_sat: OK\n");
    return failures == 0 ? 0 : 1;
}
