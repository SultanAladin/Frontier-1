//============================================================================================================================================
//                                                        ATMOSPHEREINTEGRATOR.H
//============================================================================================================================================
// 🧩 Three resident lookup surfaces replacing per-pixel marching — and the only source of environmental light in Slate.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/AtmosphereProjection.slang.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/QuadratureIntegrator/Api/QuadratureIntegrator.h"
#include "SlateMath/Numeric/SpectralProjection/Api/SpectralProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE MEDIUM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The three components of `28` §3, each with its own density profile and scattering behaviour.
/// note  🔴 Ozone absorbs **without scattering** and has no scattering coefficient at all — not a zero one. It is
///        what produces the blue of twilight rather than the grey the other two components alone give, and
///        omitting it is the most common reason an atmosphere looks wrong only at low sun angles.
/// note  🚧 `28` §8 leaves artist-editability open. The values below are Earth's and are declared, not assumed:
///        a document that edits them edits this specification and the surfaces rebuild per §4's first two rows.
/// tag   nonallocating, nonthrowing
struct MediumSpecification
{
    double  PlanetRadius           = 6360000.0;   // [m]     - to the surface
    double  AtmosphereThickness    = 100000.0;    // [m]     - above it
    double  RayleighScaleHeight    = 8000.0;      // [m]     - exponential
    double  MieScaleHeight         = 1200.0;      // [m]     - exponential
    double  MieScattering          = 3.996e-6;    // [1/m]   - wavelength-neutral, at sea level
    double  MieExtinction          = 4.400e-6;    // [1/m]   - scattering plus absorption
    double  MieAsymmetry           = 0.80;        // [-]     - forward-biased; zero is isotropic
    double  OzoneCentreAltitude    = 25000.0;     // [m]     - the tent's centre
    double  OzoneHalfWidth         = 15000.0;     // [m]     - the tent falls to nothing at this departure
    double  OzonePeakAbsorption    = 6.0e-6;      // [1/m]   - at the Chappuis peak, at the tent's centre
    double  RefractiveIndex        = 1.00029;     // [-]     - of air at sea level
    double  MolecularConcentration = 2.545e25;    // [1/m³]  - at sea level
    double  Depolarisation         = 0.035;       // [-]     - the King correction factor's argument

    /// 🧩 Whether the medium describes an atmosphere that can be integrated at all.
    /// out   Result  [-]  refuses with ContentUnsupported for a non-positive radius, thickness, scale height or
    ///                     ozone half width, and for an asymmetry outside the open interval about the origin
    /// note  📐 An asymmetry reaching unity collapses the forward-scattering lobe onto a direction of zero solid
    ///        extent, and the phase magnitude diverges along it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Validate() const;
};

/// 🧩 The medium's coefficients resolved into the working space, per component.
/// note  🔴 Rayleigh and ozone are resolved **spectrally**, through `02` §5's `SpectralProjection`. Mie is
///        wavelength-neutral by declaration and needs no projection, which is why it is one magnitude rather
///        than three — writing it as three equal ones would invite a later edit to make them differ.
/// tag   nonallocating, nonthrowing
struct MediumCoefficient
{
    double  RayleighScattering[3] = { 0.0, 0.0, 0.0 };   // [1/m] - working space, at sea level
    double  OzoneAbsorption[3]    = { 0.0, 0.0, 0.0 };   // [1/m] - working space, at the tent's centre
    double  MieScattering         = 0.0;                 // [1/m] - neutral by declaration
    double  MieExtinction         = 0.0;                 // [1/m] - neutral by declaration
    bool    CoefficientResolved   = false;               // [-]   - Resolve delivered
};

