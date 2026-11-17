/*====================================================================================================================================
                                                           SOLIDARCSCENE.H
====================================================================================================================================*/
// 🧩 Display arrays of the current body (per-face corners, edge segments, name tables) and the CPU ray pick

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocument.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Sketcher
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Identity target encoding (R32_UINT): 0 = background, face f → f + 1, edge e → IdentityEdgeBit | (e + 1).
constexpr uint32_t IdentityEdgeBit = 0x80000000u;
constexpr uint32_t IdentitySectionCap = 0x7FFFFFFFu;                                         // Section cap (Alt+S): no entity
constexpr uint32_t StateSheetFace     = 0x10000u;                                            // Face-word bit 16: sheet face (no cap)

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 GPU layouts: 32-byte corners and segments (std430-friendly, see Shaders/SketcherProjection.slang).
struct SketcherSceneCorner
{
    float    Position[3] = {};                                                               // [len] - World point
    float    Normal[3]   = {};                                                               // [-]   - Outward unit normal (exact surface)
    uint32_t Face        = 0;                                                                // [idx] - Scene face index
    uint32_t Padding     = 0;                                                                // [-]   - 32-byte stride
};

struct SketcherSceneSegment
{
    float    Start[3] = {};                                                                  // [len] - World point
    float    End[3]   = {};                                                                  // [len] - World point
    uint32_t Edge     = 0;                                                                   // [idx] - Scene edge index
    uint32_t Padding  = 0;                                                                   // [-]   - 32-byte stride
};

struct SketcherSceneFace
{
    std::string Name        = {};                                                            // [-]    - Persistent name
    std::string Surface     = {};                                                            // [-]    - Carrier category ("Plane", "Cylinder")
    uint32_t    Slot        = 0;                                                             // [idx]  - Partition face slot
    uint32_t    FirstCorner = 0;                                                             // [idx]  - Span start in Corners
    uint32_t    CornerCount = 0;                                                             // [-]    - Span length (3 per triangle)
    uint32_t    Body        = 0;                                                             // [idx]  - Scene body index
    double      Area        = 0.0;                                                           // [len²] - Facet area
};

struct SketcherSceneEdge
{
    std::string Name         = {};                                                           // [-]   - Persistent name
    std::string Curve        = {};                                                           // [-]   - Carrier category ("Line", "Circle")
    uint32_t    Slot         = 0;                                                            // [idx] - Partition edge slot
    uint32_t    FirstSegment = 0;                                                            // [idx] - Span start in Segments
    uint32_t    SegmentCount = 0;                                                            // [-]   - Span length
    uint32_t    Body         = 0;                                                            // [idx] - Scene body index
    double      Length       = 0.0;                                                          // [len] - Polyline length
};

// 📝 A body vertex for mode-1 picking / direct reshape (drawn as a dot when its body is in vertex mode).
struct SketcherSceneVertex
{
    std::string           Name     = {};                                                     // [-]   - Persistent name ("Vertex(A|B|C)")
    uint32_t              Slot     = 0;                                                      // [idx] - Partition vertex slot
    uint32_t              Body     = 0;                                                      // [idx] - Scene body index
    std::array<double, 3> Position = {};                                                     // [len] - Point
};

// 📝 One body's spans in the scene tables (faces and edges are grouped by body, in creation order).
struct SketcherSceneBody
{
    std::string Key       = {};                                                              // [-]   - Creating step label ("Extrude#2")
    std::string Caption   = {};                                                              // [-]   - Shown name ("Block1")
    uint32_t    FirstFace = 0;                                                               // [idx] - Span start in Faces
    uint32_t    FaceCount = 0;                                                               // [-]   - Span length
    uint32_t    FirstEdge = 0;                                                               // [idx] - Span start in Edges
    uint32_t    EdgeCount = 0;                                                               // [-]   - Span length
};

