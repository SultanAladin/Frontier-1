//============================================================================================================================================
//                                                           SPATIALSUBDIVISION.H
//============================================================================================================================================
// 🧩 Three subdivisions, two levels, one traversal — what lets `74` answer the pointer on the host, every sample.

#pragma once

#include "Foundation/Identity.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/RegistrationIndex/Api/RegistrationIndex.h"
#include "SlateDocument/Document/TopologyConditioning/Api/TopologyConditioning.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SUBDIVISION SHAPE
//------------------------------------------------------------------------------------------------------------------------

// 📝 Read by this unit alone, so `00` §2 places them here rather than in `Foundation/`. Both are tuning figures and
//    `40` §8 carries them as open: the depth bounds the recursion so a pathological owner distribution cannot
//    make a traversal unbounded, and the occupancy is where subdividing further stops paying for itself.
inline constexpr std::uint32_t SubdivisionDepthLimit = 20u;         // [-] - subdivisions permitted below the root
inline constexpr std::uint32_t SubdivisionLeafLimit  = 8u;          // [-] - entries a record holds before dividing
inline constexpr std::uint32_t AbsentRecord            = 0xFFFFFFFFu; // [-] - no record; never a valid ordinal

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT A RAY RESOLVES TO
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One face intersection in an owner's own object space.
/// note  📝 The three corner ordinals and their weights are carried rather than an interpolated attribute,
///        because `74` interpolates the domain position, `78` interpolates the orientation, and neither wants the
///        other's. Carrying the weights lets one traversal serve every consumer.
/// tag   nonallocating, nonthrowing
struct FaceIntersection
{
    std::uint32_t  FaceIndex       = 0u;                    // [-]  - the face the ray met
    std::uint32_t  CornerIndexs[3] = { 0u, 0u, 0u };        // [-]  - the fan triangle's corners
    double         Weights[3]        = { 0.0, 0.0, 0.0 };     // [-]  - barycentric, summing to one
    double         Distance          = 0.0;                   // [mm] - along the supplied direction
    bool           Resolved          = false;                 // [-]  - a face was met at all
};

/// 🧩 The whole tuple one pointer sample resolves to — `74` §2's table, geometric half.
/// note  🔴 Resolved as **one** tuple rather than as several answers. Every consumer needs a different part of it
///        and resolving them separately would traverse the same subdivision several times per pointer sample.
/// tag   nonallocating, nonthrowing
struct ResolvedIntersection
{
    OwnerIdentity  Owner          = {};                  // [-]  - `12` registration, `78` manipulation
    std::uint32_t     FaceIndex       = 0u;                  // [-]  - component selection
    std::uint32_t     CornerIndexs[3] = { 0u, 0u, 0u };      // [-]  - `74` interpolates the domain from these
    double            Weights[3]        = { 0.0, 0.0, 0.0 };   // [-]  - barycentric
    double            Distance          = 0.0;                 // [mm] - ordering when several intersect
    DocumentPosition  Position          = {};                  // [mm] - `78`'s manipulator plane, in document space
    SurfaceDirection  Orientation       = {};                  // [-]  - `72`'s projected placement
    bool              Resolved          = false;               // [-]  - anything was met at all
};

//------------------------------------------------------------------------------------------------------------------------
//                                              THE INNER LEVEL — PER OCCUPANT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Extents over one owner's faces, in that owner's own object space.
/// note  🔴 Object space, and therefore **invariant under owner motion** — `40` §2. A single document-space
///        subdivision over every face would be re-derived whenever any owner moved, and moving an owner is
///        the most frequent thing an artist does to a scene.
/// note  ⚠️ Faces are corner runs of any count and are fan-triangulated for intersection, matching `38` §4's
///        convention. Two triangulations of one n-gon would classify its interior differently along the diagonal.
/// tag   owning
class BoundingStructure
{
public:

    /// 🧩 Builds the subdivision over one sealed topology's conditioned face extents.
    /// in    Imported    [-]  the sealed topology
    /// in    Conditioned [-]  its conditioning, whose face extents are already rounded outward
    /// out   Result     [-]  refuses with HostDenied for an unsealed topology, and with ExtentExhausted when the
    ///                        conditioning describes a different revision
    /// note  🔴 The conditioning's revision is compared against the topology's. A structure built over extents
    ///        derived from a different seal indexes faces that have moved, and every intersection it resolves is
    ///        confidently wrong.
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructSubdivision(const TopologyStructure& Imported, const TopologyConditioning& Conditioned);

    /// 🧩 Intersects one ray, in the owner's own object space.
    /// in    Origin            [mm]  the ray's origin, in object space
    /// in    DirectionX        [-]   the ray's direction; not required to be unit length
    /// in    DirectionY        [-]
    /// in    DirectionZ        [-]
    /// in    FurthestDistance  [-]   the parameter beyond which nothing is resolved
    /// out   Intersection      [-]   the nearest face met, or an unresolved intersection
    /// note  🔴 The direction is deliberately **not** normalised. The caller transforms a document-space ray into
    ///        object space through a possibly non-uniform scale, and leaving the direction unnormalised is what
    ///        keeps the returned distance a document-space distance rather than an object-space one.
    /// note  📐 The three edge tests go through `02` §4's `ClassifyOrientation`, so inside and outside are decided
    ///        exactly. `40` §6 requires it: a missed face is geometry the artist cannot click, and an approximate
    ///        test misses along exactly the shared edges that dense topology is made of.
    /// cost  🚩
    /// tag   api, nonthrowing
    FaceIntersection IntersectRay(DocumentPosition Origin,
                                  double           DirectionX,
                                  double           DirectionY,
                                  double           DirectionZ,
                                  double           FurthestDistance) const;

    /// 🧩 The whole structure's extent, in object space.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ConditionedExtent Extent() const;

    std::uint32_t RecordCount() const;
    std::uint32_t FaceCount() const;

    /// 🧩 Whether the structure describes a topology at all.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Constructed() const;

private:

    struct BoundingRecord
    {
        ConditionedExtent  Extent        = {};             // [mm] - over the faces this record holds
        std::uint32_t      FirstFace     = 0u;             // [-]  - into the face ordering
        std::uint32_t      FaceCount     = 0u;             // [-]  - faces held
        std::uint32_t      FirstDivided  = AbsentRecord;   // [-]  - first of eight; AbsentRecord for a leaf
    };

    void Divide(std::uint32_t RecordIndex, std::uint32_t Depth);
    void Descend(std::uint32_t     RecordIndex,
                 DocumentPosition  Origin,
                 double            ReciprocalX,
                 double            ReciprocalY,
                 double            ReciprocalZ,
                 double            DirectionX,
                 double            DirectionY,
                 double            DirectionZ,
                 FaceIntersection& Nearest) const;

    std::vector<BoundingRecord>     Records;                 // [-]  - record zero is the root
    std::vector<std::uint32_t>      FaceOrder;               // [-]  - face ordinals, partitioned by record
    std::vector<ConditionedExtent>  FaceExtents;             // [-]  - per face, outward, from the conditioning
    std::vector<DocumentPosition>   Positions;               // [mm] - the topology's own, object space
    std::vector<std::uint32_t>      FaceFirstCorners;        // [-]  - per face
    std::vector<std::uint32_t>      FaceCornerCounts;        // [-]  - per face
    std::vector<std::uint32_t>      CornerVertices;          // [-]  - per corner
    std::uint64_t                   DescribedRevision = 0u;  // [-]  - the topology's sealed revision
    bool                            StructureBuilt    = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                            THE OUTER LEVEL — OVER OCCUPANTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One owner accepted to the document-space subdivision.
/// note  🔴 The transform held is the **composed** one — `12`'s `AttachmentFollows` resolved downward. An owner
///        attached to a moved carrier moves here even though its own transform did not change, which is `40` §5's
///        rule and the one place the two nesting relations differ observably in intersection.
/// tag   nonallocating, nonthrowing
struct AcceptedOwner
{
    OwnerIdentity          Owner  = {};        // [-]  - the population slot
    DecomposedTransform       Composed  = {};        // [mm] - as `12` §4 step ③ compounded it
    ConditionedExtent         Extent    = {};        // [mm] - document space, outward
    const BoundingStructure*  Inner     = nullptr;   // [-]  - not owned; the caller keeps it alive
};

/// 🧩 The document-space subdivision over owners, and the traversal `74` asks every pointer sample.
/// note  🔴 An owner move is a **refit** of one extent, never a rebuild — `40` §4. Refitting propagates the
///        changed extent upward and leaves the subdivision's shape alone; the shape degrades as owners move far
///        from where they were built, and quality is recovered by a rebuild through `34` at Background. Degraded
///        quality costs traversal time; a stalled tick costs the artist's stroke.
/// note  🔴 Registration exclusion is tested **before** descent, so a locked or hidden population costs nothing
///        rather than costing a rejected test each — `40` §3.
/// tag   owning
class OctantSpace
{
public:

    /// 🧩 Accepts one owner, or amends the one already accepted at that identity.
    /// out   Result  [-]  refuses with IdentityStale for an undeclared identity
    /// post  the subdivision is owed a rebuild before the admission is traversable
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Accept(const AcceptedOwner& Incoming);

    /// 🧩 Withdraws one owner.
    /// out   Result  [-]  refuses with IdentityStale when the owner is not accepted
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(OwnerIdentity Subject);

    /// 🧩 Refits one owner's extent and composed transform, without rebuilding.
    /// out   Result  [-]  refuses with IdentityStale when the owner is not accepted
    /// note  🔴 This is `40` §4's most frequent row and it is deliberately cheap. The subdivision's shape is
    ///        untouched; only the extents along the path to the owner's record widen.
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Refit(OwnerIdentity Subject, const DecomposedTransform& Composed, ConditionedExtent Extent);

    /// 🧩 Returns one accepted owner's standing record.
    /// out   Result  [-]  refuses with IdentityStale when the owner is not accepted
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<AcceptedOwner> Current(OwnerIdentity Subject) const;

    /// 🧩 Rebuilds the subdivision's shape over every accepted owner.
    /// post  the shape is optimal for the current extents; nothing is owed
    /// cost  🔴
    /// tag   api, nonthrowing
    Deliver<bool> ConstructOctants();

    /// 🧩 Resolves the nearest owner surface along one document-space ray — `74`'s precedence 2.
    /// in    Origin      [mm]  the ray's origin, in document space
    /// in    DirectionX  [-]   unit length, in document space
    /// in    DirectionY  [-]
    /// in    DirectionZ  [-]
    /// in    Subsets     [-]   `12`'s registrations; locked and visibility-excluded owners are never traversed
    /// out   Resolved    [-]   the whole tuple, or an unresolved intersection
    /// note  🔴 Descends nearest-first and stops when the remaining extents are further than the nearest confirmed
    ///        hit. Without the ordering the traversal visits every record whose extent the ray touches, which on a
    ///        dense scene is most of them.
    /// cost  🚩
    /// tag   api, nonthrowing
    ResolvedIntersection IntersectRay(DocumentPosition       Origin,
                                      double                 DirectionX,
                                      double                 DirectionY,
                                      double                 DirectionZ,
                                      const RegistrationIndex& Subsets) const;

    /// 🧩 Resolves every owner a document-space extent reaches — `74`'s marquee and `16`'s culling.
    /// in    Containment  [-]  true registers only owners wholly inside; false registers any that overlap
    /// out   Registered     [-]  in admission order
    /// note  🔴 One traversal over the extent, never one per position inside it — `74` §4.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::vector<OwnerIdentity> IntersectExtent(ConditionedExtent      Extent,
                                                  bool                   Containment,
                                                  const RegistrationIndex& Subsets) const;

    /// 🧩 Whether the shape has degraded enough that a rebuild is worth declaring through `34`.
    /// note  📝 Measured as the ratio of accumulated refit widening to the extent at the last build. A rebuild
    ///        triggered by a refit count would rebuild for a thousand owners that each moved a millimetre.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool RebuildWorthwhile() const;

