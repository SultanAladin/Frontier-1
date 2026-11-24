/*============================================================================================================================================
                                                    VEHICLETIREDEFORMBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Phase D11 4-wheel Jolt VehicleDynamicsSolver -> Deformable FEM / Pneumatic XPBD / 0-CPU VAT LOD bridge

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/VehicleTireDeformBridge.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                               VEHICLE TIRE DEFORM BRIDGE
//------------------------------------------------------------------------------------------------------------------------

void VehicleTireDeformBridge::Initialize(
    const VehicleTireLodPolicy& policy,
    const GeometrySpace::Deformation::VertexMotionAtlas* sharedBakedAtlas)
{
    Policy_      = policy;
    SharedAtlas_ = sharedBakedAtlas;

    // Build shared 7,552-vertex / 12,544-triangle sculpted sport tire surface once for all 4 wheels
    SharedSurface_ = GeometrySpace::Deformation::TireSurfaceBuilder::ConstructSportTireSurface(
        0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);

    for (size_t i = 0u; i < 4u; ++i)
    {
        auto& slot = Wheels_[i];
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, slot.FemBody, slot.FemConfig);
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTirePneumatic, slot.XpbdBody, slot.XpbdConfig);
        slot.VatPlayer.SetAtlas(SharedAtlas_);
        slot.VatPlayer.SetPlaying(true);

        Telemetry_[i]        = {};
        Telemetry_[i].Corner = static_cast<WheelCornerOrdinal>(i);
    }

    // Compute shared rest-pose cage embeddings once (all 4 wheel cages share identical local topology)
    FemEmbedded_ = GeometrySpace::Deformation::TetrahedralCageEmbedder::EmbedIntoFemSolid(Wheels_[0].FemBody, SharedSurface_);
    const auto xpbdRestSurf = GeometrySpace::Deformation::TireSurfaceBuilder::ConstructSportTireSurface(
        0.0f, -0.62f, 0.0f, 0.66f, 1.14f, 0.42f);
    XpbdEmbedded_ = GeometrySpace::Deformation::SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(Wheels_[0].XpbdBody, xpbdRestSurf);
    Initialized_  = true;
}

WheelSolverTier VehicleTireDeformBridge::SelectTierForDistance(float cameraDistanceMeters) const noexcept
{
    if (!Policy_.AllowAutoLodSwitch)
    {
        return WheelSolverTier::LiveFem3D;
    }
    if (cameraDistanceMeters <= Policy_.LiveFemMaxDistanceMeters)
    {
        return WheelSolverTier::LiveFem3D;
    }
    if (cameraDistanceMeters <= Policy_.LiveXpbdMaxDistanceMeters)
    {
        return WheelSolverTier::LivePneumaticXpbd;
    }
    return (SharedAtlas_ != nullptr && SharedAtlas_->Ready())
        ? WheelSolverTier::BakedVatZeroCpu
        : WheelSolverTier::LivePneumaticXpbd;
}

void VehicleTireDeformBridge::StepAllWheels(
    const std::array<VehicleWheelDeformInput, 4u>& wheelInputs,
    float dtSeconds,
    float simTimeSeconds,
    float cameraDistanceMeters)
{
    if (!Initialized_)
    {
        return;
    }

    const WheelSolverTier tier = SelectTierForDistance(cameraDistanceMeters);
    const float refLoad        = std::max(500.0f, Policy_.ReferenceCornerLoadN);
    const float refTemp        = std::max(200.0f, Policy_.AmbientTempKelvin);

    for (size_t i = 0u; i < 4u; ++i)
    {
        const VehicleWheelDeformInput& in = wheelInputs[i];
        WheelSlot& slot                   = Wheels_[i];
        VehicleWheelDeformTelemetry& tel  = Telemetry_[i];

        tel.Corner               = static_cast<WheelCornerOrdinal>(i);
        tel.ActiveTier           = tier;
        const float tempRatio    = std::clamp(in.CarcassTempKelvin / refTemp, 0.75f, 1.65f);
        tel.EffectivePressurePsi = std::clamp(in.ColdPressurePsi * tempRatio, 10.0f, 65.0f);
        tel.NormalizedAxleLoad   = in.InContact ? std::clamp(in.VerticalLoadNewtons / refLoad, 0.0f, 3.0f) : 0.0f;

        if (tier == WheelSolverTier::BakedVatZeroCpu && SharedAtlas_ != nullptr && SharedAtlas_->Ready())
        {
            // 0.00 ms CPU physics path: advance VAT playback proportional to wheel angular speed
            const float speedScale = std::clamp(std::fabs(in.AngularVelocityRadS) / 5.5f, 0.0f, 3.0f);
            slot.VatPlayer.SetPlaybackSpeed(speedScale);
            tel.VatReading = slot.VatPlayer.AdvanceAndSamplePose(dtSeconds);

            // Analytic deflection & contact patch chord from radial load & inflation pressure
            const float stiffnessNPerM = 1.6e5f * (tel.EffectivePressurePsi / 32.0f);
            const float defl = in.InContact ? std::clamp(in.VerticalLoadNewtons / stiffnessNPerM, 0.0f, 0.14f) : 0.0f;
            const float outerR = 1.15f;
            tel.RadialDeflectionMeters   = defl;
            tel.ContactPatchLengthMeters = (defl > 1.0e-5f) ? 2.0f * std::sqrt(std::max(0.0f, 2.0f * outerR * defl - defl * defl)) : 0.0f;
            tel.PeakStrain               = defl * 4.2f;
            continue;
        }

        DeformableBody& body = (tier == WheelSolverTier::LiveFem3D) ? slot.FemBody : slot.XpbdBody;
        DeformableSimulationConfiguration& cfg = (tier == WheelSolverTier::LiveFem3D) ? slot.FemConfig : slot.XpbdConfig;

        cfg.TirePressurePsi   = tel.EffectivePressurePsi;
        cfg.TireAxleLoad      = tel.NormalizedAxleLoad;
        cfg.TireSpinRadPerSec = std::clamp(in.AngularVelocityRadS, -24.0f, 24.0f);

        const bool hasBump = in.RoadBumpHeightMeters > 0.01f;
        cfg.ColliderEnabled  = hasBump;
        body.Collider.Active = hasBump;
        if (hasBump)
        {
            body.Collider.RadiusMeters    = std::clamp(in.RoadBumpHeightMeters * 1.8f, 0.12f, 0.55f);
            body.Collider.CenterMeters[0] = in.RoadBumpOffsetXMeters;
            body.Collider.CenterMeters[1] = body.GroundY + in.RoadBumpHeightMeters - body.Collider.RadiusMeters * 0.5f;
            body.Collider.CenterMeters[2] = 0.0f;
        }

        XpbdConstraintSolver::Step(body, dtSeconds, cfg, simTimeSeconds, 0.0f);

        // Measure live radial deflection, contact patch length & peak 3D strain directly from simulated carcass
        float minY      = 1.0e9f;
        float minContactX = 1.0e9f;
        float maxContactX = -1.0e9f;
        float peakStrain  = 0.0f;
        for (uint32_t p = 0u; p < body.PointCount; ++p)
        {
            const float px = body.Positions[static_cast<size_t>(p) * 3u + 0u];
            const float py = body.Positions[static_cast<size_t>(p) * 3u + 1u];
            minY       = std::min(minY, py);
            peakStrain = std::max(peakStrain, body.PointStrain[p]);
            if (py <= body.GroundY + 0.025f)
            {
                minContactX = std::min(minContactX, px);
                maxContactX = std::max(maxContactX, px);
            }
        }

        const float axleY  = (tier == WheelSolverTier::LiveFem3D) ? body.FemTires[0].AxleY : body.PneumaticTires[0].AxleY;
        const float outerR = (tier == WheelSolverTier::LiveFem3D) ? body.FemTires[0].OuterRadius : body.PneumaticTires[0].OuterRadius;
        const float bottomClearance = axleY - minY;
        tel.RadialDeflectionMeters   = std::max(0.0f, outerR - bottomClearance);
        tel.ContactPatchLengthMeters = (maxContactX > minContactX) ? (maxContactX - minContactX) : 0.0f;
        tel.PeakStrain               = peakStrain;
    }
}

void VehicleTireDeformBridge::EvaluateWheelSurface(
    WheelCornerOrdinal corner,
    GeometrySpace::Deformation::DeformedSurfaceSample& outSample) const
{
    if (!Initialized_)
    {
        return;
    }
    const size_t idx = static_cast<size_t>(corner);
    const auto& slot = Wheels_[idx];
    const auto& tel  = Telemetry_[idx];

    if (tel.ActiveTier == WheelSolverTier::BakedVatZeroCpu && SharedAtlas_ != nullptr && SharedAtlas_->Ready())
    {
        GeometrySpace::Deformation::VertexMotionAtlasPlayer::SampleInterpolatedSurface(*SharedAtlas_, tel.VatReading, outSample);
    }
    else if (tel.ActiveTier == WheelSolverTier::LiveFem3D)
    {
        GeometrySpace::Deformation::TetrahedralCageEmbedder::EvaluateLiveSurface(slot.FemBody, FemEmbedded_, outSample);
    }
    else
    {
        GeometrySpace::Deformation::SurfaceQuadCageEmbedder::EvaluateLiveSurface(slot.XpbdBody, XpbdEmbedded_, outSample);
    }
}

} // namespace Frontier::SimulationSpace::Deformable
