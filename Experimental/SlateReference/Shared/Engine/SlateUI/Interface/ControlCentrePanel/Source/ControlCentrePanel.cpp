//============================================================================================================================================
//                                                      CONTROLCENTREPANEL.CPP
//============================================================================================================================================
// 🧩 Every rendered route of the notch Control Centre, using the default
// ImGui typeface and placeholder symbol.

#include "SlateUI/Interface/ControlCentrePanel/Api/ControlCentrePanel.h"

#include "SlateUI/Interface/SymbolSpecification/Api/SymbolSpecification.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slate
{

namespace
{

constexpr ThemeToken White = Covering(0xFFFFFFu);
constexpr ThemeToken Black = Covering(0x000000u);
constexpr ThemeToken QuietDark = Partial(0xFFFFFFu, .08);
constexpr ThemeToken QuietLight = Partial(0x000000u, .08);
constexpr float PagePad = 32.0f;
constexpr float HeaderHeight = 64.0f;
constexpr float CardGap = 16.0f;
constexpr float RowHeight = 76.0f;
constexpr float FontTileHeight = 130.0f;
constexpr float DragDuration = 300.0f;

float CentreText(RecordingSurface& Surface, const PlaneExtent& Extent, const char* Text, float Size)
{
    return Extent.MinimumX + (Extent.Width() - Surface.MeasureRun(Text, Size)) * 0.5f;
}

float CentredY(RecordingSurface& Surface, const PlaneExtent& Extent, float Size)
{
    return Extent.MinimumY + (Extent.Height() - Surface.ResolveTypographySize(Size)) * 0.5f;
}

std::uint32_t WrappedText(RecordingSurface& Surface, const PlaneExtent& Extent, ThemeToken Colour,
                          const char* Text, float Size, bool Record)
{
    char Line[256] = {};
    std::uint32_t LineLength = 0u;
    std::uint32_t LineCount = 0u;
    std::uint32_t Cursor = 0u;

    while (Text[Cursor] != '\0')
    {
        while (Text[Cursor] == ' ') ++Cursor;
        const std::uint32_t WordBegin = Cursor;
        while (Text[Cursor] != '\0' && Text[Cursor] != ' ') ++Cursor;
        const std::uint32_t WordLength = Cursor - WordBegin;
        if (WordLength == 0u) break;

        char Candidate[256] = {};
        std::uint32_t CandidateLength = 0u;
        for (std::uint32_t Index = 0u; Index < LineLength && CandidateLength + 1u < 256u; ++Index)
            Candidate[CandidateLength++] = Line[Index];
        if (CandidateLength > 0u && CandidateLength + 1u < 256u) Candidate[CandidateLength++] = ' ';
        for (std::uint32_t Index = 0u; Index < WordLength && CandidateLength + 1u < 256u; ++Index)
            Candidate[CandidateLength++] = Text[WordBegin + Index];
        Candidate[CandidateLength] = '\0';

        if (LineLength > 0u && Surface.MeasureRun(Candidate, Size) > Extent.Width())
        {
            if (Record)
                Surface.TextRunTruncated(Extent.MinimumX,
                                         Extent.MinimumY + static_cast<float>(LineCount) * (Size + 4.0f),
                                         Extent.MaximumX, Colour, Line, Size);
            ++LineCount;
            LineLength = 0u;
        }

        if (LineLength > 0u && LineLength + 1u < 256u) Line[LineLength++] = ' ';
        for (std::uint32_t Index = 0u; Index < WordLength && LineLength + 1u < 256u; ++Index)
            Line[LineLength++] = Text[WordBegin + Index];
        Line[LineLength] = '\0';
    }

    if (LineLength > 0u)
    {
        if (Record)
            Surface.TextRunTruncated(Extent.MinimumX,
                                     Extent.MinimumY + static_cast<float>(LineCount) * (Size + 4.0f),
                                     Extent.MaximumX, Colour, Line, Size);
        ++LineCount;
    }
    return LineCount;
}

ThemeToken WithOpacity(ThemeToken Colour, float Fraction)
{
    Colour.Opacity = static_cast<std::uint8_t>(static_cast<float>(Colour.Opacity) * Fraction + .5f);
    return Colour;
}

ThemeToken Between(ThemeToken From, ThemeToken To, float Fraction)
{
    const auto Mix = [Fraction](std::uint8_t First, std::uint8_t Second)
    {
        return static_cast<std::uint8_t>(static_cast<float>(First) +
                                         (static_cast<float>(Second) - static_cast<float>(First)) * Fraction + .5f);
    };
    return {Mix(From.Red, To.Red), Mix(From.Green, To.Green), Mix(From.Blue, To.Blue), Mix(From.Opacity, To.Opacity)};
}

const char* PageCaption(ControlCentrePage Page)
{
    switch (Page)
    {
    case ControlCentrePage::Settings:
        return "Settings";
    case ControlCentrePage::Notifications:
        return "Apps & Notifications";
    case ControlCentrePage::Display:
        return "Display Settings";
    case ControlCentrePage::Input:
        return "Input Devices";
    default:
        return "Control Center";
    }
}

// 📝 The live per-role weight, read straight from the configuration the artist writes. The panel never
//    reads `ThemeProfile::Fonts` for its own chrome, because that copy is refreshed by the host only when a
//    display factor or the theme moves — the strip's choice must land on the same tick as the press.
FontWeight RoleWeightOf(const std::uint32_t (&Weights)[8], std::uint32_t Role)
{
    return static_cast<FontWeight>(Weights[Role < 8u ? Role : 0u]);
}

// 📝 The weight faces every role strip offers, in ascending order. `FontLoader::Face` falls back to the
//    regular face when the selected family lacks the requested weight, so a strip never offers a tile that
//    cannot draw — the ones the family does not carry are skipped, exactly as the main family carousel
//    skips nothing because every family carries a regular face.
constexpr FontWeight CandidateFaces[] = {FontWeight::Thin,    FontWeight::ExtraLight, FontWeight::Light,
                                         FontWeight::Regular, FontWeight::Medium,    FontWeight::Semibold,
                                         FontWeight::Bold,    FontWeight::ExtraBold, FontWeight::Black};
constexpr const char* FaceNames[] = {"Thin",  "ExtraLight", "Light",   "Regular", "Medium",
                                     "Semibold", "Bold",    "ExtraBold", "Black"};

// 📝 Control ordinals for the eight per-role family strips. Each strip owns ten: two arrows and up to eight
//    pressable tile positions. They sit above the panel's historical ceiling of 192, which is why the
//    ceiling is 256 — a press past the ceiling was drawn but never registered, and a tile that can never be
//    grabbed is exactly the dead square this page previously presented.
constexpr std::uint32_t RoleArrowBase = 192u;   // [role * 2 + 0] left arrow, [role * 2 + 1] right arrow
constexpr std::uint32_t RoleTileBase = 208u;    // [role * 8 + visible position]
constexpr std::uint32_t RoleTilePositions = 8u;

} // namespace

Deliver<bool> ControlCentrePanel::ConstructControlCentrePanel(MotionIntegrator& IncomingMotion, RecordingSurface& IncomingSurface,
                                            const ThemeProfile& IncomingAppearance)
{
    if (Motion != nullptr)
        return Deliver<bool>::Refuse(
            {RefusalReason::ContentUnsupported, "a Control Centre construction already stands"});

    Motion = &IncomingMotion;
    Surface = &IncomingSurface;
    Appearance = &IncomingAppearance;

    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse(
            {RefusalReason::ExtentExhausted, "the Control Centre interaction index was rejected"});

    if (!SharedControls.ConstructComponents(Interaction, IncomingSurface, IncomingAppearance).Resolved)
        return Deliver<bool>::Refuse(
            {RefusalReason::ContentUnsupported, "the shared Control Centre controls were rejected"});
    if (!SettingsNotice.ConstructNoticeDialog(IncomingMotion, IncomingSurface).Resolved)
        return Deliver<bool>::Refuse(
            {RefusalReason::ContentUnsupported, "the Control Centre notice dialog was rejected"});

    for (std::uint32_t Index = 0u; Index < ControlCapacity; ++Index)
    {
        const Deliver<ControlIdentity> Registered = Interaction.Register();
        if (!Registered.Resolved) return Deliver<bool>::Refuse(Registered.Error);
        Controls[Index] = Registered.Resolve();
    }

    const Deliver<std::uint32_t> PageRegistered = IncomingMotion.RegisterEased(1.0);
    const Deliver<std::uint32_t> TabRegistered = IncomingMotion.RegisterEased(1.0);
    const Deliver<std::uint32_t> ThemeRegistered = IncomingMotion.RegisterEased(1.0);
    const Deliver<std::uint32_t> FontRegistered = IncomingMotion.RegisterEased(1.0);
    if (!PageRegistered.Resolved || !TabRegistered.Resolved || !ThemeRegistered.Resolved ||
        !FontRegistered.Resolved)
        return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted, "the Control Centre carousel was rejected"});

    PageMotion = PageRegistered.Resolve();
    TabMotion = TabRegistered.Resolve();
    ThemeMotion = ThemeRegistered.Resolve();
    FontMotion = FontRegistered.Resolve();

    for (std::uint32_t Index = 0u;
         Index < static_cast<std::uint32_t>(ControlCentrePage::PageCount); ++Index)
    {
        const Deliver<std::uint32_t> ScrollRegistered = IncomingMotion.RegisterEased(1.0);
        if (!ScrollRegistered.Resolved)
            return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted,
                                          "the Control Centre scroll motion was rejected"});
        ScrollMotion[Index] = ScrollRegistered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const Deliver<std::uint32_t> ScrollRegistered = IncomingMotion.RegisterEased(1.0);
        if (!ScrollRegistered.Resolved)
            return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted,
                                          "the display tab scroll motion was rejected"});
        DisplayScrollMotion[Index] = ScrollRegistered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
    {
        const Deliver<std::uint32_t> RoleRegistered = IncomingMotion.RegisterEased(1.0);
        if (!RoleRegistered.Resolved)
            return Deliver<bool>::Refuse({RefusalReason::ExtentExhausted,
                                          "the typography strip motion was rejected"});
        RoleFontMotion[Index] = RoleRegistered.Resolve();
    }

    return Deliver<bool>::Result(true);
}

void ControlCentrePanel::Advance(const PointerCondition& Sampled, double Elapsed)
{
    Pointer = Sampled;
    Interaction.Advance(Sampled, Elapsed);
    SharedControls.Sample(Sampled);
    SettingsNotice.Advance(Sampled, Elapsed);
}

void ControlCentrePanel::RetainExclusion(const PlaneExtent& Extent)
{
    if (ExclusionCount < ControlCapacity)
        Exclusions[ExclusionCount++] = Extent;
}

void ControlCentrePanel::Exclude(DrawerSpace& Drawers) const
{
    for (std::uint32_t Index = 0u; Index < ExclusionCount; ++Index)
        Drawers.Exclude(DrawerBearing::North, Exclusions[Index]);
}

bool ControlCentrePanel::Pressed(std::uint32_t Index, const PlaneExtent& Extent)
{
    if (Index >= ControlCapacity) return false;

    RetainExclusion(Extent);
    const ControlIdentity Target = Controls[Index];
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed && !Interaction.AnyDisclosed()) Interaction.Grab(Target, ControlPart::Body);

    Interaction.DeclareHovered(Target, Hovered, 130.0);
    const bool Quick = Hovered && Pointer.ContactPressed && Pointer.ContactReleased;
    return (Interaction.Released(Target) && Hovered) || Quick;
}

