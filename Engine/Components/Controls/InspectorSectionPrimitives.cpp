/*============================================================================================================================================
                                                     INSPECTORSECTIONPRIMITIVES.CPP
============================================================================================================================================*/
// 🧩 Implementation of generic 1:1 HTML-parity Inspector controls, retained RGBA32 preview surfaces, curves, tags & notes

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Controls/InspectorSectionPrimitives.h"

#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                           ON-DEMAND RETAINED PREVIEW SURFACE
//------------------------------------------------------------------------------------------------------------------------

InspectorPreviewSurface::~InspectorPreviewSurface() noexcept
{
    ResidentTexture.DestroyPixels();
    ResidentTexture.SetStatus(ImTextureStatus_Destroyed);
}

void InspectorPreviewSurface::EnsureUpdated(
    uint64_t                                          RevisionSignature,
    uint32_t                                          TargetWidthPx,
    uint32_t                                          TargetHeightPx,
    const std::function<void(std::vector<uint8_t>&)>& RasterCallback)
{
    if (TargetWidthPx == 0u || TargetHeightPx == 0u || !RasterCallback)
    {
        return;
    }

    const bool SizeMismatch = (WidthPx != TargetWidthPx) || (HeightPx != TargetHeightPx) || (ResidentTexture.Pixels == nullptr);
    if (!SizeMismatch && RetainedSignature == RevisionSignature)
    {
        return;
    }

    StagingPixels.resize(static_cast<std::size_t>(TargetWidthPx) * TargetHeightPx * 4u);
    RasterCallback(StagingPixels);

    if (SizeMismatch)
    {
        ResidentTexture.DestroyPixels();
        ResidentTexture.SetStatus(ImTextureStatus_Destroyed);
        ResidentTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(TargetWidthPx), static_cast<int>(TargetHeightPx));
        WidthPx  = TargetWidthPx;
        HeightPx = TargetHeightPx;
    }

    if (ResidentTexture.Pixels != nullptr)
    {
        std::memcpy(ResidentTexture.Pixels, StagingPixels.data(), StagingPixels.size());
        if (!SizeMismatch && ResidentTexture.Status == ImTextureStatus_OK)
        {
            ResidentTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(TargetWidthPx), static_cast<unsigned short>(TargetHeightPx) };
            ResidentTexture.SetStatus(ImTextureStatus_WantUpdates);
        }
    }
    RetainedSignature = RevisionSignature;
    ++UploadCounter;
}

void InspectorPreviewSurface::Present(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    float       CornerRadius,
    ColorRgba   Tint) noexcept
{
    if (!QueryResident())
    {
        Surface.FillRectangle(Bounds, InspectorMetrics::ChamberDark, CornerRadius);
        return;
    }
    Surface.FillTexturedRectangle(Bounds, &ResidentTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, CornerRadius, Tint);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  INSPECTOR PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

namespace InspectorPrimitives
{

uint64_t MixRevisionU64(uint64_t Seed, uint64_t Bits) noexcept
{
    Seed ^= Bits + 0x9E3779B97F4A7C15ull + (Seed << 6u) + (Seed >> 2u);
    return Seed;
}

uint64_t MixRevisionFloat(uint64_t Seed, float Reading) noexcept
{
    const int64_t Quantized = static_cast<int64_t>(std::llround(static_cast<double>(Reading) * 1000.0));
    return MixRevisionU64(Seed, static_cast<uint64_t>(Quantized));
}

ColorRgba EncodeLinearRgbToSrgb8(const Mathematics::LinearRGB& Linear) noexcept
{
    auto Encode = [](float Channel) noexcept -> uint8_t
    {
        const float Clamped = std::clamp(Channel, 0.0f, 1.0f);
        const float Srgb    = (Clamped <= 0.0031308f) ? (Clamped * 12.92f) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
    };
    return ColorRgba{ Encode(Linear.R), Encode(Linear.G), Encode(Linear.B), 255u };
}

void ConstructSwitchSymbol(
    PixelSpace&           Surface,
    InspectorSwitchSymbol Symbol,
    Point2D               Center,
    float                 Diameter,
    ColorRgba             Ink,
    ColorRgba             WellFill) noexcept
{
    const float R = Diameter * 0.5f;
    switch (Symbol)
    {
        case InspectorSwitchSymbol::Eye:
        {
            const std::array<Point2D, 7u> Upper = {{
                { Center.X - R * 0.85f, Center.Y },
                { Center.X - R * 0.45f, Center.Y - R * 0.42f },
                { Center.X,             Center.Y - R * 0.54f },
                { Center.X + R * 0.45f, Center.Y - R * 0.42f },
                { Center.X + R * 0.85f, Center.Y },
                { Center.X + R * 0.45f, Center.Y + R * 0.42f },
                { Center.X - R * 0.45f, Center.Y + R * 0.42f },
            }};
            Surface.StrokePolyline(Upper, Ink, 1.45f, true);
            Surface.FillCircle(Center, R * 0.28f, Ink);
            break;
        }
        case InspectorSwitchSymbol::Sun:
            Surface.FillCircle(Center, R * 0.34f, Ink);
            for (int32_t Ray = 0; Ray < 8; ++Ray)
            {
                const float Angle = static_cast<float>(Ray) * 0.78539816f;
                Surface.StrokeLine(
                    { Center.X + std::cos(Angle) * R * 0.54f, Center.Y + std::sin(Angle) * R * 0.54f },
                    { Center.X + std::cos(Angle) * R * 0.84f, Center.Y + std::sin(Angle) * R * 0.84f },
                    Ink, 1.45f);
            }
            break;
        case InspectorSwitchSymbol::Clock:
            Surface.StrokeCircle(Center, R * 0.72f, Ink, 1.5f);
            Surface.StrokeLine(Center, { Center.X, Center.Y - R * 0.44f }, Ink, 1.5f);
            Surface.StrokeLine(Center, { Center.X + R * 0.36f, Center.Y + R * 0.14f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Cycle:
            Surface.StrokeCircle(Center, R * 0.66f, Ink, 1.5f);
            Surface.FillRectangle({ Center.X - R * 0.12f, Center.Y - R * 0.82f, R * 0.28f, R * 0.36f }, WellFill);
            Surface.StrokeLine({ Center.X + R * 0.14f, Center.Y - R * 0.68f }, { Center.X + R * 0.46f, Center.Y - R * 0.52f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X + R * 0.14f, Center.Y - R * 0.68f }, { Center.X + R * 0.34f, Center.Y - R * 0.36f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Compass:
            Surface.StrokeCircle(Center, R * 0.74f, Ink, 1.45f);
            {
                const std::array<Point2D, 4u> Needle = {{
                    { Center.X + R * 0.36f, Center.Y - R * 0.36f },
                    { Center.X + R * 0.12f, Center.Y + R * 0.12f },
                    { Center.X - R * 0.36f, Center.Y + R * 0.36f },
                    { Center.X - R * 0.12f, Center.Y - R * 0.12f },
                }};
                Surface.FillPolygon(Needle, Ink);
            }
            break;
        case InspectorSwitchSymbol::Orbit:
            Surface.StrokeCircle(Center, R * 0.68f, Ink, 1.45f);
            Surface.FillCircle({ Center.X + R * 0.48f, Center.Y - R * 0.48f }, R * 0.22f, Ink);
            Surface.FillCircle(Center, R * 0.24f, Ink);
            break;
        case InspectorSwitchSymbol::LimbLine:
            Surface.StrokeLine({ Center.X - R * 0.80f, Center.Y + R * 0.26f }, { Center.X + R * 0.80f, Center.Y + R * 0.26f }, Ink, 1.5f);
            Surface.FillCircle({ Center.X, Center.Y + R * 0.16f }, R * 0.34f, Ink);
            Surface.FillRectangle({ Center.X - R * 0.50f, Center.Y + R * 0.32f, R * 1.0f, R * 0.45f }, WellFill);
            for (int32_t Ray = -2; Ray <= 2; ++Ray)
            {
                const float A = -1.5707963f + static_cast<float>(Ray) * 0.48f;
                Surface.StrokeLine(
                    { Center.X + std::cos(A) * R * 0.48f, Center.Y + R * 0.16f + std::sin(A) * R * 0.48f },
                    { Center.X + std::cos(A) * R * 0.74f, Center.Y + R * 0.16f + std::sin(A) * R * 0.74f },
                    Ink, 1.35f);
            }
            break;
        case InspectorSwitchSymbol::Cloud:
            Surface.FillCircle({ Center.X - R * 0.28f, Center.Y + R * 0.14f }, R * 0.32f, Ink);
            Surface.FillCircle({ Center.X + R * 0.26f, Center.Y + R * 0.14f }, R * 0.28f, Ink);
            Surface.FillCircle({ Center.X - R * 0.02f, Center.Y - R * 0.12f }, R * 0.38f, Ink);
            Surface.FillRectangle({ Center.X - R * 0.34f, Center.Y + R * 0.08f, R * 0.68f, R * 0.32f }, Ink, R * 0.12f);
            break;
        case InspectorSwitchSymbol::Stars:
        case InspectorSwitchSymbol::Twinkle:
        {
            const std::array<Point2D, 8u> Spark = {{
                { Center.X,             Center.Y - R * 0.76f },
                { Center.X + R * 0.20f, Center.Y - R * 0.20f },
                { Center.X + R * 0.76f, Center.Y },
                { Center.X + R * 0.20f, Center.Y + R * 0.20f },
                { Center.X,             Center.Y + R * 0.76f },
                { Center.X - R * 0.20f, Center.Y + R * 0.20f },
                { Center.X - R * 0.76f, Center.Y },
                { Center.X - R * 0.20f, Center.Y - R * 0.20f },
            }};
            Surface.FillPolygon(Spark, Ink);
            if (Symbol == InspectorSwitchSymbol::Stars)
            {
                Surface.FillCircle({ Center.X + R * 0.56f, Center.Y - R * 0.54f }, R * 0.14f, Ink);
            }
            break;
        }
        case InspectorSwitchSymbol::Galaxy:
            Surface.StrokeCircle(Center, R * 0.68f, Ink, 1.35f);
            Surface.StrokeCircle(Center, R * 0.36f, Ink, 1.35f);
            Surface.FillCircle(Center, R * 0.16f, Ink);
            break;
        case InspectorSwitchSymbol::Zodiac:
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y + R * 0.38f }, { Center.X + R * 0.72f, Center.Y - R * 0.38f }, Ink, 1.6f);
            Surface.FillCircle(Center, R * 0.26f, Ink);
            break;
        case InspectorSwitchSymbol::Wind:
            Surface.StrokeLine({ Center.X - R * 0.74f, Center.Y - R * 0.26f }, { Center.X + R * 0.44f, Center.Y - R * 0.26f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.54f, Center.Y + R * 0.04f }, { Center.X + R * 0.74f, Center.Y + R * 0.04f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.74f, Center.Y + R * 0.34f }, { Center.X + R * 0.24f, Center.Y + R * 0.34f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::Fog:
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y - R * 0.28f }, { Center.X + R * 0.72f, Center.Y - R * 0.28f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.56f, Center.Y + R * 0.04f }, { Center.X + R * 0.56f, Center.Y + R * 0.04f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.72f, Center.Y + R * 0.36f }, { Center.X + R * 0.72f, Center.Y + R * 0.36f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::LightBulb:
            Surface.StrokeCircle({ Center.X, Center.Y - R * 0.14f }, R * 0.46f, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.24f, Center.Y + R * 0.48f }, { Center.X + R * 0.24f, Center.Y + R * 0.48f }, Ink, 1.5f);
            Surface.StrokeLine({ Center.X - R * 0.18f, Center.Y + R * 0.68f }, { Center.X + R * 0.18f, Center.Y + R * 0.68f }, Ink, 1.5f);
            break;
        case InspectorSwitchSymbol::CameraLens:
            Surface.StrokeRectangle({ Center.X - R * 0.72f, Center.Y - R * 0.46f, R * 1.44f, R * 0.96f }, Ink, 3.0f, 1.45f);
            Surface.StrokeCircle({ Center.X, Center.Y + R * 0.02f }, R * 0.28f, Ink, 1.45f);
            break;
        case InspectorSwitchSymbol::Cube:
            Surface.StrokeRectangle({ Center.X - R * 0.54f, Center.Y - R * 0.54f, R * 1.08f, R * 1.08f }, Ink, 2.0f, 1.45f);
            Surface.StrokeLine({ Center.X - R * 0.54f, Center.Y - R * 0.14f }, { Center.X + R * 0.54f, Center.Y - R * 0.14f }, Ink, 1.2f);
            break;
        case InspectorSwitchSymbol::Shadow:
            Surface.FillCircle({ Center.X - R * 0.16f, Center.Y - R * 0.16f }, R * 0.42f, Ink);
            Surface.StrokeCircle({ Center.X + R * 0.22f, Center.Y + R * 0.22f }, R * 0.42f, Ink, 1.35f);
            break;
        case InspectorSwitchSymbol::Physics:
            Surface.StrokeCircle(Center, R * 0.66f, Ink, 1.45f);
            Surface.FillCircle({ Center.X + R * 0.24f, Center.Y - R * 0.18f }, R * 0.22f, Ink);
            break;
        case InspectorSwitchSymbol::PostFx:
            Surface.StrokeCircle(Center, R * 0.72f, Ink, 1.45f);
            Surface.StrokeCircle(Center, R * 0.34f, Ink, 1.45f);
            break;
    }
}

float ConstructInspectorHeader(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Breadcrumb,
    const char* Title,
    const char* Subtitle) noexcept
{
    float CursorY = TopY;
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, CursorY }, InnerWidth, 10.0f, InspectorMetrics::DimInk, Breadcrumb);
    CursorY += 32.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Light, { LeftX, CursorY }, InnerWidth, 26.0f, WorkspaceSurfaceColors::PrimaryInk, Title);
    CursorY += 34.0f;

    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX, CursorY }, InnerWidth, 11.0f, InspectorMetrics::MutedInk, Subtitle);
    CursorY += 24.0f;
    return CursorY;
}

