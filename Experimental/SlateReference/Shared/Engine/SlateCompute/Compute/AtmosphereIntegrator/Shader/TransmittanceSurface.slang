//============================================================================================================================================
//                                                        TRANSMITTANCESURFACE.SLANG
//============================================================================================================================================
// 🧩 ① of `28` §2 — the optical depth to the atmosphere boundary, exponentiated, at 256 × 64.

#include "SlateCompute/Compute/AtmosphereIntegrator/Shader/AtmosphereUniform.slang"

// 📝 🔴 The device form of `AtmosphereIntegrator::BuildTransmittance`, texel for texel. The host builds the same
//    surface through the same `Shared/` mapping and the same medium profile, which is what makes ① comparable
//    rather than merely similar — `02` §7 and `32` §4.1 stage D.
//
// ⚠️ ① is **sun-independent**. Nothing here reads the uploaded sun direction, and nothing may: `28` §4 rebuilds
//    ① on a medium amendment alone, and a sun read here would make that rebuild condition false without making
//    anything fail. The surface would simply be stale in a way only a moved sun reveals.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

// 📝 The march step count is this entry point's own and is not shared with ② or ③. The path ① integrates is the
//    longest of the three and carries no in-scatter, so it converges on step count faster than either — forty is
//    where the surface stops changing at half precision, which is the only precision it is stored at.
static const Unsigned32 TransmittanceStepCount = 40u;   // [-] - march steps to the atmosphere boundary

ConstantBuffer<MediumUniform>  Medium;                 // [-] - the uploaded medium
RWTexture2D<Real32x4>          TransmittanceOutput;    // [-] - RGBA16F, 256 × 64

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE BAKE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Integrates one texel's optical depth and writes its transmittance.
/// in    TexelIndex  [-]  the dispatch ordinal; ordinals outside the declared extent write nothing
/// note  📐 Beer–Lambert over the whole path to the boundary, per component, in one march. Three separate marches
///        would evaluate the same two density profiles three times and accumulate in three orders — `02` §5.
/// note  📝 The path is **not** shortened at the ground. A ray that descends through the planet accumulates the
///        whole of the dense lower atmosphere twice and extinguishes to nothing on its own, which is the answer a
///        ground test would write by hand — and it writes it continuously rather than as a step the horizon texels
///        straddle, which is where the surface's own filtering would otherwise smear a discontinuity.
/// cost  🔴
/// tag   api, nonallocating, nonthrowing
[shader("compute")]
[numthreads(8, 8, 1)]
void IntegrateTransmittance(Unsigned32x3 TexelIndex : SV_DispatchThreadID)
{
    const Unsigned32 X  = TexelIndex.x;
    const Unsigned32 Y = TexelIndex.y;

    if (X >= TransmittanceExtentX || Y >= TransmittanceExtentY)
    {
        return;
    }

    const MediumProfile Profile = ResolveMediumProfile(Medium);

    Real64 CoordinateX  = 0.0;
    Real64 CoordinateY = 0.0;
    ProjectTexelCoordinate(X, Y, TransmittanceExtentX, TransmittanceExtentY,
                           CoordinateX, CoordinateY);

    Real64 Radius       = 0.0;
    Real64 ZenithCosine = 0.0;
    ProjectTransmittanceParameter(Profile, CoordinateX, CoordinateY, Radius, ZenithCosine);

    const Real64 AtmosphereRadius = Profile.PlanetRadius + Profile.AtmosphereThickness;
    const Real64 Distance         = ProjectSphereDistance(Radius, ZenithCosine, AtmosphereRadius);

    Real64 DepthRed   = 0.0;
    Real64 DepthGreen = 0.0;
    Real64 DepthBlue  = 0.0;

    if (Distance > 0.0)
    {
        const Real64 StepSize = Distance / Real64(TransmittanceStepCount);

        for (Unsigned32 Step = 0u; Step < TransmittanceStepCount; ++Step)
        {
            const Real64 Position     = (Real64(Step) + 0.5) * StepSize;
            const Real64 SampleRadius = AdvanceRadius(Radius, ZenithCosine, Position);

            Real64 ExtinctionRed   = 0.0;
            Real64 ExtinctionGreen = 0.0;
            Real64 ExtinctionBlue  = 0.0;
            ResolveMediumExtinction(Profile, SampleRadius - Profile.PlanetRadius,
                                    ExtinctionRed, ExtinctionGreen, ExtinctionBlue);

            DepthRed   += ExtinctionRed   * StepSize;
            DepthGreen += ExtinctionGreen * StepSize;
            DepthBlue  += ExtinctionBlue  * StepSize;
        }
    }

    // 📝 Every length is in metres and every coefficient in reciprocal metres, so the exponent is dimensionless
    //    without a conversion. The donor formulation marches in kilometres and carries a factor of a thousand at
    //    exactly this line — the factor to look for first if a port of this comes out opaque or absent.
    TransmittanceOutput[Unsigned32x2(X, Y)] = Real32x4(Real32(Exponential(-DepthRed)),
                                                                Real32(Exponential(-DepthGreen)),
                                                                Real32(Exponential(-DepthBlue)),
                                                                1.0f);
}

}   // namespace Slate
