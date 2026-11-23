//============================================================================================================================================
//                                                        ANNOTATIONINTENT.H
//============================================================================================================================================
// 🧩 What each of the thirteen annotation tiles in the catalogue actually asks for.
//
// 🔴 A UNIT OF ITS OWN, AND DELIBERATELY SO. This mapping is the single place a catalogue tile becomes a
//    dimension or constraint subject, and it must be provable without dragging in the recording surface,
//    the control index and the option palette that the driver above it needs. A rule that can only be
//    tested by standing up the whole interface will not be tested.
//
// 📝 Pure data. No pointer, no camera, no sketch -- a tool subject in, an intent out.

#pragma once

#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"

namespace Slate
{

/// 🧩 What one of the thirteen annotation tiles asks for.
struct AnnotationIntent
{
    bool Standing = false;      // [-] - whether this tool is an annotation tool at all
    bool Constraining = false;  // [-] - a constraint rather than a dimension

    WorldDimensionSubject  Dimension  = WorldDimensionSubject::Aligned;
    WorldConstraintSubject Constraint = WorldConstraintSubject::Coincident;

    /// 🧩 Whether the engine can actually carry this out yet.
    /// note  🔴 SOME TILES ARE HONEST PLACEHOLDERS. Midpoint, Symmetry and Concentric are listed in the
    ///        catalogue but are not among the eight relations the constraint solver supports. Reporting
    ///        that here -- rather than letting them fall through to something that looks similar -- is
    ///        what stops a tile from quietly applying the WRONG constraint.
    bool Supported = false;
};

/// 🧩 What a catalogue tile means, resolved once.
/// tag   api, nonthrowing
AnnotationIntent ResolveAnnotationIntent(ParametricToolSubject Subject);

/// 🧩 Whether a tool is one of the thirteen this arm drives.
inline bool AnnotationToolStanding(ParametricToolSubject Subject)
{
    return ResolveAnnotationIntent(Subject).Standing;
}

} // namespace Slate