float ConstructPageStrip(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> PageLabels,
    uint32_t&                    InOutActivePage,
    const PaneInputReading&      Input,
    bool&                        OutPageSwitched) noexcept
{
    OutPageSwitched = false;
    const uint32_t PageCount = static_cast<uint32_t>(PageLabels.size());
    if (PageCount == 0u)
    {
        return TopY;
    }
    InOutActivePage = std::min(InOutActivePage, PageCount - 1u);

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   StripH = InspectorMetrics::PageStripHeight; // 34px
    const float   StepW  = 26.0f;
    const float   StepH  = 28.0f;
    const float   StepY  = TopY + (StripH - StepH) * 0.5f;

    // 1. Left "<" circular step button (26x28)
    const Rectangle2D PrevBtn { LeftX, StepY, StepW, StepH };
    const bool        PrevDisabled = (InOutActivePage == 0u);
    const bool        PrevHov      = !PrevDisabled && PrevBtn.Contains(Cursor);
    Surface.FillRectangle(
        PrevBtn,
        PrevHov ? ColorRgba{ 41u, 41u, 45u, 255u } : InspectorMetrics::PageStepFill.ScaledAlpha(PrevDisabled ? 0.45f : 1.0f),
        StepW * 0.5f);
    Surface.StrokeRectangle(PrevBtn, ColorRgba{ 255u, 255u, 255u, 18u }, StepW * 0.5f, 1.0f);
    {
        const ColorRgba ChevInk = PrevDisabled ? ColorRgba{ 95u, 98u, 104u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u };
        const Point2D   C       = PrevBtn.Center();
        const std::array<Point2D, 3u> Chev = {{
            { C.X + 2.2f, C.Y - 4.0f },
            { C.X - 2.0f, C.Y },
            { C.X + 2.2f, C.Y + 4.0f },
        }};
        Surface.StrokePolyline(Chev, ChevInk, 1.45f, false);
    }
    if (PrevHov && Input.PrimaryPointer.Pressed)
    {
        --InOutActivePage;
        OutPageSwitched = true;
    }

    // 2. Right counter ("1 / N") & ">" step button
    char CounterBuf[24]{};
    std::snprintf(CounterBuf, sizeof(CounterBuf), "%u / %u", InOutActivePage + 1u, PageCount);
    const float CounterW  = std::max(28.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, CounterBuf));
    const float RightEdge = LeftX + InnerWidth;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RightEdge - CounterW, TopY + 10.5f }, 10.5f, ColorRgba{ 138u, 142u, 149u, 255u }, CounterBuf);

    const Rectangle2D NextBtn { RightEdge - CounterW - 6.0f - StepW, StepY, StepW, StepH };
    const bool        NextDisabled = (InOutActivePage + 1u >= PageCount);
    const bool        NextHov      = !NextDisabled && NextBtn.Contains(Cursor);
    Surface.FillRectangle(
        NextBtn,
        NextHov ? ColorRgba{ 41u, 41u, 45u, 255u } : InspectorMetrics::PageStepFill.ScaledAlpha(NextDisabled ? 0.45f : 1.0f),
        StepW * 0.5f);
    Surface.StrokeRectangle(NextBtn, ColorRgba{ 255u, 255u, 255u, 18u }, StepW * 0.5f, 1.0f);
    {
        const ColorRgba ChevInk = NextDisabled ? ColorRgba{ 95u, 98u, 104u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u };
        const Point2D   C       = NextBtn.Center();
        const std::array<Point2D, 3u> Chev = {{
            { C.X - 2.2f, C.Y - 4.0f },
            { C.X + 2.0f, C.Y },
            { C.X - 2.2f, C.Y + 4.0f },
        }};
        Surface.StrokePolyline(Chev, ChevInk, 1.45f, false);
    }
    if (NextHov && Input.PrimaryPointer.Pressed)
    {
        ++InOutActivePage;
        OutPageSwitched = true;
    }

    // 3. Center capsule rail (#141416, height 34px, radius 17px, padding 3px, inset 1px rgba(255,255,255,0.07))
    const float       RailX = PrevBtn.Right() + 6.0f;
    const float       RailW = std::max(56.0f, NextBtn.X - 6.0f - RailX);
    const Rectangle2D RailRect { RailX, TopY, RailW, StripH };
    Surface.FillRectangle(RailRect, InspectorMetrics::PageRailFill, StripH * 0.5f);
    Surface.StrokeRectangle(RailRect, ColorRgba{ 255u, 255u, 255u, 18u }, StripH * 0.5f, 1.0f);

    const Rectangle2D RailInner { RailRect.X + 3.0f, RailRect.Y + 3.0f, std::max(24.0f, RailRect.Width - 6.0f), StripH - 6.0f };
    Surface.PushClip(RailInner);

    // Keep active tab anchored cleanly while showing neighbor tabs
    const uint32_t StartIdx = (InOutActivePage > 0u && InOutActivePage + 1u == PageCount) ? (InOutActivePage - 1u) : InOutActivePage;
    float TabX = RailInner.X;
    for (uint32_t Idx = StartIdx; Idx < PageCount && TabX < RailInner.Right(); ++Idx)
    {
        const bool  Open   = (Idx == InOutActivePage);
        const float LabelW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, PageLabels[Idx]);
        const float TabW   = std::min(128.0f, 4.0f + 18.0f + 6.0f + LabelW + 11.0f);
        const Rectangle2D TabRect { TabX, RailInner.Y, TabW, RailInner.Height };
        const bool Hovered = RailInner.Contains(Cursor) && TabRect.Contains(Cursor);

        if (Open)
        {
            Surface.FillRectangle(TabRect, InspectorMetrics::PageTabOpenFill, TabRect.Height * 0.5f);
            Surface.StrokeRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 20u }, TabRect.Height * 0.5f, 1.0f);
        }
        else if (Hovered)
        {
            Surface.FillRectangle(TabRect, ColorRgba{ 255u, 255u, 255u, 12u }, TabRect.Height * 0.5f);
        }

        // 18x18 circular number indicator
        const Point2D CircleCenter { TabRect.X + 13.0f, TabRect.Center().Y };
        Surface.FillCircle(CircleCenter, 9.0f, Open ? InspectorMetrics::PageNumOpenFill : InspectorMetrics::PageNumClosedFill);
        char NumBuf[16]{};
        std::snprintf(NumBuf, sizeof(NumBuf), "%u", Idx + 1u);
        const float NumW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 9.5f, NumBuf);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::SemiBold,
            { CircleCenter.X - NumW * 0.5f, CircleCenter.Y - 5.2f },
            9.5f,
            Open ? ColorRgba{ 20u, 20u, 22u, 255u } : ColorRgba{ 201u, 204u, 209u, 255u },
            NumBuf);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { TabRect.X + 28.0f, TabRect.Y + 7.5f },
            std::max(10.0f, TabRect.Width - 36.0f),
            11.0f,
            Open ? ColorRgba{ 244u, 244u, 245u, 255u } : ColorRgba{ 154u, 158u, 165u, 255u },
            PageLabels[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed && !Open)
        {
            InOutActivePage = Idx;
            OutPageSwitched = true;
        }
        TabX += TabW + 2.0f;
    }

    // Soft right-edge fade mask when additional tabs overflow the capsule rail
    if (TabX > RailInner.Right())
    {
        for (int32_t Band = 0; Band < 6; ++Band)
        {
            const float BandW = 4.0f;
            const float BandX = RailInner.Right() - static_cast<float>(6 - Band) * BandW;
            const uint8_t A   = static_cast<uint8_t>(25u + Band * 36u);
            Surface.FillRectangle({ BandX, RailInner.Y, BandW, RailInner.Height }, InspectorMetrics::PageRailFill.WithAlpha(A));
        }
    }

    Surface.PopClip();
    return TopY + StripH + 14.0f;
}

