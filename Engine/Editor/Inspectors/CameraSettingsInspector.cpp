/*============================================================================================================================================
                                                        CAMERASETTINGSINSPECTOR.CPP
============================================================================================================================================*/
// 🧩 Standalone Camera Settings Inspector matching ProjectZeroEditor RenderComponents.jsx 1:1 while driving real C++ / .slang uniforms live

#include "Engine/Editor/Inspectors/CameraSettingsInspector.h"

#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace Frontier
{

namespace
{

constexpr float kPiLocal = 3.14159265359f;

void StrokeOval(PixelSpace& Surface, Point2D Center, float Rx, float Ry, ColorRgba Stroke, float Thickness) noexcept
{
    std::array<Point2D, 24u> Pts {};
    for (uint32_t I = 0u; I < 24u; ++I)
    {
        const float A = (2.0f * kPiLocal * static_cast<float>(I)) / 24.0f;
        Pts[I] = { Center.X + std::cos(A) * Rx, Center.Y + std::sin(A) * Ry };
    }
    Surface.StrokePolyline(Pts, Stroke, Thickness, true);
}

void ApplyMasterQualityTier(CameraSettingsData& Data, uint32_t TierIdx) noexcept
{
    Data.QualityTierIndex = std::min(TierIdx, 4u);
    static constexpr std::array<float, 5u> GtaoSlices  = {{ 2.0f, 3.0f, 4.0f, 6.0f, 8.0f }};
    static constexpr std::array<float, 5u> GtaoSteps   = {{ 2.0f, 3.0f, 4.0f, 6.0f, 8.0f }};
    static constexpr std::array<float, 5u> SdfSteps    = {{ 4.0f, 6.0f, 8.0f, 10.0f, 12.0f }};
    static constexpr std::array<float, 5u> SmrtRays    = {{ 2.0f, 3.0f, 4.0f, 6.0f, 8.0f }};
    static constexpr std::array<float, 5u> SmrtMarch   = {{ 6.0f, 8.0f, 10.0f, 12.0f, 16.0f }};
    static constexpr std::array<float, 5u> PtBatch     = {{ 1.0f, 1.0f, 2.0f, 4.0f, 8.0f }};
    static constexpr std::array<float, 5u> PtTarget    = {{ 64.0f, 128.0f, 256.0f, 512.0f, 1024.0f }};
    static constexpr std::array<float, 5u> PtBounces   = {{ 2.0f, 3.0f, 4.0f, 6.0f, 8.0f }};
    static constexpr std::array<float, 5u> LodErr      = {{ 2.0f, 1.4f, 1.0f, 0.7f, 0.4f }};

    const std::size_t T = static_cast<std::size_t>(Data.QualityTierIndex);
    Data.XeGtaoSliceCount     = GtaoSlices[T];
    Data.XeGtaoStepsPerSide   = GtaoSteps[T];
    Data.SdfConeSteps         = SdfSteps[T];
    Data.SmrtShadowRays       = SmrtRays[T];
    Data.SmrtMarchSteps       = SmrtMarch[T];
    Data.PathTraceBatchSpp    = PtBatch[T];
    Data.PathTraceTargetSpp   = PtTarget[T];
    Data.PathTraceMaxBounces  = PtBounces[T];
    Data.ClusterLodPixelError = LodErr[T];
}

// Mini shaded sphere thumbnail drawn vector-crisp inside each 4-column Render Type tile
void PaintMiniEngineSphere(PixelSpace& Surface, Point2D Center, float Radius, uint32_t EngineIdx) noexcept
{
    const ColorRgba CoreTint =
        (EngineIdx == 0u) ? ColorRgba{ 118u, 168u, 232u, 255u } :
        (EngineIdx == 1u) ? ColorRgba{ 118u, 214u, 162u, 255u } :
        (EngineIdx == 2u) ? ColorRgba{ 166u, 146u, 236u, 255u } :
                            ColorRgba{ 208u, 194u, 176u, 255u };
    const ColorRgba HiTint =
        (EngineIdx == 0u) ? ColorRgba{ 212u, 234u, 255u, 255u } :
        (EngineIdx == 1u) ? ColorRgba{ 214u, 252u, 230u, 255u } :
        (EngineIdx == 2u) ? ColorRgba{ 234u, 226u, 255u, 255u } :
                            ColorRgba{ 244u, 236u, 224u, 255u };

    // Subtle ground horizon line
    const float FloorY = Center.Y + Radius + 2.0f;
    Surface.StrokeLine({ Center.X - Radius * 1.45f, FloorY }, { Center.X + Radius * 1.45f, FloorY }, ColorRgba{ 255u, 255u, 255u, 30u }, 1.0f);

    if (EngineIdx == 1u)
    {
        // Concentric SDF distance-field rings on floor (1:1 HTML B · SDF tile)
        StrokeOval(Surface, { Center.X + 3.0f, FloorY + 1.5f }, Radius * 1.15f, Radius * 0.28f, ColorRgba{ 102u, 210u, 158u, 120u }, 1.0f);
        StrokeOval(Surface, { Center.X + 3.0f, FloorY + 1.5f }, Radius * 0.68f, Radius * 0.16f, ColorRgba{ 102u, 210u, 158u, 170u }, 1.0f);
    }
    else if (EngineIdx == 2u)
    {
        // Dotted diagonal ray paths (1:1 HTML C · Ray traced tile)
        const ColorRgba RayCol { 176u, 154u, 245u, 175u };
        Surface.StrokeLine({ Center.X - Radius * 1.45f, Center.Y - Radius * 1.1f }, { Center.X - Radius * 0.72f, Center.Y - Radius * 0.55f }, RayCol, 1.1f);
        Surface.StrokeLine({ Center.X + Radius * 0.72f, Center.Y - Radius * 0.55f }, { Center.X + Radius * 1.45f, Center.Y - Radius * 1.1f }, RayCol, 1.1f);
        Surface.StrokeLine({ Center.X + Radius * 0.72f, Center.Y + Radius * 0.50f }, { Center.X + Radius * 1.40f, Center.Y + Radius * 0.95f }, RayCol, 1.1f);
    }

    // Soft layered 3D sphere shading
    Surface.FillCircle({ Center.X, FloorY }, Radius * 0.65f, ColorRgba{ 0u, 0u, 0u, 90u });
    Surface.FillCircle(Center, Radius, ColorRgba{
        static_cast<uint8_t>(CoreTint.Red * 0.48f),
        static_cast<uint8_t>(CoreTint.Green * 0.48f),
        static_cast<uint8_t>(CoreTint.Blue * 0.48f), 255u });
    Surface.FillCircle({ Center.X - Radius * 0.14f, Center.Y - Radius * 0.14f }, Radius * 0.80f, CoreTint);
    Surface.FillCircle({ Center.X - Radius * 0.28f, Center.Y - Radius * 0.30f }, Radius * 0.42f, HiTint);
}

// Technical study diagram well for Shadows (1:1 HTML 02-cs-1-render-camera-sdf_shadows.png)
void PaintShadowStudyWell(PixelSpace& Surface, Rectangle2D Box, uint32_t PenumbraIdx, float MarchSteps) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 22u, 22u, 24u, 255u }, 14.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 12u }, 14.0f, 1.0f);

    const float FloorY = Box.Bottom() - 24.0f;
    const Point2D Origin { Box.X + 34.0f, FloorY };
    const Point2D LightPt { Box.Right() - 34.0f, Box.Y + 20.0f };
    const Point2D Blocker { Box.X + Box.Width * 0.56f, Box.Y + Box.Height * 0.47f };

    // Surface line
    Surface.StrokeLine({ Box.X + 12.0f, FloorY }, { Box.Right() - 12.0f, FloorY }, ColorRgba{ 255u, 255u, 255u, 32u }, 1.0f);
    // Area emitter bar top-right
    Surface.FillRectangle({ LightPt.X - 18.0f, LightPt.Y - 3.0f, 36.0f, 5.0f }, ColorRgba{ 238u, 226u, 174u, 255u }, 2.5f);

    // Subtle cone fan on left surface origin
    for (int K = -2; K <= 2; ++K)
    {
        const float Ang = -kPiLocal * 0.5f + static_cast<float>(K) * 0.34f;
        Surface.StrokeLine(Origin, { Origin.X + std::cos(Ang) * 28.0f, Origin.Y + std::sin(Ang) * 28.0f }, ColorRgba{ 98u, 186u, 146u, 140u }, 1.0f);
    }

    // Ray march voxel boxes & sphere-trace rings from Origin to LightPt
    const uint32_t Steps = std::clamp(static_cast<uint32_t>(std::lround(MarchSteps * 0.5f)), 3u, 8u);
    for (uint32_t I = 1u; I <= Steps; ++I)
    {
        const float T = static_cast<float>(I) / static_cast<float>(Steps + 1u);
        const Point2D P { Origin.X + (LightPt.X - Origin.X) * T, Origin.Y + (LightPt.Y - Origin.Y) * T };
        const float Sz = 6.0f + T * 5.0f;
        if (PenumbraIdx >= 2u)
        {
            Surface.FillRectangle({ P.X - Sz * 0.5f, P.Y - Sz * 0.5f, Sz, Sz }, ColorRgba{ 88u, 172u, 134u, 28u }, 1.0f);
            Surface.StrokeRectangle({ P.X - Sz * 0.5f, P.Y - Sz * 0.5f, Sz, Sz }, ColorRgba{ 112u, 198u, 158u, 72u }, 1.0f, 1.0f);
        }
        Surface.FillCircle(P, 1.8f, ColorRgba{ 170u, 232u, 198u, 190u });
    }
    Surface.StrokeLine(Origin, LightPt, ColorRgba{ 118u, 206u, 164u, 120u }, 1.1f);
    Surface.StrokeCircle({ Origin.X + 22.0f, Origin.Y - 12.0f }, 26.0f, ColorRgba{ 255u, 255u, 255u, 18u }, 1.0f);

    // Blocker sphere on march ray
    Surface.FillCircle(Blocker, 16.0f, ColorRgba{ 70u, 72u, 78u, 210u });
    Surface.StrokeCircle(Blocker, 16.0f, ColorRgba{ 166u, 170u, 178u, 180u }, 1.1f);
    Surface.FillCircle(Origin, 3.0f, ColorRgba{ 238u, 242u, 248u, 255u });

    char Cap[64]{};
    std::snprintf(Cap, sizeof(Cap), "%u sphere-trace steps · hits the blocker", static_cast<unsigned>(std::lround(MarchSteps)));
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Bottom() - 16.0f }, 9.6f, ColorRgba{ 138u, 142u, 150u, 255u }, Cap);
}

