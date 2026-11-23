//============================================================================================================================================
//                                                       APPEARANCESPECIFICATION.CPP
//============================================================================================================================================
// 🧩 Multiplies every declared extent by the display scale exactly once.

#include "SlateUI/Interface/AppearanceSpecification/Api/AppearanceSpecification.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE RESOLVE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

/// 🧩 Scales only members measured in display pixels.
/// note  Tracking is measured in em, TongueClipFraction is dimensionless, and DisplayScale records the factor.
///       Every newly declared metric must therefore choose explicitly whether it enters this function.
/// cost  ✔️
void ScaleLengths(MetricScale& Measure, float AppliedScale)
{
    Measure.SpacingUnit             *= AppliedScale;
    Measure.RadiusFine              *= AppliedScale;
    Measure.RadiusSmall             *= AppliedScale;
    Measure.RadiusMedium            *= AppliedScale;
    Measure.RadiusGrand             *= AppliedScale;
    Measure.TextFine                *= AppliedScale;
    Measure.TextSmall               *= AppliedScale;
    Measure.TextBody                *= AppliedScale;
    Measure.TextTitle               *= AppliedScale;
    Measure.LeadingFine             *= AppliedScale;
    Measure.LeadingSmall            *= AppliedScale;
    Measure.LeadingBody             *= AppliedScale;
    Measure.LeadingTitle            *= AppliedScale;
    Measure.WheelTravel             *= AppliedScale;
    Measure.TongueX             *= AppliedScale;
    Measure.TongueY            *= AppliedScale;
    Measure.TongueGapX          *= AppliedScale;
    Measure.TonguePadX          *= AppliedScale;
    Measure.GripX               *= AppliedScale;
    Measure.GripHeight              *= AppliedScale;
    Measure.GripStripHeight         *= AppliedScale;
    Measure.GripLiftNorth           *= AppliedScale;
    Measure.RailY              *= AppliedScale;
    Measure.SymbolChevron           *= AppliedScale;
    Measure.SymbolTongue            *= AppliedScale;
    Measure.SymbolToggle            *= AppliedScale;
    Measure.SymbolVacant            *= AppliedScale;
    Measure.MedallionLattice        *= AppliedScale;
    Measure.MedallionColumn         *= AppliedScale;
    Measure.MedallionPreview        *= AppliedScale;
    Measure.LibraryXMedium      *= AppliedScale;
    Measure.LibraryXLarge       *= AppliedScale;
    Measure.PreviewXMedium      *= AppliedScale;
    Measure.PreviewXLarge       *= AppliedScale;
    Measure.LibraryPadX         *= AppliedScale;
    Measure.LibraryCaptionHeight    *= AppliedScale;
    Measure.GroupPadY          *= AppliedScale;
    Measure.GroupGapY          *= AppliedScale;
    Measure.SubjectIndentX      *= AppliedScale;
    Measure.SubjectPadTrailing      *= AppliedScale;
    Measure.SubjectStripPad         *= AppliedScale;
    Measure.ContentPad              *= AppliedScale;
    Measure.ContentPadLeading       *= AppliedScale;
    Measure.ContentHeadHeight       *= AppliedScale;
    Measure.ContentHeadPadX     *= AppliedScale;
    Measure.ContentHeadGap          *= AppliedScale;
    Measure.ContentTrailingPad      *= AppliedScale;
    Measure.ContentScrollPad        *= AppliedScale;
    Measure.EntryXLimit       *= AppliedScale;
    Measure.EntryPadX           *= AppliedScale;
    Measure.EntryPadY          *= AppliedScale;
    Measure.TogglePad               *= AppliedScale;
    Measure.ToggleGap               *= AppliedScale;
    Measure.CardGapLattice          *= AppliedScale;
    Measure.CardGapColumn           *= AppliedScale;
    Measure.CardPadColumn           *= AppliedScale;
    Measure.CardGapColumnInner      *= AppliedScale;
    Measure.CardScrimHeight         *= AppliedScale;
    Measure.CardMetaGap             *= AppliedScale;
    Measure.CardMetaLift            *= AppliedScale;
    Measure.CardMetaDot             *= AppliedScale;
    Measure.PreviewGap              *= AppliedScale;
    Measure.PreviewPad              *= AppliedScale;
    Measure.PreviewBoxFloor         *= AppliedScale;
    Measure.PreviewBoxLimit       *= AppliedScale;
    Measure.SkeletonGapUpper        *= AppliedScale;
    Measure.SkeletonGapLower        *= AppliedScale;
    Measure.SkeletonLeading         *= AppliedScale;
    Measure.BreakpointSmall         *= AppliedScale;
    Measure.BreakpointMedium        *= AppliedScale;
    Measure.BreakpointLarge         *= AppliedScale;
}

