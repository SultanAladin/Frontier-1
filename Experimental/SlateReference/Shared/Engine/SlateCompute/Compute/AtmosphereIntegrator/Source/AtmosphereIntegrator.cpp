//============================================================================================================================================
//                                                       ATMOSPHEREINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 The three surfaces in construction order, the spectral coefficients behind them, and the convolution derived on rebuild.

#include "SlateCompute/Compute/AtmosphereIntegrator/Api/AtmosphereIntegrator.h"

#include "Shared/AtmosphereProjection.slang.h"
#include "Shared/SampleProjection.slang.h"

#include <cmath>
#include <cstring>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   HALF PRECISION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 🚧 File-local because `28` is the only component that stores half precision today. `06` will need the same
//    pair the moment it claims an RGBA16F target it writes from the host, and this moves to `SlateMath` then —
//    two units reading one conversion is `00` §2's rule for constants applied to arithmetic.
std::uint16_t EncodeHalf(float Magnitude)
{
    std::uint32_t Bits = 0u;
    std::memcpy(&Bits, &Magnitude, sizeof(Bits));

    const std::uint32_t Signum   = (Bits >> 16) & 0x8000u;
    std::int32_t        Exponent = static_cast<std::int32_t>((Bits >> 23) & 0xFFu) - 112;   // 127 − 15
    std::uint32_t       Mantissa = Bits & 0x7FFFFFu;

    // 📝 Subnormals flush to zero rather than encoding. A radiance below the half-precision subnormal floor is
    //    below anything `66`'s tone projection can distinguish from black, and encoding it costs a branch at
    //    every texel to preserve a distinction no display carries.
    if (Exponent <= 0)
        return static_cast<std::uint16_t>(Signum);

    if (Exponent >= 31)
        return static_cast<std::uint16_t>(Signum | 0x7BFFu);   // the largest finite half, never an infinity

    // 📐 Round to nearest, ties to even. Truncating instead biases every encoded magnitude downward, and a
    //    transmittance surface biased downward is an atmosphere that is uniformly and inexplicably too dark.
    const std::uint32_t Rounded = Mantissa + 0x0FFFu + ((Mantissa >> 13) & 1u);

    if ((Rounded & 0x800000u) != 0u)
    {
        ++Exponent;

        if (Exponent >= 31)
            return static_cast<std::uint16_t>(Signum | 0x7BFFu);

        return static_cast<std::uint16_t>(Signum | (static_cast<std::uint32_t>(Exponent) << 10));
    }

    return static_cast<std::uint16_t>(Signum | (static_cast<std::uint32_t>(Exponent) << 10) | (Rounded >> 13));
}

