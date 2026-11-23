//============================================================================================================================================
//                                                      WORKSPACENAMEINDEX.CPP
//============================================================================================================================================

#include "SlateShape/Record/WorkspaceNameIndex/Api/WorkspaceNameIndex.h"
#include "SlateShape/Record/WorkspaceRecordStructure/Api/WorkspaceRecordStructure.h"

#include <cstdio>

namespace Slate
{

namespace
{
std::string FormatPaddedName(const char* Prefix, std::uint32_t Index)
{
    char Buffer[64];
    std::snprintf(Buffer, sizeof(Buffer), "%s_%03u", Prefix, Index);
    return std::string(Buffer);
}
} // namespace

std::string WorkspaceNameIndex::Issue(WorkspaceRecordSubject Subject, WorkspaceShapeFamily Family)
{
    switch (Subject)
    {
        case WorkspaceRecordSubject::Point:
            return FormatPaddedName("Point", ++PointCount);

        case WorkspaceRecordSubject::OpenCurve:
        {
            switch (Family)
            {
                case WorkspaceShapeFamily::Hermite:
                    return FormatPaddedName("HermiteCurve", ++HermiteCount);
                case WorkspaceShapeFamily::Bezier:
                    return FormatPaddedName("BezierCurve", ++BezierCount);
                case WorkspaceShapeFamily::BasisSpline:
                    return FormatPaddedName("SplineCurve", ++SplineCount);
                case WorkspaceShapeFamily::Nurbs:
                    return FormatPaddedName("NurbsCurve", ++NurbsCount);
                case WorkspaceShapeFamily::CircularArc:
                    return FormatPaddedName("ArcCurve", ++ArcCount);
                case WorkspaceShapeFamily::Circle:
                    return FormatPaddedName("CircleCurve", ++CircleCount);
                case WorkspaceShapeFamily::Ellipse:
                    return FormatPaddedName("EllipseCurve", ++EllipseCount);
                case WorkspaceShapeFamily::Line:
                default:
                    return FormatPaddedName("LineCurve", ++LineCount);
            }
        }

        case WorkspaceRecordSubject::ClosedProfile:
        {
            switch (Family)
            {
                case WorkspaceShapeFamily::Circle:
                    return FormatPaddedName("Circle", ++CircleCount);
                case WorkspaceShapeFamily::Ellipse:
                    return FormatPaddedName("Ellipse", ++EllipseCount);
                case WorkspaceShapeFamily::Rectangle:
                    return FormatPaddedName("Rectangle", ++RectangleCount);
                case WorkspaceShapeFamily::Polygon:
                    return FormatPaddedName("Polygon", ++PolygonCount);
                case WorkspaceShapeFamily::Slot:
                    return FormatPaddedName("Slot", ++SlotCount);
                default:
                    return FormatPaddedName("Profile", ++ProfileCount);
            }
        }

        case WorkspaceRecordSubject::ThinSurface:   return FormatPaddedName("ThinSurface", ++SurfaceCount);
        case WorkspaceRecordSubject::Solid:         return FormatPaddedName("Solid", ++SolidCount);
        case WorkspaceRecordSubject::Dimension:     return FormatPaddedName("Dimension", ++DimensionCount);
        case WorkspaceRecordSubject::Constraint:    return FormatPaddedName("Constraint", ++ConstraintCount);
        case WorkspaceRecordSubject::Pattern:       return FormatPaddedName("Pattern", ++PatternCount);
        case WorkspaceRecordSubject::Mirror:        return FormatPaddedName("Mirror", ++MirrorCount);
        case WorkspaceRecordSubject::Folder:        return FormatPaddedName("Folder", ++FolderCount);
        case WorkspaceRecordSubject::SubjectCount:  return "Unnamed_000";
    }
    return "Unnamed_000";
}

void WorkspaceNameIndex::Reclaim()
{
    PointCount = 0u;
    CurveCount = 0u;
    LineCount = 0u;
    ArcCount = 0u;
    CircleCount = 0u;
    EllipseCount = 0u;
    RectangleCount = 0u;
    PolygonCount = 0u;
    SlotCount = 0u;
    BezierCount = 0u;
    HermiteCount = 0u;
    SplineCount = 0u;
    NurbsCount = 0u;
    ProfileCount = 0u;
    SurfaceCount = 0u;
    SolidCount = 0u;
    DimensionCount = 0u;
    ConstraintCount = 0u;
    PatternCount = 0u;
    MirrorCount = 0u;
    FolderCount = 0u;
}

} // namespace Slate