struct SketcherScene
{
    std::vector<SketcherSceneCorner>  Corners     = {};                                      // [-]   - 3 per triangle, grouped by face
    std::vector<SketcherSceneSegment> Segments    = {};                                      // [-]   - Edge polylines as segments
    std::vector<SketcherSceneFace>    Faces       = {};                                      // [-]   - Scene face table
    std::vector<SketcherSceneEdge>    Edges       = {};                                      // [-]   - Scene edge table
    std::vector<SketcherSceneBody>    Bodies      = {};                                      // [-]   - Scene body table
    std::array<double, 3>             BoundsLow   = { -1.0, -1.0, -1.0 };                    // [len] - Box corner
    std::array<double, 3>             BoundsHigh  = { 1.0, 1.0, 1.0 };                       // [len] - Box corner
    uint64_t                          Revision    = 0;                                       // [-]   - Document revision it was built from
    uint64_t                          Generation  = 0;                                       // [-]   - Distinct per build (density changes re-upload)
    double                            Sag         = 0.0;                                     // [len] - Tessellation ε
    double                            Milliseconds = 0.0;                                    // [ms]  - Tessellation + packing
    std::string                       Refusal     = {};                                      // [-]   - Tessellation refusal (empty: fine)
    std::vector<SketcherSceneVertex>  Vertices    = {};                                      // [-]   - Body vertices (named), grouped by body
    uint32_t                          SolidCornerCount  = 0;                                 // [-]   - Opaque corners prefix (LiveCutters follow semitransparent)
    uint32_t                          SolidSegmentCount = 0;                                 // [-]   - Opaque edge segments prefix (LiveCutters follow)
};

// 📝 Per-face / per-edge state words for the viewport programs (bits: 1 selected, 2 active, 4 hovered).
struct SketcherSceneStates
{
    std::vector<uint32_t> Faces = {};                                                        // [-] - One word per scene face
    std::vector<uint32_t> Edges = {};                                                        // [-] - One word per scene edge
};

// 📝 Section clip (Alt+S) shared by the CPU pick and the viewport programs: the kept side is n·P + w ≥ 0; the back side
//    of a solid's face seen through the opening is the cap (SheetFaces[f] = 1 exempts open-sheet faces).
struct SketcherSectionClip
{
    std::array<double, 4> Plane      = { 0.0, 0.0, 1.0, 0.0 };                               // [-] - (n, w)
    std::vector<uint8_t>  SheetFaces = {};                                                   // [-] - One per scene face: 1 = sheet
};

