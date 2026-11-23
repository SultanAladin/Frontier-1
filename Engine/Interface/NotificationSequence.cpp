/*==========================================================================================================================================
                                                        NOTIFICATIONSEQUENCE.CPP
==========================================================================================================================================*/
// 🧩 Retain bounded alert sequences and construct the supplied light/dark semantic cards with shared controls.

#include "NotificationSequence.h"
#include "GlyphSpace.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   NOTIFICATION STORAGE
//------------------------------------------------------------------------------------------------------------------------

namespace
{
std::string ConstrainText(const std::string &Text, size_t Capacity)
{
    if (Text.size() <= Capacity)
        return Text;
    size_t Boundary = Capacity - 3;
    while (Boundary > 0 && (static_cast<unsigned char>(Text[Boundary]) & 0xC0) == 0x80)
        --Boundary;
    return Text.substr(0, Boundary) + "...";
}
} // namespace

uint64_t NotificationSequence::Broadcast(NotificationCategory Category, const std::string &Title,
                                         const std::string &Description, double HoldSeconds, bool PresentationEnabled)
{
    if (Category > NotificationCategory::Error || NextToken == std::numeric_limits<uint64_t>::max())
        return 0;
    NotificationEntry Entry;
    Entry.Token            = NextToken++;
    Entry.Category         = Category;
    Entry.Title            = ConstrainText(Title, 160);
    Entry.Description      = ConstrainText(Description, 2048);
    Entry.RemainingSeconds = std::isfinite(HoldSeconds) ? std::clamp(HoldSeconds, 1.0, 30.0) : 3.5;
    if (History.size() == HistoryCapacity)
        History.pop_front();
    History.push_back(Entry);
    if (PresentationEnabled)
    {
        if (Pending.size() == PendingCapacity)
            Pending.pop_front();
        Pending.push_back(std::move(Entry));
    }
    return History.back().Token;
}

void NotificationSequence::Integrate(double Δτ, size_t VisibleCount, uint64_t PausedToken) noexcept
{
    if (!std::isfinite(Δτ) || Δτ <= 0)
        return;
    // 📝 Hidden queued alerts receive their full hold interval once a visible slot opens.
    const size_t Count = std::min({VisibleCount, VisibleCapacity, Pending.size()});
    for (size_t Index = 0; Index < Count; ++Index)
        if (Pending[Index].Token != PausedToken)
            Pending[Index].RemainingSeconds -= Δτ;
    std::erase_if(Pending, [](const NotificationEntry &Entry) { return Entry.RemainingSeconds <= 0; });
}

