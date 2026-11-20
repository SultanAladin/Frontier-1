/*============================================================================================================================================
                                                           PHYSICSEXTENSION.CPP
============================================================================================================================================*/
// 🧩 JoltPhysics v5.6.0 Pimpl runtime implementing rigid dynamics, breakable strain bonds, constraints & collider proxies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <Jolt/Jolt.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Geometry/ConvexHullBuilder.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyActivationListener.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/EstimateCollisionResponse.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/Constraints/SwingTwistConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <mutex>

namespace Frontier::SimulationSpace::PhysicsSpace
{

namespace
{

std::mutex        GlobalJoltInitMutex;
uint32_t          GlobalJoltRefCount = 0u;

void AcquireJoltGlobals() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalJoltInitMutex);
    if (GlobalJoltRefCount == 0u)
    {
        JPH::RegisterDefaultAllocator();
        JPH::Factory::sInstance = new JPH::Factory();
        JPH::RegisterTypes();
    }
    ++GlobalJoltRefCount;
}

void ReleaseJoltGlobals() noexcept
{
    std::lock_guard<std::mutex> Lock(GlobalJoltInitMutex);
    if (GlobalJoltRefCount == 0u)
    {
        return;
    }
    --GlobalJoltRefCount;
    if (GlobalJoltRefCount == 0u)
    {
        JPH::UnregisterTypes();
        delete JPH::Factory::sInstance;
        JPH::Factory::sInstance = nullptr;
    }
}

inline JPH::Vec3 ToJoltVec3(const Mathematics::Axis3D& Axis) noexcept
{
    return JPH::Vec3(Axis.X, Axis.Y, Axis.Z);
}

inline JPH::RVec3 ToJoltRVec3(const Mathematics::Axis3D& Axis) noexcept
{
    return JPH::RVec3(Axis.X, Axis.Y, Axis.Z);
}

inline Mathematics::Axis3D FromJoltVec3(JPH::Vec3Arg V) noexcept
{
    return { V.GetX(), V.GetY(), V.GetZ() };
}

inline Mathematics::Axis3D FromJoltRVec3(JPH::RVec3Arg V) noexcept
{
    return { static_cast<float>(V.GetX()), static_cast<float>(V.GetY()), static_cast<float>(V.GetZ()) };
}

inline JPH::Quat ToJoltQuat(const RigidOrientationQuaternion& Q) noexcept
{
    const RigidOrientationQuaternion Norm = RigidBodyDynamics::NormalizeQuaternion(Q);
    return JPH::Quat(Norm.X, Norm.Y, Norm.Z, Norm.W);
}

inline RigidOrientationQuaternion FromJoltQuat(JPH::QuatArg Q) noexcept
{
    return { Q.GetX(), Q.GetY(), Q.GetZ(), Q.GetW() };
}

namespace BroadPhaseLayers
{
    inline constexpr JPH::BroadPhaseLayer NonMoving(0);
    inline constexpr JPH::BroadPhaseLayer Moving(1);
    inline constexpr JPH::uint            LayerCount = 2u;
} // namespace BroadPhaseLayers

class FrontierBroadPhaseLayerInterface final : public JPH::BroadPhaseLayerInterface
{
public:
    JPH::uint GetNumBroadPhaseLayers() const override
    {
        return BroadPhaseLayers::LayerCount;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer Layer) const override
    {
        if (Layer == static_cast<JPH::ObjectLayer>(CollisionLayerChannel::StaticEnvironment))
        {
            return BroadPhaseLayers::NonMoving;
        }
        return BroadPhaseLayers::Moving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer Layer) const override
    {
        switch (static_cast<JPH::BroadPhaseLayer::Type>(Layer))
        {
            case 0: return "NonMoving";
            case 1: return "Moving";
            default: return "Unknown";
        }
    }
#endif
};

class ChannelVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer Layer1, JPH::BroadPhaseLayer Layer2) const override
    {
        if (Layer1 == static_cast<JPH::ObjectLayer>(CollisionLayerChannel::StaticEnvironment))
        {
            return Layer2 == BroadPhaseLayers::Moving;
        }
        return true;
    }
};

class ChannelLayerPairFilter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer LayerA, JPH::ObjectLayer LayerB) const override
    {
        constexpr JPH::ObjectLayer StaticCh  = static_cast<JPH::ObjectLayer>(CollisionLayerChannel::StaticEnvironment);
        constexpr JPH::ObjectLayer SensorCh  = static_cast<JPH::ObjectLayer>(CollisionLayerChannel::SensorTrigger);

        if (LayerA == StaticCh && LayerB == StaticCh)
        {
            return false;
        }
        if (LayerA == SensorCh && LayerB == SensorCh)
        {
            return false;
        }
        return true;
    }
};

class ChannelMaskLayerFilter final : public JPH::ObjectLayerFilter
{
public:
    explicit ChannelMaskLayerFilter(uint32_t Mask) noexcept
        : AllowedMask(Mask)
    {
    }

    bool ShouldCollide(JPH::ObjectLayer Layer) const override
    {
        if (Layer >= 32u)
        {
            return false;
        }
        return (AllowedMask & (1u << Layer)) != 0u;
    }

private:
    uint32_t AllowedMask = 0xFFFFFFFFu;
};

struct BodySlotEntry
{
    JPH::BodyID          JoltId            {};
    RigidBodyDescriptor  Descriptor        {};
    uint32_t             Generation        = 1u;
    float                EffectiveMassKg   = 0.0f;
    float                PeakImpulseNs     = 0.0f;
    Mathematics::Scalar3D ProxyHalfExtents { 0.5f, 0.5f, 0.5f };
    float                ProxyRadius       = 0.5f;
    uint32_t             ProxyShapeCode    = 0u;
    bool                 Occupied          = false;
};

struct ConstraintSlotEntry
{
    JPH::Ref<JPH::TwoBodyConstraint> JoltConstraint   {};
    ConstraintDescriptor             Descriptor       {};
    uint32_t                         Generation       = 1u;
    float                            AppliedForceN    = 0.0f;
    float                            AppliedTorqueNm  = 0.0f;
    float                            StrainRatio      = 0.0f;
    bool                             Occupied         = false;
    bool                             Intact           = false;
};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                               JOLT SIMULATION PIMPL
//------------------------------------------------------------------------------------------------------------------------

struct PhysicsExtension::JoltSimulationRuntime final : public JPH::ContactListener
{
    FrontierBroadPhaseLayerInterface BroadPhaseInterface {};
    ChannelVsBroadPhaseFilter        ChannelBroadFilter  {};
    ChannelLayerPairFilter           ChannelPairFilter   {};

    std::unique_ptr<JPH::TempAllocatorImpl> TempAllocator {};
    std::unique_ptr<JPH::JobSystem>         JobSystem     {};
    std::unique_ptr<JPH::PhysicsSystem>     JoltWorld     {};

    std::vector<BodySlotEntry>       BodySlots            {};
    std::vector<uint32_t>            RecycledBodySlots    {};
    std::vector<ConstraintSlotEntry> ConstraintSlots      {};
    std::vector<uint32_t>            RecycledConstraintSlots {};

    CollisionDetection               ContactJournal       {};
    std::mutex                       ContactMutex         {};

    bool     Initialized        = false;
    bool     Suspended          = false;
    double   AccumulatedSeconds = 0.0;
    double   SimulatedSeconds   = 0.0;
    uint64_t CompletedSteps     = 0u;

    RigidBodyToken ResolveBodyTokenFromTag(JPH::uint64 UserTag) const noexcept
    {
        const uint32_t SlotIndex  = static_cast<uint32_t>(UserTag & 0xFFFFFFFFull);
        const uint32_t Generation = static_cast<uint32_t>(UserTag >> 32u);
        if (SlotIndex == 0u || SlotIndex > BodySlots.size())
        {
            return {};
        }
        const BodySlotEntry& Entry = BodySlots[SlotIndex - 1u];
        if (!Entry.Occupied || Entry.Generation != Generation)
        {
            return {};
        }
        return { SlotIndex, Generation };
    }