// Technical study diagram well for Cone GI (1:1 HTML 02-cs-2-render-camera-sdf_gi.png)
void PaintConeGiStudyWell(PixelSpace& Surface, Rectangle2D Box, uint32_t ConeCount, float FillGain) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 22u, 22u, 24u, 255u }, 14.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 12u }, 14.0f, 1.0f);

    const float FloorY = Box.Bottom() - 22.0f;
    const Point2D Origin { Box.Center().X, FloorY };
    const float Radius = std::min(Box.Width * 0.28f, Box.Height - 44.0f);

    Surface.StrokeLine({ Box.X + 16.0f, FloorY }, { Box.Right() - 16.0f, FloorY }, ColorRgba{ 255u, 255u, 255u, 34u }, 1.0f);

    // Hemisphere arc outline
    const uint32_t ArcSegs = 28u;
    for (uint32_t I = 0u; I < ArcSegs; ++I)
    {
        const float A0 = -kPiLocal + (kPiLocal * static_cast<float>(I)) / static_cast<float>(ArcSegs);
        const float A1 = -kPiLocal + (kPiLocal * static_cast<float>(I + 1u)) / static_cast<float>(ArcSegs);
        Surface.StrokeLine(
            { Origin.X + std::cos(A0) * (Radius + 4.0f), Origin.Y + std::sin(A0) * (Radius + 4.0f) },
            { Origin.X + std::cos(A1) * (Radius + 4.0f), Origin.Y + std::sin(A1) * (Radius + 4.0f) },
            ColorRgba{ 255u, 255u, 255u, 22u }, 1.0f);
    }

    // Radial translucent green cone wedges fanning across hemisphere
    const uint32_t ClampedCones = std::clamp(ConeCount, 3u, 12u);
    const float StepAng = kPiLocal / static_cast<float>(ClampedCones);
    for (uint32_t C = 0u; C < ClampedCones; ++C)
    {
        const float MidAng = -kPiLocal + (static_cast<float>(C) + 0.5f) * StepAng;
        const float HalfW  = StepAng * 0.38f;
        const Point2D LeftTip  { Origin.X + std::cos(MidAng - HalfW) * Radius, Origin.Y + std::sin(MidAng - HalfW) * Radius };
        const Point2D RightTip { Origin.X + std::cos(MidAng + HalfW) * Radius, Origin.Y + std::sin(MidAng + HalfW) * Radius };
        const float Shade = 0.55f + 0.45f * std::sin(std::fabs(MidAng));
        const ColorRgba FillCol {
            static_cast<uint8_t>(56u * Shade),
            static_cast<uint8_t>(128u * Shade),
            static_cast<uint8_t>(92u * Shade),
            120u,
        };
        const std::array<Point2D, 3u> Wedge = {{ Origin, LeftTip, RightTip }};
        Surface.FillPolygon(Wedge, FillCol);
        Surface.StrokeLine(Origin, LeftTip, ColorRgba{ 118u, 206u, 158u, 110u }, 1.0f);
        Surface.StrokeLine(Origin, RightTip, ColorRgba{ 118u, 206u, 158u, 110u }, 1.0f);
        Surface.StrokeLine(LeftTip, RightTip, ColorRgba{ 118u, 206u, 158u, 130u }, 1.0f);
    }
    Surface.FillCircle(Origin, 3.2f, ColorRgba{ 242u, 246u, 250u, 255u });

    char TopCap[64]{}, FillCap[32]{};
    std::snprintf(TopCap, sizeof(TopCap), "%u cones through the distance field", ClampedCones);
    std::snprintf(FillCap, sizeof(FillCap), "fill × %.2f", FillGain);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Y + 9.0f }, 9.8f, ColorRgba{ 138u, 142u, 150u, 255u }, TopCap);
    const float FW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, FillCap);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.Right() - 12.0f - FW, Box.Bottom() - 15.0f }, 9.5f, ColorRgba{ 138u, 142u, 150u, 255u }, FillCap);
}

// Technical study diagram well for AO (1:1 HTML 02-cs-3-render-camera-sdf_ao.png)
void PaintAoStudyWell(PixelSpace& Surface, Rectangle2D Box, uint32_t Slices) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 22u, 22u, 24u, 255u }, 14.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 12u }, 14.0f, 1.0f);

    const float FloorY = Box.Bottom() - 24.0f;
    const float WallX  = Box.X + Box.Width * 0.70f;
    const Point2D Origin { Box.X + Box.Width * 0.60f, FloorY };
    const float Radius = 36.0f;

    // Occluder wall block on the right
    Surface.FillRectangle({ WallX, Box.Y + 14.0f, Box.Right() - 14.0f - WallX, FloorY - (Box.Y + 14.0f) }, ColorRgba{ 36u, 37u, 41u, 220u }, 2.0f);
    Surface.StrokeLine({ WallX, Box.Y + 14.0f }, { WallX, FloorY }, ColorRgba{ 166u, 170u, 178u, 160u }, 1.2f);
    Surface.StrokeLine({ Box.X + 14.0f, FloorY }, { Box.Right() - 14.0f, FloorY }, ColorRgba{ 255u, 255u, 255u, 34u }, 1.0f);

    // Warm amber fan cones darkening as they meet the wall
    const uint32_t ConeN = std::clamp(Slices, 2u, 8u);
    const float StepA = (kPiLocal * 0.85f) / static_cast<float>(ConeN);
    const float StartA = -kPiLocal * 0.92f;
    for (uint32_t C = 0u; C < ConeN; ++C)
    {
        const float A0 = StartA + static_cast<float>(C) * StepA + 0.04f;
        const float A1 = StartA + static_cast<float>(C + 1u) * StepA - 0.04f;
        const auto ClampedEnd = [&](float Ang) noexcept -> Point2D
        {
            const float Dx = std::cos(Ang);
            const float Dy = std::sin(Ang);
            float Len = Radius;
            if (Dx > 0.05f)
            {
                Len = std::min(Len, (WallX - 1.5f - Origin.X) / Dx);
            }
            return { Origin.X + Dx * Len, Origin.Y + Dy * Len };
        };
        const Point2D T0 = ClampedEnd(A0);
        const Point2D T1 = ClampedEnd(A1);
        const float Mid = 0.5f * (A0 + A1);
        const bool HitsWall = (std::cos(Mid) > 0.25f);
        const ColorRgba ConeCol = HitsWall ? ColorRgba{ 96u, 78u, 58u, 105u } : ColorRgba{ 148u, 118u, 84u, 130u };
        const std::array<Point2D, 3u> Cone = {{ Origin, T0, T1 }};
        Surface.FillPolygon(Cone, ConeCol);
        Surface.StrokeLine(Origin, T0, ColorRgba{ 196u, 160u, 118u, 130u }, 1.0f);
        Surface.StrokeLine(Origin, T1, ColorRgba{ 196u, 160u, 118u, 130u }, 1.0f);
        Surface.StrokeLine(T0, T1, ColorRgba{ 196u, 160u, 118u, 130u }, 1.0f);
    }
    Surface.FillCircle(Origin, 3.0f, ColorRgba{ 244u, 246u, 250u, 255u });

    char Cap[64]{};
    std::snprintf(Cap, sizeof(Cap), "%u slices · darker ones meet the wall", ConeN);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.X + 12.0f, Box.Bottom() - 16.0f }, 9.6f, ColorRgba{ 138u, 142u, 150u, 255u }, Cap);
}

