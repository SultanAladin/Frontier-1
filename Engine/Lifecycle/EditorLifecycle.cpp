/*============================================================================================================================================
                                                             EDITORLIFECYCLE.CPP
============================================================================================================================================*/
// 🧩 Editor-only authoring loop implementation

#include "Engine/Lifecycle/EditorLifecycle.h"
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

    InputCoordinator.Initialize();

    if (!Workspaces.Initialize(false))
    {
        InputCoordinator.Finalize();
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
        InputCoordinator.Finalize();
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
        InputCoordinator.Finalize();
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
            if (PrimaryEntry == nullptr || PrimaryEntry->Category != ConstructSlotCategory::ForceField)
            {
                return false;
            }
            WorkspaceSessionPanes* ActivePanes = Workspaces.AccessActivePanes();
            if (ActivePanes == nullptr)
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
                    (void)GeometricRaster::ConstructShowcasePrimitiveInOutliner(ActivePanes->Outliner, *ShowcaseBundle, Shape, Pos);
                    RtxLatticePrepared = false;
                    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
                    {
                        GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, true);
                    }
                    else if (ShowcaseRenderer != nullptr)
                    {
                        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
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
    if (Advance && Δt > 0.0)
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
    ForceAccumulator.StepAnimation(ForceClockSeconds, Advance);

    const auto ProbePos = Panes->Viewport.QueryForceOverlaySettings().AnemometerWorldMeters;
    const auto Diag     = ForceAccumulator.QueryAnemometerDiagnostics(ProbePos, ForceClockSeconds);
    const float WindSpd = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.AdvectionVelocity);
    const float AccSpd  = SimulationSpace::ForceFields::VectorLength(Diag.TotalForce.LinearAcceleration);
    const float Speed   = (WindSpd > 0.01f) ? WindSpd : AccSpd;
    std::rotate(AnemometerHistory.begin(), AnemometerHistory.begin() + 1, AnemometerHistory.end());
    AnemometerHistory.back() = Speed;
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

    ShowcaseBundle     = std::make_unique<GeometricRaster::ShowcaseWorldSceneBundle>();
    ShowcaseRenderer   = std::make_unique<GeometricRaster::TieredWorldRenderSequence>();
    RtxLatticePrepared = false;
    CachedWorldRgba8.clear();
    CachedWorldWidthPx  = 0u;
    CachedWorldHeightPx = 0u;
    WorldTransformGizmo.ConfigureDimensionMode(Editor::GizmoDimensionMode::Spatial3D);
    WorldTransformGizmo.ConfigureHandleFilter(Editor::GizmoHandleFilter::Universal);

    GeometricRaster::BuildShowcaseGalleryScene(*ShowcaseBundle, RenderWidthPx, RenderHeightPx);
    if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
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
    RefreshShowcaseViewportRaster();
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
    RefreshShowcaseViewportRaster();
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
        const bool FastDrag = WorldTransformGizmo.QueryDragging();
        bool NeedsPrep      = false;
        (void)GeometricRaster::SynchronizeShowcaseGallerySceneLive(Panes->Outliner, *ShowcaseBundle, NeedsPrep, !FastDrag);
        if (GizmoDragEnded)
        {
            GeometricRaster::RebuildShowcaseSceneAcceleration(*ShowcaseBundle, true);
            NeedsPrep = true;
        }
        if (NeedsPrep)
        {
            RtxLatticePrepared = false;
            if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
            {
                GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, !FastDrag);
            }
            else if (ShowcaseRenderer != nullptr)
            {
                ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                RtxLatticePrepared = true;
            }
        }
        RefreshShowcaseViewportRaster();
    }
    else if (GizmoOverlayOnly)
    {
        RefreshGizmoOverlayFromCachedWorld();
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
    BrowserPanel.ClearColourSlots();

    const OutlinerSlotToken    PrimarySlot = Panes->Outliner.QueryPrimarySelection();
    const OutlinerRecordEntry* Selected    = PrimarySlot.Valid() ? Panes->Outliner.FindEntry(PrimarySlot) : nullptr;
    const std::string          TargetLabel = (Selected != nullptr) ? (Selected->Title + " BaseColor") : std::string("Active World Surface");

    ColourSlotRegistration WorldSlot {};
    WorldSlot.SlotIdentifier = "ActiveWorldSurfaceColour";
    WorldSlot.DisplayTitle   = TargetLabel;
    WorldSlot.ScreenBounds   = BrowserPanel.MapPhysicalToLogicalBounds(Panes->Viewport.QueryLastCanvasPaneBounds());
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
            RtxLatticePrepared = false;
            if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
            {
                GpuWorldRenderer->SynchronizeSceneResources(ShowcaseBundle->Scene, nullptr, false);
            }
            else if (ShowcaseRenderer != nullptr)
            {
                ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                RtxLatticePrepared = true;
            }
            RefreshShowcaseViewportRaster();
        }
    };
    BrowserPanel.RegisterColourSlot(WorldSlot);
    BrowserPanel.FocusColourSlot("ActiveWorldSurfaceColour");
}