bool ControlCentrePanel::Slider(std::uint32_t Index, const PlaneExtent& Extent, std::uint32_t Minimum,
                                std::uint32_t Maximum, std::uint32_t& Reading, const char* UnitGlyph,
                                ThemeToken Rail, ThemeToken Accent)
{
    if (Index >= ControlCapacity || Maximum <= Minimum) return false;

    PlaneExtent Fitted = Extent;
    const float RequiredHeight = std::max(Extent.Height(), Surface->ResolveTypographySize(14.0f) + 18.0f);
    Fitted.MinimumY -= (RequiredHeight - Extent.Height()) * 0.5f;
    Fitted.MaximumY = Fitted.MinimumY + RequiredHeight;
    RetainExclusion(Fitted);
    MagnitudeDeclaration Declared;
    Declared.Caption = "";
    Declared.UnitGlyph = UnitGlyph;
    Declared.Minimum = static_cast<double>(Minimum);
    Declared.Maximum = static_cast<double>(Maximum);

    double Coordinate = static_cast<double>(Reading);
    const ControlVerdict Verdict = SharedControls.MagnitudeRow(Controls[Index], Fitted, Declared, Coordinate, true);
    Reading = static_cast<std::uint32_t>(std::round(Coordinate));
    static_cast<void>(Rail);
    static_cast<void>(Accent);
    return Verdict.ReadingAltered;
}

void ControlCentrePanel::Toggle(std::uint32_t Index, const PlaneExtent& Extent, bool& Enabled, ThemeToken Quiet,
                                ThemeToken Accent)
{
    if (Pressed(Index, Extent)) Enabled = !Enabled;

    Interaction.DeclareTaken(Controls[Index], Enabled, 150.0);
    const float Fraction = Interaction.TakenFraction(Controls[Index]);
    Surface->Ground(Extent, Enabled ? Accent : Quiet, Extent.Height() * .5f, CornerAll);
    Surface->Medallion(Extent.MinimumX + 12.0f + (Extent.Width() - 24.0f) * Fraction,
                       Extent.MinimumY + Extent.Height() * .5f, 8.0f, White);
}

void ControlCentrePanel::Symbol(const PlaneExtent& Extent, ThemeToken Colour)
{
    Surface->Stroke(SymbolSubject::PlaceholderMark, Extent, Colour);
}

void ControlCentrePanel::Navigate(ControlCentrePage Incoming)
{
    if (Incoming == CurrentPage || Motion == nullptr) return;
    PreviousPage = CurrentPage;
    PageForward = static_cast<std::uint32_t>(Incoming) >= static_cast<std::uint32_t>(CurrentPage);
    CurrentPage = Incoming;
    Motion->Eased(PageMotion).Depart(0.0, 1.0, DragDuration, 0.0, EaseCurve::Carousel);
}

Deliver<bool> ControlCentrePanel::Record(const PlaneExtent& Interior, ControlCentreConfiguration& Committed)
{
    if (Surface == nullptr || Motion == nullptr)
        return Deliver<bool>::Refuse({RefusalReason::CapabilityAbsent, "no Control Centre construction stands"});

    if (Interior.Width() <= 0.0f || Interior.Height() <= 0.0f || Surface->Excluded(Interior))
        return Deliver<bool>::Result(true);

    ExclusionCount = 0u;
    if (!WorkingConfigurationReady)
    {
        WorkingConfiguration = Committed;
        AppliedConfiguration = Committed;
        WorkingConfigurationReady = true;
    }
    ControlCentreConfiguration& Configuration = WorkingConfiguration;
    // The drawer previews its staged typography while the rest of the application remains on the
    // committed profile until confirmation. The host re-seats the committed roles after Record.
    Surface->ApplyTypographyRoles(Configuration.TypographySize, Configuration.TypographyWeight);
    if (Configuration.Page != CurrentPage) Navigate(Configuration.Page);

    if (Configuration.Theme != CurrentTheme)
    {
        PreviousTheme = CurrentTheme;
        CurrentTheme = Configuration.Theme;
        Motion->Eased(ThemeMotion).Depart(0.0, 1.0, 500.0, 0.0, EaseCurve::Standard);
    }

    const ThemeDeclaration& FromTheme = ThemeSpecification::Theme(PreviousTheme);
    const ThemeDeclaration& ToTheme = ThemeSpecification::Theme(CurrentTheme);
    const float ThemeFraction = static_cast<float>(Motion->Eased(ThemeMotion).Current());
    ThemeDeclaration Theme = ToTheme;
    Theme.Ground = Between(FromTheme.Ground, ToTheme.Ground, ThemeFraction);
    Theme.Panel = Between(FromTheme.Panel, ToTheme.Panel, ThemeFraction);
    Theme.Primary = Between(FromTheme.Primary, ToTheme.Primary, ThemeFraction);
    Theme.Secondary = Between(FromTheme.Secondary, ToTheme.Secondary, ThemeFraction);
    Theme.Edge = Between(FromTheme.Edge, ToTheme.Edge, ThemeFraction);
    Theme.Card = Between(FromTheme.Card, ToTheme.Card, ThemeFraction);
    // Foreground settings pages are always covering, including while interpolating from a
    // previously persisted theme that declared translucent panel roles.
    Theme.Panel.Opacity = 255u;
    Theme.Card.Opacity = 255u;
    const ThemeToken Accent = ThemeSpecification::Accent(Configuration.Primary).Colour;
    Surface->Ground(Interior, Theme.Panel, 0.0f, CornerNone);

    const PlaneExtent SettingsButton = Spanning(Interior.MaximumX - 68.0f, Interior.MinimumY + 24.0f, 44.0f, 44.0f);
    Surface->Ground(SettingsButton, Theme.Card, 22.0f, CornerAll);
    Surface->Edge(SettingsButton, Theme.Edge, 1.0f, 22.0f, CornerAll);
    Symbol(Spanning(SettingsButton.MinimumX + 10.0f, SettingsButton.MinimumY + 10.0f, 24.0f, 24.0f),
           Theme.Primary);
    if (Pressed(0u, SettingsButton))
    {
        Configuration.Page = ControlCentrePage::Settings;
        Navigate(Configuration.Page);
    }

    const bool DisplayFooter = CurrentPage == ControlCentrePage::Display;
    const float FooterHeight = std::max(78.0f, static_cast<float>(Configuration.TypographySize[3]) + 48.0f);
    // One enclosing panel owns a page. Display Settings alone reserves its summary/action footer;
    // the Control Centre itself has no global footer.
    const PlaneExtent PageFrame = {Interior.MinimumX + 16.0f, Interior.MinimumY + 80.0f,
                                   Interior.MaximumX - 16.0f, Interior.MaximumY - 18.0f};
    Surface->Ground(PageFrame, Theme.Panel, 18.0f, CornerAll);
    Surface->Edge(PageFrame, Theme.Edge, 1.0f, 18.0f, CornerAll);
    const PlaneExtent DisplayFooterExtent = {
        PageFrame.MinimumX + PagePad, PageFrame.MaximumY - FooterHeight - 8.0f,
        PageFrame.MaximumX - PagePad, PageFrame.MaximumY - 8.0f };
    const PlaneExtent PageExtent = {PageFrame.MinimumX + PagePad, PageFrame.MinimumY + 16.0f,
                                    PageFrame.MaximumX - PagePad,
                                    DisplayFooter ? DisplayFooterExtent.MinimumY - 8.0f
                                                  : PageFrame.MaximumY - 16.0f};
    const std::uint32_t PageIndex = static_cast<std::uint32_t>(CurrentPage);
    // 📝 The Fonts page ceiling follows the page's own content: the eight role strips and the sections
    //    below them stand about 1700px past the viewport, and a ceiling shorter than the content parks
    //    the wheel short of the antialiasing section at the foot.
    const float ScrollLimit[5] = {120.0f, 80.0f, 260.0f, 1900.0f, 520.0f};
    const float ScrollFraction = static_cast<float>(Motion->Eased(ScrollMotion[PageIndex]).Current());
    Scroll[PageIndex] = ScrollFrom[PageIndex] +
                          (ScrollTarget[PageIndex] - ScrollFrom[PageIndex]) * ScrollFraction;

    if (PageExtent.Encloses(Pointer.PositionX, Pointer.PositionY) && Pointer.WheelY != 0.0f)
    {
        ScrollFrom[PageIndex] = Scroll[PageIndex];
        ScrollTarget[PageIndex] -= Pointer.WheelY * 72.0f;
        if (ScrollTarget[PageIndex] < 0.0f) ScrollTarget[PageIndex] = 0.0f;
        if (ScrollTarget[PageIndex] > ScrollLimit[PageIndex])
            ScrollTarget[PageIndex] = ScrollLimit[PageIndex];
        Motion->Eased(ScrollMotion[PageIndex]).Depart(0.0, 1.0, 180.0, 0.0, EaseCurve::CssEase);
    }
    const double Travel = Motion->Eased(PageMotion).Current();

    auto RenderPage = [&](ControlCentrePage Page, const PlaneExtent& Extent)
    {
        PlaneExtent Scrolled = Extent;
        if (Page != ControlCentrePage::Display)
        {
            const float Offset = Scroll[static_cast<std::uint32_t>(Page)];
            Scrolled.MinimumY -= Offset;
            Scrolled.MaximumY -= Offset;
        }
        switch (Page)
        {
        case ControlCentrePage::Settings:
            SettingsPage(Scrolled, Configuration, Theme, Accent);
            break;
        case ControlCentrePage::Notifications:
            NotificationsPage(Scrolled, Configuration, Theme, Accent);
            break;
        case ControlCentrePage::Display:
            DisplayPage(Scrolled, Configuration, Theme, Accent);
            break;
        case ControlCentrePage::Input:
            InputPage(Scrolled, Configuration, Theme, Accent);
            break;
        default:
            DashboardPage(Scrolled, Configuration, Theme, Accent);
            break;
        }
    };

    const PointerCondition LivePointer = Pointer;
    if (SettingsNotice.Opened())
    {
        Pointer.PositionX = Pointer.PositionY = -1000000.0f;
        Pointer.ContactHeld = Pointer.ContactPressed = Pointer.ContactReleased = false;
        Pointer.ContactDoublePressed = false;
        Pointer.WheelY = 0.0f;
        SharedControls.Sample(Pointer);
    }
    Surface->Confine(PageExtent);
    if (!Motion->Eased(PageMotion).Settled)
    {
        const float Direction = PageForward ? 1.0f : -1.0f;
        PlaneExtent Departing = PageExtent;
        PlaneExtent Incoming = PageExtent;
        Departing.MinimumX -= Direction * static_cast<float>(Travel) * PageExtent.Width();
        Departing.MaximumX -= Direction * static_cast<float>(Travel) * PageExtent.Width();
        Incoming.MinimumX += Direction * static_cast<float>(1.0 - Travel) * PageExtent.Width();
        Incoming.MaximumX += Direction * static_cast<float>(1.0 - Travel) * PageExtent.Width();
        RenderPage(PreviousPage, Departing);
        RenderPage(CurrentPage, Incoming);
    }
    else
    {
        RenderPage(CurrentPage, PageExtent);
    }
    Surface->Release();
    Pointer = LivePointer;
    SharedControls.Sample(LivePointer);

    if (DisplayFooter)
        RecordSettingsFooter(DisplayFooterExtent, Configuration, Theme, Accent);

    if (SettingsNotice.Opened())
        RetainExclusion(Interior);
    SettingsNotice.Record(Interior, Theme, Configuration.TypographySize, Configuration.TypographyWeight);
    const NoticeDecision Decision = SettingsNotice.ConsumeDecision();
    if (Decision == NoticeDecision::Accepted)
    {
        AppliedConfiguration = WorkingConfiguration;
        Committed = AppliedConfiguration;
    }
    // Dismissing the confirmation returns to the staged edits. Only the footer's explicit Discard
    // action restores AppliedConfiguration.
    return Deliver<bool>::Result(true);
}

