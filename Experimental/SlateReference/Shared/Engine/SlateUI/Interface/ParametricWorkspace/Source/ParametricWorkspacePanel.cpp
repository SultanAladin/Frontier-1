//============================================================================================================================================
//                                                      PARAMETRICWORKSPACEPANEL.CPP
//============================================================================================================================================

#include "SlateUI/Interface/ParametricWorkspace/Api/ParametricWorkspacePanel.h"

#include <algorithm>
#include <cstdio>

namespace Slate
{

namespace
{

constexpr double HoverOver = 120.0;
constexpr float RunLeading = 1.30f;

constexpr ThemeToken Faded(ThemeToken Declared, float Fraction)
{
    const float Held = Fraction < 0.0f ? 0.0f : (Fraction > 1.0f ? 1.0f : Fraction);
    Declared.Opacity = static_cast<std::uint8_t>(static_cast<float>(Declared.Opacity) * Held + 0.5f);
    return Declared;
}

const ThemeToken ParametricFacetColours[ParametricFacetCount] =
{
    ParametricCategoryHue(ParametricCategory::Sketch),
    ParametricCategoryHue(ParametricCategory::Geometry),
    ParametricCategoryHue(ParametricCategory::Annotation),
    ParametricCategoryHue(ParametricCategory::Operation)
};

} // namespace

Deliver<bool> ParametricWorkspacePanel::ConstructParametricWorkspacePanel(ControlIndex& IncomingInteraction,
                                                                          MotionIntegrator& Integrator,
                                                                          RecordingSurface& IncomingSurface,
                                                                          const ThemeProfile& Resolved)
{
    if (Interaction != nullptr)
    {
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the parametric workspace panel is already constructed" });
    }

    Interaction = &IncomingInteraction;
    Motion = &Integrator;
    Surface = &IncomingSurface;
    Appearance = &Resolved;

    if (!Controls.ConstructControlPanel(IncomingInteraction, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the shared CAD controls were rejected" });
    }

    if (!Facets.ConstructFacetPanel(Integrator, IncomingSurface, Resolved).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the CAD facet controls were rejected" });
    }

    ControlIdentity* const Fixed[] = { &SearchField, &InspectorStrip, &DirectoryCall, &InspectCall };
    for (ControlIdentity* Identity : Fixed)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
        {
            Reset();
            return Deliver<bool>::Refuse(Registered.Error);
        }
        *Identity = Registered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < ParametricWorkspaceContext::RowLimit; ++Index)
    {
        ControlIdentity* const RowControls[] = { &RowContacts[Index], &RowDisclosures[Index] };
        for (ControlIdentity* Identity : RowControls)
        {
            const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
            if (!Registered.Resolved)
            {
                Reset();
                return Deliver<bool>::Refuse(Registered.Error);
            }
            *Identity = Registered.Resolve();
        }
    }

    if (!OutlinePages.ConstructSlidingPages(Integrator, 0u).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the CAD page travel was rejected" });
    }

    Reapply(Resolved);
    return Deliver<bool>::Result(true);
}

void ParametricWorkspacePanel::Advance(const PointerCondition& Contact, double Elapsed,
                                       ParametricWorkspaceContext& Applied, bool TabPressed,
                                       const ModifierCondition& Modifiers)
{
    Sampled = Contact;
    Modified = Modifiers;
    Controls.Advance(Contact, Elapsed);
    Facets.Advance(Contact, Elapsed);

    const std::uint32_t PriorPage = Applied.OutlinePage;
    if (TabPressed)
        Applied.OutlinePage = Applied.OutlinePage == 0u ? 1u : 0u;
    if (Applied.OutlinePage != PriorPage)
    {
        Interaction->Withdraw();
        Interaction->Abandon();
    }

    Applied.SearchTaken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);
}

void ParametricWorkspacePanel::Reapply(const ThemeProfile& Resolved)
{
    Appearance = &Resolved;
    Tinted = Resolved.Shell;

    const float Applied = static_cast<float>(Resolved.Measure.DisplayScale)
                        * Resolved.ControlMeasure.ArtistFactor;
    Scaled = ScaleShellLengths(Applied);
}

void ParametricWorkspacePanel::Reset()
{
    Controls.Reset();
    Facets.Reset();
    OutlineOverflow.Reset();
    InspectorOverflow.Reset();
    OutlinePages.Reset();

    Interaction = nullptr;
    Motion = nullptr;
    Surface = nullptr;
    Appearance = nullptr;
    Sampled = {};
    Modified = {};
    Tinted = {};
    Scaled = {};
}

