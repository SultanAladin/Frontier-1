/*==========================================================================================================================================
                                                   CELESTIAL MODEL DEFINITION
==========================================================================================================================================*/

#include "CelestialModel.h"

#include "Engine/Foundation/DeterministicRandom.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <istream>
#include <sstream>
#include <string>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;
constexpr double DegToRad = Pi / 180.0;

double WrapDegrees(double Degrees) noexcept
{
    Degrees = std::fmod(Degrees, 360.0);
    return Degrees < 0.0 ? Degrees + 360.0 : Degrees;
}
double WrapPi(double Radians) noexcept
{
    while (Radians > Pi) Radians -= 2.0 * Pi;
    while (Radians < -Pi) Radians += 2.0 * Pi;
    return Radians;
}

} // namespace

double JulianDate(int Year, unsigned Month, unsigned Day, double UtcHours) noexcept
{
    const int A = (14 - static_cast<int>(Month)) / 12;
    const int Y = Year + 4800 - A;
    const int M = static_cast<int>(Month) + 12 * A - 3;
    const long JDN = static_cast<long>(Day) + (153L * M + 2) / 5 + 365L * Y + Y / 4 - Y / 100 + Y / 400 - 32045;
    return static_cast<double>(JDN) + (UtcHours - 12.0) / 24.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            SUN
//------------------------------------------------------------------------------------------------------------------------

SolarPosition SunPosition(double Julian, double LatitudeDeg, double LongitudeDeg) noexcept
{
    const double N = Julian - 2451545.0;   // days since J2000.0

    const double MeanLongitude = WrapDegrees(280.460 + 0.9856474 * N);
    const double MeanAnomaly   = WrapDegrees(357.528 + 0.9856003 * N) * DegToRad;
    const double EclipticLongitude =
        (MeanLongitude + 1.915 * std::sin(MeanAnomaly) + 0.020 * std::sin(2.0 * MeanAnomaly)) * DegToRad;
    const double Obliquity = (23.439 - 0.0000004 * N) * DegToRad;

    const double Declination = std::asin(std::sin(Obliquity) * std::sin(EclipticLongitude));
    const double RightAscension = std::atan2(std::cos(Obliquity) * std::sin(EclipticLongitude),
                                             std::cos(EclipticLongitude));

    // Greenwich mean sidereal time (hours) -> local hour angle.
    double GmstHours = 18.697374558 + 24.06570982441908 * N;
    GmstHours = std::fmod(GmstHours, 24.0);
    if (GmstHours < 0.0) GmstHours += 24.0;
    const double LocalSiderealRad = (GmstHours * 15.0 + LongitudeDeg) * DegToRad;
    const double HourAngle = WrapPi(LocalSiderealRad - RightAscension);

    const double Latitude = LatitudeDeg * DegToRad;
    const double SinElevation = std::sin(Declination) * std::sin(Latitude) +
                                std::cos(Declination) * std::cos(Latitude) * std::cos(HourAngle);
    const double Elevation = std::asin(std::clamp(SinElevation, -1.0, 1.0));
    const double Azimuth = std::atan2(std::sin(HourAngle),
                                      std::cos(HourAngle) * std::sin(Latitude) -
                                          std::tan(Declination) * std::cos(Latitude)) + Pi;   // from north, clockwise

    SolarPosition Result;
    Result.ElevationRad   = Elevation;
    Result.AzimuthRad     = std::fmod(Azimuth, 2.0 * Pi);
    Result.DeclinationRad = Declination;
    return Result;
}

SpatialVector SunDirectionLocal(const SolarPosition& Position) noexcept
{
    const double CosEl = std::cos(Position.ElevationRad);
    // Azimuth clockwise from north: east = sin(az), north = cos(az).
    return {CosEl * std::sin(Position.AzimuthRad), CosEl * std::cos(Position.AzimuthRad),
            std::sin(Position.ElevationRad)};
}

DayPhase ClassifyDayPhase(double SolarElevationRad) noexcept
{
    const double Deg = SolarElevationRad / DegToRad;
    if (Deg > -0.833) return DayPhase::Day;
    if (Deg > -6.0) return DayPhase::CivilTwilight;
    if (Deg > -12.0) return DayPhase::NauticalTwilight;
    if (Deg > -18.0) return DayPhase::AstronomicalTwilight;
    return DayPhase::Night;
}

const char* DayPhaseName(DayPhase Phase) noexcept
{
    switch (Phase)
    {
        case DayPhase::Day: return "Day";
        case DayPhase::CivilTwilight: return "CivilTwilight";
        case DayPhase::NauticalTwilight: return "NauticalTwilight";
        case DayPhase::AstronomicalTwilight: return "AstronomicalTwilight";
        case DayPhase::Night: return "Night";
    }
    return "Night";
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            MOON
//------------------------------------------------------------------------------------------------------------------------

// Meeus chapter 47 periodic coefficients and algorithm adapted from Sonia Keys' MIT-licensed
// github.com/soniakeys/meeus/v3/moonposition, revision bfbd9ac2c7f709c94f1dee190c633d24a723f628.
// Copyright 2013 Sonia Keys. Full license: Documentation/Notices/SoniaKeysMeeus.LICENSE.
namespace
{
struct LunarLongitudeTerm { int D, M, Prime, F, Longitude, Distance; };
struct LunarLatitudeTerm { int D, M, Prime, F, Latitude; };
constexpr LunarLongitudeTerm LunarLongitudeTerms[] = {
    {0, 0, 1, 0, 6288774, -20905355},
    {2, 0, -1, 0, 1274027, -3699111},
    {2, 0, 0, 0, 658314, -2955968},
    {0, 0, 2, 0, 213618, -569925},
    {0, 1, 0, 0, -185116, 48888},
    {0, 0, 0, 2, -114332, -3149},
    {2, 0, -2, 0, 58793, 246158},
    {2, -1, -1, 0, 57066, -152138},
    {2, 0, 1, 0, 53322, -170733},
    {2, -1, 0, 0, 45758, -204586},
    {0, 1, -1, 0, -40923, -129620},
    {1, 0, 0, 0, -34720, 108743},
    {0, 1, 1, 0, -30383, 104755},
    {2, 0, 0, -2, 15327, 10321},
    {0, 0, 1, 2, -12528, 0},
    {0, 0, 1, -2, 10980, 79661},
    {4, 0, -1, 0, 10675, -34782},
    {0, 0, 3, 0, 10034, -23210},
    {4, 0, -2, 0, 8548, -21636},
    {2, 1, -1, 0, -7888, 24208},
    {2, 1, 0, 0, -6766, 30824},
    {1, 0, -1, 0, -5163, -8379},
    {1, 1, 0, 0, 4987, -16675},
    {2, -1, 1, 0, 4036, -12831},
    {2, 0, 2, 0, 3994, -10445},
    {4, 0, 0, 0, 3861, -11650},
    {2, 0, -3, 0, 3665, 14403},
    {0, 1, -2, 0, -2689, -7003},
    {2, 0, -1, 2, -2602, 0},
    {2, -1, -2, 0, 2390, 10056},
    {1, 0, 1, 0, -2348, 6322},
    {2, -2, 0, 0, 2236, -9884},
    {0, 1, 2, 0, -2120, 5751},
    {0, 2, 0, 0, -2069, 0},
    {2, -2, -1, 0, 2048, -4950},
    {2, 0, 1, -2, -1773, 4130},
    {2, 0, 0, 2, -1595, 0},
    {4, -1, -1, 0, 1215, -3958},
    {0, 0, 2, 2, -1110, 0},
    {3, 0, -1, 0, -892, 3258},
    {2, 1, 1, 0, -810, 2616},
    {4, -1, -2, 0, 759, -1897},
    {0, 2, -1, 0, -713, -2117},
    {2, 2, -1, 0, -700, 2354},
    {2, 1, -2, 0, 691, 0},
    {2, -1, 0, -2, 596, 0},
    {4, 0, 1, 0, 549, -1423},
    {0, 0, 4, 0, 537, -1117},
    {4, -1, 0, 0, 520, -1571},
    {1, 0, -2, 0, -487, -1739},
    {2, 1, 0, -2, -399, 0},
    {0, 0, 2, -2, -381, -4421},
    {1, 1, 1, 0, 351, 0},
    {3, 0, -2, 0, -340, 0},
    {4, 0, -3, 0, 330, 0},
    {2, -1, 2, 0, 327, 0},
    {0, 2, 1, 0, -323, 1165},
    {1, 1, -1, 0, 299, 0},
    {2, 0, 3, 0, 294, 0},
    {2, 0, -1, -2, 0, 8752}
};
constexpr LunarLatitudeTerm LunarLatitudeTerms[] = {
    {0, 0, 0, 1, 5128122},
    {0, 0, 1, 1, 280602},
    {0, 0, 1, -1, 277693},
    {2, 0, 0, -1, 173237},
    {2, 0, -1, 1, 55413},
    {2, 0, -1, -1, 46271},
    {2, 0, 0, 1, 32573},
    {0, 0, 2, 1, 17198},
    {2, 0, 1, -1, 9266},
    {0, 0, 2, -1, 8822},
    {2, -1, 0, -1, 8216},
    {2, 0, -2, -1, 4324},
    {2, 0, 1, 1, 4200},
    {2, 1, 0, -1, -3359},
    {2, -1, -1, 1, 2463},
    {2, -1, 0, 1, 2211},
    {2, -1, -1, -1, 2065},
    {0, 1, -1, -1, -1870},
    {4, 0, -1, -1, 1828},
    {0, 1, 0, 1, -1794},
    {0, 0, 0, 3, -1749},
    {0, 1, -1, 1, -1565},
    {1, 0, 0, 1, -1491},
    {0, 1, 1, 1, -1475},
    {0, 1, 1, -1, -1410},
    {0, 1, 0, -1, -1344},
    {1, 0, 0, -1, -1335},
    {0, 0, 3, 1, 1107},
    {4, 0, 0, -1, 1021},
    {4, 0, -1, 1, 833},
    {0, 0, 1, -3, 777},
    {4, 0, -2, 1, 671},
    {2, 0, 0, -3, 607},
    {2, 0, 2, -1, 596},
    {2, -1, 1, -1, 491},
    {2, 0, -2, 1, -451},
    {0, 0, 3, -1, 439},
    {2, 0, 2, 1, 422},
    {2, 0, -3, -1, 421},
    {2, 1, -1, 1, -366},
    {2, 1, 0, 1, -351},
    {4, 0, 0, 1, 331},
    {2, -1, 1, 1, 315},
    {2, -2, 0, -1, 302},
    {0, 0, 1, 3, -283},
    {2, 1, 1, -1, -229},
    {1, 1, 0, -1, 223},
    {1, 1, 0, 1, 223},
    {0, 1, -2, -1, -220},
    {2, 1, -1, -1, -220},
    {1, 0, 1, 1, -185},
    {2, -1, -2, -1, 181},
    {0, 1, 2, 1, -177},
    {4, 0, -2, -1, 176},
    {4, -1, -1, -1, 166},
    {1, 0, 1, -1, -164},
    {4, 0, 1, -1, 132},
    {1, 0, -1, -1, -119},
    {4, -1, 0, -1, 115},
    {2, -2, 0, 1, 107}
};

double Polynomial(double T, double A, double B, double C, double D, double E = 0.0) noexcept
{
    return ((((E*T + D)*T + C)*T + B)*T + A) * DegToRad;
}
double SolarEccentricityFactor(int SolarMultiplier, double E) noexcept
{
    return SolarMultiplier == 0 ? 1.0 : (std::abs(SolarMultiplier) == 1 ? E : E*E);
}
} // namespace

LunarGeocentricPosition MoonGeocentric(double JulianTt) noexcept
{
    if (!std::isfinite(JulianTt)) return {};
    const double T = (JulianTt - 2451545.0)/36525.0;
    if (!std::isfinite(T) || std::abs(T) > 5.0) return {}; // bounded approximation, years ~1500–2500
    const double L = Polynomial(T, 218.3164477, 481267.88123421, -0.0015786, 1.0/538841.0, -1.0/65194000.0);
    const double D = Polynomial(T, 297.8501921, 445267.1114034, -0.0018819, 1.0/545868.0, -1.0/113065000.0);
    const double M = Polynomial(T, 357.5291092, 35999.0502909, -0.0001535, 1.0/24490000.0);
    const double Prime = Polynomial(T, 134.9633964, 477198.8675055, 0.0087414, 1.0/69699.0, -1.0/14712000.0);
    const double F = Polynomial(T, 93.272095, 483202.0175233, -0.0036539, -1.0/3526000.0, 1.0/863310000.0);
    const double A1 = (119.75 + 131.849*T)*DegToRad, A2 = (53.09 + 479264.29*T)*DegToRad;
    const double A3 = (313.45 + 481266.484*T)*DegToRad;
    const double E = 1.0 - 0.002516*T - 0.0000074*T*T;
    double Longitude = 3958.0*std::sin(A1) + 1962.0*std::sin(L-F) + 318.0*std::sin(A2);
    double Distance = 0.0;
    double Latitude = -2235.0*std::sin(L) + 382.0*std::sin(A3) + 175.0*std::sin(A1-F) +
                      175.0*std::sin(A1+F) + 127.0*std::sin(L-Prime) - 115.0*std::sin(L+Prime);
    for (const auto& Term : LunarLongitudeTerms)
    {
        const double Angle = Term.D*D + Term.M*M + Term.Prime*Prime + Term.F*F;
        const double Factor = SolarEccentricityFactor(Term.M, E);
        Longitude += Term.Longitude * std::sin(Angle) * Factor;
        Distance += Term.Distance * std::cos(Angle) * Factor;
    }
    for (const auto& Term : LunarLatitudeTerms)
    {
        const double Angle = Term.D*D + Term.M*M + Term.Prime*Prime + Term.F*F;
        Latitude += Term.Latitude * std::sin(Angle) * SolarEccentricityFactor(Term.M, E);
    }
    double LongitudeRad = std::fmod(L + Longitude*1e-6*DegToRad, 2.0*Pi);
    if (LongitudeRad < 0.0) LongitudeRad += 2.0*Pi;
    return {LongitudeRad, Latitude*1e-6*DegToRad, 385000.56 + Distance*1e-3, F};
}

double MoonGeometricIllumination(const LunarGeocentricPosition& Moon, double JulianTt) noexcept
{
    if (!std::isfinite(JulianTt) || std::abs(JulianTt - 2451545.0) > 5.0*36525.0 ||
        !std::isfinite(Moon.LongitudeRad) || !std::isfinite(Moon.LatitudeRad) ||
        std::abs(Moon.LatitudeRad) > 0.5*Pi || !std::isfinite(Moon.DistanceKm) || Moon.DistanceKm <= 6379.0)
        return 0.0;
    const double N = JulianTt - 2451545.0;
    const double Anomaly = WrapDegrees(357.528 + 0.9856003*N)*DegToRad;
    const double SunLongitude = (WrapDegrees(280.460 + 0.9856474*N) +
                                 1.915*std::sin(Anomaly) + 0.020*std::sin(2.0*Anomaly))*DegToRad;
    const double SunDistanceKm = 149597870.7*(1.00014 - 0.01671*std::cos(Anomaly) - 0.00014*std::cos(2.0*Anomaly));
    const double CosElongation = std::clamp(std::cos(Moon.LatitudeRad)*std::cos(Moon.LongitudeRad-SunLongitude), -1.0, 1.0);
    const double SunMoonDistance = std::sqrt(SunDistanceKm*SunDistanceKm + Moon.DistanceKm*Moon.DistanceKm -
                                              2.0*SunDistanceKm*Moon.DistanceKm*CosElongation);
    if (!std::isfinite(SunMoonDistance) || SunMoonDistance <= 0.0) return 0.0;
    const double CosPhase = (Moon.DistanceKm - SunDistanceKm*CosElongation)/SunMoonDistance;
    return 0.5*(1.0 + std::clamp(CosPhase, -1.0, 1.0));
}

LunarTopocentricPosition MoonTopocentric(const LunarGeocentricPosition& Moon,
    double JulianUt1, double LatitudeDeg, double LongitudeDeg, double HeightMeters) noexcept
{
    if (!std::isfinite(Moon.LongitudeRad) || !std::isfinite(Moon.LatitudeRad) ||
        std::abs(Moon.LatitudeRad) > 0.5*Pi || !std::isfinite(Moon.DistanceKm) ||
        Moon.DistanceKm <= 6379.0 || !std::isfinite(JulianUt1) || !std::isfinite(LatitudeDeg) ||
        std::abs(LatitudeDeg) > 90.0 || !std::isfinite(LongitudeDeg) || !std::isfinite(HeightMeters)) return {};
    const double T = (JulianUt1 - 2451545.0)/36525.0;
    if (!std::isfinite(T) || std::abs(T) > 5.0) return {};
    const double Obliquity = (23.4392911111 - 0.0130041667*T - 0.0000001639*T*T +
                              0.0000005036*T*T*T)*DegToRad;
    const double Lambda = Moon.LongitudeRad, Beta = Moon.LatitudeRad;
    const double RightAscension = std::atan2(std::sin(Lambda)*std::cos(Obliquity) -
                                              std::tan(Beta)*std::sin(Obliquity), std::cos(Lambda));
    const double Declination = std::asin(std::clamp(std::sin(Beta)*std::cos(Obliquity) +
        std::cos(Beta)*std::sin(Obliquity)*std::sin(Lambda), -1.0, 1.0));
    const double N = JulianUt1 - 2451545.0;
    const double SiderealHours = std::fmod(18.697374558 + 24.06570982441908*N, 24.0);
    const double HourAngle = (SiderealHours*15.0 + LongitudeDeg)*DegToRad - RightAscension;
    const double Latitude = LatitudeDeg*DegToRad, CosDec = std::cos(Declination), SinDec = std::sin(Declination);
    // Geocentric unit vector in east/north/up coordinates.
    const double X = -CosDec*std::sin(HourAngle);
    const double Y = SinDec*std::cos(Latitude) - CosDec*std::cos(HourAngle)*std::sin(Latitude);
    const double Z = SinDec*std::sin(Latitude) + CosDec*std::cos(HourAngle)*std::cos(Latitude);
    // WGS84 observer position, resolved into the same local basis. Height is ellipsoidal.
    constexpr double EarthRadiusKm = 6378.137, Flattening = 1.0/298.257223563;
    constexpr double EccentricitySquared = Flattening*(2.0-Flattening);
    const double SinLat = std::sin(Latitude), CosLat = std::cos(Latitude);
    const double PrimeVertical = EarthRadiusKm/std::sqrt(1.0-EccentricitySquared*SinLat*SinLat);
    const double Equatorial = (PrimeVertical+HeightMeters*0.001)*CosLat;
    const double Polar = (PrimeVertical*(1.0-EccentricitySquared)+HeightMeters*0.001)*SinLat;
    const double ObserverNorth = -Equatorial*SinLat + Polar*CosLat;
    const double ObserverUp = Equatorial*CosLat + Polar*SinLat;
    const double TopoX = Moon.DistanceKm*X;
    const double TopoY = Moon.DistanceKm*Y - ObserverNorth;
    const double TopoZ = Moon.DistanceKm*Z - ObserverUp;
    const double DistanceKm = std::sqrt(TopoX*TopoX + TopoY*TopoY + TopoZ*TopoZ);
    if (!std::isfinite(DistanceKm) || DistanceKm <= 1737.4) return {};
    return {{TopoX/DistanceKm, TopoY/DistanceKm, TopoZ/DistanceKm}, DistanceKm,
            std::asin(1737.4/DistanceKm)};
}

LunarPhase MoonPhase(double Julian) noexcept
{
    double Age = std::fmod(Julian - NewMoonEpochJulian, SynodicMonthDays);
    if (Age < 0.0) Age += SynodicMonthDays;
    const double PhaseAngle = 2.0 * Pi * Age / SynodicMonthDays;
    LunarPhase Result;
    Result.AgeDays      = Age;
    Result.Illumination = 0.5 * (1.0 - std::cos(PhaseAngle));
    return Result;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LUNAR SURFACE
//------------------------------------------------------------------------------------------------------------------------

bool LunarAlbedoMap::LoadPpm(const char* Path)
{
    Values_.clear();
    Width_ = Height_ = 0;
    FILE* File = Path != nullptr ? std::fopen(Path, "rb") : nullptr;
    if (File == nullptr)
    {
        return false;
    }
    // 📝 P6 header: magic, width, height, maximum, one whitespace byte, then binary RGB triplets.
    //    Header tokens may be separated by whitespace and #-comments; fields are read one byte at a
    //    time so the binary payload offset is exact on every platform.
    auto NextToken = [&File](char* Buffer, size_t Capacity) -> bool {
        int Byte = std::fgetc(File);
        while (Byte != EOF)
        {
            if (Byte == '#')
            {
                while (Byte != EOF && Byte != '\n') Byte = std::fgetc(File);
            }
            else if (Byte == ' ' || Byte == '\t' || Byte == '\r' || Byte == '\n')
            {
                Byte = std::fgetc(File);
            }
            else
            {
                break;
            }
        }
        size_t Count = 0;
        while (Byte != EOF && Byte != ' ' && Byte != '\t' && Byte != '\r' && Byte != '\n' && Count + 1 < Capacity)
        {
            Buffer[Count++] = static_cast<char>(Byte);
            Byte = std::fgetc(File);
        }
        Buffer[Count] = '\0';
        return Count > 0;
    };
    char   Token[32] = {};
    double Maximum   = 0.0;
    bool   HeaderOk  = NextToken(Token, sizeof Token) && std::strcmp(Token, "P6") == 0;
    if (HeaderOk && NextToken(Token, sizeof Token)) Width_ = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (HeaderOk && NextToken(Token, sizeof Token)) Height_ = static_cast<uint32_t>(std::strtoul(Token, nullptr, 10));
    if (HeaderOk && NextToken(Token, sizeof Token)) Maximum = std::strtod(Token, nullptr);
    HeaderOk = HeaderOk && Width_ >= 4 && Height_ >= 2 && Maximum >= 1.0 && Maximum <= 255.0;
    // ⚠️ NextToken leaves the stream just past the maximum token's single terminating whitespace,
    //    which is exactly where the binary payload begins; do not consume another byte here.
    if (!HeaderOk)
    {
        std::fclose(File);
        Width_ = Height_ = 0;
        return false;
    }
    std::vector<unsigned char> Bytes(static_cast<size_t>(Width_) * Height_ * 3u);
    const size_t               Read = std::fread(Bytes.data(), 1, Bytes.size(), File);
    std::fclose(File);
    if (Read != Bytes.size())
    {
        Width_ = Height_ = 0;
        return false;
    }
    Values_.resize(static_cast<size_t>(Width_) * Height_);
    const double Scale = 1.0 / Maximum;
    const auto DecodeSrgb = [Scale](unsigned char Byte) noexcept
    {
        const double Encoded = Byte * Scale;
        return Encoded <= 0.04045 ? Encoded / 12.92 : std::pow((Encoded + 0.055) / 1.055, 2.4);
    };
    for (size_t I = 0; I < Values_.size(); ++I)
    {
        Values_[I] = {DecodeSrgb(Bytes[3 * I]), DecodeSrgb(Bytes[3 * I + 1]), DecodeSrgb(Bytes[3 * I + 2])};
    }
    return true;
}

SpectrumRgb LunarAlbedoMap::Sample(double LongitudeEastRad, double LatitudeRad) const noexcept
{
    if (Values_.empty())
    {
        return {0.55, 0.54, 0.52};   // [-] featureless fallback; callers gate on Loaded() for evidence
    }
    double U = (LongitudeEastRad + Pi) / (2.0 * Pi);
    U -= std::floor(U);   // longitude wraps at the antimeridian
    const double V = std::clamp((0.5 * Pi - LatitudeRad) / Pi, 0.0, 1.0);
    const double X = U * static_cast<double>(Width_) - 0.5;
    const double Y = V * static_cast<double>(Height_) - 0.5;
    const long   X0 = static_cast<long>(std::floor(X));
    const long   Y0 = static_cast<long>(std::floor(Y));
    const double Fx = X - static_cast<double>(X0);
    const double Fy = Y - static_cast<double>(Y0);
    const auto   At = [this](long Column, long Row) noexcept {
        const long WrappedColumn = ((Column % static_cast<long>(Width_)) + static_cast<long>(Width_)) %
                                   static_cast<long>(Width_);
        const long ClampedRow = Row < 0 ? 0 : (Row >= static_cast<long>(Height_) ? static_cast<long>(Height_) - 1 : Row);
        return Values_[static_cast<size_t>(ClampedRow) * Width_ + static_cast<size_t>(WrappedColumn)];
    };
    const SpectrumRgb Top{At(X0, Y0).R * (1.0 - Fx) + At(X0 + 1, Y0).R * Fx,
                          At(X0, Y0).G * (1.0 - Fx) + At(X0 + 1, Y0).G * Fx,
                          At(X0, Y0).B * (1.0 - Fx) + At(X0 + 1, Y0).B * Fx};
    const SpectrumRgb Bottom{At(X0, Y0 + 1).R * (1.0 - Fx) + At(X0 + 1, Y0 + 1).R * Fx,
                             At(X0, Y0 + 1).G * (1.0 - Fx) + At(X0 + 1, Y0 + 1).G * Fx,
                             At(X0, Y0 + 1).B * (1.0 - Fx) + At(X0 + 1, Y0 + 1).B * Fx};
    return {Top.R * (1.0 - Fy) + Bottom.R * Fy, Top.G * (1.0 - Fy) + Bottom.G * Fy,
            Top.B * (1.0 - Fy) + Bottom.B * Fy};
}

SpectrumRgb EvaluateLunarDiscRadiance(const LunarAlbedoMap& Map,
                                      double                DiscA,
                                      double                DiscB,
                                      double                SubObserverLongitudeRad,
                                      double                SubObserverLatitudeRad,
                                      double                CosIncidence,
                                      double                CosEmission,
                                      double                PhaseAngleRad,
                                      const SpectrumRgb&    SolarIrradiance,
                                      double                EarthshineStrength) noexcept
{
    const double RadiusSquared = DiscA * DiscA + DiscB * DiscB;
    if (RadiusSquared > 1.0)
    {
        return {};
    }
    // 📐 Orthographic inverse about the sub-observer point (λ₀, φ₀). With sin c = ρ (disc-normalised
    //    radius) and cos c = √(1−ρ²): sin φ = cos c·sin φ₀ + B·cos φ₀ and
    //    λ = λ₀ + atan2(A, cos c·cos φ₀ − B·sin φ₀). At λ₀ = φ₀ = 0 these reduce to the familiar
    //    φ = asin(B), λ = atan2(A, √(1−A²−B²)) with DiscA toward selenographic east.
    const double      TowardObserver = std::sqrt(1.0 - RadiusSquared);
    const double      SinLat0        = std::sin(SubObserverLatitudeRad);
    const double      CosLat0        = std::cos(SubObserverLatitudeRad);
    const double      LatitudeRad    = std::asin(std::clamp(TowardObserver * SinLat0 + DiscB * CosLat0, -1.0, 1.0));
    const double      LongitudeRad   = SubObserverLongitudeRad +
                                       std::atan2(DiscA, TowardObserver * CosLat0 - DiscB * SinLat0);
    const SpectrumRgb Albedo         = Map.Sample(LongitudeRad, LatitudeRad);
    const double      Mu0            = CosIncidence > 0.0 ? CosIncidence : 0.0;
    const double      Mu             = CosEmission > 1.0e-4 ? CosEmission : 1.0e-4;
    // 📐 Lommel–Seeliger: at full phase μ₀ = μ everywhere, so the factor is 1 across the whole disc —
    //    the flat, limb-darkening-free full Moon. The exponential is the shadow-hiding phase law.
    const double LommelSeeliger = Mu0 > 0.0 ? 2.0 * Mu0 / (Mu0 + Mu) : 0.0;
    const double PhaseDimming   = std::exp(-1.10 * std::abs(PhaseAngleRad));
    const double LitScale       = LommelSeeliger * PhaseDimming / Pi;
    return {Albedo.R * (SolarIrradiance.R * LitScale + EarthshineStrength * 0.55),
            Albedo.G * (SolarIrradiance.G * LitScale + EarthshineStrength * 0.65),
            Albedo.B * (SolarIrradiance.B * LitScale + EarthshineStrength * 1.00)};
}

//------------------------------------------------------------------------------------------------------------------------
//                                                            STARS
//------------------------------------------------------------------------------------------------------------------------

std::vector<StarRecord> DecodeHipparcosCatalogue(std::istream& Input, double JulianUt1,
                                                  double LatitudeDeg, double LongitudeDeg)
{
    std::vector<StarRecord> Stars;
    if (!std::isfinite(JulianUt1) || !std::isfinite(LatitudeDeg) || !std::isfinite(LongitudeDeg) ||
        std::abs(LatitudeDeg) > 90.0) return Stars;
    const double Years = (JulianUt1 - 2451545.0) / 365.25;
    const double Latitude = LatitudeDeg * DegToRad;
    const double GmstHours = std::fmod(18.697374558 + 24.06570982441908 * (JulianUt1 - 2451545.0), 24.0);
    const double Sidereal = (GmstHours * 15.0 + LongitudeDeg) * DegToRad;
    constexpr double ArcsecToRad = Pi / (180.0 * 3600.0);
    const double M = 46.124 * Years * ArcsecToRad, N = 20.043 * Years * ArcsecToRad;
    const double CosM = std::cos(M), SinM = std::sin(M), CosN = std::cos(N), SinN = std::sin(N);
    const double CosSidereal = std::cos(Sidereal), SinSidereal = std::sin(Sidereal);
    const double CosLatitude = std::cos(Latitude), SinLatitude = std::sin(Latitude);
    std::string Line;
    while (std::getline(Input, Line))
    {
        if (Line.empty() || Line[0] == '#' || Line.rfind("id,", 0) == 0) continue;
        std::stringstream Fields(Line);
        std::string Id, RaText, DecText, MagnitudeText, ColourText;
        if (!std::getline(Fields, Id, ',') || !std::getline(Fields, RaText, ',') ||
            !std::getline(Fields, DecText, ',') || !std::getline(Fields, MagnitudeText, ',') ||
            !std::getline(Fields, ColourText, ',')) continue;
        auto Number = [](const std::string& Text, double& Out) {
            char* End = nullptr;
            Out = std::strtod(Text.c_str(), &End);
            if (End == Text.c_str()) return false;
            while (std::isspace(static_cast<unsigned char>(*End))) ++End;
            return *End == '\0' && std::isfinite(Out);
        };
        double RaDeg = 0, DecDeg = 0, Magnitude = 0, Colour = 0;
        if (!Number(RaText, RaDeg) || !Number(DecText, DecDeg) || !Number(MagnitudeText, Magnitude) ||
            !Number(ColourText, Colour) || RaDeg < 0.0 || RaDeg >= 360.0 || std::abs(DecDeg) > 90.0 ||
            Magnitude < -35.0 || Magnitude > 40.0) continue;
        const double Ra = RaDeg * DegToRad, Dec = DecDeg * DegToRad;
        // Apply the first-order precession rates as 3D rotations, avoiding the RA tan(Dec) pole singularity.
        const double X = std::cos(Dec) * std::cos(Ra), Y = std::cos(Dec) * std::sin(Ra), Z = std::sin(Dec);
        const double RotatedX = CosN*X - SinN*Z;
        const double RotatedZ = SinN*X + CosN*Z;
        const double EquatorialX = CosM*RotatedX - SinM*Y;
        const double EquatorialY = SinM*RotatedX + CosM*Y;
        const double Meridian = CosSidereal*EquatorialX + SinSidereal*EquatorialY;
        const SpatialVector Direction{-SinSidereal*EquatorialX + CosSidereal*EquatorialY,
            RotatedZ*CosLatitude - Meridian*SinLatitude, RotatedZ*SinLatitude + Meridian*CosLatitude};
        Stars.push_back({Direction, Magnitude, Colour});
    }
    return Stars;
}

std::vector<StarRecord> GenerateStarCatalogue(uint32_t Count, uint64_t Seed)
{
    std::vector<StarRecord> Stars;
    Stars.reserve(Count);
    DeterministicRandom Random(Seed, 0x57A2ull);
    for (uint32_t I = 0; I < Count; ++I)
    {
        // Uniform sphere: z uniform in [-1, 1], azimuth uniform.
        const double Z     = 2.0 * Random.NextUnit() - 1.0;
        const double Phi   = 2.0 * Pi * Random.NextUnit();
        const double R     = std::sqrt(std::max(1.0 - Z * Z, 0.0));
        StarRecord   Star;
        Star.Direction = {R * std::cos(Phi), R * std::sin(Phi), Z};
        // Power-law magnitudes: many faint, few bright (mag in [-1, 6], mean ~3.8).
        Star.Magnitude   = -1.0 + 7.0 * std::pow(Random.NextUnit(), 0.45);
        Star.ColourIndex = 2.0 * Random.NextUnit() - 1.0;
        Stars.push_back(Star);
    }
    return Stars;
}

} // namespace Frontier