void EditorLifecycle::RefreshGizmoOverlayFromCachedWorld()
{
    WorkspaceSessionPanes* Panes = Workspaces.AccessActivePanes();
    if (Panes == nullptr || ShowcaseBundle == nullptr)
    {
        return;
    }
    if (CachedWorldRgba8.empty() || CachedWorldWidthPx == 0u || CachedWorldHeightPx == 0u)
    {
        RefreshShowcaseViewportRaster();
        return;
    }

    SynchronizeShowcaseGizmoAnchor();
    std::vector<uint8_t> ViewportRgba = CachedWorldRgba8;
    if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
    {
        (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
            ViewportRgba,
            CachedWorldWidthPx,
            CachedWorldHeightPx,
            ShowcaseBundle->EditorCamera,
            WorldTransformGizmo);
    }
    Panes->Viewport.AssignCanvasRasterImage(ViewportRgba.data(), CachedWorldWidthPx, CachedWorldHeightPx);
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

    const Rectangle2D CanvasBounds = Panes->Viewport.QueryLastCanvasPaneBounds();
    if (CanvasBounds.Width >= 120.0f && CanvasBounds.Height >= 90.0f)
    {
        const float Aspect    = std::clamp(CanvasBounds.Width / CanvasBounds.Height, 0.5f, 3.2f);
        const float ScaleNorm = std::clamp(DisplayCfg.RenderScalePercent / 100.0f, 0.50f, 1.25f);
        uint32_t    RasterW   = 384u;
        if (HwGpu)
        {
            const float TargetW = (CameraInteractiveDrag ? 680.0f : 960.0f) * ScaleNorm;
            RasterW             = static_cast<uint32_t>(std::clamp(std::round(TargetW), 420.0f, 1280.0f));
        }
        else if (GpuReady)
        {
            RasterW = CameraInteractiveDrag ? 280u : 384u;
        }
        else
        {
            RasterW = CameraInteractiveDrag ? 280u : 384u;
        }
        uint32_t RasterH = std::max(180u, static_cast<uint32_t>(std::round(static_cast<float>(RasterW) / Aspect)));
        RasterW          = std::max(240u, static_cast<uint32_t>(std::round(static_cast<float>(RasterH) * Aspect)));
        ShowcaseBundle->EditorCamera.ViewportWidthPx  = RasterW;
        ShowcaseBundle->EditorCamera.ViewportHeightPx = RasterH;
    }
    else if (!HwGpu)
    {
        ShowcaseBundle->EditorCamera.ViewportWidthPx  = 320u;
        ShowcaseBundle->EditorCamera.ViewportHeightPx = 190u;
    }

    const auto ActiveTier = (DisplayCfg.QualityTier >= 4u)
                                ? GeometricRaster::WorldRenderTier::Tier2RtxDeterministic
                                : GeometricRaster::WorldRenderTier::Tier1GtxCalibrated;

    if (ActiveTier == GeometricRaster::WorldRenderTier::Tier2RtxDeterministic && !RtxLatticePrepared)
    {
        ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
        RtxLatticePrepared = true;
        if (GpuReady)
        {
            GpuWorldRenderer->SynchronizeSceneResources(
                ShowcaseBundle->Scene,
                &ShowcaseRenderer->InspectIrradianceLattice(),
                false);
        }
    }

    std::vector<uint8_t> CleanWorldRgba {};
    uint32_t             OutWidthPx     = ShowcaseBundle->EditorCamera.ViewportWidthPx;
    uint32_t             OutHeightPx    = ShowcaseBundle->EditorCamera.ViewportHeightPx;
    bool                 RenderedOnGpu  = false;

    if (GpuReady)
    {
        RenderedOnGpu = GpuWorldRenderer->RenderWorldViewportRgba8(
            ActiveTier,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene,
            DisplayCfg.DebugChannelIndex,
            CleanWorldRgba);
    }

    if (!RenderedOnGpu)
    {
        auto CpuOutput = ShowcaseRenderer->RenderWorldFrame(
            ActiveTier,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->EditorCamera,
            ShowcaseBundle->Scene);
        OutWidthPx     = CpuOutput.WidthPx;
        OutHeightPx    = CpuOutput.HeightPx;
        CleanWorldRgba = std::move(CpuOutput.DisplayRgba8);
    }

    CachedWorldRgba8    = CleanWorldRgba;
    CachedWorldWidthPx  = OutWidthPx;
    CachedWorldHeightPx = OutHeightPx;

    SynchronizeShowcaseGizmoAnchor();
    if (WorldGizmoRasterizer != nullptr && WorldGizmoRasterizer->QueryReady() && LastGizmoSlot.Valid())
    {
        (void)WorldGizmoRasterizer->RasterizeGizmoOntoWorldBuffer(
            CleanWorldRgba,
            OutWidthPx,
            OutHeightPx,
            ShowcaseBundle->EditorCamera,
            WorldTransformGizmo);
    }

    Panes->Viewport.AssignCanvasRasterImage(CleanWorldRgba.data(), OutWidthPx, OutHeightPx);

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
    if (RenderedOnGpu)
    {
        const float GpuMs  = std::max(0.2f, GpuWorldRenderer->QueryLastDispatchMilliseconds());
        const int   EstFps = std::clamp(static_cast<int>(std::lround(1000.0f / std::max(4.16f, GpuMs))), 1, 240);
        char FpsBuf[32]{};
        std::snprintf(FpsBuf, sizeof(FpsBuf), "%d fps", EstFps);
        Footer.RealtimeText = FpsBuf;
        Footer.QualityText  = (ActiveTier == GeometricRaster::WorldRenderTier::Tier2RtxDeterministic)
                                  ? (HwGpu ? "GPU RTX Tier 2" : "VK RTX Tier 2")
                                  : (HwGpu ? "GPU GTX Tier 1" : "VK GTX Tier 1");
    }
    else
    {
        Footer.RealtimeText = "CPU GI";
        Footer.QualityText  = (ActiveTier == GeometricRaster::WorldRenderTier::Tier2RtxDeterministic) ? "RTX Tier 2" : "GTX Tier 1";
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
            const auto&             DisplayCfg = Panes->Viewport.QueryDisplaySettings();
            const uint32_t          CurCanvasW = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Width)));
            const uint32_t          CurCanvasH = static_cast<uint32_t>(std::max(0.0f, std::round(CanvasRect.Height)));
            const int32_t           CurScale   = static_cast<int32_t>(std::lround(DisplayCfg.RenderScalePercent));
            const bool              DisplayChanged =
                (CurCanvasW >= 120u && CurCanvasH >= 90u &&
                 (std::abs(static_cast<int>(CurCanvasW) - static_cast<int>(LastCanvasPaneWidth)) > 8 ||
                  std::abs(static_cast<int>(CurCanvasH) - static_cast<int>(LastCanvasPaneHeight)) > 8)) ||
                DisplayCfg.QualityTier != LastViewportTier ||
                DisplayCfg.DebugChannelIndex != LastViewportDebug ||
                CurScale != LastRenderScalePct;

            if (CurCanvasW >= 120u && CurCanvasH >= 90u)
            {
                LastCanvasPaneWidth  = CurCanvasW;
                LastCanvasPaneHeight = CurCanvasH;
            }
            LastViewportTier   = DisplayCfg.QualityTier;
            LastViewportDebug  = DisplayCfg.DebugChannelIndex;
            LastRenderScalePct = CurScale;

            bool NeedsPrep = false;
            const bool SceneMutated = GeometricRaster::SynchronizeShowcaseGallerySceneLive(
                Panes->Outliner,
                *ShowcaseBundle,
                NeedsPrep,
                !WorldTransformGizmo.QueryDragging());
            if (NeedsPrep)
            {
                RtxLatticePrepared = false;
                if (GpuWorldRenderer != nullptr && GpuWorldRenderer->QueryReady())
                {
                    GpuWorldRenderer->SynchronizeSceneResources(
                        ShowcaseBundle->Scene,
                        nullptr,
                        !WorldTransformGizmo.QueryDragging());
                }
                else if (ShowcaseRenderer != nullptr)
                {
                    ShowcaseRenderer->PrepareWorldScene(ShowcaseBundle->Scene);
                    RtxLatticePrepared = true;
                }
            }
            if (SceneMutated || DisplayChanged)
            {
                RefreshShowcaseViewportRaster();
            }
            else if (CurrentSel != LastGizmoSlot)
            {
                RefreshGizmoOverlayFromCachedWorld();
            }
        }
    }
    EditorSequence.Tick(SafeStep);
    return !TerminationRequested;
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

    InputCoordinator.BeginTick();
    DockContainer.PollOperatingWindows(InputCoordinator);
    (void)InputCoordinator.TickExchange();

    if (Windowing::HeadlessPlatformWindow* Primary = DockContainer.AccessOperatingSurface(Components::HostWindowToken{ 0u }))
    {
        if (Primary->CloseRequested())
        {
            TerminationRequested = true;
        }
        else
        {
            (void)InputCoordinator.FlushOutboundDirectives(*Primary);
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
        if (ControlCentre.QueryOpen())
        {
            WorkspaceAppearance::AssignOverlayPointerShield(Bounds);
        }
        else
        {
            WorkspaceAppearance::AssignOverlayPointerShield(
                BrowserPanel.QueryDrawerPhysicalBounds(),
                BrowserPanel.QueryNotchPhysicalBounds());
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
    if (!Initialized)
    {
        EditorSequence.Finalize();
        return;
    }

    // 🔴 CRITICAL: Tear down GPU overlay hooks, then workspace sessions, then input, then dock windows.
    EditorSequence.Finalize();
    AuthoredForces.clear();
    DefaultForcesFolder = {};
    CachedWorldRgba8.clear();
    WorldGizmoRasterizer.reset();
    GpuWorldRenderer.reset();
    ShowcaseRenderer.reset();
    ShowcaseBundle.reset();
    Workspaces.Finalize();

    if (TypographyRepository::QueryCurrent() == &Typography)
    {
        TypographyRepository::AssignCurrent(nullptr);
    }

    InputCoordinator.Finalize();
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
