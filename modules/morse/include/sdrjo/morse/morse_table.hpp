#pragma once
//
// Tabella Morse internazionale (ITU-R M.1677-1).
//
#include <string>

namespace sdrjo::morse {

// Converte una sequenza di '.' e '-' in un carattere; '\0' se sconosciuta.
char symbolToChar(const std::string& symbol);

// Converte un carattere in punti/linee; stringa vuota se non codificabile.
std::string charToSymbol(char c);

} // namespace sdrjo::morse
