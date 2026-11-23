//============================================================================================================================================
//                                                            TOOLSEQUENCE.H
//============================================================================================================================================
// 🧩 Everything the application holds that is not the document and not a panel's own layout — with exactly one owner.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"
#include "SlateDocument/Document/MaterialSpecification/Api/MaterialSpecification.h"
#include "SlateDocument/Document/PointerIntersection/Api/PointerIntersection.h"
#include "SlateDocument/Document/PropertySpecification/Api/PropertySpecification.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

// 📝 No tool and no brush; never a valid ordinal. Declared per unit, matching `12`'s `AbsentSlot` and `34`'s
//    `AbsentWork` — nothing reads two of them, and a shared spelling would be a dependency edge no traversal sees.
inline constexpr std::uint32_t AbsentTool = 0xFFFFFFFFu;   // [-] - nothing is active

//------------------------------------------------------------------------------------------------------------------------
//                                                    POINTER PRECEDENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 `14` §4.2's four levels, in the order they are consulted.
/// note  🔴 The ordinal **is** the precedence, ascending, so an arbitration is one integer comparison rather than
///        a table. Nothing outside this enumeration may hold the pointer.
/// tag   guarantee
enum class PointerPrecedence : std::uint32_t
{
    Interface       = 0u,   // [-] - the interface reports the pointer over itself
    Manipulator     = 1u,   // [-] - an open drag in `78`
    Stroke          = 2u,   // [-] - an open stroke in `22`
    Workspace       = 3u,   // [-] - picking and navigation
    PrecedenceCount = 4u    // [-] - the closed count, never a precedence
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT IS PRESENTED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of `80` §3's overlays is presented.
/// tag   guarantee
enum class OverlaySubject : std::uint32_t
{
    GroundLattice     = 0u,   // [-] - depth-tested, `08` §3 ⑩
    Guide             = 1u,   // [-] - depth-tested
    Wireframe         = 2u,   // [-] - depth-tested
    SeamDisplay       = 3u,   // [-] - depth-tested; `68`'s chart boundaries
    SurfaceAnnotation = 4u,   // [-] - depth-tested
    Manipulator       = 5u,   // [-] - depth-free, `08` §3 ⑪
    Pivot             = 6u,   // [-] - depth-free
    OverlayCount      = 7u    // [-] - the closed count, never an overlay
};

/// 🧩 How the workspace presents the shaded result.
/// note  ⚠️ Read by `66` and by `14`, and stored in neither. `76` §2: none of this travels with the document,
///        because a document that reopened in someone else's display mode has restored a decision about the
///        machine rather than about the work.
/// tag   guarantee
enum class DisplaySubject : std::uint32_t
{
    Shaded          = 0u,   // [-] - `18`'s full integration
    ChannelIsolated = 1u,   // [-] - one of `42`'s channels, presented alone
    Unlit           = 2u,   // [-] - albedo only, no incident radiance
    DisplayCount    = 3u    // [-] - the closed count, never a mode
};

/// 🧩 What `82` presents for a tool before the artist commits.
/// tag   guarantee
enum class PreviewSubject : std::uint32_t
{
    Absent       = 0u,   // [-] - the tool previews nothing
    Impression   = 1u,   // [-] - the brush impression under the cursor — `58` §8
    Placement    = 2u,   // [-] - a placement under the manipulator — `72` §3
    Parameter    = 3u,   // [-] - the result at the value being dragged
    PreviewCount = 4u    // [-] - the closed count, never a preview
};

/// 🧩 What shape a tool's edit takes in `10` §2.4's lifecycle.
/// note  🔴 Declared per tool rather than inferred from the pointer. A tool whose edit is immediate and one whose
///        edit is a drag seal a transaction at different moments, and inferring it from whether the pointer moved
///        would make a tap and a one-pixel drag two different operations.
/// tag   guarantee
enum class TransactionSubject : std::uint32_t
{
    Unrecorded   = 0u,   // [-] - the tool mutates nothing in the document
    Immediate    = 1u,   // [-] - one transaction, sealed where the gesture arrives
    Dragged      = 2u,   // [-] - Open, Amend, Seal — `10` §2.4
    SubjectCount = 3u    // [-] - the closed count, never a shape
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        ONE TOOL
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One tool — its parameters, and the three behaviours a caller must know without knowing which tool it is.
/// note  🔴 Parameters are `PropertySpecification` declarations from `10` §2.2 — typed, named, validated — so
///        `ToolPanel` presents any tool without knowing which. A tool that presents itself through hand-written
///        panel code is a tool the panel has to be edited to add, and the panel then knows every tool by name.
/// tag   owning
struct ToolSpecification
{
    std::string         Identity   = {};                            // [-] - the mechanism's spelling
    std::string         Current  = {};                            // [-] - what the artist reads
    PropertyIndex       Parameters = {};                            // [-] - `10` §2.2's declarations
    PointerPrecedence   Reserved    = PointerPrecedence::Workspace;  // [-] - which level it takes the pointer at
    PreviewSubject      Previewed  = PreviewSubject::Absent;        // [-] - what `82` shows
    TransactionSubject  Recorded   = TransactionSubject::Dragged;   // [-] - what shape its edit takes
};

/// 🧩 Every declared tool, addressed by the ordinal its declaration returned.
/// tag   owning
class ToolIndex
{
public:

    /// 🧩 Declares one tool and issues its ordinal.
    /// out   Result  [-]  refuses with ContentUnsupported for an empty identity or an out-of-range declaration,
    ///                     and with ExtentExhausted at the declared ceiling
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Declare(const ToolSpecification& Declaring);

    Deliver<const ToolSpecification*> Resolve(std::uint32_t ToolIndex) const;
    Deliver<ToolSpecification*>       Amend(std::uint32_t ToolIndex);

    /// 🧩 The ordinal one identity was declared at.
    /// out   Result  [-]  refuses with ContentUnsupported when nothing declares that identity
    /// cost  🚩
    /// tag   api, nonthrowing
    Deliver<std::uint32_t> Located(const std::string& Identity) const;

    std::uint32_t DeclaredCount() const;

private:

    static constexpr std::uint32_t ToolLimit = 256u;   // [-] - tools one application may declare

    std::vector<ToolSpecification>  Declared;   // [-] - by tool ordinal
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE POINTER CAPTURE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Who holds the pointer, and what the capture was taken against.
/// note  🔴 `Opened` is the pick as it stood at the moment of capture, and it is the reason `76` reads `74` at
///        all. `78` §2 fixes the drag plane at Open and never re-derives it per sample; re-deriving it makes the
///        manipulated object chase the cursor with increasing gain, which reads as the manipulator being
///        slippery rather than as the plane being wrong.
/// tag   nonallocating, nonthrowing
struct PointerCapture
{
    PointerPrecedence  Holder          = PointerPrecedence::Workspace;   // [-] - meaningful while declared
    ResolvedPointer    Opened          = {};                             // [-] - the pick the capture was taken against
    bool               CaptureDeclared = false;                          // [-] - a holder stands
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE TOOL SEQUENCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every item of state `14` §4.1 places beside the document, with one owner and one copy.
/// note  🔴 Nothing here is a transaction and nothing here enters `RevisionSequence` — `14` §4.1. Undo must not
///        step back through a colour change: the artist who picks a colour, textures, and undoes expects the stroke
///        to disappear and the colour to remain.
/// note  🔴 Held in `SlateDocument.lib` because **both** `SlateCompute` and `SlateUI` read it and `SlateCompute`
///        cannot link `SlateUI` — `00` §2. This is `86` §3's reasoning about the register, one layer up: the
///        storage lives where both can reach it and the interface presents it rather than owning it. Held in
///        `SlateUI` instead, `22` could not read the active colour, and the stroke would resolve against
///        something else — which is the defect the artist meets in the first minute.
/// note  ⚠️ Living in `SlateDocument.lib` is a **link-unit** fact and not a persistence one. `48` §2 decides what
///        is written into the file, and none of this is: `76` §2 keeps every row here per application.
/// tag   owning
class ToolSequence
{
public:

    /// 🧩 The declared tools, for presentation and for activation.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    ToolIndex&       Tools();
    const ToolIndex& Tools() const;

    /// 🧩 The application's brush declarations — `58` §7's per-application store.
    /// note  📝 Held here rather than beside `58` because `58` declares what a brush **is** and `76` is the
    ///        declared home for per-application state. `48` §6's rule places both on the application side, so a
    ///        second home would be a second lifetime for one collection.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    BrushIndex&       Brushes();
    const BrushIndex& Brushes() const;

    /// 🧩 Activates one declared tool.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared tool count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareTool(std::uint32_t ToolIndex);

    /// 🧩 Activates one declared brush.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared brush count
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareBrush(std::uint32_t BrushIndex);

