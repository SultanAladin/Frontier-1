/*====================================================================================================================================
                                                    ROLLINGBALLGEOMETRY.H
====================================================================================================================================*/
// 🧩 Rolling-ball geometry of one edge: spine = offset ∩ offset, contact curves, exact blend carrier, conic sections

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Geometry/CurveGeometry.h"
#include "Engine/GeometrySpace/Parametric/Geometry/SurfaceGeometry.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class BlendProfile : uint8_t
{
    Fillet  = 0,                                                                             // Circular cross-section (rolling ball)
    Chamfer = 1,                                                                             // Chord of the same ball (ruled)
};

// 📝 Size is the ball radius for a fillet and the equal setback along each face for a chamfer. EndSize makes the blend
//    variable: the size runs linearly from Size at each edge's start vertex to EndSize at its end vertex. SizeAnchors
//    pins that direction to vertices instead of edge senses (which a regeneration may reverse): each edge takes Size at
//    the end listed there, and an edge with neither or both ends listed is refused. AnchorSizes (one per SizeAnchors
//    entry) instead gives every listed vertex its own size: each edge runs linearly from its start vertex's size to its
//    end vertex's, an unlisted vertex takes Size, and blends meeting at a vertex agree there (handles on a loop).
struct BlendSettings
{
    BlendProfile             Profile             = BlendProfile::Fillet;                     // [-]   - Cross-section
    double                   Size                = 0.1;                                      // [len] - Radius / setback (at the start vertex)
    bool                     TangentChainEnabled = false;                                    // [-]   - Grow the edge set along G1 chains first
    std::optional<double>    EndSize             = {};                                       // [len] - Size at the end vertex (empty = constant)
    std::vector<VertexToken> SizeAnchors         = {};                                       // [-]   - Variable: vertices that take Size (empty = start vertices)
    std::vector<double>      AnchorSizes         = {};                                       // [len] - Per vertex: size at SizeAnchors[i] (empty = Size / EndSize)
};

// 💡 Everything about one blended edge that does not depend on its neighbours. Spine and both contact curves share
//    one parameter τ: Contacts[i](τ) is the foot of Spine(τ) on support i, so a cross-section is a single τ value.
struct EdgeBlendSheet
{
    EdgeToken       Edge            = {};                                                    // [-]   - Blended edge
    FinToken        Fins[2]         = {};                                                    // [-]   - [0] = radial entry, [1] = mate
    FaceToken       Faces[2]        = {};                                                    // [-]   - Supports (faces of Fins)
    bool            ConvexCondition = true;                                                  // [-]   - Ball inside the material
    double          Radius          = 0.0;                                                   // [len] - Ball radius
    double          Dihedral        = 0.0;                                                   // [rad] - Material-free wedge angle θ
    SurfaceGeometry Offsets[2]      = {};                                                    // [-]   - Ball-centre loci
    CurveGeometry   Spine           = StraightLine{};                                        // [-]   - Ball-centre path
    CurveGeometry   Contacts[2]     = {};                                                    // [-]   - Feet on the supports
    SurfaceGeometry Surface         = PlaneSurface{};                                        // [-]   - Exact blend carrier
    Orientation     Sense           = Orientation::Forward;                                  // [-]   - Outward vs natural normal
    int             SpineAxis       = 0;                                                     // [-]   - Chart coordinate equal to τ (0 = 𝑢, 1 = 𝑣, −1 none)
    double          CrossCentre     = 0.0;                                                   // [-]   - Other chart coordinate at mid-section
    double          Alignment       = 1.0;                                                   // [-]   - +1 if τ runs along the edge, −1 against
    double          MaxResidual     = 0.0;                                                   // [len] - Worst ball / foot residual sampled
    double          RadiusRate      = 0.0;                                                   // [-]   - dr/dτ: r(τ) = Radius + RadiusRate·τ (variable)
    // ⚠️ A variable sheet (RadiusRate ≠ 0) has a tilted line spine; its contact lines are unit-speed with their own τ, so
    //    Contacts[i](τ) is NOT the foot of Spine(τ) there. Every same-τ consumer refuses variable sheets. Only a straight
    //    edge between planes takes this form; a variable size anywhere else is a ball-law sheet (RadiusRate = 0 here).
    [[nodiscard]] bool VariableCondition() const noexcept { return RadiusRate != 0.0; }
    // 💡 A ball-law sheet runs on the edge's τ with a law radius: r(τ) = d(τ)·tan(θ(τ)/2) for a chamfer on a traced or
    //    elliptical edge (or any variable chamfer off the planar line), r(τ) linear for a variable fillet there (canal
    //    carrier). Its contacts DO share the spine's τ, but Radius is only the mid-edge value and Offsets are reference
    //    loci at it, so consumers that build constant-radius geometry refuse it (MeasureSheetRadius gives the exact r(τ)).
    [[nodiscard]] bool LawCondition() const noexcept;
};

