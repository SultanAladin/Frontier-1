/*============================================================================================================================================
                                                          COMPONENTSPACECODEC.CPP
============================================================================================================================================*/
// 🧩 Implementation of .ComponentSpace Two-Tier Hybrid TOML manifest + 64B-aligned binary thumbnail & Revision serialization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/ComponentSpaceCodec.h"

#include <cstring>
#include <unordered_map>

namespace Frontier::Codecs
{

namespace
{

[[nodiscard]] toml::array EncodeVec3(const Mathematics::Axis3D& Vec)
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(Vec.X));
    Arr.push_back(static_cast<double>(Vec.Y));
    Arr.push_back(static_cast<double>(Vec.Z));
    return Arr;
}

[[nodiscard]] toml::array EncodeScalar3(const Mathematics::Scalar3D& Vec)
{
    toml::array Arr;
    Arr.push_back(static_cast<double>(Vec.X));
    Arr.push_back(static_cast<double>(Vec.Y));
    Arr.push_back(static_cast<double>(Vec.Z));
    return Arr;
}

[[nodiscard]] Mathematics::Axis3D DecodeVec3(const toml::array* Arr, Mathematics::Axis3D Fallback = {}) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return Fallback;
    }
    return {
        static_cast<float>((*Arr)[0].value_or(static_cast<double>(Fallback.X))),
        static_cast<float>((*Arr)[1].value_or(static_cast<double>(Fallback.Y))),
        static_cast<float>((*Arr)[2].value_or(static_cast<double>(Fallback.Z))),
    };
}

[[nodiscard]] Mathematics::Scalar3D DecodeScalar3(const toml::array* Arr, Mathematics::Scalar3D Fallback = {}) noexcept
{
    if (Arr == nullptr || Arr->size() < 3u)
    {
        return Fallback;
    }
    return {
        static_cast<float>((*Arr)[0].value_or(static_cast<double>(Fallback.X))),
        static_cast<float>((*Arr)[1].value_or(static_cast<double>(Fallback.Y))),
        static_cast<float>((*Arr)[2].value_or(static_cast<double>(Fallback.Z))),
    };
}

void WriteUint32LE(std::vector<std::byte>& Out, uint32_t V)
{
    const std::size_t Offset = Out.size();
    Out.resize(Offset + sizeof(V));
    std::memcpy(Out.data() + Offset, &V, sizeof(V));
}

[[nodiscard]] uint32_t ReadUint32LE(const std::byte* Ptr) noexcept
{
    uint32_t V = 0u;
    std::memcpy(&V, Ptr, sizeof(V));
    return V;
}

} // namespace

SpaceContainerPayload ComponentSpaceEncoder::ConstructPayload(
    const ComponentClass::ComponentClassDefinition& Definition,
    const Workspaces::RevisionTracker*              OptionalRevision)
{
    SpaceContainerPayload Payload {};
    Payload.SuffixCategory     = SpaceSuffixCategory::ComponentSpace;
    Payload.DocumentTitle      = Definition.Title;
    Payload.AuthoringWorkspace = "Frontier.ComponentEditor";

    toml::table HeaderTbl;
    HeaderTbl.insert_or_assign("Title", Definition.Title);
    HeaderTbl.insert_or_assign("ClassRole", static_cast<int64_t>(Definition.ClassArchetype));
    HeaderTbl.insert_or_assign("RevisionNumber", static_cast<int64_t>(Definition.RevisionNumber));
    HeaderTbl.insert_or_assign("CompiledRevision", static_cast<int64_t>(Definition.CompiledRevision));
    HeaderTbl.insert_or_assign("CompiledDirty", Definition.CompiledDirty);
    Payload.DomainManifest.insert_or_assign(".DocumentHeader", std::move(HeaderTbl));

    const ComponentClass::ComponentInstanceStorage& Storage = Definition.Storage;
    toml::array InstancesArray;

    auto AppendEntry = [&](const ComponentClass::ComponentInstanceHeader& H)
    {
        toml::table Entry;
        Entry.insert_or_assign("Title", H.Title);
        Entry.insert_or_assign("Category", static_cast<int64_t>(H.Category));
        if (H.EnclosingIndex != ComponentClass::InvalidNestingIndex)
        {
            if (const auto* Enc = Storage.InspectHeaderByIndex(H.EnclosingIndex))
            {
                Entry.insert_or_assign("EnclosingTitle", Enc->Title);
            }
        }
        Entry.insert_or_assign("Location", EncodeVec3(H.LocalTransform.LocationMeters));
        Entry.insert_or_assign("Rotation", EncodeVec3(H.LocalTransform.RotationDegrees));
        Entry.insert_or_assign("Scale", EncodeScalar3(H.LocalTransform.ScaleFactors));
        Entry.insert_or_assign("Visible", H.VisibilityEnabled);
        Entry.insert_or_assign("HiddenInPlay", H.HiddenInPlay);
        if (!H.TagCsv.empty())
        {
            Entry.insert_or_assign("TagCsv", H.TagCsv);
        }

        switch (H.Category)
        {
            case ComponentClass::ComponentCategory::GeometryInstance:
                if (const auto* G = Storage.InspectGeometry(H.Token))
                {
                    Entry.insert_or_assign("AssetIdentifier", G->AssetIdentifier);
                    Entry.insert_or_assign("ForcedLodLevel", static_cast<int64_t>(G->ForcedLodLevel));
                    Entry.insert_or_assign("CastShadow", G->CastShadow);
                    Entry.insert_or_assign("CollisionProfile", static_cast<int64_t>(G->CollisionProfile));
                }
                break;
            case ComponentClass::ComponentCategory::ConstraintInstance:
                if (const auto* C = Storage.InspectConstraint(H.Token))
                {
                    Entry.insert_or_assign("ConstraintKind", static_cast<int64_t>(C->Form));
                    Entry.insert_or_assign("Axis", static_cast<int64_t>(C->Axis));
                    Entry.insert_or_assign("DriveMode", static_cast<int64_t>(C->DriveMode));
                    Entry.insert_or_assign("MinAngleDegrees", static_cast<double>(C->MinAngleDegrees));
                    Entry.insert_or_assign("MaxAngleDegrees", static_cast<double>(C->MaxAngleDegrees));
                    Entry.insert_or_assign("MinTravelMeters", static_cast<double>(C->MinTravelMeters));
                    Entry.insert_or_assign("MaxTravelMeters", static_cast<double>(C->MaxTravelMeters));
                    Entry.insert_or_assign("ConeAngleDegrees", static_cast<double>(C->ConeAngleDegrees));
                    Entry.insert_or_assign("TwistLimitDegrees", static_cast<double>(C->TwistLimitDegrees));
                    Entry.insert_or_assign("NaturalFrequencyHz", static_cast<double>(C->NaturalFrequencyHz));
                    Entry.insert_or_assign("DampingRatioZeta", static_cast<double>(C->DampingRatioZeta));
                    Entry.insert_or_assign("TravelEachWayMeters", static_cast<double>(C->TravelEachWayMeters));
                    Entry.insert_or_assign("TargetRpm", static_cast<double>(C->TargetRpm));
                    Entry.insert_or_assign("MaxMotorTorqueNm", static_cast<double>(C->MaxMotorTorqueNm));
                    Entry.insert_or_assign("RopeLengthMeters", static_cast<double>(C->RopeLengthMeters));
                    Entry.insert_or_assign("BreakForceNewtons", static_cast<double>(C->BreakForceNewtons));
                    Entry.insert_or_assign("BreakTorqueNm", static_cast<double>(C->BreakTorqueNm));
                    Entry.insert_or_assign("SnapRadiusMeters", static_cast<double>(C->SnapRadiusMeters));
                    Entry.insert_or_assign("NormalizedEditPose", static_cast<double>(C->NormalizedEditPose));
                    Entry.insert_or_assign("DockKey", C->DockKey);
                    Entry.insert_or_assign("DockConnected", C->DockConnected);
                    Entry.insert_or_assign("DisableSelfCollide", C->DisableSelfCollide);
                }
                break;
            case ComponentClass::ComponentCategory::LightInstance:
                if (const auto* L = Storage.InspectLight(H.Token))
                {
                    Entry.insert_or_assign("LightKind", static_cast<int64_t>(L->Form));
                    Entry.insert_or_assign("LinearColourRGB", EncodeScalar3(L->LinearColourRGB));
                    Entry.insert_or_assign("IntensityCandela", static_cast<double>(L->IntensityCandela));
                    Entry.insert_or_assign("AttenuationRangeMeters", static_cast<double>(L->AttenuationRangeMeters));
                    Entry.insert_or_assign("ConeAngleDegrees", static_cast<double>(L->ConeAngleDegrees));
                    Entry.insert_or_assign("PenumbraSoftness", static_cast<double>(L->PenumbraSoftness));
                    Entry.insert_or_assign("CastShadow", L->CastShadow);
                    Entry.insert_or_assign("IesProfileIdentifier", L->IesProfileIdentifier);
                }
                break;
            case ComponentClass::ComponentCategory::CameraInstance:
                if (const auto* Cam = Storage.InspectCamera(H.Token))
                {
                    Entry.insert_or_assign("VerticalFovDegrees", static_cast<double>(Cam->VerticalFovDegrees));
                    Entry.insert_or_assign("NearClipMeters", static_cast<double>(Cam->NearClipMeters));
                    Entry.insert_or_assign("FarClipMeters", static_cast<double>(Cam->FarClipMeters));
                    Entry.insert_or_assign("AspectRatio", static_cast<double>(Cam->AspectRatio));
                }
                break;
            case ComponentClass::ComponentCategory::CollisionVolume:
                if (const auto* Col = Storage.InspectCollision(H.Token))
                {
                    Entry.insert_or_assign("CollisionKind", static_cast<int64_t>(Col->Form));
                    Entry.insert_or_assign("HalfExtentsMeters", EncodeScalar3(Col->HalfExtentsMeters));
                    Entry.insert_or_assign("RadiusMeters", static_cast<double>(Col->RadiusMeters));
                    Entry.insert_or_assign("HalfHeightMeters", static_cast<double>(Col->HalfHeightMeters));
                    Entry.insert_or_assign("ConvexMarginMeters", static_cast<double>(Col->ConvexMarginMeters));
                    Entry.insert_or_assign("CollisionChannel", static_cast<int64_t>(Col->CollisionChannel));
                    Entry.insert_or_assign("CollideWithMask", static_cast<int64_t>(Col->CollideWithMask));
                    Entry.insert_or_assign("PhysicsMaterialId", static_cast<int64_t>(Col->PhysicsMaterialId));
                    Entry.insert_or_assign("MaxCompoundHulls", static_cast<int64_t>(Col->MaxCompoundHulls));
                    Entry.insert_or_assign("TriggerOverlapOnly", Col->TriggerOverlapOnly);
                    if (!Col->SourceGeometryIdentifier.empty())
                    {
                        Entry.insert_or_assign("SourceGeometryIdentifier", Col->SourceGeometryIdentifier);
                    }
                }
                break;
            case ComponentClass::ComponentCategory::DirectionArrow:
                if (const auto* A = Storage.InspectArrow(H.Token))
                {
                    Entry.insert_or_assign("LengthMeters", static_cast<double>(A->LengthMeters));
                    Entry.insert_or_assign("LinearColourRGB", EncodeScalar3(A->LinearColourRGB));
                }
                break;
            case ComponentClass::ComponentCategory::VehicleDynamics:
                if (const auto* V = Storage.InspectVehicle(H.Token))
                {
                    Entry.insert_or_assign("ChassisMassKg", static_cast<double>(V->ChassisMassKg));
                    Entry.insert_or_assign("WheelRadiusMeters", static_cast<double>(V->WheelRadiusMeters));
                    Entry.insert_or_assign("IdleRpm", static_cast<double>(V->IdleRpm));
                    Entry.insert_or_assign("RedlineRpm", static_cast<double>(V->RedlineRpm));
                    Entry.insert_or_assign("DragCoefficient", static_cast<double>(V->DragCoefficient));
                    Entry.insert_or_assign("FrontalAreaM2", static_cast<double>(V->FrontalAreaM2));
                    Entry.insert_or_assign("AeroLiftAreaM2", static_cast<double>(V->AeroLiftAreaM2));
                    Entry.insert_or_assign("FinalDriveRatio", static_cast<double>(V->FinalDriveRatio));
                    Entry.insert_or_assign("Interpolation", static_cast<int64_t>(V->Interpolation));
                    toml::array Gears;
                    for (const float R : V->GearRatios)
                    {
                        Gears.push_back(static_cast<double>(R));
                    }
                    Entry.insert_or_assign("GearRatios", std::move(Gears));
                    toml::array Curve;
                    for (const auto& S : V->TorqueCurve)
                    {
                        toml::array Pt;
                        Pt.push_back(static_cast<double>(S.Rpm));
                        Pt.push_back(static_cast<double>(S.TorqueNm));
                        Curve.push_back(std::move(Pt));
                    }
                    Entry.insert_or_assign("TorqueCurve", std::move(Curve));
                    toml::array Wheels;
                    for (const ComponentClass::ComponentInstanceToken WT : V->BoundWheelTokens)
                    {
                        if (const auto* WH = Storage.InspectHeader(WT))
                        {
                            Wheels.push_back(WH->Title);
                        }
                    }
                    Entry.insert_or_assign("BoundWheelTitles", std::move(Wheels));
                }
                break;
            case ComponentClass::ComponentCategory::RotationalMotion:
                if (const auto* R = Storage.InspectRotation(H.Token))
                {
                    Entry.insert_or_assign("RateDegreesPerSecond", static_cast<double>(R->RateDegreesPerSecond));
                    if (const auto* TH = Storage.InspectHeader(R->TargetInstance))
                    {
                        Entry.insert_or_assign("TargetTitle", TH->Title);
                    }
                }
                break;
            case ComponentClass::ComponentCategory::RigidBodyInstance:
                if (const auto* RB = Storage.InspectRigidBody(H.Token))
                {
                    Entry.insert_or_assign("MotionKind", static_cast<int64_t>(RB->MotionKind));
                    Entry.insert_or_assign("ExplicitMassKg", static_cast<double>(RB->ExplicitMassKg));
                    Entry.insert_or_assign("DensityKgPerM3", static_cast<double>(RB->DensityKgPerM3));
                    Entry.insert_or_assign("CenterOfMassOffsetM", EncodeVec3(RB->CenterOfMassOffsetM));
                    Entry.insert_or_assign("LinearDamping", static_cast<double>(RB->LinearDamping));
                    Entry.insert_or_assign("AngularDamping", static_cast<double>(RB->AngularDamping));
                    Entry.insert_or_assign("GravityFactor", static_cast<double>(RB->GravityFactor));
                    Entry.insert_or_assign("CollisionChannel", static_cast<int64_t>(RB->CollisionChannel));
                    Entry.insert_or_assign("CollideWithMask", static_cast<int64_t>(RB->CollideWithMask));
                    Entry.insert_or_assign("PhysicsMaterialId", static_cast<int64_t>(RB->PhysicsMaterialId));
                    Entry.insert_or_assign("FreezeAxisMask", static_cast<int64_t>(RB->FreezeAxisMask));
                    Entry.insert_or_assign("AllowSleep", RB->AllowSleep);
                    Entry.insert_or_assign("ContinuousMotion", RB->ContinuousMotion);
                }
                break;
            case ComponentClass::ComponentCategory::SoftBodyInstance:
                if (const auto* SB = Storage.InspectSoftBody(H.Token))
                {
                    Entry.insert_or_assign("SoftBodyAssetPath", SB->SoftBodyAssetPath);
                    Entry.insert_or_assign("VertexAtlasPath", SB->VertexAtlasPath);
                    Entry.insert_or_assign("SleepDistanceMeters", static_cast<double>(SB->SleepDistanceMeters));
                }
                break;
            case ComponentClass::ComponentCategory::ForceFieldInstance:
                if (const auto* FF = Storage.InspectForceField(H.Token))
                {
                    Entry.insert_or_assign("ModelCode", static_cast<int64_t>(FF->ModelCode));
                    Entry.insert_or_assign("Direction", EncodeVec3(FF->Direction));
                    Entry.insert_or_assign("Strength", static_cast<double>(FF->Strength));
                    Entry.insert_or_assign("RadiusMeters", static_cast<double>(FF->RadiusMeters));
                    Entry.insert_or_assign("FalloffCode", static_cast<int64_t>(FF->FalloffCode));
                }
                break;
            case ComponentClass::ComponentCategory::FluidRegionInstance:
                if (const auto* FL = Storage.InspectFluidRegion(H.Token))
                {
                    Entry.insert_or_assign("RadiusMeters", static_cast<double>(FL->RadiusMeters));
                    Entry.insert_or_assign("VelocityMps", EncodeVec3(FL->VelocityMps));
                    Entry.insert_or_assign("Density", static_cast<double>(FL->Density));
                    Entry.insert_or_assign("Temperature", static_cast<double>(FL->Temperature));
                    Entry.insert_or_assign("Fuel", static_cast<double>(FL->Fuel));
                }
                break;
            case ComponentClass::ComponentCategory::ParticleCloudInstance:
                if (const auto* PC = Storage.InspectParticleCloud(H.Token))
                {
                    Entry.insert_or_assign("RadiusMeters", static_cast<double>(PC->RadiusMeters));
                    Entry.insert_or_assign("ParticleCount", static_cast<int64_t>(PC->ParticleCount));
                    Entry.insert_or_assign("ReleaseRate", static_cast<double>(PC->ReleaseRate));
                    Entry.insert_or_assign("SpeedMps", static_cast<double>(PC->SpeedMps));
                    Entry.insert_or_assign("LifetimeSec", static_cast<double>(PC->LifetimeSec));
                    Entry.insert_or_assign("GravityY", static_cast<double>(PC->GravityY));
                    Entry.insert_or_assign("GasInfluence", static_cast<double>(PC->GasInfluence));
                    Entry.insert_or_assign("FieldInfluence", static_cast<double>(PC->FieldInfluence));
                }
                break;
            case ComponentClass::ComponentCategory::SpatialOrigin:
            case ComponentClass::ComponentCategory::TransformPivot:
                break;
        }
        InstancesArray.push_back(std::move(Entry));
    };

    Storage.WalkSpatialNesting(AppendEntry);
    std::vector<ComponentClass::ComponentInstanceToken> Behaviours {};
    Storage.CollectBehaviourTokens(Behaviours);
    for (const auto Tok : Behaviours)
    {
        if (const auto* H = Storage.InspectHeader(Tok))
        {
            AppendEntry(*H);
        }
    }

    toml::table HierarchyTbl;
    HierarchyTbl.insert_or_assign("Instances", std::move(InstancesArray));
    Payload.DomainManifest.insert_or_assign(".ComponentHierarchy", std::move(HierarchyTbl));

    if (Definition.BakedIconWidth > 0u && Definition.BakedIconHeight > 0u && !Definition.BakedIconRgba8.empty())
    {
        BinarySectionBlock IconBlock {};
        IconBlock.SectionIdentifier = "BakedClassIcon";
        IconBlock.SubFormatTag      = ".ComponentHierarchy";
        WriteUint32LE(IconBlock.PayloadBytes, Definition.BakedIconWidth);
        WriteUint32LE(IconBlock.PayloadBytes, Definition.BakedIconHeight);
        const std::size_t Prev = IconBlock.PayloadBytes.size();
        IconBlock.PayloadBytes.resize(Prev + Definition.BakedIconRgba8.size());
        std::memcpy(IconBlock.PayloadBytes.data() + Prev, Definition.BakedIconRgba8.data(), Definition.BakedIconRgba8.size());
        Payload.BinarySections.push_back(std::move(IconBlock));
    }

    if (OptionalRevision != nullptr)
    {
        RevisionEncoder::AttachRevisionSections(*OptionalRevision, Payload);
    }

    return Payload;
}

bool ComponentSpaceEncoder::EncodeToBytes(
    const ComponentClass::ComponentClassDefinition& Definition,
    std::vector<std::byte>&                         OutContainerBytes,
    std::string&                                    OutDiagnostic,
    const Workspaces::RevisionTracker*              OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToBytes(
        ConstructPayload(Definition, OptionalRevision),
        OutContainerBytes,
        OutDiagnostic);
}

bool ComponentSpaceEncoder::EncodeToFile(
    const std::filesystem::path&                    TargetPath,
    const ComponentClass::ComponentClassDefinition& Definition,
    std::string&                                    OutDiagnostic,
    const Workspaces::RevisionTracker*              OptionalRevision)
{
    return SpaceContainerEncoder::EncodeToFile(
        TargetPath,
        ConstructPayload(Definition, OptionalRevision),
        OutDiagnostic);
}

bool ComponentSpaceDecoder::ExtractDefinition(
    const SpaceContainerPayload&              Payload,
    ComponentClass::ComponentClassDefinition& OutDefinition,
    std::string&                              OutDiagnostic,
    Workspaces::RevisionTracker*              OptionalOutRevision)
{
    if (Payload.SuffixCategory != SpaceSuffixCategory::ComponentSpace)
    {
        OutDiagnostic = "Expected .ComponentSpace container category.";
        return false;
    }

    OutDefinition = ComponentClass::ComponentClassDefinition{};
    OutDefinition.Title = Payload.DocumentTitle;

    if (const toml::table* Header = Payload.DomainManifest[".DocumentHeader"].as_table())
    {
        OutDefinition.Title            = (*Header)["Title"].value_or(Payload.DocumentTitle);
        OutDefinition.ClassArchetype        = static_cast<ComponentClass::ComponentClassArchetype>((*Header)["ClassRole"].value_or<int64_t>(0));
        OutDefinition.RevisionNumber   = static_cast<uint64_t>((*Header)["RevisionNumber"].value_or<int64_t>(1));
        OutDefinition.CompiledRevision = static_cast<uint64_t>((*Header)["CompiledRevision"].value_or<int64_t>(0));
        OutDefinition.CompiledDirty    = (*Header)["CompiledDirty"].value_or(false);
    }

    const toml::table* HierarchyTbl = Payload.DomainManifest[".ComponentHierarchy"].as_table();
    const toml::array* InstancesArr = (HierarchyTbl != nullptr) ? (*HierarchyTbl)["Instances"].as_array() : nullptr;
    if (InstancesArr == nullptr || InstancesArr->empty())
    {
        OutDiagnostic = ".ComponentSpace container missing .ComponentHierarchy instances.";
        return false;
    }

    std::unordered_map<std::string, ComponentClass::ComponentInstanceToken> TitleToToken {};
    std::vector<std::pair<ComponentClass::ComponentInstanceToken, std::vector<std::string>>> PendingWheels {};
    std::vector<std::pair<ComponentClass::ComponentInstanceToken, std::string>>              PendingRotationTargets {};

    for (std::size_t Idx = 0u; Idx < InstancesArr->size(); ++Idx)
    {
        const toml::table* Entry = (*InstancesArr)[Idx].as_table();
        if (Entry == nullptr)
        {
            continue;
        }

        const std::string Title = (*Entry)["Title"].value_or(std::string("Component"));
        const auto Category = static_cast<ComponentClass::ComponentCategory>((*Entry)["Category"].value_or<int64_t>(1));
        const std::string EnclosingTitle = (*Entry)["EnclosingTitle"].value_or(std::string{});

        ComponentClass::ComponentTransform Transform {};
        Transform.LocationMeters  = DecodeVec3((*Entry)["Location"].as_array());
        Transform.RotationDegrees = DecodeVec3((*Entry)["Rotation"].as_array());
        Transform.ScaleFactors    = DecodeScalar3((*Entry)["Scale"].as_array(), { 1.0f, 1.0f, 1.0f });

        ComponentClass::ComponentInstanceToken Token {};
        if (Category == ComponentClass::ComponentCategory::SpatialOrigin)
        {
            OutDefinition.Storage.ResetWithSpatialOrigin(Title);
            Token = OutDefinition.Storage.QueryOriginToken();
        }
        else
        {
            const auto EnclosingIt = TitleToToken.find(EnclosingTitle);
            const ComponentClass::ComponentInstanceToken EnclosingTok =
                (EnclosingIt != TitleToToken.end()) ? EnclosingIt->second : OutDefinition.Storage.QueryOriginToken();
            Token = OutDefinition.Storage.AppendInstance(Category, Title, EnclosingTok, Transform);
        }

        if (ComponentClass::ComponentInstanceHeader* H = OutDefinition.Storage.ResolveHeader(Token))
        {
            H->VisibilityEnabled = (*Entry)["Visible"].value_or(true);
            H->HiddenInPlay      = (*Entry)["HiddenInPlay"].value_or(false);
            H->TagCsv            = (*Entry)["TagCsv"].value_or(std::string{});
        }
        TitleToToken.emplace(Title, Token);

        switch (Category)
        {
            case ComponentClass::ComponentCategory::GeometryInstance:
                if (auto* G = OutDefinition.Storage.ResolveGeometry(Token))
                {
                    G->AssetIdentifier  = (*Entry)["AssetIdentifier"].value_or(std::string{});
                    G->ForcedLodLevel   = static_cast<int32_t>((*Entry)["ForcedLodLevel"].value_or<int64_t>(-1));
                    G->CastShadow       = (*Entry)["CastShadow"].value_or(true);
                    G->CollisionProfile = static_cast<ComponentClass::GeometryCollisionProfile>((*Entry)["CollisionProfile"].value_or<int64_t>(0));
                }
                break;
            case ComponentClass::ComponentCategory::ConstraintInstance:
                if (auto* C = OutDefinition.Storage.ResolveConstraint(Token))
                {
                    C->Form                = static_cast<ComponentClass::PhysicsConstraintCategory>((*Entry)["ConstraintKind"].value_or<int64_t>(0));
                    C->Axis                = static_cast<ComponentClass::ConstraintAxis>((*Entry)["Axis"].value_or<int64_t>(1));
                    C->DriveMode           = static_cast<ComponentClass::ConstraintDriveMode>((*Entry)["DriveMode"].value_or<int64_t>(0));
                    C->MinAngleDegrees     = static_cast<float>((*Entry)["MinAngleDegrees"].value_or(-32.0));
                    C->MaxAngleDegrees     = static_cast<float>((*Entry)["MaxAngleDegrees"].value_or(32.0));
                    C->MinTravelMeters     = static_cast<float>((*Entry)["MinTravelMeters"].value_or(-0.09));
                    C->MaxTravelMeters     = static_cast<float>((*Entry)["MaxTravelMeters"].value_or(0.09));
                    C->ConeAngleDegrees    = static_cast<float>((*Entry)["ConeAngleDegrees"].value_or(35.0));
                    C->TwistLimitDegrees   = static_cast<float>((*Entry)["TwistLimitDegrees"].value_or(45.0));
                    C->NaturalFrequencyHz  = static_cast<float>((*Entry)["NaturalFrequencyHz"].value_or(2.0));
                    C->DampingRatioZeta    = static_cast<float>((*Entry)["DampingRatioZeta"].value_or(0.42));
                    C->TravelEachWayMeters = static_cast<float>((*Entry)["TravelEachWayMeters"].value_or(0.09));
                    C->TargetRpm           = static_cast<float>((*Entry)["TargetRpm"].value_or(600.0));
                    C->MaxMotorTorqueNm    = static_cast<float>((*Entry)["MaxMotorTorqueNm"].value_or(400.0));
                    C->RopeLengthMeters    = static_cast<float>((*Entry)["RopeLengthMeters"].value_or(0.32));
                    C->BreakForceNewtons   = static_cast<float>((*Entry)["BreakForceNewtons"].value_or(3500.0));
                    C->BreakTorqueNm       = static_cast<float>((*Entry)["BreakTorqueNm"].value_or(0.0));
                    C->SnapRadiusMeters    = static_cast<float>((*Entry)["SnapRadiusMeters"].value_or(0.35));
                    C->NormalizedEditPose  = static_cast<float>((*Entry)["NormalizedEditPose"].value_or(0.5));
                    C->DockKey             = (*Entry)["DockKey"].value_or(std::string{});
                    C->DockConnected       = (*Entry)["DockConnected"].value_or(true);
                    C->DisableSelfCollide  = (*Entry)["DisableSelfCollide"].value_or(true);
                }
                break;
            case ComponentClass::ComponentCategory::LightInstance:
                if (auto* L = OutDefinition.Storage.ResolveLight(Token))
                {
                    L->Form                   = static_cast<ComponentClass::LightSourceCategory>((*Entry)["LightKind"].value_or<int64_t>(1));
                    L->LinearColourRGB        = DecodeScalar3((*Entry)["LinearColourRGB"].as_array(), { 1.0f, 1.0f, 1.0f });
                    L->IntensityCandela       = static_cast<float>((*Entry)["IntensityCandela"].value_or(30.0));
                    L->AttenuationRangeMeters = static_cast<float>((*Entry)["AttenuationRangeMeters"].value_or(30.0));
                    L->ConeAngleDegrees       = static_cast<float>((*Entry)["ConeAngleDegrees"].value_or(28.0));
                    L->PenumbraSoftness       = static_cast<float>((*Entry)["PenumbraSoftness"].value_or(0.35));
                    L->CastShadow             = (*Entry)["CastShadow"].value_or(false);
                    L->IesProfileIdentifier   = (*Entry)["IesProfileIdentifier"].value_or(std::string{});
                }
                break;
            case ComponentClass::ComponentCategory::CameraInstance:
                if (auto* Cam = OutDefinition.Storage.ResolveCamera(Token))
                {
                    Cam->VerticalFovDegrees = static_cast<float>((*Entry)["VerticalFovDegrees"].value_or(52.0));
                    Cam->NearClipMeters     = static_cast<float>((*Entry)["NearClipMeters"].value_or(0.1));
                    Cam->FarClipMeters      = static_cast<float>((*Entry)["FarClipMeters"].value_or(5000.0));
                    Cam->AspectRatio        = static_cast<float>((*Entry)["AspectRatio"].value_or(16.0 / 9.0));
                }
                break;
            case ComponentClass::ComponentCategory::CollisionVolume:
                if (auto* Col = OutDefinition.Storage.ResolveCollision(Token))
                {
                    Col->Form                     = static_cast<ComponentClass::CollisionVolumeCategory>((*Entry)["CollisionKind"].value_or<int64_t>(0));
                    Col->HalfExtentsMeters        = DecodeScalar3((*Entry)["HalfExtentsMeters"].as_array(), { 0.5f, 0.5f, 0.5f });
                    Col->RadiusMeters             = static_cast<float>((*Entry)["RadiusMeters"].value_or(0.35));
                    Col->HalfHeightMeters         = static_cast<float>((*Entry)["HalfHeightMeters"].value_or(0.45));
                    Col->ConvexMarginMeters       = static_cast<float>((*Entry)["ConvexMarginMeters"].value_or(0.015));
                    Col->CollisionChannel         = static_cast<uint16_t>((*Entry)["CollisionChannel"].value_or<int64_t>(1));
                    Col->CollideWithMask          = static_cast<uint32_t>((*Entry)["CollideWithMask"].value_or<int64_t>(0xFFFFFFFFll));
                    Col->PhysicsMaterialId        = static_cast<uint16_t>((*Entry)["PhysicsMaterialId"].value_or<int64_t>(0));
                    Col->MaxCompoundHulls         = static_cast<uint32_t>((*Entry)["MaxCompoundHulls"].value_or<int64_t>(4));
                    Col->TriggerOverlapOnly       = (*Entry)["TriggerOverlapOnly"].value_or(false);
                    Col->SourceGeometryIdentifier = (*Entry)["SourceGeometryIdentifier"].value_or(std::string());
                }
                break;
            case ComponentClass::ComponentCategory::DirectionArrow:
                if (auto* A = OutDefinition.Storage.ResolveArrow(Token))
                {
                    A->LengthMeters    = static_cast<float>((*Entry)["LengthMeters"].value_or(0.8));
                    A->LinearColourRGB = DecodeScalar3((*Entry)["LinearColourRGB"].as_array(), { 0.98f, 0.82f, 0.24f });
                }
                break;
            case ComponentClass::ComponentCategory::VehicleDynamics:
                if (auto* V = OutDefinition.Storage.ResolveVehicle(Token))
                {
                    V->ChassisMassKg     = static_cast<float>((*Entry)["ChassisMassKg"].value_or(1300.0));
                    V->WheelRadiusMeters = static_cast<float>((*Entry)["WheelRadiusMeters"].value_or(0.50855));
                    V->IdleRpm           = static_cast<float>((*Entry)["IdleRpm"].value_or(700.0));
                    V->RedlineRpm        = static_cast<float>((*Entry)["RedlineRpm"].value_or(7200.0));
                    V->DragCoefficient   = static_cast<float>((*Entry)["DragCoefficient"].value_or(0.34));
                    V->FrontalAreaM2     = static_cast<float>((*Entry)["FrontalAreaM2"].value_or(2.15));
                    V->AeroLiftAreaM2    = static_cast<float>((*Entry)["AeroLiftAreaM2"].value_or(1.1));
                    V->FinalDriveRatio   = static_cast<float>((*Entry)["FinalDriveRatio"].value_or(3.55));
                    V->Interpolation     = static_cast<ComponentClass::CurveInterpolationForm>((*Entry)["Interpolation"].value_or<int64_t>(1));
                    if (const toml::array* Gears = (*Entry)["GearRatios"].as_array())
                    {
                        for (std::size_t GIdx = 0u; GIdx < std::min<std::size_t>(Gears->size(), V->GearRatios.size()); ++GIdx)
                        {
                            V->GearRatios[GIdx] = static_cast<float>((*Gears)[GIdx].value_or(1.0));
                        }
                    }
                    if (const toml::array* Curve = (*Entry)["TorqueCurve"].as_array())
                    {
                        V->TorqueCurve.clear();
                        for (std::size_t SIdx = 0u; SIdx < Curve->size(); ++SIdx)
                        {
                            if (const toml::array* Pt = (*Curve)[SIdx].as_array(); Pt != nullptr && Pt->size() >= 2u)
                            {
                                V->TorqueCurve.push_back({
                                    static_cast<float>((*Pt)[0].value_or(800.0)),
                                    static_cast<float>((*Pt)[1].value_or(200.0)),
                                });
                            }
                        }
                    }
                    if (const toml::array* Wheels = (*Entry)["BoundWheelTitles"].as_array())
                    {
                        std::vector<std::string> Names {};
                        for (std::size_t WIdx = 0u; WIdx < Wheels->size(); ++WIdx)
                        {
                            if (const auto NameOpt = (*Wheels)[WIdx].value<std::string>())
                            {
                                Names.push_back(*NameOpt);
                            }
                        }
                        PendingWheels.emplace_back(Token, std::move(Names));
                    }
                }
                break;
            case ComponentClass::ComponentCategory::RotationalMotion:
                if (auto* R = OutDefinition.Storage.ResolveRotation(Token))
                {
                    R->RateDegreesPerSecond = static_cast<float>((*Entry)["RateDegreesPerSecond"].value_or(45.0));
                    if (const auto TargetTitle = (*Entry)["TargetTitle"].value<std::string>())
                    {
                        PendingRotationTargets.emplace_back(Token, *TargetTitle);
                    }
                }
                break;
            case ComponentClass::ComponentCategory::RigidBodyInstance:
                if (auto* RB = OutDefinition.Storage.ResolveRigidBody(Token))
                {
                    RB->MotionKind          = static_cast<ComponentClass::RigidBodyMotionKind>((*Entry)["MotionKind"].value_or<int64_t>(2));
                    RB->ExplicitMassKg      = static_cast<float>((*Entry)["ExplicitMassKg"].value_or(0.0));
                    RB->DensityKgPerM3      = static_cast<float>((*Entry)["DensityKgPerM3"].value_or(2350.0));
                    RB->CenterOfMassOffsetM = DecodeVec3((*Entry)["CenterOfMassOffsetM"].as_array(), { 0.0f, 0.0f, 0.0f });
                    RB->LinearDamping       = static_cast<float>((*Entry)["LinearDamping"].value_or(0.02));
                    RB->AngularDamping      = static_cast<float>((*Entry)["AngularDamping"].value_or(0.05));
                    RB->GravityFactor       = static_cast<float>((*Entry)["GravityFactor"].value_or(1.0));
                    RB->CollisionChannel    = static_cast<uint16_t>((*Entry)["CollisionChannel"].value_or<int64_t>(1));
                    RB->CollideWithMask     = static_cast<uint32_t>((*Entry)["CollideWithMask"].value_or<int64_t>(0xFFFFFFFFll));
                    RB->PhysicsMaterialId   = static_cast<uint16_t>((*Entry)["PhysicsMaterialId"].value_or<int64_t>(0));
                    RB->FreezeAxisMask      = static_cast<uint8_t>((*Entry)["FreezeAxisMask"].value_or<int64_t>(0));
                    RB->AllowSleep          = (*Entry)["AllowSleep"].value_or(true);
                    RB->ContinuousMotion    = (*Entry)["ContinuousMotion"].value_or(false);
                }
                break;
            case ComponentClass::ComponentCategory::SoftBodyInstance:
                if (auto* SB = OutDefinition.Storage.ResolveSoftBody(Token))
                {
                    SB->SoftBodyAssetPath   = (*Entry)["SoftBodyAssetPath"].value_or(std::string("EngineContent/Simulation/Deformables/WovenCloth.SoftBody"));
                    SB->VertexAtlasPath     = (*Entry)["VertexAtlasPath"].value_or(std::string());
                    SB->SleepDistanceMeters = static_cast<float>((*Entry)["SleepDistanceMeters"].value_or(60.0));
                }
                break;
            case ComponentClass::ComponentCategory::ForceFieldInstance:
                if (auto* FF = OutDefinition.Storage.ResolveForceField(Token))
                {
                    FF->ModelCode    = static_cast<uint8_t>((*Entry)["ModelCode"].value_or<int64_t>(0));
                    FF->Direction    = DecodeVec3((*Entry)["Direction"].as_array(), { 1.0f, 0.0f, 0.0f });
                    FF->Strength     = static_cast<float>((*Entry)["Strength"].value_or(4.0));
                    FF->RadiusMeters = static_cast<float>((*Entry)["RadiusMeters"].value_or(8.0));
                    FF->FalloffCode  = static_cast<uint8_t>((*Entry)["FalloffCode"].value_or<int64_t>(3));
                }
                break;
            case ComponentClass::ComponentCategory::FluidRegionInstance:
                if (auto* FL = OutDefinition.Storage.ResolveFluidRegion(Token))
                {
                    FL->RadiusMeters = static_cast<float>((*Entry)["RadiusMeters"].value_or(0.42));
                    FL->VelocityMps  = DecodeVec3((*Entry)["VelocityMps"].as_array(), { 0.0f, 2.2f, 0.0f });
                    FL->Density      = static_cast<float>((*Entry)["Density"].value_or(1.5));
                    FL->Temperature  = static_cast<float>((*Entry)["Temperature"].value_or(2.6));
                    FL->Fuel         = static_cast<float>((*Entry)["Fuel"].value_or(1.1));
                }
                break;
            case ComponentClass::ComponentCategory::ParticleCloudInstance:
                if (auto* PC = OutDefinition.Storage.ResolveParticleCloud(Token))
                {
                    PC->RadiusMeters   = static_cast<float>((*Entry)["RadiusMeters"].value_or(0.36));
                    PC->ParticleCount  = static_cast<uint32_t>((*Entry)["ParticleCount"].value_or<int64_t>(256));
                    PC->ReleaseRate    = static_cast<float>((*Entry)["ReleaseRate"].value_or(12.0));
                    PC->SpeedMps       = static_cast<float>((*Entry)["SpeedMps"].value_or(1.5));
                    PC->LifetimeSec    = static_cast<float>((*Entry)["LifetimeSec"].value_or(2.4));
                    PC->GravityY       = static_cast<float>((*Entry)["GravityY"].value_or(0.15));
                    PC->GasInfluence   = static_cast<float>((*Entry)["GasInfluence"].value_or(0.85));
                    PC->FieldInfluence = static_cast<float>((*Entry)["FieldInfluence"].value_or(0.85));
                }
                break;
            case ComponentClass::ComponentCategory::SpatialOrigin:
            case ComponentClass::ComponentCategory::TransformPivot:
                break;
        }
    }

    for (const auto& [VToken, WheelNames] : PendingWheels)
    {
        if (auto* V = OutDefinition.Storage.ResolveVehicle(VToken))
        {
            for (const std::string& WName : WheelNames)
            {
                if (const auto It = TitleToToken.find(WName); It != TitleToToken.end())
                {
                    V->BoundWheelTokens.push_back(It->second);
                }
            }
        }
    }

    for (const auto& [RToken, TargetTitle] : PendingRotationTargets)
    {
        if (auto* R = OutDefinition.Storage.ResolveRotation(RToken))
        {
            if (const auto It = TitleToToken.find(TargetTitle); It != TitleToToken.end())
            {
                R->TargetInstance = It->second;
            }
        }
    }

    for (const BinarySectionBlock& Block : Payload.BinarySections)
    {
        if (Block.SectionIdentifier == "BakedClassIcon" && Block.PayloadBytes.size() >= 8u)
        {
            const uint32_t W = ReadUint32LE(Block.PayloadBytes.data());
            const uint32_t H = ReadUint32LE(Block.PayloadBytes.data() + 4u);
            const std::size_t Expected = 8u + static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u;
            if (W > 0u && H > 0u && Block.PayloadBytes.size() == Expected)
            {
                OutDefinition.BakedIconWidth  = W;
                OutDefinition.BakedIconHeight = H;
                OutDefinition.BakedIconRgba8.resize(Expected - 8u);
                std::memcpy(OutDefinition.BakedIconRgba8.data(), Block.PayloadBytes.data() + 8u, Expected - 8u);
            }
        }
    }

    if (OptionalOutRevision != nullptr)
    {
        return RevisionDecoder::ExtractRevisionSections(Payload, *OptionalOutRevision, OutDiagnostic);
    }
    return true;
}

bool ComponentSpaceDecoder::DecodeFromBytes(
    std::span<const std::byte>                RawContainerBytes,
    ComponentClass::ComponentClassDefinition& OutDefinition,
    std::string&                              OutDiagnostic,
    Workspaces::RevisionTracker*              OptionalOutRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromBytes(RawContainerBytes, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDefinition(Payload, OutDefinition, OutDiagnostic, OptionalOutRevision);
}

bool ComponentSpaceDecoder::DecodeFromFile(
    const std::filesystem::path&              SourcePath,
    ComponentClass::ComponentClassDefinition& OutDefinition,
    std::string&                              OutDiagnostic,
    Workspaces::RevisionTracker*              OptionalOutRevision)
{
    SpaceContainerPayload Payload {};
    if (!SpaceContainerDecoder::DecodeFromFile(SourcePath, Payload, OutDiagnostic))
    {
        return false;
    }
    return ExtractDefinition(Payload, OutDefinition, OutDiagnostic, OptionalOutRevision);
}

} // namespace Frontier::Codecs
