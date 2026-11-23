//============================================================================================================================================
//                                                        TRANSFORMSEQUENCE.H
//============================================================================================================================================
// 🧩 The keyboard grammar of a direct-manipulation transform: G/R/S start a move, rotate or scale, X and Z
//    constrain it to an axis, digits type an exact amount, backspace retracts the last thing given, and a
//    second G within the tap window slides along the curve instead of across the plane.
//
// 🔴 This is a GRAMMAR, not a transform. Nothing here moves a point, reads a sketch or touches a device —
//    it turns keystrokes into a standing intent, and reports what that intent currently reads as. The
//    arithmetic that applies the intent stays with the geometry that owns it.
//
// 📝 Lifted verbatim out of `ParametricSketchHost`, where it was nine file-local functions interleaved with
//    the viewport drawing code. It is here because the grammar is a property of the sketching discipline,
//    not of the executable that happens to host it, and because it is testable in isolation — which it was
//    not, while it lived beside a Vulkan surface.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"
#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A TRANSFORM IS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which of the three manipulations is standing.
enum class TransformManner : std::uint32_t
{
    Move   = 0u,
    Rotate = 1u,
    Scale  = 2u
};

/// 🧩 What the manipulation is restricted to.
/// note 📝 `Curve` is reached by tapping G twice rather than by a letter, because it is a manner of moving
///       rather than an axis to move along — the placement slides through the curve's own parameter.
/// 🧩 What a manipulation is confined to.
/// note  🔴 THE AXIS LETTERS ARE PLANE-RELATIVE, NOT WORLD-RELATIVE. `AxisX` is the sketch plane's Along
///       direction and `AxisZ` its Across; on the ground plane, which is where a sketch starts, those ARE
///       world X and Z, which is what makes `G Z 10` read the way the artist expects. On a plane the
///       artist has re-seated they follow the plane, because a sketch that ignored its own plane would
///       move geometry off the surface it was drawn on.
/// note  📝 `AxisY` is the plane's NORMAL. A planar sketch cannot move a point along it — the whole
///       structure is (Along, Across) pairs — so it is offered for rotation only, where it is not merely
///       supported but the only rotation a plane HAS. `R Y 35` therefore names its axis honestly rather
///       than having the letter silently dropped and the rotation happen anyway.
enum class TransformRestriction : std::uint32_t
{
    Free   = 0u,
    AxisX  = 1u,
    AxisZ  = 2u,
    Screen = 3u,
    Curve  = 4u,
    AxisY  = 5u   // [-] - the plane normal; rotation only
};

/// 🧩 The most a typed amount may run to, including its terminator.
constexpr std::size_t TransformNumericLimit = 32u;

//------------------------------------------------------------------------------------------------------------------------
//                                                   WHAT THE KEYS ASKED FOR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One frame of keystrokes, read as intent.
/// note ⚠️ `MoveTapCount` counts every G in the frame, not only the ones that started something. The
///       double-tap that means "slide along the curve" is recognised from this count together with the
///       elapsed time since the previous G, which is why the count survives rather than collapsing to a
///       bool.
struct TransformCommandIntake
{
    std::uint32_t        MoveTapCount        = 0u;                            // [-] - every G seen this frame
    bool                 StartRequested      = false;                         // [-] - a manipulation should begin
    TransformManner      StartManner         = TransformManner::Move;         // [-] - which one
    bool                 RestrictionRequested = false;                        // [-] - an axis letter was pressed
    TransformRestriction Restriction         = TransformRestriction::Free;    // [-] - which axis
    char                 NumericAppend[TransformNumericLimit] = {};           // [-] - digits typed this frame
};