    void OnContactAdded(
        const JPH::Body&            Body1,
        const JPH::Body&            Body2,
        const JPH::ContactManifold& Manifold,
        JPH::ContactSettings&       Settings) override
    {
        JPH::CollisionEstimationResult Estimation {};
        JPH::EstimateCollisionResponse(
            Body1,
            Body2,
            Manifold,
            Estimation,
            Settings.mCombinedFriction,
            Settings.mCombinedRestitution,
            1.0f,
            4u);

        float MaxNormalImpulse = 0.0f;
        for (float Imp : Estimation.mContactImpulse)
        {
            MaxNormalImpulse = std::max(MaxNormalImpulse, std::abs(Imp));
        }

        const JPH::RVec3 PointR = Manifold.mBaseOffset + Manifold.GetWorldSpaceContactPointOn1(0u);
        const JPH::Vec3  RelVel = Body1.GetLinearVelocity() - Body2.GetLinearVelocity();
        const float      InvM1  = Body1.GetMotionPropertiesUnchecked() != nullptr && Body1.IsDynamic()
            ? Body1.GetMotionProperties()->GetInverseMass() : 0.0f;
        const float      InvM2  = Body2.GetMotionPropertiesUnchecked() != nullptr && Body2.IsDynamic()
            ? Body2.GetMotionProperties()->GetInverseMass() : 0.0f;
        const float      SumInv = InvM1 + InvM2;
        const float      ReducedMass = (SumInv > 1.0e-7f) ? (1.0f / SumInv) : 0.0f;
        const float      Vn = std::abs(RelVel.Dot(Manifold.mWorldSpaceNormal));

        ContactImpulseReading Reading {};
        Reading.BodyA              = ResolveBodyTokenFromTag(Body1.GetUserData());
        Reading.BodyB              = ResolveBodyTokenFromTag(Body2.GetUserData());
        Reading.ContactPoint       = FromJoltRVec3(PointR);
        Reading.ContactNormal      = FromJoltVec3(Manifold.mWorldSpaceNormal);
        Reading.NormalImpulseNs    = MaxNormalImpulse;
        Reading.PenetrationDepthM  = Manifold.mPenetrationDepth;
        Reading.ImpactEnergyJoules = 0.5f * ReducedMass * Vn * Vn;

        std::lock_guard<std::mutex> Lock(ContactMutex);
        ContactJournal.AppendContactReading(Reading);
        if (Reading.BodyA.Valid())
        {
            BodySlots[Reading.BodyA.SlotIndex - 1u].PeakImpulseNs = std::max(
                BodySlots[Reading.BodyA.SlotIndex - 1u].PeakImpulseNs,
                MaxNormalImpulse);
        }
        if (Reading.BodyB.Valid())
        {
            BodySlots[Reading.BodyB.SlotIndex - 1u].PeakImpulseNs = std::max(
                BodySlots[Reading.BodyB.SlotIndex - 1u].PeakImpulseNs,
                MaxNormalImpulse);
        }
    }

    void EvaluateConstraintRuptures(float StepSeconds) noexcept
    {
        if (StepSeconds <= 1.0e-6f)
        {
            return;
        }
        for (ConstraintSlotEntry& Slot : ConstraintSlots)
        {
            if (!Slot.Occupied || !Slot.Intact || Slot.JoltConstraint == nullptr)
            {
                continue;
            }

            float ForceLambda  = 0.0f;
            float TorqueLambda = 0.0f;
            switch (Slot.Descriptor.Category)
            {
                case ConstraintCategory::Fixed:
                case ConstraintCategory::BreakableBond:
                case ConstraintCategory::Dock:
                {
                    const auto* Fixed = static_cast<const JPH::FixedConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda  = Fixed->GetTotalLambdaPosition().Length();
                    TorqueLambda = Fixed->GetTotalLambdaRotation().Length();
                    break;
                }
                case ConstraintCategory::Distance:
                case ConstraintCategory::Tether:
                {
                    const auto* Dist = static_cast<const JPH::DistanceConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda = std::abs(Dist->GetTotalLambdaPosition());
                    break;
                }
                case ConstraintCategory::Point:
                {
                    const auto* Pt = static_cast<const JPH::PointConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda = Pt->GetTotalLambdaPosition().Length();
                    break;
                }
                case ConstraintCategory::SwingTwist:
                {
                    const auto* ST = static_cast<const JPH::SwingTwistConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda  = ST->GetTotalLambdaPosition().Length();
                    TorqueLambda = std::hypot(ST->GetTotalLambdaSwingY(), ST->GetTotalLambdaSwingZ()) + std::abs(ST->GetTotalLambdaTwist());
                    break;
                }
                case ConstraintCategory::Hinge:
                case ConstraintCategory::Spin:
                {
                    const auto* Hinge = static_cast<const JPH::HingeConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda  = Hinge->GetTotalLambdaPosition().Length();
                    TorqueLambda = Hinge->GetTotalLambdaRotation().Length();
                    break;
                }
                case ConstraintCategory::Slider:
                case ConstraintCategory::Spring:
                {
                    const auto* Slider = static_cast<const JPH::SliderConstraint*>(Slot.JoltConstraint.GetPtr());
                    ForceLambda  = Slider->GetTotalLambdaPosition().Length();
                    TorqueLambda = Slider->GetTotalLambdaRotation().Length();
                    break;
                }
            }

            Slot.AppliedForceN   = ForceLambda / StepSeconds;
            Slot.AppliedTorqueNm = TorqueLambda / StepSeconds;
            Slot.StrainRatio     = ConstraintSolver::EvaluateBondStrainRatio(
                Slot.AppliedForceN,
                Slot.AppliedTorqueNm,
                Slot.Descriptor.BreakForceN,
                Slot.Descriptor.BreakTorqueNm);

            if (Slot.StrainRatio > 1.0f)
            {
                JoltWorld->RemoveConstraint(Slot.JoltConstraint);
                Slot.JoltConstraint = nullptr;
                Slot.Intact         = false;
            }
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

PhysicsExtension::PhysicsExtension() noexcept
    : Runtime(std::make_unique<JoltSimulationRuntime>())
    , Config{}
{
}

PhysicsExtension::PhysicsExtension(const PhysicsSimulationParameters& InitialParameters) noexcept
    : Runtime(std::make_unique<JoltSimulationRuntime>())
    , Config(InitialParameters)
{
}

PhysicsExtension::~PhysicsExtension() noexcept
{
    Finalize();
}

bool PhysicsExtension::Initialize() noexcept
{
    if (!Runtime)
    {
        return false;
    }
    if (Runtime->Initialized)
    {
        return true;
    }

    AcquireJoltGlobals();

    const uint32_t MaxBodies        = std::max(Config.MaxRigidBodies, 256u);
    const uint32_t NumBodyMutexes   = 0u;
    const uint32_t MaxBodyPairs     = std::max(MaxBodies * 4u, 4096u);
    const uint32_t MaxConstraints   = std::max(MaxBodies * 2u, 2048u);

    Runtime->TempAllocator = std::make_unique<JPH::TempAllocatorImpl>(std::max(Config.TempAllocatorBytes, 2u * 1024u * 1024u));
    if (Config.SolverThreadCount > 0u)
    {
        Runtime->JobSystem = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            static_cast<int>(Config.SolverThreadCount));
    }
    else
    {
        Runtime->JobSystem = std::make_unique<JPH::JobSystemSingleThreaded>(JPH::cMaxPhysicsJobs);
    }

    Runtime->JoltWorld = std::make_unique<JPH::PhysicsSystem>();
    Runtime->JoltWorld->Init(
        MaxBodies,
        NumBodyMutexes,
        MaxBodyPairs,
        MaxConstraints,
        Runtime->BroadPhaseInterface,
        Runtime->ChannelBroadFilter,
        Runtime->ChannelPairFilter);

    JPH::PhysicsSettings Settings = Runtime->JoltWorld->GetPhysicsSettings();
    Settings.mDeterministicSimulation = true;
    Runtime->JoltWorld->SetPhysicsSettings(Settings);
    Runtime->JoltWorld->SetGravity(ToJoltVec3(Config.GravityMps2));
    Runtime->JoltWorld->SetContactListener(Runtime.get());

    Runtime->AccumulatedSeconds = 0.0;
    Runtime->SimulatedSeconds   = 0.0;
    Runtime->CompletedSteps     = 0u;
    Runtime->Initialized        = true;
    Runtime->Suspended          = false;
    return true;
}

void PhysicsExtension::Tick(double Δt) noexcept
{
    if (!Runtime || !Runtime->Initialized || Runtime->Suspended || !Runtime->JoltWorld)
    {
        return;
    }

    const double SafeDt = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, 0.25) : 0.0;
    if (SafeDt <= 0.0)
    {
        return;
    }

    const double FixedStep = 1.0 / static_cast<double>(std::max(Config.FixedStepHz, 15.0f));
    Runtime->AccumulatedSeconds += SafeDt;

    for (BodySlotEntry& Entry : Runtime->BodySlots)
    {
        Entry.PeakImpulseNs = 0.0f;
    }
    {
        std::lock_guard<std::mutex> Lock(Runtime->ContactMutex);
        Runtime->ContactJournal.ClearTickJournal();
    }

    uint32_t Substeps = 0u;
    const uint32_t MaxSteps = std::max(Config.MaxSubstepsPerTick, 1u);
    while (Runtime->AccumulatedSeconds + 1.0e-9 >= FixedStep && Substeps < MaxSteps)
    {
        Runtime->JoltWorld->Update(
            static_cast<float>(FixedStep),
            1,
            Runtime->TempAllocator.get(),
            Runtime->JobSystem.get());
        Runtime->EvaluateConstraintRuptures(static_cast<float>(FixedStep));

        Runtime->AccumulatedSeconds -= FixedStep;
        Runtime->SimulatedSeconds   += FixedStep;
        ++Runtime->CompletedSteps;
        ++Substeps;
    }
}

void PhysicsExtension::Suspend() noexcept
{
    if (Runtime && Runtime->Initialized)
    {
        Runtime->Suspended = true;
    }
}

void PhysicsExtension::Resume() noexcept
{
    if (Runtime && Runtime->Initialized)
    {
        Runtime->Suspended = false;
    }
}

void PhysicsExtension::Finalize() noexcept
{
    if (!Runtime || !Runtime->Initialized)
    {
        return;
    }

    for (ConstraintSlotEntry& CSlot : Runtime->ConstraintSlots)
    {
        if (CSlot.Occupied && CSlot.Intact && CSlot.JoltConstraint != nullptr && Runtime->JoltWorld)
        {
            Runtime->JoltWorld->RemoveConstraint(CSlot.JoltConstraint);
        }
        CSlot.JoltConstraint = nullptr;
    }
    Runtime->ConstraintSlots.clear();
    Runtime->RecycledConstraintSlots.clear();

    if (Runtime->JoltWorld)
    {
        JPH::BodyInterface& Bodies = Runtime->JoltWorld->GetBodyInterface();
        for (const BodySlotEntry& Entry : Runtime->BodySlots)
        {
            if (Entry.Occupied && !Entry.JoltId.IsInvalid())
            {
                Bodies.RemoveBody(Entry.JoltId);
                Bodies.DestroyBody(Entry.JoltId);
            }
        }
    }
    Runtime->BodySlots.clear();
    Runtime->RecycledBodySlots.clear();
    Runtime->ContactJournal.ClearTickJournal();

    Runtime->JoltWorld.reset();
    Runtime->JobSystem.reset();
    Runtime->TempAllocator.reset();
    Runtime->Initialized = false;
    Runtime->Suspended   = false;

    ReleaseJoltGlobals();
}

bool PhysicsExtension::QueryInitialized() const noexcept
{
    return Runtime && Runtime->Initialized;
}

bool PhysicsExtension::QuerySuspended() const noexcept
{
    return Runtime && Runtime->Suspended;
}

void PhysicsExtension::AssignGravity(const Mathematics::Axis3D& GravityMps2) noexcept
{
    Config.GravityMps2 = GravityMps2;
    if (Runtime && Runtime->Initialized && Runtime->JoltWorld)
    {
        Runtime->JoltWorld->SetGravity(ToJoltVec3(GravityMps2));
    }
}

void PhysicsExtension::ConfigureFixedStepHz(float FixedStepHz) noexcept
{
    Config.FixedStepHz = std::clamp(FixedStepHz, 15.0f, 480.0f);
}

RigidBodyToken PhysicsExtension::IssueRigidBody(const RigidBodyDescriptor& Descriptor) noexcept
{
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return {};
    }

