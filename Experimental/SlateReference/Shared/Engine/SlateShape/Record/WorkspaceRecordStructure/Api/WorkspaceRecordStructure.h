//============================================================================================================================================
//                                                     WORKSPACERECORDSTRUCTURE.H
//============================================================================================================================================
// 🧩 Committed semantic records for the parametric workspace outliner. Preview clicks stay in the tool state;
//    only committed semantic objects enter this structure.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/WorkspaceShapeFamily.h"
#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Reference/ReferenceSpecification/Api/ReferenceSpecification.h"
#include "SlateShape/Sketch/ConstraintSpecification/Api/ConstraintSpecification.h"
#include "SlateShape/Sketch/DimensionSpecification/Api/DimensionSpecification.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Slate
{

struct WorkspaceRecordName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class WorkspaceCategory : std::uint32_t
{
    Sketch = 0u,
    Geometry = 1u,
    Annotation = 2u,
    Operation = 3u,
    Folder = 4u,
    CategoryCount = 5u
};

enum class WorkspaceRecordSubject : std::uint32_t
{
    Point = 0u,
    OpenCurve = 1u,
    ClosedProfile = 2u,
    ThinSurface = 3u,
    Solid = 4u,
    Dimension = 5u,
    Constraint = 6u,
    Pattern = 7u,
    Mirror = 8u,
    Folder = 9u,
    SubjectCount = 10u
};

WorkspaceCategory CategoryOfRecord(WorkspaceRecordSubject Subject);

struct WorkspaceRecord;
WorkspaceCategory PresentedCategoryOfRecord(const WorkspaceRecord& Subject);

struct WorkspaceRecord
{
    WorkspaceRecordSubject Subject = WorkspaceRecordSubject::Point;
    WorkspaceShapeFamily Family = WorkspaceShapeFamily::Unknown;
    WorkspaceRecordName ParentFolder = {};
    WorkspaceCategory FolderCategory = WorkspaceCategory::Sketch;
    std::string Naming = {};
    bool AutoNamed = true;
    bool Visible = true;
    bool Locked = false;
    bool ClosedSemantic = false;
    bool CappedExtrusionSemantic = false;
    bool ConstructionSemantic = false;
    SketchPointName SketchPoint = {};
    SketchCurveName SketchCurve = {};
    ProfileNameInFeature Profile = {};
    SolidNameInFeature Solid = {};
    DimensionName Dimension = {};
    ConstraintName Constraint = {};
    FeatureName Feature = {};

    bool Declared() const;
};

struct WorkspacePresentedRow
{
    WorkspaceRecordName Record = {};
    WorkspaceRecordSubject Subject = WorkspaceRecordSubject::Point;
    WorkspaceCategory Category = WorkspaceCategory::Sketch;
    WorkspaceRecordName ParentFolder = {};
    const char* Naming = "";
    std::uint32_t Depth = 0u;
    bool Visible = true;
    bool Locked = false;
    bool ClosedSemantic = false;
    bool CappedExtrusionSemantic = false;
    bool ConstructionSemantic = false;
};

class WorkspaceRecordStructure
{
public:
    WorkspaceRecordName Declare(WorkspaceRecord Incoming);
    Deliver<bool> Promote(WorkspaceRecordName Subject, WorkspaceRecordSubject TargetSubject);
    Deliver<bool> Rename(WorkspaceRecordName Subject, const std::string& Naming);
    Deliver<bool> SetFolderCategory(WorkspaceRecordName Subject, WorkspaceCategory Category);
    Deliver<bool> MoveToFolder(WorkspaceRecordName Subject, WorkspaceRecordName Folder);
    Deliver<bool> ToggleVisible(WorkspaceRecordName Subject, bool Visible);
    Deliver<bool> ToggleLocked(WorkspaceRecordName Subject, bool Locked);
    const WorkspaceRecord* Resolve(WorkspaceRecordName Subject) const;
    WorkspaceRecord* Resolve(WorkspaceRecordName Subject);
    void ResolvePresented(std::vector<WorkspacePresentedRow>& Presented) const;
    std::uint32_t DeclaredCount() const { return static_cast<std::uint32_t>(HeldRecords.size()); }
    void Reclaim();

private:
    std::uint32_t ResolveDepth(WorkspaceRecordName Subject) const;

    std::vector<WorkspaceRecord> HeldRecords = {};
};

} // namespace Slate
