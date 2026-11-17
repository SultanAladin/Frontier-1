/*====================================================================================================================================
                                                         GENERALFUSE.H
====================================================================================================================================*/
// 🧩 General Fuse Booleans: interferences → pave blocks → split faces → GWN classification → selection → assembly

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Containment/WindingNumber.h"
#include "Engine/GeometrySpace/Parametric/Intersection/SurfaceIntersection.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

enum class BooleanOperation : uint8_t
{
    Union              = 0,                                                                  // A ∪ B
    Intersection       = 1,                                                                  // A ∩ B
    Difference         = 2,                                                                  // A − B
    ReversedDifference = 3,                                                                  // B − A
    FirstInside        = 4,                                                                  // Faces of A inside B (sheet cut piece, no B caps)
    FirstOutside       = 5,                                                                  // Faces of A outside B (sheet cut piece, no B caps)
    Imprint            = 6,                                                                  // Every face of A, split where B meets it (B dropped)
};

// 📝 State of a split face relative to the OTHER argument (OCCT: IN / OUT / ON with same or opposite orientation).
enum class FragmentState : uint8_t
{
    Inside     = 0,                                                                          // Inside the other solid
    Outside    = 1,                                                                          // Outside the other solid
    OnSame     = 2,                                                                          // On its boundary, normals agree
    OnOpposite = 3,                                                                          // On its boundary, normals oppose
};

struct GeneralFuseSettings
{
    double                      Precision    = SessionPrecision;                             // [len] - Incidence / merge distance
    SurfaceIntersectionSettings Intersection = {};                                           // [-]   - Face–face SSI
    WindingSettings             Containment  = {};                                           // [-]   - Fragment classification
    bool                        MaximalFacesEnabled = true;                                  // [-]   - Remove redundant topology after assembly
    bool                        SecondClassificationEnabled = true;                          // [-]   - Classify B fragments against A (off: A open, B discarded)
};

struct GeneralFuseStatistics
{
    int    FacePairs          = 0;                                                           // [-]  - A-face × B-face pairs
    int    FacePairsTested    = 0;                                                           // [-]  - After bounding-box pruning
    int    CoincidentPairs    = 0;                                                           // [-]  - Same carrier surface
    int    SectionBranches    = 0;                                                           // [-]  - SSI branches kept
    int    TracedSections     = 0;                                                           // [-]  - Marched branches made exact carriers
    int    SingularVertices   = 0;                                                           // [-]  - Branch crossings registered as vertices
    int    EdgeEdgeTests      = 0;                                                           // [-]  - Curve–curve tests
    int    EdgeFaceTests      = 0;                                                           // [-]  - Curve–surface tests
    int    InterferenceVertices = 0;                                                         // [-]  - New vertices from interferences
    int    Paves              = 0;                                                           // [-]  - Interior split points on edges
    int    SplitEdges         = 0;                                                           // [-]  - Pave blocks of argument edges
    int    CommonBlocks       = 0;                                                           // [-]  - Pave blocks shared by A and B
    int    SectionEdges       = 0;                                                           // [-]  - Section pave blocks
    int    ImprintedEdges     = 0;                                                           // [-]  - Argument edges lying in the other's faces
    int    Fragments[2]       = { 0, 0 };                                                    // [-]  - Split faces per argument
    int    States[4]          = { 0, 0, 0, 0 };                                              // [-]  - Fragments per FragmentState
    int    PropagatedStates   = 0;                                                           // [-]  - Fragments that took a neighbour's state
    int    DroppedCycles      = 0;                                                           // [-]  - Degenerate face-finding cycles
    double IntersectMilliseconds = 0.0;                                                      // [ms] - Interference phase
    double SplitMilliseconds     = 0.0;                                                      // [ms] - Face splitting
    double ClassifyMilliseconds  = 0.0;                                                      // [ms] - Fragment classification
};

struct FuseVertex
{
    Vector3 Position = {};                                                                   // [len] - Merged position
};

// 📝 Pave block: an argument edge between consecutive paves, or a section curve between consecutive vertices.
struct FuseEdge
{
    CurveGeometry Curve    = StraightLine{};                                                 // [-] - Carrier
    double        Low      = 0.0;                                                            // [-] - τ at Start
    double        High     = 1.0;                                                            // [-] - τ at End
    int           Start    = -1;                                                             // [-] - FuseVertex index
    int           End      = -1;                                                             // [-] - FuseVertex index
    int           Argument = -1;                                                             // [-] - 0 / 1, −1 for sections
    bool          Section  = false;                                                          // [-] - From a face–face section
};

// 📝 Directed use of a pave block by a fragment loop; Edge = −1 is a virtual closure along a singular row.
struct FuseUse
{
    int            Edge         = -1;                                                        // [-] - FuseEdge index
    Orientation    Sense        = Orientation::Forward;                                      // [-] - Along / against τ
    ParameterCurve SurfaceCurve = ParameterLine{};                                           // [-] - SP-curve (same τ as the edge)
    double         Start        = 0.0;                                                       // [-] - τ at the use start
    double         End          = 1.0;                                                       // [-] - τ at the use end
    int            VertexStart  = -1;                                                        // [-] - FuseVertex at the start
    int            VertexEnd    = -1;                                                        // [-] - FuseVertex at the end
};