    JPH::ShapeRefC ShapeRef;
    Mathematics::Scalar3D ProxyExtents = Descriptor.Shape.HalfExtentsMeters;
    float ProxyRadius  = Descriptor.Shape.RadiusMeters;
    uint32_t ProxyCode = 0u;

    switch (Descriptor.Shape.ShapeCategory)
    {
        case CollisionShapeCategory::Box:
        {
            const JPH::Vec3 Half(
                std::max(Descriptor.Shape.HalfExtentsMeters.X, 1.0e-3f),
                std::max(Descriptor.Shape.HalfExtentsMeters.Y, 1.0e-3f),
                std::max(Descriptor.Shape.HalfExtentsMeters.Z, 1.0e-3f));
            const float MinExt = std::min({ Half.GetX(), Half.GetY(), Half.GetZ() });
            const float ConvexR = std::min(std::max(Descriptor.Shape.ConvexRadiusMeters, 0.0f), MinExt * 0.25f);
            JPH::BoxShapeSettings Settings(Half, ConvexR);
            Settings.SetDensity(std::max(Descriptor.Shape.DensityKgPerM3, 1.0f));
            auto Result = Settings.Create();
            if (Result.HasError()) return {};
            ShapeRef  = Result.Get();
            ProxyCode = 1u;
            break;
        }
        case CollisionShapeCategory::Sphere:
        {
            const float Radius = std::max(Descriptor.Shape.RadiusMeters, 1.0e-3f);
            JPH::SphereShapeSettings Settings(Radius);
            Settings.SetDensity(std::max(Descriptor.Shape.DensityKgPerM3, 1.0f));
            auto Result = Settings.Create();
            if (Result.HasError()) return {};
            ShapeRef    = Result.Get();
            ProxyRadius = Radius;
            ProxyCode   = 0u;
            break;
        }
        case CollisionShapeCategory::Capsule:
        {
            const float Radius = std::max(Descriptor.Shape.RadiusMeters, 1.0e-3f);
            const float HalfH  = std::max(Descriptor.Shape.HalfHeightMeters, 1.0e-3f);
            JPH::CapsuleShapeSettings CapsuleCfg(HalfH, Radius);
            CapsuleCfg.SetDensity(std::max(Descriptor.Shape.DensityKgPerM3, 1.0f));
            auto CapsuleResult = CapsuleCfg.Create();
            if (CapsuleResult.HasError()) return {};
            // Rotate +Y Jolt capsule by +90 deg around +X so local symmetry axis aligns with Frontier +Z Up
            const JPH::Quat AlignZ = JPH::Quat::sRotation(JPH::Vec3::sAxisX(), 0.5f * JPH::JPH_PI);
            JPH::RotatedTranslatedShapeSettings RotatedCfg(JPH::Vec3::sZero(), AlignZ, CapsuleResult.Get());
            auto Result = RotatedCfg.Create();
            if (Result.HasError()) return {};
            ShapeRef       = Result.Get();
            ProxyRadius    = Radius;
            ProxyExtents.Z = HalfH;
            ProxyCode      = 2u;
            break;
        }
        case CollisionShapeCategory::ConvexHull:
        {
            if (Descriptor.Shape.ConvexVertices.size() < 4u)
            {
                return {};
            }
            JPH::Array<JPH::Vec3> Pts;
            Pts.reserve(Descriptor.Shape.ConvexVertices.size());
            Mathematics::Axis3D MinC = Descriptor.Shape.ConvexVertices.front();
            Mathematics::Axis3D MaxC = Descriptor.Shape.ConvexVertices.front();
            for (const Mathematics::Axis3D& V : Descriptor.Shape.ConvexVertices)
            {
                Pts.push_back(ToJoltVec3(V));
                MinC.X = std::min(MinC.X, V.X); MinC.Y = std::min(MinC.Y, V.Y); MinC.Z = std::min(MinC.Z, V.Z);
                MaxC.X = std::max(MaxC.X, V.X); MaxC.Y = std::max(MaxC.Y, V.Y); MaxC.Z = std::max(MaxC.Z, V.Z);
            }
            ProxyExtents = {
                std::max((MaxC.X - MinC.X) * 0.5f, 1.0e-3f),
                std::max((MaxC.Y - MinC.Y) * 0.5f, 1.0e-3f),
                std::max((MaxC.Z - MinC.Z) * 0.5f, 1.0e-3f)
            };
            ProxyCode = 1u;

            JPH::ConvexHullShapeSettings Settings(Pts, std::max(Descriptor.Shape.ConvexRadiusMeters, 0.002f));
            Settings.SetDensity(std::max(Descriptor.Shape.DensityKgPerM3, 1.0f));
            auto Result = Settings.Create();
            if (Result.HasError()) return {};
            ShapeRef = Result.Get();
            break;
        }
        case CollisionShapeCategory::CompoundHull:
        {
            if (Descriptor.Shape.ConvexVertices.size() < 4u || Descriptor.Shape.CompoundHullVertexCounts.empty())
            {
                return {};
            }
            Mathematics::Axis3D MinC = Descriptor.Shape.ConvexVertices.front();
            Mathematics::Axis3D MaxC = Descriptor.Shape.ConvexVertices.front();
            for (const Mathematics::Axis3D& V : Descriptor.Shape.ConvexVertices)
            {
                MinC.X = std::min(MinC.X, V.X); MinC.Y = std::min(MinC.Y, V.Y); MinC.Z = std::min(MinC.Z, V.Z);
                MaxC.X = std::max(MaxC.X, V.X); MaxC.Y = std::max(MaxC.Y, V.Y); MaxC.Z = std::max(MaxC.Z, V.Z);
            }
            ProxyExtents = {
                std::max((MaxC.X - MinC.X) * 0.5f, 1.0e-3f),
                std::max((MaxC.Y - MinC.Y) * 0.5f, 1.0e-3f),
                std::max((MaxC.Z - MinC.Z) * 0.5f, 1.0e-3f)
            };
            ProxyCode = 1u;

            JPH::StaticCompoundShapeSettings CompoundCfg {};
            uint32_t Offset = 0u;
            uint32_t AddedHulls = 0u;
            JPH::ShapeRefC SingleSubShape {};
            for (uint32_t Count : Descriptor.Shape.CompoundHullVertexCounts)
            {
                if (Offset + Count > Descriptor.Shape.ConvexVertices.size()) break;
                if (Count >= 4u)
                {
                    JPH::Array<JPH::Vec3> Pts;
                    Pts.reserve(Count);
                    for (uint32_t K = 0u; K < Count; ++K)
                    {
                        Pts.push_back(ToJoltVec3(Descriptor.Shape.ConvexVertices[Offset + K]));
                    }
                    JPH::ConvexHullShapeSettings HullCfg(Pts, std::max(Descriptor.Shape.ConvexRadiusMeters, 0.001f));
                    HullCfg.SetDensity(std::max(Descriptor.Shape.DensityKgPerM3, 1.0f));
                    auto HullRes = HullCfg.Create();
                    if (!HullRes.HasError())
                    {
                        SingleSubShape = HullRes.Get();
                        CompoundCfg.AddShape(JPH::Vec3::sZero(), JPH::Quat::sIdentity(), SingleSubShape);
                        ++AddedHulls;
                    }
                }
                Offset += Count;
            }
            if (AddedHulls == 0u) return {};
            if (AddedHulls == 1u)
            {
                ShapeRef = SingleSubShape;
            }
            else
            {
                auto CompRes = CompoundCfg.Create();
                if (CompRes.HasError()) return {};
                ShapeRef = CompRes.Get();
            }
            break;
        }
        case CollisionShapeCategory::TriangleMesh:
        {
            if (Descriptor.Shape.MeshTriangles.size() < 3u || (Descriptor.Shape.MeshTriangles.size() % 3u) != 0u)
            {
                return {};
            }
            JPH::TriangleList Tris;
            Tris.reserve(Descriptor.Shape.MeshTriangles.size() / 3u);
            Mathematics::Axis3D MinC = Descriptor.Shape.MeshTriangles.front();
            Mathematics::Axis3D MaxC = Descriptor.Shape.MeshTriangles.front();
            for (size_t I = 0u; I < Descriptor.Shape.MeshTriangles.size(); I += 3u)
            {
                const auto& A = Descriptor.Shape.MeshTriangles[I + 0u];
                const auto& B = Descriptor.Shape.MeshTriangles[I + 1u];
                const auto& C = Descriptor.Shape.MeshTriangles[I + 2u];
                Tris.emplace_back(ToJoltVec3(A), ToJoltVec3(B), ToJoltVec3(C));
                for (const auto& V : { A, B, C })
                {
                    MinC.X = std::min(MinC.X, V.X); MinC.Y = std::min(MinC.Y, V.Y); MinC.Z = std::min(MinC.Z, V.Z);
                    MaxC.X = std::max(MaxC.X, V.X); MaxC.Y = std::max(MaxC.Y, V.Y); MaxC.Z = std::max(MaxC.Z, V.Z);
                }
            }
            ProxyExtents = {
                std::max((MaxC.X - MinC.X) * 0.5f, 1.0e-3f),
                std::max((MaxC.Y - MinC.Y) * 0.5f, 1.0e-3f),
                std::max((MaxC.Z - MinC.Z) * 0.5f, 1.0e-3f)
            };
            ProxyCode = 1u;

            JPH::MeshShapeSettings MeshCfg(Tris);
            auto MeshRes = MeshCfg.Create();
            if (MeshRes.HasError()) return {};
            ShapeRef = MeshRes.Get();
            break;
        }
    }