void ConstructSectionSurface(
    PixelSpace& Surface,
    Rectangle2D Bounds,
    const char* Title,
    const char* RightText,
    ColorRgba   RightInk) noexcept
{
    Surface.FillRectangle(Bounds, InspectorMetrics::SectionFill, InspectorMetrics::SectionRadius);
    Surface.StrokeRectangle(Bounds, InspectorMetrics::SectionBorder, InspectorMetrics::SectionRadius, 1.0f);

    if (Title != nullptr && Title[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Light,
            { Bounds.X + InspectorMetrics::SectionPadX, Bounds.Y + InspectorMetrics::SectionPadTop },
            12.0f,
            InspectorMetrics::HeadingInk,
            Title);
    }

    if (RightText != nullptr && RightText[0] != '\0')
    {
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, RightText);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { Bounds.Right() - InspectorMetrics::SectionPadX - RW, Bounds.Y + InspectorMetrics::SectionPadTop + 0.5f },
            11.0f,
            RightInk,
            RightText);
    }
}

float ConstructMetricReadout(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    const char* NumberText,
    const char* UnitText,
    const char* CaptionText,
    float       NumberSize) noexcept
{
    // Split integer + fractional part so decimals render softly muted (1:1 HTML parity: "18" bright + ".3" muted)
    const std::string_view FullNum(NumberText != nullptr ? NumberText : "");
    const std::size_t      DotPos = FullNum.find('.');
    float                  NumAdvance = 0.0f;
    if (DotPos != std::string_view::npos)
    {
        const std::string IntPart(FullNum.substr(0u, DotPos));
        const std::string FracPart(FullNum.substr(DotPos));
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX, TopY }, NumberSize, ColorRgba{ 233u, 233u, 233u, 255u }, IntPart);
        const float IntW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, IntPart);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX + IntW, TopY }, NumberSize, ColorRgba{ 138u, 138u, 142u, 255u }, FracPart);
        NumAdvance = IntW + WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, FracPart);
    }
    else
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { LeftX, TopY }, NumberSize, ColorRgba{ 233u, 233u, 233u, 255u }, FullNum);
        NumAdvance = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, NumberSize, FullNum);
    }

    if (UnitText != nullptr && UnitText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Regular,
            { LeftX + NumAdvance + 7.0f, TopY + NumberSize * 0.42f },
            14.5f,
            ColorRgba{ 153u, 153u, 153u, 255u },
            UnitText);
    }

    float NextY = TopY + NumberSize * 1.18f + 4.0f;
    if (CaptionText != nullptr && CaptionText[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { LeftX, NextY }, 11.0f, InspectorMetrics::MutedInk, CaptionText);
        NextY += 18.0f;
    }
    return NextY;
}

float ConstructKpiMetricGrid(
    PixelSpace&                             Surface,
    float                                   LeftX,
    float                                   TopY,
    float                                   InnerWidth,
    std::span<const InspectorKpiMetricSpec> Metrics) noexcept
{
    if (Metrics.empty())
    {
        return TopY;
    }
    const float    Gap  = 10.0f;
    const float    BoxW = std::floor((InnerWidth - Gap) * 0.5f);
    const float    BoxH = 74.0f;
    const uint32_t Rows = (static_cast<uint32_t>(Metrics.size()) + 1u) / 2u;

    for (std::size_t Idx = 0u; Idx < Metrics.size(); ++Idx)
    {
        const uint32_t Col = static_cast<uint32_t>(Idx) % 2u;
        const uint32_t Row = static_cast<uint32_t>(Idx) / 2u;
        const Rectangle2D Box {
            LeftX + static_cast<float>(Col) * (BoxW + Gap),
            TopY + static_cast<float>(Row) * (BoxH + Gap),
            BoxW,
            BoxH,
        };
        Surface.FillRectangle(Box, InspectorMetrics::SectionFill, 16.0f);
        Surface.StrokeRectangle(Box, InspectorMetrics::SectionBorder, 16.0f, 1.0f);

        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Light, { Box.X + 16.0f, Box.Y + 14.0f }, 20.0f, ColorRgba{ 234u, 234u, 234u, 255u }, Metrics[Idx].ReadingText);
        if (Metrics[Idx].UnitText != nullptr && Metrics[Idx].UnitText[0] != '\0')
        {
            const float VW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Light, 20.0f, Metrics[Idx].ReadingText);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Box.X + 21.0f + VW, Box.Y + 21.0f }, 10.5f, InspectorMetrics::MutedInk, Metrics[Idx].UnitText);
        }
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Box.X + 16.0f, Box.Bottom() - 21.0f }, 9.0f, InspectorMetrics::DimInk, Metrics[Idx].CaptionText);
    }

    return TopY + static_cast<float>(Rows) * BoxH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + InspectorMetrics::SectionGap;
}

