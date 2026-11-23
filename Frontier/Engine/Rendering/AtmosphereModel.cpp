/*==========================================================================================================================================
                                                   ATMOSPHERE MODEL DEFINITION
==========================================================================================================================================*/

#include "AtmosphereModel.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }

SpectrumRgb ExpNeg(const SpectrumRgb& OpticalDepth) noexcept
{
    return {std::exp(-OpticalDepth.R), std::exp(-OpticalDepth.G), std::exp(-OpticalDepth.B)};
}

SpectrumRgb AddRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R + B.R, A.G + B.G, A.B + B.B}; }
SpectrumRgb MulRgb(SpectrumRgb A, double S) noexcept { return {A.R * S, A.G * S, A.B * S}; }
SpectrumRgb MulRgb(SpectrumRgb A, SpectrumRgb B) noexcept { return {A.R * B.R, A.G * B.G, A.B * B.B}; }

// Distance from Position along unit Direction to a sphere of Radius (planet-centred); <0 when no hit ahead.
double RaySphereAhead(SpatialVector Position, SpatialVector Direction, double Radius) noexcept
{
    const double B    = Dot(Position, Direction);
    const double C    = Dot(Position, Position) - Radius * Radius;
    const double Disc = B * B - C;
    if (Disc < 0.0)
    {
        return -1.0;
    }
    const double Root = std::sqrt(Disc);
    const double Near = -B - Root;
    return Near >= 0.0 ? Near : (-B + Root >= 0.0 ? -B + Root : -1.0);
}

// Optical depth accumulated along segment A->B (trapezoidal on Steps segments).
SpectrumRgb OpticalDepthBetween(const AtmosphereParameters& P, SpatialVector A, SpatialVector B, uint32_t Steps) noexcept
{
    const SpatialVector Delta{B.X - A.X, B.Y - A.Y, B.Z - A.Z};
    const double        LengthKm = Length(Delta);
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    double TauR = 0.0, TauM = 0.0, TauO = 0.0;
    const double Dt = LengthKm / static_cast<double>(Steps);
    double PrevR = 0.0, PrevM = 0.0, PrevO = 0.0;
    for (uint32_t I = 0; I <= Steps; ++I)
    {
        const double        F = static_cast<double>(I) / static_cast<double>(Steps);
        const SpatialVector At{A.X + Delta.X * F, A.Y + Delta.Y * F, A.Z + Delta.Z * F};
        const double        Altitude = std::max(Length(At) - P.GroundRadiusKm, 0.0);
        const double        DR = RayleighDensity(P, Altitude);
        const double        DM = MieDensity(P, Altitude);
        const double        DO = OzoneDensity(P, Altitude);
        if (I > 0)
        {
            TauR += 0.5 * (PrevR + DR) * Dt;
            TauM += 0.5 * (PrevM + DM) * Dt;
            TauO += 0.5 * (PrevO + DO) * Dt;
        }
        PrevR = DR; PrevM = DM; PrevO = DO;
    }
    const double MieExtinction = P.MieScattering + P.MieAbsorption;
    return {P.RayleighScattering.R * TauR + MieExtinction * TauM + P.OzoneAbsorption.R * TauO,
            P.RayleighScattering.G * TauR + MieExtinction * TauM + P.OzoneAbsorption.G * TauO,
            P.RayleighScattering.B * TauR + MieExtinction * TauM + P.OzoneAbsorption.B * TauO};
}

double AltitudeToV(const AtmosphereParameters& P, double RadiusKm) noexcept
{
    const double H = std::clamp((RadiusKm - P.GroundRadiusKm) / (P.TopRadiusKm - P.GroundRadiusKm), 0.0, 1.0);
    return std::sqrt(H);
}
double VToAltitudeRadius(const AtmosphereParameters& P, double V) noexcept
{
    return P.GroundRadiusKm + V * V * (P.TopRadiusKm - P.GroundRadiusKm);
}
double MuToU(double Mu) noexcept
{
    return 0.5 + 0.5 * std::copysign(std::pow(std::abs(std::clamp(Mu, -1.0, 1.0)), 0.35), Mu);
}
double UToMu(double U) noexcept
{
    const double S = 2.0 * std::clamp(U, 0.0, 1.0) - 1.0;
    return std::copysign(std::pow(std::abs(S), 1.0 / 0.35), S);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          MEDIUM
//------------------------------------------------------------------------------------------------------------------------

double RayleighDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / P.RayleighScaleHeightKm);
}
double MieDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::exp(-std::max(AltitudeKm, 0.0) / P.MieScaleHeightKm);
}
double OzoneDensity(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    return std::max(1.0 - std::abs(AltitudeKm - P.OzonePeakKm) / P.OzoneWidthKm, 0.0);
}