    if (!ShapeRef)
    {
        return {};
    }

    JPH::EMotionType JoltMotion = JPH::EMotionType::Dynamic;
    JPH::EActivation Activate   = JPH::EActivation::Activate;
    if (Descriptor.MotionCategory == RigidMotionCategory::Static)
    {
        JoltMotion = JPH::EMotionType::Static;
        Activate   = JPH::EActivation::DontActivate;
    }
    else if (Descriptor.MotionCategory == RigidMotionCategory::Kinematic)
    {
        JoltMotion = JPH::EMotionType::Kinematic;
    }

    const JPH::ObjectLayer ChannelLayer = static_cast<JPH::ObjectLayer>(
        std::min<uint16_t>(Descriptor.CollisionChannel, static_cast<uint16_t>(CollisionLayerChannel::Count) - 1u));

    JPH::BodyCreationSettings BodyCfg(
        ShapeRef,
        ToJoltRVec3(Descriptor.PositionMeters),
        ToJoltQuat(Descriptor.Orientation),
        JoltMotion,
        ChannelLayer);

    BodyCfg.mLinearVelocity       = ToJoltVec3(Descriptor.LinearVelocity);
    BodyCfg.mAngularVelocity      = ToJoltVec3(Descriptor.AngularVelocity);
    BodyCfg.mFriction             = std::clamp(Descriptor.Shape.Friction, Constants::MinimumFrictionLimit, Constants::MaximumFrictionLimit);
    BodyCfg.mRestitution          = std::clamp(Descriptor.Shape.Restitution, 0.0f, Constants::MaximumRestitutionLimit);
    BodyCfg.mLinearDamping        = std::max(Descriptor.LinearDamping, 0.0f);
    BodyCfg.mAngularDamping       = std::max(Descriptor.AngularDamping, 0.0f);
    BodyCfg.mGravityFactor        = Descriptor.GravityFactor;
    BodyCfg.mAllowSleeping        = Descriptor.AllowSleep;
    BodyCfg.mMotionQuality        = Descriptor.ContinuousMotion ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
    BodyCfg.mIsSensor             = (ChannelLayer == static_cast<JPH::ObjectLayer>(CollisionLayerChannel::SensorTrigger));

