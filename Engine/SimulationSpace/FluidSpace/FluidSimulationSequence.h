/*============================================================================================================================================
                                                         FLUIDSIMULATIONSEQUENCE.H
============================================================================================================================================*/
// 🧩 Fixed-step 3D Eulerian Fire/Smoke solver + Dual-tier World Drift Grid + ForceFields/Particle/GI coupling (CPU & Vulkan Compute)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/DistanceFieldSpace/SignedDistanceVolume.h"
#include "Engine/Mathematics/LinearColors.h"
#include "Engine/ParticleSpace/ParticleBuffers.h"
#include "Engine/SimulationSpace/FluidSpace/FluidDescriptors.h"
#include "Engine/SimulationSpace/FluidSpace/FluidForceCoupler.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGiLightExtractor.h"
#include "Engine/SimulationSpace/FluidSpace/FluidGridBuffers.h"
#include "Engine/SimulationSpace/FluidSpace/FluidObstacleVoxelizer.h"
#include "Engine/SimulationSpace/FluidSpace/FluidParticleCoupler.h"
#include "Engine/SimulationSpace/FluidSpace/FluidWorldGridPartition.h"
#include "Engine/SimulationSpace/ForceFields/FieldLatticeCascade.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldAccumulator.h"
#include "Engine/SpatialAcceleration/WideBoundingTree.h"

#include <span>
#include <vector>

namespace Frontier::SimulationSpace::FluidSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct FluidRaymarchResult
{
    Mathematics::LinearRGB RadianceHdr   {};
    float                  Transmittance = 1.0f;
};

struct FluidStepInput
{
    const ForceFields::ForceFieldAccumulator*      ForceFieldSource   = nullptr;
    ForceFields::ForceFieldAccumulator*            MutableForceFields = nullptr;
    const ForceFields::FieldLatticeCascade*        WindLatticeCascade = nullptr;
    const DistanceFieldSpace::SignedDistanceVolume* SceneSdf          = nullptr;
    const SpatialAcceleration::WideBoundingTree*   SceneTree          = nullptr;
    std::span<ParticleSpace::ParticleSlot>         ExternalParticles  {};
    Mathematics::Axis3D                            WorldFocusPoint    { 0.0f, 0.0f, 0.0f };
    bool                                           BakeGiObstaclesNow = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FLUID SIMULATION SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

class FluidSimulationSequence
{
public:
    FluidSimulationSequence();

    void Configure(const FluidSceneDocument& Document);
    void ResetSimulation();

    void TriggerBlast(
        const FluidBlastSpecification& Blast,
        const FluidStepInput&        Context = {});

    void ApplyPyroBrush(
        const Mathematics::Axis3D& WorldPosition,
        const Mathematics::Axis3D& BrushVelocityMps,
        float                      BrushRadiusMeters,
        float                      Intensity = 1.0f);

    void StepSimulation(
        float                   DeltaSeconds,
        const FluidStepInput& Context = {});

    // Evaluates depth-clipped Hero + World Drift fluid radiance & transmittance over [0, MaxDistanceMeters]
    [[nodiscard]] FluidRaymarchResult EvaluateFluidSegment(
        const Mathematics::Axis3D&    RayOriginWorld,
        const Mathematics::Axis3D&    RayDirection,
        float                         MaxDistanceMeters,
        const Mathematics::Axis3D&    SunDirection,
        const Mathematics::LinearRGB& SolarIrradiance,
        const Mathematics::LinearRGB& SkyRadiance,
        float                         PixelJitter01 = 0.5f) const noexcept;

    // Evaluates volumetric smoke shadow transmittance [0..1] for GI Sun, Area Light & DDGI rays
    [[nodiscard]] float EvaluateFluidShadowTransmittance(
        const Mathematics::Axis3D& RayOriginWorld,
        const Mathematics::Axis3D& UnitDirToLight,
        float                      MaxDistanceMeters = 40.0f) const noexcept;

    // Samples combined Hero + World Drift smoke/temp/velocity at WorldPoint (used by DDGI/PathTrace/Particles)
    [[nodiscard]] FluidVoxelSample4 SampleThermoAtWorldPoint(const Mathematics::Axis3D& WorldPoint) const noexcept;
    [[nodiscard]] FluidVoxelSample4 SampleVelocityAtWorldPoint(const Mathematics::Axis3D& WorldPoint) const noexcept;

    [[nodiscard]] const FluidSceneDocument&                 InspectDocument() const noexcept       { return Document; }
    [[nodiscard]] FluidSceneDocument&                       AccessDocument() noexcept              { return Document; }
    [[nodiscard]] const FluidGridVolume&                    InspectHeroGrid() const noexcept       { return HeroGrid; }
    [[nodiscard]] const FluidWorldDriftVolume&              InspectWorldDriftGrid() const noexcept { return WorldGrid; }
    [[nodiscard]] const FluidParticleCoupler&               InspectParticleCoupler() const noexcept { return ParticleCoupler; }
    [[nodiscard]] const std::vector<FluidEmissiveLightProxy>& InspectEmissiveLights() const noexcept { return EmissiveLights; }
    [[nodiscard]] float                                     QueryElapsedSeconds() const noexcept   { return ElapsedSeconds; }
    [[nodiscard]] uint64_t                                  QueryStepCount() const noexcept        { return StepCount; }
    [[nodiscard]] float                                     QueryShockwaveAge() const noexcept     { return ShockwaveAge; }
    [[nodiscard]] const Mathematics::Axis3D&                QueryShockwaveCenter() const noexcept  { return ShockwaveCenter; }

private:
    void ExecuteSplatAndObstacles(float Dt, const FluidStepInput& Context);
    void ExecuteMacCormackAdvection(float Dt);
    void ExecuteVorticityAndCombustion(float Dt, const FluidStepInput& Context);
    void ExecuteDivergenceAndPressure(float Dt);

    FluidSceneDocument                    Document           {};
    FluidGridVolume                       HeroGrid           {};
    FluidWorldDriftVolume                 WorldGrid          {};
    FluidWorldGridPartition                 WorldPartition       {};
    FluidParticleCoupler                  ParticleCoupler    {};
    std::vector<FluidBlastSpecification>  PendingBlasts      {};
    std::vector<FluidParticleSplat>       PendingSplats      {};
    std::vector<FluidEmissiveLightProxy>  EmissiveLights     {};
    bool                                  ObstaclesBakedOnce = false;
    float                                 ElapsedSeconds     = 0.0f;
    uint64_t                              StepCount          = 0ull;
    float                                 DynamicSurge01     = 0.0f;
    float                                 DynamicTarget01    = 0.0f;
    float                                 ShockwaveAge       = 99.0f;
    Mathematics::Axis3D                   ShockwaveCenter    { 0.0f, 0.0f, 0.25f };

    struct BrushStroke
    {
        Mathematics::Axis3D Position {};
        Mathematics::Axis3D Velocity {};
        float               Radius   = 0.12f;
        float               Gain     = 1.0f;
    };
    std::vector<BrushStroke> PendingBrushes {};
};

} // namespace Frontier::SimulationSpace::FluidSpace
