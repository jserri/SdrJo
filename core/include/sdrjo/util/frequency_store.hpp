#pragma once
//
// Frequency manager: memorie di frequenza con nome, modo e banda,
// salvate in un file CSV leggibile accanto all'eseguibile.
//
#include <string>
#include <vector>

namespace sdrjo {

struct FavoriteFrequency {
    std::string name;
    double freqHz = 0.0;
    std::string mode;      // "WFM", "NFM", "AM", "USB", "LSB", ...
    double bandwidthHz = 0.0;
    std::string category;  // opzionale: es. "Radio", "Aereo", "Ham"...
};

class FrequencyStore {
public:
    // Carica dal file (true anche se il file non esiste ancora: lista vuota).
    bool load(const std::string& path);

    // Salva sul file. Formato: nome;freq_hz;modo;banda_hz
    bool save(const std::string& path) const;

    std::vector<FavoriteFrequency>& items() { return items_; }
    const std::vector<FavoriteFrequency>& items() const { return items_; }

    void add(FavoriteFrequency f);
    void remove(size_t index);

private:
    std::vector<FavoriteFrequency> items_;
};

} // namespace sdrjo
