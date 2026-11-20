/*============================================================================================================================================
                                                      CONTROLVEHICLECLASSBUILDER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ControlVehicle FCT1 ingestion, RearWing synthesis, 4-corner Spring/Hinge wheels, Dock, Tether & VehicleDynamics

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Constants/MathConstants.h"
#include "Engine/Interchange/IndexedTriangleStreamInterchange.h"

#include <array>
#include <cmath>

namespace Frontier::ComponentClass
{

namespace
{

void AppendTiltedWingBox(
    GeometrySpace::PolygonNetwork& Network,
    Mathematics::Axis3D            Center,
    Mathematics::Scalar3D          HalfExtents,
    uint32_t                       CourseSlotId,
    float                          TiltRadians)
{
    const float Cs = std::cos(TiltRadians);
    const float Sn = std::sin(TiltRadians);

    std::array<uint32_t, 8u> V {};
    for (uint32_t Corner = 0u; Corner < 8u; ++Corner)
    {
        const float X = ((Corner & 1u) != 0u ? 1.0f : -1.0f) * HalfExtents.X;
        const float Y = ((Corner & 2u) != 0u ? 1.0f : -1.0f) * HalfExtents.Y;
        const float Z = ((Corner & 4u) != 0u ? 1.0f : -1.0f) * HalfExtents.Z;
        V[Corner] = Network.AppendVertex({
            Center.X + X * Cs - Y * Sn,
            Center.Y + X * Sn + Y * Cs,
            Center.Z + Z,
        });
    }

    const Interchange::CourseMaterialDescriptor* Mat =
        Interchange::IndexedTriangleStreamInterchange::InspectCourseMaterial(CourseSlotId);
    GeometrySpace::FaceShadingAssignment Shading {};
    if (Mat != nullptr)
    {
        Shading.AlbedoLinearRGB     = Mat->AlbedoLinearRGB;
        Shading.MatCapTintLinearRGB = Mat->AlbedoLinearRGB;
        Shading.Metallic            = Mat->Metallic;
        Shading.Roughness           = Mat->Roughness;
        Shading.Specular            = 0.5f;
        Shading.MatCapBlend         = 0.0f;
        Shading.MatCapSlot          = static_cast<uint8_t>(CourseSlotId & 0x0Fu);
    }

    constexpr std::array<std::array<uint32_t, 4u>, 6u> Quads {{
        { 0u, 2u, 3u, 1u },
        { 4u, 5u, 7u, 6u },
        { 0u, 1u, 5u, 4u },
        { 2u, 6u, 7u, 3u },
        { 0u, 4u, 6u, 2u },
        { 1u, 3u, 7u, 5u },
    }};

    const std::array<Mathematics::UVAxes, 3u> DefaultUVs {{
        { 0.10f, 0.10f },
        { 0.90f, 0.10f },
        { 0.50f, 0.90f },
    }};

    for (const auto& Q : Quads)
    {
        const std::array<uint32_t, 3u> T0 { V[Q[0]], V[Q[1]], V[Q[2]] };
        const std::array<uint32_t, 3u> T1 { V[Q[0]], V[Q[2]], V[Q[3]] };
        (void)Network.AppendFace(T0, DefaultUVs, Shading, 1u, static_cast<uint16_t>(CourseSlotId));
        (void)Network.AppendFace(T1, DefaultUVs, Shading, 1u, static_cast<uint16_t>(CourseSlotId));
    }
}

} // namespace

GeometrySpace::ConstructedSampleAsset ControlVehicleClassBuilder::ConstructProceduralRearWing()
{
    GeometrySpace::ConstructedSampleAsset Out {};
    Out.Network.NetworkTitle = "RearWing";
    Out.UDIMTiles = { 1001u };

    constexpr float AoaDeg         = 8.0f;
    constexpr float BodyHalfWidth  = 1.2498f;
    constexpr float ChordHalf      = 0.17f;
    constexpr float PlaneHalfThick = 0.016f;
    constexpr float PlateHalfThick = 0.008f;
    constexpr float PlaneY         = 0.25f;

    const float Tilt = -AoaDeg * static_cast<float>(Constants::DegToRad);
    const float PlateZ = BodyHalfWidth - PlateHalfThick;
    const float Span   = PlateZ - PlateHalfThick;

    AppendTiltedWingBox(Out.Network, { 0.0f, PlaneY, 0.0f }, { ChordHalf, PlaneHalfThick, Span }, 8u, Tilt);

    const float TeX = -ChordHalf * std::cos(Tilt) - PlaneHalfThick * std::sin(Tilt);
    const float TeY = PlaneY - ChordHalf * std::sin(Tilt) + PlaneHalfThick * std::cos(Tilt);
    AppendTiltedWingBox(
        Out.Network,
        { TeX + 0.008f * std::cos(Tilt) - 0.022f * std::sin(Tilt), TeY + 0.008f * std::sin(Tilt) + 0.022f * std::cos(Tilt), 0.0f },
        { 0.008f, 0.022f, Span },
        6u,
        Tilt);

    AppendTiltedWingBox(Out.Network, { -0.01f, 0.23f, -PlateZ }, { 0.20f, 0.10f, PlateHalfThick }, 6u, 0.0f);
    AppendTiltedWingBox(Out.Network, { -0.01f, 0.23f,  PlateZ }, { 0.20f, 0.10f, PlateHalfThick }, 6u, 0.0f);
    AppendTiltedWingBox(Out.Network, {  0.02f, 0.11f,  -0.56f }, { 0.05f, 0.11f, 0.01f }, 10u, 0.0f);
    AppendTiltedWingBox(Out.Network, {  0.02f, 0.11f,   0.56f }, { 0.05f, 0.11f, 0.01f }, 10u, 0.0f);

    Out.Network.LinkOppositeEdges(false);
    Out.Network.MarkDihedralSharpSeams(40.0f, false);
    Out.Network.RecomputeNormals();
    Out.Layers.SynchronizePartitions(Out.Network, GeometrySpace::GeometryInstanceToken{ 0u, 1u });
    return Out;
}

bool ControlVehicleClassBuilder::ConstructControlVehicle(
    const std::filesystem::path& ModelsDirectory,
    ControlVehicleBuildResult&   OutResult,
    std::string&                 OutDiagnostic)
{
    OutDiagnostic.clear();
    OutResult = ControlVehicleBuildResult{};

    struct PartSpec
    {
        const char* Identifier;
        const char* Title;
        const char* FileName;
    };

    constexpr std::array<PartSpec, 3u> FileParts {{
        { "ControlVehicleBody", "ControlVehicle Body", "ControlVehicleBody.facet" },
        { "ControlVehicleTyre", "ControlVehicle Tyre", "ControlVehicleTyre.facet" },
        { "ControlVehicleRim",  "ControlVehicle Rim",  "ControlVehicleRim.facet"  },
    }};

    for (const PartSpec& Spec : FileParts)
    {
        CompiledGeometryRepositoryEntry Entry {};
        Entry.AssetIdentifier = Spec.Identifier;
        Entry.DisplayTitle    = Spec.Title;
        Entry.SourceFormat    = "FCT1";
        if (!Interchange::IndexedTriangleStreamInterchange::ImportFct1File(
                ModelsDirectory / Spec.FileName,
                true,
                Entry.Asset,
                OutDiagnostic))
        {
            return false;
        }
        OutResult.GeometryRepository.push_back(std::move(Entry));
    }

    {
        CompiledGeometryRepositoryEntry WingEntry {};
        WingEntry.AssetIdentifier = "RearWing";
        WingEntry.DisplayTitle    = "Rear Wing";
        WingEntry.SourceFormat    = "Procedural";
        WingEntry.Asset           = ConstructProceduralRearWing();
        OutResult.GeometryRepository.push_back(std::move(WingEntry));
    }

    ComponentClassDefinition& Def = OutResult.ClassDefinition;
    Def.Title     = "ControlVehicle";
    Def.ClassArchetype = ComponentClassArchetype::Driveable;
    Def.Storage.ResetWithSpatialOrigin("Origin");

    const ComponentInstanceToken Origin = Def.Storage.QueryOriginToken();

    // Ground offset = TyreRadius (0.50855 m) - AxleZ (0.0914 m) = 0.41715 m
    constexpr float GroundOffset = 0.50855f - 0.0914f;
    const ComponentInstanceToken Body = Def.Storage.AppendInstance(
        ComponentCategory::GeometryInstance,
        "Body",
        Origin,
        ComponentTransform{ { 0.0f, GroundOffset, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Body))
    {
        G->AssetIdentifier  = "ControlVehicleBody";
        G->CastShadow       = true;
        G->CollisionProfile = GeometryCollisionProfile::BlockAll;
    }

    struct AxleSpec
    {
        const char* Suffix;
        float       AuthoredX;
        float       AuthoredY;
        float       AuthoredZ;
        bool        Steerable;
    };

    constexpr std::array<AxleSpec, 4u> Axles {{
        { "FL",  1.7274f,  1.0475f, 0.0914f, true  },
        { "FR",  1.7274f, -1.0475f, 0.0914f, true  },
        { "RL", -1.6686f,  1.0475f, 0.0914f, false },
        { "RR", -1.6686f, -1.0475f, 0.0914f, false },
    }};

    std::vector<ComponentInstanceToken> WheelTokens {};
    WheelTokens.reserve(4u);

    for (const AxleSpec& Axle : Axles)
    {
        const std::string SpringName = std::string("Suspension") + Axle.Suffix;
        const ComponentInstanceToken Spring = Def.Storage.AppendInstance(
            ComponentCategory::ConstraintInstance,
            SpringName,
            Body,
            ComponentTransform{ { Axle.AuthoredX, Axle.AuthoredZ, -Axle.AuthoredY }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
        if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(Spring))
        {
            C->Form                = PhysicsConstraintCategory::Spring;
            C->Axis                = ConstraintAxis::AxisY;
            C->NaturalFrequencyHz  = 2.00f;
            C->DampingRatioZeta    = 0.42f;
            C->TravelEachWayMeters = 0.090f;
            C->MinTravelMeters     = -0.090f;
            C->MaxTravelMeters     = 0.090f;
        }

        ComponentInstanceToken WheelMount = Spring;
        if (Axle.Steerable)
        {
            const std::string SteerName = std::string("Steer") + Axle.Suffix;
            WheelMount = Def.Storage.AppendInstance(
                ComponentCategory::ConstraintInstance,
                SteerName,
                Spring,
                ComponentTransform{});
            if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(WheelMount))
            {
                C->Form               = PhysicsConstraintCategory::Hinge;
                C->Axis               = ConstraintAxis::AxisY;
                C->DriveMode          = ConstraintDriveMode::Steer;
                C->MinAngleDegrees    = -32.0f;
                C->MaxAngleDegrees    = 32.0f;
                C->NormalizedEditPose = 0.50f;
            }
        }

        const std::string TyreName = std::string("Wheel") + Axle.Suffix;
        const ComponentInstanceToken Tyre = Def.Storage.AppendInstance(
            ComponentCategory::GeometryInstance,
            TyreName,
            WheelMount,
            ComponentTransform{ { 0.0f, 0.0f, 0.0f }, { 0.0f, (Axle.AuthoredY < 0.0f) ? 180.0f : 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
        if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Tyre))
        {
            G->AssetIdentifier  = "ControlVehicleTyre";
            G->CollisionProfile = GeometryCollisionProfile::BlockAll;
        }

        const std::string RimName = TyreName + "Rim";
        const ComponentInstanceToken Rim = Def.Storage.AppendInstance(
            ComponentCategory::GeometryInstance,
            RimName,
            Tyre,
            ComponentTransform{});
        if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Rim))
        {
            G->AssetIdentifier  = "ControlVehicleRim";
            G->CollisionProfile = GeometryCollisionProfile::NoCollision;
        }

        WheelTokens.push_back(Tyre);
    }

    // Headlights (SpotBeam)
    for (int Side = 0; Side < 2; ++Side)
    {
        const ComponentInstanceToken Lamp = Def.Storage.AppendInstance(
            ComponentCategory::LightInstance,
            Side == 0 ? "HeadlightL" : "HeadlightR",
            Body,
            ComponentTransform{ { 2.95f, 0.42f, Side == 0 ? -0.78f : 0.78f }, { 0.0f, 0.0f, -4.0f }, { 1.0f, 1.0f, 1.0f } });
        if (LightComponentSettings* L = Def.Storage.ResolveLight(Lamp))
        {
            L->Form                   = LightSourceCategory::SpotBeam;
            L->LinearColourRGB        = { 0.96f, 0.97f, 1.00f };
            L->IntensityCandela       = 30.0f;
            L->AttenuationRangeMeters = 30.0f;
            L->ConeAngleDegrees       = 28.0f;
        }
    }

    // RearWing Dock + RearWing
    const ComponentInstanceToken WingDock = Def.Storage.AppendInstance(
        ComponentCategory::ConstraintInstance,
        "RearWingConstraint",
        Body,
        ComponentTransform{ { -2.9822f, 0.8367f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(WingDock))
    {
        C->Form              = PhysicsConstraintCategory::Dock;
        C->DockKey           = "wing";
        C->SnapRadiusMeters  = 0.30f;
        C->BreakForceNewtons = 1200.0f;
        C->DockConnected     = true;
    }

    const ComponentInstanceToken Wing = Def.Storage.AppendInstance(
        ComponentCategory::GeometryInstance,
        "RearWing",
        WingDock,
        ComponentTransform{});
    if (GeometryComponentSettings* G = Def.Storage.ResolveGeometry(Wing))
    {
        G->AssetIdentifier  = "RearWing";
        G->CollisionProfile = GeometryCollisionProfile::BlockAll;
    }
    if (ComponentInstanceHeader* H = Def.Storage.ResolveHeader(Wing))
    {
        H->TagCsv = "wing";
    }

    // TowStrap Tether
    const ComponentInstanceToken TowStrap = Def.Storage.AppendInstance(
        ComponentCategory::ConstraintInstance,
        "TowStrap",
        Body,
        ComponentTransform{ { -3.10f, 0.10f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (ConstraintComponentSettings* C = Def.Storage.ResolveConstraint(TowStrap))
    {
        C->Form              = PhysicsConstraintCategory::Tether;
        C->RopeLengthMeters  = 0.32f;
        C->BreakForceNewtons = 1800.0f;
    }

    // Forward indicator, ChaseCamera & FrontBumper collision trigger
    const ComponentInstanceToken Forward = Def.Storage.AppendInstance(
        ComponentCategory::DirectionArrow,
        "Forward",
        Body,
        ComponentTransform{ { 3.20f, 0.20f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (DirectionArrowSettings* A = Def.Storage.ResolveArrow(Forward))
    {
        A->LengthMeters = 0.80f;
    }

    const ComponentInstanceToken ChaseCam = Def.Storage.AppendInstance(
        ComponentCategory::CameraInstance,
        "ChaseCamera",
        Origin,
        ComponentTransform{ { -7.20f, 2.40f, 0.0f }, { 0.0f, 0.0f, -12.0f }, { 1.0f, 1.0f, 1.0f } });
    if (CameraComponentSettings* Cam = Def.Storage.ResolveCamera(ChaseCam))
    {
        Cam->VerticalFovDegrees = 62.0f;
    }

    const ComponentInstanceToken Bumper = Def.Storage.AppendInstance(
        ComponentCategory::CollisionVolume,
        "FrontBumperTrigger",
        Body,
        ComponentTransform{ { 3.05f, 0.10f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (CollisionVolumeSettings* Col = Def.Storage.ResolveCollision(Bumper))
    {
        Col->Form               = CollisionVolumeCategory::Box;
        Col->HalfExtentsMeters  = { 0.18f, 0.28f, 1.05f };
        Col->TriggerOverlapOnly = true;
    }

    // VehicleDynamics behaviour
    const ComponentInstanceToken Vehicle = Def.Storage.AppendInstance(
        ComponentCategory::VehicleDynamics,
        "VehicleMovement",
        {});
    if (VehicleDynamicsSettings* V = Def.Storage.ResolveVehicle(Vehicle))
    {
        V->ChassisMassKg     = 1300.0f;
        V->WheelRadiusMeters = 0.50855f;
        V->IdleRpm           = 700.0f;
        V->RedlineRpm        = 7200.0f;
        V->DragCoefficient   = 0.34f;
        V->FrontalAreaM2     = 2.15f;
        V->AeroLiftAreaM2    = 1.10f;
        V->FinalDriveRatio   = 3.55f;
        V->GearRatios        = { 4.2f, 3.1f, 2.5f, 1.9f, 1.5f, 1.2f };
        V->Interpolation     = CurveInterpolationForm::SmoothSpline;
        V->BoundWheelTokens  = std::move(WheelTokens);
        V->TorqueCurve       = {
            {  800.0f, 234.0f }, { 1200.0f, 300.0f }, { 1600.0f, 352.0f }, { 2000.0f, 392.0f },
            { 2400.0f, 428.0f }, { 2800.0f, 460.0f }, { 3200.0f, 486.0f }, { 3600.0f, 504.0f },
            { 4000.0f, 514.0f }, { 4400.0f, 518.0f }, { 4800.0f, 520.0f }, { 5200.0f, 520.0f },
            { 5800.0f, 520.0f }, { 6200.0f, 506.0f }, { 6600.0f, 486.0f }, { 7000.0f, 462.0f },
            { 7600.0f, 448.0f }, { 8500.0f, 442.0f },
        };
    }

    Def.FinalizeCompiledRevision();
    return true;
}

} // namespace Frontier::ComponentClass
