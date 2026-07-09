#pragma once
//
// Interfaccia web del modulo ADS-B: mappa dei voli in stile SDRAngel /
// tar1090, servita da un HttpServer integrato.
//
//   /                    -> pagina con mappa Leaflet + tabella voli
//   /data/aircraft.json  -> stato corrente degli aerei (aggiornato dal client)
//
#include "aircraft_tracker.hpp"

#include <sdrjo/util/http_server.hpp>

#include <mutex>
#include <optional>
#include <string>

namespace sdrjo::adsb {

// Serializza lo stato del tracker in JSON per la pagina web.
// antenna: posizione del ricevitore (per cerchi di distanza e rilevamenti).
std::string aircraftToJson(const AircraftTracker& tracker,
                           const std::optional<Position>& antenna);

// Pagina HTML della mappa (autonoma, usa i tile OSM/Carto e Leaflet da CDN).
const char* mapPageHtml();

// Server web ADS-B pronto all'uso: incapsula HttpServer + serializzazione,
// con il mutex per l'accesso al tracker dal thread HTTP.
class AdsbWebServer {
public:
    static constexpr uint16_t kDefaultPort = 8757;

    // Il tracker resta di proprieta' del chiamante, che deve proteggere i
    // propri aggiornamenti con lo stesso mutex passato qui.
    AdsbWebServer(AircraftTracker& tracker, std::mutex& trackerMutex);

    void setAntennaPosition(double latDeg, double lonDeg);

    // bindAll = true rende la mappa visibile anche dagli altri dispositivi
    // della rete locale.
    bool start(uint16_t port = kDefaultPort, bool bindAll = false);
    void stop();

    uint16_t port() const { return server_.port(); }
    bool isRunning() const { return server_.isRunning(); }

private:
    AircraftTracker& tracker_;
    std::mutex& mutex_;
    std::optional<Position> antenna_;
    HttpServer server_;
};

} // namespace sdrjo::adsb