// Technical study diagram well for Output scale (1:1 HTML 02-cs-4-render-camera-sdf_output.png)
void PaintOutputScaleWell(PixelSpace& Surface, Rectangle2D Box, float RenderScalePct) noexcept
{
    Surface.FillRectangle(Box, ColorRgba{ 22u, 22u, 24u, 255u }, 14.0f);
    Surface.StrokeRectangle(Box, ColorRgba{ 255u, 255u, 255u, 12u }, 14.0f, 1.0f);

    const float Ratio = std::clamp(RenderScalePct / 100.0f, 0.5f, 2.0f);
    const uint32_t ResW = static_cast<uint32_t>(std::lround(1920.0f * Ratio));
    const uint32_t ResH = static_cast<uint32_t>(std::lround(1080.0f * Ratio));

    const float BaseW = 136.0f;
    const float BaseH = 74.0f;
    const float ScaledW = std::clamp(BaseW * std::sqrt(Ratio), 84.0f, Box.Width - 36.0f);
    const float ScaledH = std::clamp(BaseH * std::sqrt(Ratio), 46.0f, Box.Height - 36.0f);
    const Rectangle2D Frame { Box.Center().X - ScaledW * 0.5f, Box.Y + 12.0f + (BaseH - ScaledH) * 0.35f, ScaledW, ScaledH };

    Surface.FillRectangle(Frame, ColorRgba{ 30u, 32u, 36u, 255u }, 3.0f);
    Surface.StrokeRectangle(Frame, ColorRgba{ 216u, 220u, 228u, 180u }, 3.0f, 1.1f);

    char MidBuf[32]{};
    std::snprintf(MidBuf, sizeof(MidBuf), "%u × %u", ResW, ResH);
    const float MW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, MidBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Frame.Center().X - MW * 0.5f, Frame.Center().Y - 5.0f }, 9.2f, ColorRgba{ 216u, 220u, 228u, 255u }, MidBuf);

    const char* Tag = (std::fabs(RenderScalePct - 100.0f) < 1.0f) ? "native" : ((RenderScalePct < 100.0f) ? "upscaled" : "supersampled");
    char FootBuf[64]{};
    std::snprintf(FootBuf, sizeof(FootBuf), "for 1920 × 1080 output · %s", Tag);
    const float FW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, FootBuf);
    WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Box.Center().X - FW * 0.5f, Box.Bottom() - 16.0f }, 9.6f, ColorRgba{ 138u, 142u, 150u, 255u }, FootBuf);
}

} // namespace

void CameraSettingsInspector::UpdateRendererHeroSurface(const CameraSettingsData& Data)
{
    uint64_t Sig = InspectorPrimitives::MixRevisionU64(0xCA5E771490000002ull, Data.RendererIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Data.MatcapChannelIndex);
    Sig = InspectorPrimitives::MixRevisionU64(Sig, Data.QualityTierIndex);

    InspectorGpuBakeSpec GpuSpec {
        InspectorGpuBakeMode::RendererHeroPreview, 0u,
        { static_cast<float>(Data.RendererIndex), static_cast<float>(Data.MatcapChannelIndex), static_cast<float>(Data.QualityTierIndex), 0.0f }
    };
    RendererHeroPreview.EnsureUpdatedGpu(Sig, 220u, 108u, GpuSpec, [&](std::vector<uint8_t>& Rgba)
    {
        const uint32_t W = 220u, H = 108u;
        const float Cx = W * 0.5f, Cy = H * 0.45f, Rad = 32.0f;
        const float Lx = -0.45f, Ly = -0.58f, Lz = 0.68f;
        const float LLen = std::sqrt(Lx * Lx + Ly * Ly + Lz * Lz);
        const float NxL = Lx / LLen, NyL = Ly / LLen, NzL = Lz / LLen;

        for (uint32_t Y = 0u; Y < H; ++Y)
        {
            for (uint32_t X = 0u; X < W; ++X)
            {
                const float Fx = static_cast<float>(X) + 0.5f;
                const float Fy = static_cast<float>(Y) + 0.5f;
                // Deep dark stage matching HTML #15171c radial glow
                const float Dbg = std::hypot((Fx - Cx) / 96.0f, (Fy - Cy) / 62.0f);
                const float Vign = std::clamp(1.0f - Dbg * 0.55f, 0.0f, 1.0f);
                float R = 0.082f + 0.032f * Vign;
                float G = 0.086f + 0.034f * Vign;
                float B = 0.104f + 0.042f * Vign;

                // Subtle ground horizon & shadow
                const float FloorY = Cy + Rad + 4.0f;
                if (std::fabs(Fy - FloorY) < 0.7f && std::fabs(Fx - Cx) < 62.0f)
                {
                    R = 0.24f; G = 0.25f; B = 0.28f;
                }
                if (Fy > FloorY - 4.0f && Fy < FloorY + 12.0f)
                {
                    const float Sdx = (Fx - Cx) / 34.0f;
                    const float Sdy = (Fy - FloorY - 1.5f) / 6.0f;
                    const float Sd2 = Sdx * Sdx + Sdy * Sdy;
                    if (Sd2 < 1.0f)
                    {
                        const float Occ = (1.0f - Sd2) * 0.52f;
                        R *= (1.0f - Occ); G *= (1.0f - Occ); B *= (1.0f - Occ);
                    }
                }

                // Concentric mint SDF distance rings on floor for B · SDF (1:1 HTML)
                if (Data.RendererIndex == 1u)
                {
                    const float Ex = (Fx - (Cx + 10.0f)) / 42.0f;
                    const float Ey = (Fy - (FloorY + 3.0f)) / 9.5f;
                    const float Ed = std::sqrt(Ex * Ex + Ey * Ey);
                    if (std::fabs(Ed - 0.92f) < 0.055f || std::fabs(Ed - 0.58f) < 0.055f)
                    {
                        R = std::min(1.0f, R + 0.16f);
                        G = std::min(1.0f, G + 0.44f);
                        B = std::min(1.0f, B + 0.30f);
                    }
                }

                // Dotted diagonal rays for C · Ray traced (1:1 HTML)
                if (Data.RendererIndex == 2u)
                {
                    const float Dx0 = Fx - Cx, Dy0 = Fy - Cy;
                    const float Dist = std::hypot(Dx0, Dy0);
                    if (Dist > Rad + 4.0f && Dist < Rad + 36.0f)
                    {
                        const float Ang = std::atan2(Dy0, Dx0);
                        const float Dash = std::fmod(Dist, 7.5f);
                        if (Dash < 3.6f && (std::fabs(Ang + 2.42f) < 0.045f || std::fabs(Ang + 0.72f) < 0.045f || std::fabs(Ang - 0.62f) < 0.045f))
                        {
                            R = 0.66f; G = 0.56f; B = 0.92f;
                        }
                    }
                }

                // 3D sphere shading
                const float Dx = (Fx - Cx) / Rad;
                const float Dy = (Fy - Cy) / Rad;
                const float R2 = Dx * Dx + Dy * Dy;
                if (R2 <= 1.0f)
                {
                    const float Dz = std::sqrt(std::max(0.0f, 1.0f - R2));
                    const float NdotL = std::max(0.0f, Dx * NxL + Dy * NyL + Dz * NzL);
                    const float Rim   = std::pow(1.0f - Dz, 2.4f);
                    const float Hx = NxL, Hy = NyL, Hz = NzL + 1.0f;
                    const float HLen = std::sqrt(Hx * Hx + Hy * Hy + Hz * Hz);
                    const float Spec = std::pow(std::max(0.0f, (Dx * Hx + Dy * Hy + Dz * Hz) / HLen), Data.RendererIndex == 3u ? 18.0f : 40.0f);

                    if (Data.RendererIndex == 0u) // A · PBR sky-blue studio sphere
                    {
                        R = 0.14f + 0.44f * NdotL + 0.36f * Spec + 0.14f * Rim;
                        G = 0.22f + 0.52f * NdotL + 0.38f * Spec + 0.18f * Rim;
                        B = 0.34f + 0.60f * NdotL + 0.40f * Spec + 0.24f * Rim;
                    }
                    else if (Data.RendererIndex == 1u) // B · SDF mint-green sphere
                    {
                        R = 0.14f + 0.42f * NdotL + 0.34f * Spec + 0.12f * Rim;
                        G = 0.28f + 0.58f * NdotL + 0.36f * Spec + 0.20f * Rim;
                        B = 0.22f + 0.46f * NdotL + 0.34f * Spec + 0.16f * Rim;
                    }
                    else if (Data.RendererIndex == 2u) // C · Ray traced lavender sphere
                    {
                        R = 0.22f + 0.46f * NdotL + 0.38f * Spec + 0.18f * Rim;
                        G = 0.18f + 0.42f * NdotL + 0.36f * Spec + 0.14f * Rim;
                        B = 0.34f + 0.58f * NdotL + 0.42f * Spec + 0.24f * Rim;
                    }
                    else // D · MatCap warm clay
                    {
                        R = 0.28f + 0.56f * NdotL + 0.18f * Spec + 0.10f * Rim;
                        G = 0.24f + 0.50f * NdotL + 0.16f * Spec + 0.09f * Rim;
                        B = 0.20f + 0.44f * NdotL + 0.14f * Spec + 0.08f * Rim;
                    }
                }

                const std::size_t O = (static_cast<std::size_t>(Y) * W + X) * 4u;
                Rgba[O + 0u] = static_cast<uint8_t>(std::clamp(R, 0.0f, 1.0f) * 255.0f);
                Rgba[O + 1u] = static_cast<uint8_t>(std::clamp(G, 0.0f, 1.0f) * 255.0f);
                Rgba[O + 2u] = static_cast<uint8_t>(std::clamp(B, 0.0f, 1.0f) * 255.0f);
                Rgba[O + 3u] = 255u;
            }
        }
    });
}

