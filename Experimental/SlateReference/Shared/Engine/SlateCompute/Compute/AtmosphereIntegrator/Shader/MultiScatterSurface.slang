//============================================================================================================================================
//                                                        MULTISCATTERSURFACE.SLANG
//============================================================================================================================================
// 🧩 ② of `28` §2 — the isotropic multiple-scattering series, closed, reading ① at 32 × 32.

#include "SlateCompute/Compute/AtmosphereIntegrator/Shader/AtmosphereUniform.slang"
#include "Shared/SampleProjection.slang.h"

// 📝 🔴 The device form of `AtmosphereIntegrator::BuildMultiScatter`. ② reads ① and nothing after it — `28` §2's
//    ordering, which is a correctness statement and not a scheduling preference: a multiple-scattering surface
//    built against an unwritten transmittance is uniformly black and reads as a device failure.
//
// ⚠️ ② is **sun-independent** in the same sense ① is. The sun is placed in the plane the texel's own zenith
//    cosine names rather than read from the uploaded direction, which is what lets the sun move through every
//    rotation of a session without ② being rebuilt once. Reading the uploaded direction here would be a one-line
//    edit that silently makes `28` §4's second row false.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Thirty-two directions and twenty steps are the donor formulation's own and are declared beside the host's
//    identical pair in `AtmosphereIntegrator.h`. ② is a thirty-two square surface of a quantity that has already
//    been approximated as isotropic, so refining either count refines an approximation rather than the answer.
static const Unsigned32 MultiScatterDirectionCount = 32u;   // [-] - sphere directions per texel
static const Unsigned32 MultiScatterStepCount      = 20u;   // [-] - march steps per direction

// 📐 The transfer fraction is held below unity before it divides. A fraction reaching one is a medium returning
//    every photon it receives, which the geometric series does not converge for and which no atmosphere is.
static const Real64 TransferLimit = 0.999;   // [-] - above this the divisor is held at the floor below
static const Real64 TransferFloor   = 0.001;   // [-] - the held divisor

