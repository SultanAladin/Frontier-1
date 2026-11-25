/*============================================================================================================================================
                                                            SOFTBODYCODEC.H
============================================================================================================================================*/
// 🧩 Phase D07 .SoftBody two-tier codec (human-readable TOML header for XPBD/FEM/Tire/Botanical presets + optional 64B-aligned rest-cage binary)
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3.6

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace Frontier::Codecs::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SOFT BODY DOCUMENT MODEL
//------------------------------------------------------------------------------------------------------------------------

struct SoftBodyDocumentModel
{
    std::string                                                       DocumentTitle       { "UntitledSoftBody" };
    std::string                                                       PresetId            { "SportTireFem" };
    std::string                                                       SolverCategory      { "FemVolumetric3D" }; // "FemVolumetric3D" | "PneumaticXpbd" | "XpbdStrand" | "XpbdCloth"
    uint32_t                                                          SchemaVersion       = kSimulationCodecSchemaVersion;
    SimulationSpace::Deformable::DeformablePresetKind                 PresetKind          = SimulationSpace::Deformable::DeformablePresetKind::SportTireFem;
    SimulationSpace::Deformable::DeformableSimulationConfiguration    Configuration       {};
    std::string                                                       Description         {};

    // Optional Tier-2 baked rest-cage binary (positions, invMass, distance/bend/tet constraints)
    bool                                                              EmbedRestCageBinary = false;
    SimulationSpace::Deformable::DeformableBody                       EmbeddedRestBody    {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        SOFT BODY CODEC
//------------------------------------------------------------------------------------------------------------------------

class SoftBodyCodec
{
public:
    /// Builds a SoftBodyDocumentModel directly from one of the 9 built-in DeformablePresetKind presets.
    [[nodiscard]] static SoftBodyDocumentModel BuildFromPreset(
        SimulationSpace::Deformable::DeformablePresetKind kind,
        bool embedRestCageBinary = false);

    [[nodiscard]] static bool EncodeToBytes(
        const SoftBodyDocumentModel& model,
        std::vector<std::byte>& outBytes,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromBytes(
        std::span<const std::byte> bytes,
        SoftBodyDocumentModel& outModel,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const SoftBodyDocumentModel& model,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        SoftBodyDocumentModel& outModel,
        std::string* outError = nullptr);
};

} // namespace Frontier::Codecs::Simulation