float ConstructQuickSwitchSection(
    PixelSpace&                               Surface,
    float                                     LeftX,
    float                                     TopY,
    float                                     InnerWidth,
    std::span<const InspectorQuickSwitchSpec> Switches,
    const PaneInputReading&                   Input,
    int32_t&                                  OutClickedIndex) noexcept
{
    OutClickedIndex = -1;
    if (Switches.empty())
    {
        return TopY;
    }

    uint32_t ActiveCount = 0u;
    uint32_t LiveCount   = 0u;
    for (const auto& Sw : Switches)
    {
        if (!Sw.Disabled)
        {
            ++LiveCount;
            if (Sw.Active) ++ActiveCount;
        }
    }

    const uint32_t Rows   = (static_cast<uint32_t>(Switches.size()) + 1u) / 2u;
    const float    PadX   = InspectorMetrics::SectionPadX;
    const float    PadTop = InspectorMetrics::SectionPadTop;
    const float    PadBot = InspectorMetrics::SectionPadBottom;
    const float    Gap    = 10.0f;
    const float    TileH  = InspectorMetrics::QuickTileHeight;
    const float    BoxH   = PadTop + 32.0f + static_cast<float>(Rows) * TileH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + PadBot;

    const Rectangle2D Bounds { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Bounds, "Quick controls");

    // Green active count ("2 / 3 on")
    char ActiveBuf[16]{};
    char TotalBuf[24]{};
    std::snprintf(ActiveBuf, sizeof(ActiveBuf), "%u", ActiveCount);
    std::snprintf(TotalBuf,  sizeof(TotalBuf),  " / %u on", LiveCount);
    const float AW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, ActiveBuf);
    const float TW     = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, TotalBuf);
    const float RightX = Bounds.Right() - PadX - (AW + TW);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { RightX, Bounds.Y + PadTop + 0.5f }, 11.0f,
        ActiveCount > 0u ? InspectorMetrics::QuickActiveGreen : InspectorMetrics::MutedInk, ActiveBuf);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { RightX + AW, Bounds.Y + PadTop + 0.5f }, 11.0f, InspectorMetrics::MutedInk, TotalBuf);

    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const float   TileW = std::floor((InnerWidth - PadX * 2.0f - Gap) * 0.5f);
    const float   GridY = Bounds.Y + PadTop + 32.0f;

    for (std::size_t Idx = 0u; Idx < Switches.size(); ++Idx)
    {
        const uint32_t    Col = static_cast<uint32_t>(Idx) % 2u;
        const uint32_t    Row = static_cast<uint32_t>(Idx) / 2u;
        const Rectangle2D Tile {
            LeftX + PadX + static_cast<float>(Col) * (TileW + Gap),
            GridY + static_cast<float>(Row) * (TileH + Gap),
            TileW,
            TileH,
        };
        const bool Hovered = !Switches[Idx].Disabled && Tile.Contains(Cursor);

        // Exact HTML sheen tile (radius 17px, border rgba(255,255,255,0.07))
        const ColorRgba TileBg = Hovered ? ColorRgba{ 46u, 46u, 49u, 255u } : ColorRgba{ 38u, 38u, 40u, 255u };
        Surface.FillRectangle(Tile, TileBg, InspectorMetrics::QuickTileRadius);
        Surface.StrokeRectangle(
            Tile,
            Hovered ? ColorRgba{ 255u, 255u, 255u, 32u } : ColorRgba{ 255u, 255u, 255u, 18u },
            InspectorMetrics::QuickTileRadius,
            1.0f);

        // Centered 36x36 rounded icon well
        const Rectangle2D Well { Tile.Center().X - 18.0f, Tile.Y + 18.0f, 36.0f, 36.0f };
        const ColorRgba   WellFill = Switches[Idx].Active ? InspectorMetrics::QuickWellActiveFill : InspectorMetrics::QuickWellIdleFill;
        const ColorRgba   SymInk   = Switches[Idx].Active ? InspectorMetrics::QuickActiveGreen : ColorRgba{ 152u, 152u, 156u, 255u };
        Surface.FillRectangle(Well, WellFill, 11.0f);
        ConstructSwitchSymbol(Surface, Switches[Idx].Symbol, Well.Center(), 18.0f, SymInk, WellFill);

        // Centered caption below icon well
        const float MaxW = Tile.Width - 12.0f;
        const float LW   = std::min(MaxW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.5f, Switches[Idx].Label));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { Tile.Center().X - LW * 0.5f, Tile.Bottom() - 24.0f },
            MaxW,
            10.5f,
            Switches[Idx].Active ? ColorRgba{ 230u, 230u, 230u, 255u } : ColorRgba{ 165u, 165u, 165u, 255u },
            Switches[Idx].Label);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutClickedIndex = static_cast<int32_t>(Idx);
        }
    }

    return Bounds.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructScalarFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlId,
    const char*             Label,
    const char*             UnitText,
    const char*             FormatSpec,
    float                   Minimum,
    float                   Maximum,
    float&                  InOutReading,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    const float Opacity = Disabled ? 0.42f : 1.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY }, 11.0f, InspectorMetrics::MutedInk.ScaledAlpha(Opacity), Label);

    char NumBuf[32]{};
    std::snprintf(NumBuf, sizeof(NumBuf), FormatSpec, InOutReading);

    const float RowY = TopY + 18.0f;
    const bool  UnitPresent = (UnitText != nullptr && UnitText[0] != '\0');
    ScalarSlider::ConstructReadoutCapsule(
        Surface,
        LeftX,
        RowY,
        NumBuf,
        UnitPresent ? UnitText : "",
        Opacity,
        InspectorMetrics::SplitWidth,
        34.0f);

    const float       Gap = 10.0f;
    const Rectangle2D TrackRect {
        LeftX + InspectorMetrics::SplitWidth + Gap,
        RowY + 4.0f,
        std::max(44.0f, InnerWidth - InspectorMetrics::SplitWidth - Gap),
        20.0f,
    };

    const bool Dragging = !Disabled && (InOutActiveDragId == ControlId);
    float      Updated  = InOutReading;
    const ControlPointer Ptr = Disabled ? ControlPointer{} : Input.PrimaryPointer;
    const ControlContact Contact = ScalarSlider::Construct(
        Surface, TrackRect, Minimum, Maximum, InOutReading, Dragging, Ptr, Updated, Opacity, 20.0f, 18.0f);

    if (!Disabled)
    {
        if (Contact.Pressed)
        {
            InOutActiveDragId = ControlId;
        }
        if (Dragging && !Input.PrimaryPointer.Down)
        {
            InOutActiveDragId = 0u;
        }
        if ((Contact.Dragging || Contact.Pressed) && std::fabs(Updated - InOutReading) > 1.0e-5f)
        {
            InOutReading = Updated;
            OutChanged   = true;
        }
    }

    return RowY + InspectorMetrics::SplitHeight + 14.0f;
}

float ConstructToggleFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    bool&                   InOutEnabled,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    const float Opacity = Disabled ? 0.42f : 1.0f;
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 5.0f }, 11.0f, ColorRgba{ 170u, 170u, 170u, 255u }.ScaledAlpha(Opacity), Label);

    const Rectangle2D Track {
        LeftX + InnerWidth - InspectorMetrics::ToggleWidth,
        TopY,
        InspectorMetrics::ToggleWidth,
        InspectorMetrics::ToggleHeight,
    };
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Hovered = !Disabled && Track.Contains(Cursor);

    Surface.FillRectangle(
        Track,
        (InOutEnabled ? InspectorMetrics::ToggleOnFill : InspectorMetrics::ToggleOffFill).ScaledAlpha(Opacity),
        InspectorMetrics::ToggleHeight * 0.5f);

    const float KnobX = InOutEnabled ? (Track.Right() - 12.0f) : (Track.X + 12.0f);
    Surface.FillCircle({ KnobX, Track.Center().Y }, 10.0f, ColorRgba{ 238u, 238u, 238u, 255u }.ScaledAlpha(Opacity));

    if (Hovered && Input.PrimaryPointer.Pressed)
    {
        InOutEnabled = !InOutEnabled;
        OutChanged   = true;
    }

    return TopY + InspectorMetrics::ToggleHeight + 16.0f;
}

float ConstructColourFieldRow(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    uint32_t                ControlOriginId,
    const char*             Label,
    Mathematics::LinearRGB& InOutColour,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutChanged,
    bool                    Disabled) noexcept
{
    const float     Opacity = Disabled ? 0.42f : 1.0f;
    const ColorRgba Swatch  = EncodeLinearRgbToSrgb8(InOutColour);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { LeftX, TopY + 3.0f }, 11.0f, InspectorMetrics::MutedInk.ScaledAlpha(Opacity), Label);

    char HexBuf[16]{};
    std::snprintf(HexBuf, sizeof(HexBuf), "#%02x%02x%02x", Swatch.Red, Swatch.Green, Swatch.Blue);
    const float HexW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, HexBuf);
    const Rectangle2D SwatchRect { LeftX + InnerWidth - 34.0f, TopY, 34.0f, 20.0f };
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { SwatchRect.X - HexW - 8.0f, TopY + 4.0f }, 10.0f, InspectorMetrics::DimInk.ScaledAlpha(Opacity), HexBuf);
    Surface.FillRectangle(SwatchRect, Swatch.ScaledAlpha(Opacity), 6.0f);
    Surface.StrokeRectangle(SwatchRect, ColorRgba{ 255u, 255u, 255u, 36u }, 6.0f, 1.0f);

    // Compact RGB micro-sliders directly beneath swatch header
    const float BarY   = TopY + 26.0f;
    const float BarGap = 6.0f;
    const float BarW   = std::floor((InnerWidth - BarGap * 2.0f) / 3.0f);
    const std::array<float*, 3u>     Channels = {{ &InOutColour.R, &InOutColour.G, &InOutColour.B }};
    const std::array<ColorRgba, 3u>  Tints    = {{
        ColorRgba{ 198u, 92u,  92u,  255u },
        ColorRgba{ 96u,  188u, 114u, 255u },
        ColorRgba{ 98u,  148u, 218u, 255u },
    }};
    const Point2D Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    for (uint32_t Ch = 0u; Ch < 3u; ++Ch)
    {
        const Rectangle2D Bar { LeftX + static_cast<float>(Ch) * (BarW + BarGap), BarY, BarW, 10.0f };
        const uint32_t    Cid = ControlOriginId + Ch;
        Surface.FillRectangle({ Bar.X, Bar.Y + 3.0f, Bar.Width, 4.0f }, ColorRgba{ 46u, 46u, 50u, 255u }.ScaledAlpha(Opacity), 2.0f);
        const float Norm = std::clamp(*Channels[Ch], 0.0f, 1.0f);
        Surface.FillRectangle({ Bar.X, Bar.Y + 3.0f, Bar.Width * Norm, 4.0f }, Tints[Ch].ScaledAlpha(Opacity), 2.0f);
        Surface.FillCircle({ Bar.X + Bar.Width * Norm, Bar.Y + 5.0f }, 4.5f, ColorRgba{ 238u, 238u, 238u, 255u }.ScaledAlpha(Opacity));

        if (!Disabled)
        {
            if (Bar.Contains(Cursor) && Input.PrimaryPointer.Pressed) InOutActiveDragId = Cid;
            if (InOutActiveDragId == Cid && Input.PrimaryPointer.Down)
            {
                const float Next = std::clamp((Cursor.X - Bar.X) / std::max(1.0f, Bar.Width), 0.0f, 1.0f);
                if (std::fabs(Next - *Channels[Ch]) > 1.0e-4f)
                {
                    *Channels[Ch] = Next;
                    OutChanged    = true;
                }
            }
            else if (InOutActiveDragId == Cid && !Input.PrimaryPointer.Down)
            {
                InOutActiveDragId = 0u;
            }
        }
    }

    return BarY + 22.0f;
}

