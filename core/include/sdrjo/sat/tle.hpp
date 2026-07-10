#pragma once
//
// Parser dei TLE (Two-Line Elements) NORAD/Celestrak: gli elementi
// orbitali con cui si predicono i passaggi di NOAA, Meteor, ISS...
// I TLE invecchiano: per previsioni buone vanno rinnovati ogni pochi
// giorni (celestrak.org, gruppo "weather").
//
#include <string>
#include <vector>

namespace sdrjo::sat {

struct Tle {
    std::string name;      // es. "NOAA 19"
    int satNum = 0;        // numero di catalogo NORAD
    double epochUnix = 0;  // epoca degli elementi (secondi Unix, UTC)
    double inclDeg = 0;    // inclinazione
    double raanDeg = 0;    // ascensione retta del nodo ascendente
    double ecc = 0;        // eccentricita'
    double argpDeg = 0;    // argomento del perigeo
    double meanAnomDeg = 0;// anomalia media all'epoca
    double meanMotion = 0; // giri/giorno
};

// Somma di controllo di una riga TLE (cifre + 1 per ogni '-') modulo 10.
int tleChecksum(const std::string& line);

// true se le due righe sono ben formate (lunghezza, '1 '/'2 ', checksum).
bool parseTle(const std::string& name, const std::string& line1,
              const std::string& line2, Tle& out);

// Legge un testo in formato TLE a 3 righe (nome + riga 1 + riga 2).
std::vector<Tle> parseTleText(const std::string& text);

// Come sopra, da file (es. tle.txt accanto all'eseguibile).
std::vector<Tle> loadTleFile(const std::string& path);

} // namespace sdrjo::sat
