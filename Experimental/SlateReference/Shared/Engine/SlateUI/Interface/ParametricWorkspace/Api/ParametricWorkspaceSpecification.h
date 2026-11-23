//============================================================================================================================================
//                                                   PARAMETRICWORKSPACESPECIFICATION.H
//============================================================================================================================================
// 🧩 The parametric workspace's shared guarantee between a future dedicated CAD panel and the host-owned
//    presentation state it will draw: directory rows, Properties | Revision presentation, and category/
//    subject classification. This is the CAD sibling of SceneDirectorySpecification and TexturingSpecification.

#pragma once

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"
#include "SlateUI/Interface/TreeMechanics/Api/TreeMechanics.h"
#include "Foundation/WorkspaceShapeFamily.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CATEGORIES
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which CAD discipline one committed workspace row belongs to.
/// tag   guarantee
// 🔴 These are the OUTLINER and inspector categories, not kernel disciplines. The split is what the artist
//    filters and what the future properties/revision pages label; it therefore follows the workspace record
//    structure rather than the GPU pass or the exact-geometry library seams.
enum class ParametricCategory : std::uint32_t
{
    Sketch       = 0u,
    Geometry     = 1u,
    Annotation   = 2u,
    Operation    = 3u,
    CategoryCount = 4u
};

/// 🧩 The hue one CAD category carries in the outliner and the properties crest.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ThemeToken ParametricCategoryHue(ParametricCategory Category);

/// 🧩 The run naming one CAD category.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* ParametricCategoryText(ParametricCategory Category);

/// 🧩 The outliner filter category a CAD category belongs to; one-to-one on purpose.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
std::uint32_t ParametricFacetOf(ParametricCategory Category);

