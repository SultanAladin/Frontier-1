/*============================================================================================================================================
                                                       FRACTURESIMULATIONRUNNER.CPP
============================================================================================================================================*/
// 🧩 JoltPhysics v5.6.0 rigid-body fracture simulation runner: ConvexHull shards, BreakableBond strain rupture, and ground interaction

#include "Engine/SimulationSpace/PhysicsSpace/FractureSimulationRunner.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::PhysicsSpace
{

FractureSimulationRunner::~FractureSimulationRunner() noexcept
{
    Clear();
}

void FractureSimulationRunner::Clear() noexcept
{
    BodyStates.clear();
    BondStates.clear();
    InitialCentroids.clear();
    GroundToken = {};
    if (Physics.QueryInitialized())
    {
        Physics.Finalize();
    }
    Loaded = false;
}

bool FractureSimulationRunner::LoadPartitionResult(
    const GeometrySpace::Fracture::FracturePartitionResult& Partition,
    const FractureSimulationParameters&                     Params) noexcept
{
    Clear();
    ActiveParams = Params;

    PhysicsSimulationParameters SimConfig {};
    SimConfig.GravityMps2        = { 0.0f, 0.0f, -static_cast<float>(Constants::StandardGravity) };
    SimConfig.FixedStepHz        = std::clamp(Params.FixedStepHz, 30.0f, 240.0f);
    SimConfig.MaxSubstepsPerTick = 8u;
    SimConfig.SolverThreadCount  = 0u;
    SimConfig.MaxRigidBodies     = std::max<uint32_t>(1024u, static_cast<uint32_t>(Partition.Fragments.size()) * 2u + 64u);

    Physics.~PhysicsExtension();
    new (&Physics) PhysicsExtension(SimConfig);
    if (!Physics.Initialize())
    {
        return false;
    }

    // RHR +Z Up static ground slab whose top surface sits at GroundPlaneZ
    RigidBodyDescriptor GroundDesc {};
    GroundDesc.MotionCategory          = RigidMotionCategory::Static;
    GroundDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    GroundDesc.PositionMeters          = { 0.0f, 0.0f, Params.GroundPlaneZ - 0.5f };
    GroundDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    GroundDesc.Shape.HalfExtentsMeters = { 40.0f, 40.0f, 0.5f };
    GroundDesc.Shape.Friction          = Params.Friction;
    GroundDesc.Shape.Restitution       = Params.Restitution;
    GroundToken = Physics.IssueRigidBody(GroundDesc);
    if (!GroundToken.Valid())
    {
        Clear();
        return false;
    }

    BodyStates.reserve(Partition.Fragments.size());
    InitialCentroids.reserve(Partition.Fragments.size());
    std::vector<Mathematics::Axis3D> LocalHullVertices;
    std::vector<uint32_t>            CompoundVertexCounts;

    for (uint32_t FragIdx = 0u; FragIdx < Partition.Fragments.size(); ++FragIdx)
    {
        const auto& Frag = Partition.Fragments[FragIdx];
        LocalHullVertices.clear();
        CompoundVertexCounts.clear();

        const float MinHalfExtent = 0.5f * std::max(0.001f, std::min({ Frag.Span.X, Frag.Span.Y, Frag.Span.Z }));
        const bool  UseMeshMode   = (Params.CollisionMode == FractureCollisionShapeMode::CollisionMesh);
        const float SafeConvexMargin = UseMeshMode
            ? std::clamp(MinHalfExtent * 0.12f, 0.001f, 0.003f)
            : std::clamp(MinHalfExtent * 0.25f, 0.002f, 0.008f);

        RigidBodyDescriptor Desc {};
        Desc.MotionCategory         = RigidMotionCategory::Dynamic;
        Desc.CollisionChannel       = static_cast<uint16_t>(CollisionLayerChannel::FractureFragment);
        Desc.PositionMeters         = Frag.Centroid + Params.SolidWorldOffsetM;
        Desc.Orientation            = {};
        Desc.LinearVelocity         = Params.ApplyImpactImpulse ? Frag.InitialVelocity : Mathematics::Axis3D { 0.0f, 0.0f, 0.0f };
        Desc.AngularVelocity        = Params.ApplyImpactImpulse ? Frag.InitialAngular  : Mathematics::Axis3D { 0.0f, 0.0f, 0.0f };
        Desc.ExplicitMassKg         = static_cast<float>(std::max(0.02, Frag.MassKg));
        Desc.LinearDamping          = Params.LinearDamping;
        Desc.AngularDamping         = Params.AngularDamping;
        Desc.AllowSleep             = true;
        Desc.ContinuousMotion       = Frag.RadiusM < 0.12;
        Desc.InstanceTag            = Frag.Index;
        Desc.Shape.DensityKgPerM3   = Partition.MaterialSpec.DensityKgPerM3;
        Desc.Shape.Friction         = Params.Friction;
        Desc.Shape.Restitution      = Params.Restitution;

        RigidBodyToken Token {};
        bool UsedBoxFallback = false;
        CollisionShapeCategory ActiveCat = CollisionShapeCategory::ConvexHull;
        uint32_t SubShapeCount = 1u;

        if (UseMeshMode && Frag.SubConvexHulls.size() >= 2u && MinHalfExtent >= 0.004f)
        {
            for (const auto& Sub : Frag.SubConvexHulls)
            {
                if (Sub.Vertices.size() < 4u) continue;
                CompoundVertexCounts.push_back(static_cast<uint32_t>(Sub.Vertices.size()));
                for (const auto& Vertex : Sub.Vertices)
                {
                    LocalHullVertices.push_back(Vertex - Frag.Centroid);
                }
            }
            if (CompoundVertexCounts.size() >= 2u)
            {
                Desc.Shape.ShapeCategory            = CollisionShapeCategory::CompoundHull;
                Desc.Shape.ConvexVertices           = LocalHullVertices;
                Desc.Shape.CompoundHullVertexCounts = CompoundVertexCounts;
                Desc.Shape.ConvexRadiusMeters       = SafeConvexMargin;
                Token = Physics.IssueRigidBody(Desc);
                if (Token.Valid())
                {
                    ActiveCat     = CollisionShapeCategory::CompoundHull;
                    SubShapeCount = static_cast<uint32_t>(CompoundVertexCounts.size());
                }
            }
        }

        if (!Token.Valid())
        {
            LocalHullVertices.clear();
            if (!Frag.SubConvexHulls.empty())
            {
                for (const auto& Sub : Frag.SubConvexHulls)
                {
                    for (const auto& Vertex : Sub.Vertices)
                    {
                        LocalHullVertices.push_back(Vertex - Frag.Centroid);
                    }
                }
            }
            else
            {
                LocalHullVertices.reserve(Frag.Polyhedron.Vertices.size());
                for (const auto& Vertex : Frag.Polyhedron.Vertices)
                {
                    LocalHullVertices.push_back(Vertex - Frag.Centroid);
                }
            }

            if (LocalHullVertices.size() >= 4u && MinHalfExtent >= 0.005f)
            {
                Desc.Shape.ShapeCategory            = CollisionShapeCategory::ConvexHull;
                Desc.Shape.ConvexVertices           = LocalHullVertices;
                Desc.Shape.CompoundHullVertexCounts = {};
                Desc.Shape.ConvexRadiusMeters       = SafeConvexMargin;
                Token = Physics.IssueRigidBody(Desc);
                if (Token.Valid())
                {
                    ActiveCat     = CollisionShapeCategory::ConvexHull;
                    SubShapeCount = 1u;
                }
            }
        }

        if (!Token.Valid())
        {
            UsedBoxFallback                      = true;
            ActiveCat                            = CollisionShapeCategory::Box;
            SubShapeCount                        = 1u;
            Desc.Shape.ShapeCategory             = CollisionShapeCategory::Box;
            Desc.Shape.ConvexVertices            = {};
            Desc.Shape.CompoundHullVertexCounts  = {};
            Desc.Shape.HalfExtentsMeters         = {
                std::max(0.01f, Frag.Span.X * 0.5f),
                std::max(0.01f, Frag.Span.Y * 0.5f),
                std::max(0.01f, Frag.Span.Z * 0.5f)
            };
            Token = Physics.IssueRigidBody(Desc);
        }

        FractureBodyTransformPose State {};
        State.FragmentIndex          = Frag.Index;
        State.Token                  = Token;
        State.PositionMeters         = Desc.PositionMeters;
        State.LinearVelocity         = Desc.LinearVelocity;
        State.AngularVelocity        = Desc.AngularVelocity;
        State.Awake                  = Token.Valid();
        State.UsingBoxFallback       = UsedBoxFallback;
        State.ActiveCollisionShape   = ActiveCat;
        State.CollisionSubShapeCount = SubShapeCount;
        BodyStates.push_back(State);
        InitialCentroids.push_back(Frag.Centroid);
    }

    if (Params.EnableBonds)
    {
        BondStates.reserve(Partition.Bonds.size());
        for (uint32_t BIdx = 0u; BIdx < Partition.Bonds.size(); ++BIdx)
        {
            const auto& Bond = Partition.Bonds[BIdx];
            FractureBondCondition State {};
            State.BondIndex   = BIdx;
            State.IndexA      = Bond.IndexA;
            State.IndexB      = Bond.IndexB;
            State.StrainRatio = static_cast<float>(Bond.StrainRatio);
            State.Intact      = !Bond.BrokenOnImpact;

            if (!Bond.BrokenOnImpact &&
                Bond.IndexA < BodyStates.size() &&
                Bond.IndexB < BodyStates.size() &&
                BodyStates[Bond.IndexA].Token.Valid() &&
                BodyStates[Bond.IndexB].Token.Valid())
            {
                ConstraintDescriptor CDesc {};
                CDesc.Category           = ConstraintCategory::BreakableBond;
                CDesc.FirstBody          = BodyStates[Bond.IndexA].Token;
                CDesc.SecondBody         = BodyStates[Bond.IndexB].Token;
                CDesc.AnchorWorldMeters  = (Bond.PointA + Bond.PointB) * 0.5f + Params.SolidWorldOffsetM;
                CDesc.BreakForceN        = static_cast<float>(std::max(25.0, Bond.CapacityKN * 1000.0));
                CDesc.BreakTorqueNm      = CDesc.BreakForceN * 0.35f;
                CDesc.DisableSelfCollide = true;

                State.Token  = Physics.IssueConstraint(CDesc);
                State.Intact = State.Token.Valid();
            }

            BondStates.push_back(State);
        }
    }

    Loaded = true;
    RefreshSnapshots();
    return true;
}

void FractureSimulationRunner::Step(double ΔtSeconds) noexcept
{
    if (!Loaded || ΔtSeconds <= 0.0)
    {
        return;
    }
    Physics.Tick(ΔtSeconds);
    RefreshSnapshots();
}

void FractureSimulationRunner::TriggerImpactImpulse(
    const Mathematics::Axis3D& WorldPointMeters,
    const Mathematics::Axis3D& ImpulseNs,
    float                      InfluenceRadiusM) noexcept
{
    if (!Loaded)
    {
        return;
    }

    const float Radius = std::max(0.05f, InfluenceRadiusM);
    for (const auto& State : BodyStates)
    {
        if (!State.Token.Valid()) continue;
        const float Dist = Mathematics::EvaluateDistance(State.PositionMeters, WorldPointMeters);
        if (Dist > Radius) continue;
        const float Weight = 1.0f - (Dist / Radius);
        const auto  ScaledImpulse = ImpulseNs * (Weight * Weight);
        Physics.ApplyImpulseAtPoint(State.Token, ScaledImpulse, WorldPointMeters);
    }
    RefreshSnapshots();
}

void FractureSimulationRunner::RefreshSnapshots() noexcept
{
    for (auto& State : BodyStates)
    {
        if (!State.Token.Valid()) continue;
        const auto Snap      = Physics.InspectBodySnapshot(State.Token);
        State.PositionMeters  = Snap.PositionMeters;
        State.Orientation     = Snap.Orientation;
        State.LinearVelocity  = Snap.LinearVelocity;
        State.AngularVelocity = Snap.AngularVelocity;
        State.KineticEnergyJ  = Snap.KineticEnergyJoules;
        State.PeakImpulseNs   = Snap.PeakContactImpulseNs;
        State.Awake           = Snap.Awake;
    }

    for (auto& Bond : BondStates)
    {
        if (!Bond.Token.Valid()) continue;
        const auto Reading = Physics.InspectConstraintReading(Bond.Token);
        Bond.Intact        = Reading.Intact;
        Bond.AppliedForceN = Reading.AppliedForceN;
        Bond.StrainRatio   = Reading.StrainRatio;
    }
}

bool FractureSimulationRunner::QueryLoaded() const noexcept
{
    return Loaded;
}

const std::vector<FractureBodyTransformPose>& FractureSimulationRunner::InspectBodyStates() const noexcept
{
    return BodyStates;
}

const std::vector<FractureBondCondition>& FractureSimulationRunner::InspectBondStates() const noexcept
{
    return BondStates;
}

FractureSimulationTelemetry FractureSimulationRunner::InspectTelemetry() const noexcept
{
    FractureSimulationTelemetry Telemetry {};
    Telemetry.SimulatedSeconds = Physics.QuerySimulatedSeconds();
    Telemetry.CompletedSteps   = Physics.QueryCompletedStepCount();
    Telemetry.FragmentCount    = static_cast<uint32_t>(BodyStates.size());
    Telemetry.TotalBondCount   = static_cast<uint32_t>(BondStates.size());
    Telemetry.ContactCount     = static_cast<uint32_t>(Physics.InspectRecentContacts().size());
    Telemetry.CollisionMode    = ActiveParams.CollisionMode;

    for (const auto& Body : BodyStates)
    {
        if (Body.Awake) ++Telemetry.AwakeFragmentCount;
        Telemetry.TotalSubShapes += Body.CollisionSubShapeCount;
        Telemetry.TotalKineticJ  += static_cast<double>(Body.KineticEnergyJ);
        Telemetry.MaxSpeedMps     = std::max(Telemetry.MaxSpeedMps, Mathematics::EvaluateLength(Body.LinearVelocity));
    }

    for (const auto& Bond : BondStates)
    {
        if (Bond.Intact) ++Telemetry.IntactBondCount;
        else ++Telemetry.BrokenBondCount;
    }

    return Telemetry;
}

const PhysicsExtension& FractureSimulationRunner::InspectPhysics() const noexcept
{
    return Physics;
}

PhysicsExtension& FractureSimulationRunner::AccessPhysics() noexcept
{
    return Physics;
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