/// 🧩 Everything a standing manipulation remembers between frames.
struct TransformStanding
{
    TransformManner      Manner      = TransformManner::Move;
    TransformRestriction Restriction = TransformRestriction::Free;
    bool                 Engaged     = false;
    bool                 SlideAlongCurve = false;
    char                 Numeric[TransformNumericLimit] = {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                      READING THE KEYS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 True for a character that may appear in a typed amount.
/// note 📝 The minus sign is accepted anywhere in the run, not only first. That is deliberate and matches
///       what shipped: `strtod` stops at the first character it cannot read, so a stray minus truncates
///       the amount rather than rejecting the keystroke.
constexpr bool NumericCharacter(char Character)
{
    return (Character >= '0' && Character <= '9') || Character == '.' || Character == '-';
}

/// 🧩 Appends the numeric characters of `Source` onto whatever `Target` already holds.
/// in    Target    [-]  a terminated run, extended in place
/// in    Capacity  [-]  the whole extent of `Target`, terminator included
/// note ⚠️ Non-numeric characters in `Source` are dropped, not refused. The caller hands over a whole
///       frame of text input and expects only the digits to land.
void AppendTransformNumericRun(char* Target, std::size_t Capacity, const char* Source);

/// 🧩 Reads one frame of typed characters into a standing intent.
/// in    Intake      [-]  the characters typed this frame
/// in    IntakeCount [-]  how many of them
/// in    Engaged     [-]  whether a manipulation is already standing
/// in    Current     [-]  the manner it is standing in, if so
/// out   Resolved    [-]  what the frame asked for
/// note 🔴 A manner may only be STARTED when nothing is standing, and an axis may only be requested once a
///       manner is known — either because one was already engaged or because this same frame started one.
///       That ordering is what makes `G X 5` mean "move 5 along X" and `X` alone mean nothing at all.
/// note ⚠️ Rotation takes no axis. A rotation in a sketch plane has one axis available, so X and Z are
///       ignored while rotating rather than silently restricting it.
TransformCommandIntake ResolveTransformCommand(const char* Intake,
                                               std::uint32_t IntakeCount,
                                               bool Engaged,
                                               TransformManner Current);

/// 🧩 Undoes the last thing the artist gave, one step at a time.
/// note 🔴 The order is deliberate: the typed amount goes first, then the axis restriction. Backspace
///       therefore walks back out of `G X 5` as `G X`, then `G` — it never abandons the manipulation
///       itself, which is what the escape key is for.
/// note 📝 A `Screen` restriction is not retracted, because it is not something the artist asked for.
void RetractTransformCommand(TransformStanding& Standing);

/// 🧩 Forgets any typed amount, leaving the manipulation standing.
void ClearTransformNumeric(TransformStanding& Standing);

/// 🧩 The typed amount, if one reads as a number.
/// out   Value     [-]  written only when the amount reads
/// out   Standing  [-]  false when nothing was typed, or when what was typed is not a number
bool ResolveNumericOverride(const TransformStanding& Standing, double& Value);

/// 🧩 Whether a second G asked to slide along the curve rather than across the plane.
/// in    MoveTapCount     [-]  every G seen this frame
/// in    Elapsed          [-]  milliseconds now
/// in    LastTapped       [-]  milliseconds at the previous G
/// in    CurveAvailable   [-]  whether the selection actually lies on a curve
/// note ⚠️ Two G in ONE frame counts, and so does one G within 350 ms of the last. A frame can carry both
///       taps when the display is slow, and dropping that case would make the gesture fail exactly when
///       the machine is struggling.
bool ResolveSlideRequested(std::uint32_t MoveTapCount,
                           double Elapsed,
                           double LastTapped,
                           bool CurveAvailable);

//------------------------------------------------------------------------------------------------------------------------
//                                                   SAYING WHAT IS STANDING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The word for a manner, for a readout.
const char* TransformMannerText(TransformManner Manner);

/// 🧩 The word for a restriction, for a readout.
const char* TransformRestrictionText(TransformRestriction Restriction);

/// 🧩 The single letter that starts a manner, as the artist typed it.
const char* TransformCommandToken(TransformManner Manner);

/// 🧩 Writes the standing command the way the artist typed it — `G`, `G X`, `G X 5`, `G G 5`.
/// in    Delivered  [-]  written and terminated; emptied first
/// in    Capacity   [-]  its whole extent
/// note 📝 Only an axis restriction is shown. `Free` and `Screen` are the absence of a restriction and
///       spelling them out would make the readout longer without saying more.
void FormatTransformCommand(const TransformStanding& Standing, char* Delivered, std::size_t Capacity);

}   // namespace Slate
