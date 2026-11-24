/*============================================================================================================================================
                                                     WORLDINSTANCEEDITORBRIDGE.CPP
============================================================================================================================================*/
// 🧩 Implementation of Editor Outliner binding, .Instance spawning & WorldScene SoA synchronization

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/WorldInstanceEditorBridge.h"

#if FRONTIER_PROFILE_EDITOR

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Frontier::Editor
{

namespace
{

[[nodiscard]] bool DifferAboveEpsilon(float ValueA, float ValueB, float Epsilon = 1.0e-4f) noexcept
{
    return std::fabs(ValueA - ValueB) > Epsilon;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 WORLD INSTANCE EDITOR BRIDGE
//------------------------------------------------------------------------------------------------------------------------

WorldInstanceEditorBridge::~WorldInstanceEditorBridge() noexcept
{
    Unbind();
}

ConstructSlotCategory WorldInstanceEditorBridge::ResolveOutlinerCategory(World::InstanceCategory Category) noexcept
{
    switch (Category)
    {
        case World::InstanceCategory::ContainmentFolder: return ConstructSlotCategory::Folder;
        case World::InstanceCategory::Luminaire:         return ConstructSlotCategory::PointLuminaire;
        case World::InstanceCategory::CameraOptics:      return ConstructSlotCategory::OpticsCamera;
        case World::InstanceCategory::SolarBody:
        case World::InstanceCategory::LunarBody:
        case World::InstanceCategory::AtmosphereMedium:
        case World::InstanceCategory::CloudDeck:
        case World::InstanceCategory::StellarField:      return ConstructSlotCategory::LayerGroup;
        default:                                         return ConstructSlotCategory::GeometryVolume;
    }
}

void WorldInstanceEditorBridge::BindOutliner(
    World::WorldInstanceRegister& TargetRegister,
    OutlinerPanel&                TargetOutliner)
{
    Unbind();

    BoundRegister = &TargetRegister;
    BoundOutliner = &TargetOutliner;

    World::EditorSurfaceAdapter Adapter{};
    Adapter.RegisterEditorEntry = [this](const World::EditorInstanceRegistrationDescriptor& Descriptor)
    {
        if (BoundOutliner == nullptr || !Descriptor.Token.QueryValid())
        {
            return;
        }

        OutlinerRecordEntry Record{};
        Record.EnclosingToken     = QueryOutlinerSlot(Descriptor.EnclosingToken);
        Record.Category           = ResolveOutlinerCategory(Descriptor.Token.QueryCategory());
        Record.Title              = std::string(Descriptor.DisplayTitle);
        Record.CategoryLabel      = std::string(Descriptor.CategoryLabel);
        Record.Subtitle           = std::string(Descriptor.Subtitle);
        Record.VisibilityEnabled  = Descriptor.VisibilityEnabled;
        Record.RemovalProtected   = Descriptor.RemovalProtected;
        Record.DuplicateProtected = Descriptor.DuplicateProtected;
        Record.DomainTag          = std::string(Descriptor.CategoryTag);

        const OutlinerSlotToken IssuedSlot = BoundOutliner->ConstructDirectEntry(std::move(Record));
        if (IssuedSlot.Valid())
        {
            InstanceToOutlinerMap[Descriptor.Token.SlotIndex] = IssuedSlot;
            OutlinerToInstanceMap[IssuedSlot.Identifier]      = Descriptor.Token;
        }
    };

    Adapter.UnregisterEditorEntry = [this](World::InstanceToken Token)
    {
        const auto Found = InstanceToOutlinerMap.find(Token.SlotIndex);
        if (Found == InstanceToOutlinerMap.end())
        {
            return;
        }

        const OutlinerSlotToken OutlinerSlot = Found->second;
        InstanceToOutlinerMap.erase(Found);
        OutlinerToInstanceMap.erase(OutlinerSlot.Identifier);
        DocumentModelsBySlot.erase(Token.SlotIndex);

        if ((BoundOutliner != nullptr) && OutlinerSlot.Valid())
        {
            // Programmatic instance destruction always removes its Outliner row even if user-delete protected
            if (OutlinerRecordEntry* Entry = BoundOutliner->AccessEntry(OutlinerSlot))
            {
                Entry->RemovalProtected = false;
            }
            (void)BoundOutliner->DeleteSlot(OutlinerSlot);
        }
    };

    Adapter.AssignEditorVisibility = [this](World::InstanceToken Token, bool VisibilityEnabled)
    {
        if (BoundOutliner == nullptr)
        {
            return;
        }

        const OutlinerSlotToken OutlinerSlot = QueryOutlinerSlot(Token);
        if (OutlinerSlot.Valid())
        {
            if (OutlinerRecordEntry* Entry = BoundOutliner->AccessEntry(OutlinerSlot))
            {
                Entry->VisibilityEnabled = VisibilityEnabled;
            }
        }
    };

    TargetRegister.BindEditorSurfaceAdapter(std::move(Adapter));
}

void WorldInstanceEditorBridge::Unbind() noexcept
{
    ReleaseManagedInstances();

    if (BoundRegister != nullptr)
    {
        BoundRegister->ClearEditorSurfaceAdapter();
    }

    InstanceToOutlinerMap.clear();
    OutlinerToInstanceMap.clear();
    BoundRegister = nullptr;
    BoundOutliner = nullptr;
}

void WorldInstanceEditorBridge::ReleaseManagedInstances() noexcept
{
    ManagedHandles.clear();
    DocumentModelsBySlot.clear();
}

void WorldInstanceEditorBridge::AdoptWorldSceneIntoRegister(
    World::WorldInstanceRegister&                   TargetRegister,
    const GeometricRaster::WorldSceneSpecification& Scene)
{
    ReleaseManagedInstances();

    const GeometricRaster::WorldAtmosphereConfiguration& Atmo = Scene.Atmosphere;

    // Adopt Atmosphere medium (Production constructor: avoids duplicating WorldEnvironmentRegistry Outliner rows)
    {
        World::AtmosphereInstanceParameters Params{};
        Params.RayleighScaleMeters    = Atmo.Medium.RayleighScaleHeightMeters;
        Params.MieScaleMeters         = Atmo.Medium.MieScaleHeightMeters;
        Params.OzoneDensityMultiplier = Atmo.Medium.OzoneStrength;
        Params.ObserverAltitudeMeters = Atmo.Medium.ObserverAltitudeMeters;
        Params.VisibilityEnabled      = Atmo.Medium.AtmosphereEnabled;
        World::AtmosphereInstance Instance(TargetRegister, Params);
        ManagedHandles.push_back(std::move(Instance));
    }

    // Adopt Solar body
    {
        World::SolarInstanceParameters Params{};
        Params.AzimuthDegrees         = Atmo.Solar.ManualAzimuthDegrees;
        Params.ElevationDegrees       = Atmo.Solar.ManualElevationDegrees;
        Params.AngularDiameterDegrees = Atmo.Solar.AngularDiameterDegrees;
        Params.IntensityMultiplier    = Atmo.Solar.SolarIntensityMultiplier;
        Params.CycleAnimationEnabled  = Atmo.Solar.CycleAnimationEnabled;
        Params.DayCycleSpeedDegreesS  =
            (Atmo.Solar.DayDurationHours > 1.0e-3f) ? (360.0f / (Atmo.Solar.DayDurationHours * 3600.0f)) : 0.0f;
        Params.VisibilityEnabled      = Atmo.Solar.SolarEnabled;
        World::SolarInstance Instance(TargetRegister, Params);
        ManagedHandles.push_back(std::move(Instance));
    }

    // Adopt Cloud deck
    {
        World::CloudInstanceParameters Params{};
        Params.BaseAltitudeMeters  = Atmo.Clouds.CloudFloorMeters;
        Params.ThicknessMeters     = Atmo.Clouds.LayerThicknessMeters;
        Params.DensityFraction     = Atmo.Clouds.CloudFraction;
        Params.WindSpeedMetersPerS = Atmo.Clouds.WindSpeedMetersPerSecond;
        Params.VisibilityEnabled   = Atmo.Clouds.CloudsEnabled;
        World::CloudInstance Instance(TargetRegister, Params);
        ManagedHandles.push_back(std::move(Instance));
    }

    // Adopt active Lunar bodies [0..5]
    for (uint32_t SlotIndex = 0u; SlotIndex < static_cast<uint32_t>(Atmo.Moons.size()); ++SlotIndex)
    {
        const GeometricRaster::LunarBodyConfiguration& Moon = Atmo.Moons[SlotIndex];
        if (!Moon.Visible)
        {
            continue;
        }

        World::LunarInstanceParameters Params{};
        Params.MoonSlotOrdinal        = SlotIndex;
        Params.RightAscensionDegrees  = Moon.AzimuthDegrees;
        Params.DeclinationDegrees     = Moon.ElevationDegrees;
        Params.AngularDiameterDegrees = Moon.AngularDiameterDegrees;
        Params.PhaseAngleDegrees      = Moon.SynodicPhaseFraction * 360.0f;
        Params.OrbitalRateDegreesPerS = 0.0f;
        Params.AlbedoReflectance      = 0.136f;
        Params.VisibilityEnabled      = Moon.Visible;
        World::LunarInstance Instance(TargetRegister, Params);
        ManagedHandles.push_back(std::move(Instance));
    }

    // Flush initial adoption RequireRebuild bits so idle Edit Mode starts with zero redundant work
    (void)World::ExecuteWorldSimulationSequence(TargetRegister, 0.0f, false);
}

World::InstanceToken WorldInstanceEditorBridge::SpawnInstanceFromDocument(
    World::WorldInstanceRegister&        TargetRegister,
    const Codecs::InstanceDocumentModel& DocumentModel,
    World::InstanceToken                 EnclosingToken)
{
    const std::string& DisplayTitle =
        DocumentModel.EditorDirectives.OutlinerTitle.empty()
            ? DocumentModel.DocumentTitle
            : DocumentModel.EditorDirectives.OutlinerTitle;

    const bool Visible = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableVisibility);
    const bool CastShadows = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableShadowProjection);
    const bool TickEnabled = World::QueryDirectiveEnabled(
        DocumentModel.DirectiveMask,
        World::InstanceDirectiveMask::EnableSimulationTick);

    uint64_t GeometryHash = 0ull;
    uint64_t MaterialHash = 0ull;
    uint64_t ParticleHash = 0ull;
    uint64_t AcousticHash = 0ull;
    for (const Codecs::InstanceSpaceSlotEntry& Slot : DocumentModel.SpaceSlots)
    {
        if (Slot.FormatExtension == ".GeometrySpace")
        {
            GeometryHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".MaterialSpace")
        {
            MaterialHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".ParticleSpace")
        {
            ParticleHash = Slot.DocumentHash64;
        }
        else if (Slot.FormatExtension == ".Acoustic")
        {
            AcousticHash = Slot.DocumentHash64;
        }
    }

    World::CategoryInstanceHandle Spawned{};
    switch (DocumentModel.Category)
    {
        case World::InstanceCategory::SolarBody:
        {
            World::SolarInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::SolarInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::LunarBody:
        {
            World::LunarInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::LunarInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::AtmosphereMedium:
        {
            World::AtmosphereInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::AtmosphereInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::CloudDeck:
        {
            World::CloudInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::CloudInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::Luminaire:
        {
            World::LuminaireInstanceParameters Params{};
            Params.VisibilityEnabled = Visible;
            World::LuminaireInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::ParticleCloud:
        {
            World::ParticleInstanceParameters Params{};
            Params.ParticleDocumentHash64 = ParticleHash;
            Params.SimulationActive       = Visible && TickEnabled;
            World::ParticleInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::ForceFieldVolume:
        {
            World::ForceFieldInstanceParameters Params{};
            Params.FieldActive = Visible && TickEnabled;
            World::ForceFieldInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::TriggerZone:
        {
            World::TriggerInstanceParameters Params{};
            Params.SensorActive = Visible && TickEnabled;
            World::TriggerInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::AcousticSource:
        {
            World::AcousticInstanceParameters Params{};
            Params.AcousticDocumentHash64 = AcousticHash;
            Params.PlaybackActive         = Visible;
            World::AcousticInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::CameraOptics:
        {
            World::CameraInstanceParameters Params{};
            Params.PrimaryViewActive = Visible;
            World::CameraInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
        case World::InstanceCategory::SurfaceGeometry:
        default:
        {
            World::SurfaceInstanceParameters Params{};
            Params.GeometryDocumentHash64 = GeometryHash;
            Params.MaterialDocumentHash64 = MaterialHash;
            Params.CastShadows            = CastShadows;
            Params.VisibilityEnabled      = Visible;
            World::SurfaceInstance Instance(TargetRegister, DisplayTitle, Params, EnclosingToken);
            Spawned = std::move(Instance);
            break;
        }
    }

    if (!Spawned.QueryValid())
    {
        return World::InstanceToken{};
    }

    const World::InstanceToken Token = Spawned.QueryToken();
    (void)TargetRegister.AssignWorldTransform(
        Token,
        DocumentModel.PositionMeters[0],
        DocumentModel.PositionMeters[1],
        DocumentModel.PositionMeters[2],
        DocumentModel.RotationDegrees[0],
        DocumentModel.RotationDegrees[1],
        DocumentModel.RotationDegrees[2],
        DocumentModel.ScaleScalar[0],
        DocumentModel.ScaleScalar[1],
        DocumentModel.ScaleScalar[2]);

    // Synchronize transform into Outliner entry so Inspector reflects spawned .Instance coordinates
    if (BoundOutliner != nullptr)
    {
        const OutlinerSlotToken OutlinerSlot = QueryOutlinerSlot(Token);
        if (OutlinerRecordEntry* Entry = BoundOutliner->AccessEntry(OutlinerSlot))
        {
            Entry->PositionX   = DocumentModel.PositionMeters[0];
            Entry->PositionY   = DocumentModel.PositionMeters[1];
            Entry->PositionZ   = DocumentModel.PositionMeters[2];
            Entry->RotationX   = DocumentModel.RotationDegrees[0];
            Entry->RotationY   = DocumentModel.RotationDegrees[1];
            Entry->RotationZ   = DocumentModel.RotationDegrees[2];
            Entry->ScaleX      = DocumentModel.ScaleScalar[0];
            Entry->ScaleY      = DocumentModel.ScaleScalar[1];
            Entry->ScaleZ      = DocumentModel.ScaleScalar[2];
            Entry->CastShadows = CastShadows;
            if (!DocumentModel.EditorDirectives.CategoryLabel.empty())
            {
                Entry->CategoryLabel = DocumentModel.EditorDirectives.CategoryLabel;
            }
            if (!DocumentModel.EditorDirectives.Subtitle.empty())
            {
                Entry->Subtitle = DocumentModel.EditorDirectives.Subtitle;
            }
        }
    }

    DocumentModelsBySlot[Token.SlotIndex] = DocumentModel;
    ManagedHandles.push_back(std::move(Spawned));
    return Token;
}

World::InstanceToken WorldInstanceEditorBridge::SpawnInstanceFromFile(
    World::WorldInstanceRegister& TargetRegister,
    const std::filesystem::path&  SourcePath,
    const std::array<float, 3>&   WorldPosition,
    World::InstanceToken          EnclosingToken,
    std::string*                  OutDiagnostic)
{
    Codecs::InstanceDocumentModel DecodedModel{};
    std::string LocalDiagnostic{};
    if (!Codecs::InstanceDecoder::DecodeFromFile(SourcePath, DecodedModel, LocalDiagnostic))
    {
        if (OutDiagnostic != nullptr)
        {
            *OutDiagnostic = std::move(LocalDiagnostic);
        }
        return World::InstanceToken{};
    }

    DecodedModel.PositionMeters = WorldPosition;
    const World::InstanceToken Token = SpawnInstanceFromDocument(TargetRegister, DecodedModel, EnclosingToken);
    if (Token.QueryValid() && (BoundOutliner != nullptr))
    {
        const OutlinerSlotToken OutlinerSlot = QueryOutlinerSlot(Token);
        if (OutlinerSlot.Valid())
        {
            BoundOutliner->SelectSingle(OutlinerSlot);
        }
    }

    if (OutDiagnostic != nullptr)
    {
        OutDiagnostic->clear();
    }
    return Token;
}

bool WorldInstanceEditorBridge::RemoveManagedInstance(World::InstanceToken Token) noexcept
{
    for (auto It = ManagedHandles.begin(); It != ManagedHandles.end(); ++It)
    {
        if (It->QueryToken() == Token)
        {
            DocumentModelsBySlot.erase(Token.SlotIndex);
            ManagedHandles.erase(It);
            return true;
        }
    }
    return false;
}

uint32_t WorldInstanceEditorBridge::RemoveOutlinerBoundSlots(std::span<const OutlinerSlotToken> Tokens) noexcept
{
    uint32_t ReclaimedCount = 0u;
    for (const OutlinerSlotToken OutlinerSlot : Tokens)
    {
        const World::InstanceToken Token = QueryInstanceToken(OutlinerSlot);
        if (Token.QueryValid() && RemoveManagedInstance(Token))
        {
            ++ReclaimedCount;
        }
    }
    return ReclaimedCount;
}

OutlinerSlotToken WorldInstanceEditorBridge::DuplicateOutlinerBoundSlot(
    World::WorldInstanceRegister& TargetRegister,
    OutlinerSlotToken             SourceSlot)
{
    const World::InstanceToken SourceToken = QueryInstanceToken(SourceSlot);
    if (!SourceToken.QueryValid())
    {
        return OutlinerSlotToken{};
    }

    const auto ModelIt = DocumentModelsBySlot.find(SourceToken.SlotIndex);
    if (ModelIt == DocumentModelsBySlot.end())
    {
        return OutlinerSlotToken{};
    }

    Codecs::InstanceDocumentModel ClonedModel = ModelIt->second;

    // Pull latest live transform & directives from WorldRegister before duplicating
    if (const World::InstanceRecordEntry* Entry = TargetRegister.FindRecordEntry(SourceToken))
    {
        ClonedModel.DirectiveMask = Entry->DirectiveMask;
        if (Entry->TransformSlot != World::InvalidInstanceSlot)
        {
            const World::WorldTransformStorage& Transforms = TargetRegister.InspectTransforms();
            const uint32_t Slot                            = Entry->TransformSlot;
            ClonedModel.PositionMeters  = {
                Transforms.PositionMetersX[Slot] + 1.0f,
                Transforms.PositionMetersY[Slot],
                Transforms.PositionMetersZ[Slot]
            };
            ClonedModel.RotationDegrees = {
                Transforms.RotationDegreesX[Slot],
                Transforms.RotationDegreesY[Slot],
                Transforms.RotationDegreesZ[Slot]
            };
            ClonedModel.ScaleScalar     = {
                Transforms.ScaleScalarX[Slot],
                Transforms.ScaleScalarY[Slot],
                Transforms.ScaleScalarZ[Slot]
            };
        }
    }

    ClonedModel.DocumentTitle += " Copy";
    if (!ClonedModel.EditorDirectives.OutlinerTitle.empty())
    {
        ClonedModel.EditorDirectives.OutlinerTitle += " Copy";
    }

    const World::InstanceToken ClonedToken = SpawnInstanceFromDocument(TargetRegister, ClonedModel);
    if (!ClonedToken.QueryValid())
    {
        return OutlinerSlotToken{};
    }

    const OutlinerSlotToken ClonedOutlinerSlot = QueryOutlinerSlot(ClonedToken);
    if ((BoundOutliner != nullptr) && ClonedOutlinerSlot.Valid())
    {
        BoundOutliner->SelectSingle(ClonedOutlinerSlot);
    }
    return ClonedOutlinerSlot;
}

bool WorldInstanceEditorBridge::SynchronizeOutlinerMutationsToRegister(
    World::WorldInstanceRegister& TargetRegister) noexcept
{
    if ((BoundOutliner == nullptr) || InstanceToOutlinerMap.empty())
    {
        return false;
    }

    bool Mutated = false;
    const World::WorldTransformStorage& Transforms = TargetRegister.InspectTransforms();

    for (const auto& [SlotIndex, OutlinerSlot] : InstanceToOutlinerMap)
    {
        const OutlinerRecordEntry* Row = BoundOutliner->FindEntry(OutlinerSlot);
        if (Row == nullptr)
        {
            continue;
        }

        const World::InstanceToken Token = QueryInstanceToken(OutlinerSlot);
        const World::InstanceRecordEntry* Entry = TargetRegister.FindRecordEntry(Token);
        if ((Entry == nullptr) || (Entry->TransformSlot == World::InvalidInstanceSlot))
        {
            continue;
        }

        const uint32_t TSlot = Entry->TransformSlot;
        const bool TransformChanged =
            DifferAboveEpsilon(Transforms.PositionMetersX[TSlot], Row->PositionX) ||
            DifferAboveEpsilon(Transforms.PositionMetersY[TSlot], Row->PositionY) ||
            DifferAboveEpsilon(Transforms.PositionMetersZ[TSlot], Row->PositionZ) ||
            DifferAboveEpsilon(Transforms.RotationDegreesX[TSlot], Row->RotationX) ||
            DifferAboveEpsilon(Transforms.RotationDegreesY[TSlot], Row->RotationY) ||
            DifferAboveEpsilon(Transforms.RotationDegreesZ[TSlot], Row->RotationZ) ||
            DifferAboveEpsilon(Transforms.ScaleScalarX[TSlot], Row->ScaleX) ||
            DifferAboveEpsilon(Transforms.ScaleScalarY[TSlot], Row->ScaleY) ||
            DifferAboveEpsilon(Transforms.ScaleScalarZ[TSlot], Row->ScaleZ);

        if (TransformChanged)
        {
            (void)TargetRegister.AssignWorldTransform(
                Token,
                Row->PositionX,
                Row->PositionY,
                Row->PositionZ,
                Row->RotationX,
                Row->RotationY,
                Row->RotationZ,
                Row->ScaleX,
                Row->ScaleY,
                Row->ScaleZ);
            Mutated = true;
        }

        const bool RegisterVisible = World::QueryDirectiveEnabled(
            Entry->DirectiveMask,
            World::InstanceDirectiveMask::EnableVisibility);
        if (RegisterVisible != Row->VisibilityEnabled)
        {
            (void)TargetRegister.AssignVisibility(Token, Row->VisibilityEnabled);
            Mutated = true;
        }
    }

    return Mutated;
}

bool WorldInstanceEditorBridge::ExportInstanceToFile(
    const World::WorldInstanceRegister& SourceRegister,
    World::InstanceToken                Token,
    const std::filesystem::path&        TargetPath,
    bool                                StripEditorDirectives,
    std::string&                        OutDiagnostic) const
{
    const World::InstanceRecordEntry* Entry = SourceRegister.FindRecordEntry(Token);
    if (Entry == nullptr)
    {
        OutDiagnostic = "Cannot export invalid or reclaimed InstanceToken.";
        return false;
    }

    Codecs::InstanceDocumentModel ExportModel{};
    if (const auto ModelIt = DocumentModelsBySlot.find(Token.SlotIndex); ModelIt != DocumentModelsBySlot.end())
    {
        ExportModel = ModelIt->second;
    }
    else
    {
        ExportModel.DocumentTitle = std::string(World::QueryInstanceCategoryName(Token.QueryCategory()));
        ExportModel.Category      = Token.QueryCategory();
    }

    ExportModel.DirectiveMask = Entry->DirectiveMask;
    if (Entry->TransformSlot != World::InvalidInstanceSlot)
    {
        const World::WorldTransformStorage& Transforms = SourceRegister.InspectTransforms();
        const uint32_t Slot                            = Entry->TransformSlot;
        ExportModel.PositionMeters  = {
            Transforms.PositionMetersX[Slot],
            Transforms.PositionMetersY[Slot],
            Transforms.PositionMetersZ[Slot]
        };
        ExportModel.RotationDegrees = {
            Transforms.RotationDegreesX[Slot],
            Transforms.RotationDegreesY[Slot],
            Transforms.RotationDegreesZ[Slot]
        };
        ExportModel.ScaleScalar     = {
            Transforms.ScaleScalarX[Slot],
            Transforms.ScaleScalarY[Slot],
            Transforms.ScaleScalarZ[Slot]
        };
    }

    return Codecs::InstanceEncoder::EncodeToFile(
        TargetPath,
        ExportModel,
        OutDiagnostic,
        StripEditorDirectives,
        nullptr);
}

bool WorldInstanceEditorBridge::SynchronizeRegisterToWorldScene(
    const World::WorldInstanceRegister&       SourceRegister,
    const World::WorldLoopStepMetrics&        Metrics,
    GeometricRaster::WorldSceneSpecification& Scene,
    float                                     Δt) const noexcept
{
    bool SceneMutated = false;
    GeometricRaster::WorldAtmosphereConfiguration& Atmo = Scene.Atmosphere;

    if (Metrics.EvaluatedSolarCount > 0u)
    {
        const World::SolarInstanceRegister& Solar = SourceRegister.InspectSolarRegister();
        if (Solar.QueryActiveCount() > 0u)
        {
            const bool Visible = (Solar.VisibilityEnabled[0] != 0u);
            if (Atmo.Solar.SolarEnabled != Visible)
            {
                Atmo.Solar.SolarEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Solar.ManualAzimuthDegrees, Solar.AzimuthDegrees[0]))
            {
                Atmo.Solar.ManualAzimuthDegrees    = Solar.AzimuthDegrees[0];
                Atmo.Solar.ManualDirectionOverride = true;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Solar.ManualElevationDegrees, Solar.ElevationDegrees[0]))
            {
                Atmo.Solar.ManualElevationDegrees = Solar.ElevationDegrees[0];
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedLunarCount > 0u)
    {
        const World::LunarInstanceRegister& Lunar = SourceRegister.InspectLunarRegister();
        const uint32_t Count                      = Lunar.QueryActiveCount();
        for (uint32_t i = 0u; i < Count; ++i)
        {
            const uint32_t MoonSlot = Lunar.MoonSlotOrdinal[i];
            if (MoonSlot >= Atmo.Moons.size())
            {
                continue;
            }

            GeometricRaster::LunarBodyConfiguration& TargetMoon = Atmo.Moons[MoonSlot];
            const bool  Visible   = (Lunar.VisibilityEnabled[i] != 0u);
            const float PhaseFrac = std::clamp(Lunar.PhaseAngleDegrees[i] / 360.0f, 0.0f, 1.0f);

            if (TargetMoon.Visible != Visible)
            {
                TargetMoon.Visible = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.AzimuthDegrees, Lunar.RightAscensionDegrees[i]))
            {
                TargetMoon.AzimuthDegrees = Lunar.RightAscensionDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.ElevationDegrees, Lunar.DeclinationDegrees[i]))
            {
                TargetMoon.ElevationDegrees = Lunar.DeclinationDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.AngularDiameterDegrees, Lunar.AngularDiameterDegrees[i]))
            {
                TargetMoon.AngularDiameterDegrees = Lunar.AngularDiameterDegrees[i];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(TargetMoon.SynodicPhaseFraction, PhaseFrac))
            {
                TargetMoon.SynodicPhaseFraction = PhaseFrac;
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedAtmosphereCount > 0u)
    {
        const World::AtmosphereInstanceRegister& Medium = SourceRegister.InspectAtmosphereRegister();
        if (Medium.QueryActiveCount() > 0u)
        {
            const bool Visible = (Medium.VisibilityEnabled[0] != 0u);
            if (Atmo.Medium.AtmosphereEnabled != Visible)
            {
                Atmo.Medium.AtmosphereEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.RayleighScaleHeightMeters, Medium.RayleighScaleMeters[0]))
            {
                Atmo.Medium.RayleighScaleHeightMeters = Medium.RayleighScaleMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.MieScaleHeightMeters, Medium.MieScaleMeters[0]))
            {
                Atmo.Medium.MieScaleHeightMeters = Medium.MieScaleMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.OzoneStrength, Medium.OzoneDensityMultiplier[0]))
            {
                Atmo.Medium.OzoneStrength = Medium.OzoneDensityMultiplier[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Medium.ObserverAltitudeMeters, Medium.ObserverAltitudeMeters[0]))
            {
                Atmo.Medium.ObserverAltitudeMeters = Medium.ObserverAltitudeMeters[0];
                SceneMutated = true;
            }
        }
    }

    if (Metrics.EvaluatedCloudCount > 0u)
    {
        const World::CloudInstanceRegister& Clouds = SourceRegister.InspectCloudRegister();
        if (Clouds.QueryActiveCount() > 0u)
        {
            const bool Visible = (Clouds.VisibilityEnabled[0] != 0u);
            if (Atmo.Clouds.CloudsEnabled != Visible)
            {
                Atmo.Clouds.CloudsEnabled = Visible;
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.CloudFloorMeters, Clouds.BaseAltitudeMeters[0]))
            {
                Atmo.Clouds.CloudFloorMeters = Clouds.BaseAltitudeMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.LayerThicknessMeters, Clouds.ThicknessMeters[0]))
            {
                Atmo.Clouds.LayerThicknessMeters = Clouds.ThicknessMeters[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.CloudFraction, Clouds.DensityFraction[0]))
            {
                Atmo.Clouds.CloudFraction = Clouds.DensityFraction[0];
                SceneMutated = true;
            }
            if (DifferAboveEpsilon(Atmo.Clouds.WindSpeedMetersPerSecond, Clouds.WindSpeedMetersPerS[0]))
            {
                Atmo.Clouds.WindSpeedMetersPerSecond = Clouds.WindSpeedMetersPerS[0];
                SceneMutated = true;
            }
            if ((Δt > 0.0f) && (Clouds.WindSpeedMetersPerS[0] != 0.0f))
            {
                Atmo.SimulationSeconds += Δt;
                SceneMutated = true;
            }
        }
    }

    return SceneMutated;
}

OutlinerSlotToken WorldInstanceEditorBridge::QueryOutlinerSlot(World::InstanceToken Token) const noexcept
{
    if (!Token.QueryValid())
    {
        return OutlinerSlotToken{};
    }

    const auto Found = InstanceToOutlinerMap.find(Token.SlotIndex);
    return (Found != InstanceToOutlinerMap.end()) ? Found->second : OutlinerSlotToken{};
}

World::InstanceToken WorldInstanceEditorBridge::QueryInstanceToken(OutlinerSlotToken SlotToken) const noexcept
{
    if (!SlotToken.Valid())
    {
        return World::InstanceToken{};
    }

    const auto Found = OutlinerToInstanceMap.find(SlotToken.Identifier);
    return (Found != OutlinerToInstanceMap.end()) ? Found->second : World::InstanceToken{};
}

} // namespace Frontier::Editor

#endif // FRONTIER_PROFILE_EDITOR
