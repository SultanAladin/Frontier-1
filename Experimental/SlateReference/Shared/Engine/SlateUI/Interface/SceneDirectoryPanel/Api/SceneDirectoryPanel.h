//============================================================================================================================================
//                                                       SCENEDIRECTORYPANEL.H
//============================================================================================================================================
// 🧩 The editor's scene directory — content drawn INSIDE workspace leaves, never
//    over the whole display. It records the sky in viewport leaves, the directory
//    in outliner leaves, and details/bookmarks in properties leaves.
//
//    🔴 FUTURE AGENTS, READ THIS. The retired validation-shell prototype is not a
//       second implementation. The runtime layout is workspace windows →
//       splittable panels (EditorPanel + PanelStructure) → this leaf content.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ControlPanel/Api/ControlPanel.h"
#include "Shared/OverlayGeometry.slang.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"
#include "SlateUI/Interface/FacetPanel/Api/FacetPanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/OverflowScroll/Api/OverflowScroll.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectorySpecification.h"
#include "SlateUI/Interface/SlidingPages/Api/SlidingPages.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <cstdint>

namespace Slate
{

/// 🧩 One explicit scene-transfer request emitted by the dedicated import/save page.
enum class SceneTransferDemand : std::uint32_t
{
    None   = 0u,
    Import = 1u,
    Save   = 2u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT THE HOST OWNS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every datum the scene-directory panel presents, owned by the host and written through by the panel.
/// note  🔴 `14` §1: the panel presents what it is handed and retains none of it. Every condition the artist
///        can alter lives here, so the host — and only the host — is the home of the scene directory's
///        content. This is the editor twin of the shell's `ShellContext`, holding only what the editor's
///        leaves present.
/// tag   guarantee
struct SceneDirectoryContext
{
    static constexpr std::uint32_t EntityLimit = 16u;   // [-] - outline rows, the reference declares fourteen
    static constexpr std::uint32_t CardLimit   =  4u;   // [-] - property cards, the reference states four

    // 📐 The editor's environment. `EnvironmentPresented` gates every environment branch, so a host that
    //    never sets it (the validation host) renders no environment anywhere.
    bool                       EnvironmentPresented = false;   // [-] - the sun/sky/atmosphere is presented
    EnvironmentConfiguration   Environment          = {};      // [-] - host-owned; the sliders write it
    std::uint32_t              EntityTaken = 2u;              // [-] - primary outline row (the Sun)
    bool                       EntitySelected[EntityLimit] = { false, false, true }; // [-] - persistent membership
    std::uint32_t              EntitySelectionAnchor = 2u;     // [-] - visible-range origin for Shift selection

    // 📝 Left-contact parenting. Scene rows do not reorder freely: dropping onto an entity parents to it,
    //    and dropping onto a grouping row adds the carried subtree to that folder.
    std::uint32_t              DragSource      = EntityLimit;
    std::uint32_t              DragDestination = EntityLimit;
    float                      DragOriginY      = 0.0f;

    // 📝 The device-local GPU atmosphere surface the viewport leaf draws. Opaque on purpose: the panel
    //    names no vendor, so the identity is an integer the recording surface resolves.
    std::uintptr_t             SkyTextureIdentity      = 0u;   // [-] - zero draws no sky at all
    std::uintptr_t             GeometryTextureIdentity = 0u;   // [-] - transparent resolved surface overlay

    // 📝 The sky's own camera, declared by the host each tick it regenerates. The dome is
    //    direction-indexed, and the viewport leaf crops it to this camera's field of view.
    SkyViewCamera              ViewportSkyCamera  = {};       // [-] - the dome crop the viewport draws

    // 📐 The Properties / Bookmarks strip, and the folds the reference applies empty so every card and
    //    every revision group arrives disclosed.
    std::uint32_t              InspectorTab    = 0u;          // [-] - 0 Properties, 1 Bookmarks (Editor Camera only)
    bool                       CardFolded[CardLimit]       = {};