ConstantBuffer<MediumUniform>  Medium;                 // [-] - the uploaded medium
Texture2D<Real32x4>            TransmittanceSurface;   // [-] - ①, RGBA16F, 256 × 64
SamplerState                   SurfaceSampler;         // [-] - bilinear, clamped on both axes
RWTexture2D<Real32x4>          MultiScatterOutput;     // [-] - RGBA16F, 32 × 32

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LOOKUP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 ① at a declared radius and zenith cosine.
/// note  🔴 Through `Shared/`'s forward mapping, which is the same routine the bake inverted. That is the whole
///        reason the mapping lives in `Shared/` — see the file note there.
/// cost  🚩
/// tag   nonallocating, nonthrowing
void ResolveTransmittance(MediumProfile Profile, Real64 Radius, Real64 ZenithCosine,
                          SLATE_OUT(Real64) Red, SLATE_OUT(Real64) Green, SLATE_OUT(Real64) Blue)
{
    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectTransmittanceCoordinate(Profile, Radius, ZenithCosine, CoordinateX, CoordinateY);

    const Real32x4 Sampled = TransmittanceSurface.SampleLevel(
        SurfaceSampler, Real32x2(Real32(CoordinateX), Real32(CoordinateY)), 0.0f);

    Red   = Real64(Sampled[0]);
    Green = Real64(Sampled[1]);
    Blue  = Real64(Sampled[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE BAKE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Integrates one texel's second-order in-scatter and its transfer fraction, and writes their closed series.
/// note  📐 Ψ = L₂ₙd / (1 − f_ms) — the closed form of Σ L₂ₙd·f_msⁿ. Summing the series term by term would need
///        as many marches as terms retained; the closed form needs the two the march already produces.
/// note  🔴 The phase is **isotropic**, 1/4π, in place of either real one. Light that has bounced more than once
///        has forgotten which way it came from, and carrying a phase through the series carries a direction that
///        no longer exists. This is the approximation that makes ② eight kibibytes rather than a directional set.
/// note  📝 Directions come from `02` §6's one sphere sampling and not from a Fibonacci sphere written here.
///        `SampleProjection` distributes the zenith about its **third** axis and `28` carries it on the second —
///        `46`'s upward convention — so the projected direction is renamed onto this frame at the call site rather
///        than duplicated with the axes already swapped.
/// cost  🔴
/// tag   api, nonallocating, nonthrowing
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateMultiScatter(Unsigned32x3 TexelIndex : SV_DispatchThreadID)
{
    const Unsigned32 X  = TexelIndex.x;
    const Unsigned32 Y = TexelIndex.y;

    if (X >= MultiScatterExtentX || Y >= MultiScatterExtentY)
    {
        return;
    }

    const MediumProfile Profile          = ResolveMediumProfile(Medium);
    const Real64        AtmosphereRadius = Profile.PlanetRadius + Profile.AtmosphereThickness;
    const Real64        Reciprocal       = 1.0 / Real64(MultiScatterDirectionCount);

    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectTexelCoordinate(X, Y, MultiScatterExtentX, MultiScatterExtentY,
                           CoordinateX, CoordinateY);

    Real64 StartRadius     = 0.0;
    Real64 SunZenithCosine = 0.0;
    ProjectMultiScatterParameter(Profile, CoordinateX, CoordinateY, StartRadius, SunZenithCosine);

    // 📝 The sun in the plane the zenith cosine names — the surface's own parameter, never the uploaded direction.
    const Real64 SunAltitudeSine = SquareRoot(BoundedMagnitude(1.0 - SunZenithCosine * SunZenithCosine, 0.0, 1.0));
    const Real64 SunX            = SunAltitudeSine;
    const Real64 SunY            = SunZenithCosine;
    const Real64 SunZ            = 0.0;

    Real64 SecondOrder[3] = { 0.0, 0.0, 0.0 };
    Real64 Transfer[3]    = { 0.0, 0.0, 0.0 };

    for (Unsigned32 Direction = 0u; Direction < MultiScatterDirectionCount; ++Direction)
    {
        Real64 FirstCoordinate  = 0.0;
        Real64 SecondCoordinate = 0.0;
        ProjectPlanarSample(Direction + 1u, FirstCoordinate, SecondCoordinate);

        Real64 SampleX = 0.0;
        Real64 SampleY = 0.0;
        Real64 SampleZ = 0.0;
        ProjectSphericalSample(FirstCoordinate, SecondCoordinate, SampleX, SampleY, SampleZ);

        const Real64 ViewX            = SampleX;
        const Real64 ViewY            = SampleZ;
        const Real64 ViewZ            = SampleY;
        const Real64 ViewZenithCosine = ViewY;

        const Real64 Distance = ProjectMarchDistance(StartRadius, ViewZenithCosine,
                                                    Profile.PlanetRadius, AtmosphereRadius);

        if (Distance <= 0.0)
        {
            continue;
        }

        const Real64 StepSize      = Distance / Real64(MultiScatterStepCount);
        Real64       Throughput[3] = { 1.0, 1.0, 1.0 };

        for (Unsigned32 Step = 0u; Step < MultiScatterStepCount; ++Step)
        {
            const Real64 Position       = (Real64(Step) + 0.5) * StepSize;
            const Real64 SampleRadius   = AdvanceRadius(StartRadius, ViewZenithCosine, Position);
            const Real64 SampleAltitude = SampleRadius - Profile.PlanetRadius;

            Real64 ExtinctionComponent[3] = { 0.0, 0.0, 0.0 };
            ResolveMediumExtinction(Profile, SampleAltitude,
                                    ExtinctionComponent[0], ExtinctionComponent[1], ExtinctionComponent[2]);

            Real64 RayleighComponent[3] = { 0.0, 0.0, 0.0 };
            Real64 MieComponent         = 0.0;
            ResolveMediumScattering(Profile, SampleAltitude,
                                    RayleighComponent[0], RayleighComponent[1], RayleighComponent[2],
                                    MieComponent);

            Real64 UpX = 0.0;
            Real64 UpY = 0.0;
            Real64 UpZ = 0.0;
            ProjectLocalUp(StartRadius, SampleRadius, Position, ViewX, ViewY, ViewZ, UpX, UpY, UpZ);

            const Real64 SampleSunCosine = BoundedMagnitude(UpX * SunX + UpY * SunY + UpZ * SunZ, -1.0, 1.0);

            Real64 SunTransmit[3] = { 0.0, 0.0, 0.0 };
            ResolveTransmittance(Profile, SampleRadius, SampleSunCosine,
                                 SunTransmit[0], SunTransmit[1], SunTransmit[2]);

            for (Unsigned32 Component = 0u; Component < 3u; ++Component)
            {
                const Real64 ScatteringComponent = RayleighComponent[Component] + MieComponent;
                const Real64 StepTransmittance   = Exponential(-ExtinctionComponent[Component] * StepSize);
                const Real64 InScatter           = ScatteringComponent / (4.0 * Pi);

                Transfer[Component] += Throughput[Component]
                                     * IntegrateStepInScatter(ScatteringComponent,
                                                              ExtinctionComponent[Component],
                                                              StepTransmittance, StepSize);

                SecondOrder[Component] += Throughput[Component] * SunTransmit[Component] * InScatter * StepSize;

                Throughput[Component] *= StepTransmittance;
            }
        }
    }

    Real64 Series[3] = { 0.0, 0.0, 0.0 };

    for (Unsigned32 Component = 0u; Component < 3u; ++Component)
    {
        const Real64 Order    = SecondOrder[Component] * Reciprocal;
        const Real64 Fraction = Transfer[Component]    * Reciprocal;
        const Real64 Divisor  = Fraction < TransferLimit ? 1.0 - Fraction : TransferFloor;

        Series[Component] = Order / Divisor;
    }

    MultiScatterOutput[Unsigned32x2(X, Y)] = Real32x4(Real32(Series[0]),
                                                               Real32(Series[1]),
                                                               Real32(Series[2]),
                                                               1.0f);
}

}   // namespace Slate