void ParametricWorkspacePanel::RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                                                const ThemeToken& Hue, const char* Titled,
                                                const char* Secondary)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MaximumY - 1.0f, Extent.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    const float Pad = Scaled.HeaderPadX;
    const float Medallion = Scaled.MedallionExtent;
    const PlaneExtent Crest = Spanning(Extent.MinimumX + Pad,
                                       Extent.MinimumY + (Extent.Height() - Medallion) * 0.5f,
                                       Medallion, Medallion);

    Surface->Ground(Crest, Hue, 6.0f, CornerAll);
    const float Figure = Medallion * 0.62f;
    Surface->Stroke(Glyph,
                    Spanning(Crest.MinimumX + (Medallion - Figure) * 0.5f,
                             Crest.MinimumY + (Medallion - Figure) * 0.5f,
                             Figure, Figure),
                    Covering(0xFFFFFFu));

    const bool HasSecondary = Secondary != nullptr && Secondary[0] != '\0';
    const float PrimaryRun = Scaled.RunPrimary;
    const float SecondaryRun = Scaled.RunFine;
    const float PairHeight = HasSecondary ? (PrimaryRun * RunLeading + SecondaryRun * RunLeading)
                                          : PrimaryRun;
    const float PairLead = Extent.MinimumY + (Extent.Height() - PairHeight) * 0.5f;
    const float RunLead = Crest.MaximumX + Pad;

    Surface->TextRunTruncated(RunLead, PairLead, Extent.MaximumX - RunLead - Pad,
                              Tinted.Primary, Titled, PrimaryRun, true);
    if (HasSecondary)
    {
        Surface->TextRunTruncated(RunLead, PairLead + PrimaryRun * RunLeading,
                                  Extent.MaximumX - RunLead - Pad,
                                  Hue, Secondary, SecondaryRun);
    }
}

void ParametricWorkspacePanel::RecordSearchField(const PlaneExtent& Extent,
                                                 ParametricWorkspaceContext& Applied)
{
    const bool Hovered = Extent.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (Hovered && Sampled.ContactPressed && !Interaction->AnyDisclosed())
        Interaction->Grab(SearchField, ControlPart::Body);

    Interaction->DeclareHovered(SearchField, Hovered, HoverOver);

    const bool Taken = Interaction->Holding(SearchField) || Interaction->Disclosed(SearchField);
    const float Radius = Extent.Height() * 0.5f;

    Surface->Ground(Extent, Tinted.MenuLower, Radius, CornerAll);
    Surface->Edge(Extent, Taken ? Tinted.Primary : Tinted.Hairline,
                  1.0f, Radius, CornerAll);

    const float GlyphExtent = 14.0f;
    const float GlyphLead = Extent.MinimumX + 10.0f;
    const float GlyphTop = Extent.MinimumY + (Extent.Height() - GlyphExtent) * 0.5f;
    Surface->Stroke(SymbolSubject::MagnifierLens,
                    Spanning(GlyphLead, GlyphTop, GlyphExtent, GlyphExtent), Tinted.Faint);

    const float RunLead = GlyphLead + GlyphExtent + 8.0f;
    const float FieldRun = Scaled.RunSecondary * (12.0f / 11.5f);
    const float RunTop = Extent.MinimumY + (Extent.Height() - FieldRun) * 0.5f;
    const bool Empty = Applied.RowRetention[0] == '\0';

    Surface->TextRunTruncated(RunLead, RunTop, Extent.MaximumX - RunLead - 8.0f,
                              Empty ? Tinted.Faint : Tinted.Primary,
                              Empty ? "Filter CAD records..." : Applied.RowRetention,
                              FieldRun);
}