void ControlCentrePanel::RecordSettingsFooter(const PlaneExtent& Extent,
                                              ControlCentreConfiguration& Configuration,
                                              const ThemeDeclaration& Theme, ThemeToken Accent)
{
    Surface->Ground(Extent, Theme.Card, 16.0f, CornerAll);
    Surface->Edge(Extent, Theme.Edge, 1.0f, 16.0f, CornerAll);

    const char* Family = FontArchive != nullptr ? FontArchive->FamilyName(Configuration.Font) : nullptr;
    if (Family == nullptr) Family = "Inter";
    const char* ThemeName = ThemeSpecification::Theme(Configuration.Theme).Caption;
    char Summary[256] = {};
    std::snprintf(Summary, sizeof(Summary),
                  "Title %upx  |  Font %s  |  Theme %s  |  Scale %u%%  |  Icons %upx",
                  static_cast<unsigned>(Configuration.TypographySize[0]), Family, ThemeName,
                  static_cast<unsigned>(Configuration.Scaling), static_cast<unsigned>(Configuration.IconSize));
    const float SummarySize = static_cast<float>(std::clamp(Configuration.TypographySize[4], 10u, 20u));
    const float ButtonSize = SummarySize;
    constexpr float AuthoredFooterSize = 12.0f;
    const FontWeight FooterWeight = RoleWeightOf(Configuration.TypographyWeight, 4u);
    const float ButtonHeight = std::max(40.0f, ButtonSize + 22.0f);
    const char* ApplyCaption = "Apply Settings";
    const char* DiscardCaption = "Discard";
    const float ApplyWidth = std::max(148.0f,
        Surface->MeasureRun(ApplyCaption, AuthoredFooterSize, 0.0f, FooterWeight) + 38.0f);
    const float DiscardWidth = std::max(92.0f,
        Surface->MeasureRun(DiscardCaption, AuthoredFooterSize, 0.0f, FooterWeight) + 32.0f);
    const PlaneExtent Apply = Spanning(Extent.MaximumX - 18.0f - ApplyWidth,
                                       Extent.MinimumY + (Extent.Height() - ButtonHeight) * 0.5f,
                                       ApplyWidth, ButtonHeight);
    const PlaneExtent Discard = Spanning(Apply.MinimumX - 10.0f - DiscardWidth, Apply.MinimumY,
                                         DiscardWidth, ButtonHeight);
    Surface->TextRunTruncated(Extent.MinimumX + 18.0f,
                              Extent.MinimumY + (Extent.Height() - SummarySize) * 0.5f,
                              Discard.MinimumX - 18.0f, Theme.Primary, Summary, AuthoredFooterSize, false,
                              FooterWeight);
    Surface->Ground(Discard, Theme.Panel, ButtonHeight * 0.5f, CornerAll);
    Surface->Edge(Discard, Theme.Edge, 1.0f, ButtonHeight * 0.5f, CornerAll);
    Surface->Ground(Apply, Accent, ButtonHeight * 0.5f, CornerAll);
    Surface->TextRun(Discard.MinimumX + (Discard.Width() -
                     Surface->MeasureRun(DiscardCaption, AuthoredFooterSize, 0.0f, FooterWeight)) * 0.5f,
                     Discard.MinimumY + (Discard.Height() - ButtonSize) * 0.5f,
                     Theme.Primary, DiscardCaption, AuthoredFooterSize, 0.0f, false, FooterWeight);
    Surface->TextRun(Apply.MinimumX + (Apply.Width() -
                     Surface->MeasureRun(ApplyCaption, AuthoredFooterSize, 0.0f, FooterWeight)) * 0.5f,
                     Apply.MinimumY + (Apply.Height() - ButtonSize) * 0.5f,
                     White, ApplyCaption, AuthoredFooterSize, 0.0f, false, FooterWeight);

    if (!SettingsNotice.Opened() && Pressed(250u, Apply))
        SettingsNotice.Open(NoticeTone::Confirmation, "Apply interface settings?",
                            "The staged font, theme, scale, icon, and interaction settings will be applied.",
                            "Apply", "Cancel");
    if (!SettingsNotice.Opened() && Pressed(251u, Discard))
    {
        Configuration = AppliedConfiguration;
        Navigate(Configuration.Page);
    }
}

void ControlCentrePanel::DashboardPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                       const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const float ContentX = (Extent.Width() < 1024.0f) ? Extent.Width() : 1024.0f;
    const float Start = Extent.MinimumX + (Extent.Width() - ContentX) * .5f;
    const float LeftX = ContentX / 3.0f - 20.0f;
    const PlaneExtent Left = Spanning(Start, Extent.MinimumY, LeftX, Extent.Height());
    const PlaneExtent Right =
        Spanning(Start + LeftX + 48.0f, Extent.MinimumY, ContentX - LeftX - 48.0f, Extent.Height());
    Surface->TextRun(Left.MinimumX + 8.0f, Left.MinimumY, Theme.Primary, "Control Center", 20.0f, 0.0f, true);

    const char* QualityNames[5] = {"Low", "Medium", "High", "Epic", "Cinematic"};
    const char* AntialiasNames[3] = {"Refined", "Basic", "None"};
    const std::uint32_t Antialiasing = static_cast<std::uint32_t>(Configuration.GeometryAntialiasing);
    char LabelRuns[5][64] = {};
    std::snprintf(LabelRuns[0], sizeof(LabelRuns[0]), "Quality: %s", QualityNames[Configuration.Quality % 5u]);
    std::snprintf(LabelRuns[1], sizeof(LabelRuns[1]), "VSync: %s", Configuration.VsyncEnabled ? "ON" : "OFF");
    std::snprintf(LabelRuns[2], sizeof(LabelRuns[2]), "Global Illumination: %s",
                  Configuration.IlluminationEnabled ? "ON" : "OFF");
    std::snprintf(LabelRuns[3], sizeof(LabelRuns[3]), "Notifications: %s",
                  Configuration.NotificationsEnabled ? "ON" : "OFF");
    std::snprintf(LabelRuns[4], sizeof(LabelRuns[4]), "Interface AA: %s", AntialiasNames[Antialiasing % 3u]);
    for (std::uint32_t Index = 0u; Index < 5u; ++Index)
    {
        const float Column = static_cast<float>(Index % 2u);
        const float Row = static_cast<float>(Index / 2u);
        const PlaneExtent Tile =
            Spanning(Left.MinimumX + Column * (Left.Width() * .5f + 4.0f),
                     Left.MinimumY + 42.0f + Row * (FontTileHeight + 16.0f), Left.Width() * .5f - 8.0f, FontTileHeight);
        const bool Active = Index == 0u ||
                            (Index == 1u && Configuration.VsyncEnabled) ||
                            (Index == 2u && Configuration.IlluminationEnabled) ||
                            (Index == 3u && Configuration.NotificationsEnabled) ||
                            (Index == 4u && Configuration.GeometryAntialiasing != InterfaceAntialiasing::None);
        Surface->Ground(Tile, Active ? Accent : Theme.Card, static_cast<float>(Configuration.Radius), CornerAll);
        Surface->Edge(Tile, Theme.Edge, 1.0f, static_cast<float>(Configuration.Radius), CornerAll);
        Symbol(Spanning(Tile.MinimumX + Tile.Width() * .5f - 18.0f, Tile.MinimumY + 24.0f, 36.0f, 36.0f),
               Active ? White : Theme.Secondary);
        Surface->TextRunTruncated(Tile.MinimumX + 10.0f, Tile.MaximumY - 32.0f, Tile.MaximumX - 10.0f,
                                  Active ? White : Theme.Secondary, LabelRuns[Index], 12.0f, true);
        if (Pressed(10u + Index, Tile))
        {
            if (Index == 0u) Configuration.Quality = (Configuration.Quality + 1u) % 5u;
            if (Index == 1u) Configuration.VsyncEnabled = !Configuration.VsyncEnabled;
            if (Index == 2u) Configuration.IlluminationEnabled = !Configuration.IlluminationEnabled;
            if (Index == 3u) Configuration.NotificationsEnabled = !Configuration.NotificationsEnabled;
            if (Index == 4u)
                Configuration.GeometryAntialiasing = static_cast<InterfaceAntialiasing>((Antialiasing + 1u) % 3u);
        }
    }

    const PlaneExtent Monitor =
        Spanning(Left.MinimumX, Left.MinimumY + 42.0f + 3.0f * (FontTileHeight + 16.0f), Left.Width(), 64.0f);
    Surface->Ground(Monitor, Theme.Card, static_cast<float>(Configuration.Radius), CornerAll);
    Symbol(Spanning(Monitor.MinimumX + 22.0f, Monitor.MinimumY + 22.0f, 20.0f, 20.0f), Theme.Secondary);
    Slider(21u, Spanning(Monitor.MinimumX + 58.0f, Monitor.MinimumY + 12.0f,
                         Monitor.Width() - 76.0f, 40.0f),
           0u, 100u, Configuration.MonitorLevel, "%", Theme.Edge, Accent);

    Surface->TextRun(Right.MinimumX + 8.0f, Right.MinimumY, Theme.Primary, "Notifications", 20.0f, 0.0f, true);
    const PlaneExtent Clear = Spanning(Right.MaximumX - 110.0f, Right.MinimumY - 4.0f, 110.0f, 30.0f);
    Surface->TextRun(Clear.MinimumX, CentredY(*Surface, Clear, 12.0f), Accent, "Clear messages", 12.0f);
    if (Pressed(20u, Clear)) Configuration.NotificationsPresent = false;

    if (!Configuration.NotificationsPresent)
    {
        Symbol(Spanning(Right.MinimumX + Right.Width() * .5f - 24.0f, Right.MinimumY + 120.0f, 48.0f, 48.0f),
               WithOpacity(Theme.Secondary, .25f));
        Surface->TextRun(CentreText(*Surface, Right, "You're all caught up.", 14.0f), Right.MinimumY + 184.0f,
                         Theme.Secondary, "You're all caught up.", 14.0f);
        return;
    }

    const char* Titles[4] = {"Storage Almost Full", "High Memory Usage", "System Update", "New Message"};
    const char* Times[4] = {"Just now", "2m ago", "10m ago", "1h ago"};
    const char* Descriptions[4] = {"You have used 95% of your allocated cloud storage. Please upgrade your "
                                   "plan to avoid data loss.",
                                   "System memory is running high. Consider closing unused applications to "
                                   "improve performance.",
                                   "A new software update is available for your workspace. This includes "
                                   "security patches and performance improvements.",
                                   "Hey, are we still on for the design review tomorrow? I have some new "
                                   "mockups to share."};
    float NotificationCursor = Right.MinimumY + 42.0f;
    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const PlaneExtent DescriptionMeasure = {Right.MinimumX + 76.0f, 0.0f,
                                                Right.MaximumX - 20.0f, 0.0f};
        const std::uint32_t DescriptionLines = WrappedText(*Surface, DescriptionMeasure, Theme.Secondary,
                                                           Descriptions[Index], 13.0f, false);
        const float RequiredHeight = 71.0f + static_cast<float>(DescriptionLines) * 17.0f;
        const float CardHeight = RequiredHeight > 102.0f ? RequiredHeight : 102.0f;
        const PlaneExtent Card = Spanning(Right.MinimumX, NotificationCursor, Right.Width(), CardHeight);
        Surface->Ground(Card, Theme.Card, static_cast<float>(Configuration.Radius), CornerAll);
        Surface->Edge(Card, Theme.Edge, 1.0f, static_cast<float>(Configuration.Radius), CornerAll);
        Surface->Medallion(Card.MinimumX + 36.0f, Card.MinimumY + 38.0f, 24.0f, WithOpacity(Accent, .12f));
        Symbol(Spanning(Card.MinimumX + 24.0f, Card.MinimumY + 26.0f, 24.0f, 24.0f), Accent);

        const float TimeX = Surface->MeasureRun(Times[Index], 12.0f);
        Surface->TextRunTruncated(Card.MinimumX + 76.0f, Card.MinimumY + 18.0f,
                                  Card.MaximumX - TimeX - 36.0f,
                                  Index < 3u ? Accent : Theme.Primary, Titles[Index], 16.0f, true);
        Surface->TextRun(Card.MaximumX - TimeX - 20.0f, Card.MinimumY + 20.0f,
                         Theme.Secondary, Times[Index], 12.0f);

        const PlaneExtent DescriptionClip = {Card.MinimumX + 76.0f, Card.MinimumY + 51.0f,
                                             Card.MaximumX - 20.0f, Card.MaximumY - 16.0f};
        Surface->Confine(DescriptionClip);
        WrappedText(*Surface, DescriptionClip, Theme.Secondary, Descriptions[Index], 13.0f, true);
        Surface->Release();
        NotificationCursor = Card.MaximumY + 16.0f;
    }
}