double RayleighPhase(double CosTheta) noexcept
{
    return 3.0 / (16.0 * Pi) * (1.0 + CosTheta * CosTheta);
}
double HenyeyGreensteinPhase(double CosTheta, double G) noexcept
{
    const double Denom = 1.0 + G * G - 2.0 * G * CosTheta;
    return (1.0 - G * G) / (4.0 * Pi * Denom * std::sqrt(std::max(Denom, 1e-12)));
}

// Cornette–Shanks aerosol phase: the demonstration's Rayleigh-shaped HG correction.
double CornetteShanksPhase(double CosTheta, double G) noexcept
{
    const double Cosine = std::clamp(CosTheta, -1.0, 1.0);
    const double Asymmetry = std::clamp(G, -0.999, 0.999);
    return HenyeyGreensteinPhase(Cosine, Asymmetry) *
           1.5 * (1.0 + Cosine * Cosine) / (2.0 + Asymmetry * Asymmetry);
}

SpectrumRgb TransmittanceBetween(const AtmosphereParameters& P, SpatialVector A, SpatialVector B, uint32_t Steps) noexcept
{
    return ExpNeg(OpticalDepthBetween(P, A, B, Steps));
}

SpectrumRgb TransmittanceToTop(const AtmosphereParameters& P, double RadiusKm, double CosZenith, uint32_t Steps) noexcept
{
    // Canonical frame: observer on +Y at RadiusKm, view in the YZ plane by cos-zenith.
    const double        SinZenith = std::sqrt(std::max(1.0 - CosZenith * CosZenith, 0.0));
    const SpatialVector Position{0.0, RadiusKm, 0.0};
    const SpatialVector Direction{0.0, CosZenith, SinZenith};

    const double ToGround = RaySphereAhead(Position, Direction, P.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(Position, Direction, P.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))   // > eps: tangent self-hit is no hit
    {
        LengthKm = ToGround;   // looking down: integrate to the surface
    }
    if (LengthKm < 0.0)
    {
        return {1.0, 1.0, 1.0};
    }
    const SpatialVector End = Add(Position, Mul(Direction, LengthKm));
    return ExpNeg(OpticalDepthBetween(P, Position, End, Steps));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRANSMITTANCE LUT
//------------------------------------------------------------------------------------------------------------------------

bool TransmittanceLut::Bake(const AtmosphereParameters& P, uint32_t WidthMu, uint32_t HeightAltitude,
                            uint32_t IntegrationSteps)
{
    Values_.clear();
    if (WidthMu < 2 || HeightAltitude < 2)
    {
        WidthMu_ = HeightAltitude_ = 0;
        return false;
    }
    WidthMu_        = WidthMu;
    HeightAltitude_ = HeightAltitude;
    Values_.resize(static_cast<size_t>(WidthMu) * HeightAltitude);
    for (uint32_t Row = 0; Row < HeightAltitude; ++Row)
    {
        const double V        = static_cast<double>(Row) / static_cast<double>(HeightAltitude - 1);
        const double RadiusKm = VToAltitudeRadius(P, V);
        for (uint32_t Col = 0; Col < WidthMu; ++Col)
        {
            const double U  = static_cast<double>(Col) / static_cast<double>(WidthMu - 1);
            const double Mu = UToMu(U);
            Values_[static_cast<size_t>(Row) * WidthMu + Col] = TransmittanceToTop(P, RadiusKm, Mu, IntegrationSteps);
        }
    }
    return true;
}

SpectrumRgb TransmittanceLut::Sample(const AtmosphereParameters& P, double RadiusKm, double CosZenith) const noexcept
{
    if (Values_.empty())
    {
        return TransmittanceToTop(P, RadiusKm, CosZenith);
    }
    const double U  = MuToU(CosZenith) * static_cast<double>(WidthMu_ - 1);
    const double V  = AltitudeToV(P, RadiusKm) * static_cast<double>(HeightAltitude_ - 1);
    const uint32_t X0 = std::min(static_cast<uint32_t>(U), WidthMu_ - 2);
    const uint32_t Y0 = std::min(static_cast<uint32_t>(V), HeightAltitude_ - 2);
    const double FX = U - X0, FY = V - Y0;
    const auto At = [&](uint32_t X, uint32_t Y) { return Values_[static_cast<size_t>(Y) * WidthMu_ + X]; };
    const SpectrumRgb C00 = AddRgb(MulRgb(At(X0, Y0), 1 - FX), MulRgb(At(X0 + 1, Y0), FX));
    const SpectrumRgb C10 = AddRgb(MulRgb(At(X0, Y0 + 1), 1 - FX), MulRgb(At(X0 + 1, Y0 + 1), FX));
    return AddRgb(MulRgb(C00, 1 - FY), MulRgb(C10, FY));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCATTERING
//------------------------------------------------------------------------------------------------------------------------

SpectrumRgb SingleScattering(const AtmosphereParameters& P, SpatialVector PositionKm, SpatialVector Direction,
                             SpatialVector SunDirection, const SpectrumRgb& SunIrradiance,
                             const TransmittanceLut* Lut, uint32_t Steps) noexcept
{
    const double ToGround = RaySphereAhead(PositionKm, Direction, P.GroundRadiusKm);
    const double ToTop    = RaySphereAhead(PositionKm, Direction, P.TopRadiusKm);
    double       LengthKm = ToTop;
    if (ToGround > 1e-4 && (LengthKm < 0.0 || ToGround < LengthKm))   // > eps: tangent self-hit is no hit
    {
        LengthKm = ToGround;
    }
    if (LengthKm <= 0.0 || Steps == 0)
    {
        return {};
    }
    const double CosTheta = Dot(Direction, SunDirection);
    const double PhaseR   = RayleighPhase(CosTheta);
    const double PhaseM   = CornetteShanksPhase(CosTheta, P.MiePhaseG);
    const double Dt       = LengthKm / static_cast<double>(Steps);

    SpectrumRgb Radiance{};
    // March with running transmittance from the eye (segment-by-segment, trapezoidal densities).
    SpectrumRgb ViewTransmittance{1.0, 1.0, 1.0};
    for (uint32_t I = 0; I < Steps; ++I)
    {
        const double        T0 = Dt * static_cast<double>(I);
        const double        T1 = Dt * static_cast<double>(I + 1);
        const SpatialVector At0 = Add(PositionKm, Mul(Direction, T0));
        const SpatialVector At1 = Add(PositionKm, Mul(Direction, T1));
        const SpectrumRgb   SegmentDepth = OpticalDepthBetween(P, At0, At1, 1);
        const SpectrumRgb   SegmentTransmittance = ExpNeg(SegmentDepth);

        // Sample at segment midpoint: sun transmittance from there to the top along the sun dir.
        const SpatialVector Mid      = Add(PositionKm, Mul(Direction, 0.5 * (T0 + T1)));
        const double        MidRadius = Length(Mid);
        const double        Altitude  = std::max(MidRadius - P.GroundRadiusKm, 0.0);
        const double        CosSunZenith = Dot(Mul(Mid, 1.0 / std::max(MidRadius, 1e-9)), SunDirection);
        const double        SunGroundHit = RaySphereAhead(Mid, SunDirection, P.GroundRadiusKm);
        const bool          SunVisible = SunGroundHit < 0.0 || SunGroundHit <= 1e-4;
        if (SunVisible)
        {
            const SpectrumRgb SunT = Lut != nullptr ? Lut->Sample(P, MidRadius, CosSunZenith)
                                                    : TransmittanceToTop(P, MidRadius, CosSunZenith, 24);
            const double DnsR = RayleighDensity(P, Altitude);
            const double DnsM = MieDensity(P, Altitude);
            // Integral of (1 - exp(-tau_seg * s)) weighting: use the segment-mean transmittance
            // closed form per channel via the total-transmittance ratio (exact for the segment's
            // own extinction when scattering coefficient were constant within it; evaluated per
            // component with its share of the segment optical depth).
            const auto SegmentMean = [](double TauComponent, double TauTotal, double SegT) {
                if (TauTotal <= 1e-12)
                {
                    return 0.0;
                }
                return (TauComponent / TauTotal) * (1.0 - SegT);
            };
            const double SegR_R = P.RayleighScattering.R * DnsR * Dt;
            const double SegR_G = P.RayleighScattering.G * DnsR * Dt;
            const double SegR_B = P.RayleighScattering.B * DnsR * Dt;
            const double SegM   = P.MieScattering * DnsM * Dt;
            const SpectrumRgb Scatter{
                PhaseR * SegmentMean(SegR_R, SegmentDepth.R, SegmentTransmittance.R) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.R, SegmentTransmittance.R),
                PhaseR * SegmentMean(SegR_G, SegmentDepth.G, SegmentTransmittance.G) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.G, SegmentTransmittance.G),
                PhaseR * SegmentMean(SegR_B, SegmentDepth.B, SegmentTransmittance.B) +
                    PhaseM * SegmentMean(SegM, SegmentDepth.B, SegmentTransmittance.B)};
            Radiance = AddRgb(Radiance, MulRgb(MulRgb(MulRgb(ViewTransmittance, SunT), SunIrradiance), Scatter));
        }
        ViewTransmittance = MulRgb(ViewTransmittance, SegmentTransmittance);
    }
    return Radiance;
}

