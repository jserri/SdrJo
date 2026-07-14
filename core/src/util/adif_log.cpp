#include "sdrjo/util/adif_log.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <vector>

namespace sdrjo {
namespace {

std::vector<std::string> splitWords(const std::string& s)
{
    std::vector<std::string> w;
    std::string cur;
    for (char c : s) {
        if (c == ' ' || c == '\t') { if (!cur.empty()) { w.push_back(cur); cur.clear(); } }
        else cur += c;
    }
    if (!cur.empty()) w.push_back(cur);
    return w;
}

bool looksLikeCall(const std::string& w)
{
    // Un callsign ha almeno una cifra e almeno una lettera, lunghezza 3..11.
    if (w.size() < 3 || w.size() > 11) return false;
    if (w.front() == '<' || w == "RR73" || w == "RRR") return false;
    bool digit = false, alpha = false;
    for (char c : w) {
        if (std::isdigit((unsigned char)c)) digit = true;
        else if (std::isalpha((unsigned char)c)) alpha = true;
    }
    return digit && alpha;
}

bool isGrid4(const std::string& w)
{
    if (w.size() != 4) return false;
    return w[0] >= 'A' && w[0] <= 'R' && w[1] >= 'A' && w[1] <= 'R' &&
           std::isdigit((unsigned char)w[2]) && std::isdigit((unsigned char)w[3]);
}

bool isReport(const std::string& w)
{
    if (w.size() < 2) return false;
    size_t i = 0;
    if (w[0] == 'R' && w.size() >= 3) i = 1; // "R-09"
    if (w[i] != '+' && w[i] != '-') return false;
    for (size_t k = i + 1; k < w.size(); k++)
        if (!std::isdigit((unsigned char)w[k])) return false;
    return true;
}

std::string field(const std::string& name, const std::string& val)
{
    if (val.empty()) return "";
    std::ostringstream o;
    o << '<' << name << ':' << val.size() << '>' << val << ' ';
    return o.str();
}

} // namespace

bool parseFt8Message(const std::string& message, AdifSpot& out)
{
    out = AdifSpot{};
    auto w = splitWords(message);
    if (w.empty()) return false;

    size_t txIdx = 1; // per default la trasmittente e' il 2o callsign
    if (w[0] == "CQ") {
        // "CQ [DX/zona] CALL GRID": la trasmittente e' il primo callsign vero.
        txIdx = std::string::npos;
        for (size_t i = 1; i < w.size(); i++)
            if (looksLikeCall(w[i])) { txIdx = i; break; }
        if (txIdx == std::string::npos) return false;
    }
    if (txIdx >= w.size() || !looksLikeCall(w[txIdx])) return false;
    out.call = w[txIdx];

    for (size_t i = 0; i < w.size(); i++) {
        if (i == txIdx) continue;
        if (isGrid4(w[i]) && out.grid.empty()) out.grid = w[i];
        else if (isReport(w[i]) && out.rst.empty())
            out.rst = (w[i][0] == 'R') ? w[i].substr(1) : w[i];
    }
    return true;
}

std::string adifBand(double freqHz)
{
    double mhz = freqHz / 1e6;
    struct B { double lo, hi; const char* name; };
    static const B bands[] = {
        {1.8, 2.0, "160m"},   {3.5, 4.0, "80m"},    {5.06, 5.45, "60m"},
        {7.0, 7.3, "40m"},    {10.1, 10.15, "30m"}, {14.0, 14.35, "20m"},
        {18.06, 18.17, "17m"},{21.0, 21.45, "15m"}, {24.89, 24.99, "12m"},
        {28.0, 29.7, "10m"},  {50.0, 54.0, "6m"},   {144.0, 148.0, "2m"},
        {430.0, 440.0, "70cm"},
    };
    for (const auto& b : bands)
        if (mhz >= b.lo && mhz <= b.hi) return b.name;
    return "";
}

std::string adifRecord(const AdifSpot& spot, const std::string& mode,
                       double freqHz, std::time_t when)
{
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &when);
#else
    gmtime_r(&when, &tm);
#endif
    char date[16], time[16];
    std::strftime(date, sizeof(date), "%Y%m%d", &tm);
    std::strftime(time, sizeof(time), "%H%M%S", &tm);
    char freq[24];
    std::snprintf(freq, sizeof(freq), "%.6f", freqHz / 1e6);

    std::string r;
    r += field("CALL", spot.call);
    r += field("GRIDSQUARE", spot.grid);
    r += field("MODE", mode);
    r += field("FREQ", freq);
    r += field("BAND", adifBand(freqHz));
    r += field("QSO_DATE", date);
    r += field("TIME_ON", time);
    r += field("RST_RCVD", spot.rst);
    r += field("COMMENT", "SdrJo spot");
    r += "<EOR>";
    return r;
}

bool AdifLogger::open(const std::string& path)
{
    path_ = path;
    // Se il file non esiste, scrivi l'intestazione ADIF.
    FILE* f = std::fopen(path.c_str(), "r");
    bool exists = (f != nullptr);
    if (f) std::fclose(f);
    if (!exists) {
        FILE* w = std::fopen(path.c_str(), "w");
        if (!w) return false;
        std::fprintf(w, "SdrJo ADIF spot log\n"
                        "<ADIF_VER:5>3.1.4 <PROGRAMID:5>SdrJo <EOH>\n");
        std::fclose(w);
    }
    open_ = true;
    return true;
}

bool AdifLogger::logMessage(const std::string& message, const std::string& mode,
                            double freqHz, std::time_t when)
{
    if (!open_) return false;
    AdifSpot spot;
    if (!parseFt8Message(message, spot)) return false;
    FILE* f = std::fopen(path_.c_str(), "a");
    if (!f) return false;
    std::string rec = adifRecord(spot, mode, freqHz, when);
    std::fprintf(f, "%s\n", rec.c_str());
    std::fclose(f);
    return true;
}

} // namespace sdrjo
