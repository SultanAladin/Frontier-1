//============================================================================================================================================
//                                                        MATERIALSPECIFICATION.H
//============================================================================================================================================
// 🧩 What a surface's channels are, where each value comes from, and which partition resolves to which owner.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TWENTY CHANNELS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The twenty channels `18` §2 declares, in that order.
/// note  ⚠️ `18` §2's channel 1 is spelled `AlbedoColour` and channel 4 `NormalIncidenceReflectance` here, per
///        `00` §8.2's substitutions — `Base` is banned as a prefix, and a bare `Reflectance` collides with the
///        measure of the same name in §3 below.
/// note  ⚠️ Channel 19 is spelled `RefractionRatio` rather than an index of refraction. `Index` is a closed role
///        suffix meaning a slot index, and a refractive index is a ratio of propagation speeds — which states
///        the mechanism and avoids reading as a index.
/// tag   guarantee
enum class ChannelSubject : std::uint32_t
{
    AlbedoColour               = 0u,    // [-]  - diffuse albedo, or conductor reflectance at normal incidence
    Metallic                   = 1u,    // [-]  - interpolates dielectric toward conductor
    Roughness                  = 2u,    // [-]  - perceptual; squared to the distribution parameter
    NormalIncidenceReflectance = 3u,    // [-]  - dielectric reflectance at normal incidence
    SurfaceOrientation         = 4u,    // [-]  - tangent-space perturbation
    AmbientOcclusion           = 5u,    // [-]  - scalar; no bent orientation exists — `18` §7
    Emission                   = 6u,    // [-]  - radiance emitted independent of incidence
    Opacity                    = 7u,    // [-]  - coverage or transparency, by the model that reads it
    Anisotropy                 = 8u,    // [-]  - distribution stretch along the tangent
    AnisotropyDirection        = 9u,    // [-]  - tangent-space direction of the stretch
    ClearCoat                  = 10u,   // [-]  - strength of a second specular layer
    ClearCoatRoughness         = 11u,   // [-]  - roughness of that layer
    ClearCoatOrientation       = 12u,   // [-]  - independent orientation for the coat
    SheenColour                = 13u,   // [-]  - retro-reflective fibre response
    SheenRoughness             = 14u,   // [-]  - width of the fibre lobe
    SubsurfaceColour           = 15u,   // [-]  - transmitted tint
    SubsurfaceThickness        = 16u,   // [mm] - path length driving transmission falloff
    Transmission               = 17u,   // [-]  - fraction refracted rather than reflected
    RefractionRatio            = 18u,   // [-]  - refractive index; drives Fresnel and the refracted direction
    // 🚧 ChannelSubject::Displacement is declared unconsumed by standard reflectance masks (geometric or reconstruction-time offset).
    Displacement               = 19u,   // [mm] - geometric offset along the orientation
    ChannelCount               = 20u    // [-]  - the closed count, never a channel
};

//------------------------------------------------------------------------------------------------------------------------
//                                                THE EIGHT REFLECTANCES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One of `18` §3's eight reflectance specifications, selected per material.
/// note  ⚠️ `00` §8.2's substitution: `Model` is banned structurally, so what `18` §3 calls a shading model is a
///        `ReflectanceSpecification` and a material's choice of one is a `ReflectanceSelection`.
/// tag   guarantee
enum class ReflectanceSelection : std::uint32_t
{
    Standard          = 0u,   // [-] - channels 1–8
    Anisotropic       = 1u,   // [-] - Standard plus 9, 10
    ClearCoated       = 2u,   // [-] - Standard plus 11, 12, 13
    Cloth             = 3u,   // [-] - 1, 3, 5, 6, 8, 14, 15
    Subsurface        = 4u,   // [-] - Standard plus 16, 17
    Transmissive      = 5u,   // [-] - Standard plus 18, 19
    EmissiveOnly      = 6u,   // [-] - 7, 8
    Unlit             = 7u,   // [-] - 1, 8
    ReflectanceCount  = 8u    // [-] - the closed count, never a selection
};

/// 🧩 Whether one reflectance selection reads one channel.
/// in    Selected  [-]  the material's selection
/// in    Channel   [-]  the channel in question
/// out   Consumed  [-]  true where `18` §3's inventory reads it
/// note  🔴 `18` §9's gate: each selection declares its channels and unread channels are **not sampled**. This
///        predicate is what a shading dispatch asks rather than sampling twenty channels and discarding fourteen.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool ChannelConsumed(ReflectanceSelection Selected, ChannelSubject Channel);