    const float MassKg = RigidBodyDynamics::ResolveEffectiveMassKg(Descriptor);
    if (Descriptor.MotionCategory == RigidMotionCategory::Dynamic && Descriptor.ExplicitMassKg > 0.0f)
    {
        BodyCfg.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
        BodyCfg.mMassPropertiesOverride.mMass = MassKg;
    }

    uint32_t ZeroIdx = 0u;
    if (!Runtime->RecycledBodySlots.empty())
    {
        ZeroIdx = Runtime->RecycledBodySlots.back();
        Runtime->RecycledBodySlots.pop_back();
    }
    else
    {
        ZeroIdx = static_cast<uint32_t>(Runtime->BodySlots.size());
        Runtime->BodySlots.emplace_back();
    }

    BodySlotEntry& Entry = Runtime->BodySlots[ZeroIdx];
    const uint32_t SlotIndex  = ZeroIdx + 1u;
    const uint32_t Generation = Entry.Generation == 0u ? 1u : Entry.Generation;
    BodyCfg.mUserData = (static_cast<JPH::uint64>(Generation) << 32u) | static_cast<JPH::uint64>(SlotIndex);

    JPH::BodyInterface& Bodies = Runtime->JoltWorld->GetBodyInterface();
    const JPH::BodyID CreatedId = Bodies.CreateAndAddBody(BodyCfg, Activate);
    if (CreatedId.IsInvalid())
    {
        Runtime->RecycledBodySlots.push_back(ZeroIdx);
        return {};
    }

    Entry.JoltId           = CreatedId;
    Entry.Descriptor       = Descriptor;
    Entry.Generation       = Generation;
    Entry.EffectiveMassKg  = MassKg;
    Entry.PeakImpulseNs    = 0.0f;
    Entry.ProxyHalfExtents = ProxyExtents;
    Entry.ProxyRadius      = ProxyRadius;
    Entry.ProxyShapeCode   = ProxyCode;
    Entry.Occupied         = true;

    return { SlotIndex, Generation };
}

bool PhysicsExtension::ReclaimRigidBody(RigidBodyToken Token) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return false;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    BodySlotEntry& Entry = Runtime->BodySlots[ZeroIdx];

    JPH::BodyInterface& Bodies = Runtime->JoltWorld->GetBodyInterface();
    Bodies.RemoveBody(Entry.JoltId);
    Bodies.DestroyBody(Entry.JoltId);

    Entry.JoltId    = JPH::BodyID();
    Entry.Occupied  = false;
    Entry.Generation = (Entry.Generation + 1u == 0u) ? 1u : (Entry.Generation + 1u);
    Runtime->RecycledBodySlots.push_back(ZeroIdx);
    return true;
}

bool PhysicsExtension::AuthenticateBodyToken(RigidBodyToken Token) const noexcept
{
    if (!Runtime || !Runtime->Initialized || !Token.Valid() || Token.SlotIndex > Runtime->BodySlots.size())
    {
        return false;
    }
    const BodySlotEntry& Entry = Runtime->BodySlots[Token.SlotIndex - 1u];
    return Entry.Occupied && Entry.Generation == Token.Generation && !Entry.JoltId.IsInvalid();
}

RigidBodySnapshot PhysicsExtension::InspectBodySnapshot(RigidBodyToken Token) const noexcept
{
    RigidBodySnapshot Snap {};
    if (!AuthenticateBodyToken(Token))
    {
        return Snap;
    }

    const BodySlotEntry& Entry = Runtime->BodySlots[Token.SlotIndex - 1u];
    const JPH::BodyInterface& Bodies = Runtime->JoltWorld->GetBodyInterface();

    const JPH::RVec3 PosR = Bodies.GetPosition(Entry.JoltId);
    const JPH::Quat  Rot  = Bodies.GetRotation(Entry.JoltId);
    const JPH::Vec3  Vel  = Bodies.GetLinearVelocity(Entry.JoltId);
    const JPH::Vec3  Ang  = Bodies.GetAngularVelocity(Entry.JoltId);

    Snap.Token                = Token;
    Snap.InstanceTag          = Entry.Descriptor.InstanceTag;
    Snap.PositionMeters       = FromJoltRVec3(PosR);
    Snap.Orientation          = FromJoltQuat(Rot);
    Snap.LinearVelocity       = FromJoltVec3(Vel);
    Snap.AngularVelocity      = FromJoltVec3(Ang);
    Snap.MassKg               = Entry.EffectiveMassKg;
    Snap.PeakContactImpulseNs = Entry.PeakImpulseNs;
    Snap.Awake                = Bodies.IsActive(Entry.JoltId);
    Snap.Resident             = true;

    const float SpeedSq = Vel.LengthSq();
    Snap.KineticEnergyJoules = 0.5f * Entry.EffectiveMassKg * SpeedSq;
    return Snap;
}

void PhysicsExtension::ApplyForce(RigidBodyToken Token, const Mathematics::Axis3D& ForceNewtons) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().AddForce(Runtime->BodySlots[Token.SlotIndex - 1u].JoltId, ToJoltVec3(ForceNewtons));
}

void PhysicsExtension::ApplyTorque(RigidBodyToken Token, const Mathematics::Axis3D& TorqueNm) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().AddTorque(Runtime->BodySlots[Token.SlotIndex - 1u].JoltId, ToJoltVec3(TorqueNm));
}

void PhysicsExtension::ApplyLinearImpulse(RigidBodyToken Token, const Mathematics::Axis3D& ImpulseNs) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().AddImpulse(Runtime->BodySlots[Token.SlotIndex - 1u].JoltId, ToJoltVec3(ImpulseNs));
}

void PhysicsExtension::ApplyImpulseAtPoint(
    RigidBodyToken             Token,
    const Mathematics::Axis3D& ImpulseNs,
    const Mathematics::Axis3D& WorldPointMeters) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().AddImpulse(
        Runtime->BodySlots[Token.SlotIndex - 1u].JoltId,
        ToJoltVec3(ImpulseNs),
        ToJoltRVec3(WorldPointMeters));
}

void PhysicsExtension::AssignBodyVelocity(
    RigidBodyToken             Token,
    const Mathematics::Axis3D& LinearVelocityMps,
    const Mathematics::Axis3D& AngularVelocityRadPerSec) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().SetLinearAndAngularVelocity(
        Runtime->BodySlots[Token.SlotIndex - 1u].JoltId,
        ToJoltVec3(LinearVelocityMps),
        ToJoltVec3(AngularVelocityRadPerSec));
}

void PhysicsExtension::TeleportBody(
    RigidBodyToken                    Token,
    const Mathematics::Axis3D&        PositionMeters,
    const RigidOrientationQuaternion& Orientation) noexcept
{
    if (!AuthenticateBodyToken(Token))
    {
        return;
    }
    Runtime->JoltWorld->GetBodyInterface().SetPositionAndRotation(
        Runtime->BodySlots[Token.SlotIndex - 1u].JoltId,
        ToJoltRVec3(PositionMeters),
        ToJoltQuat(Orientation),
        JPH::EActivation::Activate);
}

