//============================================================================================================================================
//                                                  PARAMETRICWORKSPACESPECIFICATION.CPP
//============================================================================================================================================

#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h"

namespace Slate
{

namespace
{

bool RunHolds(const char* Subject, const char* Sought)
{
    if (Sought == nullptr || Sought[0] == '\0')
        return true;

    if (Subject == nullptr)
        return false;

    const auto Lowered = [](char Letter) -> char
    {
        return (Letter >= 'A' && Letter <= 'Z') ? static_cast<char>(Letter - 'A' + 'a') : Letter;
    };

    for (std::uint32_t Departure = 0u; Subject[Departure] != '\0'; ++Departure)
    {
        std::uint32_t Advanced = 0u;

        while (Sought[Advanced] != '\0' &&
               Lowered(Subject[Departure + Advanced]) == Lowered(Sought[Advanced]))
        {
            ++Advanced;
        }

        if (Sought[Advanced] == '\0')
            return true;
    }

    return false;
}

} // namespace

ThemeToken ParametricCategoryHue(ParametricCategory Category)
{
    switch (Category)
    {
        case ParametricCategory::Sketch:      return Covering(0x3B82F6u);
        case ParametricCategory::Geometry:    return Covering(0x10B981u);
        case ParametricCategory::Annotation:  return Covering(0x8B5CF6u);
        case ParametricCategory::Operation:   return Covering(0xF59E0Bu);
        case ParametricCategory::CategoryCount:
            return Covering(0x8A8A8Au);
    }
    return Covering(0x8A8A8Au);
}

const char* ParametricCategoryText(ParametricCategory Category)
{
    switch (Category)
    {
        case ParametricCategory::Sketch:      return "Sketch";
        case ParametricCategory::Geometry:    return "Geometry";
        case ParametricCategory::Annotation:  return "Annotation";
        case ParametricCategory::Operation:   return "Operations";
        case ParametricCategory::CategoryCount:
            return "Workspace";
    }
    return "Workspace";
}

std::uint32_t ParametricFacetOf(ParametricCategory Category)
{
    return static_cast<std::uint32_t>(Category) < ParametricFacetCount
         ? static_cast<std::uint32_t>(Category) : 0u;
}

SymbolSubject ParametricRowGlyph(ParametricRowSubject Subject)
{
    switch (Subject)
    {
        case ParametricRowSubject::CategoryRoot:  return SymbolSubject::GearCog;
        case ParametricRowSubject::Folder:        return SymbolSubject::FolderClosed;
        case ParametricRowSubject::Point:         return SymbolSubject::VertexPoint;
        case ParametricRowSubject::OpenCurve:     return SymbolSubject::EdgeSegment;
        case ParametricRowSubject::ClosedProfile: return SymbolSubject::SketchPlane;
        case ParametricRowSubject::ThinSurface:   return SymbolSubject::FacePlanar;
        case ParametricRowSubject::Solid:         return SymbolSubject::CubeSolid;
        case ParametricRowSubject::Dimension:     return SymbolSubject::ConstraintDimension;
        case ParametricRowSubject::Constraint:    return SymbolSubject::FilletRadius;
        case ParametricRowSubject::Pattern:       return SymbolSubject::LoftProfile;
        case ParametricRowSubject::Mirror:        return SymbolSubject::MirrorAxis;
        case ParametricRowSubject::SubjectCount:
            return SymbolSubject::PlaceholderMark;
    }
    return SymbolSubject::PlaceholderMark;
}

ThemeToken ParametricRowHue(ParametricRowSubject Subject, ParametricCategory Category)
{
    switch (Subject)
    {
        case ParametricRowSubject::Folder:
            return Covering(0x8A8A8Au);
        case ParametricRowSubject::CategoryRoot:
            return ParametricCategoryHue(Category);
        default:
            return ParametricCategoryHue(Category);
    }
}

const char* ParametricRowText(ParametricRowSubject Subject)
{
    switch (Subject)
    {
        case ParametricRowSubject::CategoryRoot:  return "Category";
        case ParametricRowSubject::Folder:        return "Folder";
        case ParametricRowSubject::Point:         return "Point";
        case ParametricRowSubject::OpenCurve:     return "Open Curve";
        case ParametricRowSubject::ClosedProfile: return "Closed Profile";
        case ParametricRowSubject::ThinSurface:   return "Thin Surface";
        case ParametricRowSubject::Solid:         return "Solid";
        case ParametricRowSubject::Dimension:     return "Dimension";
        case ParametricRowSubject::Constraint:    return "Constraint";
        case ParametricRowSubject::Pattern:       return "Pattern";
        case ParametricRowSubject::Mirror:        return "Mirror";
        case ParametricRowSubject::SubjectCount:
            return "Row";
    }
    return "Row";
}

const char* ParametricInspectorPageText(ParametricInspectorPage Page)
{
    switch (Page)
    {
        case ParametricInspectorPage::Properties: return "Properties";
        case ParametricInspectorPage::Revision:   return "Revision";
        case ParametricInspectorPage::PageCount:
            return "Inspector";
    }
    return "Inspector";
}

bool ParametricRetentionActive(const ParametricWorkspaceContext& Applied)
{
    if (Applied.RowRetention[0] != '\0')
        return true;

    for (std::uint32_t Facet = 0u; Facet < ParametricFacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return true;
    }

    return false;
}

bool ParametricRowRetained(const ParametricWorkspaceContext& Applied,
                           const ParametricDirectoryRow& Row)
{
    if (Applied.RowRetention[0] != '\0')
    {
        if (!RunHolds(Row.Naming, Applied.RowRetention) &&
            !RunHolds(Row.Tagged, Applied.RowRetention))
            return false;
    }

    for (std::uint32_t Facet = 0u; Facet < ParametricFacetCount; ++Facet)
    {
        if (Applied.FacetEnabled[Facet])
            return Applied.FacetEnabled[ParametricFacetOf(Row.Category)];
    }

    return true;
}

} // namespace Slate
