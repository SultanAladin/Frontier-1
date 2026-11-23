//============================================================================================================================================
//                                                           RECORDINGSURFACE.H
//============================================================================================================================================
// 🧩 Primitives in, recorded commands out — the drawing half of the interface seam, with no ImGui spelling anywhere.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/CameraCondition.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/TextComponent/Api/FontLoader.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <vulkan/vulkan.h>

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE PLANE EXTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One axis-aligned extent in display pixels, stated as its two corners.
/// note  The coordinate increases **downward**, as the display does. Nothing in the interface uses the
///       upward convention `NumericTolerance.h` declares for clip space; the two never meet.
/// tag   guarantee, nonallocating, nonthrowing
struct PlaneExtent
{
    float  MinimumX  = 0.0f;   // [px] - leading edge
    float  MinimumY = 0.0f;   // [px] - upper edge
    float  MaximumX   = 0.0f;   // [px] - trailing edge
    float  MaximumY  = 0.0f;   // [px] - lower edge

    constexpr float Width() const  { return MaximumX  - MinimumX;  }
    constexpr float Height() const { return MaximumY - MinimumY; }

    constexpr bool Encloses(float X, float Y) const
    {
        return X >= MinimumX && X < MaximumX && Y >= MinimumY && Y < MaximumY;
    }
};

/// 🧩 Constructs an extent from an origin and a span.
/// cost  ✔️
constexpr PlaneExtent Spanning(float X, float Y, float Width, float Height)
{
    return PlaneExtent{ X, Y, X + Width, Y + Height };
}

/// 🧩 Which axis a scrim's colour varies along.
/// note  The coordinate axis is declared first and carries the ordinal zero, so the enumeration's default and
///       the scrim's default are the same statement rather than two that must be kept agreeing.
/// tag   guarantee
enum class ScrimAxis : std::uint32_t
{
    Y    = 0u,   // [-] - varies from MinimumY to MaximumY; the card's caption scrim
    X     = 1u,   // [-] - varies from MinimumX to MaximumX; the ruler's leading and trailing fade
    AxisCount = 2u    // [-] - the closed count, never an axis
};

/// 🧩 Which corners of a rounded primitive are rounded. Absent bits are square.
/// note  The source's card is rounded on four; its drawer body on none; its tongue on none but is clipped
///       instead. A single mask covers all three rather than three primitives that drift apart.
inline constexpr std::uint32_t CornerNone         = 0u;
inline constexpr std::uint32_t CornerLeadingUpper = 1u << 0;
inline constexpr std::uint32_t CornerTrailingUpper= 1u << 1;
inline constexpr std::uint32_t CornerTrailingLower= 1u << 2;
inline constexpr std::uint32_t CornerLeadingLower = 1u << 3;
inline constexpr std::uint32_t CornerAll          = 0x0Fu;

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE ARRIVED CONDITION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What the pointer did between the previous tick and this one.
/// note  🔴 Read from the interface library's accumulated window condition and **not** from `04`'s
///        `InputExchange`. The two observe the same device through two surfaces that never merge: the stroke
///        path needs arrival stamps at device rate, the interface needs one resolved position per tick, and
///        merging them would give the canvas the interface's rate.
/// tag   guarantee, nonallocating, nonthrowing
struct PointerCondition
{
    float   PositionX   = 0.0f;    // [px] - in the display's drawable extent
    float   PositionY  = 0.0f;    // [px]
    float   TravelX     = 0.0f;    // [px] - since the previous tick
    float   TravelY    = 0.0f;    // [px]
    float   WheelY     = 0.0f;    // [-]  - notches; positive is away from the artist
    bool    ContactHeld     = false;   // [-]  - the primary contact is down now
    bool    ContactPressed       = false;   // [-]  - it went down during this tick
    bool    ContactDoublePressed = false;   // [-]  - the second press of a double contact arrived
    bool    ContactReleased      = false;   // [-]  - it came up during this tick
    bool    SecondaryHeld        = false;   // [-]  - the secondary contact is down now
    bool    SecondaryPressed     = false;   // [-]  - the secondary contact arrived this tick
    bool    SecondaryReleased    = false;   // [-]  - the secondary contact ended this tick
    double  HeldDuration         = 0.0;     // [ms] - how long the primary contact has been down; zero while it is not
};