float ConstructChoiceRow(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    const char*                  Label,
    std::span<const char* const> Choices,
    uint32_t                     ActiveIndex,
    uint32_t                     ColumnCount,
    const PaneInputReading&      Input,
    int32_t&                     OutChosenIndex) noexcept
{
    OutChosenIndex = -1;
    float CursorY = TopY;
    if (Label != nullptr && Label[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, CursorY }, 11.0f, InspectorMetrics::MutedInk, Label);
        CursorY += 18.0f;
    }

    const uint32_t SafeCols = std::max(1u, ColumnCount);
    const float    Gap      = 6.0f;
    const float    BtnW     = std::floor((InnerWidth - Gap * static_cast<float>(SafeCols - 1u)) / static_cast<float>(SafeCols));
    const float    BtnH     = 28.0f;
    const uint32_t Rows     = (static_cast<uint32_t>(Choices.size()) + SafeCols - 1u) / SafeCols;
    const Point2D  Cursor { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };

    for (std::size_t Idx = 0u; Idx < Choices.size(); ++Idx)
    {
        const uint32_t    Col = static_cast<uint32_t>(Idx) % SafeCols;
        const uint32_t    Row = static_cast<uint32_t>(Idx) / SafeCols;
        const Rectangle2D Btn {
            LeftX + static_cast<float>(Col) * (BtnW + Gap),
            CursorY + static_cast<float>(Row) * (BtnH + Gap),
            BtnW,
            BtnH,
        };
        const bool Active  = (static_cast<uint32_t>(Idx) == ActiveIndex);
        const bool Hovered = Btn.Contains(Cursor);

        if (Active)
        {
            Surface.FillRectangle(Btn, ColorRgba{ 236u, 236u, 239u, 255u }, 9.0f);
        }
        else
        {
            Surface.FillRectangle(Btn, Hovered ? ColorRgba{ 46u, 46u, 50u, 255u } : ColorRgba{ 24u, 24u, 27u, 255u }, 9.0f);
            Surface.StrokeRectangle(Btn, ColorRgba{ 255u, 255u, 255u, 18u }, 9.0f, 1.0f);
        }

        const float MaxW = Btn.Width - 10.0f;
        const float LW   = std::min(MaxW, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, Active ? FontWeightClass::Medium : FontWeightClass::Regular, 10.5f, Choices[Idx]));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            Active ? FontWeightClass::Medium : FontWeightClass::Regular,
            { Btn.Center().X - LW * 0.5f, Btn.Y + 7.5f },
            MaxW,
            10.5f,
            Active ? ColorRgba{ 18u, 18u, 20u, 255u } : ColorRgba{ 196u, 196u, 200u, 255u },
            Choices[Idx]);

        if (Hovered && Input.PrimaryPointer.Pressed)
        {
            OutChosenIndex = static_cast<int32_t>(Idx);
        }
    }

    return CursorY + static_cast<float>(Rows) * BtnH + static_cast<float>(Rows > 0u ? Rows - 1u : 0u) * Gap + 14.0f;
}

void ConstructCurvePlot(
    PixelSpace&            Surface,
    Rectangle2D            Bounds,
    std::span<const float> NormalizedY,
    ColorRgba              StrokeInk,
    ColorRgba              UnderFill,
    const char*            LeftCaption,
    const char*            RightCaption,
    float                  MarkerX01) noexcept
{
    Surface.FillRectangle(Bounds, ColorRgba{ 14u, 15u, 18u, 255u }, 12.0f);
    Surface.StrokeRectangle(Bounds, ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);

    // Subtle horizontal grid lines
    for (int32_t Line = 1; Line <= 3; ++Line)
    {
        const float GY = Bounds.Y + Bounds.Height * (static_cast<float>(Line) * 0.25f);
        Surface.StrokeLine({ Bounds.X + 8.0f, GY }, { Bounds.Right() - 8.0f, GY }, ColorRgba{ 255u, 255u, 255u, 12u }, 1.0f);
    }

    if (NormalizedY.size() >= 2u)
    {
        const float PadX   = 10.0f;
        const float PadTop = 10.0f;
        const float PadBot = (LeftCaption != nullptr || RightCaption != nullptr) ? 20.0f : 10.0f;
        const float PlotW  = std::max(8.0f, Bounds.Width - PadX * 2.0f);
        const float PlotH  = std::max(8.0f, Bounds.Height - PadTop - PadBot);
        const float FloorY = Bounds.Y + PadTop + PlotH;

        std::array<Point2D, 64u> Poly{};
        const std::size_t Count = std::min<std::size_t>(NormalizedY.size(), Poly.size());
        for (std::size_t I = 0u; I < Count; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Count - 1u);
            const float V = std::clamp(NormalizedY[I], 0.0f, 1.0f);
            Poly[I] = { Bounds.X + PadX + T * PlotW, FloorY - V * PlotH };
        }

        // Under-curve vertical fill ribbons
        for (std::size_t I = 0u; I + 1u < Count; ++I)
        {
            const float MidY = (Poly[I].Y + Poly[I + 1u].Y) * 0.5f;
            const float SegW = std::max(1.0f, Poly[I + 1u].X - Poly[I].X + 0.5f);
            Surface.FillRectangle({ Poly[I].X, MidY, SegW, std::max(0.0f, FloorY - MidY) }, UnderFill);
        }

        Surface.StrokePolyline(std::span<const Point2D>(Poly.data(), Count), StrokeInk, 1.8f, false);

        if (MarkerX01 >= 0.0f && MarkerX01 <= 1.0f)
        {
            const float MX  = Bounds.X + PadX + MarkerX01 * PlotW;
            const std::size_t MIdx = std::min(Count - 1u, static_cast<std::size_t>(MarkerX01 * static_cast<float>(Count - 1u)));
            Surface.StrokeLine({ MX, Bounds.Y + PadTop }, { MX, FloorY }, ColorRgba{ 255u, 255u, 255u, 45u }, 1.0f);
            Surface.FillCircle({ MX, Poly[MIdx].Y }, 3.8f, ColorRgba{ 246u, 246u, 248u, 255u });
        }
    }

    if (LeftCaption != nullptr && LeftCaption[0] != '\0')
    {
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.X + 10.0f, Bounds.Bottom() - 15.0f }, 9.0f, InspectorMetrics::DimInk, LeftCaption);
    }
    if (RightCaption != nullptr && RightCaption[0] != '\0')
    {
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, RightCaption);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Bounds.Right() - 10.0f - RW, Bounds.Bottom() - 15.0f }, 9.0f, InspectorMetrics::DimInk, RightCaption);
    }
}

namespace
{
struct QueuedTooltipData
{
    bool                              Active     = false;
    Rectangle2D                       Anchor     {};
    std::string                       Heading    {};
    std::string                       BodyText   {};
    std::vector<InspectorTooltipFact> Facts      {};
};

QueuedTooltipData ActiveTooltipQueue {};
} // namespace

void ResetTooltipQueue() noexcept
{
    ActiveTooltipQueue.Active = false;
    ActiveTooltipQueue.Heading.clear();
    ActiveTooltipQueue.BodyText.clear();
    ActiveTooltipQueue.Facts.clear();
}

void QueueTooltip(Rectangle2D AnchorRect, std::string_view Heading, std::string_view BodyText) noexcept
{
    ActiveTooltipQueue.Active   = true;
    ActiveTooltipQueue.Anchor   = AnchorRect;
    ActiveTooltipQueue.Heading  = std::string(Heading);
    ActiveTooltipQueue.BodyText = std::string(BodyText);
    ActiveTooltipQueue.Facts.clear();
}

