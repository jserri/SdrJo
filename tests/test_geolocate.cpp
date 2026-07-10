//
// Test del parser della geolocalizzazione IP (senza rete: solo JSON).
//
#include <sdrjo/util/geolocate.hpp>

#include <cmath>
#include <cstdio>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::printf("FALLITO %s:%d: %s\n", __FILE__, __LINE__, #cond);   \
            failures++;                                                      \
        }                                                                    \
    } while (0)

int main()
{
    using sdrjo::GeoIpResult;
    using sdrjo::parseGeoIpJson;

    // Risposta tipica di ip-api.com.
    {
        GeoIpResult r;
        CHECK(parseGeoIpJson(
            "{\"status\":\"success\",\"city\":\"Roma\","
            "\"lat\":41.8905,\"lon\":12.4942}", r));
        CHECK(r.ok);
        CHECK(std::fabs(r.latDeg - 41.8905) < 1e-9);
        CHECK(std::fabs(r.lonDeg - 12.4942) < 1e-9);
        CHECK(r.city == "Roma");
    }

    // Ordine dei campi diverso e lat/lon negativi/interi.
    {
        GeoIpResult r;
        CHECK(parseGeoIpJson(
            "{\"lon\":-70.65,\"lat\":-33,\"city\":\"Santiago\","
            "\"status\":\"success\"}", r));
        CHECK(r.ok);
        CHECK(std::fabs(r.latDeg + 33.0) < 1e-9);
        CHECK(std::fabs(r.lonDeg + 70.65) < 1e-9);
    }

    // Errore dichiarato dal servizio.
    {
        GeoIpResult r;
        CHECK(!parseGeoIpJson(
            "{\"status\":\"fail\",\"message\":\"private range\"}", r));
        CHECK(!r.ok);
        CHECK(r.error == "private range");
    }

    // Risposta troncata o vuota: niente crash, errore pulito.
    {
        GeoIpResult r;
        CHECK(!parseGeoIpJson("{\"status\":\"success\",\"lat\":", r));
        CHECK(!r.ok);
        GeoIpResult r2;
        CHECK(!parseGeoIpJson("", r2));
    }

    if (failures == 0) std::printf("test_geolocate: OK\n");
    return failures == 0 ? 0 : 1;
}
