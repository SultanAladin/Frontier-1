/*============================================================================================================================================
                                                             EDITORLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Editor-only authoring loop implementation

#include "Engine/Lifecycle/EditorLifecycle.h"
#include "Engine/GeometricRaster/Rasterization/ImGuiOverlayRasterization.h"
#include "Engine/SimulationSpace/ForceFields/FieldFormulaCompiler.h"
#include "Engine/SimulationSpace/ForceFields/ForceMotionPath.h"

#if FRONTIER_EDITOR_ENABLED

#include "Engine/Components/Controls/CapsuleButton.h"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Frontier::Lifecycle
{

namespace
{

[[nodiscard]] const char* QueryForceDomainLabel(SimulationSpace::ForceFields::ForceDomainCategory Domain) noexcept
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

[[nodiscard]] SimulationSpace::ForceFields::ForceSourceRecordEntry BuildEditorForcePreset(
    uint32_t         ForceModelOrdinal,
    std::string_view Title)
{
    using namespace SimulationSpace::ForceFields;
    ForceSourceRecordEntry Desc{};
    Desc.RadiusMeters          = 18.0f;
    Desc.Strength              = 16.0f;
    Desc.FalloffExponent       = 1.35f;
    Desc.CentreMeters          = { 0.0f, 0.0f, 2.4f };
    Desc.EvaluatedCentreMeters = Desc.CentreMeters;
    Desc.Label                 = std::string(Title);

    if (ForceModelOrdinal < static_cast<uint32_t>(ForceModel::Count))
    {
        Desc.Model = static_cast<ForceModel>(ForceModelOrdinal);
    }
    else if (Title.find("Thermal") != std::string_view::npos || Title.find("Updraft") != std::string_view::npos)
    {
        Desc.Model = ForceModel::Buoyancy;
    }
    else if (Title.find("Cyclone") != std::string_view::npos || Title.find("Vortex") != std::string_view::npos)
    {
        Desc.Model      = ForceModel::RotationalColumn;
        Desc.SwirlRatio = 1.25f;
    }
    else if (Title.find("Containment") != std::string_view::npos)
    {
        Desc.Model            = ForceModel::Containment;
        Desc.CoreRadiusMeters = 6.0f;
    }
    else if (Title.find("Curl") != std::string_view::npos || Title.find("Turbulent") != std::string_view::npos)
    {
        Desc.Model                 = ForceModel::CurlNoise;
        Desc.TurbulenceScaleMeters = 10.0f;
        Desc.OctaveCount           = 3u;
    }
    else if (Title.find("Attractor") != std::string_view::npos)
    {
        Desc.Model            = ForceModel::Attractor;
        Desc.CoreRadiusMeters = 2.8f;
    }
    else if (Title.find("Spline") != std::string_view::npos || Title.find("Jetstream") != std::string_view::npos)
    {
        Desc.Model = ForceModel::SplineForce;
    }
    else if (Title.find("Lorentz") != std::string_view::npos || Title.find("Plasma") != std::string_view::npos)
    {
        Desc.Model      = ForceModel::LorentzField;
        Desc.SwirlRatio = 1.1f;
    }
    else if (Title.find("Formula") != std::string_view::npos)
    {
        Desc.Model             = ForceModel::FlowStream;
        Desc.FormulaEnabled    = true;
        Desc.FormulaSourceText = QueryBuiltInFormulaPresets()[0].FormulaSource;
    }
    else
    {
        Desc.Model          = ForceModel::LinearWind;
        Desc.EulerDegrees.Z = 228.0f;
    }

    Desc.Domain = QueryDomainForModel(Desc.Model);
    return Desc;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                LIFECYCLE IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

EditorLifecycle::~EditorLifecycle() noexcept
{
    Finalize();
}

bool EditorLifecycle::Initialize(const EditorStartupConfiguration& Configuration) noexcept
{
    Finalize();

    StartupConfig        = Configuration;
    TerminationRequested = false;
    CurrentStepSeconds   = 1.0 / 60.0;

    if (!DockContainer.Initialize(StartupConfig.WindowDescriptor))
    {
        return false;
    }

    InputParticipant.Initialize();

    if (!Workspaces.Initialize(false))
    {
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    WorkspaceDocumentDescriptor DefaultDocument{};
    DefaultDocument.DocumentTitle   = "Scene";
    DefaultDocument.OutlinerVariant = StartupConfig.OutlinerHeading;
    DefaultDocument.InitialMode     = ViewportExecutionMode::Edit;
    if (!Workspaces.OpenSession(DefaultDocument).Valid())
    {
        Workspaces.Finalize();
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (StartupConfig.ProjectSpacePath.empty())
    {
        ControlCentre.AccessDraftAppearance().Theme = ThemePreset::Dark;
        ControlCentre.ApplyDraftAppearanceImmediate();
        ControlCentre.AccessNotificationStream().ClearPending();
    }
    else
    {
        (void)ControlCentre.AttachProjectSpace(StartupConfig.ProjectSpacePath);
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        ImGuiIO& Io = ImGui::GetIO();
        Io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        if (!StartupConfig.FontRepositoryRoot.empty() && Typography.LoadRepository(StartupConfig.FontRepositoryRoot) > 0u)
        {
            TypographyRepository::AssignCurrent(&Typography);
            ControlCentre.AttachTypefaces(&Typography);
            if (ImFont* TabFont = Typography.QueryTabStripFont())
            {
                Io.FontDefault = TabFont;
            }
        }

        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    ControlCentre.ConfigureWorkspaceCallbacks(
        [this](bool Allowed) noexcept
        {
            DockContainer.ConfigureDetachedWindowsAllowed(Allowed);
        },
        [this]() noexcept
        {
            DockContainer.ResetPrimaryLayout();
        });
    DockContainer.ConfigureDetachedWindowsAllowed(ControlCentre.QueryDetachedWindowsAllowed());

    const char* LeftPaneTitle = (StartupConfig.OutlinerHeading == OutlinerTitleVariant::LayerStack) ? "LayerStack" : "Outliner";

    Components::WorkspaceDockPaneDescriptor LeftDesc{};
    LeftDesc.PaneTitle         = LeftPaneTitle;
    LeftDesc.InitialDirection  = Components::DockPlacementDirection::SplitLeft;
    LeftDesc.InitialSplitRatio = 0.21f;
    LeftDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    LeftDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Outliner.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    OutlinerPaneToken = DockContainer.RegisterPane(LeftDesc);

    Components::WorkspaceDockPaneDescriptor CenterDesc{};
    CenterDesc.PaneTitle         = "Viewport";
    CenterDesc.InitialDirection  = Components::DockPlacementDirection::CenterTabGroup;
    CenterDesc.InitialSplitRatio = 0.58f;
    CenterDesc.Directives        = Components::PaneDirectiveMask::CentralViewportDirectives;
    CenterDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Viewport.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    ViewportPaneToken = DockContainer.RegisterPane(CenterDesc);

    Components::WorkspaceDockPaneDescriptor RightDesc{};
    RightDesc.PaneTitle         = "Inspector";
    RightDesc.InitialDirection  = Components::DockPlacementDirection::SplitRight;
    RightDesc.InitialSplitRatio = 0.21f;
    RightDesc.Directives        = Components::PaneDirectiveMask::StandardPaneDirectives;
    RightDesc.ContentPresenter  = [this]()
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            Panes->Inspector.PresentInImGuiPane(CurrentStepSeconds, ControlCentre.QueryInterfaceScaleFactor());
        }
    };
    InspectorPaneToken = DockContainer.RegisterPane(RightDesc);

    if (!EditorSequence.Initialize())
    {
        Workspaces.Finalize();
        InputParticipant.Finalize();
        DockContainer.Finalize();
        return false;
    }

    if (ImGui::GetCurrentContext() != nullptr)
    {
        Components::ConfigureImGuiWorkspaceStyle(DockContainer.ActiveTabMetrics());
        Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    }

    Initialized = true;
    AttachForceFieldAuthoringToActivePanes();
    if (StartupConfig.VulkanDevice != nullptr && !StartupConfig.CompiledShaderDirectory.empty())
    {
        (void)AttachVulkanGizmoOverlay(StartupConfig.VulkanDevice, StartupConfig.CompiledShaderDirectory);
    }
    if (StartupConfig.PopulateShowcaseScene)
    {
        PopulateShowcaseGallerySession();
    }
    return true;
}

void EditorLifecycle::AttachForceFieldAuthoringToActivePanes()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    ForcePresetCatalog.PopulateDefaultForcePresets();
    AuthoredForces.clear();
    AnemometerHistory.fill(0.0f);
    ForceClockSeconds = 0.0;
    NextForceToken    = 1u;

    std::vector<OutlinerConstructTemplate> Templates;
    Templates.reserve(28u);
    Templates.push_back({ "Add Box Geometry",      "Box Specimen",      "Geometry", "Box · OpenPBR Surface",      ConstructSlotCategory::GeometryVolume,   0xFFFFFFFFu });
    Templates.push_back({ "Add Sphere Geometry",   "Sphere Specimen",   "Geometry", "Sphere · OpenPBR Surface",   ConstructSlotCategory::GeometryVolume,   0xFFFFFFFFu });
    Templates.push_back({ "Add Cylinder Geometry", "Cylinder Specimen", "Geometry", "Cylinder · OpenPBR Surface", ConstructSlotCategory::GeometryVolume,   0xFFFFFFFFu });
    Templates.push_back({ "Add Cone Geometry",     "Cone Specimen",     "Geometry", "Cone · OpenPBR Surface",     ConstructSlotCategory::GeometryVolume,   0xFFFFFFFFu });
    Templates.push_back({ "Add Torus Geometry",    "Torus Specimen",    "Geometry", "Torus · OpenPBR Surface",    ConstructSlotCategory::GeometryVolume,   0xFFFFFFFFu });
    Templates.push_back({ "Add Area Light",        "Area Quad Light",   "Lighting", "LTC Polygonal Quad Light",   ConstructSlotCategory::PointLuminaire, 0xFFFFFFFFu });
    Templates.push_back({ "Add Scene Camera",      "Scene Camera",      "Camera",   "Perspective Optical Camera", ConstructSlotCategory::OpticsCamera,  0xFFFFFFFFu });
    for (uint32_t Ord = 0u; Ord < static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::Count); ++Ord)
    {
        const auto  Model   = static_cast<SimulationSpace::ForceFields::ForceModel>(Ord);
        const auto  Domain  = SimulationSpace::ForceFields::QueryDomainForModel(Model);
        const char* Name    = SimulationSpace::ForceFields::QueryForceModelDisplayName(Model);
        const char* DomName = QueryForceDomainLabel(Domain);
        Templates.push_back({
            std::string("Add ") + Name,
            Name,
            DomName,
            "Analytical ForceModel",
            ConstructSlotCategory::ForceField,
            Ord,
        });
    }
    Templates.push_back({
        "Add Custom Formula",
        "Custom Formula",
        "VectorFieldLang",
        "Symbolic GPU formula",
        ConstructSlotCategory::ForceField,
        static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::FlowStream),
    });
    Panes->Outliner.ConfigureConstructTemplates(std::move(Templates));

    std::vector<ViewportConstructPresetEntry>     ViewportPresets;
    std::vector<Codecs::ForceModelPresetDocument> CapturedPresetDocs;
    for (const auto& Asset : ForcePresetCatalog.QueryAllAssets())
    {
        if (Asset.Category == ContentAssetCategory::ForcePresets || Asset.Category == ContentAssetCategory::FormulaPresets)
        {
            ViewportPresets.push_back({
                Asset.Title,
                Asset.CategoryCaption,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Model),
                Asset.FormulaOverride,
                static_cast<uint32_t>(Asset.PresetDocument.ForceSource.Domain),
            });
            CapturedPresetDocs.push_back(Asset.PresetDocument);
        }
    }
    Panes->Viewport.ConfigureForceOverlayToolsVisible(true);
    Panes->Viewport.ConfigureConstructPresets(std::move(ViewportPresets));
    Panes->Viewport.ConfigureConstructPresetCallback(
        [this, CapturedPresetDocs = std::move(CapturedPresetDocs)](uint32_t PresetIdx)
        {
            if (PresetIdx < CapturedPresetDocs.size())
            {
                (void)SpawnForceFieldEntry(CapturedPresetDocs[PresetIdx].ForceSource);
            }
        });
    Panes->Viewport.ConfigureOpenContentBrowserCallback(
        [this]()
        {
            ContentBrowserDrawer.AccessPanel().ConfigureOriginFilter(ContentOriginFilter::All);
            (void)ContentBrowserDrawer.AccessPanel().ActivateCategory(ContentCategory::Simulations);
            ContentBrowserDrawer.AccessPanel().ConfigureShelfExtent(ContentShelfExtent::SplitHalf, false);
        });

    Panes->Inspector.ConfigureFullOverrideCallback(
        [this](PixelSpace& Surface, Rectangle2D Bounds, OutlinerPanel* OutlinerPtr, OutlinerRecordEntry* PrimaryEntry, const PaneInputReading& Input, double Δt) -> bool
        {
            if (PrimaryEntry == nullptr)
            {
                return false;
            }
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr)
            {
                return false;
            }
            if (PrimaryEntry->Category != ConstructSlotCategory::ForceField)
            {
                return false;
            }
            SimulationSpace::ForceFields::ForceSourceRecordEntry* ActiveForce = nullptr;
            for (auto& Item : AuthoredForces)
            {
                if (Item.OutlinerSlot == PrimaryEntry->Token)
                {
                    ActiveForce = &Item.Source;
                    break;
                }
            }
            if (ActiveForce == nullptr)
            {
                MappedEditorForceSource Synthesized{};
                Synthesized.OutlinerSlot       = PrimaryEntry->Token;
                Synthesized.Source             = BuildEditorForcePreset(PrimaryEntry->ForceModelOrdinal, PrimaryEntry->Title);
                Synthesized.Source.UniqueToken = NextForceToken++;
                AuthoredForces.push_back(std::move(Synthesized));
                ActiveForce = &AuthoredForces.back().Source;
            }

            Editor::Simulation::ForceInspectorViewTargets Targets{};
            Targets.Outliner          = OutlinerPtr != nullptr ? OutlinerPtr : &ActivePanes->Outliner;
            Targets.OutlinerEntry     = PrimaryEntry;
            Targets.Accumulator       = &ForceAccumulator;
            Targets.LatticeCascade    = &ForceLatticeCascade;
            Targets.ActiveForce       = ActiveForce;
            Targets.OverlaySettings   = &ActivePanes->Viewport.AccessForceOverlaySettings();
            Targets.AnemometerSamples = AnemometerHistory;
            Targets.SimulationTimeSec = ForceClockSeconds;
            Targets.SelectedWaypoint  = &SelectedWaypointIdx;

            const bool Changed = ForceInspector.Render(Surface, Bounds, Targets, Input, Δt);
            if (Changed)
            {
                PrimaryEntry->ForceModelOrdinal      = static_cast<uint32_t>(ActiveForce->Model);
                PrimaryEntry->ForceFormulaOverride = ActiveForce->FormulaEnabled;
                PrimaryEntry->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(ActiveForce->Model);
                char SubBuf[96]{};
                std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", ActiveForce->RadiusMeters, ActiveForce->Strength);
                PrimaryEntry->Subtitle  = SubBuf;
                PrimaryEntry->PositionX = ActiveForce->CentreMeters.X;
                PrimaryEntry->PositionY = ActiveForce->CentreMeters.Y;
                PrimaryEntry->PositionZ = ActiveForce->CentreMeters.Z;
                PrimaryEntry->RotationX = ActiveForce->EulerDegrees.X;
                PrimaryEntry->RotationY = ActiveForce->EulerDegrees.Y;
                PrimaryEntry->RotationZ = ActiveForce->EulerDegrees.Z;
                SynchronizeForceFieldsWithOutliner(0.0);
            }
            return true;
        });

    ContentBrowserDrawer.AccessPanel().AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            const auto Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);
            if (Category == ContentCategory::Simulations)
            {
                for (const auto& PresetAsset : ForcePresetCatalog.QueryAllAssets())
                {
                    if (PresetAsset.Title == Entry.DisplayTitle)
                    {
                        (void)SpawnForceFieldEntry(PresetAsset.PresetDocument.ForceSource);
                        return;
                    }
                }
                const auto Built = BuildEditorForcePreset(0xFFFFFFFFu, Entry.DisplayTitle);
                (void)SpawnForceFieldEntry(Built);
            }
            else if (Category == ContentCategory::Geometry && ShowcaseBundle != nullptr)
            {
                if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
                {
                    GeometricRaster::ShowcasePrimitiveShape Shape = GeometricRaster::ShowcasePrimitiveShape::Box;
                    if (Entry.DisplayTitle.find("Sphere") != std::string::npos || Entry.DisplayTitle.find("Orb") != std::string::npos)
                    {
                        Shape = GeometricRaster::ShowcasePrimitiveShape::Sphere;
                    }
                    else if (Entry.DisplayTitle.find("Cylinder") != std::string::npos)
                    {
                        Shape = GeometricRaster::ShowcasePrimitiveShape::Cylinder;
                    }
                    else if (Entry.DisplayTitle.find("Cone") != std::string::npos)
                    {
                        Shape = GeometricRaster::ShowcasePrimitiveShape::Cone;
                    }
                    else if (Entry.DisplayTitle.find("Torus") != std::string::npos)
                    {
                        Shape = GeometricRaster::ShowcasePrimitiveShape::Torus;
                    }
                    const auto Pos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.72f, 0.36f);
                    const bool GpuActive = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
                    (void)GeometricRaster::ConstructShowcasePrimitiveInOutliner(
                        ActivePanes->Outliner,
                        *ShowcaseBundle,
                        Shape,
                        Pos,
                        !GpuActive);
                    InvalidateRtxLattice();
                    if (GpuActive)
                    {
                        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, true);
                    }
                    else if (ShowcaseRenderer != nullptr)
                    {
                        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                        RtxLatticePrepared = true;
                    }
                    RefreshShowcaseViewportRaster();
                }
            }
        });
}

OutlinerSlotToken EditorLifecycle::SpawnForceFieldEntry(
    const SimulationSpace::ForceFields::ForceSourceRecordEntry& SourcePreset)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return {};
    }

    if (!DefaultForcesFolder.Valid() || Panes->Outliner.FindEntry(DefaultForcesFolder) == nullptr)
    {
        DefaultForcesFolder = Panes->Outliner.ConstructFolder("Forces & VectorFields");
    }

    SimulationSpace::ForceFields::ForceSourceRecordEntry Instance = SourcePreset;
    Instance.UniqueToken = NextForceToken++;
    Instance.Domain      = SimulationSpace::ForceFields::QueryDomainForModel(Instance.Model);
    if (Instance.Label.empty())
    {
        Instance.Label = SimulationSpace::ForceFields::QueryForceModelDisplayName(Instance.Model);
    }

    const OutlinerSlotToken Slot = Panes->Outliner.ConstructFromTemplate(
        static_cast<uint32_t>(Instance.Model),
        DefaultForcesFolder);
    if (OutlinerRecordEntry* Record = Panes->Outliner.AccessEntry(Slot))
    {
        Record->Category             = ConstructSlotCategory::ForceField;
        Record->Title                = Instance.Label;
        Record->CategoryLabel        = SimulationSpace::ForceFields::QueryForceModelDisplayName(Instance.Model);
        Record->ForceModelOrdinal      = static_cast<uint32_t>(Instance.Model);
        Record->ForceFormulaOverride = Instance.FormulaEnabled;
        char SubBuf[96]{};
        std::snprintf(SubBuf, sizeof(SubBuf), "r %.0f m  |  %.1f m/s", Instance.RadiusMeters, Instance.Strength);
        Record->Subtitle  = SubBuf;
        Record->PositionX = Instance.CentreMeters.X;
        Record->PositionY = Instance.CentreMeters.Y;
        Record->PositionZ = Instance.CentreMeters.Z;
        Record->RotationX = Instance.EulerDegrees.X;
        Record->RotationY = Instance.EulerDegrees.Y;
        Record->RotationZ = Instance.EulerDegrees.Z;
    }

    Instance.OutlinerTokenId = Slot.Identifier;
    AuthoredForces.push_back(MappedEditorForceSource{ Slot, std::move(Instance) });
    Panes->Outliner.SelectSingle(Slot);
    Panes->Viewport.AccessForceOverlaySettings().Enabled = true;
    SynchronizeForceFieldsWithOutliner(0.0);
    return Slot;
}

void EditorLifecycle::SynchronizeForceFieldsWithOutliner(double Δt)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    const bool Advance =
        (Panes->Viewport.QueryExecutionMode() == ViewportExecutionMode::Simulate) ||
        (Panes->Viewport.QueryExecutionMode() == ViewportExecutionMode::Play && !Panes->Viewport.QueryPlayPaused());
    const bool OverlayActive = Panes->Viewport.QueryForceOverlaySettings().Enabled;
    if ((Advance || OverlayActive) && Δt > 0.0)
    {
        ForceClockSeconds += Δt;
    }
    Panes->Viewport.ConfigureSimulationTimeSeconds(ForceClockSeconds);

    // Adopt newly constructed ForceField rows created via Outliner '+' or Viewport Construct ('Ctrl+A')
    for (const OutlinerRecordEntry& Entry : Panes->Outliner.QueryEntries())
    {
        if (Entry.Category != ConstructSlotCategory::ForceField)
        {
            continue;
        }
        const bool AlreadyMapped = std::any_of(
            AuthoredForces.begin(),
            AuthoredForces.end(),
            [&](const MappedEditorForceSource& Item) { return Item.OutlinerSlot == Entry.Token; });
        if (!AlreadyMapped)
        {
            MappedEditorForceSource NewItem{};
            NewItem.OutlinerSlot           = Entry.Token;
            NewItem.Source                 = BuildEditorForcePreset(Entry.ForceModelOrdinal, Entry.Title);
            NewItem.Source.UniqueToken     = NextForceToken++;
            NewItem.Source.OutlinerTokenId = Entry.Token.Identifier;
            AuthoredForces.push_back(std::move(NewItem));
        }
    }

    // Prune deleted Outliner ForceField rows & sync transforms
    std::vector<SimulationSpace::ForceFields::ForceSourceRecordEntry> ActiveList;
    ActiveList.reserve(AuthoredForces.size());
    for (auto It = AuthoredForces.begin(); It != AuthoredForces.end();)
    {
        OutlinerRecordEntry* Record = Panes->Outliner.AccessEntry(It->OutlinerSlot);
        if (Record == nullptr)
        {
            It = AuthoredForces.erase(It);
            continue;
        }
        It->Source.Label          = Record->Title;
        It->Source.Enabled        = Panes->Outliner.QueryEffectiveVisibility(It->OutlinerSlot);
        It->Source.CentreMeters.X = Record->PositionX;
        It->Source.CentreMeters.Y = Record->PositionY;
        It->Source.CentreMeters.Z = Record->PositionZ;
        It->Source.EulerDegrees.X = Record->RotationX;
        It->Source.EulerDegrees.Y = Record->RotationY;
        It->Source.EulerDegrees.Z = Record->RotationZ;
        ActiveList.push_back(It->Source);
        ++It;
    }

    ForceAccumulator.AssignSources(ActiveList);
    ForceAccumulator.StepAnimation(ForceClockSeconds, Advance || OverlayActive);
    if (ShowcaseBundle != nullptr)
    {
        ShowcaseBundle->Scene.Atmosphere.ForceFieldSource   = &ForceAccumulator;
        ShowcaseBundle->Scene.Atmosphere.WindLatticeCascade = &ForceLatticeCascade;
    }

    const auto ProbePos = Panes->Viewport.QueryForceOverlaySettings().AnemometerWorldMeters;
    const auto Diag     = ForceAccumulator.QueryAnemometerDiagnostics(ProbePos, ForceClockSeconds);
    const float WindSpd = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.AdvectionVelocity);
    const float AccSpd  = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.LinearAcceleration);
    const float Speed   = (WindSpd > 0.01f) ? WindSpd : AccSpd;
    std::rotate(AnemometerHistory.begin(), AnemometerHistory.begin() + 1, AnemometerHistory.end());
    AnemometerHistory.back() = Speed;
    if (OverlayActive)
    {
        char ProbeBuf[128]{};
        std::snprintf(
            ProbeBuf,
            sizeof(ProbeBuf),
            "Wind %.1f m/s | %u sources",
            Speed,
            static_cast<unsigned>(ForceAccumulator.QueryActiveSourceCount()));
        Panes->Viewport.ConfigureProbeReadoutText(ProbeBuf);
    }
}

bool EditorLifecycle::AttachVulkanGizmoOverlay(
    GeometricRaster::VulkanInterchange* BoundDevice,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    if (BoundDevice == nullptr || !BoundDevice->Ready())
    {
        return false;
    }

    if (GpuWorldRenderer == nullptr || !GpuWorldRenderer->QueryReady())
    {
        auto CandidateWorldGpu = std::make_unique<GeometricRaster::VulkanWorldRenderSequence>();
        if (CandidateWorldGpu->InitializeSequence(*BoundDevice, CompiledShaderDirectory))
        {
            GpuWorldRenderer = std::move(CandidateWorldGpu);
            if (ShowcaseBundle != nullptr)
            {
                GpuWorldRenderer->SynchronizeSceneResources(
                    ShowcaseBundle->Scene,
                    (ShowcaseRenderer != nullptr && RtxLatticePrepared) ? &ShowcaseRenderer->InspectIrradianceLattice() : nullptr,
                    true);
            }
        }
    }

    if (WorldGizmoRasterizer == nullptr || !WorldGizmoRasterizer->QueryReady())
    {
        WorldGizmoRasterizer = std::make_unique<GeometricRaster::WorldGizmoOverlayRasterization>();
        if (!WorldGizmoRasterizer->InitializeOverlay(BoundDevice, CompiledShaderDirectory))
        {
            WorldGizmoRasterizer.reset();
            return false;
        }
    }

    if (ForceOverlayRasterizer == nullptr || !ForceOverlayRasterizer->QueryReady())
    {
        auto CandidateOverlay = std::make_unique<Editor::Simulation::VectorFieldOverlaySequence>();
        std::string OverlayDiag {};
        if (CandidateOverlay->Initialize(*BoundDevice, CompiledShaderDirectory, OverlayDiag))
        {
            ForceOverlayRasterizer = std::move(CandidateOverlay);
        }
        else
        {
            std::fprintf(
                stderr,
                "[FrontierEditor] WARNING: VectorFieldOverlaySequence unavailable: %s\n",
                OverlayDiag.c_str());
        }
    }

    WorldTransformGizmo.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
    if (ShowcaseBundle != nullptr)
    {
        RefreshShowcaseViewportRaster();
    }
    return true;
}

void EditorLifecycle::AssignReturnToProjectBrowserCallback(std::function<void()> Callback)
{
    ReturnToBrowserCallback = std::move(Callback);
}

void EditorLifecycle::ConfigureActiveProjectTitle(std::string_view ProjectTitle)
{
    ActiveProjectTitle = std::string(ProjectTitle);
    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        if (!ActiveProjectTitle.empty())
        {
            Panes->Viewport.ConfigureSceneTitle(ActiveProjectTitle + " · Cornell-Studio Gallery");
        }
    }
}

void EditorLifecycle::PopulateShowcaseGallerySession(uint32_t RenderWidthPx, uint32_t RenderHeightPx)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr)
    {
        return;
    }

    JoinRtxLatticeThread();
    ShowcaseBundle     = std::make_unique<GeometricRaster::ShowcaseWorldSceneBundle>();
    ShowcaseRenderer   = std::make_unique<GeometricRaster::TieredWorldRenderSequence>();
    InvalidateRtxLattice();
    CachedWorldRgba8.clear();
    CachedWorldWidthPx  = 0u;
    CachedWorldHeightPx = 0u;
    WorldTransformGizmo.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);

    const bool GpuActive = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    GeometricRaster::BuildShowcaseGalleryScene(*ShowcaseBundle, RenderWidthPx, RenderHeightPx, !GpuActive);
    {
        const auto& DisplayCfg = Panes->Viewport.QueryDisplaySettings();
        auto& Atmo = ShowcaseBundle->Scene.Atmosphere;
        Atmo.Medium.AtmosphereEnabled  = DisplayCfg.AtmosphereSkyEnabled;
        Atmo.Clouds.CloudsEnabled      = DisplayCfg.VolumetricCloudsEnabled;
        Atmo.Clouds.CloudFraction      = DisplayCfg.CloudFraction;
        Atmo.Clouds.CloudShadow.ShadowEnabled      = DisplayCfg.CloudShadowsEnabled;
        Atmo.Clouds.WindSpeedMetersPerSecond       = DisplayCfg.CloudWindSpeedMps;
        Atmo.Wind.SpeedMetersPerSecond             = std::max(Atmo.Wind.SpeedMetersPerSecond, DisplayCfg.CloudWindSpeedMps);
        Atmo.ForceFieldSource                      = &ForceAccumulator;
        Atmo.WindLatticeCascade                    = &ForceLatticeCascade;
        auto& OverlayInit                          = Panes->Viewport.AccessForceOverlaySettings();
        OverlayInit.Enabled                        = false;
        OverlayInit.CrossSectionOffsetMeters       = 0.25f;
        OverlayInit.AnemometerWorldMeters          = { 0.0f, 0.55f, 0.25f };
        LastForceOverlaySettings                   = OverlayInit;
        Atmo.Stars.StarfieldEnabled    = DisplayCfg.CelestialStarsEnabled;
        Atmo.Solar.SolarEnabled        = DisplayCfg.CelestialStarsEnabled;
        Atmo.Moons[0].Visible          = DisplayCfg.CelestialStarsEnabled;
        Atmo.Solar.CycleAnimationEnabled = DisplayCfg.DayCyclePlaying;
        Atmo.Solar.LocalHours          = DisplayCfg.SolarTimeOfDayHours;
        if (Atmo.Medium.AtmosphereEnabled)
        {
            ShowcaseRenderer->SynchronizeWorldAtmosphere(ShowcaseBundle->Scene);
        }
    }
    if (GpuActive)
    {
        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, true);
    }
    else
    {
        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
        RtxLatticePrepared = true;
    }
    GeometricRaster::RegisterShowcaseGalleryInOutliner(Panes->Outliner, *ShowcaseBundle, true);
    Panes->Viewport.ConfigureSceneTitle(
        ActiveProjectTitle.empty() ? "Cornell-Studio Gallery" : (ActiveProjectTitle + " · Cornell-Studio Gallery"));

    Panes->Viewport.ConfigureInteractionCallback(
        [this](uint32_t PaneIdx, Rectangle2D PaneBounds, const PaneInputReading& Input)
        {
            ProcessShowcaseViewportInteraction(PaneIdx, PaneBounds, Input);
        });
    Panes->Viewport.AssignNavigationCallback(
        [this](ViewportNavigationDirective Directive)
        {
            ProcessShowcaseNavigationDirective(Directive);
        });
    Panes->Viewport.ConfigureConstructCanvasDropCallback(
        [this](int32_t TemplateIndex, int32_t PresetIndex, Point2D CanvasLogicalPos)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            const Rectangle2D CanvasBounds = ActivePanes->Viewport.QueryLastCanvasPaneBounds();
            const float U = std::clamp((CanvasLogicalPos.X - CanvasBounds.X) / std::max(1.0f, CanvasBounds.Width), 0.0f, 1.0f);
            const float V = std::clamp((CanvasLogicalPos.Y - CanvasBounds.Y) / std::max(1.0f, CanvasBounds.Height), 0.0f, 1.0f);
            const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.36f);

            if (PresetIndex >= 0)
            {
                const auto& AllAssets = ForcePresetCatalog.QueryAllAssets();
                uint32_t MatchIdx = 0u;
                for (const auto& Asset : AllAssets)
                {
                    if (Asset.Category == ContentAssetCategory::ForcePresets || Asset.Category == ContentAssetCategory::FormulaPresets)
                    {
                        if (MatchIdx == static_cast<uint32_t>(PresetIndex))
                        {
                            SimulationSpace::ForceFields::ForceSourceRecordEntry Preset = Asset.PresetDocument.ForceSource;
                            Preset.CentreMeters = { DropPos.X, DropPos.Y, DropPos.Z + 0.65f };
                            (void)SpawnForceFieldEntry(Preset);
                            RefreshShowcaseViewportRaster();
                            return;
                        }
                        ++MatchIdx;
                    }
                }
            }

            if (TemplateIndex >= 0)
            {
                const auto Templates = ActivePanes->Outliner.QueryConstructTemplates();
                if (static_cast<std::size_t>(TemplateIndex) < Templates.size())
                {
                    const auto& Tpl = Templates[static_cast<std::size_t>(TemplateIndex)];
                    if (Tpl.Category == ConstructSlotCategory::ForceField)
                    {
                        auto ForceDesc = BuildEditorForcePreset(Tpl.ForceModelOrdinal, Tpl.DefaultTitle);
                        ForceDesc.CentreMeters = { DropPos.X, DropPos.Y, DropPos.Z + 0.65f };
                        (void)SpawnForceFieldEntry(ForceDesc);
                        RefreshShowcaseViewportRaster();
                        return;
                    }
                    const std::string_view Title = Tpl.DefaultTitle.empty() ? std::string_view(Tpl.DropdownLabel) : std::string_view(Tpl.DefaultTitle);
                    (void)GeometricRaster::ConstructShowcaseModelInOutliner(
                        ActivePanes->Outliner,
                        *ShowcaseBundle,
                        Title,
                        { 212u, 208u, 200u, 255u },
                        DropPos,
                        {},
                        true);
                    SynchronizeShowcaseRendererAfterEdit(true);
                }
            }
            else
            {
                (void)ActivePanes->Outliner.ConstructFolder("Folder");
            }
        });

    auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
    BrowserPanel.AttachOutlinerFallback(&Panes->Outliner);
    BrowserPanel.AssignContentActivationCallback(
        [this](const ContentRepositoryEntry& Entry)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            if (Entry.IndexHeader.MatCapPresetMarker != 0u)
            {
                ActivePanes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                    static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap);
                ActivePanes->Viewport.AccessDisplaySettings().DebugChannelIndex =
                    (Entry.IndexHeader.MatCapSlotOrdinal == 9u) ? 3u : 0u;
                RefreshShowcaseViewportRaster();
                return;
            }
            const Rectangle2D DropBounds = ContentBrowserDrawer.QueryPanel().MapPhysicalToLogicalBounds(
                ActivePanes->Viewport.QueryLastCanvasPhysicalBounds());
            HandleContentDropOnViewport(
                Entry,
                { DropBounds.X + DropBounds.Width * 0.5f, DropBounds.Y + DropBounds.Height * 0.62f },
                DropBounds);
        });

    SynchronizeShowcaseGizmoAnchor();
    RefreshShowcaseViewportRaster();
}

void EditorLifecycle::SynchronizeShowcaseGizmoAnchor() noexcept
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const OutlinerSlotToken PrimarySlot = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Entry    = PrimarySlot.Valid() ? Panes->Outliner.FindEntry(PrimarySlot) : nullptr;
    if (Entry == nullptr ||
        Entry->Category == ConstructSlotCategory::Folder ||
        Entry->Token == ShowcaseBundle->EditorCameraToken ||
        Entry->Token == ShowcaseBundle->SunEmitterToken ||
        Entry->Locked)
    {
        LastGizmoSlot = {};
        return;
    }

    const Mathematics::Axis3D Pivot{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
    const float DistToCam = std::sqrt(
        (Pivot.X - ShowcaseBundle->EditorCamera.CameraOrigin.X) * (Pivot.X - ShowcaseBundle->EditorCamera.CameraOrigin.X) +
        (Pivot.Y - ShowcaseBundle->EditorCamera.CameraOrigin.Y) * (Pivot.Y - ShowcaseBundle->EditorCamera.CameraOrigin.Y) +
        (Pivot.Z - ShowcaseBundle->EditorCamera.CameraOrigin.Z) * (Pivot.Z - ShowcaseBundle->EditorCamera.CameraOrigin.Z));
    const float WorldRad = std::clamp(DistToCam * 0.17f, 0.28f, 1.85f);
    WorldTransformGizmo.ConfigureAnchor(Pivot, WorldRad);
    LastGizmoSlot = PrimarySlot;
}

void EditorLifecycle::FrameShowcaseCameraSelection() noexcept
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const OutlinerSlotToken    Primary = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Entry   = Primary.Valid() ? Panes->Outliner.FindEntry(Primary) : nullptr;
    if (Entry != nullptr &&
        Entry->Category != ConstructSlotCategory::Folder &&
        Entry->Token != ShowcaseBundle->EditorCameraToken &&
        Entry->Token != ShowcaseBundle->SunEmitterToken)
    {
        const Mathematics::Axis3D TargetPos{ Entry->PositionX, Entry->PositionY, Entry->PositionZ };
        const float MaxExtent = std::max(0.45f, std::max({ std::fabs(Entry->ScaleX), std::fabs(Entry->ScaleY), std::fabs(Entry->ScaleZ) }));
        const Mathematics::Axis3D ViewOffset = Mathematics::NormalizeAxis(
            ShowcaseBundle->EditorCamera.CameraOrigin - TargetPos,
            { 0.0f, -1.0f, 0.12f });
        const float Dist = std::clamp(MaxExtent * 2.25f, 1.45f, 7.0f);
        ShowcaseBundle->EditorCamera.TargetPoint  = TargetPos;
        ShowcaseBundle->EditorCamera.CameraOrigin = TargetPos + ViewOffset * Dist;
    }
    else
    {
        ShowcaseBundle->EditorCamera.CameraOrigin = { 0.00f, -3.55f, 0.42f };
        ShowcaseBundle->EditorCamera.TargetPoint  = { 0.00f,  0.55f, 0.15f };
    }

    if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
    {
        CamEntry->PositionX = ShowcaseBundle->EditorCamera.CameraOrigin.X;
        CamEntry->PositionY = ShowcaseBundle->EditorCamera.CameraOrigin.Y;
        CamEntry->PositionZ = ShowcaseBundle->EditorCamera.CameraOrigin.Z;
    }
    SynchronizeShowcaseGizmoAnchor();
    PendingInteractionWorldRefresh = true;
}

void EditorLifecycle::ProcessShowcaseNavigationDirective(ViewportNavigationDirective Directive)
{
    if (ShowcaseBundle == nullptr)
    {
        return;
    }

    if (Directive == ViewportNavigationDirective::FitView)
    {
        FrameShowcaseCameraSelection();
        return;
    }

    const Mathematics::Axis3D ToTarget = ShowcaseBundle->EditorCamera.TargetPoint - ShowcaseBundle->EditorCamera.CameraOrigin;
    const float               CurDist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(ToTarget, ToTarget)));
    const Mathematics::Axis3D Fwd      = Mathematics::NormalizeAxis(ToTarget, { 0.0f, 1.0f, 0.0f });
    const float               NextDist = (Directive == ViewportNavigationDirective::ZoomIn)
                                             ? std::clamp(CurDist * 0.85f, 0.55f, 18.0f)
                                             : std::clamp(CurDist * 1.18f, 0.55f, 18.0f);
    ShowcaseBundle->EditorCamera.CameraOrigin = ShowcaseBundle->EditorCamera.TargetPoint - Fwd * NextDist;

    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
        {
            CamEntry->PositionX = ShowcaseBundle->EditorCamera.CameraOrigin.X;
            CamEntry->PositionY = ShowcaseBundle->EditorCamera.CameraOrigin.Y;
            CamEntry->PositionZ = ShowcaseBundle->EditorCamera.CameraOrigin.Z;
        }
    }
    SynchronizeShowcaseGizmoAnchor();
    PendingInteractionWorldRefresh = true;
}

void EditorLifecycle::ProcessShowcaseViewportInteraction(
    uint32_t                PaneIndex,
    Rectangle2D             PaneBounds,
    const PaneInputReading& Input)
{
    (void)PaneIndex;
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || PaneBounds.Width < 16.0f || PaneBounds.Height < 16.0f)
    {
        return;
    }

    ImGuiIO& Io = ImGui::GetIO();
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float DeltaX = PointerTrackingValid ? (Cursor.X - LastPointerPosition.X) : 0.0f;
    const float DeltaY = PointerTrackingValid ? (Cursor.Y - LastPointerPosition.Y) : 0.0f;
    LastPointerPosition  = Cursor;
    PointerTrackingValid = true;

    const float NormU = std::clamp((Cursor.X - PaneBounds.X) / PaneBounds.Width, 0.0f, 1.0f);
    const float NormV = std::clamp((Cursor.Y - PaneBounds.Y) / PaneBounds.Height, 0.0f, 1.0f);

    bool CameraMoved  = false;
    bool SceneChanged = false;

    auto& Cam = ShowcaseBundle->EditorCamera;
    Mathematics::Axis3D Fwd   = Mathematics::NormalizeAxis(Cam.TargetPoint - Cam.CameraOrigin, { 0.0f, 1.0f, 0.0f });
    Mathematics::Axis3D Right = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
    Mathematics::Axis3D Up    = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });

    // 1. Z-up (+Z) Camera Navigation: RMB mouse-look + WASD, MMB pan, Alt+LMB orbit, Scroll wheel dolly
    const bool RightHeld  = ImGui::IsMouseDown(ImGuiMouseButton_Right);
    const bool MiddleHeld = ImGui::IsMouseDown(ImGuiMouseButton_Middle);
    const bool AltHeld    = Io.KeyAlt;

    CameraInteractiveDrag = RightHeld || MiddleHeld || (AltHeld && Input.PrimaryPointer.Down) || WorldTransformGizmo.QueryDragging();

    if (RightHeld && !Io.KeyShift)
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const float Dist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(Cam.TargetPoint - Cam.CameraOrigin, Cam.TargetPoint - Cam.CameraOrigin)));
            float       Yaw   = std::atan2(Fwd.X, Fwd.Y) + DeltaX * 0.0042f;
            float       Pitch = std::asin(std::clamp(Fwd.Z, -0.98f, 0.98f)) - DeltaY * 0.0042f;
            Pitch             = std::clamp(Pitch, -1.42f, 1.42f);
            const float CosP  = std::cos(Pitch);
            Fwd               = Mathematics::NormalizeAxis({ std::sin(Yaw) * CosP, std::cos(Yaw) * CosP, std::sin(Pitch) }, Fwd);
            Cam.TargetPoint   = Cam.CameraOrigin + Fwd * Dist;
            Right             = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Fwd, Cam.WorldUpAxis), { 1.0f, 0.0f, 0.0f });
            Up                = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Right, Fwd), { 0.0f, 0.0f, 1.0f });
            CameraMoved       = true;
        }

        const float FlySpeed = (Io.KeyShift ? 6.0f : 2.8f) * static_cast<float>(std::max(0.001, CurrentStepSeconds));
        Mathematics::Axis3D MoveVec { 0.0f, 0.0f, 0.0f };
        if (ImGui::IsKeyDown(ImGuiKey_W)) MoveVec = MoveVec + Fwd * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) MoveVec = MoveVec - Fwd * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) MoveVec = MoveVec + Right * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) MoveVec = MoveVec - Right * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) MoveVec = MoveVec + Cam.WorldUpAxis * FlySpeed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) MoveVec = MoveVec - Cam.WorldUpAxis * FlySpeed;
        if (Mathematics::EvaluateDot(MoveVec, MoveVec) > 1.0e-8f)
        {
            Cam.CameraOrigin = Cam.CameraOrigin + MoveVec;
            Cam.TargetPoint  = Cam.TargetPoint + MoveVec;
            CameraMoved      = true;
        }
    }
    else if (MiddleHeld || (RightHeld && Io.KeyShift))
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const float PanScale = 0.0038f;
            const Mathematics::Axis3D PanOffset = Right * (-DeltaX * PanScale) + Up * (DeltaY * PanScale);
            Cam.CameraOrigin = Cam.CameraOrigin + PanOffset;
            Cam.TargetPoint  = Cam.TargetPoint + PanOffset;
            CameraMoved      = true;
        }
    }
    else if (AltHeld && Input.PrimaryPointer.Down)
    {
        if (std::fabs(DeltaX) > 0.01f || std::fabs(DeltaY) > 0.01f)
        {
            const Mathematics::Axis3D Offset = Cam.CameraOrigin - Cam.TargetPoint;
            const float Dist  = std::max(0.45f, std::sqrt(Mathematics::EvaluateDot(Offset, Offset)));
            float       Yaw   = std::atan2(Offset.X, Offset.Y) - DeltaX * 0.0055f;
            float       Pitch = std::asin(std::clamp(Offset.Z / Dist, -0.98f, 0.98f)) + DeltaY * 0.0055f;
            Pitch             = std::clamp(Pitch, -1.35f, 1.35f);
            const float CosP  = std::cos(Pitch);
            Cam.CameraOrigin  = Cam.TargetPoint + Mathematics::Axis3D{ std::sin(Yaw) * CosP * Dist, std::cos(Yaw) * CosP * Dist, std::sin(Pitch) * Dist };
            CameraMoved       = true;
        }
    }

    if (std::fabs(Input.WheelDeltaY) > 0.01f)
    {
        const Mathematics::Axis3D ToTarget = Cam.TargetPoint - Cam.CameraOrigin;
        const float               CurDist  = std::max(0.35f, std::sqrt(Mathematics::EvaluateDot(ToTarget, ToTarget)));
        const float               Step     = std::clamp(CurDist * 0.12f, 0.12f, 1.25f) * Input.WheelDeltaY;
        const float               NextDist = std::clamp(CurDist - Step, 0.50f, 20.0f);
        Cam.CameraOrigin                   = Cam.TargetPoint - Fwd * NextDist;
        CameraMoved                        = true;
    }

    // 2. Gizmo & Frame-Selection Keyboard Shortcuts (when not flying RMB and not typing in text fields)
    bool GizmoOverlayOnly = false;
    bool GizmoDragEnded   = false;
    if (!RightHeld && !Io.WantTextInput)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_W, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::TranslateOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::RotateOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::ScaleOnly);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_U, false))
        {
            WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);
            GizmoOverlayOnly = true;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_F, false))
        {
            FrameShowcaseCameraSelection();
            return;
        }
    }

    // 3. GPU Gizmo Hover / Drag & 3D Ray Picking
    const float Aspect  = static_cast<float>(std::max(1u, Cam.ViewportWidthPx)) / static_cast<float>(std::max(1u, Cam.ViewportHeightPx));
    const float TanHalf = std::tan(std::clamp(Cam.VerticalFovRadians, 0.10f, 2.60f) * 0.5f);
    const float NdcX    = (NormU * 2.0f - 1.0f) * Aspect * TanHalf;
    const float NdcY    = (1.0f - NormV * 2.0f) * TanHalf;
    const Editor::ViewportRay3D CursorRay{
        Cam.CameraOrigin,
        Mathematics::NormalizeAxis(Fwd + Right * NdcX + Up * NdcY, Fwd),
    };

    if (WorldTransformGizmo.QueryDragging())
    {
        if (Input.PrimaryPointer.Down && LastGizmoSlot.Valid())
        {
            Configuration::GizmoSnapSettings SnapCfg {};
            const auto Delta = WorldTransformGizmo.EvaluateDragInteraction(CursorRay, Io.KeyCtrl, SnapCfg, GizmoActiveTransform);
            if (Delta.TransformChanged)
            {
                if (OutlinerRecordEntry* SelectedEntry = Panes->Outliner.AccessEntry(LastGizmoSlot))
                {
                    SelectedEntry->PositionX = GizmoActiveTransform.Position.X;
                    SelectedEntry->PositionY = GizmoActiveTransform.Position.Y;
                    SelectedEntry->PositionZ = GizmoActiveTransform.Position.Z;
                    SelectedEntry->ScaleX    = std::max(0.08f, GizmoActiveTransform.Scale.X);
                    SelectedEntry->ScaleY    = std::max(0.08f, GizmoActiveTransform.Scale.Y);
                    SelectedEntry->ScaleZ    = std::max(0.08f, GizmoActiveTransform.Scale.Z);
                    SelectedEntry->RotationZ += Delta.RotationDegrees.Z;
                    WorldTransformGizmo.ConfigureAnchor(GizmoActiveTransform.Position, WorldTransformGizmo.QueryWorldRadius());
                    SceneChanged = true;
                }
            }
        }
        else
        {
            WorldTransformGizmo.EndDragInteraction();
            CameraInteractiveDrag = false;
            GizmoDragEnded        = true;
            SceneChanged          = true;
        }
    }
    else if (!AltHeld && !RightHeld && !MiddleHeld)
    {
        Editor::GizmoHandleOrdinal Hovered = Editor::GizmoHandleOrdinal::None;
        if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
        {
            const uint32_t PixX = static_cast<uint32_t>(NormU * static_cast<float>(std::max(1u, Cam.ViewportWidthPx) - 1u));
            const uint32_t PixY = static_cast<uint32_t>(NormV * static_cast<float>(std::max(1u, Cam.ViewportHeightPx) - 1u));
            Hovered             = WorldGizmoRasterizer->QueryLastGpuPickAtPixel(PixX, PixY);
        }
        if (Hovered != WorldTransformGizmo.QueryHoveredHandle())
        {
            WorldTransformGizmo.AssignHoveredHandle(Hovered);
            GizmoOverlayOnly = true;
        }

        if (Input.PrimaryPointer.Pressed)
        {
            if (Hovered != Editor::GizmoHandleOrdinal::None && LastGizmoSlot.Valid())
            {
                if (const OutlinerRecordEntry* SelectedEntry = Panes->Outliner.FindEntry(LastGizmoSlot))
                {
                    GizmoActiveTransform          = {};
                    GizmoActiveTransform.Position = { SelectedEntry->PositionX, SelectedEntry->PositionY, SelectedEntry->PositionZ };
                    GizmoActiveTransform.Scale    = { SelectedEntry->ScaleX, SelectedEntry->ScaleY, SelectedEntry->ScaleZ };
                    (void)WorldTransformGizmo.BeginDragInteraction(Hovered, CursorRay, GizmoActiveTransform, Right, Up);
                    CameraInteractiveDrag = true;
                    GizmoOverlayOnly      = true;
                }
            }
            else
            {
                const OutlinerSlotToken Picked = GeometricRaster::PickShowcaseGalleryEntryAtUv(
                    Panes->Outliner,
                    *ShowcaseBundle,
                    NormU,
                    NormV);
                if (Picked.Valid() && Picked != Panes->Outliner.QueryPrimarySelection())
                {
                    Panes->Outliner.SelectSingle(Picked);
                    SynchronizeShowcaseGizmoAnchor();
                    GizmoOverlayOnly = true;
                }
            }
        }
    }

    if (CameraMoved)
    {
        if (OutlinerRecordEntry* CamEntry = Panes->Outliner.AccessEntry(ShowcaseBundle->EditorCameraToken))
        {
            CamEntry->PositionX = Cam.CameraOrigin.X;
            CamEntry->PositionY = Cam.CameraOrigin.Y;
            CamEntry->PositionZ = Cam.CameraOrigin.Z;
        }
        SynchronizeShowcaseGizmoAnchor();
    }

    if (CameraMoved || SceneChanged)
    {
        PendingInteractionWorldRefresh = true;
        PendingInteractionSceneSync    = true;
        PendingInteractionFastDrag     = WorldTransformGizmo.QueryDragging() && !GizmoDragEnded;
    }
    else if (GizmoOverlayOnly)
    {
        PendingInteractionGizmoRefresh = true;
    }
}

void EditorLifecycle::SynchronizeShowcaseRendererAfterEdit(bool RebuildGeometry)
{
    if (ShowcaseBundle == nullptr)
    {
        return;
    }

    InvalidateRtxLattice();
    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
    {
        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, RebuildGeometry);
    }
    else if (ShowcaseRenderer != nullptr)
    {
        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
        RtxLatticePrepared = true;
    }
    SynchronizeShowcaseGizmoAnchor();
    RefreshShowcaseViewportRaster();
}

void EditorLifecycle::HandleContentDropOnViewport(
    const ContentRepositoryEntry& Entry,
    Point2D                       DropLogicalPos,
    Rectangle2D                   ViewportDropBounds)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const float U = std::clamp((DropLogicalPos.X - ViewportDropBounds.X) / std::max(1.0f, ViewportDropBounds.Width), 0.0f, 1.0f);
    const float V = std::clamp((DropLogicalPos.Y - ViewportDropBounds.Y) / std::max(1.0f, ViewportDropBounds.Height), 0.0f, 1.0f);
    const ColorRgba Tint { Entry.IndexHeader.TintRed, Entry.IndexHeader.TintGreen, Entry.IndexHeader.TintBlue, 255u };
    const ContentCategory Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);

    if (Category == ContentCategory::Geometry || Category == ContentCategory::Photometry)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.36f);
        (void)GeometricRaster::ConstructShowcaseModelInOutliner(
            Panes->Outliner,
            *ShowcaseBundle,
            Entry.DisplayTitle,
            Tint,
            DropPos,
            {},
            true);
        SynchronizeShowcaseRendererAfterEdit(true);
        return;
    }

    if (Category == ContentCategory::Materials && Entry.IndexHeader.MatCapPresetMarker == 0u)
    {
        OutlinerSlotToken Target = GeometricRaster::PickShowcaseGalleryEntryAtUv(Panes->Outliner, *ShowcaseBundle, U, V);
        if (!Target.Valid())
        {
            Target = Panes->Outliner.QueryPrimarySelection();
        }
        else
        {
            Panes->Outliner.SelectSingle(Target);
        }
        const MaterialSpace::SurfaceMaterial Mat = GeometricRaster::BuildMaterialFromContentPreset(Entry.DisplayTitle, Tint);
        if (GeometricRaster::AssignShowcaseEntryMaterial(Panes->Outliner, *ShowcaseBundle, Target, Mat, Entry.DisplayTitle))
        {
            if (Panes->Viewport.QueryDisplaySettings().RenderEngineIndex == static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap))
            {
                Panes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                    static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::SdfGlobalIllumination);
                Panes->Viewport.AccessDisplaySettings().DebugChannelIndex = 0u;
            }
            SynchronizeShowcaseRendererAfterEdit(false);
        }
        return;
    }

    if (Category == ContentCategory::Simulations)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, U, V, 0.65f);
        auto ForceDesc = BuildEditorForcePreset(0xFFFFFFFFu, Entry.DisplayTitle);
        ForceDesc.CentreMeters = { DropPos.X, DropPos.Y, DropPos.Z + 0.65f };
        (void)SpawnForceFieldEntry(ForceDesc);
        RefreshShowcaseViewportRaster();
    }
}

void EditorLifecycle::HandleContentDropOnOutliner(
    const ContentRepositoryEntry& Entry,
    Point2D                       DropLogicalPos,
    Rectangle2D                   OutlinerDropBounds)
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const Rectangle2D OutlinerPhys = Panes->Outliner.QueryLastPhysicalBounds();
    const float RelX = (DropLogicalPos.X - OutlinerDropBounds.X) / std::max(1.0f, OutlinerDropBounds.Width);
    const float RelY = (DropLogicalPos.Y - OutlinerDropBounds.Y) / std::max(1.0f, OutlinerDropBounds.Height);
    const Point2D PhysicalPt {
        OutlinerPhys.X + RelX * OutlinerPhys.Width,
        OutlinerPhys.Y + RelY * OutlinerPhys.Height,
    };

    OutlinerSlotToken HitSlot = Panes->Outliner.QuerySlotAtPhysicalPoint(PhysicalPt);
    const ColorRgba Tint { Entry.IndexHeader.TintRed, Entry.IndexHeader.TintGreen, Entry.IndexHeader.TintBlue, 255u };
    const ContentCategory Category = static_cast<ContentCategory>(Entry.IndexHeader.CategoryCode);

    if (Category == ContentCategory::Geometry || Category == ContentCategory::Photometry)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.62f, 0.36f);
        (void)GeometricRaster::ConstructShowcaseModelInOutliner(
            Panes->Outliner,
            *ShowcaseBundle,
            Entry.DisplayTitle,
            Tint,
            DropPos,
            HitSlot,
            true);
        SynchronizeShowcaseRendererAfterEdit(true);
        return;
    }

    if (Category == ContentCategory::Materials && Entry.IndexHeader.MatCapPresetMarker == 0u)
    {
        if (!HitSlot.Valid())
        {
            HitSlot = Panes->Outliner.QueryPrimarySelection();
        }
        else
        {
            Panes->Outliner.SelectSingle(HitSlot);
        }
        const MaterialSpace::SurfaceMaterial Mat = GeometricRaster::BuildMaterialFromContentPreset(Entry.DisplayTitle, Tint);
        if (GeometricRaster::AssignShowcaseEntryMaterial(Panes->Outliner, *ShowcaseBundle, HitSlot, Mat, Entry.DisplayTitle))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
        return;
    }

    if (Category == ContentCategory::Simulations)
    {
        const Mathematics::Axis3D DropPos = GeometricRaster::RaycastShowcasePlacementAtUv(*ShowcaseBundle, 0.50f, 0.58f, 0.65f);
        auto ForceDesc = BuildEditorForcePreset(0xFFFFFFFFu, Entry.DisplayTitle);
        ForceDesc.CentreMeters = { DropPos.X, DropPos.Y, DropPos.Z + 0.65f };
        const OutlinerSlotToken ForceSlot = SpawnForceFieldEntry(ForceDesc);
        if (HitSlot.Valid())
        {
            if (const OutlinerRecordEntry* TargetEntry = Panes->Outliner.FindEntry(HitSlot))
            {
                const OutlinerSlotToken FolderToken = (TargetEntry->Category == ConstructSlotCategory::Folder)
                                                          ? TargetEntry->Token
                                                          : TargetEntry->EnclosingToken;
                if (FolderToken.Valid())
                {
                    (void)Panes->Outliner.AttachToEnclosure(ForceSlot, FolderToken);
                }
            }
        }
        RefreshShowcaseViewportRaster();
    }
}

void EditorLifecycle::SynchronizeContentBrowserColourTargets()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
    const Rectangle2D ViewportPhys       = Panes->Viewport.QueryLastCanvasPhysicalBounds();
    const Rectangle2D ViewportDropBounds = BrowserPanel.MapPhysicalToLogicalBounds(ViewportPhys);
    const Rectangle2D OutlinerPhys       = Panes->Outliner.QueryLastPhysicalBounds();
    const Rectangle2D OutlinerDropBounds = BrowserPanel.MapPhysicalToLogicalBounds(OutlinerPhys);

    const OutlinerSlotToken    PrimarySlot = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Selected    = PrimarySlot.Valid() ? Panes->Outliner.FindEntry(PrimarySlot) : nullptr;
    const std::string          TargetLabel = (Selected != nullptr) ? (Selected->Title + " BaseColor") : std::string("Active World Surface");

    BrowserPanel.ClearColourSlots();
    ColourSlotRegistration WorldSlot {};
    WorldSlot.SlotIdentifier = "ActiveWorldSurfaceColour";
    WorldSlot.DisplayTitle   = TargetLabel;
    WorldSlot.ScreenBounds   = ViewportDropBounds;
    WorldSlot.NotifyColourAssigned = [this](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const OutlinerSlotToken Target = ActivePanes->Outliner.QueryPrimarySelection();
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    WorldSlot.NotifyColourAssignedAt = [this, ViewportDropBounds](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry, Point2D DropLogicalPos)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const float U = std::clamp((DropLogicalPos.X - ViewportDropBounds.X) / std::max(1.0f, ViewportDropBounds.Width), 0.0f, 1.0f);
        const float V = std::clamp((DropLogicalPos.Y - ViewportDropBounds.Y) / std::max(1.0f, ViewportDropBounds.Height), 0.0f, 1.0f);
        OutlinerSlotToken Target = GeometricRaster::PickShowcaseGalleryEntryAtUv(ActivePanes->Outliner, *ShowcaseBundle, U, V);
        if (Target.Valid())
        {
            ActivePanes->Outliner.SelectSingle(Target);
        }
        else
        {
            Target = ActivePanes->Outliner.QueryPrimarySelection();
        }
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    BrowserPanel.RegisterColourSlot(WorldSlot);

    ColourSlotRegistration OutlinerColourSlot {};
    OutlinerColourSlot.SlotIdentifier = "OutlinerSurfaceColour";
    OutlinerColourSlot.DisplayTitle   = "Outliner Row Colour";
    OutlinerColourSlot.ScreenBounds   = OutlinerDropBounds;
    OutlinerColourSlot.NotifyColourAssignedAt = [this, OutlinerDropBounds](ColorRgba AssignedColor, const ColourSwatchEntry& SwatchEntry, Point2D DropLogicalPos)
    {
        WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
        if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
        {
            return;
        }
        const Rectangle2D OutlinerPhysNow = ActivePanes->Outliner.QueryLastPhysicalBounds();
        const float RelX = (DropLogicalPos.X - OutlinerDropBounds.X) / std::max(1.0f, OutlinerDropBounds.Width);
        const float RelY = (DropLogicalPos.Y - OutlinerDropBounds.Y) / std::max(1.0f, OutlinerDropBounds.Height);
        const Point2D PhysicalPt {
            OutlinerPhysNow.X + RelX * OutlinerPhysNow.Width,
            OutlinerPhysNow.Y + RelY * OutlinerPhysNow.Height,
        };
        OutlinerSlotToken Target = ActivePanes->Outliner.QuerySlotAtPhysicalPoint(PhysicalPt);
        if (Target.Valid())
        {
            ActivePanes->Outliner.SelectSingle(Target);
        }
        else
        {
            Target = ActivePanes->Outliner.QueryPrimarySelection();
        }
        if (GeometricRaster::AssignShowcaseEntryBaseColor(
                ActivePanes->Outliner,
                *ShowcaseBundle,
                Target,
                AssignedColor,
                SwatchEntry.AliasName))
        {
            SynchronizeShowcaseRendererAfterEdit(false);
        }
    };
    BrowserPanel.RegisterColourSlot(OutlinerColourSlot);
    BrowserPanel.FocusColourSlot("ActiveWorldSurfaceColour");

    BrowserPanel.ClearMatCapSlots();
    BrowserPanel.RegisterMatCapSlot(MatCapSlotRegistration{
        "WorldViewportMatCap",
        "3D World MatCap",
        ViewportDropBounds,
        [this](uint8_t MatCapSlot, const ContentRepositoryEntry&)
        {
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr || ShowcaseBundle == nullptr)
            {
                return;
            }
            ActivePanes->Viewport.AccessDisplaySettings().RenderEngineIndex =
                static_cast<uint32_t>(GeometricRaster::WorldRenderEngine::MatCap);
            ActivePanes->Viewport.AccessDisplaySettings().DebugChannelIndex =
                (MatCapSlot == 9u) ? 3u : 0u;
            RefreshShowcaseViewportRaster();
        },
    });

    BrowserPanel.ClearContentDropZones();
    BrowserPanel.RegisterContentDropZone(ContentDropZoneRegistration{
        "world.viewport",
        "3D World Viewport",
        ViewportDropBounds,
        ContentCategory::All,
        [this, ViewportDropBounds](const ContentRepositoryEntry& Entry, Point2D DropLogicalPos)
        {
            HandleContentDropOnViewport(Entry, DropLogicalPos, ViewportDropBounds);
        },
    });
    BrowserPanel.RegisterContentDropZone(ContentDropZoneRegistration{
        "world.outliner",
        "World Outliner",
        OutlinerDropBounds,
        ContentCategory::All,
        [this, OutlinerDropBounds](const ContentRepositoryEntry& Entry, Point2D DropLogicalPos)
        {
            HandleContentDropOnOutliner(Entry, DropLogicalPos, OutlinerDropBounds);
        },
    });
}

bool EditorLifecycle::UpdateForceFieldOverlayCache(
    uint32_t WidthPx,
    uint32_t HeightPx,
    bool&    OutUpdated)
{
    OutUpdated = false;
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return false;
    }

    const auto& OverlayCfg = Panes->Viewport.QueryForceOverlaySettings();
    if (!OverlayCfg.Enabled || ForceOverlayRasterizer == nullptr || !ForceOverlayRasterizer->QueryReady())
    {
        return false;
    }

    const OutlinerSlotToken PrimarySel = Panes->Outliner.QueryPrimarySelection();
    const auto& Cam = ShowcaseBundle->EditorCamera;

    const auto NowClock = std::chrono::steady_clock::now();
    const double ElapsedOverlayMs = std::chrono::duration<double, std::milli>(NowClock - LastForceOverlayRenderAt).count();
    const float CamMove =
        Mathematics::EvaluateLength(Cam.CameraOrigin - LastForceOverlayCamOrigin) +
        Mathematics::EvaluateLength(Cam.TargetPoint  - LastForceOverlayCamTarget);
    const bool NeedsForceRender =
        ForceOverlayCacheDirty ||
        CachedForceOverlayW != WidthPx ||
        CachedForceOverlayH != HeightPx ||
        CachedForceOverlayRgba.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u ||
        CamMove > 1.0e-4f ||
        PrimarySel.Identifier != LastForceOverlaySelection ||
        ElapsedOverlayMs >= 33.0;

    if (!NeedsForceRender)
    {
        return true;
    }

    std::vector<SimulationSpace::ForceFields::ForceSourceShaderEntry> PackedSources;
    std::vector<SimulationSpace::ForceFields::SpatialVector3>         PackedWaypoints;
    ForceLatticeCascade.PackGpuSourceBuffers(ForceAccumulator, ForceClockSeconds, PackedSources, PackedWaypoints);

    const auto LiveSources = ForceAccumulator.QuerySources();
    std::vector<uint32_t> SourceTokens;
    std::vector<uint32_t> WaypointOffsets;
    std::vector<uint32_t> WaypointCounts;
    std::vector<uint8_t>  PathIsOrbit;

    if (PackedSources.empty())
    {
        // Transient wind-preview source derived from scene wind when no ForceField item has been authored yet
        const auto& WindCfg = ShowcaseBundle->Scene.Atmosphere.Wind;
        const float BearingRad = WindCfg.BearingDegrees * 0.0174532925f;
        const float WX         = std::cos(BearingRad);
        const float WY         = std::sin(BearingRad);
        const float WZ         = 0.0f;

        SimulationSpace::ForceFields::ForceSourceShaderEntry TransientWind{};
        TransientWind.CentreX          = 0.0f;
        TransientWind.CentreY          = 0.55f;
        TransientWind.CentreZ          = 0.45f;
        TransientWind.RadiusMeters     = 14.0f;
        TransientWind.DirectionX       = WX;
        TransientWind.DirectionY       = WY;
        TransientWind.DirectionZ       = WZ;
        TransientWind.Strength         = std::max(4.0f, WindCfg.SpeedMetersPerSecond);
        TransientWind.TangentLeftX     = -WY;
        TransientWind.TangentLeftY     = WX;
        TransientWind.TangentLeftZ     = 0.0f;
        TransientWind.FalloffExponent  = 0.25f;
        TransientWind.UpX              = 0.0f;
        TransientWind.UpY              = 0.0f;
        TransientWind.UpZ              = 1.0f;
        TransientWind.CoreRadiusMeters = 1.0f;
        TransientWind.HalfExtentX      = 12.0f;
        TransientWind.HalfExtentY      = 12.0f;
        TransientWind.HalfExtentZ      = 4.0f;
        TransientWind.TurbulenceScaleM = 4.0f;
        TransientWind.FrequencyHertz   = std::max(0.2f, WindCfg.GustFrequencyHz);
        TransientWind.ModelId          = static_cast<uint32_t>(SimulationSpace::ForceFields::ForceModel::LinearWind);
        TransientWind.AttenuationId    = static_cast<uint32_t>(SimulationSpace::ForceFields::ForceAttenuationMode::SmoothStep);
        TransientWind.EnvelopeId       = static_cast<uint32_t>(SimulationSpace::ForceFields::ReachEnvelopeCategory::Sphere);
        TransientWind.OctaveCount      = 3u;
        TransientWind.Seed             = 1337u;
        TransientWind.FlagMask         = 1u;
        TransientWind.TimeSeconds      = static_cast<float>(ForceClockSeconds);
        PackedSources.push_back(TransientWind);
        SourceTokens.push_back(0u);
        WaypointOffsets.push_back(0u);
        WaypointCounts.push_back(0u);
        PathIsOrbit.push_back(0u);
    }
    else
    {
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
    }

    Editor::Simulation::OverlayFrameRequest Req{};
    Req.PaneIndex              = 0u;
    Req.WidthPx                = WidthPx;
    Req.HeightPx               = HeightPx;
    Req.OverlaySettings        = OverlayCfg;
    Req.TimeSeconds            = ForceClockSeconds;
    Req.SelectedForceToken     = PrimarySel.Identifier;
    Req.SelectedWaypointIndex  = SelectedWaypointIdx;
    Req.LitShadingEnabled      = false;
    Req.TransparentBackdrop    = true;
    Req.UseExplicitCamera      = true;
    Req.ExplicitCameraOrigin   = { Cam.CameraOrigin.X, Cam.CameraOrigin.Y, Cam.CameraOrigin.Z };
    Req.ExplicitTargetPoint    = { Cam.TargetPoint.X,  Cam.TargetPoint.Y,  Cam.TargetPoint.Z };
    Req.ExplicitWorldUp        = { Cam.WorldUpAxis.X,  Cam.WorldUpAxis.Y,  Cam.WorldUpAxis.Z };
    Req.ExplicitVerticalFovDeg = Cam.VerticalFovRadians * 57.2957795f;
    Req.SliceHalfSpanMeters    = 10.0f;
    Req.PackedSources          = PackedSources;
    Req.SourceTokens           = SourceTokens;
    Req.PackedWaypoints        = PackedWaypoints;
    Req.SourceWaypointOffsets  = WaypointOffsets;
    Req.SourceWaypointCounts   = WaypointCounts;
    Req.SourcePathIsOrbit      = PathIsOrbit;

    std::string OverlayDiag {};
    const auto Out = ForceOverlayRasterizer->RenderPane(Req, OverlayDiag);
    const auto SrcRgba = ForceOverlayRasterizer->QueryColorReadbackRgba(0u);
    if (!Out.RenderedOnVulkan || Out.WidthPx != WidthPx || Out.HeightPx != HeightPx ||
        SrcRgba.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return false;
    }
    CachedForceOverlayRgba.assign(SrcRgba.begin(), SrcRgba.end());
    CachedForceOverlayW       = WidthPx;
    CachedForceOverlayH       = HeightPx;
    LastForceOverlayCamOrigin = Cam.CameraOrigin;
    LastForceOverlayCamTarget = Cam.TargetPoint;
    LastForceOverlaySelection = PrimarySel.Identifier;
    LastForceOverlayRenderAt  = NowClock;
    ForceOverlayCacheDirty    = false;
    OutUpdated                = true;
    return true;
}

void EditorLifecycle::CompositeForceFieldOverlayOntoBuffer(
    std::vector<uint8_t>& InOutRgba,
    uint32_t              WidthPx,
    uint32_t              HeightPx)
{
    if (WidthPx == 0u || HeightPx == 0u ||
        InOutRgba.size() < static_cast<std::size_t>(WidthPx) * HeightPx * 4u)
    {
        return;
    }

    bool ForceUpdated = false;
    if (!UpdateForceFieldOverlayCache(WidthPx, HeightPx, ForceUpdated))
    {
        return;
    }

    const auto& SrcRgba = CachedForceOverlayRgba;
    const std::size_t PixelCount = static_cast<std::size_t>(WidthPx) * HeightPx;
    for (std::size_t I = 0u; I < PixelCount; ++I)
    {
        const std::size_t Off = I * 4u;
        const uint32_t SA = SrcRgba[Off + 3u];
        if (SA == 0u)
        {
            continue;
        }
        const uint32_t InvA = 255u - SA;
        // Premultiplied alpha compositing over opaque world framebuffer
        for (std::size_t C = 0u; C < 3u; ++C)
        {
            const uint32_t Blended = static_cast<uint32_t>(SrcRgba[Off + C]) +
                                     ((static_cast<uint32_t>(InOutRgba[Off + C]) * InvA + 127u) / 255u);
            InOutRgba[Off + C] = static_cast<uint8_t>(std::min(255u, Blended));
        }
        InOutRgba[Off + 3u] = 255u;
    }
}

void EditorLifecycle::RefreshGizmoOverlayFromCachedWorld()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }

    const bool GpuReady = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    const bool UseGpuViewport =
        GpuReady &&
        StartupConfig.ImGuiPresenter != nullptr &&
        WorldGizmoRasterizer != nullptr &&
        WorldGizmoRasterizer->QueryReady() &&
        WorldGizmoRasterizer->QueryGpuCompositeSupported();

    if (UseGpuViewport)
    {
        if (LastWorldImageView == VK_NULL_HANDLE || LastTargetWidthPx == 0u || LastTargetHeightPx == 0u)
        {
            RefreshShowcaseViewportRaster();
            return;
        }

        SynchronizeShowcaseGizmoAnchor();
        const auto ForceStart = std::chrono::steady_clock::now();
        bool ForceUpdated = false;
        const bool ForceActive = UpdateForceFieldOverlayCache(LastTargetWidthPx, LastTargetHeightPx, ForceUpdated);
        const auto ForceEnd = std::chrono::steady_clock::now();
        LatestFrameTiming.ForceOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(ForceEnd - ForceStart).count());

        const Rectangle2D CanvasBounds = Panes->Viewport.QueryLastCanvasPaneBounds();
        const bool CursorInCanvas = PointerTrackingValid && CanvasBounds.Width >= 16.0f &&
                                    CanvasBounds.Height >= 16.0f && CanvasBounds.Contains(LastPointerPosition);
        uint32_t PickX = LastTargetWidthPx / 2u;
        uint32_t PickY = LastTargetHeightPx / 2u;
        if (CursorInCanvas)
        {
            const float U = std::clamp((LastPointerPosition.X - CanvasBounds.X) / CanvasBounds.Width, 0.0f, 1.0f);
            const float V = std::clamp((LastPointerPosition.Y - CanvasBounds.Y) / CanvasBounds.Height, 0.0f, 1.0f);
            PickX = static_cast<uint32_t>(U * static_cast<float>(std::max(1u, LastTargetWidthPx) - 1u));
            PickY = static_cast<uint32_t>(V * static_cast<float>(std::max(1u, LastTargetHeightPx) - 1u));
        }

        GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportCompositeRequest CompReq {};
        CompReq.WorldSourceView      = LastWorldImageView;
        CompReq.TargetWidthPx        = LastTargetWidthPx;
        CompReq.TargetHeightPx       = LastTargetHeightPx;
        CompReq.Camera               = ShowcaseBundle->EditorCamera;
        CompReq.Gizmo3D              = &WorldTransformGizmo;
        CompReq.DrawGizmo            = LastGizmoSlot.Valid();
        CompReq.ForceOverlayRgba     = ForceActive ? CachedForceOverlayRgba.data() : nullptr;
        CompReq.ForceOverlayWidthPx  = CachedForceOverlayW;
        CompReq.ForceOverlayHeightPx = CachedForceOverlayH;
        CompReq.ForceOverlayEnabled  = ForceActive;
        CompReq.ForceOverlayUpdated  = ForceUpdated;
        CompReq.RequestPickPatch     = !CameraInteractiveDrag && CursorInCanvas && LastGizmoSlot.Valid();
        CompReq.PickCursorX          = PickX;
        CompReq.PickCursorY          = PickY;

        GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportCompositeResult CompOut {};
        const auto GizmoStart = std::chrono::steady_clock::now();
        const bool CompOk = WorldGizmoRasterizer->CompositeOntoGpuViewport(CompReq, CompOut);
        const auto GizmoEnd = std::chrono::steady_clock::now();
        LatestFrameTiming.GizmoOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

        if (CompOk && CompOut.ColorImageView != VK_NULL_HANDLE)
        {
            const uint64_t NativeTex = StartupConfig.ImGuiPresenter->RegisterOrUpdateExternalImage(
                0x46525456503031ull,
                CompOut.ImageGeneration,
                CompOut.ColorImageView);
            if (NativeTex != 0ull)
            {
                Panes->Viewport.AssignCanvasGpuTexture(NativeTex, CompOut.WidthPx, CompOut.HeightPx);
                return;
            }
        }
    }

    if (CachedWorldRgba8.empty() || CachedWorldWidthPx == 0u || CachedWorldHeightPx == 0u)
    {
        RefreshShowcaseViewportRaster();
        return;
    }

    SynchronizeShowcaseGizmoAnchor();
    std::vector<uint8_t> ViewportRgba = CachedWorldRgba8;
    const auto ForceStart = std::chrono::steady_clock::now();
    CompositeForceFieldOverlayOntoBuffer(ViewportRgba, CachedWorldWidthPx, CachedWorldHeightPx);
    const auto ForceEnd = std::chrono::steady_clock::now();
    LatestFrameTiming.ForceOverlayMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(ForceEnd - ForceStart).count());

    const auto GizmoStart = std::chrono::steady_clock::now();
    if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
    {
        (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
            ViewportRgba,
            CachedWorldWidthPx,
            CachedWorldHeightPx,
            ShowcaseBundle->EditorCamera,
            WorldTransformGizmo);
    }
    const auto GizmoEnd = std::chrono::steady_clock::now();
    LatestFrameTiming.GizmoOverlayMs = static_cast<float>(
        std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());
    Panes->Viewport.AssignCanvasRasterImage(ViewportRgba.data(), CachedWorldWidthPx, CachedWorldHeightPx);
}

void EditorLifecycle::InvalidateRtxLattice() noexcept
{
    ++RtxLatticeGeneration;
    RtxLatticePrepared = false;
}

void EditorLifecycle::JoinRtxLatticeThread() noexcept
{
    if (RtxLatticeThread.joinable())
    {
        RtxLatticeThread.join();
    }
    RtxLatticeInFlight.store(false, std::memory_order_release);
    RtxLatticeReady.store(false, std::memory_order_release);
}

void EditorLifecycle::BeginAsyncRtxLatticeBake()
{
    if (ShowcaseBundle == nullptr || RtxLatticeInFlight.load(std::memory_order_acquire))
    {
        return;
    }
    if (RtxLatticeThread.joinable())
    {
        RtxLatticeThread.join();
    }

    GeometricRaster::TieredWorldRenderSequence::CompileBounceSummaries(ShowcaseBundle->Scene);
    const auto     ConfigCopy      = ShowcaseBundle->Scene.LatticeConfig;
    const auto     TreeCopy        = ShowcaseBundle->Scene.SceneTree;
    const auto     SummariesCopy   = ShowcaseBundle->Scene.BounceSummaries;
    const auto     SunCopy         = ShowcaseBundle->Scene.Sun;
    const auto     SkyCopy         = ShowcaseBundle->Scene.SkyRadiance;
    const uint64_t TargetGen       = RtxLatticeGeneration;

    RtxLatticeReady.store(false, std::memory_order_release);
    RtxLatticeInFlight.store(true, std::memory_order_release);

    RtxLatticeThread = std::thread(
        [this, ConfigCopy, TreeCopy, SummariesCopy, SunCopy, SkyCopy, TargetGen]()
        {
            GeometricRaster::IrradianceLatticeSequence Baked {};
            Baked.PropagateRtxIrradianceLattice(ConfigCopy, TreeCopy, SummariesCopy, SunCopy, SkyCopy);
            {
                std::lock_guard<std::mutex> Guard(RtxLatticeMutex);
                PendingRtxLattice    = std::move(Baked);
                PendingRtxLatticeGen = TargetGen;
            }
            RtxLatticeReady.store(true, std::memory_order_release);
            RtxLatticeInFlight.store(false, std::memory_order_release);
        });
}

bool EditorLifecycle::ConsumeCompletedRtxLatticeBake()
{
    if (!RtxLatticeReady.load(std::memory_order_acquire))
    {
        return false;
    }
    if (RtxLatticeThread.joinable())
    {
        RtxLatticeThread.join();
    }

    GeometricRaster::IrradianceLatticeSequence Completed {};
    uint64_t CompletedGen = 0ull;
    {
        std::lock_guard<std::mutex> Guard(RtxLatticeMutex);
        Completed    = std::move(PendingRtxLattice);
        CompletedGen = PendingRtxLatticeGen;
    }
    RtxLatticeReady.store(false, std::memory_order_release);

    if (ShowcaseRenderer != nullptr)
    {
        ShowcaseRenderer->AssignIrradianceLattice(std::move(Completed));
        if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
        {
            GpuWorldRenderer->UploadIrradianceLattice(ShowcaseRenderer->InspectIrradianceLattice());
        }
    }

    if (CompletedGen == RtxLatticeGeneration)
    {
        RtxLatticePrepared = true;
    }
    return true;
}

void EditorLifecycle::RefreshShowcaseViewportRaster()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr || ShowcaseRenderer == nullptr)
    {
        return;
    }

    const auto& DisplayCfg = Panes->Viewport.QueryDisplaySettings();
    const bool  GpuReady   = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
    const bool  HwGpu      = GpuReady && GpuWorldRenderer->QueryHardwareGpuActive();
    const bool  UseGpuViewport =
        GpuReady &&
        StartupConfig.ImGuiPresenter != nullptr &&
        WorldGizmoRasterizer != nullptr &&
        WorldGizmoRasterizer->QueryReady() &&
        WorldGizmoRasterizer->QueryGpuCompositeSupported();

    const uint32_t TraceResIdx = DisplayCfg.TraceResolutionIndex % 3u;
    const uint32_t TraceDiv    = (TraceResIdx == 2u) ? 4u : ((TraceResIdx == 1u) ? 2u : 1u);

    uint32_t TargetW = 320u;
    uint32_t TargetH = 190u;
    const Rectangle2D CanvasBounds = Panes->Viewport.QueryLastCanvasPaneBounds();
    if (CanvasBounds.Width >= 120.0f && CanvasBounds.Height >= 90.0f)
    {
        const float Aspect    = std::clamp(CanvasBounds.Width / CanvasBounds.Height, 0.5f, 3.2f);
        const float ScaleNorm = std::clamp(DisplayCfg.RenderScalePercent / 100.0f, 0.50f, 1.50f);
        if (HwGpu)
        {
            const float DesiredW = (UseGpuViewport ? 960.0f : (CameraInteractiveDrag ? 680.0f : 960.0f)) * ScaleNorm;
            TargetW              = static_cast<uint32_t>(std::clamp(std::round(DesiredW), 320.0f, 1600.0f));
        }
        else
        {
            const float DesiredW = (UseGpuViewport ? 384.0f : (CameraInteractiveDrag ? 280.0f : 384.0f)) * ScaleNorm;
            TargetW              = static_cast<uint32_t>(std::clamp(std::round(DesiredW), 192.0f, 640.0f));
        }
        TargetH = std::max(140u, static_cast<uint32_t>(std::round(static_cast<float>(TargetW) / Aspect)));
        TargetW = std::max(192u, static_cast<uint32_t>(std::round(static_cast<float>(TargetH) * Aspect)));
    }
    else if (HwGpu)
    {
        TargetW = 960u;
        TargetH = 640u;
    }

    const uint32_t TraceW = std::max(96u, (TargetW + TraceDiv - 1u) / TraceDiv);
    const uint32_t TraceH = std::max(70u, (TargetH + TraceDiv - 1u) / TraceDiv);

    ShowcaseBundle->EditorCamera.ViewportWidthPx  = UseGpuViewport ? TargetW : TraceW;
    ShowcaseBundle->EditorCamera.ViewportHeightPx = UseGpuViewport ? TargetH : TraceH;

    const uint32_t QTier = std::min(4u, DisplayCfg.QualityTier);
    constexpr std::array<uint32_t, 5u> TierBounces = {{ 2u, 3u, 4u, 5u, 6u }};
    constexpr std::array<uint32_t, 5u> TierSamples = {{ 1u, 1u, 1u, 2u, 2u }};

    GeometricRaster::WorldRenderConfiguration RenderConfig {};
    RenderConfig.Engine = static_cast<GeometricRaster::WorldRenderEngine>(std::min(4u, DisplayCfg.RenderEngineIndex));
    if (!DisplayCfg.RaytracingEnabled && RenderConfig.Engine == GeometricRaster::WorldRenderEngine::DdgiRaytracing)
    {
        RenderConfig.Engine = GeometricRaster::WorldRenderEngine::SdfGlobalIllumination;
    }
    if (!DisplayCfg.GlobalIllumination &&
        (RenderConfig.Engine == GeometricRaster::WorldRenderEngine::DdgiRaytracing ||
         RenderConfig.Engine == GeometricRaster::WorldRenderEngine::SdfGlobalIllumination))
    {
        RenderConfig.Engine = GeometricRaster::WorldRenderEngine::OpenPbrDirect;
    }
    RenderConfig.ShadowAlgorithm           = static_cast<GeometricRaster::SolarShadowAlgorithm>(std::min(4u, DisplayCfg.ShadowAlgorithmIndex));
    RenderConfig.ReflectionSource          = (DisplayCfg.ReflectionsEnabled && DisplayCfg.RaytracingEnabled && DisplayCfg.ReflectionSourceIndex == 0u)
        ? GeometricRaster::SpecularReflectionSource::WideTreeRaytrace
        : GeometricRaster::SpecularReflectionSource::SkyEnvironment;
    RenderConfig.DebugChannel              = DisplayCfg.DebugChannelIndex;
    RenderConfig.QualityTier               = QTier;
    RenderConfig.PathTraceMaxBounces       = TierBounces[QTier];
    RenderConfig.PathTraceSamples          = CameraInteractiveDrag ? 1u : (HwGpu ? TierSamples[QTier] : 1u);
    RenderConfig.PathTraceDenoise          = DisplayCfg.AntiAliasingEnabled;
    RenderConfig.SdfGiBilateral            = DisplayCfg.AntiAliasingEnabled;
    RenderConfig.GlobalIlluminationEnabled = DisplayCfg.GlobalIllumination;
    RenderConfig.ReflectionsEnabled        = DisplayCfg.ReflectionsEnabled;
    RenderConfig.RaytracingEnabled         = DisplayCfg.RaytracingEnabled;

    if (RenderConfig.Engine == GeometricRaster::WorldRenderEngine::DdgiRaytracing)
    {
        if (GpuReady && GpuWorldRenderer->QueryIrradianceLatticeUploaded())
        {
            RtxLatticePrepared = true;
        }
        else
        {
            (void)ConsumeCompletedRtxLatticeBake();
            if (!RtxLatticePrepared)
            {
                if (GpuReady)
                {
                    // Propagate multi-bounce DDGI lattice off the UI thread only when GPU lattice is absent
                    BeginAsyncRtxLatticeBake();
                }
                else
                {
                    ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                    RtxLatticePrepared = true;
                }
            }
        }
    }

    const float CamDelta =
        Mathematics::EvaluateLength(ShowcaseBundle->EditorCamera.CameraOrigin - LastPtCameraOrigin) +
        Mathematics::EvaluateLength(ShowcaseBundle->EditorCamera.TargetPoint  - LastPtCameraTarget);
    const bool ResetPathAccum =
        CamDelta > 1.0e-4f ||
        CameraInteractiveDrag ||
        TraceW != LastPtWidthPx ||
        TraceH != LastPtHeightPx ||
        TraceResIdx != LastPtTraceResIdx ||
        DisplayCfg.RenderEngineIndex != LastPtEngineIndex ||
        DisplayCfg.ShadowAlgorithmIndex != LastPtShadowAlg ||
        DisplayCfg.ReflectionSourceIndex != LastPtReflSrc ||
        DisplayCfg.DebugChannelIndex != LastPtDebugChan ||
        DisplayCfg.QualityTier != LastPtQualityTier ||
        DisplayCfg.GlobalIllumination != LastPtGi ||
        DisplayCfg.ReflectionsEnabled != LastPtRefl ||
        DisplayCfg.AntiAliasingEnabled != LastPtAa ||
        DisplayCfg.RaytracingEnabled != LastPtRt ||
        DisplayCfg.AtmosphereSkyEnabled != LastPtAtmoSky ||
        DisplayCfg.VolumetricCloudsEnabled != LastPtClouds ||
        DisplayCfg.CelestialStarsEnabled != LastPtCelestials ||
        std::fabs(DisplayCfg.SolarTimeOfDayHours - LastPtSolarHours) > 1.0e-3f ||
        std::fabs(DisplayCfg.CloudFraction - LastPtCloudFraction) > 1.0e-3f;
    if (ResetPathAccum)
    {
        PathTraceAccumIndex = 0u;
    }
    LastPtCameraOrigin   = ShowcaseBundle->EditorCamera.CameraOrigin;
    LastPtCameraTarget   = ShowcaseBundle->EditorCamera.TargetPoint;
    LastPtWidthPx        = TraceW;
    LastPtHeightPx       = TraceH;
    LastPtTraceResIdx    = TraceResIdx;
    LastPtEngineIndex    = DisplayCfg.RenderEngineIndex;
    LastPtShadowAlg      = DisplayCfg.ShadowAlgorithmIndex;
    LastPtReflSrc        = DisplayCfg.ReflectionSourceIndex;
    LastPtDebugChan      = DisplayCfg.DebugChannelIndex;
    LastPtQualityTier    = DisplayCfg.QualityTier;
    LastPtGi             = DisplayCfg.GlobalIllumination;
    LastPtRefl           = DisplayCfg.ReflectionsEnabled;
    LastPtAa             = DisplayCfg.AntiAliasingEnabled;
    LastPtRt             = DisplayCfg.RaytracingEnabled;
    LastPtAtmoSky        = DisplayCfg.AtmosphereSkyEnabled;
    LastPtClouds         = DisplayCfg.VolumetricCloudsEnabled;
    LastPtCelestials     = DisplayCfg.CelestialStarsEnabled;
    LastPtSolarHours     = DisplayCfg.SolarTimeOfDayHours;
    LastPtCloudFraction  = DisplayCfg.CloudFraction;

    std::vector<uint8_t> CleanWorldRgba {};
    uint32_t             OutWidthPx     = TraceW;
    uint32_t             OutHeightPx    = TraceH;
    bool                 RenderedOnGpu  = false;
    bool                 GpuCompositeDone = false;

    if (UseGpuViewport)
    {
        auto TraceCam = ShowcaseBundle->EditorCamera;
        TraceCam.ViewportWidthPx  = TraceW;
        TraceCam.ViewportHeightPx = TraceH;
        VkImageView WorldView = VK_NULL_HANDLE;
        uint32_t    TracedW   = 0u;
        uint32_t    TracedH   = 0u;
        if (GpuWorldRenderer->RenderWorldViewportToGpuImage(
                RenderConfig,
                PathTraceAccumIndex,
                TraceCam,
                ShowcaseBundle->Scene,
                WorldView,
                TracedW,
                TracedH) && WorldView != VK_NULL_HANDLE)
        {
            RenderedOnGpu = true;
            LastWorldImageView = WorldView;
            LastTargetWidthPx  = TargetW;
            LastTargetHeightPx = TargetH;

            const float SavedTickMs = LatestFrameTiming.EditorTickMs;
            LatestFrameTiming = GpuWorldRenderer->QueryLastTimingBreakdown();
            LatestFrameTiming.EditorTickMs = SavedTickMs;

            if (RenderConfig.Engine == GeometricRaster::WorldRenderEngine::DeterministicPathTrace && !CameraInteractiveDrag)
            {
                const uint32_t SamplesRan = HwGpu ? std::clamp(RenderConfig.PathTraceSamples, 1u, 4u) : 1u;
                PathTraceAccumIndex += SamplesRan;
            }

            const auto ForceStart = std::chrono::steady_clock::now();
            bool ForceUpdated = false;
            const bool ForceActive = UpdateForceFieldOverlayCache(TargetW, TargetH, ForceUpdated);
            const auto ForceEnd = std::chrono::steady_clock::now();
            LatestFrameTiming.ForceOverlayMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(ForceEnd - ForceStart).count());

            SynchronizeShowcaseGizmoAnchor();
            const bool CursorInCanvas = PointerTrackingValid && CanvasBounds.Width >= 16.0f &&
                                        CanvasBounds.Height >= 16.0f && CanvasBounds.Contains(LastPointerPosition);
            uint32_t PickX = TargetW / 2u;
            uint32_t PickY = TargetH / 2u;
            if (CursorInCanvas)
            {
                const float U = std::clamp((LastPointerPosition.X - CanvasBounds.X) / CanvasBounds.Width, 0.0f, 1.0f);
                const float V = std::clamp((LastPointerPosition.Y - CanvasBounds.Y) / CanvasBounds.Height, 0.0f, 1.0f);
                PickX = static_cast<uint32_t>(U * static_cast<float>(std::max(1u, TargetW) - 1u));
                PickY = static_cast<uint32_t>(V * static_cast<float>(std::max(1u, TargetH) - 1u));
            }

            GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportCompositeRequest CompReq {};
            CompReq.WorldSourceView      = WorldView;
            CompReq.TargetWidthPx        = TargetW;
            CompReq.TargetHeightPx       = TargetH;
            CompReq.Camera               = ShowcaseBundle->EditorCamera;
            CompReq.Gizmo3D              = &WorldTransformGizmo;
            CompReq.DrawGizmo            = LastGizmoSlot.Valid();
            CompReq.ForceOverlayRgba     = ForceActive ? CachedForceOverlayRgba.data() : nullptr;
            CompReq.ForceOverlayWidthPx  = CachedForceOverlayW;
            CompReq.ForceOverlayHeightPx = CachedForceOverlayH;
            CompReq.ForceOverlayEnabled  = ForceActive;
            CompReq.ForceOverlayUpdated  = ForceUpdated;
            CompReq.RequestPickPatch     = !CameraInteractiveDrag && CursorInCanvas && LastGizmoSlot.Valid();
            CompReq.PickCursorX          = PickX;
            CompReq.PickCursorY          = PickY;

            GeometricRaster::WorldGizmoOverlayRasterization::GpuViewportCompositeResult CompOut {};
            const auto GizmoStart = std::chrono::steady_clock::now();
            const bool CompOk = WorldGizmoRasterizer->CompositeOntoGpuViewport(CompReq, CompOut);
            const auto GizmoEnd = std::chrono::steady_clock::now();
            LatestFrameTiming.GizmoOverlayMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

            if (CompOk && CompOut.ColorImageView != VK_NULL_HANDLE)
            {
                const uint64_t NativeTex = StartupConfig.ImGuiPresenter->RegisterOrUpdateExternalImage(
                    0x46525456503031ull,
                    CompOut.ImageGeneration,
                    CompOut.ColorImageView);
                if (NativeTex != 0ull)
                {
                    Panes->Viewport.AssignCanvasGpuTexture(NativeTex, CompOut.WidthPx, CompOut.HeightPx);
                    GpuCompositeDone = true;
                }
            }
        }
    }
    else if (GpuReady)
    {
        RenderedOnGpu = GpuWorldRenderer->RenderWorldViewportWithEngine(
            RenderConfig,
            PathTraceAccumIndex,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene,
            CleanWorldRgba);
        if (RenderedOnGpu)
        {
            const float SavedTickMs = LatestFrameTiming.EditorTickMs;
            LatestFrameTiming = GpuWorldRenderer->QueryLastTimingBreakdown();
            LatestFrameTiming.EditorTickMs = SavedTickMs;
        }
        if (RenderedOnGpu && RenderConfig.Engine == GeometricRaster::WorldRenderEngine::DeterministicPathTrace && !CameraInteractiveDrag)
        {
            const uint32_t SamplesRan = HwGpu ? std::clamp(RenderConfig.PathTraceSamples, 1u, 4u) : 1u;
            PathTraceAccumIndex += SamplesRan;
        }
    }

    if (!RenderedOnGpu)
    {
        if (GpuReady)
        {
            std::fprintf(stderr, "[FrontierEditor] GPU world compute failed; skipping CPU fallback to protect desktop responsiveness.\n");
            return;
        }
        auto CpuOutput = ShowcaseRenderer->RenderWorldFrameWithEngine(
            RenderConfig,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene);
        OutWidthPx     = CpuOutput.WidthPx;
        OutHeightPx    = CpuOutput.HeightPx;
        CleanWorldRgba = std::move(CpuOutput.DisplayRgba8);
    }

    if (!GpuCompositeDone)
    {
        CachedWorldRgba8    = CleanWorldRgba;
        CachedWorldWidthPx  = OutWidthPx;
        CachedWorldHeightPx = OutHeightPx;

        const auto ForceStart = std::chrono::steady_clock::now();
        CompositeForceFieldOverlayOntoBuffer(CleanWorldRgba, OutWidthPx, OutHeightPx);
        const auto ForceEnd = std::chrono::steady_clock::now();
        LatestFrameTiming.ForceOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(ForceEnd - ForceStart).count());

        SynchronizeShowcaseGizmoAnchor();
        const auto GizmoStart = std::chrono::steady_clock::now();
        if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
        {
            (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
                CleanWorldRgba,
                OutWidthPx,
                OutHeightPx,
                ShowcaseBundle->EditorCamera,
                WorldTransformGizmo);
        }
        const auto GizmoEnd = std::chrono::steady_clock::now();
        LatestFrameTiming.GizmoOverlayMs = static_cast<float>(
            std::chrono::duration<double, std::milli>(GizmoEnd - GizmoStart).count());

        Panes->Viewport.AssignCanvasRasterImage(CleanWorldRgba.data(), OutWidthPx, OutHeightPx);
    }

    const float SunElevDeg = std::asin(std::clamp(ShowcaseBundle->Scene.Sun.DirectionToSun.Z, -1.0f, 1.0f)) * 57.2957795f;
    uint32_t    ActiveLtc  = 0u;
    for (const auto& Quad : ShowcaseBundle->Scene.AreaLights)
    {
        if (Quad.EmittedRadiance.R + Quad.EmittedRadiance.G + Quad.EmittedRadiance.B > 0.01f)
        {
            ++ActiveLtc;
        }
    }

    OutlinerFooterReadings Footer {};
    const char* EngineShortTag = "SDF GI";
    switch (RenderConfig.Engine)
    {
    case GeometricRaster::WorldRenderEngine::MatCap:                 EngineShortTag = "MatCap"; break;
    case GeometricRaster::WorldRenderEngine::OpenPbrDirect:          EngineShortTag = "OpenPBR"; break;
    case GeometricRaster::WorldRenderEngine::DdgiRaytracing:         EngineShortTag = "DDGI RT"; break;
    case GeometricRaster::WorldRenderEngine::SdfGlobalIllumination:  EngineShortTag = "SDF GI"; break;
    case GeometricRaster::WorldRenderEngine::DeterministicPathTrace: EngineShortTag = "PathTrace"; break;
    }
    if (RenderedOnGpu)
    {
        const float GpuMs  = std::max(0.2f, GpuWorldRenderer->QueryLastDispatchMilliseconds());
        const int   EstFps = std::clamp(static_cast<int>(std::lround(1000.0f / std::max(4.16f, GpuMs))), 1, 240);
        char FpsBuf[32]{};
        std::snprintf(FpsBuf, sizeof(FpsBuf), "%d fps", EstFps);
        Footer.RealtimeText = FpsBuf;
        Footer.QualityText  = std::string(HwGpu ? "GPU " : "VK ") + EngineShortTag;
    }
    else
    {
        Footer.RealtimeText = "CPU GI";
        Footer.QualityText  = std::string("CPU ") + EngineShortTag;
    }
    char SunBuf[32]{};
    std::snprintf(SunBuf, sizeof(SunBuf), "Sun %.0f°", std::max(0.0f, SunElevDeg));
    Footer.PrimarySlot = SunBuf;
    char LtcBuf[32]{};
    std::snprintf(LtcBuf, sizeof(LtcBuf), "LTC %u", ActiveLtc);
    Footer.SecondarySlot = LtcBuf;
    char CamBuffer[48]{};
    std::snprintf(
        CamBuffer,
        sizeof(CamBuffer),
        "%.1f, %.1f, %.1f",
        ShowcaseBundle->EditorCamera.CameraOrigin.X,
        ShowcaseBundle->EditorCamera.CameraOrigin.Y,
        ShowcaseBundle->EditorCamera.CameraOrigin.Z);
    Footer.CameraText = CamBuffer;
    Panes->Outliner.ConfigureFooterReadings(Footer);
}

bool EditorLifecycle::ExecuteExternalDesktopTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const auto TickStart = std::chrono::steady_clock::now();
    LatestFrameTiming.WorldDispatched = false;
    LatestFrameTiming.ForceOverlayMs  = 0.0f;
    LatestFrameTiming.GizmoOverlayMs  = 0.0f;
    LatestFrameTiming.SceneSyncGpuMs  = 0.0f;

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    Workspaces.Tick(SafeStep);
    SynchronizeForceFieldsWithOutliner(SafeStep);
    if (ShowcaseBundle != nullptr)
    {
        if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
        {
            const OutlinerSlotToken CurrentSel = Panes->Outliner.QueryPrimarySelection();
            const Rectangle2D       CanvasRect = Panes->Viewport.QueryLastCanvasPaneBounds();
            auto&                   DisplayCfg = Panes->Viewport.AccessDisplaySettings();
            const uint32_t          CurCanvasW = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Width)));
            const uint32_t          CurCanvasH = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Height)));
            const int32_t           CurScale   = static_cast<int32_t>(std::lround(DisplayCfg.RenderScalePercent));

            auto& Atmo = ShowcaseBundle->Scene.Atmosphere;
            if (DisplayCfg.DayCyclePlaying && SafeStep > 0.0)
            {
                Atmo.Solar.CycleAnimationEnabled = true;
                GeometricRaster::CelestialEphemerisSolver::AdvanceDayCycleHours(Atmo.Solar, SafeStep);
                DisplayCfg.SolarTimeOfDayHours = Atmo.Solar.LocalHours;
            }
            const bool ActivePathTraceFrozen =
                (DisplayCfg.RenderEngineIndex == 4u) && !DisplayCfg.DayCyclePlaying;
            const bool CloudsMoving =
                DisplayCfg.VolumetricCloudsEnabled &&
                Atmo.Clouds.FollowWindEnabled &&
                Atmo.Clouds.WindSpeedMetersPerSecond > 0.01f;
            const bool AtmoAnimTick =
                DisplayCfg.AtmosphereSkyEnabled &&
                !ActivePathTraceFrozen &&
                (DisplayCfg.DayCyclePlaying || CloudsMoving) &&
                SafeStep > 0.0;
            if (AtmoAnimTick)
            {
                Atmo.SimulationSeconds += static_cast<float>(SafeStep);
            }

            const bool ViewportTogglesChanged =
                DisplayCfg.AtmosphereSkyEnabled != LastAtmosphereSky ||
                DisplayCfg.VolumetricCloudsEnabled != LastCloudsEnabled ||
                DisplayCfg.CloudShadowsEnabled != LastCloudShadowsEnabled ||
                DisplayCfg.CelestialStarsEnabled != LastCelestialsEnabled ||
                std::fabs(DisplayCfg.SolarTimeOfDayHours - LastSolarTimeHours) > 1.0e-3f ||
                std::fabs(DisplayCfg.CloudFraction - LastCloudFraction) > 1.0e-3f ||
                std::fabs(DisplayCfg.CloudWindSpeedMps - LastCloudWindSpeedMps) > 1.0e-2f ||
                ForceAccumulator.QueryActiveSourceCount() != LastForceSourceCount;

            if (DisplayCfg.AtmosphereSkyEnabled != LastAtmosphereSky)
            {
                Atmo.Medium.AtmosphereEnabled = DisplayCfg.AtmosphereSkyEnabled;
            }
            if (DisplayCfg.VolumetricCloudsEnabled != LastCloudsEnabled)
            {
                Atmo.Clouds.CloudsEnabled = DisplayCfg.VolumetricCloudsEnabled;
            }
            if (DisplayCfg.CloudShadowsEnabled != LastCloudShadowsEnabled)
            {
                Atmo.Clouds.CloudShadow.ShadowEnabled = DisplayCfg.CloudShadowsEnabled;
            }
            if (std::fabs(DisplayCfg.CloudFraction - LastCloudFraction) > 1.0e-3f)
            {
                Atmo.Clouds.CloudFraction = DisplayCfg.CloudFraction;
            }
            if (std::fabs(DisplayCfg.CloudWindSpeedMps - LastCloudWindSpeedMps) > 1.0e-2f)
            {
                Atmo.Clouds.WindSpeedMetersPerSecond = DisplayCfg.CloudWindSpeedMps;
                Atmo.Wind.SpeedMetersPerSecond       = std::max(Atmo.Wind.SpeedMetersPerSecond, DisplayCfg.CloudWindSpeedMps);
            }
            if (DisplayCfg.CelestialStarsEnabled != LastCelestialsEnabled)
            {
                Atmo.Stars.StarfieldEnabled = DisplayCfg.CelestialStarsEnabled;
                Atmo.Solar.SolarEnabled     = DisplayCfg.CelestialStarsEnabled;
                Atmo.Moons[0].Visible       = DisplayCfg.CelestialStarsEnabled;
            }
            if (std::fabs(DisplayCfg.SolarTimeOfDayHours - LastSolarTimeHours) > 1.0e-3f)
            {
                Atmo.Solar.LocalHours = DisplayCfg.SolarTimeOfDayHours;
            }
            Atmo.Solar.CycleAnimationEnabled = DisplayCfg.DayCyclePlaying;

            const bool GpuReady = (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady());
            const bool AtmoChanged = ViewportTogglesChanged || AtmoAnimTick;
            const bool NeedCpuSkySync =
                ShowcaseRenderer != nullptr && Atmo.Medium.AtmosphereEnabled &&
                (ViewportTogglesChanged || (!GpuReady && (DisplayCfg.DayCyclePlaying || AtmoAnimTick)));
            if (NeedCpuSkySync)
            {
                ShowcaseRenderer->SynchronizeWorldAtmosphere(ShowcaseBundle->Scene);
            }
            else if (Atmo.Medium.AtmosphereEnabled && DisplayCfg.DayCyclePlaying)
            {
                const auto Solved = GeometricRaster::CelestialEphemerisSolver::SolveSolarEphemeris(Atmo.Solar);
                ShowcaseBundle->Scene.Sun.DirectionToSun = Solved.WorldDirectionToSun;
            }

            const ViewportProjectionPreset CurProjection = Panes->Viewport.QueryPrimaryProjection();
            bool ProjectionChanged = false;
            if (CurProjection != LastPrimaryProjection)
            {
                LastPrimaryProjection = CurProjection;
                ProjectionChanged     = true;
                auto& Cam             = ShowcaseBundle->EditorCamera;
                Cam.TargetPoint       = { 0.00f, 0.55f, 0.15f };
                Cam.WorldUpAxis       = { 0.00f, 0.00f, 1.00f };
                switch (CurProjection)
                {
                case ViewportProjectionPreset::Top:
                    Cam.CameraOrigin = { 0.00f, 0.50f, 5.20f };
                    Cam.WorldUpAxis  = { 0.00f, 1.00f, 0.00f };
                    break;
                case ViewportProjectionPreset::Front:
                    Cam.CameraOrigin = { 0.00f, -4.40f, 0.45f };
                    break;
                case ViewportProjectionPreset::Right:
                    Cam.CameraOrigin = { 4.60f, 0.55f, 0.45f };
                    break;
                case ViewportProjectionPreset::Perspective:
                case ViewportProjectionPreset::Orthographic:
                default:
                    Cam.CameraOrigin = { 0.00f, -3.55f, 0.42f };
                    break;
                }
            }

            const bool              DisplayChanged =
                (CurCanvasW >= 120u && CurCanvasH >= 90u &&
                 (std::abs(static_cast<int>(CurCanvasW) - static_cast<int>(LastCanvasPaneWidth)) > 8 ||
                  std::abs(static_cast<int>(CurCanvasH) - static_cast<int>(LastCanvasPaneHeight)) > 8)) ||
                DisplayCfg.QualityTier != LastViewportTier ||
                DisplayCfg.DebugChannelIndex != LastViewportDebug ||
                DisplayCfg.RenderEngineIndex != LastRenderEngine ||
                DisplayCfg.ShadowAlgorithmIndex != LastShadowAlgorithm ||
                DisplayCfg.ReflectionSourceIndex != LastReflectionSource ||
                DisplayCfg.TraceResolutionIndex != LastTraceResolutionIdx ||
                DisplayCfg.GlobalIllumination != LastGiEnabled ||
                DisplayCfg.ReflectionsEnabled != LastReflectionsEnabled ||
                DisplayCfg.AntiAliasingEnabled != LastAaEnabled ||
                DisplayCfg.RaytracingEnabled != LastRtEnabled ||
                AtmoChanged ||
                ProjectionChanged ||
                CurScale != LastRenderScalePct;

            const auto& CurOverlayCfg = Panes->Viewport.QueryForceOverlaySettings();
            const bool  ForceSettingsChanged =
                CurOverlayCfg.Enabled != LastForceOverlaySettings.Enabled ||
                CurOverlayCfg.ShowStreamlines != LastForceOverlaySettings.ShowStreamlines ||
                CurOverlayCfg.ShowVectors != LastForceOverlaySettings.ShowVectors ||
                CurOverlayCfg.ShowHeatmap != LastForceOverlaySettings.ShowHeatmap ||
                CurOverlayCfg.ShowBounds != LastForceOverlaySettings.ShowBounds ||
                CurOverlayCfg.CrossSectionAxis != LastForceOverlaySettings.CrossSectionAxis ||
                std::fabs(CurOverlayCfg.CrossSectionOffsetMeters - LastForceOverlaySettings.CrossSectionOffsetMeters) > 1.0e-3f ||
                CurOverlayCfg.StreamColorMode != LastForceOverlaySettings.StreamColorMode ||
                std::fabs(CurOverlayCfg.StreamOpacity - LastForceOverlaySettings.StreamOpacity) > 1.0e-3f ||
                CurOverlayCfg.StreamLineCount != LastForceOverlaySettings.StreamLineCount;
            if (ForceSettingsChanged)
            {
                ForceOverlayCacheDirty = true;
            }
            const bool ForceOverlayDirty = ForceSettingsChanged || (CurOverlayCfg.Enabled && SafeStep > 0.0);

            if (CurCanvasW >= 120u && CurCanvasH >= 90u)
            {
                LastCanvasPaneWidth  = CurCanvasW;
                LastCanvasPaneHeight = CurCanvasH;
            }
            LastViewportTier       = DisplayCfg.QualityTier;
            LastViewportDebug      = DisplayCfg.DebugChannelIndex;
            LastRenderEngine       = DisplayCfg.RenderEngineIndex;
            LastShadowAlgorithm    = DisplayCfg.ShadowAlgorithmIndex;
            LastReflectionSource   = DisplayCfg.ReflectionSourceIndex;
            LastTraceResolutionIdx = DisplayCfg.TraceResolutionIndex;
            LastGiEnabled          = DisplayCfg.GlobalIllumination;
            LastReflectionsEnabled = DisplayCfg.ReflectionsEnabled;
            LastAaEnabled          = DisplayCfg.AntiAliasingEnabled;
            LastRtEnabled          = DisplayCfg.RaytracingEnabled;
            LastAtmosphereSky      = DisplayCfg.AtmosphereSkyEnabled;
            LastCloudsEnabled      = DisplayCfg.VolumetricCloudsEnabled;
            LastCloudShadowsEnabled = DisplayCfg.CloudShadowsEnabled;
            LastCelestialsEnabled  = DisplayCfg.CelestialStarsEnabled;
            LastSolarTimeHours     = DisplayCfg.SolarTimeOfDayHours;
            LastCloudFraction      = DisplayCfg.CloudFraction;
            LastCloudWindSpeedMps  = DisplayCfg.CloudWindSpeedMps;
            LastForceOverlaySettings = CurOverlayCfg;
            LastForceSourceCount   = ForceAccumulator.QueryActiveSourceCount();
            LastRenderScalePct     = CurScale;

            const bool InteractionWorldRefresh = PendingInteractionWorldRefresh;
            const bool InteractionGizmoRefresh = PendingInteractionGizmoRefresh;
            const bool InteractionSceneSync    = PendingInteractionSceneSync;
            const bool InteractionFastDrag     = PendingInteractionFastDrag;
            PendingInteractionWorldRefresh = false;
            PendingInteractionGizmoRefresh = false;
            PendingInteractionSceneSync    = false;
            PendingInteractionFastDrag     = false;

            const bool LatticeUpdated = ConsumeCompletedRtxLatticeBake();
            bool       NeedsPrep = false;
            const bool SceneMutated = GeometricRaster::SynchronizeShowcaseGallerySceneLive(
                Panes->Outliner,
                *ShowcaseBundle,
                NeedsPrep,
                !GpuReady && !WorldTransformGizmo.QueryDragging() && !InteractionFastDrag);
            if (InteractionSceneSync && !NeedsPrep && !InteractionFastDrag && !WorldTransformGizmo.QueryDragging())
            {
                if (!GpuReady)
                {
                    GeometricRaster::RebuildShowcaseSceneAcceleration(*ShowcaseBundle, true);
                }
                NeedsPrep = true;
            }
            if (NeedsPrep)
            {
                PathTraceAccumIndex = 0u;
                InvalidateRtxLattice();
                if (GpuReady)
                {
                    GpuWorldRenderer->SynchronizeSceneResources(
                        ShowcaseBundle->Scene,
                        nullptr,
                        !WorldTransformGizmo.QueryDragging() && !InteractionFastDrag);
                }
                else if (ShowcaseRenderer != nullptr)
                {
                    ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                    RtxLatticePrepared = true;
                }
            }
            const bool ProgressivePathTraceTick =
                GpuReady && DisplayCfg.RenderEngineIndex == 4u && PathTraceAccumIndex < 64u && !CameraInteractiveDrag;
            const auto TickPrepEnd = std::chrono::steady_clock::now();
            LatestFrameTiming.EditorTickMs = static_cast<float>(
                std::chrono::duration<double, std::milli>(TickPrepEnd - TickStart).count());
            if (InteractionWorldRefresh || SceneMutated || DisplayChanged || LatticeUpdated || ProgressivePathTraceTick)
            {
                RefreshShowcaseViewportRaster();
            }
            else if (InteractionGizmoRefresh || CurrentSel != LastGizmoSlot || ForceOverlayDirty)
            {
                RefreshGizmoOverlayFromCachedWorld();
            }
        }
    }
    EditorSequence.Tick(SafeStep);
    return !TerminationRequested;
}

void EditorLifecycle::RecordFramePresentationTiming(
    float DesktopPollAndUiMs,
    float SwapchainPresentMs,
    float TotalFrameMs) noexcept
{
    ++TimingFrameCounter;
    LatestFrameTiming.FrameIndex         = TimingFrameCounter;
    LatestFrameTiming.DesktopPollAndUiMs = DesktopPollAndUiMs;
    LatestFrameTiming.SwapchainPresentMs = SwapchainPresentMs;
    LatestFrameTiming.TotalFrameMs       = TotalFrameMs;

    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady() && LatestFrameTiming.RenderWidthPx == 0u)
    {
        LatestFrameTiming.HardwareGpuActive = GpuWorldRenderer->QueryHardwareGpuActive();
        LatestFrameTiming.AdapterName       = std::string(GpuWorldRenderer->QueryAdapterTitle());
    }

    if (WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes())
    {
        Panes->Viewport.ConfigureFrameTimingBreakdown(LatestFrameTiming);
    }

    if (!TimingLogInitialized)
    {
        InitializeTimingLogFile(
            LatestFrameTiming.TimingLogPath,
            LatestFrameTiming.AdapterName,
            LatestFrameTiming.HardwareGpuActive);
        TimingLogInitialized = true;
    }

    const bool LogStartup = (TimingFrameCounter <= 5ull);
    const bool LogPeriodic = (TimingFrameCounter % 60ull) == 0ull;
    const bool LogSpike = (TotalFrameMs > 30.0f) || (LatestFrameTiming.SceneSyncGpuMs > 10.0f);
    if (LogStartup || LogPeriodic || LogSpike)
    {
        const char* Tag = LogStartup ? "INIT" : (LogSpike ? "SPIKE" : "SAMPLE");
        AppendTimingLogLine(LatestFrameTiming.TimingLogPath, LatestFrameTiming, Tag);
    }
}

bool EditorLifecycle::ExecuteAuthoringTick(double Δt)
{
    if (!Initialized || TerminationRequested)
    {
        return false;
    }

    const double MaxStep  = StartupConfig.MaximumStep > 0.0 ? StartupConfig.MaximumStep : 0.100;
    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? std::min(Δt, MaxStep) : 0.0;
    CurrentStepSeconds    = SafeStep;

    InputParticipant.BeginTick();
    DockContainer.PollOperatingWindows(InputParticipant);
    (void)InputParticipant.TickExchange();

    if (Windowing::HeadlessPlatformWindow* Primary = DockContainer.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
    {
        if (Primary->CloseRequested())
        {
            TerminationRequested = true;
        }
        else
        {
            (void)InputParticipant.FlushOutboundDirectives(*Primary);
        }
    }

    if (!TerminationRequested)
    {
        return ExecuteExternalDesktopTick(SafeStep);
    }

    return !TerminationRequested;
}

void EditorLifecycle::PresentPrimaryEditorImGui(double Δt)
{
    if (!Initialized || ImGui::GetCurrentContext() == nullptr)
    {
        return;
    }

    const double SafeStep = (std::isfinite(Δt) && Δt > 0.0) ? Δt : CurrentStepSeconds;
    CurrentStepSeconds    = SafeStep;

    Components::ConfigureImGuiWorkspaceTheme(DockContainer.ActiveTabMetrics(), ControlCentre.QueryThemeRepository());
    ContentBrowserDrawer.AttachThemeRepository(&ControlCentre.QueryThemeRepository());

    const float UiScale = std::clamp(ControlCentre.QueryInterfaceScaleFactor(), 0.75f, 2.0f);
    const Components::HostWindowReading HostReading = DockContainer.InspectHostWindow(Components::HostWindowToken{ 0u });
    const bool HostValid = HostReading.Active && HostReading.SurfaceExtent.PixelWidth > 0u && HostReading.SurfaceExtent.PixelHeight > 0u;
    const Rectangle2D Bounds = HostValid
                                   ? Rectangle2D{
                                         0.0f,
                                         0.0f,
                                         static_cast<float>(HostReading.SurfaceExtent.PixelWidth),
                                         static_cast<float>(HostReading.SurfaceExtent.PixelHeight),
                                     }
                                   : Rectangle2D{};

    if (HostValid)
    {
        auto& BrowserPanel = ContentBrowserDrawer.AccessPanel();
        BrowserPanel.SynchronizeHostBounds(Bounds, UiScale);
        ControlCentre.SynchronizeHostBounds(Bounds, UiScale);
        if (ControlCentre.QueryFullScreenBlocking() || BrowserPanel.QueryShelfDragging())
        {
            WorkspaceAppearance::AssignOverlayPointerShield(Bounds);
        }
        else
        {
            WorkspaceAppearance::AssignOverlayPointerShield(
                BrowserPanel.QueryDrawerPhysicalBounds(),
                BrowserPanel.QueryNotchPhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryShadePhysicalBounds());
            WorkspaceAppearance::AppendOverlayPointerShield(ControlCentre.QueryTabPhysicalBounds());
        }
    }

    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
    {
        ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
    }

    DockContainer.PresentWindowImGuiPanes(Components::HostWindowToken{ 0u });
    WorkspaceAppearance::ClearOverlayPointerShield();

    if (WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes())
    {
        ContentBrowserDrawer.SynchronizeWorkspaceTargets(&ActivePanes->Outliner, &ActivePanes->Inspector);
        SynchronizeContentBrowserColourTargets();
    }

    if (HostValid)
    {
        const float RefreshHz = HostReading.SurfaceExtent.RefreshRateHz > 0u
                                    ? static_cast<float>(HostReading.SurfaceExtent.RefreshRateHz)
                                    : 60.0f;
        ContentBrowserDrawer.Render(Bounds, SafeStep, UiScale);
        ControlCentre.Render(Bounds, SafeStep, RefreshHz);

        if (ReturnToBrowserCallback && !ControlCentre.QueryOpen())
        {
            PixelSpace TopOverlay(ImGui::GetForegroundDrawList(), Point2D{ 0.0f, 0.0f }, UiScale);
            const Point2D MouseLogical = TopOverlay.QueryMousePosition();
            ControlPointer Ptr {};
            Ptr.X        = MouseLogical.X;
            Ptr.Y        = MouseLogical.Y;
            Ptr.Down     = TopOverlay.QueryMouseDown(0);
            Ptr.Pressed  = TopOverlay.QueryMouseClicked(0);
            Ptr.Released = TopOverlay.QueryMouseReleased(0);

            const float LogicalW = Bounds.Width / UiScale;
            const Rectangle2D BtnBounds { std::max(320.0f, LogicalW * 0.5f + 98.0f), 3.0f, 82.0f, 23.0f };
            ButtonDescriptor Desc {};
            Desc.Label    = "Projects";
            Desc.Tone     = ButtonTone::Secondary;
            Desc.FontSize = 10.5f;
            Desc.PaddingX = 12.0f;
            if (CapsuleButton::Construct(TopOverlay, BtnBounds, Desc, Ptr).Pressed)
            {
                ReturnToBrowserCallback();
            }
        }
    }
}

bool EditorLifecycle::Suspend() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool OverlaySuspended   = EditorSequence.Suspend();
    const bool WorkspaceSuspended = Workspaces.Suspend();
    return OverlaySuspended && WorkspaceSuspended;
}

bool EditorLifecycle::Resume() noexcept
{
    if (!Initialized)
    {
        return false;
    }
    const bool WorkspaceResumed = Workspaces.Resume();
    const bool OverlayResumed   = EditorSequence.Resume();
    return WorkspaceResumed && OverlayResumed;
}

void EditorLifecycle::RequestTermination() noexcept
{
    TerminationRequested = true;
}

void EditorLifecycle::Finalize() noexcept
{
    JoinRtxLatticeThread();
    if (!Initialized)
    {
        EditorSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down GPU overlay hooks, then workspace sessions, then input, then dock windows.
    EditorSequence.Finalize();
    if (StartupConfig.ImGuiPresenter != nullptr)
    {
        StartupConfig.ImGuiPresenter->ReleaseExternalImages();
    }
    AuthoredForces.clear();
    DefaultForcesFolder = {};
    CachedWorldRgba8.clear();
    LastWorldImageView = VK_NULL_HANDLE;
    ForceOverlayRasterizer.reset();
    WorldGizmoRasterizer.reset();
    GpuWorldRenderer.reset();
    ShowcaseRenderer.reset();
    ShowcaseBundle.reset();
    Workspaces.Finalize();

    if (TypographyRepository::QueryCurrent() == &Typography)
    {
        TypographyRepository::AssignCurrent(nullptr);
    }

    InputParticipant.Finalize();
    DockContainer.Finalize();

    OutlinerPaneToken    = {};
    ViewportPaneToken    = {};
    InspectorPaneToken   = {};
    Initialized          = false;
    TerminationRequested = false;
}

EditorLifecycleReading EditorLifecycle::InspectReading() const noexcept
{
    EditorLifecycleReading Reading{};
    Reading.SequencePhase       = EditorSequence.QueryPhase();
    Reading.SequenceMetrics     = EditorSequence.InspectMetrics();
    Reading.ActiveWindowCount   = DockContainer.ActiveOperatingWindowCount();
    Reading.RegisteredPaneCount = static_cast<uint32_t>(DockContainer.RegisteredPaneCount());
    Reading.OpenWorkspaceCount  = Workspaces.OpenSessionCount();
    Reading.GpuOverlayCount     = RegisteredGpuOverlays;
    Reading.ControlCentreOpen   = ControlCentre.QueryOpen();
    Reading.ContentBrowserOpen  = ContentBrowserDrawer.QueryPanel().QueryOpen();
    Reading.AuthoringActive     = QueryActive();
    return Reading;
}

} // namespace Frontier::Lifecycle

#endif // FRONTIER_EDITOR_ENABLED