/// 🧩 Scales only the control members measured in display pixels.
/// note  🔴 Four members are deliberately absent — ReadoutTracking is em, MagnitudeLimit is a domain bound,
///       RulerDegreesPerPixel is a rate, and TickReach is a count. Multiplying the domain bound would move the
///       slider's own maximum with the display, and multiplying the rate would make the ruler turn at a
///       different speed on a second monitor.
/// note  Point sizes are scaled here and floored afterwards, never floored here — the floor must be applied
///       once, against the finished product, and not once per member per factor.
/// cost  ✔️
void ScaleControlLengths(ControlMetric& Measure, float AppliedScale)
{
    Measure.ColumnX          *= AppliedScale;
    Measure.CardGapY        *= AppliedScale;
    Measure.CardPad              *= AppliedScale;
    Measure.CardRowGap           *= AppliedScale;
    Measure.CardRadius           *= AppliedScale;
    Measure.CardEdgeWeight       *= AppliedScale;
    Measure.PagePad              *= AppliedScale;
    Measure.PagePadY        *= AppliedScale;

    Measure.LabelText            *= AppliedScale;
    Measure.RowText              *= AppliedScale;
    Measure.ReadoutText          *= AppliedScale;
    Measure.UnitText             *= AppliedScale;
    Measure.TickCaptionText      *= AppliedScale;
    Measure.TooltipTitleText     *= AppliedScale;
    Measure.TooltipBodyText      *= AppliedScale;
    Measure.TooltipBodyLeading   *= AppliedScale;

    Measure.LabelX           *= AppliedScale;
    Measure.RowGapX          *= AppliedScale;

    Measure.FieldHeight          *= AppliedScale;
    Measure.FieldPadX        *= AppliedScale;
    Measure.ChevronCellX     *= AppliedScale;
    Measure.ChevronSymbol        *= AppliedScale;
    Measure.MenuLift             *= AppliedScale;
    Measure.MenuRadius           *= AppliedScale;
    Measure.MenuPad              *= AppliedScale;
    Measure.MenuGapY        *= AppliedScale;
    Measure.OptionPadX       *= AppliedScale;
    Measure.OptionPadY      *= AppliedScale;

    Measure.ReadoutX         *= AppliedScale;
    Measure.UnitCellX        *= AppliedScale;
    Measure.SliderX          *= AppliedScale;
    Measure.SliderHeight         *= AppliedScale;
    Measure.ThumbExtent          *= AppliedScale;

    Measure.RulerHeight          *= AppliedScale;
    Measure.RulerRadius          *= AppliedScale;
    Measure.TickSpacing          *= AppliedScale;
    Measure.TickWeight           *= AppliedScale;
    Measure.TickMajorHeight      *= AppliedScale;
    Measure.TickMediumHeight     *= AppliedScale;
    Measure.TickMinorHeight      *= AppliedScale;
    Measure.TickCaptionLift      *= AppliedScale;
    Measure.PointerWeight        *= AppliedScale;
    Measure.PointerY        *= AppliedScale;
    Measure.PointerDot           *= AppliedScale;
    Measure.PointerDotLift       *= AppliedScale;

    Measure.WellInset              *= AppliedScale;
    Measure.WellRadius           *= AppliedScale;
    Measure.WellGapY        *= AppliedScale;
    Measure.ToggleRowHeight      *= AppliedScale;
    Measure.ToggleRowPadX    *= AppliedScale;
    Measure.ToggleGapX       *= AppliedScale;
    Measure.RingExtent           *= AppliedScale;
    Measure.RingWeight           *= AppliedScale;
    Measure.RingDotExtent        *= AppliedScale;

    Measure.SubsetRowHeight      *= AppliedScale;
    Measure.SubsetRowPadX    *= AppliedScale;
    Measure.SubsetRailX      *= AppliedScale;

    Measure.StopStripHeight      *= AppliedScale;
    Measure.StopStripPadLeading  *= AppliedScale;
    Measure.StopStripPadTrailing *= AppliedScale;
    Measure.StopQuietExtent      *= AppliedScale;
    Measure.StopTakenExtent      *= AppliedScale;

    Measure.TooltipX         *= AppliedScale;
    Measure.TooltipPad           *= AppliedScale;
    Measure.TooltipRadius        *= AppliedScale;
    Measure.TooltipLift          *= AppliedScale;
    Measure.TooltipTitleGap      *= AppliedScale;
    Measure.TooltipArrowExtent   *= AppliedScale;
    Measure.TooltipArrowRadius   *= AppliedScale;
    Measure.TooltipArrowX    *= AppliedScale;
    Measure.TooltipArrowScolour     *= AppliedScale;
    Measure.TriggerExtent        *= AppliedScale;
    Measure.TriggerRadius        *= AppliedScale;
    Measure.TriggerLeadX     *= AppliedScale;
    Measure.TriggerSymbol        *= AppliedScale;
    Measure.TooltipWellInset       *= AppliedScale;
    Measure.TooltipWellRadius    *= AppliedScale;
    Measure.TooltipWellFloor     *= AppliedScale;
    Measure.TooltipWellGap       *= AppliedScale;
}

