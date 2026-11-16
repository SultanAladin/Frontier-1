/*============================================================================================================================================
                                                        FRACTURESIMULATIONRUNNER.H
============================================================================================================================================*/
// 🧩 JoltPhysics v5.6.0 rigid-body fracture simulation runner: ConvexHull shards, BreakableBond strain rupture, and RHR +Z Up ground contacts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Fracture/FracturePartitionTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <cstdint>
#include <vector>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

enum class FractureCollisionShapeMode : uint8_t
{
    ConvexHull    = 0u, // [-] - Single outer ConvexHull wrapping each fragment
    CollisionMesh = 1u, // [-] - Exact fragment shape (StaticCompound decomposition on concave shards / tight exact hull)
};

struct FractureSimulationParameters
{
    Mathematics::Axis3D        SolidWorldOffsetM { 0.0f, 0.0f, 0.0f }; // [m]   - World placement offset applied to source fragments
    float                      GroundPlaneZ      = 0.0f;               // [m]   - RHR +Z Up ground collision plane height
    float                      Friction          = 0.65f;              // [-]   - Coulomb friction between shards and ground
    float                      Restitution       = 0.14f;              // [0-1] - Coefficient of restitution
    float                      LinearDamping     = 0.04f;              // [1/s] - Air drag damping
    float                      AngularDamping    = 0.08f;              // [1/s] - Rotational damping
    float                      FixedStepHz       = 120.0f;             // [Hz]  - Deterministic Jolt solver frequency
    bool                       EnableBonds       = true;               // [-]   - Registers BreakableBond constraints for intact edges
    bool                       ApplyImpactImpulse = true;              // [-]   - Applies initial fragment velocities / impact impulse
    FractureCollisionShapeMode CollisionMode     = FractureCollisionShapeMode::ConvexHull; // [-] - Fragment collision geometry mode
};

struct FractureBodyTransformState
{
    uint32_t                   FragmentIndex  = 0u;
    RigidBodyToken             Token          {};
    Mathematics::Axis3D        PositionMeters { 0.0f, 0.0f, 0.0f };
    RigidOrientationQuaternion Orientation    {};
    Mathematics::Axis3D        LinearVelocity { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D        AngularVelocity { 0.0f, 0.0f, 0.0f };
    float                      KineticEnergyJ = 0.0f;
    float                      PeakImpulseNs  = 0.0f;
    bool                       Awake          = false;
    bool                       UsingBoxFallback = false;
    CollisionShapeCategory     ActiveCollisionShape = CollisionShapeCategory::ConvexHull;
    uint32_t                   CollisionSubShapeCount = 1u;
};

struct FractureBondState
{
    uint32_t        BondIndex      = 0u;
    uint32_t        IndexA         = 0u;
    uint32_t        IndexB         = 0u;
    ConstraintToken Token          {};
    bool            Intact         = false;
    float           StrainRatio    = 0.0f;
    float           AppliedForceN  = 0.0f;
};

struct FractureSimulationTelemetry
{
    double                     SimulatedSeconds   = 0.0;
    uint64_t                   CompletedSteps     = 0ull;
    uint32_t                   FragmentCount      = 0u;
    uint32_t                   AwakeFragmentCount = 0u;
    uint32_t                   TotalBondCount     = 0u;
    uint32_t                   IntactBondCount    = 0u;
    uint32_t                   BrokenBondCount    = 0u;
    uint32_t                   ContactCount       = 0u;
    uint32_t                   TotalSubShapes     = 0u;
    FractureCollisionShapeMode CollisionMode      = FractureCollisionShapeMode::ConvexHull;
    double                     TotalKineticJ      = 0.0;
    float                      MaxSpeedMps        = 0.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class FractureSimulationRunner
{
public:
    FractureSimulationRunner() noexcept = default;
    ~FractureSimulationRunner() noexcept;

    FractureSimulationRunner(const FractureSimulationRunner&)            = delete;
    FractureSimulationRunner& operator=(const FractureSimulationRunner&) = delete;
    FractureSimulationRunner(FractureSimulationRunner&&)                 = delete;
    FractureSimulationRunner& operator=(FractureSimulationRunner&&)      = delete;

    [[nodiscard]] bool LoadPartitionResult(
        const GeometrySpace::Fracture::FracturePartitionResult& Partition,
        const FractureSimulationParameters&                     Params = {}) noexcept;

    void Clear() noexcept;
    void Step(double ΔtSeconds) noexcept;
    void TriggerImpactImpulse(
        const Mathematics::Axis3D& WorldPointMeters,
        const Mathematics::Axis3D& ImpulseNs,
        float                      InfluenceRadiusM = 0.45f) noexcept;

    [[nodiscard]] bool                                       QueryLoaded() const noexcept;
    [[nodiscard]] const std::vector<FractureBodyTransformState>& InspectBodyStates() const noexcept;
    [[nodiscard]] const std::vector<FractureBondState>&          InspectBondStates() const noexcept;
    [[nodiscard]] FractureSimulationTelemetry                InspectTelemetry() const noexcept;
    [[nodiscard]] const PhysicsExtension&                    InspectPhysics() const noexcept;
    [[nodiscard]] PhysicsExtension&                          AccessPhysics() noexcept;

private:
    void RefreshSnapshots() noexcept;

    PhysicsExtension                        Physics         {};
    FractureSimulationParameters            ActiveParams    {};
    RigidBodyToken                          GroundToken     {};
    std::vector<FractureBodyTransformState> BodyStates      {};
    std::vector<FractureBondState>          BondStates      {};
    std::vector<Mathematics::Axis3D>        InitialCentroids {};
    bool                                    Loaded          = false;
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
