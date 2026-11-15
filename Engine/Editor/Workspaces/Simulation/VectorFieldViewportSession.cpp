/*============================================================================================================================================
                                                        VECTORFIELDVIEWPORTSESSION.CPP
============================================================================================================================================*/
// 🧩 Editor session binding ForceFieldAccumulator, FieldLatticeVulkanExecutor, VectorFieldOverlaySequence & 3-dock UI panes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Editor/Workspaces/Simulation/VectorFieldViewportSession.h"
#include "Engine/Codecs/ForceModelPresetCodec.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier::Editor::Simulation
{

namespace
{

[[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry BuildPresetForTemplateEntry(
    uint32_t         ForceModelOrdinal,
    std::string_view Title)
{
    using namespace SimulationSpace::ForceFields;
    ForceSourceRecordEntry Desc{};
    Desc.RadiusMeters      = 20.0f;
    Desc.Strength          = 18.0f;
    Desc.FalloffExponent   = 1.4f;
    Desc.CentreMeters      = { 0.0f, 0.0f, 6.0f };
    Desc.EvaluatedCentreMeters = Desc.CentreMeters;
    Desc.Label             = std::string(Title);

    if (ForceModelOrdinal < static_cast<uint32_t>(ForceModel::Count))
    {
        Desc.Model = static_cast<ForceModel>(ForceModelOrdinal);
    }
    else if (Title.find("Radial") != std::string_view::npos || Title.find("Burst") != std::string_view::npos)
    {
        Desc.Model = ForceModel::RadialBurst;
    }
    else if (Title.find("Rotational") != std::string_view::npos || Title.find("Vortex") != std::string_view::npos)
    {
        Desc.Model = ForceModel::RotationalColumn;
    }
    else if (Title.find("Attractor") != std::string_view::npos)
    {
        Desc.Model = ForceModel::Attractor;
    }
    else if (Title.find("Curl") != std::string_view::npos || Title.find("Turbulence") != std::string_view::npos)
    {
        Desc.Model = ForceModel::CurlNoise;
    }
    else if (Title.find("Formula") != std::string_view::npos)
    {
        Desc.Model = ForceModel::FlowStream;
    }
    else
    {
        Desc.Model = ForceModel::LinearWind;
    }

    Desc.Domain = QueryDomainForModel(Desc.Model);
    switch (Desc.Model)
    {
    case ForceModel::LinearWind:
        Desc.Strength       = 14.5f;
        Desc.RadiusMeters   = 26.0f;
        Desc.EulerDegrees.Z = 228.0f;
        break;
    case ForceModel::Buoyancy:
        Desc.Strength     = 15.0f;
        Desc.RadiusMeters = 18.0f;
        Desc.Envelope     = ReachEnvelopeCategory::Column;
        break;
    case ForceModel::RadialBurst:
        Desc.Strength           = 28.0f;
        Desc.RadiusMeters       = 18.0f;
        Desc.DurationMode       = ForceDurationMode::Impulse;
        Desc.ImpulseDurationSec = 1.6f;
        break;
    case ForceModel::Attractor:
        Desc.Strength         = 22.0f;
        Desc.CoreRadiusMeters = 3.0f;
        Desc.Attenuation      = ForceAttenuationMode::InverseSquare;
        break;
    case ForceModel::Repulsor:
        Desc.Strength         = 24.0f;
        Desc.CoreRadiusMeters = 3.5f;
        break;
    case ForceModel::AxisAttractor:
        Desc.Envelope   = ReachEnvelopeCategory::Column;
        Desc.Strength   = 18.0f;
        Desc.SwirlRatio = 0.55f;
        break;
    case ForceModel::Containment:
        Desc.Strength         = 24.0f;
        Desc.CoreRadiusMeters = 8.0f;
        Desc.RadiusMeters     = 22.0f;
        break;
    case ForceModel::RotationalColumn:
        Desc.Strength     = 24.0f;
        Desc.SwirlRatio   = 1.1f;
        Desc.RadiusMeters = 20.0f;
        break;
    case ForceModel::Torque:
        Desc.Strength   = 16.0f;
        Desc.SwirlRatio = 1.4f;
        break;
    case ForceModel::Drag:
        Desc.Strength        = 12.0f;
        Desc.DragCoefficient = 1.15f;
        break;
    case ForceModel::CurlNoise:
        Desc.Strength              = 16.5f;
        Desc.TurbulenceScaleMeters = 12.0f;
        Desc.OctaveCount           = 3u;
        break;
    case ForceModel::AtmosphericGust:
        Desc.Strength       = 21.0f;
        Desc.DurationMode   = ForceDurationMode::Modulated;
        Desc.FrequencyHertz = 0.25f;
        break;
    case ForceModel::VorticityConfinement:
        Desc.Strength              = 18.0f;
        Desc.TurbulenceScaleMeters = 10.0f;
        break;
    case ForceModel::BrownianNoise:
        Desc.Strength              = 14.0f;
        Desc.TurbulenceScaleMeters = 8.0f;
        break;
    case ForceModel::LorentzField:
        Desc.Strength   = 20.0f;
        Desc.SwirlRatio = 1.1f;
        break;
    case ForceModel::MagneticDipole:
        Desc.Strength         = 19.0f;
        Desc.CoreRadiusMeters = 3.5f;
        break;
    case ForceModel::FlowStream:
        Desc.Strength      = 24.0f;
        Desc.RadiusMeters  = 18.0f;
        Desc.StreamProfile = FlowStreamProfile::SlipstreamJet;
        if (Title.find("Formula") != std::string_view::npos)
        {
            Desc.FormulaEnabled    = true;
            Desc.FormulaSourceText = QueryBuiltInFormulaPresets()[0].FormulaSource;
        }
        break;
    case ForceModel::SplineForce:
        Desc.Strength     = 20.0f;
        Desc.RadiusMeters = 18.0f;
        break;
    case ForceModel::BoundaryAdhesion:
        Desc.Strength         = 15.0f;
        Desc.CoreRadiusMeters = 4.0f;
        break;
    default:
        break;
    }
    return Desc;
}

[[nodiscard]] const char* QueryForceDomainCaption(SimulationSpace::ForceFields::ForceDomainCategory Domain) noexcept
{
    using namespace SimulationSpace::ForceFields;
    switch (Domain)
    {
    case ForceDomainCategory::VectorField:     return "VectorField";
    case ForceDomainCategory::StreamTransport:       return "StreamTransport";
    case ForceDomainCategory::TurbulenceField: return "Turbulence";
    default:                                   return "ForceField";
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                INITIALIZATION & BINDING
//------------------------------------------------------------------------------------------------------------------------

bool VectorFieldViewportSession::AttachInitializedSession(
    OutlinerPanel&               Outliner,
    ViewportPanel&               Viewport,
    InspectorPanel&              Inspector,
    const std::filesystem::path& ShaderBinaryDirectory,
    const std::filesystem::path& SwiftShaderIcdDir,
    std::string&                 OutDiagnostic) noexcept
{
    BoundOutliner  = &Outliner;
    BoundViewport  = &Viewport;
    BoundInspector = &Inspector;
    AuthoredSources.clear();
    NextForceToken = 1u;

    Cameras[0]            = OverlayCameraState{};
    Cameras[0].Projection = ViewportProjectionPreset::Perspective;
    Cameras[1]            = OverlayCameraState{};
    Cameras[1].Projection = ViewportProjectionPreset::Top;
    Cameras[1].OrthoHalfHeightM = 36.0f;

    const auto ComputeSpv = ShaderBinaryDirectory / "FieldLatticeCompute.spv";
    if (!VulkanExecutor.Initialize(ComputeSpv, SwiftShaderIcdDir, OutDiagnostic))
    {
        return false;
    }
    if (!OverlaySequence.Initialize(VulkanExecutor, ShaderBinaryDirectory, OutDiagnostic))
    {
        return false;
    }

    std::vector<OutlinerConstructTemplate> ForceTemplates;
    ForceTemplates.reserve(22u);
    for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::Count); ++Ord)
    {
        const auto Model    = static_cast<SimulationSpace::ForceFields::ForceModel>(Ord);
        const auto Domain   = SimulationSpace::ForceFields::QueryDomainForModel(Model);
        const char* Name    = SimulationSpace::ForceFields::QueryForceModelDisplayName(Model);
        const char* DomName = QueryForceDomainCaption(Domain);
        ForceTemplates.push_back({
            std::string("Add ") + Name,
            Name,
            DomName,
            "Analytical ForceModel",
            ConstructSlotCategory::ForceField,
            Ord
        });
    }
    ForceTemplates.push_back({ "Add Custom Formula", "Custom Formula", "VectorFieldLang", "Symbolic GPU formula", ConstructSlotCategory::ForceField, static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::FlowStream) });
    ForceTemplates.push_back({ "Add Cube Mesh",      "Cube",           "Static Mesh",     "Position 0, 0, 0 m",   ConstructSlotCategory::GeometryVolume,  0xFFFFFFFFu });
    ForceTemplates.push_back({ "Add Point Light",    "Point Light",    "Light",           "Position 0, 0, 0 m",   ConstructSlotCategory::PointLuminaire,0xFFFFFFFFu });
    Outliner.ConfigureConstructTemplates(std::move(ForceTemplates));

    ContentBrowser.PopulateDefaultForcePresets();
    ContentBrowser.ConfigureConstructPresetCallback(
        [this](const Codecs::ForceModelPresetDocument& Doc)
        {
            OutlinerSlotToken TargetFolder = DefaultForceFolderToken;
            if (TargetFolder.Valid() && BoundOutliner != nullptr && BoundOutliner->FindEntry(TargetFolder) == nullptr)
            {
                TargetFolder = {};
            }
            (void)AddForceSourceEntry(Doc.ForceSource, TargetFolder);
        });
    ContentBrowser.ConfigureSaveSelectedCallback(
        [this](Codecs::ForceModelPresetDocument& OutDoc) -> bool
        {
            if (BoundOutliner == nullptr)
            {
                return false;
            }
            const OutlinerSlotToken Primary = BoundOutliner->QueryPrimarySelection();
            const uint32_t          ForceTok = QueryMappedForceToken(Primary);
            for (const auto& Src : Accumulator.QuerySources())
            {
                if (Src.UniqueToken == ForceTok)
                {
                    OutDoc.PresetTitle  = Src.Label + " Preset";
                    OutDoc.AuthorNote   = "Captured from active Frontier scene.";
                    OutDoc.ForceSource  = Src;
                    return true;
                }
            }
            return false;
        });

    std::vector<ViewportConstructPresetEntry> ViewportPresets;
    std::vector<Codecs::ForceModelPresetDocument> CapturedPresetDocs;
    for (const auto& Asset : ContentBrowser.QueryAllAssets())
    {
        if (Asset.Category == ContentAssetCategory::ForcePresets || Asset.Category == ContentAssetCategory::FormulaPresets)
        {
            ViewportPresets.push_back({
                Asset.Title,
                Asset.CategoryCaption,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Model),
                Asset.FormulaOverride,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Domain)
            });
            CapturedPresetDocs.push_back(Asset.PresetDocument);
        }
    }
    Viewport.ConfigureConstructPresets(std::move(ViewportPresets));
    Viewport.ConfigureConstructPresetCallback(
        [this, CapturedPresetDocs = std::move(CapturedPresetDocs)](uint32_t PresetIdx)
        {
            if (PresetIdx >= CapturedPresetDocs.size())
            {
                return;
            }
            OutlinerSlotToken TargetFolder = DefaultForceFolderToken;
            if (TargetFolder.Valid() && BoundOutliner != nullptr && BoundOutliner->FindEntry(TargetFolder) == nullptr)
            {
                TargetFolder = {};
            }
            (void)AddForceSourceEntry(CapturedPresetDocs[PresetIdx].ForceSource, TargetFolder);
        });
    Viewport.ConfigureOpenContentBrowserCallback(
        [this]()
        {
            ContentBrowser.ConfigureOriginFilter(ContentAssetOrigin::All);
            ContentBrowser.ConfigureCategoryFilter(ContentAssetCategory::ForcePresets);
            ContentBrowser.ConfigureOpen(true, false);
        });

    Viewport.AttachOutliner(&Outliner);
    Inspector.AttachOutliner(&Outliner);

    Viewport.ConfigureForceOverlayToolsVisible(true);
    Viewport.ConfigureSimulationTimeSeconds(SimulationTimeSec);
    Viewport.ConfigureRasterCallback([this](const ViewportPaneSurfaceSpecification& Spec)
                                     { return DispatchViewportRaster(Spec); });
    Viewport.ConfigureInteractionCallback([this](uint32_t PaneIdx, Rectangle2D Bounds, const PaneInputReading& Input)
                                          { ProcessViewportInteraction(PaneIdx, Bounds, Input); });
    Inspector.ConfigureFullOverrideCallback(
        [this](PixelSpace& Surface, Rectangle2D Bounds, OutlinerPanel* OutlinerPtr, OutlinerRecordEntry* PrimaryEntry, const PaneInputReading& Input, double Δt)
        { return RenderFullForceInspector(Surface, Bounds, OutlinerPtr, PrimaryEntry, Input, Δt); });

    AnemometerHistoryMs.clear();
    return true;
}

void VectorFieldViewportSession::RecordAnemometerSample(double SampleTimeSec) noexcept
{
    const auto ProbePos = (BoundViewport != nullptr)
                              ? BoundViewport->QueryForceOverlaySettings().AnemometerWorldMeters
                              : SimulationSpace::ForceFields::SpatialVector3{ 0.0f, 0.0f, 8.0f };
    const auto Diag     = Accumulator.QueryAnemometerDiagnostics(ProbePos, SampleTimeSec);
    const float WindSpd = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.AdvectionVelocity);
    const float AccSpd  = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.LinearAcceleration);
    const float Speed   = (WindSpd > 0.01f) ? WindSpd : AccSpd;

    AnemometerHistoryMs.push_back(Speed);
    if (AnemometerHistoryMs.size() > 72u)
    {
        AnemometerHistoryMs.erase(AnemometerHistoryMs.begin());
    }
}

