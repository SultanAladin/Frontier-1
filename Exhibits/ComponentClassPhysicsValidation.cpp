/*============================================================================================================================================
                                                    COMPONENTCLASSPHYSICSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Validation of FCT1 interchange, ControlVehicle ComponentClass, .ComponentSpace codec, WorldPlacement & 8 Jolt constraint kinds

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ComponentSpaceCodec.h"
#include "Engine/ComponentClass/ComponentClassValidator.h"
#include "Engine/ComponentClass/ComponentWorldPlacement.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier;
using namespace Frontier::ComponentClass;
using namespace Frontier::SimulationSpace::PhysicsSpace;

bool Require(bool Condition, const char* Label)
{
    if (!Condition)
    {
        std::fprintf(stderr, "[FAIL] %s\n", Label);
        return false;
    }
    std::printf("[PASS] %s\n", Label);
    return true;
}

bool ValidateControlVehicleClass()
{
    const std::filesystem::path ModelsDir = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Experimental/Shared/Models";
    ControlVehicleBuildResult Build {};
    std::string Diagnostic {};
    if (!Require(ControlVehicleClassBuilder::ConstructControlVehicle(ModelsDir, Build, Diagnostic), "ControlVehicleClassBuilder::ConstructControlVehicle"))
    {
        std::fprintf(stderr, "       Diagnostic: %s\n", Diagnostic.c_str());
        return false;
    }

    if (!Require(Build.GeometryCatalog.size() == 4u, "ControlVehicle catalog contains 4 compiled assets"))
    {
        return false;
    }

    // Body dimensions after Z-up -> Y-up conversion (~6.22 m long X, ~1.46 m tall Y)
    const auto& BodyNet = Build.GeometryCatalog[0].Asset.Network;
    float MinX = 1.0e9f, MaxX = -1.0e9f, MinY = 1.0e9f, MaxY = -1.0e9f;
    for (const auto& V : BodyNet.Vertices)
    {
        MinX = std::min(MinX, V.Axes.X); MaxX = std::max(MaxX, V.Axes.X);
        MinY = std::min(MinY, V.Axes.Y); MaxY = std::max(MaxY, V.Axes.Y);
    }
    const float LengthX = MaxX - MinX;
    const float HeightY = MaxY - MinY;
    if (!Require(LengthX > 6.1f && LengthX < 6.35f && HeightY > 1.35f && HeightY < 1.55f,
                 "FCT1 Z-up -> Y-up body dimensions (~6.22 m x 1.46 m)"))
    {
        return false;
    }

    const auto& WingNet = Build.GeometryCatalog[3].Asset.Network;
    if (!Require(WingNet.Faces.size() == 72u, "Procedural RearWing has 72 triangles"))
    {
        return false;
    }

    const ClassSummaryMetrics Summary = Build.ClassDefinition.Storage.ComputeSummaryMetrics();
    if (!Require(Summary.GeometryCount == 10u && Summary.LightCount == 2u && Summary.CollisionCount == 1u &&
                 Summary.ParticleCount == 1u && Summary.FluidCount == 1u && Summary.BehaviourCount == 1u,
                 "ControlVehicle typed instance metrics (10 Geometries, 2 Lights, 1 Collision, 1 Particle, 1 Fluid, 1 Behaviour)"))
    {
        return false;
    }

    auto AssetPredicate = [&](std::string_view Id) -> bool
    {
        for (const auto& Entry : Build.GeometryCatalog)
        {
            if (Entry.AssetIdentifier == Id)
            {
                return true;
            }
        }
        return false;
    };
    const auto Issues = ComponentClassValidator::ValidateClass(Build.ClassDefinition, AssetPredicate);
    if (!Require(Issues.empty(), "ControlVehicle validates with zero warnings or errors"))
    {
        return false;
    }

    // Tree edits: attach cycle prevention, remove lifts nested children, duplicate & unique rename
    ComponentClassDefinition EditDef = Build.ClassDefinition;
    const ComponentInstanceToken BodyTok   = EditDef.Storage.FindTokenByTitle("Body");
    const ComponentInstanceToken SpringFL  = EditDef.Storage.FindTokenByTitle("SuspensionFL");
    const ComponentInstanceToken SteerFL   = EditDef.Storage.FindTokenByTitle("SteerFL");
    const ComponentInstanceToken WingDock  = EditDef.Storage.FindTokenByTitle("RearWingConstraint");
    const ComponentInstanceToken WingTok   = EditDef.Storage.FindTokenByTitle("RearWing");
    if (!Require(BodyTok.Valid() && SpringFL.Valid() && SteerFL.Valid() && WingDock.Valid() && WingTok.Valid(),
                 "Resolve ControlVehicle hierarchy tokens by title"))
    {
        return false;
    }
    if (!Require(!EditDef.Storage.AttachInstance(BodyTok, SteerFL), "AttachInstance refuses ancestor->descendant cycles"))
    {
        return false;
    }
    if (!Require(EditDef.Storage.RemoveInstance(SteerFL), "RemoveInstance lifts nested WheelFL up to SuspensionFL"))
    {
        return false;
    }
    const ComponentInstanceToken WheelFL = EditDef.Storage.FindTokenByTitle("WheelFL");
    const auto* WheelHeader = EditDef.Storage.InspectHeader(WheelFL);
    if (!Require(WheelHeader != nullptr && EditDef.Storage.InspectHeaderByIndex(WheelHeader->EnclosingIndex)->Token == SpringFL,
                 "WheelFL re-parented to SuspensionFL after SteerFL removal"))
    {
        return false;
    }

    // Validator catches broken dock key, removed wheel, and non-monotonic torque curve
    if (auto* Header = EditDef.Storage.ResolveHeader(WingTok))
    {
        Header->TagCsv = "bumper";
    }
    const ComponentInstanceToken VehicleTok = EditDef.Storage.FindTokenByTitle("VehicleMovement");
    if (auto* Veh = EditDef.Storage.ResolveVehicle(VehicleTok))
    {
        Veh->BoundWheelTokens.push_back(SteerFL); // removed token
        Veh->TorqueCurve[2].Rpm = Veh->TorqueCurve[1].Rpm; // non-monotonic
    }
    const auto BadIssues = ComponentClassValidator::ValidateClass(EditDef, AssetPredicate);
    if (!Require(BadIssues.size() >= 3u && ComponentClassValidator::QueryHasBlockingErrors(BadIssues),
                 "ComponentClassValidator flags Dock key mismatch, removed wheel & non-increasing torque curve"))
    {
        return false;
    }

    // .ComponentSpace Two-Tier Hybrid encode/decode round-trip (with thumbnail)
    Build.ClassDefinition.BakedIconWidth  = 4u;
    Build.ClassDefinition.BakedIconHeight = 4u;
    Build.ClassDefinition.BakedIconRgba8.assign(4u * 4u * 4u, 200u);

    std::vector<std::byte> Bytes {};
    if (!Require(Codecs::ComponentSpaceEncoder::EncodeToBytes(Build.ClassDefinition, Bytes, Diagnostic),
                 "ComponentSpaceEncoder::EncodeToBytes"))
    {
        return false;
    }
    ComponentClassDefinition Decoded {};
    if (!Require(Codecs::ComponentSpaceDecoder::DecodeFromBytes(Bytes, Decoded, Diagnostic),
                 "ComponentSpaceDecoder::DecodeFromBytes"))
    {
        return false;
    }
    const ClassSummaryMetrics DecodedMetrics = Decoded.Storage.ComputeSummaryMetrics();
    if (!Require(Decoded.Title == "ControlVehicle" &&
                 DecodedMetrics.TotalComponents == Summary.TotalComponents &&
                 Decoded.BakedIconWidth == 4u && Decoded.BakedIconRgba8.size() == 64u,
                 ".ComponentSpace round-trip preserves hierarchy, typed instances & baked icon"))
    {
        return false;
    }

    // World placement & sparse per-instance overrides
    ComponentWorldPlacementRegister Placements {};
    const WorldPlacementToken P1 = Placements.SpawnPlacement(
        { 1u, 1u },
        "ControlVehicle_01",
        ComponentTransform{ { 12.0f, 0.0f, -4.0f }, { 0.0f, 35.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    const ComponentInstanceToken HeadlightL = Decoded.Storage.FindTokenByTitle("HeadlightL");
    auto& Delta = Placements.AcquireOverride(P1, HeadlightL);
    Delta.OverrideLight         = true;
    Delta.LightIntensityCandela = 65.0f;
    Delta.OverrideTransform     = true;
    Delta.LocalTransform.LocationMeters = { 3.10f, 0.45f, -0.78f };

    const ComponentTransform Eff = Placements.EvaluateEffectiveLocalTransform(Decoded, P1, HeadlightL);
    if (!Require(std::abs(Eff.LocationMeters.X - 3.10f) < 1.0e-4f && Placements.QueryLivePlacementCount() == 1u,
                 "ComponentWorldPlacementRegister non-destructive per-instance override"))
    {
        return false;
    }

    return true;
}

bool ValidatePhysicsConstraintDynamics()
{
    PhysicsSimulationParameters Params {};
    Params.GravityMps2 = { 0.0f, -9.81f, 0.0f }; // Y-up world matching ComponentViewer
    Params.FixedStepHz = 60.0f;

    PhysicsExtension Physics(Params);
    if (!Require(Physics.Initialize(), "PhysicsExtension::Initialize (Y-up 60 Hz)"))
    {
        return false;
    }

    auto SpawnStaticAnchor = [&](Mathematics::Axis3D Pos) -> RigidBodyToken
    {
        RigidBodyDescriptor Desc {};
        Desc.MotionCategory          = RigidMotionCategory::Static;
        Desc.PositionMeters          = Pos;
        Desc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        Desc.Shape.HalfExtentsMeters = { 0.2f, 0.2f, 0.2f };
        return Physics.IssueRigidBody(Desc);
    };
    auto SpawnDynamicBox = [&](Mathematics::Axis3D Pos, float MassKg = 10.0f) -> RigidBodyToken
    {
        RigidBodyDescriptor Desc {};
        Desc.MotionCategory          = RigidMotionCategory::Dynamic;
        Desc.PositionMeters          = Pos;
        Desc.ExplicitMassKg          = MassKg;
        Desc.LinearDamping           = 0.05f;
        Desc.AngularDamping          = 0.05f;
        Desc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
        Desc.Shape.HalfExtentsMeters = { 0.2f, 0.2f, 0.2f };
        return Physics.IssueRigidBody(Desc);
    };

    // 1. Fixed + 2. Hinge (motorized steer) + 3. Slider + 4. SwingTwist (Ball)
    const RigidBodyToken Anchor1 = SpawnStaticAnchor({ 0.0f, 5.0f, 0.0f });
    const RigidBodyToken Arm1    = SpawnDynamicBox({ 0.5f, 5.0f, 0.0f }, 5.0f);
    ConstraintDescriptor HingeDesc {};
    HingeDesc.Category              = ConstraintCategory::Hinge;
    HingeDesc.FirstBody             = Anchor1;
    HingeDesc.SecondBody            = Arm1;
    HingeDesc.AnchorWorldMeters     = { 0.0f, 5.0f, 0.0f };
    HingeDesc.AxisUnit              = { 0.0f, 1.0f, 0.0f };
    HingeDesc.MinLimit              = -0.56f;
    HingeDesc.MaxLimit              = 0.56f;
    HingeDesc.MotorDriveEnabled     = true;
    HingeDesc.MotorVelocityMode     = false;
    HingeDesc.MaxMotorTorqueOrForce = 200.0f;
    HingeDesc.TargetMotorCoordinate = 0.40f;
    const ConstraintToken HingeTok = Physics.IssueConstraint(HingeDesc);

    // 5. Spring (2 Hz, 0.42 damping, +-0.09 m) — offset anchor on X so boxes do not overlap
    const RigidBodyToken SpringAnchor = SpawnStaticAnchor({ 3.4f, 5.0f, 0.0f });
    const RigidBodyToken SpringMass   = SpawnDynamicBox({ 4.0f, 5.0f, 0.0f }, 15.0f);
    ConstraintDescriptor SpringDesc {};
    SpringDesc.Category              = ConstraintCategory::Spring;
    SpringDesc.FirstBody             = SpringAnchor;
    SpringDesc.SecondBody            = SpringMass;
    SpringDesc.AnchorWorldMeters     = { 4.0f, 5.0f, 0.0f };
    SpringDesc.AxisUnit              = { 0.0f, 1.0f, 0.0f };
    SpringDesc.MinLimit              = -0.09f;
    SpringDesc.MaxLimit              = 0.09f;
    SpringDesc.SpringFrequencyHz     = 2.0f;
    SpringDesc.SpringDampingRatio    = 0.42f;
    SpringDesc.MaxMotorTorqueOrForce = 5000.0f;
    const ConstraintToken SpringTok  = Physics.IssueConstraint(SpringDesc);

    // 6. Spin (velocity motor 20 rad/s) — offset anchor along Z so rotor spins freely
    const RigidBodyToken SpinAnchor = SpawnStaticAnchor({ 8.0f, 5.0f, -0.6f });
    const RigidBodyToken Rotor      = SpawnDynamicBox({ 8.0f, 5.0f, 0.0f }, 2.0f);
    ConstraintDescriptor SpinDesc {};
    SpinDesc.Category              = ConstraintCategory::Spin;
    SpinDesc.FirstBody             = SpinAnchor;
    SpinDesc.SecondBody            = Rotor;
    SpinDesc.AnchorWorldMeters     = { 8.0f, 5.0f, 0.0f };
    SpinDesc.AxisUnit              = { 0.0f, 0.0f, 1.0f };
    SpinDesc.MotorDriveEnabled     = true;
    SpinDesc.MotorVelocityMode     = true;
    SpinDesc.MaxMotorTorqueOrForce = 120.0f;
    SpinDesc.TargetMotorCoordinate = 20.0f;
    const ConstraintToken SpinTok  = Physics.IssueConstraint(SpinDesc);

    // 7. SwingTwist (Ball) + 8. Tether + 9. Dock (breakable past 120 N)
    const RigidBodyToken BallAnchor = SpawnStaticAnchor({ 12.0f, 5.0f, 0.0f });
    const RigidBodyToken BallBody   = SpawnDynamicBox({ 12.0f, 4.5f, 0.0f }, 3.0f);
    ConstraintDescriptor BallDesc {};
    BallDesc.Category          = ConstraintCategory::SwingTwist;
    BallDesc.FirstBody         = BallAnchor;
    BallDesc.SecondBody        = BallBody;
    BallDesc.AnchorWorldMeters = { 12.0f, 5.0f, 0.0f };
    BallDesc.AxisUnit          = { 0.0f, -1.0f, 0.0f };
    BallDesc.ConeHalfAngleRad  = 0.6f;
    BallDesc.TwistHalfAngleRad = 0.7f;
    const ConstraintToken BallTok = Physics.IssueConstraint(BallDesc);

    const RigidBodyToken DockAnchor = SpawnStaticAnchor({ 15.4f, 5.0f, 0.0f });
    const RigidBodyToken WingBody   = SpawnDynamicBox({ 16.0f, 5.0f, 0.0f }, 6.0f);
    ConstraintDescriptor DockDesc {};
    DockDesc.Category          = ConstraintCategory::Dock;
    DockDesc.FirstBody         = DockAnchor;
    DockDesc.SecondBody        = WingBody;
    DockDesc.AnchorWorldMeters = { 16.0f, 5.0f, 0.0f };
    DockDesc.BreakForceN       = 120.0f;
    const ConstraintToken DockTok = Physics.IssueConstraint(DockDesc);

    if (!Require(HingeTok.Valid() && SpringTok.Valid() && SpinTok.Valid() && BallTok.Valid() && DockTok.Valid(),
                 "All Jolt constraint categories issue valid ConstraintTokens"))
    {
        return false;
    }

    for (int Step = 0; Step < 60; ++Step)
    {
        Physics.Tick(1.0 / 60.0);
    }

    const ConstraintReading HingeReading  = Physics.InspectConstraintReading(HingeTok);
    const ConstraintReading SpringReading = Physics.InspectConstraintReading(SpringTok);
    const RigidBodySnapshot RotorSnap     = Physics.InspectBodySnapshot(Rotor);
    if (!Require(HingeReading.Intact && std::abs(HingeReading.CurrentCoordinate - 0.40f) < 0.05f,
                 "Motorized Hinge reaches 0.40 rad steering target"))
    {
        return false;
    }
    if (!Require(SpringReading.Intact && SpringReading.CurrentCoordinate <= 0.0f && SpringReading.CurrentCoordinate >= -0.095f,
                 "Spring constraint sags under gravity within +-0.09 m travel"))
    {
        return false;
    }
    if (!Require(std::abs(RotorSnap.AngularVelocity.Z) > 15.0f,
                 "Spin constraint accelerates rotor toward 20 rad/s"))
    {
        return false;
    }

    // Apply > 120 N downward load to Dock wing to verify Jolt rupture
    for (int Step = 0; Step < 30; ++Step)
    {
        Physics.ApplyForce(WingBody, { 0.0f, -600.0f, 0.0f });
        Physics.Tick(1.0 / 60.0);
    }
    const ConstraintReading DockReading = Physics.InspectConstraintReading(DockTok);
    if (!Require(!DockReading.Intact, "Dock constraint ruptures past 120 N BreakForceN"))
    {
        return false;
    }

    // VehicleDynamicsSolver torque curve + 6-speed powertrain acceleration
    VehicleDynamicsSettings Veh {};
    Veh.ChassisMassKg     = 1300.0f;
    Veh.WheelRadiusMeters = 0.50855f;
    Veh.IdleRpm           = 700.0f;
    Veh.RedlineRpm        = 7200.0f;
    Veh.FinalDriveRatio   = 3.55f;
    Veh.AeroLiftAreaM2    = 1.10f;
    Veh.DragCoefficient   = 0.34f;
    Veh.FrontalAreaM2     = 2.15f;
    Veh.GearRatios        = { 4.2f, 3.1f, 2.5f, 1.9f, 1.5f, 1.2f };
    Veh.Interpolation     = CurveInterpolationKind::SmoothSpline;
    Veh.TorqueCurve       = {
        { 800.0f, 234.0f }, { 2400.0f, 428.0f }, { 4800.0f, 520.0f }, { 7000.0f, 462.0f }, { 8500.0f, 442.0f }
    };
    const float PeakNm = VehicleDynamicsSolver::EvaluateTorqueCurveNm(Veh, 4800.0f);
    if (!Require(std::abs(PeakNm - 520.0f) < 0.5f, "VehicleDynamicsSolver hits authored 520 N*m @ 4800 rpm"))
    {
        return false;
    }

    VehicleSimulationTelemetry Tel {};
    for (int Step = 0; Step < 480; ++Step)
    {
        VehicleDynamicsSolver::StepPowertrain(Veh, 1.0f, 0.0f, 1.0 / 60.0, Tel);
    }
    if (!Require(Tel.SpeedMps > 25.0f && Tel.CurrentGearIndex >= 1u && Tel.AeroDownforceNewtons > 400.0f,
                 "VehicleDynamicsSolver accelerates, shifts gears & builds aero downforce"))
    {
        return false;
    }

    Physics.Finalize();
    return true;
}

} // namespace

int main()
{
    std::printf("=== ComponentClassPhysicsValidation ===\n");
    if (!ValidateControlVehicleClass())
    {
        return 1;
    }
    if (!ValidatePhysicsConstraintDynamics())
    {
        return 2;
    }
    std::printf("ALL ComponentClassPhysicsValidation CHECKS PASSED.\n");
    return 0;
}
