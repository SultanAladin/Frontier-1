//============================================================================================================================================
//                                                        WORLDSKETCHBOOLEAN.H
//============================================================================================================================================
// 🧩 Two-dimensional booleans over the closed regions of the world sketch -- union, cut and intersect between two
//    shapes, and a cut of one shape by an open curve that crosses it. The result is real world-sketch geometry:
//    new lines and arcs declared into the same structure Trim, Cut and Fill already edit, so a boolean is undone,
//    picked, snapped and filled by exactly the machinery every other operation uses.
//
// 🔴 THE WORLD SKETCH IS THE AUTHORITY, NOT THE COMPATIBILITY PROFILE. `Sketch/ProfileBoolean` works on a
//    `ProfileSpecification`, which is a downstream mirror -- so its results were invisible to picking and could
//    not be trimmed or filled, and it refused every case that actually overlapped. These operate on the same
//    `WorldSketchStructure` the viewport edits, and lean on Clipper2 for the robust planar area work rather than
//    the hand-rolled convex-only clip that unit could manage.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHICH BOOLEAN
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The booleans this unit performs.
/// note  🔴 CUT IS TWO OPERATIONS WEARING ONE NAME, and which one runs is decided by what the second
///        operand IS, not by a separate tool. A closed second shape subtracts its area from the first
///        (a hole, or a bite taken out of the side). An OPEN curve that crosses the first splits it into
///        the pieces on either side. Both are "cut" to the artist -- remove material along this boundary
///        -- so both live under one subject and the geometry chooses the branch.
enum class WorldBooleanManner : std::uint32_t
{
    Union     = 0u,   // [-] - the area covered by either shape, welded into one region
    Cut       = 1u,   // [-] - the first shape with the second removed, or split by an open curve
    Intersect = 2u    // [-] - only the area both shapes cover
};

/// 🧩 What a boolean did, or why it could not.
enum class WorldBooleanVerdict : std::uint32_t
{
    Produced          = 0u,   // [-] - the boolean ran and its result stands in the sketch
    OperandMissing    = 1u,   // [-] - fewer than two operands, or one names nothing
    SubjectNotClosed  = 2u,   // [-] - a shape operand is not a closed region
    CutterNotCrossing = 3u,   // [-] - an open cutting curve does not cross the shape, so it splits nothing
    DifferentPlane    = 4u,   // [-] - the operands do not share one plane, so there is no area to combine
    EmptyResult       = 5u,   // [-] - the boolean is legitimate but leaves nothing (a shape cut away entirely)
    BackendAbsent     = 6u    // [-] - the robust area backend is not built in, so the result cannot be trusted
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS SELECTED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The two things a boolean is asked to combine, named by what the artist selected.
/// note  🔴 A LOOP OR A LONE CURVE, NOT A PROFILE. The artist selects the geometry on screen -- a closed
///        loop the analysis found, or an open curve that has no loop -- so an operand names one of those.
///        A closed loop is a shape; an open curve is only ever a cutter, and only for Cut.
struct WorldBooleanOperand
{
    WorldLoopName  Loop  = {};   // [-] - set when the operand is a closed region
    WorldCurveName Curve = {};   // [-] - set when the operand is a single open curve (a cutter)

    bool NamesLoop()  const { return Loop.Assigned(); }
    bool NamesCurve() const { return Curve.Assigned() && !Loop.Assigned(); }
    bool Declared()   const { return NamesLoop() || NamesCurve(); }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                        THE WORK
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether a boolean would succeed, without changing anything.
/// note  🔴 THE PREVIEW IS THE COMMIT'S OWN ANSWER. The driver asks this every frame to decide whether to
///        arm, and it must refuse for exactly the reasons `PerformWorldBoolean` refuses -- otherwise the
///        tool arms on a pair it then declines, and the artist clicks at nothing.
/// tag   api, nonthrowing
WorldBooleanVerdict EvaluateWorldBoolean(const WorldSketchStructure& Declared,
                                         const WorldBooleanOperand& First,
                                         const WorldBooleanOperand& Second,
                                         WorldBooleanManner Manner);

/// 🧩 Performs a boolean, declaring its result into the sketch and naming the loops produced.
/// in    First    [-] the shape the boolean is built on -- for Cut, the shape that is kept and reduced
/// in    Second   [-] the other shape, or the open curve that cuts the first
/// out   Produced [-] the loops the boolean created; a split leaves two, a cut-to-nothing leaves none
/// note  🔴 KEEP THE OPERANDS. The originals are left standing and the result is added alongside them, so
///        a boolean is non-destructive and its inputs remain to be re-used, re-cut or dimensioned. What
///        changes is only that new region geometry now exists; nothing that named a prior curve moves.
/// note  📝 THE RESULT IS DRAWN WITH LINES AND ARCS, one per edge of each result loop, carrying the
///        operands' support frame. Clipper returns a polygon, so a straight-edged result is exact and a
///        curved one is the polygon that approximated it -- which is the same fidelity the fill preview
///        and the area analysis already work at.
/// cost  🚩🚩🚩
/// tag   api, nonthrowing
Deliver<std::vector<WorldLoopName>> PerformWorldBoolean(WorldSketchStructure& Declared,
                                                        const WorldBooleanOperand& First,
                                                        const WorldBooleanOperand& Second,
                                                        WorldBooleanManner Manner);

/// 🧩 Whether the robust planar-area backend (Clipper2) is compiled in.
/// note  📝 The unit still builds and reports without it; every boolean simply refuses with
///        `BackendAbsent` rather than returning a result the hand geometry could not be trusted to make.
bool WorldBooleanBackendAvailable();

} // namespace Slate
