//============================================================================================================================================
//                                                WORKSPACEDIRECTORYPROJECTION.CPP
//============================================================================================================================================

#include "SlateShape/Record/WorkspaceDirectoryProjection/Api/WorkspaceDirectoryProjection.h"

namespace Slate
{

namespace
{

constexpr std::uint32_t NoDirectoryParent = 0xFFFFFFFFu;
constexpr std::uint64_t CategoryIdentityBit = 0x8000000000000000ull;

constexpr bool HasCategoryRoot(WorkspaceCategory Category)
{
    return Category == WorkspaceCategory::Sketch
        || Category == WorkspaceCategory::Geometry
        || Category == WorkspaceCategory::Annotation
        || Category == WorkspaceCategory::Operation;
}

constexpr std::uint64_t CategoryIdentity(WorkspaceCategory Category)
{
    return CategoryIdentityBit | static_cast<std::uint64_t>(static_cast<std::uint32_t>(Category) + 1u);
}

const char* CategoryRootTags(WorkspaceCategory Category)
{
    switch (Category)
    {
        case WorkspaceCategory::Sketch:     return "cad sketch directory root";
        case WorkspaceCategory::Geometry:   return "cad geometry directory root";
        case WorkspaceCategory::Annotation: return "cad annotation directory root";
        case WorkspaceCategory::Operation:  return "cad operation directory root";
        case WorkspaceCategory::Folder:
        case WorkspaceCategory::CategoryCount:
            return "cad directory root";
    }
    return "cad directory root";
}

class WorkspaceDirectoryBuilder
{
public:
    WorkspaceDirectoryBuilder(const WorkspaceRecordStructure& Incoming,
                              WorkspaceDirectoryProjection& Outgoing)
        : Records(Incoming), Presented(Outgoing)
    {
        const std::uint32_t Count = Records.DeclaredCount();
        RootBuckets.resize(static_cast<std::size_t>(WorkspaceCategory::CategoryCount));
        ChildBuckets.resize(static_cast<std::size_t>(Count) + 1u);
        Recorded.assign(static_cast<std::size_t>(Count) + 1u, false);
    }

    void Bucket()
    {
        for (std::uint32_t RecordIndex = 1u; RecordIndex <= Records.DeclaredCount(); ++RecordIndex)
        {
            const WorkspaceRecordName RecordName = { RecordIndex };
            const WorkspaceRecord* Held = Records.Resolve(RecordName);
            if (Held == nullptr)
                continue;

            if (Held->ParentFolder.Assigned() && Records.Resolve(Held->ParentFolder) != nullptr)
            {
                ChildBuckets[Held->ParentFolder.IssuedIndex].push_back(RecordName);
                continue;
            }

            WorkspaceCategory Category = PresentedCategoryOfRecord(*Held);
            if (!HasCategoryRoot(Category))
                Category = WorkspaceCategory::Sketch;
            RootBuckets[static_cast<std::size_t>(Category)].push_back(RecordName);
        }
    }

    void Emit()
    {
        EmitCategoryRoot(WorkspaceCategory::Sketch);
        EmitCategoryRoot(WorkspaceCategory::Geometry);
        EmitCategoryRoot(WorkspaceCategory::Annotation);
        EmitCategoryRoot(WorkspaceCategory::Operation);

        for (std::uint32_t RecordIndex = 1u; RecordIndex <= Records.DeclaredCount(); ++RecordIndex)
        {
            if (Recorded[RecordIndex])
                continue;

            const WorkspaceRecord* Held = Records.Resolve({ RecordIndex });
            if (Held == nullptr)
                continue;

            WorkspaceCategory Category = PresentedCategoryOfRecord(*Held);
            if (!HasCategoryRoot(Category))
                Category = WorkspaceCategory::Sketch;

            EmitRecord({ RecordIndex }, CategoryRootRow[static_cast<std::size_t>(Category)], 1u);
        }

        for (WorkspaceDirectoryRow& Row : Presented.Rows)
            Row.EnclosedCount = 0u;

        for (std::uint32_t RowIndex = 0u; RowIndex < Presented.Rows.size(); ++RowIndex)
        {
            const std::uint32_t Parent = Presented.Rows[RowIndex].Enclosing;
            if (Parent < Presented.Rows.size())
                ++Presented.Rows[Parent].EnclosedCount;
        }
    }

private:
    void EmitCategoryRoot(WorkspaceCategory Category)
    {
        WorkspaceDirectoryRow Row = {};
        Row.Role = WorkspaceDirectoryRowRole::CategoryRoot;
        Row.Category = Category;
        Row.Subject = WorkspaceRecordSubject::Folder;
        Row.Family = WorkspaceShapeFamily::Unknown;
        Row.Depth = 0u;
        Row.Enclosing = NoDirectoryParent;
        Row.StableIdentity = CategoryIdentity(Category);
        Row.Naming = WorkspaceCategoryText(Category);
        Row.Tagged = CategoryRootTags(Category);

        CategoryRootRow[static_cast<std::size_t>(Category)] = static_cast<std::uint32_t>(Presented.Rows.size());
        Presented.Rows.push_back(Row);

        const auto& Bucket = RootBuckets[static_cast<std::size_t>(Category)];
        for (WorkspaceRecordName Record : Bucket)
            EmitRecord(Record, CategoryRootRow[static_cast<std::size_t>(Category)], 1u);
    }