/// 🧩 Text and editing-key arrivals sampled with the pointer for one interface tick.
/// note  Printable ASCII is carried because the standing font surface does not yet provide shaped IME runs.
/// tag   guarantee, nonallocating, nonthrowing
struct TextInputCondition
{
    static constexpr std::uint32_t IntakeLimit = 32u;

    char          Intake[IntakeLimit] = {};   // [-] - printable characters, terminated
    std::uint32_t IntakeCount            = 0u;  // [-] - bytes preceding the terminator
    bool          AcceptPressed          = false;   // [-] - Enter
    bool          CancelPressed          = false;   // [-] - Escape
    bool          BackspacePressed       = false;   // [-] - remove preceding character
    bool          DeletePressed          = false;   // [-] - remove following character
    bool          HomePressed            = false;   // [-] - move to the run's leading edge
    bool          EndPressed             = false;   // [-] - move to the run's trailing edge
    bool          LeftPressed            = false;   // [-] - move one character toward the leading edge
    bool          RightPressed           = false;   // [-] - move one character toward the trailing edge
};

/// 🧩 What the display reported for this tick.
/// tag   guarantee, nonallocating, nonthrowing
struct DisplayCondition
{
    float   Width  = 0.0f;   // [px] - the drawable extent
    float   Height = 0.0f;   // [px]
    double  Elapsed      = 0.0;    // [ms] - since the previous tick; what every interpolant is advanced by
    double  DisplayScale = 1.0;    // [-]  - what ThemeProfile was resolved against
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE KEY SUBJECTS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The keys the shell arbitrates for itself, named by what they do rather than by their scan position.
/// note  🔴 A closed roster and not a scan coordinate. The seam exists so that a panel may ask "did the artist
///        summon" without naming a vendor key enumeration, which `00` §2.2 keeps inside this unit. Adding a
///        key is adding a line here and a line in the source's translation, and nothing else moves.
/// note  ⚠️ Every arrival is edge-triggered and unrepeated — the shell's summon must not fire sixty times
///        while the key is held down.
/// tag   guarantee
enum class KeySubject : std::uint32_t
{
    Summon       = 0u,   // [-] - Tab; carries the inspector between its two presentations
    Withdraw     = 1u,   // [-] - Escape; closes the inspector, then the summoned menu
    Retract      = 2u,   // [-] - Backspace; removes the last character of the hovered filter field

    // 📐 The layer stack's own roster, from `LayerstackV1`'s KEYBOARD section. Every one is edge-triggered
    //    and unrepeated on the same terms as the three above.
    DeclareTexturing      =  3u,   // [-] - P
    DeclareFill       =  4u,   // [-] - F
    DeclareAdjustment =  5u,   // [-] - A
    DeclareRetention  =  6u,   // [-] - R; the reference's `filter`
    DeclareDecal      =  7u,   // [-] - D
    DeclarePattern    =  8u,   // [-] - T
    DeclareFolder     =  9u,   // [-] - G
    AttachMask        = 10u,   // [-] - M
    Secure            = 11u,   // [-] - L
    Solo              = 12u,   // [-] - S
    Conceal           = 13u,   // [-] - H
    Seek              = 14u,   // [-] - forward slash; rouses the search run
    Rename            = 15u,   // [-] - F2
    Unfold            = 16u,   // [-] - Space; unfolds the taken card
    Retire            = 17u,   // [-] - Delete
    StepPrior         = 18u,   // [-] - Up arrow
    StepNext          = 19u,   // [-] - Down arrow
    Disclose          = 20u,   // [-] - Right arrow; opens the taken folder
    Withhold          = 21u,   // [-] - Left arrow; closes it
    Revert            = 22u,   // [-] - Z; commanded it reverts, commanded and shifted it reinstates