//------------------------------------------------------------------------------------------------------------------------
//                                                 SOURCE AND MEASURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Where a channel's value comes from.
/// note  ⚠️ Layered and Analytic are not alternatives at the material level — `56` is where they interleave. The
///        distinction survives to here only because `70` resolves one of them and `20` transfers the other.
/// tag   guarantee
enum class ChannelSource : std::uint32_t
{
    Constant    = 0u,   // [-] - one value over the whole surface
    Layered     = 1u,   // [-] - `56`'s layer sequence for this channel
    Analytic    = 2u,   // [-] - `54` tiling or `52` outlines, resolved at promotion by `70`
    Imported    = 3u,   // [-] - an image from `50`, addressed through the domain
    Absent      = 4u,   // [-] - the channel's declared default
    SourceCount = 5u    // [-] - the closed count, never a source
};

/// 🧩 What a channel's value measures, which fixes whether `36` §4 converts it.
/// note  🔴 `36` §4 reads this declaration **and nothing else**. Not the image's encoding, not its channel count,
///        and not its file name. A roughness value put through a transfer function is a wrong number that still
///        looks like a plausible surface, which is why the mistake survives review.
/// tag   guarantee
enum class ChannelMeasure : std::uint32_t
{
    Reflectance  = 0u,   // [-] - colour-carrying, bounded to the unit interval
    Emission     = 1u,   // [-] - colour-carrying, unbounded above
    Scalar       = 2u,   // [-] - not colour; a declared interval
    Direction    = 3u,   // [-] - not colour; unit length
    Registration    = 4u,   // [-] - not colour; an identity
    MeasureCount = 5u    // [-] - the closed count, never a measure
};