float CameraSettingsInspector::Construct(
    PixelSpace& Surface, float LeftX, float TopY, float InnerWidth,
    OutlinerRecordEntry& Entry, CameraSettingsData& Data,
    ViewportDisplaySettings* DisplaySettings,
    const PaneInputReading& Input, bool& OutMutated, bool& OutPageSwitched)
{
    const float PadX  = std::clamp(InnerWidth * 0.065f, 14.0f, 22.0f);
    const float BodyW = std::max(100.0f, InnerWidth - PadX * 2.0f);
    const Point2D Cur { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool Clicked = Input.PrimaryPointer.Pressed;

    // 1:1 HTML Editor Camera component banner right above the page carousel
    float CursorY = TopY;
    {
        const Rectangle2D Banner { LeftX, CursorY, InnerWidth, 62.0f };
        const bool Hov = Banner.Contains(Cur);
        Surface.FillRectangle(Banner, Hov ? ColorRgba{ 26u, 27u, 31u, 255u } : ColorRgba{ 20u, 21u, 24u, 255u }, InspectorMetrics::SectionRadius);
        Surface.StrokeRectangle(Banner, ColorRgba{ 255u, 255u, 255u, 18u }, InspectorMetrics::SectionRadius, 1.0f);
        InspectorPrimitives::ConstructSwitchSymbol(
            Surface, InspectorSwitchSymbol::CameraLens, { Banner.X + 24.0f, Banner.Center().Y }, 18.0f,
            ColorRgba{ 166u, 170u, 178u, 255u }, ColorRgba{ 20u, 21u, 24u, 255u });
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Banner.X + 46.0f, Banner.Y + 11.0f }, 12.2f, InspectorMetrics::HeadingInk, "Editor Camera");
        const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 12.2f, "Editor Camera");
        Surface.StrokeLine({ Banner.X + 46.0f, Banner.Y + 25.5f }, { Banner.X + 46.0f + TitleW, Banner.Y + 25.5f }, ColorRgba{ 220u, 224u, 232u, 150u }, 1.0f);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Banner.X + 46.0f, Banner.Y + 29.0f }, InnerWidth - 78.0f, 9.6f, InspectorMetrics::DimInk,
            "Component of this camera · values are stored on it");
        const float ChevX = Banner.Right() - 16.0f, ChevY = Banner.Center().Y;
        Surface.StrokeLine({ ChevX - 3.0f, ChevY - 4.0f }, { ChevX + 1.5f, ChevY }, ColorRgba{ 140u, 144u, 152u, 255u }, 1.3f);
        Surface.StrokeLine({ ChevX + 1.5f, ChevY }, { ChevX - 3.0f, ChevY + 4.0f }, ColorRgba{ 140u, 144u, 152u, 255u }, 1.3f);
        CursorY = Banner.Bottom() + 14.0f;
    }

    // Per-engine page strips matching HTML RenderComponents.jsx 1:1
    static constexpr std::array<const char*, 5u> PbrPages    = {{ "Overview", "Shadows", "Reflections", "AO", "Output" }};
    static constexpr std::array<const char*, 5u> SdfPages    = {{ "Overview", "Shadows", "GI", "AO", "Output" }};
    static constexpr std::array<const char*, 7u> RayPages    = {{ "Overview", "Tracer", "GI", "Shadows", "AO", "Reflections", "Output" }};
    static constexpr std::array<const char*, 3u> MatCapPages = {{ "Overview", "Studio", "Output" }};

    std::span<const char* const> ActivePages = PbrPages;
    if (Data.RendererIndex == 1u)      ActivePages = SdfPages;
    else if (Data.RendererIndex == 2u) ActivePages = RayPages;
    else if (Data.RendererIndex == 3u) ActivePages = MatCapPages;

    if (ActivePage >= static_cast<uint32_t>(ActivePages.size())) ActivePage = 0u;
    CursorY = InspectorPrimitives::ConstructPageStrip(Surface, LeftX, CursorY, InnerWidth, ActivePages, ActivePage, Input, OutPageSwitched);

    const auto SyncLiveNow = [&]() noexcept
    {
        InspectorInstanceRegister::SyncCameraSettingsToDisplay(Data, DisplaySettings);
    };

    const auto ConstructXeGtaoSection = [&](float Top) -> float
    {
        const float SectionH = 632.0f;
        const Rectangle2D Sec { LeftX, Top, InnerWidth, SectionH };
        InspectorPrimitives::ConstructSectionSurface(Surface, Sec, "Ambient occlusion");
        InspectorPrimitives::ConstructTooltipTrigger(
            Surface, { Sec.Right() - PadX - 18.0f, Sec.Y + InspectorMetrics::SectionPadTop },
            "XeGTAO (.slang)",
            "Horizon-based cosine visibility integral in WorldViewportRadiometricCompute.slang. Drives slice count, horizon steps per side, search radius, and strength.",
            Input);

        float Y = Top + InspectorMetrics::SectionPadTop + 28.0f;
        char KpiBuf[16]{}, SubBuf[88]{};
        std::snprintf(KpiBuf, sizeof(KpiBuf), "%.0f", Data.XeGtaoSliceCount);
        std::snprintf(SubBuf, sizeof(SubBuf), "Horizon-based visibility in .slang · Radius %.2f m", Data.XeGtaoSearchRadiusM);
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, KpiBuf, "slices", SubBuf);

        // 1:1 HTML AO cone-fan + wall technical diagram
        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 106.0f };
        PaintAoStudyWell(Surface, Well, static_cast<uint32_t>(std::lround(Data.XeGtaoSliceCount)));
        Y = Well.Bottom() + 14.0f;

        bool Local = false;
        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Screen-space XeGTAO", Data.XeGtaoEnabled, Input, Local);

        // 2-column AO method tiles (1:1 HTML .rc-tiles)
        const float TileW = std::floor((BodyW - 6.0f) * 0.5f);
        struct AoTileDesc { const char* Title; const char* Sub; uint32_t Slices; uint32_t Steps; };
        static constexpr std::array<AoTileDesc, 2u> AoModes = {{
            { "XeGTAO Horizon", "Cosine visibility integral per slice", 4u, 4u },
            { "Fine Multi-Slice", "High-precision crevice occlusion", 6u, 6u },
        }};
        for (uint32_t I = 0u; I < 2u; ++I)
        {
            const Rectangle2D T { LeftX + PadX + static_cast<float>(I) * (TileW + 6.0f), Y, TileW, 64.0f };
            const bool Sel = (I == 0u ? (Data.XeGtaoSliceCount <= 4.5f) : (Data.XeGtaoSliceCount > 4.5f));
            const bool Hov = T.Contains(Cur);
            Surface.FillRectangle(T, Sel ? ColorRgba{ 44u, 44u, 46u, 255u } : (Hov ? ColorRgba{ 30u, 31u, 34u, 255u } : ColorRgba{ 22u, 22u, 24u, 255u }), 12.0f);
            Surface.StrokeRectangle(T, Sel ? ColorRgba{ 255u, 255u, 255u, 58u } : ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Medium, { T.X + 10.0f, T.Y + 10.0f }, T.Width - 18.0f, 10.6f, Sel ? ColorRgba{ 242u, 242u, 242u, 255u } : ColorRgba{ 206u, 206u, 206u, 255u }, AoModes[I].Title);
            WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { T.X + 10.0f, T.Y + 28.0f }, T.Width - 18.0f, 9.2f, ColorRgba{ 122u, 122u, 126u, 255u }, AoModes[I].Sub);
            if (Hov && Clicked)
            {
                Data.XeGtaoSliceCount   = static_cast<float>(AoModes[I].Slices);
                Data.XeGtaoStepsPerSide = static_cast<float>(AoModes[I].Steps);
                Data.XeGtaoEnabled      = true;
                Local = true;
            }
        }
        Y += 76.0f;

        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8720u, "AO strength",    "x", "%.2f", 0.0f,  2.5f, Data.XeGtaoStrength,     ActiveDragId, Input, Local, !Data.XeGtaoEnabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8721u, "AO slices",      "",  "%.0f", 2.0f,  8.0f, Data.XeGtaoSliceCount,   ActiveDragId, Input, Local, !Data.XeGtaoEnabled);
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8722u, "AO steps / side","",  "%.0f", 2.0f,  8.0f, Data.XeGtaoStepsPerSide, ActiveDragId, Input, Local, !Data.XeGtaoEnabled);
        (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8723u, "AO radius",    "m", "%.2f", 0.25f, 3.0f, Data.XeGtaoSearchRadiusM,ActiveDragId, Input, Local, !Data.XeGtaoEnabled);
        if (Local) { OutMutated = true; SyncLiveNow(); }
        return Sec.Bottom() + InspectorMetrics::SectionGap;
    };

    const auto ConstructOutputSection = [&](float Top) -> float
    {
        const float ScalePct = DisplaySettings ? DisplaySettings->RenderScalePercent : 100.0f;
        const Rectangle2D OutSec { LeftX, Top, InnerWidth, 612.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, OutSec, "Output");
        float Y = Top + InspectorMetrics::SectionPadTop + 34.0f;
        char ScaleBuf[16]{}, SubBuf[88]{};
        std::snprintf(ScaleBuf, sizeof(ScaleBuf), "%.0f", ScalePct);
        std::snprintf(SubBuf, sizeof(SubBuf), "Exposure gain %.2f × · edge AA %s", Data.ExposureGain, Data.SpatialAntiAliasing ? "on" : "off");
        Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, ScaleBuf, "% scale", SubBuf);
        Y += 8.0f;

        const Rectangle2D Well { LeftX + PadX, Y, BodyW, 126.0f };
        PaintOutputScaleWell(Surface, Well, ScalePct);
        Y = Well.Bottom() + 20.0f;

        bool Local = false;
        Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8740u, "Exposure gain", "x", "%.2f", 0.10f, 4.0f, Data.ExposureGain, ActiveDragId, Input, Local);
        Y += 6.0f;

        // 6-step scale pill track (1:1 HTML 50% .. 200%)
        static constexpr std::array<const char*, 6u> ScaleLabels = {{ "50%", "75%", "100%", "125%", "150%", "200%" }};
        static constexpr std::array<float, 6u>       ScaleVals   = {{ 50.0f, 75.0f, 100.0f, 125.0f, 150.0f, 200.0f }};
        uint32_t ActiveScaleIdx = 2u;
        for (uint32_t I = 0u; I < 6u; ++I)
        {
            if (std::fabs(ScalePct - ScaleVals[I]) < 8.0f) { ActiveScaleIdx = I; break; }
        }
        int32_t PickScale = -1;
        Y = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "", ScaleLabels, ActiveScaleIdx, Input, PickScale);
        if (PickScale >= 0 && PickScale < 6)
        {
            Data.RenderScalePercent = ScaleVals[static_cast<std::size_t>(PickScale)];
            if (DisplaySettings) DisplaySettings->RenderScalePercent = Data.RenderScalePercent;
            Local = true;
        }
        Y += 6.0f;

        Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Edge anti-aliasing", Data.SpatialAntiAliasing, Input, Local);
        Y += 6.0f;

        static constexpr std::array<const char*, 2u> ReflSrc = {{ "WideTree", "Sky" }};
        int32_t PickRefl = -1;
        Y = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "REFLECTION SOURCE", ReflSrc, std::min(Data.ReflectionSourceIndex, 1u), Input, PickRefl);
        if (PickRefl >= 0) { Data.ReflectionSourceIndex = static_cast<uint32_t>(PickRefl); Local = true; }
        Y += 6.0f;

        static constexpr std::array<const char*, 3u> TraceRes = {{ "Full", "½", "¼" }};
        int32_t PickRes = -1;
        (void)InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "TRACE RESOLUTION", TraceRes, std::min(Data.TraceResolutionIndex, 2u), Input, PickRes);
        if (PickRes >= 0) { Data.TraceResolutionIndex = static_cast<uint32_t>(PickRes); Local = true; }

        float NextY = OutSec.Bottom() + InspectorMetrics::SectionGap;

        // Cluster-LOD & Debug channel card
        const Rectangle2D LodSec { LeftX, NextY, InnerWidth, 296.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, LodSec, "Cluster-LOD & debug");
        float LY = NextY + InspectorMetrics::SectionPadTop + 28.0f;
        LY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, LY, BodyW, "Nanite cluster-LOD", Data.ClusterLodEnabled, Input, Local);
        LY = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, LY, BodyW, 8741u, "LOD pixel error", "px", "%.2f", 0.25f, 4.0f, Data.ClusterLodPixelError, ActiveDragId, Input, Local, !Data.ClusterLodEnabled);
        static constexpr std::array<const char*, 5u> ForcedLodLabels = {{ "Auto", "L0", "L1", "L2", "L3" }};
        const uint32_t ForcedLodIdx = static_cast<uint32_t>(std::clamp(Data.ClusterForcedLodLevel + 1, 0, 4));
        int32_t PickLod = -1;
        LY = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, LY, BodyW, "FORCED LOD LEVEL", ForcedLodLabels, ForcedLodIdx, Input, PickLod);
        if (PickLod >= 0 && PickLod <= 4) { Data.ClusterForcedLodLevel = PickLod - 1; Local = true; }
        LY = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, LY, BodyW, "Cluster wireframe", Data.ClusterWireframeOverlay, Input, Local);
        static constexpr std::array<const char*, 4u> DbgCh = {{ "Final", "Albedo", "Normal", "Shadows" }};
        static constexpr std::array<uint32_t, 4u>    DbgId = {{ 0u, 1u, 2u, 6u }};
        uint32_t DbgIdx = 0u;
        for (uint32_t I = 0u; I < 4u; ++I) if (Data.DebugChannelIndex == DbgId[I]) DbgIdx = I;
        int32_t PickDbg = -1;
        (void)InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, LY, BodyW, "", DbgCh, DbgIdx, Input, PickDbg);
        if (PickDbg >= 0 && PickDbg < 4) { Data.DebugChannelIndex = DbgId[static_cast<std::size_t>(PickDbg)]; Local = true; }

        if (Local) { OutMutated = true; SyncLiveNow(); }
        return LodSec.Bottom() + InspectorMetrics::SectionGap;
    };

    if (ActivePage == 0u)
    {
        UpdateRendererHeroSurface(Data);

        // 1:1 HTML Render type hero card (01-camsettings-overview-top.png & 03-cs-ray-overview.png)
        static constexpr std::array<const char*, 4u> HeroTitles = {{
            "A · PBR", "B · SDF", "C · Ray traced", "D · MatCap"
        }};
        static constexpr std::array<const char*, 4u> HeroSubtitles = {{
            "Direct OpenPBR shading with SMRT shadows",
            "Distance-field shadows with real-time cone GI",
            "BVH or path-traced light, shadows and reflections",
            "Material capture clay & studio surface inspection"
        }};
        static constexpr std::array<const char*, 4u> HeroCaptions = {{
            "OpenPBR direct + LTC area lights + shadow lab + XeGTAO.",
            "Hybrid cone penumbra with multi-bounce cone GI.",
            "BVH4 two-bounce GI or progressive path tracer.",
            "Single-pass spherical MatCap lookup without lights."
        }};
        const uint32_t EIdx = std::min(Data.RendererIndex, 3u);

        // 1:1 HTML Render type hero card height = 409px
        const Rectangle2D HeroSec { LeftX, CursorY, InnerWidth, 409.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, HeroSec, "Render type");
        float Y = CursorY + InspectorMetrics::SectionPadTop + 34.0f;

        // Dark glossy hero stage box containing sphere + bold title + subtitle + capability tag chips
        const Rectangle2D Stage { LeftX + PadX, Y, BodyW, 210.0f };
        Surface.FillRectangle(Stage, ColorRgba{ 21u, 23u, 28u, 255u }, InspectorMetrics::QuickTileRadius);
        Surface.StrokeRectangle(Stage, ColorRgba{ 255u, 255u, 255u, 14u }, InspectorMetrics::QuickTileRadius, 1.0f);
        RendererHeroPreview.Present(
            Surface,
            { Stage.X + 8.0f, Stage.Y + 8.0f, Stage.Width - 16.0f, 118.0f },
            InspectorMetrics::PreviewRadius,
            { 255u, 255u, 255u, 255u },
            InspectorPreviewFitMode::Contain,
            ColorRgba{ 0u, 0u, 0u, 0u });

        const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 13.5f, HeroTitles[EIdx]);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { Stage.Center().X - TitleW * 0.5f, Stage.Y + 134.0f }, 13.5f, ColorRgba{ 242u, 244u, 248u, 255u }, HeroTitles[EIdx]);

        const float SubW = std::min(Stage.Width - 16.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.6f, HeroSubtitles[EIdx]));
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Stage.Center().X - SubW * 0.5f, Stage.Y + 154.0f }, Stage.Width - 16.0f, 9.6f, ColorRgba{ 146u, 150u, 158u, 255u }, HeroSubtitles[EIdx]);

        // Centered capability chips inside hero stage ([Shadows] [GI] [AO])
        static constexpr std::array<const char*, 3u> PbrChips = {{ "Shadows", "Reflections", "AO" }};
        static constexpr std::array<const char*, 3u> SdfChips = {{ "Shadows", "GI", "AO" }};
        static constexpr std::array<const char*, 5u> RayChips = {{ "Tracer", "GI", "Shadows", "AO", "Refl" }};
        static constexpr std::array<const char*, 2u> McChips  = {{ "Studio", "Output" }};
        std::span<const char* const> Chips = PbrChips;
        if (EIdx == 1u) Chips = SdfChips;
        else if (EIdx == 2u) Chips = RayChips;
        else if (EIdx == 3u) Chips = McChips;

        float TotalChipW = 0.0f;
        for (std::size_t I = 0u; I < Chips.size(); ++I)
        {
            TotalChipW += WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, Chips[I]) + 14.0f;
            if (I + 1u < Chips.size()) TotalChipW += 5.0f;
        }
        float ChipX = Stage.Center().X - TotalChipW * 0.5f;
        const float ChipY = Stage.Y + 176.0f;
        for (const char* CLabel : Chips)
        {
            const float CW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.0f, CLabel) + 14.0f;
            const Rectangle2D CR { ChipX, ChipY, CW, 18.0f };
            Surface.FillRectangle(CR, ColorRgba{ 38u, 40u, 46u, 255u }, 9.0f);
            Surface.StrokeRectangle(CR, ColorRgba{ 255u, 255u, 255u, 16u }, 9.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { CR.X + 7.0f, CR.Y + 4.0f }, 9.0f, ColorRgba{ 196u, 200u, 208u, 255u }, CLabel);
            ChipX += CW + 5.0f;
        }
        Y = Stage.Bottom() + 12.0f;

        // 4-column visual thumbnail selector tiles (1:1 HTML A PBR | B SDF | C Ray traced | D MatCap)
        static constexpr std::array<const char*, 4u> TileLetters = {{ "A", "B", "C", "D" }};
        static constexpr std::array<const char*, 4u> TileNames   = {{ "PBR", "SDF", "Ray traced", "MatCap" }};
        const float TileGap = 6.0f;
        const float TileW   = std::floor((BodyW - TileGap * 3.0f) / 4.0f);
        const float TileH   = 74.0f;
        for (uint32_t I = 0u; I < 4u; ++I)
        {
            const Rectangle2D T { LeftX + PadX + static_cast<float>(I) * (TileW + TileGap), Y, TileW, TileH };
            const bool Sel = (EIdx == I);
            const bool Hov = T.Contains(Cur);
            Surface.FillRectangle(T, Sel ? ColorRgba{ 36u, 38u, 43u, 255u } : (Hov ? ColorRgba{ 28u, 30u, 34u, 255u } : ColorRgba{ 22u, 23u, 26u, 255u }), 11.0f);
            Surface.StrokeRectangle(T, Sel ? ColorRgba{ 235u, 238u, 244u, 185u } : ColorRgba{ 255u, 255u, 255u, 18u }, 11.0f, Sel ? 1.2f : 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { T.X + 6.0f, T.Y + 5.0f }, 9.5f,
                Sel ? ColorRgba{ 239u, 68u, 68u, 255u } : ColorRgba{ 118u, 122u, 130u, 255u }, TileLetters[I]);
            PaintMiniEngineSphere(Surface, { T.Center().X, T.Y + 32.0f }, 10.5f, I);
            const float NW = std::min(T.Width - 6.0f, WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.2f, TileNames[I]));
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { T.Center().X - NW * 0.5f, T.Bottom() - 16.0f }, T.Width - 6.0f, 9.2f,
                Sel ? ColorRgba{ 238u, 240u, 244u, 255u } : ColorRgba{ 148u, 152u, 160u, 255u }, TileNames[I]);
            if (Hov && Clicked && Data.RendererIndex != I)
            {
                Data.RendererIndex = I;
                ActivePage         = 0u;
                OutMutated         = true;
                OutPageSwitched    = true;
                SyncLiveNow();
            }
        }
        Y += TileH + 10.0f;
        WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { LeftX + PadX, Y }, BodyW, 9.6f, ColorRgba{ 138u, 142u, 150u, 255u }, HeroCaptions[EIdx]);
        CursorY = HeroSec.Bottom() + InspectorMetrics::SectionGap;

        // 1:1 HTML Active engine summary navigation card ("Each line opens its page")
        static constexpr std::array<const char*, 4u> SumHeading = {{ "OpenPBR Direct", "SDF tracing", "Ray tracing", "MatCap shading" }};
        static constexpr std::array<const char*, 4u> SumBig     = {{ "PBR", "SDF", "Ray traced", "MatCap" }};
        static constexpr std::array<const char*, 4u> SumTag     = {{ "Direct", "GTX GI", "BVH4 GI", "Clay" }};
        const float ScalePct = DisplaySettings ? DisplaySettings->RenderScalePercent : 100.0f;
        char ShadowLine[48]{}, GiLine[48]{}, AoLine[48]{}, SdfAoLine[48]{}, OutLine[48]{}, TracerLine[48]{};
        std::snprintf(ShadowLine, sizeof(ShadowLine), "SMRT · %.0f rays · %.0f steps", Data.SmrtShadowRays, Data.SmrtMarchSteps);
        std::snprintf(GiLine,     sizeof(GiLine),     "Cone GI · %.0f cones", Data.SdfConeSteps);
        std::snprintf(AoLine,     sizeof(AoLine),     "XeGTAO · %.0f slices", Data.XeGtaoSliceCount);
        std::snprintf(SdfAoLine,  sizeof(SdfAoLine),  "SDF AO · %.2f m · × %.2f", Data.SdfAoRadiusM, Data.SdfAoContrast);
        std::snprintf(OutLine,    sizeof(OutLine),    "%.0f%% · exposure × %.2f", ScalePct, Data.ExposureGain);
        std::snprintf(TracerLine, sizeof(TracerLine), "%s · %.0f spp", (Data.RayTracerIndex == 0u ? "BVH4 GI" : (Data.RayTracerIndex == 1u ? "PT Realtime" : "PT Final")), Data.PathTraceBatchSpp);

        const uint32_t NavRows = (EIdx == 3u) ? 2u : 4u;
        const Rectangle2D SumSec { LeftX, CursorY, InnerWidth, 128.0f + static_cast<float>(NavRows) * 30.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, SumSec, SumHeading[EIdx]);
        float SY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        SY = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, SY, SumBig[EIdx], SumTag[EIdx], "Each line opens its page");

        bool NavClick = false;
        if (EIdx == 0u)
        {
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Shadows", ShadowLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 1u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Reflections", Data.PbrReflectionsEnabled ? "WideTree · specular" : "Off", true, Input, NavClick);
            if (NavClick) { ActivePage = 2u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "AO", AoLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 3u; OutPageSwitched = true; }
            (void)InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Output", OutLine, false, Input, NavClick);
            if (NavClick) { ActivePage = 4u; OutPageSwitched = true; }
        }
        else if (EIdx == 1u)
        {
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Shadows", ShadowLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 1u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "GI", GiLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 2u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "AO", SdfAoLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 3u; OutPageSwitched = true; }
            (void)InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Output", OutLine, false, Input, NavClick);
            if (NavClick) { ActivePage = 4u; OutPageSwitched = true; }
        }
        else if (EIdx == 2u)
        {
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Tracer", TracerLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 1u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "GI", GiLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 2u; OutPageSwitched = true; }
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "AO", AoLine, true, Input, NavClick);
            if (NavClick) { ActivePage = 4u; OutPageSwitched = true; }
            (void)InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Output", OutLine, false, Input, NavClick);
            if (NavClick) { ActivePage = 6u; OutPageSwitched = true; }
        }
        else
        {
            SY = InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Studio", "Clay · spherical normal", true, Input, NavClick);
            if (NavClick) { ActivePage = 1u; OutPageSwitched = true; }
            (void)InspectorPrimitives::ConstructNavigationRow(Surface, LeftX + PadX, SY, BodyW, "Output", OutLine, false, Input, NavClick);
            if (NavClick) { ActivePage = 2u; OutPageSwitched = true; }
        }
        CursorY = SumSec.Bottom() + InspectorMetrics::SectionGap;

        // 1:1 HTML Quality budget card (01-camsettings-overview-mid.png)
        const uint32_t ShadowBudget = static_cast<uint32_t>(std::lround(Data.SmrtShadowRays * 4.0f));
        const uint32_t GiBudget     = (EIdx == 0u || EIdx == 3u) ? 0u : static_cast<uint32_t>(std::lround(Data.SdfConeSteps));
        const uint32_t ReflBudget   = (Data.RayReflectionsEnabled && EIdx == 2u) ? static_cast<uint32_t>(std::lround(Data.PathTraceBatchSpp * 2.0f)) : 0u;
        const uint32_t TotalBudget  = std::max(1u, ShadowBudget + GiBudget + ReflBudget);

        static constexpr std::array<const char*, 5u> TierNames = {{ "Low", "Medium", "High", "Ultra", "Ref" }};
        const uint32_t TierIdx = std::min(Data.QualityTierIndex, 4u);

        const Rectangle2D QualSec { LeftX, CursorY, InnerWidth, 272.0f };
        InspectorPrimitives::ConstructSectionSurface(Surface, QualSec, "Quality");
        float QY = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
        char TotBuf[16]{}, TierSub[48]{};
        std::snprintf(TotBuf, sizeof(TotBuf), "%u", TotalBudget);
        std::snprintf(TierSub, sizeof(TierSub), "%s on every page", TierNames[TierIdx]);
        QY = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, QY, TotBuf, "samples / pixel", TierSub);

        // Dark budget well with proportional 3-color bar + color-dot legend
        const Rectangle2D Well { LeftX + PadX, QY, BodyW, 56.0f };
        Surface.FillRectangle(Well, ColorRgba{ 18u, 19u, 22u, 255u }, 12.0f);
        Surface.StrokeRectangle(Well, ColorRgba{ 255u, 255u, 255u, 12u }, 12.0f, 1.0f);

        const float BarPad = 12.0f;
        const float BarW   = Well.Width - BarPad * 2.0f;
        const float BarY   = Well.Y + 13.0f;
        const float BarH   = 11.0f;
        const float ShW    = BarW * (static_cast<float>(ShadowBudget) / static_cast<float>(TotalBudget));
        const float GiW    = BarW * (static_cast<float>(GiBudget)     / static_cast<float>(TotalBudget));
        const float RfW    = std::max(0.0f, BarW - ShW - GiW);
        float SegX = Well.X + BarPad;
        if (ShW > 2.0f) { Surface.FillRectangle({ SegX, BarY, ShW - 1.5f, BarH }, ColorRgba{ 96u, 154u, 224u, 255u }, 5.0f); SegX += ShW; }
        if (GiW > 2.0f) { Surface.FillRectangle({ SegX, BarY, GiW - 1.5f, BarH }, ColorRgba{ 92u, 186u, 134u, 255u }, 5.0f); SegX += GiW; }
        if (RfW > 2.0f) { Surface.FillRectangle({ SegX, BarY, RfW, BarH },        ColorRgba{ 152u, 126u, 226u, 255u }, 5.0f); }

        const float LegY = Well.Y + 34.0f;
        char ShLeg[24]{}, GiLeg[24]{}, RfLeg[24]{};
        std::snprintf(ShLeg, sizeof(ShLeg), "Shadows %u", ShadowBudget);
        std::snprintf(GiLeg, sizeof(GiLeg), "GI %u", GiBudget);
        std::snprintf(RfLeg, sizeof(RfLeg), "Refl %u", ReflBudget);
        Surface.FillCircle({ Well.X + 14.0f, LegY + 5.0f }, 3.2f, ColorRgba{ 96u, 154u, 224u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 21.0f, LegY }, 9.2f, ColorRgba{ 138u, 142u, 150u, 255u }, ShLeg);
        Surface.FillCircle({ Well.X + 104.0f, LegY + 5.0f }, 3.2f, ColorRgba{ 92u, 186u, 134u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 111.0f, LegY }, 9.2f, ColorRgba{ 138u, 142u, 150u, 255u }, GiLeg);
        Surface.FillCircle({ Well.X + 174.0f, LegY + 5.0f }, 3.2f, ColorRgba{ 152u, 126u, 226u, 255u });
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Well.X + 181.0f, LegY }, 9.2f, ColorRgba{ 138u, 142u, 150u, 255u }, RfLeg);
        QY = Well.Bottom() + 12.0f;

        // 1:1 HTML .rc-segments pill track for Quality tiers
        int32_t PickTier = -1;
        QY = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, QY, BodyW, "", TierNames, TierIdx, Input, PickTier);
        if (PickTier >= 0 && PickTier < 5)
        {
            ApplyMasterQualityTier(Data, static_cast<uint32_t>(PickTier));
            OutMutated = true;
            SyncLiveNow();
        }
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { LeftX + PadX, QY }, BodyW, 9.2f, ColorRgba{ 132u, 136u, 144u, 255u },
            "A tier sets shadow, GI & AO counts; each page can override.");
        CursorY = QualSec.Bottom() + InspectorMetrics::SectionGap;
    }
    else if (Data.RendererIndex == 0u) // A · PBR sub-pages
    {
        if (ActivePage == 1u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 636.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Direct shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            static constexpr std::array<const char*, 4u> AlgTitles = {{ "Contact PCSS", "Plane PCSS", "SMRT Raymarch", "PCF 3×3" }};
            static constexpr std::array<const char*, 4u> AlgSubs   = {{
                "Screen-space blocker search with contact hardening",
                "Planar receiver-space percentage-closer soft shadows",
                "Multi-ray shadow march controlled directly in .slang",
                "Fast 3×3 hardware-filtered percentage closer lookup"
            }};
            const uint32_t Alg = std::min(Data.PbrShadowIndex, 3u);
            char NumBuf[8]{}; std::snprintf(NumBuf, sizeof(NumBuf), "%u", Alg);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, NumBuf, AlgTitles[Alg], "Real-time direct solar shadow algorithm");
            PaintShadowStudyWell(Surface, { LeftX + PadX, Y, BodyW, 108.0f }, Alg, Data.SmrtMarchSteps);
            Y += 120.0f;

            bool Local = false;
            for (uint32_t I = 0u; I < 4u; ++I)
            {
                const Rectangle2D R { LeftX + PadX, Y, BodyW, 46.0f };
                const bool Sel = (Alg == I);
                const bool Hov = R.Contains(Cur);
                Surface.FillRectangle(R, Sel ? ColorRgba{ 44u, 44u, 46u, 255u } : (Hov ? ColorRgba{ 30u, 31u, 34u, 255u } : ColorRgba{ 22u, 22u, 24u, 255u }), 12.0f);
                Surface.StrokeRectangle(R, Sel ? ColorRgba{ 255u, 255u, 255u, 52u } : ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);
                char IdxTxt[8]{}; std::snprintf(IdxTxt, sizeof(IdxTxt), "%u", I);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { R.X + 12.0f, R.Y + 10.0f }, 11.0f, ColorRgba{ 148u, 152u, 160u, 255u }, IdxTxt);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { R.X + 30.0f, R.Y + 9.0f }, 11.0f, Sel ? ColorRgba{ 242u, 242u, 242u, 255u } : ColorRgba{ 208u, 208u, 208u, 255u }, AlgTitles[I]);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { R.X + 30.0f, R.Y + 25.0f }, R.Width - 40.0f, 9.3f, ColorRgba{ 122u, 122u, 126u, 255u }, AlgSubs[I]);
                if (Hov && Clicked) { Data.PbrShadowIndex = I; Local = true; }
                Y += 50.0f;
            }
            Y += 6.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8701u, "SMRT shadow rays",  "rays",  "%.0f", 1.0f, 8.0f,  Data.SmrtShadowRays, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8702u, "SMRT march steps","steps", "%.0f", 4.0f, 16.0f, Data.SmrtMarchSteps, ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 224.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Reflections & sky fill");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            bool Local = false;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Specular reflections", Data.PbrReflectionsEnabled, Input, Local);
            static constexpr std::array<const char*, 3u> ReflSrc = {{ "Sky", "WideTree", "SDF" }};
            int32_t PickR = -1;
            Y = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "REFLECTION SOURCE", ReflSrc, std::min(Data.ReflectionSourceIndex, 2u), Input, PickR);
            if (PickR >= 0) { Data.ReflectionSourceIndex = static_cast<uint32_t>(PickR); Local = true; }
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8703u, "Sky fill gain", "x", "%.2f", 0.0f, 3.0f, Data.PbrFillGain, ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            CursorY = ConstructXeGtaoSection(CursorY);
        }
        else
        {
            CursorY = ConstructOutputSection(CursorY);
        }
    }
    else if (Data.RendererIndex == 1u) // B · SDF sub-pages (1:1 HTML 02-cs-1..4)
    {
        if (ActivePage == 1u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 736.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Distance-field shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            static constexpr std::array<const char*, 6u> PenTitles = {{
                "Point SDF", "Analytic Cone", "Voxel Clipmap", "Voxel + Cone GI", "Hybrid Sobol Cap", "SDF MC Ref 96 spp"
            }};
            static constexpr std::array<const char*, 6u> PenSubs = {{
                "Crisp point-centroid occlusion with no penumbra spread.",
                "Closed-form solid-angle cone penumbra across area emitters.",
                "Stratified 3D clipmap, cone-traced area penumbra.",
                "Clipmap penumbra coupled with multi-cone indirect bounce GI.",
                "Owen-scrambled Sobol spherical-cap penumbra integration.",
                "High-sample Monte Carlo reference penumbra."
            }};
            const uint32_t PenIdx = std::min(Data.SdfPenumbraIndex, 5u);
            char NumBuf[8]{}, SubBuf[72]{};
            std::snprintf(NumBuf, sizeof(NumBuf), "%u", PenIdx);
            std::snprintf(SubBuf, sizeof(SubBuf), "SDF penumbra · %.0f rays · %.0f march steps", Data.SmrtShadowRays, Data.SmrtMarchSteps);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, NumBuf, PenTitles[PenIdx], SubBuf);
            PaintShadowStudyWell(Surface, { LeftX + PadX, Y, BodyW, 108.0f }, PenIdx, Data.SmrtMarchSteps);
            Y += 120.0f;

            bool Local = false;
            for (uint32_t I = 0u; I < 6u; ++I)
            {
                const Rectangle2D R { LeftX + PadX, Y, BodyW, 46.0f };
                const bool Sel = (PenIdx == I);
                const bool Hov = R.Contains(Cur);
                Surface.FillRectangle(R, Sel ? ColorRgba{ 44u, 44u, 46u, 255u } : (Hov ? ColorRgba{ 30u, 31u, 34u, 255u } : ColorRgba{ 22u, 22u, 24u, 255u }), 12.0f);
                Surface.StrokeRectangle(R, Sel ? ColorRgba{ 255u, 255u, 255u, 52u } : ColorRgba{ 255u, 255u, 255u, 14u }, 12.0f, 1.0f);
                char IdxTxt[8]{}; std::snprintf(IdxTxt, sizeof(IdxTxt), "%u", I);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { R.X + 12.0f, R.Y + 10.0f }, 11.0f, ColorRgba{ 148u, 152u, 160u, 255u }, IdxTxt);
                WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { R.X + 30.0f, R.Y + 9.0f }, 11.0f, Sel ? ColorRgba{ 242u, 242u, 242u, 255u } : ColorRgba{ 208u, 208u, 208u, 255u }, PenTitles[I]);
                WorkspaceAppearance::ConstructTruncatedText(Surface, FontWeightClass::Regular, { R.X + 30.0f, R.Y + 25.0f }, R.Width - 40.0f, 9.3f, ColorRgba{ 122u, 122u, 126u, 255u }, PenSubs[I]);
                if (Hov && Clicked) { Data.SdfPenumbraIndex = I; Local = true; }
                Y += 50.0f;
            }
            Y += 6.0f;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8710u, "SMRT shadow rays",  "rays",  "%.0f", 1.0f, 8.0f,  Data.SmrtShadowRays, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8711u, "SMRT march steps","steps", "%.0f", 4.0f, 16.0f, Data.SmrtMarchSteps, ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 444.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Cone GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char KpiBuf[16]{}, SubBuf[64]{};
            std::snprintf(KpiBuf, sizeof(KpiBuf), "%.0f", Data.SdfConeSteps);
            std::snprintf(SubBuf, sizeof(SubBuf), "Bounce × %.2f · live .slang GDF clipmap", Data.SdfGiIntensity);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, KpiBuf, "cones", SubBuf);
            PaintConeGiStudyWell(Surface, { LeftX + PadX, Y, BodyW, 114.0f }, static_cast<uint32_t>(std::lround(Data.SdfConeSteps)), Data.SdfFillGain);
            Y += 128.0f;

            bool Local = false;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8712u, "Cone directions",   "",  "%.0f", 3.0f, 12.0f, Data.SdfConeSteps,   ActiveDragId, Input, Local);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8713u, "Bounce intensity",  "x", "%.2f", 0.0f, 2.5f,  Data.SdfGiIntensity, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8714u, "Sky & ambient fill","x", "%.2f", 0.0f, 3.0f,  Data.SdfFillGain,    ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            // Dedicated GPU SDF Distance-Field Ambient Occlusion (XeGTAO is PBR/Ray only)
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 446.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "SDF Ambient occlusion");
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { S.Right() - PadX - 18.0f, S.Y + InspectorMetrics::SectionPadTop },
                "SDF UDF Ambient Occlusion (.slang)",
                "Evaluates 12-ray local + global signed distance hemisphere occlusion in WorldViewportRadiometricCompute.slang (EvaluateGlobalDistanceAmbientOcclusion). Zero screen-space disocclusion artifacts.",
                Input);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char KpiBuf[16]{}, SubBuf[88]{};
            std::snprintf(KpiBuf, sizeof(KpiBuf), "%.2f", Data.SdfAoRadiusM);
            std::snprintf(SubBuf, sizeof(SubBuf), "Contrast × %.2f · Local + Global SDF clipmap", Data.SdfAoContrast);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, KpiBuf, "m reach", SubBuf);
            const Rectangle2D Well { LeftX + PadX, Y, BodyW, 106.0f };
            PaintAoStudyWell(Surface, Well, 6u);
            Y = Well.Bottom() + 14.0f;

            bool Local = false;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8715u, "SDF AO reach",    "m", "%.2f", 0.35f, 3.50f, Data.SdfAoRadiusM,  ActiveDragId, Input, Local);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8716u, "SDF AO contrast", "x", "%.2f", 0.40f, 3.50f, Data.SdfAoContrast, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Specular reflections", Data.PbrReflectionsEnabled, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            CursorY = ConstructOutputSection(CursorY);
        }
    }
    else if (Data.RendererIndex == 2u) // C · Ray / Path Tracer sub-pages
    {
        if (ActivePage == 1u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 404.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Ray & path tracer");
            InspectorPrimitives::ConstructTooltipTrigger(
                Surface, { S.Right() - PadX - 18.0f, S.Y + InspectorMetrics::SectionPadTop },
                "Ray / Path Tracer (.slang)",
                "Drives WorldRenderEngine 3..5 (BVH4 GI, Path Tracer Realtime, Path Tracer Final), Samples/frame, Target samples, and Max bounces in WorldViewportRadiometricCompute.slang.",
                Input);
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char KpiBuf[16]{}; std::snprintf(KpiBuf, sizeof(KpiBuf), "%.0f", Data.PathTraceBatchSpp);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, KpiBuf, "spp / frame", "Live progressive accumulation in Vulkan compute");

            bool Local = false;
            static constexpr std::array<const char*, 3u> Engines = {{ "BVH4 GI", "PT Realtime", "PT Final" }};
            int32_t PickEng = -1;
            Y = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "TRACER ENGINE", Engines, std::min(Data.RayTracerIndex, 2u), Input, PickEng);
            if (PickEng >= 0) { Data.RayTracerIndex = static_cast<uint32_t>(PickEng); Local = true; }
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Enable BVH4 raytracing", Data.RaytracingEnabled, Input, Local);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8730u, "Samples / frame", "spp", "%.0f", 1.0f,  8.0f,    Data.PathTraceBatchSpp,   ActiveDragId, Input, Local);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8731u, "Target samples",  "spp", "%.0f", 0.0f,  2048.0f, Data.PathTraceTargetSpp,  ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8732u, "Max bounces",   "",    "%.0f", 1.0f,  8.0f,    Data.PathTraceMaxBounces, ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 2u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 444.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Raytraced GI");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            char KpiBuf[16]{}; std::snprintf(KpiBuf, sizeof(KpiBuf), "%.0f", Data.SdfConeSteps);
            Y = InspectorPrimitives::ConstructMetricReadout(Surface, LeftX + PadX, Y, KpiBuf, "samples", "Indirect bounce radiance & sky fill");
            PaintConeGiStudyWell(Surface, { LeftX + PadX, Y, BodyW, 114.0f }, static_cast<uint32_t>(std::lround(Data.SdfConeSteps)), Data.PbrFillGain);
            Y += 128.0f;
            bool Local = false;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8733u, "GI samples",        "",  "%.0f", 3.0f, 12.0f, Data.SdfConeSteps,   ActiveDragId, Input, Local);
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8734u, "Bounce intensity",  "x", "%.2f", 0.0f, 2.5f,  Data.SdfGiIntensity, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8735u, "Sky & ambient fill","x", "%.2f", 0.0f, 3.0f,  Data.PbrFillGain,    ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 3u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 340.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Raytraced shadows");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            PaintShadowStudyWell(Surface, { LeftX + PadX, Y, BodyW, 108.0f }, 2u, Data.SmrtMarchSteps);
            Y += 120.0f;
            bool Local = false;
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8736u, "Shadow rays",      "rays",  "%.0f", 1.0f, 8.0f,  Data.SmrtShadowRays, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8737u, "Ray march steps","steps", "%.0f", 4.0f, 16.0f, Data.SmrtMarchSteps, ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else if (ActivePage == 4u)
        {
            CursorY = ConstructXeGtaoSection(CursorY);
        }
        else if (ActivePage == 5u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 212.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "Specular reflections");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            bool Local = false;
            Y = InspectorPrimitives::ConstructToggleFieldRow(Surface, LeftX + PadX, Y, BodyW, "Specular reflections", Data.RayReflectionsEnabled, Input, Local);
            static constexpr std::array<const char*, 3u> ReflSrc = {{ "Sky", "WideTree", "SDF" }};
            int32_t PickR = -1;
            (void)InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "REFLECTION SOURCE", ReflSrc, std::min(Data.ReflectionSourceIndex, 2u), Input, PickR);
            if (PickR >= 0) { Data.ReflectionSourceIndex = static_cast<uint32_t>(PickR); Local = true; }
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            CursorY = ConstructOutputSection(CursorY);
        }
    }
    else // D · MatCap sub-pages
    {
        if (ActivePage == 1u)
        {
            const Rectangle2D S { LeftX, CursorY, InnerWidth, 278.0f };
            InspectorPrimitives::ConstructSectionSurface(Surface, S, "MatCap studio");
            float Y = CursorY + InspectorMetrics::SectionPadTop + 28.0f;
            bool Local = false;
            static constexpr std::array<const char*, 4u> Presets = {{ "Clay", "Ceramic", "Metallic", "Normal" }};
            int32_t PickP = -1;
            Y = InspectorPrimitives::ConstructSegmentTrack(Surface, LeftX + PadX, Y, BodyW, "STUDIO SPHERE", Presets, std::min(Data.MatcapChannelIndex, 3u), Input, PickP);
            if (PickP >= 0) { Data.MatcapChannelIndex = static_cast<uint32_t>(PickP); Local = true; }
            Y = InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8751u, "Rim highlight",  "x", "%.2f", 0.0f, 1.0f, Data.MatcapRimGain, ActiveDragId, Input, Local);
            (void)InspectorPrimitives::ConstructScalarFieldRow(Surface, LeftX + PadX, Y, BodyW, 8750u, "Exposure gain", "x", "%.2f", 0.1f, 4.0f, Data.ExposureGain,  ActiveDragId, Input, Local);
            if (Local) { OutMutated = true; SyncLiveNow(); }
            CursorY = S.Bottom() + InspectorMetrics::SectionGap;
        }
        else
        {
            CursorY = ConstructOutputSection(CursorY);
        }
    }

    // 1:1 HTML quiet footer row: "Source defaults · this camera only" + [Reset] pill
    {
        const Rectangle2D FootRow { LeftX + 4.0f, CursorY, InnerWidth - 8.0f, 30.0f };
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { FootRow.X, FootRow.Y + 8.0f }, 9.8f, ColorRgba{ 138u, 142u, 150u, 255u }, "Source defaults · this camera only");
        const Rectangle2D ResetButtonBounds { FootRow.Right() - 62.0f, FootRow.Y + 2.0f, 62.0f, 26.0f };
        const bool Hov = ResetButtonBounds.Contains(Cur);
        Surface.FillRectangle(ResetButtonBounds, Hov ? ColorRgba{ 54u, 56u, 62u, 255u } : ColorRgba{ 42u, 42u, 44u, 255u }, 13.0f);
        Surface.StrokeRectangle(ResetButtonBounds, ColorRgba{ 255u, 255u, 255u, 24u }, 13.0f, 1.0f);
        const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 10.0f, "Reset");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { ResetButtonBounds.Center().X - RW * 0.5f, ResetButtonBounds.Y + 7.0f }, 10.0f, ColorRgba{ 230u, 230u, 230u, 255u }, "Reset");
        if (Hov && Clicked)
        {
            Data = CameraSettingsData {};
            OutMutated = true;
            SyncLiveNow();
        }
        CursorY = FootRow.Bottom() + 12.0f;
    }

    (void)Entry;
    return CursorY;
}

} // namespace Frontier
