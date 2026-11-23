//============================================================================================================================================
//                                                             EDITORHOST.CPP
//============================================================================================================================================
// 🧩 The combined editor — every workspace subject in one host, with every device concern held by HostLifecycle.
//
// 🔴 LAYOUT RULE, READ BEFORE EDITING THIS HOST. The editor's layout is:
//      workspace windows (WorkspacePanel + WorkspaceIndex + the vendor dock)
//        → splittable panels (EditorPanel + PanelStructure: viewport | UV |
//          outliner | properties leaves, each with chrome and a footer)
//          → leaf content (SceneDirectoryPanel: the sky in a viewport leaf,
//            the outliner | details column in an outliner leaf, the
//            properties / camera-bookmark pages in a properties leaf).
//    The retired validation-shell prototype once duplicated the options rail,
//    texture-texture stack, parametric directory, and inspector. Runtime UI belongs
//    only to the standing panels named above; the editor's sky lives in the
//    viewport LEAF.

#define SLATE_EDITOR_HOST 1
#include <algorithm>

#include "SlateWorkspace/Discipline/SketchViewportOverlay/Api/SketchViewportOverlay.h"
#include "Application/EditorHost/Api/EditorFrameContext.h"
#include "Application/EditorHost/Api/ViewportRuntimeState.h"
#include "Application/EditorHost/Api/CadPacketUploadState.h"
#include "SlateShape/Sketch/SketchRenderingProjection/Api/SketchRenderingProjection.h"
#include "SlateWorkspace/Discipline/ContentImportCommit/Api/ContentImportCommit.h"
#include "SlateWorkspace/Discipline/SketchInteraction/Api/SketchInteraction.h"
#include "SlateWorkspace/Discipline/SketchRevisionHistory/Api/SketchRevisionHistory.h"
#include "SlateWorkspace/Discipline/SketchDirectoryPresentation/Api/SketchDirectoryPresentation.h"
#include "SlateWorkspace/Discipline/ViewportLookInput/Api/ViewportLookInput.h"
#include "SlateWorkspace/Discipline/WorldSketchBridge/Api/WorldSketchBridge.h"
#include "SketchToolset/SketchTool/SelectionOptions/Api/SelectionOptions.h"
#include "SlateUI/Interface/ToolOptionsWidget/Api/ToolOptionsWidget.h"
#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"
#include "SlateWorkspace/Discipline/SketchOperationDriver/Api/SketchOperationDriver.h"
#include "SlateWorkspace/Discipline/AnnotationDriver/Api/AnnotationDriver.h"
#include "SlateWorkspace/Discipline/WorldSketchDimensionProjection/Api/WorldSketchDimensionProjection.h"
#include "SlateWorkspace/Discipline/WorldSketchPicking/Api/WorldSketchScreenPicking.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/SketchBasis.h"
#include "SlateWorkspace/Discipline/WorkplaneCatalogue/Api/WorkplaneCatalogue.h"
#include "Foundation/DeliveryGuarantee.h"
#include "Application/Api/HostFeature.h"
#include "SlateWorkspace/Discipline/CodexActivation/Api/CodexActivation.h"
#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationCube.h"
#include "SlateWorkspace/Discipline/MaterialLayerProjection/Api/MaterialLayerProjection.h"
#include "SlateWorkspace/Discipline/CodexSceneProxy/Api/CodexSceneProxy.h"
#include "SlateWorld/World/EditorCameraComponent/Api/EditorCameraComponent.h"
#include "SlateWorld/World/AtmosphereComponent/Api/AtmosphereComponent.h"
#include "SlateWorld/World/DirectionalLightComponent/Api/DirectionalLightComponent.h"
#include "SlateWorkspace/Discipline/WorkspaceDeclaration/Api/WorkspaceDeclaration.h"
#include "SlateUI/Interface/ContentBrowserPanel/Api/ContentBrowserPanel.h"
#include "SlateUI/Interface/ControlCentrePanel/Api/ControlCentrePanel.h"
#include "SlateUI/Interface/ThemeInterchange/Api/ThemeInterchange.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"
#include "SlateUI/Interface/TexturingPanel/Api/TexturingPanel.h"
#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspacePanel.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsPanel.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectoryPanel.h"
#include "SlateUI/Interface/WorkspacePanel/Api/WorkspaceIndex.h"
#include "SlateRuntime/Session/SessionSequence/Api/SessionSequence.h"
#include "SlateRuntime/Session/HostEnvironment/Api/HostEnvironment.h"
#include "SlateVulkan/Device/WorkspaceOverlayPass/Api/WorkspaceOverlayPass.h"
#include "SlateVulkan/Device/WorkspaceCadPass/Api/WorkspaceCadPass.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/DrawableScale.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/CadProjection.h"
#include "SlateVulkan/Device/ShaderCodec/Api/ShaderCodec.h"
#include "SlateVulkan/Device/AtmospherePresentationSurface/Api/AtmospherePresentationSurface.h"
#include "SlateCompute/Compute/MaterialTextureExport/Api/MaterialTextureExport.h"
#include "SlateCompute/Compute/GeometryDeviceExchange/Api/GeometryDeviceExchange.h"
#include "SlateCompute/Compute/GeometryRenderingExchange/Api/GeometryRenderingExchange.h"
#include "SlateCompute/Compute/VisibilityIndex/Api/VisibilityIndex.h"
#include "SlateDocument/Document/GeometryInterchange/Api/GeometryFileInterchange.h"
#include "SlateDocument/Document/IntakeIndex/Api/IntakeIndex.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/PopulationIndex/Api/PopulationIndex.h"
#include "SlateDocument/Format/CodexInterchange/Api/CodexInterchange.h"
#include "SlateDocument/Format/CodexInterchange/Api/WorkspaceCodex.h"
#include "SlateDocument/Format/WorkspaceSceneActivation/Api/WorkspaceSceneActivation.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <vector>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
#endif
#include <cstring>
#include <system_error>

//------------------------------------------------------------------------------------------------------------------------
//                                                          FIGURES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

using namespace Slate;

constexpr std::uint32_t InitialWidth  = 1280u;   // [px]
constexpr std::uint32_t InitialHeight = 720u;    // [px]

constexpr const char* WindowTitle = "Slate \u2014 Editor";
constexpr const char* HostName    = "EditorHost";

// 📝 The workspace ground the interface is recorded over. Stated here because it is the one visual decision
//    this host makes; everything else it presents belongs to a panel.
//------------------------------------------------------------------------------------------------------------------------
//                                                      THE THREE CEILINGS
//------------------------------------------------------------------------------------------------------------------------

// 🔴 Applying the content browser in the south drawer crossed all three of the budgets that have each, at
//    least once, taken a host down with no window and no log line. They are asserted here so a fourth panel
//    cannot repeat any of them silently.

// ① EASED INTERPOLANTS. `ControlIndex::Register` draws two fades per control, and the integrator's supply
//    is shared by every index in the process — the browser's private index does not get its own pool.
constexpr std::uint32_t EasesPerControl = 2u;
constexpr std::uint32_t CentreControls  = ControlCentrePanel::ControlCapacity;
constexpr std::uint32_t BrowserControls = ContentBrowserPanel::RegistrationDemand;
constexpr std::uint32_t EditorControls  = PanelStructure::RecordLimit * EditorPanel::ControlsPerRecord;
constexpr std::uint32_t SceneControls   = SceneDirectoryPanel::RegistrationDemand
                                        + TexturingPanel::RegistrationDemand;
constexpr std::uint32_t ParametricControls = 4u + ParametricWorkspaceContext::RowLimit * 2u
                                           + 1u + ParametricToolsContext::BandLimit
                                           + ParametricToolsContext::TileLimit;
constexpr std::uint32_t BareEases       = 9u + 1u + 1u + 4u; // [-] - centre, shell, and transfer/export rails

constexpr std::uint32_t DemandedEases =
    ((CentreControls + BrowserControls + EditorControls + SceneControls + ParametricControls)
     * EasesPerControl) + BareEases;

static_assert(DemandedEases <= MotionIntegrator::EaseCapacity,
              "this host's panels demand more eased interpolants than the integrator holds — the panel "
              "constructed last is rejected mid-registration and the host exits before its first frame; raise "
              "MotionIntegrator::EaseCapacity or reduce a panel's control count");

// ② INDEX SLOTS. Counted per index, not per host. The browser is the only owner of its own
//    `BrowserInteraction`, so only its demand is weighed here; the Control Centre answers to its private index.
static_assert(BrowserControls <= ControlIndex::ControlCapacity,
              "the content browser registers more controls than one ControlIndex holds — Construct is "
              "rejected with \"no further control slot\" and the south drawer opens onto blank ground");

static_assert(SceneControls <= ControlIndex::ControlCapacity,
              "the scene directory registers more controls than one ControlIndex holds — Construct is "
              "rejected with \"no further control slot\" and the editor opens without its scene directory");

static_assert(ParametricControls <= ControlIndex::ControlCapacity,
              "the sketch directory and parametric tools register more controls than one ControlIndex holds — "
              "the editor cannot add the sketch panels to its dropdown safely");

// ③ AUTOMATIC STORAGE. A Windows thread is given one megabyte and a refusal here is not a refusal at all:
//    the guard page is touched in the prologue, so the process dies before a statement can report anything.
//    Linux hands out eight megabytes, which is exactly why no gate here can catch it.
constexpr std::size_t WindowsThreadStack = 1048576u;                  // [B] - the shipped linker default
constexpr std::size_t AutomaticLimit   = WindowsThreadStack / 4u;   // [B] - a quarter, leaving room to call

constexpr std::size_t AutomaticUiBytes = sizeof(ShaderCodec) + sizeof(WorkspaceOverlayPass);

static_assert(AutomaticUiBytes <= AutomaticLimit,
              "this host's automatic UI members no longer fit a quarter of a Windows thread stack — the "
              "prologue's stack probe will fault before main runs a statement and the host will exit with "
              "no window and no log line; move the largest member to static storage");

// 🔴 The session is deliberately absent from the sum above, and that is load-bearing rather than an
//    oversight: `SessionSequence` holds `ViewportSequence`, which is over four hundred kilobytes on its
//    own — more than the whole budget below. It is declared `static` in main for exactly that reason, so
//    it never enters this frame. This assertion states the fact the arrangement depends on, so a reader
//    who moves the session onto the stack is refused here rather than by a stack probe with no log line.
static_assert(sizeof(SessionSequence) > AutomaticLimit,
              "SessionSequence now fits the automatic budget — re-check whether it still needs static "
              "storage in main, and whether this assertion is still stating anything true");

constexpr float WorkspaceGround[4] = { 0.06f, 0.06f, 0.08f, 1.0f };   // [-]

// 🧩 Records a GPU overlay across a scissor box with one rectangle subtracted from it.
// in    Overlay   [-]  the pass to record into
// in    Leaf      [px] the leaf's WHOLE box -- the camera's canvas, never the clipped one
// in    X0,Y0,X1,Y1  [px] the scissor the drawers left uncovered
// in    Withheld  [px] a box to keep clear; a zero-area extent records the scissor unchanged
// note  🔴 A scissor is ONE rectangle, so "everything except this box" is recorded as up to four
//        bands around it -- above, below, left and right. The bands are disjoint, so no fragment is
//        shaded twice and the overlay's straight-alpha blend stays exactly as it was.
// note  ⚠️ The LEAF box is passed through untouched to every band. Clipping it to the band is what
//        made the grid squash into the space it had left instead of simply being hidden there.
// cost  ✔️ At most four recordings, and exactly one whenever no menu stands.
}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                            MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    using namespace Slate;

    // ① The session — window, device, chain, recordings, interface, appearance and font atlas, in the one
    //    order every windowed product shares. 🔴 `SlateRuntime` owns that order; this host owns the panels
    //    and the device passes it puts inside the tick, and nothing else.
    SessionDeclaration Declared;
    Declared.Naming        = HostName;
    Declared.WindowCaption = WindowTitle;
    Declared.InvokedAs     = (ArgumentCount > 0) ? ArgumentValues[0] : "";
    Declared.InitialWidth  = InitialWidth;
    Declared.InitialHeight = InitialHeight;
    Declared.Pacing        = LatencyIntent::SteadyPacing;

    Declared.North.Caption       = "ControlCentre";
    Declared.North.TongueSubject = SymbolSubject::PulseTrace;
    Declared.North.PoseCount     = 2u;

    Declared.South.Caption       = "ContentBrowser";
    Declared.South.TongueSubject = SymbolSubject::FolderClosed;
    Declared.South.PoseCount     = 3u;

    for (std::uint32_t Channel = 0u; Channel < 4u; ++Channel)
        Declared.WorkspaceGround[Channel] = WorkspaceGround[Channel];

    // 📝 Static because `ViewportSequence` alone is 406 KB and a Windows thread is handed one megabyte.
    //    The stack assertion above weighs only what is left on automatic storage.
    static SessionSequence Session;

    const Deliver<bool> Opened = Session.ConstructSession(Declared);

    if (!Opened.Resolved)
    {
        std::printf("%s \u2014 %s\n", HostName, Opened.Error.Detail);
        return 1;
    }

    // 📝 The two seams this host still drives directly. The session owns their order; this host owns what
    //    it records through them.
    ViewportSequence& Viewport = Session.Interface();
    HostLifecycle&    Lifetime = Session.Device();

    // 🔴 Stated once, from the ONE reader of the product macro. `HostFeature.h` was declared, wired into
    //    the build, and included by nothing — which is exactly the arrangement that let an agent conclude
    //    there was no live feature seam and write its own camera, sky and CAD editor. A product that
    //    names itself on the console is a product whose seam is demonstrably read.
    std::printf("%s \u2014 %s\n", HostName, HostProduct);

    // ─────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                       THE TICK LOOP
    // ─────────────────────────────────────────────────────────────────────────────────────────────────────

    // 📝 🔴 Every refusal is resolved inside Await, before a display image is acquired. A tick that reports
    //    Recording has a recording open and cannot be abandoned; a tick that reports Idle has opened
    //    nothing. That is what makes the `continue` below safe — the arrangement this host previously got
    //    wrong five times over, returning to the top of the loop with a command buffer still recording.

    // ③ The workspaces this host opens. 🔴 The INDEX owns them and the panel presents them — `14` §1
    //    forbids a panel from holding what it displays, and separating the two is the whole reason there
    //    are two components here rather than one.
    // 📝 The subject this host opens by default, named once so the startup registration and the strip's `+`
    //    cannot disagree about what a new workspace is.
    constexpr WorkspaceSubject DefaultSubject = WorkspaceSubject::Vacant;

    static WorkspaceIndex          Workspaces;
    static WorkspacePanel          Workspace;
    static EditorPanel             WorkspacePanels;
    static PanelStructure          PanelPartitions[WorkspaceIndex::WorkspaceLimit];
    static EditorPanelConfiguration    PanelConfiguration[WorkspaceIndex::WorkspaceLimit];
    static ControlCentrePanel      ControlCentre;
    static ControlCentreConfiguration  ControlCentreValues;
    static SceneDirectoryPanel     SceneDirectory;
    static SceneDirectoryContext   SceneApplied;
    static ControlIndex        SceneInteraction;
    static ParametricWorkspacePanel SketchDirectory;
    // 🔴 THE SKETCH STATE THE PARAMETRIC PRODUCT DRAWS INTO. This lived only in
    //    `ParametricSketchHost`, which is why that host could not be deleted: the hosts hold ZERO
    //    definitions, so nothing would have failed to compile, and every gate would have stayed
    //    green while the shipped product quietly lost the ability to draw.
    //
    // 📝 Declared unconditionally and used behind `HostHasFeature(FeatureParametric)`. A `constexpr`
    //    test rather than an `#ifdef` keeps the body type-checked in every product, which is the same
    //    reason `DiagnosticLayersRequested()` replaced three copies of `#ifdef SLATE_DEBUG`.
    static WorkspaceNameIndex        SketchNaming;
    static SketchStructure           Sketch;
    static WorldSketchStructure       SketchWorld;
    static WorldSketchMapping   SketchWorldMapping;
    static WorkspaceRecordStructure  SketchRecords;
    static WorkspaceRevisionSequence SketchRevisions;
    // 🔴 Seated beside the sketch, never inside it: a sketch holds exactly ONE plane and overwrites
    //    it, which is what made placing a second workplane silently re-interpret everything already
    //    drawn.
    static WorkplaneCatalogue        SketchWorkplanes;
    static SketchPlacement           SketchTool;
    static WorkspaceRecordName       SketchPendingSelection;
    // 🔴 THE SELECTION AND GIZMO PATH WAS DEFINED, PROVEN AND NEVER CALLED. Every part of it existed —
    //    the picker, the gizmo handles, the transform sessions, the Blender-style G/R/S parser — and
    //    `DriveViewportSelectionAndTransform` had exactly ZERO call sites outside its own translation
    //    unit. That is why the Select tool did nothing: not a broken implementation, an unreached one.
    //    These four are the state it needs to keep between frames.
    static SketchPick                SketchSemanticSelection;
    static SketchSelectionSet        SketchSelectionSetState;
    static SketchPick                SketchHoveredSelection;
    static TransformSession          SketchTransform;
    static WorldSketchTransformSession SketchWorldTransform;
    static double                    SketchLastMovePressed = 0.0;
    // 📝 A monotonic run of the session, accumulated from the tick's own elapsed figure, so the
    //    double-tap that switches a move between plane and free travel has a clock to measure against.
    static double                    SketchSessionMilliseconds = 0.0;
    // 📝 What the Select widget writes and the picker obeys. Vertex by default, 8 px of reach, and no
    //    snapping — choosing a specific element and being dragged to the nearest grid line are opposite
    //    intentions.
    static SelectionOptions          SketchSelection;
    static GizmoOptions              SketchGizmo;
    // 📝 Which record the outliner has highlighted, so a selection made in the tree and one made by
    //    clicking in the viewport are the same selection. Projected from the records each tick.
    static WorkspaceDirectoryProjection SketchDirectoryRows;
    static SketchDirectoryPresentation SketchDirectoryPresented;
    static bool SketchDirectorySeeded = false;
    static std::vector<SketchRevisionSnapshot> SketchRetreated;
    static std::vector<SketchRevisionSnapshot> SketchReinstated;
    // 🔴 SELECT AND THE GIZMO ARE ONE WIDGET. Choosing something and then moving it is one thing the
    //    artist does; two panels would put the gizmo's switch somewhere else at the moment it is wanted.
    static ToolOptionsWidget         SketchToolOptions;
    static ToolContextMenu           SketchContextMenu;

// 🔴 ONE STATE FOR ALL SEVEN OPERATIONS, and the host holds nothing else about them. What used to sit
//    here -- a tool, an apply flag, a distance, a trim side and two magic-number bounds -- was the
//    operations' own state spread across the host, where none of it could be proven. It now lives in
//    `SketchOperationState` and is driven by `DriveSketchOperations`, which is proven headlessly.
static SketchOperationState      SketchOperations    = {};