ConstraintToken PhysicsExtension::IssueConstraint(const ConstraintDescriptor& Descriptor) noexcept
{
    if (!AuthenticateBodyToken(Descriptor.FirstBody) || !AuthenticateBodyToken(Descriptor.SecondBody))
    {
        return {};
    }

    const JPH::BodyID IdA = Runtime->BodySlots[Descriptor.FirstBody.SlotIndex - 1u].JoltId;
    const JPH::BodyID IdB = Runtime->BodySlots[Descriptor.SecondBody.SlotIndex - 1u].JoltId;
    if (IdA == IdB)
    {
        return {};
    }

    const JPH::BodyID Ids[2] = { IdA, IdB };
    JPH::BodyLockMultiWrite Lock(Runtime->JoltWorld->GetBodyLockInterface(), Ids, 2);
    JPH::Body* BodyA = Lock.GetBody(0);
    JPH::Body* BodyB = Lock.GetBody(1);
    if (BodyA == nullptr || BodyB == nullptr)
    {
        return {};
    }

    JPH::Ref<JPH::TwoBodyConstraint> Created;
    switch (Descriptor.Category)
    {
        case ConstraintCategory::Fixed:
        case ConstraintCategory::BreakableBond:
        case ConstraintCategory::Dock:
        {
            JPH::FixedConstraintSettings Cfg;
            Cfg.mSpace  = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPoint1 = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPoint2 = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Created = Cfg.Create(*BodyA, *BodyB);
            break;
        }
        case ConstraintCategory::Distance:
        case ConstraintCategory::Tether:
        {
            JPH::DistanceConstraintSettings Cfg;
            Cfg.mSpace  = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPoint1 = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPoint2 = ToJoltRVec3(Descriptor.SecondAnchorMeters);
            if (Descriptor.Category == ConstraintCategory::Tether)
            {
                Cfg.mMinDistance = 0.0f;
                Cfg.mMaxDistance = std::max(Descriptor.MaxLimit, 0.01f);
            }
            else if (Descriptor.MaxLimit > Descriptor.MinLimit && Descriptor.MaxLimit > 0.0f)
            {
                Cfg.mMinDistance = std::max(Descriptor.MinLimit, 0.0f);
                Cfg.mMaxDistance = Descriptor.MaxLimit;
            }
            if (Descriptor.SpringFrequencyHz > 0.0f)
            {
                Cfg.mLimitsSpringSettings.mMode      = JPH::ESpringMode::FrequencyAndDamping;
                Cfg.mLimitsSpringSettings.mFrequency = Descriptor.SpringFrequencyHz;
                Cfg.mLimitsSpringSettings.mDamping   = Descriptor.SpringDampingRatio;
            }
            Created = Cfg.Create(*BodyA, *BodyB);
            break;
        }
        case ConstraintCategory::Point:
        {
            JPH::PointConstraintSettings Cfg;
            Cfg.mSpace  = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPoint1 = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPoint2 = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Created = Cfg.Create(*BodyA, *BodyB);
            break;
        }
        case ConstraintCategory::SwingTwist:
        {
            const JPH::Vec3 TwistAxis = ToJoltVec3(Mathematics::NormalizeAxis(Descriptor.AxisUnit, { 0.0f, 1.0f, 0.0f }));
            const JPH::Vec3 PlaneAxis = TwistAxis.GetNormalizedPerpendicular();
            JPH::SwingTwistConstraintSettings Cfg;
            Cfg.mSpace               = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPosition1           = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPosition2           = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mTwistAxis1          = TwistAxis;
            Cfg.mTwistAxis2          = TwistAxis;
            Cfg.mPlaneAxis1          = PlaneAxis;
            Cfg.mPlaneAxis2          = PlaneAxis;
            Cfg.mNormalHalfConeAngle = std::clamp(Descriptor.ConeHalfAngleRad, 0.01f, 3.10f);
            Cfg.mPlaneHalfConeAngle  = Cfg.mNormalHalfConeAngle;
            Cfg.mTwistMinAngle       = -std::clamp(Descriptor.TwistHalfAngleRad, 0.01f, 3.14f);
            Cfg.mTwistMaxAngle       = std::clamp(Descriptor.TwistHalfAngleRad, 0.01f, 3.14f);
            Created = Cfg.Create(*BodyA, *BodyB);
            break;
        }
        case ConstraintCategory::Hinge:
        case ConstraintCategory::Spin:
        {
            const JPH::Vec3 HingeAxis  = ToJoltVec3(Mathematics::NormalizeAxis(Descriptor.AxisUnit, { 0.0f, 0.0f, 1.0f }));
            const JPH::Vec3 NormalAxis = HingeAxis.GetNormalizedPerpendicular();
            JPH::HingeConstraintSettings Cfg;
            Cfg.mSpace       = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPoint1      = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPoint2      = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mHingeAxis1  = HingeAxis;
            Cfg.mHingeAxis2  = HingeAxis;
            Cfg.mNormalAxis1 = NormalAxis;
            Cfg.mNormalAxis2 = NormalAxis;
            if (Descriptor.Category == ConstraintCategory::Hinge && Descriptor.MaxLimit > Descriptor.MinLimit)
            {
                Cfg.mLimitsMin = Descriptor.MinLimit;
                Cfg.mLimitsMax = Descriptor.MaxLimit;
            }
            if (Descriptor.MotorDriveEnabled || Descriptor.Category == ConstraintCategory::Spin)
            {
                const float MaxT = std::max(Descriptor.MaxMotorTorqueOrForce, 1.0f);
                Cfg.mMotorSettings = JPH::MotorSettings(10.0f, 1.0f, MaxT, MaxT);
                Cfg.mMotorSettings.mMinTorqueLimit = -MaxT;
                Cfg.mMotorSettings.mMaxTorqueLimit = MaxT;
            }
            Created = Cfg.Create(*BodyA, *BodyB);
            if (Created != nullptr && (Descriptor.MotorDriveEnabled || Descriptor.Category == ConstraintCategory::Spin))
            {
                auto* Hinge = static_cast<JPH::HingeConstraint*>(Created.GetPtr());
                if (Descriptor.MotorVelocityMode || Descriptor.Category == ConstraintCategory::Spin)
                {
                    Hinge->SetMotorState(JPH::EMotorState::Velocity);
                    Hinge->SetTargetAngularVelocity(Descriptor.TargetMotorCoordinate);
                }
                else
                {
                    Hinge->SetMotorState(JPH::EMotorState::Position);
                    Hinge->SetTargetAngle(Descriptor.TargetMotorCoordinate);
                }
            }
            break;
        }
        case ConstraintCategory::Slider:
        case ConstraintCategory::Spring:
        {
            const JPH::Vec3 SliderAxis = ToJoltVec3(Mathematics::NormalizeAxis(Descriptor.AxisUnit, { 1.0f, 0.0f, 0.0f }));
            const JPH::Vec3 NormalAxis = SliderAxis.GetNormalizedPerpendicular();
            JPH::SliderConstraintSettings Cfg;
            Cfg.mSpace        = JPH::EConstraintSpace::WorldSpace;
            Cfg.mPoint1       = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mPoint2       = ToJoltRVec3(Descriptor.AnchorWorldMeters);
            Cfg.mSliderAxis1  = SliderAxis;
            Cfg.mSliderAxis2  = SliderAxis;
            Cfg.mNormalAxis1  = NormalAxis;
            Cfg.mNormalAxis2  = NormalAxis;
            if (Descriptor.MaxLimit > Descriptor.MinLimit)
            {
                Cfg.mLimitsMin = Descriptor.MinLimit;
                Cfg.mLimitsMax = Descriptor.MaxLimit;
            }
            if (Descriptor.Category == ConstraintCategory::Spring)
            {
                const float Freq = std::max(Descriptor.SpringFrequencyHz, 0.25f);
                const float Damp = std::max(Descriptor.SpringDampingRatio, 0.05f);
                const float MaxF = std::max(Descriptor.MaxMotorTorqueOrForce, 50000.0f);
                Cfg.mMotorSettings = JPH::MotorSettings(Freq, Damp, MaxF, MaxF);
                Cfg.mMotorSettings.mMinForceLimit = -MaxF;
                Cfg.mMotorSettings.mMaxForceLimit = MaxF;
            }
            Created = Cfg.Create(*BodyA, *BodyB);
            if (Created != nullptr && Descriptor.Category == ConstraintCategory::Spring)
            {
                auto* Slider = static_cast<JPH::SliderConstraint*>(Created.GetPtr());
                Slider->SetMotorState(JPH::EMotorState::Position);
                Slider->SetTargetPosition(Descriptor.TargetMotorCoordinate);
            }
            break;
        }
    }

    if (Created == nullptr)
    {
        return {};
    }

    Runtime->JoltWorld->AddConstraint(Created);

    uint32_t ZeroIdx = 0u;
    if (!Runtime->RecycledConstraintSlots.empty())
    {
        ZeroIdx = Runtime->RecycledConstraintSlots.back();
        Runtime->RecycledConstraintSlots.pop_back();
    }
    else
    {
        ZeroIdx = static_cast<uint32_t>(Runtime->ConstraintSlots.size());
        Runtime->ConstraintSlots.emplace_back();
    }

    ConstraintSlotEntry& CSlot = Runtime->ConstraintSlots[ZeroIdx];
    CSlot.JoltConstraint  = Created;
    CSlot.Descriptor      = Descriptor;
    CSlot.Generation      = CSlot.Generation == 0u ? 1u : CSlot.Generation;
    CSlot.AppliedForceN   = 0.0f;
    CSlot.AppliedTorqueNm = 0.0f;
    CSlot.StrainRatio     = 0.0f;
    CSlot.Occupied        = true;
    CSlot.Intact          = true;

    return { ZeroIdx + 1u, CSlot.Generation };
}