/// 🧩 Resolves a medium's spectral coefficients into a declared working space.
/// in    Declared  [-]  the medium
/// in    Working   [-]  the space the coefficients are expressed in
/// in    Rule      [-]  a derived rule, integrated over the declared wavelength interval
/// out   Result   [-]  carries the medium's own refusal, and `02` §5's where the projection declines
/// note  📐 The Rayleigh coefficient is **derived** from the medium's refractive index, molecular concentration
///        and depolarisation rather than transcribed as three magnitudes. Transcribed, the three are correct for
///        exactly one set of primaries and one working space, and a document declaring a wider space would get a
///        sky whose blue is the old space's blue reinterpreted.
/// note  📝 The ozone absorption is a **fit to measured absorption** rather than a derivation, unlike Rayleigh.
///        The Chappuis band has no closed form, and saying so here is what stops a later reader from assuming
///        the same first-principles standing for both.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<MediumCoefficient> Resolve(const MediumSpecification&      Declared,
                                   const ColourSpaceSpecification& Working,
                                   const QuadratureRule&           Rule);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                 ONE RESIDENT SURFACE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One precomputed lookup surface — sampled by coordinate, resident on the device, rebuilt on a declared condition.
/// note  ⚠️ `Table` is banned by `00` §8, and the substitution is not a euphemism: these are sampled by
///        coordinate with filtering between their texels, which is what `Surface` means everywhere else in the
///        series and is not what a lookup indexed by an ordinal would be.
/// note  🔴 Stored at half precision, which is what makes `28` §1's declared byte totals true rather than
///        aspirational. All three are Tier D — perceptual output, no numeric guarantee — so half precision is
///        correct by definition rather than by concession.
/// tag   owning
class ResidentSurface
{
public:

    /// 🧩 Reservations the surface at a declared extent, every texel zero.
    /// in    WrapXDeclared  [-]  the first axis is periodic and its filter wraps rather than clamps
    /// out   Result  [-]  refuses with ContentUnsupported for a zero extent on either axis
    /// note  🔴 Wrapping is declared per surface because only ③'s azimuth is periodic. ①'s and ②'s axes are
    ///        altitude and sun zenith, both genuinely bounded — a wrapped sample at ③'s zenith would read the
    ///        horizon while standing at the pole, which appears as a bright ring directly overhead.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructResidentSurface(std::uint32_t Width, std::uint32_t Height, bool WrapXDeclared);

    /// 🧩 Writes one texel's three components; the fourth is written as unity.
    /// note  📝 The fourth component is claimed and unused. `08` §2 declares the format RGBA16F and a
    ///        three-component format would be a fourth format for the device to negotiate — which `06` §2.1's
    ///        one-queue, explicit-descriptor spine has no appetite for.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Write(std::uint32_t X, std::uint32_t Y, double Red, double Green, double Blue);

    /// 🧩 Samples the surface bilinearly at a declared coordinate; the axis declared periodic wraps, the other clamps.
    /// in    CoordinateX   [-]  the closed unit interval; outside it the sample clamps, or wraps where constructed
    /// in    CoordinateY  [-]  likewise
    /// note  🔴 Wrapping is declared per surface by `Construct`'s `WrapXDeclared`, and only ③'s azimuth is
    ///        periodic. The zenith axis genuinely ends at the zenith, and a wrapped sample there reads the horizon —
    ///        which appears as a bright ring directly overhead.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Sample(double CoordinateX, double CoordinateY, double& Red, double& Green, double& Blue) const;

    /// 🧩 The encoded texels, for whoever uploads them.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<std::uint16_t>& Texels() const;

    /// 🧩 What the surface occupies once resident.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ResidentBytes() const;

    std::uint32_t Width() const;
    std::uint32_t Height() const;
    bool          SurfaceConstructed() const;

private:

