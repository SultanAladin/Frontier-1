/*============================================================================================================================================
                                                     VEHICLETIREDEFORMBRIDGE.H
============================================================================================================================================*/
// 🧩 Phase D11 4-wheel Jolt VehicleDynamicsSolver -> Deformable FEM / Pneumatic XPBD / 0-CPU VAT LOD bridge
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §4 (D11)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Deformation/CageWeightTypes.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasPlayer.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h"
#include "Engine/SimulationSpace/Deformable/DeformableTypes.h"

#include <array>
#include <cstdint>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  VEHICLE WHEEL COUPLING TYPES
//------------------------------------------------------------------------------------------------------------------------

enum class WheelCornerOrdinal : uint8_t
{
    FrontLeft  = 0u,
    FrontRight = 1u,
    RearLeft   = 2u,
    RearRight  = 3u,
    Count      = 4u
};

enum class WheelSolverTier : uint8_t
{
    LiveFem3D         = 0u, // Full 1,080-tet 3D Stable Neo-Hookean FEM + Nanson cofactor GPU cage
    LivePneumaticXpbd = 1u, // 48x9 Pneumatic XPBD shell + quad-cage GPU skinning
    BakedVatZeroCpu   = 2u  // 0.00 ms CPU physics playback from baked RGBA32F Vertex Motion Atlas
};

/// Jolt-header-free mirror of PhysicsSpace::WheelCornerReading physical fields driving deformable tire response.
struct VehicleWheelDeformInput
{
    bool  InContact              = true;
    float VerticalLoadNewtons    = 3800.0f; // [N]     - Pacejka contact normal load Fz
    float AngularVelocityRadS    = 0.0f;    // [rad/s] - Wheel spin rate omega
    float SpinAngleRad           = 0.0f;    // [rad]   - Visual wheel rotation angle
    float SuspensionCompressionM = 0.0f;    // [m]     - Positive upward jounce compression
    float DynamicCamberRad       = 0.0f;    // [rad]   - Suspension camber angle
    float SteerAngleRad          = 0.0f;    // [rad]   - Steered wheel angle
    float CarcassTempKelvin      = 293.15f; // [K]     - Thermal carcass temperature (drives Ideal Gas pressure rise)
    float ColdPressurePsi        = 32.0f;   // [psi]   - Cold inflation pressure at 293.15 K
    float RoadBumpHeightMeters   = 0.0f;    // [m]     - Local curb / pothole obstacle height above road plane
    float RoadBumpOffsetXMeters  = 0.0f;    // [m]     - Longitudinal offset of curb obstacle relative to wheel hub
};

struct VehicleTireLodPolicy
{
    float LiveFemMaxDistanceMeters  = 18.0f; // [m] - Within 18 m -> Live 3D Neo-Hookean FEM
    float LiveXpbdMaxDistanceMeters = 45.0f; // [m] - 18..45 m -> Live Pneumatic XPBD; beyond -> Baked 0-CPU VAT
    float ReferenceCornerLoadN      = 3800.0f; // [N] - Nominal 1.0x quarter-car static wheel load
    float AmbientTempKelvin         = 293.15f; // [K] - 20 C reference ambient temperature
    bool  AllowAutoLodSwitch        = true;
};

struct VehicleWheelDeformTelemetry
{
    WheelCornerOrdinal                                 Corner                   = WheelCornerOrdinal::FrontLeft;
    WheelSolverTier                                    ActiveTier               = WheelSolverTier::LiveFem3D;
    float                                              EffectivePressurePsi     = 32.0f;  // [psi]
    float                                              NormalizedAxleLoad       = 1.0f;   // [x]
    float                                              RadialDeflectionMeters   = 0.0f;   // [m]
    float                                              ContactPatchLengthMeters = 0.0f;   // [m]
    float                                              PeakStrain               = 0.0f;   // [-]
    GeometrySpace::Deformation::AtlasPlaybackReading   VatReading               {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                               VEHICLE TIRE DEFORM BRIDGE
//------------------------------------------------------------------------------------------------------------------------

class VehicleTireDeformBridge
{
public:
    /// Initializes all 4 wheel corners with shared 7,552-vert sculpted tire rest geometry, FEM + XPBD cages,
    /// and an optional pre-baked Vertex Motion Atlas for distant 0-CPU LOD playback.
    void Initialize(
        const VehicleTireLodPolicy& policy = {},
        const GeometrySpace::Deformation::VertexMotionAtlas* sharedBakedAtlas = nullptr);

    /// Selects the active solver tier for a wheel based on camera distance and LOD policy.
    [[nodiscard]] WheelSolverTier SelectTierForDistance(float cameraDistanceMeters) const noexcept;

    /// Steps all 4 vehicle wheels for one simulation tick from Jolt WheelCornerReading inputs & camera distance.
    void StepAllWheels(
        const std::array<VehicleWheelDeformInput, 4u>& wheelInputs,
        float dtSeconds,
        float simTimeSeconds,
        float cameraDistanceMeters);

    /// Evaluates the deformed 7,552-vertex high-poly surface for one wheel corner (from Live FEM, Live XPBD, or VAT).
    void EvaluateWheelSurface(
        WheelCornerOrdinal corner,
        GeometrySpace::Deformation::DeformedSurfaceSample& outSample) const;

    [[nodiscard]] const VehicleWheelDeformTelemetry& InspectTelemetry(WheelCornerOrdinal corner) const noexcept
    {
        return Telemetry_[static_cast<size_t>(corner)];
    }

    [[nodiscard]] const GeometrySpace::Deformation::HighPolySurfaceRest& InspectSharedTireSurface() const noexcept
    {
        return SharedSurface_;
    }

    [[nodiscard]] bool Initialized() const noexcept { return Initialized_; }

private:
    struct WheelSlot
    {
        DeformableBody                                    FemBody      {};
        DeformableSimulationConfiguration                 FemConfig    {};
        DeformableBody                                    XpbdBody     {};
        DeformableSimulationConfiguration                 XpbdConfig   {};
        GeometrySpace::Deformation::VertexMotionAtlasPlayer VatPlayer  {};
    };

    VehicleTireLodPolicy                                 Policy_         {};
    GeometrySpace::Deformation::HighPolySurfaceRest      SharedSurface_  {};
    GeometrySpace::Deformation::CageEmbeddedSurface      FemEmbedded_    {};
    GeometrySpace::Deformation::CageEmbeddedSurface      XpbdEmbedded_   {};
    const GeometrySpace::Deformation::VertexMotionAtlas* SharedAtlas_    = nullptr;
    std::array<WheelSlot, 4u>                            Wheels_         {};
    std::array<VehicleWheelDeformTelemetry, 4u>          Telemetry_      {};
    bool                                                 Initialized_    = false;
};

} // namespace Frontier::SimulationSpace::Deformable
