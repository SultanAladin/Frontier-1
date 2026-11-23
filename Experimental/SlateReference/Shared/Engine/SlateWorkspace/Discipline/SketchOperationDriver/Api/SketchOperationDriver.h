//============================================================================================================================================
//                                                      SKETCHOPERATIONDRIVER.H
//============================================================================================================================================
// 🧩 The one arm that turns a pointer in a viewport into the seven 2D operations — Fillet, Chamfer, Cut,
//    Trim, Extend, Offset and Fill — and raises the readout for the two that carry a figure.
//
// 🔴 IT EXISTS BECAUSE THE HOST MUST NOT KNOW HOW AN OPERATION WORKS. `EditorHost.cpp` already carries the
//    whole editor; adding seven tool branches to it would put the geometry of a fillet in the same
//    function as window layout, where none of it can be proven. The host hands over a camera, an extent
//    and a pointer, and gets back whether the contact was consumed.
//
// 🔴 THE SCREEN-TO-WORLD STEP IS THE WHOLE JOB OF THIS SEAM. The sessions beneath are pure and take world
//    points; the pointer arrives in pixels. Casting the ray and meeting the workplane happens ONCE, here,
//    so every operation agrees about where the artist is pointing.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/MeasureDisplay.h"
#include "Shared/WorkspaceCadPacket.slang.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/ToolContextMenu/Api/ToolContextMenu.h"
#include "SlateWorkspace/Discipline/CornerDragSession/Api/CornerDragSession.h"
#include "SlateWorkspace/Discipline/SketchOperationSession/Api/SketchOperationSession.h"
#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"
#include "SlateWorkspace/Discipline/WorkplaneStanding/Api/WorkplaneStanding.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       WHAT IT KEEPS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Everything the seven operations remember between frames.
/// note  📝 One struct rather than seven statics in the host. The tools are mutually exclusive -- an
///        artist is filleting or trimming, never both -- so switching tool need only reset this.
struct SketchOperationState
{
    CornerDragSession      Corner    = {};   // [-] - Fillet and Chamfer
    SketchOperationSession Operation = {};   // [-] - Cut, Trim, Extend, Offset, Fill

    /// 🧩 The curves an Offset will copy, gathered from the standing selection.
    std::vector<WorldCurveName> Chain = {};

    /// 🧩 The tool the state was last prepared for, so a change of tool can clear it.
    ParametricToolSubject Prepared = ParametricToolSubject::Select;

    /// 🧩 The unit the readout shows figures in. Millimetres are what is stored, always.
    /// note  🔴 THE SAME ARRANGEMENT THE ANNOTATION BAND ALREADY USES, and for the same reason: the model
    ///        has exactly one unit and the artist has whichever one they are working in. An artist
    ///        working in metres was given a millimetre slider -- a 50 mm range on a part half a metre
    ///        across, so the control was useless long before the number was wrong.
    MeasureUnit Unit = MeasureUnit::Millimetre;

    /// 🧩 The figure the readout edits, shared by the drag and the typed value, in the DISPLAYED unit.
    /// note  🔴 A FLOAT BECAUSE THE OPTION ROWS TAKE A FLOAT, and the session keeps a double. The two are
    ///        synchronised at the seam rather than at either end, so neither has to know about the other.
    /// note  🔴 DISPLAYED, NOT STORED. The sessions beneath work in millimetres and never learn that any
    ///        other unit exists; the conversion happens once in each direction, at the readout.
    float Figure = 4.0f;