void QueueFactTooltip(
    Rectangle2D                           AnchorRect,
    std::string_view                      Heading,
    std::span<const InspectorTooltipFact> Facts) noexcept
{
    ActiveTooltipQueue.Active = true;
    ActiveTooltipQueue.Anchor = AnchorRect;
    ActiveTooltipQueue.Heading = std::string(Heading);
    ActiveTooltipQueue.BodyText.clear();
    ActiveTooltipQueue.Facts.assign(Facts.begin(), Facts.end());
}

void FlushTooltipOverlay(PixelSpace& Surface, Rectangle2D PaneBounds) noexcept
{
    if (!ActiveTooltipQueue.Active) return;

    const float TipW = std::min(258.0f, std::max(180.0f, PaneBounds.Width - 20.0f));
    const float Pad  = 11.0f;
    float TipH = Pad * 2.0f;
    if (!ActiveTooltipQueue.Heading.empty()) TipH += 18.0f;
    if (!ActiveTooltipQueue.BodyText.empty()) TipH += 32.0f;
    TipH += static_cast<float>(ActiveTooltipQueue.Facts.size()) * 17.0f;

    float TipX = ActiveTooltipQueue.Anchor.X;
    if (TipX + TipW > PaneBounds.Right() - 8.0f) TipX = PaneBounds.Right() - 8.0f - TipW;
    if (TipX < PaneBounds.X + 8.0f) TipX = PaneBounds.X + 8.0f;

    float TipY = ActiveTooltipQueue.Anchor.Bottom() + 6.0f;
    if (TipY + TipH > PaneBounds.Bottom() - 8.0f) TipY = std::max(PaneBounds.Y + 8.0f, ActiveTooltipQueue.Anchor.Y - TipH - 6.0f);

    const Rectangle2D Box { TipX, TipY, TipW, TipH };
    // Soft elevated dark shadow & glass surface matching F76 .baked-map-tip
    Surface.FillRectangle({ Box.X + 2.0f, Box.Y + 5.0f, Box.Width, Box.Height }, ColorRgba{ 0u, 0u, 0u, 145u }, 10.0f);
    Surface.FillRectangle(Box, ColorRgba{ 22u, 23u, 27u, 248u }, 10.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 32u }, 10.0f, 1.0f);

    float Y = Box.Y + Pad;
    if (!ActiveTooltipQueue.Heading.empty())
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { Box.X + Pad, Y }, TipW - Pad * 2.0f, 10.5f, InspectorMetrics::HeadingInk, ActiveTooltipQueue.Heading.c_str());
        Y += 18.0f;
    }
    if (!ActiveTooltipQueue.BodyText.empty())
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Box.X + Pad, Y }, TipW - Pad * 2.0f, 9.8f, InspectorMetrics::MutedInk, ActiveTooltipQueue.BodyText.c_str());
        Y += 16.0f;
    }
    for (const auto& Fact : ActiveTooltipQueue.Facts)
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Medium, { Box.X + Pad, Y }, 68.0f, 8.8f, ColorRgba{ 130u, 136u, 146u, 255u }, Fact.Label.c_str());
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Box.X + Pad + 72.0f, Y }, TipW - Pad * 2.0f - 72.0f, 9.5f, ColorRgba{ 220u, 223u, 228u, 255u }, Fact.Reading.c_str());
        Y += 17.0f;
    }
}

void ConstructTooltipTrigger(
    PixelSpace&             Surface,
    Point2D                 Center,
    std::string_view        Heading,
    std::string_view        BodyText,
    const PaneInputReading& Input) noexcept
{
    const Rectangle2D Hit { Center.X - 8.0f, Center.Y - 8.0f, 16.0f, 16.0f };
    const Point2D     Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Hov = Hit.Contains(Cur);
    Surface.FillCircle(Center, 7.0f, Hov ? ColorRgba{ 58u, 72u, 92u, 255u } : ColorRgba{ 36u, 37u, 42u, 255u });
    Surface.StrokeCircle(Center, 7.0f, Hov ? ColorRgba{ 156u, 198u, 246u, 190u } : ColorRgba{ 255u, 255u, 255u, 32u }, 1.0f);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { Center.X - 2.6f, Center.Y - 5.2f }, 9.2f,
        Hov ? ColorRgba{ 228u, 238u, 255u, 255u } : InspectorMetrics::MutedInk, "?");
    if (Hov)
    {
        QueueTooltip(Hit, Heading, BodyText);
    }
}