void ParametricWorkspacePanel::RecordDirectoryPage(const PlaneExtent& Extent,
                                                   ParametricWorkspaceContext& Applied,
                                                   const ParametricDirectoryRow* Rows,
                                                   std::uint32_t RowCount,
                                                   const ParametricPropertyPresentation* Property)
{
    if (Rows == nullptr)
        RowCount = 0u;
    RowCount = std::min<std::uint32_t>(RowCount, ParametricWorkspaceContext::RowLimit);

    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);

    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, SymbolSubject::SketchPlane,
                     ParametricCategoryHue(ParametricCategory::Sketch),
                     "Parametric Directory", "Committed CAD records");

    const float Pad = Scaled.PanePad;
    const PlaneExtent Search = Spanning(Extent.MinimumX + Pad, Header.MaximumY + Pad,
                                        Extent.Width() - Pad * 2.0f, Scaled.SearchHeight);
    RecordSearchField(Search, Applied);

    const FacetDeclaration Declared =
    {
        "CAD Filters",
        ParametricFacetNames,
        ParametricFacetColours,
        ParametricFacetCount,
        0xFFFFFFFFu
    };

    const float FacetY = Facets.MeasureHeight(Extent.Width() - Pad * 2.0f,
                                              Declared, Applied.FacetEnabled);
    const PlaneExtent FacetCard = Spanning(Extent.MinimumX + Pad, Search.MaximumY + Pad,
                                           Extent.Width() - Pad * 2.0f, FacetY);
    Discard(Facets.Record(FacetCard, Declared, Applied.FacetEnabled));

    const PlaneExtent Footer = Spanning(Extent.MinimumX,
                                        Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);
    const PlaneExtent Body = Spanning(Extent.MinimumX + Pad, FacetCard.MaximumY + Pad,
                                      Extent.Width() - Pad * 2.0f,
                                      Footer.MinimumY - FacetCard.MaximumY - Pad);

    bool Presented[ParametricWorkspaceContext::RowLimit] = {};
    bool Retained[ParametricWorkspaceContext::RowLimit] = {};
    float PresentedFraction[ParametricWorkspaceContext::RowLimit] = {};
    float Expansion[ParametricWorkspaceContext::RowLimit] = {};
    std::uint32_t Parents[ParametricWorkspaceContext::RowLimit] = {};

    const bool Filtering = ParametricRetentionActive(Applied);
    float ContentHeight = 0.0f;

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        Parents[Index] = Rows[Index].Enclosing;
        Retained[Index] = ParametricRowRetained(Applied, Rows[Index]);
        const bool Branch = Rows[Index].EnclosedCount > 0u;
        const bool Expanded = Rows[Index].Subject == ParametricRowSubject::CategoryRoot
                            ? true : Applied.RowExpanded[Index];
        Expansion[Index] = Branch ? Controls.OutlineExpansion(RowDisclosures[Index], Filtering ? true : Expanded, true)
                                  : 1.0f;
    }

    VisibleTree::Resolve(Parents, Expansion, Retained, RowCount, Filtering, true,
                         Presented, PresentedFraction);

    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
        if (Presented[Index])
            ContentHeight += Scaled.RowHeight * PresentedFraction[Index];

    const float Scroll = Body.Height() > 0.0f
                       ? OutlineOverflow.Advance(Sampled, Body, ContentHeight + Pad) : 0.0f;

    Surface->Confine(Body);

    float Sweep = Body.MinimumY - Scroll;
    for (std::uint32_t Index = 0u; Index < RowCount; ++Index)
    {
        if (!Presented[Index])
            continue;

        const ParametricDirectoryRow& Current = Rows[Index];
        const float Occupancy = PresentedFraction[Index];
        const PlaneExtent Row = Spanning(Body.MinimumX, Sweep, Body.Width(), Scaled.RowHeight);
        const PlaneExtent RowClip = Spanning(Body.MinimumX, Sweep, Body.Width(), Scaled.RowHeight * Occupancy);
        Sweep += RowClip.Height();

        if (Surface->Excluded(RowClip))
            continue;

        Surface->Confine(RowClip);

        const bool Selected = Applied.RowSelected[Index];
        const bool Hovered = RowClip.Encloses(Sampled.PositionX, Sampled.PositionY);
        const bool Branch = Current.EnclosedCount > 0u;
        const bool InteractiveDisclosure = Branch && Current.Subject != ParametricRowSubject::CategoryRoot;

        const float LeadX = Row.MinimumX + Scaled.RowLeadX + static_cast<float>(Current.Depth) * Scaled.RowStepX;
        const PlaneExtent Chevron = Spanning(LeadX,
                                             Row.MinimumY + (Row.Height() - Scaled.ChevronExtent) * 0.5f,
                                             Scaled.ChevronExtent, Scaled.ChevronExtent);
        const bool OnChevron = InteractiveDisclosure && Chevron.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Sampled.ContactPressed && !Interaction->AnyDisclosed())
        {
            if (OnChevron)
                Interaction->Grab(RowDisclosures[Index], ControlPart::Chevron);
            else if (Hovered)
                Interaction->Grab(RowContacts[Index], ControlPart::Body);
        }

        if (OnChevron && Interaction->Released(RowDisclosures[Index]))
            Applied.RowExpanded[Index] = !Applied.RowExpanded[Index];

        if (Hovered && !OnChevron && Interaction->Released(RowContacts[Index]))
        {
            SelectionSet::Apply(Applied.RowSelected, RowCount, Applied.RowSelectionAnchor,
                                Index, Presented,
                                SelectionGesture{ Modified.Shifted, Modified.Commanded });
            Applied.RowTaken = SelectionSet::Primary(Applied.RowSelected, RowCount, Index);
        }

        if (Hovered && Sampled.ContactDoublePressed && Current.Subject != ParametricRowSubject::CategoryRoot)
        {
            SelectionSet::Apply(Applied.RowSelected, RowCount, Applied.RowSelectionAnchor,
                                Index, Presented, SelectionGesture{});
            Applied.RowTaken = SelectionSet::Primary(Applied.RowSelected, RowCount, Index);
            Applied.OutlinePage = 1u;
        }

        Interaction->DeclareHovered(RowContacts[Index], Hovered, HoverOver);

        if (Selected)
            Surface->Ground(Row, Tinted.EntityTaken, Scaled.FieldRadius, CornerAll);
        else if (Hovered)
            Surface->Ground(Row, Tinted.RowHovered, Scaled.FieldRadius, CornerAll);

        if (Selected)
        {
            const PlaneExtent Rail = Spanning(Row.MinimumX - Scaled.RailOffsetX,
                                              Row.MinimumY + (Row.Height() - Scaled.RailY) * 0.5f,
                                              Scaled.RailX, Scaled.RailY);
            Surface->Ground(Rail, Tinted.EntityAccent, 2.0f,
                            CornerTrailingUpper | CornerTrailingLower);
        }

        if (Branch)
        {
            const bool Expanded = Filtering || Current.Subject == ParametricRowSubject::CategoryRoot || Applied.RowExpanded[Index];
            Surface->Stroke(Expanded ? SymbolSubject::ChevronDown : SymbolSubject::ChevronRight,
                            Chevron, Tinted.Faint);
        }

        const float GlyphLead = LeadX + Scaled.ChevronExtent + Scaled.PanePad;
        const PlaneExtent Glyph = Spanning(GlyphLead,
                                           Row.MinimumY + (Row.Height() - Scaled.GlyphExtent) * 0.5f,
                                           Scaled.GlyphExtent, Scaled.GlyphExtent);
        Surface->Stroke(ParametricRowGlyph(Current.Subject), Glyph,
                        ParametricRowHue(Current.Subject, Current.Category));

        float NamingLimit = Row.MaximumX - Scaled.PanePad;
        if (Current.Locked)
        {
            const PlaneExtent Lock = Spanning(NamingLimit - 14.0f,
                                              Row.MinimumY + (Row.Height() - 14.0f) * 0.5f,
                                              14.0f, 14.0f);
            Surface->Stroke(SymbolSubject::LockClosed, Lock, Tinted.Faint);
            NamingLimit = Lock.MinimumX - 6.0f;
        }
        if (!Current.Visible)
        {
            const PlaneExtent Eye = Spanning(NamingLimit - 14.0f,
                                             Row.MinimumY + (Row.Height() - 14.0f) * 0.5f,
                                             14.0f, 14.0f);
            Surface->Stroke(SymbolSubject::EyeClosed, Eye, Tinted.Faint);
            NamingLimit = Eye.MinimumX - 6.0f;
        }
        if (Branch)
        {
            char Counted[12] = {};
            std::snprintf(Counted, sizeof(Counted), "%u", static_cast<unsigned>(Current.EnclosedCount));
            const float CountRun = Scaled.RunFine;
            const float CountWidth = Surface->MeasureRun(Counted, CountRun, 0.0f);
            Surface->TextRun(NamingLimit - CountWidth,
                             Row.MinimumY + (Row.Height() - CountRun) * 0.5f,
                             Tinted.Faint, Counted, CountRun);
            NamingLimit -= CountWidth + 6.0f;
        }

        const float NamingLead = Glyph.MaximumX + Scaled.PanePad;
        Surface->TextRunTruncated(NamingLead,
                                  Row.MinimumY + (Row.Height() - Scaled.RunPrimary) * 0.5f,
                                  NamingLimit - NamingLead,
                                  Selected ? Tinted.Primary : (Hovered ? Tinted.Primary : Tinted.Muted),
                                  Current.Naming, Scaled.RunPrimary);

        Surface->Release();
    }

    if (Filtering && ContentHeight <= 0.5f)
    {
        const char* Prose = "No CAD records match the search or filters.";
        Surface->TextRun(Body.MinimumX + (Body.Width() - Surface->MeasureRun(Prose, Scaled.RunSecondary, 0.0f)) * 0.5f,
                         Body.MinimumY + Pad * 2.0f, Tinted.Faint, Prose, Scaled.RunSecondary);
    }

    Surface->Release();

    const PlaneExtent Thumb = OutlineOverflow.Thumb(Body, ContentHeight + Pad);
    if (Thumb.Height() > 0.0f)
        Surface->Ground(Thumb, Tinted.HairlineFirm, 1.5f, CornerAll);

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    char Counted[24] = {};
    std::snprintf(Counted, sizeof(Counted), "%u", static_cast<unsigned>(RowCount));
    const float FooterTop = Footer.MinimumY + (Footer.Height() - Scaled.RunFine) * 0.5f;
    const float FooterLead = Footer.MinimumX + Scaled.HeaderPadX;
    Surface->TextRun(FooterLead, FooterTop, Tinted.Primary, Counted, Scaled.RunFine, 0.0f, true);
    Surface->TextRun(FooterLead + Surface->MeasureRun(Counted, Scaled.RunFine, 0.0f) + 4.0f,
                     FooterTop, Tinted.Muted, " rows", Scaled.RunFine);

    if (Property != nullptr && Property->Naming != nullptr && Property->Naming[0] != '\0')
    {
        const char* Caption = "Inspect";
        const float Run = Scaled.RunFine;
        const float Width = 60.0f;
        const PlaneExtent Call = Spanning(Footer.MaximumX - Scaled.HeaderPadX - Width,
                                          Footer.MinimumY + 3.0f, Width, Footer.Height() - 6.0f);
        const bool Hovered = Call.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Hovered)
            Interaction->Grab(InspectCall, ControlPart::Body);
        if (Hovered && Interaction->Released(InspectCall))
            Applied.OutlinePage = 1u;
        Interaction->DeclareHovered(InspectCall, Hovered, HoverOver);
        Surface->Ground(Call, Hovered ? Tinted.TileHovered : Tinted.Tile, 9.0f, CornerAll);
        Surface->Edge(Call, Tinted.HairlineFirm, 1.0f, 9.0f, CornerAll);
        const float CaptionWidth = Surface->MeasureRun(Caption, Run, 0.0f);
        Surface->TextRun(Call.MinimumX + (Call.Width() - CaptionWidth) * 0.5f,
                         FooterTop, Tinted.Primary, Caption, Run);
    }

    Facets.RecordDeferred();
}

