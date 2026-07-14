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

// BMP a 24 bit da RGB (3 byte/pixel, riga 0 in alto). bottomUp=true se i
// dati arrivano gia' capovolti (es. glReadPixels, riga 0 in basso).
bool writeRgbBmp(const std::string& path, const uint8_t* rgb, int width,
                 int height, bool bottomUp = false);

} // namespace sdrjo
