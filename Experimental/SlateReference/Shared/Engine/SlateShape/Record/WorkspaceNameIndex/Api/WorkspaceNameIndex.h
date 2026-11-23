//============================================================================================================================================
//                                                        WORKSPACENAMEINDEX.H
//============================================================================================================================================
// 🧩 Monotonic default naming for committed parametric workspace records. Auto-generated names advance by
//    subject and never renumber after deletion, so history and references remain readable.

#pragma once

#include "Foundation/WorkspaceShapeFamily.h"

#include <cstdint>
#include <string>

namespace Slate
{

enum class WorkspaceRecordSubject : std::uint32_t;

class WorkspaceNameIndex
{
public:
    std::string Issue(WorkspaceRecordSubject Subject, WorkspaceShapeFamily Family = WorkspaceShapeFamily::Unknown);
    void Reclaim();

private:
    std::uint32_t PointCount = 0u;
    std::uint32_t CurveCount = 0u;
    std::uint32_t LineCount = 0u;
    std::uint32_t ArcCount = 0u;
    std::uint32_t CircleCount = 0u;
    std::uint32_t EllipseCount = 0u;
    std::uint32_t RectangleCount = 0u;
    std::uint32_t PolygonCount = 0u;
    std::uint32_t SlotCount = 0u;
    std::uint32_t BezierCount = 0u;
    std::uint32_t HermiteCount = 0u;
    std::uint32_t SplineCount = 0u;
    std::uint32_t NurbsCount = 0u;
    std::uint32_t ProfileCount = 0u;
    std::uint32_t SurfaceCount = 0u;
    std::uint32_t SolidCount = 0u;
    std::uint32_t DimensionCount = 0u;
    std::uint32_t ConstraintCount = 0u;
    std::uint32_t PatternCount = 0u;
    std::uint32_t MirrorCount = 0u;
    std::uint32_t FolderCount = 0u;
};

} // namespace Slate