float ConstructBakedLutSection(
    PixelSpace&              Surface,
    float                    LeftX,
    float                    TopY,
    float                    InnerWidth,
    const char*              Title,
    const char*              ResolutionText,
    const char*              FormatText,
    const char*              AxisCaption,
    InspectorPreviewSurface& Preview,
    float                    PreviewHeightPx,
    const PaneInputReading*  OptionalInput,
    bool*                    OptionalSettingsOpen,
    uint32_t*                OptionalScaleTier,
    bool*                    OptionalRebakeClicked,
    bool*                    OptionalSaveClicked) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Point2D Cur = OptionalInput ? Point2D{ OptionalInput->PrimaryPointer.X, OptionalInput->PrimaryPointer.Y } : Point2D{ -9999.0f, -9999.0f };
    const bool Clicked = OptionalInput && OptionalInput->PrimaryPointer.Pressed;

    // Bake Settings face (F76): resolution tier selector + texel/memory facts + Cancel / Bake buttons
    if (OptionalSettingsOpen && *OptionalSettingsOpen)
    {
        const float BoxH = InspectorMetrics::SectionPadTop + 28.0f + 56.0f + 114.0f + 40.0f + InspectorMetrics::SectionPadBottom;
        const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
        ConstructSectionSurface(Surface, Section, "Bake settings", Title, InspectorMetrics::MutedInk);

        float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
        uint32_t Tier = OptionalScaleTier ? *OptionalScaleTier : 1u;
        static constexpr std::array<const char*, 3u> TierLabels = {{ "Half", "Default", "2x Fine" }};
        const float SegW = (BodyW - 12.0f) / 3.0f;
        for (uint32_t I = 0u; I < 3u; ++I)
        {
            const Rectangle2D Seg { LeftX + PadX + static_cast<float>(I) * (SegW + 6.0f), Y, SegW, 44.0f };
            const bool On  = (Tier == I);
            const bool Hov = Seg.Contains(Cur);
            Surface.FillRectangle(Seg, On ? ColorRgba{ 46u, 52u, 62u, 255u } : (Hov ? ColorRgba{ 36u, 37u, 42u, 255u } : ColorRgba{ 28u, 29u, 32u, 255u }), 9.0f);
            Surface.StrokeRectangle(Seg, On ? ColorRgba{ 146u, 190u, 245u, 140u } : ColorRgba{ 255u, 255u, 255u, 18u }, 9.0f, 1.0f);
            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.5f, TierLabels[I]);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Seg.Center().X - TW * 0.5f, Seg.Y + 9.0f }, 10.5f, InspectorMetrics::HeadingInk, TierLabels[I]);
            const char* Sub = (I == 0u) ? "0.5x res" : (I == 1u ? "default" : "2.0x res");
            const float SW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Sub);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Seg.Center().X - SW * 0.5f, Seg.Y + 25.0f }, 9.0f, InspectorMetrics::DimInk, Sub);
            if (Hov && Clicked)
            {
                Tier = I;
                if (OptionalScaleTier) *OptionalScaleTier = I;
            }
        }
        Y += 54.0f;

        const uint32_t SourceW = std::max(1u, Preview.QueryWidthPx());
        const uint32_t SourceH = std::max(1u, Preview.QueryHeightPx());
        const uint32_t MulNum = (Tier == 0u) ? 1u : (Tier == 1u ? 2u : 4u);
        const uint32_t OutW = std::max(16u, (SourceW * MulNum) / 2u);
        const uint32_t OutH = std::max(16u, (SourceH * MulNum) / 2u);
        const uint32_t BytesTotal = OutW * OutH * 4u;
        char ResLine[32]{}, TexLine[32]{}, MemLine[32]{};
        std::snprintf(ResLine, sizeof(ResLine), "%u x %u", OutW, OutH);
        std::snprintf(TexLine, sizeof(TexLine), "%u texels", OutW * OutH);
        std::snprintf(MemLine, sizeof(MemLine), "%u KB (RGBA8)", std::max(1u, BytesTotal / 1024u));

        const Rectangle2D FactBox { LeftX + PadX, Y, BodyW, 104.0f };
        Surface.FillRectangle(FactBox, ColorRgba{ 255u, 255u, 255u, 8u }, 8.0f);
        Surface.StrokeRectangle(FactBox, ColorRgba{ 255u, 255u, 255u, 14u }, 8.0f, 1.0f);
        float FY = FactBox.Y + 8.0f;
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Resolution", ResLine);
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Format", FormatText);
        FY = ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Texels", TexLine);
        (void)ConstructMetadataRow(Surface, FactBox.X + 10.0f, FY, FactBox.Width - 20.0f, "Memory", MemLine);
        Y = FactBox.Bottom() + 10.0f;

        const float HalfBtnW = (BodyW - 8.0f) * 0.5f;
        if (OptionalInput && ConstructSectionActionButton(Surface, LeftX + PadX, Y, HalfBtnW, "Cancel", *OptionalInput, false))
        {
            *OptionalSettingsOpen = false;
        }
        if (OptionalInput && ConstructSectionActionButton(Surface, LeftX + PadX + HalfBtnW + 8.0f, Y, HalfBtnW, "Bake now", *OptionalInput, true))
        {
            *OptionalSettingsOpen = false;
            if (OptionalRebakeClicked) *OptionalRebakeClicked = true;
        }
        return Section.Bottom() + InspectorMetrics::SectionGap;
    }

    // Standard Baked face (F76): Title header, Baked status bar (green Baked capsule + tooltip + res + Rebake/Save icons), LUT image, axis/format captions
    const float BarH = 26.0f;
    const float BoxH = InspectorMetrics::SectionPadTop + 26.0f + BarH + 6.0f + PreviewHeightPx + 42.0f + InspectorMetrics::SectionPadBottom;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, Title, "", InspectorMetrics::MutedInk);

    const float BarY = TopY + InspectorMetrics::SectionPadTop + 24.0f;
    // Green "Baked" capsule (hover opens F76 details tooltip)
    const Rectangle2D BakedCap { LeftX + PadX, BarY + 2.0f, 68.0f, 22.0f };
    const bool BakedHover = BakedCap.Contains(Cur);
    Surface.FillRectangle(BakedCap, BakedHover ? ColorRgba{ 28u, 62u, 42u, 255u } : ColorRgba{ 22u, 46u, 33u, 255u }, 11.0f);
    Surface.StrokeRectangle(BakedCap, ColorRgba{ 88u, 214u, 141u, 110u }, 11.0f, 1.0f);
    // Crisp check stroke inside Baked capsule
    Surface.StrokeLine({ BakedCap.X + 9.0f, BakedCap.Y + 11.5f }, { BakedCap.X + 12.0f, BakedCap.Y + 14.5f }, ColorRgba{ 118u, 232u, 162u, 255u }, 1.5f);
    Surface.StrokeLine({ BakedCap.X + 12.0f, BakedCap.Y + 14.5f }, { BakedCap.X + 17.5f, BakedCap.Y + 8.0f }, ColorRgba{ 118u, 232u, 162u, 255u }, 1.5f);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { BakedCap.X + 23.0f, BakedCap.Y + 5.0f }, 10.0f, ColorRgba{ 150u, 238u, 184u, 255u }, "Baked");

    // Monospace resolution readout beside Baked capsule
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { BakedCap.Right() + 8.0f, BarY + 7.0f }, BodyW - 134.0f, 9.8f, InspectorMetrics::MutedInk, ResolutionText);

    // Right-side Rebake (↺) & Save PNG (↓) icon buttons
    const Rectangle2D SaveBtn   { LeftX + PadX + BodyW - 24.0f, BarY + 1.0f, 24.0f, 24.0f };
    const Rectangle2D RebakeBtn { SaveBtn.X - 28.0f,            BarY + 1.0f, 24.0f, 24.0f };
    const bool RebakeHov = RebakeBtn.Contains(Cur);
    const bool SaveHov   = SaveBtn.Contains(Cur);

    Surface.FillRectangle(RebakeBtn, RebakeHov ? ColorRgba{ 46u, 48u, 54u, 255u } : ColorRgba{ 30u, 31u, 35u, 255u }, 7.0f);
    Surface.StrokeRectangle(RebakeBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(RebakeHov ? 42u : 18u) }, 7.0f, 1.0f);
    Surface.StrokeCircle(RebakeBtn.Center(), 5.0f, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ RebakeBtn.Center().X - 5.5f, RebakeBtn.Center().Y - 4.0f }, { RebakeBtn.Center().X - 2.0f, RebakeBtn.Center().Y - 4.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);

    Surface.FillRectangle(SaveBtn, SaveHov ? ColorRgba{ 46u, 48u, 54u, 255u } : ColorRgba{ 30u, 31u, 35u, 255u }, 7.0f);
    Surface.StrokeRectangle(SaveBtn, ColorRgba{ 255u, 255u, 255u, static_cast<uint8_t>(SaveHov ? 42u : 18u) }, 7.0f, 1.0f);
    Surface.StrokeLine({ SaveBtn.Center().X, SaveBtn.Y + 6.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.Center().X - 3.5f, SaveBtn.Y + 11.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.5f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.Center().X + 3.5f, SaveBtn.Y + 11.0f }, { SaveBtn.Center().X, SaveBtn.Y + 14.5f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);
    Surface.StrokeLine({ SaveBtn.X + 6.5f, SaveBtn.Bottom() - 6.0f }, { SaveBtn.Right() - 6.5f, SaveBtn.Bottom() - 6.0f }, ColorRgba{ 210u, 214u, 222u, 255u }, 1.3f);

    if (BakedHover)
    {
        const uint32_t Wpx = std::max(1u, Preview.QueryWidthPx());
        const uint32_t Hpx = std::max(1u, Preview.QueryHeightPx());
        char SizeBuf[32]{}, CountBuf[32]{};
        std::snprintf(SizeBuf,  sizeof(SizeBuf),  "%u KB · 4 B/texel", std::max(1u, (Wpx * Hpx * 4u) / 1024u));
        std::snprintf(CountBuf, sizeof(CountBuf), "Retained bake #%u", Preview.QueryUploadCount());
        const std::array<InspectorTooltipFact, 5u> Facts = {{
            { "MAP",        Title ? Title : "Baked LUT" },
            { "RESOLUTION", ResolutionText ? ResolutionText : "-" },
            { "FORMAT",     FormatText ? FormatText : "RGBA8" },
            { "SIZE",       SizeBuf },
            { "STATUS",     CountBuf },
        }};
        QueueFactTooltip(BakedCap, "Baked LUT details", Facts);
    }
    else if (RebakeHov)
    {
        QueueTooltip(RebakeBtn, "Rebake", "Open resolution settings & re-bake this LUT");
    }
    else if (SaveHov)
    {
        QueueTooltip(SaveBtn, "Save PNG", "Export current baked LUT image to disk");
    }

    if (RebakeHov && Clicked)
    {
        if (OptionalSettingsOpen) *OptionalSettingsOpen = true;
        else if (OptionalRebakeClicked) *OptionalRebakeClicked = true;
    }
    if (SaveHov && Clicked && OptionalSaveClicked)
    {
        *OptionalSaveClicked = true;
    }

    const Rectangle2D ImgBox { LeftX + PadX, BarY + BarH + 6.0f, BodyW, PreviewHeightPx };
    Preview.Present(Surface, ImgBox, 10.0f);
    Surface.StrokeRectangle(ImgBox, ColorRgba{ 255u, 255u, 255u, 22u }, 10.0f, 1.0f);

    // Live cursor crosshair readout when pointing inside the baked image
    if (ImgBox.Contains(Cur))
    {
        Surface.StrokeCircle(Cur, 5.5f, ColorRgba{ 255u, 255u, 255u, 220u }, 1.0f);
        const float U = std::clamp((Cur.X - ImgBox.X) / std::max(1.0f, ImgBox.Width), 0.0f, 1.0f);
        const float V = std::clamp((Cur.Y - ImgBox.Y) / std::max(1.0f, ImgBox.Height), 0.0f, 1.0f);
        char UvBuf[48]{};
        std::snprintf(UvBuf, sizeof(UvBuf), "Cursor U %.2f · V %.2f", U, V);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, ImgBox.Bottom() + 10.0f }, BodyW, 10.0f, ColorRgba{ 226u, 232u, 240u, 255u }, UvBuf);
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, ImgBox.Bottom() + 10.0f }, BodyW, 10.0f, InspectorMetrics::MutedInk, FormatText);
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { LeftX + PadX, ImgBox.Bottom() + 26.0f }, BodyW, 9.5f, InspectorMetrics::DimInk, AxisCaption);

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructTagsSection(
    PixelSpace&                  Surface,
    float                        LeftX,
    float                        TopY,
    float                        InnerWidth,
    std::span<const char* const> TagLabels) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX;
    char CountBuf[16]{};
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu", TagLabels.size());

    // Measure wrapped chip height
    float ChipX = LeftX + PadX;
    float ChipY = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    const float MaxX = LeftX + InnerWidth - PadX;
    const float ChipH = 26.0f;
    for (const char* Tag : TagLabels)
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Tag) + 28.0f;
        if (ChipX + W > MaxX && ChipX > LeftX + PadX)
        {
            ChipX = LeftX + PadX;
            ChipY += ChipH + 6.0f;
        }
        ChipX += W + 6.0f;
    }

    const float BoxH = (ChipY + ChipH + InspectorMetrics::SectionPadBottom) - TopY;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, "Tags", CountBuf, InspectorMetrics::MutedInk);

    ChipX = LeftX + PadX;
    ChipY = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    for (const char* Tag : TagLabels)
    {
        const float W = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 11.0f, Tag) + 28.0f;
        if (ChipX + W > MaxX && ChipX > LeftX + PadX)
        {
            ChipX = LeftX + PadX;
            ChipY += ChipH + 6.0f;
        }
        const Rectangle2D Chip { ChipX, ChipY, W, ChipH };
        Surface.FillRectangle(Chip, InspectorMetrics::TagFill, ChipH * 0.5f);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.X + 11.0f, Chip.Y + 6.5f }, 11.0f, InspectorMetrics::TagInk, Tag);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Chip.Right() - 14.0f, Chip.Y + 6.0f }, 11.0f, InspectorMetrics::TagInk.ScaledAlpha(0.65f), "x");
        ChipX += W + 6.0f;
    }

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructNotesSection(
    PixelSpace&  Surface,
    float        LeftX,
    float        TopY,
    float        InnerWidth,
    std::string& NotesText) noexcept
{
    const float PadX = InspectorMetrics::SectionPadX;
    const float AreaH = 86.0f;
    const float BoxH  = InspectorMetrics::SectionPadTop + 28.0f + AreaH + InspectorMetrics::SectionPadBottom;
    const Rectangle2D Section { LeftX, TopY, InnerWidth, BoxH };
    ConstructSectionSurface(Surface, Section, "Notes");

    const Rectangle2D Area { LeftX + PadX, TopY + InspectorMetrics::SectionPadTop + 28.0f, InnerWidth - PadX * 2.0f, AreaH };
    Surface.FillRectangle(Area, InspectorMetrics::NotesFill, 12.0f);
    Surface.StrokeRectangle(Area, InspectorMetrics::NotesBorder, 12.0f, 1.0f);

    const char* Shown = NotesText.empty() ? "Purpose, ownership, review notes..." : NotesText.c_str();
    const ColorRgba Ink = NotesText.empty() ? InspectorMetrics::DimInk : ColorRgba{ 214u, 214u, 214u, 255u };
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { Area.X + 12.0f, Area.Y + 11.0f }, Area.Width - 24.0f, 11.5f, Ink, Shown);

    return Section.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructTransformSections(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    float CursorY     = TopY;
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Position");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 1u, "X", "m", "%.2f", -500.0f, 500.0f, Entry.PositionX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 2u, "Y", "m", "%.2f", -500.0f, 500.0f, Entry.PositionY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 3u, "Z", "m", "%.2f", -500.0f, 500.0f, Entry.PositionZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Rotation");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 4u, "Pitch", "deg", "%.1f", -180.0f, 180.0f, Entry.RotationX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 5u, "Roll",  "deg", "%.1f", -180.0f, 180.0f, Entry.RotationY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 6u, "Yaw",   "deg", "%.1f", -180.0f, 180.0f, Entry.RotationZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    {
        const Rectangle2D S { LeftX, CursorY, InnerWidth, 250.0f };
        ConstructSectionSurface(Surface, S, "Scale");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 7u, "Scale X", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleX, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 8u, "Scale Y", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleY, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 9u, "Scale Z", "x", "%.2f", 0.05f, 50.0f, Entry.ScaleZ, InOutActiveDragId, Input, OutMutated, Entry.Locked);
        CursorY = S.Bottom() + InspectorMetrics::SectionGap;
    }
    return CursorY;
}

float ConstructMaterialSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    GeometrySurfaceBinding& Binding,
    uint32_t                ControlOriginId,
    uint32_t&               InOutActiveDragId,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    char SlotTxt[24]{};
    std::snprintf(SlotTxt, sizeof(SlotTxt), "Slot %u", static_cast<unsigned>(Binding.MaterialSlotOrdinal));
    const Rectangle2D S { LeftX, TopY, InnerWidth, 344.0f };
    ConstructSectionSurface(Surface, S, "OpenPBR surface material", SlotTxt);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    Y = ConstructColourFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 1u,  "Albedo tint", Binding.AlbedoTint, InOutActiveDragId, Input, OutMutated);
    Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 10u, "Roughness",   "",  "%.2f", 0.04f, 1.0f, Binding.Roughness, InOutActiveDragId, Input, OutMutated);
    Y = ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 11u, "Metallic",    "",  "%.2f", 0.0f,  1.0f, Binding.Metallic,  InOutActiveDragId, Input, OutMutated);
    (void)ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, ControlOriginId + 12u, "Self glow", "x", "%.2f", 0.0f,  4.0f, Binding.SelfGlow,  InOutActiveDragId, Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructPhysicsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    const char*             CollisionSummary,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, 256.0f };
    ConstructSectionSurface(Surface, S, "Physics & visibility", CollisionSummary);
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Visible in viewport", Entry.VisibilityEnabled, Input, OutMutated);
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows",        Entry.CastShadows,       Input, OutMutated);
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Dynamic motion",      Entry.DynamicMotion,     Input, OutMutated);
    (void)ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock transform",    Entry.Locked,            Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

float ConstructMetadataRow(
    PixelSpace& Surface,
    float       LeftX,
    float       TopY,
    float       InnerWidth,
    const char* Label,
    const char* ReadingText) noexcept
{
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { LeftX, TopY }, 11.0f, InspectorMetrics::MutedInk, Label);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.0f, ReadingText);
    const float MaxW = std::max(40.0f, InnerWidth - 96.0f);
    const float TextX = LeftX + InnerWidth - std::min(RW, MaxW);
    WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { TextX, TopY }, MaxW, 11.0f, ColorRgba{ 220u, 222u, 228u, 255u }, ReadingText);
    return TopY + 24.0f;
}

bool ConstructSectionActionButton(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    const char*             Label,
    const PaneInputReading& Input,
    bool                    Accent) noexcept
{
    const Rectangle2D Btn { LeftX, TopY, InnerWidth, 30.0f };
    const Point2D     Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool        Hov = Btn.Contains(Cur);
    const ColorRgba   Bg  = Accent
        ? (Hov ? ColorRgba{ 42u, 62u, 88u, 255u } : ColorRgba{ 32u, 46u, 64u, 255u })
        : (Hov ? ColorRgba{ 48u, 48u, 54u, 255u } : ColorRgba{ 34u, 34u, 38u, 255u });
    const ColorRgba   Bdr = Accent ? ColorRgba{ 138u, 184u, 240u, 72u } : ColorRgba{ 255u, 255u, 255u, 20u };
    const ColorRgba   Ink = Accent ? ColorRgba{ 196u, 224u, 255u, 255u } : InspectorMetrics::HeadingInk;
    Surface.FillRectangle(Btn, Bg, 12.0f);
    Surface.StrokeRectangle(Btn, Bdr, 12.0f, 1.0f);
    const float LW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 11.5f, Label);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Btn.Center().X - LW * 0.5f, Btn.Y + 8.0f }, 11.5f, Ink, Label);
    return Hov && Input.PrimaryPointer.Pressed;
}

Mathematics::LinearRGB ComputeKelvinRgb(float Kelvin) noexcept
{
    const float Heat = std::clamp(Kelvin, 1000.0f, 40000.0f) * 0.01f;
    float R = 255.0f, G = 255.0f, B = 255.0f;
    if (Heat > 66.0f)
    {
        R = 329.6987f * std::pow(Heat - 60.0f, -0.1332f);
        G = 288.1222f * std::pow(Heat - 60.0f, -0.0755f);
    }
    else
    {
        G = 99.4708f * std::log(std::max(1.0f, Heat)) - 161.1196f;
        B = (Heat <= 19.0f) ? 0.0f : (138.5177f * std::log(std::max(1.0f, Heat - 10.0f)) - 305.0448f);
    }
    return {
        std::clamp(R / 255.0f, 0.0f, 1.0f),
        std::clamp(G / 255.0f, 0.0f, 1.0f),
        std::clamp(B / 255.0f, 0.0f, 1.0f),
    };
}

float ConstructLuminaireShadowsSection(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   TopY,
    float                   InnerWidth,
    OutlinerRecordEntry&    Entry,
    bool                    SupportsShadowCasting,
    const char*             HelperToggleLabel,
    bool&                   InOutHelperVisible,
    const PaneInputReading& Input,
    bool&                   OutMutated) noexcept
{
    const float PadX  = InspectorMetrics::SectionPadX;
    const float BodyW = InnerWidth - PadX * 2.0f;
    const float H     = SupportsShadowCasting ? 204.0f : 164.0f;
    const Rectangle2D S { LeftX, TopY, InnerWidth, H };
    ConstructSectionSurface(Surface, S, "Scene participation");
    float Y = TopY + InspectorMetrics::SectionPadTop + 28.0f;
    if (SupportsShadowCasting)
    {
        Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Cast shadows", Entry.CastShadows, Input, OutMutated);
    }
    Y = ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, HelperToggleLabel, InOutHelperVisible, Input, OutMutated);
    (void)ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Lock luminaire", Entry.Locked, Input, OutMutated);
    return S.Bottom() + InspectorMetrics::SectionGap;
}

void ConstructInspectorFooter(
    PixelSpace& Surface,
    Rectangle2D FooterBounds,
    const char* LeftCaption,
    const char* RightCaption) noexcept
{
    Surface.StrokeLine({ FooterBounds.X, FooterBounds.Y }, { FooterBounds.Right(), FooterBounds.Y }, ColorRgba{ 36u, 36u, 36u, 255u }, 1.0f);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { FooterBounds.X + 18.0f, FooterBounds.Y + 8.0f }, FooterBounds.Width - 110.0f, 10.0f, InspectorMetrics::DimInk, LeftCaption);
    const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, RightCaption);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { FooterBounds.Right() - 18.0f - RW, FooterBounds.Y + 8.0f }, 10.0f, InspectorMetrics::DimInk, RightCaption);
}

} // namespace InspectorPrimitives

} // namespace Frontier