void ControlCentrePanel::SettingsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                      const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const float Width = (Extent.Width() < 672.0f) ? Extent.Width() : 672.0f;
    const float X = Extent.MinimumX + (Extent.Width() - Width) * .5f;
    const PlaneExtent Back = Spanning(X, Extent.MinimumY, 42.0f, 42.0f);
    Symbol(Spanning(Back.MinimumX + 9.0f, Back.MinimumY + 9.0f, 24.0f, 24.0f), Theme.Primary);
    if (Pressed(30u, Back))
    {
        Configuration.Page = ControlCentrePage::Dashboard;
        Navigate(Configuration.Page);
    }
    Surface->TextRun(X + 58.0f, Extent.MinimumY + 8.0f, Theme.Primary, "Settings", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 0u));

    const float AdaptiveRowHeight = std::max(RowHeight,
        static_cast<float>(Configuration.TypographySize[2] + Configuration.TypographySize[3]) + 42.0f);
    const PlaneExtent Card = Spanning(X, Extent.MinimumY + 74.0f, Width, 5.0f * AdaptiveRowHeight);
    Surface->Ground(Card, Theme.Card, static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius), CornerAll);
    Surface->Edge(Card, Theme.Edge, 1.0f, static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius),
                  CornerAll);
    const char* Titles[5] = {"Display Settings", "Display & Workspace", "Input Devices", "Privacy & Security",
                             "Apps & Notifications"};
    const char* Subs[5] = {"Appearance, theme, fonts, and system colors", "Resolution, scaling, multiple displays",
                           "Keyboard, mouse, and touch settings", "Permissions, camera access, firewall",
                           "Do not disturb, app permissions"};
    for (std::uint32_t Index = 0u; Index < 5u; ++Index)
    {
        const PlaneExtent Row = Spanning(Card.MinimumX,
                                         Card.MinimumY + AdaptiveRowHeight * static_cast<float>(Index),
                                         Card.Width(), AdaptiveRowHeight);
        if (Index < 4u)
            Surface->Ground(Spanning(Row.MinimumX + 20.0f, Row.MaximumY - 1.0f, Row.Width() - 40.0f, 1.0f),
                            Theme.Edge, 0.0f, CornerNone);
        Surface->Medallion(Row.MinimumX + 44.0f, Row.MinimumY + 38.0f, 24.0f, Theme.Ground);
        Symbol(Spanning(Row.MinimumX + 32.0f, Row.MinimumY + 26.0f, 24.0f, 24.0f),
               Index == 0u ? Accent : Theme.Secondary);
        Surface->TextRun(Row.MinimumX + 82.0f, Row.MinimumY + 18.0f, Theme.Primary, Titles[Index], 16.0f, 0.0f,
                         true);
        Surface->TextRun(Row.MinimumX + 82.0f, Row.MinimumY + 43.0f, Theme.Secondary, Subs[Index], 13.0f);
        Symbol(Spanning(Row.MaximumX - 40.0f, Row.MinimumY + 28.0f, 20.0f, 20.0f),
               WithOpacity(Theme.Secondary, .5f));
        if (Pressed(31u + Index, Row))
        {
            if (Index <= 1u)
            {
                Configuration.Page = ControlCentrePage::Display;
                Configuration.DisplayPage = Index == 0u ? DisplayPreferencePage::Theme : DisplayPreferencePage::Display;
            }
            else if (Index == 2u)
                Configuration.Page = ControlCentrePage::Input;
            else if (Index == 4u)
                Configuration.Page = ControlCentrePage::Notifications;
            Navigate(Configuration.Page);
        }
    }
}

void ControlCentrePanel::NotificationsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                           const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const float Width = (Extent.Width() < 768.0f) ? Extent.Width() : 768.0f;
    const float X = Extent.MinimumX + (Extent.Width() - Width) * .5f;
    Surface->TextRun(X, Extent.MinimumY, Theme.Primary, "Apps & Notifications", 29.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 0u));
    const PlaneExtent Back = Spanning(X + Width - 44.0f, Extent.MinimumY, 40.0f, 40.0f);
    Surface->Ground(Back, Theme.Card, 20.0f, CornerAll);
    Surface->Edge(Back, Theme.Edge, 1.0f, 20.0f, CornerAll);
    Symbol(Spanning(Back.MinimumX + 10.0f, Back.MinimumY + 10.0f, 20.0f, 20.0f), Theme.Primary);
    if (Pressed(40u, Back))
    {
        Configuration.Page = ControlCentrePage::Settings;
        Navigate(Configuration.Page);
    }

    const PlaneExtent Global = Spanning(X, Extent.MinimumY + 66.0f, Width, 176.0f);
    Surface->Ground(Global, Theme.Card, static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius), CornerAll);
    Surface->Edge(Global, Theme.Edge, 1.0f, 20.0f, CornerAll);
    const char* Titles[2] = {"Do Not Disturb", "Notification Sounds"};
    const char* Subs[2] = {"Silence all notifications and alerts", "Play sounds for incoming alerts"};
    bool* Conditions[2] = {&Configuration.DisturbanceWithheld, &Configuration.SoundEnabled};
    for (std::uint32_t Index = 0u; Index < 2u; ++Index)
    {
        const PlaneExtent Row =
            Spanning(Global.MinimumX + 24.0f, Global.MinimumY + 12.0f + 80.0f * static_cast<float>(Index),
                     Global.Width() - 48.0f, 72.0f);
        Surface->Medallion(Row.MinimumX + 24.0f, Row.MinimumY + 36.0f, 20.0f, WithOpacity(Accent, .10f));
        Symbol(Spanning(Row.MinimumX + 14.0f, Row.MinimumY + 26.0f, 20.0f, 20.0f), Accent);
        Surface->TextRun(Row.MinimumX + 62.0f, Row.MinimumY + 17.0f, Theme.Primary, Titles[Index], 18.0f, 0.0f,
                         true);
        Surface->TextRun(Row.MinimumX + 62.0f, Row.MinimumY + 43.0f, Theme.Secondary, Subs[Index], 13.0f);
        Toggle(41u + Index, Spanning(Row.MaximumX - 48.0f, Row.MinimumY + 24.0f, 48.0f, 24.0f),
               *Conditions[Index], Theme.Edge, Accent);
    }

    Surface->TextRun(X + 8.0f, Global.MaximumY + 36.0f, Theme.Primary, "App Permissions", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 0u));
    Surface->TextRun(X + 8.0f, Global.MaximumY + 68.0f, Theme.Secondary,
                     "Choose which apps can send you notifications", 14.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 5u));
    const float AdaptiveRowHeight = std::max(RowHeight,
        static_cast<float>(Configuration.TypographySize[2] + Configuration.TypographySize[3]) + 42.0f);
    const PlaneExtent Apps = Spanning(X, Global.MaximumY + 100.0f, Width, 4.0f * AdaptiveRowHeight);
    Surface->Ground(Apps, Theme.Card, static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius), CornerAll);
    Surface->Edge(Apps, Theme.Edge, 1.0f, 20.0f, CornerAll);
    const char* AppTitles[4] = {"Mail", "Calendar", "Messages", "System Alerts"};
    const char* AppSubs[4] = {"New emails and calendar invites", "Upcoming events and reminders",
                              "Direct messages and mentions", "Critical system and security updates"};
    for (std::uint32_t Index = 0u; Index < 4u; ++Index)
    {
        const PlaneExtent Row =
            Spanning(Apps.MinimumX + 20.0f,
                     Apps.MinimumY + AdaptiveRowHeight * static_cast<float>(Index),
                     Apps.Width() - 40.0f, AdaptiveRowHeight);
        Symbol(Spanning(Row.MinimumX + 8.0f, Row.MinimumY + 26.0f, 24.0f, 24.0f), Theme.Secondary);
        Surface->TextRun(Row.MinimumX + 52.0f, Row.MinimumY + 17.0f, Theme.Primary, AppTitles[Index], 16.0f,
                         0.0f, true);
        Surface->TextRun(Row.MinimumX + 52.0f, Row.MinimumY + 43.0f, Theme.Secondary, AppSubs[Index], 13.0f);
        Toggle(45u + Index, Spanning(Row.MaximumX - 48.0f, Row.MinimumY + 26.0f, 48.0f, 24.0f),
               Configuration.AppNotifications[Index], Theme.Edge, Accent);
    }
}

