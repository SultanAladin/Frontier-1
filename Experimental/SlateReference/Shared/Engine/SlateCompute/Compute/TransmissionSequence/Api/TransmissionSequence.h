//============================================================================================================================================
//                                                        TRANSMISSIONSEQUENCE.H
//============================================================================================================================================
// 🧩 `62` — cutout resolved at `16` and never here, transmissive collected into a bounded sorted column, and amended back to front.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"
#include "Shared/TransmissionProjection.slang.h"
#include "SlateCompute/Compute/ReflectanceIntegrator/Api/ReflectanceIntegrator.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateMath/Numeric/ReportSequence/Api/ReportSequence.h"
#include "SlateVulkan/Device/RenderSchedule/Api/RenderSchedule.h"

#include <cstdint>
#include <array>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE THREE BEHAVIOURS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `62` §2's three rows an owner's material takes.
/// note  🔴 Cutout is **not** transmission and is resolved at `16`, at visibility time — `62` §2. Conflating the
///        two is the defect that makes foliage cost what glass costs: a leaf card is opaque with a hole in it,
///        and resolving it as transparent puts every leaf into the sorted column below, so a tree becomes the
///        most expensive object in the workspace for no visual gain whatever.
/// note  🔴 Because a cutout owner writes `VisibilityIndex`, it is shaded by `18`, outlined by `26`, picked by
///        `74` and occluded by `60` with no special case in any of them. That is the whole reason the
///        classification lives at `16` rather than here.
/// tag   guarantee
enum class TransmissionBehaviour : std::uint32_t
{
    Opaque         = 0u,   // [-] - channel 8 is not read at all
    Cutout         = 1u,   // [-] - channel 8 as coverage, thresholded; resolved by `16` §3.1
    Transmissive   = 2u,   // [-] - channel 8 as transparency; resolved here
    BehaviourCount = 3u    // [-] - the closed count, never a behaviour
};

/// 🧩 Which behaviour one declared material takes.
/// in    Declared   [-]  the material
/// out   Behaviour  [-]  derived from the declaration; never authored beside it
/// note  🔴 Derived rather than declared as a fourth property. A behaviour declared separately is a second
///        answer to a question the reflectance selection and the cutout registration already answer, and the two
///        disagree the moment an artist switches a material from transmissive to standard.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
TransmissionBehaviour BehaviourOf(const MaterialSpecification& Declared);

/// 🧩 Whether one cutout coverage reading is present at a pixel.
/// in    Declared  [-]  the material, for its own threshold
/// in    Coverage  [-]  channel 8 as sampled
/// out   Present   [-]  true where the surface is there at all
/// note  🔴 The threshold is **per material** and never global — `62` §2. A single threshold across a document
///        makes one artist's foliage disappear while another's grows a halo, and neither can correct it.
/// note  ⚠️ Declared here and consumed by `16` §3.1, which is where the test is actually performed. `62` owns
///        the classification and `16` owns the resolution; the split is `62` §2's whole content.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool CoverageResolved(const MaterialSpecification& Declared, double Coverage);

//------------------------------------------------------------------------------------------------------------------------
//                                                      ONE FRAGMENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One transmissive fragment, as `TransmissionIndex` carries it.
/// tag   nonallocating, nonthrowing
struct TransmissionFragment
{
    std::uint32_t  DepthKey    = 0u;                        // [-] - the ordering key; near is greatest
    std::uint32_t  SurfaceWord = SlateTransmissionAbsent;   // [-] - partition and triangle, packed
    double         Depth       = 0.0;                       // [-] - reversed, as recorded; host-side only
};

