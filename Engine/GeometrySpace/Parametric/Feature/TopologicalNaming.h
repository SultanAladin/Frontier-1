/*====================================================================================================================================
                                                      TOPOLOGICALNAMING.H
====================================================================================================================================*/
// 🧩 Persistent names from operation histories (research § L 🥇) and reference resolution: exact → source → geometric

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Feature/ProfileSweep.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceOperations.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Names are keyed by record slot. Faces are named from the history of the operation that made them; edges and
//    vertices are named from their incident faces (realthunder's lower-element rule), so they follow automatically:
//      face   ·  Extrude#2.Side(Line3) · Extrude#2.Cap(End) · Extrude#4.Side(Circle1)/1 (split piece) · Blend#5.Strip(…)
//      edge   ·  Edge(A|B)            (sorted face names; A|∅ laminar, A|A seam; #k when one face pair has several)
//      vertex ·  Vertex(A|B|C)        (sorted incident face set; #k when repeated)
struct BodyNaming
{
    std::map<uint32_t, std::string> Faces    = {};                                           // [-] - Face slot → name
    std::map<uint32_t, std::string> Edges    = {};                                           // [-] - Edge slot → name
    std::map<uint32_t, std::string> Vertices = {};                                           // [-] - Vertex slot → name
};

enum class ReferenceKind : uint8_t
{
    Face   = 0,                                                                              // A face of the current body
    Edge   = 1,                                                                              // An edge
    Vertex = 2,                                                                              // A vertex
};

// 📝 What a reference looked like when it was authored, for geometric rebinding when its name no longer resolves.
struct ReferenceSignature
{
    bool        CapturedCondition = false;                                                   // [-]   - Filled at authoring
    Vector3     Position          = {};                                                      // [len] - Boundary centre / midpoint / point
    Vector3     Direction         = {};                                                      // [-]   - Edge tangent at the midpoint (faces: unused)
    std::string Category          = {};                                                      // [-]   - Surface / curve category label
    double      Size              = 0.0;                                                     // [len] - Boundary length / edge length
};

struct TopologyReference
{
    ReferenceKind      Kind      = ReferenceKind::Edge;                                      // [-] - What it names
    std::string        Name      = {};                                                       // [-] - Persistent name
    ReferenceSignature Signature = {};                                                       // [-] - Authoring-time geometry
};

enum class ResolutionMethod : uint8_t
{
    Exact     = 0,                                                                           // Name found as is
    Source    = 1,                                                                           // Unique subject with the same sources (split / renumbered)
    Geometric = 2,                                                                           // Signature rebinding with a ratio test
    Refused   = 3,                                                                           // Missing or ambiguous: nothing chosen
};

struct ResolvedReference
{
    ResolutionMethod         Method     = ResolutionMethod::Refused;                         // [-] - How it resolved
    uint32_t                 Slot       = UINT32_MAX;                                        // [-] - Record slot when resolved
    std::string              Matched    = {};                                                // [-] - Name of the subject chosen
    std::string              Refusal    = {};                                                // [-] - Why not, when refused
    std::vector<std::string> Candidates = {};                                                // [-] - Ambiguous candidates (refusals)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Face names of a swept body: Prefix + "." + role ("Extrude#2.Side(Line3)"); edges and vertices completed.
[[nodiscard]] BodyNaming NameSweptBody(const ModelPartition& Partition, const SweepOutcome& Sweep, const std::string& Prefix);

// Boolean result: every face keeps its source face's name; pieces of one split source get /1, /2, … ordered by a stable
// geometric key (boundary centre, lexicographic at 10⁻⁶·L).
[[nodiscard]] BodyNaming NameBooleanBody(const ModelPartition& Partition, BodyToken Body, const BooleanOutcome& Boolean, const BodyNaming& First, const BodyNaming& Second);

// Blend result: Modified keeps its name; Prefix.Strip(edge name) and Prefix.Corner(vertex name) for new faces.
[[nodiscard]] BodyNaming NameBlendedBody(const ModelPartition& Partition, BodyToken Body, const BlendOutcome& Blend, const BodyNaming& Source, const std::string& Prefix);

// Face-operation result: Unchanged / Trimmed / Replaced keep their name; Generated → Prefix.Generated(edge or face name).
[[nodiscard]] BodyNaming NameChangedBody(const ModelPartition& Partition, BodyToken Body, const FaceOperationOutcome& Change, const BodyNaming& Source, const std::string& Prefix);

// Assembled panels (vehicle plan V7): Faces[k] takes FaceNames[k] ("Loft#4.Side(Surface)", "Mirror#5.Image(…)",
// "Close#6.Cap(0)"); duplicates get /1, /2, …; edges and vertices completed.
[[nodiscard]] BodyNaming NamePanelBody(const ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, const std::vector<std::string>& FaceNames);

// Recomputes edge and vertex names of Body from its face names (called by every Name… function).
void CompleteNaming(const ModelPartition& Partition, BodyToken Body, BodyNaming& Naming);

// Signature of a named subject as it is now (authoring capture).
[[nodiscard]] ReferenceSignature CaptureSignature(const ModelPartition& Partition, ReferenceKind Kind, uint32_t Slot);

// 💡 Resolution order (plan § 4): ① exact name; ② the unique subject whose name equals the reference with split /k and
//    #k ordinals removed (same sources); ③ geometric rebind among the same-source candidates (else every subject of the
//    same category and comparable size): nearest signature position, accepted only if it is within the reference size
//    and the runner-up is ≥ 2× farther; ④ otherwise refused with the candidates listed — never a silent best guess.
[[nodiscard]] ResolvedReference ResolveReference(const ModelPartition& Partition, BodyToken Body, const BodyNaming& Naming, const TopologyReference& Reference);

// Name with split (/k) and ordinal (#k) disambiguators removed, recursively inside Edge(…|…) / Vertex(…).
[[nodiscard]] std::string StripDisambiguators(const std::string& Name);

[[nodiscard]] const char* ResolutionMethodLabel(ResolutionMethod Method) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
