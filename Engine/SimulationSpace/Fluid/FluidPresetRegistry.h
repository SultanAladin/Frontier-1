/*============================================================================================================================================
                                                        FLUIDPRESETREGISTRY.H
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) 1:1 54-preset .FluidGrid registry, tracer/debris CPU mirrors, and RGBA8 flipbook PNG exporter
// Reference: Experimental/FluidEditor/Engine/PresetScenes.js L1-L171 & FluidShaders.js L951-L999, L1589-L1740

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"
#include "Engine/SimulationSpace/EffectLayers/SimulationCheckpointRing.h"
#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/Fluid/FluidRegionTypes.h"
#include "Engine/SimulationSpace/Fluid/FluidSimulationPostOverlay.h"
#include "Engine/SimulationSpace/Fluid/FluidVolumeRenderer.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::SimulationSpace::Fluid
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 PRESET & DEBRIS STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidDebrisSettings
{
    bool     Visible  = false;
    uint32_t Count    = 1024u;
    float    Lifetime = 3.2f;
    float    Size     = 2.0f;
    float    Gravity  = 9.8f;
    float    Drag     = 1.2f;
    float    Bounce   = 0.35f;
    float    Glow     = 1.5f;
};

struct FluidPresetSourceEntry
{
    std::string                  Token        {};
    FluidReleaseSourceParameters Source       {};
    float                        DebrisAmount = 0.0f;
    float                        DebrisSpeed  = 2.0f;
    FluidDebrisKindOrdinal       DebrisKind   = FluidDebrisKindOrdinal::Rubble;
};

struct FluidPresetParticleRef
{
    std::string TemplateId {};
    std::string Name       {};
    uint32_t    Count      = 0u;
};

struct FluidPresetGridDescriptor
{
    FluidGridTopologyOrdinal Topology       = FluidGridTopologyOrdinal::Fixed;
    std::array<uint32_t, 3>  Resolution     { 48u, 48u, 48u };
    std::array<float, 3>     SizeMeters     { 4.0f, 4.0f, 4.0f };
    uint32_t                 MaxCells       = 96u;
    float                    TileSizeMeters = 2.0f;
    uint32_t                 TileResolution = 24u;
    uint32_t                 TileSlotBudget = 48u;
    uint32_t                 WorldTiles     = 0u; // 1..5 for WorldGrid
};

struct FluidPresetRecord
{
    int32_t                                                  Order          = 0;
    std::string                                              FilePath       {};
    std::string                                              Id             {};
    std::string                                              Name           {};
    std::string                                              Family         {};
    std::string                                              Icon           {};
    std::string                                              Summary        {};
    FluidBoundaryOrdinal                                     Boundary       = FluidBoundaryOrdinal::OpenTop;
    FluidPresetGridDescriptor                                Grid           {};
    bool                                                     RenderVisible  = true;
    float                                                    RenderExposure = 1.05f;
    int32_t                                                  RenderSteps    = 72;
    bool                                                     VoxelPreview   = false;
    FluidDebrisSettings                                      Debris         {};
    Frontier::SimulationSpace::EffectLayers::EffectLayerStack GasStack      {};
    std::vector<FluidPresetSourceEntry>                      ReleaseSources {};
    std::vector<Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord> ForceRecords {};
    std::vector<FluidColliderSphere>                         Colliders      {};
    std::vector<FluidPresetParticleRef>                      ParticleRefs   {};
};

struct FluidPresetRuntimeBundle
{
    FluidStepSettings                         StepSettings    {};
    FluidVolumeRenderSettings                 RenderSettings  {};
    FluidShimmerInput                         Shimmer         {};
    std::vector<FluidReleaseSourceParameters> ActiveSources   {};
    bool                                      StackStopped    = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                             FLUIDPRESETREGISTRY
//------------------------------------------------------------------------------------------------------------------------

class FluidPresetRegistry
{
public:
    FluidPresetRegistry() noexcept = default;

    /// Loads all *.FluidGrid preset documents in `directoryPath`, sorted by Scene.Order.
    [[nodiscard]] bool LoadFromDirectory(
        std::string_view directoryPath,
        const Frontier::SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
        std::string* outError = nullptr);

    [[nodiscard]] std::span<const FluidPresetRecord> QueryPresets() const noexcept { return Presets; }
    [[nodiscard]] const FluidPresetRecord* FindById(std::string_view presetId) const noexcept;

private:
    std::vector<FluidPresetRecord> Presets {};
};

/// Evaluates `preset.GasStack` via EffectLayerStack to build runtime step/render/shimmer settings & active sources.
[[nodiscard]] FluidPresetRuntimeBundle EvaluateFluidPresetRuntime(
    const Frontier::SimulationSpace::EffectLayers::EffectLayerSpecificationSet& specs,
    const FluidPresetRecord& preset,
    double simulationTimeSeconds = 0.0);

//------------------------------------------------------------------------------------------------------------------------
//                                        TRACERS, DEBRIS & FLIPBOOK PNG EXPORT
//------------------------------------------------------------------------------------------------------------------------

/// Exact CPU mirror of FluidTracerDebrisCompute.slang::TracerUpdateComputeEntry (supports 8,192+ tracers).
void StepFluidTracersCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    uint32_t width,
    uint32_t height,
    float deltaTimeSeconds,
    float simulationTimeSeconds,
    float lifetimeSeconds,
    Frontier::SimulationSpace::EffectLayers::FluidTracerDebrisBufferSet& buffers);

/// Exact CPU mirror of FluidTracerDebrisCompute.slang::DebrisUpdateComputeEntry (supports 8,192+ debris, Rubble/Ember/Paper).
void StepFluidDebrisCpu(
    const FluidVolumeUniformBlock& volume,
    const FluidVolumeGridCpu& velocity,
    std::span<const FluidPresetSourceEntry> sources,
    const FluidDebrisSettings& settings,
    uint32_t width,
    uint32_t height,
    float deltaTimeSeconds,
    float simulationTimeSeconds,
    Frontier::SimulationSpace::EffectLayers::FluidTracerDebrisBufferSet& buffers);

/// Encodes an RGBA8 image (row-major top-to-bottom) into a standards-compliant PNG file (uncompressed zlib blocks).
[[nodiscard]] bool WriteRgba8PngFile(
    std::string_view filePath,
    uint32_t width,
    uint32_t height,
    std::span<const uint8_t> rgba8Bytes);

} // namespace Frontier::SimulationSpace::Fluid