    std::vector<std::uint16_t>  Encoded;              // [-] - four components per texel, half precision
    std::uint32_t               SpannedX  = 0u;   // [px]
    std::uint32_t               SpannedY = 0u;   // [px]
    bool                        WrapX     = false; // [-] - the first axis filters periodically
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE AMBIENT TERM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The sky's radiance projected onto a second-order harmonic basis and convolved against the cosine lobe.
/// note  🔴 `28` §5: the convolution is derived when `SkyViewSurface` rebuilds, **never per pixel**. A hemisphere
///        integral evaluated per shaded pixel is the thing the whole precomputation exists to avoid, and it
///        would be evaluated at every pixel of every rotation rather than at every rebuild.
/// note  ⚠️ `00` §5.1's third substitution point, made concrete. The donor documents describe an irradiance
///        product "for probes"; no probe population exists in Slate, so the product is retained and retargeted to
///        `18` §5's diffuse ambient. `Probe` is banned and nothing here spells it.
/// note  📐 Nine coefficients is the standard order for a diffuse convolution and is not a budget: the cosine
///        lobe's own harmonic expansion has less than a percent of its energy above the second order, so a
///        higher order would carry coefficients that the convolution multiplies by very nearly nothing.
/// tag   nonallocating, nonthrowing
struct IrradianceProjection
{
    double  Coefficient[9][3] = {};   // [-] - nine harmonics, three components, cosine-convolved

    /// 🧩 Evaluates the irradiance incoming at a surface of a declared orientation.
    /// in    DirectionX  [-]  the surface's outward orientation, unit length
    /// in    DirectionY  [-]
    /// in    DirectionZ  [-]
    /// out   Red/Green/Blue  [-]  never negative; the reconstruction is clamped at zero
    /// note  ⚠️ A truncated harmonic reconstruction rings, and the ring goes negative where the sky is dark
    ///        against a bright horizon. Clamped at zero because a negative irradiance is not a dim surface, it
    ///        is a surface that subtracts light from whatever else reaches it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Evaluate(double DirectionX, double DirectionY, double DirectionZ,
                  double& Red, double& Green, double& Blue) const;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The three resident surfaces, their construction order, and the conditions each rebuilds on.
/// note  🔴 `28` §2: strictly ordered ① ② ③, and each surface reads only the surfaces before it. Constructing ③
///        against a ① that does not stand is rejected rather than performed against zeros — a sky-view surface
///        built against an absent transmittance is uniformly black and reads as a device failure.
/// note  🔴 The sun direction is **supplied**, not read from `44`. `44` §8 gates that exactly one illuminant is
///        registered as the atmospheric source and that `28` reads it; the requester resolves that registration on the
///        tick and hands the direction over. Declaring the edge instead would put `44` in this document's
///        Upstream and move `28` from `00` §9.1's stratum 4 to stratum 5, and `00` §11 gates that a declared edge
///        is a real read — so the choice is between a false edge and a supplied parameter.
/// note  ⚠️ There is no ground albedo. Hillaire's formulation includes one and it materially brightens the
///        multiple-scattering term near the horizon; `28` §3 declares three medium components and a fourth is not
///        this document's to invent. Recorded as an open row rather than added quietly.
/// tag   owning
class AtmosphereIntegrator
{
public:

    // 📝 Read by this component alone. `28` §8 carries none of the three as open rows because they are the
    //    donor formulation's own, so they are declared here with the reason each is what it is.
    static constexpr std::uint32_t MultiScatterDirectionCount = 32u;   // [-] - sphere directions per cell
    static constexpr std::uint32_t MultiScatterStepCount      = 20u;   // [-] - march steps per direction
    static constexpr std::uint32_t SkyViewStepCount           = 30u;   // [-] - march steps per view ray
    static constexpr std::uint32_t IrradianceSampleCount      = 256u;  // [-] - sphere samples per rebuild

    /// 🧩 Declares the medium, which owes all three surfaces a rebuild.
    /// out   Result  [-]  carries the medium's own refusal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareMedium(const MediumSpecification& Declaring);

    /// 🧩 Declares the direction toward the atmospheric source, as `44`'s registered illuminant reports it.
    /// in    DirectionX  [-]  toward the sun; normalised here, so an unnormalised direction is accepted
    /// in    DirectionY  [-]  the local zenith is the second axis, matching `46`'s upward convention
    /// in    DirectionZ  [-]
    /// out   Result     [-]  refuses with ContentUnsupported for a direction of no length
    /// post  🔴 the sky-view surface is owed a rebuild only when the direction moved **materially** — `28` §4
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareSun(double DirectionX, double DirectionY, double DirectionZ);

