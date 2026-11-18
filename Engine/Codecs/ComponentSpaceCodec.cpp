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
    HeaderTbl.insert_or_assign("ClassRole", static_cast<int64_t>(Definition.ClassRole));
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
        if (H.EnclosingIndex != ComponentClass::InvalidHierarchyIndex)
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
                    Entry.insert_or_assign("ConstraintKind", static_cast<int64_t>(C->Kind));
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
            case ComponentClass::ComponentCategory::ParticleInstance:
                if (const auto* P = Storage.InspectParticle(H.Token))
                {
                    Entry.insert_or_assign("PresetIdentifier", P->PresetIdentifier);
                    Entry.insert_or_assign("EmissionRateScale", static_cast<double>(P->EmissionRateScale));
                    Entry.insert_or_assign("VelocityScale", static_cast<double>(P->VelocityScale));
                    Entry.insert_or_assign("AutoActivate", P->AutoActivate);
                    Entry.insert_or_assign("LocalSpaceSimulate", P->LocalSpaceSimulate);
                }
                break;
            case ComponentClass::ComponentCategory::FluidInstance:
                if (const auto* F = Storage.InspectFluid(H.Token))
                {
                    Entry.insert_or_assign("VolumeIdentifier", F->VolumeIdentifier);
                    Entry.insert_or_assign("EmissionMode", static_cast<int64_t>(F->EmissionMode));
                    Entry.insert_or_assign("BoundsExtentMeters", EncodeScalar3(F->BoundsExtentMeters));
                    Entry.insert_or_assign("BuoyancyScale", static_cast<double>(F->BuoyancyScale));
                    Entry.insert_or_assign("FuelInjectionRate", static_cast<double>(F->FuelInjectionRate));
                    Entry.insert_or_assign("AutoActivate", F->AutoActivate);
                }
                break;
            case ComponentClass::ComponentCategory::LightInstance:
                if (const auto* L = Storage.InspectLight(H.Token))
                {
                    Entry.insert_or_assign("LightKind", static_cast<int64_t>(L->Kind));
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
                    Entry.insert_or_assign("CollisionKind", static_cast<int64_t>(Col->Kind));
                    Entry.insert_or_assign("HalfExtentsMeters", EncodeScalar3(Col->HalfExtentsMeters));
                    Entry.insert_or_assign("RadiusMeters", static_cast<double>(Col->RadiusMeters));
                    Entry.insert_or_assign("HalfHeightMeters", static_cast<double>(Col->HalfHeightMeters));
                    Entry.insert_or_assign("TriggerOverlapOnly", Col->TriggerOverlapOnly);
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
            case ComponentClass::ComponentCategory::SpatialOrigin:
            case ComponentClass::ComponentCategory::TransformPivot:
                break;
        }
        InstancesArray.push_back(std::move(Entry));
    };

    Storage.WalkSpatialHierarchy(AppendEntry);
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
        OutDefinition.ClassRole        = static_cast<ComponentClass::ComponentClassRole>((*Header)["ClassRole"].value_or<int64_t>(0));
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
                    C->Kind                = static_cast<ComponentClass::PhysicsConstraintKind>((*Entry)["ConstraintKind"].value_or<int64_t>(0));
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
            case ComponentClass::ComponentCategory::ParticleInstance:
                if (auto* P = OutDefinition.Storage.ResolveParticle(Token))
                {
                    P->PresetIdentifier   = (*Entry)["PresetIdentifier"].value_or(std::string("ExhaustPlume"));
                    P->EmissionRateScale  = static_cast<float>((*Entry)["EmissionRateScale"].value_or(1.0));
                    P->VelocityScale      = static_cast<float>((*Entry)["VelocityScale"].value_or(1.0));
                    P->AutoActivate       = (*Entry)["AutoActivate"].value_or(true);
                    P->LocalSpaceSimulate = (*Entry)["LocalSpaceSimulate"].value_or(false);
                }
                break;
            case ComponentClass::ComponentCategory::FluidInstance:
                if (auto* F = OutDefinition.Storage.ResolveFluid(Token))
                {
                    F->VolumeIdentifier   = (*Entry)["VolumeIdentifier"].value_or(std::string("TyreSmokePlume"));
                    F->EmissionMode       = static_cast<ComponentClass::FluidEmissionKind>((*Entry)["EmissionMode"].value_or<int64_t>(0));
                    F->BoundsExtentMeters = DecodeScalar3((*Entry)["BoundsExtentMeters"].as_array(), { 0.8f, 0.8f, 1.2f });
                    F->BuoyancyScale      = static_cast<float>((*Entry)["BuoyancyScale"].value_or(1.2));
                    F->FuelInjectionRate  = static_cast<float>((*Entry)["FuelInjectionRate"].value_or(0.45));
                    F->AutoActivate       = (*Entry)["AutoActivate"].value_or(true);
                }
                break;
            case ComponentClass::ComponentCategory::LightInstance:
                if (auto* L = OutDefinition.Storage.ResolveLight(Token))
                {
                    L->Kind                   = static_cast<ComponentClass::LightEmitterKind>((*Entry)["LightKind"].value_or<int64_t>(1));
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
                    Cam->FarClipMeters      = static_cast<float>((*Entry)["FarClipMeters"].value_or(120.0));
                    Cam->AspectRatio        = static_cast<float>((*Entry)["AspectRatio"].value_or(16.0 / 9.0));
                }
                break;
            case ComponentClass::ComponentCategory::CollisionVolume:
                if (auto* Col = OutDefinition.Storage.ResolveCollision(Token))
                {
                    Col->Kind               = static_cast<ComponentClass::CollisionVolumeKind>((*Entry)["CollisionKind"].value_or<int64_t>(0));
                    Col->HalfExtentsMeters  = DecodeScalar3((*Entry)["HalfExtentsMeters"].as_array(), { 0.5f, 0.5f, 0.5f });
                    Col->RadiusMeters       = static_cast<float>((*Entry)["RadiusMeters"].value_or(0.35));
                    Col->HalfHeightMeters   = static_cast<float>((*Entry)["HalfHeightMeters"].value_or(0.45));
                    Col->TriggerOverlapOnly = (*Entry)["TriggerOverlapOnly"].value_or(false);
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
                    V->Interpolation     = static_cast<ComponentClass::CurveInterpolationKind>((*Entry)["Interpolation"].value_or<int64_t>(1));
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