bool PhysicsExtension::ReclaimConstraint(ConstraintToken Token) noexcept
{
    if (!Runtime || !Runtime->Initialized || !Token.Valid() || Token.SlotIndex > Runtime->ConstraintSlots.size())
    {
        return false;
    }
    const uint32_t ZeroIdx = Token.SlotIndex - 1u;
    ConstraintSlotEntry& CSlot = Runtime->ConstraintSlots[ZeroIdx];
    if (!CSlot.Occupied || CSlot.Generation != Token.Generation)
    {
        return false;
    }
    if (CSlot.Intact && CSlot.JoltConstraint != nullptr)
    {
        Runtime->JoltWorld->RemoveConstraint(CSlot.JoltConstraint);
    }
    CSlot.JoltConstraint = nullptr;
    CSlot.Occupied       = false;
    CSlot.Intact         = false;
    CSlot.Generation     = (CSlot.Generation + 1u == 0u) ? 1u : (CSlot.Generation + 1u);
    Runtime->RecycledConstraintSlots.push_back(ZeroIdx);
    return true;
}

bool PhysicsExtension::BreakConstraint(ConstraintToken Token) noexcept
{
    if (!Runtime || !Runtime->Initialized || !Token.Valid() || Token.SlotIndex > Runtime->ConstraintSlots.size())
    {
        return false;
    }
    ConstraintSlotEntry& CSlot = Runtime->ConstraintSlots[Token.SlotIndex - 1u];
    if (!CSlot.Occupied || CSlot.Generation != Token.Generation || !CSlot.Intact)
    {
        return false;
    }
    if (CSlot.JoltConstraint != nullptr)
    {
        Runtime->JoltWorld->RemoveConstraint(CSlot.JoltConstraint);
        CSlot.JoltConstraint = nullptr;
    }
    CSlot.Intact = false;
    return true;
}

bool PhysicsExtension::AssignConstraintMotorTarget(ConstraintToken Token, float TargetCoordinateOrVelocity) noexcept
{
    if (!Runtime || !Runtime->Initialized || !Token.Valid() || Token.SlotIndex > Runtime->ConstraintSlots.size())
    {
        return false;
    }
    ConstraintSlotEntry& CSlot = Runtime->ConstraintSlots[Token.SlotIndex - 1u];
    if (!CSlot.Occupied || CSlot.Generation != Token.Generation || !CSlot.Intact || CSlot.JoltConstraint == nullptr)
    {
        return false;
    }

    CSlot.Descriptor.TargetMotorCoordinate = TargetCoordinateOrVelocity;
    switch (CSlot.Descriptor.Category)
    {
        case ConstraintCategory::Hinge:
        case ConstraintCategory::Spin:
        {
            auto* Hinge = static_cast<JPH::HingeConstraint*>(CSlot.JoltConstraint.GetPtr());
            if (CSlot.Descriptor.MotorVelocityMode || CSlot.Descriptor.Category == ConstraintCategory::Spin)
            {
                Hinge->SetMotorState(JPH::EMotorState::Velocity);
                Hinge->SetTargetAngularVelocity(TargetCoordinateOrVelocity);
            }
            else
            {
                Hinge->SetMotorState(JPH::EMotorState::Position);
                Hinge->SetTargetAngle(TargetCoordinateOrVelocity);
            }
            return true;
        }
        case ConstraintCategory::Slider:
        case ConstraintCategory::Spring:
        {
            auto* Slider = static_cast<JPH::SliderConstraint*>(CSlot.JoltConstraint.GetPtr());
            Slider->SetMotorState(JPH::EMotorState::Position);
            Slider->SetTargetPosition(TargetCoordinateOrVelocity);
            return true;
        }
        default:
            return false;
    }
}

ConstraintReading PhysicsExtension::InspectConstraintReading(ConstraintToken Token) const noexcept
{
    ConstraintReading Reading {};
    if (!Runtime || !Runtime->Initialized || !Token.Valid() || Token.SlotIndex > Runtime->ConstraintSlots.size())
    {
        return Reading;
    }
    const ConstraintSlotEntry& CSlot = Runtime->ConstraintSlots[Token.SlotIndex - 1u];
    if (!CSlot.Occupied || CSlot.Generation != Token.Generation)
    {
        return Reading;
    }
    Reading.Token           = Token;
    Reading.Resident        = true;
    Reading.Intact          = CSlot.Intact;
    Reading.AppliedForceN   = CSlot.AppliedForceN;
    Reading.AppliedTorqueNm = CSlot.AppliedTorqueNm;
    Reading.StrainRatio     = CSlot.StrainRatio;
    if (CSlot.Intact && CSlot.JoltConstraint != nullptr)
    {
        if (CSlot.Descriptor.Category == ConstraintCategory::Hinge || CSlot.Descriptor.Category == ConstraintCategory::Spin)
        {
            Reading.CurrentCoordinate = static_cast<const JPH::HingeConstraint*>(CSlot.JoltConstraint.GetPtr())->GetCurrentAngle();
        }
        else if (CSlot.Descriptor.Category == ConstraintCategory::Slider || CSlot.Descriptor.Category == ConstraintCategory::Spring)
        {
            Reading.CurrentCoordinate = static_cast<const JPH::SliderConstraint*>(CSlot.JoltConstraint.GetPtr())->GetCurrentPosition();
        }
    }
    return Reading;
}

RayCastHitReading PhysicsExtension::ExecuteRayCast(const RayCastQuery& Query) const noexcept
{
    RayCastHitReading Reading {};
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return Reading;
    }

    const Mathematics::Axis3D Dir = Mathematics::NormalizeAxis(Query.DirectionUnit, { 1.0f, 0.0f, 0.0f });
    const float Dist = std::max(Query.MaxDistanceMeters, 1.0e-3f);
    const JPH::RRayCast Ray(ToJoltRVec3(Query.OriginMeters), ToJoltVec3(Dir) * Dist);

    JPH::RayCastResult Hit {};
    const ChannelMaskLayerFilter       LayerFilter(Query.ChannelMask);
    const JPH::BroadPhaseLayerFilter   BroadFilter;
    if (!Runtime->JoltWorld->GetNarrowPhaseQuery().CastRay(Ray, Hit, BroadFilter, LayerFilter))
    {
        return Reading;
    }

    const JPH::BodyLockRead BodyLock(Runtime->JoltWorld->GetBodyLockInterface(), Hit.mBodyID);
    if (!BodyLock.Succeeded())
    {
        return Reading;
    }
    const JPH::Body& HitBody = BodyLock.GetBody();
    const JPH::RVec3 ImpactR = Ray.GetPointOnRay(Hit.mFraction);
    const JPH::Vec3  Normal  = HitBody.GetWorldSpaceSurfaceNormal(Hit.mSubShapeID2, ImpactR);

    Reading.HitDetected    = true;
    Reading.HitBody        = Runtime->ResolveBodyTokenFromTag(HitBody.GetUserData());
    Reading.Fraction       = Hit.mFraction;
    Reading.DistanceMeters = Hit.mFraction * Dist;
    Reading.WorldPoint     = FromJoltRVec3(ImpactR);
    Reading.SurfaceNormal  = FromJoltVec3(Normal);
    return Reading;
}