void ControlCentrePanel::DisplayPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                     const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const PlaneExtent Back = Spanning(Extent.MinimumX, Extent.MinimumY, 42.0f, 42.0f);
    Surface->Ground(Back, Theme.Card, 21.0f, CornerAll);
    Surface->Edge(Back, Theme.Edge, 1.0f, 21.0f, CornerAll);
    Symbol(Spanning(Back.MinimumX + 9.0f, Back.MinimumY + 9.0f, 24.0f, 24.0f), Theme.Primary);
    if (Pressed(50u, Back))
    {
        Configuration.Page = ControlCentrePage::Settings;
        Navigate(Configuration.Page);
    }
    Surface->TextRun(Extent.MinimumX + 58.0f, Extent.MinimumY + 3.0f, Theme.Primary, "Display Settings", 29.0f,
                     0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 0u));
    Surface->TextRun(Extent.MinimumX + 58.0f, Extent.MinimumY + 40.0f, Theme.Secondary, "Appearance & typography",
                     14.0f, 0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 5u));

    if (Configuration.DisplayPage != CurrentTab)
    {
        PreviousTab = CurrentTab;
        TabForward = static_cast<std::uint32_t>(Configuration.DisplayPage) >= static_cast<std::uint32_t>(CurrentTab);
        CurrentTab = Configuration.DisplayPage;
        Motion->Eased(TabMotion).Depart(0.0, 1.0, 220.0, 0.0, EaseCurve::Carousel);
    }

    const char* Tabs[3] = {"Display", "Fonts", "Theme"};
    float TabX = Extent.MinimumX + 58.0f;
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const float Width = Surface->MeasureRun(Tabs[Index], 24.0f) + 8.0f;
        const PlaneExtent Tab = Spanning(TabX, Extent.MinimumY + 78.0f, Width, 42.0f);
        Surface->TextRun(Tab.MinimumX + 4.0f, Tab.MinimumY + 4.0f,
                         Configuration.DisplayPage == static_cast<DisplayPreferencePage>(Index) ? Theme.Primary
                                                                                              : Theme.Secondary,
                         Tabs[Index], 24.0f, 0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 2u));
        if (Configuration.DisplayPage == static_cast<DisplayPreferencePage>(Index))
            Surface->Ground(Spanning(Tab.MinimumX, Tab.MaximumY - 3.0f, Tab.Width(), 3.0f), Accent, 1.5f,
                            CornerAll);
        if (Pressed(51u + Index, Tab)) Configuration.DisplayPage = static_cast<DisplayPreferencePage>(Index);
        TabX += Width + 24.0f;
    }

    const PlaneExtent Viewport = {Extent.MinimumX + 58.0f, Extent.MinimumY + 136.0f, Extent.MaximumX - 16.0f,
                                  Extent.MaximumY};
    const std::uint32_t ActiveTab = static_cast<std::uint32_t>(CurrentTab);
    const float TabContentHeight[3] = { 180.0f, 2300.0f, 1340.0f };
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const float Fraction = static_cast<float>(Motion->Eased(DisplayScrollMotion[Index]).Current());
        DisplayScroll[Index] = DisplayScrollFrom[Index] +
                             (DisplayScrollTarget[Index] - DisplayScrollFrom[Index]) * Fraction;
    }
    if (Viewport.Encloses(Pointer.PositionX, Pointer.PositionY) && Pointer.WheelY != 0.0f)
    {
        const float Limit = std::max(TabContentHeight[ActiveTab] - Viewport.Height(), 0.0f);
        DisplayScrollFrom[ActiveTab] = DisplayScroll[ActiveTab];
        DisplayScrollTarget[ActiveTab] = std::clamp(DisplayScrollTarget[ActiveTab] - Pointer.WheelY * 72.0f,
                                                    0.0f, Limit);
        Motion->Eased(DisplayScrollMotion[ActiveTab]).Depart(0.0, 1.0, 180.0, 0.0,
                                                            EaseCurve::CssEase);
    }
    const auto RenderTab = [&](DisplayPreferencePage Page, PlaneExtent Content)
    {
        const float Offset = DisplayScroll[static_cast<std::uint32_t>(Page)];
        Content.MinimumY -= Offset;
        Content.MaximumY -= Offset;
        if (Page == DisplayPreferencePage::Display)
            DisplayHardwarePage(Content, Configuration, Theme, Accent);
        else if (Page == DisplayPreferencePage::Theme)
            ThemePage(Content, Configuration, Theme, Accent);
        else
            FontsPage(Content, Configuration, Theme, Accent);
    };

    Surface->Confine(Viewport);
    if (!Motion->Eased(TabMotion).Settled)
    {
        const float Travel = static_cast<float>(Motion->Eased(TabMotion).Current());
        const float Direction = TabForward ? 1.0f : -1.0f;
        PlaneExtent Departing = Viewport;
        PlaneExtent Incoming = Viewport;
        Departing.MinimumX -= Direction * Travel * Viewport.Width();
        Departing.MaximumX -= Direction * Travel * Viewport.Width();
        Incoming.MinimumX += Direction * (1.0f - Travel) * Viewport.Width();
        Incoming.MaximumX += Direction * (1.0f - Travel) * Viewport.Width();
        RenderTab(PreviousTab, Departing);
        RenderTab(CurrentTab, Incoming);
    }
    else
    {
        RenderTab(CurrentTab, Viewport);
    }
    Surface->Release();
}

void ControlCentrePanel::DisplayHardwarePage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                             const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const PlaneExtent Card = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 156.0f);
    Surface->Ground(Card, Theme.Card,
                    static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius), CornerAll);
    Surface->Edge(Card, Theme.Edge, 1.0f, 20.0f, CornerAll);
    Surface->TextRun(Card.MinimumX + 28.0f, Card.MinimumY + 25.0f,
                     Theme.Primary, "Interface Scaling", 22.0f, 0.0f, true);
    Surface->TextRun(Card.MinimumX + 28.0f, Card.MinimumY + 56.0f,
                     Theme.Secondary,
                     "Resolution, refresh rate, and monitor topology follow the operating system.",
                     13.0f);
    Slider(63u, Spanning(Card.MinimumX + 28.0f, Card.MinimumY + 101.0f,
                         Card.Width() - 56.0f, 24.0f),
           75u, 200u, Configuration.Scaling, "%", Theme.Edge, Accent);
}

void ControlCentrePanel::ThemePage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                   const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const PlaneExtent Section = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(), 1340.0f);
    Surface->Ground(Section, WithOpacity(Theme.Card, 1.0f), static_cast<float>(Configuration.Radius < 24u ? 24u : Configuration.Radius), CornerAll);
    Surface->Edge(Section, Theme.Edge, 1.0f, static_cast<float>(Configuration.Radius < 24u ? 24u : Configuration.Radius), CornerAll);
    const float Inset = 28.0f;
    const float ContentLeft = Extent.MinimumX + Inset;
    const float ContentRight = Extent.MaximumX - Inset;
    Surface->TextRun(ContentLeft, Extent.MinimumY + Inset, Theme.Primary, "Theme", 24.0f, 0.0f, true);
    Surface->TextRun(ContentLeft, Extent.MinimumY + Inset + 32.0f, Theme.Secondary, "Customize UI colors", 14.0f);
    const float AvailableTileWidth = (ContentRight - ContentLeft - 40.0f) / 3.0f;
    const float TileWidth = AvailableTileWidth < 300.0f ? AvailableTileWidth : 300.0f;
    const float PreviewTileHeight = TileWidth * (250.0f / 300.0f);
    const float GridWidth = TileWidth * 3.0f + 40.0f;
    const float GridTop = ContentLeft + (ContentRight - ContentLeft - GridWidth) * 0.5f;
    const ThemeToken SelectionColour = Covering(0x7B42F6u);

    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        const ThemeSubject PreviewSubject = static_cast<ThemeSubject>(Index);
        const ThemeDeclaration& Preview = ThemeSpecification::Theme(PreviewSubject);
        const bool WhitePreview = PreviewSubject == ThemeSubject::CleanWhite;
        const ThemeToken SidebarQuiet = WhitePreview ? Covering(0xDADAE0u) : Preview.PreviewSidebarQuiet;
        const ThemeToken SidebarStrong = WhitePreview ? Covering(0xC8C8CEu) : Preview.PreviewSidebarStrong;
        const ThemeToken MainQuiet = WhitePreview ? Covering(0xF0F0F0u) : Preview.PreviewQuiet;
        const ThemeToken MainStrong = WhitePreview ? Covering(0xE0E0E0u) : Preview.PreviewStrong;
        const float Column = static_cast<float>(Index % 3u);
        const float Row = static_cast<float>(Index / 3u);
        const PlaneExtent Tile = Spanning(GridTop + Column * (TileWidth + 20.0f),
                                          Extent.MinimumY + Inset + 64.0f + Row * (PreviewTileHeight + 20.0f),
                                          TileWidth, PreviewTileHeight);
        const float XScale = TileWidth / 300.0f;
        const float YScale = PreviewTileHeight / 250.0f;
        const float OuterRadius = static_cast<float>(Configuration.Radius) * (18.0f / 24.0f) * XScale;
        const bool Selected = Configuration.Theme == static_cast<ThemeSubject>(Index);
        const PlaneExtent Outer = Spanning(Tile.MinimumX + 15.0f * XScale,
                                           Tile.MinimumY + 15.0f * YScale,
                                           270.0f * XScale, 195.0f * YScale);

        if (Selected)
            Surface->Edge(Outer, WithOpacity(SelectionColour, .25f), 4.0f * XScale,
                          OuterRadius, CornerAll);
        Surface->Ground(Outer, Preview.PreviewGround, OuterRadius, CornerAll);
        Surface->Edge(Outer, Selected ? SelectionColour : Preview.Edge,
                      (Selected ? 1.5f : 1.0f) * XScale, OuterRadius, CornerAll);

        const PlaneExtent Window = Spanning(Tile.MinimumX + 45.0f * XScale,
                                            Tile.MinimumY + 40.0f * YScale,
                                            210.0f * XScale, 150.0f * YScale);
        const float WindowRadius = static_cast<float>(Configuration.Radius) * (14.0f / 24.0f) * XScale;
        Surface->Ground(Window, Preview.PreviewSidebar, WindowRadius, CornerAll);

        const PlaneExtent RightPanel = Spanning(Tile.MinimumX + 110.0f * XScale,
                                                Tile.MinimumY + 40.0f * YScale,
                                                145.0f * XScale, 150.0f * YScale);
        Surface->Ground(RightPanel, Preview.PreviewWindow, WindowRadius, CornerAll);
        Surface->Edge(Window, Preview.Edge, 1.0f, WindowRadius, CornerAll);

        for (std::uint32_t Dot = 0u; Dot < 3u; ++Dot)
            Surface->Medallion(Tile.MinimumX + (60.0f + 9.0f * static_cast<float>(Dot)) * XScale,
                               Tile.MinimumY + 55.0f * YScale, 2.5f * XScale,
                               SidebarStrong);

        const float SidebarWidths[3] = {40.0f, 28.0f, 18.0f};
        for (std::uint32_t Line = 0u; Line < 3u; ++Line)
            Surface->Ground(Spanning(Tile.MinimumX + 58.0f * XScale,
                                     Tile.MinimumY + (72.0f + 14.0f * static_cast<float>(Line)) * YScale,
                                     SidebarWidths[Line] * XScale, 6.0f * YScale),
                            SidebarQuiet, 3.0f * XScale, CornerAll);

        Surface->Medallion(Tile.MinimumX + 63.0f * XScale,
                           Tile.MinimumY + 175.0f * YScale, 5.0f * XScale,
                           SidebarStrong);
        Surface->Ground(Spanning(Tile.MinimumX + 74.0f * XScale,
                                 Tile.MinimumY + 172.0f * YScale,
                                 16.0f * XScale, 6.0f * YScale),
                        SidebarQuiet, 3.0f * XScale, CornerAll);

        Surface->Ground(Spanning(Tile.MinimumX + 123.0f * XScale,
                                 Tile.MinimumY + 60.0f * YScale,
                                 45.0f * XScale, 6.0f * YScale),
                        MainStrong, 3.0f * XScale, CornerAll);
        Surface->Ground(Spanning(Tile.MinimumX + 123.0f * XScale,
                                 Tile.MinimumY + 75.0f * YScale,
                                 42.0f * XScale, 6.0f * YScale),
                        MainStrong, 3.0f * XScale, CornerAll);

        for (std::uint32_t Cell = 0u; Cell < 3u; ++Cell)
            Surface->Ground(Spanning(Tile.MinimumX + (123.0f + 44.0f * static_cast<float>(Cell)) * XScale,
                                     Tile.MinimumY + 95.0f * YScale,
                                     32.0f * XScale, 32.0f * YScale),
                            MainQuiet, 8.0f * XScale, CornerAll);

        Surface->Ground(Spanning(Tile.MinimumX + 123.0f * XScale,
                                 Tile.MinimumY + 172.0f * YScale,
                                 26.0f * XScale, 6.0f * YScale),
                        MainStrong, 3.0f * XScale, CornerAll);

        Surface->TextRun(CentreText(*Surface, Tile, Preview.Caption, 13.0f * XScale),
                         Tile.MinimumY + 222.0f * YScale,
                         Selected ? Theme.Primary : Theme.Secondary,
                         Preview.Caption, 13.0f * XScale, .04f, true);
        if (Pressed(75u + Index, Tile)) Configuration.Theme = static_cast<ThemeSubject>(Index);
    }

    const float Below = Extent.MinimumY + Inset + 64.0f + 2.0f * (PreviewTileHeight + 20.0f) + 16.0f;
    Surface->TextRun(ContentLeft, Below, Theme.Primary, "Corner Radius", 22.0f, 0.0f, true);
    Slider(82u, Spanning(ContentLeft, Below + 48.0f, ContentRight - ContentLeft, 40.0f), 0u, 48u,
           Configuration.Radius, "px", Theme.Edge, Accent);
    Surface->TextRun(ContentLeft, Below + 100.0f, Theme.Primary, "Sidebar", 22.0f, 0.0f, true);
    Surface->TextRun(ContentLeft, Below + 130.0f, Theme.Secondary, "Make the sidebar transparent", 14.0f);
    Toggle(83u, Spanning(ContentRight - 48.0f, Below + 104.0f, 48.0f, 24.0f), Configuration.TransparentSidebar,
           Theme.Edge, Accent);

    const float ColoursTop = Below + 184.0f;
    Surface->TextRun(ContentLeft, ColoursTop, Theme.Primary, "System Colors", 24.0f, 0.0f, true);
    Surface->TextRun(ContentLeft, ColoursTop + 32.0f, Theme.Secondary, "Semantic colors for UI elements", 14.0f);
    const char* Names[5] = {"Primary", "Secondary", "Info", "Warning", "Alert"};
    const char* Descriptions[5] = {"Main interactive elements and accents", "Alternative interactive elements",
                                   "Informational messages and badges", "Non-critical alerts and warnings",
                                   "Critical errors and destructive actions"};
    float Cursor = ColoursTop + 70.0f;
    for (std::uint32_t Index = 0u; Index < 5u; ++Index)
    {
        bool Open = OpenPalette == Index;
        const PlaneExtent Header = Spanning(ContentLeft, Cursor, ContentRight - ContentLeft, 58.0f);
        if (Pressed(84u + Index, Header))
        {
            OpenPalette = Open ? 5u : Index;
            Open = OpenPalette == Index;
        }

        Interaction.DeclareTaken(Controls[84u + Index], Open, 220.0, EaseCurve::CssEase);
        const float Disclosure = Interaction.TakenFraction(Controls[84u + Index]);
        const float Height = 58.0f + 68.0f * Disclosure;
        const PlaneExtent Row = Spanning(ContentLeft, Cursor, ContentRight - ContentLeft, Height);
        Surface->Ground(Row, Theme.Card, Index == 0u || Index == 4u ? 16.0f : 0.0f, CornerAll);
        Surface->TextRun(Header.MinimumX + 20.0f, Header.MinimumY + 20.0f, Theme.Primary, Names[Index],
                         14.0f, 0.0f, true);
        Surface->Medallion(Header.MaximumX - 48.0f, Header.MinimumY + 28.0f, 10.0f,
                           ThemeSpecification::Accent(Configuration.SemanticColours[Index]).Colour);
        Symbol(Spanning(Header.MaximumX - 26.0f, Header.MinimumY + 20.0f, 16.0f, 16.0f), Theme.Secondary);

        if (Disclosure > 0.0f)
        {
            const PlaneExtent Revealed = {Row.MinimumX, Header.MaximumY,
                                          Row.MaximumX, Header.MaximumY + 68.0f * Disclosure};
            Surface->Confine(Revealed);
            Surface->TextRun(Row.MinimumX + 20.0f, Header.MaximumY + 6.0f, Theme.Secondary,
                             Descriptions[Index], 12.0f);
            for (std::uint32_t Colour = 0u; Colour < 8u; ++Colour)
            {
                const PlaneExtent Swatch = Spanning(Row.MinimumX + 22.0f + 44.0f * static_cast<float>(Colour),
                                                    Header.MaximumY + 26.0f, 32.0f, 32.0f);
                Surface->Ground(Swatch, ThemeSpecification::Accent(static_cast<AccentSubject>(Colour)).Colour,
                                16.0f, CornerAll);
                if (Configuration.SemanticColours[Index] == static_cast<AccentSubject>(Colour))
                    Surface->Edge(Spanning(Swatch.MinimumX - 3.0f, Swatch.MinimumY - 3.0f, 38.0f, 38.0f),
                                  WithOpacity(White, .55f), 2.0f, 19.0f, CornerAll);
                if (Disclosure > .95f && Pressed(90u + Index * 8u + Colour, Swatch))
                {
                    Configuration.SemanticColours[Index] = static_cast<AccentSubject>(Colour);
                    if (Index == 0u) Configuration.Primary = static_cast<AccentSubject>(Colour);
                }
            }
            Surface->Release();
        }
        Cursor += Height;
    }
}

