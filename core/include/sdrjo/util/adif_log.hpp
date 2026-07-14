#pragma once
//
// Log ADIF dei decode FT8/FT4. ADIF e' il formato standard dei log
// radioamatoriali (importabile in WSJT-X, Log4OM, N1MM, LoTW...). SdrJo
// riceve soltanto, quindi qui registriamo gli "spot": ogni stazione sentita
// diventa un record con callsign, locatore, modo, frequenza e ora.
//
#include <ctime>
#include <string>

namespace sdrjo {

// Dati estratti da un messaggio FT8/FT4 per il log.
struct AdifSpot {
    std::string call;   // callsign della stazione trasmittente
    std::string grid;   // locatore Maidenhead (se presente)
    std::string rst;    // rapporto in dB (se presente), es. "-09"
};

// Estrae call/grid/rst da un messaggio decodificato (es. "CQ IZ0ABC JN61"
// oppure "K1ABC IZ0ABC R-09"). Ritorna false se non trova un callsign utile.
bool parseFt8Message(const std::string& message, AdifSpot& out);

// Banda amatoriale (es. "20m") da una frequenza in Hz; vuota se fuori banda.
std::string adifBand(double freqHz);

// Un record ADIF completo (campi + <EOR>) su una riga.
std::string adifRecord(const AdifSpot& spot, const std::string& mode,
                       double freqHz, std::time_t when);

// Logger che accoda record a un file .adi (scrive l'intestazione se nuovo).
class AdifLogger {
public:
    // Apre/crea il file in append. Ritorna false su errore di I/O.
    bool open(const std::string& path);
    bool isOpen() const { return open_; }
    const std::string& path() const { return path_; }

    // Accoda un record; ritorna false se il messaggio non e' loggabile.
    bool logMessage(const std::string& message, const std::string& mode,
                    double freqHz, std::time_t when);
    void close() { open_ = false; }

private:
    std::string path_;
    bool open_ = false;
};

} // namespace sdrjo
