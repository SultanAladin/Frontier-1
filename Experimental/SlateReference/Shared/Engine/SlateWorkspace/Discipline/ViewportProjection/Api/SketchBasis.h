//============================================================================================================================================
//                                                           SKETCHBASIS.H
//============================================================================================================================================
// 🧩 The one bridge from a sketch to the plane it is drawn on.
//
// 🔴 Separate from `ViewportProjection.h` on purpose. This is the only declaration in the unit that names
//    a `SketchStructure`, and putting it in the main header would make everything that projects a point
//    depend on the whole sketch kernel to link. The projection needs a basis; it does not care that a
//    sketch is where this one came from.

#pragma once

#include "SlateWorkspace/Discipline/ViewportProjection/Api/ViewportProjection.h"

#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

namespace Slate
{

/// 🧩 The basis a sketch is drawn on.
/// note ⚠️ An undeclared sketch resolves to the world axes rather than refusing, so a viewport opened
///       before any sketch exists still has somewhere to draw.
/// note 🔴 `Across` is recomputed as the cross of the normal and the along direction rather than trusted
///       from the stored plane. A plane whose directions have drifted out of square would otherwise skew
///       every coordinate measured on it.
SpatialBasis ResolveSketchBasis(const SketchStructure& Sketch);

}   // namespace Slate