void ControlCentrePanel::SetFontFamilies(FontLoader& Loader)
{
    FontArchive = &Loader;
}

void ControlCentrePanel::FontsPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                   const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const char* DefaultFamily = "Inter";
    const std::uint32_t FontCount = (FontArchive != nullptr && FontArchive->FamilyCount() > 0u)
                                  ? FontArchive->FamilyCount() : 1u;
    const auto FamilyAt = [&](std::uint32_t Index) -> const char*
    {
        return (FontArchive != nullptr && FontArchive->FamilyCount() > 0u)
             ? FontArchive->FamilyName(Index)
             : DefaultFamily;
    };
    const float Inset = 28.0f;
    const float ContentLeft = Extent.MinimumX + Inset;
    const float ContentRight = Extent.MaximumX - Inset;
    const float SpecimenTop = Extent.MinimumY + Inset + 230.0f;

    // 📐 The section card is sized from its content rather than from a fixed figure, because every role row
    //    now carries a family strip: the eight strips add a constant 40px each, and the tallest sample text
    //    still decides the row. A card that ended mid-content would draw the icon and antialiasing sections
    //    on the bare page ground.
    const auto EntryHeightOf = [](std::uint32_t Size) -> float
    {
        const float Sample = static_cast<float>(Size);
        return (Sample + 180.0f > 190.0f) ? Sample + 180.0f : 190.0f;
    };
    float ContentBottom = SpecimenTop + 176.0f + 30.0f;
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
        ContentBottom += EntryHeightOf(Configuration.TypographySize[Index]) + 12.0f;
    ContentBottom += 340.0f;   // the icon style, icon font and antialiasing sections below the roles

    const PlaneExtent Section = Spanning(Extent.MinimumX, Extent.MinimumY, Extent.Width(),
                                         ContentBottom - Extent.MinimumY + 24.0f);
    Surface->Ground(Section, WithOpacity(Theme.Card, 1.0f),
                    static_cast<float>(Configuration.Radius < 24u ? 24u : Configuration.Radius), CornerAll);
    Surface->Edge(Section, Theme.Edge, 1.0f,
                  static_cast<float>(Configuration.Radius < 24u ? 24u : Configuration.Radius), CornerAll);
    Surface->TextRun(ContentLeft, Extent.MinimumY + Inset, Theme.Primary, "Typography", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 0u));
    Surface->TextRun(ContentLeft, Extent.MinimumY + Inset + 32.0f, Theme.Secondary, "Typeface & scale",
                     14.0f, 0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 5u));
    const float RailY = Extent.MinimumY + Inset + 64.0f;
    const PlaneExtent Left = Spanning(ContentLeft, RailY + 46.0f, 44.0f, 44.0f);
    const PlaneExtent Right = Spanning(ContentRight - 44.0f, RailY + 46.0f, 44.0f, 44.0f);
    const PlaneExtent FontRail = {Left.MaximumX + 12.0f, RailY,
                                  Right.MinimumX - 12.0f, RailY + 136.0f};

    const float FontFraction = static_cast<float>(Motion->Eased(FontMotion).Current());
    FontScroll = FontFrom + (FontTarget - FontFrom) * FontFraction;
    Surface->Confine(FontRail);
    for (std::uint32_t Index = 0u; Index < FontCount; ++Index)
    {
        const PlaneExtent Tile = Spanning(FontRail.MinimumX + 4.0f +
                                              208.0f * static_cast<float>(Index) - FontScroll,
                                          RailY, 192.0f, 132.0f);
        Surface->ApplyFontPreview(FontArchive != nullptr ? FontArchive->Preview(FamilyAt(Index), 1.0f) : nullptr);
        Surface->Ground(Tile, Configuration.Font == Index ? Theme.Card : Theme.Panel, 16.0f, CornerAll);
        Surface->Edge(Tile, Configuration.Font == Index ? Theme.Edge : WithOpacity(Theme.Edge, 0.0f), 1.0f,
                      16.0f, CornerAll);
        Surface->TextRun(Tile.MinimumX + 18.0f, Tile.MinimumY + 18.0f, Theme.Primary, "Aa", 30.0f);
        Surface->TextRun(Tile.MinimumX + 18.0f, Tile.MinimumY + 66.0f, Theme.Primary, FamilyAt(Index),
                         14.0f, 0.0f, true);
        Surface->TextRun(Tile.MinimumX + 18.0f, Tile.MinimumY + 92.0f, Theme.Secondary,
                         "The quick brown fox", 12.0f);
        const PlaneExtent TileContact = {
            Tile.MinimumX > FontRail.MinimumX ? Tile.MinimumX : FontRail.MinimumX,
            Tile.MinimumY,
            Tile.MaximumX < FontRail.MaximumX ? Tile.MaximumX : FontRail.MaximumX,
            Tile.MaximumY
        };
        if (TileContact.MaximumX > TileContact.MinimumX && 130u + Index < ControlCapacity &&
            Pressed(130u + Index, TileContact))
            Configuration.Font = Index;
    }
    Surface->Release();
    Surface->ApplyFontPreview(nullptr);

    Surface->Ground(Left, Theme.Card, 22.0f, CornerAll);
    Surface->Ground(Right, Theme.Card, 22.0f, CornerAll);
    Surface->Edge(Left, Theme.Edge, 1.0f, 22.0f, CornerAll);
    Surface->Edge(Right, Theme.Edge, 1.0f, 22.0f, CornerAll);
    Surface->TextRun(CentreText(*Surface, Left, "<", 20.0f), CentredY(*Surface, Left, 20.0f),
                     Theme.Primary, "<", 20.0f, 0.0f, true);
    Surface->TextRun(CentreText(*Surface, Right, ">", 20.0f), CentredY(*Surface, Right, 20.0f),
                     Theme.Primary, ">", 20.0f, 0.0f, true);

    const float FontMaximum = static_cast<float>(FontCount) * 208.0f - FontRail.Width();
    if (Pressed(142u, Left))
    {
        FontFrom = FontScroll;
        FontTarget = FontScroll - 250.0f;
        if (FontTarget < 0.0f) FontTarget = 0.0f;
        Motion->Eased(FontMotion).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
    }
    if (Pressed(143u, Right))
    {
        FontFrom = FontScroll;
        FontTarget = FontScroll + 250.0f;
        if (FontTarget > FontMaximum) FontTarget = FontMaximum;
        Motion->Eased(FontMotion).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
    }

    const PlaneExtent Specimen = Spanning(ContentLeft, SpecimenTop, ContentRight - ContentLeft, 176.0f);
    Surface->Ground(Specimen, Theme.Card, static_cast<float>(Configuration.Radius < 24u ? 24u : Configuration.Radius),
                    CornerAll);
    Surface->Edge(Specimen, Theme.Edge, 1.0f, 24.0f, CornerAll);
    Surface->TextRun(Specimen.MinimumX + 32.0f, Specimen.MinimumY + 25.0f, Theme.Secondary, "TYPEFACE & COLORS",
                     12.0f, .12f, false, RoleWeightOf(Configuration.TypographyWeight, 5u));
    Surface->TextRun(Specimen.MinimumX + 32.0f, Specimen.MinimumY + 58.0f, Theme.Primary, FamilyAt(Configuration.Font),
                     48.0f, 0.0f, true);
    Surface->TextRun(Specimen.MaximumX - 330.0f, Specimen.MinimumY + 45.0f, Theme.Secondary,
                     "ABCDEFGHIJKLMNOPQRSTUVWXYZ", 13.0f);
    Surface->TextRun(Specimen.MaximumX - 330.0f, Specimen.MinimumY + 70.0f, Theme.Secondary,
                     "abcdefghijklmnopqrstuvwxyz", 13.0f);
    Surface->TextRun(Specimen.MaximumX - 330.0f, Specimen.MinimumY + 95.0f, Theme.Secondary, "0123456789", 13.0f);

    static const char* Roles[8] = {"Title", "Header", "Subheader", "Body", "Label", "Caption", "Warning", "Alert"};
    static const std::uint32_t Minimum[8] = {20u, 16u, 12u, 10u, 8u, 8u, 10u, 10u};
    static const std::uint32_t Maximum[8] = {64u, 40u, 32u, 24u, 20u, 16u, 24u, 24u};
    float Cursor = Specimen.MaximumY + 30.0f;
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
    {
        const float PreviewText = static_cast<float>(Configuration.TypographySize[Index]);
        const float EntryHeight = EntryHeightOf(Configuration.TypographySize[Index]);
        const PlaneExtent Entry = Spanning(ContentLeft, Cursor, ContentRight - ContentLeft, EntryHeight);
        Surface->Ground(Entry, Theme.Card, 16.0f, CornerAll);
        Surface->Edge(Entry, Theme.Edge, 1.0f, 16.0f, CornerAll);

        // 📝 The role name is drawn in the role's own face, so the strip's choice is legible in the label
        //    that owns it — a Title row whose strip applied Black names itself in Black.
        const FontWeight RoleWeight = RoleWeightOf(Configuration.TypographyWeight, Index);
        Surface->TextRun(Entry.MinimumX + 18.0f, Entry.MinimumY + 12.0f, Theme.Primary, Roles[Index],
                         16.0f, 0.0f, false, RoleWeight);
        const float LabelWidth = Surface->MeasureRun(Roles[Index], 16.0f, 0.0f, RoleWeight);
        Surface->TextRun(Entry.MinimumX + 18.0f + LabelWidth + 14.0f, Entry.MinimumY + 17.0f,
                         Theme.Secondary, FamilyAt(Configuration.Font), 12.0f);

        // 📝 The same carousel the family rail above presents, driven by the family the main rail applies:
        //    one tile per available weight of that family, every tile drawing "Aa" in its own face. The
        //    press applies the weight for the role and nothing else, and the strip scrolls like the main rail.
        const float StripY = Entry.MinimumY + 40.0f;
        const PlaneExtent LeftArrow = Spanning(Entry.MinimumX + 18.0f, StripY + 21.0f, 26.0f, 30.0f);
        const PlaneExtent RightArrow = Spanning(Entry.MaximumX - 44.0f, StripY + 21.0f, 26.0f, 30.0f);
        const PlaneExtent RoleRail = {LeftArrow.MaximumX + 8.0f, StripY,
                                      RightArrow.MinimumX - 8.0f, StripY + 72.0f};

        const std::uint32_t WeightCount = [&]() -> std::uint32_t
        {
            std::uint32_t Count = 0u;
            for (const FontWeight Candidate : CandidateFaces)
            {
                if (FontArchive != nullptr && !FontArchive->HasFace(Candidate, FontSlant::Upright))
                    continue;
                ++Count;
            }
            return Count;
        }();
        constexpr float TileStep = 132.0f;
        constexpr float TileSpan = 120.0f;
        const float RoleMaximum = (static_cast<float>(WeightCount) * TileStep > RoleRail.Width())
                                ? static_cast<float>(WeightCount) * TileStep - RoleRail.Width() : 0.0f;

        const float RoleFraction = static_cast<float>(Motion->Eased(RoleFontMotion[Index]).Current());
        RoleFontScroll[Index] = RoleFontFrom[Index] +
                                  (RoleFontTarget[Index] - RoleFontFrom[Index]) * RoleFraction;

        if (Pressed(RoleArrowBase + Index * 2u, LeftArrow) && RoleFontTarget[Index] > 0.0f)
        {
            RoleFontFrom[Index] = RoleFontScroll[Index];
            RoleFontTarget[Index] = RoleFontScroll[Index] - TileStep;
            if (RoleFontTarget[Index] < 0.0f) RoleFontTarget[Index] = 0.0f;
            Motion->Eased(RoleFontMotion[Index]).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
        }
        if (Pressed(RoleArrowBase + Index * 2u + 1u, RightArrow) && RoleFontTarget[Index] < RoleMaximum)
        {
            RoleFontFrom[Index] = RoleFontScroll[Index];
            RoleFontTarget[Index] = RoleFontScroll[Index] + TileStep;
            if (RoleFontTarget[Index] > RoleMaximum) RoleFontTarget[Index] = RoleMaximum;
            Motion->Eased(RoleFontMotion[Index]).Depart(0.0, 1.0, 250.0, 0.0, EaseCurve::Carousel);
        }

        Surface->Ground(LeftArrow, Theme.Card, 15.0f, CornerAll);
        Surface->Ground(RightArrow, Theme.Card, 15.0f, CornerAll);
        Surface->Edge(LeftArrow, Theme.Edge, 1.0f, 15.0f, CornerAll);
        Surface->Edge(RightArrow, Theme.Edge, 1.0f, 15.0f, CornerAll);
        Surface->TextRun(CentreText(*Surface, LeftArrow, "<", 14.0f), CentredY(*Surface, LeftArrow, 14.0f),
                         Theme.Primary, "<", 14.0f, 0.0f, true);
        Surface->TextRun(CentreText(*Surface, RightArrow, ">", 14.0f), CentredY(*Surface, RightArrow, 14.0f),
                         Theme.Primary, ">", 14.0f, 0.0f, true);

        Surface->Confine(RoleRail);
        std::uint32_t FaceIndex = 0u;
        for (std::uint32_t Candidate = 0u; Candidate < 9u; ++Candidate)
        {
            if (FontArchive != nullptr && !FontArchive->HasFace(CandidateFaces[Candidate], FontSlant::Upright))
                continue;
            const float FaceX = RoleRail.MinimumX + 4.0f +
                                    TileStep * static_cast<float>(FaceIndex) - RoleFontScroll[Index];
            const PlaneExtent Tile = Spanning(FaceX, StripY, TileSpan, 72.0f);
            const bool Selected = Configuration.TypographyWeight[Index] ==
                                  static_cast<std::uint32_t>(CandidateFaces[Candidate]);
            const PlaneExtent TileContact = {
                Tile.MinimumX > RoleRail.MinimumX ? Tile.MinimumX : RoleRail.MinimumX,
                Tile.MinimumY,
                Tile.MaximumX < RoleRail.MaximumX ? Tile.MaximumX : RoleRail.MaximumX,
                Tile.MaximumY
            };
            if (TileContact.MaximumX > TileContact.MinimumX)
            {
                if (FontArchive != nullptr)
                    Surface->ApplyFontPreview(FontArchive->Face(CandidateFaces[Candidate], FontSlant::Upright));
                Surface->Ground(Tile, Selected ? Theme.Card : Theme.Panel, 10.0f, CornerAll);
                Surface->Edge(Tile, Selected ? Theme.Edge : WithOpacity(Theme.Edge, 0.0f), 1.0f, 10.0f, CornerAll);
                Surface->TextRun(Tile.MinimumX + 10.0f, Tile.MinimumY + 7.0f, Theme.Primary, "Aa", 22.0f);
                Surface->TextRun(Tile.MinimumX + 10.0f, Tile.MinimumY + 38.0f, Theme.Primary,
                                 FaceNames[Candidate], 10.5f, 0.0f, true);
                Surface->TextRun(Tile.MinimumX + 10.0f, Tile.MinimumY + 54.0f, Theme.Secondary,
                                 FamilyAt(Configuration.Font), 9.5f);
                // 📝 The ordinal is the tile's VISIBLE slot, not its index in the face run: the strip scrolls
                //    a whole tile at a time, so slot arithmetic is exact, and a slot's identity must not move
                //    when the run beneath it changes.
                const std::uint32_t Slot = FaceIndex -
                                           static_cast<std::uint32_t>(RoleFontScroll[Index] / TileStep + 0.5f);
                if (Slot < RoleTilePositions &&
                    Pressed(RoleTileBase + Index * RoleTilePositions + Slot, TileContact))
                    Configuration.TypographyWeight[Index] = static_cast<std::uint32_t>(CandidateFaces[Candidate]);
                Surface->ApplyFontPreview(nullptr);
            }
            ++FaceIndex;
        }
        Surface->Release();

        Slider(144u + Index,
               Spanning(Entry.MinimumX + 18.0f, Entry.MinimumY + 130.0f,
                        420.0f, 34.0f),
               Minimum[Index], Maximum[Index], Configuration.TypographySize[Index], "px", Theme.Edge, Accent);

        const PlaneExtent PreviewClip = {Entry.MinimumX + 18.0f, Entry.MinimumY + 170.0f,
                                         Entry.MaximumX - 18.0f, Entry.MaximumY - 10.0f};
        const float PreviewHeight = PreviewClip.MinimumY +
                                    (PreviewClip.Height() - PreviewText) * 0.5f;
        Surface->Confine(PreviewClip);
        Surface->TextRunTruncated(PreviewClip.MinimumX, PreviewHeight, PreviewClip.MaximumX,
                                  Index == 6u   ? ThemeSpecification::Accent(Configuration.Warning).Colour
                                  : Index == 7u ? ThemeSpecification::Accent(Configuration.Alert).Colour
                                                  : Theme.Primary,
                                  Index == 4u   ? "METADATA · 10:42 AM · SYSTEM"
                                  : Index == 5u ? "* This is a small caption text"
                                                  : "The quick brown fox jumps over the lazy dog",
                                  PreviewText, false, RoleWeight);
        Surface->Release();
        Cursor = Entry.MaximumY + 12.0f;
    }

    const PlaneExtent IconSection = Spanning(ContentLeft, Cursor - 4.0f,
                                             ContentRight - ContentLeft, 224.0f);
    Surface->Ground(IconSection, Theme.Card, 20.0f, CornerAll);
    Surface->Edge(IconSection, Theme.Edge, 1.0f, 20.0f, CornerAll);
    Surface->TextRun(IconSection.MinimumX + 20.0f, Cursor + 10.0f, Theme.Primary,
                     "Icon Style", 24.0f, 0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    const char* Styles[3] = {"Monotone", "Duotone", "Coloured"};
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent B = Spanning(IconSection.MinimumX + 20.0f +
                                           (IconSection.Width() - 40.0f) / 3.0f * Index,
                                       Cursor + 52.0f, (IconSection.Width() - 40.0f) / 3.0f, 42.0f);
        Surface->Ground(B, Configuration.Icons == static_cast<IconAppearance>(Index) ? Theme.Card : QuietDark, 12.0f,
                        CornerAll);
        Surface->TextRun(CentreText(*Surface, B, Styles[Index], 13.0f), CentredY(*Surface, B, 13.0f), Theme.Primary,
                         Styles[Index], 13.0f);
        if (Pressed(160u + Index, B)) Configuration.Icons = static_cast<IconAppearance>(Index);
    }
    Cursor += 118.0f;
    Surface->TextRun(IconSection.MinimumX + 20.0f, Cursor, Theme.Primary, "Icon Font", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    Slider(164u, Spanning(IconSection.MinimumX + 20.0f, Cursor + 40.0f, 420.0f, 40.0f), 16u, 48u,
           Configuration.IconSize, "px", Theme.Edge, Accent);
    for (std::uint32_t Icon = 0u; Icon < 4u; ++Icon)
        Symbol(Spanning(Extent.MaximumX - 220.0f + 50.0f * Icon, Cursor + 30.0f,
                        static_cast<float>(Configuration.IconSize), static_cast<float>(Configuration.IconSize)),
               Theme.Primary);
    Cursor += 124.0f;
    const PlaneExtent AntialiasSection = Spanning(ContentLeft, Cursor - 18.0f,
                                                  ContentRight - ContentLeft, 116.0f);
    Surface->Ground(AntialiasSection, Theme.Card, 20.0f, CornerAll);
    Surface->Edge(AntialiasSection, Theme.Edge, 1.0f, 20.0f, CornerAll);
    Surface->TextRun(AntialiasSection.MinimumX + 20.0f, Cursor, Theme.Primary,
                     "Interface Antialiasing", 24.0f, 0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    const char* Aa[3] = {"Refined", "Basic", "None"};
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent B = Spanning(AntialiasSection.MinimumX + 20.0f +
                                           (AntialiasSection.Width() - 40.0f) / 3.0f * Index,
                                       Cursor + 45.0f, (AntialiasSection.Width() - 40.0f) / 3.0f, 42.0f);
        Surface->Ground(B, static_cast<std::uint32_t>(Configuration.GeometryAntialiasing) == Index
                           ? Theme.Card : QuietDark, 12.0f, CornerAll);
        Surface->TextRun(CentreText(*Surface, B, Aa[Index], 13.0f), CentredY(*Surface, B, 13.0f), Theme.Primary,
                         Aa[Index], 13.0f);
        if (Pressed(168u + Index, B))
            Configuration.GeometryAntialiasing = static_cast<InterfaceAntialiasing>(Index);
    }
}


