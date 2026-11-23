//============================================================================================================================================
//                                                        EDITORLEAFPANELS.H
//============================================================================================================================================
// 🧩 One skeletal render target, applied four ways, for the scene, UV, outliner and property leaves inside editor chrome.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"
#include "SlateUI/Interface/InterfaceExchange/Api/RecordingSurface.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONTENT PRESENTATIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Names which leaf a `LeafPanel` presents. The ordinal selects the leaf's ground colour, caption and text
///     size out of `ThemeProfile`; it carries no other meaning.
/// note  🔴 Adding a leaf is adding an enumerator and a row in `LeafAppearance`, never a new class. The four
///       leaves differed only in those three ordinates, so four classes were four copies of one body.
enum class LeafSubject : std::uint32_t
{
    Scene      = 0u,   // [-] - the three-dimensional scene render target
    Uv         = 1u,   // [-] - the selected geometry's parametric render target
    Outliner   = 2u,   // [-] - the scene outline
    Property   = 3u,   // [-] - the selected record's properties
    LayerStack = 4u,   // [-] - the texture-texture layer stack
    SubjectCount
};

/// 🧩 Presents one skeletal render target beneath shared editor chrome, chosen by `LeafSubject`.
/// tag   owning, nonallocating, nonthrowing
/// note  This replaced `ScenePanel`, `UvPanel`, `OutlinerPanel` and `PropertyPanel`, which shared a body and
///       differed only in the ground colour, the caption and the text size. `Slate::OutlinerPanel` also collided
///       by name with a second panel class of the same spelling; that collision is gone with the class.
class LeafPanel
{
public:
    /// 🧩 Applies the recording surface, the appearance declarations and the leaf this instance presents.
    /// in    IncomingSurface     [-]  the surface every recording is made against
    /// in    IncomingAppearance  [-]  the appearance declarations the ground colour is read from
    /// in    IncomingSubject     [-]  which leaf this instance presents
    /// out   Deliver<bool>       [-]  refuses when a construction already stands
    /// cost  ✔️
    Deliver<bool> ConstructLeafPanel(RecordingSurface& IncomingSurface,
                            const ThemeProfile& IncomingAppearance,
                            LeafSubject IncomingSubject);

    /// 🧩 Records the leaf's ground and its centred caption across the incoming extent.
    /// in    Extent  [px]  the body the leaf occupies
    /// cost  ✔️
    void Record(const PlaneExtent& Extent);

    /// 🧩 Returns the instance to its unconstructed condition.
    /// cost  ✔️
    void Reset();

private:
    RecordingSurface* Surface = nullptr;
    const ThemeProfile* Appearance = nullptr;
    LeafSubject Subject = LeafSubject::Scene;
};

}   // namespace Slate