bool NotificationSequence::Reclaim(uint64_t Token) noexcept
{
    return std::erase_if(Pending, [Token](const NotificationEntry &Entry) { return Entry.Token == Token; }) != 0;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   CARD TEXT AND CONTOUR
//------------------------------------------------------------------------------------------------------------------------

namespace
{
std::vector<std::string> PartitionText(PixelSpace &Surface, const std::string &Text, float Width, float FontSize,
                                       size_t MaximumLines)
{
    std::vector<std::string> Lines;
    std::string              Line;
    size_t                   Offset = 0;
    while (Offset < Text.size() && Lines.size() < MaximumLines)
    {
        const size_t Begin = Offset++;
        while (Offset < Text.size() && (static_cast<unsigned char>(Text[Offset]) & 0xC0) == 0x80)
            ++Offset;
        const std::string Glyph = Text.substr(Begin, Offset - Begin);
        if (Glyph == "\n" || (!Line.empty() && Surface.MeasureText((Line + Glyph).c_str(), FontSize).X > Width))
        {
            Lines.push_back(Line);
            Line.clear();
            if (Lines.size() == MaximumLines)
            {
                Line = Lines.back();
                while (!Line.empty() && Surface.MeasureText((Line + "...").c_str(), FontSize).X > Width)
                {
                    size_t Boundary = Line.size() - 1;
                    while (Boundary > 0 && (static_cast<unsigned char>(Line[Boundary]) & 0xC0) == 0x80)
                        --Boundary;
                    Line.resize(Boundary);
                }
                Lines.back() = Line + "...";
                return Lines;
            }
        }
        if (Glyph != "\n" && Glyph != "\r")
            Line += Glyph;
    }
    if (!Line.empty())
        Lines.push_back(Line);
    return Lines;
}
} // namespace

float NotificationCard::QueryHeight(PixelSpace &Surface, float Width, const NotificationEntry &Entry)
{
    const auto Lines = PartitionText(Surface, Entry.Description, std::max(20.0f, Width - 86), 14, 4);
    return 68.0f + std::max<size_t>(1, Lines.size()) * 20.0f;
}

bool NotificationCard::Construct(PixelSpace &Surface, const PlaneExtent &Extent, const NotificationEntry &Entry,
                                 const ControlPointer &Pointer, bool DismissEnabled)
{
    const auto     &Palette = ControlKit::Palette();
    const ColorQuad Tones[] = {Palette.TextDim, Palette.Ok, Palette.Warning, Palette.Danger};
    const auto      Tone    = Tones[static_cast<size_t>(Entry.Category)];
    Surface.FillRectangle(Spanning(Extent.MinimumX, Extent.MinimumY + 2, Extent.Width(), Extent.Height()),
                          ColorQuad{0, 0, 0, Palette.LightSurface ? 0.08f : 0.2f}, 14);
    Surface.FillRectangle(Extent, Palette.Panel, 14);
    ControlKit::OutlineRounded(Surface, Extent, Palette.StrokeStrong, 14);
    const auto      Disc = Spanning(Extent.MinimumX + 18, Extent.MinimumY + 17, 24, 24);
    const ColorQuad Ink{1, 1, 1, 1};
    const auto      Icon = Entry.Category == NotificationCategory::Success   ? ControlCentreIconCategory::CircleCheck
                           : Entry.Category == NotificationCategory::Warning ? ControlCentreIconCategory::TriangleAlert
                           : Entry.Category == NotificationCategory::Error   ? ControlCentreIconCategory::CloseCross
                                                                             : ControlCentreIconCategory::CircleInfo;
    const auto      Path = VectorCodec::QueryControlCentreSvgPath(Icon);
    if (Entry.Category == NotificationCategory::Warning)
        GlyphSpace::Fill(Surface, Path, GlyphPlacement{Disc.MinimumX, Disc.MinimumY, 24, 2, Tone});
    else
        Surface.FillRectangle(Disc, Tone, 12);
    // 📝 Reuse the inherited SVG's interior mark; its outline is replaced by the reference's filled contour.
    const auto Interior = Entry.Category == NotificationCategory::Error ? Path : Path.substr(Path.find('M', 1));
    GlyphSpace::Stroke(Surface, Interior, GlyphPlacement{Disc.MinimumX + 3, Disc.MinimumY + 3, 18, 2.4f, Ink});
    const auto Titles = PartitionText(Surface, Entry.Title, std::max(20.0f, Extent.Width() - 100), 16, 1);
    if (!Titles.empty())
        Surface.Text(Extent.MinimumX + 54, Extent.MinimumY + 19, Palette.Text, Titles.front().c_str(), 16);
    const auto Lines = PartitionText(Surface, Entry.Description, std::max(20.0f, Extent.Width() - 86), 14, 4);
    for (size_t Index = 0; Index < Lines.size(); ++Index)
        Surface.Text(Extent.MinimumX + 54, Extent.MinimumY + 46 + Index * 20, Palette.TextDim, Lines[Index].c_str(),
                     14);
    if (!DismissEnabled)
        return false;
    return ControlKit::RoundIconButton(Surface, Extent.MaximumX - 22, Extent.MinimumY + 29,
                                       ControlCentreIconCategory::CloseCross, Pointer)
        .Clicked;
}

} // namespace Frontier
