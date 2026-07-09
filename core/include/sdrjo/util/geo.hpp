#pragma once
//
// Utility geografiche: distanza e rilevamento tra coordinate,
// usate per posizionare i contatti rispetto all'antenna.
//
#include <algorithm>
#include <cmath>

namespace sdrjo::geo {

constexpr double kEarthRadiusKm = 6371.0;

// Non si usa M_PI nei header pubblici: su MSVC richiederebbe
// _USE_MATH_DEFINES in ogni file che li include.
constexpr double kPi = 3.14159265358979323846;

inline double deg2rad(double d) { return d * kPi / 180.0; }

// Distanza sul cerchio massimo (haversine), in chilometri.
inline double haversineKm(double lat1, double lon1, double lat2, double lon2)
{
    double dLat = deg2rad(lat2 - lat1);
    double dLon = deg2rad(lon2 - lon1);
    double a = std::sin(dLat / 2) * std::sin(dLat / 2) +
               std::cos(deg2rad(lat1)) * std::cos(deg2rad(lat2)) *
               std::sin(dLon / 2) * std::sin(dLon / 2);
    return 2.0 * kEarthRadiusKm * std::asin(std::min(1.0, std::sqrt(a)));
}

// Rilevamento iniziale da (lat1,lon1) verso (lat2,lon2), gradi 0..360 (0 = nord).
inline double bearingDeg(double lat1, double lon1, double lat2, double lon2)
{
    double dLon = deg2rad(lon2 - lon1);
    double y = std::sin(dLon) * std::cos(deg2rad(lat2));
    double x = std::cos(deg2rad(lat1)) * std::sin(deg2rad(lat2)) -
               std::sin(deg2rad(lat1)) * std::cos(deg2rad(lat2)) * std::cos(dLon);
    double b = std::atan2(y, x) * 180.0 / kPi;
    return (b < 0) ? b + 360.0 : b;
}

} // namespace sdrjo::geo
