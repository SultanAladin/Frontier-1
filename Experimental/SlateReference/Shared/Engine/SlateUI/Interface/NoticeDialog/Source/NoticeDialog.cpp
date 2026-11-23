//============================================================================================================================================
//                                                          NOTICEDIALOG.CPP
//============================================================================================================================================

#include "SlateUI/Interface/NoticeDialog/Api/NoticeDialog.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace Slate
{
namespace
{
ThemeToken WithOpacity(ThemeToken Colour, float Fraction)
{
    Colour.Opacity = static_cast<std::uint8_t>(static_cast<float>(Colour.Opacity) * Fraction + 0.5f);
    return Colour;
}

void Retain(char* Destination, std::size_t Capacity, const char* Source)
{
    if (Capacity == 0u) return;
    std::snprintf(Destination, Capacity, "%s", Source != nullptr ? Source : "");
}

void RecordWrapped(RecordingSurface& Surface, const PlaneExtent& Extent, ThemeToken Colour,
                   const char* Text, float Size, FontWeight Weight)
{
    char Line[192] = {};
    std::uint32_t Occupied = 0u;
    std::uint32_t Cursor = 0u;
    std::uint32_t LineIndex = 0u;
    while (Text[Cursor] != '\0' && LineIndex < 3u)
    {
        while (Text[Cursor] == ' ') ++Cursor;
        const std::uint32_t Start = Cursor;
        while (Text[Cursor] != '\0' && Text[Cursor] != ' ') ++Cursor;
        const std::uint32_t WordLength = Cursor - Start;
        if (WordLength == 0u) break;
        char Candidate[192] = {};
        std::uint32_t CandidateLength = Occupied;
        for (std::uint32_t Index = 0u; Index < Occupied; ++Index) Candidate[Index] = Line[Index];
        if (CandidateLength > 0u) Candidate[CandidateLength++] = ' ';
        for (std::uint32_t Index = 0u; Index < WordLength && CandidateLength + 1u < 192u; ++Index)
            Candidate[CandidateLength++] = Text[Start + Index];
        Candidate[CandidateLength] = '\0';
        if (Occupied > 0u && Surface.MeasureRun(Candidate, Size, 0.0f, Weight) > Extent.Width())
        {
            Surface.TextRun(Extent.MinimumX, Extent.MinimumY + static_cast<float>(LineIndex) *
                            (Surface.ResolveTypographySize(Size) + 7.0f),
                            Colour, Line, Size, 0.0f, false, Weight);
            ++LineIndex;
            Occupied = 0u;
        }
        if (Occupied > 0u && Occupied + 1u < 192u) Line[Occupied++] = ' ';
        for (std::uint32_t Index = 0u; Index < WordLength && Occupied + 1u < 192u; ++Index)
            Line[Occupied++] = Text[Start + Index];
        Line[Occupied] = '\0';
    }
    if (Occupied > 0u && LineIndex < 3u)
        Surface.TextRunTruncated(Extent.MinimumX,
                                 Extent.MinimumY + static_cast<float>(LineIndex) *
                                 (Surface.ResolveTypographySize(Size) + 7.0f),
                                 Extent.MaximumX, Colour, Line, Size, false, Weight);
}
}

Deliver<bool> NoticeDialog::ConstructNoticeDialog(MotionIntegrator& IncomingMotion,
                                                   RecordingSurface& IncomingSurface)
{
    if (Surface != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                      "a notice dialog construction already stands" });
    Motion = &IncomingMotion;
    Surface = &IncomingSurface;
    if (!Interaction.AttachMotion(IncomingMotion).Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                      "the notice dialog interaction index was rejected" });
    const Deliver<ControlIdentity> Accepted = Interaction.Register();
    const Deliver<ControlIdentity> Dismissed = Interaction.Register();
    if (!Accepted.Resolved || !Dismissed.Resolved)
        return Deliver<bool>::Refuse({ RefusalReason::ExtentExhausted,
                                      "the notice dialog controls were rejected" });
    Accept = Accepted.Resolve();
    Dismiss = Dismissed.Resolve();
    return Deliver<bool>::Result(true);
}