/// 🧩 One pixel's bounded, depth-sorted transmissive column.
/// note  🔴 Nearest first, and the overflow leaves the **end** — `62` §3.1. The nearest surface is the one whose
///        amendment dominates, so discarding the farthest is the correct direction and is what
///        `ProjectTransmissionSlot` already encodes.
/// note  ⚠️ The truncation is **counted** and reported through `86`. A discarded fragment is content the artist
///        authored and cannot see, and a column that discards silently is one whose depth ceiling nobody can
///        measure against their own scene.
/// tag   nonallocating, nonthrowing
struct TransmissionColumn
{
    TransmissionFragment  Held[TransmissionDepth] = {};   // [-] - nearest first
    std::uint32_t         HeldCount               = 0u;   // [-] - entries occupied
    std::uint32_t         TruncatedCount          = 0u;   // [-] - fragments the ceiling turned away
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT ONE FRAGMENT IS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What a transmissive owner declares, read from `42` and added to by nothing.
/// note  🔴 `62` §4: this document **adds no channel**. Every field below is one of `18` §2's twenty, read
///        unamended, and the reflectance selection that consumes them is `18` §3's Transmissive.
/// tag   nonallocating, nonthrowing
struct TransmissionSpecification
{
    double  Opacity         = 1.0;   // [-] - channel 8; how much of what is behind survives
    double  Transmission    = 0.0;   // [-] - channel 18; the fraction refracted rather than reflected
    double  RefractionRatio = 1.5;   // [-] - channel 19; Fresnel and the transmitted lobe's width
    double  TintComponent[3] = { 1.0, 1.0, 1.0 };   // [-] - channel 1; the tint applied to what is transmitted
    double  Roughness       = 0.0;   // [-] - channel 3; the width of the transmitted lobe
};

/// 🧩 Reads one fragment's transmission specification out of an already-resolved channel set.
/// in    Resolved  [-]  `18`'s resolution at the fragment's own domain position
/// out   Declared  [-]  the five channels, unamended
/// note  📝 Taken from the resolved set rather than resampled, because `18` §8 already resolved the whole set
///        once at that position. Resampling here would walk `56`'s layer sequence a second time for the one
///        surface in the scene most likely to be layered.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
TransmissionSpecification DeclaredTransmission(const ResolvedChannelSet& Resolved);

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE METRICS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What `62` reports through `86`.
/// note  🔴 The truncation **appends** and every count **overwrites** — `86` §2. A per-slot owner count
///        appended once per rotation would bury the one truncation the artist did not expect.
/// tag   nonallocating, nonthrowing
struct TransmissionMetrics
{
    std::uint32_t  OwnerCount        = 0u;   // [-] - transmissive owners collected this rotation
    std::uint32_t  MaximumColumnDepth  = 0u;   // [-] - the deepest column any pixel reached
    std::uint32_t  TruncatedThisRecording = 0u;  // [-] - fragments the ceiling turned away this rotation
    std::uint64_t  TruncatedTotal       = 0u;   // [-] - across the session
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `62` — the two recordings, the sorted collection, and the back-to-front amendment of `RadianceSurface`.
/// note  🔴 `62` §6: **nothing here writes** `VisibilityIndex`, `DepthSurface` or `OccupancySurface`. A
///        transmissive owner that wrote depth would occlude what is behind it in `16`, and the surface it
///        exists to reveal would never be shaded at all.
/// note  🔴 A transmissive owner casts **no** occlusion and a cutout owner does — `62` §5. The asymmetry
///        is deliberate: `60` §3's projections resolve topology, so a transmissive owner registered in one
///        would cast the shadow of a solid object, while a cutout owner is topology with a coverage test and
///        casts correctly with no special case at all.
/// note  ⚠️ 🚧 Ordering between this and `30` is `08` §2's amendment list. `RenderSchedule` orders by declared
///        reads and writes and does not yet order two amenders of one target, so both declare an amendment
///        ordinal and the schedule prefers the lower — the diff below closes that.
/// tag   owning
class TransmissionSequence
{
public:

    // 📝 `08` §3 ⑤ is two recordings. The ordinals are declared here so that the schedule orders them against
    //    `30`'s without either document knowing the other's number by heart.
    static constexpr std::uint32_t CollectAmendmentIndex = 10u;   // [-] - ⑤·i writes `TransmissionIndex`
    static constexpr std::uint32_t ResolveAmendmentIndex = 20u;   // [-] - ⑤·ii amends `RadianceSurface`

    /// 🧩 Contributes ⑤·i — the collection that writes `TransmissionIndex` and no depth.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// note  🔴 Produces `TransmissionIndex` and amends nothing. Every fragment is inserted with an atomic
    ///        sorted insertion, so no depth write is performed and the opaque resolution `16` produced stands
    ///        untouched.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ContributeCollection(RenderSchedule& Schedule) const;