SpectrumRgb MultipleScatteringApprox(const AtmosphereParameters& P, SpatialVector PositionKm,
                                     SpatialVector Direction, SpatialVector SunDirection,
                                     const SpectrumRgb& SingleScattered, const SpectrumRgb& SunIrradiance) noexcept
{
    // Directional average of sun transmittance at the observer (fixed 8x4 sphere lattice),
    // turned into an isotropic second-bounce factor ~ albedo-weighted throughput of the air mass.
    (void)Direction;
    const double RadiusKm = Length(PositionKm);
    const SpatialVector Up = Mul(PositionKm, 1.0 / std::max(RadiusKm, 1e-9));
    double AverageR = 0.0, AverageG = 0.0, AverageB = 0.0;
    uint32_t Count = 0;
    for (uint32_t Ring = 0; Ring < 4; ++Ring)
    {
        const double CosZenith = -0.75 + 0.5 * static_cast<double>(Ring);
        const SpectrumRgb T = TransmittanceToTop(P, RadiusKm, CosZenith, 24);
        AverageR += T.R; AverageG += T.G; AverageB += T.B;
        ++Count;
    }
    const SpectrumRgb Average{AverageR / Count, AverageG / Count, AverageB / Count};
    // 📝 Sun gating: the return is gathered sunlight, so it carries the sun's own transmittance at
    //    the observer and vanishes when the ground sphere occults the sun. (The previous form was
    //    sun-independent and returned its full factor at midnight.)
    const double      CosSunZenith = Dot(Up, SunDirection);
    const double      SunGroundHit = RaySphereAhead(PositionKm, SunDirection, P.GroundRadiusKm);
    const bool        SunUp        = SunGroundHit < 0.0 || SunGroundHit <= 1e-4;
    const SpectrumRgb SunT         = SunUp ? TransmittanceToTop(P, RadiusKm, CosSunZenith, 24) : SpectrumRgb{};
    // Spherical-albedo return ψ/(1−ψ) with ψ the direction-averaged atmospheric reflectance. The
    // 0.35 calibration is the clear-air spherical albedo scale (ψ ≲ 0.3): it keeps the boost in the
    // measured 10–30% range over single scattering instead of the previous unbounded ~190%, which
    // also desaturated the zenith. GroundAlbedo enters as the surface-return term.
    const double      Altitude  = std::max(RadiusKm - P.GroundRadiusKm, 0.0);
    const double      AirAlbedo = std::clamp(0.6 * RayleighDensity(P, Altitude) + 0.1, 0.0, 0.9);
    const auto        ChannelFactor = [](double AverageT, double SunTransmittance, double ReturnAlbedo,
                                          double GroundAlbedo) {
        const double Psi = std::clamp(AverageT * ReturnAlbedo * 0.35 + GroundAlbedo * 0.06, 0.0, 0.55);
        return std::clamp(Psi / (1.0 - Psi), 0.0, 1.2) * SunTransmittance;
    };
    const SpectrumRgb Factor{ChannelFactor(Average.R, SunT.R, AirAlbedo, P.GroundAlbedo),
                             ChannelFactor(Average.G, SunT.G, AirAlbedo, P.GroundAlbedo),
                             ChannelFactor(Average.B, SunT.B, AirAlbedo, P.GroundAlbedo)};
    (void)SunIrradiance;
    return MulRgb(SingleScattered, Factor);
}

} // namespace Frontier