float DecodeHalf(std::uint16_t Encoded)
{
    const std::uint32_t Signum   = static_cast<std::uint32_t>(Encoded & 0x8000u) << 16;
    const std::uint32_t Exponent = (Encoded >> 10) & 0x1Fu;
    const std::uint32_t Mantissa = Encoded & 0x3FFu;

    if (Exponent == 0u)
    {
        // 📐 A subnormal half is Mantissa × 2⁻²⁴. Encode never produces one, but a surface uploaded and read back
        //    by a device might, so decoding accepts them rather than reporting them as zero.
        const float Subnormal = static_cast<float>(Mantissa) * 5.960464477539063e-8f;

        return (Signum != 0u) ? -Subnormal : Subnormal;
    }

    std::uint32_t Bits = 0u;

    if (Exponent == 31u)
        Bits = Signum | 0x7F800000u | (Mantissa << 13);
    else
        Bits = Signum | ((Exponent + 112u) << 23) | (Mantissa << 13);

    float Magnitude = 0.0f;
    std::memcpy(&Magnitude, &Bits, sizeof(Magnitude));

    return Magnitude;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  GEOMETRY HELPERS
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 Every routine that stood here is now `Shared/AtmosphereProjection.slang.h`'s, unchanged. They had not
//    diverged from the shared forms yet — `02` §7's exact condition — but `ParityRunner` was comparing the
//    shared mappings while this file marched through its own copies, so the parity gate was passing on code
//    the integrator did not run.

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  MEDIUM VALIDATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> MediumSpecification::Validate() const
{
    if (PlanetRadius <= 0.0 || AtmosphereThickness <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the planet or its atmosphere has no extent" });

    if (RayleighScaleHeight <= 0.0 || MieScaleHeight <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a scale height of zero has no profile" });

    if (OzoneHalfWidth <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the ozone tent has no width" });

    if (MieAsymmetry <= -1.0 || MieAsymmetry >= 1.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the asymmetry collapses the phase lobe" });

    if (MieExtinction < MieScattering)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "a component cannot scatter more than it extinguishes" });
    }

    if (MolecularConcentration <= 0.0 || RefractiveIndex <= 1.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the medium is not a refracting gas" });

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE SPECTRAL COEFFICIENTS
//------------------------------------------------------------------------------------------------------------------------

Deliver<MediumCoefficient> Resolve(const MediumSpecification&      Declared,
                                   const ColourSpaceSpecification& Working,
                                   const QuadratureRule&           Rule)
{
    const Deliver<bool> Validated = Declared.Validate();

    if (!Validated.Resolved)
        return Deliver<MediumCoefficient>::Refuse(Validated.Error);

    if (!Rule.Derived())
        return Deliver<MediumCoefficient>::Refuse({ RefusalReason::ContentUnsupported, "the rule is not derived" });

    // 📐 β(λ) = 8π³(n²−1)² / (3Nλ⁴) × (6+3p)/(6−7p). The King correction factor on the right accounts for the
    //    molecules not being spherically symmetric; omitting it understates the coefficient by about six percent,
    //    which is a sky that is very slightly too dark and entirely the right colour — so nobody finds it.
    const double IndexTerm  = Declared.RefractiveIndex * Declared.RefractiveIndex - 1.0;
    const double KingFactor = (6.0 + 3.0 * Declared.Depolarisation) / (6.0 - 7.0 * Declared.Depolarisation);
    const double Numerator  = 8.0 * Pi * Pi * Pi * IndexTerm * IndexTerm * KingFactor;

    const Deliver<TristimulusCoordinate> Rayleigh = ProjectSpectrum(
        Rule,
        [&](double Wavelength)
        {
            const double Metres  = Wavelength * 1.0e-9;
            const double Quartic = Metres * Metres * Metres * Metres;

            return Numerator / (3.0 * Declared.MolecularConcentration * Quartic);
        });

    if (!Rayleigh.Resolved)
        return Deliver<MediumCoefficient>::Refuse(Rayleigh.Error);

    // 📐 The Chappuis band, as a two-lobe fit peaking near six hundred nanometres. 🔴 This is a fit to measured
    //    absorption and not a derivation: ozone's cross-section has no closed form, and the note on `Resolve`
    //    exists so that nobody later reads it as having the same standing as the Rayleigh expression above.
    const Deliver<TristimulusCoordinate> Ozone = ProjectSpectrum(
        Rule,
        [&](double Wavelength)
        {
            const double Principal = (Wavelength - 602.0) / 78.0;
            const double Secondary = (Wavelength - 505.0) / 52.0;

            const double Shape = std::exp(-0.5 * Principal * Principal)
                               + 0.42 * std::exp(-0.5 * Secondary * Secondary);

            return Declared.OzonePeakAbsorption * Shape;
        });

    if (!Ozone.Resolved)
        return Deliver<MediumCoefficient>::Refuse(Ozone.Error);

    TristimulusCoordinate RayleighTristimulus = Rayleigh.Resolve();

    // 📝 🔴 An extinction coefficient is a per-wavelength rate (1/m) and not light emission (radiance). CIE x̄(λ)
    //    carries a secondary lobe at 442 nm to represent violet light in positive XYZ coordinates. For light, that
    //    lobe models human cone crosstalk; for an extinction coefficient, integrating 1/λ⁴ Rayleigh scattering
    //    against it inflates X with short-wavelength scattering, making Red extinction exceed Green after
    //    XYZ-to-RGB projection. Subtracting that secondary blue lobe from X recovers the primary red response
    //    and preserves physical monotonicity (Red < Green < Blue).
    const double SecondaryBlueIntegral = Rule.IntegrateInterval(
        SpectralLowerWavelength,
        SpectralUpperWavelength,
        [&](double Wavelength)
        {
            const double Metres    = Wavelength * 1.0e-9;
            const double Quartic   = Metres * Metres * Metres * Metres;
            const double Rate      = Numerator / (3.0 * Declared.MolecularConcentration * Quartic);
            const double Departure = (Wavelength - 442.0) / (Wavelength < 442.0 ? 16.0 : 26.7);
            const double LobeValue = 0.362 * std::exp(-0.5 * Departure * Departure);

            return Rate * LobeValue;
        });

    const Deliver<double> Normalisation = LuminanceNormalisation(Rule);

    if (Normalisation.Resolved && Normalisation.Resolve() > 0.0)
        RayleighTristimulus.MagnitudeX -= SecondaryBlueIntegral / Normalisation.Resolve();

    const Deliver<ColourSpecification> RayleighWorking =
        ProjectTristimulus(RayleighTristimulus.MagnitudeX,
                           RayleighTristimulus.MagnitudeY,
                           RayleighTristimulus.MagnitudeZ,
                           Working);

    TristimulusCoordinate OzoneTristimulus = Ozone.Resolve();

    const double SecondaryBlueOzone = Rule.IntegrateInterval(
        SpectralLowerWavelength,
        SpectralUpperWavelength,
        [&](double Wavelength)
        {
            const double Principal = (Wavelength - 602.0) / 78.0;
            const double Secondary = (Wavelength - 505.0) / 52.0;
            const double Shape     = std::exp(-0.5 * Principal * Principal)
                                   + 0.42 * std::exp(-0.5 * Secondary * Secondary);
            const double Rate      = Declared.OzonePeakAbsorption * Shape;
            const double Departure = (Wavelength - 442.0) / (Wavelength < 442.0 ? 16.0 : 26.7);
            const double LobeValue = 0.362 * std::exp(-0.5 * Departure * Departure);

            return Rate * LobeValue;
        });

    const double AreaX = Rule.IntegrateInterval(
        SpectralLowerWavelength, SpectralUpperWavelength,
        [](double Wavelength)
        {
            const TristimulusCoordinate Resp = ProjectWavelength(Wavelength);
            const double Departure = (Wavelength - 442.0) / (Wavelength < 442.0 ? 16.0 : 26.7);

            return Resp.MagnitudeX - 0.362 * std::exp(-0.5 * Departure * Departure);
        });

    const double AreaY = Rule.IntegrateInterval(
        SpectralLowerWavelength, SpectralUpperWavelength,
        [](double Wavelength) { return ProjectWavelength(Wavelength).MagnitudeY; });

    const double AreaZ = Rule.IntegrateInterval(
        SpectralLowerWavelength, SpectralUpperWavelength,
        [](double Wavelength) { return ProjectWavelength(Wavelength).MagnitudeZ; });

    if (Normalisation.Resolved && Normalisation.Resolve() > 0.0 && AreaX > 0.0 && AreaY > 0.0 && AreaZ > 0.0)
    {
        const double LuminanceNorm = Normalisation.Resolve();
        const double UnscaledX     = (OzoneTristimulus.MagnitudeX * LuminanceNorm - SecondaryBlueOzone) / AreaX;
        const double UnscaledY     = (OzoneTristimulus.MagnitudeY * LuminanceNorm) / AreaY;
        const double UnscaledZ     = (OzoneTristimulus.MagnitudeZ * LuminanceNorm) / AreaZ;

        OzoneTristimulus.MagnitudeX = UnscaledX * (AreaX / AreaY);
        OzoneTristimulus.MagnitudeY = UnscaledY;
        OzoneTristimulus.MagnitudeZ = UnscaledZ * (AreaZ / AreaY);
    }

    const Deliver<ColourSpecification> OzoneWorking =
        ProjectTristimulus(OzoneTristimulus.MagnitudeX,
                           OzoneTristimulus.MagnitudeY,
                           OzoneTristimulus.MagnitudeZ,
                           Working);

    if (!RayleighWorking.Resolved)
        return Deliver<MediumCoefficient>::Refuse(RayleighWorking.Error);

    if (!OzoneWorking.Resolved)
        return Deliver<MediumCoefficient>::Refuse(OzoneWorking.Error);

    MediumCoefficient Resolved;

    // 📝 An extinction coefficient is never negative. A wide working space can carry a negative coordinate
    //    legitimately — `36` §7 transfers negatives rather than clamping them — but a negative extinction is a
    //    medium that amplifies light along a path, and no tone projection recovers from it.
    Resolved.RayleighScattering[0] = RayleighWorking.Resolve().RedCoordinate   > 0.0
                                   ? RayleighWorking.Resolve().RedCoordinate   : 0.0;
    Resolved.RayleighScattering[1] = RayleighWorking.Resolve().GreenCoordinate > 0.0
                                   ? RayleighWorking.Resolve().GreenCoordinate : 0.0;
    Resolved.RayleighScattering[2] = RayleighWorking.Resolve().BlueCoordinate  > 0.0
                                   ? RayleighWorking.Resolve().BlueCoordinate  : 0.0;

    Resolved.OzoneAbsorption[0] = OzoneWorking.Resolve().RedCoordinate   > 0.0
                                ? OzoneWorking.Resolve().RedCoordinate   : 0.0;
    Resolved.OzoneAbsorption[1] = OzoneWorking.Resolve().GreenCoordinate > 0.0
                                ? OzoneWorking.Resolve().GreenCoordinate : 0.0;
    Resolved.OzoneAbsorption[2] = OzoneWorking.Resolve().BlueCoordinate  > 0.0
                                ? OzoneWorking.Resolve().BlueCoordinate  : 0.0;

    Resolved.MieScattering       = Declared.MieScattering;
    Resolved.MieExtinction       = Declared.MieExtinction;
    Resolved.CoefficientResolved = true;

    return Deliver<MediumCoefficient>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ONE RESIDENT SURFACE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ResidentSurface::ConstructResidentSurface(std::uint32_t Width_,
                                         std::uint32_t Height_,
                                         bool          WrapXDeclared)
{
    if (Width_ == 0u || Height_ == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a surface of no extent" });

    SpannedX  = Width_;
    SpannedY = Height_;
    WrapX     = WrapXDeclared;

    Encoded.assign(static_cast<std::size_t>(Width_) * Height_ * AtmosphereComponentCount, 0u);

    return Deliver<bool>::Result(true);
}

void ResidentSurface::Write(std::uint32_t X, std::uint32_t Y, double Red, double Green, double Blue)
{
    if (X >= SpannedX || Y >= SpannedY)
        return;

    const std::size_t Writing = (static_cast<std::size_t>(Y) * SpannedX + X)
                              * AtmosphereComponentCount;

    Encoded[Writing]      = EncodeHalf(static_cast<float>(Red));
    Encoded[Writing + 1u] = EncodeHalf(static_cast<float>(Green));
    Encoded[Writing + 2u] = EncodeHalf(static_cast<float>(Blue));
    Encoded[Writing + 3u] = EncodeHalf(1.0f);
}

void ResidentSurface::Sample(double CoordinateX, double CoordinateY,
                             double& Red, double& Green, double& Blue) const
{
    Red   = 0.0;
    Green = 0.0;
    Blue  = 0.0;

    if (Encoded.empty())
        return;

    const double Width  = static_cast<double>(SpannedX);
    const double Height = static_cast<double>(SpannedY);

    double TexelY = BoundedMagnitude(CoordinateY, 0.0, 1.0) * Height - 0.5;
    TexelY        = BoundedMagnitude(TexelY, 0.0, Height - 1.0);

    // 📐 🔴 The periodic axis wraps its **filter**, not merely its coordinate. Wrapping the coordinate into the
    //    unit interval and then clamping the texel blends texel 191 with itself at the seam instead of with
    //    texel 0, which is a one-texel discontinuity running down the sky at the azimuth origin.
    double TexelX = CoordinateX * Width - 0.5;

    if (WrapX)
    {
        while (TexelX <  0.0)       TexelX += Width;
        while (TexelX >= Width) TexelX -= Width;
    }
    else
    {
        TexelX = BoundedMagnitude(TexelX, 0.0, Width - 1.0);
    }

    const std::uint32_t MinimumX  = static_cast<std::uint32_t>(TexelX);
    const std::uint32_t MinimumY = static_cast<std::uint32_t>(TexelY);

    const std::uint32_t NextX  = WrapX
                                   ? (MinimumX + 1u) % SpannedX
                                   : (MinimumX + 1u < SpannedX ? MinimumX + 1u : MinimumX);
    const std::uint32_t NextY = MinimumY + 1u < SpannedY ? MinimumY + 1u : MinimumY;

    const double FractionX  = TexelX  - static_cast<double>(MinimumX);
    const double FractionY = TexelY - static_cast<double>(MinimumY);

    const std::size_t LowerLeft  = (static_cast<std::size_t>(MinimumY) * SpannedX + MinimumX)
                                 * AtmosphereComponentCount;
    const std::size_t LowerRight = (static_cast<std::size_t>(MinimumY) * SpannedX + NextX)
                                 * AtmosphereComponentCount;
    const std::size_t UpperLeft  = (static_cast<std::size_t>(NextY)  * SpannedX + MinimumX)
                                 * AtmosphereComponentCount;
    const std::size_t UpperRight = (static_cast<std::size_t>(NextY)  * SpannedX + NextX)
                                 * AtmosphereComponentCount;

    double* Resolved[3] = { &Red, &Green, &Blue };

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        const double Lower = static_cast<double>(DecodeHalf(Encoded[LowerLeft  + Component])) * (1.0 - FractionX)
                           + static_cast<double>(DecodeHalf(Encoded[LowerRight + Component])) * FractionX;

        const double Upper = static_cast<double>(DecodeHalf(Encoded[UpperLeft  + Component])) * (1.0 - FractionX)
                           + static_cast<double>(DecodeHalf(Encoded[UpperRight + Component])) * FractionX;

        *Resolved[Component] = Lower * (1.0 - FractionY) + Upper * FractionY;
    }
}

const std::vector<std::uint16_t>& ResidentSurface::Texels() const { return Encoded; }

std::uint64_t ResidentSurface::ResidentBytes() const
{
    return static_cast<std::uint64_t>(Encoded.size()) * AtmosphereComponentBytes;
}

std::uint32_t ResidentSurface::Width() const        { return SpannedX;  }
std::uint32_t ResidentSurface::Height() const       { return SpannedY; }
bool          ResidentSurface::SurfaceConstructed() const { return !Encoded.empty(); }

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE AMBIENT TERM
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 The nine real second-order harmonic basis magnitudes at one direction, in the conventional ordering.
void HarmonicBasis(double DirectionX, double DirectionY, double DirectionZ, double Basis[9])
{
    Basis[0] = 0.282095;
    Basis[1] = 0.488603 * DirectionY;
    Basis[2] = 0.488603 * DirectionZ;
    Basis[3] = 0.488603 * DirectionX;
    Basis[4] = 1.092548 * DirectionX * DirectionY;
    Basis[5] = 1.092548 * DirectionY * DirectionZ;
    Basis[6] = 0.315392 * (3.0 * DirectionZ * DirectionZ - 1.0);
    Basis[7] = 1.092548 * DirectionX * DirectionZ;
    Basis[8] = 0.546274 * (DirectionX * DirectionX - DirectionY * DirectionY);
}

// 📐 The cosine lobe's own harmonic expansion, band by band: π at the zeroth, 2π/3 at the first, π/4 at the
//    second. The convolution is a multiply per band, which is the whole reason a harmonic basis is used here
//    rather than a directional surface.
constexpr double CosineLobe[3] = { 3.141592653589793, 2.0943951023931953, 0.7853981633974483 };

}   // namespace

void IrradianceProjection::Evaluate(double DirectionX, double DirectionY, double DirectionZ,
                                    double& Red, double& Green, double& Blue) const
{
    double Basis[9] = {};
    HarmonicBasis(DirectionX, DirectionY, DirectionZ, Basis);

    double Accumulated[3] = { 0.0, 0.0, 0.0 };

    for (std::uint32_t Harmonic = 0u; Harmonic < 9u; ++Harmonic)
    {
        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            Accumulated[Component] += Coefficient[Harmonic][Component] * Basis[Harmonic];
    }

    Red   = Accumulated[0] > 0.0 ? Accumulated[0] : 0.0;
    Green = Accumulated[1] > 0.0 ? Accumulated[1] : 0.0;
    Blue  = Accumulated[2] > 0.0 ? Accumulated[2] : 0.0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AtmosphereIntegrator::DeclareMedium(const MediumSpecification& Declaring)
{
    const Deliver<bool> Validated = Declaring.Validate();

    if (!Validated.Resolved)
        return Validated;

    DeclaredMedium = Declaring;
    MediumDeclared = true;

    // 🔴 A medium amendment owes all three. ③ reads ① and ②, so rebuilding it alone against coefficients they
    //    were not built from is a sky-view surface describing an atmosphere that no longer exists.
    MediumOwed  = true;
    SkyViewOwed = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> AtmosphereIntegrator::DeclareSun(double DirectionX, double DirectionY, double DirectionZ)
{
    const double Length = std::sqrt(DirectionX * DirectionX + DirectionY * DirectionY + DirectionZ * DirectionZ);

    if (Length <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a direction of no length" });

    SunDirectionX = DirectionX / Length;
    SunDirectionY = DirectionY / Length;
    SunDirectionZ = DirectionZ / Length;

    // 🔴 `28` §4: "materially" is a declared threshold and not a strict inequality. A sun that advances by a
    //    hundredth of a degree per rotation would otherwise rebuild ③ on every rotation of a still workspace,
    //    which makes the precomputed surface an expensive way to compute what it was meant to precompute.
    const double Alignment = BoundedMagnitude(SunDirectionX * BuiltSunX
                                            + SunDirectionY * BuiltSunY
                                            + SunDirectionZ * BuiltSunZ, -1.0, 1.0);

    if (std::acos(Alignment) > SunDirectionMateriality)
        SkyViewOwed = true;

    return Deliver<bool>::Result(true);
}

Deliver<bool> AtmosphereIntegrator::DeclareCameraAltitude(double Altitude)
{
    if (!MediumDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no medium is declared" });

    if (Altitude < 0.0 || Altitude > DeclaredMedium.AtmosphereThickness)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the altitude lies outside the declared atmosphere" });
    }

    CameraAltitude = Altitude;

    if (BuiltAltitude < 0.0 || std::fabs(CameraAltitude - BuiltAltitude) > CameraAltitudeMateriality)
        SkyViewOwed = true;

    return Deliver<bool>::Result(true);
}

void AtmosphereIntegrator::DeclareAtmospherePresence(bool PresenceEnabled)
{
    PresenceDeclared = PresenceEnabled;
}

Deliver<bool> AtmosphereIntegrator::DeclareConstantFloor(const ColourSpecification& Declaring)
{
    if (!Declaring.ColourDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the floor declares no colour space" });

    ConstantFloor = Declaring;
    FloorDeclared = true;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE MEDIUM PROFILES
//------------------------------------------------------------------------------------------------------------------------

void AtmosphereIntegrator::ShapeProfile()
{
    // 📝 Built once per rebuild rather than per sample. `Shared/` takes the medium as one profile because a
    //    device reads it from one uniform; shaping it here is the single place the specification and the
    //    resolved coefficients become that one thing.
    ShapedProfile.PlanetRadius             = DeclaredMedium.PlanetRadius;
    ShapedProfile.AtmosphereThickness      = DeclaredMedium.AtmosphereThickness;
    ShapedProfile.RayleighScaleHeight      = DeclaredMedium.RayleighScaleHeight;
    ShapedProfile.MieScaleHeight           = DeclaredMedium.MieScaleHeight;
    ShapedProfile.OzoneCentreAltitude      = DeclaredMedium.OzoneCentreAltitude;
    ShapedProfile.OzoneHalfWidth           = DeclaredMedium.OzoneHalfWidth;
    ShapedProfile.MieAsymmetry             = DeclaredMedium.MieAsymmetry;
    ShapedProfile.MieScattering            = ResolvedCoefficient.MieScattering;
    ShapedProfile.MieExtinction            = ResolvedCoefficient.MieExtinction;
    ShapedProfile.RayleighScatteringRed    = ResolvedCoefficient.RayleighScattering[0];
    ShapedProfile.RayleighScatteringGreen  = ResolvedCoefficient.RayleighScattering[1];
    ShapedProfile.RayleighScatteringBlue   = ResolvedCoefficient.RayleighScattering[2];
    ShapedProfile.OzoneAbsorptionRed       = ResolvedCoefficient.OzoneAbsorption[0];
    ShapedProfile.OzoneAbsorptionGreen     = ResolvedCoefficient.OzoneAbsorption[1];
    ShapedProfile.OzoneAbsorptionBlue      = ResolvedCoefficient.OzoneAbsorption[2];
    ShapedProfile.SunDirectionX            = SunDirectionX;
    ShapedProfile.SunDirectionY            = SunDirectionY;
    ShapedProfile.SunDirectionZ            = SunDirectionZ;
    ShapedProfile.CameraAltitude           = CameraAltitude;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              ① THE TRANSMITTANCE SURFACE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 🔴 ①'s parameterisation pair and the march step's integral are `Shared/AtmosphereProjection.slang.h`'s now.
//    A mapping written once here and once there is the defect this file removed itself from: a bake and its
//    lookup read the same routine because they literally call the same routine, and the device's version is not
//    a second implementation a host copy can drift from.

}   // namespace

Deliver<bool> AtmosphereIntegrator::BuildTransmittance(const QuadratureRule& Rule)
{
    if (!Rule.Derived())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rule is not derived" });

    const Deliver<bool> Reserved =
        TransmittanceSurface.ConstructResidentSurface(TransmittanceExtentX, TransmittanceExtentY, false);

    if (!Reserved.Resolved)
        return Reserved;

    const double PlanetRadius     = DeclaredMedium.PlanetRadius;
    const double Thickness        = DeclaredMedium.AtmosphereThickness;
    const double AtmosphereRadius = PlanetRadius + Thickness;

    for (std::uint32_t Y = 0u; Y < TransmittanceExtentY; ++Y)
    {
        for (std::uint32_t X = 0u; X < TransmittanceExtentX; ++X)
        {
            const double CoordinateX  = (static_cast<double>(X)  + 0.5) / TransmittanceExtentX;
            const double CoordinateY = (static_cast<double>(Y) + 0.5) / TransmittanceExtentY;

            double Radius       = 0.0;
            double ZenithCosine = 0.0;
            ProjectTransmittanceParameter(ShapedProfile, CoordinateX, CoordinateY,
                                          Radius, ZenithCosine);

            // 🔴 A sun below the horizon is occluded by the planet, so its transmittance is zero. Marching
            //    through the planet also reaches it — the chord accumulates negative altitudes and the density
            //    diverges — but it reaches it through an exp of about 795, which is an infinity that happens to
            //    round the right way. Stating the occlusion is both correct and finite.
            if (ClassifyGroundReach(Radius, ZenithCosine, PlanetRadius))
            {
                TransmittanceSurface.Write(X, Y, 0.0, 0.0, 0.0);
                continue;
            }

            // 📝 The path runs to the atmosphere boundary and is not shortened at the ground. A ray that descends
            //    through the planet accumulates the whole of the dense lower atmosphere twice and extinguishes to
            //    nothing on its own, which is the answer a ground test would write by hand — and it writes it
            //    continuously rather than as a step the horizon texels straddle.
            const double Distance = ProjectSphereDistance(Radius, ZenithCosine, AtmosphereRadius);

            double DepthRed   = 0.0;
            double DepthGreen = 0.0;
            double DepthBlue  = 0.0;

            // 🔴 One walk, three components, in ordinal order. `02` §5: three separate integrations would
            //    evaluate the same two density profiles three times and would accumulate in three orders.
            if (Distance > 0.0)
            {
                for (std::uint32_t Index = 0u; Index < Rule.DeclaredCount(); ++Index)
                {
                    double Position  = 0.0;
                    double Weighting = 0.0;

                    if (!Rule.Project(Index, 0.0, Distance, Position, Weighting).Resolved)
                        continue;

                    const double SampleRadius = AdvanceRadius(Radius, ZenithCosine, Position);

                    double ExtinctionRed   = 0.0;
                    double ExtinctionGreen = 0.0;
                    double ExtinctionBlue  = 0.0;
                    ResolveMediumExtinction(ShapedProfile, SampleRadius - PlanetRadius,
                                            ExtinctionRed, ExtinctionGreen, ExtinctionBlue);

                    DepthRed   += ExtinctionRed   * Weighting;
                    DepthGreen += ExtinctionGreen * Weighting;
                    DepthBlue  += ExtinctionBlue  * Weighting;
                }
            }

            // 📐 Beer–Lambert. Every length here is in metres and every coefficient in reciprocal metres, so the
            //    exponent is dimensionless without a conversion — the donor formulation marches in kilometres and
            //    carries a factor of a thousand at exactly this line, which is the factor to look for first if an
            //    atmosphere ported from it is either opaque or absent.
            TransmittanceSurface.Write(X, Y,
                                       std::exp(-DepthRed), std::exp(-DepthGreen), std::exp(-DepthBlue));
        }
    }

    return Deliver<bool>::Result(true);
}

void AtmosphereIntegrator::TransmittanceAt(double Radius, double ZenithCosine,
                                           double& Red, double& Green, double& Blue) const
{
    double CoordinateX  = 0.0;
    double CoordinateY = 0.0;

    ProjectTransmittanceCoordinate(ShapedProfile, Radius, ZenithCosine, CoordinateX, CoordinateY);

    TransmittanceSurface.Sample(CoordinateX, CoordinateY, Red, Green, Blue);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          ② THE MULTIPLE-SCATTERING SURFACE
//------------------------------------------------------------------------------------------------------------------------

void AtmosphereIntegrator::MultiScatterAt(double Radius, double SunZenithCosine,
                                          double& Red, double& Green, double& Blue) const
{
    double CoordinateX  = 0.0;
    double CoordinateY = 0.0;
    ProjectMultiScatterCoordinate(ShapedProfile, Radius, SunZenithCosine, CoordinateX, CoordinateY);

    MultiScatterSurface.Sample(CoordinateX, CoordinateY, Red, Green, Blue);
}

Deliver<bool> AtmosphereIntegrator::BuildMultiScatter()
{
    if (!TransmittanceSurface.SurfaceConstructed())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the transmittance surface does not stand" });

    const Deliver<bool> Reserved =
        MultiScatterSurface.ConstructResidentSurface(MultiScatterExtentX, MultiScatterExtentY, false);

    if (!Reserved.Resolved)
        return Reserved;

    const double PlanetRadius     = DeclaredMedium.PlanetRadius;
    const double Thickness        = DeclaredMedium.AtmosphereThickness;
    const double AtmosphereRadius = PlanetRadius + Thickness;
    const double Reciprocal       = 1.0 / static_cast<double>(MultiScatterDirectionCount);

    for (std::uint32_t Y = 0u; Y < MultiScatterExtentY; ++Y)
    {
        for (std::uint32_t X = 0u; X < MultiScatterExtentX; ++X)
        {
            const double SunZenithCosine = 2.0 * ((static_cast<double>(X) + 0.5) / MultiScatterExtentX)
                                         - 1.0;
            const double StartRadius     = PlanetRadius
                                         + ((static_cast<double>(Y) + 0.5) / MultiScatterExtentY)
                                         * Thickness;

            // 📝 The surface is sun-independent: the sun is placed in the plane the zenith cosine names rather
            //    than read from the declaration, which is why ② survives every rotation the sun moves through.
            const double SunAltitudeSine = std::sqrt(std::fmax(0.0, 1.0 - SunZenithCosine * SunZenithCosine));
            const double SunX            = SunAltitudeSine;
            const double SunY            = SunZenithCosine;
            const double SunZ            = 0.0;

            double SecondOrder[3] = { 0.0, 0.0, 0.0 };
            double Transfer[3]    = { 0.0, 0.0, 0.0 };

            for (std::uint32_t Direction = 0u; Direction < MultiScatterDirectionCount; ++Direction)
            {
                double FirstCoordinate  = 0.0;
                double SecondCoordinate = 0.0;
                ProjectPlanarSample(Direction + 1u, FirstCoordinate, SecondCoordinate);

                // 🔴 `02` §6's one sphere sampling, not a second one written here. `SampleProjection` distributes
                //    the zenith about its **third** axis and `28` carries the zenith on its second — `46`'s upward
                //    convention — so the projected direction is renamed onto this component's frame rather than
                //    reused as it arrives.
                double SampleX = 0.0;
                double SampleY = 0.0;
                double SampleZ = 0.0;
                ProjectSphericalSample(FirstCoordinate, SecondCoordinate, SampleX, SampleY, SampleZ);

                const double ViewX           = SampleX;
                const double ViewY           = SampleZ;
                const double ViewZ           = SampleY;
                const double ViewZenithCosine = ViewY;

                const double Distance = ProjectMarchDistance(StartRadius, ViewZenithCosine,
                                                             PlanetRadius, AtmosphereRadius);

                if (Distance <= 0.0)
                    continue;

                const double StepSize   = Distance / static_cast<double>(MultiScatterStepCount);
                double       Throughput[3] = { 1.0, 1.0, 1.0 };

                for (std::uint32_t Step = 0u; Step < MultiScatterStepCount; ++Step)
                {
                    const double Position     = (static_cast<double>(Step) + 0.5) * StepSize;
                    const double SampleRadius = AdvanceRadius(StartRadius, ViewZenithCosine, Position);
                    const double SampleAltitude = SampleRadius - PlanetRadius;

                    double ExtinctionComponent[3] = { 0.0, 0.0, 0.0 };
                    ResolveMediumExtinction(ShapedProfile, SampleAltitude,
                                            ExtinctionComponent[0], ExtinctionComponent[1], ExtinctionComponent[2]);

                    double RayleighComponent[3] = { 0.0, 0.0, 0.0 };
                    double MieComponent         = 0.0;
                    ResolveMediumScattering(ShapedProfile, SampleAltitude,
                                            RayleighComponent[0], RayleighComponent[1], RayleighComponent[2],
                                            MieComponent);

                    // 📐 The sun's zenith cosine at the sample, against the **local** up — which is the sample's
                    //    own position direction and not the starting one. A ray that travels a hundred kilometres
                    //    around a planet of six thousand has turned measurably under itself.
                    double LocalX = 0.0, LocalY = 0.0, LocalZ = 0.0;
                    ProjectLocalUp(StartRadius, SampleRadius, Position, ViewX, ViewY, ViewZ,
                                   LocalX, LocalY, LocalZ);

                    const double SampleSunCosine = BoundedMagnitude(LocalX * SunX + LocalY * SunY + LocalZ * SunZ,
                                                                    -1.0, 1.0);

                    double SunTransmit[3] = { 0.0, 0.0, 0.0 };
                    TransmittanceAt(SampleRadius, SampleSunCosine,
                                    SunTransmit[0], SunTransmit[1], SunTransmit[2]);

                    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
                    {
                        const double ScatteringComponent = RayleighComponent[Component] + MieComponent;
                        const double StepTransmittance   = std::exp(-ExtinctionComponent[Component] * StepSize);

                        // 📐 The multiple-scattering approximation is **isotropic** — 1/4π in place of either
                        //    phase. Light that has bounced more than once has forgotten which way it came from,
                        //    and carrying a phase through the series is carrying a direction that no longer exists.
                        const double InScatter = ScatteringComponent / (4.0 * Pi);

                        Transfer[Component] += Throughput[Component]
                                             * IntegrateStepInScatter(ScatteringComponent,
                                                                      ExtinctionComponent[Component],
                                                                      StepTransmittance, StepSize);

                        SecondOrder[Component] += Throughput[Component] * SunTransmit[Component]
                                                * InScatter * StepSize;

                        Throughput[Component] *= StepTransmittance;
                    }
                }
            }

            double Psi[3] = { 0.0, 0.0, 0.0 };

            for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            {
                const double Order    = SecondOrder[Component] * Reciprocal;
                const double Fraction = Transfer[Component]    * Reciprocal;

                // 📐 Ψ = L₂ₙd / (1 − f_ms) — the closed form of the geometric series Σ L₂ₙd·f_msⁿ. The divisor is
                //    held below unity because a transfer that reaches one is a medium returning every photon it
                //    receives, which the series does not converge for and which no atmosphere is.
                const double Divisor = Fraction < 0.999 ? 1.0 - Fraction : 0.001;

                Psi[Component] = Order / Divisor;
            }

            MultiScatterSurface.Write(X, Y, Psi[0], Psi[1], Psi[2]);
        }
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ③ THE SKY-VIEW SURFACE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 🔴 ③'s parameterisation pair is `Shared/AtmosphereProjection.slang.h`'s now, with the azimuth wrapped by the
//    shared routine because only it knows the azimuth is periodic. A bake and its lookup call the same mapping,
//    and the device's copy is the one both sides compare against.

}   // namespace

Deliver<bool> AtmosphereIntegrator::BuildSkyView()
{
    if (!TransmittanceSurface.SurfaceConstructed() || !MultiScatterSurface.SurfaceConstructed())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "① or ② does not stand" });

    // 🔴 The azimuth is the one periodic axis in the whole component — finding ② above.
    const Deliver<bool> Reserved = SkyViewSurface.ConstructResidentSurface(SkyViewExtentX, SkyViewExtentY, true);

    if (!Reserved.Resolved)
        return Reserved;

    const double PlanetRadius     = DeclaredMedium.PlanetRadius;
    const double AtmosphereRadius = PlanetRadius + DeclaredMedium.AtmosphereThickness;
    const double StartRadius      = PlanetRadius + CameraAltitude;

    for (std::uint32_t Y = 0u; Y < SkyViewExtentY; ++Y)
    {
        for (std::uint32_t X = 0u; X < SkyViewExtentX; ++X)
        {
            const double CoordinateX  = (static_cast<double>(X)  + 0.5) / SkyViewExtentX;
            const double CoordinateY = (static_cast<double>(Y) + 0.5) / SkyViewExtentY;

            double ViewX = 0.0;
            double ViewY = 0.0;
            double ViewZ = 0.0;
            ProjectSkyViewDirection(CoordinateX, CoordinateY, ViewX, ViewY, ViewZ);

            const double ViewZenithCosine = ViewY;

            const double Distance = ProjectMarchDistance(StartRadius, ViewZenithCosine,
                                                         PlanetRadius, AtmosphereRadius);

            double Radiance[3] = { 0.0, 0.0, 0.0 };

            if (Distance > 0.0)
            {
                const double ScatterCosine = BoundedMagnitude(ViewX * SunDirectionX
                                                            + ViewY * SunDirectionY
                                                            + ViewZ * SunDirectionZ, -1.0, 1.0);

                const double RayleighWeighting = ProjectRayleighPhase(ScatterCosine);
                const double MieWeighting      = ProjectMiePhase(ScatterCosine, ShapedProfile.MieAsymmetry);

                const double StepSize      = Distance / static_cast<double>(SkyViewStepCount);
                double       Throughput[3] = { 1.0, 1.0, 1.0 };

                for (std::uint32_t Step = 0u; Step < SkyViewStepCount; ++Step)
                {
                    const double Position       = (static_cast<double>(Step) + 0.5) * StepSize;
                    const double SampleRadius   = AdvanceRadius(StartRadius, ViewZenithCosine, Position);
                    const double SampleAltitude = SampleRadius - PlanetRadius;

                    double ExtinctionComponent[3] = { 0.0, 0.0, 0.0 };
                    ResolveMediumExtinction(ShapedProfile, SampleAltitude,
                                            ExtinctionComponent[0], ExtinctionComponent[1], ExtinctionComponent[2]);

                    double RayleighComponent[3] = { 0.0, 0.0, 0.0 };
                    double MieComponent         = 0.0;
                    ResolveMediumScattering(ShapedProfile, SampleAltitude,
                                            RayleighComponent[0], RayleighComponent[1], RayleighComponent[2],
                                            MieComponent);

                    double LocalX = 0.0, LocalY = 0.0, LocalZ = 0.0;
                    ProjectLocalUp(StartRadius, SampleRadius, Position, ViewX, ViewY, ViewZ,
                                   LocalX, LocalY, LocalZ);

                    const double SampleSunCosine = BoundedMagnitude(LocalX * SunDirectionX
                                                                 + LocalY * SunDirectionY
                                                                 + LocalZ * SunDirectionZ, -1.0, 1.0);

                    double SunTransmit[3] = { 0.0, 0.0, 0.0 };
                    TransmittanceAt(SampleRadius, SampleSunCosine,
                                    SunTransmit[0], SunTransmit[1], SunTransmit[2]);

                    double Multiple[3] = { 0.0, 0.0, 0.0 };
                    MultiScatterAt(SampleRadius, SampleSunCosine, Multiple[0], Multiple[1], Multiple[2]);

                    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
                    {
                        // 🔴 The two components are phase-weighted **separately**. Rayleigh is near-isotropic and
                        //    Mie is sharply forward, so weighting their sum by either one is a sky with the sun's
                        //    halo spread over the whole dome or with no halo at all.
                        const double Single = (RayleighComponent[Component] * RayleighWeighting
                                             + MieComponent                * MieWeighting)
                                            * SunTransmit[Component];

                        const double Multiplied = (RayleighComponent[Component] + MieComponent)
                                                * Multiple[Component];

                        const double InScatter       = Single + Multiplied;
                        const double StepTransmittance = std::exp(-ExtinctionComponent[Component] * StepSize);

                        Radiance[Component] += Throughput[Component]
                                             * IntegrateStepInScatter(InScatter, ExtinctionComponent[Component],
                                                                      StepTransmittance, StepSize);

                        Throughput[Component] *= StepTransmittance;
                    }
                }
            }

            // 📝 🚧 The illuminant's own magnitude is **not** applied here. `44` §8 registers the atmospheric source
            //    and the requester supplies its direction; its flux scales this radiance uniformly and applying it
            //    at this depth would bake one illuminant's brightness into a surface rebuilt on direction alone.
            SkyViewSurface.Write(X, Y, Radiance[0], Radiance[1], Radiance[2]);
        }
    }

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                              THE HARMONIC CONVOLUTION
//------------------------------------------------------------------------------------------------------------------------

void AtmosphereIntegrator::DeriveIrradiance()
{
    ConvolvedIrradiance = IrradianceProjection{};

    if (!SkyViewSurface.SurfaceConstructed())
        return;

    // 📐 The estimator is (4π/N)·Σ L(ω)Y(ω): the sphere's solid angle over the sample count, because the
    //    directions are distributed uniformly in solid angle rather than importance-weighted toward the sky.
    const double SolidAngleShare = 4.0 * Pi / static_cast<double>(IrradianceSampleCount);

    for (std::uint32_t Index = 0u; Index < IrradianceSampleCount; ++Index)
    {
        double FirstCoordinate  = 0.0;
        double SecondCoordinate = 0.0;
        ProjectPlanarSample(Index + 1u, FirstCoordinate, SecondCoordinate);

        double SampleX = 0.0;
        double SampleY = 0.0;
        double SampleZ = 0.0;
        ProjectSphericalSample(FirstCoordinate, SecondCoordinate, SampleX, SampleY, SampleZ);

        const double DirectionX = SampleX;
        const double DirectionY = SampleZ;
        const double DirectionZ = SampleY;

        double CoordinateX  = 0.0;
        double CoordinateY = 0.0;
        ProjectSkyViewCoordinate(DirectionX, DirectionY, DirectionZ, CoordinateX, CoordinateY);

        double Radiance[3] = { 0.0, 0.0, 0.0 };
        SkyViewSurface.Sample(CoordinateX, CoordinateY, Radiance[0], Radiance[1], Radiance[2]);

        double Basis[9] = {};
        HarmonicBasis(DirectionX, DirectionY, DirectionZ, Basis);

        for (std::uint32_t Harmonic = 0u; Harmonic < 9u; ++Harmonic)
        {
            for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            {
                ConvolvedIrradiance.Coefficient[Harmonic][Component] +=
                    Radiance[Component] * Basis[Harmonic] * SolidAngleShare;
            }
        }
    }

    // 📐 The convolution itself: one multiply per band, the zeroth against π, the three first-order against 2π/3
    //    and the five second-order against π/4. Convolving after projecting rather than before is what makes the
    //    cosine lobe a constant per band instead of an integral per sample.
    for (std::uint32_t Harmonic = 0u; Harmonic < 9u; ++Harmonic)
    {
        const std::uint32_t Band = Harmonic == 0u ? 0u : (Harmonic < 4u ? 1u : 2u);

        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            ConvolvedIrradiance.Coefficient[Harmonic][Component] *= CosineLobe[Band];
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REBUILD
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AtmosphereIntegrator::Rebuild(const ColourSpaceSpecification& Working, const QuadratureRule& Rule)
{
    if (!MediumDeclared)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no medium is declared" });

    if (!Rule.Derived())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rule is not derived" });

    // 🔴 `28` §4: with nothing owed, nothing is rebuilt and nothing is recorded. The delivery is not a rebuild of
    //    zero surfaces reported as success — it is the schedule's contributor being told there is no work.
    if (!MediumOwed && !SkyViewOwed)
        return Deliver<bool>::Result(true);

    if (MediumOwed)
    {
        const Deliver<MediumCoefficient> Resolved = Resolve(DeclaredMedium, Working, Rule);

        if (!Resolved.Resolved)
            return Deliver<bool>::Refuse(Resolved.Error);

        ResolvedCoefficient = Resolved.Resolve();
        ShapeProfile();

        const Deliver<bool> First = BuildTransmittance(Rule);

        if (!First.Resolved)
            return First;

        const Deliver<bool> Second = BuildMultiScatter();

        if (!Second.Resolved)
            return Second;

        MediumOwed  = false;
        SkyViewOwed = true;
        ++MediumRebuilds;
    }

    if (SkyViewOwed)
    {
        // 📝 Reshaped before ③ because the sun direction and the camera altitude are the two fields ③ reads
        //    and the two that move without the medium moving with them.
        ShapeProfile();

        const Deliver<bool> Third = BuildSkyView();

        if (!Third.Resolved)
            return Third;

        DeriveIrradiance();

        BuiltSunX     = SunDirectionX;
        BuiltSunY     = SunDirectionY;
        BuiltSunZ     = SunDirectionZ;
        BuiltAltitude = CameraAltitude;
        SkyViewOwed   = false;
        ++SkyViewRebuilds;
    }

    return Deliver<bool>::Result(true);
}

bool AtmosphereIntegrator::RebuildOwed() const
{
    return MediumOwed || SkyViewOwed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 THE SAMPLED RESULTS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> AtmosphereIntegrator::SampleSkyView(double DirectionX, double DirectionY, double DirectionZ,
                                                  double& Red, double& Green, double& Blue) const
{
    // 🔴 The disabled atmosphere delivers the floor rather than refusing — `18` §5 and `30` §3 both reach their
    //    second source through this one call, so neither writes the fallback a second time.
    if (!PresenceDeclared)
    {
        Red   = FloorDeclared ? ConstantFloor.RedCoordinate   : 0.0;
        Green = FloorDeclared ? ConstantFloor.GreenCoordinate : 0.0;
        Blue  = FloorDeclared ? ConstantFloor.BlueCoordinate  : 0.0;

        return Deliver<bool>::Result(true);
    }

    if (!SkyViewSurface.SurfaceConstructed())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no sky-view surface stands" });

    const double Length = std::sqrt(DirectionX * DirectionX + DirectionY * DirectionY + DirectionZ * DirectionZ);

    if (Length <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a direction of no length" });

    double CoordinateX  = 0.0;
    double CoordinateY = 0.0;
    ProjectSkyViewCoordinate(DirectionX / Length, DirectionY / Length, DirectionZ / Length,
                             CoordinateX, CoordinateY);

    SkyViewSurface.Sample(CoordinateX, CoordinateY, Red, Green, Blue);

    return Deliver<bool>::Result(true);
}

Deliver<bool> AtmosphereIntegrator::SampleTransmittance(double Altitude, double ZenithCosine,
                                                        double& Red, double& Green, double& Blue) const
{
    if (!TransmittanceSurface.SurfaceConstructed())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no transmittance surface stands" });

    TransmittanceAt(DeclaredMedium.PlanetRadius + Altitude, BoundedMagnitude(ZenithCosine, -1.0, 1.0),
                    Red, Green, Blue);

    return Deliver<bool>::Result(true);
}

Deliver<bool> AtmosphereIntegrator::AerialTransmittance(double Altitude,
                                                        double DirectionX, double DirectionY, double DirectionZ,
                                                        double Distance,
                                                        const QuadratureRule& Rule,
                                                        double& Red, double& Green, double& Blue) const
{
    Red   = 1.0;
    Green = 1.0;
    Blue  = 1.0;

    if (!MediumDeclared || !ResolvedCoefficient.CoefficientResolved)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no medium is resolved" });

    if (!Rule.Derived())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rule is not derived" });

    const double Length = std::sqrt(DirectionX * DirectionX + DirectionY * DirectionY + DirectionZ * DirectionZ);

    if (Length <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a direction of no length" });

    if (Distance <= 0.0)
        return Deliver<bool>::Result(true);

    const double Radius       = DeclaredMedium.PlanetRadius + Altitude;
    const double ZenithCosine = BoundedMagnitude(DirectionY / Length, -1.0, 1.0);

    double DepthRed   = 0.0;
    double DepthGreen = 0.0;
    double DepthBlue  = 0.0;

    for (std::uint32_t Index = 0u; Index < Rule.DeclaredCount(); ++Index)
    {
        double Position  = 0.0;
        double Weighting = 0.0;

        if (!Rule.Project(Index, 0.0, Distance, Position, Weighting).Resolved)
            continue;

        const double SampleRadius = AdvanceRadius(Radius, ZenithCosine, Position);   // now `Shared/`'s

        double ExtinctionRed   = 0.0;
        double ExtinctionGreen = 0.0;
        double ExtinctionBlue  = 0.0;
        ResolveMediumExtinction(ShapedProfile, SampleRadius - ShapedProfile.PlanetRadius,
                                ExtinctionRed, ExtinctionGreen, ExtinctionBlue);

        DepthRed   += ExtinctionRed   * Weighting;
        DepthGreen += ExtinctionGreen * Weighting;
        DepthBlue  += ExtinctionBlue  * Weighting;
    }

    Red   = std::exp(-DepthRed);
    Green = std::exp(-DepthGreen);
    Blue  = std::exp(-DepthBlue);

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHAT IS PRESENTED
//------------------------------------------------------------------------------------------------------------------------

const IrradianceProjection& AtmosphereIntegrator::Irradiance() const     { return ConvolvedIrradiance;  }
const ResidentSurface&      AtmosphereIntegrator::Transmittance() const  { return TransmittanceSurface; }
const ResidentSurface&      AtmosphereIntegrator::MultiScatter() const   { return MultiScatterSurface;  }
const ResidentSurface&      AtmosphereIntegrator::SkyView() const        { return SkyViewSurface;       }

std::uint64_t AtmosphereIntegrator::ResidentBytes() const
{
    return TransmittanceSurface.ResidentBytes()
         + MultiScatterSurface.ResidentBytes()
         + SkyViewSurface.ResidentBytes();
}

std::uint32_t              AtmosphereIntegrator::MediumRebuildCount() const  { return MediumRebuilds;      }
std::uint32_t              AtmosphereIntegrator::SkyViewRebuildCount() const { return SkyViewRebuilds;     }
const MediumSpecification& AtmosphereIntegrator::Medium() const              { return DeclaredMedium;      }
const MediumCoefficient&   AtmosphereIntegrator::Coefficient() const         { return ResolvedCoefficient; }
bool                       AtmosphereIntegrator::AtmospherePresent() const   { return PresenceDeclared;    }

}   // namespace Slate