void ParametricWorkspacePanel::RecordPropertyPage(const PlaneExtent& Extent, ParametricWorkspaceContext& Applied,
                                                  const ParametricPropertyPresentation& Property,
                                                  float ScrollOffset)
{
    const float Pad = Scaled.PanePad * 1.5f;
    float Sweep = Extent.MinimumY + Pad - ScrollOffset;

    const float HeroHeight = Scaled.HeroCrest + Scaled.HeroPad * 2.0f;
    const PlaneExtent Hero = Spanning(Extent.MinimumX + Pad, Sweep,
                                      Extent.Width() - Pad * 2.0f, HeroHeight);
    const ThemeToken Hue = ParametricRowHue(Property.Subject, Property.Category);

    Surface->Ground(Hero, Tinted.Tile, Scaled.CardRadius, CornerAll);
    Surface->Edge(Hero, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

    const PlaneExtent Crest = Spanning(Hero.MinimumX + Scaled.HeroPad,
                                       Hero.MinimumY + Scaled.HeroPad,
                                       Scaled.HeroCrest, Scaled.HeroCrest);
    Surface->Ground(Crest, Hue, 8.0f, CornerAll);

    const float Figure = Scaled.HeroCrest * 0.55f;
    Surface->Stroke(ParametricRowGlyph(Property.Subject),
                    Spanning(Crest.MinimumX + (Scaled.HeroCrest - Figure) * 0.5f,
                             Crest.MinimumY + (Scaled.HeroCrest - Figure) * 0.5f,
                             Figure, Figure), Covering(0xFFFFFFu));

    const float NameLead = Crest.MaximumX + Scaled.HeroPad;
    Surface->TextRunTruncated(NameLead,
                              Hero.MinimumY + Scaled.HeroPad * 0.9f,
                              Hero.MaximumX - NameLead - Scaled.HeroPad,
                              Tinted.Primary, Property.Naming, Scaled.RunPrimary, true);
    Surface->TextRun(Crest.MaximumX + Scaled.HeroPad,
                     Hero.MinimumY + Scaled.HeroPad * 0.9f + Scaled.RunPrimary * RunLeading,
                     Hue, Property.Secondary, Scaled.RunFine);

    Sweep = Hero.MaximumY + Pad;

    if (Property.ClosedSemantic)
    {
        const PlaneExtent ToggleCard = Spanning(Extent.MinimumX + Pad, Sweep,
                                                Extent.Width() - Pad * 2.0f, Scaled.ComponentY + Pad * 2.0f);
        const bool Hovered = ToggleCard.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Hovered)
        {
            Applied.ExtrusionCapToggleDemand = true;
            Applied.ExtrusionCapToggleIdentity = Property.Identity;
        }
        Surface->Ground(ToggleCard, Hovered ? Tinted.TileHovered : Tinted.Desk, Scaled.CardRadius, CornerAll);
        Surface->Edge(ToggleCard, Hovered ? Tinted.HairlineFirm : Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);
        Surface->TextRun(ToggleCard.MinimumX + Pad,
                         ToggleCard.MinimumY + (ToggleCard.Height() - Scaled.RunSmall) * 0.5f,
                         Tinted.Muted, "Extrude Caps", Scaled.RunSmall, 0.0f, true);
        const float SwitchW = 92.0f;
        const PlaneExtent Switch = Spanning(ToggleCard.MaximumX - Pad - SwitchW,
                                            ToggleCard.MinimumY + (ToggleCard.Height() - 24.0f) * 0.5f,
                                            SwitchW, 24.0f);
        Surface->Ground(Switch, Property.CappedExtrusionSemantic ? Hue : Tinted.Tile, 12.0f, CornerAll);
        Surface->TextRun(Switch.MinimumX + 13.0f,
                         Switch.MinimumY + (Switch.Height() - Scaled.RunFine) * 0.5f,
                         Covering(0x101014u), Property.CappedExtrusionSemantic ? "Solid" : "Walls", Scaled.RunFine, 0.0f, true);
        Surface->Medallion(Switch.MaximumX - 13.0f, Switch.MinimumY + 12.0f, 8.0f, Covering(0xFFFFFFu));
        Sweep = ToggleCard.MaximumY + Pad;
    }

    const float CardHeight = Scaled.ComponentY + Pad * 2.0f
                           + static_cast<float>(Property.FieldCount) * Scaled.StatY;
    const PlaneExtent Card = Spanning(Extent.MinimumX + Pad, Sweep,
                                      Extent.Width() - Pad * 2.0f, CardHeight);
    Surface->Ground(Card, Tinted.Desk, Scaled.CardRadius, CornerAll);
    Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

    const PlaneExtent CardHeader = Spanning(Card.MinimumX, Card.MinimumY,
                                            Card.Width(), Scaled.ComponentY);
    Surface->Ground(CardHeader, Tinted.MenuLower, Scaled.CardRadius,
                    CornerLeadingUpper | CornerTrailingUpper);
    Surface->TextRun(CardHeader.MinimumX + Scaled.HeaderPadX,
                     CardHeader.MinimumY + (CardHeader.Height() - Scaled.RunSmall) * 0.5f,
                     Tinted.Muted, "Properties", Scaled.RunSmall, 0.0f, true);

    float RowY = CardHeader.MaximumY + Pad;
    for (std::uint32_t Field = 0u; Field < Property.FieldCount; ++Field)
    {
        const PlaneExtent Row = Spanning(Card.MinimumX + Pad, RowY,
                                         Card.Width() - Pad * 2.0f, Scaled.StatY);
        Surface->TextRun(Row.MinimumX + 2.0f,
                         Row.MinimumY + (Row.Height() - Scaled.RunFine) * 0.5f,
                         Tinted.Muted, Property.Fields[Field].Caption, Scaled.RunFine);

        char Combined[192] = {};
        if (Property.Fields[Field].Secondary != nullptr && Property.Fields[Field].Secondary[0] != '\0')
            std::snprintf(Combined, sizeof(Combined), "%s %s",
                          Property.Fields[Field].Value, Property.Fields[Field].Secondary);
        else
            std::snprintf(Combined, sizeof(Combined), "%s", Property.Fields[Field].Value);

        const float CombinedWidth = Surface->MeasureRun(Combined, Scaled.RunFine, 0.0f);
        Surface->TextRun(Row.MaximumX - CombinedWidth,
                         Row.MinimumY + (Row.Height() - Scaled.RunFine) * 0.5f,
                         Tinted.Primary, Combined, Scaled.RunFine);
        RowY += Scaled.StatY;
    }
}