    /// 🧩 Contributes ⑤·ii — the resolution that amends `RadianceSurface` back to front.
    /// out   Result  [-]  refuses with whatever the schedule rejected
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ContributeResolution(RenderSchedule& Schedule) const;

    /// 🧩 Inserts one fragment into one pixel's column, in depth order.
    /// in    Column        [-]  the column, amended in place
    /// in    Incoming      [-]  the fragment
    /// in    OpaqueDepth   [-]  `16`'s resolved depth at this pixel, reversed
    /// out   Accepted      [-]  false where the fragment was discarded
    /// note  🔴 A transmissive surface **behind the opaque depth is discarded** — `62` §3. It is not visible,
    ///        and resolving it would amend a pixel it does not reach.
    /// note  📝 The host form of the device's atomic sorted insertion, and the same ordering. `82` §5 resolves a
    ///        preview through it and `00` §11 gates the agreement, which is why the comparison itself lives in
    ///        `Shared/` rather than here.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Insert(TransmissionColumn& Column, const TransmissionFragment& Incoming, double OpaqueDepth) const;

    /// 🧩 Amends one pixel's standing radiance by one fragment.
    /// in    Behind    [-]  what stands behind the fragment, in the working space
    /// in    Declared  [-]  the fragment's five channels
    /// in    Shaded    [-]  what `18` resolved for the fragment itself, direct and ambient already summed
    /// in    ViewCosine[-]  the fragment's orientation against the view direction
    /// out   Amended   [-]  the three amended components
    /// note  🔴 A transmissive surface is shaded through `18` **exactly as an opaque one is** — `62` §5. The same
    ///        direct term over `44` §5's reaching set, the same ambient term from `28`, the same models from
    ///        `18` §3. Nothing about transmission changes how the surface itself is lit; it changes only what
    ///        survives from behind it.
    /// note  🔴 Refraction drives the Fresnel term and the lobe's width and **displaces nothing** — `62` §4.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void AmendRadiance(const double                     Behind[3],
                       const TransmissionSpecification& Declared,
                       const double                     Shaded[3],
                       double                           ViewCosine,
                       double                           Amended[3]) const;

    /// 🧩 Resolves one whole column back to front, amending the radiance behind it.
    /// in    Column    [-]  the column, nearest first
    /// in    Declared  [-]  one specification per held fragment, parallel to the column
    /// in    Shaded    [-]  one shaded radiance per held fragment, parallel to the column
    /// in    ViewCosine[-]  one per held fragment
    /// in    Current  [-]  what `18` left in `RadianceSurface`, amended in place
    /// note  🔴 Walked from the **last** held entry to the first, which is far to near — the column is stored
    ///        nearest first and read in reverse. Reading it forward composites the near pane under the far one,
    ///        which reads as two sheets of glass in the wrong order and is not visibly an ordering defect.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Resolve(const TransmissionColumn&                       Column,
                 const std::vector<TransmissionSpecification>&   Declared,
                 const std::vector<std::array<double, 3>>&       Shaded,
                 const std::vector<double>&                      ViewCosine,
                 double                                          Current[3]) const;

    /// 🧩 Records what one rotation's collection cost, for `86`.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void DeclareRotation(std::uint32_t OwnerCount,
                         std::uint32_t MaximumColumnDepth,
                         std::uint32_t TruncatedThisRecording);

    /// 🧩 Appends `62` §3.1's truncation and declares every measure beside it.
    /// note  🔴 The truncation appends and the counts overwrite — `86` §2. Coalesced by the ceiling as its
    ///        subject rather than by the pixel, because a per-pixel subject would present a million entries for
    ///        one pane of glass and `86` §6's coalescing would then be doing the discarding.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Report(ReportSequence& Reporting, MeasureIndex& Measured, TickPoint Sampled) const;

    const TransmissionMetrics& Metrics() const;

private:

    TransmissionMetrics  Reported = {};   // [-] - what `86` presents
};

// 📐 Owner identity, the depth ordering and the packed slot are Exact; the Fresnel term and the amendment are
//    Bounded, and the accumulation is Perceptual because `RadianceSurface` is Tier D — `62` §7. `00` §3's
//    transitivity rule folds them to the weakest.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Perceptual,
                         PrecisionGuarantee::Bounded,
                         PrecisionGuarantee::Exact);

}   // namespace Slate
