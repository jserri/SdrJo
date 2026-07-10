#include "sdrjo/sat/tle.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace sdrjo::sat {

int tleChecksum(const std::string& line)
{
    int sum = 0;
    size_t n = line.size() < 68 ? line.size() : 68;
    for (size_t i = 0; i < n; i++) {
        char c = line[i];
        if (c >= '0' && c <= '9') sum += c - '0';
        else if (c == '-') sum += 1;
    }
    return sum % 10;
}

// Campo numerico alle colonne [from, to) (indici 0-based).
static double field(const std::string& line, size_t from, size_t to)
{
    if (line.size() < to) return 0.0;
    return std::atof(line.substr(from, to - from).c_str());
}

// Giorni dal 1970 al 1 gennaio dell'anno dato (solo anni >= 1970).
static long long daysToYear(int year)
{
    long long days = 0;
    for (int y = 1970; y < year; y++) {
        bool leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
        days += leap ? 366 : 365;
    }
    return days;
}

bool parseTle(const std::string& name, const std::string& line1,
              const std::string& line2, Tle& out)
{
    if (line1.size() < 69 || line2.size() < 69) return false;
    if (line1[0] != '1' || line2[0] != '2') return false;
    if (tleChecksum(line1) != line1[68] - '0') return false;
    if (tleChecksum(line2) != line2[68] - '0') return false;

    Tle t;
    t.name = name;
    t.satNum = int(field(line1, 2, 7));

    // Epoca: anno a 2 cifre (57-99 -> 19xx, 00-56 -> 20xx) + giorno
    // frazionario dell'anno (il giorno 1.0 e' il 1 gennaio 00:00 UTC).
    int yy = int(field(line1, 18, 20));
    int year = (yy >= 57) ? 1900 + yy : 2000 + yy;
    double doy = field(line1, 20, 32);
    t.epochUnix = (double(daysToYear(year)) + (doy - 1.0)) * 86400.0;

    t.inclDeg = field(line2, 8, 16);
    t.raanDeg = field(line2, 17, 25);
    t.ecc = field(line2, 26, 33) * 1e-7; // decimale implicito
    t.argpDeg = field(line2, 34, 42);
    t.meanAnomDeg = field(line2, 43, 51);
    t.meanMotion = field(line2, 52, 63);
    if (t.meanMotion <= 0.0) return false;

    out = t;
    return true;
}

std::vector<Tle> parseTleText(const std::string& text)
{
    std::vector<Tle> out;
    std::istringstream in(text);
    std::string line, name, l1;
    int stage = 0; // 0 = aspetto nome/riga1, 1 = ho la riga 1
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
            line.pop_back();
        if (line.empty()) continue;
        if (line.size() >= 69 && line[0] == '1' && line[1] == ' ') {
            l1 = line;
            stage = 1;
        } else if (stage == 1 && line.size() >= 69 && line[0] == '2' &&
                   line[1] == ' ') {
            Tle t;
            if (parseTle(name, l1, line, t)) out.push_back(std::move(t));
            stage = 0;
            name.clear();
        } else {
            name = line; // riga del nome (formato a 3 righe)
            stage = 0;
        }
    }
    // Nomi mancanti (formato a 2 righe): usa il numero di catalogo.
    for (auto& t : out) {
        if (t.name.empty()) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "NORAD %d", t.satNum);
            t.name = buf;
        }
    }
    return out;
}

std::vector<Tle> loadTleFile(const std::string& path)
{
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string text;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0)
        text.append(buf, n);
    std::fclose(f);
    return parseTleText(text);
}

} // namespace sdrjo::sat
