//============================================================================================================================================
//                                                     INTERFACEVALIDATIONHOST.CPP
//============================================================================================================================================
// 🧩 Records the control sheet and reusable global-interface components for direct visual comparison.

#include "SlateRuntime/Session/HostEnvironment/Api/HostEnvironment.h"
#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ThemeInterchange/Api/ThemeInterchange.h"
#include "SlateUI/Interface/ControlPanel/Api/ControlPanel.h"
#include "SlateUI/Interface/EditorPanel/Api/EditorPanel.h"
#include "SlateUI/Interface/FacetPanel/Api/FacetPanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/InterfaceExchange.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateVulkan/Device/HostLifecycle/Api/HostLifecycle.h"

#include <cstdio>
#include <cstring>

//------------------------------------------------------------------------------------------------------------------------
//                                                          FIGURES
//------------------------------------------------------------------------------------------------------------------------

namespace
{

using namespace Slate;

constexpr std::uint32_t InitialWidth  = 1280u;   // [px]
constexpr std::uint32_t InitialHeight = 900u;    // [px] - the component catalogue scrolls beyond 720

constexpr const char* WindowTitle = "Slate \u2014 Interface Validation";
constexpr const char* HostName    = "InterfaceValidationHost";

// 📐 🔴 The sheet declares `scale-110` on its own column. That is a property of the reference page and not of
//    the controls, so it is **not** folded into AuthoredReduction — it arrives here, as the artist scale, which
//    is exactly the seam a real application would expose to its own preference.
constexpr double SheetColumnScale = 1.10;   // [-] - scale-110

// 📝 The sheet's own page ground, #050505, as four unit ordinates. HostLifecycle clears the colour target
//    to this before the host records anything over it.
constexpr float PageGroundInk[4] = { 0.0196f, 0.0196f, 0.0196f, 1.0f };   // [-]

/// 🧩 Copies the device handles across the layer seam into the attachment the interface declares.
/// note  🔴 `SlateVulkan` cannot name `InterfaceAttachment` — it lives one layer above — so `HostLifecycle`
///        offers the same handles as `DeviceOffering` and the host performs the copy.

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT THE SHEET SEATS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every datum the sheet presents, applied at the value the sheet itself states.
/// note  🔴 The host owns these and the panel does not. `14` §1's gate is visible here as an ordinary struct:
///       every control below is handed a reference into this record and writes through it.
struct ValidationConfiguration
{
    std::uint32_t  SelectionTaken = 0u;      // [-]   - "Entry name"
    double         Degree         = 123.0;   // [deg] - value="123"
    double         Percent        =  85.0;   // [%]   - value="85"
    double         Pixel          = 123.0;   // [px]  - value="123"
    double         Rotation       =   0.0;   // [deg] - rotationValue = 0
    bool           Snapping       = true;    // [-]   - data-checked="true"
    bool           GridLines      = false;   // [-]
    bool           AspectLocked   = false;   // [-]
    bool           EntryOne       = true;    // [-]   - data-checked="true"
    bool           EntryTwo       = false;   // [-]
    bool           EntryThree     = true;    // [-]   - data-checked="true"
    bool           EntryFour      = false;   // [-]
    std::uint32_t  SizeTaken       = 2u;      // [-]   - the taken stop is L
    bool           InspectorDocked = true;    // [-]   - reference switch begins taken
    std::uint32_t  WorkspaceTaken  = 1u;      // [-]   - Texturing
    std::uint32_t  InspectorTaken  = 0u;      // [-]   - Properties
    bool           TransformOpen   = true;    // [-]   - folding property card
    std::uint32_t  ShadingTaken    = 0u;      // [-]   - dropdown selection
    PickerColour   Albedo          = { 214u, 216u, 222u, 255u };   // [-] - HSV colour picker
    bool           OutlineExpanded[5] = { true, true, true, true, true };   // [-] - branch disclosure
    bool           OutlineTaken[5]    = { false, true, true, false, false };   // [-] - additive multi-selection
    bool           OutlinePresent[5]  = { true, true, true, false, true };   // [-] - row presence action
    std::uint32_t  OutlineEnclosure[5] = { 5u, 0u, 1u, 1u, 0u };   // [-] - enclosing record; five is root
    std::uint32_t  OutlineOrder[5]     = { 0u, 0u, 0u, 1u, 1u };   // [-] - sibling position
    bool           FacetEnabled[14]    = { true, true, true, true, true, false, false,
                                           true, true, true, true, false, false, false };   // [-] - active filters
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE INTERPOLANT BUDGET
//------------------------------------------------------------------------------------------------------------------------

// Base controls share this host's interaction and motion stores. FacetPanel and EditorPanel retain their
// own interaction indices but draw from the same motion integrator.
constexpr std::uint32_t SheetControls  = 31u;
constexpr std::uint32_t FacetControls  = 24u + 2u;
constexpr std::uint32_t EditorControls = 11u * 22u;
constexpr std::uint32_t EasesPerControl = 2u;
constexpr std::uint32_t DemandedEases =
    (SheetControls + FacetControls + EditorControls) * EasesPerControl;

static_assert(DemandedEases <= MotionIntegrator::EaseCapacity,
              "the validation components exceed the shared eased-interpolant capacity");
static_assert(SheetControls <= ControlIndex::ControlCapacity,
              "the validation component sheet exceeds its interaction index capacity");

/// 🧩 Every identity the sheet's controls are registered under, claimed once at bring-up.
struct ValidationIdentities
{
    ControlIdentity  Selection    = {};
    ControlIdentity  Degree       = {};
    ControlIdentity  Percent      = {};
    ControlIdentity  Pixel        = {};
    ControlIdentity  Rotation     = {};
    ControlIdentity  Snapping     = {};
    ControlIdentity  GridLines    = {};
    ControlIdentity  AspectLocked = {};
    ControlIdentity  EntryOne     = {};
    ControlIdentity  EntryTwo     = {};
    ControlIdentity  EntryThree   = {};
    ControlIdentity  EntryFour    = {};
    ControlIdentity  Size         = {};
    ControlIdentity  TooltipLight  = {};
    ControlIdentity  TooltipDark   = {};
    ControlIdentity  InspectorDock = {};
    ControlIdentity  WorkspaceMode = {};
    ControlIdentity  InspectorTabs = {};
    ControlIdentity  TransformFold = {};
    ControlIdentity  ShadingMenu   = {};
    ControlIdentity  AlbedoPicker       = {};
    ControlIdentity  OutlineRows[5]     = {};
    ControlIdentity  OutlineExpansion[5] = {};
};

/// 🧩 Reservations every identity the sheet needs, refusing in full rather than in part.
/// out   Result  [-]  refuses with ExtentExhausted when the index declines any requested identity
/// note  🔴 A partial registration would leave one control reading another's fade, which draws correctly on the
///       first tick and diverges on the second — the hardest possible shape of defect to attribute.
Deliver<ValidationIdentities> RegisterEvery(ControlIndex& IncomingInteraction)
{
    ValidationIdentities  Target;
    ControlIdentity*      Every[] = {
        &Target.Selection, &Target.Degree,     &Target.Percent,    &Target.Pixel,
        &Target.Rotation,  &Target.Snapping,   &Target.GridLines,  &Target.AspectLocked,
        &Target.EntryOne,  &Target.EntryTwo,   &Target.EntryThree, &Target.EntryFour,
        &Target.Size,      &Target.TooltipLight, &Target.TooltipDark,
        &Target.InspectorDock, &Target.WorkspaceMode, &Target.InspectorTabs, &Target.TransformFold,
        &Target.ShadingMenu, &Target.AlbedoPicker,
        &Target.OutlineRows[0], &Target.OutlineRows[1], &Target.OutlineRows[2],
        &Target.OutlineRows[3], &Target.OutlineRows[4],
        &Target.OutlineExpansion[0], &Target.OutlineExpansion[1], &Target.OutlineExpansion[2],
        &Target.OutlineExpansion[3], &Target.OutlineExpansion[4]
    };

    for (ControlIdentity* Target : Every)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();

        if (!Registered.Resolved)
        {
            return Deliver<ValidationIdentities>::Refuse(Registered.Error);
        }

        *Target = Registered.Resolve();
    }