void ParametricWorkspacePanel::RecordRevisionPage(const PlaneExtent& Extent,
                                                  const ParametricRevisionRow* Revisions,
                                                  std::uint32_t RevisionCount,
                                                  float ScrollOffset)
{
    const float Pad = Scaled.PanePad * 1.5f;
    if (RevisionCount == 0u || Revisions == nullptr)
    {
        const char* Prose = "No revisions are associated with this CAD record yet.";
        Surface->TextRun(Extent.MinimumX + (Extent.Width() - Surface->MeasureRun(Prose, Scaled.RunSecondary, 0.0f)) * 0.5f,
                         Extent.MinimumY + Pad * 2.0f, Tinted.Faint, Prose, Scaled.RunSecondary);
        return;
    }

    float Sweep = Extent.MinimumY + Pad - ScrollOffset;
    for (std::uint32_t Revision = 0u; Revision < RevisionCount; ++Revision)
    {
        const PlaneExtent Card = Spanning(Extent.MinimumX + Pad, Sweep,
                                          Extent.Width() - Pad * 2.0f, 62.0f);
        if (Surface->Excluded(Card))
        {
            Sweep += Card.Height() + Pad;
            continue;
        }

        Surface->Ground(Card, Tinted.Tile, Scaled.CardRadius, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);

        const ParametricRevisionRow& Current = Revisions[Revision];
        Surface->TextRunTruncated(Card.MinimumX + 10.0f, Card.MinimumY + 8.0f,
                                  Card.Width() - 94.0f,
                                  Tinted.Primary, Current.Description, Scaled.RunSecondary, true);
        const float TimeWidth = Surface->MeasureRun(Current.SealedAt, Scaled.RunFine, 0.0f);
        Surface->TextRun(Card.MaximumX - 10.0f - TimeWidth,
                         Card.MinimumY + 10.0f, Tinted.Faint, Current.SealedAt, Scaled.RunFine);
        Surface->TextRunTruncated(Card.MinimumX + 10.0f, Card.MinimumY + 28.0f,
                                  Card.Width() - 20.0f,
                                  ParametricCategoryHue(ParametricCategory::Operation),
                                  Current.Operation, Scaled.RunFine);
        Surface->TextRunTruncated(Card.MinimumX + 10.0f, Card.MinimumY + 44.0f,
                                  Card.Width() - 20.0f,
                                  Tinted.Muted, Current.Affected, Scaled.RunFine);

        Sweep += Card.Height() + Pad;
    }
}

