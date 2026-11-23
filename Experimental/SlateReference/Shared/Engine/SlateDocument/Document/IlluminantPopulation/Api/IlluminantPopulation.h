//============================================================================================================================================
//                                                          ILLUMINANTPOPULATION.H
//============================================================================================================================================
// 🧩 The illuminants `18` integrates — every one an owner, every one with a size, every extent declared not derived.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE FOUR SHAPES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The geometry radiance leaves from — `44` §3's four shapes.
/// note  🔴 Every shape has a **non-zero size**, and the declaration below refuses one that does not. This is not
///        a refinement: `18` §4's reflectance models integrate over a solid extent, and a zero-extent source
///        produces a specular highlight that is either absent or a single aliased pixel depending on the roughness.
/// tag   guarantee
enum class EmissionShape : std::uint32_t
{
    Point       = 0u,   // [-] - a position, with a radius
    Directional = 1u,   // [-] - a direction, with an angular size
    Spot        = 2u,   // [-] - a position, within a cone
    Extended    = 3u,   // [-] - a rectangle or a disc
    ShapeCount  = 4u    // [-] - the closed count, never a shape
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ONE ILLUMINANT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one illuminant declares — `44` §2's table as storage.
/// note  🔴 The extent is a **declared** cutoff, not a threshold discovered from the falloff. A cutoff derived
///        from a magnitude changes when the artist changes the magnitude, so brightening an illuminant would
///        silently enlarge the set of surfaces it lights and `60`'s projection cost with it. Falloff within the
///        extent is physical and is not authored; the two are different decisions.
/// note  🔴 Colour is a `ColourSpecification` carrying its space, or a temperature. Where a temperature is
///        declared it is **retained as the authored value** — `36` §5 — because an artist who set 5600 expects to
///        see 5600 when they return, and a coordinate cannot be inverted back to it exactly.
/// tag   nonallocating, nonthrowing
struct IlluminantSpecification
{
    EmissionShape        Emission            = EmissionShape::Point;   // [-]   - which size fields below are read
    DecomposedTransform  Placement           = {};                     // [mm]  - position and orientation
    double               RadiantIntensity    = 1.0;                    // [-]   - magnitude in the working space
    ColourSpecification  DeclaredColour      = {};                     // [-]   - carries its space, per `36` §1
    double               Temperature         = 0.0;                    // [K]   - retained as authored — `36` §5
    bool                 TemperatureDeclared = false;                  // [-]   - the colour derives from it
    double               ExtentReach         = 1000.0;                 // [mm]  - declared cutoff, never derived
    double               EmissionRadius      = 10.0;                   // [mm]  - Point and Spot; never zero
    double               AngularSize         = 0.53;                   // [deg] - Directional; never zero
    double               ConeAngle           = 45.0;                   // [deg] - Spot; the fully lit interior
    double               ConeSoftness        = 5.0;                    // [deg] - Spot; the attenuating margin
    double               ExtendedWidth       = 100.0;                  // [mm]  - Extended; never zero
    double               ExtendedHeight      = 100.0;                  // [mm]  - Extended; never zero
    bool                 ExtendedElliptical  = false;                  // [-]   - a disc rather than a rectangle
    bool                 OcclusionRegistered   = true;                   // [-]   - whether `60` projects it
    bool                 AtmosphericSource   = false;                  // [-]   - `28` reads exactly one of these
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     INCIDENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Direction, distance and solid extent at one shaded position.
/// note  📐 `SolidExtent` is the solid angle the emission shape subtends from the shaded position, which is what
///        `18` §4's distribution integrates over. It is what makes an area source produce a highlight with width
///        rather than a highlight with an aliased centre.
/// tag   nonallocating, nonthrowing
struct IncidenceProjection
{
    double  DirectionX  = 0.0;   // [-]  - unit, from the shaded position toward the illuminant
    double  DirectionY  = 0.0;   // [-]
    double  DirectionZ  = 0.0;   // [-]
    double  Distance    = 0.0;   // [mm] - zero for a directional shape
    double  SolidExtent = 0.0;   // [sr] - the solid angle subtended; never zero
    double  Attenuation = 0.0;   // [-]  - falloff and cone attenuation; zero beyond the declared extent
};

/// 🧩 Projects one illuminant's incidence at one shaded position.
/// in    Declared  [-]   the illuminant
/// in    Shaded    [mm]  the position being lit, in document space
/// out   Result   [-]   refuses with ContentUnsupported for a shape with no declared size
/// note  ⚠️ An attenuation of zero is a legitimate result and is delivered rather than rejected. The position is
///        outside the declared extent or outside the cone, and `18` multiplies by zero — which costs less than a
///        refusal the integrator would have to branch on.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<IncidenceProjection> ProjectIncidence(const IlluminantSpecification& Declared, DocumentPosition Shaded);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE POPULATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every illuminant in the document, held in identity order.
/// note  🔴 An illuminant is an owner of `10`'s population. It registers in `12`, appears in the outliner,
///        attaches through `AttachmentFollows` and is manipulated by `78` like anything else. An illuminant held
///        outside the population is one the artist cannot select, name, group or undo.
/// note  🔴 Order is by identity and is therefore stable across ticks, runs and machines. `18` integrates in this
///        order and `02` §5's ordered recombination is why: an accumulation in arrival order is a different number
///        each run at Bounded, and the difference is visible as flicker on a surface lit by many sources.
/// note  🔴 Exactly one illuminant may be registered as the atmospheric source. Two suns disagreeing about their
///        direction is a scene where the shadows fall one way and the sky brightens the other.
/// tag   owning
class IlluminantPopulation
{
public:

    /// 🧩 Declares one illuminant against an registered owner.
    /// out   Result  [-]  refuses with IdentityStale for an undeclared identity, with ContentUnsupported for a
    ///                     shape of zero size or a non-positive extent, and with HostDenied for a second
    ///                     atmospheric source
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Declare(OwnerIdentity Subject, const IlluminantSpecification& Declaring);

    /// 🧩 Amends one declared illuminant, validated exactly as a declaration is.
    /// out   Result  [-]  refuses with IdentityStale when the owner declares no illuminant
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Amend(OwnerIdentity Subject, const IlluminantSpecification& Amending);

    /// 🧩 Withdraws one illuminant.
    /// out   Result  [-]  refuses with IdentityStale when the owner declares no illuminant
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(OwnerIdentity Subject);

    /// 🧩 One declared illuminant.
    /// out   Result  [-]  refuses with IdentityStale when the owner declares no illuminant
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<IlluminantSpecification> Resolve(OwnerIdentity Subject) const;

    /// 🧩 One illuminant's colour, projected into a declared space.
    /// in    Working  [-]  the space to express the colour in — `36` §2's working space
    /// out   Result  [-]  refuses with IdentityStale for an unknown owner, and carries `36`'s refusal when a
    ///                     declared temperature lies outside the locus interval
    /// note  📝 A declared temperature is projected here and is **not** written back over the declaration. The
    ///        authored temperature is what the artist edits; the coordinate is what `18` integrates.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<ColourSpecification> ResolveColour(OwnerIdentity Subject, const ColourSpaceSpecification& Working) const;

    /// 🧩 The owner registered as `28`'s atmospheric source.
    /// out   Result  [-]  refuses with ExtentExhausted when none is registered
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<OwnerIdentity> AtmosphericSource() const;

    /// 🧩 Every declared illuminant, in identity order.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const std::vector<OwnerIdentity>& Registered() const;

    /// 🧩 How many illuminants are declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t RegisteredCount() const;

    /// 🧩 The revision the last amendment advanced to; `IlluminantIndex` compares counters against it.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision() const;

private:

