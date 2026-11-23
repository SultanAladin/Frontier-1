//============================================================================================================================================
//                                                         ANNOTATIONDRIVER.H
//============================================================================================================================================
// 🧩 The arm that turns a pointer in a viewport into a dimension or a constraint, and raises the readout
//    for the one that carries a figure.
//
// 🔴 THE TOOL SUBJECT IS TRANSLATED IN EXACTLY ONE PLACE. Fourteen catalogue tiles map onto six dimension
//    subjects and eight constraint subjects, and if that mapping existed at more than one site the two
//    would eventually disagree about what "Tangent" means. Everything below reads `AnnotationIntent`.
//
// 📝 Same shape as `SketchOperationDriver`: the session beneath is pure and takes world points, the
//    pointer arrives in pixels, and casting the ray happens once here.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/MeasureDisplay.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"
#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"
#include "SlateWorkspace/Discipline/AnnotationIntent/Api/AnnotationIntent.h"
#include "SlateWorkspace/Discipline/AnnotationSession/Api/AnnotationSession.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    WHAT A TILE MEANS
//------------------------------------------------------------------------------------------------------------------------

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE ARM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything the annotation tools remember between frames.
struct AnnotationState
{
    AnnotationSession Session = {};

    /// 🧩 The tool this state was prepared for, so changing tool can clear it.
    ParametricToolSubject Prepared = ParametricToolSubject::Select;

    /// 🧩 The unit figures are shown in. Millimetres are what is stored, always.
    MeasureUnit Unit = MeasureUnit::Millimetre;

    /// 🧩 The figure the readout edits, in the DISPLAYED unit.
    /// note  🔴 DISPLAYED, NOT STORED, and that is deliberate. The artist types in whatever unit is on
    ///        screen; the conversion to millimetres happens on the way into the session. Keeping this in
    ///        millimetres would make the readout show 4200 when the panel says metres.
    float Figure = 0.0f;

    /// 🧩 How many constraints the last commit withdrew to make the typed value hold.
    /// note  📝 Zero for an ordinary edit. Non-zero is a SUCCESS that cost something, and the host
    ///        reports it -- the artist needs to know a relation they authored is no longer enforced.
    std::uint32_t RetiredCount = 0u;

    /// 🧩 How many more frames the withdrawal notice should be shown for.
    /// note  🔴 A NOTICE MUST EXPIRE BY ITSELF. `RetiredCount` is only ever written when a commit
    ///        happens, so a host keyed on it alone would draw the message from the first retirement
    ///        until the editor closed -- and a banner that never leaves is one the artist stops seeing,
    ///        which defeats the entire point of reporting the withdrawal.
    std::uint32_t NoticeFramesLeft = 0u;

    /// 🧩 Whether the withdrawal notice should be drawn this frame.
    bool NoticeStanding() const { return RetiredCount > 0u && NoticeFramesLeft > 0u; }

    /// 🧩 Set for one frame when the last commit was refused by the solver.
    /// note  📝 Worth surfacing. A dimension that silently declines to take a value looks like a broken
    ///        text box; one that says the sketch cannot take that value is telling the truth.
    bool Refused = false;
};

/// 🧩 Drives whichever annotation tool is active for one frame, and records its readout.
/// in    Hovered       [-] what the pointer is over, already picked by the caller
/// out   PointerTaken  [-] set when the annotation consumed the contact
/// note  🔴 DOES NOTHING when the active tool is not an annotation tool, so Select, drawing, the gizmo
///        and the seven operations are all unaffected by its presence.
/// cost  🚩🚩
/// tag   api, nonthrowing
void DriveAnnotations(const PlaneExtent& Bounds,
                      const PointerCondition& Pointer,
                      const ResolvedCamera& Camera,
                      ParametricToolSubject ActiveTool,
                      const WorldPlacementFrame& Workplane,
                      const WorldPick& Hovered,
                      WorldDimensionName FigureUnderPointer,
                      WorldSketchStructure& World,
                      AnnotationState& State,
                      ToolContextMenu& Readout,
                      bool& PointerTaken);

} // namespace Slate