    // 🔴 The Select tool's own key, on the same edge-triggered terms as the roster above. It is NOT a
    //    layer-stack key, but the seam arbitrates exactly one keyboard and a second roster would give
    //    two places to ask "did Q arrive" — which is how a key ends up handled twice or not at all.
    ChooseSelect      = 23u,   // [-] - Q; makes Select the active tool, so nothing draws

    SubjectCount = 24u   // [-] - the closed count, never a subject
};

/// 🧩 Which modifiers stood down when a key arrived, so a caller may separate `D` from `⌘D`.
/// note  🔴 Read alongside `KeyPressed` rather than folded into it. The reference branches on the SAME key
///        by modifier — `d` declares a decal, `⌘d` copies the taken entry — so a seam that reported only
///        "D arrived" would make both branches fire from one press.
/// tag   guarantee, nonallocating, nonthrowing
struct ModifierCondition
{
    bool  Commanded = false;   // [-] - Control on Windows and Linux, Command on macOS
    bool  Shifted   = false;   // [-]
    bool  Alternate = false;   // [-]
};

//------------------------------------------------------------------------------------------------------------------------
//                                                          THE SEAM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Records the interface's own primitives into the open interface tick.
/// note  🔴 The second and last translation unit in the engine that names ImGui, and it names it only in its
///        source file. `00` §2.2 makes a **host** including `imgui.h` a defect; it does not forbid a second
///        unit inside `SlateUI`, which already owns the one copy. The alternative was recording panels through
///        built-in widgets, and no built-in widget produces a clipped tongue, a scrim or tracked small capitals
///        — the three things the source is mostly made of.
/// note  ⚠️ Every method is valid only between `InterfaceExchange::Advance` and `Seal`. Recording outside that
///        window writes into content nothing will assemble.
/// tag   owning
class RecordingSurface
{
public:

    RecordingSurface()                                   = default;
    RecordingSurface(const RecordingSurface&)            = delete;
    RecordingSurface& operator=(const RecordingSurface&) = delete;
    ~RecordingSurface()                                  = default;

    /// 🧩 Binds this surface to the open interface tick and samples the arrived condition.
    /// out   Result  [-]  refuses with CapabilityAbsent when no interface context is current
    /// post  Pointer and Display report this tick; every recording method is valid until Seal
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    /// 🧩 Which of the two shell layers a tick's recording is laid into.
    /// note  🔴 `Beneath` is behind every ImGui window; `Above` is in front of all of them. The workspace
    ///        ground belongs beneath, so a docked panel sits on it; the drawers belong above, because
    ///        `DockWorkspace.html` overlays them on everything and a window docked full-width would
    ///        otherwise bury the control centre and the asset browser.
    /// tag   guarantee
    enum class ShellLayer : std::uint32_t
    {
        Beneath   = 0u,   // [-] - behind every window; the workspace ground
        Above     = 1u,   // [-] - in front of every window; the drawers
        LayerCount = 2u   // [-] - the closed count, never a layer
    };

    /// 🧩 Opens a tick's recording against one shell layer.
    /// in    Layer    [-]  which side of the window stack this tick's content is laid on
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> Adopt(ShellLayer Layer = ShellLayer::Beneath);

    /// 🧩 Moves the standing tick's recordings to the other shell layer, without re-adopting it.
    /// in    Layer    [-]  which side of the window stack subsequent recordings land on
    /// note  🔴 A layer change is NOT an adoption. `Adopt` re-samples the pointer, resets the confine
    ///        depth and stamps a new tick ordinal — performing all three merely to move the drawers above
    ///        the windows sampled the pointer twice within one tick and discarded any confine the caller
    ///        was holding. This changes the destination list and nothing else.
    /// note  🔴 `Beneath` RETURNS TO THE WORKSPACE WINDOW when one stands entered, rather than to the
    ///        background list. Every caller of this pair raises one small thing — a tool options card, a
    ///        context menu, the orientation widget — and then asks to be put back where it was. Inside a
    ///        workspace window "where it was" is that WINDOW's list, and answering with the background list
    ///        silently redirected everything recorded afterwards: the second viewport leaf's sky went to the
    ///        background, the leaf's own ground and "3D VIEWPORT RENDER TARGET" caption were recorded into
    ///        the window over the top of it, and the sky vanished behind the placeholder while the grid —
    ///        a separate pass, recorded between the bands — carried on drawing. `SwitchToWindow` states the
    ///        window to return to; `LeaveWindow` withdraws it.
    /// note  ⚠️ Refuses when no tick stands adopted, so a layer change cannot open one by accident.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> SwitchLayer(ShellLayer Layer);