    std::size_t   Located(OwnerIdentity Subject) const;
    Deliver<bool> Validate(const IlluminantSpecification& Declaring, OwnerIdentity Subject) const;

    std::vector<OwnerIdentity>         RegisteredOrder;         // [-] - ascending by slot, then by generation
    std::vector<IlluminantSpecification>  Declarations;          // [-] - parallel to it
    std::uint64_t                         DeclaredRevision = 1u; // [-] - advanced by every amendment
};

//------------------------------------------------------------------------------------------------------------------------
//                                              WHICH REACH A PARTITION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One partition's extent in document space, as its producer measured it.
/// note  ⚠️ Declared here rather than shared with `38`'s `ConditionedExtent`. `44` names no `38` edge in its
///        Position block, and `00` §11 gates that every declared edge is a real read — acquiring a dependency to
///        avoid duplicating a two-field structure would be paying a build-order cost to save nothing.
/// tag   nonallocating, nonthrowing
struct PartitionExtent
{
    DocumentPosition  Minimum    = {};   // [mm] - the lower corner
    DocumentPosition  Maximum = {};   // [mm] - the upper corner
};

/// 🧩 Which illuminants each partition may be lit by — `44` §5.
/// note  🔴 `18` integrates this set and not the whole population. Re-derivation is targeted: an illuminant that
///        moved re-derives its own entries, a partition that moved re-derives its own, a camera that moved
///        re-derives nothing, and a radiant intensity that changed re-derives nothing because the extent is
///        declared rather than inferred from it — which is the most common illuminant edit an artist makes.
/// note  🔴 A partition whose reaching set exceeds `IlluminantReachCapacity` truncates, and the truncation is
///        counted so `86` can present it. `60` §3.1 truncates independently at the narrower packed capacity of
///        `DirectOcclusionSurface`, and the excess is integrated unattenuated rather than dropped.
/// tag   owning
class IlluminantIndex
{
public:

    /// 🧩 Derives the whole index against a declared partition set.
    /// in    Illuminants  [-]  the population
    /// in    Extents      [-]  one extent per partition, in partition order
    /// post  every partition carries its reaching set, ordered by illuminant identity
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> Derive(const IlluminantPopulation& Illuminants, const std::vector<PartitionExtent>& Extents);

    /// 🧩 Re-derives one partition's reaching set — the row `44` §5 reaches when an owner moved.
    /// out   Result  [-]  refuses with ExtentExhausted outside the derived partition count
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> DerivePartition(const IlluminantPopulation& Illuminants,
                                  std::uint32_t               PartitionIndex,
                                  PartitionExtent             Extent);

    /// 🧩 How many illuminants reach one partition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t ReachingCount(std::uint32_t PartitionIndex) const;

    /// 🧩 One reaching illuminant, in identity order.
    /// out   Result  [-]  refuses with ExtentExhausted outside the reaching count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<OwnerIdentity> Reaching(std::uint32_t PartitionIndex, std::uint32_t ReachIndex) const;

    /// 🧩 How many illuminants one partition could not carry — `86`'s truncation row.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t TruncatedCount(std::uint32_t PartitionIndex) const;

    /// 🧩 The truncation across every partition.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t TruncatedTotal() const;

    /// 🧩 How many partitions the index spans.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t SpannedCount() const;

    /// 🧩 The population revision this index describes.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t DescribedRevision() const;

private:

    std::vector<std::vector<OwnerIdentity>>  ReachingSets;         // [-] - per partition, in identity order
    std::vector<std::uint32_t>                  TruncatedCounts;      // [-] - per partition
    std::uint32_t                               TruncatedAccumulated = 0u;   // [-] - across every partition
    std::uint64_t                               DescribedIndex     = 0u;   // [-] - the population revision
};

// 📐 Identity, registration and the reach comparison are Exact; the incidence projection reads a square root and a
//    pair of circular functions and is Bounded. The component claims Bounded, per `00` §3's transitivity rule.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