/// 🧩 Whether a measure carries colour and is therefore converted at intake.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool MeasureCarriesColour(ChannelMeasure Measured)
{
    return Measured == ChannelMeasure::Reflectance || Measured == ChannelMeasure::Emission;
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE CHANNEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One channel as a material declares it — its source, its measure, and the default an absence resolves to.
/// note  🔴 `Absent` is **not zero**. A material with no occlusion channel is fully unoccluded, and one with no transmission
///       channel is opaque; both defaults are declared here and neither is the number zero. A channel defaulted to zero produces
///       surfaces that are black or invisible, and the artist reads that as a broken material rather than as a missing declaration.
/// tag   owning
struct ChannelSpecification
{
    ChannelSource        Source          = ChannelSource::Absent;         // [-] - where the value comes from
    ChannelMeasure       Measured        = ChannelMeasure::Scalar;        // [-] - `36` §4 reads only this
    std::uint32_t        SourceIndex     = 0u;                            // [-] - into imported-image/layer source tables
    double               ConstantScalar  = 0.0;                          // [-] - Constant, at a scalar measure
    ColourSpecification  ConstantColour  = {};                           // [-] - Constant, at a colour measure
    double               DefaultScalar   = 0.0;                          // [-] - Absent, at a scalar measure
    ColourSpecification  DefaultColour   = {};                           // [-] - Absent, at a colour measure
    double               LowerMagnitude  = 0.0;                          // [-] - the declared interval
    double               UpperMagnitude  = 1.0;                          // [-] - the declared interval
    bool                 ChannelDeclared = false;                        // [-] - the material declares it at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE MATERIAL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One material — its reflectance selection, its twenty channel declarations, and its cutout threshold.
/// note  🔴 Model selection is per material and never per texel — `42` §5. A per-texel selection is a divergent
///        branch in shading, and `18` reads the selection once per partition instead.
/// note  🔴 A channel declared for a selection the material does not use is **retained**, not discarded — `42` §5.
///        The artist who switches a material from one selection to another and back expects to find their work;
///        discarding on switch is a destructive edit disguised as a settings change.
/// tag   owning
class MaterialSpecification
{
public:

    /// 🧩 Declares which reflectance the material selects.
    /// note  Retains every channel declaration, including those the new selection does not consume.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareReflectance(ReflectanceSelection Selected);

    /// 🧩 Declares one channel.
    /// in    Channel    [-]  which of the twenty
    /// in    Declaring  [-]  its source, measure, default and interval
    /// out   Result    [-]  refuses with ContentUnsupported for an out-of-range channel, for a colour default
    ///                       carrying no space, and for a default outside the declared interval
    /// note  🔴 The default is validated against its own interval here, for `10` §2.2's reason: a default outside
    ///        its bounds is an invalid value presented on every surface that never overrode it.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DeclareChannel(ChannelSubject Channel, const ChannelSpecification& Declaring);

    /// 🧩 Declares the coverage threshold a cutout owner is resolved against.
    /// note  🔴 Per material, never global — `62` §2. A single threshold across a document makes one artist's
    ///        foliage disappear while another's grows a halo, and neither can correct it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareCutoutThreshold(double Threshold);

    /// 🧩 Registers the material as cutout, so `16` §3.1 resolves its coverage at visibility time.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareCutoutRegistration(bool CutoutEnabled);

    ReflectanceSelection        Reflectance() const;
    const ChannelSpecification& Channel(ChannelSubject Subject) const;
    double                      CutoutThreshold() const;
    bool                        CutoutRegistered() const;

    /// 🧩 Whether one channel is both declared and consumed by the selected reflectance.
    /// note  🔴 This is what a dispatch asks. A channel that is declared but unconsumed is not sampled, and one
    ///        that is consumed but undeclared resolves to its declared default rather than to zero.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ChannelSampled(ChannelSubject Subject) const;

    /// 🧩 Whether the channel's value is converted by `36` at intake.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ChannelConverted(ChannelSubject Subject) const;

private:

    static constexpr std::size_t ChannelSpan = static_cast<std::size_t>(ChannelSubject::ChannelCount);

    ChannelSpecification  Declarations[ChannelSpan] = {};                            // [-] - all twenty, retained
    ReflectanceSelection  Selected                  = ReflectanceSelection::Standard;
    double                CoverageThreshold         = 0.5;                           // [-] - `62` §2's per-material
    bool                  CutoutDeclared            = false;                         // [-] - resolved at `16` §3.1
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE MATERIAL INDEX
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every material in the document, addressed by identity and shared by many owners.
/// note  🔴 `42` §6: editing a material changes every owner registered in it, in one transaction. The `56` layer
///        sequence beneath a layered channel belongs to the **surface** and not to the material — two owners
///        sharing a material and each textured differently is the ordinary case.
/// tag   owning
class MaterialIndex
{
public:

    /// 🧩 Declares one material and issues its identity.
    /// in    Named    [-]  what the artist calls it; may be empty
    /// out   Result  [-]  refuses with ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const std::string& Named);

    /// 🧩 One declared material, for reading.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const MaterialSpecification*> Resolve(std::uint32_t MaterialIndex) const;

    /// 🧩 One declared material, for amending.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<MaterialSpecification*> Amend(std::uint32_t MaterialIndex);

    const std::string& DeclaredName(std::uint32_t MaterialIndex) const;
    std::uint32_t      DeclaredCount() const;

private:

    static constexpr std::uint32_t MaterialLimit = 65536u;   // [-] - materials one document may declare

    std::vector<MaterialSpecification>  Declared;       // [-] - by material ordinal
    std::vector<std::string>            DeclaredNames;  // [-] - parallel to it
    std::string                         AbsentName;     // [-] - returned for an undeclared ordinal
};

//------------------------------------------------------------------------------------------------------------------------
//                                             PARTITION RESOLUTION — `00` §10 CONFLICT 15
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one partition identity resolves to — the owner, its material, and the face range it covers.
/// note  🔴 `16` §4.1: a partition identity is **not** an owner identity. `26`'s registration test, `16` §5's
///        classification and `74`'s picking all need the owner and none of them can derive it from what `16`
///        writes. This is the one resolution every consumer reads.
/// tag   nonallocating, nonthrowing
struct ResolvedPartition
{
    OwnerIdentity  Owner        = {};   // [-] - `26` outlining, `18` transform
    std::uint32_t     MaterialIndex = 0u;   // [-] - `18` reflectance and channel selection
    std::uint32_t     FirstFace       = 0u;   // [-] - domain reconstruction at the pixel
    std::uint32_t     FaceCount       = 0u;   // [-] - faces the partition covers
};

/// 🧩 Partition identity to owner, material and face range — a derived projection, never authored.
/// note  🔴 `42` §4: rebuilt when the population changes and **never authored**. Two sources of truth about which
///        owner a partition belongs to would disagree exactly when an owner was added, which is the moment
///        the artist is looking at it.
/// note  🔴 It is a device-resident indexed lookup, never a search. Every consumer reads this one resolution
///        rather than deriving its own — `16` §6's gate.
/// tag   owning
class PartitionResolutionIndex
{
public:

    /// 🧩 Discards every entry, ahead of a rebuild.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

    /// 🧩 Declares one partition's resolution, issuing its identity.
    /// in    Resolving  [-]  the owner, material and face range
    /// out   Result    [-]  refuses with IdentityStale for an undeclared owner, and with ExtentExhausted at
    ///                       the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<PartitionIdentity> Declare(const ResolvedPartition& Resolving);

    /// 🧩 Resolves one partition identity.
    /// out   Result  [-]  refuses with IdentityStale when the generation no longer matches — which is what a
    ///                     re-partition since the identity was taken looks like
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<ResolvedPartition> Resolve(PartitionIdentity Subject) const;

    /// 🧩 The revision the last rebuild advanced to; `70` §2 compares counters against it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision() const;

    std::uint32_t DeclaredCount() const;

private:

    static constexpr std::uint32_t PartitionLimit = 1048576u;   // [-] - partitions one document may hold

    std::vector<ResolvedPartition>  Resolutions;         // [-] - by partition ordinal
    std::uint64_t                   DerivedRevision = 1u; // [-] - advanced by Reclaim
};

}   // namespace Slate
