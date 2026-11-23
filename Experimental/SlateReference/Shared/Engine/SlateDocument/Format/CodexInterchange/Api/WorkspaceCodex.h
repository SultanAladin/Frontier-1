//============================================================================================================================================
//                                                           WORKSPACECODEX.H
//============================================================================================================================================
// 🧩 Typed workspace, environment, scene-entry, material-reference, and embedded-Codex section translation.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/SurfaceLayerSequence/Api/SurfaceLayerSequence.h"
#include "SlateDocument/Format/CodexInterchange/Api/CodexInterchange.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

/// 🧩 Which real workspace entry a Codex scene declaration represents.
enum class CodexSceneSubject : std::uint32_t
{
    Sun         = 0u,
    Sky         = 1u,
    Atmosphere  = 2u,
    Geometry    = 3u
};

/// 🧩 One named scene entry, its authored placement, and its assigned material reference.
struct CodexSceneEntry
{
    CodexSceneSubject  Subject             = CodexSceneSubject::Geometry;   // [-]
    std::string        Naming              = {};                            // [-] - persisted artist-visible naming
    std::string        GeometryReference   = {};                            // [-] - section-local geometry identity
    std::string        MaterialReference   = {};                            // [-] - section-local material identity
    double             Position[3]         = {};                            // [m] - authored world placement
    double             Rotation[3]         = {};                            // [deg] - yaw, pitch, roll
    double             Scale[3]            = { 1.0, 1.0, 1.0 };             // [-] - per-axis authored scale
};

/// 🧩 The environment figures a workspace persists beside its scene entries.
struct CodexEnvironment
{
    double         SunElevation          = 35.0;    // [deg]
    double         SunAzimuth            = 120.0;   // [deg]
    double         SunIntensity          = 4.8;     // [lx]
    double         SunTemperature        = 5500.0;  // [K]
    double         SkyIntensity          = 1.0;     // [-]
    double         AtmosphereDensity     = 1.0;     // [-]
    double         AtmosphereScaleHeight = 1.0;     // [-]
};

/// 🧩 One binary polygon mesh carried directly inside a WorkspaceCodex document.
struct CodexSceneMesh
{
    std::string                  Naming    = {}; // [-] - matches CodexSceneEntry::GeometryReference
    std::vector<double>          Positions = {}; // [m] - xyz triples, local to the scene entry placement
    std::vector<std::uint32_t>   Indices   = {}; // [-] - triangle-list indices into Positions triples
};

/// 🧩 One imported image reference declared by a material channel. Pixels remain external until image sampling lands.
struct WorkspaceMaterialImageReference
{
    std::string      ReferenceName = {};     // [-] - artist-visible source name
    std::string      OriginPath = {};        // [-] - path or package-local locator retained for missing reports
    std::uint32_t    Width = 0u;             // [px]
    std::uint32_t    Height = 0u;            // [px]
    std::uint32_t    ComponentCount = 0u;    // [-] - 1, 2, 3 or 4 where known
    std::uint32_t    BitsPerComponent = 8u;  // [b]
    bool             ColourData = true;      // [-] - true for colour-managed channels, false for data maps
};

/// 🧩 One document-backed material slot carried beside workspace scene entries.
struct WorkspaceMaterialRecord
{
    std::string                                  Reference = {};   // [-] - section-local material identity used by scene entries
    MaterialSpecification                       Material  = {};   // [-] - twenty-channel declaration and reflectance choice
    SurfaceLayerSequence                        Layers    = {};   // [-] - ordered layer sequence with a mandatory Base Material entry
    std::vector<WorkspaceMaterialImageReference> Images    = {};   // [-] - imported image sources addressed by channel SourceIndex
};

/// 🧩 A complete workspace payload carried by a WorkspaceCodex document.
struct WorkspaceCodex
{
    std::string                          Naming       = {};   // [-] - document title
    CodexEnvironment                     Environment  = {};   // [-] - Sun, Sky, and Atmosphere declaration
    std::vector<CodexSceneEntry>         Scene        = {};   // [-] - only persisted scene entries
    std::vector<CodexSceneMesh>          SceneMeshes  = {};   // [m] - embedded binary scene geometry
    std::vector<WorkspaceMaterialRecord> Materials    = {};   // [-] - document-backed material slots
    std::vector<CodexDocument>           Embedded     = {};   // [B] - whole specialized Codex documents
};

/// 🧩 Creates Slate's default white dielectric material with its mandatory Base Material layer.
WorkspaceMaterialRecord DefaultWorkspaceMaterialRecord(const std::string& Reference);

/// 🧩 Ensures every geometry scene entry has a corresponding document-backed material record.
void EnsureWorkspaceMaterialRecords(WorkspaceCodex& Workspace);

/// 🧩 Assigns a declared material to one geometry entry without changing any mesh data.
Deliver<bool> AssignWorkspaceMaterial(WorkspaceCodex& Workspace,
                                      std::uint32_t SceneIndex,
                                      const std::string& MaterialReference);

/// 🧩 Binds one imported image reference to a material channel and returns its material-local source ordinal.
Deliver<std::uint32_t> BindWorkspaceMaterialImage(WorkspaceMaterialRecord& Material,
                                                  ChannelSubject Channel,
                                                  const WorkspaceMaterialImageReference& Image);

/// 🧩 Translates typed workspace content to and from preserved Codex sections.
class WorkspaceCodexInterchange
{
public:

    /// 🧩 Adds typed workspace sections to a WorkspaceCodex document.
    /// in    Workspace [-]  typed workspace content
    /// in    Identity  [-]  stable document identity
    /// in    Revision  [-]  committed revision introducing the sections
    /// out   Result    [-]  complete WorkspaceCodex document
    Deliver<CodexDocument> EncodeWorkspace(const WorkspaceCodex& Workspace,
                                           std::uint64_t          Identity,
                                           std::uint64_t          Revision) const;

    /// 🧩 Resolves typed workspace sections from a WorkspaceCodex document while leaving unknown sections retained.
    /// in    Document [-]  decoded WorkspaceCodex document
    /// out   Result   [-]  typed workspace content
    Deliver<WorkspaceCodex> DecodeWorkspace(const CodexDocument& Document) const;
};

}   // namespace Slate