    return Deliver<ValidationIdentities>::Result(Target);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE MEASURE OVERLAY
//------------------------------------------------------------------------------------------------------------------------

#ifdef SLATE_DEBUG

/// 🧩 One control's recorded extent, retained for the overlay to stroke over it.
/// note  🔍 Debug only. Nothing in a shipped build registers, retains or records any of this.
struct MeasuredExtent
{
    const char*  Naming  = "";   // [-] - static text; never allocated
    PlaneExtent  Where   = {};   // [px] - what the control was handed
    float        Target = 0.0f; // [px] - what the sheet declares it should span across, already reduced
};

/// 🧩 Retains what each control was arranged at, so the overlay can compare it against the sheet.
/// note  🔍 The comparison is the point: "exact" is checkable rather than asserted. A control whose extent
///       disagrees with its declared figure by more than half a pixel is reported in the pointer ink.
class MeasureOverlay
{
public:

    static constexpr std::uint32_t MeasuredLimit = 32u;   // [-] - never allocated

    void Retain(const char* Naming, const PlaneExtent& Where, float Target)
    {
        if (MeasuredCount >= MeasuredLimit)
            return;

        Measured[MeasuredCount].Naming  = Naming;
        Measured[MeasuredCount].Where   = Where;
        Measured[MeasuredCount].Target = Target;
        ++MeasuredCount;
    }

    void Discard()
    {
        MeasuredCount = 0u;
    }

    /// 🧩 Strokes every retained extent and reports the four factors the appearance was resolved by.
    void Record(RecordingSurface& Surface, const ThemeProfile& Appearance,
                double ArtistScale, float Width, std::uint32_t Disagreeing) const
    {
        const ControlColour&    Colours = Appearance.Control;
        const ControlMetric& Measure = Appearance.ControlMeasure;

        for (std::uint32_t Index = 0u; Index < MeasuredCount; ++Index)
        {
            const MeasuredExtent& Held  = Measured[Index];
            const float           Apart = Held.Where.Height() - Held.Target;
            const bool            Agreed = (Apart < 0.5f && Apart > -0.5f);

            Surface.Edge(Held.Where, Agreed ? Colours.RulerPointer : Colours.StopTaken, 1.0f, 0.0f, CornerNone);
        }

        // 📝 The header states every factor separately, so a wrong extent is attributable to which multiplier
        //    produced it without a debugger. A single product would say only that something is wrong.
        char Reading[192] = {};
        std::snprintf(Reading, sizeof(Reading),
                      "reduction %.2f  density %u  artist %.2f  applied %.3f  extent %.0f  measured %u  apart %u",
                      static_cast<double>(AuthoredReduction),
                      static_cast<unsigned>(Measure.Density),
                      ArtistScale,
                      static_cast<double>(Measure.AppliedFactor),
                      static_cast<double>(Width),
                      static_cast<unsigned>(MeasuredCount),
                      static_cast<unsigned>(Disagreeing));

        Surface.TextRun(12.0f, 12.0f, Colours.RulerPointer, Reading, Measure.RowText, 0.0f, true);
    }

    /// 🧩 How many retained extents disagree with the figure the sheet declares for them.
    std::uint32_t Disagreeing() const
    {
        std::uint32_t Counted = 0u;

        for (std::uint32_t Index = 0u; Index < MeasuredCount; ++Index)
        {
            const float Apart = Measured[Index].Where.Height() - Measured[Index].Target;

            if (Apart >= 0.5f || Apart <= -0.5f)
                ++Counted;
        }

        return Counted;
    }

private:

    MeasuredExtent  Measured[MeasuredLimit] = {};   // [-] - never allocated
    std::uint32_t   MeasuredCount             = 0u;   // [-]
};

#endif   // SLATE_DEBUG

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                            MAIN
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    using namespace Slate;

    // ① The five lifetimes — window, instance, surface, diagnostic, device, chain, slots, recordings.
    HostDeclaration Declared;
    Declared.Naming        = HostName;
    Declared.WindowCaption = WindowTitle;
    Declared.InitialWidth  = InitialWidth;
    Declared.InitialHeight = InitialHeight;
    Declared.Pacing        = LatencyIntent::SteadyPacing;

    // 🔴 Requested in EVERY configuration. `Build\Construct.bat` produces Release by default, so gating
    //    this compiled the validation layer out of the binary that is actually run — and every run then
    //    reported itself unwatched, which is the one answer indistinguishable from a clean one.
    Declared.DiagnosticRequested = true;

    HostLifecycle Lifetime;

    if (!Lifetime.ConstructHost(Declared).Resolved)
        return 1;

    // ② 🔴 The interface, the integrator, the index and the panel — **not** `ViewportSequence`. The sheet
    //    declares no drawers, and constructing two of them to hold both closed forever would be recording
    //    chrome nothing in the reference has, which is the opposite of what a validation host is for.
    InterfaceExchange Interface;

