//============================================================================================================================================
//                                                        REFLECTANCEINTEGRATOR.H
//============================================================================================================================================
// 🧩 `18` — attributes reconstructed rather than read, channels resolved rather than walked, and one target written at every pixel.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/OcclusionProjection.slang.h"
#include "Shared/ReflectanceProjection.slang.h"
#include "SlateCompute/Compute/AnalyticProjection/Api/AnalyticProjection.h"
#include "SlateCompute/Compute/AtmosphereIntegrator/Api/AtmosphereIntegrator.h"
#include "SlateCompute/Compute/OcclusionProjection/Api/OcclusionProjection.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATTRIBUTE RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one pixel's attributes reconstruct to, from identity and pixel position alone.
/// note  🔴 `18` §1 and `16` §4: **nothing is read from a wide attribute target**, because `16` deliberately did
///        not write one. Every value below is reconstructed from the partition identity, the triangle index and
///        the pixel position — which is the whole reason the visibility target is two integers rather than a
///        sheaf of surfaces multiplied by the display extent.
/// note  🔴 The tangent basis is **interpolated** here and **derived** in `38` §4. `00` §10 conflict 39 splits
///        the two deliberately: handedness must be stored per vertex because a domain that mirrors across a seam
///        inverts it, and an imported basis must be retained rather than recomputed — neither is expressible per
///        pixel. What is per pixel is the interpolation and the re-orthonormalisation against the interpolated
///        perpendicular, and that is what this holds.
/// tag   nonallocating, nonthrowing
struct ReconstructedSurface
{
    DocumentPosition  Position          = {};                  // [mm] - the shaded position, in document space
    SurfaceDirection  Orientation       = {};                  // [-]  - interpolated and renormalised
    TangentBasis      Basis             = {};                  // [-]  - interpolated; absent where the domain is
    double            DomainX       = 0.0;                 // [-]  - the domain's first axis
    double            DomainY      = 0.0;                 // [-]  - its second
    double            DomainGradient[4] = { 0.0, 0.0, 0.0, 0.0 };   // [-] - ∂along/∂x, ∂along/∂y, ∂across/∂x, ∂across/∂y
    double            Weights[3]        = { 0.0, 0.0, 0.0 };   // [-]  - barycentric, summing to one
    bool              BasisDeclared     = false;               // [-]  - false where the domain is degenerate
    bool              Reconstructed     = false;               // [-]  - a surface resolved at all
};

/// 🧩 One triangle's three corners, as the reconstruction reads them.
/// note  📝 Supplied as a value rather than fetched through a topology reference, because the same routine
///        serves the device dispatch — which has a resident span and no `TopologyStructure` at all — and the
///        host preview, which has the structure and no span. One shape, two suppliers.
/// tag   nonallocating, nonthrowing
struct ReconstructionTriangle
{
    DocumentPosition  Position[3]    = {};   // [mm] - in document space, already placed
    SurfaceDirection  Orientation[3] = {};   // [-]  - per corner, as `38` derived or retained them
    TangentBasis      Basis[3]       = {};   // [-]  - per corner
    DomainCoordinate  Domain[3]      = {};   // [-]  - per corner
};

/// 🧩 Reconstructs one pixel's attributes from its triangle and the ray that reached it.
/// in    Triangle    [-]   the three corners
/// in    Origin      [mm]  the ray's origin, in document space
/// in    DirectionX  [-]   unit, in document space
/// in    DirectionY  [-]
/// in    DirectionZ  [-]
/// out   Reconstructed [-] an unreconstructed surface where the ray misses the triangle's plane
/// note  🔴 Screen-space derivatives are computed **analytically** from the triangle's own gradients and never
///        by finite differencing across lanes — `18` §1 and §9's second gate. A material's pixel list is
///        spatially scattered, so neighbouring lanes are not neighbouring pixels; differencing across them
///        produces texture filtering that is wrong precisely at material boundaries, which is where every
///        artist looks first.
/// note  🔴 Where the domain is **degenerate** — a chart of no area, a seam vertex — the basis is marked absent
///        rather than orthonormalised from the orientation. `18` §1.1: a substitute is a fabricated value, which
///        `24` §2 rejects for transfer and which is no better here; `ChannelsSampled` below then withholds the
///        perturbation channels, exactly as `18` §3's unread-channel rule already requires.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
ReconstructedSurface ReconstructSurface(const ReconstructionTriangle& Triangle,
                                        DocumentPosition              Origin,
                                        double                        DirectionX,
                                        double                        DirectionY,
                                        double                        DirectionZ);

