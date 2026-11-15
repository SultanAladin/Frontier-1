/*==========================================================================================================================================
                                                    MATERIALVIEWPORTHOST.CPP
==========================================================================================================================================*/
// 🧩 Implementation of MaterialViewportHost presenting Direct Studio OpenPBR (No GI) + interactive click-to-impact & playback controls

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/Material/MaterialViewportHost.h"
#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces
{

MaterialViewportHost::~MaterialViewportHost()
{
    if (PreviewTexture.Pixels != nullptr)
    {
        PreviewTexture.DestroyPixels();
    }
}

void MaterialViewportHost::UploadRgbaTexture(const uint8_t* Pixels, uint32_t WidthPx, uint32_t HeightPx) noexcept
{
    if (Pixels == nullptr || WidthPx == 0u || HeightPx == 0u)
    {
        return;
    }
    const bool NeedsAlloc =
        PreviewTexture.Width != static_cast<int>(WidthPx) ||
        PreviewTexture.Height != static_cast<int>(HeightPx) ||
        PreviewTexture.Pixels == nullptr;
    if (NeedsAlloc)
    {
        PreviewTexture.DestroyPixels();
        PreviewTexture.SetStatus(ImTextureStatus_Destroyed);
        PreviewTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(WidthPx), static_cast<int>(HeightPx));
    }
    std::memcpy(PreviewTexture.Pixels, Pixels, static_cast<std::size_t>(WidthPx) * HeightPx * 4u);
    PreviewTexture.SetStatus(ImTextureStatus_OK);
}