void VectorFieldViewportSession::StepSimulation(double ΔtSeconds) noexcept
{
    if (BoundViewport != nullptr && BoundViewport->ConsumeRewindRequested())
    {
        SimulationTimeSec = 0.0;
        AnemometerHistoryMs.clear();
    }

    const bool Advance = (BoundViewport == nullptr) ||
                         (BoundViewport->QueryExecutionMode() == ViewportExecutionMode::Simulate) ||
                         (BoundViewport->QueryExecutionMode() == ViewportExecutionMode::Play && !BoundViewport->QueryPlayPaused());
    if (Advance && ΔtSeconds > 0.0)
    {
        SimulationTimeSec += ΔtSeconds;
    }

    SynchronizeForceAccumulator();
    Accumulator.StepAnimation(SimulationTimeSec, Advance);
    RecordAnemometerSample(SimulationTimeSec);

    if (BoundViewport != nullptr)
    {
        BoundViewport->ConfigureSimulationTimeSeconds(SimulationTimeSec);
    }
}

OutlinerSlotToken VectorFieldViewportSession::AddForceSourceEntry(
    SimulationSpace::ForceFields::ForceSourceRecordEntry Descriptor,
    OutlinerSlotToken                                    EnclosingFolder)
{
    if (BoundOutliner == nullptr)
    {
        return {};
    }

    if (EnclosingFolder.Valid() && !DefaultForceFolderToken.Valid())
    {
        DefaultForceFolderToken = EnclosingFolder;
    }

    Descriptor.UniqueToken = NextForceToken++;
    Descriptor.Domain      = SimulationSpace::ForceFields::QueryDomainForModel(Descriptor.Model);

    const OutlinerSlotToken Token = BoundOutliner->ConstructFromTemplate(static_cast<uint32_t>(Descriptor.Model), EnclosingFolder);
    if (OutlinerRecordEntry* Created = BoundOutliner->AccessEntry(Token))
    {
        Created->Category             = ConstructSlotCategory::ForceField;
        Created->Title                = Descriptor.Label;
        Created->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(Descriptor.Model);
        Created->ForceModelOrdinal      = static_cast<uint32_t>(Descriptor.Model);
        Created->ForceFormulaOverride = Descriptor.FormulaEnabled;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", Descriptor.RadiusMeters, Descriptor.Strength);
        Created->Subtitle  = SubBuf;
        Created->PositionX = Descriptor.CentreMeters.X;
        Created->PositionY = Descriptor.CentreMeters.Y;
        Created->PositionZ = Descriptor.CentreMeters.Z;
        Created->RotationX = Descriptor.EulerDegrees.X;
        Created->RotationY = Descriptor.EulerDegrees.Y;
        Created->RotationZ = Descriptor.EulerDegrees.Z;
    }

    Descriptor.OutlinerTokenId = Token.Identifier;
    AuthoredSources.push_back(std::move(Descriptor));
    Accumulator.AssignSources(AuthoredSources);
    return Token;
}

//------------------------------------------------------------------------------------------------------------------------
//                                           OUTLINER <-> ACCUMULATOR SYNC
//------------------------------------------------------------------------------------------------------------------------

void VectorFieldViewportSession::SynchronizeForceAccumulator()
{
    if (BoundOutliner == nullptr)
    {
        return;
    }

    // Keep local AuthoredSources synchronized with current Accumulator edits
    if (Accumulator.QuerySources().size() == AuthoredSources.size())
    {
        const auto Live = Accumulator.QuerySources();
        AuthoredSources.assign(Live.begin(), Live.end());
    }

    bool Dirty = false;
    for (const auto& Entry : BoundOutliner->QueryEntries())
    {
        if (Entry.Category != ConstructSlotCategory::ForceField)
        {
            continue;
        }
        auto It = std::find_if(
            AuthoredSources.begin(),
            AuthoredSources.end(),
            [&](const auto& Src) { return Src.OutlinerTokenId == Entry.Token.Identifier; });

        if (It == AuthoredSources.end())
        {
            auto Desc            = BuildPresetForTemplateEntry(Entry.ForceModelOrdinal, Entry.Title);
            Desc.UniqueToken     = NextForceToken++;
            Desc.OutlinerTokenId = Entry.Token.Identifier;
            Desc.CentreMeters    = { Entry.PositionX, Entry.PositionY, Entry.PositionZ };
            Desc.EvaluatedCentreMeters = Desc.CentreMeters;
            Desc.EulerDegrees    = { Entry.RotationX, Entry.RotationY, Entry.RotationZ };
            Desc.Enabled         = BoundOutliner->QueryEffectiveVisibility(Entry.Token);
            if (OutlinerRecordEntry* Mut = BoundOutliner->AccessEntry(Entry.Token))
            {
                Mut->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(Desc.Model);
                Mut->ForceModelOrdinal      = static_cast<uint32_t>(Desc.Model);
                Mut->ForceFormulaOverride = Desc.FormulaEnabled;
                char SubBuf[96]{};
                std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", Desc.RadiusMeters, Desc.Strength);
                Mut->Subtitle = SubBuf;
            }
            AuthoredSources.push_back(std::move(Desc));
            Dirty = true;
        }
        else
        {
            It->Enabled = BoundOutliner->QueryEffectiveVisibility(Entry.Token);
            if (!It->DynamicMotion && It->Motion.Topology == SimulationSpace::ForceFields::MotionPathTopology::Static)
            {
                It->CentreMeters          = { Entry.PositionX, Entry.PositionY, Entry.PositionZ };
                It->EvaluatedCentreMeters = It->CentreMeters;
                It->EulerDegrees          = { Entry.RotationX, Entry.RotationY, Entry.RotationZ };
            }
            else if (OutlinerRecordEntry* Mut = BoundOutliner->AccessEntry(Entry.Token))
            {
                Mut->PositionX            = It->EvaluatedCentreMeters.X;
                Mut->PositionY            = It->EvaluatedCentreMeters.Y;
                Mut->PositionZ            = It->EvaluatedCentreMeters.Z;
                Mut->ForceModelOrdinal      = static_cast<uint32_t>(It->Model);
                Mut->ForceFormulaOverride = It->FormulaEnabled;
            }
        }
    }

    // Erase deleted outliner entries
    const std::size_t BeforeSz = AuthoredSources.size();
    AuthoredSources.erase(
        std::remove_if(
            AuthoredSources.begin(),
            AuthoredSources.end(),
            [&](const auto& Src)
            { return BoundOutliner->FindEntry(OutlinerSlotToken{ Src.OutlinerTokenId }) == nullptr; }),
        AuthoredSources.end());
    if (AuthoredSources.size() != BeforeSz || Dirty)
    {
        Accumulator.AssignSources(AuthoredSources);
    }
    else
    {
        // Push lightweight flag/transform updates cleanly
        auto LiveMut = Accumulator.AccessSources();
        for (std::size_t I = 0u; I < std::min(LiveMut.size(), AuthoredSources.size()); ++I)
        {
            LiveMut[I].Enabled               = AuthoredSources[I].Enabled;
            LiveMut[I].CentreMeters          = AuthoredSources[I].CentreMeters;
            LiveMut[I].EvaluatedCentreMeters = AuthoredSources[I].EvaluatedCentreMeters;
            LiveMut[I].EulerDegrees          = AuthoredSources[I].EulerDegrees;
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                        VIEWPORT RASTER & PICKING HANDLERS
//------------------------------------------------------------------------------------------------------------------------

ViewportRasterSurface VectorFieldViewportSession::DispatchViewportRaster(const ViewportPaneSurfaceSpecification& Spec)
{
    SynchronizeForceAccumulator();

    std::vector<SimulationSpace::ForceFields::ForceSourceShaderEntry> PackedSources;
    std::vector<SimulationSpace::ForceFields::SpatialVector3>         PackedWaypoints;
    LatticeCascade.PackGpuSourceBuffers(Accumulator, Spec.TimeSeconds, PackedSources, PackedWaypoints);

    const auto LiveSources = Accumulator.QuerySources();
    std::vector<uint32_t> SourceTokens;
    std::vector<uint32_t> WaypointOffsets;
    std::vector<uint32_t> WaypointCounts;
    std::vector<uint8_t>  PathIsOrbit;
    SourceTokens.reserve(PackedSources.size());
    WaypointOffsets.reserve(PackedSources.size());
    WaypointCounts.reserve(PackedSources.size());
    PathIsOrbit.reserve(PackedSources.size());

    for (std::size_t I = 0u; I < PackedSources.size(); ++I)
    {
        const uint32_t OutlinerId = (I < LiveSources.size()) ? LiveSources[I].OutlinerTokenId : static_cast<uint32_t>(I + 1u);
        const bool     IsOrbit    = (I < LiveSources.size()) &&
                                    (LiveSources[I].Motion.Topology == SimulationSpace::ForceFields::MotionPathTopology::HelicalOrbit);
        SourceTokens.push_back(OutlinerId);
        WaypointOffsets.push_back(PackedSources[I].WaypointStartIdx);
        WaypointCounts.push_back(PackedSources[I].WaypointCount);
        PathIsOrbit.push_back(IsOrbit ? 1u : 0u);
    }

    const uint32_t PaneIdx = (Spec.PaneIndex < 2u) ? Spec.PaneIndex : 0u;
    Cameras[PaneIdx].Projection = Spec.Projection;

    const OutlinerSlotToken PrimarySel = (BoundOutliner != nullptr) ? BoundOutliner->QueryPrimarySelection() : OutlinerSlotToken{};

    // Update live anemometer telemetry
    const auto ProbePos = Spec.ForceOverlay.AnemometerWorldMeters;
    const auto Diag     = Accumulator.QueryAnemometerDiagnostics(ProbePos, Spec.TimeSeconds);
    const float Speed   = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.AdvectionVelocity);
    if (AnemometerHistoryMs.empty())
    {
        AnemometerHistoryMs.push_back(Speed);
    }

    if (BoundViewport != nullptr)
    {
        char ProbeBuf[160]{};
        std::snprintf(
            ProbeBuf,
            sizeof(ProbeBuf),
            "Probe (%.1f, %.1f, %.1f) m  |  %.2f m/s (Bft %u)  |  |curl| %.2f /s  |  div %.2f /s  |  q %.1f Pa",
            ProbePos.X,
            ProbePos.Y,
            ProbePos.Z,
            Speed,
            Diag.BeaufortScale,
            Diag.CurlMagnitude,
            Diag.Divergence,
            Diag.DynamicPressurePa);
        BoundViewport->ConfigureProbeReadoutText(ProbeBuf);
    }

    OverlayFrameRequest Req{};
    Req.PaneIndex             = PaneIdx;
    Req.WidthPx               = Spec.WidthPx;
    Req.HeightPx              = Spec.HeightPx;
    Req.Camera                = Cameras[PaneIdx];
    Req.OverlaySettings       = Spec.ForceOverlay;
    Req.TimeSeconds           = Spec.TimeSeconds;
    Req.SelectedForceToken    = PrimarySel.Identifier;
    Req.SelectedWaypointIndex = SelectedWaypointIdx;
    Req.LitShadingEnabled     = true;
    Req.SliceHalfSpanMeters   = 36.0f;
    Req.PackedSources         = PackedSources;
    Req.SourceTokens          = SourceTokens;
    Req.PackedWaypoints       = PackedWaypoints;
    Req.SourceWaypointOffsets = WaypointOffsets;
    Req.SourceWaypointCounts  = WaypointCounts;
    Req.SourcePathIsOrbit     = PathIsOrbit;

    std::string Diagnostic;
    const OverlayRenderOutput Out = OverlaySequence.RenderPane(Req, Diagnostic);

    ViewportRasterSurface Surface{};
    Surface.SoftwareTextureData = Out.ImTextureDataPtr;
    Surface.ActiveForceCount    = static_cast<uint32_t>(PackedSources.size());
    return Surface;
}

void VectorFieldViewportSession::ProcessViewportInteraction(
    uint32_t                PaneIndex,
    Rectangle2D             PaneBounds,
    const PaneInputReading& Input)
{
    if (PaneBounds.Width <= 2.0f || PaneBounds.Height <= 2.0f)
    {
        return;
    }

    const uint32_t SafePane = (PaneIndex < 2u) ? PaneIndex : 0u;
    const float    NormU    = std::clamp((Input.PrimaryPointer.X - PaneBounds.X) / PaneBounds.Width, 0.0f, 1.0f);
    const float    NormV    = std::clamp((Input.PrimaryPointer.Y - PaneBounds.Y) / PaneBounds.Height, 0.0f, 1.0f);

    if (Input.PrimaryPointer.Pressed)
    {
        const OverlayPickResult Pick = OverlaySequence.QueryPickAtNormalizedUv(SafePane, NormU, NormV);
        if (Pick.Category == OverlayPickCategory::ForceCircle && Pick.PrimaryId != 0u && BoundOutliner != nullptr)
        {
            BoundOutliner->SelectSingle(OutlinerSlotToken{ Pick.PrimaryId });
            SelectedWaypointIdx = -1;
        }
        else if (Pick.Category == OverlayPickCategory::MotionWaypoint && Pick.PrimaryId != 0u && BoundOutliner != nullptr)
        {
            BoundOutliner->SelectSingle(OutlinerSlotToken{ Pick.PrimaryId });
            SelectedWaypointIdx = static_cast<int32_t>(Pick.SecondaryId);
        }
        else
        {
            OrbitDragging  = true;
            DragPaneIndex  = SafePane;
            LastDragCursor = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
        }
    }

    if (OrbitDragging && Input.PrimaryPointer.Down && DragPaneIndex == SafePane)
    {
        const float Dx = Input.PrimaryPointer.X - LastDragCursor.X;
        const float Dy = Input.PrimaryPointer.Y - LastDragCursor.Y;
        LastDragCursor = { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

        if (Input.ShiftHeld && BoundViewport != nullptr)
        {
            auto& Probe = BoundViewport->AccessForceOverlaySettings().AnemometerWorldMeters;
            Probe.X    += Dx * 0.08f;
            Probe.Y    -= Dy * 0.08f;
        }
        else
        {
            Cameras[SafePane].YawRadians   -= Dx * 0.008f;
            Cameras[SafePane].PitchRadians  = std::clamp(Cameras[SafePane].PitchRadians + Dy * 0.007f, 0.08f, 1.48f);
        }
    }
    if (!Input.PrimaryPointer.Down)
    {
        OrbitDragging = false;
    }

    if (std::abs(Input.WheelDeltaY) > 0.01f)
    {
        const float ZoomFactor = (Input.WheelDeltaY > 0.0f) ? 0.90f : 1.11f;
        Cameras[SafePane].DistanceMeters    = std::clamp(Cameras[SafePane].DistanceMeters * ZoomFactor, 12.0f, 180.0f);
        Cameras[SafePane].OrthoHalfHeightM  = std::clamp(Cameras[SafePane].OrthoHalfHeightM * ZoomFactor, 8.0f, 120.0f);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                           1:1 FORCE INSPECTOR DELEGATION
//------------------------------------------------------------------------------------------------------------------------

bool VectorFieldViewportSession::RenderFullForceInspector(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    OutlinerPanel*          Outliner,
    OutlinerRecordEntry*    PrimaryEntry,
    const PaneInputReading& Input,
    double                  Δt)
{
    if (PrimaryEntry != nullptr && PrimaryEntry->Category != ConstructSlotCategory::ForceField)
    {
        return false;
    }

    auto LiveSources = Accumulator.AccessSources();
    SimulationSpace::ForceFields::ForceSourceRecordEntry* ActiveSource = nullptr;
    if (PrimaryEntry != nullptr)
    {
        for (auto& Src : LiveSources)
        {
            if (Src.OutlinerTokenId == PrimaryEntry->Token.Identifier)
            {
                ActiveSource = &Src;
                break;
            }
        }
    }

    if (AnemometerHistoryMs.empty())
    {
        RecordAnemometerSample(SimulationTimeSec);
    }

    ForceInspectorViewTargets Targets{};
    Targets.Outliner          = Outliner;
    Targets.OutlinerEntry     = PrimaryEntry;
    Targets.Accumulator       = &Accumulator;
    Targets.LatticeCascade    = &LatticeCascade;
    Targets.ActiveForce       = ActiveSource;
    Targets.OverlaySettings   = (BoundViewport != nullptr) ? &BoundViewport->AccessForceOverlaySettings() : nullptr;
    Targets.AnemometerSamples = AnemometerHistoryMs;
    Targets.SimulationTimeSec = SimulationTimeSec;
    Targets.SelectedWaypoint  = &SelectedWaypointIdx;

    const bool Handled = InspectorView.Render(Surface, Bounds, Targets, Input, Δt);

    if (ActiveSource != nullptr && PrimaryEntry != nullptr)
    {
        PrimaryEntry->ForceModelOrdinal      = static_cast<uint32_t>(ActiveSource->Model);
        PrimaryEntry->ForceFormulaOverride = ActiveSource->FormulaEnabled;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", ActiveSource->RadiusMeters, ActiveSource->Strength);
        PrimaryEntry->Subtitle = SubBuf;
    }
    return Handled;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ACCESSORS
//------------------------------------------------------------------------------------------------------------------------

SimulationSpace::ForceFields::ForceFieldAccumulator& VectorFieldViewportSession::AccessAccumulator() noexcept { return Accumulator; }
const SimulationSpace::ForceFields::ForceFieldAccumulator& VectorFieldViewportSession::QueryAccumulator() const noexcept { return Accumulator; }
SimulationSpace::ForceFields::FieldLatticeCascade& VectorFieldViewportSession::AccessLatticeCascade() noexcept { return LatticeCascade; }
SimulationSpace::ForceFields::FieldLatticeVulkanExecutor& VectorFieldViewportSession::AccessVulkanExecutor() noexcept { return VulkanExecutor; }
VectorFieldOverlaySequence& VectorFieldViewportSession::AccessOverlaySequence() noexcept { return OverlaySequence; }
ForceInspectorView& VectorFieldViewportSession::AccessForceInspectorView() noexcept { return InspectorView; }
ForcePresetBrowserPanel& VectorFieldViewportSession::AccessContentBrowser() noexcept { return ContentBrowser; }
OverlayCameraState& VectorFieldViewportSession::AccessCamera(uint32_t PaneIndex) noexcept { return Cameras[PaneIndex < 2u ? PaneIndex : 0u]; }
int32_t VectorFieldViewportSession::QuerySelectedWaypointIndex() const noexcept { return SelectedWaypointIdx; }
void VectorFieldViewportSession::ConfigureSelectedWaypointIndex(int32_t WaypointIdx) noexcept { SelectedWaypointIdx = WaypointIdx; }
uint32_t VectorFieldViewportSession::QueryMappedForceToken(OutlinerSlotToken OutlinerToken) const noexcept
{
    for (const auto& Src : AuthoredSources)
    {
        if (Src.OutlinerTokenId == OutlinerToken.Identifier)
        {
            return Src.UniqueToken;
        }
    }
    return 0u;
}
double VectorFieldViewportSession::QuerySimulationTimeSeconds() const noexcept { return SimulationTimeSec; }

} // namespace Frontier::Editor::Simulation
