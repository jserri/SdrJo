#pragma once
//
// Scrittura di immagini BMP a 8 bit in scala di grigi (con palette):
// formato senza compressione, apribile ovunque su Windows.
//
#include <cstdint>
#include <string>

namespace sdrjo {

// data: width*height byte, riga 0 in alto. Ritorna false su errore di I/O.
bool writeGrayscaleBmp(const std::string& path, const uint8_t* data,
                       int width, int height);

} // namespace sdrjo
