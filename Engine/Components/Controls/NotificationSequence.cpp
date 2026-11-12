/*============================================================================================================================================
                                                           NOTIFICATIONSEQUENCE.CPP
============================================================================================================================================*/
// 🧩 Semantic toast banners, word-wrapped descriptions, and Quick Controls notification stream

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/NotificationSequence.h"
#include "Engine/Components/Controls/CapsuleButton.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                               WORD-BOUNDARY LINE WRAPPER
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] std::vector<std::string> WrapDescriptionLines(
    PixelSpace&        Surface,
    float              MaxWidth,
    const std::string& Text)
{
    std::vector<std::string> Lines{};
    if (Text.empty() || MaxWidth <= 16.0f)
    {
        return Lines;
    }

    std::string CurrentLine{};
    std::size_t Cursor = 0u;
    while (Cursor < Text.size() && Lines.size() < 4u)
    {
        while (Cursor < Text.size() && Text[Cursor] == ' ')
        {
            ++Cursor;
        }
        if (Cursor >= Text.size())
        {
            break;
        }

        const std::size_t NextSpace  = Text.find(' ', Cursor);
        const std::size_t WordEnd    = (NextSpace == std::string::npos) ? Text.size() : NextSpace;
        const std::string SingleWord = Text.substr(Cursor, WordEnd - Cursor);
        const std::string Candidate  = CurrentLine.empty() ? SingleWord : (CurrentLine + " " + SingleWord);

        if (Surface.MeasureTextWidth(13.5f, Candidate.c_str()) <= MaxWidth)
        {
            CurrentLine = Candidate;
            Cursor      = WordEnd;
        }
        else if (!CurrentLine.empty())
        {
            Lines.push_back(CurrentLine);
            CurrentLine.clear();
        }
        else
        {
            Lines.push_back(SingleWord);
            Cursor = WordEnd;
        }
    }

    if (!CurrentLine.empty() && Lines.size() < 4u)
    {
        Lines.push_back(CurrentLine);
    }
    return Lines;
}