    // 📐 The outliner leaf has three OUTER slides: Directory + Details, Inspector, and scene transfer.
    //    Editor Camera bookmarks are an inner inspector page, never a duplicate outer destination.
    std::uint32_t              OutlinePage         = 0u;   // [-] - 0 Directory, 1 Inspector, 2 Import/Export
    std::uint32_t              OutlineInspectorTab = 0u;   // [-] - 0 Properties, 1 Bookmarks (Editor Camera only)
    std::uint32_t              TransferMode        = 0u;   // [-] - 0 Import, 1 Export
    std::uint32_t              TransferFormat      = 0u;   // [-] - format carousel selection
    char                       TransferName[64] = "Untitled Scene";
    char                       TransferTags[96] = "scene, dcc";
    char                       TransferLocation[96] = "Project/Scenes";
    double                     TransferScale = 1.0;
    char                       TransferScaleRun[32] = "1.0"; // [-] - expression-capable authored scale
    std::uint32_t              TransferForwardAxis = 0u;   // [-] - 0 -Z, 1 +Z, 2 +X, 3 -X
    std::uint32_t              TransferUpAxis = 0u;        // [-] - 0 +Y, 1 +Z
    std::uint32_t              TransferNormalMode = 0u;    // [-] - 0 import/export, 1 calculate, 2 face
    bool                       TransferApplyTransform = true;
    bool                       TransferApplyUnits = true;
    bool                       TransferPreservePivots = true;
    std::uint32_t              TransferTransformMode = 0u;       // [-] - bake, preserve, or geometry only
    bool                       TransferMaterials = true;
    std::uint32_t              TransferMaterialMode = 0u;        // [-] - create, reuse, or link
    std::uint32_t              TransferTexturePathMode = 0u;     // [-] - relative, copy, or embed
    bool                       TransferAnimation = true;
    std::uint32_t              TransferAnimationMode = 0u;      // [-] - all, active action, or current take
    bool                       TransferResampleAnimation = true;
    bool                       TransferVertexColours = true;
    std::uint32_t              TransferVertexColourMode = 0u;    // [-] - replace, multiply, or ignore
    std::uint32_t              TransferVertexColourSpace = 0u;   // [-] - source, sRGB, or linear
    bool                       TransferTriangulate = false;
    bool                       TransferCustomProperties = false;
    std::uint32_t              TransferCustomPropertyMode = 0u;  // [-] - all, supported, or none
    bool                       TransferPreserveNamespaces = true;
    bool                       TransferArmatures = true;
    bool                       TransferLeafBones = false;
    bool                       TransferDeformBonesOnly = false;
    std::uint32_t              TransferPrimaryBoneAxis = 0u;     // [-] - Y, X, or Z
    std::uint32_t              TransferSecondaryBoneAxis = 0u;   // [-] - X, Z, or Y
    bool                       TransferCardExpanded[6] = {};      // [-] - animated option cards
    SceneTransferDemand        TransferDemand = SceneTransferDemand::None; // [-] - drained by the owning host

    // 📝 The scene directory's own search and filter, placed between the outliner's header and its
    //    rows. `EntityRetention` is the search run the host feeds through the seam's `AcceptTyped`
    //    while `SearchTaken` stands; the facets are the editor's generic categories (objects, lights,
    //    cameras, folders, audio, particles, triggers, environment, layers) and a row matches when
    //    its NAME or its TAGS contain the search run AND its category's facet is enabled. All facets
    //    off = no filtering; an empty run = no search.
    static constexpr std::uint32_t RetentionLimit = 48u;   // [-] - the search run, terminator included
    static constexpr std::uint32_t FacetCount       =  9u;   // [-] - the editor's filter categories

    char                       EntityRetention[RetentionLimit] = {};   // [-] - the search run
    bool                       SearchTaken   = false;   // [-] - the search field holds the contact
    bool                       FacetEnabled[FacetCount] = {};   // [-] - active filter categories

    // 📝 The disclosure and presence conditions of the outline rows, exactly as the shell's own declare
    //    them: the level, Lighting, Environment and Systems arrive expanded and everything else folded.
    bool  EntityExpanded[EntityLimit] = { true, true, false, false, false, false,
                                            true, false, false, false, true, false, false, false };
    bool  EntityPresent[EntityLimit]  = { true, true, true, true, true, true,
                                            true, true, true, true, true, true, true, true };