struct EdgeBlendSheetOutcome
{
    std::optional<EdgeBlendSheet> Sheet   = {};                                              // [-] - Present on success
    std::string                   Refusal = {};                                              // [-] - Reason otherwise
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Research § I (Choi–Ju): ① convexity and wedge angle θ from the outward normals at mid-edge; ② ball radius (chamfer:
//    r = d·tan(θ/2), exact equal setback where the cross-section of the supports is straight); ③ each support offset by
//    r towards the ball (exact, OffsetSurface — refused past a curvature radius); ④ spine = exact hybrid SSI branch of
//    the two offsets nearest the edge (Line / Circle only — traced spines need procedural carriers, 🚧); ⑤ contact
//    curves from the feet of the spine; ⑥ carrier: line spine → cylinder (fillet) / plane (chamfer); circle spine →
//    torus (fillet) / cone, cylinder or annulus plane (chamfer); ⑦ sampled residual |C(τ) − S(τ)| − r and foot distance.
//    Override (smooth overflow): support Side is replaced by Face, the face across a tangent edge of the original support;
//    convexity and the wedge angle still come from the edge's own faces, the offsets, spine and contacts from Face.
struct SupportOverride
{
    int       Side = -1;                                                                     // [-] - Replaced support (0 / 1)
    FaceToken Face = {};                                                                     // [-] - Face the ball rolls onto
};
[[nodiscard]] EdgeBlendSheetOutcome ResolveEdgeBlendSheet(const ModelPartition& Partition, EdgeToken Edge, const BlendSettings& Settings, const SupportOverride& Override = {});

// Ball radius at spine parameter τ: Radius + RadiusRate·τ.
[[nodiscard]] double MeasureSheetRadius(const EdgeBlendSheet& Sheet, double τ) noexcept;

// 📝 Distance of Position from the envelope of the sheet's balls, min_τ (|X − C(τ)| − r(τ)); 0 on a fillet carrier. For
//    a line spine with |r′| < 1 the minimum is closed-form: ρ·√(1 − r′²) − r(τ_X), ρ the distance from the spine and
//    τ_X the foot parameter (constant radius: ρ − r).
[[nodiscard]] double MeasureEnvelopeResidual(const EdgeBlendSheet& Sheet, Vector3 Position) noexcept;

// Unwrapped chart reference of a sheet at spine parameter τ (anchor for SP-curve fitting on the blend face).
[[nodiscard]] Vector2 ResolveSheetChart(const EdgeBlendSheet& Sheet, double τ) noexcept;

// Exact section of a cylinder by a plane: a circle when the plane is perpendicular to the axis, else an ellipse whose
// minor semi-axis is the radius. Empty when the plane is parallel to the axis.
[[nodiscard]] std::optional<CurveGeometry> SectionCylinderByPlane(const CylinderSurface& Cylinder, Vector3 PlanePoint, Vector3 PlaneNormal) noexcept;

// Closest point of a surface to a position (exact inversion).
[[nodiscard]] Vector3 ProjectPointOntoSurface(const SurfaceGeometry& Surface, Vector3 Position) noexcept;

// 💡 Tangent-chain propagation: the seeds plus every sharp edge reached through a vertex where the edge curve continues
//    G1 (unit tangents agree to 1e-9). Smooth edges (tangent faces, e.g. the seam between a slot wall and its rounded
//    end) are never added — they have nothing to blend. Order: seeds first, then breadth-first along the chains.
[[nodiscard]] std::vector<EdgeToken> GrowTangentChain(const ModelPartition& Partition, BodyToken Body, const std::vector<EdgeToken>& Seeds);

// True when the two faces of a manifold edge meet tangentially at mid-edge (normals parallel to 1e-9).
[[nodiscard]] bool SmoothEdgeCondition(const ModelPartition& Partition, EdgeToken Edge);

[[nodiscard]] const char* BlendProfileLabel(BlendProfile Profile) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