void ParametricWorkspacePanel::RecordProperties(const PlaneExtent& Extent,
                                                ParametricWorkspaceContext& Applied,
                                                const ParametricPropertyPresentation* Property,
                                                const ParametricRevisionRow* Revisions,
                                                std::uint32_t RevisionCount,
                                                bool OutlinePresentation)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);

    const bool Selected = Property != nullptr;
    const ThemeToken Hue = Selected ? ParametricRowHue(Property->Subject, Property->Category)
                                    : Tinted.Faint;
    const SymbolSubject Glyph = Selected ? ParametricRowGlyph(Property->Subject)
                                         : SymbolSubject::SketchPlane;

    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, Glyph, Hue,
                     Selected ? Property->Naming : "Nothing selected",
                     Selected ? Property->Secondary : "Select a committed CAD record");

    if (OutlinePresentation)
    {
        const char* Caption = "Directory";
        const float Run = Scaled.RunSecondary;
        const float PadX = 10.0f;
        const float CallSpan = PadX * 2.0f + Surface->MeasureRun(Caption, Run, 0.0f) + 12.0f;
        const PlaneExtent Call = Spanning(Header.MaximumX - Scaled.HeaderPadX - CallSpan,
                                          Header.MinimumY + (Header.Height() - 24.0f) * 0.5f,
                                          CallSpan, 24.0f);
        const bool OnCall = Call.Encloses(Sampled.PositionX, Sampled.PositionY);

        if (Sampled.ContactPressed && OnCall && !Interaction->AnyDisclosed())
            Interaction->Grab(DirectoryCall, ControlPart::Body);
        if (OnCall && Interaction->Released(DirectoryCall))
            Applied.OutlinePage = 0u;

        Interaction->DeclareHovered(DirectoryCall, OnCall, HoverOver);
        Surface->Ground(Call, OnCall ? Tinted.TileHovered : Tinted.Tile,
                        Call.Height() * 0.5f, CornerAll);
        Surface->Edge(Call, OnCall ? Tinted.HairlineFirm : Tinted.Hairline, 1.0f,
                      Call.Height() * 0.5f, CornerAll);
        Surface->TextRun(Call.MinimumX + PadX * 0.7f,
                         Call.MinimumY + (Call.Height() - Run) * 0.5f,
                         OnCall ? Tinted.Primary : Tinted.Faint, "<", Run, 0.0f, true);
        Surface->TextRun(Call.MinimumX + PadX + 12.0f,
                         Call.MinimumY + (Call.Height() - Run) * 0.5f,
                         OnCall ? Tinted.Primary : Tinted.Muted, Caption, Run);
    }

    const char* const Captions[2] =
    {
        ParametricInspectorPageText(ParametricInspectorPage::Properties),
        ParametricInspectorPageText(ParametricInspectorPage::Revision)
    };

    std::uint32_t TakenIndex = static_cast<std::uint32_t>(Applied.InspectorPage);
    const PlaneExtent Strip = Spanning(Extent.MinimumX, Header.MaximumY,
                                       Extent.Width(), Scaled.ComponentY);
    static_cast<void>(Controls.TabStrip(InspectorStrip, Strip,
                                        TabDeclaration{ Captions, 2u }, TakenIndex));
    TakenIndex = TakenIndex < 2u ? TakenIndex : 0u;
    Applied.InspectorPage = static_cast<ParametricInspectorPage>(TakenIndex);

    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);
    const PlaneExtent Body = Spanning(Extent.MinimumX, Strip.MaximumY,
                                      Extent.Width(), Footer.MinimumY - Strip.MaximumY);

    float ContentHeight = 0.0f;
    if (!Selected)
        ContentHeight = Body.Height();
    else if (Applied.InspectorPage == ParametricInspectorPage::Properties)
        ContentHeight = Scaled.HeroCrest + Scaled.HeroPad * 2.0f + Scaled.PanePad * 6.0f
                      + Scaled.ComponentY + Scaled.PanePad * 2.0f
                      + static_cast<float>(Property->FieldCount) * Scaled.StatY;
    else
        ContentHeight = RevisionCount == 0u ? Body.Height()
                      : static_cast<float>(RevisionCount) * (62.0f + Scaled.PanePad * 1.5f)
                      + Scaled.PanePad * 3.0f;

    const float Scroll = Body.Height() > 0.0f
                       ? InspectorOverflow.Advance(Sampled, Body, ContentHeight) : 0.0f;

    Surface->Confine(Body);
    if (!Selected)
    {
        const char* Prose = "Select a committed CAD row in the outliner to inspect its properties and revision feed.";
        Surface->TextRun(Body.MinimumX + (Body.Width() - Surface->MeasureRun(Prose, Scaled.RunSecondary, 0.0f)) * 0.5f,
                         Body.MinimumY + Scaled.PanePad * 3.0f,
                         Tinted.Faint, Prose, Scaled.RunSecondary);
    }
    else if (Applied.InspectorPage == ParametricInspectorPage::Properties)
        RecordPropertyPage(Body, Applied, *Property, Scroll);
    else
        RecordRevisionPage(Body, Revisions, RevisionCount, Scroll);
    Surface->Release();

    const PlaneExtent Thumb = InspectorOverflow.Thumb(Body, ContentHeight);
    if (Thumb.Height() > 0.0f)
        Surface->Ground(Thumb, Tinted.HairlineFirm, 1.5f, CornerAll);

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);
    if (Selected)
    {
        const float ChipY = Footer.MinimumY + (Footer.Height() - Scaled.ChipExtent) * 0.5f;
        Surface->Ground(Spanning(Footer.MinimumX + Scaled.HeaderPadX, ChipY,
                                 Scaled.ChipExtent, Scaled.ChipExtent),
                        Hue, 2.0f, CornerAll);
        Surface->TextRun(Footer.MinimumX + Scaled.HeaderPadX + Scaled.ChipExtent + Scaled.PanePad,
                         Footer.MinimumY + (Footer.Height() - Scaled.RunFine) * 0.5f,
                         Tinted.Muted,
                         ParametricInspectorPageText(Applied.InspectorPage), Scaled.RunFine);
    }
}

