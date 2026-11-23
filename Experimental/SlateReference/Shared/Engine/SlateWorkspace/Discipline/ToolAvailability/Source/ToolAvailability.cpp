//============================================================================================================================================
//                                                       TOOLAVAILABILITY.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ToolAvailability/Api/ToolAvailability.h"

namespace Slate
{

namespace
{

bool ResolveCurrentSemanticSelection(const SketchStructure& Sketch,
                                     const WorkspaceRecordStructure& Records,
                                     const SketchPick& SemanticSelection,
                                     SketchPick& Resolved)
{
    Resolved = SemanticSelection;

    switch (SemanticSelection.Subject)
    {
        case SketchPickSubject::Point:
            Resolved.Record = ResolveRecordForPoint(Sketch, Records, SemanticSelection.Point);
            if (!Resolved.Record.Assigned())
                return false;
            Resolved.Curve = { SemanticSelection.Point.IssuedIndex >> 8u };
            return ResolveSketchPointPosition(Sketch, SemanticSelection.Point, Resolved.Position);

        case SketchPickSubject::Control:
        {
            const std::uint32_t CurveIndex = SemanticSelection.Control.IssuedIndex >> 12u;
            if (CurveIndex == 0u || CurveIndex > Sketch.Curves().size())
                return false;

            std::vector<SketchControlPlacement> Controls;
            if (!ResolveSketchControls(Sketch, { CurveIndex }, Controls))
                return false;

            for (const SketchControlPlacement& Control : Controls)
                if (Control.Name.IssuedIndex == SemanticSelection.Control.IssuedIndex)
                {
                    Resolved.Curve = Control.SourceCurve;
                    Resolved.Position = Control.Position;
                    Resolved.Record = ResolveRecordForCurve(Sketch, Records, Control.SourceCurve);
                    return Resolved.Record.Assigned();
                }
            return false;
        }

        case SketchPickSubject::Curve:
            Resolved.Record = ResolveRecordForCurve(Sketch, Records, SemanticSelection.Curve);
            return Resolved.Record.Assigned()
                && ResolveCurvePivot(Sketch, SemanticSelection.Curve, Resolved.Position);

        case SketchPickSubject::Record:
            return ResolvePickForRecord(Sketch, Records, SemanticSelection.Record, Resolved);

        case SketchPickSubject::None:
            return false;
    }

    return false;
}

} // namespace

ToolAvailability AvailabilityFor(WorkspaceRecordSubject Subject)
{
    ToolAvailability Answered = {};

    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:
            Answered.Dimension  = ParametricToolDimension::Vertex;
            Answered.Measurable = true;
            return Answered;

        case WorkspaceRecordSubject::OpenCurve:
            Answered.Dimension          = ParametricToolDimension::Edge;
            Answered.Path               = true;
            Answered.Axis               = true;
            Answered.Measurable         = true;
            Answered.TangentEndpoint    = true;
            Answered.PerimeterEdgeCount = 1u;
            return Answered;

        case WorkspaceRecordSubject::ClosedProfile:
            Answered.Dimension          = ParametricToolDimension::Wire;
            Answered.ProfileCount       = 1u;
            Answered.Axis               = true;
            Answered.Path               = true;
            Answered.ClosedProfile      = true;
            Answered.Measurable         = true;
            Answered.PerimeterEdgeCount = 5u;
            return Answered;

        case WorkspaceRecordSubject::ThinSurface:
            Answered.Dimension        = ParametricToolDimension::Shell;
            Answered.ProfileCount     = 1u;
            Answered.SupportsMaterial = true;
            Answered.Opening          = true;
            Answered.Measurable       = true;
            return Answered;

        case WorkspaceRecordSubject::Solid:
            Answered.Dimension        = ParametricToolDimension::Solid;
            Answered.SolidCount       = 1u;
            Answered.SupportsMaterial = true;
            Answered.ReferencePlane   = true;
            Answered.Axis             = true;
            Answered.Measurable       = true;
            Answered.ClosedProfile    = true;
            return Answered;

        case WorkspaceRecordSubject::Dimension:
        case WorkspaceRecordSubject::Constraint:
            Answered.Dimension  = ParametricToolDimension::Edge;
            Answered.Measurable = true;
            return Answered;

        case WorkspaceRecordSubject::Pattern:
        case WorkspaceRecordSubject::Mirror:
            Answered.Dimension      = ParametricToolDimension::Face;
            Answered.ReferencePlane = true;
            Answered.Measurable     = true;
            return Answered;

        // 📝 A folder is a place to put things, not a thing. Nothing is offered for one, which is also why
        //    `InitialRowIn` steps past folders when it chooses where to open.
        case WorkspaceRecordSubject::Folder:
        case WorkspaceRecordSubject::SubjectCount:
            return Answered;
    }

    return Answered;
}

WorkspaceRecordName SelectedRecordIn(const WorkspaceDirectoryProjection& Directory,
                                     const ParametricWorkspaceContext& Applied)
{
    if (Applied.RowTaken >= Directory.Rows.size())
        return {};

    const WorkspaceDirectoryRow& Row = Directory.Rows[Applied.RowTaken];
    return Row.Role == WorkspaceDirectoryRowRole::Record ? Row.Record : WorkspaceRecordName{};
}