    // 📐 The detail pane's small option switches for the taken row: bit 0 Locked, bit 1 Cast Shadows.
    //    `Visible` is `EntityPresent`, which the eye already owns. For the CAMERA row the bits read
    //    differently: bit 1 is the camera lag, bit 2 the inverted pitch — the settings the camera's
    //    details pane presents.
    std::uint32_t              DetailBits[EntityLimit] = {};

    // 📝 Each record's transform, edited by the Transform card's three axis rows.
    // 🔴 The card previously drew Position, Rotation and Scale as bare labels with
    //    no reading at all, because there was nowhere to keep one. Seeded so a
    //    fresh scene reads sensibly rather than all-zero scale.
    double                     EntityPosition[EntityLimit][3] = {};
    double                     EntityRotation[EntityLimit][3] = {};
    double                     EntityScale[EntityLimit][3] = {};
    bool                       TransformSeeded = false;   // [-] - scale defaults applied once

    // 📝 The camera's own ordinates, owned by the host and written every tick: the fly speed the
    //    properties leaf's Fly Speed card edits (with a drag-end history demand, like the
    //    environment), and the pose the details pane states.
    double                     CameraSpeed = 50.0;      // [m/s] - the fly camera's rate
    double                     CameraFieldOfView = 60.0; // [deg] - editor perspective vertical field
    double                     CameraNearClip = 0.1;     // [m] - nearest presented geometry
    double                     CameraFarClip = 10000.0;  // [m] - furthest presented geometry
    double                     CameraPosition[3] = { 0.0, 1.5, 0.0 };   // [m] - host-written
    double                     CameraRotation[3] = { 100.0, 15.0, 0.0 }; // [deg] - yaw, pitch, roll

    // 📝 Editor-camera bookmarks are authored here rather than in a viewport popup. Each captures a
    //    pose, carries an editable name, and can request that the host restore its camera component.
    static constexpr std::uint32_t CameraBookmarkLimit = 8u;
    char                       CameraBookmarkNames[CameraBookmarkLimit][32] = {};
    double                     CameraBookmarkPosition[CameraBookmarkLimit][3] = {};
    double                     CameraBookmarkRotation[CameraBookmarkLimit][3] = {};
    std::uint32_t              CameraBookmarkCount = 0u;
    std::uint32_t              CameraBookmarkTaken = 0u;
    bool                       CameraBookmarkRecallRequested = false;

    // 📝 The viewport's overlay record — the grid and the gizmo, filled by the panel and drawn by
    //    the GPU overlay pass. The host uploads it when its generation changes; the pass draws it
    //    in its own straight-alpha pass so the CPU never tessellates and the colours stay vivid.
    OverlayGeometry            Overlay = {};             // [-] - the GPU pass's input
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE PANEL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Records the scene directory's leaf content — the viewport sky, the outliner | details column and the
///    properties | history pages — inside the extents the editor's panel chrome hands over.
/// note  🔴 The panel draws ONLY leaf content. It never draws a rail, a top bar or a fullscreen shell; the
///        workspace and the panel chrome belong to `WorkspacePanel` and `EditorPanel`, and this panel fills
///        the leaves they leave.
/// tag   owning
class SceneDirectoryPanel
{
public:

    /// 🧩 Exactly how many control identities `Construct` claims, stated where they are claimed.
    /// note  🔴 The arithmetic lives beside the registrations it describes so the two can only disagree by
    ///        an edit that touches both.
    static constexpr std::uint32_t RegistrationDemand =
          SceneDirectoryContext::EntityLimit * 6u   // [-] - row, presence, fold, and three detail options
        + 52u                                         // [-] - fixed navigation, fields, folds, and per-card sliders
        + SceneDirectoryContext::CameraBookmarkLimit
        + FacetPanel::FacetCapacity + 2u              // [-] - filter chips, dropdown, and clear
        + 45u;                                         // [-] - transfer carousel, fields, and animated option cards

    SceneDirectoryPanel()                                   = default;
    SceneDirectoryPanel(const SceneDirectoryPanel&)            = delete;
    SceneDirectoryPanel& operator=(const SceneDirectoryPanel&) = delete;
    ~SceneDirectoryPanel()                                  = default;