    void EmitRecord(WorkspaceRecordName RecordName, std::uint32_t ParentRow, std::uint32_t Depth)
    {
        if (!RecordName.Assigned() || RecordName.IssuedIndex >= Recorded.size() || Recorded[RecordName.IssuedIndex])
            return;

        const WorkspaceRecord* Held = Records.Resolve(RecordName);
        if (Held == nullptr)
            return;

        Recorded[RecordName.IssuedIndex] = true;

        WorkspaceDirectoryRow Row = {};
        Row.Role = WorkspaceDirectoryRowRole::Record;
        Row.Category = PresentedCategoryOfRecord(*Held);
        Row.Record = RecordName;
        Row.Subject = Held->Subject;
        Row.Family = Held->Family;
        Row.Depth = Depth;
        Row.Enclosing = ParentRow;
        Row.StableIdentity = static_cast<std::uint64_t>(RecordName.IssuedIndex);
        Row.Naming = Held->Naming.c_str();
        Row.Tagged = WorkspaceDirectorySubjectTags(Held->Subject, Held->ClosedSemantic);
        Row.Visible = Held->Visible;
        Row.Locked = Held->Locked;
        Row.ClosedSemantic = Held->ClosedSemantic;
        Row.AutoNamed = Held->AutoNamed;

        const std::uint32_t RowIndex = static_cast<std::uint32_t>(Presented.Rows.size());
        Presented.Rows.push_back(Row);

        if (RecordName.IssuedIndex >= ChildBuckets.size())
            return;

        const auto& Children = ChildBuckets[RecordName.IssuedIndex];
        for (WorkspaceRecordName Child : Children)
            EmitRecord(Child, RowIndex, Depth + 1u);
    }

    const WorkspaceRecordStructure& Records;
    WorkspaceDirectoryProjection& Presented;
    std::vector<std::vector<WorkspaceRecordName>> RootBuckets = {};
    std::vector<std::vector<WorkspaceRecordName>> ChildBuckets = {};
    std::vector<bool> Recorded = {};
    std::uint32_t CategoryRootRow[static_cast<std::uint32_t>(WorkspaceCategory::CategoryCount)] = {};
};

} // namespace

void WorkspaceDirectoryProjection::Reclaim()
{
    Rows.clear();
}

const char* WorkspaceCategoryText(WorkspaceCategory Category)
{
    switch (Category)
    {
        case WorkspaceCategory::Sketch:     return "Sketch";
        case WorkspaceCategory::Geometry:   return "Geometry";
        case WorkspaceCategory::Annotation: return "Annotation";
        case WorkspaceCategory::Operation:  return "Operations";
        case WorkspaceCategory::Folder:     return "Folders";
        case WorkspaceCategory::CategoryCount:
            return "Workspace";
    }
    return "Workspace";
}

const char* WorkspaceRecordSubjectText(WorkspaceRecordSubject Subject)
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:         return "Point";
        case WorkspaceRecordSubject::OpenCurve:     return "Open Curve";
        case WorkspaceRecordSubject::ClosedProfile: return "Profile";
        case WorkspaceRecordSubject::ThinSurface:   return "Thin Surface";
        case WorkspaceRecordSubject::Solid:         return "Solid";
        case WorkspaceRecordSubject::Dimension:     return "Dimension";
        case WorkspaceRecordSubject::Constraint:    return "Constraint";
        case WorkspaceRecordSubject::Pattern:       return "Pattern";
        case WorkspaceRecordSubject::Mirror:        return "Mirror";
        case WorkspaceRecordSubject::Folder:        return "Folder";
        case WorkspaceRecordSubject::SubjectCount:
            return "Record";
    }
    return "Record";
}

