/*============================================================================================================================================
                                                            PARTICLEINTERCHANGE.H
============================================================================================================================================*/
// 🧩 External particle interchange: HTML Particle Studio JSON snapshots, Workspace Handoff packets, & 3D .obj/.gltf surface anchor intake

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleRig.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::Interchange
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ExternalParticleFormat : uint8_t
{
    Unknown          = 0u, // [-] - Unrecognized external particle format
    HtmlStudioJson   = 1u, // [-] - HTML Particle Studio JSON parameter snapshot
    WavefrontSurface = 2u, // [-] - Wavefront .obj 3D surface for anchor sampling
    GltfSurface      = 3u, // [-] - Khronos glTF 2.0 .gltf 3D surface for anchor sampling
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ParticleInterchange
{
public:
    [[nodiscard]] static ExternalParticleFormat DetectFormatFromPath(const std::filesystem::path& FilePath) noexcept;

    /// Converts HTML Particle Studio Y-Up coordinates to Frontier Right-Hand +Z Up (and vice-versa).
    static void ConvertYUpToRightHandZUp(const float SrcYUp[3], float DstZUp[3]) noexcept;
    static void ConvertRightHandZUpToYUp(const float SrcZUp[3], float DstYUp[3]) noexcept;

    /// Imports an HTML Particle Studio JSON parameter snapshot (delegating colour blocks directly to ColourInterchange).
    [[nodiscard]] static bool ImportHtmlStudioRigJson(
        std::string_view            JsonText,
        ParticleSpace::ParticleRig& OutRig,
        std::string&                OutDiagnostic,
        bool                        ConvertAxisToZUp = false);

    /// Exports a ParticleRig to an HTML Particle Studio JSON snapshot (delegating colour blocks directly to ColourInterchange).
    [[nodiscard]] static std::string ExportHtmlStudioRigJson(
        const ParticleSpace::ParticleRig& Rig,
        bool                              ConvertAxisFromZUp = false);

    /// Extracts area-weighted ParticleAnchorSamples directly from any Frontier PolygonNetwork.
    [[nodiscard]] static std::vector<ParticleSpace::ParticleAnchorSample> SampleAnchorsFromPolygonNetwork(
        const GeometrySpace::PolygonNetwork&       Network,
        const ParticleSpace::SurfaceAnchorOrigin&  TransformOrigin,
        uint32_t                                   SampleCount,
        uint32_t                                   Seed = 1337u);

    /// Imports external .obj / .gltf geometry via GeometryInterchange and samples surface anchors directly.
    [[nodiscard]] static bool ImportSurfaceAnchorsFromExternalFile(
        const std::filesystem::path&                       SourcePath,
        const ParticleSpace::SurfaceAnchorOrigin&          TransformOrigin,
        uint32_t                                           SampleCount,
        uint32_t                                           Seed,
        std::vector<ParticleSpace::ParticleAnchorSample>&  OutAnchors,
        std::string&                                       OutDiagnostic);
};

} // namespace Frontier::Interchange