    /// 🧩 Borrows the recording facilities and registers every identity and interpolant the panel needs.
    /// out   Result  [-]  refuses with ContentUnsupported when a construction already stands, and with
    ///                     ExtentExhausted when the index or the integrator declines an registration
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<bool> ConstructSceneDirectoryPanel(ControlIndex&              IncomingInteraction,
                            MotionIntegrator&              Integrator,
                            RecordingSurface&              Surface,
                            const ThemeProfile& Resolved);

    /// 🧩 Samples the contact for this tick, after the tick owner has advanced the shared index once.
    /// note  🔴 This does not advance the index; several panels share it and the tick owner advances it once.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Advance(const PointerCondition& Sampled, double Elapsed,
                 SceneDirectoryContext& Applied, bool TabPressed = false,
                 const ModifierCondition& Modifiers = {});

    /// 🧩 Re-applies every scaled extent after the appearance was resolved against a new display extent.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reapply(const ThemeProfile& Resolved);

    /// 🧩 Returns the panel to its unconstructed condition.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Reset();

    /// 🧩 Records the GPU-generated sky dome across one viewport leaf, cropped to the camera's field of view.
    /// in    Extent   [px]  the leaf body the sky fills
    /// in    Applied  [-]   the environment and the sky identity; written through only for the crop
    /// note  🔴 The identity is the host's; a zero identity records nothing and the leaf's own ground shows.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void RecordViewportSky(const PlaneExtent& Extent, const SceneDirectoryContext& Applied);

    // 🔴 `RecordGroundGrid` is withdrawn. The ground lattice is solved per pixel
    //    by `WorkspaceOverlayFragment.slang` mode 3 — see the note where its 1828 lines
    //    were removed from the source. The host pushes the camera to the overlay
    //    pass instead of handing it screen-space line segments.

    /// 🧩 Records the world-origin translation gizmo — the three vivid axis arrows and the centre
    ///    handle — into the overlay geometry, projected through the same pinhole as the grid.
    /// in    Extent   [px]  the leaf body the gizmo is projected into
    /// in    Applied  [-]   the camera's pose and position
    /// in    Overlay  [-]   the overlay record the gizmo is written into; the host owns one per
    ///                      viewport leaf, so the pass can clip each leaf's geometry to its own box
    /// note  🔴 The gizmo colours are FULL-OPACITY straight alpha — the whole reason the overlay has
    ///        its own GPU pass is that the interface's premultiplied blend washed them out.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void RecordGizmo(const PlaneExtent& Extent, SceneDirectoryContext& Applied, OverlayGeometry& Overlay);

    /// 🧩 Records the outliner column and its details pane across one outliner leaf.
    /// in    Rows   [-]  the entity rows, borrowed for the tick
    /// in    RowCount [-]  how many of them stand
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void RecordOutliner(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                        EntityRow* Rows, std::uint32_t RowCount);

    /// 🧩 Records entity properties and, for the Editor Camera only, its bookmark page.
    void RecordProperties(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                          const EntityRow* Rows, std::uint32_t RowCount,
                          std::uint32_t& InspectorTab, bool OutlinePresentation = false);

private:

    void RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph, const ThemeToken& Hue,
                          const char* Titled, const char* Secondary);
    void RecordTransfer(const PlaneExtent& Extent, SceneDirectoryContext& Applied);
    void RecordDetailOptions(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                             std::uint32_t Index, const EntityRow& Current);
    void RecordPropertyCards(const PlaneExtent& Extent, SceneDirectoryContext& Applied,
                             const EntityRow* Rows, std::uint32_t RowCount);
    void RecordEnvironmentCard(SceneDirectoryContext& Applied,
                               const PlaneExtent& Extent, float& Sweep, std::uint32_t& CardIndex,
                               const char* Caption,
                               const char* const* SliderCaptions,
                               const char* const* UnitGlyphs,
                               const double* Minimums, const double* Maximums,
                               double* Values, std::uint32_t SliderCount,
                               const std::uint32_t* DecimalPlaces = nullptr);
    void RecordCameraBookmarks(const PlaneExtent& Extent, SceneDirectoryContext& Applied);

