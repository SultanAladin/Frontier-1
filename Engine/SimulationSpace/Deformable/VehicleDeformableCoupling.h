/*============================================================================================================================================
                                                    VEHICLEDEFORMABLECOUPLING.H
============================================================================================================================================*/
// 🧩 Phase D18 Native adapter coupling Frontier 4-wheel VehicleReading corner telemetry into VehicleTireDeformBridge
//    with world-space sculpted tire & alloy rim ProceduralRenderSurface assembly (Live FEM / Live XPBD / 0-CPU VAT).
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D18)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"
#include "Engine/SimulationSpace/Deformable/VehicleTireDeformBridge.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 CHASSIS CORNER PLACEMENT
//------------------------------------------------------------------------------------------------------------------------

/// World-space placement and steering kinematics for one vehicle wheel corner.
struct VehicleWheelPlacement
{
    std::array<float, 3> WheelCenterMeters   { 0.0f, 0.0f, 0.0f }; // [m]   - Axle center in world space
    float                SteerYawRad         = 0.0f;               // [rad] - Steering rotation around world +Y
    float                CamberRollRad       = 0.0f;               // [rad] - Suspension camber angle around longitudinal axis
    float                WheelScale          = 0.34f;              // [-]   - Scale from canonical 1.15m studio tire to ~0.39m road tire
    bool                 RightSideMirror     = false;              // [-]   - True for FR/RR wheels facing +Z/+X outward
};

/// Lightweight Jolt-header-free snapshot mirroring PhysicsSpace::VehicleReading for 4-wheel coupling.
struct VehicleChassisCouplingInput
{
    std::array<float, 3>                 ChassisOriginMeters { 0.0f, 0.55f, 0.0f };
    float                                ChassisYawRad       = 0.0f;
    float                                ForwardSpeedMps     = 0.0f;
    float                                RoadPlaneY          = 0.0f;
    std::array<VehicleWheelDeformInput, 4u> CornerInputs     {};
    std::array<VehicleWheelPlacement, 4u>   CornerPlacements {};
};

struct VehicleCouplingSummary
{
    std::array<WheelSolverTier, 4u> ActiveTiers             {};
    std::array<float, 4u>           EffectivePressurePsi    {};
    std::array<float, 4u>           RadialDeflectionMeters  {};
    float                           PeakVehicleTireStrain   = 0.0f;
    uint32_t                        LiveFemWheelCount       = 0u;
    uint32_t                        LiveXpbdWheelCount      = 0u;
    uint32_t                        BakedVatWheelCount      = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 VEHICLE DEFORMABLE COUPLING
//------------------------------------------------------------------------------------------------------------------------

class VehicleDeformableCoupling
{
public:
    /// Initializes all 4 deformable wheel corners and loads an optional shipped .VertexAtlas binary file for 0-CPU VAT LOD.
    [[nodiscard]] bool Initialize(
        const VehicleTireLodPolicy& policy = {},
        const std::filesystem::path& optionalVertexAtlasPath = {},
        std::string* outError = nullptr);

    /// Initializes all 4 wheel corners from an already-loaded VertexMotionAtlas in memory.
    void InitializeWithAtlas(
        const VehicleTireLodPolicy& policy,
        const GeometrySpace::Deformation::VertexMotionAtlas* sharedAtlas);

    /// Steps all 4 wheels using either uniform camera distance or per-corner distance overrides.
    [[nodiscard]] VehicleCouplingSummary StepVehicle(
        const VehicleChassisCouplingInput& input,
        float dtSeconds,
        float simTimeSeconds,
        const std::array<float, 3>& cameraPositionMeters);

    /// Appends all 4 world-positioned deformed sculpted tires + 5-spoke forged alloy rims (and optional chassis body)
    /// into `outSurface` for direct rendering or Vulkan upload.
    void BuildWorldVehicleSurface(
        const VehicleChassisCouplingInput& input,
        GeometrySpace::Deformation::ProceduralRenderSurface& outSurface,
        bool appendChassisBody = true) const;

    /// Populates a canonical 4-wheel passenger/sport car chassis placement (wheelbase 2.74m, track 1.66m) at origin.
    [[nodiscard]] static VehicleChassisCouplingInput BuildCanonicalSportCarInput(
        const std::array<float, 3>& chassisOriginMeters = { 0.0f, 0.39f, 0.0f });

    [[nodiscard]] bool Ready() const noexcept { return TireDriver_.Initialized(); }
    [[nodiscard]] bool HasShippedVertexAtlas() const noexcept { return LoadedAtlas_.Ready(); }
    [[nodiscard]] const VehicleTireDeformBridge& InspectTireDriver() const noexcept { return TireDriver_; }
    [[nodiscard]] const VehicleCouplingSummary& InspectLastSummary() const noexcept { return LastSummary_; }

private:
    VehicleTireDeformBridge                         TireDriver_  {};
    GeometrySpace::Deformation::VertexMotionAtlas   LoadedAtlas_ {};
    VehicleCouplingSummary                          LastSummary_ {};
};

} // namespace Frontier::SimulationSpace::Deformable