    /// 🧩 Moves subsequent primitives into the currently open workspace window's command list.
    /// out   Result  [-]  refuses when no tick or no workspace window stands open
    /// note  Used only between `InterfaceExchange::EnterWorkspaceWindow` and `LeaveWorkspaceWindow` so panel
    ///       content clips and orders with its own dockable window instead of the global shell layers.
    /// post  a later `SwitchLayer(Beneath)` returns here rather than to the background list
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    Deliver<bool> SwitchToWindow();

    /// 🧩 Withdraws the remembered workspace window, so `SwitchLayer(Beneath)` means the background again.
    /// note  🔴 Called by whoever left the workspace window, immediately after leaving it. Without this the
    ///        surface keeps naming a window that has been ended, and the next restore hands back a list
    ///        belonging to a panel the caller is no longer recording.
    /// post  subsequent recordings land on the background list
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void LeaveWindow();

    /// 🧩 What the pointer did this tick.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const PointerCondition& Pointer() const;

    /// 🧩 What text and editing keys arrived during this tick.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const TextInputCondition& TextInput() const;

    /// 🧩 What the display reported this tick.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    const DisplayCondition& Display() const;

    //--------------------------------------------------------------------------------------------------------
    //                                             GROUNDS AND EDGES
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 Fills an extent, optionally rounding the named corners.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Ground(const PlaneExtent& Extent, ThemeToken Colour, float Radius = 0.0f, std::uint32_t Corners = CornerAll);

    /// 🧩 Strokes an extent's edge inward from its stated corners.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Edge(const PlaneExtent& Extent, ThemeToken Colour, float Weight = 1.0f,
              float Radius = 0.0f, std::uint32_t Corners = CornerAll);

