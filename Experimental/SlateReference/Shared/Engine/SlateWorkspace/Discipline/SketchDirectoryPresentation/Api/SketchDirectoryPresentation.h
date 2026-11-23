//============================================================================================================================================
//                                                  SKETCHDIRECTORYPRESENTATION.H
//============================================================================================================================================
// 🧩 Turning the parametric document into what the sketch directory panel shows: the row list, the
//    inspector, the revision history and which row is picked.
//
// 📝 The last behaviour to leave `ParametricSketchHost`. Four functions that were only ever called from
//    `main()` and could only be exercised by launching the application and clicking.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Record/WorkspaceDirectoryProjection/Api/WorkspaceDirectoryProjection.h"
#include "SlateShape/Record/WorkspacePropertyProjection/Api/WorkspacePropertyProjection.h"
#include "SlateShape/Record/WorkspaceRevisionSequence/Api/WorkspaceRevisionSequence.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"
#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h"
#include "SlateWorkspace/Discipline/ToolAvailability/Api/ToolAvailability.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                          WHAT THE PANEL IS GIVEN TO SHOW
//------------------------------------------------------------------------------------------------------------------------
// 🔴 THESE TEN WERE `Application/Api/ParametricWorkspaceBridge.h` — 347 `inline` lines whose own header
//    comment said "the bridge lives in Application on purpose: the host is the only layer allowed to see
//    both the exact backend and the UI guarantee at once."
//
//    That premise stopped being true the moment this unit existed. `SlateWorkspace` sees `SlateShape`'s
//    records AND `SlateUI`'s presentation, and turning one into the other is precisely its job. Left where
//    it was, the unit had to reach UP into `Application/` to compile — a unit depending on an application,
//    which is the dependency arrow pointing the wrong way.
//
// 📝 "Bridge" named the file, not the behaviour, so the names change with the address: what these do is
//    BUILD A PRESENTATION from records.

struct SketchDirectoryPresentation
{
    struct DirectoryText
    {
        std::string Naming = {};
        std::string Tagged = {};
    };

    struct RevisionText
    {
        std::string Description = {};
        std::string Operation = {};
        std::string Affected = {};
        std::string SealedAt = {};
    };

    std::vector<ParametricDirectoryRow> DirectoryRows = {};
    std::vector<DirectoryText> DirectoryBacking = {};

    ParametricPropertyPresentation Property = {};
    std::string PropertyNaming = {};
    std::string PropertySecondary = {};
    std::array<std::string, ParametricPropertyPresentation::FieldLimit> PropertyCaptions = {};
    std::array<std::string, ParametricPropertyPresentation::FieldLimit> PropertyValues = {};
    std::array<std::string, ParametricPropertyPresentation::FieldLimit> PropertyTrails = {};

    std::vector<ParametricRevisionRow> RevisionRows = {};
    std::vector<RevisionText> RevisionBacking = {};

    void Reclaim()
    {
        DirectoryRows.clear();
        DirectoryBacking.clear();
        Property = ParametricPropertyPresentation{};
        PropertyNaming.clear();
        PropertySecondary.clear();
        for (std::string& Run : PropertyCaptions) Run.clear();
        for (std::string& Run : PropertyValues)   Run.clear();
        for (std::string& Run : PropertyTrails)   Run.clear();
        RevisionRows.clear();
        RevisionBacking.clear();
    }
};

ParametricCategory PresentedCategory(WorkspaceCategory Category);

ParametricRowSubject PresentedRowSubject(const WorkspaceDirectoryRow& Source);

const char* AffirmationText(bool Reading);

std::string IdentityRun(const char* Stem, std::uint32_t Index);

std::string RecordDisplayName(const WorkspaceRecordStructure& Records, WorkspaceRecordName Subject);

std::string AffectedRecordsRun(const WorkspaceRecordStructure& Records,
                                     const std::vector<WorkspaceRecordName>& Affected);

std::string SubjectReferenceText(const WorkspaceRecord& Subject);

Deliver<bool> BuildDirectoryPresentation(const WorkspaceDirectoryProjection& Source,
                                               SketchDirectoryPresentation& Delivered);

Deliver<bool> BuildInspectorPresentation(const WorkspaceRecordStructure& Records,
                                               WorkspaceRecordName SubjectName,
                                               const WorkspacePropertyProjection& Source,
                                               const WorkspaceRevisionSequence& Revisions,
                                               SketchDirectoryPresentation& Delivered);

/// 🧩 Empties the inspector, so a stale property panel cannot outlive the record it described.
void ClearInspectorPresentation(SketchDirectoryPresentation& Bridge);

/// 🧩 What a brand-new parametric workspace starts with, which is nothing.
/// note 📝 Deliberately empty: `ResolveSketchBasis` supplies the ground plane until the artist makes a
///       real record, so seeding geometry here would put something in the document nobody asked for.
void SeedParametricWorkspace(WorkspaceNameIndex& Naming,
                             SketchStructure& Sketch,
                             WorkspaceRecordStructure& Records,
                             WorkspaceRevisionSequence& Revisions);

/// 🧩 Seats the directory's rows into the panel context, and clears the rows beyond them.
/// note 🔴 THE ROWS PAST THE END MUST BE CLEARED, not left. `RowSelected[RowLimit] = { true }` initialises
///       only element zero, and a shrinking directory otherwise leaves the previous run's ticks behind on
///       rows that no longer name anything.
void SeatParametricContext(const WorkspaceDirectoryProjection& Directory,
                           ParametricWorkspaceContext& Applied,
                           bool& Seeded);

struct SketchSelectionSet;

/// 🧩 Brings the whole sketch directory presentation up to date after the records change.
/// note 📝 Directory, inspector, revision list and selection in one call, because they are only ever
///       correct together — a directory rebuilt without reseating the context shows rows that select the
///       wrong record.
Deliver<bool> SynchroniseParametricPresentation(const WorkspaceRecordStructure& Records,
                                                const WorkspaceRevisionSequence& Revisions,
                                                WorkspaceDirectoryProjection& Directory,
                                                SketchDirectoryPresentation& Bridge,
                                                ParametricWorkspaceContext& Applied,
                                                WorkspaceRecordName& PendingSelection,
                                                bool& Seeded,
                                                const SketchSelectionSet* SelectionSet = nullptr);

}   // namespace Slate
