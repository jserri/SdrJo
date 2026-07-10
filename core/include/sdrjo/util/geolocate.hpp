#pragma once
//
// Posizione approssimata della stazione tramite geolocalizzazione IP
// (servizio ip-api.com, HTTP semplice). Precisione a livello di citta':
// piu' che sufficiente per la mappa ADS-B e i passaggi satellite; chi
// vuole puo' sempre rifinire lat/lon a mano.
//
#include <string>

namespace sdrjo {

struct GeoIpResult {
    bool ok = false;
    double latDeg = 0.0;
    double lonDeg = 0.0;
    std::string city;  // es. "Roma" (puo' essere vuota)
    std::string error; // valorizzata quando ok == false
};

// Estrae lat/lon/citta' dal JSON di ip-api.com. Esposta separatamente
// per poterla testare senza rete.
bool parseGeoIpJson(const std::string& body, GeoIpResult& out);

// Interroga il servizio (bloccante, timeout di qualche secondo):
// chiamare da un thread di lavoro, non dal thread della GUI.
GeoIpResult geolocateByIp();

} // namespace sdrjo