void NoticeDialog::Advance(const PointerCondition& Sampled, double ElapsedMilliseconds)
{
    Pointer = Sampled;
    Interaction.Advance(Sampled, ElapsedMilliseconds);
}

void NoticeDialog::Open(NoticeTone IncomingTone, const char* IncomingTitle, const char* IncomingMessage,
                        const char* IncomingAcceptCaption, const char* IncomingDismissCaption)
{
    Role = IncomingTone;
    Retain(Title, sizeof(Title), IncomingTitle);
    Retain(Message, sizeof(Message), IncomingMessage);
    Retain(AcceptCaption, sizeof(AcceptCaption), IncomingAcceptCaption);
    Retain(DismissCaption, sizeof(DismissCaption), IncomingDismissCaption);
    Decision = NoticeDecision::None;
    IsOpen = true;
}

bool NoticeDialog::Pressed(const ControlIdentity& Identity, const PlaneExtent& Extent)
{
    const bool Hovered = Extent.Encloses(Pointer.PositionX, Pointer.PositionY);
    if (Hovered && Pointer.ContactPressed)
        Interaction.Grab(Identity, ControlPart::Body);
    Interaction.DeclareHovered(Identity, Hovered, 120.0);
    return Interaction.Released(Identity) && Hovered;
}

void NoticeDialog::Record(const PlaneExtent& Available, const ThemeDeclaration& Theme,
                          const std::uint32_t TypographySize[8], const std::uint32_t TypographyWeight[8])
{
    if (!IsOpen || Surface == nullptr || Available.Width() <= 0.0f || Available.Height() <= 0.0f)
    {
        ModalExclusion = {};
        return;
    }

    const ThemeToken Status = ThemeSpecification::Accent(
        Role == NoticeTone::Confirmation ? AccentSubject::Emerald
        : Role == NoticeTone::Warning ? AccentSubject::Amber : AccentSubject::Rose).Colour;
    const std::uint32_t TitleRoleIndex = Role == NoticeTone::Warning ? 6u
                                          : Role == NoticeTone::Error ? 7u : 1u;
    const float TitleSize = static_cast<float>(std::clamp(TypographySize[TitleRoleIndex], 10u, 40u));
    const float BodySize = static_cast<float>(std::clamp(TypographySize[3], 10u, 24u));
    const float ButtonSize = static_cast<float>(std::clamp(TypographySize[4], 10u, 20u));
    constexpr float AuthoredBodySize = 14.0f;
    constexpr float AuthoredButtonSize = 12.0f;
    const float ButtonHeight = std::max(40.0f, ButtonSize + 22.0f);
    const float HeaderHeight = std::max(92.0f, TitleSize + 62.0f);
    const float MaximumWidth = std::max(280.0f, Available.Width() - 32.0f);
    const float MinimumWidth = std::min(420.0f, MaximumWidth);
    const float Width = std::clamp(Available.Width() * 0.52f, MinimumWidth,
                                   std::min(680.0f, MaximumWidth));
    const float DesiredHeight = HeaderHeight + std::max(112.0f, BodySize * 3.0f + 44.0f) +
                                ButtonHeight + 30.0f;
    const float Height = std::min(DesiredHeight, std::max(220.0f, Available.Height() - 32.0f));
    const PlaneExtent Card = Spanning(Available.MinimumX + (Available.Width() - Width) * 0.5f,
                                      Available.MinimumY + (Available.Height() - Height) * 0.5f,
                                      Width, Height);
    ModalExclusion = Available;

    Surface->Ground(Available, WithOpacity(Theme.Ground, 0.82f), 0.0f, CornerNone);
    Surface->Ground(Card, Theme.Panel, 18.0f, CornerAll);
    Surface->Edge(Card, Theme.Edge, 1.0f, 18.0f, CornerAll);
    const PlaneExtent Header = Spanning(Card.MinimumX, Card.MinimumY, Card.Width(), HeaderHeight);
    Surface->Ground(Header, Status, 18.0f, CornerLeadingUpper | CornerTrailingUpper);

    const float MarkSize = std::max(34.0f, TitleSize * 1.65f);
    const PlaneExtent Mark = Spanning(Header.MinimumX + 26.0f,
                                      Header.MinimumY + (Header.Height() - MarkSize) * 0.5f,
                                      MarkSize, MarkSize);
    Surface->Stroke(SymbolSubject::PlaceholderMark, Mark, Covering(0xFFFFFFu));
    Surface->TextRunRole(Mark.MaximumX + 20.0f,
                         Header.MinimumY + (Header.Height() - TitleSize) * 0.5f,
                         Covering(0xFFFFFFu), Title,
                         static_cast<RecordingSurface::TypographyRole>(TitleRoleIndex));

    const float ButtonY = Card.MaximumY - ButtonHeight - 20.0f;
    const float MessageY = Header.MaximumY + 24.0f;
    RecordWrapped(*Surface,
                  {Card.MinimumX + 28.0f, MessageY, Card.MaximumX - 28.0f,
                   ButtonY - 14.0f},
                  Theme.Primary, Message, AuthoredBodySize,
                  static_cast<FontWeight>(TypographyWeight[3]));

    const FontWeight ButtonWeight = static_cast<FontWeight>(TypographyWeight[4]);
    float DismissWidth = std::max(96.0f,
        Surface->MeasureRun(DismissCaption, AuthoredButtonSize, 0.0f, ButtonWeight) + 34.0f);
    float AcceptWidth = std::max(132.0f,
        Surface->MeasureRun(AcceptCaption, AuthoredButtonSize, 0.0f, ButtonWeight) + 38.0f);
    const float ButtonRoom = Card.Width() - 60.0f;
    if (DismissWidth + AcceptWidth > ButtonRoom)
    {
        const float Ratio = ButtonRoom / (DismissWidth + AcceptWidth);
        DismissWidth *= Ratio;
        AcceptWidth *= Ratio;
    }
    const PlaneExtent AcceptButton = Spanning(Card.MaximumX - 24.0f - AcceptWidth, ButtonY,
                                              AcceptWidth, ButtonHeight);
    const PlaneExtent DismissButton = Spanning(AcceptButton.MinimumX - 12.0f - DismissWidth, ButtonY,
                                               DismissWidth, ButtonHeight);
    Surface->Ground(DismissButton, Theme.Card, ButtonHeight * 0.5f, CornerAll);
    Surface->Edge(DismissButton, Theme.Edge, 1.0f, ButtonHeight * 0.5f, CornerAll);
    Surface->Ground(AcceptButton, Status, ButtonHeight * 0.5f, CornerAll);
    Surface->TextRun(DismissButton.MinimumX + (DismissButton.Width() -
                     Surface->MeasureRun(DismissCaption, AuthoredButtonSize, 0.0f, ButtonWeight)) * 0.5f,
                     DismissButton.MinimumY + (ButtonHeight - ButtonSize) * 0.5f,
                     Theme.Primary, DismissCaption, AuthoredButtonSize, 0.0f, false, ButtonWeight);
    Surface->TextRun(AcceptButton.MinimumX + (AcceptButton.Width() -
                     Surface->MeasureRun(AcceptCaption, AuthoredButtonSize, 0.0f, ButtonWeight)) * 0.5f,
                     AcceptButton.MinimumY + (ButtonHeight - ButtonSize) * 0.5f,
                     Covering(0xFFFFFFu), AcceptCaption, AuthoredButtonSize, 0.0f, false, ButtonWeight);

    if (Pressed(Accept, AcceptButton))
    {
        Decision = NoticeDecision::Accepted;
        IsOpen = false;
    }
    else if (Pressed(Dismiss, DismissButton))
    {
        Decision = NoticeDecision::Dismissed;
        IsOpen = false;
    }
}

NoticeDecision NoticeDialog::ConsumeDecision()
{
    const NoticeDecision Delivered = Decision;
    Decision = NoticeDecision::None;
    return Delivered;
}

void NoticeDialog::Reset()
{
    Motion = nullptr;
    Surface = nullptr;
    Interaction.Reset();
    Accept = {};
    Dismiss = {};
    Pointer = {};
    Decision = NoticeDecision::None;
    IsOpen = false;
    ModalExclusion = {};
    Title[0] = Message[0] = AcceptCaption[0] = DismissCaption[0] = '\0';
}

} // namespace Slate
