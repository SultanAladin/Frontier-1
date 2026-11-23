//============================================================================================================================================
//                                                 WORKSPACEDIRECTORYPROJECTION.H
//============================================================================================================================================
// 🧩 Backend projection from committed CAD workspace records into a SceneDirectory-style linear tree. The
//    rows stay CAD-native: the adapter carries folder/category/record semantics without taking a SlateUI
//    dependency or leaking scene subjects into SlateShape.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class WorkspaceDirectoryRowRole : std::uint32_t
{
    CategoryRoot = 0u,
    Record = 1u
};

struct WorkspaceDirectoryRow
{
    WorkspaceDirectoryRowRole Role = WorkspaceDirectoryRowRole::Record;
    WorkspaceCategory Category = WorkspaceCategory::Sketch;
    WorkspaceRecordName Record = {};
    WorkspaceRecordSubject Subject = WorkspaceRecordSubject::Folder;
    WorkspaceShapeFamily Family = WorkspaceShapeFamily::Unknown;
    std::uint32_t Depth = 0u;
    std::uint32_t Enclosing = 0xFFFFFFFFu;
    std::uint32_t EnclosedCount = 0u;
    std::uint64_t StableIdentity = 0u;
    const char* Naming = "";
    const char* Tagged = "";
    bool Visible = true;
    bool Locked = false;
    bool ClosedSemantic = false;
    bool AutoNamed = false;
};

struct WorkspaceDirectoryProjection
{
    std::vector<WorkspaceDirectoryRow> Rows = {};

    void Reclaim();
};

const char* WorkspaceCategoryText(WorkspaceCategory Category);
const char* WorkspaceRecordSubjectText(WorkspaceRecordSubject Subject);
const char* WorkspaceShapeFamilyText(WorkspaceShapeFamily Family);
const char* WorkspaceDirectoryCategoryTags(WorkspaceCategory Category);
const char* WorkspaceDirectorySubjectTags(WorkspaceRecordSubject Subject, bool ClosedSemantic);

void ProjectWorkspaceDirectory(const WorkspaceRecordStructure& Records,
                               WorkspaceDirectoryProjection& Presented);

Deliver<std::uint32_t> ResolveWorkspaceDirectoryRow(const WorkspaceDirectoryProjection& Presented,
                                                    WorkspaceRecordName Subject);

} // namespace Slate