    ControlIndex*           Interaction = nullptr;        // [-] - borrowed; never owned
    MotionIntegrator*           Motion = nullptr;        // [-] - borrowed; never owned
    RecordingSurface*           Surface = nullptr;       // [-] - borrowed; never owned
    const ThemeProfile*         Appearance = nullptr;    // [-] - borrowed; never owned
    ShellColour                 Tinted = {};             // [-] - the shell's own colour record
    ShellMetric                 Scaled = {};             // [-] - re-applied on every appearance resolve

    ControlPanel                Controls = {};           // [-] - tab strips, revision rows, fold animation
    ComponentSpecification      EnvironmentControls = {};   // [-] - the environment slider rows

    PointerCondition            Sampled = {};            // [-] - this tick's contact
    ModifierCondition           Modified = {};           // [-] - Command/Ctrl and Shift selection intent

    ControlIdentity RowContacts[SceneDirectoryContext::EntityLimit]    = {};
    ControlIdentity RowDisclosures[SceneDirectoryContext::EntityLimit] = {};
    ControlIdentity RowPresences[SceneDirectoryContext::EntityLimit]   = {};
    ControlIdentity DetailOptions[SceneDirectoryContext::EntityLimit][3] = {};
    ControlIdentity CardFolds[SceneDirectoryContext::CardLimit]        = {};
    // 🧩 One identity per card row, so a transform axis can be dragged.
    ControlIdentity CardFields[SceneDirectoryContext::CardLimit][4]     = {};
    ControlIdentity InspectorStrip = {};
    ControlIdentity OutlineStrip    = {};
    ControlIdentity InspectCall     = {};
    ControlIdentity DirectoryCall   = {};
    ControlIdentity TransferBack    = {};
    ControlIdentity TransferCalls[2] = {}; // [-] - Import and Save calls from the directory footer
    ControlIdentity TransferExecute = {};  // [-] - confirms the dedicated transfer-page declaration
    ControlIdentity TransferArrows[2] = {};
    ControlIdentity TransferFormatOptions[11] = {};
    ControlIdentity TransferFields[4] = {};
    ControlIdentity TransferOptions[18] = {};
    ControlIdentity TransferCardFolds[6] = {};
    // Only one option card opens at once, so its five visible rows safely share this identity run.
    ControlIdentity TransferCardFields[5] = {};
    ControlIdentity BookmarkNames[SceneDirectoryContext::CameraBookmarkLimit] = {};
    ControlIdentity BookmarkSave    = {};
    ControlIdentity BookmarkRecall  = {};
    ControlIdentity BookmarkRetire  = {};

    // 📐 Shared travel for Directory, Properties, and Transfer pages.
    SlidingPages    OutlinePages    = {};
    std::uint32_t   TransferMotion  = 0u;   // [-] - format rail travel
    double          TransferFrom    = 0.0;
    double          TransferTarget  = 0.0;
    OverflowScroll  TransferOverflow = {}; // [-] - shared vertical page overflow

    // 📐 Separate inner carousel slots for the outliner inspector and a dedicated properties leaf.
    std::uint32_t   InspectorMotion[2]   = {};   // [-] - eased Properties / Bookmarks travel
    std::uint32_t   InspectorDeparted[2] = {};   // [-] - inner page left
    std::uint32_t   InspectorArriving[2] = {};   // [-] - inner page arriving

    // 📐 The properties page's own list scroll, eased the same way the layer stack's is.
    float           PropertyShown   = 0.0f;
    float           PropertyWanted  = 0.0f;
    float           PropertyContent = 0.0f;   // [px] - last tick's laid-out column height

    /// 🧩 Advances the properties column's scroll and answers where it stands.
    float AdvanceOutlineScroll(SceneDirectoryContext& Applied, const PlaneExtent& Viewport);
    ControlIdentity SearchField     = {};
    ControlIdentity FacetFold       = {};
    static constexpr std::uint32_t EnvironmentFieldLimit = 6u;
    ControlIdentity EnvironmentSliders[SceneDirectoryContext::CardLimit][EnvironmentFieldLimit] = {};
    ControlIdentity EnvironmentQuality = {};

    FacetPanel                   Facets = {};         // [-] - the filter card

    bool   EnvironmentArmed[SceneDirectoryContext::CardLimit][EnvironmentFieldLimit] = {};
    double EnvironmentFrom[SceneDirectoryContext::CardLimit][EnvironmentFieldLimit]  = {};
};

} // namespace Slate