void PhysicsExtension::QuerySphereOverlaps(const SphereOverlapQuery& Query, std::vector<RigidBodyToken>& OutBodies) const
{
    OutBodies.clear();
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return;
    }

    class OverlapCollector final : public JPH::CollideShapeCollector
    {
    public:
        OverlapCollector(const JoltSimulationRuntime& InRuntime, std::vector<RigidBodyToken>& InOut)
            : SimRuntime(InRuntime)
            , OutTokens(InOut)
        {
        }

        void AddHit(const JPH::CollideShapeResult& Result) override
        {
            const JPH::BodyLockRead Lock(SimRuntime.JoltWorld->GetBodyLockInterface(), Result.mBodyID2);
            if (!Lock.Succeeded())
            {
                return;
            }
            const RigidBodyToken Token = SimRuntime.ResolveBodyTokenFromTag(Lock.GetBody().GetUserData());
            if (Token.Valid())
            {
                OutTokens.push_back(Token);
            }
        }

    private:
        const JoltSimulationRuntime& SimRuntime;
        std::vector<RigidBodyToken>& OutTokens;
    };

    const JPH::SphereShape Sphere(std::max(Query.RadiusMeters, 1.0e-3f));
    const JPH::RMat44 Transform = JPH::RMat44::sTranslation(ToJoltRVec3(Query.CenterMeters));
    JPH::CollideShapeSettings Settings {};
    OverlapCollector Collector(*Runtime, OutBodies);
    const JPH::BroadPhaseLayerFilter BroadFilter;
    const ChannelMaskLayerFilter     LayerFilter(Query.ChannelMask);

    Runtime->JoltWorld->GetNarrowPhaseQuery().CollideShape(
        &Sphere,
        JPH::Vec3::sReplicate(1.0f),
        Transform,
        Settings,
        JPH::RVec3::sZero(),
        Collector,
        BroadFilter,
        LayerFilter);
}

void PhysicsExtension::ExportRigidColliderProxies(
    std::vector<RigidColliderProxy>& OutProxies,
    uint32_t                         ChannelMask) const
{
    OutProxies.clear();
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return;
    }

    const JPH::BodyInterface& Bodies = Runtime->JoltWorld->GetBodyInterface();
    for (uint32_t I = 0u; I < Runtime->BodySlots.size(); ++I)
    {
        const BodySlotEntry& Entry = Runtime->BodySlots[I];
        if (!Entry.Occupied || !Entry.Descriptor.ColliderProxyEnabled)
        {
            continue;
        }
        const uint32_t Bit = 1u << static_cast<uint32_t>(Entry.Descriptor.CollisionChannel);
        if ((ChannelMask & Bit) == 0u)
        {
            continue;
        }

        const JPH::RVec3 Pos = Bodies.GetPosition(Entry.JoltId);
        const JPH::Quat  Rot = Bodies.GetRotation(Entry.JoltId);
        const JPH::Vec3  Vel = Bodies.GetLinearVelocity(Entry.JoltId);

        RigidColliderProxy Proxy {};
        Proxy.CenterMeters[0]      = static_cast<float>(Pos.GetX());
        Proxy.CenterMeters[1]      = static_cast<float>(Pos.GetY());
        Proxy.CenterMeters[2]      = static_cast<float>(Pos.GetZ());
        Proxy.ShapeCode            = Entry.ProxyShapeCode;
        Proxy.VelocityMps[0]       = Vel.GetX();
        Proxy.VelocityMps[1]       = Vel.GetY();
        Proxy.VelocityMps[2]       = Vel.GetZ();
        Proxy.RadiusMeters         = Entry.ProxyRadius;
        Proxy.HalfExtentsMeters[0] = Entry.ProxyHalfExtents.X;
        Proxy.HalfExtentsMeters[1] = Entry.ProxyHalfExtents.Y;
        Proxy.HalfExtentsMeters[2] = Entry.ProxyHalfExtents.Z;
        Proxy.Restitution          = Entry.Descriptor.Shape.Restitution;
        Proxy.OrientationQuat[0]   = Rot.GetX();
        Proxy.OrientationQuat[1]   = Rot.GetY();
        Proxy.OrientationQuat[2]   = Rot.GetZ();
        Proxy.OrientationQuat[3]   = Rot.GetW();
        Proxy.Friction             = Entry.Descriptor.Shape.Friction;
        Proxy.BodySlotIndex        = I + 1u;
        Proxy.ChannelMask          = Bit;
        Proxy.DynamicResponseBit   = (Entry.Descriptor.MotionCategory == RigidMotionCategory::Dynamic) ? 1u : 0u;
        OutProxies.push_back(Proxy);
    }
}

std::span<const ContactImpulseReading> PhysicsExtension::InspectRecentContacts() const noexcept
{
    if (!Runtime)
    {
        return {};
    }
    return Runtime->ContactJournal.InspectContacts();
}

uint32_t PhysicsExtension::QueryLiveBodyCount() const noexcept
{
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return 0u;
    }
    return Runtime->JoltWorld->GetNumBodies();
}

uint32_t PhysicsExtension::QueryActiveBodyCount() const noexcept
{
    if (!Runtime || !Runtime->Initialized || !Runtime->JoltWorld)
    {
        return 0u;
    }
    return Runtime->JoltWorld->GetNumActiveBodies(JPH::EBodyType::RigidBody);
}

uint32_t PhysicsExtension::QueryIntactBondCount() const noexcept
{
    if (!Runtime)
    {
        return 0u;
    }
    uint32_t Count = 0u;
    for (const ConstraintSlotEntry& CSlot : Runtime->ConstraintSlots)
    {
        if (CSlot.Occupied && CSlot.Intact)
        {
            ++Count;
        }
    }
    return Count;
}

uint64_t PhysicsExtension::QueryCompletedStepCount() const noexcept
{
    return Runtime ? Runtime->CompletedSteps : 0u;
}

double PhysicsExtension::QuerySimulatedSeconds() const noexcept
{
    return Runtime ? Runtime->SimulatedSeconds : 0.0;
}

std::vector<std::pair<uint32_t, uint32_t>> PhysicsExtension::BuildOuterConvexHullEdges(
    const std::vector<Mathematics::Axis3D>& Vertices)
{
    std::vector<std::pair<uint32_t, uint32_t>> OutEdges;
    if (Vertices.size() < 4u)
    {
        return OutEdges;
    }
    if (JPH::Allocate == nullptr)
    {
        JPH::RegisterDefaultAllocator();
    }

    JPH::ConvexHullBuilder::Positions Positions;
    Positions.reserve(Vertices.size());
    for (const auto& V : Vertices)
    {
        Positions.push_back(JPH::Vec3(V.X, V.Y, V.Z));
    }

    JPH::ConvexHullBuilder Builder(Positions);
    const char* HullErr = nullptr;
    const auto HullRes  = Builder.Initialize(std::numeric_limits<int>::max(), 1.0e-3f, HullErr);
    if (HullRes != JPH::ConvexHullBuilder::EResult::Success &&
        HullRes != JPH::ConvexHullBuilder::EResult::MaxVerticesReached)
    {
        return OutEdges;
    }

    std::vector<uint64_t> SeenKeys;
    SeenKeys.reserve(Builder.GetFaces().size() * 4u);
    OutEdges.reserve(Builder.GetFaces().size() * 3u);
    for (const auto* Face : Builder.GetFaces())
    {
        if (Face == nullptr || Face->mRemoved || Face->mFirstEdge == nullptr) continue;
        const auto* Edge = Face->mFirstEdge;
        do
        {
            const int StartIdx = Edge->mStartIdx;
            const int EndIdx   = Edge->mNextEdge->mStartIdx;
            if (StartIdx >= 0 && EndIdx >= 0 && StartIdx != EndIdx &&
                static_cast<size_t>(StartIdx) < Vertices.size() &&
                static_cast<size_t>(EndIdx) < Vertices.size())
            {
                const uint32_t Lo = static_cast<uint32_t>(std::min(StartIdx, EndIdx));
                const uint32_t Hi = static_cast<uint32_t>(std::max(StartIdx, EndIdx));
                const uint64_t Key = (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
                if (std::find(SeenKeys.begin(), SeenKeys.end(), Key) == SeenKeys.end())
                {
                    SeenKeys.push_back(Key);
                    OutEdges.emplace_back(Lo, Hi);
                }
            }
            Edge = Edge->mNextEdge;
        }
        while (Edge != nullptr && Edge != Face->mFirstEdge);
    }
    return OutEdges;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