    if (!Interface.AttachInterface(Lifetime.Offering()).Resolved)
    {
        std::printf("%s \u2014 the interface context was rejected\n", HostName);
        return 1;
    }

    MotionIntegrator Motion;
    ControlIndex Interaction;
    RecordingSurface       Surface;
    FontLoader             Fonts;
    ComponentSpecification  Panel;
    ControlPanel             ReferenceControls;
    FacetPanel               Facets;
    EditorPanel              EditorPanels;
    PanelStructure           EditorPartition;
    EditorPanelConfiguration     EditorConfiguration;

    // 📝 The appearance file sits beside the executable and is read once, before any panel is recorded. A
    //    first run has no file yet, which is the ordinary case and not a fault — the build's own appearance
    //    stands and the first colour the artist changes writes the file.
    const char* const InvokedAs = (ArgumentCount > 0) ? ArgumentValues[0] : "";

    // 🔴 Resolve font archives relative to the executable, not the working directory.  Slate is not
    //    always launched from the repository root, so a relative path silently falls back to the ImGui
    //    default font.
    constexpr std::uint32_t FontPathLimit = 512u;
    char FontArchivesPath[FontPathLimit] = {};
    {
        const std::size_t Spanned = std::strlen(InvokedAs);
        std::size_t Folder = 0u;
        for (std::size_t Place = Spanned; Place > 0u; --Place)
        {
            const char Letter = InvokedAs[Place - 1u];
            if (Letter == '\\' || Letter == '/') { Folder = Place; break; }
        }
        if (Folder > 0u) std::memcpy(FontArchivesPath, InvokedAs, Folder);
        const char Leaf[] = "EngineContent/FontArchives";
        std::memcpy(FontArchivesPath + Folder, Leaf, sizeof(Leaf));
    }

    ThemeSelection Selected;
    static_cast<void>(ThemeInterchange::AdoptBeside(InvokedAs, Selected));
    char LoadedFontFamily[64] = {};

    if (const auto Verdict = Interaction.AttachMotion(Motion); !Verdict.Resolved)
    {
        std::printf("%s \u2014 the interaction index was rejected: %s\n", HostName, Verdict.Error.Detail);
        std::fflush(stdout);
        return 1;
    }

    const Deliver<ValidationIdentities> Registered = RegisterEvery(Interaction);

    if (!Registered.Resolved)
    {
        std::printf("%s \u2014 the index rejected an registration: %s\n", HostName, Registered.Error.Detail);
        std::fflush(stdout);
        return 1;
    }

    const ValidationIdentities Target = Registered.Resolve();

    ThemeProfile Appearance = ResolveTinted(1.0, SheetColumnScale, 0.0f, Selected);
    std::snprintf(Appearance.Fonts.Family, sizeof(Appearance.Fonts.Family), "%s", Selected.FontFamily);
    Discard(Interface.ApplyWorkspaceStyle(Appearance.WorkspaceMeasure, Appearance.Workspace));
    Surface.ApplyTypographyScale(Appearance.TextScale);
    Surface.ApplyCornerScale(Appearance.CornerScale);
    Surface.ApplyFontLoader(Fonts);
    Discard(Fonts.Discover(FontArchivesPath));
    Discard(Fonts.Load(FontArchivesPath, Appearance.Fonts, 1.0f));
    std::snprintf(LoadedFontFamily, sizeof(LoadedFontFamily), "%s", Appearance.Fonts.Family);

    // Every construct refusal below is reported WITH its detail and flushed before the return. A refusal
    //    that printed only a headline and left the text in a buffered stdout was invisible: the window is
    //    already open by this point, so the host appeared to "open white and crash" when it had in fact
    //    stated its reason and exited 1. Naming the stage and flushing it is what makes the next one legible.
    const auto Rejected = [](const char* Stage, const Refusal& Rejected) -> int
    {
        std::printf("%s \u2014 %s was rejected: %s\n", HostName, Stage, Rejected.Detail);
        std::fflush(stdout);
        return 1;
    };

    if (const auto Verdict = Panel.ConstructComponents(Interaction, Surface, Appearance); !Verdict.Resolved)
        return Rejected("the control panel", Verdict.Error);

    if (const auto Verdict = ReferenceControls.ConstructControlPanel(Interaction, Surface, Appearance); !Verdict.Resolved)
        return Rejected("the reference controls", Verdict.Error);

    if (const auto Verdict = Facets.ConstructFacetPanel(Motion, Surface, Appearance); !Verdict.Resolved)
        return Rejected("the facet panel", Verdict.Error);

    if (const auto Verdict = EditorPanels.ConstructEditorPanel(Motion, Surface, Appearance); !Verdict.Resolved)
        return Rejected("the editor panels", Verdict.Error);

    EditorPartition.ConstructPanelPartition(PanelSubject::Viewport);

    // What the sheet applies, and the runs it presents — the sole owner of every datum below.
    ValidationConfiguration Applied;

    const char* SelectionOptions[] = { "Entry name", "Second Entry", "Third Entry" };
    const char* SizeStops[]        = { "S", "M", "L", "XL" };

    SelectionDeclaration Selection;
    Selection.Caption     = "Selection";
    Selection.Options     = SelectionOptions;
    Selection.OptionCount = 3u;

    MagnitudeDeclaration Degree;
    Degree.Caption   = "Degree";
    Degree.UnitGlyph = "\u00B0";

    MagnitudeDeclaration Percent;
    Percent.Caption   = "Percent";
    Percent.UnitGlyph = "%";

    MagnitudeDeclaration Pixel;
    Pixel.Caption   = "Pixel";
    Pixel.UnitGlyph = "px";

    RulerDeclaration Rotation;
    Rotation.Caption   = "Rotation";
    Rotation.UnitGlyph = "\u00B0";

    ToggleDeclaration Snapping;     Snapping.Caption     = "Enable Snapping";
    ToggleDeclaration GridLines;    GridLines.Caption    = "Show Grid Lines";
    ToggleDeclaration AspectLocked; AspectLocked.Caption = "Lock Aspect Ratio";

    SubsetDeclaration EntryOne;   EntryOne.Caption   = "Entry one";
    SubsetDeclaration EntryTwo;   EntryTwo.Caption   = "Entry two";
    SubsetDeclaration EntryThree; EntryThree.Caption = "Entry three";
    SubsetDeclaration EntryFour;  EntryFour.Caption  = "Entry four";

    StopDeclaration Size;
    Size.Caption   = "Size";
    Size.Stops     = SizeStops;
    Size.StopCount = 4u;