void ControlCentrePanel::InputPage(const PlaneExtent& Extent, ControlCentreConfiguration& Configuration,
                                   const ThemeDeclaration& Theme, ThemeToken Accent)
{
    const float Width = (Extent.Width() < 768.0f) ? Extent.Width() : 768.0f;
    const float X = Extent.MinimumX + (Extent.Width() - Width) * .5f;
    Surface->TextRun(X, Extent.MinimumY, Theme.Primary, "Input Devices", 29.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 0u));
    Surface->TextRun(X, Extent.MinimumY + 38.0f, Theme.Secondary, "Keyboard, mouse, and touch settings", 14.0f);
    const PlaneExtent Back = Spanning(X + Width - 44.0f, Extent.MinimumY, 40.0f, 40.0f);
    Surface->Ground(Back, Theme.Card, 20.0f, CornerAll);
    Symbol(Spanning(Back.MinimumX + 10.0f, Back.MinimumY + 10.0f, 20.0f, 20.0f), Theme.Primary);
    if (Pressed(172u, Back))
    {
        Configuration.Page = ControlCentrePage::Settings;
        Navigate(Configuration.Page);
    }
    const PlaneExtent Hotkeys = Spanning(X, Extent.MinimumY + 76.0f, Width, 620.0f);
    Surface->Ground(Hotkeys, Theme.Card, static_cast<float>(Configuration.Radius < 16u ? 16u : Configuration.Radius),
                    CornerAll);
    Surface->Edge(Hotkeys, Theme.Edge, 1.0f, 20.0f, CornerAll);
    Surface->TextRun(Hotkeys.MinimumX + 28.0f, Hotkeys.MinimumY + 24.0f, Theme.Primary, "Global Hotkeys", 24.0f,
                     0.0f, false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    const PlaneExtent Preset = Spanning(Hotkeys.MaximumX - 184.0f, Hotkeys.MinimumY + 19.0f, 156.0f, 34.0f);
    Surface->Ground(Preset, QuietDark, 8.0f, CornerAll);
    Surface->TextRun(Preset.MinimumX + 12.0f, CentredY(*Surface, Preset, 12.0f), Theme.Primary,
                     ShortcutSpecification::Caption(Configuration.InputPreset), 12.0f);
    if (Pressed(173u, Preset)) InputPresetOpen = !InputPresetOpen;
    std::uint32_t Count = 0u;
    const ShortcutDeclaration* Shortcuts = ShortcutSpecification::Shortcuts(Configuration.InputPreset, Count);
    for (std::uint32_t Index = 0u; Index < Count; ++Index)
    {
        const PlaneExtent Row = Spanning(Hotkeys.MinimumX + 24.0f, Hotkeys.MinimumY + 76.0f + 62.0f * Index,
                                         Hotkeys.Width() - 48.0f, 54.0f);
        Surface->Ground(Row, Partial(0xFFFFFFu, .02), 12.0f, CornerAll);
        Surface->Edge(Row, Theme.Edge, 1.0f, 12.0f, CornerAll);
        Surface->TextRun(Row.MinimumX + 14.0f, Row.MinimumY + 10.0f, Theme.Primary, Shortcuts[Index].Action,
                         14.0f, 0.0f, true);
        Surface->TextRun(Row.MinimumX + 14.0f, Row.MinimumY + 31.0f, Theme.Secondary, Shortcuts[Index].Grouping,
                         11.0f);
        float KeyX = Row.MaximumX - 160.0f;
        if (Shortcuts[Index].Chord.ControlEnabled)
        {
            Surface->Ground(Spanning(KeyX, Row.MinimumY + 11.0f, 38.0f, 32.0f), QuietDark, 8.0f, CornerAll);
            Surface->TextRun(KeyX + 7.0f, Row.MinimumY + 21.0f, Theme.Secondary, "Ctrl", 11.0f);
            KeyX += 44.0f;
        }
        const PlaneExtent Key = Spanning(KeyX, Row.MinimumY + 11.0f, 96.0f, 32.0f);
        Surface->Ground(Key, QuietDark, 8.0f, CornerAll);
        Surface->Edge(Key, Theme.Edge, 1.0f, 8.0f, CornerAll);
        Surface->TextRun(
            CentreText(*Surface, Key,
                       Configuration.ListeningShortcut == Index ? "Listening..." : Shortcuts[Index].Chord.Key, 11.0f),
            CentredY(*Surface, Key, 11.0f), Theme.Primary,
            Configuration.ListeningShortcut == Index ? "Listening..." : Shortcuts[Index].Chord.Key, 11.0f);
        if (!InputPresetOpen && Pressed(174u + Index, Key))
            Configuration.ListeningShortcut = Configuration.ListeningShortcut == Index ? 0xFFFFFFFFu : Index;
    }
    const float MouseTop = Hotkeys.MaximumY + 24.0f;
    const PlaneExtent Mouse = Spanning(X, MouseTop, Width, 150.0f);
    Surface->Ground(Mouse, Theme.Card, 20.0f, CornerAll);
    Surface->TextRun(Mouse.MinimumX + 28.0f, Mouse.MinimumY + 24.0f, Theme.Primary, "Mouse Settings", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    Surface->TextRun(Mouse.MinimumX + 28.0f, Mouse.MinimumY + 72.0f, Theme.Primary, "Invert Scroll Direction",
                     14.0f);
    Toggle(184u, Spanning(Mouse.MaximumX - 76.0f, Mouse.MinimumY + 64.0f, 48.0f, 24.0f), Configuration.InvertScroll,
           Theme.Edge, Accent);
    Surface->TextRun(Mouse.MinimumX + 28.0f, Mouse.MinimumY + 116.0f, Theme.Primary, "Pointer Speed", 14.0f);
    Slider(185u, Spanning(Mouse.MaximumX - 388.0f, Mouse.MinimumY + 100.0f, 360.0f, 40.0f), 1u, 10u,
           Configuration.PointerSpeed, "", Theme.Edge, Accent);
    const PlaneExtent Touch = Spanning(X, Mouse.MaximumY + 24.0f, Width, 190.0f);
    Surface->Ground(Touch, Theme.Card, 20.0f, CornerAll);
    Surface->TextRun(Touch.MinimumX + 28.0f, Touch.MinimumY + 24.0f, Theme.Primary, "Touch & Stylus", 24.0f, 0.0f,
                     false, RoleWeightOf(Configuration.TypographyWeight, 1u));
    Surface->TextRun(Touch.MinimumX + 28.0f, Touch.MinimumY + 72.0f, Theme.Primary, "Enable Touch Gestures",
                     14.0f);
    Toggle(186u, Spanning(Touch.MaximumX - 76.0f, Touch.MinimumY + 64.0f, 48.0f, 24.0f), Configuration.TouchGestures,
           Theme.Edge, Accent);
    Surface->TextRun(Touch.MinimumX + 28.0f, Touch.MinimumY + 114.0f, Theme.Primary, "Stylus Pressure Sensitivity",
                     14.0f);
    Toggle(187u, Spanning(Touch.MaximumX - 76.0f, Touch.MinimumY + 106.0f, 48.0f, 24.0f), Configuration.PressureEnabled,
           Theme.Edge, Accent);
    const char* Actions[3] = {"Orbit", "Pan", "Select"};
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        const PlaneExtent B =
            Spanning(Touch.MaximumX - 260.0f + 78.0f * Index, Touch.MinimumY + 146.0f, 72.0f, 32.0f);
        Surface->Ground(B, Configuration.TouchAction == Index ? Theme.Card : QuietDark, 8.0f, CornerAll);
        Surface->TextRun(CentreText(*Surface, B, Actions[Index], 11.0f), CentredY(*Surface, B, 11.0f), Theme.Primary,
                         Actions[Index], 11.0f);
        if (Pressed(188u + Index, B)) Configuration.TouchAction = Index;
    }

    if (InputPresetOpen)
    {
        const PlaneExtent Menu = Spanning(Preset.MinimumX, Preset.MaximumY + 6.0f, Preset.Width(), 108.0f);
        Surface->Ground(Menu, Theme.Card, 10.0f, CornerAll);
        Surface->Edge(Menu, Theme.Edge, 1.0f, 10.0f, CornerAll);
        for (std::uint32_t Index = 0u; Index < 3u; ++Index)
        {
            const PlaneExtent Option = Spanning(Menu.MinimumX + 4.0f, Menu.MinimumY + 4.0f + 34.0f * Index,
                                                Menu.Width() - 8.0f, 32.0f);
            if (Configuration.InputPreset == static_cast<ShortcutPreset>(Index))
                Surface->Ground(Option, QuietDark, 7.0f, CornerAll);
            Surface->TextRun(Option.MinimumX + 10.0f, CentredY(*Surface, Option, 11.0f), Theme.Primary,
                             ShortcutSpecification::Caption(static_cast<ShortcutPreset>(Index)), 11.0f);
            if (Pressed(120u + Index, Option))
            {
                Configuration.InputPreset = static_cast<ShortcutPreset>(Index);
                InputPresetOpen = false;
            }
        }
    }
}