const char* WorkspaceShapeFamilyText(WorkspaceShapeFamily Family)
{
    switch (Family)
    {
        case WorkspaceShapeFamily::Point:         return "Point";
        case WorkspaceShapeFamily::Line:          return "Line";
        case WorkspaceShapeFamily::CircularArc:   return "Circular Arc";
        case WorkspaceShapeFamily::Bezier:        return "Bezier";
        case WorkspaceShapeFamily::Hermite:       return "Hermite";
        case WorkspaceShapeFamily::BasisSpline:   return "Basis Spline";
        case WorkspaceShapeFamily::Nurbs:         return "NURBS";
        case WorkspaceShapeFamily::Polygon:       return "Polygon";
        case WorkspaceShapeFamily::Rectangle:     return "Rectangle";
        case WorkspaceShapeFamily::Slot:          return "Slot";
        case WorkspaceShapeFamily::Circle:        return "Circle";
        case WorkspaceShapeFamily::Ellipse:       return "Ellipse";
        case WorkspaceShapeFamily::Profile:       return "Profile";
        case WorkspaceShapeFamily::Surface:       return "Surface";
        case WorkspaceShapeFamily::Solid:         return "Solid";
        case WorkspaceShapeFamily::Dimension:     return "Dimension";
        case WorkspaceShapeFamily::Constraint:    return "Constraint";
        case WorkspaceShapeFamily::Workplane:     return "Workplane";
        case WorkspaceShapeFamily::Construction:  return "Construction";
        case WorkspaceShapeFamily::Unknown:
        case WorkspaceShapeFamily::ShapeFamilyCount:
            return "Unclassified";
    }
    return "Unclassified";
}

const char* WorkspaceDirectoryCategoryTags(WorkspaceCategory Category)
{
    switch (Category)
    {
        case WorkspaceCategory::Sketch:     return "sketch curves profiles points cad";
        case WorkspaceCategory::Geometry:   return "geometry surfaces solids cad";
        case WorkspaceCategory::Annotation: return "annotation dimensions constraints cad";
        case WorkspaceCategory::Operation:  return "operation pattern mirror cad";
        case WorkspaceCategory::Folder:     return "folders organization cad";
        case WorkspaceCategory::CategoryCount:
            return "cad workspace";
    }
    return "cad workspace";
}

const char* WorkspaceDirectorySubjectTags(WorkspaceRecordSubject Subject, bool ClosedSemantic)
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:
            return "point vertex sketch cad";
        case WorkspaceRecordSubject::OpenCurve:
            return "open curve line arc bezier sketch cad";
        case WorkspaceRecordSubject::ClosedProfile:
            return ClosedSemantic ? "closed profile region loop sketch cad"
                                  : "profile loop sketch cad";
        case WorkspaceRecordSubject::ThinSurface:
            return "thin surface sheet geometry cad";
        case WorkspaceRecordSubject::Solid:
            return "solid body geometry cad";
        case WorkspaceRecordSubject::Dimension:
            return "dimension annotation measurement cad";
        case WorkspaceRecordSubject::Constraint:
            return "constraint annotation relation cad";
        case WorkspaceRecordSubject::Pattern:
            return "pattern operation feature cad";
        case WorkspaceRecordSubject::Mirror:
            return "mirror operation feature cad";
        case WorkspaceRecordSubject::Folder:
            return "folder organization cad";
        case WorkspaceRecordSubject::SubjectCount:
            return "cad";
    }
    return "cad";
}

void ProjectWorkspaceDirectory(const WorkspaceRecordStructure& Records,
                               WorkspaceDirectoryProjection& Presented)
{
    Presented.Reclaim();

    WorkspaceDirectoryBuilder Builder(Records, Presented);
    Builder.Bucket();
    Builder.Emit();
}

Deliver<std::uint32_t> ResolveWorkspaceDirectoryRow(const WorkspaceDirectoryProjection& Presented,
                                                    WorkspaceRecordName Subject)
{
    if (!Subject.Assigned())
    {
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported,
                                                "the workspace record is not assigned" });
    }

    for (std::uint32_t RowIndex = 0u; RowIndex < Presented.Rows.size(); ++RowIndex)
    {
        if (Presented.Rows[RowIndex].Record.IssuedIndex == Subject.IssuedIndex)
            return Deliver<std::uint32_t>::Result(RowIndex);
    }

    return Deliver<std::uint32_t>::Refuse({ RefusalReason::ContentUnsupported,
                                            "the workspace record is not present in the directory projection" });
}

} // namespace Slate