/// 🧩 The four filter captions the CAD outliner presents.
/// tag   guarantee
inline constexpr std::uint32_t ParametricFacetCount = 4u;
inline constexpr const char* const ParametricFacetNames[ParametricFacetCount] =
{
    "Sketch", "Geometry", "Annotation", "Operations"
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE ROWS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 What one parametric workspace row is, which decides its glyph, hue and property schema.
/// note  📐 `CategoryRoot` is explicit rather than encoded as a Folder with a flag, because the future CAD
///        panel will style the four synthetic roots differently from user folders while keeping both in one
///        flattened tree.
/// tag   guarantee
enum class ParametricRowSubject : std::uint32_t
{
    CategoryRoot   = 0u,
    Folder         = 1u,
    Point          = 2u,
    OpenCurve      = 3u,
    ClosedProfile  = 4u,
    ThinSurface    = 5u,
    Solid          = 6u,
    Dimension      = 7u,
    Constraint     = 8u,
    Pattern        = 9u,
    Mirror         = 10u,
    SubjectCount   = 11u
};

/// 🧩 The glyph one parametric row is drawn with.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
SymbolSubject ParametricRowGlyph(ParametricRowSubject Subject);

/// 🧩 The hue one parametric row carries. Category roots inherit their category hue; folders stay neutral.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
ThemeToken ParametricRowHue(ParametricRowSubject Subject, ParametricCategory Category);

/// 🧩 The run naming one parametric row subject.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* ParametricRowText(ParametricRowSubject Subject);

/// 🧩 One row of the parametric workspace directory, linearised and carrying its own depth exactly as the
///    scene directory and the texture stack do.
/// note  📝 Borrowed for the tick: name and tags outlive the call that records them; the host owns the
///        storage and the future panel retains none of it.
/// tag   guarantee, nonallocating, nonthrowing
struct ParametricDirectoryRow
{
    const char*           Naming         = "";
    ParametricRowSubject  Subject        = ParametricRowSubject::Point;
    WorkspaceShapeFamily  Family         = WorkspaceShapeFamily::Unknown;
    ParametricCategory    Category       = ParametricCategory::Sketch;
    std::uint32_t         Depth          = 0u;
    std::uint32_t         Enclosing      = 0xFFFFFFFFu;
    std::uint32_t         EnclosedCount  = 0u;
    const char*           Tagged         = "";
    StableRowIdentity     Identity       = 0u;
    bool                  Visible        = true;
    bool                  Locked         = false;
    bool                  ClosedSemantic = false;
    bool                  CappedExtrusionSemantic = false;
    bool                  AutoNamed      = false;
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PROPERTIES | REVISION CONTENT
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Which page the future CAD properties leaf is on.
/// tag   guarantee
enum class ParametricInspectorPage : std::uint32_t
{
    Properties = 0u,
    Revision   = 1u,
    PageCount  = 2u
};

/// 🧩 The run naming one inspector page.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
const char* ParametricInspectorPageText(ParametricInspectorPage Page);

/// 🧩 One property row the future properties page states.
/// note  📝 `Secondary` is the small trailing run — unit, state or token — and may be empty.
/// tag   guarantee, nonallocating, nonthrowing
struct ParametricPropertyField
{
    const char* Caption   = "";
    const char* Value     = "";
    const char* Secondary = "";
};

/// 🧩 The selected row's property presentation, already resolved into runs the future panel can record.
/// note  The host prepares these runs from the exact workspace record and its filtered revision set; the
///       panel later reads only what it is handed.
/// tag   guarantee, nonallocating, nonthrowing
struct ParametricPropertyPresentation
{
    static constexpr std::uint32_t FieldLimit = 12u;

    const char*           Naming         = "";
    const char*           Secondary      = "";
    ParametricRowSubject  Subject        = ParametricRowSubject::Point;
    WorkspaceShapeFamily  Family         = WorkspaceShapeFamily::Unknown;
    ParametricCategory    Category       = ParametricCategory::Sketch;
    StableRowIdentity     Identity       = 0u;
    bool                  Visible        = true;
    bool                  Locked         = false;
    bool                  ClosedSemantic = false;
    bool                  CappedExtrusionSemantic = false;
    bool                  AutoNamed      = false;
    ParametricPropertyField Fields[FieldLimit] = {};
    std::uint32_t         FieldCount     = 0u;
};

/// 🧩 One revision row presented on the selected object's Revision page.
/// note  `Affected` is a preformatted run — e.g. `Solid_1, Profile_2` — because the future panel presents
///       text rather than resolving graph identity inside a tick.
/// tag   guarantee, nonallocating, nonthrowing
struct ParametricRevisionRow
{
    const char*       Description = "";
    const char*       Operation   = "";
    const char*       Affected    = "";
    const char*       SealedAt    = "";
    StableRowIdentity Identity    = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   HOST-OWNED UI STATE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Every mutable UI condition the future dedicated parametric workspace leaf will own through its host:
///    selection, disclosure, filter, and the one-leaf Directory | Properties | Revision travel.
/// note  🔴 Visibility and lock do NOT live here. Those are authored workspace semantics and belong to the
///        borrowed rows; this context holds only presentation state the panel mutates directly.
/// tag   guarantee
struct ParametricWorkspaceContext
{
    static constexpr std::uint32_t RowLimit       = 128u;
    static constexpr std::uint32_t SearchLimit    = 48u;
    static constexpr std::uint32_t RevisionLimit  = 128u;

    char                     RowRetention[SearchLimit] = {};
    bool                     SearchTaken = false;
    bool                     FacetEnabled[ParametricFacetCount] = {};

    bool                     RowExpanded[RowLimit] = {};
    bool                     RowSelected[RowLimit] = { true };
    std::uint32_t            RowSelectionAnchor = 0u;
    std::uint32_t            RowTaken           = 0u;

    std::uint32_t            OutlinePage = 0u; // [-] - 0 Directory, 1 Inspector
    ParametricInspectorPage  InspectorPage = ParametricInspectorPage::Properties;

    // Authored by the Properties page when a selected closed profile exposes its capped/wall extrusion toggle.
    bool                     ExtrusionCapToggleDemand = false;
    StableRowIdentity        ExtrusionCapToggleIdentity = 0u;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ROW FILTERING
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Whether the search or any CAD facet is active.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool ParametricRetentionActive(const ParametricWorkspaceContext& Applied);

/// 🧩 Whether the search and CAD facets jointly retain one row.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
bool ParametricRowRetained(const ParametricWorkspaceContext& Applied,
                           const ParametricDirectoryRow& Row);

} // namespace Slate