    /// 🧩 Fills an extent with a linearly varying colour — the card's caption scrim, and the ruler's fade.
    /// in    UpperColour  [-]  at MinimumY, or at MinimumX when the axis is X
    /// in    LowerColour  [-]  at MaximumY, or at MaximumX when the axis is X
    /// in    Axis      [-]  which axis the colour varies along; the default is what every existing caller means
    /// note  📐 A four-stop ramp is two of these. `Controls.html` masks its ruler with
    ///       `linear-gradient(to right, transparent, black 20%, black 80%, transparent)`, which records as one
    ///       X scrim over the leading fifth and a second, reversed, over the trailing fifth. Declaring a
    ///       four-stop primitive to serve one call site would put the stop fractions inside this component,
    ///       where the sheet that states them could never be compared against them.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Scrim(const PlaneExtent& Extent, ThemeToken UpperColour, ThemeToken LowerColour,
               ScrimAxis Axis = ScrimAxis::Y);

    /// 🧩 Covers the four areas outside a rounded extent after rectangular gradients were recorded into it.
    /// in    OutsideColour  [-]  the surrounding ground restored at each corner
    /// in    Radius      [px] the rounded corner radius
    /// cost  🚩
    /// tag   api, nonthrowing
    void MaskCorners(const PlaneExtent& Extent, ThemeToken OutsideColour, float Radius);

    /// 🧩 Fills a disc — every medallion and the meta separator.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Medallion(float CentreX, float CentreY, float Radius, ThemeToken Colour);

    /// 🧩 Fills a convex outline of up to eight corners — the drawer tongue's clip polygon.
    /// in    Corners      [px] alternating along and across ordinates, in winding order
    /// in    CornerCount  [-]  three to eight; anything else records nothing
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Tongue(const float* Corners, std::uint32_t CornerCount, ThemeToken Colour);

    //--------------------------------------------------------------------------------------------------------
    //                                                 SYMBOLS
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 Strokes one declared figure inside a square extent.
    /// in    Subject       [-]  an unresolved subject strokes the placeholder mark at the same extent
    /// in    SquareExtent  [px] the figure's 24-unit square is scaled onto this
    /// in    TurnRadians   [rad] rotation about the declared square's centre; zero preserves the figure
    /// cost  🚩
    /// tag   api, nonthrowing
    void Stroke(SymbolSubject Subject, const PlaneExtent& SquareExtent, ThemeToken Colour,
                float TurnRadians = 0.0f);

    //--------------------------------------------------------------------------------------------------------
    //                                                  TEXT
    //--------------------------------------------------------------------------------------------------------

    enum class TypographyRole : std::uint32_t
    {
        Title = 0u, Header = 1u, Subheader = 2u, Body = 3u,
        Label = 4u, Caption = 5u, Warning = 6u, Alert = 7u
    };

    /// 🧩 Records a run of text at a declared size and tracking.
    /// in    Tracking  [em] added to every advance; zero records the run in one command
    /// in    Emphatic  [-]  approximates a heavier weight by recording the run twice, offset by a third pixel
    /// in    Weight    [-]  the face the run draws with; Regular is the standing active face, and a family
    ///                      without the requested face falls back to its regular face exactly as `FontLoader`
    ///                      resolves it. `Emphatic` and a real weight are alternatives: a run that names a
    ///                      weight draws once, because the face itself is the emphasis.
    /// cost  🚩
    /// tag   api, nonthrowing
    void TextRun(float X, float Y, ThemeToken Colour, const char* Text,
                 float PointSize, float Tracking = 0.0f, bool Emphatic = false,
                 FontWeight Weight = FontWeight::Regular);
    /// Records a run with an explicit semantic role, including Warning and Alert which cannot be
    /// inferred reliably from an authored numeric size.
    void TextRunRole(float X, float Y, ThemeToken Colour, const char* Text, TypographyRole Role,
                     float Tracking = 0.0f);
    float MeasureRunRole(const char* Text, TypographyRole Role, float Tracking = 0.0f) const;

    /// 🧩 Records a run in capitals, for the two small-capital captions the source declares.
    /// note  ⚠️ ASCII only. A capital of a codepoint outside ASCII is a locale question, not a formatting one.
    /// cost  🚩
    /// tag   api, nonthrowing
    void TextRunCapitalised(float X, float Y, ThemeToken Colour, const char* Text,
                            float PointSize, float Tracking = 0.0f, bool Emphatic = false,
                            FontWeight Weight = FontWeight::Regular);

    /// 🧩 Records a run truncated to a stated extent, with a trailing ellipsis when it did not fit.
    /// in    Weight  [-]  the face the run draws and measures with; see `TextRun`
    /// note  The ellipsis is three full stops rather than U+2026, which the default typeface does not carry.
    /// cost  🚩
    /// tag   api, nonthrowing
    void TextRunTruncated(float X, float Y, float LimitX, ThemeToken Colour,
                          const char* Text, float PointSize, bool Emphatic = false,
                          FontWeight Weight = FontWeight::Regular);

    /// 🧩 The extent a run would occupy, without recording it.
    /// out   Width  [px] zero for empty text
    /// in    Weight  [-]  the face the run is measured with; see `TextRun`
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float MeasureRun(const char* Text, float PointSize, float Tracking = 0.0f,
                     FontWeight Weight = FontWeight::Regular) const;

    /// Sets the shared typography scale for all text measurement and drawing on this surface.
    void ApplyTypographyScale(float Scale);
    /// Applies the eight semantic text roles configured in the Control Centre. Existing panel runs are
    /// classified from their authored size, so drawing and measurement resolve through the same role.
    void ApplyTypographyRoles(const std::uint32_t Sizes[8], const std::uint32_t Weights[8]);
    /// Returns the effective size used by TextRun and MeasureRun for an authored size.
    float ResolveTypographySize(float AuthoredSize) const;
    void ApplyCornerScale(float Scale);
    void ApplyFontLoader(FontLoader& Loader);
    void ApplyFontPreview(ImFont* Preview);

    //--------------------------------------------------------------------------------------------------------
    //                                                  IMAGES
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 Records one sampled image across an extent, clipped by the standing confine.
    /// in    Identity  [-]  an opaque texture identity from `RegisterSampledImage`; the vendor resolves
    ///                      it to a sampled image (a `VkDescriptorSet` in the windowed hosts). Zero
    ///                      records nothing.
    /// in    U0 [-]  the sampled rectangle in the image's own unit interval; the caller crops
    /// in    V0 [-]  with these so the presented aspect need not match the texture's
    /// in    U1 [-]
    /// in    V1 [-]
    /// note  🔴 The image must already stand in a shader-readable layout; the host owns the transition
    ///        and the upload. This records only the quad.
    /// cost  🚩
    /// tag   api, nonthrowing
    void Image(const PlaneExtent& Extent, std::uintptr_t Identity,
               float U0 = 0.0f, float V0 = 0.0f, float U1 = 1.0f, float V1 = 1.0f);

    /// 🧩 Registers a sampled image with the interface's Vulkan backend, for `Image` to draw.
    /// in    Sampler    [-]  the Vulkan sampler the image is read with
    /// in    ImageView  [-]  the Vulkan image view the image is read through
    /// out   Result     [-]  a non-zero opaque identity, or zero when either handle is absent
    /// note  🔴 The registration belongs here and not in a host: `00` §2.2 makes a host that names the
    ///        vendor's ImGui attachment a defect, and this translation unit already owns the second and
    ///        last spelling of ImGui in the engine. The descriptor is allocated from the interface's own
    ///        pool, so it dies with the interface — a device rebuild re-creates and re-registers it, as
    ///        the font atlas is.
    /// note  📝 Valid outside the recording window as well as inside it: the backend's pool is
    ///        constructed with the interface, and the registration only allocates from it.
    /// cost  ✔️
    /// tag   api, nonthrowing
    std::uintptr_t RegisterSampledImage(VkSampler Sampler, VkImageView ImageView);

    /// 🧩 Records one sampled image as a triangle geometry with PER-VERTEX texture coordinates, so a caller
    ///    can warp the image — a perspective sky dome, a curved scrim — instead of the single quad
    ///    `Image` records.
    /// in    Identity      [-]  an opaque texture identity from `RegisterSampledImage`; zero records nothing
    /// in    Positions     [-]  `VertexCount` screen positions, `X, Y` pairs in display pixels
    /// in    UVs           [-]  `VertexCount` texture coordinates, `U, V` pairs in the image's unit interval
    /// in    VertexCount   [-]  how many vertices stand
    /// in    Indices       [-]  `IndexCount` triangle indices into the vertex runs
    /// in    IndexCount    [-]  a multiple of three; nothing is drawn otherwise
    /// note  🔴 The vertices are recorded into the open tick exactly as the quad is — white, clipped by
    ///        the standing confine — so the geometry and the quad composite identically. The caller keeps
    ///        the runs alive only for the call.
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void ImageGeometry(std::uintptr_t Identity,
                   const float* Positions, const float* UVs, std::uint32_t VertexCount,
                   const std::uint32_t* Indices, std::uint32_t IndexCount);

    /// 🧩 Records one polyline of a declared weight, in the standing confine.
    /// in    PointsX [-]  `Count` screen positions, display pixels
    /// in    PointsY [-]  `Count` screen positions, display pixels
    /// in    Count             [-]  at least two and at most 64; longer runs are clamped
    /// in    Colour            [-]  the line's colour; zero opacity records nothing
    /// in    Weight            [px] the line's thickness
    /// cost  🚩
    /// tag   api, nonallocating, nonthrowing
    void Polyline(const float* PointsX, const float* PointsY, std::uint32_t Count,
                  ThemeToken Colour, float Weight = 1.0f);

    /// 🧩 The baseline-to-baseline extent at one point size.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    float LineHeight(float PointSize) const;

    //--------------------------------------------------------------------------------------------------------
    //                                                CLIPPING
    //--------------------------------------------------------------------------------------------------------

    /// 🧩 Intersects the recording extent with the supplied one, until the matching Release.
    /// note  ⚠️ Every Confine must be matched. Sixteen may nest; a seventeenth records nothing and is ignored.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Confine(const PlaneExtent& Extent);

    /// 🧩 Restores the recording extent the matching Confine replaced.
    /// cost  ✔️
    /// tag   api, nonthrowing
    void Release();

    /// 🧩 Whether an extent is wholly outside the standing recording extent — what a scroll extent culls with.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Excluded(const PlaneExtent& Extent) const;

    /// 🧩 Closes the adopted tick, so nothing may record through this surface until the next Adopt.
    /// note  🔴 Called by whoever sealed the interface tick, immediately after sealing. A surface left
    ///        adopted past its seal accepts recordings into content that has already been assembled — the
    ///        commands are built, cost time, and are discarded without anything reporting it.
    /// post  every recording method refuses until Adopt delivers again
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Retire();

    /// 🧩 Whether a tick stands adopted and this surface may be recorded through.
    /// note  📝 The standing command list IS the condition, and the only one. An earlier reading of this
    ///        claimed a generation ordinal guarded it as well; nothing consumed the ordinal and no
    ///        reference carried one to compare against, so the claim described a guarantee the code did
    ///        not make. Retired rather than left standing — `00` §4 holds a comment to the same account
    ///        as the code it sits over.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    bool Recording() const;

    /// 🧩 Returns the surface to its constructed condition, releasing every unmatched Confine.
    /// note  🔴 Called instead of placement-new over a live object. Re-constructing over storage without
    ///       first destroying what sits in it is a defect the compiler will never report.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    void Reset();

