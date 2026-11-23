//============================================================================================================================================
//                                                          PRIMITIVESTRUCTURE.H
//============================================================================================================================================
// 🧩 Parametric polygon generation — the closed set of solids every authored surface and every manipulator grip is built from.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateDocument/Document/TopologyStructure/Api/TopologyStructure.h"
#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT IS GENERATED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which solid a declaration generates.
/// note  🔴 A closed set rather than an open one. Every entry here is a surface of revolution or a box, generated
///        from parameters that are held rather than from vertices that are typed in — which is what lets an
///        authored primitive be re-generated at a different subdivision after the artist has placed it.
/// note  📝 The cone, the cylinder and the sphere are what the manipulator's grips are built from — `78` §4's
///        cone tip, its scale grip and the central ring. They are declared here rather than beside the
///        manipulator so that a modelled cone and a grip cone are one generation and cannot come to differ.
/// tag   guarantee
enum class PrimitiveSubject : std::uint32_t
{
    Box            = 0u,   // [-] - six quadrilateral faces about the origin
    Sphere         = 1u,   // [-] - latitude and longitude subdivision; poles are triangle fans
    Cylinder       = 2u,   // [-] - along the second axis, capped at both ends
    Cone           = 3u,   // [-] - along the second axis, apex at the far end, capped at the near one
    Torus          = 4u,   // [-] - about the second axis; the ring the manipulator's centre is drawn as
    Plane          = 5u,   // [-] - one subdivided quadrilateral in the first and third axes
    AnnularSector  = 6u,   // [-] - a flat curved band; `78` §4's rotation grips are exactly this
    SubjectCount   = 7u    // [-] - the closed count, never a subject
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE PARAMETERS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One primitive's declared parameters — every solid's, in one declaration.
/// note  🔴 One declaration for all seven rather than seven declarations. A primitive whose subject the artist
///        changes keeps the extents they already set, and a per-subject declaration would discard them at each
///        change. The unread members of a given subject cost nothing and the retained edit is the point.
/// note  ⚠️ Extents are half-extents about the origin, so a box of unit extent spans −1 to +1 on every axis and
///        the generated solid is centred on whatever transform places it. A primitive generated about a corner
///        rotates about that corner, and the artist reads that as the rotation being wrong.
/// tag   nonallocating, nonthrowing
struct PrimitiveSpecification
{
    PrimitiveSubject  Generated        = PrimitiveSubject::Box;   // [-]  - which solid
    double            HalfExtentX  = 1.0;                     // [mm] - the first axis; also the radius where one applies
    double            HalfExtentUp     = 1.0;                     // [mm] - the second axis; the height of a cylinder or cone
    double            HalfExtentY = 1.0;                     // [mm] - the third axis
    double            MinorRadius      = 0.25;                    // [mm] - the torus tube, and the annular band's half-width
    double            SweepRadians     = 6.283185307179586;       // [rad] - the arc a sector sweeps; a full turn otherwise
    double            SweepOffset      = 0.0;                     // [rad] - where the sweep begins, about the second axis
    std::uint32_t     RadialCount      = 24u;                     // [-]  - subdivisions about the axis of revolution
    std::uint32_t     AxialCount       = 12u;                     // [-]  - subdivisions along it, or across a plane
    bool              CapsDeclared     = true;                    // [-]  - false leaves a cylinder or cone open-ended
};

// 📝 Bounds rather than an absence of them. A radial count of two closes no surface and a count in the millions
//    exhausts the host while the artist is dragging a parameter slider that has no reason to reach it.
inline constexpr std::uint32_t RadialCountMinimum    = 3u;        // [-] - fewer closes no surface of revolution
inline constexpr std::uint32_t AxialCountMinimum     = 1u;        // [-] - one span is a single ring of faces
inline constexpr std::uint32_t SubdivisionLimit  = 4096u;     // [-] - on either count, per primitive

/// 🧩 Whether a specification generates a surface at all.
/// note  📝 Asked before generation rather than reported after it. A degenerate extent generates a solid with
///        coincident vertices, which seals successfully and then fails at whichever consumer first divides by
///        the extent it has — and that consumer is named in the refusal instead of this one.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool PrimitiveGenerable(const PrimitiveSpecification& Declaring)
{
    return Declaring.Generated       != PrimitiveSubject::SubjectCount
        && Declaring.RadialCount     >= RadialCountMinimum
        && Declaring.RadialCount     <= SubdivisionLimit
        && Declaring.AxialCount      >= AxialCountMinimum
        && Declaring.AxialCount      <= SubdivisionLimit
        && Declaring.HalfExtentX  > 0.0
        && Declaring.HalfExtentUp     > 0.0
        && Declaring.HalfExtentY > 0.0;
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE GENERATION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Generates one primitive into a sealed topology.
/// in    Declaring  [-]  the parameters
/// in    Generated  [-]  an unsealed topology; positions, faces, coordinates and perpendiculars are declared into it
/// out   Result    [-]  refuses with ContentUnsupported for a specification `PrimitiveGenerable` rejects, and
///                       with HostDenied for a topology that is already sealed
/// post  🔴 the topology is sealed; its revision has advanced and nothing further may be declared into it
/// note  🔴 The topology is sealed **here**, at the end of generation, because a primitive is complete by
///        construction. `38`'s non-mutation guarantee begins at the seal, and a generated surface handed back
///        unsealed would be one a caller could add a face to — which is a surface no parameter describes.
/// note  🔴 Domain coordinates are generated alongside the positions rather than left to `68` to unwrap. The
///        parametrisation is what the solid is *defined by*, so it is exact here and merely approximated there,
///        and a generated box that arrived unwrapped would have seams `68` chose rather than the six the box has.
/// note  📝 Perpendiculars are declared per vertex and are the analytic ones, not the ones `38` §4 would average.
///        A cylinder's cap and its wall meet at one ring of positions and the two perpendiculars there are
///        opposite; the generation splits that ring so each side carries its own, which is what makes the edge
///        read as an edge.
/// cost  🔴
/// tag   api, nonthrowing
Deliver<bool> GeneratePrimitive(const PrimitiveSpecification& Declaring, TopologyStructure& Generated);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 The half-extents the generated solid actually occupies, before any transform places it.
/// in    Declaring  [-]  the parameters
/// out   Minimum      [mm] the lower corner in object space
/// out   Maximum   [mm] its upper corner
/// note  📝 Derived from the parameters rather than measured from the generated positions, so a caller can size
///        a subdivision or frame a camera before generating anything. The torus is the only entry where the
///        answer is not the three half-extents, and it is the reason this exists rather than being assumed.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
void ProjectPrimitiveExtent(const PrimitiveSpecification& Declaring,
                            DocumentPosition&             Minimum,
                            DocumentPosition&             Maximum);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DECLARATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every primitive the document holds parametrically, addressed by the ordinal its declaration returned.
/// note  🔴 The **parameters** are retained and the topology is not. A primitive whose radial count the artist
///        raises is re-generated from the amended specification, and a stored topology would be a second
///        representation that the parameters no longer describe — `02` §3.1's rule about the decomposed
///        transform, applied to a surface.
/// note  ⚠️ An owner carrying a primitive declaration is still an owner carrying a topology: `12` holds
///        the generated surface and this holds what generated it. Retiring the owner retires both, and the
///        cascade in `12` §12 is what calls `Withdraw`.
/// tag   owning
class PrimitiveIndex
{
public:

    static constexpr std::uint32_t PrimitiveLimit = 65536u;   // [-] - parametric primitives in one document

    /// 🧩 Declares one primitive's parameters and issues the ordinal it is addressed by.
    /// in    Declaring  [-]  the parameters
    /// out   Result    [-]  refuses with ContentUnsupported for a specification `PrimitiveGenerable` rejects,
    ///                       and with ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const PrimitiveSpecification& Declaring);

    /// 🧩 Amends one primitive's parameters, advancing its revision where the generated surface would differ.
    /// in    PrimitiveIndex  [-]  an ordinal this component registered
    /// in    Amending          [-]  the amended parameters
    /// out   Result           [-]  refuses with ContentUnsupported for an unclaimed ordinal or a specification
    ///                              `PrimitiveGenerable` rejects
    /// note  🔴 The revision advances only where the amendment changes the generated surface. Every member here
    ///        does, which is why the comparison is over the whole specification — but stating it that way is what
    ///        keeps a later member that does *not* from silently forcing a re-generation of every owner.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Amend(std::uint32_t PrimitiveIndex, const PrimitiveSpecification& Amending);

    /// 🧩 One declared primitive's parameters.
    /// out   Result  [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const PrimitiveSpecification*> Resolve(std::uint32_t PrimitiveIndex) const;

    /// 🧩 Withdraws one primitive, returning its slot for reuse.
    /// out   Result  [-]  refuses with ContentUnsupported for an unclaimed ordinal
    /// note  ⚠️ The slot is reused rather than erased, exactly as `72`'s placements are. Erasing would renumber
    ///        every ordinal above it and every owner naming one would name a different primitive.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> Withdraw(std::uint32_t PrimitiveIndex);

    /// 🧩 One primitive's revision, so a consumer knows whether its generated surface is still current.
    /// out   Revision  [-]  zero for an unclaimed ordinal, which no generated surface ever recorded
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint64_t Revision(std::uint32_t PrimitiveIndex) const;

    /// 🧩 How many primitives stand declared.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t DeclaredCount() const;

    /// 🧩 How many slots the index spans, claimed or not.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    std::uint32_t SpannedCount() const;

    /// 🧩 Discards every declaration. Called when the document holding them closes.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reclaim();

private:

    struct HeldPrimitive
    {
        PrimitiveSpecification  Declared        = {};      // [-] - the parameters, as Declare validated them
        std::uint64_t           DeclaredRevision = 0u;     // [-] - advanced by an amendment that changes the surface
        bool                    SlotOccupied    = false;   // [-] - false where the slot awaits reuse
    };

    std::vector<HeldPrimitive>  Primitives;         // [-] - by primitive ordinal
    std::vector<std::uint32_t>  ReleasedIndexs;   // [-] - withdrawn slots, reused before the span grows
    std::uint64_t               LatestRevision = 0u;// [-] - the last revision any primitive was advanced to
    std::uint32_t               OccupiedCount  = 0u;// [-] - primitives currently declared
};

}   // namespace Slate
