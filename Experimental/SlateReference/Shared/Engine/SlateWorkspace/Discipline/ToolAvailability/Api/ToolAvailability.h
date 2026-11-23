//============================================================================================================================================
//                                                        TOOLAVAILABILITY.H
//============================================================================================================================================
// 🧩 What the artist has selected, and therefore which tools may be offered. A closed profile can be
//    extruded and a loose point cannot; a solid can carry a material and a dimension cannot.
//
// 🔴 THE TABLE BELOW WAS A NINE-ARM SWITCH IN THE HOST, ASSIGNING TWELVE FLAGS BY HAND IN EACH ARM. Every
//    arm re-stated which of the twelve it wanted, so an arm that forgot one inherited whatever the reset
//    block above had left — and the reset block and the arms had to agree, in two places, forever. Stating
//    it once per subject as data means a new record subject cannot be added without answering every flag.
//
// 📝 This is a workspace question, not a toolset one. `SketchToolset` knows how to place a rectangle; it
//    does not know that the artist has a solid selected and may therefore be offered a shell operation.

#pragma once

#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"
#include "SlateShape/Record/WorkspaceDirectoryProjection/Api/WorkspaceDirectoryProjection.h"
#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h"
#include "SlateWorkspace/Discipline/SketchPicking/Api/SketchPicking.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                              WHAT ONE RECORD MAKES POSSIBLE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What holding one record selected makes available.
/// note 🔴 Every field is stated for every subject in the table, including the falses. A row that left a
///       field out would inherit it from whatever ran last, which is exactly the coupling that made the
///       host's reset block and its switch arms depend on each other.
struct ToolAvailability
{
    ParametricToolDimension Dimension            = ParametricToolDimension::Nothing;   // [-]
    std::uint32_t           ProfileCount         = 0u;                                 // [-]
    std::uint32_t           PerimeterEdgeCount   = 0u;                                 // [-]
    std::uint32_t           SolidCount           = 0u;                                 // [-]
    bool                    Axis                 = false;                              // [-] - revolve about it
    bool                    Path                 = false;                              // [-] - sweep along it
    bool                    SupportsMaterial     = false;                              // [-]
    bool                    TangentEndpoint      = false;                              // [-]
    bool                    Opening              = false;                              // [-] - a shell may be opened
    bool                    Measurable           = false;                              // [-] - a dimension may be taken
    bool                    ClosedProfile        = false;                              // [-]
    bool                    ReferencePlane       = false;                              // [-] - a plane may be taken from it
};

/// 🧩 What the given record subject makes available.
/// note 📝 Total over the enumeration, so adding a record subject is a compile error here until its row is
///       written — which is the point of holding it as a table rather than as a switch with a default.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
ToolAvailability AvailabilityFor(WorkspaceRecordSubject Subject);

//------------------------------------------------------------------------------------------------------------------------
//                                            WHAT THE DIRECTORY SAYS IS SELECTED
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The record the directory's taken row names, or nothing when the row is a folder or out of range.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
WorkspaceRecordName SelectedRecordIn(const WorkspaceDirectoryProjection& Directory,
                                     const ParametricWorkspaceContext& Applied);

/// 🧩 All records ticked in the directory's multi-selection context.
void SelectedRecordsIn(const WorkspaceDirectoryProjection& Directory,
                       const ParametricWorkspaceContext& Applied,
                       std::vector<WorkspaceRecordName>& Selected);

/// 🧩 Whether any row at all is ticked.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
bool AnyRowSelected(const ParametricWorkspaceContext& Applied, std::uint32_t RowCount);

/// 🧩 Which row a freshly seated directory should start on.
/// note 📝 The first row that is a record and NOT a folder, so a workspace whose first entry is a category
///       folder opens on something the artist can actually act upon. A directory of nothing but folders
///       falls back to the first record, and an empty one to zero.
/// cost ✔️
/// tag  api, nonallocating, nonthrowing
std::uint32_t InitialRowIn(const WorkspaceDirectoryProjection& Directory);

/// 🧩 What the artist is editing: the viewport's own pick when it still agrees with the directory,
///    otherwise whatever the directory's selected record resolves to.
/// note 🔴 The viewport pick WINS while it agrees, because it is finer — it names a point or a control
///       inside a curve, and the directory can only name the curve. Falling back to the record would
///       silently widen the selection from an endpoint to the whole curve the moment anything refreshed.
/// cost 🚩
/// tag  api, allocating
SketchPick EditableSelection(const SketchStructure& Sketch,
                             const WorkspaceRecordStructure& Records,
                             WorkspaceRecordName SelectedRecord,
                             WorkspaceRecordName PendingSelection,
                             const SketchPick& SemanticSelection);

//------------------------------------------------------------------------------------------------------------------------
//                                              THE WHOLE TOOL CONTEXT AT ONCE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Fills in everything the tool panel needs to know about the current selection.
/// note ⚠️ Writes every field it owns on every call, including back to its resting value. A caller must
///       not have to reset the context first, and one that did would be relying on the order of two
///       statements that look independent.
/// cost 🚩
/// tag  api, allocating
void ResolveToolContext(const WorkspaceDirectoryProjection& Directory,
                        const WorkspaceRecordStructure& Records,
                        const SketchStructure& Sketch,
                        const ParametricWorkspaceContext& WorkspaceApplied,
                        ParametricToolsContext& ToolsApplied);

}   // namespace Slate