private:

    // 🔴 The tick this surface was adopted for. `Adopt` stamps it, `Retire` clears it, and every recording
    //    method refuses when it is absent. Before this existed, a surface stayed usable after `Seal` and a
    //    late recording wrote into content nothing would ever assemble — no refusal, no diagnostic, and the
    //    only symptom a panel that silently failed to appear.
    void*             CommandSlot   = nullptr;   // [-] - opaque; the ImGui spelling stays in the source file

    // 🔴 The workspace window a restore returns to. `SwitchToWindow` stamps it, `LeaveWindow`, `Retire` and
    //    `Reset` clear it, and `SwitchLayer(Beneath)` prefers it over the background list. It is the whole
    //    of the second-panel sky defect: a restore is "put me back", and inside a window that is the
    //    window, not the shell.
    void*             WindowSlot    = nullptr;   // [-] - opaque; borrowed from the vendor for the tick
    PointerCondition  SampledPointer = {};       // [-] - sampled once, at Adopt
    TextInputCondition SampledText    = {};       // [-] - sampled once, at Adopt
    DisplayCondition  SampledDisplay = {};       // [-] - sampled once, at Adopt
    float             TypographyScale = 1.0f;    // [-] - shared text scale
    float             TypographySizes[8] = {24.0f, 20.0f, 16.0f, 14.0f, 12.0f, 10.0f, 14.0f, 14.0f};
    FontWeight        TypographyWeights[8] = {FontWeight::Semibold, FontWeight::Semibold,
                                               FontWeight::Medium, FontWeight::Regular,
                                               FontWeight::Medium, FontWeight::Regular,
                                               FontWeight::Medium, FontWeight::Semibold};
    float             CornerScale = 1.0f;        // [-] - shared corner scale
    FontLoader*       Fonts = nullptr;           // [-] - borrowed active font loader
    ImFont*           FontOverride = nullptr;    // [-] - one preview face for the current card
    std::uint32_t     ConfineDepth   = 0u;       // [-] - how many Confines stand unmatched
};

}   // namespace Slate
