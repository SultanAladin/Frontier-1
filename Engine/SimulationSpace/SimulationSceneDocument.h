/*============================================================================================================================================
                                                   SIMULATIONSCENEDOCUMENT.H
============================================================================================================================================*/
// 🧩 Phase D25 Data-driven pure-TOML multi-domain simulation scene document (.VolumetricSpace) serializing and mounting
//    ForceFields, 3D Eulerian Fluid, ParticleSpace, Multi-Instance .SoftBody Deformables, 4-Wheel Vehicle & Jolt proxies.
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D25)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::SimulationSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                             UNIFIED SCENE DOCUMENT TYPES
//------------------------------------------------------------------------------------------------------------------------

struct SceneForceEntry
{
    std::string                    Identifier    {};
    ForceFields::ForceModelOrdinal ModelOrdinal  = ForceFields::ForceModelOrdinal::DirectionalWind;
    std::array<float, 3>           Position      { 0.0f, 0.0f, 0.0f };
    std::array<float, 3>           Direction     { 1.0f, 0.0f, 0.0f };
    float                          Strength      = 4.0f;
    float                          RadiusMeters  = 8.0f;
    ForceFields::ForceFalloffOrdinal Falloff     = ForceFields::ForceFalloffOrdinal::Constant;
};

struct SceneFluidSourceEntry
{
    std::string          Identifier   {};
    std::array<float, 3> Position     { 0.0f, 0.5f, 0.0f };
    float                RadiusMeters = 0.4f;
    std::array<float, 3> VelocityMps  { 0.0f, 2.0f, 0.0f };
    float                Density      = 1.5f;
    float                Temperature  = 2.5f;
    float                Fuel         = 1.0f;
};

struct SceneParticleCloudEntry
{
    std::string          Identifier     {};
    std::array<float, 3> Position       { 0.0f, 0.5f, 0.0f };
    float                RadiusMeters   = 0.35f;
    uint32_t             ParticleCount  = 256u;
    float                ReleaseRate    = 12.0f;
    float                SpeedMps       = 1.5f;
    float                LifetimeSec    = 2.4f;
    float                GravityY       = 0.15f;
    float                GasInfluence   = 0.85f;
    float                FieldInfluence = 0.85f;
};

struct SceneSoftBodyPlacement
{
    std::string          Label          {};
    std::string          SoftBodyPath   {};
    std::array<float, 3> WorldOrigin    { 0.0f, 0.0f, 0.0f };
};

struct SceneRigidColliderEntry
{
    uint32_t             BodySlotIndex  = 1u;
    std::array<float, 3> CenterMeters   { 0.0f, 1.0f, 0.0f };
    float                RadiusMeters   = 0.4f;
    bool                 DynamicReply   = true;
};

struct SimulationSceneDocument
{
    std::string                          Title                  { "UntitledSimulationScene" };
    uint32_t                             SchemaVersion          = 1u;
    float                                FixedStepHz            = 60.0f;
    std::array<uint32_t, 3>              FluidGridCells         { 16u, 16u, 16u };
    float                                FluidCellSizeMeters    = 0.22f;
    std::array<float, 3>                 FluidCentreMeters      { 0.0f, 1.75f, 0.0f };
    float                                FluidBuoyancy          = 2.0f;
    float                                FluidCombustion        = 1.2f;
    uint32_t                             FluidPressureIters     = 12u;
    bool                                 EnableVehicleCoupling  = true;
    std::string                          ShippedVertexAtlasPath {};
    std::array<float, 3>                 CameraWorldPosition    { 4.10f, 2.35f, 4.95f };
    std::vector<SceneForceEntry>         Forces                 {};
    std::vector<SceneFluidSourceEntry>   FluidSources           {};
    std::vector<SceneParticleCloudEntry> ParticleClouds         {};
    std::vector<SceneSoftBodyPlacement>  SoftBodies             {};
    std::vector<SceneRigidColliderEntry> RigidColliders         {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                SIMULATION SCENE CODEC
//------------------------------------------------------------------------------------------------------------------------

class SimulationSceneCodec
{
public:
    /// Constructs the canonical 5-domain Trackside Coupled Showcase document.
    [[nodiscard]] static SimulationSceneDocument BuildTracksideShowcaseDocument();

    /// Encodes a SimulationSceneDocument into canonical pure-TOML text.
    [[nodiscard]] static std::string EncodeToTomlText(const SimulationSceneDocument& doc);

    /// Decodes a SimulationSceneDocument from pure-TOML text via Frontier's zero-dependency TomlSubsetParser.
    [[nodiscard]] static bool DecodeFromTomlText(
        std::string_view tomlText,
        SimulationSceneDocument& outDoc,
        std::string* outError = nullptr);

    [[nodiscard]] static bool EncodeToFile(
        const std::filesystem::path& targetPath,
        const SimulationSceneDocument& doc,
        std::string* outError = nullptr);

    [[nodiscard]] static bool DecodeFromFile(
        const std::filesystem::path& sourcePath,
        SimulationSceneDocument& outDoc,
        std::string* outError = nullptr);

    /// Builds SimulationCoordinatorParameters matching the scene document's Fluid & Deformable settings.
    [[nodiscard]] static SimulationCoordinatorParameters BuildCoordinatorParameters(const SimulationSceneDocument& doc);

    /// Mounts all ForceFields, Fluid sources, Particle clouds, .SoftBody instances, and RigidColliderProxies
    /// from `doc` into an initialized SimulationCoordinatorExtension.
    [[nodiscard]] static bool MountIntoCoordinator(
        const SimulationSceneDocument& doc,
        SimulationCoordinatorExtension& coordinator,
        std::string* outError = nullptr);

    /// Enumerates all shipped .VolumetricSpace scene files in `scenesDirectory` in deterministic sorted order.
    [[nodiscard]] static std::vector<std::filesystem::path> EnumerateShippedScenes(
        const std::filesystem::path& scenesDirectory = "EngineContent/Simulation/Scenes");
};

} // namespace Frontier::SimulationSpace