/// 🧩 Raises every recorded point size to the legibility floor, after the whole product has been applied.
/// note  📐 The floor is applied to the eight point sizes and to the one leading that follows a point size.
///       TooltipBodyLeading is raised in the same proportion its run was, so a floored run keeps the line
///       spacing the sheet declared rather than overlapping the line beneath it.
/// cost  ✔️
void FloorRuns(ControlMetric& Measure)
{
    const float BodyBeforeFloor = Measure.TooltipBodyText;

    if (Measure.LabelText        < TextLegibilityFloor) Measure.LabelText        = TextLegibilityFloor;
    if (Measure.RowText          < TextLegibilityFloor) Measure.RowText          = TextLegibilityFloor;
    if (Measure.ReadoutText      < TextLegibilityFloor) Measure.ReadoutText      = TextLegibilityFloor;
    if (Measure.UnitText         < TextLegibilityFloor) Measure.UnitText         = TextLegibilityFloor;
    if (Measure.TickCaptionText  < TextLegibilityFloor) Measure.TickCaptionText  = TextLegibilityFloor;
    if (Measure.TooltipTitleText < TextLegibilityFloor) Measure.TooltipTitleText = TextLegibilityFloor;
    if (Measure.TooltipBodyText  < TextLegibilityFloor) Measure.TooltipBodyText  = TextLegibilityFloor;

    if (BodyBeforeFloor > 0.0f && Measure.TooltipBodyText > BodyBeforeFloor)
    {
        Measure.TooltipBodyLeading *= Measure.TooltipBodyText / BodyBeforeFloor;
    }
}

/// 🧩 Scales the workspace tab figures. Every member is a length; none is a fraction or a count.
/// note  The tab strip is authored at the engine's own density and not at the control sheet's 2x, so it
///       takes the display and artist factors alone and never AuthoredReduction. A 24 px tab halved is
///       twelve pixels, which no run fits inside.
/// cost  ✔️
void ScaleWorkspaceLengths(WorkspaceMetric& Measure, float AppliedScale)
{
    Measure.TabY        *= AppliedScale;
    Measure.TabSlant         *= AppliedScale;
    Measure.TabOverlap       *= AppliedScale;
    Measure.TabPadX      *= AppliedScale;
    Measure.TabXFloor    *= AppliedScale;
    Measure.TabXLimit  *= AppliedScale;
    Measure.TabRadius        *= AppliedScale;
    Measure.TabEdgeWeight    *= AppliedScale;
    Measure.StripY      *= AppliedScale;
    Measure.StripPadTop      *= AppliedScale;
    Measure.FooterHeight     *= AppliedScale;
    Measure.FooterEdgeWeight *= AppliedScale;
    Measure.TabText          *= AppliedScale;
    Measure.VacantText       *= AppliedScale;

    if (Measure.TabText < TextLegibilityFloor)
        Measure.TabText = TextLegibilityFloor;

    // 📝 The placeholder run is held to the same floor. It is the only text on an empty workspace, so a
    //    scale that shrank it below legibility would leave the artist an unreadable panel and no other cue.
    if (Measure.VacantText < TextLegibilityFloor)
        Measure.VacantText = TextLegibilityFloor;

    // 📝 🔴 `VacantTracking` is NOT scaled. It is stated in em against the text size, so it follows the
    //    scaled size on its own; multiplying it here would apply the scale to it a second time.
}

