#include "sdrjo/sat/orbit.hpp"

#include <cmath>

namespace sdrjo::sat {

namespace {
constexpr double kPi = 3.14159265358979323846;
constexpr double kMu = 398600.4418;      // km^3/s^2
constexpr double kRe = 6378.137;         // raggio equatoriale, km
constexpr double kJ2 = 1.08262668e-3;
constexpr double kC = 299792.458;        // km/s
constexpr double kE2 = 6.69437999014e-3; // eccentricita'^2 WGS84

double deg2rad(double d) { return d * kPi / 180.0; }
double rad2deg(double r) { return r * 180.0 / kPi; }

double wrap360(double deg)
{
    deg = std::fmod(deg, 360.0);
    return deg < 0 ? deg + 360.0 : deg;
}

// Osservatore geodetico WGS84 -> ECEF (km).
void observerEcef(double latDeg, double lonDeg, double altKm, double out[3])
{
    double lat = deg2rad(latDeg), lon = deg2rad(lonDeg);
    double sl = std::sin(lat), cl = std::cos(lat);
    double nn = kRe / std::sqrt(1.0 - kE2 * sl * sl);
    out[0] = (nn + altKm) * cl * std::cos(lon);
    out[1] = (nn + altKm) * cl * std::sin(lon);
    out[2] = (nn * (1.0 - kE2) + altKm) * sl;
}
} // namespace

double gmstDeg(double unixTime)
{
    // Giorni da J2000 (2000-01-01 12:00 UTC): formula IAU semplificata.
    double d = unixTime / 86400.0 - 10957.5;
    return wrap360(280.46061837 + 360.98564736629 * d);
}

OrbitPropagator::OrbitPropagator(const Tle& tle)
{
    epoch_ = tle.epochUnix;
    incl_ = deg2rad(tle.inclDeg);
    raan0_ = deg2rad(tle.raanDeg);
    ecc_ = tle.ecc;
    argp0_ = deg2rad(tle.argpDeg);
    m0_ = deg2rad(tle.meanAnomDeg);
    n_ = tle.meanMotion * 2.0 * kPi / 86400.0; // rad/s
    a_ = std::cbrt(kMu / (n_ * n_));

    // Derive secolari da J2 (precessione del nodo e del perigeo): sono
    // quelle che fanno "girare" il piano orbitale dei sun-sincroni.
    double p = a_ * (1.0 - ecc_ * ecc_);
    double j2f = kJ2 * (kRe / p) * (kRe / p) * n_;
    double s2 = std::sin(incl_) * std::sin(incl_);
    raanDot_ = -1.5 * j2f * std::cos(incl_);
    argpDot_ = 0.75 * j2f * (5.0 * (1.0 - s2) - 1.0);
}

double OrbitPropagator::raanRateDegPerDay() const
{
    return rad2deg(raanDot_) * 86400.0;
}

void OrbitPropagator::positionEci(double unixTime, double r[3],
                                  double v[3]) const
{
    double dt = unixTime - epoch_;
    double m = m0_ + n_ * dt;
    double raan = raan0_ + raanDot_ * dt;
    double argp = argp0_ + argpDot_ * dt;

    // Equazione di Keplero: E - e sinE = M (Newton).
    double e = ecc_;
    double E = m;
    for (int i = 0; i < 12; i++) {
        double f = E - e * std::sin(E) - m;
        E -= f / (1.0 - e * std::cos(E));
    }
    double sv = std::sqrt(1.0 + e) * std::sin(E / 2.0);
    double cv = std::sqrt(1.0 - e) * std::cos(E / 2.0);
    double nu = 2.0 * std::atan2(sv, cv);
    double rad = a_ * (1.0 - e * std::cos(E));

    // Coordinate e velocita' nel piano orbitale (perifocale).
    double p = a_ * (1.0 - e * e);
    double sqmp = std::sqrt(kMu / p);
    double xp = rad * std::cos(nu), yp = rad * std::sin(nu);
    double vxp = -sqmp * std::sin(nu), vyp = sqmp * (e + std::cos(nu));

    // Rotazione perifocale -> ECI: Rz(raan) * Rx(i) * Rz(argp).
    double co = std::cos(raan), so = std::sin(raan);
    double ci = std::cos(incl_), si = std::sin(incl_);
    double cw = std::cos(argp), sw = std::sin(argp);
    double r11 = co * cw - so * sw * ci, r12 = -co * sw - so * cw * ci;
    double r21 = so * cw + co * sw * ci, r22 = -so * sw + co * cw * ci;
    double r31 = sw * si, r32 = cw * si;

    r[0] = r11 * xp + r12 * yp;
    r[1] = r21 * xp + r22 * yp;
    r[2] = r31 * xp + r32 * yp;
    v[0] = r11 * vxp + r12 * vyp;
    v[1] = r21 * vxp + r22 * vyp;
    v[2] = r31 * vxp + r32 * vyp;
}

AzEl OrbitPropagator::observe(double unixTime, double latDeg, double lonDeg,
                              double altM) const
{
    auto rangeVec = [&](double t, double d[3]) {
        double r[3], v[3];
        positionEci(t, r, v);
        // ECI -> ECEF: rotazione di -GMST attorno a Z.
        double th = deg2rad(gmstDeg(t));
        double ct = std::cos(th), st = std::sin(th);
        double se[3] = {ct * r[0] + st * r[1], -st * r[0] + ct * r[1], r[2]};
        double obs[3];
        observerEcef(latDeg, lonDeg, altM / 1000.0, obs);
        d[0] = se[0] - obs[0];
        d[1] = se[1] - obs[1];
        d[2] = se[2] - obs[2];
    };

    double d[3];
    rangeVec(unixTime, d);

    // ECEF -> ENU rispetto all'osservatore.
    double lat = deg2rad(latDeg), lon = deg2rad(lonDeg);
    double sl = std::sin(lat), cl = std::cos(lat);
    double so = std::sin(lon), co = std::cos(lon);
    double east = -so * d[0] + co * d[1];
    double north = -sl * co * d[0] - sl * so * d[1] + cl * d[2];
    double up = cl * co * d[0] + cl * so * d[1] + sl * d[2];

    AzEl out;
    out.rangeKm = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    out.azDeg = wrap360(rad2deg(std::atan2(east, north)));
    out.elDeg = rad2deg(std::atan2(up, std::sqrt(east * east +
                                                 north * north)));

    // Range rate numerico su +/- 0.5 s (robusto e piu' che sufficiente).
    double d0[3], d1[3];
    rangeVec(unixTime - 0.5, d0);
    rangeVec(unixTime + 0.5, d1);
    double rr0 = std::sqrt(d0[0] * d0[0] + d0[1] * d0[1] + d0[2] * d0[2]);
    double rr1 = std::sqrt(d1[0] * d1[0] + d1[1] * d1[1] + d1[2] * d1[2]);
    out.rangeRateKmS = rr1 - rr0;
    return out;
}

double OrbitPropagator::dopplerHz(double unixTime, double latDeg,
                                  double lonDeg, double freqHz) const
{
    AzEl a = observe(unixTime, latDeg, lonDeg);
    return -a.rangeRateKmS / kC * freqHz;
}

std::vector<SatPass> OrbitPropagator::findPasses(double startUnix,
                                                 double hours, double latDeg,
                                                 double lonDeg,
                                                 double minElDeg) const
{
    std::vector<SatPass> out;
    auto elAt = [&](double t) {
        return observe(t, latDeg, lonDeg).elDeg;
    };

    const double step = 30.0;
    const double endT = startUnix + hours * 3600.0;
    bool wasUp = elAt(startUnix) >= minElDeg;
    double aos = wasUp ? startUnix : 0.0;

    for (double t = startUnix + step; t <= endT; t += step) {
        bool up = elAt(t) >= minElDeg;
        if (up && !wasUp) {
            // Il raffinamento vuole (sotto, sopra) in ordine temporale.
            double lo = t - step, hi = t;
            for (int i = 0; i < 24 && hi - lo > 0.5; i++) {
                double mid = 0.5 * (lo + hi);
                (elAt(mid) >= minElDeg ? hi : lo) = mid;
            }
            aos = 0.5 * (lo + hi);
        } else if (!up && wasUp && aos > 0.0) {
            double lo = t - step, hi = t;
            for (int i = 0; i < 24 && hi - lo > 0.5; i++) {
                double mid = 0.5 * (lo + hi);
                (elAt(mid) >= minElDeg ? lo : hi) = mid;
            }
            double los = 0.5 * (lo + hi);

            SatPass p;
            p.aosUnix = aos;
            p.losUnix = los;
            p.aosAzDeg = observe(aos, latDeg, lonDeg).azDeg;
            p.losAzDeg = observe(los, latDeg, lonDeg).azDeg;
            p.maxElDeg = -90.0;
            for (double tt = aos; tt <= los; tt += 1.0) {
                double el = elAt(tt);
                if (el > p.maxElDeg) {
                    p.maxElDeg = el;
                    p.tcaUnix = tt;
                }
            }
            out.push_back(p);
            aos = 0.0;
        }
        wasUp = up;
    }
    return out;
}

} // namespace sdrjo::sat