    /// 🧩 Declares the camera's altitude above the planet surface.
    /// out   Result  [-]  refuses with ContentUnsupported for an altitude outside the declared atmosphere
    /// post  the sky-view surface is owed a rebuild only when the altitude changed materially
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareCameraAltitude(double Altitude);

    /// 🧩 Declares whether the atmosphere is present at all.
    /// note  🔴 `28` §7's last gate: with the atmosphere disabled, `18` falls back to the constant floor and `30`
    ///        to the same. Both reach that fallback through `SampleSkyView` below, so neither carries a branch of
    ///        its own and the two cannot come to disagree about what "disabled" looks like.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareAtmospherePresence(bool PresenceEnabled);

    /// 🧩 Declares the constant floor the disabled atmosphere resolves to.
    /// out   Result  [-]  refuses with ContentUnsupported for a colour declaring no space — `36` §1
    /// note  🚧 `18` §10 carries the floor's magnitude as an open row and records that it blocks nothing
    ///        structural. It is declared by the caller here rather than chosen, which is what keeps the row open.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareConstantFloor(const ColourSpecification& Declaring);

    /// 🧩 Rebuilds whatever the declared conditions owe, in construction order, and nothing else.
    /// in    Working  [-]  the space the radiance is expressed in
    /// in    Rule     [-]  a derived rule; the optical depths are integrated against it
    /// out   Result  [-]  refuses with ContentUnsupported before a medium is declared or before the rule is
    ///                     derived, and carries `Resolve`'s refusal where the spectral projection declines
    /// post  🔴 with nothing owed, nothing is rebuilt and nothing is recorded — `28` §4
    /// note  🔴 The construction order is ① transmittance, ② multiple scattering reading ①, ③ sky-view reading
    ///        both. A rebuild owed on ③ alone runs ③ alone, which is the whole reason the sun may move without
    ///        the medium being re-integrated.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Rebuild(const ColourSpaceSpecification& Working, const QuadratureRule& Rule);

    /// 🧩 Whether anything is owed a rebuild.
    /// note  🔴 `28` §4: `28` is conditional in `08` §3. When nothing changed, it records nothing — and this is
    ///        what the schedule's contributor reads to decide.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool RebuildOwed() const;

    /// 🧩 Samples sky-view radiance along a view direction — `18` §5's specular ambient and `30`'s fallback.
    /// in    DirectionX  [-]  the view direction, unit length, in the local frame
    /// in    DirectionY  [-]
    /// in    DirectionZ  [-]
    /// out   Red/Green/Blue  [-]  radiance, in the working space
    /// out   Result     [-]  refuses with ContentUnsupported when the atmosphere is enabled and no sky-view
    ///                        surface stands
    /// note  🔴 With the atmosphere disabled this delivers the constant floor rather than refusing. `18` §5 and
    ///        `30` §3 both name the floor as the second of exactly two sources, and a refusal here would make
    ///        each of them write the fallback again.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> SampleSkyView(double DirectionX, double DirectionY, double DirectionZ,
                                double& Red, double& Green, double& Blue) const;

    /// 🧩 Samples transmittance from a declared altitude along a declared zenith cosine, to the atmosphere boundary.
    /// out   Result  [-]  refuses with ContentUnsupported before ① stands
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> SampleTransmittance(double Altitude, double ZenithCosine,
                                      double& Red, double& Green, double& Blue) const;