    /// 🧩 Declares the active colour.
    /// out   Result  [-]  refuses with ContentUnsupported for a colour declaring no space
    /// note  🔴 `36` §1: no bare triple. A colour without its space is a number three subsystems will each
    ///        interpret differently, and all three will look plausible.
    /// note  ⚠️ `36` §6 rules a colour sampled from the workspace **scene-referred**, sampled before `66`. What
    ///        arrives here is therefore in the working space, and a display-referred sample is a separate action
    ///        that says so.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareColour(const ColourSpecification& Declaring);

    /// 🧩 Declares how the workspace presents the shaded result.
    /// out   Result  [-]  refuses with ContentUnsupported for a mode outside the declared set
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareDisplay(DisplaySubject Declaring);

    /// 🧩 Declares which of `42`'s channels is presented alone at `ChannelIsolated`.
    /// out   Result  [-]  refuses with ContentUnsupported for a channel outside `42`'s twenty
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareChannel(ChannelSubject Declaring);

    /// 🧩 Declares whether one of `80`'s overlays is presented.
    /// out   Result  [-]  refuses with ContentUnsupported outside the declared set
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> DeclareOverlay(OverlaySubject Declaring, bool PresenceEnabled);

    /// 🧩 Takes the pointer at a declared precedence, recording the pick it was taken against.
    /// in    Precedence  [-]  which level is claiming it
    /// in    Opened    [-]  the pick as `74` resolved it at this instant
    /// out   Result   [-]  refuses with HostDenied while a capture already stands
    /// note  🔴 A standing capture is **not** stolen by a stronger claimant. `76` §3 and `14` §4.2: capture
    ///        persists for the whole drag, and re-arbitrating per sample is the defect where a stroke stops the
    ///        moment the cursor crosses a floating panel. Arbitration happens before capture is taken, once.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> OpenCapture(PointerPrecedence Precedence, const ResolvedPointer& Opened);

    /// 🧩 Releases the standing capture.
    /// out   Result  [-]  refuses with HostDenied when no capture stands
    /// note  🔴 Releasing is an explicit event and never a consequence of the pointer moving elsewhere.
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<bool> ReleaseCapture();

    /// 🧩 Which precedence would take the pointer, given what is open and where the pointer is.
    /// in    InterfaceReported  [-]  the interface reports the pointer over itself — `14` §4.2's level 1
    /// in    ManipulatorOpen    [-]  a drag stands in `78`
    /// in    StrokeOpen         [-]  a stroke stands in `22`
    /// out   Precedence           [-]  the standing holder while one stands, the arbitration otherwise
    /// note  📝 Declared here rather than in `14` because `22` and `78` both ask it and neither may read
    ///        `SlateUI`. The interface supplies its own condition as an argument.
    /// cost  ✔️
    /// tag   api, nonallocating, nonthrowing
    PointerPrecedence Arbitrate(bool InterfaceReported, bool ManipulatorOpen, bool StrokeOpen) const;

    const ColourSpecification&  Colour() const;
    const PointerCapture&       Capture() const;

    /// 🧩 The active tool, refusing where none has been activated.
    /// out   Result  [-]  refuses with ContentUnsupported before a tool is declared active
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const ToolSpecification*> ActiveTool() const;

    /// 🧩 The active brush, refusing where none has been activated.
    /// out   Result  [-]  refuses with ContentUnsupported before a brush is declared active
    /// cost  ✔️
    /// tag   api, nonthrowing
    Deliver<const BrushSpecification*> ActiveBrush() const;

    std::uint32_t   ActiveToolIndex() const;
    std::uint32_t   ActiveBrushIndex() const;
    DisplaySubject  Display() const;
    ChannelSubject  IsolatedChannel() const;
    bool            OverlayActive(OverlaySubject Subject) const;

private:

    static constexpr std::size_t OverlaySpan = static_cast<std::size_t>(OverlaySubject::OverlayCount);

    ToolIndex            DeclaredTools     = {};                                 // [-] - the application's tools
    BrushIndex           DeclaredBrushes   = {};                                 // [-] - `58` §7's store
    ColourSpecification  ActiveColour      = {};                                 // [-] - carries its space
    PointerCapture       CurrentCapture   = {};                                 // [-] - who holds the pointer
    DisplaySubject       CurrentDisplay  = DisplaySubject::Shaded;             // [-]
    ChannelSubject       CurrentChannel  = ChannelSubject::AlbedoColour;       // [-] - read at ChannelIsolated
    std::uint32_t        SelectedToolIndex  = AbsentTool;                         // [-] - the active tool
    std::uint32_t        SelectedBrushIndex = AbsentTool;                         // [-] - the active brush
    bool                 OverlayPresent[OverlaySpan] = {};                       // [-] - per `80` overlay
};

}   // namespace Slate
