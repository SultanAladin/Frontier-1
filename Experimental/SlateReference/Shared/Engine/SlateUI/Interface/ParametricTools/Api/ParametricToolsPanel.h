//============================================================================================================================================
//                                                        PARAMETRICTOOLSPANEL.H
//============================================================================================================================================
// 🧩 Theme-aware CAD construction catalogue panel. It ports the reference's two-slide rail/grid → probe/options
//    travel into a real workspace leaf rather than a floating viewport widget.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/ControlIndex/Api/ControlIndex.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"
#include "SlateUI/Interface/MotionIntegrator/Api/MotionIntegrator.h"
#include "SlateUI/Interface/OverflowScroll/Api/OverflowScroll.h"
#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsSpecification.h"
#include "SlateUI/Interface/SceneDirectoryPanel/Api/SceneDirectorySpecification.h"
#include "SlateUI/Interface/SlidingPages/Api/SlidingPages.h"

#include <cstdint>

namespace Slate
{

class ParametricToolsPanel
{
public:
    ParametricToolsPanel() = default;
    ParametricToolsPanel(const ParametricToolsPanel&) = delete;
    ParametricToolsPanel& operator=(const ParametricToolsPanel&) = delete;
    ~ParametricToolsPanel() = default;

    Deliver<bool> ConstructParametricToolsPanel(ControlIndex& IncomingInteraction,
                                                MotionIntegrator& Integrator,
                                                RecordingSurface& Surface,
                                                const ThemeProfile& Resolved);
    void Advance(const PointerCondition& Sampled, double Elapsed,
                 ParametricToolsContext& Applied, bool TabPressed = false);
    void Reapply(const ThemeProfile& Resolved);
    void Reset();

    void Record(const PlaneExtent& Extent, ParametricToolsContext& Applied);

private:
    void RecordBrowsePage(const PlaneExtent& Extent, ParametricToolsContext& Applied);
    void RecordDetailPage(const PlaneExtent& Extent, ParametricToolsContext& Applied);
    void RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                          const ThemeToken& Hue, const char* Titled, const char* Secondary);

    ControlIndex* Interaction = nullptr;
    MotionIntegrator* Motion = nullptr;
    RecordingSurface* Surface = nullptr;
    const ThemeProfile* Appearance = nullptr;
    PointerCondition Sampled = {};
    ShellColour Tinted = {};
    ShellMetric Scaled = {};

    SlidingPages Pages = {};
    OverflowScroll RailOverflow = {};
    OverflowScroll GridOverflow = {};
    OverflowScroll ProbeOverflow = {};
    OverflowScroll OptionOverflow = {};

    ControlIdentity BackCall = {};
    ControlIdentity BandRows[ParametricToolsContext::BandLimit] = {};
    ControlIdentity TileRows[ParametricToolsContext::TileLimit] = {};
};

} // namespace Slate