//------------------------------------------------------------------------------------------------------------------------
//                                             THE DIRECTIONAL-ALBEDO LOOKUP
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `18` §4.1's lookup — three magnitudes over view angle and roughness, derived once and sampled thereafter.
/// note  🔴 Declared here rather than reusing `28`'s `ResidentSurface`. That surface is a half-precision **colour**
///        surface carrying `28`'s own axis clamping; these three components are not a colour and share no space —
///        the split-sum scale, the single-scatter directional albedo and the Charlie albedo answer three different
///        questions. One spelling covering both is the defect `00` §8 exists to refuse, and it would make `18`
///        inherit an atmosphere parameterisation for a surface the atmosphere never reads.
/// note  🔴 The coordinate mapping is `Shared/ReflectanceProjection.slang.h`'s and is **not** written again here.
///        The lookup is derived on the host and sampled on the device, so the mapping crosses the toolchain seam
///        in both directions; a derivation placing its samples through one arrangement and a sample reading them
///        through another is wrong by half a texel everywhere — uniformly, so every metal is simply a little dark.
/// tag   owning
class DirectionalAlbedoSurface
{
public:

    static constexpr std::uint32_t ComponentCount = 3u;   // [-] - scale, single-scatter albedo, Charlie albedo

    /// 🧩 Sizes the lookup and clears it.
    /// out   Result  [-]  refuses with ContentUnsupported for an extent of nothing on either axis
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructDirectionalAlbedoSurface(std::uint32_t Width, std::uint32_t Height);

    /// 🧩 Writes one texel's three components.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Declare(std::uint32_t X, std::uint32_t Y, double Scale, double SingleScatter, double Charlie);

    /// 🧩 Samples the three components at a declared coordinate, bilinearly, clamped on both axes.
    /// note  📝 Clamped rather than wrapped. Neither axis is periodic — a view cosine of one is normal incidence
    ///        and a roughness of one is fully rough, and a wrapped read at either end returns the opposite end.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Sample(double  CoordinateX,
                double  CoordinateY,
                double& Scale,
                double& SingleScatter,
                double& Charlie) const;

    std::uint32_t Width() const;
    std::uint32_t Height() const;
    std::uint64_t ResidentBytes() const;
    bool          Constructed() const;

private:

    std::vector<float>  Components    = {};   // [-] - interleaved, ComponentCount per texel
    std::uint32_t       SpannedX  = 0u;   // [px] - view cosine
    std::uint32_t       SpannedY = 0u;   // [px] - roughness, square-root biased
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE CHANNEL SET
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The twenty channels resolved at one pixel, and which of them were sampled at all.
/// note  🔴 `18` §8: a channel value is **read resolved** and the dispatch never walks a layer sequence. `20`'s
///        tile promotion resolves `42`'s declaration against `56`'s sequence once per tile per level, not once
///        per pixel per rotation — a dispatch that walked the sequence would pay its depth at every pixel of
///        every rotation, and the artist's thirtieth layer would cost as much as their first thirty combined.
/// note  📝 A scalar and a colour share one storage run, three components wide, because `42` declares no channel
///        wider than three. A per-measure union would be a second declaration of what `42` already declares.
/// tag   nonallocating, nonthrowing
struct ResolvedChannelSet
{
    double         Component[static_cast<std::size_t>(ChannelSubject::ChannelCount)][3] = {};
    std::uint32_t  SampledMask = 0u;   // [-] - one bit per channel actually sampled
};