/// 🧩 Scales the editor-panel chrome transcribed from `References/Panels`.
/// cost  ✔️
void ScaleEditorPanelLengths(EditorPanelMetric& Measure, float AppliedScale)
{
    Measure.HeaderHeight        *= AppliedScale;
    Measure.FooterHeight        *= AppliedScale;
    Measure.SplitterHeight      *= AppliedScale;
    Measure.EdgeWeight          *= AppliedScale;
    Measure.HeaderPadX      *= AppliedScale;
    Measure.FooterPadX      *= AppliedScale;
    Measure.HeaderAction        *= AppliedScale;
    Measure.HeaderSymbol        *= AppliedScale;
    Measure.HeaderTitleGap      *= AppliedScale;
    Measure.FooterGap           *= AppliedScale;
    Measure.PillY          *= AppliedScale;
    Measure.PillRadius          *= AppliedScale;
    Measure.MenuX           *= AppliedScale;
    Measure.SplitMenuX      *= AppliedScale;
    Measure.MenuPadY       *= AppliedScale;
    Measure.MenuRowHeight       *= AppliedScale;
    Measure.MenuRadius          *= AppliedScale;
    Measure.MenuLift            *= AppliedScale;
    Measure.ChooserButtonX  *= AppliedScale;
    Measure.ChooserButtonHeight *= AppliedScale;
    Measure.ChooserGap          *= AppliedScale;
    Measure.ChooserRadius       *= AppliedScale;
    Measure.TextFine            *= AppliedScale;
    Measure.TextSmall           *= AppliedScale;
    Measure.TextBody            *= AppliedScale;

    if (Measure.TextFine < TextLegibilityFloor)  Measure.TextFine = TextLegibilityFloor;
    if (Measure.TextSmall < TextLegibilityFloor) Measure.TextSmall = TextLegibilityFloor;
    if (Measure.TextBody < TextLegibilityFloor)  Measure.TextBody = TextLegibilityFloor;
}

}   // namespace

ComfortDensity ClassifyDensity(const MetricScale& Measure, float Width)
{
    if (Width <= 0.0f)
        return ComfortDensity::Regular;

    if (Width >= Measure.BreakpointLarge * 2.5f)
        return ComfortDensity::Expansive;

    if (Width >= Measure.BreakpointLarge * 1.875f)
        return ComfortDensity::Spacious;

    if (Width >= Measure.BreakpointLarge)
        return ComfortDensity::Regular;

    return ComfortDensity::Compact;
}