void ParametricWorkspacePanel::RecordOutliner(const PlaneExtent& Extent,
                                              ParametricWorkspaceContext& Applied,
                                              const ParametricDirectoryRow* Rows,
                                              std::uint32_t RowCount,
                                              const ParametricPropertyPresentation* Property,
                                              const ParametricRevisionRow* Revisions,
                                              std::uint32_t RevisionCount)
{
    OutlinePages.Navigate(Applied.OutlinePage);

    const PlaneExtent DirectoryExtent = OutlinePages.Page(Extent, 0u);
    const PlaneExtent InspectorExtent = OutlinePages.Page(Extent, 1u);
    const PointerCondition LivePointer = Sampled;

    const auto SeatPagePointer = [&](std::uint32_t Page)
    {
        Sampled = LivePointer;
        if (OutlinePages.CurrentPage() != Page)
        {
            Sampled.PositionX = -1000000.0f;
            Sampled.PositionY = -1000000.0f;
            Sampled.ContactHeld = false;
            Sampled.ContactPressed = false;
            Sampled.ContactReleased = false;
            Sampled.ContactDoublePressed = false;
            Sampled.SecondaryHeld = false;
            Sampled.SecondaryPressed = false;
            Sampled.SecondaryReleased = false;
            Sampled.WheelY = 0.0f;
        }
    };

    if (!Surface->Excluded(InspectorExtent))
    {
        SeatPagePointer(1u);
        Surface->Confine(Extent);
        RecordProperties(InspectorExtent, Applied, Property, Revisions, RevisionCount, true);
        Surface->Release();
    }

    if (!Surface->Excluded(DirectoryExtent))
    {
        SeatPagePointer(0u);
        Surface->Confine(Extent);
        RecordDirectoryPage(DirectoryExtent, Applied, Rows, RowCount, Property);
        Surface->Release();
    }

    Sampled = LivePointer;
}

} // namespace Slate