    constexpr const char* TooltipBody =
        "Try connecting to another server. In case of a repeated error, please wait, "
        "if nothing happens, try to write a letter to the post office.";

    TooltipDeclaration TooltipLight;
    TooltipLight.Title      = "Tooltip";
    TooltipLight.Body       = TooltipBody;
    TooltipLight.Figure     = SymbolSubject::BulbFilament;
    TooltipLight.Appearance = TooltipAppearance::Light;

    TooltipDeclaration TooltipDark = TooltipLight;
    TooltipDark.Appearance         = TooltipAppearance::Dark;

    const char* WorkspaceCaptions[] = { "Parametric", "Texturing", "Game Editor" };
    const char* InspectorCaptions[] = { "Properties", "History" };

    SwitchDeclaration InspectorDock;
    InspectorDock.Caption = "Dock Inspector";

    SegmentDeclaration WorkspaceMode;
    WorkspaceMode.Captions     = WorkspaceCaptions;
    WorkspaceMode.CaptionCount = 3u;

    TabDeclaration InspectorTabs;
    InspectorTabs.Captions     = InspectorCaptions;
    InspectorTabs.CaptionCount = 2u;

    const char* TransformRuns[] = { "Position", "Rotation", "Scale" };
    const char* ShadingOptions[] = { "Smooth", "Faceted", "Flat" };
    const char* PropertyCards[]  = { "Record · 2 fields", "Transform · 3 fields", "Appearance · 4 fields" };
    const char* RevisionCards[]  = { "Set Parameter · 10:42", "Translate SOL_Boss · 10:37", "Created SOL_Boss · 10:31" };

    CarouselDeclaration InspectorCarousel;
    InspectorCarousel.LeadingRuns   = PropertyCards;
    InspectorCarousel.LeadingCount  = 3u;
    InspectorCarousel.TrailingRuns  = RevisionCards;
    InspectorCarousel.TrailingCount = 3u;

    FoldDeclaration TransformFold;
    TransformFold.Caption   = "TRANSFORM";
    TransformFold.BodyRuns  = TransformRuns;
    TransformFold.BodyCount = 3u;

    SelectionDeclaration ShadingMenu;
    ShadingMenu.Caption     = "Shading";
    ShadingMenu.Options     = ShadingOptions;
    ShadingMenu.OptionCount = 3u;
    ShadingMenu.Indicator   = SelectionIndicator::Marked;

    ColourPickerDeclaration AlbedoPicker;
    AlbedoPicker.Caption = "Albedo";

    const char* FacetOptions[14] = {
        "Base Colour", "Metallic", "Roughness", "Height", "Normal", "Opacity", "Emissive",
        "Ambient Occlusion", "Anisotropy", "Anisotropy Angle", "Clearcoat", "Refraction Index",
        "Sheen", "Subsurface"
    };
    const ThemeToken FacetColours[14] = {
        Covering(0xB87333u), Covering(0x8B5CF6u), Covering(0x3B82F6u), Covering(0x8A8A8Au),
        Covering(0x10B981u), Covering(0x94A3B8u), Covering(0xF59E0Bu), Covering(0x6B7280u),
        Covering(0x22D3EEu), Covering(0x0EA5E9u), Covering(0xE2E8F0u), Covering(0xA78BFAu),
        Covering(0xF472B6u), Covering(0xFB7185u)
    };
    FacetDeclaration FacetCard;
    FacetCard.Caption       = "Filters";
    FacetCard.Options       = FacetOptions;
    FacetCard.Colours          = FacetColours;
    FacetCard.OptionCount   = 14u;
    FacetCard.LockedIndex = 0u;

    OutlineDeclaration OutlineRows[5] = {
        { "Part",         0u, 2u, true,  true  },
        { "Bodies",       1u, 3u, true,  true  },
        { "SOL_Boss",     2u, 0u, true,  true  },
        { "SOL_Rib",      2u, 0u, true,  false },
        { "SOL_Housing",  1u, 0u, true,  true  }
    };

    RevisionDeclaration RevisionRows[3] = {
        { "Set Parameter",  "Radius = 6.25 mm", "10:42" },
        { "Translate SOL_Boss", "Moved 4.20 mm", "10:37" },
        { "Created SOL_Boss", "Initial condition", "10:31" }
    };

#ifdef SLATE_DEBUG
    MeasureOverlay Overlay;
    bool           OverlayShown = false;
#endif

    double ArtistScale     = SheetColumnScale;
    float  ResolvedAgainst = 0.0f;

    // 📝 🔴 The sheet is a scrolling page — `py-32` above and below a column that runs past 1000 px at the
    //    reduced scale. A host that recorded it into a fixed window would present the first four cards and
    //    silently lose the last two, which is exactly the kind of disagreement this host exists to catch.
    float ScrollY  = 0.0f;   // [px] - how far the column has been carried upward
    float ColumnMeasured = 0.0f;  // [px] - what the previous tick's column actually occupied

    std::printf("%s \u2014 running\n", HostName);

    // ─────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                       THE TICK LOOP
    // ─────────────────────────────────────────────────────────────────────────────────────────────────────