struct SketcherPick
{
    uint32_t Identity = 0;                                                                   // [-]   - Identity code (see IdentityEdgeBit)
    double   Distance = 0.0;                                                                 // [len] - Along the ray
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Tessellates the document's body (SagRatio of the body diagonal) and packs the display arrays; empty without a body.
// FineCondition (zebra on): sides no longer than FineEdgeRatio of the diagonal, so interpolated normals follow the surface.
[[nodiscard]] SketcherScene BuildSketcherScene(const SketcherDocument& Document, double SagRatio = 1.5e-3, bool FineCondition = false);

constexpr double FineEdgeRatio = 0.02;                                                       // [-] - Zebra tessellation: longest side / diagonal

// 📝 Vehicle plan V8: one imported limit point and its foot on the nearest surface panel.
struct SketcherDeviationNeedle
{
    GeometrySpace::Parametric::Vector3 Point    = {};                                                         // [len] - Imported point (SpaceCurve step)
    GeometrySpace::Parametric::Vector3 Foot     = {};                                                         // [len] - Closest point on the panels
    double            Distance = 0.0;                                                        // [len] - |Point − Foot|
};

struct SketcherDeviationMap
{
    uint64_t                             Revision = 0;                                       // [-]   - Document revision measured
    std::vector<SketcherDeviationNeedle> Needles  = {};                                      // [-]   - One per imported point
    double                               Worst    = 0.0;                                     // [len] - Largest distance
    double                               Mean     = 0.0;                                     // [len] - Mean distance
};

// 💡 Distance from every SpaceCurve point (lofted or not — check loops count) to the nearest surface panel: best of a
//    9 × 9 parameter grid per panel, then Newton projection (ProjectOntoNurbsSurface, clamped to the panel). Empty
//    without panels.
[[nodiscard]] SketcherDeviationMap MeasureSketcherDeviation(const SketcherDocument& Document);

// 💡 Zebra phase (mirrors SketcherSurface.slang): the view direction reflected in the unit normal, its angle to the
//    camera up axis × Stripes / π — stripe k covers phase [k, k + ½). A function of the normal alone (view fixed), so
//    a G0 joint breaks the stripes, G1 kinks them and G2 keeps them smooth.
[[nodiscard]] double SketcherZebraPhase(GeometrySpace::Parametric::Vector3 Normal, GeometrySpace::Parametric::Vector3 Forward, GeometrySpace::Parametric::Vector3 Up, double Stripes) noexcept;

// 💡 CPU pick: nearest triangle hit (Möller–Trumbore); an edge wins over the face behind it when the ray passes within
//    EdgeReach of the segment and no closer than the face hit minus EdgeReach (so silhouette and crease edges are
//    pickable but hidden edges are not). XRayEnabled (Alt+Z) drops the face test for edges: the nearest edge within reach
//    wins wherever it lies, as the occluded strokes of the x-ray frame are pickable. Returns nothing on a miss.
//    Section (Alt+S, may be null): hits on the removed side are ignored, as the viewport discards them; when the first
//    kept hit is the back of a solid's face the ray meets the cap — IdentitySectionCap at that back face's distance (the
//    viewport keeps the same depth), which also hides the edges behind it.
[[nodiscard]] std::optional<SketcherPick> PickSketcherScene(const SketcherScene& Scene, const OrbitRay& Ray, double EdgeReach, bool XRayEnabled = false,
                                                            const SketcherSectionClip* Section = nullptr);

// 💡 How deep a sketch's profile must start below its plane to reach into the body under it: each profile sample casts a
//    ray down the plane normal into the scene; when every sample lands on a curved face (the plane is tangent to a
//    cylinder, cone, sphere, …) the deepest landing plus the tessellation sag (facets lie inside a convex surface, so the
//    facet depth already over-reaches the exact one). 0 when the profile is flush (a planar face), overhangs the body,
//    or lands farther than its own size.
[[nodiscard]] double MeasureSketcherProfileSink(const SketcherDocument& Document, const SketcherScene& Scene, const std::string& SketchLabel);

// 💡 Blender's extrude manifold reads where the material is: probes just inside the profile region, a hair above and below
//    the sketch plane, are classified against the target body (empty: the active one). +1 the material lies along +𝑧 (an
//    extrusion that way cuts into it), −1 along −𝑧 (a sketch drawn on a face: pushing in cuts), 0 when no side is clear
//    (most probes on it and none on the other).
[[nodiscard]] int MeasureSketcherProfileMaterial(const SketcherDocument& Document, const std::string& SketchLabel, const std::string& Target);

// Section clip of a scene: the plane and the faces of the document's sheet bodies.
[[nodiscard]] SketcherSectionClip ComposeSketcherSectionClip(const SketcherScene& Scene, const SketcherDocument& Document, std::array<double, 4> Plane);

// Selection names → state words; the last name of the most recently picked kind is the active operand.
[[nodiscard]] SketcherSceneStates ComposeSketcherStates(const SketcherScene& Scene, const SketcherSelection& Selection, uint32_t HoverIdentity);
// 📝 Matcap layer of every scene face (its own assignment, else the body matcap; unknown names → 0 = Studio), already
//    shifted into bits 8 … 15 so the caller ORs it into the face state words.
[[nodiscard]] std::vector<uint32_t> ComposeSketcherMatcapLayers(const SketcherScene& Scene, const SketcherAppearance& Appearance,
                                                                const SketcherMatcapLibrary& Library);
// Face assignments whose persistent name the current scene no longer has (kept in the document, reported in the Inspector).
[[nodiscard]] size_t CountDanglingMatcaps(const SketcherScene& Scene, const SketcherAppearance& Appearance);

// 📝 Closest approach of the ray to one scene edge: ray parameter and separation at the nearest of its segments.
struct SketcherEdgeApproach
{
    double Reach      = 0.0;                                                                 // [len] - Along the ray
    double Separation = 1.0e300;                                                             // [len] - Ray ↔ edge distance
};
[[nodiscard]] SketcherEdgeApproach ApproachSketcherEdge(const SketcherScene& Scene, const OrbitRay& Ray, uint32_t EdgeIndex);

// Identity → persistent name ("" for the background or an out-of-range code).
[[nodiscard]] std::string NameSketcherIdentity(const SketcherScene& Scene, uint32_t Identity);
[[nodiscard]] SketcherSelectionKind SketcherIdentityKind(uint32_t Identity) noexcept;

} // namespace Frontier::Workspaces::Sketcher