void ConstructToastSeveritySign(
    PixelSpace&          Surface,
    Point2D              Center,
    NotificationSeverity Severity,
    const ControlColorSet& Colors) noexcept
{
    const ColorRgba SignInk{ 255u, 255u, 255u, 255u };
    switch (Severity)
    {
        case NotificationSeverity::Warning:
        {
            const Point2D Triangle[3] = {
                { Center.X, Center.Y - 11.5f },
                { Center.X + 12.0f, Center.Y + 9.5f },
                { Center.X - 12.0f, Center.Y + 9.5f },
            };
            Surface.FillPolygon(std::span<const Point2D>{ Triangle, 3u }, Colors.Warning.ToRgba8());
            Surface.StrokePolyline(std::span<const Point2D>{ Triangle, 3u }, Colors.Warning.ToRgba8(), 2.2f, true);
            Surface.StrokeLine({ Center.X, Center.Y - 4.0f }, { Center.X, Center.Y + 2.5f }, ColorRgba{ 16u, 16u, 18u, 255u }, 2.2f);
            Surface.FillCircle({ Center.X, Center.Y + 6.5f }, 1.4f, ColorRgba{ 16u, 16u, 18u, 255u });
            break;
        }
        case NotificationSeverity::Success:
        {
            ControlAppearance::ConstructCircleFill(Surface, Center.X, Center.Y, 12.0f, Colors.Success);
            const Point2D CheckPoints[3] = {
                { Center.X - 4.8f, Center.Y + 0.3f },
                { Center.X - 1.2f, Center.Y + 3.9f },
                { Center.X + 5.2f, Center.Y - 3.3f },
            };
            Surface.StrokePolyline(std::span<const Point2D>{ CheckPoints, 3u }, SignInk, 2.0f, false);
            break;
        }
        case NotificationSeverity::Error:
        {
            ControlAppearance::ConstructCircleFill(Surface, Center.X, Center.Y, 12.0f, Colors.Danger);
            Surface.StrokeLine({ Center.X - 4.0f, Center.Y - 4.0f }, { Center.X + 4.0f, Center.Y + 4.0f }, SignInk, 2.0f);
            Surface.StrokeLine({ Center.X + 4.0f, Center.Y - 4.0f }, { Center.X - 4.0f, Center.Y + 4.0f }, SignInk, 2.0f);
            break;
        }
        case NotificationSeverity::Notice:
        default:
        {
            const ColorQuad NeutralDisc = Colors.LightSurface
                                              ? ColorQuad{ 0.35f, 0.38f, 0.44f, 1.0f }
                                              : ColorQuad{ 0.48f, 0.50f, 0.54f, 1.0f };
            ControlAppearance::ConstructCircleFill(Surface, Center.X, Center.Y, 12.0f, NeutralDisc);
            Surface.FillCircle({ Center.X, Center.Y - 4.5f }, 1.3f, SignInk);
            Surface.StrokeLine({ Center.X, Center.Y - 1.2f }, { Center.X, Center.Y + 5.2f }, SignInk, 2.0f);
            break;
        }
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             NOTIFICATION SEQUENCE METHODS
//------------------------------------------------------------------------------------------------------------------------

uint64_t NotificationSequence::Broadcast(
    NotificationSeverity Severity,
    const std::string&   Title,
    const std::string&   Description,
    double               HoldSeconds,
    bool                 PresentationEnabled)
{
    if (Title.empty() && Description.empty())
    {
        return 0u;
    }

    const uint64_t Token = NextToken++;
    const double   Hold  = std::isfinite(HoldSeconds) ? std::clamp(HoldSeconds, 1.0, 10.0) : 3.5;

    NotificationEntry Entry{};
    Entry.Token            = Token;
    Entry.Severity         = Severity;
    Entry.Title            = Title;
    Entry.Description      = Description;
    Entry.RemainingSeconds = Hold;

    if (HistoryEntries.size() >= HistoryCapacity)
    {
        HistoryEntries.pop_front();
    }
    HistoryEntries.push_back(Entry);

    if (PresentationEnabled)
    {
        if (PendingEntries.size() >= PendingCapacity)
        {
            PendingEntries.pop_front();
        }
        PendingEntries.push_back(std::move(Entry));
    }
    return Token;
}

void NotificationSequence::Integrate(double DeltaSeconds, std::size_t VisibleCount, uint64_t PausedToken) noexcept
{
    if (!std::isfinite(DeltaSeconds) || DeltaSeconds <= 0.0 || PendingEntries.empty())
    {
        return;
    }

    const std::size_t ActiveLimit = std::min(VisibleCount, PendingEntries.size());
    for (std::size_t Index = 0u; Index < ActiveLimit; ++Index)
    {
        if (PendingEntries[Index].Token != PausedToken)
        {
            PendingEntries[Index].RemainingSeconds -= DeltaSeconds;
        }
    }

    std::size_t Checked = 0u;
    for (auto Iterator = PendingEntries.begin(); Iterator != PendingEntries.end() && Checked < ActiveLimit;)
    {
        if (Iterator->RemainingSeconds <= 0.0)
        {
            Iterator = PendingEntries.erase(Iterator);
        }
        else
        {
            ++Iterator;
        }
        ++Checked;
    }
}

bool NotificationSequence::Dismiss(uint64_t Token) noexcept
{
    for (auto Iterator = PendingEntries.begin(); Iterator != PendingEntries.end(); ++Iterator)
    {
        if (Iterator->Token == Token)
        {
            PendingEntries.erase(Iterator);
            return true;
        }
    }
    return false;
}

float NotificationSequence::QueryBannerHeight(PixelSpace& Surface, float Width, const NotificationEntry& Entry)
{
    const float TextAreaWidth = std::max(80.0f, Width - 96.0f);
    const auto  Lines         = WrapDescriptionLines(Surface, TextAreaWidth, Entry.Description);
    if (Lines.empty())
    {
        return 58.0f;
    }
    return std::max(72.0f, 46.0f + static_cast<float>(Lines.size()) * 20.0f + 16.0f);
}

bool NotificationSequence::ConstructBanner(
    PixelSpace&              Surface,
    Rectangle2D              Bounds,
    const NotificationEntry& Entry,
    const ControlPointer&    Pointer,
    bool                     DismissButtonEnabled)
{
    const ControlColorSet& Colors = ControlAppearance::Colors();

    Surface.FillRectangle(
        { Bounds.X, Bounds.Y + 2.0f, Bounds.Width, Bounds.Height },
        ColorRgba{ 0u, 0u, 0u, Colors.LightSurface ? uint8_t{ 24u } : uint8_t{ 90u } },
        14.0f);
    Surface.FillRectangle(Bounds, Colors.Panel.ToRgba8(), 14.0f);
    ControlAppearance::ConstructRoundedOutline(Surface, Bounds, Colors.StrokeStrong, 14.0f);

    ConstructToastSeveritySign(Surface, { Bounds.X + 30.0f, Bounds.Y + 30.0f }, Entry.Severity, Colors);

    const float RightGutter   = DismissButtonEnabled ? 48.0f : 20.0f;
    const float TextAreaWidth = std::max(80.0f, Bounds.Width - 54.0f - RightGutter);

    Surface.Text({ Bounds.X + 54.0f, Bounds.Y + 18.0f }, 15.0f, Colors.Text.ToRgba8(), Entry.Title.c_str());

    const auto Lines = WrapDescriptionLines(Surface, TextAreaWidth, Entry.Description);
    for (std::size_t LineIndex = 0u; LineIndex < Lines.size(); ++LineIndex)
    {
        Surface.Text(
            { Bounds.X + 54.0f, Bounds.Y + 44.0f + static_cast<float>(LineIndex) * 20.0f },
            13.5f,
            Colors.TextDim.ToRgba8(),
            Lines[LineIndex].c_str());
    }

    if (DismissButtonEnabled)
    {
        const ControlContact CloseHit = CapsuleButton::ConstructRoundIcon(
            Surface,
            Bounds.Right() - 24.0f,
            Bounds.Y + 29.0f,
            ControlIcon::CloseCross,
            Pointer);
        return CloseHit.Clicked;
    }
    return false;
}

void NotificationSequence::ConstructToastOverlay(
    PixelSpace&           Surface,
    Rectangle2D           ViewportLogicalBounds,
    const ControlPointer& Pointer,
    double                DeltaSeconds)
{
    if (PendingEntries.empty())
    {
        return;
    }

    const float       ToastWidth  = std::clamp(ViewportLogicalBounds.Width - 32.0f, 260.0f, 420.0f);
    const float       LeftX       = ViewportLogicalBounds.Right() - 16.0f - ToastWidth;
    float             CursorY     = ViewportLogicalBounds.Y + 16.0f;
    uint64_t          PausedToken = 0u;
    uint64_t          ClosedToken = 0u;
    const std::size_t Count       = std::min<std::size_t>(VisibleCapacity, PendingEntries.size());

    for (std::size_t Index = 0u; Index < Count; ++Index)
    {
        const NotificationEntry& Entry  = PendingEntries[Index];
        const float              Height = QueryBannerHeight(Surface, ToastWidth, Entry);
        const Rectangle2D        Bounds{ LeftX, CursorY, ToastWidth, Height };
        if (ControlAppearance::QueryPointerOver(Bounds, Pointer))
        {
            PausedToken = Entry.Token;
        }
        if (ConstructBanner(Surface, Bounds, Entry, Pointer, true))
        {
            ClosedToken = Entry.Token;
        }
        CursorY += Height + 10.0f;
    }

    if (ClosedToken != 0u)
    {
        (void)Dismiss(ClosedToken);
    }
    Integrate(DeltaSeconds, VisibleCapacity, PausedToken);
}

} // namespace Frontier