    while (Lifetime.Active())
    {
        const TickPass Pass = Lifetime.Await(PageGroundInk);

        if (Pass.Current == TickCondition::Closed)
            break;

        // 📝 The pending font load is applied here, between frames, where the atlas is unlocked and the
        //    re-rasterisation does not hold up a recording. Inside the frame RemoveFont would trip the
        //    vendor's locked-atlas assert.
        Discard(Fonts.FlushPending());

        // 🔴 The DEVICE was rebuilt, so every device handle the interface holds names an object the vendor
        //    has returned. The interface alone is reconstructed: the index, the panel and the recording
        //    surface hold no device handle, so retiring them would discard interaction state — the grab
        //    an artist is mid-drag on — over a rebuild that did not invalidate any of it.
        //    Tested before DisplayRecovered because a device rebuild raises both.
        if (Lifetime.DeviceRecovered())
        {
            Interface.Reclaim();

            if (!Interface.AttachInterface(Lifetime.Offering()).Resolved)
            {
                std::printf("%s \u2014 the interface could not be rebuilt on the recovered device\n", HostName);
                break;
            }

            // 📝 The display recovery this rebuild also raised is consumed here; the reconstruction above
            //    already took the counts the new chain holds.
            static_cast<void>(Lifetime.DisplayRecovered());
        }

        // The chain was re-established; the interface is told the counts it now holds, exactly once.
        else if (Lifetime.DisplayRecovered())
        {
            const DeviceOffering Offered = Lifetime.Offering();
            // 🔴 Read, not discarded. An interface still holding the previous image counts records
            //    against a chain depth that no longer exists, and the vendor reports that as a
            //    descriptor mismatch several ticks later rather than as the resize that caused it.
            if (!Interface.Renegotiate(Offered.MinimumDisplayImageCount, Offered.DisplayImageCount))
            {
                std::printf("%s \u2014 the interface rejected the restated image counts\n", HostName);
            }
        }

        if (Pass.Current != TickCondition::Recording)
            continue;

        const double ElapsedMs = Pass.ElapsedMilliseconds;

        // ① Open the interface tick and adopt the surface. 🔴 A refusal here must NOT return to the top
        //    of the loop: Await has already acquired an image and opened a recording, and only Complete
        //    closes them. The tick records nothing and the cleared ground is presented instead.
        bool ContentBuilt = Interface.Advance().Resolved;

        if (ContentBuilt && !Surface.Adopt().Resolved)
        {
            Discard(Interface.Abandon());
            ContentBuilt = false;
        }

        if (ContentBuilt)
        {

        const DisplayCondition& Display = Surface.Display();

        // ② 🔴 Re-resolve only when a factor actually moved. Resolved unconditionally, every extent is
        //    recomputed sixty times a second to the same figures, and the density classification steps
        //    while a drag is live.
        if (Display.Width != ResolvedAgainst)
        {
            Appearance      = ResolveTinted(Display.DisplayScale, ArtistScale, Display.Width, Selected);
            std::snprintf(Appearance.Fonts.Family, sizeof(Appearance.Fonts.Family), "%s", Selected.FontFamily);
            Discard(Interface.ApplyWorkspaceStyle(Appearance.WorkspaceMeasure, Appearance.Workspace));
    Surface.ApplyTypographyScale(Appearance.TextScale);
    Surface.ApplyCornerScale(Appearance.CornerScale);
    if (std::strcmp(Appearance.Fonts.Family, LoadedFontFamily) != 0)
    {
        Discard(Fonts.Discover(FontArchivesPath));
        Discard(Fonts.PreparePreviews(1.0f));
        Fonts.RequestLoad(FontArchivesPath, Appearance.Fonts, 1.0f);
        std::snprintf(LoadedFontFamily, sizeof(LoadedFontFamily), "%s", Appearance.Fonts.Family);
    }
            ResolvedAgainst = Display.Width;

        }

        Motion.Advance(ElapsedMs);

        Panel.Advance(Surface.Pointer(), ElapsedMs);
        ReferenceControls.Advance(Surface.Pointer(), ElapsedMs);
        Facets.Advance(Surface.Pointer(), ElapsedMs);
        EditorPanels.Advance(Surface.Pointer(), ElapsedMs);

#ifdef SLATE_DEBUG
        Overlay.Discard();
#endif

        const ControlColour&    Colours = Appearance.Control;
        const ControlMetric& Measure = Appearance.ControlMeasure;

        // ③ The page ground, then the sheet's own column, centred.
        const PlaneExtent Page = Spanning(0.0f, 0.0f, Display.Width, Display.Height);

        Surface.Ground(Page, Colours.PageGround, 0.0f, CornerNone);

        const float ColumnX  = (Measure.ColumnX < Display.Width - Measure.PagePad * 2.0f)
                                 ? Measure.ColumnX
                                 : Display.Width - Measure.PagePad * 2.0f;
        const float ColumnMinimum  = (Display.Width - ColumnX) * 0.5f;

        // 📐 The wheel carries the column, and the travel is held between zero and whatever the column
        //    overruns the display by. Clamped against the **previous** tick's measured extent, because this
        //    tick's is not known until every card has been arranged — and a scroll clamped against a stale
        //    extent by one tick is invisible, where an unclamped one scrolls into empty space forever.
        const float Overrun = (ColumnMeasured > Display.Height)
                            ? (ColumnMeasured - Display.Height) : 0.0f;

        ScrollY -= Surface.Pointer().WheelY * Measure.CardGapY * 2.0f;
        ScrollY  = (ScrollY < 0.0f) ? 0.0f
                      : (ScrollY > Overrun) ? Overrun : ScrollY;

        float Cursor = Measure.PagePad - ScrollY;

        // 📝 Every card below is arranged from its own row extents and then recorded, so the arrangement the
        //    overlay measures and the arrangement the control was handed are the same object by construction.
        const auto AdvanceCard = [&](const float* RowExtents, std::uint32_t RowCount) -> CardArrangement
        {
            const CardArrangement Arranged = Panel.ArrangeCard(ColumnMinimum, Cursor, ColumnX,
                                                               RowExtents, RowCount);
            Panel.RecordCard(Arranged);
            Cursor = Arranged.Enclosure.MaximumY + Measure.CardGapY;
            return Arranged;
        };

        const auto RowAt = [&](const CardArrangement& Card, const float* RowExtents,
                               std::uint32_t Index) -> PlaneExtent
        {
            float X = Card.Interior.MinimumY;

            for (std::uint32_t Passed = 0u; Passed < Index; ++Passed)
                X += RowExtents[Passed] + Card.RowGap;

            return Spanning(Card.Interior.MinimumX, X, Card.Interior.Width(), RowExtents[Index]);
        };

        // ④ Card one — the selection field and the three magnitude rows.
        const float TopRows[4] = { Measure.FieldHeight, Measure.FieldHeight,
                                   Measure.FieldHeight, Measure.FieldHeight };
        const CardArrangement TopCard = AdvanceCard(TopRows, 4u);

        const PlaneExtent SelectionRow = RowAt(TopCard, TopRows, 0u);
        const PlaneExtent DegreeRow    = RowAt(TopCard, TopRows, 1u);
        const PlaneExtent PercentRow   = RowAt(TopCard, TopRows, 2u);
        const PlaneExtent PixelRow     = RowAt(TopCard, TopRows, 3u);

        Panel.SelectionField(Target.Selection, SelectionRow, Selection, Applied.SelectionTaken);
        Panel.MagnitudeRow(Target.Degree,  DegreeRow,  Degree,  Applied.Degree);
        Panel.MagnitudeRow(Target.Percent, PercentRow, Percent, Applied.Percent);
        Panel.MagnitudeRow(Target.Pixel,   PixelRow,   Pixel,   Applied.Pixel);

        // ⑤ Card two — the two tooltip triggers inside their well.
        const float TooltipRows[1] = { Measure.TooltipWellFloor };
        const CardArrangement TooltipCard = AdvanceCard(TooltipRows, 1u);
        const PlaneExtent     TooltipWell = RowAt(TooltipCard, TooltipRows, 0u);

        Surface.Ground(TooltipWell, Colours.WellGround, Measure.TooltipWellRadius, CornerAll);
        Surface.Edge(TooltipWell, Colours.CardEdge, Measure.CardEdgeWeight, Measure.TooltipWellRadius, CornerAll);

        // 📐 The sheet places its two triggers `items-end` with a gap of 32 units between them, and lifts each
        //    by `ml-8`. Both are applied against the well's lower padding, which is what items-end states.
        const float TriggerY = TooltipWell.MaximumY - Measure.TooltipWellInset - Measure.TriggerExtent;
        const float TriggerPair   = Measure.TriggerExtent * 2.0f + Measure.TooltipWellGap;
        const float TriggerTop  = TooltipWell.MinimumX + (TooltipWell.Width() - TriggerPair) * 0.5f;

        const PlaneExtent LightTrigger = Spanning(TriggerTop + Measure.TriggerLeadX, TriggerY,
                                                  Measure.TriggerExtent, Measure.TriggerExtent);
        const PlaneExtent DarkTrigger  = Spanning(TriggerTop + Measure.TriggerExtent + Measure.TooltipWellGap,
                                                  TriggerY, Measure.TriggerExtent, Measure.TriggerExtent);

        Panel.TooltipTrigger(Target.TooltipLight, LightTrigger, TooltipLight);
        Panel.TooltipTrigger(Target.TooltipDark,  DarkTrigger,  TooltipDark);

        // ⑥ Card three — the rotation ruler.
        const float RulerRows[1] = { Measure.FieldHeight + Measure.CardRowGap * 0.5f + Measure.RulerHeight };
        const CardArrangement RulerCard = AdvanceCard(RulerRows, 1u);

        Panel.RotationRuler(Target.Rotation, RowAt(RulerCard, RulerRows, 0u), Rotation, Applied.Rotation);

        // ⑦ Card four — the three toggles inside their well.
        const float ToggleWellHeight = Measure.ToggleRowHeight * 3.0f + Measure.WellGapY * 2.0f
                                     + Measure.WellInset * 2.0f;
        const float ToggleRows[1] = { ToggleWellHeight };
        const CardArrangement ToggleCard = AdvanceCard(ToggleRows, 1u);
        const PlaneExtent     ToggleWell = RowAt(ToggleCard, ToggleRows, 0u);

        Surface.Ground(ToggleWell, Colours.WellGround, Measure.WellRadius, CornerAll);
        Surface.Edge(ToggleWell, Colours.CardEdge, Measure.CardEdgeWeight, Measure.WellRadius, CornerAll);

        const auto WellRow = [&](const PlaneExtent& Well, float RowHeight, std::uint32_t Index) -> PlaneExtent
        {
            return Spanning(Well.MinimumX + Measure.WellInset,
                            Well.MinimumY + Measure.WellInset +
                                static_cast<float>(Index) * (RowHeight + Measure.WellGapY),
                            Well.Width() - Measure.WellInset * 2.0f, RowHeight);
        };

        Panel.ToggleRow(Target.Snapping,     WellRow(ToggleWell, Measure.ToggleRowHeight, 0u),
                        Snapping,     Applied.Snapping);
        Panel.ToggleRow(Target.GridLines,    WellRow(ToggleWell, Measure.ToggleRowHeight, 1u),
                        GridLines,    Applied.GridLines);
        Panel.ToggleRow(Target.AspectLocked, WellRow(ToggleWell, Measure.ToggleRowHeight, 2u),
                        AspectLocked, Applied.AspectLocked);

        // ⑧ Card five — the four multi-select rows inside their well.
        const float SubsetWellHeight = Measure.SubsetRowHeight * 4.0f + Measure.WellGapY * 3.0f
                                     + Measure.WellInset * 2.0f;
        const float SubsetRows[1] = { SubsetWellHeight };
        const CardArrangement SubsetCard = AdvanceCard(SubsetRows, 1u);
        const PlaneExtent     SubsetWell = RowAt(SubsetCard, SubsetRows, 0u);

        Surface.Ground(SubsetWell, Colours.WellGround, Measure.WellRadius, CornerAll);
        Surface.Edge(SubsetWell, Colours.CardEdge, Measure.CardEdgeWeight, Measure.WellRadius, CornerAll);

        Panel.SubsetRow(Target.EntryOne,   WellRow(SubsetWell, Measure.SubsetRowHeight, 0u),
                        EntryOne,   Applied.EntryOne);
        Panel.SubsetRow(Target.EntryTwo,   WellRow(SubsetWell, Measure.SubsetRowHeight, 1u),
                        EntryTwo,   Applied.EntryTwo);
        Panel.SubsetRow(Target.EntryThree, WellRow(SubsetWell, Measure.SubsetRowHeight, 2u),
                        EntryThree, Applied.EntryThree);
        Panel.SubsetRow(Target.EntryFour,  WellRow(SubsetWell, Measure.SubsetRowHeight, 3u),
                        EntryFour,  Applied.EntryFour);

        // ⑨ Card six — the magnitude stops.
        const float StopRows[1] = { Measure.StopStripHeight };
        const CardArrangement StopCard = AdvanceCard(StopRows, 1u);

        Panel.MagnitudeStops(Target.Size, RowAt(StopCard, StopRows, 0u), Size, Applied.SizeTaken);

        // ⑩ The general-purpose filter card — wrapped active chips, removal, clear-all, and the shared dropdown.
        const float FacetY = Facets.MeasureHeight(ColumnX, FacetCard, Applied.FacetEnabled);
        const PlaneExtent FacetExtent = Spanning(ColumnMinimum, Cursor, ColumnX, FacetY);
        Discard(Facets.Record(FacetExtent, FacetCard, Applied.FacetEnabled));
        Cursor = FacetExtent.MaximumY + Measure.CardGapY;

        // ⑪ The global-interface primitives — switch, segmented selector, inspector carousel, fold and dropdown.
        const float ReferenceRows[7] = { 32.0f, 38.0f, 31.0f, 154.0f, 129.0f, 124.0f, 341.0f };
        const CardArrangement ReferenceCard = AdvanceCard(ReferenceRows, 7u);

        ReferenceControls.SwitchToggle(Target.InspectorDock, RowAt(ReferenceCard, ReferenceRows, 0u),
                                       InspectorDock, Applied.InspectorDocked);
        ReferenceControls.SegmentedSelection(Target.WorkspaceMode, RowAt(ReferenceCard, ReferenceRows, 1u),
                                          WorkspaceMode, Applied.WorkspaceTaken);
        ReferenceControls.TabStrip(Target.InspectorTabs, RowAt(ReferenceCard, ReferenceRows, 2u),
                                   InspectorTabs, Applied.InspectorTaken);
        ReferenceControls.CarouselPages(Target.InspectorTabs, RowAt(ReferenceCard, ReferenceRows, 3u),
                                        InspectorCarousel, Applied.InspectorTaken);
        ReferenceControls.CollapsibleCard(Target.TransformFold, RowAt(ReferenceCard, ReferenceRows, 4u),
                                          TransformFold, Applied.TransformOpen);
        Panel.SelectionField(Target.ShadingMenu, RowAt(ReferenceCard, ReferenceRows, 5u),
                             ShadingMenu, Applied.ShadingTaken);
        ReferenceControls.ColourPicker(Target.AlbedoPicker, RowAt(ReferenceCard, ReferenceRows, 6u),
                                       AlbedoPicker, Applied.Albedo);

        // ⑫ One identity-backed outline. A drop's destination is declared here; document ownership stays outside
        //     the panel exactly as it does for selection and visibility.
        for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
        {
            OutlineRows[RecordIndex].EnclosedCount = 0u;
            if (Applied.OutlineEnclosure[RecordIndex] < 5u)
                ++OutlineRows[Applied.OutlineEnclosure[RecordIndex]].EnclosedCount;
        }

        std::uint32_t CurrentRecords[5] = {};
        std::uint32_t CurrentCount = 0u;
        const auto LinearizeOutline = [&](auto&& Traverse, std::uint32_t Enclosing, std::uint32_t Depth) -> void
        {
            for (std::uint32_t Position = 0u; Position < 5u; ++Position)
            {
                for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
                {
                    if (Applied.OutlineEnclosure[RecordIndex] != Enclosing ||
                        Applied.OutlineOrder[RecordIndex] != Position)
                        continue;

                    OutlineRows[RecordIndex].Depth = Depth;
                    CurrentRecords[CurrentCount++] = RecordIndex;
                    Traverse(Traverse, RecordIndex, Depth + 1u);
                }
            }
        };
        LinearizeOutline(LinearizeOutline, 5u, 0u);

        float OutlineExpansion[5] = {};
        for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
        {
            OutlineExpansion[RecordIndex] = ReferenceControls.OutlineExpansion(
                Target.OutlineExpansion[RecordIndex], Applied.OutlineExpanded[RecordIndex],
                OutlineRows[RecordIndex].AnimationEnabled);
        }

        float RowPresence[5] = {};
        float OutlineHeight = 0.0f;
        for (std::uint32_t CurrentIndex = 0u; CurrentIndex < CurrentCount; ++CurrentIndex)
        {
            const std::uint32_t RecordIndex = CurrentRecords[CurrentIndex];
            float Presence = 1.0f;
            std::uint32_t Enclosing = Applied.OutlineEnclosure[RecordIndex];
            std::uint32_t WalkCount = 0u;

            while (Enclosing < 5u && WalkCount++ < 5u)
            {
                Presence *= OutlineExpansion[Enclosing];
                Enclosing = Applied.OutlineEnclosure[Enclosing];
            }

            RowPresence[CurrentIndex] = Presence;
            OutlineHeight += 28.0f * Presence;
        }

        std::uint32_t DragSource = 5u;
        const float DragX = Surface.Pointer().PositionX - Interaction.OriginX();
        const float DragY = Surface.Pointer().PositionY - Interaction.OriginY();
        const bool DragTravelled = DragX * DragX + DragY * DragY >= 16.0f;

        if (DragTravelled)
        {
            for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
            {
                const bool BodyHeld = Interaction.Holding(Target.OutlineRows[RecordIndex]) &&
                                      Interaction.HeldPart(Target.OutlineRows[RecordIndex]) == ControlPart::Body;
                const bool BodyReleased = Interaction.Released(Target.OutlineRows[RecordIndex]) &&
                                          Interaction.ReleasedControlPart(Target.OutlineRows[RecordIndex]) == ControlPart::Body;
                if (BodyHeld || BodyReleased)
                    DragSource = RecordIndex;
            }
        }

        const float OutlineRowsHeight[1] = { OutlineHeight };
        const CardArrangement OutlineCard = AdvanceCard(OutlineRowsHeight, 1u);
        float OutlineCursor = OutlineCard.Interior.MinimumY;
        std::uint32_t DropTarget = 5u;
        OutlineDropPlacement DropPlacement = OutlineDropPlacement::Absent;

        for (std::uint32_t CurrentIndex = 0u; CurrentIndex < CurrentCount; ++CurrentIndex)
        {
            const std::uint32_t RecordIndex = CurrentRecords[CurrentIndex];
            const float Presence = RowPresence[CurrentIndex];
            if (Presence <= 0.0f)
                continue;

            const PlaneExtent Row = Spanning(OutlineCard.Interior.MinimumX, OutlineCursor,
                                             OutlineCard.Interior.Width(), 28.0f);
            OutlineDropPlacement RowPlacement = OutlineDropPlacement::Absent;

            if (DragSource < 5u && DragSource != RecordIndex &&
                Row.Encloses(Surface.Pointer().PositionX, Surface.Pointer().PositionY))
            {
                const float RowFraction = (Surface.Pointer().PositionY - Row.MinimumY) / Row.Height();
                RowPlacement = (RowFraction < 0.25f) ? OutlineDropPlacement::Before
                             : (RowFraction > 0.75f) ? OutlineDropPlacement::After
                                                     : OutlineDropPlacement::Enclosed;
                DropTarget = RecordIndex;
                DropPlacement = RowPlacement;
            }

            const PlaneExtent Revealed = Spanning(Row.MinimumX, Row.MinimumY,
                                                  Row.Width(), 28.0f * Presence);
            Surface.Confine(Revealed);
            ReferenceControls.OutlineRow(Target.OutlineRows[RecordIndex], Row, OutlineRows[RecordIndex], true,
                                         OutlineExpansion[RecordIndex], RowPlacement,
                                         Applied.OutlineExpanded[RecordIndex], Applied.OutlineTaken[RecordIndex],
                                         Applied.OutlinePresent[RecordIndex]);
            Surface.Release();
            OutlineCursor += 28.0f * Presence;
        }

        if (DragSource < 5u && DropTarget < 5u && Interaction.Released(Target.OutlineRows[DragSource]))
        {
            const std::uint32_t ProposedEnclosure = (DropPlacement == OutlineDropPlacement::Enclosed)
                                                   ? DropTarget : Applied.OutlineEnclosure[DropTarget];
            bool CycleDeclared = ProposedEnclosure == DragSource;
            std::uint32_t Walking = ProposedEnclosure;
            std::uint32_t WalkCount = 0u;

            while (!CycleDeclared && Walking < 5u && WalkCount++ < 5u)
            {
                CycleDeclared = Walking == DragSource;
                Walking = Applied.OutlineEnclosure[Walking];
            }

            if (!CycleDeclared)
            {
                const std::uint32_t DepartingEnclosure = Applied.OutlineEnclosure[DragSource];
                const std::uint32_t DepartingOrder = Applied.OutlineOrder[DragSource];
                for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
                {
                    if (RecordIndex != DragSource && Applied.OutlineEnclosure[RecordIndex] == DepartingEnclosure &&
                        Applied.OutlineOrder[RecordIndex] > DepartingOrder)
                        --Applied.OutlineOrder[RecordIndex];
                }

                std::uint32_t IncomingOrder = 0u;
                if (DropPlacement == OutlineDropPlacement::Enclosed)
                {
                    Applied.OutlineExpanded[DropTarget] = true;
                }
                else
                {
                    IncomingOrder = Applied.OutlineOrder[DropTarget];
                    if (DropPlacement == OutlineDropPlacement::After)
                        ++IncomingOrder;
                }

                for (std::uint32_t RecordIndex = 0u; RecordIndex < 5u; ++RecordIndex)
                {
                    if (RecordIndex != DragSource && Applied.OutlineEnclosure[RecordIndex] == ProposedEnclosure &&
                        Applied.OutlineOrder[RecordIndex] >= IncomingOrder)
                        ++Applied.OutlineOrder[RecordIndex];
                }

                Applied.OutlineEnclosure[DragSource] = ProposedEnclosure;
                Applied.OutlineOrder[DragSource] = IncomingOrder;
            }
        }

        // ⑬ The revision timeline, presented from newest to oldest.
        const float RevisionRowExtents[3] = { 54.0f, 54.0f, 54.0f };
        const CardArrangement RevisionCard = AdvanceCard(RevisionRowExtents, 3u);

        for (std::uint32_t Index = 0u; Index < 3u; ++Index)
        {
            ReferenceControls.RevisionRow(RowAt(RevisionCard, RevisionRowExtents, Index),
                                          RevisionRows[Index], Index == 0u);
        }

        // ⑭ The reusable editor partition stands inside a workspace-sized page. Its leaf headers, footers,
        //     menus, split rails, resizing and withdrawal are live; scene and UV GPU targets remain skeletal.
        const float EditorX = (Display.Width - 32.0f < 1152.0f)
                                ? Display.Width - 32.0f : 1152.0f;
        const float EditorY = (Display.Height - 48.0f > 600.0f)
                                 ? Display.Height - 48.0f : 600.0f;
        const PlaneExtent EditorExtent = Spanning((Display.Width - EditorX) * 0.5f,
                                                  Cursor,
                                                  EditorX,
                                                  EditorY);
        Discard(EditorPanels.Record(EditorExtent, EditorPartition, EditorConfiguration));
        Cursor = EditorExtent.MaximumY + Measure.CardGapY;

        // 🔴 The deferred sweep — every menu and every tooltip card, above every row recorded above.
        Panel.RecordDeferred();
        Facets.RecordDeferred();

        // 📝 What the component sequence occupied, retained so the next tick's scroll stays bounded.
        //    The trailing page padding lets the last validation fixture clear the lower edge.
        ColumnMeasured = Cursor + ScrollY + Measure.PagePadY;

#ifdef SLATE_DEBUG
        // 🔍 The overlay retains what the sheet declares each control should span across, and strokes the
        //    disagreement. Recorded last so it sits above even the deferred sweep.
        Overlay.Retain("selection", SelectionRow, Measure.FieldHeight);
        Overlay.Retain("degree",    DegreeRow,    Measure.FieldHeight);
        Overlay.Retain("percent",   PercentRow,   Measure.FieldHeight);
        Overlay.Retain("pixel",     PixelRow,     Measure.FieldHeight);
        Overlay.Retain("light",     LightTrigger, Measure.TriggerExtent);
        Overlay.Retain("dark",      DarkTrigger,  Measure.TriggerExtent);
        Overlay.Retain("toggles",   ToggleWell,   ToggleWellHeight);
        Overlay.Retain("subsets",   SubsetWell,   SubsetWellHeight);

        if (OverlayShown)
            Overlay.Record(Surface, Appearance, ArtistScale, Display.Width, Overlay.Disagreeing());
#endif

            // ⑫ Seal the tick and record it into the recording Await opened.
            // 🔴 The surface is retired at the seal. This host records through it directly rather than
            //    through ViewportSequence, so it performs the retirement ViewportSequence would.
            Surface.Retire();

            if (Interface.Seal().Resolved)
            {
                Discard(Lifetime.BeginDisplay());

                // 🔴 Read. A rejected Record presents the cleared ground with nothing on it, which is
                //    indistinguishable from a panel that drew nothing, so the refusal is named here.
                if (!Interface.Record(Pass.Recording))
                {
                    std::printf("%s \u2014 the interface content was not recorded\n", HostName);
                }
            }
            else
            {
                Discard(Interface.Abandon());
            }
        }

        // ⑬ Close the scope, submit, present, advance. A rejected present re-establishes the chain rather
        //    than ending the loop, and a tick whose content rejected still presents the cleared ground.
        if (!Lifetime.Complete().Resolved)
            break;
    }

    // ─────────────────────────────────────────────────────────────────────────────────────────────────────
    //                                                      RECLAMATION
    // ─────────────────────────────────────────────────────────────────────────────────────────────────────

    // 📝 The interface content is retired before the lifetimes it was constructed over. HostLifecycle idles
    //    the device inside Reclaim, so nothing here needs to.
    // 🔴 Read before Reclaim. The register is Device lifetime, and a reclaimed device has emptied it.
    const std::uint32_t Serious = Lifetime.StateDiagnostics();

    EditorPanels.Reset();
    EditorPartition.Reset();
    Facets.Reset();
    ReferenceControls.Reset();
    Panel.Reset();
    Interaction.Reset();
    Surface.Reset();
    Interface.Reclaim();
    Lifetime.Reclaim();

    std::printf("%s \u2014 exited cleanly\n", HostName);

    // 🔴 Returned rather than only stated. This is the host a validation run is driven through, so a
    //    serious arrival has to fail the run and not merely appear in it.
    return (Serious == 0u) ? 0 : 1;
}