    /// 🧩 Transmittance over a bounded distance along a view ray — `28` §6's aerial perspective.
    /// in    Altitude  [m]  where the ray begins
    /// in    Distance  [m]  how far along it the surface sits
    /// out   Result   [-]  refuses with ContentUnsupported before a medium is declared or before the rule is derived
    /// note  🔴 Integrated directly rather than read from ①. ① holds transmittance **to the atmosphere boundary**
    ///        and the ratio trick that recovers a bounded segment from it loses its conditioning near the horizon,
    ///        which is exactly where distant geometry sits. `28` §8 leaves whether aerial perspective earns a
    ///        resident surface of its own open, and integrating here is what keeps that row open rather than
    ///        answering it by accident.
    /// note  ⚠️ This applies to scene surfaces in `18` and not only to the sky. Without it distant geometry reads
    ///        as unnaturally crisp against a correct sky — `28` §6.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> AerialTransmittance(double Altitude,
                                      double DirectionX, double DirectionY, double DirectionZ,
                                      double Distance,
                                      const QuadratureRule& Rule,
                                      double& Red, double& Green, double& Blue) const;

    /// 🧩 The cosine-convolved irradiance, derived at the last sky-view rebuild — `18` §5's diffuse ambient.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const IrradianceProjection& Irradiance() const;

    const ResidentSurface& Transmittance() const;
    const ResidentSurface& MultiScatter() const;
    const ResidentSurface& SkyView() const;

    /// 🧩 What all three occupy once resident — `28` §7's first gate, measured rather than asserted.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t ResidentBytes() const;

    /// 🧩 How many times each surface has been rebuilt this session.
    /// note  📝 Current so that `28` §4's "almost never" and "occasionally" are measurable rather than hoped
    ///        for. A transmittance count that tracks the rotation count is the defect §4 exists to prevent.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t MediumRebuildCount() const;
    std::uint32_t SkyViewRebuildCount() const;

    const MediumSpecification& Medium() const;
    const MediumCoefficient&   Coefficient() const;
    bool                       AtmospherePresent() const;

private:

    void          ShapeProfile();
    Deliver<bool> BuildTransmittance(const QuadratureRule& Rule);
    Deliver<bool> BuildMultiScatter();
    Deliver<bool> BuildSkyView();
    void          DeriveIrradiance();

    void          TransmittanceAt(double Radius, double ZenithCosine,
                                  double& Red, double& Green, double& Blue) const;
    void          MultiScatterAt(double Radius, double SunZenithCosine,
                                 double& Red, double& Green, double& Blue) const;

    MediumSpecification   DeclaredMedium      = {};      // [-]
    MediumCoefficient     ResolvedCoefficient = {};      // [-] - working space
    MediumProfile         ShapedProfile       = {};      // [-] - `Shared/`'s view of the two above
    ResidentSurface       TransmittanceSurface;          // [-] - ①
    ResidentSurface       MultiScatterSurface;           // [-] - ②
    ResidentSurface       SkyViewSurface;                // [-] - ③
    IrradianceProjection  ConvolvedIrradiance = {};      // [-] - derived with ③
    ColourSpecification   ConstantFloor       = {};      // [-] - carries its space
    double                SunDirectionX       = 0.0;     // [-] - toward the sun, unit
    double                SunDirectionY       = 1.0;     // [-]
    double                SunDirectionZ       = 0.0;     // [-]
    double                BuiltSunX           = 0.0;     // [-] - the direction ③ was built against
    double                BuiltSunY           = 0.0;     // [-]
    double                BuiltSunZ           = 0.0;     // [-]
    double                CameraAltitude      = 0.0;     // [m]
    double                BuiltAltitude       = -1.0;    // [m] - negative declares ③ never built
    std::uint32_t         MediumRebuilds      = 0u;      // [-]
    std::uint32_t         SkyViewRebuilds     = 0u;      // [-]
    bool                  MediumDeclared      = false;   // [-]
    bool                  MediumOwed          = true;    // [-] - ① and ② are owed
    bool                  SkyViewOwed         = true;    // [-] - ③ is owed
    bool                  PresenceDeclared    = true;    // [-] - the atmosphere is enabled
    bool                  FloorDeclared       = false;   // [-]
};

// 📐 Every quantity here is continuous and the three surfaces are Tier D by declaration — `28` §1. The component
//    claims Perceptual, and `00` §3's transitivity rule then forbids anything downstream from claiming better.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded);

}   // namespace Slate