ThemeProfile Resolve(double DisplayScale, double ArtistScale, float Width)
{
    ThemeProfile Resolved;

    const float AppliedScale = (DisplayScale > 0.0) ? static_cast<float>(DisplayScale) : 1.0f;

    // 📝 The preference is clamped and never rejected — a preference outside the bounds is a settings file to
    //    survive, not a reason to bring the interface up at an extent nothing can be read at.
    const double Preferred    = (ArtistScale < ArtistScaleFloor)   ? ArtistScaleFloor
                              : (ArtistScale > ArtistScaleLimit) ? ArtistScaleLimit
                                                                   : ArtistScale;
    const float  ArtistFactor = static_cast<float>(Preferred);

    ScaleLengths(Resolved.Measure, AppliedScale);
    Resolved.Measure.DisplayScale = AppliedScale;

    // 🔴 The density is classified against breakpoints that have already been scaled, so the extent it reads
    //    and the thresholds it compares against are in the same units. Classifying first would compare a
    //    display-pixel extent against declared-pixel thresholds and step a density early on every dense panel.
    const ComfortDensity Classified   = ClassifyDensity(Resolved.Measure, Width);
    const float          ControlScale = AuthoredReduction * DensityFactor(Classified) * AppliedScale * ArtistFactor;

    ScaleControlLengths(Resolved.ControlMeasure, ControlScale);
    FloorRuns(Resolved.ControlMeasure);

    // 📝 The workspace strip is authored at engine density, so it takes the display and artist factors and
    //    not the control sheet's reduction. Its own density factor still applies: a tab strip on a 4K panel
    //    wants the same easing outward that every other extent gets.
    const float InterfaceScale = DensityFactor(Classified) * AppliedScale * ArtistFactor;
    ScaleWorkspaceLengths(Resolved.WorkspaceMeasure, InterfaceScale);
    ScaleEditorPanelLengths(Resolved.EditorPanelMeasure, InterfaceScale);

    Resolved.ControlMeasure.Density       = Classified;
    Resolved.ControlMeasure.AppliedFactor = ControlScale;
    Resolved.ControlMeasure.ArtistFactor  = ArtistFactor;

    // 📝 🔴 The three snap rates are the only figures outside `MetricScale` carrying a length, and they are
    //    scaled explicitly here rather than registered with its pixel measurements. Scaling the whole motion
    //    declaration would also multiply its fractions and elasticity, changing drawer arbitration.
    Resolved.Motion.SnapRateSoft *= static_cast<double>(AppliedScale);
    Resolved.Motion.SnapRateFirm *= static_cast<double>(AppliedScale);
    Resolved.Motion.SnapRateHard *= static_cast<double>(AppliedScale);

    return Resolved;
}

void ApplyUserScale(ThemeProfile& Profile, float TextScale, float CornerScale)
{
    const float Text = (TextScale < 0.75f) ? 0.75f : ((TextScale > 1.50f) ? 1.50f : TextScale);
    const float Corners = (CornerScale < 0.50f) ? 0.50f : ((CornerScale > 1.50f) ? 1.50f : CornerScale);

    Profile.TextScale = Text;
    Profile.CornerScale = Corners;

    Profile.Typography.Display *= Text;
    Profile.Typography.Title *= Text;
    Profile.Typography.Heading *= Text;
    Profile.Typography.Body *= Text;
    Profile.Typography.Label *= Text;
    Profile.Typography.Caption *= Text;
    Profile.Typography.Small *= Text;
    Profile.Typography.Tab *= Text;

    Profile.Corners.Small *= Corners;
    Profile.Corners.Medium *= Corners;
    Profile.Corners.Large *= Corners;
}

void ApplyFontWeights(ThemeProfile& Profile, const std::uint32_t (&Weights)[8])
{
    Profile.Fonts.Title = static_cast<FontWeight>(Weights[0]);
    Profile.Fonts.Heading = static_cast<FontWeight>(Weights[1]);
    Profile.Fonts.Body = static_cast<FontWeight>(Weights[3]);
    Profile.Fonts.Label = static_cast<FontWeight>(Weights[4]);
    Profile.Fonts.Caption = static_cast<FontWeight>(Weights[5]);
    Profile.Fonts.Tab = static_cast<FontWeight>(Weights[2]);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RESPONSIVE ARRANGEMENT
//------------------------------------------------------------------------------------------------------------------------

// 📝 🔴 The source's breakpoints are evaluated against the **viewport**, not against the content column. Slate
//    has no viewport media query, so they are evaluated against the extent the lattice actually occupies. At
//    the source's own proportions the two agree; a panel torn off into its own window is where they part, and
//    the content-relative reading is the one that stays correct there.
std::uint32_t LatticeColumns(const MetricScale& Measure, float ContentX)
{
    if (ContentX >= Measure.BreakpointLarge)
        return 5u;

    if (ContentX >= Measure.BreakpointMedium)
        return 4u;

    if (ContentX >= Measure.BreakpointSmall)
        return 3u;

    return 2u;
}

}   // namespace Slate
