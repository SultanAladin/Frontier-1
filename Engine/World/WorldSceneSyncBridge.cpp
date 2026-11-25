/*============================================================================================================================================
                                                       WORLDSCENESYNCBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of O(rebuilt) transform streaming & atmosphere synchronization into WorldSceneSpecification

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/WorldSceneSyncBridge.h"

#include <algorithm>
#include <cmath>

namespace Frontier::World
{

namespace
{

[[nodiscard]] bool DifferAboveEpsilon(float ValueA, float ValueB, float Epsilon = 1.0e-4f) noexcept
{
    return std::fabs(ValueA - ValueB) > Epsilon;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  WORLD SCENE SYNC BRIDGE
//------------------------------------------------------------------------------------------------------------------------

void WorldSceneSyncBridge::ClearBindings() noexcept
{
    ClusterIndexBySlot.clear();
    ActiveBindingCount = 0u;
}

void WorldSceneSyncBridge::BindSurfaceToClusterInstance(InstanceToken Token, uint32_t ClusterInstanceIndex)
{
    if (!Token.QueryValid())
    {
        return;
    }

    if (Token.SlotIndex >= ClusterIndexBySlot.size())
    {
        ClusterIndexBySlot.resize(static_cast<size_t>(Token.SlotIndex) + 1u, UnboundClusterSlot);
    }

    if (ClusterIndexBySlot[Token.SlotIndex] == UnboundClusterSlot)
    {
        ++ActiveBindingCount;
    }
    ClusterIndexBySlot[Token.SlotIndex] = ClusterInstanceIndex;
}

void WorldSceneSyncBridge::UnbindSurfaceCluster(InstanceToken Token) noexcept
{
    if (!Token.QueryValid() || (Token.SlotIndex >= ClusterIndexBySlot.size()))
    {
        return;
    }

    if (ClusterIndexBySlot[Token.SlotIndex] != UnboundClusterSlot)
    {
        ClusterIndexBySlot[Token.SlotIndex] = UnboundClusterSlot;
        if (ActiveBindingCount > 0u)
        {
            --ActiveBindingCount;
        }
    }
}

uint32_t WorldSceneSyncBridge::QueryBoundClusterIndex(InstanceToken Token) const noexcept
{
    if (!Token.QueryValid() || (Token.SlotIndex >= ClusterIndexBySlot.size()))
    {
        return UnboundClusterSlot;
    }

    return ClusterIndexBySlot[Token.SlotIndex];
}

uint32_t WorldSceneSyncBridge::FlushRebuiltTransformsToScene(
    const WorldInstanceRegister&              SourceRegister,
    GeometricRaster::WorldSceneSpecification& TargetScene) const noexcept
{
    const WorldTransformStorage& Transforms = SourceRegister.InspectTransforms();
    if (!Transforms.RequireRebuild || Transforms.RebuildTransformSlots.empty() || ClusterIndexBySlot.empty())
    {
        return 0u;
    }

    const uint32_t ClusterCount = static_cast<uint32_t>(TargetScene.ClusterInstances.size());
    uint32_t SyncedCount        = 0u;

    for (const uint32_t TransformSlot : Transforms.RebuildTransformSlots)
    {
        if ((TransformSlot >= Transforms.OwnerTokens.size()) ||
            (Transforms.TransformRequireRebuild[TransformSlot] == 0u))
        {
            continue;
        }

        const InstanceToken Owner = Transforms.OwnerTokens[TransformSlot];
        if (!Owner.QueryValid() || (Owner.SlotIndex >= ClusterIndexBySlot.size()))
        {
            continue;
        }

        const uint32_t ClusterIndex = ClusterIndexBySlot[Owner.SlotIndex];
        if ((ClusterIndex == UnboundClusterSlot) || (ClusterIndex >= ClusterCount))
        {
            continue;
        }

        GeometricRaster::RigidClusterInstance& Cluster = TargetScene.ClusterInstances[ClusterIndex];
        Cluster.PreviousTranslation = Cluster.WorldTranslation;
        Cluster.WorldTranslation    = {
            Transforms.PositionMetersX[TransformSlot],
            Transforms.PositionMetersY[TransformSlot],
            Transforms.PositionMetersZ[TransformSlot]
        };
        Cluster.RotationZDegrees = Transforms.RotationDegreesZ[TransformSlot];
        const float ScaleX       = Transforms.ScaleScalarX[TransformSlot];
        const float ScaleY       = Transforms.ScaleScalarY[TransformSlot];
        const float ScaleZ       = Transforms.ScaleScalarZ[TransformSlot];
        Cluster.NonUniformScale  = { ScaleX, ScaleY, ScaleZ };
        Cluster.UniformScale     = (ScaleX + ScaleY + ScaleZ) / 3.0f;
        ++SyncedCount;
    }

    return SyncedCount;
}

bool WorldSceneSyncBridge::SynchronizeAtmosphereToScene(
    const WorldInstanceRegister&              SourceRegister,
    const WorldLoopStepMetrics&               Metrics,
    GeometricRaster::WorldSceneSpecification& TargetScene,
    float                                     Δt) noexcept
{
    bool SceneMutated = false;
    GeometricRaster::WorldAtmosphereConfiguration& Atmo = TargetScene.Atmosphere;

    if (Metrics.EvaluatedSolarCount > 0u)
    {
        const SolarInstanceRegister& Solar = SourceRegister.InspectSolarRegister();
        if (Solar.QueryActiveCount() > 0u)
        {
            const bool Visible = (Solar.VisibilityEnabled[0] != 0u);
            if (Atmo.Solar.SolarEnabled != Visible)
            {
                Atmo.Solar.SolarEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Solar.ManualAzimuthDegrees, Solar.AzimuthDegrees[0]))
            {
                Atmo.Solar.ManualAzimuthDegrees    = Solar.AzimuthDegrees[0];
                Atmo.Solar.ManualDirectionOverride = true;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Solar.ManualElevationDegrees, Solar.ElevationDegrees[0]))
            {
                Atmo.Solar.ManualElevationDegrees = Solar.ElevationDegrees[0];
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedLunarCount > 0u)
    {
        const LunarInstanceRegister& Lunar = SourceRegister.InspectLunarRegister();
        const uint32_t Count               = Lunar.QueryActiveCount();
        for (uint32_t i = 0u; i < Count; ++i)
        {
            const uint32_t MoonSlot = Lunar.MoonSlotOrdinal[i];
            if (MoonSlot >= Atmo.Moons.size())
            {
                continue;
            }

            GeometricRaster::LunarBodyConfiguration& TargetMoon = Atmo.Moons[MoonSlot];
            const bool  Visible   = (Lunar.VisibilityEnabled[i] != 0u);
            const float PhaseFrac = std::clamp(Lunar.PhaseAngleDegrees[i] / 360.0f, 0.0f, 1.0f);

            if (TargetMoon.Visible != Visible)
            {
                TargetMoon.Visible = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.AzimuthDegrees, Lunar.RightAscensionDegrees[i]))
            {
                TargetMoon.AzimuthDegrees = Lunar.RightAscensionDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.ElevationDegrees, Lunar.DeclinationDegrees[i]))
            {
                TargetMoon.ElevationDegrees = Lunar.DeclinationDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.AngularDiameterDegrees, Lunar.AngularDiameterDegrees[i]))
            {
                TargetMoon.AngularDiameterDegrees = Lunar.AngularDiameterDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.SynodicPhaseFraction, PhaseFrac))
            {
                TargetMoon.SynodicPhaseFraction = PhaseFrac;
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedAtmosphereCount > 0u)
    {
        const AtmosphereInstanceRegister& Medium = SourceRegister.InspectAtmosphereRegister();
        if (Medium.QueryActiveCount() > 0u)
        {
            const bool Visible = (Medium.VisibilityEnabled[0] != 0u);
            if (Atmo.Medium.AtmosphereEnabled != Visible)
            {
                Atmo.Medium.AtmosphereEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.RayleighScaleHeightMeters, Medium.RayleighScaleMeters[0]))
            {
                Atmo.Medium.RayleighScaleHeightMeters = Medium.RayleighScaleMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.MieScaleHeightMeters, Medium.MieScaleMeters[0]))
            {
                Atmo.Medium.MieScaleHeightMeters = Medium.MieScaleMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.OzoneStrength, Medium.OzoneDensityMultiplier[0]))
            {
                Atmo.Medium.OzoneStrength = Medium.OzoneDensityMultiplier[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.ObserverAltitudeMeters, Medium.ObserverAltitudeMeters[0]))
            {
                Atmo.Medium.ObserverAltitudeMeters = Medium.ObserverAltitudeMeters[0];
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedCloudCount > 0u)
    {
        const CloudInstanceRegister& Clouds = SourceRegister.InspectCloudRegister();
        if (Clouds.QueryActiveCount() > 0u)
        {
            const bool Visible = (Clouds.VisibilityEnabled[0] != 0u);
            if (Atmo.Clouds.CloudsEnabled != Visible)
            {
                Atmo.Clouds.CloudsEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.CloudFloorMeters, Clouds.BaseAltitudeMeters[0]))
            {
                Atmo.Clouds.CloudFloorMeters = Clouds.BaseAltitudeMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.LayerThicknessMeters, Clouds.ThicknessMeters[0]))
            {
                Atmo.Clouds.LayerThicknessMeters = Clouds.ThicknessMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.CloudFraction, Clouds.DensityFraction[0]))
            {
                Atmo.Clouds.CloudFraction = Clouds.DensityFraction[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.WindSpeedMetersPerSecond, Clouds.WindSpeedMetersPerS[0]))
            {
                Atmo.Clouds.WindSpeedMetersPerSecond = Clouds.WindSpeedMetersPerS[0];
                SceneMutated = true;
            }
            if ((Δt > 0.0f) && (Clouds.WindSpeedMetersPerS[0] != 0.0f))
            {
                Atmo.SimulationSeconds += Δt;
                SceneMutated = true;
            }
        }
    }

    return SceneMutated;
}

} // namespace Frontier::World
