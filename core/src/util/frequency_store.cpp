#include "sdrjo/util/frequency_store.hpp"

#include <algorithm>
#include <cstdio>

namespace sdrjo {

bool FrequencyStore::load(const std::string& path)
{
    items_.clear();
    FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return true; // nessun file = lista vuota, non e' un errore

    char line[512];
    while (std::fgets(line, sizeof(line), f)) {
        // nome;freq_hz;modo;banda_hz
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r'))
            s.pop_back();
        if (s.empty() || s[0] == '#') continue;

        // nome;freq_hz;modo;banda_hz[;categoria]  (categoria opzionale)
        size_t p1 = s.find(';');
        size_t p2 = (p1 == std::string::npos) ? p1 : s.find(';', p1 + 1);
        size_t p3 = (p2 == std::string::npos) ? p2 : s.find(';', p2 + 1);
        if (p3 == std::string::npos) continue;
        size_t p4 = s.find(';', p3 + 1); // separatore della categoria

        FavoriteFrequency ff;
        ff.name = s.substr(0, p1);
        ff.freqHz = std::atof(s.substr(p1 + 1, p2 - p1 - 1).c_str());
        ff.mode = s.substr(p2 + 1, p3 - p2 - 1);
        ff.bandwidthHz = std::atof(
            s.substr(p3 + 1, p4 == std::string::npos ? p4 : p4 - p3 - 1)
                .c_str());
        if (p4 != std::string::npos) ff.category = s.substr(p4 + 1);
        if (ff.freqHz > 0) items_.push_back(std::move(ff));
    }
    std::fclose(f);
    return true;
}

bool FrequencyStore::save(const std::string& path) const
{
    FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f, "# SdrJo frequency manager: "
                    "nome;freq_hz;modo;banda_hz;categoria\n");
    for (const auto& ff : items_) {
        std::string name = ff.name, cat = ff.category;
        std::replace(name.begin(), name.end(), ';', ','); // niente separatori
        std::replace(cat.begin(), cat.end(), ';', ',');
        std::fprintf(f, "%s;%.0f;%s;%.0f;%s\n", name.c_str(), ff.freqHz,
                     ff.mode.c_str(), ff.bandwidthHz, cat.c_str());
    }
    std::fclose(f);
    return true;
}

void FrequencyStore::add(FavoriteFrequency f)
{
    items_.push_back(std::move(f));
    std::sort(items_.begin(), items_.end(),
              [](const FavoriteFrequency& a, const FavoriteFrequency& b) {
                  return a.freqHz < b.freqHz;
              });
}

void FrequencyStore::remove(size_t index)
{
    if (index < items_.size())
        items_.erase(items_.begin() + ptrdiff_t(index));
}

} // namespace sdrjo
