//============================================================================================================================================
//                                                        SCENEMESHIMPORT.H
//============================================================================================================================================
// 🧩 File-to-workspace-scene mesh intake. The file decoder produces transient Codex scene geometry; ownership
//    remains with the host workspace scene, not the Content Browser catalogue.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"

#include <string>
#include <vector>

namespace Slate
{

enum class SceneMeshFormat : std::uint32_t
{
    Unsupported = 0u,
    WavefrontObj = 1u,
    GltfText = 2u,
    GltfBinary = 3u,
    FbxAscii = 4u,
    Stl = 5u,
    Ply = 6u
};

struct ImportedSceneMesh
{
    CodexSceneEntry Entry = {};
    CodexSceneMesh Mesh = {};
    std::vector<std::string> MaterialSlots = {};
    std::vector<WorkspaceMaterialRecord> MaterialRecords = {};
    SceneMeshFormat Format = SceneMeshFormat::Unsupported;
};

SceneMeshFormat ClassifySceneMeshFormat(const std::string& Path);
bool SceneMeshFormatSupported(const std::string& Path);
Deliver<ImportedSceneMesh> ImportSceneMeshFile(const std::string& Path);

} // namespace Slate