    std::uint32_t AcceptedCount() const;
    std::uint32_t RecordCount() const;

    /// 🧩 Whether a rebuild is owed before the subdivision may be traversed.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool ConstructionOwed() const;

private:

    struct OctantRecord
    {
        ConditionedExtent  Extent       = {};
        std::uint32_t      FirstEntry   = 0u;
        std::uint32_t      EntryCount   = 0u;
        std::uint32_t      FirstDivided = AbsentRecord;
    };

    std::size_t Located(OwnerIdentity Subject) const;
    void        Divide(std::uint32_t RecordIndex, std::uint32_t Depth);
    void        Descend(std::uint32_t          RecordIndex,
                        DocumentPosition       Origin,
                        double                 DirectionX,
                        double                 DirectionY,
                        double                 DirectionZ,
                        const RegistrationIndex& Subsets,
                        ResolvedIntersection&  Nearest) const;

    std::vector<AcceptedOwner>  Accepted;                    // [-] - in admission order
    std::vector<OctantRecord>      Records;                     // [-] - record zero is the root
    std::vector<std::uint32_t>     EntryOrder;                  // [-] - admission ordinals, partitioned
    double                         BuiltVolume    = 0.0;        // [-] - root extent volume at the last build
    double                         WidenedVolume  = 0.0;        // [-] - accumulated by refit
    bool                           BuildOwed      = true;       // [-] - an admission stands unabsorbed
};

//------------------------------------------------------------------------------------------------------------------------
//                                          THE DOMAIN LEVEL — PER SURFACE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One placement's extent in a surface's parametric domain.
/// tag   nonallocating, nonthrowing
struct DomainExtent
{
    double         MinimumX     = 0.0;   // [-] - the domain's first axis
    double         MinimumY    = 0.0;   // [-] - its second
    double         MaximumX  = 0.0;   // [-]
    double         MaximumY = 0.0;   // [-]
    std::uint32_t  PlacementIndex = 0u;  // [-] - what the caller resolves it back to
    std::uint32_t  SequenceIndex  = 0u;  // [-] - `56` layer order; the topmost containing extent wins
};

/// 🧩 The two-dimensional subdivision over one surface's domain — `74` precedence 1 and `72`.
/// note  🔴 Over the **domain**, not over space, and per surface. It answers which placements contain a domain
///        position, which is a different question from which owner a ray meets and shares no space with it.
/// note  ⚠️ The topmost containing placement wins, by `56` sequence order. Resolving the first found would make
///        picking depend on insertion order, and the artist would select whichever decal happened to be declared
///        first rather than the one they can see.
/// tag   owning
class AxisSpace
{
public:

    /// 🧩 Declares the whole placement set, replacing what stood.
    /// cost  🚩
    /// tag   api, nonthrowing
    void ConstructAxes(const std::vector<DomainExtent>& Declaring);

    /// 🧩 Amends one placement's extent without rebuilding — the row `40` §4 reaches when a placement moved.
    /// out   Result  [-]  refuses with ExtentExhausted when no placement carries that ordinal
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> Refit(std::uint32_t PlacementIndex, DomainExtent Amending);

    /// 🧩 Resolves the topmost placement containing one domain position.
    /// out   Result  [-]  refuses with ExtentExhausted when no placement contains it
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Resolve(double PositionX, double PositionY) const;

    /// 🧩 Every placement whose extent overlaps a domain extent.
    /// cost  🚩
    /// tag   api, nonthrowing
    std::vector<std::uint32_t> Overlapping(DomainExtent Extent) const;

    std::uint32_t DeclaredCount() const;

private:

    std::vector<DomainExtent>  Extents;   // [-] - in declaration order
};

// 📐 Extent overlap, ray-face classification and domain containment are all Exact through `02` §4's predicates;
//    distance ordering is Bounded and only orders confirmed hits. The component claims Bounded per `00` §3.
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded, PrecisionGuarantee::Exact);

}   // namespace Slate