struct FuseFragment
{
    int                               Argument  = 0;                                         // [-] - 0 = A, 1 = B
    FaceToken                         Source    = {};                                        // [-] - Argument face split
    SurfaceGeometry                   Surface   = PlaneSurface{};                            // [-] - Carrier
    Orientation                       Sense     = Orientation::Forward;                      // [-] - Outward vs surface normal
    std::vector<std::vector<FuseUse>> Loops     = {};                                        // [-] - Outer first, then holes
    FragmentState                     State     = FragmentState::Outside;                    // [-] - Against the other argument
    Vector3                           Probe     = {};                                        // [len] - Classification point
};

struct GeneralFuseOutcome
{
    std::string               Refusal    = {};                                               // [-] - Non-empty when refused
    std::vector<FuseVertex>   Vertices   = {};                                               // [-] - All vertices
    std::vector<FuseEdge>     Edges      = {};                                               // [-] - All pave blocks
    std::vector<FuseFragment> Fragments  = {};                                               // [-] - All split faces
    GeneralFuseStatistics     Statistics = {};                                               // [-] - Work counters
};

// 📝 History in the OCCT sense: every result face is Modified from one argument face (split, possibly flipped).
struct FaceHistory
{
    FaceToken     Result   = {};                                                             // [-] - Face of the result body
    int           Argument = 0;                                                              // [-] - 0 = A, 1 = B
    FaceToken     Source   = {};                                                             // [-] - Argument face it was split from
    FragmentState State    = FragmentState::Outside;                                         // [-] - Classification that kept it
    bool          Flipped  = false;                                                          // [-] - Orientation reversed (B in A − B)
};

struct EdgeHistory
{
    EdgeToken Result   = {};                                                                 // [-] - Edge of the result body
    int       Argument = -1;                                                                 // [-] - 0 / 1, −1 for sections (Generated)
};

struct BooleanOutcome
{
    BodyToken   Body    = {};                                                                // [-] - Result body (new records)
    std::string Refusal = {};                                                                // [-] - Non-empty when refused
    int         Solids  = 0;                                                                 // [-] - Solid regions
    int         Shells  = 0;                                                                 // [-] - Shells (outer + cavities)
    int         Faces   = 0;                                                                 // [-] - Faces of the result
    int         FacesMerged = 0;                                                             // [-] - Fragments merged into maximal faces
    int         EdgesJoined = 0;                                                             // [-] - Edges joined across degree-two vertices
    std::vector<FaceHistory> FaceHistories = {};                                             // [-] - Result face → argument face
    std::vector<EdgeHistory> EdgeHistories = {};                                             // [-] - Result edge → argument / section
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 General Fuse (OCCT PaveFiller + Builder, research § G), non-destructive on the arguments:
//    ① vertex registry (VV merge) · EE curve–curve · EF curve–surface + point-in-face → interference vertices;
//    ② paves on every edge from all registry vertices (VE) → pave blocks, shared by A and B when coincident (common blocks);
//    ③ FF exact SSI → section curves split at registry vertices, kept where inside both faces; argument edges lying in
//       the other's faces are imprinted;
//    ④ each face split in its chart by smallest-turn cycle tracing (holes by containment);
//    ⑤ every fragment classified once against the other argument (generalized winding number, ON by normals).
//    Traced (marched) section carriers are refused in this step.
[[nodiscard]] GeneralFuseOutcome RunGeneralFuse(const ModelPartition& Partition, BodyToken First, BodyToken Second, const GeneralFuseSettings& Settings = {});

// 💡 Selection (BOP = GF + selection) and direct assembly of a new body: fins, radial pairs, shells by face connectivity,
//    outer shells (positive volume) become solid regions, cavities (negative volume) join the region containing them.
//    Maximal faces (G5): fragments of one carrier are merged afterwards and the history redirected to the merged face.
[[nodiscard]] BooleanOutcome AssembleBoolean(ModelPartition& Partition, const GeneralFuseOutcome& Fuse, BooleanOperation Operation, bool MaximalFacesEnabled = true);

[[nodiscard]] BooleanOutcome ComputeBoolean(ModelPartition&            Partition,
                                            BodyToken                  First,
                                            BodyToken                  Second,
                                            BooleanOperation           Operation,
                                            const GeneralFuseSettings& Settings   = {},
                                            GeneralFuseStatistics*     Statistics = nullptr);

// 💡 Imprint (Plasticity / Fusion "split face", the 2D cutter): A with its faces split along the curves where Tool's
//    faces meet them — every fragment of A kept, Tool's dropped. Only splits whose edges lie in the plane (PlaneOrigin,
//    PlaneNormal) stay; any other split (Tool grazing a raised neighbour) is healed by the maximal-face merge. The volume
//    is unchanged. Faces receives A's face count before (Outcome.Faces is the count after).
[[nodiscard]] BooleanOutcome ImprintBody(ModelPartition& Partition, BodyToken Body, BodyToken Tool, const Vector3& PlaneOrigin, const Vector3& PlaneNormal, int& Faces);

[[nodiscard]] const char* BooleanOperationLabel(BooleanOperation Operation) noexcept;
[[nodiscard]] const char* FragmentStateLabel(FragmentState State) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