    /// 🧩 The readout's heading, which carries the live figure now that no row states it.
    /// note  🔴 HELD HERE BECAUSE THE POPUP BORROWS IT. `PopupDeclaration::Title` is a pointer read after
    ///        the driver returns, so the text must outlive the call -- a local buffer would dangle.
    char Heading[64] = {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE ARM
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The world placement frame a workplane stands for.
/// note  📝 A workplane and a placement frame are the same three facts under two names -- an origin, a
///        normal and an along direction. The conversion existed as a file-local helper in the drawing
///        arm, which is why the operations could not reach it; it is declared once here instead.
inline WorldPlacementFrame ResolveWorkplacementFrame(const Workplane& Standing)
{
    const SpatialBasis Basis = ResolveWorkplaneBasis(Standing);
    return { Basis.Origin, Basis.Normal, Basis.Along };
}

/// 🧩 Whether a tool is one of the seven this arm drives.
constexpr bool OperationToolStanding(ParametricToolSubject Subject)
{
    return Subject == ParametricToolSubject::Fillet  ||
           Subject == ParametricToolSubject::Chamfer ||
           Subject == ParametricToolSubject::Cut     ||
           Subject == ParametricToolSubject::Trim    ||
           Subject == ParametricToolSubject::Extend  ||
           Subject == ParametricToolSubject::Offset  ||
           Subject == ParametricToolSubject::FillFace;
}

/// 🧩 Drives whichever of the seven is active for one frame, and records its readout.
/// in    Bounds        [px] the viewport leaf, which bounds the readout
/// in    Selection     [-]  the standing world selection, which an Offset copies
/// out   PointerTaken  [-]  set when the operation consumed the contact
/// note  🔴 DOES NOTHING AND CONSUMES NOTHING when the active tool is not one of the seven, so Select,
///        drawing and the gizmo are unaffected by its presence.
/// note  📝 The readout is recorded from here rather than by the host because only this arm knows whether
///        a figure is standing, and a readout drawn from stale state is worse than none.
/// cost  🚩🚩
/// tag   api, nonthrowing
void DriveSketchOperations(const PlaneExtent& Bounds,
                           const PointerCondition& Pointer,
                           const ResolvedCamera& Camera,
                           ParametricToolSubject ActiveTool,
                           const WorldPlacementFrame& Workplane,
                           const std::vector<WorldCurveName>& Selection,
                           WorldSketchStructure& World,
                           SketchOperationState& State,
                           ToolContextMenu& Readout,
                           bool& PointerTaken);

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IT SHOWS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 How the operation previews are drawn.
/// note  📝 Amber for a shape that will be ADDED, red for geometry that will be REMOVED. The pair is the
///        one convention the artist has to learn, and it holds across all four tools.
struct OperationPreviewStyle
{
    Unsigned32 AddingColour   = PackWorkspaceCadColour(251u, 191u, 36u, 255u);   // [-] - amber: appears
    Unsigned32 RemovingColour = PackWorkspaceCadColour(248u, 113u, 113u, 255u);  // [-] - red: goes away
    Unsigned32 MarkerColour   = PackWorkspaceCadColour(251u, 191u, 36u, 255u);
    Real32     Thickness      = 2.2f;    // [px] - heavier than a curve, so it reads as an overlay
    Real32     MarkerRadius   = 4.5f;    // [px]
    Unsigned32 ArcSteps       = 24u;     // [-] - enough that a filleted corner reads as round
};

/// 🧩 Draws what the standing operation would do, into the same packet the sketch is drawn in.
/// in    PhysicalExtent  [px] the leaf in FRAMEBUFFER pixels, as the curves were projected into
/// out   Delivered       [-]  appended to; never cleared, so it layers over the sketch
/// out   Result          [-]  whether anything was appended
///
/// 🔴 THIS IS THE HALF THAT WAS MISSING. Every operation resolved its target, previewed its verdict and
///    committed correctly, and NONE of it was ever drawn -- so a fillet looked like a sharp corner until
///    the moment it was applied, and Trim and Cut asked the artist to click without saying what would be
///    destroyed. The geometry was never the defect; the silence was.
///
/// 📝 Reads the sessions and draws; it decides nothing. Whatever the gesture resolved is what appears,
///    which is why the preview cannot contradict the commit.
/// cost  🚩🚩
/// tag   api, nonthrowing
Deliver<bool> ProjectOperationPreview(const SketchOperationState& State,
                                      ParametricToolSubject ActiveTool,
                                      const ResolvedCamera& Camera,
                                      const PlaneExtent& PhysicalExtent,
                                      WorkspaceCadPacket& Delivered,
                                      const OperationPreviewStyle& Style = {});

} // namespace Slate
