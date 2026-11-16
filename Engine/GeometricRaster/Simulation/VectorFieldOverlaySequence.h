/*============================================================================================================================================
                                                         VECTORFIELDOVERLAYSEQUENCE.H
============================================================================================================================================*/
// 🧩 Editor-only Vulkan 1.3 dynamic-rendering overlay sequence executing all 6 .slang ForceField rasterization pipelines + MRT PickID

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/WorkspaceAppearance.h"
#include "Engine/GeometricRaster/Compute/FieldLatticeVulkanExecutor.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldDescriptors.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace Frontier::Editor::Simulation
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class OverlayPickCategory : uint8_t
{
    None           = 0u, // [-] - Empty sky/backdrop
    ForceCircle    = 1u, // [-] - Force source center beacon or reach envelope
    MotionWaypoint = 2u, // [-] - Motion path waypoint handle (A, P_i, B)
    Anemometer     = 3u, // [-] - Cross-section probe beacon
    LitObject      = 4u, // [-] - Architectural PBR scene geometry
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct OverlayCameraState
{
    SimulationSpace::ForceFields::SpatialVector3 FocusTargetMeters { 0.0f, 0.0f, 4.0f }; // [m] - Orbit/look target in +Z Up world space
    float                           YawRadians        = -0.785398f;      // [rad] - Azimuth around +Z
    float                           PitchRadians      = 0.46f;           // [rad] - Elevation above XY ground plane
    float                           DistanceMeters    = 62.0f;           // [m]   - Orbit distance from FocusTarget
    float                           VerticalFovDeg    = 48.0f;           // [deg] - Perspective field of view
    float                           OrthoHalfHeightM  = 34.0f;           // [m]   - Half-height for Top/Ortho projections
    ViewportProjectionPreset        Projection        = ViewportProjectionPreset::Perspective;
};

struct OverlayPickResult
{
    OverlayPickCategory Category    = OverlayPickCategory::None; // [-]  - Hit object category
    uint32_t            PrimaryId   = 0u;                        // [id] - Force token or object ID
    uint32_t            SecondaryId = 0u;                        // [id] - Waypoint index or sub-element
};

struct OverlayFrameRequest
{
    uint32_t                                                            PaneIndex             = 0u;
    uint32_t                                                            WidthPx               = 640u;
    uint32_t                                                            HeightPx              = 360u;
    OverlayCameraState                                                  Camera                {};
    SimulationSpace::ForceFields::ForceOverlaySettings                  OverlaySettings       {};
    double                                                              TimeSeconds           = 0.0;
    uint32_t                                                            SelectedForceToken    = 0u;
    int32_t                                                             SelectedWaypointIndex = -1;
    bool                                                                LitShadingEnabled     = true;
    float                                                               SliceHalfSpanMeters   = 36.0f;
    std::span<const SimulationSpace::ForceFields::ForceSourceShaderEntry> PackedSources       {};
    std::span<const uint32_t>                                           SourceTokens          {};
    std::span<const SimulationSpace::ForceFields::SpatialVector3>       PackedWaypoints       {};
    std::span<const uint32_t>                                           SourceWaypointOffsets {};
    std::span<const uint32_t>                                           SourceWaypointCounts  {};
    std::span<const uint8_t>                                            SourcePathIsOrbit     {};
};

struct OverlayRenderOutput
{
    const void* ImTextureDataPtr   = nullptr; // [-] - Persistent ImTextureData* updated cleanly for PixelSpace::FillTexturedRectangle
    uint32_t    WidthPx            = 0u;      // [px]- Framebuffer width
    uint32_t    HeightPx           = 0u;      // [px]- Framebuffer height
    bool        RenderedOnVulkan   = false;   // [-] - True when Vulkan 1.3 dynamic rendering succeeded
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VECTORFIELDOVERLAYSEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class VectorFieldOverlaySequence
{
public:
    VectorFieldOverlaySequence() noexcept;
    ~VectorFieldOverlaySequence() noexcept;

    VectorFieldOverlaySequence(const VectorFieldOverlaySequence&)            = delete;
    VectorFieldOverlaySequence& operator=(const VectorFieldOverlaySequence&) = delete;

    [[nodiscard]] bool Initialize(
        SimulationSpace::ForceFields::FieldLatticeVulkanExecutor& VulkanExecutor,
        const std::filesystem::path&                              ShaderBinaryDirectory,
        std::string&                                              OutDiagnostic) noexcept;

    void Finalize() noexcept;

    [[nodiscard]] bool QueryReady() const noexcept;

    [[nodiscard]] OverlayRenderOutput RenderPane(
        const OverlayFrameRequest& Request,
        std::string&               OutDiagnostic) noexcept;

    [[nodiscard]] OverlayPickResult QueryPickAtNormalizedUv(
        uint32_t PaneIndex,
        float    NormU,
        float    NormV) const noexcept;

    [[nodiscard]] bool SavePanePng(
        uint32_t                     PaneIndex,
        const std::filesystem::path& OutputPngPath) const noexcept;

    [[nodiscard]] std::span<const uint8_t> QueryColorReadbackRgba(uint32_t PaneIndex) const noexcept;
    [[nodiscard]] uint32_t QueryPaneWidth(uint32_t PaneIndex) const noexcept;
    [[nodiscard]] uint32_t QueryPaneHeight(uint32_t PaneIndex) const noexcept;

private:
    struct SequenceImplementation;
    std::unique_ptr<SequenceImplementation> Impl{};
};

} // namespace Frontier::Editor::Simulation
