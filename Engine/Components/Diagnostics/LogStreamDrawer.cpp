/*============================================================================================================================================
                                                           LOGSTREAMDRAWER.CPP
============================================================================================================================================*/
// 🧩 Implementation of reusable LogStreamDrawer (channel filter pills & timestamped coloured diagnostic log stream)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Diagnostics/LogStreamDrawer.h"

#include <algorithm>
#include <cstdio>

namespace Frontier
{

const char* LogStreamDrawer::QueryChannelTag(LogChannelCategory Channel) noexcept
{
    switch (Channel)
    {
    case LogChannelCategory::All:       return "ALL";
    case LogChannelCategory::Particles: return "PARTICLES";
    case LogChannelCategory::Forces:    return "FORCES";
    case LogChannelCategory::Wind:      return "WIND";
    case LogChannelCategory::Viewport:  return "VIEW";
    }
    return "LOG";
}

ColorRgba LogStreamDrawer::QueryChannelAccent(LogChannelCategory Channel) noexcept
{
    switch (Channel)
    {
    case LogChannelCategory::Particles: return { 62u,  196u, 109u, 255u };
    case LogChannelCategory::Forces:    return { 86u,  208u, 168u, 255u };
    case LogChannelCategory::Wind:      return { 232u, 194u, 122u, 255u };
    case LogChannelCategory::Viewport:  return { 130u, 180u, 255u, 255u };
    case LogChannelCategory::All:       return { 220u, 220u, 220u, 255u };
    }
    return { 200u, 200u, 200u, 255u };
}

void LogStreamDrawer::Construct(
    PixelSpace&                      Surface,
    Rectangle2D                      Bounds,
    std::span<const LogStreamRecord> Records,
    LogStreamDrawerSession&          Session,
    const ControlPointer&            Pointer,
    float                            Opacity) noexcept
{
    if (Bounds.Width < 180.0f || Bounds.Height < 52.0f || Opacity <= 0.001f)
    {
        return;
    }

    const ColorRgba DrawerBg { 15u,  16u,  18u,  static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba Hairline { 255u, 255u, 255u, static_cast<uint8_t>(20.0f * Opacity) };
    const ColorRgba TextInk  { 218u, 218u, 218u, static_cast<uint8_t>(255.0f * Opacity) };
    const ColorRgba MutedInk { 128u, 128u, 128u, static_cast<uint8_t>(255.0f * Opacity) };

    Surface.FillRectangle(Bounds, DrawerBg, 0.0f);
    Surface.StrokeLine({ Bounds.X + 8.0f, Bounds.Y }, { Bounds.Right() - 8.0f, Bounds.Y }, Hairline, 1.0f);

    // Filter chip header row
    constexpr LogChannelCategory Channels[5] = {
        LogChannelCategory::All,
        LogChannelCategory::Particles,
        LogChannelCategory::Forces,
        LogChannelCategory::Wind,
        LogChannelCategory::Viewport,
    };

    float ChipX = Bounds.X + 14.0f;
    const float ChipY = Bounds.Y + 8.0f;
    for (LogChannelCategory Channel : Channels)
    {
        char LabelBuf[32];
        if (Channel == LogChannelCategory::All)
        {
            std::snprintf(LabelBuf, sizeof(LabelBuf), "All (%zu)", Records.size());
        }
        else
        {
            std::snprintf(LabelBuf, sizeof(LabelBuf), "%s", QueryChannelTag(Channel));
        }

        const float ChipW = Surface.MeasureTextWidth(9.2f, LabelBuf) + 18.0f;
        const Rectangle2D ChipRect { ChipX, ChipY, ChipW, 20.0f };
        const bool Active = (Session.ActiveFilter == Channel);
        const bool Hover  = ControlAppearance::QueryPointerOver(ChipRect, Pointer);

        Surface.FillRectangle(
            ChipRect,
            Active ? ColorRgba { 42u, 44u, 48u, 255u } : (Hover ? ColorRgba { 28u, 30u, 34u, 255u } : ColorRgba { 18u, 19u, 21u, 0u }),
            10.0f);
        if (Active)
        {
            Surface.StrokeRectangle(ChipRect, ColorRgba { 255u, 255u, 255u, 42u }, 10.0f, 0.9f);
        }
        Surface.TextCentered(ChipRect, 9.0f, Active ? TextInk : MutedInk, LabelBuf);
        if (Hover && Pointer.Pressed)
        {
            Session.ActiveFilter = Channel;
            Session.ScrollY      = 0.0f;
        }
        ChipX = ChipRect.Right() + 6.0f;
    }

    // Timestamped diagnostic log list
    const Rectangle2D ListRect { Bounds.X + 10.0f, ChipY + 26.0f, Bounds.Width - 20.0f, Bounds.Height - 38.0f };
    Surface.PushClip(ListRect);
    float RowY = ListRect.Y - Session.ScrollY;
    constexpr float RowH = 20.0f;

    for (const LogStreamRecord& Entry : Records)
    {
        if (Session.ActiveFilter != LogChannelCategory::All && Entry.Channel != Session.ActiveFilter)
        {
            continue;
        }
        if (RowY + RowH >= ListRect.Y && RowY <= ListRect.Bottom())
        {
            Surface.Text({ ListRect.X + 4.0f, RowY + 4.0f }, 9.8f, MutedInk, Entry.TimestampText.c_str());

            const char*     Tag    = QueryChannelTag(Entry.Channel);
            const ColorRgba Accent = QueryChannelAccent(Entry.Channel);
            const float     TagW   = Surface.MeasureTextWidth(8.4f, Tag) + 12.0f;
            const Rectangle2D TagRect { ListRect.X + 64.0f, RowY + 2.0f, TagW, 15.0f };
            Surface.FillRectangle(TagRect, Accent.ScaledAlpha(0.18f), 4.0f);
            Surface.TextCentered(TagRect, 8.2f, Accent, Tag);

            Surface.Text({ TagRect.Right() + 8.0f, RowY + 4.0f }, 10.0f, TextInk, Entry.Message.c_str());
        }
        RowY += RowH;
    }
    Surface.PopClip();
}

} // namespace Frontier