void ControlCentrePanel::Reset()
{
    Interaction.Reset();
    SettingsNotice.Reset();
    Motion = nullptr;
    Surface = nullptr;
    Appearance = nullptr;
    Pointer = {};
    CurrentPage = ControlCentrePage::Dashboard;
    PreviousPage = CurrentPage;
    PageMotion = 0u;
    TabMotion = 0u;
    ThemeMotion = 0u;
    FontMotion = 0u;
    CurrentTheme = ThemeSubject::Oled;
    PreviousTheme = ThemeSubject::Oled;
    for (std::uint32_t Index = 0u;
         Index < static_cast<std::uint32_t>(ControlCentrePage::PageCount); ++Index)
    {
        ScrollMotion[Index] = 0u;
        Scroll[Index] = 0.0f;
        ScrollFrom[Index] = 0.0f;
        ScrollTarget[Index] = 0.0f;
    }
    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        DisplayScrollMotion[Index] = 0u;
        DisplayScroll[Index] = 0.0f;
        DisplayScrollFrom[Index] = 0.0f;
        DisplayScrollTarget[Index] = 0.0f;
    }
    FontScroll = 0.0f;
    FontFrom = 0.0f;
    FontTarget = 0.0f;
    for (std::uint32_t Index = 0u; Index < 8u; ++Index)
    {
        RoleFontMotion[Index] = 0u;
        RoleFontScroll[Index] = 0.0f;
        RoleFontFrom[Index] = 0.0f;
        RoleFontTarget[Index] = 0.0f;
    }
    OpenPalette = 5u;
    InputPresetOpen = false;
    WorkingConfigurationReady = false;
    WorkingConfiguration = {};
    AppliedConfiguration = {};
}

} // namespace Slate