void SelectedRecordsIn(const WorkspaceDirectoryProjection& Directory,
                       const ParametricWorkspaceContext& Applied,
                       std::vector<WorkspaceRecordName>& Selected)
{
    Selected.clear();
    const std::uint32_t Reach = std::min(static_cast<std::uint32_t>(Directory.Rows.size()),
                                         ParametricWorkspaceContext::RowLimit);
    for (std::uint32_t Index = 0u; Index < Reach; ++Index)
    {
        if (Applied.RowSelected[Index] && Directory.Rows[Index].Role == WorkspaceDirectoryRowRole::Record)
            Selected.push_back(Directory.Rows[Index].Record);
    }
}

bool AnyRowSelected(const ParametricWorkspaceContext& Applied, std::uint32_t RowCount)
{
    // ⚠️ Bounded by BOTH the count asked for and the storage the context actually has. The host's version
    //    trusted the caller's count alone, so a directory reporting more rows than the fixed array holds
    //    would have read past the end of it.
    const std::uint32_t Reach = RowCount < ParametricWorkspaceContext::RowLimit
                              ? RowCount
                              : ParametricWorkspaceContext::RowLimit;

    for (std::uint32_t Index = 0u; Index < Reach; ++Index)
        if (Applied.RowSelected[Index])
            return true;

    return false;
}

std::uint32_t InitialRowIn(const WorkspaceDirectoryProjection& Directory)
{
    for (std::uint32_t Index = 0u; Index < Directory.Rows.size(); ++Index)
        if (Directory.Rows[Index].Role == WorkspaceDirectoryRowRole::Record &&
            Directory.Rows[Index].Subject != WorkspaceRecordSubject::Folder)
            return Index;

    for (std::uint32_t Index = 0u; Index < Directory.Rows.size(); ++Index)
        if (Directory.Rows[Index].Role == WorkspaceDirectoryRowRole::Record)
            return Index;

    return 0u;
}

SketchPick EditableSelection(const SketchStructure& Sketch,
                             const WorkspaceRecordStructure& Records,
                             WorkspaceRecordName SelectedRecord,
                             WorkspaceRecordName PendingSelection,
                             const SketchPick& SemanticSelection)
{
    if (SemanticSelection.Standing() &&
        ((!SelectedRecord.Assigned() || SemanticSelection.Record.IssuedIndex == SelectedRecord.IssuedIndex) ||
         (PendingSelection.Assigned() && PendingSelection.IssuedIndex == SemanticSelection.Record.IssuedIndex)))
    {
        SketchPick Refreshed = {};
        if (ResolveCurrentSemanticSelection(Sketch, Records, SemanticSelection, Refreshed))
            return Refreshed;
    }

    SketchPick Selection = {};
    ResolvePickForRecord(Sketch, Records, SelectedRecord, Selection);
    return Selection;
}

void ResolveToolContext(const WorkspaceDirectoryProjection& Directory,
                        const WorkspaceRecordStructure& Records,
                        const SketchStructure& Sketch,
                        const ParametricWorkspaceContext& WorkspaceApplied,
                        ParametricToolsContext& ToolsApplied)
{
    // 📝 What the sketch itself makes possible, before anything is selected.
    ToolsApplied.WorkplaneActivation      = Sketch.Declared();
    ToolsApplied.ReferencePlaneCondition  = Sketch.Declared();
    ToolsApplied.PlanarProfileCondition   = true;
    ToolsApplied.UniformClosureCondition  = true;
    ToolsApplied.PendingGeometryCondition = false;
    ToolsApplied.SourceImageryCondition   = false;

    ToolsApplied.SelectedCount = 0u;
    for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
        if (WorkspaceApplied.RowSelected[Index])
            ++ToolsApplied.SelectedCount;

    // 🔴 One table lookup replaces the nine-arm switch, and — this is the part that was fragile — the
    //    availability is applied WHOLE. Every field is written from the row, so there is no reset block
    //    for an arm to disagree with. `Folder` and an out-of-range row answer a resting row, which writes
    //    the same falses the host's reset block used to write by hand.
    const WorkspaceRecordName Selected = SelectedRecordIn(Directory, WorkspaceApplied);
    const WorkspaceRecord* Record = Selected.Assigned() ? Records.Resolve(Selected) : nullptr;

    const ToolAvailability Available = Record != nullptr
                                     ? AvailabilityFor(Record->Subject)
                                     : ToolAvailability{};

    ToolsApplied.ActiveDimension           = Available.Dimension;
    ToolsApplied.ProfileCount              = Available.ProfileCount;
    ToolsApplied.PerimeterEdgeCount        = Available.PerimeterEdgeCount;
    ToolsApplied.ExistingCircleCount       = 0u;
    ToolsApplied.SolidCount                = Available.SolidCount;
    ToolsApplied.AxisAvailability          = Available.Axis;
    ToolsApplied.PathAvailability          = Available.Path;
    ToolsApplied.SupportMaterialCondition  = Available.SupportsMaterial;
    ToolsApplied.TangentEndpointCondition  = Available.TangentEndpoint;
    ToolsApplied.OpeningCondition          = Available.Opening;
    ToolsApplied.MeasurableCondition       = Available.Measurable;
    ToolsApplied.ClosedProfileCondition    = Available.ClosedProfile;

    // ⚠️ The reference plane is available when the SKETCH has one or when the SELECTION offers one, so the
    //    two sources are combined rather than the second overwriting the first. The host's switch assigned
    //    `true` in three arms and never assigned `false`, which happened to be right only because the
    //    reset ran first.
    ToolsApplied.ReferencePlaneCondition = Sketch.Declared() || Available.ReferencePlane;
}

}   // namespace Slate