/// 🧩 Whether one channel was sampled rather than defaulted.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool ChannelSampledIn(const ResolvedChannelSet& Resolved, ChannelSubject Channel)
{
    return Channel != ChannelSubject::ChannelCount
        && (Resolved.SampledMask & (1u << static_cast<std::uint32_t>(Channel))) != 0u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TWO TERMS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One illuminant's contribution at one pixel, and the two occlusions that attenuated it.
/// tag   nonallocating, nonthrowing
struct DirectContribution
{
    double  Component[3]     = { 0.0, 0.0, 0.0 };   // [-] - working-space radiance
    double  Visibility       = 1.0;                 // [-] - `60`'s direct term at this pixel
    bool    Unattenuated     = false;               // [-] - the packed word could not carry it — `60` §3.1
};

/// 🧩 The ambient contribution at one pixel, diffuse and specular apart.
/// note  🔴 `18` §5: sky-view radiance is convolved against the cosine lobe for the diffuse ambient and sampled
///        at the reflection direction for the specular. **Both** are attenuated by channel 6 and by `60`'s
///        resolved occlusion, and the two occlusions **multiply** rather than one superseding the other —
///        channel 6 is detail the topology does not carry and `60` is contact the topology does carry.
/// note  🔴 There is no global illumination in Slate, and this is not a placeholder for one — `18` §5. The
///        source is `28` when the atmosphere is enabled and the constant floor when it is not; beyond that there
///        is no indirect light, and a material that appears to need some is lit incorrectly rather than
///        under-featured. This is substitution point one of `00` §5.1's four.
/// tag   nonallocating, nonthrowing
struct AmbientContribution
{
    double  DiffuseComponent[3]  = { 0.0, 0.0, 0.0 };   // [-]
    double  SpecularComponent[3] = { 0.0, 0.0, 0.0 };   // [-]
    double  EmissiveComponent[3] = { 0.0, 0.0, 0.0 };   // [-] - 🔴 never attenuated; channel 7 is emitted, not lit
    double  Attenuation          = 1.0;                 // [-] - channel 6 × `60`'s resolved term
};

// 🔴 The two lit members arrive **already attenuated** and `Attenuation` is carried beside them for `86` rather
//    than for the caller to apply a second time. The emissive member is outside that product entirely — a surface
//    that emits does not emit less for standing in a corner, and attenuating it is the one occlusion defect an
//    artist cannot correct by adjusting occlusion.

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE INTEGRATOR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `18` — the shading dispatch, the directional-albedo lookup it compensates through, and `RadianceSurface`.
/// note  🔴 `18` §6: this **produces** `RadianceSurface` and writes its whole extent, the unoccupied class
///        included; `62` and `30` amend it afterwards, in that order, as `08` §2 declares. Nothing here reads
///        what was in the target before.
/// note  🔴 `18` §5.1's unoccupied class is dispatched **like any other** and reconstructs no attribute and
///        reads no material. Without it nothing in the entire schedule writes the background — every other
///        dispatch is per material over pixels that resolved to a surface, and an unoccupied pixel resolved to
///        none. The image would carry a hole exactly where the sky belongs, filled with whatever the rotation
///        slot held previously.
/// note  ⚠️ 🚧 `18` §10 records that the channel packing layout **does not exist in any source document** — no
///        bit depths, no slot assignment, no packing order — and refuses to invent one. `ResolvedChannelSet`
///        therefore carries three components per channel and `ResolveChannels` reads through `70`'s declared
///        placements, so the arrangement is the caller's and the row stays open.
/// tag   owning
class ReflectanceIntegrator
{
public:

    /// 🧩 Contributes `08` §3 ④'s recording.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Contribute(RenderSchedule& Schedule) const;

    /// 🧩 Derives `18` §4.1's directional-albedo lookup, once.
    /// in    Rule     [-]  a derived quadrature rule; the hemisphere integrals are taken against it
    /// out   Result  [-]  refuses with ContentUnsupported before the rule is derived
    /// post  the lookup stands and is sampled by every specular and cloth evaluation thereafter
    /// note  🔴 One resident lookup, parameterised by view angle and roughness, whose three components are the
    ///        split-sum scale, the single-scatter directional albedo that drives §4's compensation, and the
    ///        Charlie directional albedo for the cloth selection. Derived once at bring-up rather than per
    ///        rotation: it depends on nothing a document holds, and re-deriving it per rotation would integrate
    ///        a hemisphere per texel per rotation for a surface that never changes.
    /// note  📝 Sampled through `Shared/`'s own mapping, in both directions. The lookup is derived on the host
    ///        and sampled on the device, so the mapping crosses the toolchain seam twice and lives in `Shared/`
    ///        for the reason `28`'s three surface parameterisations do.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> DeriveDirectionalAlbedo(const QuadratureRule& Rule);

    /// 🧩 Resolves the channel set one material declares at one domain position.
    /// in    Declared    [-]  the material, from `42`
    /// in    Resolving   [-]  `70`'s resolution over the surface's layer sequence
    /// in    Content     [-]  the surface's layer sequence
    /// in    Placements  [-]  where each channel sits among the resolved components
    /// in    Reconstructed [-] the pixel's reconstructed attributes
    /// in    Tolerance   [-]  the flattening tolerance for the level being shaded
    /// out   Result     [-]  carries whatever the resolution rejected
    /// note  🔴 `18` §9's fifth gate: each selection declares its channels and **unread channels are not
    ///        sampled**. The mask is consulted before the sample and not after it, so an unread channel costs
    ///        nothing rather than costing a sample that is then discarded.
    /// note  🔴 A channel a material declares but does not sample resolves to its **declared default** and never
    ///        to zero — `42` §2. An occlusion channel defaulted to zero is a black surface and a transmission
    ///        channel defaulted to zero is an opaque one; only one of those is right, which is exactly why the
    ///        default is declared rather than assumed.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<ResolvedChannelSet> ResolveChannels(const MaterialSpecification&          Declared,
                                                const AnalyticProjection&             Resolving,
                                                const SurfaceLayerSequence&           Content,
                                                const std::vector<ChannelPlacement>&  Placements,
                                                const ReconstructedSurface&           Reconstructed,
                                                double                                Tolerance) const;

    /// 🧩 Integrates one illuminant's direct contribution at one pixel.
    /// in    Selected     [-]  the material's reflectance selection
    /// in    Resolved     [-]  the channel set
    /// in    Reconstructed[-]  the pixel's attributes
    /// in    Incidence    [-]  `44`'s projection of this illuminant at this position
    /// in    Radiance     [-]  the illuminant's colour, already in the working space
    /// in    Visibility   [-]  `60`'s direct term for this illuminant at this pixel
    /// in    ViewX        [-]  toward the camera, unit
    /// in    ViewY        [-]
    /// in    ViewZ        [-]
    /// out   Contribution [-]  working-space radiance, already attenuated
    /// note  🔴 Every term is `Shared/`'s and none is written here. `82` §5 resolves the same surface on the
    ///        host and `00` §11 gates the agreement at Tier B — a second implementation of the distribution or
    ///        the attenuation is a preview that converges to a different image than the workspace does.
    /// note  🔴 Multi-scatter compensation is applied **wherever GGX is** — `18` §9. Single-scatter GGX loses
    ///        energy at high roughness and the loss reads as rough metal being too dark, which the artist
    ///        corrects by raising an albedo that was already correct.
    /// note  ⚠️ Neither occlusion source touches the direct term's own attenuation beyond `60`'s per-illuminant
    ///        visibility — `18` §7. Occlusion in the direct term is already resolved by shadowing, and applying
    ///        the ambient term here would darken a lit surface for standing near another one.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    DirectContribution IntegrateDirect(ReflectanceSelection        Selected,
                                       const ResolvedChannelSet&   Resolved,
                                       const ReconstructedSurface& Reconstructed,
                                       const IncidenceProjection&  Incidence,
                                       const ColourSpecification&  Radiance,
                                       double                      Visibility,
                                       double                      ViewX,
                                       double                      ViewY,
                                       double                      ViewZ) const;

    /// 🧩 Integrates the ambient contribution at one pixel.
    /// in    Atmosphere       [-]  `28`; delivers sky-view radiance, or the constant floor where disabled
    /// in    ResolvedOcclusion[-]  `60`'s ambient term at this pixel, already upsampled
    /// out   Result          [-]  carries `28`'s refusal where the atmosphere stands and no surface does
    /// note  🔴 The diffuse ambient reads `28`'s **cosine-convolved irradiance** rather than integrating the
    ///        hemisphere here. `28` §5 derives the convolution when the sky-view surface rebuilds and never per
    ///        pixel; a hemisphere integral evaluated per shaded pixel is the thing the whole precomputation
    ///        exists to avoid, and it would be evaluated at every pixel of every rotation rather than at every
    ///        rebuild.
    /// note  🔴 Channel 6 and `60`'s resolved term **multiply** — `18` §5 and `60` §2, from both sides.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<AmbientContribution> IntegrateAmbient(ReflectanceSelection        Selected,
                                                  const ResolvedChannelSet&   Resolved,
                                                  const ReconstructedSurface& Reconstructed,
                                                  const AtmosphereIntegrator& Atmosphere,
                                                  double                      ResolvedOcclusion,
                                                  double                      ViewX,
                                                  double                      ViewY,
                                                  double                      ViewZ) const;

    /// 🧩 Resolves the unoccupied class at one pixel — `18` §5.1.
    /// in    Atmosphere  [-]  the same two sources §5 already declares
    /// in    ViewX       [-]  the view direction, unit, in the atmosphere-local frame
    /// in    ViewY       [-]
    /// in    ViewZ       [-]
    /// out   Result     [-]  carries `28`'s refusal
    /// note  🔴 Reconstructs **no attribute** and reads **no material**. It samples one source and writes it,
    ///        and it exists because every other dispatch is per material over pixels that resolved to a surface.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> IntegrateUnoccupied(const AtmosphereIntegrator& Atmosphere,
                                      double ViewX, double ViewY, double ViewZ,
                                      double& Red, double& Green, double& Blue) const;

    /// 🧩 The directional-albedo lookup, for whoever uploads it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const DirectionalAlbedoSurface& DirectionalAlbedo() const;

    /// 🧩 The lookup's three components at one view angle and roughness.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void SampleDirectionalAlbedo(double  ViewCosine,
                                 double  Roughness,
                                 double& SplitSumScale,
                                 double& SingleScatterAlbedo,
                                 double& CharlieAlbedo) const;

    bool AlbedoDerived() const;

private:

    // 📝 🚧 `18` §10 leaves the lookup's own extent unstated along with the packing. Sixty-four square is where
    //    the split-sum terms stop changing at the single precision `DirectionalAlbedoSurface` stores at, and the
    //    whole lookup is beneath the granularity of any budget that would object to it.
    static constexpr std::uint32_t AlbedoExtentX  = 64u;   // [px] - view cosine
    static constexpr std::uint32_t AlbedoExtentY = 64u;   // [px] - roughness, square-root biased
    static constexpr std::uint32_t AlbedoSampleCount  = 256u;  // [-]  - hemisphere samples per texel

    DirectionalAlbedoSurface  AlbedoLookup;              // [-] - `18` §4.1's three components
    bool                      LookupDerived = false;     // [-] - DeriveDirectionalAlbedo has delivered
};

// 📐 🔴 The output is `RadianceSurface`, RGBA16F, at Tier D — `18` §6. Half precision is correct here rather
//    than conceded: perceptual output carries no numeric guarantee, which is what Tier D means. `00` §3's
//    transitivity rule then forbids anything downstream from claiming better, which is `18` §9's last gate.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