// 🔴 ONE STATE FOR THE WHOLE ANNOTATION BAND, held for the same reason as the operations state beside
//    it: dimensioning is a gesture that spans frames -- pick, drag, release, type -- and the frame
//    function cannot remember any of it. Everything provable lives in `AnnotationState`; the host holds
//    the variable and nothing else about how annotation behaves.
static AnnotationState           SketchAnnotations   = {};

// 📝 The figure chips the dimension projection hands back, so the labels can be drawn with the
//    interface's text recorder and hit-tested for the double-click that opens an edit. Rebuilt every
//    frame by the projection; never a source of truth.
static std::vector<DimensionFigureChip> SketchDimensionFigures;
    // 📝 A right-press is a look while it travels and a menu when it does not. The distance is summed
    //    across the hold, because on the release tick the per-tick travel has already fallen to zero.
    static float SecondaryTravel = 0.0f;   // [px] - summed over the current secondary hold
    [[maybe_unused]] static float SecondaryOpenX  = 0.0f;   // [px] - where the press landed
    [[maybe_unused]] static float SecondaryOpenY  = 0.0f;   // [px]
    constexpr float SecondaryClickTravel = 4.0f;   // [px] - beyond this the gesture was a look
    // 📝 The sketch tools measure against an orbit standing; the editor flies a free camera. The
    //    standing is kept beside it and driven from the same yaw/pitch, so both describe one view.
    // 🔴 HOW FAR THE PROJECTION HAS TRAVELLED BETWEEN ITS TWO ENDS. Pressing Ortho used to swap the
    //    projection between one tick and the next, so the whole scene jumped and the artist lost
    //    track of where they were looking. One per leaf, because two split viewports may be part-way
    //    through opposite transits at the same time.
    static ViewportRuntimeState     ViewportRuntime[WorkspaceIndex::WorkspaceLimit];
    // 📝 Static: the packet is large and is reused every frame.
    static WorkspaceCadPacket        SketchCadPacket;

    static ParametricWorkspaceContext SketchDirectoryApplied;
    static ParametricToolsPanel    ParametricTools;
    static ParametricToolsContext  ParametricToolsApplied;
    static ControlIndex            ParametricInteraction;
    static TexturingPanel        Texturing;
    static TexturingContext     TexturingApplied;
    TexturingStack        StackRows;                 // [-] - the mutable row set; the panel borrows it
    MaterialSpecification     EditorMaterialDocument;
    SurfaceLayerSequence      EditorMaterialLayers;
    MaterialProcessingExchange EditorMaterialExchange;
    MaterialProcessingSnapshot EditorMaterialSnapshot;
    bool                      EditorMaterialSnapshotReady = false;
    AtmospherePresentationSurface AtmosphereSurface;
    AtmosphereComponent       DynamicAtmosphere;
    DirectionalLightComponent SunLight;
    EditorCameraComponent     EditorCamera;
    ShaderCodec             OverlayCodec;
    WorkspaceOverlayPass             Overlay;
    // 🔴 THE SKETCH'S OWN GPU PASS. Curves and fills were being drawn through the interface's draw
    //    lists -- ImGui tessellates every segment on the CPU each frame, which is why the viewport
    //    bogged down as shapes accumulated, and why the fill showed its triangulation as a wireframe:
    //    each triangle was a separate anti-aliased primitive with visible shared edges. The data still
    //    lives on the CPU; only the rasterisation moves.
    WorkspaceCadPass                 CadPass;
    CadPacketUploadState              CadUploadState;
    GeometryDeviceExchange           GeometryDevice = {};
    GeometryFileInterchange          GeometryTransfer = {};
    GeometryInterchange              ImportedGeometry = {};
    GeometryRenderingExchange        ImportedRendering = {};
    IntakeIndex                      ImportedIntake = {};
    PopulationIndex                  ImportedOwners = {};
    PartitionResolutionIndex         ImportedPartitions = {};
    VisibilityIndex                  ImportedVisibility = {};
    GeometryRenderingIdentity        PendingRendering = {};
    std::uint32_t                    PendingVisibilityRegistration = 0u;
    std::uint32_t                    PendingRegistrationBase = 0u;
    bool                             GeometryAdmissionPending = false;
    bool                             EditorCameraLookLatched = false;

    // 📝 One overlay record per viewport leaf, in STATIC storage: each record is ~70 KB and the
    //    automatic-storage budget (a quarter of a Windows thread stack) cannot hold eleven of them.
    std::uint32_t            ViewportLeafIndexs[PanelStructure::RecordLimit] = {};
    PlaneExtent              ViewportLeafRects[PanelStructure::RecordLimit]    = {};
    std::uint32_t            ViewportLeafTally = 0u;

    // 📝 The texture-texture leaves, for the Tab arbitration: the layer stack consumes Tab only when
    //    the pointer is over one of its leaves.
    PlaneExtent              LayerLeafRects[PanelStructure::RecordLimit] = {};
    std::uint32_t            LayerLeafTally = 0u;
    bool                    SkyEverGenerated = false;
    std::uint32_t           SkyQuality = 0xFFFFFFFFu;
    std::uintptr_t          SkyTextureIdentity = 0u;
    bool                    SkyRegistered = false;

    // 📐 The editor's scene directory — the sun and sky the viewport renders, registered under the
    //    Lighting grouping. `Sun` and `Sky` are the two appended `EntitySubject` ordinals, so the
    //    inspector's slider cards branch on them while every reference entity keeps its g_NN identity.
    static EntityRow EditorEntities[6] =
    {
        { "Lighting",                EntitySubject::Grouping,   0u, 0xFFFFFFFFu, 2u, "folder lighting", CameraRole::Absent, 1002u },
        { "Directional Light (Sun)", EntitySubject::Sun,        1u,  0u,         0u, "sun light directional", CameraRole::Absent, 1003u },
        { "Sky Atmosphere",          EntitySubject::Sky,        1u,  0u,         0u, "sky atmosphere dome", CameraRole::Absent, 1004u },
        { "Environment",             EntitySubject::Grouping,   0u, 0xFFFFFFFFu, 1u, "folder environment", CameraRole::Absent, 1005u },
        { "Post Process Volume",     EntitySubject::Actor,      1u,  3u,         0u, "post volume effects", CameraRole::Absent, 1006u },
        { "Editor Camera",           EntitySubject::Camera,     0u, 0xFFFFFFFFu, 0u, "camera fly view", CameraRole::Editor, 1007u }
    };
    static SceneDirectoryRows WorkspaceSceneRows = {};
    static WorkspaceCodex OpenedScene = {};
    static bool OpenedSceneStanding = false;
    static const char* const WhiteDielectricChannels[] = { "Base Color", "Metallic", "Roughness", "Opacity" };
    static TextureLayerRow WhiteDielectricLayer = {
        "White Dielectric", TextureLayerClassification::Material, "Normal", 100u, 0xE7E3D8u, 0xE7E3D8u,
        false, 100u, false, "WhiteDielectric.pigment", "Shared Engine Content material",
        { WhiteDielectricChannels[0], WhiteDielectricChannels[1], WhiteDielectricChannels[2], WhiteDielectricChannels[3] },
        4u, 0u, 0xFFFFFFFFu, 0u, true, "white dielectric shared tea service", false, "", true, 4001u
    };
    EntityRow* PresentedEntities = EditorEntities;
    std::uint32_t PresentedEntityCount = 6u;

    // The full catalogue and its 356 arbitrated controls are process-lifetime UI state. Keep them out
    // of main's Windows-sized automatic frame; ordinary host setup reinitialises their presented state.
    static ControlIndex                 BrowserInteraction;

    // 📝 The south drawer's owner. The library is the HOST's, not the panel's — `14` §1 forbids a panel
    //    from holding what it displays, which is the same separation WorkspaceIndex and WorkspacePanel keep.
    static ContentBrowserPanel          ContentBrowser;
    static ContentBrowserConfiguration  ContentBrowserApplied;
    static ContentLibrary               ContentApplied;

    // 📝 The appearance file sits beside the executable and is read once, before any panel is recorded. A
    //    first run has no file yet, which is the ordinary case and not a fault — the build's own appearance
    //    stands and the first colour the artist changes writes the file.
    // 📝 The appearance the session adopted from beside the executable, seated into the Control Centre so
    //    the panel opens on what is actually applied. The session owns the file and the atlas.
    const ThemeSelection& Adopted = Session.Inscribed();

    ControlCentreValues.Theme       = Adopted.Current;
    ControlCentreValues.Primary     = Adopted.Primary;
    ControlCentreValues.Secondary   = Adopted.Secondary;
    ControlCentreValues.Information = Adopted.Information;
    ControlCentreValues.Warning     = Adopted.Warning;
    ControlCentreValues.Alert       = Adopted.Alert;

    const std::filesystem::path EngineContentRoot = Session.ContentRoot();

    // 📝 Which dock node the next registered workspace is applied into; zero means the main dock space.
    std::uint32_t  RegisterIntoNode = 0u;

    FontLoader& Fonts = Session.Fonts();

    ControlCentre.SetFontFamilies(Fonts);
    // 📝 Seat the family carousel on the family the appearance names. Without this the carousel opened
    //    on ordinal zero (the alphabetically first family) while the loaded faces were the appearance's
    //    own — and the role strips draw the LOADED family's faces, so the two have to agree at bring-up.
    for (std::uint32_t Index = 0u; Index < Fonts.FamilyCount(); ++Index)
        if (Fonts.FamilyName(Index) != nullptr &&
            std::strcmp(Fonts.FamilyName(Index), Viewport.Appearance().Fonts.Family) == 0)
        {
            ControlCentreValues.Font = Index;
            break;
        }


    if (!Workspace.ConstructWorkspacePanel(Viewport.Surface(), Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the workspace panel was rejected\n", HostName);
        return 1;
    }

    if (!WorkspacePanels.ConstructEditorPanel(Viewport.MotionSource(), Viewport.Surface(), Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the editor panels were rejected\n", HostName);
        return 1;
    }

    if (!ControlCentre.ConstructControlCentrePanel(Viewport.MotionSource(), Viewport.Surface(), Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the Control Centre panel was rejected\n", HostName);
        return 1;
    }

    if (!BrowserInteraction.AttachMotion(Viewport.MotionSource()).Resolved)
    {
        std::printf("%s \u2014 the content browser index was rejected\n", HostName);
        return 1;
    }

    // 📝 The editor's sun and sky arrive presented, so the viewport draws the sky from the very first
    //    frame and the inspector edits the same ordinates.
    SceneApplied.EnvironmentPresented = true;
    SceneApplied.Environment.SunElevation    = 35.0;
    SceneApplied.Environment.SunAzimuth      = 120.0;
    SceneApplied.Environment.SunIntensity    = 4.8;
    SceneApplied.Environment.SunTemperature  = 5500.0;
    SceneApplied.Environment.SkyIntensity    = 1.0;
    SceneApplied.Environment.SkyTurbidity    = 2.0;
    SceneApplied.Environment.AtmosphereDensity = 1.0;
    SceneApplied.Environment.AtmosphereScaleHeight = 1.0;
    SceneApplied.EntityTaken = 2u;   // [-] - the sun, taken at bring-up

    // 📝 The texture-texture layer stack — the reference's own tree from LayerstackV1.html, seeded as
    //    the editor's mock: a folder holding an adjustment, a decal and two fills (one with a
    //    generator mask), then a fill with a texture mask, a pattern, and a second folder of materials.
    //    The row detail runs are the small sub-lines the stack page shows; the full settings live on
    //    the properties page.
    static const char* const StackChannels[TextureChannelLimit] =
    {
        "Base Color", "Metallic", "Roughness", "Normal",
        "Height", "Ambient Occlusion", "Emissive", "Opacity"
    };

    static TextureLayerRow StackSeed[TextureLayerLimit] =
    {
        { "Surface Detail",  TextureLayerClassification::Folder,  "Passthrough", 100u, 0x9B8CF0u, 0x9B8CF0u,
          false, 100u, false, "", "4 layers", { StackChannels[0], StackChannels[1], StackChannels[2] }, 3u,
          0u, 0xFFFFFFFFu, 4u, true, "folder detail group", false, "", false, 2001u },
        { "Levels",          TextureLayerClassification::Adjustment, "Overlay",   64u, 0x8B8D98u, 0x8B8D98u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[3] }, 2u,
          1u, 0u, 0u, true, "adjust levels", false, "", false, 2002u },
        { "Warning Stencil", TextureLayerClassification::Decal,   "Normal",    100u, 0xE5484Du, 0xE5484Du,
          true,  100u, false, "Bitmap", "Planar \u00B7 100%", { StackChannels[0] }, 1u,
          1u, 0u, 0u, true, "decal stencil warning", false, "", false, 2003u },
        { "Scratches",       TextureLayerClassification::Brushed,    "Screen",     38u, 0xB0E64Cu, 0xB0E64Cu,
          true,   88u, false, "Generator", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[2] }, 2u,
          1u, 0u, 0u, true, "texture scratches grunge", false, "Blur", false, 2004u },
        { "Edge Wear",       TextureLayerClassification::Fill,     "Multiply",   82u, 0xF76B15u, 0xF76B15u,
          true,  100u, false, "Generator", "2048px \u00B7 RGBA 8", { StackChannels[1], StackChannels[2] }, 2u,
          1u, 0u, 0u, true, "fill edge wear rust", false, "", false, 2005u },
        { "Emissive Trim",   TextureLayerClassification::Fill,     "Normal",    100u, 0xFFC53Du, 0xFFC53Du,
          true,  100u, false, "Brushed", "2048px \u00B7 RGBA 8", { StackChannels[6] }, 1u,
          0u, 0xFFFFFFFFu, 0u, true, "fill emissive trim", false, "", false, 2006u },
        { "Hex Panelling",   TextureLayerClassification::Pattern,  "Normal",    100u, 0x8AB4D8u, 0x8AB4D8u,
          true,  100u, false, "Generator", "Hex Grid \u00B7 4\u00D74", { StackChannels[2], StackChannels[4] }, 2u,
          0u, 0xFFFFFFFFu, 0u, true, "pattern hex panel", false, "", false, 2007u },
        { "Base Materials",  TextureLayerClassification::Folder,   "Passthrough", 100u, 0x12A594u, 0x12A594u,
          false, 100u, false, "", "4 layers", { StackChannels[0], StackChannels[1] }, 2u,
          0u, 0xFFFFFFFFu, 4u, true, "folder materials base", false, "", false, 2008u },
        { "Brushed Steel",   TextureLayerClassification::Fill,     "Normal",    100u, 0x8AB4D8u, 0x8AB4D8u,
          true,  100u, false, "Generator", "4096px \u00B7 RGBA 8", { StackChannels[0], StackChannels[1] }, 2u,
          1u, 7u, 0u, true, "fill brushed steel metal", false, "", false, 2009u },
        { "Gold Inlay",      TextureLayerClassification::Fill,     "Normal",    100u, 0xE5484Du, 0xE5484Du,
          true,   50u, true,  "Color Selection", "2048px \u00B7 RGBA 8", { StackChannels[0] }, 1u,
          1u, 7u, 0u, true, "fill gold inlay", false, "Levels, HSL Shift", false, 2010u },
        { "Oak Panel",       TextureLayerClassification::Material, "Normal",    100u, 0xF76B15u, 0xF76B15u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[2] }, 2u,
          1u, 7u, 0u, true, "material oak wood", true, "", false, 2011u },
        { "Canvas Weave",    TextureLayerClassification::Material, "Normal",     90u, 0xE93D82u, 0xE93D82u,
          false, 100u, false, "", "2048px \u00B7 RGBA 8", { StackChannels[0], StackChannels[3] }, 2u,
          1u, 7u, 0u, false, "material canvas fabric", false, "", false, 2012u }
    };

    TexturingApplied.LayerTaken = 1u;

    // 📝 The shared stack helper seeds the mutable row set and every working copy, exactly as the
    //    harness drives it — the two can never drift.
    StackRows.Seed(StackSeed, 12u);
    SeedTexturingContextFromRows(TexturingApplied, StackRows.Rows, StackRows.Count);

    for (std::uint32_t Index = 0u; Index < TextureLayerLimit; ++Index)
    {
        TexturingApplied.MaskSourceTaken[Index] =
            (Index == 2u || Index == 3u || Index == 4u) ? 4u : 0u;
        TexturingApplied.MaskDensity[Index] = (Index == 3u) ? 88u : 100u;
        TexturingApplied.MaskInverted[Index] = (Index == 9u);
    }

    MaterialLayerProjectionReport InitialMaterialBridge = ProjectMaterialLayersFromTextureStack(
        EditorMaterialDocument, EditorMaterialLayers, StackRows, TexturingApplied,
        EditorMaterialExchange, nullptr);
    EditorMaterialSnapshot = InitialMaterialBridge.Snapshot;
    EditorMaterialSnapshotReady = true;

    // 📝 The editor camera, registered as the seventh row. Its details' options are the camera's own:
    //    bit 1 is the camera lag, bit 2 the inverted pitch — the lag arrives enabled so the camera
    //    eases out of the gate, and the pitch arrives un-inverted (the standard fly-cam convention).
    SceneApplied.DetailBits[6u] = 2u;
    SceneApplied.CameraSpeed = 50.0;
    EditorCamera.YawDegrees   = SceneApplied.Environment.SunAzimuth - 20.0;
    // 📐 The fly camera looks slightly DOWN at bring-up, matching the reference editors: the ground
    //    lattice fills the lower frame rather than a sliver at the horizon. A +15 degree default
    //    pointed above the horizon and crushed the perspective grid into the bottom ~100 px.
    EditorCamera.PitchDegrees = -15.0;
    EditorCamera.Position[0]  = 0.0;
    EditorCamera.Position[1]  = 1.5;
    EditorCamera.Position[2]  = 0.0;
    EditorCamera.Snap();
    SceneApplied.CameraPosition[0] = 0.0;
    SceneApplied.CameraPosition[1] = 1.5;
    SceneApplied.CameraPosition[2] = 0.0;
    SceneApplied.CameraRotation[0] = EditorCamera.YawDegrees;
    SceneApplied.CameraRotation[1] = EditorCamera.PitchDegrees;
    // The Editor Camera row's Transform card is the camera component's authored pose, not a disconnected
    // entity mirror. Other rows keep their ordinary scene transforms.
    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
    {
        SceneApplied.EntityPosition[6u][Axis] = SceneApplied.CameraPosition[Axis];
        SceneApplied.EntityRotation[6u][Axis] = SceneApplied.CameraRotation[Axis];
    }
    EditorCamera.PublishTransform(SceneApplied.EntityPosition[6u], SceneApplied.EntityRotation[6u]);

    if (!SceneInteraction.AttachMotion(Viewport.MotionSource()).Resolved)
    {
        std::printf("%s \u2014 the scene directory index was rejected\n", HostName);
        return 1;
    }

    if (!SceneDirectory.ConstructSceneDirectoryPanel(SceneInteraction, Viewport.MotionSource(), Viewport.Surface(),
                                  Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the scene directory was rejected\n", HostName);
        return 1;
    }

    if (!Texturing.ConstructTexturingPanel(SceneInteraction, Viewport.MotionSource(), Viewport.Surface(),
                              Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the texturing panel was rejected\n", HostName);
        return 1;
    }

    if (!ParametricInteraction.AttachMotion(Viewport.MotionSource()).Resolved)
    {
        std::printf("%s \u2014 the sketch-directory index was rejected\n", HostName);
        return 1;
    }

    if (!SketchDirectory.ConstructParametricWorkspacePanel(ParametricInteraction, Viewport.MotionSource(),
                                                           Viewport.Surface(), Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the sketch directory was rejected\n", HostName);
        return 1;
    }

    if (!ParametricTools.ConstructParametricToolsPanel(ParametricInteraction, Viewport.MotionSource(),
                                                       Viewport.Surface(), Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the parametric tools panel was rejected\n", HostName);
        return 1;
    }

    if (!SketchToolOptions.ConstructToolOptionsWidget(Viewport.MotionSource(), Viewport.Surface(),
                                                      Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the tool options widget was rejected\n", HostName);
        return 1;
    }

    if (!SketchContextMenu.ConstructToolContextMenu(Viewport.MotionSource(), Viewport.Surface(),
                                                    Viewport.Appearance()).Resolved)
    {
        std::printf("%s \u2014 the tool context menu was refused\n", HostName);
        return 1;
    }

    // One shader stream index feeds both the dynamic atmosphere compute pass and the overlay pass.
    const Deliver<bool> CodecDelivery =
        OverlayCodec.AttachShaderStreams(Lifetime.DeviceExchange(), ShaderStreamDirectory());

    if (CodecDelivery.Resolved)
    {
        const Deliver<bool> AtmosphereDelivery = AtmosphereSurface.ConstructAtmosphereSurface(
            Lifetime.DeviceExchange(), Lifetime.DiagnosticsExtension(), OverlayCodec);
        if (AtmosphereDelivery.Resolved)
        {
            SkyTextureIdentity = Viewport.Surface().RegisterSampledImage(
                AtmosphereSurface.Sampler(), AtmosphereSurface.View());
            SkyRegistered = SkyTextureIdentity != 0u;
        }
        else
        {
            std::printf("%s \u2014 the GPU atmosphere presentation was rejected (reason %u: %s)\n", HostName,
                        static_cast<unsigned>(AtmosphereDelivery.Error.DeclaredReason),
                        AtmosphereDelivery.Error.Detail);
        }
    }

    // The overlay pass shares the lowered-stream index. A sandbox with no SPIR-V keeps the interface
    // functional, but a packaged editor uses the compute sky and GPU overlay paths.


    if (!CodecDelivery.Resolved)
    {
        std::printf("%s \u2014 the overlay shader streams were not found (reason %u: %s); "
                    "GPU overlay is required for grid and axis rendering\n",
                    HostName,
                    static_cast<unsigned>(CodecDelivery.Error.DeclaredReason),
                    CodecDelivery.Error.Detail);
    }
    else
    {
        const Deliver<bool> PassDelivery = Overlay.ConstructWorkspaceOverlayPass(Lifetime.DeviceExchange(),
                                                            Lifetime.DiagnosticsExtension(),
                                                            OverlayCodec,
                                                            Lifetime.Offering().ColourTargetFormat);

        if (!PassDelivery.Resolved)
        {
            std::printf("%s \u2014 the overlay pass was rejected (reason %u: %s); "
                        "GPU overlay is required for grid and axis rendering\n",
                        HostName,
                        static_cast<unsigned>(PassDelivery.Error.DeclaredReason),
                        PassDelivery.Error.Detail);
        }
        else
        {
            std::printf("%s \u2014 overlay pass standing: the grid, the axes and the gizmo draw on the GPU\n",
                        HostName);
        }

        // 📝 The sketch pass shares the overlay's codec and colour format.
        if constexpr (HostHasFeature(FeatureParametric))
        {
            const Deliver<bool> CadDelivery = CadPass.ConstructWorkspaceCadPass(
                Lifetime.DeviceExchange(), Lifetime.DiagnosticsExtension(), OverlayCodec,
                Lifetime.Offering().ColourTargetFormat);
            if (!CadDelivery.Resolved)
                std::printf("%s \u2014 the CAD pass was not standing (reason %u: %s); "
                            "GPU CAD is required for sketch rendering\n",
                            HostName,
                            static_cast<unsigned>(CadDelivery.Error.DeclaredReason),
                            CadDelivery.Error.Detail);
            else
                std::printf("%s \u2014 CAD pass standing: sketch curves and fills draw on the GPU\n",
                            HostName);
        }
    }

    // 🔴 The renderer's device estate is deliberately brought up before any geometry is admitted. The next
    //    geometry increment supplies a selected imported packet; until then it owns no residency and records no
    //    geometry. Keeping the estate separate makes device recovery and display-sized target reclamation testable
    //    without inventing a placeholder surface.
    const DeviceOffering GeometryOffering = Lifetime.Offering();
    const Deliver<bool> GeometryDelivery = GeometryDevice.ConstructGeometryDeviceExchange(
        Lifetime.DeviceExchange(), Lifetime.DiagnosticsExtension(), ShaderStreamDirectory().c_str(),
        InitialWidth, InitialHeight, GeometryOffering.ColourTargetFormat);
    if (!GeometryDelivery.Resolved)
    {
        std::printf("%s \u2014 the geometry device estate was rejected (reason %u: %s)\n", HostName,
                    static_cast<unsigned>(GeometryDelivery.Error.DeclaredReason), GeometryDelivery.Error.Detail);
    }
    if (!ImportedVisibility.ConstructVisibilityIndex(InitialWidth, InitialHeight).Resolved)
    {
        std::printf("%s — imported topology partition visibility could not be prepared\n", HostName);
    }

    // 🔴 The browser carries its OWN index, as every panel here does, so its registration cannot exhaust the
    //    Control Centre's. Read — an registration refusal is silent at the call site and a browser that was
    //    rejected records nothing at all, which reads as a drawer that opens onto blank ground.
    const Deliver<bool> BrowserDelivery = ContentBrowser.ConstructContentBrowserPanel(BrowserInteraction, Viewport.Surface(), Viewport.Appearance());
    if (!BrowserDelivery.Resolved)
    {
        std::printf("%s \u2014 the content browser was rejected (reason %u: %s)\n", HostName,
                    static_cast<unsigned>(BrowserDelivery.Error.DeclaredReason), BrowserDelivery.Error.Detail);
        return 1;
    }

    // 🔴 The browser takes no appearance at Construct — it is applied here, once the viewport has resolved one.
    ContentBrowser.Reapply(Viewport.Appearance());

    ApplyReferenceContent(ContentApplied);
    PopulateImportDirectory(ContentBrowserApplied, EngineContentRoot);

    // 📝 🔴 The editor opens a VACANT workspace, where the texturing host opens a canvas. This is the one
    //    thing that distinguishes the two hosts, and it is the reason there are two: the editor carries
    //    every subject and cannot presume which the artist wants, so it presents a blank one and lets them
    //    say. A host that guessed would open a canvas for someone who came to sketch.
    const Deliver<std::uint32_t> DefaultWorkspace = Workspaces.Register(DefaultSubject);
    if (!DefaultWorkspace.Resolved)
    {
        std::printf("%s \u2014 the default workspace could not be opened\n", HostName);
        return 1;
    }
    Discard(ApplyWorkspace(DeclaredWorkspaceFor(DefaultSubject), PanelPartitions[DefaultWorkspace.Resolve()]));

    std::printf("%s \u2014 opened %s\n", HostName, Workspaces.ActiveTitle());

    while (Session.Active())
    {
        // 🔴 One call answers the window, the resize, the device loss and the interface tick. Every branch
        //    this host used to carry for the INTERFACE — retiring, rebuilding, renegotiating — is
        //    `SlateRuntime`'s now. What remains below is this host's own device estate, which the session
        //    cannot know about and reports through `DeviceRetiring` and `DeviceRebuilt`.
        const SessionPass Pass = Session.Await();

        if (Pass.Current == SessionCondition::Closed)
            break;

        // 🔴 Phase one of a device rebuild. The device STILL STANDS here, so this is the one moment this
        //    host can release its own passes and images against a live handle. Reclaiming after the
        //    rebuild idles a device the vendor has already destroyed, which the loader reports as
        //    VUID-vkDeviceWaitIdle-device-parameter. The session has already retired the interface.
        if (Pass.Current == SessionCondition::DeviceRetiring)
        {
            GeometryDevice.Reclaim();
            AtmosphereSurface.Reclaim();
            SkyRegistered = false;
            SkyTextureIdentity = 0u;
            Overlay.Reclaim();
            OverlayCodec.Reclaim();
            continue;
        }

        // ③·i 🔴 The DEVICE was rebuilt, so every device handle this host holds names an object the vendor
        //      has returned. The session has already reconstructed the interface against the rebuilt
        //      device; what follows is this host's own estate, rebuilt in the same tick.
        if (Pass.DeviceRebuilt)
        {
            // The shader modules, dynamic atmosphere image and overlay all died with the old device.
            // Reattach streams first because both passes resolve their modules from that index.
            if (OverlayCodec.AttachShaderStreams(Lifetime.DeviceExchange(), ShaderStreamDirectory()).Resolved)
            {
                if (AtmosphereSurface.ConstructAtmosphereSurface(Lifetime.DeviceExchange(), Lifetime.DiagnosticsExtension(),
                                                   OverlayCodec).Resolved)
                {
                    SkyTextureIdentity = Viewport.Surface().RegisterSampledImage(
                        AtmosphereSurface.Sampler(), AtmosphereSurface.View());
                    SkyRegistered = SkyTextureIdentity != 0u;
                    SkyEverGenerated = false;
                }

                static_cast<void>(Overlay.ConstructWorkspaceOverlayPass(Lifetime.DeviceExchange(),
                                                    Lifetime.DiagnosticsExtension(),
                                                    OverlayCodec,
                                                    Lifetime.Offering().ColourTargetFormat));
                if constexpr (HostHasFeature(FeatureParametric))
                {
                    // ⚠️ Reclaimed first: the pass holds device memory bound to the old chain.
                    CadPass.Reclaim();
                    static_cast<void>(CadPass.ConstructWorkspaceCadPass(Lifetime.DeviceExchange(),
                                                        Lifetime.DiagnosticsExtension(),
                                                        OverlayCodec,
                                                        Lifetime.Offering().ColourTargetFormat));
                    // 📝 Force a re-upload: the vertex buffer went with the old pass.
                    CadUploadState.Invalidate();
                }
                for (std::uint32_t Index = 0u; Index < PanelStructure::RecordLimit; ++Index)
                {
                    ViewportRuntime[Index].InvalidateOverlayUpload();
                }
            }

            const DeviceOffering ResizedGeometryOffering = Lifetime.Offering();
            const Deliver<bool> ResizedGeometryDelivery = GeometryDevice.ConstructGeometryDeviceExchange(
                Lifetime.DeviceExchange(), Lifetime.DiagnosticsExtension(), ShaderStreamDirectory().c_str(),
                Pass.Width, Pass.Height, ResizedGeometryOffering.ColourTargetFormat);
            if (!ResizedGeometryDelivery.Resolved)
            {
                std::printf("%s \u2014 the geometry device estate could not be rebuilt (reason %u: %s)\n", HostName,
                            static_cast<unsigned>(ResizedGeometryDelivery.Error.DeclaredReason), ResizedGeometryDelivery.Error.Detail);
            }
        }

        // ③ The chain was re-established. The session has restated the interface's image counts; this
        //    host re-derives its own display-sized targets against the extent the chain now holds.
        else if (Pass.DisplayReestablished)
        {
            if (GeometryDevice.Standing() && !GeometryDevice.ReclaimDisplay(Pass.Width, Pass.Height).Resolved)
            {
                std::printf("%s \u2014 the geometry targets could not be re-derived after display recovery\n", HostName);
            }
        }

        // 🔴 A SESSION THAT STOPS RECORDING GOES BLACK WITH THE WINDOW STILL OPEN, and says nothing.
        //    That is exactly what "a flash of the viewport, then a black screen" looks like from the
        //    outside: the loop is alive, `Await` keeps returning, and every tick skips the draw. The
        //    condition is reported ONCE per run of the same value rather than every frame, because a
        //    per-frame print floods the console and hides the transition that matters.
        if (Pass.Current != SessionCondition::Recording)
        {
            static SessionCondition LastReported = SessionCondition::Recording;
            static std::uint32_t    SkippedTally = 0u;

            ++SkippedTally;
            if (Pass.Current != LastReported)
            {
                const char* Named = Pass.Current == SessionCondition::Idle           ? "Idle"
                                  : Pass.Current == SessionCondition::DeviceRetiring ? "DeviceRetiring"
                                  : Pass.Current == SessionCondition::Closed         ? "Closed"
                                                                                     : "unknown";
                std::printf("%s \u2014 not recording: %s (%u frame(s) skipped)\n",
                            HostName, Named, static_cast<unsigned>(SkippedTally));
                std::fflush(stdout);
                LastReported = Pass.Current;
                SkippedTally = 0u;
            }
            continue;
        }

        {
            // 🔴 The workspace is recorded FIRST and the drawers over it. One background draw list, so
            //    the order of recording IS the z-order — and the previous arrangement recorded the
            //    workspace after `RecordDrawers`, which textured the whole surface over the control
            //    centre and the asset browser.
            const PlaneExtent Whole = Spanning(0.0f, 0.0f,
                                               static_cast<float>(Pass.Width),
                                               static_cast<float>(Pass.Height));

            const PointerCondition& ForegroundPointer = Viewport.Surface().Pointer();
            // 🔴 The interface and the swapchain use different pixel units on a scaled Windows
            //    display. All pointer/projection work below stays in logical points; the GPU
            //    overlay and CAD scissor are converted once through this measured display scale.
            const DrawableScale ViewportDrawable = DrawableScale::Between(
                Viewport.Surface().Display().Width,
                static_cast<double>(Pass.Width));
            const PlaneExtent NorthInterior = Viewport.Drawers().Interior(DrawerBearing::North);
            const PlaneExtent SouthInterior = Viewport.Drawers().Interior(DrawerBearing::South);

            bool ForegroundOverViewport = false;
            for (std::uint32_t Leaf = 0u; Leaf < WorkspacePanels.LeafCount(); ++Leaf)
            {
                if (WorkspacePanels.LeafSubject(Leaf) == PanelSubject::Viewport &&
                    WorkspacePanels.LeafBody(Leaf).Encloses(ForegroundPointer.PositionX,
                                                            ForegroundPointer.PositionY))
                {
                    ForegroundOverViewport = true;
                    break;
                }
            }

            const bool PointerBehindDrawer =
                NorthInterior.Encloses(ForegroundPointer.PositionX, ForegroundPointer.PositionY) ||
                SouthInterior.Encloses(ForegroundPointer.PositionX, ForegroundPointer.PositionY);

            // 🔴 WHEN THE LOOK GESTURE OWNS THE VIEWPORT, WASDEQ BELONG TO THE CAMERA ONLY. The same `S`
            //    was reaching the fly camera as "backward" and the sketch transform grammar as "Scale",
            //    so one press moved the camera and scaled the shape at once. The look latch survives the
            //    cursor warp, so the suppression follows the gesture even when the pointer has been recentered.
            // 🔴 The navigation mode is a property of the active viewport, not merely of the
            //    mouse button. In ortho the same secondary drag is CAD pan; feeding it to the free
            //    camera as well changes the hidden perspective pose and makes the next toggle appear
            //    to jump back to stale WASD data.
            const bool ActiveViewportPerspective = PanelConfiguration[0].Perspective;
            const bool ViewportLookPermitted =
                (ForegroundOverViewport || EditorCameraLookLatched) && !PointerBehindDrawer;
            const bool ViewportLookHeld = ForegroundPointer.SecondaryHeld && ViewportLookPermitted
                                        && ActiveViewportPerspective;
            const TextInputCondition SketchViewportText =
                FilterViewportLookTextInput(Viewport.Surface().TextInput(), ViewportLookHeld);

            const bool TabPressed = Viewport.Seam().KeyPressed(KeySubject::Summon);
            // 🔴 Q IS THE SELECT TOOL, AND IT IS ALSO THE FLY CAMERA'S DOWN KEY. While the look gesture is
            //    held that Q belongs to navigation and must not also flip the sketch into Select.
            //
            // 📝 Any half-placed shape is abandoned. Pressing Q while two anchors of a rectangle
            //    are down and leaving them pending would draw the rectangle on the next click that
            //    was meant to select something.
            if (!ViewportLookHeld && Viewport.Seam().KeyPressed(KeySubject::ChooseSelect))
            {
                ParametricToolsApplied.ActiveSubject = ParametricToolSubject::Select;
                ParametricToolsApplied.Page = ParametricToolPage::Catalogue;
                SketchTool.Abandon();
                CancelCornerDragSession(SketchOperations.Corner);
                CancelSketchOperationSession(SketchOperations.Operation);
                SketchContextMenu.Close();

                // 🔴 Q DID NOT REACH SELECT WHILE AN OPERATION STOOD, AND THIS IS WHY. The catalogue
                //    reads its tiles through the SHARED control index, whose release latch is retired by
                //    `ParametricInteraction.Advance` -- which runs near the END of the frame, long after
                //    this handler. A tile the pointer still rests on therefore carries a release into the
                //    NEXT frame, where `ParametricToolsPanel::Record` re-reads it and re-assigns
                //    `ActiveSubject` from the tile. The tool was set to Select here and put straight back
                //    to Fillet before anything drew, so the artist saw the operation refuse to let go.
                //    Dropping the grab makes the keyboard the authority for the frame it arrives in.
                ParametricInteraction.Abandon();

                // 🔴 THE PREPARED TOOL IS THE DRIVER'S ONLY MEMORY, and the driver is not called at all
                //    while Select stands -- the host gates it on `OperationToolStanding`. Cancelling the
                //    sessions without retiring `Prepared` leaves it naming the abandoned operation, so
                //    re-entering that same tool later finds `ActiveTool == Prepared`, skips the
                //    tool-change cancel, and resumes against state the artist already dismissed.
                SketchOperations.Prepared = ParametricToolSubject::Select;

                // 📝 The band and tile are carried back with the subject. They are what the settings page
                //    titles itself from, so leaving them on the abandoned operation kept its name and its
                //    options on screen underneath a Select that had already taken effect.
                ParametricToolsApplied.ActiveBand = 0u;
                ParametricToolsApplied.ActiveTool = 0u;
            }

            const bool RevertPressed = Viewport.Seam().KeyPressed(KeySubject::Revert)
                                    && Viewport.Seam().Modifiers().Commanded;
            if (RevertPressed)
            {
                const bool ReinstatePressed = Viewport.Seam().Modifiers().Shifted;
                const bool Restored = ReinstatePressed
                                   ? ReinstateSketchRevision(SketchRetreated, SketchReinstated,
                                                            SketchNaming, Sketch, SketchWorld,
                                                            SketchWorldMapping, SketchRecords,
                                                            SketchRevisions, SketchWorkplanes,
                                                            SketchPendingSelection,
                                                            SketchSemanticSelection)
                                   : RetreatSketchRevision(SketchRetreated, SketchReinstated,
                                                          SketchNaming, Sketch, SketchWorld,
                                                          SketchWorldMapping, SketchRecords,
                                                          SketchRevisions, SketchWorkplanes,
                                                          SketchPendingSelection,
                                                          SketchSemanticSelection);
                if (Restored)
                {
                    SketchHoveredSelection = {};
                    SketchTransform = {};
                    CancelCornerDragSession(SketchOperations.Corner);
                    CancelSketchOperationSession(SketchOperations.Operation);
                    SketchContextMenu.Close();
                }
            }
            EditorFrameContext FrameContext = {
                Pass, ForegroundPointer, ForegroundPointer, ViewportDrawable,
                Whole, NorthInterior, SouthInterior,
                ForegroundOverViewport, PointerBehindDrawer
            };
            PointerCondition& BackgroundPointer = FrameContext.BackgroundPointer;
            if (PointerBehindDrawer)
            {
                BackgroundPointer.PositionX = -1000000.0f;
                BackgroundPointer.PositionY = -1000000.0f;
                BackgroundPointer.TravelX = BackgroundPointer.TravelY = BackgroundPointer.WheelY = 0.0f;
                BackgroundPointer.ContactHeld = BackgroundPointer.ContactPressed = false;
                BackgroundPointer.ContactDoublePressed = BackgroundPointer.ContactReleased = false;
                BackgroundPointer.SecondaryHeld = BackgroundPointer.SecondaryPressed = false;
                BackgroundPointer.SecondaryReleased = false;
            }

            Discard(Workspace.Record(Whole, Workspaces.ActiveTitle()));

            // 🔴 The dock space FIRST, over the whole panel. Every workspace below docks into it, and the
            //    vendor draws their tabs with PatchA's trapezoid — which is what makes a tab draggable out
            //    into a floating window. A hand-recorded tab bar cannot be undocked: the vendor's docking
            //    operates on WINDOWS, so a workspace has to be one.
            Viewport.Seam().RecordDockSpace(Whole);

            const std::uint32_t OpenCount = Workspaces.OpenCount();

            // 🔴 Titles are read through `Titled`, which points into the index's own storage. The delivered
            //    form copies the entry, so a pointer taken from it dangles at the semicolon — every label
            //    then decayed to the same garbage and ImGui reported four conflicting IDs.
            std::uint32_t Withdrawing = OpenCount;

            // 📝 The node the previous tick's `+` named, so the workspace it registered is applied into the
            //    strip the artist actually pressed rather than always into the main dock space.
            const std::uint32_t ApplyInto = RegisterIntoNode;

            RegisterIntoNode = 0u;

            SketchSessionMilliseconds += Pass.ElapsedMilliseconds;
            RecordSketchRevisionSnapshot(
                ResolveSketchRevisionSnapshot(SketchNaming, Sketch, SketchWorld, SketchWorldMapping,
                                              SketchRecords, SketchRevisions,
                                              SketchWorkplanes, SketchPendingSelection,
                                              SketchSemanticSelection),
                SketchRetreated, SketchReinstated);
            SketchToolOptions.Advance(BackgroundPointer, Pass.ElapsedMilliseconds);
            SketchContextMenu.Advance(BackgroundPointer, Pass.ElapsedMilliseconds);
            WorkspacePanels.Advance(BackgroundPointer, Pass.ElapsedMilliseconds);

            // 🔴 THE TOOL PANEL ADVANCES BEFORE THE VIEWPORT READS THE ACTIVE TOOL. It used to run at
            //    the END of the frame, after the viewport had already dispatched -- so the click that
            //    picked Line was not visible to the drawing code until the NEXT frame, and the frame
            //    that mattered still held the previous tool. Picking a shape then clicking the grid
            //    drew nothing, because `ActiveSubject` was still `Select`.
            //
            // ⚠️ `ParametricSketchHost` had this right: it advanced at line 644 and drew at 776. A gate
            //    asserted the order -- and that gate named the host by its path, so when the host was
            //    deleted the CLAIM was deleted with it instead of being re-aimed. The claim below
            //    restores it against the file that ships.
            ParametricTools.Advance(BackgroundPointer, Pass.ElapsedMilliseconds,
                                    ParametricToolsApplied,
                                    TabPressed && !PointerBehindDrawer);

            // 📐 The fly camera is integrated BEFORE any leaf is recorded, so the sky geometry, the
            //    ground lattice and the gizmo are all projected through the SAME current-tick pose.
            //    The previous order advanced the camera AFTER recording, which left every overlay
            //    one frame behind the artist's input — the lattice and axes trailed the camera while
            //    panning or flying. The sky regeneration below depends only on the environment and is
            //    intentionally left where it stands.
            {
                bool PointerOverViewport = false;
                const PointerCondition& Pointer = Viewport.Surface().Pointer();

                // 🔴 THE HOVERED LEAF DECIDES, NOT LEAF ZERO. The free fly eye was advanced from the
                //    pointer being over ANY viewport, with no projection test at all, so a right-drag
                //    with WASD in a Top view panned the orthographic focus AND flew the hidden
                //    perspective eye sideways along an unrelated yaw. The next parallel transition then
                //    rebuilt the sketch standing from that drifted eye and the view appeared to
                //    teleport. While the leaf under the pointer is parallel, the orthographic standing
                //    is the only camera that moves.
                bool HoveredLeafPerspective = false;
                for (std::uint32_t Leaf = 0u; Leaf < WorkspacePanels.LeafCount(); ++Leaf)
                {
                    if (WorkspacePanels.LeafSubject(Leaf) == PanelSubject::Viewport &&
                        WorkspacePanels.LeafBody(Leaf).Encloses(Pointer.PositionX, Pointer.PositionY))
                    {
                        PointerOverViewport = true;
                        HoveredLeafPerspective = PanelConfiguration[Leaf].Perspective;
                        break;
                    }
                }

                if (Pointer.SecondaryPressed)
                {
                    EditorCameraLookLatched = PointerOverViewport && !PointerBehindDrawer
                                            && HoveredLeafPerspective;
                }
                if (Pointer.SecondaryReleased || !Pointer.SecondaryHeld)
                    EditorCameraLookLatched = false;

                CameraCondition FlyInput = Viewport.Seam().CameraInput(
                    (PointerOverViewport || EditorCameraLookLatched)
                    && !PointerBehindDrawer
                    && HoveredLeafPerspective);

                // A direct XYZ edit in the Editor Camera's Transform card is consumed before navigation.
                // The transform synchronizer distinguishes it from the values the camera published last tick,
                // so WASD movement is never reset by a stale UI mirror.
                static_cast<void>(EditorCamera.ConsumeTransform(SceneApplied.EntityPosition[6u],
                                                                SceneApplied.EntityRotation[6u]));

                EditorCamera.FlySpeed = std::clamp(SceneApplied.CameraSpeed, 1.0, 5000.0);
                EditorCamera.FieldOfViewDegrees = std::clamp(SceneApplied.CameraFieldOfView, 20.0, 150.0);
                EditorCamera.NearClipMetres = std::clamp(SceneApplied.CameraNearClip, 0.01, 10.0);
                EditorCamera.FarClipMetres = std::clamp(SceneApplied.CameraFarClip,
                                                        EditorCamera.NearClipMetres + 0.01, 100000.0);

                if (FlyInput.SpeedSteps != 0.0f)
                {
                    // 📐 Unreal-style fly-speed gearing: while right-button look owns the viewport,
                    //    each wheel notch changes the persistent Editor Camera speed by one 25% step.
                    //    It changes speed, not FOV or position, and the outliner control reflects it.
                    EditorCamera.AdjustFlySpeed(static_cast<double>(FlyInput.SpeedSteps));
                }

                SceneApplied.CameraSpeed       = EditorCamera.FlySpeed;
                SceneApplied.CameraFieldOfView = EditorCamera.FieldOfViewDegrees;
                SceneApplied.CameraNearClip    = EditorCamera.NearClipMetres;
                SceneApplied.CameraFarClip     = EditorCamera.FarClipMetres;

                CameraSettings FlySettings;
                FlySettings.FlySpeed    = EditorCamera.FlySpeed;
                FlySettings.LagEnabled  = (SceneApplied.DetailBits[6u] & 2u) != 0u;
                FlySettings.InvertPitch = (SceneApplied.DetailBits[6u] & 4u) != 0u;

                EditorCamera.Advance(Pass.ElapsedMilliseconds / 1000.0, FlyInput, FlySettings);

                SceneApplied.ViewportSkyCamera.AzimuthDegrees    = static_cast<float>(EditorCamera.LaggedYawDegrees);
                SceneApplied.ViewportSkyCamera.ElevationDegrees  = static_cast<float>(EditorCamera.LaggedPitchDegrees);
                SceneApplied.ViewportSkyCamera.FieldOfViewDegrees =
                    static_cast<float>(EditorCamera.FieldOfViewDegrees);
                SceneApplied.CameraPosition[0] = EditorCamera.LaggedPosition[0];
                SceneApplied.CameraPosition[1] = EditorCamera.LaggedPosition[1];
                SceneApplied.CameraPosition[2] = EditorCamera.LaggedPosition[2];
                SceneApplied.CameraRotation[0] = EditorCamera.LaggedYawDegrees;
                SceneApplied.CameraRotation[1] = EditorCamera.LaggedPitchDegrees;
                SceneApplied.CameraRotation[2] = 0.0;
                EditorCamera.PublishTransform(SceneApplied.EntityPosition[6u],
                                              SceneApplied.EntityRotation[6u]);
            }

            for (std::uint32_t Index = 0u; Index < OpenCount; ++Index)
            {
                const char* Titled = Workspaces.Titled(Index);

                if (Titled == nullptr)
                    continue;

                bool Current = true;

                const PlaneExtent PanelExtent = Viewport.Seam().EnterWorkspaceWindow(
                    Titled, !Workspaces.Applied(Index), ApplyInto, Current);
                Workspaces.Apply(Index);

                if (PanelExtent.Width() > 0.0f && PanelExtent.Height() > 0.0f)
                {
                    Discard(Viewport.Surface().SwitchToWindow());
                    // 🔴 The popups are deferred: the chrome's split/subject menus must record AFTER
                    //    the leaf content, or the sky quad textures over them and the menus become
                    //    unreadable — the reported defect when splitting a panel.
                    Discard(WorkspacePanels.Record(PanelExtent,
                                                      PanelPartitions[Index],
                                                      PanelConfiguration[Index],
                                                      Index,
                                                      true));

                    // 📝 The leaf content — the editor's scene directory inside the workspace's own
                    //    panels. Recorded into the same window the panel chrome was, so it clips and
                    //    orders with it: the sky fills a viewport leaf, the outliner | details fills
                    //    an outliner leaf, and the properties / camera bookmarks fill a properties leaf.
                    //    Panels draw their content only while they exist in the partition; there is
                    //    no fullscreen scene directory in this host (see the header's layout rule).
                    // 📝 Each viewport leaf owns its overlay record: the panel empties the leaf's
                    //    record and refills it with the grid, the axes and the gizmo projected for
                    //    THAT leaf. The host uploads each record when its generation changed and
                    //    the GPU pass draws each one clipped to its own leaf's box — so with two
                    //    viewports, each shows its own grid and neither leaks onto the panels.
                    ViewportLeafTally = 0u;
                    LayerLeafTally = 0u;

                    for (std::uint32_t Leaf = 0u; Leaf < WorkspacePanels.LeafCount(); ++Leaf)
                    {
                        const PlaneExtent LeafBody = WorkspacePanels.LeafBody(Leaf);

                        switch (WorkspacePanels.LeafSubject(Leaf))
                        {
                            case PanelSubject::Viewport:
                            {
                                ViewportRuntimeState& LeafRuntime = ViewportRuntime[Leaf];
                                // The local alias keeps the viewport phase readable while the state remains
                                // owned by this leaf rather than by the application-wide host.
                                ViewportStanding& SketchView = ViewportRuntime[Leaf].Standing;
                                OverlayGeometry& LeafOverlay = LeafRuntime.Overlay;
                                LeafRuntime.BeginFrame();

                                // 🔴 One press, one claimant. Scene selection, the workplane tool and
                                //    the drawing tools all read the same pointer, so the first to take
                                //    it stops the rest -- otherwise a click that picks a mesh also
                                //    starts a curve.
                                FrameContext.Dispatch.Reset();
                                bool& PointerTaken = FrameContext.Dispatch.Consumed;

                                // 🔴 ORIENTATION IS A VIEWPORT COMMAND, NOT A DRAWING CONTACT. Resolve it
                                //    before the camera, workplane, projection and tools are assembled for
                                //    this leaf. The old late hit-test changed the camera after the drawing
                                //    basis had already been selected, so a cube click could leave the next
                                //    operation using the previous plane. Making the camera the single
                                //    source of the selected orientation also means every consumer below
                                //    reads the same current-tick state.
                                CubeBasis GizmoBasis = CubeBasisFromYawPitch(
                                    SceneApplied.ViewportSkyCamera.AzimuthDegrees,
                                    SceneApplied.ViewportSkyCamera.ElevationDegrees);
                                const EditorPanelConfiguration& PanelDeclaredForGizmo = PanelConfiguration[Index];
                                if (BackgroundPointer.ContactPressed &&
                                    LeafBody.Encloses(BackgroundPointer.PositionX, BackgroundPointer.PositionY))
                                {
                                    const Deliver<ViewportOrientation> Hit = HitOrientationWidget(
                                        LeafBody, GizmoBasis, BackgroundPointer.PositionX, BackgroundPointer.PositionY,
                                        PanelDeclaredForGizmo.Gizmo == PanelGizmo::Cad);
                                    if (Hit.Resolved)
                                    {
                                        const ViewportOrientation TargetOrientation = Hit.Resolve();
                                        double Yaw = EditorCamera.YawDegrees;
                                        double Pitch = EditorCamera.PitchDegrees;
                                        OrientationYawPitch(TargetOrientation, Yaw, Pitch);
                                        EditorCamera.YawDegrees = Yaw;
                                        EditorCamera.PitchDegrees = Pitch;
                                        EditorCamera.Snap();
                                        SceneApplied.ViewportSkyCamera.AzimuthDegrees =
                                            static_cast<float>(EditorCamera.LaggedYawDegrees);
                                        SceneApplied.ViewportSkyCamera.ElevationDegrees =
                                            static_cast<float>(EditorCamera.LaggedPitchDegrees);
                                        SceneApplied.CameraRotation[0] = EditorCamera.LaggedYawDegrees;
                                        SceneApplied.CameraRotation[1] = EditorCamera.LaggedPitchDegrees;
                                        SceneApplied.EntityRotation[6u][0] = EditorCamera.LaggedYawDegrees;
                                        SceneApplied.EntityRotation[6u][1] = EditorCamera.LaggedPitchDegrees;

                                        SketchView.Orientation = TargetOrientation;
                                        SketchView.OrbitYaw = Yaw;
                                        SketchView.OrbitPitch = Pitch;
                                        static_cast<void>(ActivateViewedWorkplane(SketchWorkplanes, TargetOrientation, false));

                                        PointerTaken = true;
                                        FrameContext.Dispatch.AdoptLegacyOwner(PointerOwner::OrientationWidget);
                                        GizmoBasis = CubeBasisFromYawPitch(EditorCamera.LaggedYawDegrees,
                                                                          EditorCamera.LaggedPitchDegrees);
                                    }
                                }

                                SceneDirectory.RecordViewportSky(LeafBody, SceneApplied);
                                // 🔴 The same drawing the parametric host uses. It was written twice
                                //    because the shared projection could not express a free-flying
                                //    camera; `ResolveFreeCamera` now does. The editor works in METRES
                                //    and the parametric workspace in millimetres, so the unit scale is
                                //    named here rather than hidden inside the unit.
                                // 🔴 THE FOOTER'S ORTHO/PERSPECTIVE BUTTON, HONOURED. The panel stored
                                //    the artist's choice and nothing read it: the camera was resolved
                                //    perspective unconditionally and every overlay below was passed a
                                //    literal `true`. Pressing Ortho changed the label and nothing else.
                                // 🔴 THE PROJECTION EASES, IT DOES NOT SNAP. The footer's choice is
                                //    the DESTINATION; what is drawn is wherever the transit has got
                                //    to. `Perspective` below therefore asks the transit, not the
                                //    button, so the grid and the geometry flatten together over the
                                //    same quarter second instead of both cutting on one frame.
                                ProjectionTransit& Transit = ViewportRuntime[Leaf].Projection;
                                AdvanceProjectionTransit(Transit, Pass.ElapsedMilliseconds / 1000.0,
                                                         !PanelConfiguration[Index].Perspective);
                                const bool LeafPerspective = !Transit.Parallel();

                                // 🔴 A RIGHT-CLICK OPENS THE CONTEXT MENU, AND THE SAME BUTTON FLIES THE
                                //    CAMERA. Binding the menu to the press would repeat the mistake `Q`
                                //    made -- two features quietly sharing one input. A press that ends
                                //    without the pointer having travelled is a CLICK; one that dragged
                                //    was a look, and the camera has already consumed it. The travel is
                                //    accumulated over the whole hold rather than read on the release
                                //    tick, where it is always near zero.
                                if (BackgroundPointer.SecondaryPressed &&
                                    LeafBody.Encloses(BackgroundPointer.PositionX,
                                                      BackgroundPointer.PositionY))
                                {
                                    SecondaryTravel = 0.0f;
                                    SecondaryOpenX  = BackgroundPointer.PositionX;
                                    SecondaryOpenY  = BackgroundPointer.PositionY;
                                }

                                if (BackgroundPointer.SecondaryHeld)
                                {
                                    SecondaryTravel += std::fabs(BackgroundPointer.TravelX) +
                                                       std::fabs(BackgroundPointer.TravelY);
                                }

                                if (BackgroundPointer.SecondaryReleased &&
                                    SecondaryTravel <= SecondaryClickTravel &&
                                    LeafBody.Encloses(BackgroundPointer.PositionX,
                                                      BackgroundPointer.PositionY))
                                {
                                    // 📐 The pointer itself is the anchor: a right-click has no tile, so
                                    //    the popup hangs off a point rather than a button.
                                    // 📝 Operations are visual-only for now, so the stationary
                                    //    secondary-click gesture no longer reopens a construction popup.
                                    //    A dormant popup must stay shut even when an old tool state
                                    //    survives into this frame.
                                    // 🔴 A RIGHT-CLICK ABANDONS THE OPERATION, not merely its readout.
                                    //    Closing the popup alone would leave the gesture pending, and
                                    //    the next Apply from anywhere would commit a figure the artist
                                    //    had already dismissed.
                                    if (SketchContextMenu.Standing())
                                    {
                                        CancelCornerDragSession(SketchOperations.Corner);
                                        CancelSketchOperationSession(SketchOperations.Operation);
                                        SketchContextMenu.Close();
                                    }
                                }

                                ResolvedCamera& SceneCamera = ViewportRuntime[Leaf].Camera;
                                SceneCamera = ResolveFreeCamera(
                                    { SceneApplied.CameraPosition[0], SceneApplied.CameraPosition[1],
                                      SceneApplied.CameraPosition[2] },
                                    SceneApplied.ViewportSkyCamera.AzimuthDegrees,
                                    SceneApplied.ViewportSkyCamera.ElevationDegrees,
                                    SceneApplied.ViewportSkyCamera.FieldOfViewDegrees,
                                    LeafPerspective, SketchView.OrthoScale);

                                // 🔴 THE PARAMETRIC TOOLS, IN THE PRODUCT THAT SHIPS. This block is
                                //    why `ParametricSketchHost` existed. `HostHasFeature` is
                                //    `constexpr`, so the texture-only product compiles this away
                                //    while still TYPE-CHECKING it -- an `#ifdef` would not.
                                if constexpr (HostHasFeature(FeatureParametric))
                                {
                                    // 🔴 THE WIDGET IS OFFERED THE CONTACT FIRST. It floats OVER the
                                    //    leaf, so a press that lands on it is a press on it — offering
                                    //    it to the viewport first would drag the camera out from under
                                    //    the slider the artist was holding, and clicking the panel
                                    //    would deselect the very thing its options apply to.
                                    //
                                    // 📝 Select and the gizmo are ONE widget, so its rows are declared
                                    //    together: the element mode and reach that govern picking, then
                                    //    whether the gizmo is shown for what was picked.
                                    {
                                        const bool SelectMode =
                                            ParametricToolsApplied.ActiveSubject == ParametricToolSubject::Select;

                                        if (SelectMode)
                                        {
                                            // 🔴 FIVE MODES, AND THE ORDER IS THE GRAIN THEY WORK AT:
                                            //    a vertex, the edge between two, the face those bound, the
                                            //    whole object, then Free — which admits any of them. Reading
                                            //    left to right is reading from finest to coarsest, so the
                                            //    artist does not have to learn the order.
                                            static const char* const ElementCaptions[5] =
                                                { "Vertex", "Edge", "Face", "Object", "Free" };
                                            static const SymbolSubject ElementGlyphs[5] =
                                                { SymbolSubject::VertexPoint,
                                                  SymbolSubject::EdgeSegment,
                                                  SymbolSubject::FacePlanar,
                                                  SymbolSubject::CubeSolid,
                                                  SymbolSubject::CrosshairCentre };

                                            // 📝 The mode is held as an ordinal for the widget and read back
                                            //    into the enum, so the two can never disagree about which
                                            //    element is standing.
                                            std::uint32_t ElementSelected =
                                                static_cast<std::uint32_t>(SketchSelection.Element);

                                            OptionDeclaration SelectRows[3] = {};
                                            SelectRows[0].Kind        = OptionControl::Segmented;
                                            SelectRows[0].Caption     = "Mode";
                                            SelectRows[0].Selected    = &ElementSelected;
                                            SelectRows[0].Options     = ElementCaptions;
                                            SelectRows[0].Glyphs      = ElementGlyphs;
                                            SelectRows[0].OptionCount = 5u;

                                            SelectRows[1].Kind    = OptionControl::Slider;
                                            SelectRows[1].Caption = "Tolerance";
                                            SelectRows[1].Unit    = "px";
                                            SelectRows[1].Reading = &SketchSelection.Tolerance;
                                            SelectRows[1].Minimum = SelectionOptions::ToleranceMinimum;
                                            SelectRows[1].Maximum = SelectionOptions::ToleranceMaximum;

                                            SelectRows[2].Kind    = OptionControl::Toggle;
                                            SelectRows[2].Caption = "Show gizmo";
                                            SelectRows[2].Taken   = &SketchGizmo.Shown;

                                            Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Above));


                                            Discard(SketchToolOptions.Record(


                                                LeafBody, "Selection", SymbolSubject::CrosshairCentre,


                                                SelectRows, 3u, PointerTaken));


                                            Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Beneath));
                                            if (PointerTaken && FrameContext.Dispatch.Owner == PointerOwner::None)
                                                FrameContext.Dispatch.AdoptLegacyOwner(PointerOwner::PanelControl);

                                            if (ElementSelected <
                                                static_cast<std::uint32_t>(SelectionElement::ElementCount))
                                                SketchSelection.Element =
                                                    static_cast<SelectionElement>(ElementSelected);

                                            // 🔴 THE CONTEXT MENU MUST NOT DRAW ON TOP OF ANOTHER WIDGET, and
                                            //    it can only avoid what it is told about. The options widget
                                            //    is DRAGGABLE, so its box is declared here from what it
                                            //    actually recorded a moment ago rather than from a constant
                                            //    kept beside it -- a stale box steers the menu into the very
                                            //    widget it was trying to miss.
                                            SketchContextMenu.Avoid(SketchToolOptions.Occupies());
                                        }
                                        else
                                        {
                                            SketchContextMenu.Avoid({});
                                        }

                                    }

                                    // 🔴 ONE CAMERA, NOT TWO. The orbit angles were copied straight off
                                    //    the editor camera, but the orbit arm measures yaw against the
                                    //    sketch BASIS and places the eye at `Focus - Forward*Distance`,
                                    //    so the two resolved to genuinely different cameras -- forward
                                    //    vectors disagreeing by as much as a dot of -0.5. Sketch
                                    //    geometry consequently sat on a surface that slid out from
                                    //    under it as the artist orbited. The conversion derives the
                                    //    orbit standing FROM the free camera, so both descriptions
                                    //    resolve to one frame (verified over 225 orientations).
                                    const double PreservedScale = SketchView.OrthoScale;

                                    if (LeafPerspective)
                                    {
                                        static_cast<void>(SketchWorkplanes.Activate(
                                            SketchWorkplanes.StandingName(StandingWorkplane::Ground)));
                                    }
                                    else
                                    {
                                        if (SketchView.Orientation != ViewportOrientation::Isometric)
                                        {
                                            static_cast<void>(ActivateViewedWorkplane(
                                                SketchWorkplanes, SketchView.Orientation, false));
                                        }
                                        else
                                        {
                                            static_cast<void>(SketchWorkplanes.Activate(
                                                SketchWorkplanes.StandingName(StandingWorkplane::Ground)));
                                        }
                                    }

                                    const bool SketchHasCommittedGeometry =
                                        !Sketch.Curves().empty() || !Sketch.Profiles().empty();
                                    if (!Sketch.PlaneDeclared() || !SketchHasCommittedGeometry)
                                        Sketch.DeclarePlane({ SketchWorkplanes.Active().Origin,
                                                              SketchWorkplanes.Active().Normal,
                                                              SketchWorkplanes.Active().Along });

                                    const SpatialBasis SketchBasis = ResolveWorkplaneBasis(SketchWorkplanes.Active());
                                    ViewportRuntime[Leaf].ActiveWorkplane = SketchWorkplanes.ActiveName();

                                    const bool EnteredParallel = !ViewportRuntime[Leaf].WasParallel && !LeafPerspective;
                                    if (LeafPerspective || EnteredParallel)
                                    {
                                        SketchView = ResolveOrbitStandingFromFree(
                                            { SceneApplied.CameraPosition[0], SceneApplied.CameraPosition[1],
                                              SceneApplied.CameraPosition[2] },
                                            SceneApplied.ViewportSkyCamera.AzimuthDegrees,
                                            SceneApplied.ViewportSkyCamera.ElevationDegrees,
                                            SketchBasis);
                                        SketchView.OrthoScale = PreservedScale;
                                    }

                                    SketchView.FieldOfViewDegrees =
                                        SceneApplied.ViewportSkyCamera.FieldOfViewDegrees;

                                    if (!LeafPerspective)
                                    {
                                        double ViewYaw = SketchView.OrbitYaw;
                                        double ViewPitch = SketchView.OrbitPitch;
                                        if (SketchView.Orientation != ViewportOrientation::Isometric)
                                            OrientationYawPitch(SketchView.Orientation, ViewYaw, ViewPitch);
                                        SceneApplied.ViewportSkyCamera.AzimuthDegrees = static_cast<float>(ViewYaw);
                                        SceneApplied.ViewportSkyCamera.ElevationDegrees = static_cast<float>(ViewPitch);
                                        SceneApplied.CameraRotation[0] = ViewYaw;
                                        SceneApplied.CameraRotation[1] = ViewPitch;
                                        SceneApplied.EntityRotation[6u][0] = ViewYaw;
                                        SceneApplied.EntityRotation[6u][1] = ViewPitch;
                                    }

                                    // 🔴 THE WHEEL IS SPENT ONCE, INSIDE `DriveViewport`. A second zoom
                                    //    arm stood here and then zeroed `WheelY` before the call, which
                                    //    made the navigation unit's own wheel handling -- including the
                                    //    entire cursor anchor and the perspective dolly -- unreachable
                                    //    from the editor. Two implementations of one rule, and the live
                                    //    one was the weaker: sign-only, centre-anchored, clamped to a
                                    //    range the artist hit constantly.
                                    const CameraCondition FlyInput = Viewport.Seam().CameraInput(
                                        LeafBody.Encloses(BackgroundPointer.PositionX, BackgroundPointer.PositionY)
                                        && !PointerBehindDrawer);
                                    DriveViewport(LeafBody, BackgroundPointer,
                                                  Viewport.Seam().Modifiers(), SketchView, LeafPerspective,
                                                  FlyInput, Pass.ElapsedMilliseconds / 1000.0,
                                                  ViewportRuntime[Leaf].Navigation);
                                    ViewportRuntime[Leaf].WasParallel = !LeafPerspective;

                                    SceneCamera = LeafPerspective
                                        ? ResolveFreeCamera(
                                            { SceneApplied.CameraPosition[0], SceneApplied.CameraPosition[1],
                                              SceneApplied.CameraPosition[2] },
                                            SceneApplied.ViewportSkyCamera.AzimuthDegrees,
                                            SceneApplied.ViewportSkyCamera.ElevationDegrees,
                                            SceneApplied.ViewportSkyCamera.FieldOfViewDegrees,
                                            true, SketchView.OrthoScale)
                                        : ResolveOrbitCamera(SketchBasis, SketchView, false);

                                    // 🔴 THE OPERATIONS RUN AFTER THE CAMERA THEY AIM THROUGH IS SETTLED.
                                    //    They used to run further up, against `SceneCamera` as it stood
                                    //    at the top of the leaf -- but on an ORTHOGRAPHIC leaf that value
                                    //    is replaced a few lines above by an ORBIT camera resolved from
                                    //    the sketch basis, and the two are genuinely different cameras:
                                    //    measured over four yaws their forward vectors disagree by a dot
                                    //    of -0.42 and their eyes sit 100 units apart. Every operation
                                    //    therefore un-projected the pointer through a camera the artist
                                    //    was not looking through, landed somewhere else entirely on the
                                    //    workplane, reached no curve and no corner, and did nothing --
                                    //    which is exactly the report. In PERSPECTIVE the two agree
                                    //    exactly (dot +1.000000), which is why this looked intermittent
                                    //    rather than broken.
                                    //
                                    // 📝 It also puts them after the active workplane has been chosen for
                                    //    this frame, so the plane they intersect is the one being drawn on
                                    //    rather than the one left standing by the previous frame.
                                    //
                                    // 📝 ONLY THE READOUT IS THE HOST'S BUSINESS. It contributes exactly
                                    //    two things the arm cannot know: the box the readout must dodge,
                                    //    which only the host can see, and the standing selection an
                                    //    Offset copies. Everything else is inside the arm.
                                    if (OperationToolStanding(ParametricToolsApplied.ActiveSubject))
                                    {
                                        // 📝 An Offset copies what is selected. The picks are already
                                        //    resolved to world names for the renderer just below; the
                                        //    curves among them are the chain.
                                        std::vector<WorldCurveName> OperationChain;
                                        for (const SketchPick& Item : SketchSelectionSetState.Items)
                                        {
                                            WorldPick Held = {};
                                            if (ResolveWorldPickForSketchPick(Sketch, SketchRecords,
                                                                              SketchWorld,
                                                                              SketchWorldMapping,
                                                                              Item, Held) &&
                                                Held.Subject == WorldPickSubject::Curve)
                                                OperationChain.push_back(Held.Curve);
                                        }

                                        Discard(Viewport.Surface().SwitchLayer(
                                            RecordingSurface::ShellLayer::Above));

                                        bool OperationTaken = false;
                                        DriveSketchOperations(LeafBody, BackgroundPointer, SceneCamera,
                                                              ParametricToolsApplied.ActiveSubject,
                                                              ResolveWorkplacementFrame(
                                                                  SketchWorkplanes.Active()),
                                                              OperationChain, SketchWorld,
                                                              SketchOperations, SketchContextMenu,
                                                              OperationTaken);

                                        Discard(Viewport.Surface().SwitchLayer(
                                            RecordingSurface::ShellLayer::Beneath));

                                        if (OperationTaken)
                                        {
                                            PointerTaken = true;
                                            if (FrameContext.Dispatch.Owner == PointerOwner::None)
                                                FrameContext.Dispatch.AdoptLegacyOwner(
                                                    PointerOwner::DrawingTool);
                                        }
                                    }

                                    // 🔴 THE ANNOTATION BAND RUNS HERE, and the host's whole
                                    //    contribution is one pick. The band was written, given
                                    //    thirteen tiles, and then reachable from nowhere: no
                                    //    `BandEntry` listed it and `ToolSubjectOf` had no case for
                                    //    it, so every tile reported `Select`. Both halves are fixed
                                    //    in the panel; this is the third -- the tools now exist,
                                    //    resolve to their own subjects, and are finally driven.
                                    //
                                    // 📝 A dimension is placed against something, so what the
                                    //    pointer is over has to be resolved before the arm can be
                                    //    asked. The same screen picker the selection uses answers
                                    //    it, at the same tolerance, so a dimension grabs exactly
                                    //    what a click would have selected.
                                    if (AnnotationToolStanding(ParametricToolsApplied.ActiveSubject))
                                    {
                                        WorldPick AnnotationHover = {};
                                        static_cast<void>(ResolveWorldSketchPick(
                                            SketchWorld, SceneCamera, LeafBody,
                                            static_cast<float>(BackgroundPointer.PositionX),
                                            static_cast<float>(BackgroundPointer.PositionY),
                                            SketchSelection.ResolvedTolerance(),
                                            AnnotationHover));

                                        Discard(Viewport.Surface().SwitchLayer(
                                            RecordingSurface::ShellLayer::Above));

                                        bool AnnotationTaken = false;
                                        DriveAnnotations(LeafBody, BackgroundPointer, SceneCamera,
                                                         ParametricToolsApplied.ActiveSubject,
                                                         ResolveWorkplacementFrame(
                                                             SketchWorkplanes.Active()),
                                        AnnotationHover,
                                        ResolveDimensionFigureAt(
                                            SketchDimensionFigures,
                                            BackgroundPointer.PositionX,
                                            BackgroundPointer.PositionY),
                                        SketchWorld,
                                        SketchAnnotations, SketchContextMenu,
                                        AnnotationTaken);

                                        Discard(Viewport.Surface().SwitchLayer(
                                            RecordingSurface::ShellLayer::Beneath));

                                        if (AnnotationTaken)
                                        {
                                            PointerTaken = true;
                                            if (FrameContext.Dispatch.Owner == PointerOwner::None)
                                                FrameContext.Dispatch.AdoptLegacyOwner(
                                                    PointerOwner::DrawingTool);
                                        }
                                    }

                                    // 🔴 The workplane tool is offered the press FIRST and consumes
                                    //    it, or the click that places a plane is also read as the
                                    //    first point of a curve -- drawn onto the plane it replaced.
                                    PointerTaken = PointerTaken || ApplyWorkplaneTool(
                                        LeafBody, BackgroundPointer, SketchBasis, SketchView,
                                        LeafPerspective,
                                        ParametricToolsApplied, SketchNaming, Sketch, SketchRecords,
                                        SketchRevisions, SketchWorkplanes);
                                    if (PointerTaken && FrameContext.Dispatch.Owner == PointerOwner::None)
                                        FrameContext.Dispatch.AdoptLegacyOwner(PointerOwner::DrawingTool);

                                    if (!PointerTaken)
                                        DriveDrawingWithModifiers(
                                            LeafBody, BackgroundPointer,
                                            SketchViewportText, Viewport.Seam().Modifiers(),
                                            SketchBasis, SketchView, LeafPerspective,
                                            ParametricToolsApplied, SketchNaming, Sketch,
                                            SketchWorld, SketchWorldMapping,
                                            SketchRecords, SketchRevisions, SketchWorkplanes,
                                            SketchPendingSelection, SketchTool, PointerTaken);

                                    // 🔴 THE CALL THAT WAS MISSING. Selection, the gizmo and the whole
                                    //    Blender-style command path were implemented, gated correctly
                                    //    on the Select tool, proven in isolation — and never invoked.
                                    //    The tool was not broken; nothing ever ran it.
                                    //
                                    // 📝 It follows the drawing arm and reads `PointerTaken`, so a tool
                                    //    that is placing anchors keeps the press. With Select active
                                    //    `SelectedTool(...).Subject` is `None`, the drawing arm takes
                                    //    nothing, and every press reaches the picker.
                                    ProjectWorkspaceDirectory(SketchRecords, SketchDirectoryRows);
                                    const Deliver<bool> DirectoryPresentation = SynchroniseParametricPresentation(
                                        SketchRecords, SketchRevisions, SketchDirectoryRows,
                                        SketchDirectoryPresented, SketchDirectoryApplied,
                                        SketchPendingSelection, SketchDirectorySeeded,
                                        &SketchSelectionSetState);
                                    if (!DirectoryPresentation.Resolved)
                                        SketchDirectoryPresented.Reclaim();
                                    DriveViewportSelectionAndTransformWorldBacked(
                                         LeafBody, BackgroundPointer,
                                         SketchViewportText, Viewport.Seam().Modifiers(),
                                         ParametricToolsApplied.ActiveSubject,
                                         SketchSelection, SketchGizmo,
                                         SceneCamera,
                                         SketchDirectoryRows, SketchDirectoryApplied,
                                         SketchNaming,
                                         Sketch, SketchWorld, SketchWorldMapping,
                                         SketchRecords, SketchRevisions,
                                         SketchPendingSelection, SketchSemanticSelection,
                                         SketchSelectionSetState,
                                         SketchHoveredSelection, SketchWorldTransform, LeafOverlay,
                                         PointerTaken, SketchSessionMilliseconds,
                                         SketchLastMovePressed);

                                    if (PointerTaken && FrameContext.Dispatch.Owner == PointerOwner::None)
                                    {
                                        const PointerOwner Owner = SketchWorldTransform.Engaged()
                                            ? PointerOwner::Gizmo
                                            : (ParametricToolsApplied.ActiveSubject == ParametricToolSubject::Select
                                                 ? PointerOwner::Selection : PointerOwner::DrawingTool);
                                        FrameContext.Dispatch.AdoptLegacyOwner(Owner);
                                    }

                                    // 🔴 NO SECOND GRID. `RecordViewportGridOverlay` was called here and
                                    //    drew 161 CPU line segments of its OWN lattice on top of the
                                    //    analytic ground the overlay pass already draws on the GPU.
                                    //    Worse, it projected them through `SketchView` -- an ORBIT
                                    //    camera -- while the scene and the real grid use the free
                                    //    editor camera, and the two resolve `Right` with opposite
                                    //    signs. That is why the phantom grid tracked correctly up and
                                    //    down but ran the wrong way left and right, and why drawn
                                    //    shapes appeared to sit on a surface that slid under them.
                                    //    The existing grid is the grid; the sketch draws onto it.
                                    const DrawableScale SketchDrawable = ViewportDrawable;
                                    // 🔴 The CAD packet is rasterised in physical framebuffer pixels. An
                                    //    orthographic camera's scale is pixels per world unit, so the
                                    //    render copy must carry the same logical-to-physical factor as
                                    //    the packet extent. Picking keeps the logical camera unchanged.
                                    ResolvedCamera RenderCamera = SceneCamera;
                                    if (!RenderCamera.Perspective)
                                        RenderCamera.OrthoScale *= SketchDrawable.Factor;
                                    WorldSelectionSet ActiveWorldSelection = {};
                                    for (const SketchPick& Item : SketchSelectionSetState.Items)
                                    {
                                        WorldPick WPick = {};
                                        if (ResolveWorldPickForSketchPick(Sketch, SketchRecords, SketchWorld, SketchWorldMapping, Item, WPick))
                                            SetWorldPick(ActiveWorldSelection, WPick, true);
                                    }
                                    Discard(ProjectWorldBackedSketchRendering(SketchWorld, RenderCamera,
                                                                              LeafBody, SketchDrawable,
                                                                              SketchCadPacket, ActiveWorldSelection));

                                    // 🔴 THE DIMENSIONS GO IN THE SAME PACKET AS THE CURVES, appended
                                    //    after them and before the preview, so annotation rasterises
                                    //    on the GPU with everything else rather than becoming the one
                                    //    thing walked into draw lists by hand every frame.
                                    // 📝 The extent is the PHYSICAL one the curves were just projected
                                    //    into -- `SketchDrawable.Body` -- because the packet is in
                                    //    framebuffer pixels. Passing the logical leaf here would put
                                    //    every figure at the wrong place on a scaled display.
                                    Discard(ProjectWorldSketchDimensions(
                                        SketchWorld, RenderCamera,
                                        SketchDrawable.ToPhysical(LeafBody),
                                        SketchAnnotations.Unit, SketchCadPacket,
                                        SketchDimensionFigures));

                                    // 🔴 AND THE OPERATION'S PREVIEW ON TOP OF BOTH. Every operation
                                    //    resolved its target and its verdict correctly and NONE of it
                                    //    was ever drawn, so a fillet showed a slider while the corner
                                    //    stayed visibly sharp, and Trim and Cut asked for a click
                                    //    without saying what would be destroyed. The geometry was never
                                    //    the defect; nothing was putting it on screen.
                                    // 📝 Last into the packet, so it layers over the curves it
                                    //    describes, and in the same PHYSICAL extent they were projected
                                    //    into.
                                    Discard(ProjectOperationPreview(
                                        SketchOperations, ParametricToolsApplied.ActiveSubject,
                                        RenderCamera, SketchDrawable.ToPhysical(LeafBody),
                                        SketchCadPacket));

                                    // 🔴 THE LINE WORK IS ON THE GPU; ONLY THE FIGURES COME BACK. The
                                    //    CAD packet carries segments, triangles and markers -- it has
                                    //    no glyphs, and giving it any would mean a font atlas in the
                                    //    shared shader header. So the chips are drawn here, with the
                                    //    same text recorder as the rest of the interface.
                                    // 📝 Back in LOGICAL points: the chips came out of the projection
                                    //    in framebuffer pixels, and the recorder works in the units the
                                    //    interface reports.
                                    for (const DimensionFigureChip& Chip : SketchDimensionFigures)
                                    {
                                        const float Scale = static_cast<float>(SketchDrawable.Factor);
                                        if (Scale <= 0.0f)
                                            continue;

                                        const PlaneExtent ChipBody = {
                                            Chip.Body.MinimumX / Scale, Chip.Body.MinimumY / Scale,
                                            Chip.Body.MaximumX / Scale, Chip.Body.MaximumY / Scale
                                        };
                                        if (!LeafBody.Encloses(ChipBody.MinimumX, ChipBody.MinimumY))
                                            continue;

                                        // 📝 A backing chip, because a figure drawn straight over the
                                        //    geometry it measures is unreadable exactly where it
                                        //    matters most -- on top of a dense drawing.
                                        Viewport.Surface().Ground(ChipBody, Partial(0x12121au, 0.82),
                                                                  3.0f);
                                        Viewport.Surface().TextRun(
                                            Chip.TextX / Scale,
                                            Chip.TextY / Scale + 10.0f,
                                            Chip.Selected ? ColourPrimary : ColourValue,
                                            Chip.Figure, 12.0f);
                                    }

                                    // 🔴 A WITHDRAWN CONSTRAINT IS REPORTED, NEVER SILENT. The dimension
                                    //    outranks the constraints, so typing a length can dissolve a
                                    //    relation the artist set up earlier. Doing that quietly is how a
                                    //    modeller loses the artist's trust in its own model; saying so
                                    //    turns the same act into a tool doing what it was told.
                                    // 📝 Drawn where the work is rather than in a status bar nobody
                                    //    reads, and only while the notice is fresh.
                                    if (SketchAnnotations.NoticeStanding())
                                    {
                                        char Notice[96] = {};
                                        std::snprintf(Notice, sizeof(Notice),
                                                      SketchAnnotations.RetiredCount == 1u
                                                          ? "Dimension applied - %u constraint withdrawn"
                                                          : "Dimension applied - %u constraints withdrawn",
                                                      SketchAnnotations.RetiredCount);

                                        const PlaneExtent NoticeBody =
                                            Spanning(LeafBody.MinimumX + 12.0f,
                                                     LeafBody.MinimumY + 12.0f, 268.0f, 24.0f);
                                        Viewport.Surface().Ground(NoticeBody,
                                                                  Partial(0x2a2118u, 0.92f), 4.0f);
                                        Viewport.Surface().TextRun(NoticeBody.MinimumX + 10.0f,
                                                                   NoticeBody.MinimumY + 16.0f,
                                                                   Covering(0xfacc15u), Notice, 12.0f);
                                    }

                                    // 🔴 THE SHAPE BEING DRAWN GOES IN THE SAME PACKET AS THE SHAPES
                                    //    ALREADY DRAWN, so the GPU pass rasterises both and NOTHING
                                    //    about sketch geometry is left on the CPU. It was the last
                                    //    holdout: committed shapes moved to `WorkspaceCadPass`, but the
                                    //    preview under the pointer was still walked into ImGui draw
                                    //    lists every single frame -- the most redrawn geometry in the
                                    //    editor was the one piece still being tessellated per frame by
                                    //    the CPU.
                                    //
                                    // 🔴 It also previewed by naming ONE SUBJECT PER BRANCH, and four
                                    //    curve subjects had no branch: Hermite, basis spline and NURBS
                                    //    drew no feedback at all, and Bezier drew a line with no
                                    //    control points. `ResolvePlacementCurve` answers for every
                                    //    subject through the same evaluator the commit uses, and the
                                    //    anchors come back as markers.
                                    if (SketchTool.Standing() && SketchTool.HoverStanding())
                                    {
                                        // 📝 Static: reused every tick, like the packet beside it.
                                        static std::vector<CurveSpecification> PreviewSpans;
                                        // 📝 The wheel-chosen side count reaches the preview, so
                                        //    scrolling a polygon redraws it at the new resolution
                                        //    instead of showing an unchanging circle.
                                        // 🔴 The WHOLE tool is passed, not its anchors: a slot is
                                        //    drawn in two phases through one anchor list, so the
                                        //    resolver has to be told which phase it is in rather
                                        //    than counting points and guessing.
                                        ResolvePlacementCurves(SketchTool, SketchBasis, PreviewSpans);
                                        static_cast<void>(ProjectWorldPlacementPreview(
                                            RenderCamera, LeafBody, SketchDrawable, PreviewSpans,
                                            SketchTool.Anchors(), SketchTool.HoverPosition(),
                                            SketchCadPacket));
                                    }

                                    // 🔴 GPU-ONLY SKETCH RASTER. The sketch packet and its live preview
                                    //    now travel only through the CAD pass, so this viewport arm no
                                    //    longer keeps a second interface path alive beside it.
                                }

                                // 🔴 Clicking a mesh in the viewport selects it. Lived only in the
                                //    deleted host; the editor drew scene proxies but could not pick one.
                                if (!PointerTaken && BackgroundPointer.ContactPressed &&
                                    LeafBody.Encloses(BackgroundPointer.PositionX, BackgroundPointer.PositionY))
                                    PointerTaken = SelectSceneMeshAtPointer(
                                        LeafBody, BackgroundPointer, SceneCamera,
                                        OpenedScene, OpenedSceneStanding, WorkspaceSceneRows,
                                        CodexMetresToMetres, SceneApplied);
                                if (PointerTaken && FrameContext.Dispatch.Owner == PointerOwner::None)
                                    FrameContext.Dispatch.AdoptLegacyOwner(PointerOwner::Scene);

                                RecordCodexSceneProxy(Viewport.Surface(), LeafBody, SceneCamera,
                                                      OpenedScene, OpenedSceneStanding,
                                                      WorkspaceSceneRows, SceneApplied,
                                                      CodexMetresToMetres);
                                Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Above));
                                RecordOrientationWidget(Viewport.Surface(), LeafBody, GizmoBasis,
                                                        PanelDeclaredForGizmo.Gizmo == PanelGizmo::Cad);
                                Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Beneath));

                                // 📐 The ground lattice is no longer recorded here. It is solved per
                                //    pixel in the overlay pass's mode 3, from the camera pushed below,
                                //    so the CPU hands over a pose rather than a thousand segments.
                                SceneDirectory.RecordGizmo(LeafBody, SceneApplied, LeafOverlay);

                                // 📐 The pose the analytic ground reads. Assembled here because the
                                //    host owns the EditorCameraComponent and the leaf's extent both.
                                {
                                    OverlayGroundPose& Pose = LeafOverlay.Ground;
                                    const EditorPanelConfiguration& PanelDeclared = PanelConfiguration[Index];

                                    // 🔴 THE GRID MUST NOT DISAPPEAR IN ORTHOGRAPHIC MODE. The analytic
                                    // ground is valid for every resolved camera frame, including free
                                    // orthographic navigation, not only named Front/Back/Left/Right views.
                                    Pose.Standing = PanelDeclared.Lattice != PanelLatticePresentation::None;

                                    // 🔴 The analytic grid consumes the same frame as the settled camera.
                                    //    In particular, Front/Back and Left/Right may have their eye on
                                    //    Z=0 or X=0; the resolved orthographic standing moves the frame
                                    //    to a valid side of the active plane while preserving the screen
                                    //    basis used by placement and rendering.
                                    const ViewFrame& GridFrame = SceneCamera.Frame;
                                    Pose.EyeX = static_cast<float>(GridFrame.Eye.Left);
                                    Pose.EyeY = static_cast<float>(GridFrame.Eye.Up);
                                    Pose.EyeZ = static_cast<float>(GridFrame.Eye.Forward);

                                    Pose.ForwardX = static_cast<float>(GridFrame.Forward.Left);
                                    Pose.ForwardY = static_cast<float>(GridFrame.Forward.Up);
                                    Pose.ForwardZ = static_cast<float>(GridFrame.Forward.Forward);
                                    Pose.RightX   = static_cast<float>(GridFrame.Right.Left);
                                    Pose.RightY   = static_cast<float>(GridFrame.Right.Up);
                                    Pose.RightZ   = static_cast<float>(GridFrame.Right.Forward);
                                    Pose.UpX      = static_cast<float>(GridFrame.Up.Left);
                                    Pose.UpY      = static_cast<float>(GridFrame.Up.Up);
                                    Pose.UpZ      = static_cast<float>(GridFrame.Up.Forward);

                                    const double HalfV = SceneCamera.FieldOfViewDegrees
                                                       * 0.5 * 3.14159265358979323846 / 180.0;
                                    const double Aspect = (LeafBody.Height() > 0.0f)
                                                        ? static_cast<double>(LeafBody.Width())
                                                        / static_cast<double>(LeafBody.Height()) : 1.0;

                                    Pose.TanHalfV = static_cast<float>(std::tan(HalfV));
                                    Pose.TanHalfH = static_cast<float>(std::tan(HalfV) * Aspect);

                                    const double DeclaredCell = PanelDeclared.LatticeCellMetres > 0.0
                                                              ? PanelDeclared.LatticeCellMetres : 1.0;
                                    Pose.Cell = static_cast<float>(DeclaredCell
                                              * static_cast<double>(PanelDeclared.LatticeScale));

                                    Pose.Presentation = static_cast<std::uint32_t>(PanelDeclared.Lattice);
                                    Pose.ViewedOrientation = static_cast<std::uint32_t>(SketchView.Orientation);
                                    // 🔴 The grid flattens with everything else. Zero keeps the
                                    //    perspective ray-march; a positive scale selects parallel rays
                                    //    so the lattice and the geometry drawn on it agree.
                                    // 🔴 THE GRID EASES WITH THE GEOMETRY. Flattening the lattice on
                                    //    the frame the transit completes, while the shapes on it had
                                    //    been easing for a quarter second, would put the two on
                                    //    different projections for the whole flight -- the same
                                    //    disagreement that made shapes look like they floated. The
                                    //    fragment stage reads a scale, so the transit is expressed
                                    //    as the scale that matches the blend at this instant.
                                    // 📝 The resolver reasons in double so the blend does not step;
                                    //    the fragment stage reads Real32, so narrow once, here, where
                                    //    it is visible, rather than letting the compiler do it quietly.
                                    Pose.OrthoScale = LeafPerspective ? 0.0f
                                        : static_cast<Real32>(ResolveTransitGroundScale(
                                            Transit, SceneApplied.ViewportSkyCamera.FieldOfViewDegrees,
                                            SketchView.OrthoScale, SketchView.Distance,
                                            LeafBody.Height()));
                                    Pose.LineWeight   = PanelDeclared.LatticeLineWeight;
                                    Pose.DotRadius    = PanelDeclared.LatticeDotRadius;
                                    Pose.Subdivisions = PanelDeclared.Subdivisions > 0u
                                                      ? static_cast<float>(PanelDeclared.Subdivisions) : 10.0f;
                                    Pose.ExtentMetres = static_cast<float>(
                                        std::max(PanelDeclared.LatticeExtentMetres, DeclaredCell));
                                    Pose.FadeRadiusMetres = static_cast<float>(
                                        std::max(PanelDeclared.LatticeFadeRadiusMetres, DeclaredCell));

                                    std::uint32_t Mask = 0u;
                                    if (PanelDeclared.AxisX) Mask |= 1u;
                                    if (PanelDeclared.AxisY) Mask |= 2u;
                                    if (PanelDeclared.AxisZ) Mask |= 4u;
                                    Pose.AxisMask = Mask;
                                }

                                // 🔴 GPU-ONLY OVERLAY RECORDING. The host still tracks each viewport
                                //    leaf's overlay geometry for upload, but a missing overlay pass is
                                //    now an explicit failure rather than a cue to draw a second CPU copy.
                                if (ViewportLeafTally < PanelStructure::RecordLimit)
                                {
                                    ViewportLeafIndexs[ViewportLeafTally] = Leaf;
                                    ViewportLeafRects[ViewportLeafTally]    = LeafBody;

                                    if constexpr (HostHasFeature(FeatureParametric))
                                    {
                                        // 🔴 Measured from the two extents seen THIS frame. A reported
                                        //    scale can be a frame stale after the window changes
                                        //    monitor, and a stale scale clips the wrong region.
                                        ViewportRuntime[Leaf].CadProjection =
                                            ResolveWorldSketchScreenProjection(Pass.Width, Pass.Height);
                                    }

                                    ++ViewportLeafTally;
                                }
                                break;
                            }
                            case PanelSubject::SketchDirectory:
                                SketchDirectory.RecordOutliner(
                                    LeafBody, SketchDirectoryApplied,
                                    SketchDirectoryPresented.DirectoryRows.empty()
                                        ? nullptr : SketchDirectoryPresented.DirectoryRows.data(),
                                    static_cast<std::uint32_t>(SketchDirectoryPresented.DirectoryRows.size()),
                                    &SketchDirectoryPresented.Property,
                                    SketchDirectoryPresented.RevisionRows.empty()
                                        ? nullptr : SketchDirectoryPresented.RevisionRows.data(),
                                    static_cast<std::uint32_t>(SketchDirectoryPresented.RevisionRows.size()));
                                break;

                            case PanelSubject::ParametricTools:
                            {
                                // 🔴 THE CATALOGUE IS TOLD WHAT IS ACTUALLY SELECTED. Every tool in the
                                //    "Sketch Modify" band — Fillet, Chamfer, Trim, Extend, Offset —
                                //    declares `MinimumDimension = Edge`, and `DimensionAccepted` HIDES a
                                //    tool whose minimum the active dimension does not reach. Nothing ever
                                //    set `ActiveDimension` from the real sketch: it stayed at `Nothing`
                                //    for the whole session, so those five tiles could never appear and
                                //    the artist reported the operations as missing. They were not
                                //    missing; they were permanently filtered out.
                                //
                                //    Worse, it deadlocked: the operations needed a selection to appear,
                                //    and appear they must before an artist can tell the catalogue what
                                //    they mean to operate ON. The standing pick answers it directly.
                                //
                                // 📝 The dimension is the GRAIN of what is held, so it reads from the
                                //    pick's own subject rather than from the mode the widget is showing —
                                //    what is selected, not what could be.
                                {
                                    const SketchPick& Held = SketchSemanticSelection;
                                    const SketchPick& Displayed = Held.Standing()
                                                              ? Held
                                                              : SketchHoveredSelection;

                                    ParametricToolsApplied.ActiveDimension =
                                        !Displayed.Standing()                             ? ParametricToolDimension::Nothing
                                      : Displayed.Subject == SketchPickSubject::Point    ? ParametricToolDimension::Vertex
                                      : Displayed.Subject == SketchPickSubject::Control  ? ParametricToolDimension::Vertex
                                      : Displayed.Subject == SketchPickSubject::Curve    ? ParametricToolDimension::Edge
                                      : Displayed.Subject == SketchPickSubject::Record   ? ParametricToolDimension::Wire
                                                                                          : ParametricToolDimension::Nothing;

                                    ParametricToolsApplied.SelectedCount = Displayed.Standing() ? 1u : 0u;

                                    // 🔴 A WORKPLANE IS ALWAYS STANDING once the sketch has one, and the
                                    //    sketch is planed every frame above. Left false, the whole sketch
                                    //    half of the catalogue gates itself off.
                                    ParametricToolsApplied.WorkplaneActivation = Sketch.Declared();

                                    // 📝 What the sketch actually holds, so the raising tools gate on
                                    //    geometry rather than on a preset button nobody pressed.
                                    ParametricToolsApplied.ProfileCount = SketchRecords.DeclaredCount();
                                }

                                const ParametricToolSubject Before = ParametricToolsApplied.ActiveSubject;
                                Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Above));

                                ParametricTools.Record(LeafBody, ParametricToolsApplied);

                                Discard(Viewport.Surface().SwitchLayer(RecordingSurface::ShellLayer::Beneath));

                                // 🔴 CHOOSING A CONSTRUCTION TOOL RAISES ITS POPUP. This is the whole
                                //    gesture: the tile starts the operation, the popup asks how far, and
                                //    Apply performs it. Watching for the CHANGE rather than the current
                                //    value matters -- reading the value alone would reopen the popup
                                //    every frame the tile stayed active, including the frame after the
                                //    artist cancelled it.
                                // 🔴 CHOOSING A DIFFERENT OPERATION ABANDONS THE ONE IN FLIGHT. The tile
                                //    no longer raises a popup by itself -- the popup belongs to the
                                //    GESTURE, and appears when there is a figure to show. What the tile
                                //    change must do is make sure a half-dragged fillet does not survive
                                //    into Trim and apply itself later under the wrong tool.
                                const ParametricToolSubject Chosen = ParametricToolsApplied.ActiveSubject;
                                if (Chosen != Before &&
                                    (OperationToolStanding(Chosen) || OperationToolStanding(Before)))
                                {
                                    CancelCornerDragSession(SketchOperations.Corner);
                                    CancelSketchOperationSession(SketchOperations.Operation);
                                    SketchContextMenu.Close();
                                }
                                break;
                            }

                            case PanelSubject::Outliner:
                                if (PanelConfiguration[Index].FooterDemand == EditorFooterDemand::SceneImport ||
                                    PanelConfiguration[Index].FooterDemand == EditorFooterDemand::SceneExport)
                                {
                                    SceneApplied.TransferMode =
                                        PanelConfiguration[Index].FooterDemand == EditorFooterDemand::SceneExport ? 1u : 0u;
                                    SceneApplied.OutlinePage = 2u;
                                    PanelConfiguration[Index].FooterDemand = EditorFooterDemand::None;
                                }
                                SceneDirectory.RecordOutliner(LeafBody, SceneApplied, PresentedEntities, PresentedEntityCount);
                                if (SceneApplied.TransferDemand == SceneTransferDemand::Import)
                                {
                                    const Deliver<GeometryAssetView> Imported = GeometryTransfer.Import(
                                        SceneApplied.TransferLocation, SceneApplied.TransferName,
                                        ImportedGeometry, ImportedIntake);
                                    if (!Imported.Resolved)
                                    {
                                        std::printf("%s — geometry import refused (reason %u: %s)\n", HostName,
                                                    static_cast<unsigned>(Imported.Error.DeclaredReason), Imported.Error.Detail);
                                    }
                                    else
                                    {
                                        const Deliver<OwnerIdentity> Owner = ImportedOwners.Register();
                                        const std::uint32_t Base = ImportedVisibility.DeclaredPartitionCount();
                                        const Deliver<std::uint32_t> Registered = Owner.Resolved
                                            ? ImportedVisibility.Register(Owner.Resolve(), *Imported.Resolve().Topology,
                                                                         *Imported.Resolve().Conditioning, ImportedPartitions)
                                            : Deliver<std::uint32_t>::Refuse(Owner.Error);
                                        const Deliver<GeometryRenderingIdentity> Rendered = Registered.Resolved
                                            ? ImportedRendering.Synchronise(Imported.Resolve())
                                            : Deliver<GeometryRenderingIdentity>::Refuse(Registered.Error);
                                        if (!Rendered.Resolved)
                                        {
                                            std::printf("%s — imported topology could not prepare visibility (reason %u: %s)\n",
                                                        HostName, static_cast<unsigned>(Rendered.Error.DeclaredReason),
                                                        Rendered.Error.Detail);
                                        }
                                        else
                                        {
                                            PendingRendering = Rendered.Resolve();
                                            PendingVisibilityRegistration = Registered.Resolve();
                                            PendingRegistrationBase = Base;
                                            GeometryAdmissionPending = true;
                                        }
                                    }
                                    SceneApplied.TransferDemand = SceneTransferDemand::None;
                                }
                                break;
                            case PanelSubject::Properties:
                                SceneDirectory.RecordProperties(LeafBody, SceneApplied,
                                                                PresentedEntities, PresentedEntityCount, SceneApplied.InspectorTab);
                                break;
                            case PanelSubject::Texturing:
                                if (PanelConfiguration[Index].FooterDemand == EditorFooterDemand::ExportFlattened ||
                                    PanelConfiguration[Index].FooterDemand == EditorFooterDemand::LayerExport)
                                {
                                    TexturingApplied.ExportMode =
                                        PanelConfiguration[Index].FooterDemand == EditorFooterDemand::LayerExport ? 1u : 0u;
                                    TexturingApplied.StackPage = 2u;

                                    WorkspaceMaterialRecord ExportMaterial;
                                    ExportMaterial.Reference = TexturingApplied.ExportName;
                                    ExportMaterial.Material = EditorMaterialDocument;
                                    ExportMaterial.Layers = EditorMaterialLayers;
                                    MaterialExportOptions ExportOptions;
                                    ExportOptions.OutputName = TexturingApplied.ExportName;
                                    ExportOptions.OutputDirectory = TexturingApplied.ExportLocation;
                                    ExportOptions.Target = static_cast<MaterialExportTarget>(
                                        std::min(TexturingApplied.ExportPreset,
                                                 static_cast<std::uint32_t>(MaterialExportTarget::TargetCount) - 1u));
                                    ExportOptions.Format = TexturingApplied.ExportFormat == 1u
                                        ? MaterialExportImageFormat::Tga : MaterialExportImageFormat::Png;
                                    ExportOptions.BitDepth = static_cast<MaterialExportBitDepth>(
                                        std::min(TexturingApplied.ExportBitDepth,
                                                 static_cast<std::uint32_t>(MaterialExportBitDepth::DepthCount) - 1u));
                                    ExportOptions.NormalConvention = TexturingApplied.ExportDirectXNormals
                                        ? MaterialExportNormalConvention::DirectX : MaterialExportNormalConvention::OpenGl;
                                    ExportOptions.Resolution = 128u << std::min(TexturingApplied.ExportResolution, 7u);
                                    ExportOptions.Dilation = TexturingApplied.ExportDilation;
                                    const Deliver<MaterialExportPackage> ExportPackage =
                                        BuildMaterialExportPackage(ExportMaterial, ExportOptions);
                                    if (ExportPackage.Resolved)
                                        Discard(MaterialTextureExport().WritePackage(ExportMaterial, ExportPackage.Resolve()));

                                    PanelConfiguration[Index].FooterDemand = EditorFooterDemand::None;
                                }
                                Texturing.Record(LeafBody, TexturingApplied, StackRows.Rows, StackRows.Count);

                                if (LayerLeafTally < PanelStructure::RecordLimit)
                                {
                                    LayerLeafRects[LayerLeafTally] = LeafBody;
                                    ++LayerLeafTally;
                                }
                                break;
                            default:
                                break;
                        }
                    }

                    // 🔴 The popups after the leaf content, so they composite above it.
                    WorkspacePanels.RecordDeferredPopups(PanelPartitions[Index],
                                                         PanelConfiguration[Index]);

                    if (WorkspacePanels.PointerCaptured(Index))
                        Viewport.Seam().WithholdPointer();
                }

                Viewport.Seam().LeaveWorkspaceWindow();

                // 🔴 The surface is told as well as the seam. The window's list has just ended, so a
                //    restore made after this point must mean the background again rather than a panel
                //    that is no longer being recorded.
                Viewport.Surface().LeaveWindow();

                // ⚠️ Recorded, never acted on inside the sweep. Withdrawing here edits the set being walked.
                if (!Current)
                    Withdrawing = Index;
            }

            if (Withdrawing < OpenCount)
            {
                Discard(Workspaces.Withdraw(Withdrawing));
                WorkspacePanels.WithdrawPresentation(Withdrawing);
                for (std::uint32_t Moving = Withdrawing; Moving + 1u < OpenCount; ++Moving)
                {
                    PanelPartitions[Moving] = PanelPartitions[Moving + 1u];
                    PanelConfiguration[Moving]   = PanelConfiguration[Moving + 1u];
                }
                PanelPartitions[OpenCount - 1u].Reset();
                PanelConfiguration[OpenCount - 1u] = EditorPanelConfiguration{};
            }

            // 📝 The `+`, applied inside the dock node's own tab bar so the vendor lays it after the last
            //    tab — always at the end, by construction rather than by arithmetic.
            std::uint32_t AskingNode = 0u;

            if (Viewport.Seam().RecordWorkspaceAddition(Workspace.Strip(), OpenCount, AskingNode))
            {
                // 🔴 The asking node is carried to the NEXT tick, because the workspace it registers is not
                //    recorded until then. Applying it against the main space instead is what put a new
                //    workspace in the wrong window.
                RegisterIntoNode = AskingNode;
                const Deliver<std::uint32_t> RegisteredWorkspace = Workspaces.Register(DefaultSubject);
                if (RegisteredWorkspace.Resolved)
                    Discard(ApplyWorkspace(DeclaredWorkspaceFor(DefaultSubject), PanelPartitions[RegisteredWorkspace.Resolve()]));
            }

            // 🔴 With nothing open there is no tab bar to seat a `+` in, so the empty shell carries the
            //    invitation itself. `WorkspacePanel` draws "CREATE PANEL" on plain black; a press anywhere
            //    on that ground registers one, which is the way out of a state that otherwise has none.
            if (OpenCount == 0u && Viewport.Seam().VacantPressed(Whole))
            {
                const Deliver<std::uint32_t> RegisteredWorkspace = Workspaces.Register(DefaultSubject);
                if (RegisteredWorkspace.Resolved)
                    Discard(ApplyWorkspace(DeclaredWorkspaceFor(DefaultSubject), PanelPartitions[RegisteredWorkspace.Resolve()]));
            }

            // 📝 The drawers last, so they sit ABOVE the workspace as the sheet lays them.
            // ④·b The scene directory — the shared index is advanced here, once, before the panel
            //      samples it; the panel's own Advance only samples, and a second advance would retire
            //      the release before the leaves read it.
            SceneInteraction.Advance(Viewport.Surface().Pointer(), Pass.ElapsedMilliseconds);
            ParametricInteraction.Advance(Viewport.Surface().Pointer(), Pass.ElapsedMilliseconds);

            // 📐 Tab is shared by the scene directory's pages and the layer stack's carousel, so the
            //    key goes to whichever panel the pointer is over: a Texturing leaf feeds the layer
            //    stack, anything else feeds the scene directory. With no Texturing leaf open, the
            //    scene directory keeps Tab as before.
            const PointerCondition& Hovered = Viewport.Surface().Pointer();
            bool PointerInLayers = LayerLeafTally > 0u;

            if (PointerInLayers)
            {
                PointerInLayers = false;

                for (std::uint32_t Index = 0u; Index < LayerLeafTally; ++Index)
                {
                    if (LayerLeafRects[Index].Encloses(Hovered.PositionX, Hovered.PositionY))
                    {
                        PointerInLayers = true;
                        break;
                    }
                }
            }

            SceneDirectory.Advance(BackgroundPointer, Pass.ElapsedMilliseconds,
                                   SceneApplied,
                                   TabPressed && !PointerInLayers && !PointerBehindDrawer,
                                   Viewport.Seam().Modifiers());
            Texturing.Advance(BackgroundPointer, Pass.ElapsedMilliseconds,
                               TexturingApplied, StackRows.Rows, StackRows.Count,
                               TabPressed && PointerInLayers && !PointerBehindDrawer,
                               Viewport.Seam().Modifiers());
            SketchDirectory.Advance(BackgroundPointer, Pass.ElapsedMilliseconds,
                                    SketchDirectoryApplied,
                                    TabPressed && !PointerBehindDrawer,
                                    Viewport.Seam().Modifiers());

            // 📝 The search field: while it holds the contact, the seam's typed run feeds the
            //    directory's retention run, and Backspace / Escape edit it. Gated on the panel's own
            //    `SearchTaken` — the validation shell captured every keystroke unconditionally,
            //    which is the "search box not working" a gate fixes.
            if (SceneApplied.SearchTaken)
            {
                static_cast<void>(Viewport.Seam().AcceptTyped(SceneApplied.EntityRetention,
                                                              SceneDirectoryContext::RetentionLimit));

                if (Viewport.Seam().KeyPressed(KeySubject::Retract))
                {
                    std::uint32_t Occupied = 0u;

                    while (Occupied + 1u < SceneDirectoryContext::RetentionLimit &&
                           SceneApplied.EntityRetention[Occupied] != '\0')
                    {
                        ++Occupied;
                    }

                    if (Occupied > 0u)
                        SceneApplied.EntityRetention[Occupied - 1u] = '\0';
                }

                if (Viewport.Seam().KeyPressed(KeySubject::Withdraw))
                    SceneApplied.EntityRetention[0] = '\0';
            }

            // 📝 The layer stack's own search pill — the same gated feed.
            if (TexturingApplied.SearchTaken)
            {
                static_cast<void>(Viewport.Seam().AcceptTyped(TexturingApplied.Retention,
                                                              TexturingContext::TextureRetentionLimit));

                if (Viewport.Seam().KeyPressed(KeySubject::Retract))
                {
                    std::uint32_t Occupied = 0u;

                    while (Occupied + 1u < TexturingContext::TextureRetentionLimit &&
                           TexturingApplied.Retention[Occupied] != '\0')
                    {
                        ++Occupied;
                    }

                    if (Occupied > 0u)
                        TexturingApplied.Retention[Occupied - 1u] = '\0';
                }

                if (Viewport.Seam().KeyPressed(KeySubject::Withdraw))
                    TexturingApplied.Retention[0] = '\0';
            }

            // The atmosphere is updated on the GPU in this frame's command stream. The scene component
            // classifies medium, sky-view and presentation changes; the current compatibility surface
            // composes those results directly without any CPU pixel generation or transfer submission.
            if (SkyRegistered && SceneApplied.EnvironmentPresented)
            {
                // A non-zero day-cycle rate drives the same authored azimuth the editor and game consume.
                // It therefore updates the visible disc and the directional light/shadow direction together.
                if (SceneApplied.Environment.DayCycleDegreesPerSecond != 0.0)
                {
                    SceneApplied.Environment.SunAzimuth = std::fmod(
                        SceneApplied.Environment.SunAzimuth +
                        SceneApplied.Environment.DayCycleDegreesPerSecond *
                        (Pass.ElapsedMilliseconds / 1000.0), 360.0);
                    if (SceneApplied.Environment.SunAzimuth < 0.0)
                        SceneApplied.Environment.SunAzimuth += 360.0;
                }

                AtmosphereState Authored;
                Authored.SunElevation = SceneApplied.Environment.SunElevation;
                Authored.SunAzimuth = SceneApplied.Environment.SunAzimuth;
                Authored.SunIlluminance = SceneApplied.Environment.SunIntensity;
                Authored.SunTemperature = SceneApplied.Environment.SunTemperature;
                Authored.SunAngularRadius = 0.266 * SceneApplied.Environment.SunDiscRadius;
                Authored.SunDiscIntensity = SceneApplied.Environment.SunDiscIntensity;
                Authored.SkyIntensity = SceneApplied.Environment.SkyIntensity;
                Authored.ExposureCompensation = SceneApplied.Environment.ExposureCompensation;
                Authored.GroundAlbedo = SceneApplied.Environment.GroundAlbedo;
                Authored.RayleighDensity = SceneApplied.Environment.AtmosphereDensity;
                Authored.RayleighScaleHeightKilometres = 8.0 * SceneApplied.Environment.AtmosphereScaleHeight;
                Authored.MieDensity = SceneApplied.Environment.MieDensity;
                Authored.MieScaleHeightKilometres = SceneApplied.Environment.MieScaleHeightKilometres;
                Authored.MieAsymmetry = SceneApplied.Environment.MieAsymmetry;
                Authored.OzoneDensity = SceneApplied.Environment.OzoneDensity;
                Authored.CameraAltitudeKilometres = std::max(EditorCamera.LaggedPosition[1], 0.0) * 0.001;

                const AtmosphereDirty Dirty = DynamicAtmosphere.Apply(Authored);

                SunLight.SetSolarPosition(Authored.SunAzimuth, Authored.SunElevation);
                SunLight.Illuminance = Authored.SunIlluminance;
                SunLight.TemperatureKelvin = Authored.SunTemperature;
                SunLight.AngularRadiusDegrees = Authored.SunAngularRadius;
                SunLight.ShadowStrength = SceneApplied.Environment.SunShadowStrength;
                SunLight.CastShadows = SceneApplied.Environment.SunShadowStrength > 0.0;

                DynamicSkyParameters GPU;
                GPU.SunElevationDegrees = static_cast<float>(Authored.SunElevation);
                GPU.SunAzimuthDegrees = static_cast<float>(Authored.SunAzimuth);
                GPU.SunIlluminance = static_cast<float>(Authored.SunIlluminance);
                GPU.SunTemperatureKelvin = static_cast<float>(Authored.SunTemperature);
                GPU.SunAngularRadiusDegrees = static_cast<float>(Authored.SunAngularRadius);
                GPU.SunDiscIntensity = static_cast<float>(Authored.SunDiscIntensity);
                GPU.SkyIntensity = static_cast<float>(Authored.SkyIntensity);
                GPU.ExposureCompensation = static_cast<float>(Authored.ExposureCompensation);
                GPU.GroundAlbedo = static_cast<float>(Authored.GroundAlbedo);
                GPU.RayleighDensity = static_cast<float>(Authored.RayleighDensity);
                GPU.RayleighScaleHeightKilometres = static_cast<float>(Authored.RayleighScaleHeightKilometres);
                GPU.MieDensity = static_cast<float>(Authored.MieDensity);
                GPU.MieScaleHeightKilometres = static_cast<float>(Authored.MieScaleHeightKilometres);
                GPU.MieAsymmetry = static_cast<float>(Authored.MieAsymmetry);
                GPU.OzoneDensity = static_cast<float>(Authored.OzoneDensity);
                GPU.CameraAltitudeKilometres = static_cast<float>(Authored.CameraAltitudeKilometres);
                GPU.Quality = std::min(SceneApplied.Environment.AtmosphereQuality, 3u);

                const bool Refresh = !SkyEverGenerated || Dirty != AtmosphereDirty::None ||
                                     SkyQuality != GPU.Quality;
                if (AtmosphereSurface.Record(Pass.Recording, GPU, Refresh).Resolved)
                {
                    SkyEverGenerated = true;
                    SkyQuality = GPU.Quality;
                }
                SceneApplied.SkyTextureIdentity = SkyTextureIdentity;
            }
            else
                SceneApplied.SkyTextureIdentity = 0u;

            // 📝 The layer stack's structural request is drained exactly once per tick, through the
            //    same shared helper the harness drives — the row set and the working copies stay in
            //    step with the panel's buttons and menus.
            StackRows.ApplyRequest(TexturingApplied);
            MaterialLayerProjectionReport MaterialBridge = ProjectMaterialLayersFromTextureStack(
                EditorMaterialDocument, EditorMaterialLayers, StackRows, TexturingApplied,
                EditorMaterialExchange, EditorMaterialSnapshotReady ? &EditorMaterialSnapshot : nullptr);
            EditorMaterialSnapshot = MaterialBridge.Snapshot;
            EditorMaterialSnapshotReady = true;

            // 📝 Bookmark recall is a request to the owning EditorCameraComponent, not a temporary write
            //    into the panel's mirrored pose (which the next camera tick would overwrite).
            if (SceneApplied.CameraBookmarkRecallRequested)
            {
                const std::uint32_t Bookmark = SceneApplied.CameraBookmarkTaken;
                if (Bookmark < SceneApplied.CameraBookmarkCount)
                {
                    for (std::uint32_t Axis = 0u; Axis < 3u; ++Axis)
                        EditorCamera.Position[Axis] = SceneApplied.CameraBookmarkPosition[Bookmark][Axis];
                    EditorCamera.YawDegrees = SceneApplied.CameraBookmarkRotation[Bookmark][0];
                    EditorCamera.PitchDegrees = SceneApplied.CameraBookmarkRotation[Bookmark][1];
                    EditorCamera.Snap();
                }
                SceneApplied.CameraBookmarkRecallRequested = false;
            }

            Viewport.RecordDrawers();
            Viewport.DrawerPanels();

            // ⑤ The south drawer's browser, recorded before the north drawer's Control Centre so the
            //     Control Centre's own exclusions are the last thing declared and the two cannot disagree.
            //     🔴 The interior is asked for every tick and not cached — the drawer is springing, so the
            //     extent it offers is a different one on almost every tick of an open or a close.
            const PlaneExtent BrowserInterior = Viewport.Drawers().Interior(DrawerBearing::South);

            BrowserInteraction.Advance(Viewport.Surface().Pointer(), Pass.ElapsedMilliseconds);
            ContentBrowser.Advance(Viewport.Surface().Pointer(), Pass.ElapsedMilliseconds);

            if (BrowserInterior.Width() > 0.0f && BrowserInterior.Height() > 0.0f)
            {
                ThemeToken DrawerGround = Viewport.Appearance().Colour.SurfaceCurrent;
                DrawerGround.Opacity = 255u;
                Viewport.Surface().Ground(BrowserInterior, DrawerGround, 0.0f, CornerNone);
                ContentBrowser.RecordBrowser(BrowserInterior, ContentApplied, ContentBrowserApplied);
                ContentBrowser.RecordDeferred();

                const CodexActivation ActivatedScene = ConsumeCodexActivation(
                    ContentBrowserApplied, ContentApplied, EngineContentRoot);
                if (ActivatedScene.Requested && !ActivatedScene.Resolved)
                {
                    std::printf("%s — workspace activation refused (reason %u: %s)\n", HostName,
                                static_cast<unsigned>(ActivatedScene.Error.DeclaredReason), ActivatedScene.Error.Detail);
                }
                else if (ActivatedScene.Resolved)
                {
                    OpenedScene = ActivatedScene.Scene.Workspace;
                    OpenedSceneStanding = true;
                    BuildSceneDirectoryRows(OpenedScene, WorkspaceSceneRows);
                    PresentedEntities = WorkspaceSceneRows.Rows;
                    PresentedEntityCount = WorkspaceSceneRows.RowCount;
                    ApplySceneEnvironment(OpenedScene, SceneApplied);
                    const ViewportCameraSeed CameraSeed = ViewportCameraSeed{};
                    EditorCamera.Position[0] = CameraSeed.Position[0];
                    EditorCamera.Position[1] = CameraSeed.Position[1];
                    EditorCamera.Position[2] = CameraSeed.Position[2];
                    EditorCamera.YawDegrees = CameraSeed.YawDegrees;
                    EditorCamera.PitchDegrees = CameraSeed.PitchDegrees;
                    EditorCamera.FieldOfViewDegrees = CameraSeed.FieldOfViewDegrees;
                    EditorCamera.Snap();

                    // The workspace names one shared pigment for every tea-service geometry entry.
                    // Present it once in the host-owned layer model rather than fabricating one layer per mesh.
                    StackRows.Seed(&WhiteDielectricLayer, 1u);
                    SeedTexturingContextFromRows(TexturingApplied, StackRows.Rows, StackRows.Count);
                    TexturingApplied.LayerTaken = 0u;
                }
                // 🔴 The confirmed import. This was 82 lines inside `ParametricSketchHost`; deleting
                //    that host would have taken mesh and material-image import with it, silently,
                //    because a host with zero definitions still held the only call.
                // 🔴 Edits made in the Scene Directory flow back onto the codex entries.
                SynchroniseCodexTransformsFromSceneDirectory(OpenedScene, WorkspaceSceneRows,
                                                             SceneApplied, OpenedSceneStanding);

                CommitConfirmedImport(HostName, ContentBrowserApplied, WorkspaceSceneRows,
                                      SceneApplied, OpenedScene, OpenedSceneStanding);

                if (ContentBrowserApplied.ImportBrowseRequested)
                {
                    std::filesystem::path Destination(ContentBrowserApplied.ImportLocation);
                    if (ContentBrowserApplied.ImportTaken < ContentBrowserApplied.ImportEntryCount &&
                        ContentBrowserApplied.ImportEntries[ContentBrowserApplied.ImportTaken].Directory)
                    {
                        Destination /= ContentBrowserApplied.ImportEntries[ContentBrowserApplied.ImportTaken].Naming;
                    }
                    PopulateImportDirectory(ContentBrowserApplied, Destination);
                    ContentBrowserApplied.ImportBrowseRequested = false;
                }

                // 🔴 Declared every tick or lost. Without it the drawer owns every contact inside its own
                //    body, so taking a record or dragging the lattice slides the drawer instead.
                ContentBrowser.Exclude(Viewport.Drawers(), DrawerBearing::South);
            }

            const PlaneExtent ControlInterior = Viewport.Drawers().Interior(DrawerBearing::North);
            ControlCentre.Advance(Viewport.Surface().Pointer(), Pass.ElapsedMilliseconds);
            // 📝 The artist's per-role weights are declared every tick so the workspace's panels read the
            //    current choice; the viewport re-states them after each resolve.
            Viewport.ApplyTypographyRoles(ControlCentreValues.TypographySize,
                                          ControlCentreValues.TypographyWeight);
            if (ControlInterior.Width() > 0.0f && ControlInterior.Height() > 0.0f)
            {
                ThemeToken DrawerGround = Viewport.Appearance().Colour.SurfaceCurrent;
                DrawerGround.Opacity = 255u;
                Viewport.Surface().Ground(ControlInterior, DrawerGround, 0.0f, CornerNone);
            }
            Discard(ControlCentre.Record(ControlInterior, ControlCentreValues));

            // UI Scaling was previously only a displayed Control Centre value. It now re-resolves the shared
            // appearance, while display DPI remains an independent multiplier. Panels that cache derived
            // metrics are explicitly reseated; borrowed-theme panels observe the same profile immediately.
            Discard(Viewport.Seam().ApplyInterfaceAntialiasing(
                ControlCentreValues.GeometryAntialiasing));

            // 📝 Compared rather than watched. The Control Centre writes the artist's choice straight into the
            //    ordinates, so the change is visible here as a difference and needs no callback to report it.
            // 📝 What the Control Centre holds this tick, handed to the session. The comparison against
            //    what was last inscribed, the write beside the executable and the font pipeline are all
            //    the session's — this host reapplies only the panels that keep their own copy of the inks.
            {
                ThemeSelection Chosen;
                Chosen.Current     = ControlCentreValues.Theme;
                Chosen.Primary     = ControlCentreValues.Primary;
                Chosen.Secondary   = ControlCentreValues.Secondary;
                Chosen.Information = ControlCentreValues.Information;
                Chosen.Warning     = ControlCentreValues.Warning;
                Chosen.Alert       = ControlCentreValues.Alert;

                if (ControlCentreValues.Font < Fonts.FamilyCount() && Fonts.FamilyName(ControlCentreValues.Font) != nullptr)
                    std::snprintf(Chosen.FontFamily, sizeof(Chosen.FontFamily), "%s", Fonts.FamilyName(ControlCentreValues.Font));

                if (Session.RestateAppearance(Chosen, ControlCentreValues.Scaling))
                {
                    ContentBrowser.Reapply(Viewport.Appearance());
                    SceneDirectory.Reapply(Viewport.Appearance());
                    SketchDirectory.Reapply(Viewport.Appearance());
                    ParametricTools.Reapply(Viewport.Appearance());
                    Texturing.Reapply(Viewport.Appearance());
                }
            }
            ControlCentre.Exclude(Viewport.Drawers());

            // 🧩 Admission is deliberately drained while the host command recording is open and before the
            // interface begins the display scope. The imported packet has already travelled through source drain,
            // faithful decode, document intake, Earcut rendering-packet construction, and partition registration.
            if (GeometryAdmissionPending && GeometryDevice.Standing())
            {
                const Deliver<const PartitionStructure*> Partitioned =
                    ImportedVisibility.Registered(PendingVisibilityRegistration);
                const Deliver<const GeometryRenderingSnapshot*> Rendering =
                    ImportedRendering.Resolve(PendingRendering);
                if (Partitioned.Resolved && Rendering.Resolved)
                {
                    const Deliver<std::uint32_t> Admitted = GeometryDevice.Admit(
                        *Partitioned.Resolve(), *Rendering.Resolve(), PendingRegistrationBase, Pass.Recording);
                    if (Admitted.Resolved)
                        GeometryAdmissionPending = false;
                    else
                        std::printf("%s — geometry admission refused (reason %u: %s)\n", HostName,
                                    static_cast<unsigned>(Admitted.Error.DeclaredReason), Admitted.Error.Detail);
                }
            }

            // 🔴 Scene compute and classic render constructs record BEFORE this call. The session seals the
            //    interface and opens the display scope; the overlay below records inside that scope.
            if (Session.Seal(Pass))
            {
                // 📝 The overlay pass records INSIDE the same dynamic-rendering scope, after the
                //    interface: the grid and axes draw directly on top of the sky and viewport,
                //    in their own straight-alpha GPU pass — no ImGui tessellation, vivid colours.
                //    Each viewport leaf's geometry is uploaded at most once per generation change
                //    and drawn with a scissor clipped to that leaf's box, so the overlay never
                //    textures over the outliner, the properties or any other panel.
                // 🔴 A DRAWER HIDES THE STRIP IT COVERS, NOT THE WHOLE VIEWPORT. Both GPU passes record
                //    after the interface, so either of them can texture over the Control Centre or the
                //    Content Browser unless they are clipped to the uncovered band. The drawers are
                //    full-width horizontal bands: North descends from the top edge and South rises from
                //    the bottom, so what remains visible is one vertical band.
                const float UncoveredTop    = NorthInterior.MaximumY > 0.0f
                                            ? std::max(0.0f, NorthInterior.MaximumY) : 0.0f;
                const float UncoveredBottom = SouthInterior.MinimumY < static_cast<float>(Pass.Height)
                                            ? std::min(static_cast<float>(Pass.Height), SouthInterior.MinimumY)
                                            : static_cast<float>(Pass.Height);

                // 🔴 THE SKETCH, RASTERISED ON THE GPU. The packet is uploaded once per generation
                //    change -- not per frame and not per leaf -- and each viewport leaf records it with
                //    its own projection. The drawer band above clips THIS pass as well, or the sketch
                //    draws over the drawers even after the overlay learned not to.
                if constexpr (HostHasFeature(FeatureParametric))
                {
                    if (CadPass.Standing())
                    {
                        const std::uint64_t CadFingerprint = FingerprintCadPacket(SketchCadPacket);
                        if (CadUploadState.NeedsUpload(CadFingerprint))
                        {
                            CadPass.Upload(SketchCadPacket);
                            CadUploadState.MarkUploaded(CadFingerprint);
                        }

                        for (std::uint32_t ViewportIndex = 0u;
                             UncoveredBottom > UncoveredTop && ViewportIndex < ViewportLeafTally;
                             ++ViewportIndex)
                        {
                            const PlaneExtent& LeafRect = ViewportLeafRects[ViewportIndex];
                            const float CadClipY0 = std::max(LeafRect.MinimumY, UncoveredTop);
                            const float CadClipY1 = std::min(LeafRect.MaximumY, UncoveredBottom);

                            if (CadClipY1 <= CadClipY0)
                                continue;

                            // ⚠️ The CAD pass scissor is PHYSICAL. The leaf and the uncovered drawer
                            //    band are logical, so the clipped band is converted as one extent.
                            const PlaneExtent CadClip =
                                ViewportDrawable.ToPhysical(Spanning(LeafRect.MinimumX, CadClipY0,
                                                                     LeafRect.Width(), CadClipY1 - CadClipY0));

                            // 🔴 The open menu is withheld here too, and converted to PHYSICAL first.
                            const PlaneExtent CadWithheld =
                                WorkspacePanels.AnyPopupStanding()
                                    ? ViewportDrawable.ToPhysical(WorkspacePanels.PopupExtent())
                                    : PlaneExtent{};

                            CadPass.RecordAround(Pass.Recording,
                                                 ViewportRuntime[ViewportLeafIndexs[ViewportIndex]].CadProjection,
                                                 CadClip.MinimumX, CadClip.MinimumY,
                                                 CadClip.MaximumX, CadClip.MaximumY,
                                                 CadWithheld.MinimumX, CadWithheld.MinimumY,
                                                 CadWithheld.MaximumX, CadWithheld.MaximumY);
                        }
                    }
                }

                // ⚠️ Drawers meeting in the middle leave nothing; the loop must not record an inverted
                //    box, which is a validation error rather than an empty draw.
                for (std::uint32_t ViewportIndex = 0u;
                     UncoveredBottom > UncoveredTop && ViewportIndex < ViewportLeafTally;
                     ++ViewportIndex)
                {
                    const std::uint32_t LeafIndex = ViewportLeafIndexs[ViewportIndex];
                    ViewportRuntimeState& LeafRuntime = ViewportRuntime[LeafIndex];
                    OverlayGeometry& LeafOverlay = LeafRuntime.Overlay;

                    const float DrawablePixelScale = static_cast<float>(ViewportDrawable.Factor);
                    if (LeafRuntime.NeedsOverlayUpload(DrawablePixelScale))
                    {
                        Overlay.Upload(LeafOverlay, DrawablePixelScale);
                        LeafRuntime.MarkOverlayUploaded(DrawablePixelScale);
                    }

                    const PlaneExtent& LogicalLeafRect = ViewportLeafRects[ViewportIndex];

                    // 🔴 TWO RECTANGLES, DELIBERATELY. The leaf's WHOLE box is the camera's canvas and
                    //    is passed unchanged however much a drawer covers; the scissor is the part of
                    //    it no drawer covers. Passing the clipped box for both was what made the grid
                    //    squash into the remaining space instead of simply being hidden there.
                    const float LogicalScissorY0 = std::max(LogicalLeafRect.MinimumY, UncoveredTop);
                    const float LogicalScissorY1 = std::min(LogicalLeafRect.MaximumY, UncoveredBottom);

                    if (LogicalScissorY1 <= LogicalScissorY0)
                        continue;

                    // 🔴 AND AN OPEN MENU HIDES THE BOX IT COVERS, for exactly the reason the drawers
                    //    above do. This pass records AFTER the interface, so the grid and the axes drew
                    //    straight through any dropdown opened from the viewport footer -- which reads as
                    //    "the dropdowns are transparent", because what shows through them is the viewport
                    //    behind. Every plate was already opaque; nothing was ever drawn see-through.
                    //    The menu is subtracted from the scissor rather than the overlay being dropped,
                    //    because blanking a whole viewport's grid to protect one small menu is the same
                    //    trade that made the drawers erase the sketch.
                    const PlaneExtent Withheld = WorkspacePanels.AnyPopupStanding()
                                                 ? WorkspacePanels.PopupExtent() : PlaneExtent{};

                    const PlaneExtent LeafRect = ViewportDrawable.ToPhysical(LogicalLeafRect);
                    const PlaneExtent PhysicalScissor = ViewportDrawable.ToPhysical(
                        Spanning(LogicalLeafRect.MinimumX, LogicalScissorY0, LogicalLeafRect.Width(),
                                 LogicalScissorY1 - LogicalScissorY0));
                    const PlaneExtent PhysicalWithheld = WorkspacePanels.AnyPopupStanding()
                        ? ViewportDrawable.ToPhysical(Withheld) : PlaneExtent{};
                    const float ScissorY0 = PhysicalScissor.MinimumY;
                    const float ScissorY1 = PhysicalScissor.MaximumY;

                    Overlay.RecordAround(Pass.Recording, Pass.Width, Pass.Height,
                                         LeafRect.MinimumX, LeafRect.MinimumY,
                                         LeafRect.MaximumX, LeafRect.MaximumY,
                                         PhysicalScissor.MinimumX, ScissorY0,
                                         PhysicalScissor.MaximumX, ScissorY1,
                                         PhysicalWithheld.MinimumX, PhysicalWithheld.MinimumY,
                                         PhysicalWithheld.MaximumX, PhysicalWithheld.MaximumY);
                }

                // 🔴 THE INTERFACE IS RECORDED ON TOP OF THE VIEWPORTS. This ensures that floating
                //    panels (like Selection Tool Options), context menus, docked windows, and the
                //    Control Centre / Content Browser strips/drawers are drawn with full opacity over
                //    all 3D viewport grid lines, axes, and sketch curves.
                Session.RecordInterface(Pass);
            }
        }

        // ⑤ Close the scope, submit, present, advance. A rejected present re-establishes the chain rather
        //    than ending the loop.
        if (!Session.Complete())
            break;
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                      RECLAMATION
    // ─────────────────────────────────────────────────────────────────────────────────────────────────────

    // 📝 This host's own panels first.
    CadPass.Reclaim();
    ControlCentre.Reset();
    WorkspacePanels.Reset();
    for (std::uint32_t Index = 0u; Index < WorkspaceIndex::WorkspaceLimit; ++Index)
        PanelPartitions[Index].Reset();
    Workspace.Reset();
    Workspaces.Reset();

    // 🔴 Then this host's device estate, BEFORE the session retires the device: the atmosphere surface's
    //    fence wait needs the device alive, and a surface left standing past the session's `Reclaim`
    //    waited on a dead device in its destructor — the "vkWaitForFences: Invalid device" at shutdown.
    GeometryDevice.Reclaim();
    AtmosphereSurface.Reclaim();
    SkyRegistered = false;
    SkyTextureIdentity = 0u;
    Overlay.Reclaim();
    OverlayCodec.Reclaim();

    // 🔴 Returned rather than only stated. A validation run needs an exit code, so that a serious arrival
    //    fails whatever invoked the host instead of scrolling past in a console nobody reads.
    return (Session.Reclaim() == 0u) ? 0 : 1;
}
