//============================================================================================================================================
//                                                           SKYVIEWSURFACE.SLANG
//============================================================================================================================================
// 🧩 ③ of `28` §2 — single scattering phase-weighted against ①, plus ②'s isotropic term, at 192 × 108.

#include "SlateCompute/Compute/AtmosphereIntegrator/Shader/AtmosphereUniform.slang"

// 📝 🔴 The device form of `AtmosphereIntegrator::BuildSkyView`. ③ reads ① and ② and is the only one of the three
//    that reads the uploaded sun direction and camera altitude — which is exactly why it is the only one `28` §4
//    rebuilds when either moves materially, and why the other two survive that movement untouched.
//
// ⚠️ The illuminant's own flux is **not** applied here. `44` §8 registers the atmospheric source and the requester
//    supplies its direction; its magnitude scales this radiance uniformly, and applying it at this depth would
//    bake one illuminant's brightness into a surface that `28` §4 rebuilds on direction alone. The donor
//    formulation multiplies by its solar illuminance at the end of this entry point; that multiply is deliberately
//    absent and belongs to whoever reads ③.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Thirty steps per view ray, matching the host's own declaration. ③ is the surface an artist looks directly
//    at, so its step count is the one of the three that is a quality decision rather than a convergence one.
static const Unsigned32 SkyViewStepCount = 30u;   // [-] - march steps per view ray

ConstantBuffer<MediumUniform>  Medium;                 // [-] - the uploaded medium
Texture2D<Real32x4>            TransmittanceSurface;   // [-] - ①, RGBA16F, 256 × 64
Texture2D<Real32x4>            MultiScatterSurface;    // [-] - ②, RGBA16F, 32 × 32
SamplerState                   SurfaceSampler;         // [-] - bilinear, clamped on both axes
RWTexture2D<Real32x4>          SkyViewOutput;          // [-] - RGBA16F, 192 × 108

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE LOOKUPS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 ① at a declared radius and zenith cosine.
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

/// 🧩 ② at a declared radius and sun zenith cosine.
/// cost  🚩
/// tag   nonallocating, nonthrowing
void ResolveMultiScatter(MediumProfile Profile, Real64 Radius, Real64 SunZenithCosine,
                         SLATE_OUT(Real64) Red, SLATE_OUT(Real64) Green, SLATE_OUT(Real64) Blue)
{
    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectMultiScatterCoordinate(Profile, Radius, SunZenithCosine, CoordinateX, CoordinateY);

    const Real32x4 Sampled = MultiScatterSurface.SampleLevel(
        SurfaceSampler, Real32x2(Real32(CoordinateX), Real32(CoordinateY)), 0.0f);

    Red   = Real64(Sampled[0]);
    Green = Real64(Sampled[1]);
    Blue  = Real64(Sampled[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE BAKE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Marches one texel's view ray and writes the radiance incoming along it.
/// note  🔴 Rayleigh and Mie are phase-weighted **separately** and only then summed. Rayleigh is near-isotropic
///        and Mie is sharply forward, so weighting their sum by either one produces a sky with the sun's halo
///        spread across the whole dome or with no halo at all — and both look like a plausible atmosphere.
/// note  📐 The in-scatter is attenuated by ① toward the sun, which is the shadowing term: it is what darkens the
///        sky's own limb at sunset rather than merely reddening it, and it is why ① is built first.
/// cost  🔴
/// tag   api, nonallocating, nonthrowing
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateSkyView(Unsigned32x3 TexelIndex : SV_DispatchThreadID)
{
    const Unsigned32 X  = TexelIndex.x;
    const Unsigned32 Y = TexelIndex.y;

    if (X >= SkyViewExtentX || Y >= SkyViewExtentY)
    {
        return;
    }

    const MediumProfile Profile          = ResolveMediumProfile(Medium);
    const Real64        AtmosphereRadius = Profile.PlanetRadius + Profile.AtmosphereThickness;
    const Real64        StartRadius      = Profile.PlanetRadius + Profile.CameraAltitude;

    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectTexelCoordinate(X, Y, SkyViewExtentX, SkyViewExtentY,
                           CoordinateX, CoordinateY);

    Real64 ViewX = 0.0;
    Real64 ViewY = 0.0;
    Real64 ViewZ = 0.0;
    ProjectSkyViewDirection(CoordinateX, CoordinateY, ViewX, ViewY, ViewZ);

    const Real64 ViewZenithCosine = ViewY;
    const Real64 Distance         = ProjectMarchDistance(StartRadius, ViewZenithCosine,
                                                         Profile.PlanetRadius, AtmosphereRadius);

    Real64 Radiance[3] = { 0.0, 0.0, 0.0 };

    if (Distance > 0.0)
    {
        const Real64 ScatterCosine = BoundedMagnitude(ViewX * Profile.SunDirectionX
                                                    + ViewY * Profile.SunDirectionY
                                                    + ViewZ * Profile.SunDirectionZ, -1.0, 1.0);

        const Real64 RayleighWeighting = ProjectRayleighPhase(ScatterCosine);
        const Real64 MieWeighting      = ProjectMiePhase(ScatterCosine, Profile.MieAsymmetry);

        const Real64 StepSize      = Distance / Real64(SkyViewStepCount);
        Real64       Throughput[3] = { 1.0, 1.0, 1.0 };

        for (Unsigned32 Step = 0u; Step < SkyViewStepCount; ++Step)
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

            const Real64 SampleSunCosine = BoundedMagnitude(UpX * Profile.SunDirectionX
                                                          + UpY * Profile.SunDirectionY
                                                          + UpZ * Profile.SunDirectionZ, -1.0, 1.0);

            Real64 SunTransmit[3] = { 0.0, 0.0, 0.0 };
            ResolveTransmittance(Profile, SampleRadius, SampleSunCosine,
                                 SunTransmit[0], SunTransmit[1], SunTransmit[2]);

            Real64 Multiple[3] = { 0.0, 0.0, 0.0 };
            ResolveMultiScatter(Profile, SampleRadius, SampleSunCosine,
                                Multiple[0], Multiple[1], Multiple[2]);

            for (Unsigned32 Component = 0u; Component < 3u; ++Component)
            {
                const Real64 Single = (RayleighComponent[Component] * RayleighWeighting
                                     + MieComponent                 * MieWeighting)
                                    * SunTransmit[Component];

                const Real64 Multiplied = (RayleighComponent[Component] + MieComponent) * Multiple[Component];

                const Real64 InScatter         = Single + Multiplied;
                const Real64 StepTransmittance = Exponential(-ExtinctionComponent[Component] * StepSize);

                Radiance[Component] += Throughput[Component]
                                     * IntegrateStepInScatter(InScatter, ExtinctionComponent[Component],
                                                              StepTransmittance, StepSize);

                Throughput[Component] *= StepTransmittance;
            }
        }
    }

    SkyViewOutput[Unsigned32x2(X, Y)] = Real32x4(Real32(Radiance[0]),
                                                          Real32(Radiance[1]),
                                                          Real32(Radiance[2]),
                                                          1.0f);
}

}   // namespace Slate