void MaterialViewportHost::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    MaterialWorkspaceSession& Session,
    const ControlPointer&     Pointer) noexcept
{
    const auto C = ParticleAppearance::QueryStudioColors();
    Surface.FillRectangle(Bounds, C.ViewportBg, 20.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 20.0f, 1.0f);
    Surface.PushClip(Bounds);

    // Render calibrated Direct Studio OpenPBR preview buffer matching exact viewport aspect ratio (zero stretching!)
    const float SafeAspect = std::max(0.5f, Bounds.Width / std::max(1.0f, Bounds.Height));
    const uint32_t TargetH = std::clamp(static_cast<uint32_t>(Bounds.Height * 0.58f), 260u, 440u);
    const uint32_t TargetW = std::clamp(static_cast<uint32_t>(static_cast<float>(TargetH) * SafeAspect), 260u, 640u);

    if (Session.DirtyPreviewRequested || CachedWidthPx != TargetW || CachedHeightPx != TargetH || CachedPreviewRgba8.empty())
    {
        const auto& Recipe = Session.InspectActiveRecipe();
        const auto RenderOut = PreviewRenderer.RenderPreview(
            Recipe,
            Recipe.Parameters,
            Session.ActiveSpecimen,
            Session.ActiveRenderMode,
            Session.Camera,
            TargetW,
            TargetH);

        CachedPreviewRgba8        = RenderOut.DisplayRgba8;
        CachedWidthPx             = RenderOut.WidthPx;
        CachedHeightPx            = RenderOut.HeightPx;
        Session.LastRenderWidthPx  = RenderOut.WidthPx;
        Session.LastRenderHeightPx = RenderOut.HeightPx;
        Session.LastRenderMillisec = RenderOut.RenderMillisec;
        Session.LastActiveLobeMask = RenderOut.ActiveLobeMask;
        Session.LastRenderUsedGpu  = RenderOut.UsedVulkanCompute;
        Session.DirtyPreviewRequested = false;
    }

    LastImageRect = { Bounds.X + 2.0f, Bounds.Y + 2.0f, Bounds.Width - 4.0f, Bounds.Height - 4.0f };
    if (!CachedPreviewRgba8.empty())
    {
        UploadRgbaTexture(CachedPreviewRgba8.data(), CachedWidthPx, CachedHeightPx);
        Surface.FillTexturedRectangle(LastImageRect, &PreviewTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 18.0f);
    }

    // Floating Top Toolbar: Specimen Switcher (left) + Render Mode Switcher (right)
    const Rectangle2D TopBar { Bounds.X + 14.0f, Bounds.Y + 14.0f, Bounds.Width - 28.0f, 34.0f };
    const std::array<const char*, 4u> SpecimenLabels {{ "Shaderball", "Cloth", "Box", "Plane" }};
    float SX = TopBar.X;
    for (uint32_t I = 0u; I < 4u; ++I)
    {
        const Rectangle2D Btn { SX, TopBar.Y, 74.0f, 28.0f };
        const bool Active  = (static_cast<uint32_t>(Session.ActiveSpecimen) == I);
        const bool Hovered = ControlAppearance::QueryPointerOver(Btn, Pointer);
        Surface.FillRectangle(Btn, Active ? ColorRgba { 232u, 194u, 122u, 42u } : ColorRgba { 16u, 16u, 18u, 218u }, 14.0f);
        Surface.StrokeRectangle(Btn, Active ? C.WarmAccent : C.HairlineStrong, 14.0f, 1.0f);
        Surface.TextCentered(Btn, 9.2f, Active ? C.WarmAccent : (Hovered ? C.TextPrimary : C.TextSoft), SpecimenLabels[I]);
        if (Hovered && Pointer.Pressed)
        {
            Session.ActiveSpecimen        = static_cast<PreviewSpecimenGeometry>(I);
            Session.DirtyPreviewRequested = true;
        }
        SX += 80.0f;
    }

    // Render Mode Switcher (Direct Studio OpenPBR [No GI] | Real-Time GI | Path Tracer)
    const std::array<const char*, 3u> ModeLabels {{ "Direct OpenPBR (No GI)", "Real-Time GI", "Path Tracer" }};
    const std::array<float, 3u>       ModeWidths {{ 148.0f, 88.0f, 84.0f }};
    float MX = TopBar.Right() - (148.0f + 88.0f + 84.0f + 12.0f);
    for (uint32_t I = 0u; I < 3u; ++I)
    {
        const Rectangle2D Btn { MX, TopBar.Y, ModeWidths[I], 28.0f };
        const bool Active  = (static_cast<uint32_t>(Session.ActiveRenderMode) == I);
        const bool Hovered = ControlAppearance::QueryPointerOver(Btn, Pointer);
        Surface.FillRectangle(Btn, Active ? ColorRgba { 52u, 199u, 89u, 38u } : ColorRgba { 16u, 16u, 18u, 218u }, 14.0f);
        Surface.StrokeRectangle(Btn, Active ? C.LiveGreen : C.HairlineStrong, 14.0f, 1.0f);
        Surface.TextCentered(Btn, 8.8f, Active ? C.LiveGreen : (Hovered ? C.TextPrimary : C.TextSoft), ModeLabels[I]);
        if (Hovered && Pointer.Pressed)
        {
            Session.ActiveRenderMode      = static_cast<PreviewRenderMode>(I);
            Session.DirtyPreviewRequested = true;
        }
        MX += ModeWidths[I] + 6.0f;
    }

    // Floating Bottom Playback & Telemetry Bar
    const Rectangle2D BottomBar { Bounds.X + 16.0f, Bounds.Bottom() - 52.0f, Bounds.Width - 32.0f, 38.0f };
    Surface.FillRectangle(BottomBar, ColorRgba { 16u, 16u, 18u, 226u }, 16.0f);
    Surface.StrokeRectangle(BottomBar, C.HairlineStrong, 16.0f, 1.0f);

    const Rectangle2D PlayBtn { BottomBar.X + 8.0f, BottomBar.Y + 6.0f, 58.0f, 26.0f };
    const bool PlayHover = ControlAppearance::QueryPointerOver(PlayBtn, Pointer);
    Surface.FillRectangle(PlayBtn, Session.PlaybackRunning ? C.WarmSoft : C.CardBg, 13.0f);
    Surface.StrokeRectangle(PlayBtn, Session.PlaybackRunning ? C.WarmAccent : C.Hairline, 13.0f, 1.0f);
    Surface.TextCentered(PlayBtn, 9.0f, Session.PlaybackRunning ? C.WarmAccent : C.TextSoft,
        Session.PlaybackRunning ? "PAUSE" : "PLAY");
    if (PlayHover && Pointer.Pressed)
    {
        Session.PlaybackRunning = !Session.PlaybackRunning;
    }

    // Progress scrubber bar
    const Rectangle2D ScrubTrack { PlayBtn.Right() + 14.0f, BottomBar.Center().Y - 3.0f, BottomBar.Width - 275.0f, 6.0f };
    const float Prog = std::clamp(Session.InspectActiveRecipe().Parameters.TransitionProgress, 0.0f, 1.0f);
    Surface.FillRectangle(ScrubTrack, C.SliderTrack, 3.0f);
    Surface.FillRectangle({ ScrubTrack.X, ScrubTrack.Y, ScrubTrack.Width * Prog, ScrubTrack.Height }, C.WarmAccent, 3.0f);
    Surface.FillCircle({ ScrubTrack.X + ScrubTrack.Width * Prog, ScrubTrack.Center().Y }, 5.5f, C.SliderThumb);

    if (ControlAppearance::QueryPointerOver({ ScrubTrack.X, BottomBar.Y, ScrubTrack.Width, BottomBar.Height }, Pointer) && Pointer.Down)
    {
        const float NewProg = std::clamp((Pointer.X - ScrubTrack.X) / std::max(1.0f, ScrubTrack.Width), 0.0f, 1.0f);
        Session.AccessActiveRecipe().Parameters.TransitionProgress   = NewProg;
        Session.AccessActiveRecipe().Parameters.PrimaryDriverOrdinal = static_cast<uint32_t>(MaterialSpace::Dynamic::ParameterDriverMode::ExternalTimeline);
        Session.DirtyPreviewRequested = true;
    }

    char TelBuf[96];
    std::snprintf(TelBuf, sizeof(TelBuf), "%s  |  t=%.2fs  |  %.1fms",
        Session.LastRenderUsedGpu ? "VK GPU" : "CPU REF",
        Session.PlaybackClockSeconds,
        Session.LastRenderMillisec);
    Surface.TextRightAligned({ ScrubTrack.Right() + 10.0f, BottomBar.Y + 12.0f, 178.0f, 14.0f }, 8.8f, C.TextSoft, TelBuf);

    // Interactive click-on-surface impact pulse testing (only when not clicking top/bottom toolbars)
    const Rectangle2D InteractiveCanvas { Bounds.X, TopBar.Bottom() + 6.0f, Bounds.Width, BottomBar.Y - TopBar.Bottom() - 12.0f };
    if (ControlAppearance::QueryPointerOver(InteractiveCanvas, Pointer) && Pointer.Pressed)
    {
        const float NormX = (Pointer.X - LastImageRect.X) / std::max(1.0f, LastImageRect.Width);
        const float NormY = (Pointer.Y - LastImageRect.Y) / std::max(1.0f, LastImageRect.Height);
        const float Asp   = LastImageRect.Width / std::max(1.0f, LastImageRect.Height);
        Mathematics::Axis3D HitDir {};
        if (MaterialPreviewRenderer::QuerySpecimenHitDirection(Session.Camera, NormX, NormY, Asp, HitDir))
        {
            Session.TriggerViewportImpactPulse(HitDir);
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces
