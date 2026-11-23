//============================================================================================================================================
//                                                      REFLECTANCEPROJECTION.SLANG.H
//============================================================================================================================================
// 🧩 `18` §4's direct term — the distribution, the attenuation, the Fresnel, the diffuse lobe and the compensation that keeps them energy-correct.

#pragma once

#include "Shared/ToolchainInterchange.slang.h"
#include "Foundation/NumericTolerance.h"

// 📐 🔴 Every routine below is read by `18`'s shading dispatch on the device and by `82`'s host preview of the
//    same surface — `82` §5 resolves a preview on the host and `00` §11 gates the agreement at Tier B. Where the
//    two disagree the artist blames the preview, because the preview is the thing that looks provisional, and
//    the defect is then attributed to the wrong subsystem for as long as it takes somebody to check.
//
// ⚠️ Nothing here is a shading model. `42` selects a `ReflectanceSelection` and `18` §3 declares which channels
//    each one consumes; what is declared here are the **terms** those selections compose from, so a selection is
//    a composition of these and never a second implementation of one of them.
//
// 🔴 Every term below is a **magnitude** and carries no colour. `36` §1 requires a colour to travel with the
//    space it is a coordinate in, and a term that returned three coordinates would be returning a colour in no
//    space at all. The caller applies each term per component and keeps the space it already holds.

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DISTRIBUTION
//------------------------------------------------------------------------------------------------------------------------

// 📐 The distribution parameter is the **squared** perceptual roughness — `18` §2 channel 3. Squaring at the
//    call site instead would be squaring at every call site, and the one that forgot would be a material whose
//    roughness slider behaved differently from every other material's.
//
// 📝 A floor under the parameter, because a perfectly smooth surface concentrates the whole lobe onto a
//    direction of no solid extent and the distribution's magnitude then diverges along it. The floor is the same
//    order as the tolerance `Foundation/` already declares for a vanishing quantity.
SLATE_SHARED SLATE_CONSTEXPR Real64 DistributionParameterFloor() { return 1.0e-4; }

/// 🧩 The GGX microfacet distribution at one half-direction.
/// in    RoughnessSquared  [-]  the perceptual roughness, squared
/// in    HalfCosine        [-]  the surface orientation against the half-direction
/// out   Distribution      [-]  never negative; unbounded above as the roughness vanishes
/// note  📐 GGX rather than Beckmann, which is `18` §4's own choice and is the better fit to the long tail real
///        surfaces carry. The tail is what makes a rough metal read as metal rather than as a matte solid, and
///        it is precisely what a Gaussian distribution loses.
/// note  🔴 The denominator's base is squared **after** being formed, not formed from a squared cosine. The two
///        differ by the order of one subtraction, and the subtraction is of two nearly equal magnitudes at
///        grazing angles — where every silhouette is.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectDistributionGGX(Real64 RoughnessSquared, Real64 HalfCosine)
{
    const Real64 Parameter = RoughnessSquared < DistributionParameterFloor()
                           ? DistributionParameterFloor()
                           : RoughnessSquared;

    const Real64 Cosine  = BoundedMagnitude(HalfCosine, 0.0, 1.0);
    const Real64 Squared = Parameter * Parameter;
    const Real64 Denom   = Cosine * Cosine * (Squared - 1.0) + 1.0;

    if (Denom <= 0.0)
    {
        return 0.0;
    }

    return Squared / (Pi * Denom * Denom);
}

/// 🧩 The GGX distribution stretched along the tangent by a declared anisotropy.
/// in    RoughnessX   [-]  the distribution parameter along the tangent
/// in    RoughnessY  [-]  and across it
/// in    TangentCosine    [-]  the half-direction's component along the tangent
/// in    BitangentCosine  [-]  its component across
/// in    HalfCosine       [-]  its component along the orientation
/// note  📐 The isotropic form is this one with the two parameters equal, and it is written apart rather than as
///        a call into this because the isotropic denominator collapses to one squared term. Writing the
///        isotropic case through the anisotropic form costs two divisions at every pixel of every rotation for
///        the seven of `18` §3's eight selections that are isotropic.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectDistributionAnisotropic(Real64 RoughnessX,
                                                   Real64 RoughnessY,
                                                   Real64 TangentCosine,
                                                   Real64 BitangentCosine,
                                                   Real64 HalfCosine)
{
    const Real64 X  = RoughnessX  < DistributionParameterFloor()
                        ? DistributionParameterFloor() : RoughnessX;
    const Real64 Y = RoughnessY < DistributionParameterFloor()
                        ? DistributionParameterFloor() : RoughnessY;

    const Real64 ScaledTangent   = TangentCosine   / X;
    const Real64 ScaledBitangent = BitangentCosine / Y;

    const Real64 Denom = ScaledTangent * ScaledTangent
                       + ScaledBitangent * ScaledBitangent
                       + HalfCosine * HalfCosine;

    if (Denom <= 0.0)
    {
        return 0.0;
    }

    return 1.0 / (Pi * X * Y * Denom * Denom);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE ATTENUATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The height-correlated Smith attenuation, already divided by the four cosines the reflectance carries.
/// in    RoughnessSquared  [-]  the distribution parameter
/// in    ViewCosine        [-]  the orientation against the view direction
/// in    LightCosine       [-]  the orientation against the incidence direction
/// out   Visibility        [-]  the attenuation over `4·NoV·NoL`; never negative
/// note  📐 Height-correlated, per `18` §4. The uncorrelated form treats the shadowing and the masking as
///        independent events and over-darkens at grazing angles by a factor that grows with roughness — which
///        is a rough surface whose silhouette is darker than its interior for no physical reason.
/// note  🔴 The four-cosine divisor is folded **in** rather than left to the caller. It cancels against the two
///        radicals below, so folding it here is one division instead of one division and a near-cancellation —
///        and a caller that forgot it would produce a surface that is correct only where both cosines are unity.
/// note  📐 Reciprocal in its two cosines by construction, which `ParityRunner` compares for: the attenuation of
///        a path does not depend on which end the light entered from, and a lapse there is a surface that is
///        brighter seen from one side than from the other.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectVisibilitySmith(Real64 RoughnessSquared, Real64 ViewCosine, Real64 LightCosine)
{
    const Real64 Parameter = RoughnessSquared < DistributionParameterFloor()
                           ? DistributionParameterFloor()
                           : RoughnessSquared;

    const Real64 View  = BoundedMagnitude(ViewCosine,  0.0, 1.0);
    const Real64 Light = BoundedMagnitude(LightCosine, 0.0, 1.0);

    if (View <= 0.0 || Light <= 0.0)
    {
        return 0.0;
    }

    const Real64 Squared = Parameter * Parameter;

    const Real64 ViewTerm  = Light * SquareRoot(View  * View  * (1.0 - Squared) + Squared);
    const Real64 LightTerm = View  * SquareRoot(Light * Light * (1.0 - Squared) + Squared);

    const Real64 Accumulated = ViewTerm + LightTerm;

    if (Accumulated <= 0.0)
    {
        return 0.0;
    }

    return 0.5 / Accumulated;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE FRESNEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Schlick's approximation of the Fresnel term, per component.
/// in    NormalIncidence  [-]  the reflectance at normal incidence, from `18` §2 channels 2 and 4
/// in    Cosine           [-]  the incidence against the half-direction
/// out   Reflectance      [-]  the normal-incidence magnitude at unit cosine, unity at grazing
/// note  📐 The fifth power is written as two squarings and a multiply rather than through the general power.
///        It is exact where the general form is a logarithm and an exponential, and it is the one term of the
///        direct reflectance evaluated at every pixel for every illuminant that reaches it.
/// note  🔴 Exactly the normal-incidence magnitude at a cosine of unity and exactly one at a cosine of nothing —
///        both checked by `ParityRunner`, because a Fresnel that is merely close at normal incidence is a
///        material whose declared reflectance is not the reflectance it shows.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectFresnelSchlick(Real64 NormalIncidence, Real64 Cosine)
{
    const Real64 Departure = 1.0 - BoundedMagnitude(Cosine, 0.0, 1.0);
    const Real64 Squared   = Departure * Departure;

    return NormalIncidence + (1.0 - NormalIncidence) * Squared * Squared * Departure;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DIFFUSE LOBE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The single-scatter diffuse lobe, unitless and already divided by the circle constant.
/// in    Roughness         [-]  the **perceptual** roughness, not its square
/// in    ViewCosine        [-]  the orientation against the view direction
/// in    LightCosine       [-]  the orientation against the incidence direction
/// in    LightViewCosine   [-]  the incidence direction against the view direction
/// out   Lobe              [-]  never negative; reduces to `1/π` at zero roughness
/// note  📐 The improved qualitative Oren–Nayar lobe, whose two coefficients are normalised so the lobe
///        integrates to unity at every roughness rather than gaining energy with it. The azimuth term is the
///        residual of the two directions against the orientation, which avoids the arc cosines and the tangents
///        the original formulation reaches for — and those are the two intrinsics the two toolchains agree on
///        least well.
/// note  🔴 Perceptual roughness rather than its square, and deliberately different from every other term here.
///        `18` §2 declares the channel perceptual and §4 squares it **for the distribution**; the diffuse lobe's
///        own roughness is the surface's slope variance, which is the perceptual figure directly. Squaring it
///        here would make a material's diffuse and specular roughness respond to one slider at two rates.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectDiffuseSingleScatter(Real64 Roughness,
                                                Real64 ViewCosine,
                                                Real64 LightCosine,
                                                Real64 LightViewCosine)
{
    const Real64 Bounded = BoundedMagnitude(Roughness, 0.0, 1.0);
    const Real64 View    = BoundedMagnitude(ViewCosine,  0.0, 1.0);
    const Real64 Light   = BoundedMagnitude(LightCosine, 0.0, 1.0);

    const Real64 Normalisation = Pi + (Pi * 0.5 - 2.0 / 3.0) * Bounded;

    const Real64 Leading  = 1.0 / Normalisation;
    const Real64 Trailing = Bounded / Normalisation;

    // 📐 The residual of the two directions once their components along the orientation are removed. It is
    //    positive where the two lie on one side of the orientation and negative where they oppose, which is
    //    exactly the sign the retro-reflective term carries.
    const Real64 Residual = LightViewCosine - View * Light;

    if (Residual <= 0.0)
    {
        return Leading;
    }

    // 📝 Divided by the greater of the two cosines. Dividing by their product diverges at a grazing angle, and
    //    the divergence is at the silhouette — where a diffuse surface is least expected to be brighter than a
    //    light source.
    const Real64 Greater = View > Light ? View : Light;

    if (Greater <= 0.0)
    {
        return Leading;
    }

    return Leading + Trailing * Residual / Greater;
}

/// 🧩 The multiple-scattering term the single-scatter lobe loses at high roughness.
/// in    AlbedoComponent       [-]  one component of `18` §2 channel 1
/// in    SingleScatterAlbedo   [-]  the lobe's own directional albedo, from `18` §4.1's `.y`
/// out   Recovered             [-]  what to add to the single-scatter lobe, per component
/// note  📐 The saturating series of a surface that scatters what it did not emit: a fraction of what the single
///        lobe lost is returned at the surface's own albedo, and that fraction is itself partly lost, and so on.
///        The closed form is what the series sums to — the same reasoning `28` §2's multiple-scattering surface
///        applies at a different scale.
/// note  🚧 `18` §4 names EON, and this is Oren–Nayar's lobe with the compensation `18` §4.1 already declares for
///        the specular term. The two differ in EON's own fitted coefficients, which are the donor formulation's
///        and are not this document's to transcribe — recorded as an open row rather than guessed at. What the
///        two share is the property `18` §4 actually asks for: energy correctness at high roughness, where
///        Lambert has none.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectDiffuseMultiScatter(Real64 AlbedoComponent, Real64 SingleScatterAlbedo)
{
    const Real64 Albedo = BoundedMagnitude(AlbedoComponent, 0.0, 1.0);
    const Real64 Lost   = 1.0 - BoundedMagnitude(SingleScatterAlbedo, 0.0, 1.0);

    const Real64 Divisor = 1.0 - Albedo * Lost;

    if (Divisor <= 0.0)
    {
        return 0.0;
    }

    return Albedo * Lost / (Pi * Divisor);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE SHEEN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The Charlie distribution — the fibre lobe `18` §3's Cloth selection reads.
/// in    SheenRoughness  [-]  `18` §2 channel 15; the width of the lobe
/// in    HalfCosine      [-]  the orientation against the half-direction
/// note  📐 Charlie rather than a second GGX, because a fibrous surface's response rises toward grazing rather
///        than falling away from the mirror direction. A GGX sheen produces a highlight where the light is,
///        which is the one place a velvet does not have one.
/// cost  🚩
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectSheenDistributionCharlie(Real64 SheenRoughness, Real64 HalfCosine)
{
    const Real64 Bounded = SheenRoughness < DistributionParameterFloor()
                         ? DistributionParameterFloor()
                         : BoundedMagnitude(SheenRoughness, 0.0, 1.0);

    const Real64 Cosine    = BoundedMagnitude(HalfCosine, 0.0, 1.0);
    const Real64 SineSquared = 1.0 - Cosine * Cosine;
    const Real64 Reciprocal  = 1.0 / Bounded;

    return (2.0 + Reciprocal) * Power(SineSquared < 0.0 ? 0.0 : SineSquared, Reciprocal * 0.5) / (2.0 * Pi);
}

/// 🧩 The neutral attenuation the fibre lobe is paired with.
/// note  📝 Deliberately not Smith. The Smith attenuation is derived from the same slope distribution its
///        distribution is, and Charlie's has no closed Smith form; the neutral pairing is the donor
///        formulation's own and is what keeps the cloth response bounded at grazing without inventing one.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectSheenVisibility(Real64 ViewCosine, Real64 LightCosine)
{
    const Real64 View  = BoundedMagnitude(ViewCosine,  0.0, 1.0);
    const Real64 Light = BoundedMagnitude(LightCosine, 0.0, 1.0);

    const Real64 Divisor = 4.0 * (View + Light - View * Light);

    if (Divisor <= 0.0)
    {
        return 0.0;
    }

    return 1.0 / Divisor;
}

//------------------------------------------------------------------------------------------------------------------------
//                                            THE DIRECTIONAL-ALBEDO LOOKUP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a declared view angle and roughness sit on `18` §4.1's lookup.
/// in    ViewCosine        [-]  the orientation against the view direction
/// in    Roughness         [-]  perceptual
/// out   CoordinateX   [-]  the view cosine, linear
/// out   CoordinateY  [-]  the roughness, biased toward the smooth end
/// note  📐 The roughness axis carries the **square root** of the roughness, because the split-sum terms vary
///        fastest where the surface is nearly smooth and barely at all where it is fully rough. A linear axis
///        spends most of its texels on the half of the domain that is almost constant, and the artist meets that
///        as a metal whose highlight steps rather than sweeps as they drag the slider through its first tenth.
/// note  🔴 The pair below and its inverse are one mapping written twice, and that is exactly why they sit in
///        `Shared/`: the lookup is derived on the host and sampled on the device, so the mapping crosses the
///        toolchain seam in both directions. A derivation that placed its samples through one arrangement and a
///        sample that read them through another is wrong by half a texel everywhere — uniformly, so nothing
///        looks broken and every metal is simply a little too dark.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectAlbedoCoordinate(Real64 ViewCosine,
                                          Real64 Roughness,
                                          SLATE_OUT(Real64) CoordinateX,
                                          SLATE_OUT(Real64) CoordinateY)
{
    CoordinateX  = BoundedMagnitude(ViewCosine, 0.0, 1.0);
    CoordinateY = SquareRoot(BoundedMagnitude(Roughness, 0.0, 1.0));
}

/// 🧩 The inverse — what one texel centre of the lookup stands for.
/// note  📝 Declared beside the forward mapping so that an amendment to either is made with the other in view.
///        They are a pair and neither is correct alone.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED void ProjectAlbedoParameter(Real64 CoordinateX,
                                         Real64 CoordinateY,
                                         SLATE_OUT(Real64) ViewCosine,
                                         SLATE_OUT(Real64) Roughness)
{
    ViewCosine = BoundedMagnitude(CoordinateX, 0.0, 1.0);

    const Real64 Bounded = BoundedMagnitude(CoordinateY, 0.0, 1.0);

    Roughness = Bounded * Bounded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE COMPENSATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The factor multiple-scatter compensation applies to the specular term.
/// in    NormalIncidence      [-]  one component of the reflectance at normal incidence
/// in    SingleScatterAlbedo  [-]  the lookup's `.y`, at this view angle and roughness
/// out   Compensation         [-]  unity where nothing was lost; above unity where it was
/// note  🔴 `18` §9's gate: multi-scatter compensation is applied **wherever GGX is**, which is every one of
///        `18` §3's selections but the last two. Single-scatter GGX loses energy at high roughness and the loss
///        reads as rough metal being too dark — which an artist corrects by raising an albedo that was already
///        correct, and the correction is then wrong at every other roughness.
/// cost  ✔️
/// tag   shared, parity, nonallocating, nonthrowing
SLATE_SHARED Real64 ProjectMultiScatterCompensation(Real64 NormalIncidence, Real64 SingleScatterAlbedo)
{
    const Real64 Albedo = BoundedMagnitude(SingleScatterAlbedo, 0.0, 1.0);

    if (Albedo <= 0.0)
    {
        return 1.0;
    }

    return 1.0 + NormalIncidence * (1.0 / Albedo - 1.0);
}

}   // namespace Slate
