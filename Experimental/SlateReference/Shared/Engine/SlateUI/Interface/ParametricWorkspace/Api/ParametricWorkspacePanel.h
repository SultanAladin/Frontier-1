//============================================================================================================================================
//                                                       PARAMETRICWORKSPACEPANEL.H
//============================================================================================================================================
// 🧩 Dedicated CAD leaf content for the future parametric workspace: a SceneDirectory-style outliner and a
//    Properties | Revision inspector, driven by the parametric workspace guarantee rather than scene semantics.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/ComponentSpecification/Api/ComponentSpecification.h"
#include "SlateUI/Interface/ControlPanel/Api/ControlPanel.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/FacetPanel/Api/FacetPanel.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/OverflowScroll/Api/OverflowScroll.h"
#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspaceSpecification.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectorySpecification.h"
#include "SlateUI/Interface/SlidingPages/Api/SlidingPages.h"

#include <cstdint>

namespace Slate
{

/// 🧩 Records the CAD outliner and inspector leaves inside the extents the editor or a future dedicated
///    parametric workspace host hands over.
/// note  🔴 This is a leaf-content panel only. It does not own workspace windows, panel chrome, or the CAD
///        render pass; the host records those around it.
/// tag   owning
class ParametricWorkspacePanel
{
public:

    ParametricWorkspacePanel()                                      = default;
    ParametricWorkspacePanel(const ParametricWorkspacePanel&)            = delete;
    ParametricWorkspacePanel& operator=(const ParametricWorkspacePanel&) = delete;
    ~ParametricWorkspacePanel()                                     = default;

    Deliver<bool> ConstructParametricWorkspacePanel(ControlIndex& IncomingInteraction,
                                                    MotionIntegrator& Integrator,
                                                    RecordingSurface& Surface,
                                                    const ThemeProfile& Resolved);

    /// 🧩 Samples the shared pointer/contact state for this tick and advances the dedicated CAD controls.
    /// note  🔴 This does not advance the shared interaction index; the tick owner advances it once.
    void Advance(const PointerCondition& Sampled, double Elapsed,
                 ParametricWorkspaceContext& Applied, bool TabPressed = false,
                 const ModifierCondition& Modifiers = {});

    void Reapply(const ThemeProfile& Resolved);
    void Reset();

    /// 🧩 Records the combined CAD directory leaf: outliner, and the slid-in Properties | Revision page.
    void RecordOutliner(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied,
                        const ParametricDirectoryRow* Rows, std::uint32_t RowCount,
                        const ParametricPropertyPresentation* Property,
                        const ParametricRevisionRow* Revisions,
                        std::uint32_t RevisionCount);

    /// 🧩 Records the selected CAD inspector leaf standalone, for hosts that still dedicate a full panel to it.
    void RecordProperties(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied,
                          const ParametricPropertyPresentation* Property,
                          const ParametricRevisionRow* Revisions,
                          std::uint32_t RevisionCount,
                          bool OutlinePresentation = false);

private:

    void RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                          const ThemeToken& Hue, const char* Titled,
                          const char* Secondary);
    void RecordSearchField(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied);
    void RecordPropertyPage(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied,
                            const ParametricPropertyPresentation& Property,
                            float ScrollOffset);
    void RecordRevisionPage(const PlaneExtent& Extent,
                            const ParametricRevisionRow* Revisions,
                            std::uint32_t RevisionCount,
                            float ScrollOffset);
    void RecordDirectoryPage(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied,
                             const ParametricDirectoryRow* Rows, std::uint32_t RowCount,
                             const ParametricPropertyPresentation* Property);

    ControlIndex*           Interaction = nullptr;
    MotionIntegrator*       Motion = nullptr;
    RecordingSurface*       Surface = nullptr;
    const ThemeProfile*     Appearance = nullptr;
    PointerCondition        Sampled = {};
    ModifierCondition       Modified = {};
    ShellColour             Tinted = {};
    ShellMetric             Scaled = {};

    ControlPanel            Controls = {};
    FacetPanel              Facets = {};
    OverflowScroll          OutlineOverflow = {};
    OverflowScroll          InspectorOverflow = {};
    SlidingPages            OutlinePages = {};

    ControlIdentity         SearchField = {};
    ControlIdentity         InspectorStrip = {};
    ControlIdentity         DirectoryCall = {};
    ControlIdentity         InspectCall = {};
    ControlIdentity         RowContacts[ParametricWorkspaceContext::RowLimit] = {};
    ControlIdentity         RowDisclosures[ParametricWorkspaceContext::RowLimit] = {};
};

} // namespace Slate
