//============================================================================================================================================
//                                                           WORKSPACEPANEL.CPP
//============================================================================================================================================
// 🧩 The strip ground, the body, the footer and the vacant run — the parts the vendor's tab bar does not draw.

#include "SlateUI/Interface/WorkspacePanel/Api/WorkspacePanel.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> WorkspacePanel::ConstructWorkspacePanel(RecordingSurface& Recording, const ThemeProfile& Declared)
{
    if (Surface != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a construction already stands" });

    Surface    = &Recording;
    Appearance = &Declared;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> WorkspacePanel::Record(const PlaneExtent& Extent, const char* Titled)
{
    if (Surface == nullptr || Appearance == nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent, "no construction stands" });

    const WorkspaceMetric& Measure = Appearance->WorkspaceMeasure;
    const WorkspaceColour&    Colour     = Appearance->Workspace;

    // 🔴 With NO workspace open the shell is plain black: no strip, no footer, one invitation in the
    //    middle. A grey strip and footer framing an empty panel present chrome for content that is not
    //    there, which reads as a broken workspace rather than as a fresh one.
    if (Titled == nullptr)
    {
        Surface->Ground(Extent, Colour.BodyGround);

        BodyExtent  = Extent;
        StripExtent = {};

        const float VacantTracking = Measure.VacantText * Measure.VacantTracking;

        Surface->TextRunCapitalised(Extent.MinimumX  + Extent.Width()  * 0.5f,
                                    Extent.MinimumY + Extent.Height() * 0.5f,
                                    Colour.VacantColour,
                                    "CREATE PANEL",
                                    Measure.VacantText,
                                    VacantTracking,
                                    true);

        return Deliver<bool>::Result(true);
    }

    // 🔴 The whole panel is the sheet's OLED ground and NOTHING else. The strip band and the footer
    //    were both retired: the dock node draws its own strip behind the tabs it lays out, so a band
    //    recorded here stood proud of it wherever the node did not reach — across the full window width
    //    while the node spanned only its own tabs — and read as a grey bar with tabs floating on it.
    // 📝 `WorkspaceMetric::FooterHeight`, `FooterEdgeWeight` and the two footer colours stay declared.
    //    They transcribe `.panelfooter` from the sheet and nothing here is authorised to delete a
    //    transcription; they are simply not recorded, because the artist asked for the band gone.
    Surface->Ground(Extent, Colour.BodyGround);

    // ⚠️ The strip extent is still reported, because the `+` and the dock space are applied against it.
    //    It measures where the node's own tab bar stands; it is no longer textured.
    StripExtent = { Extent.MinimumX,
                    Extent.MinimumY,
                    Extent.MaximumX,
                    Extent.MinimumY + Measure.StripY };

    BodyExtent = { Extent.MinimumX,
                   Extent.MinimumY + Measure.StripY,
                   Extent.MaximumX,
                   Extent.MaximumY };

    if (BodyExtent.MaximumY < BodyExtent.MinimumY)
        BodyExtent.MaximumY = BodyExtent.MinimumY;

    return Deliver<bool>::Result(true);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE READINGS
//------------------------------------------------------------------------------------------------------------------------

PlaneExtent WorkspacePanel::Body() const
{
    return BodyExtent;
}

PlaneExtent WorkspacePanel::Strip() const
{
    return StripExtent;
}

void WorkspacePanel::Reset()
{
    Surface    = nullptr;
    Appearance = nullptr;
    BodyExtent  = {};
    StripExtent = {};
}

}   // namespace Slate
