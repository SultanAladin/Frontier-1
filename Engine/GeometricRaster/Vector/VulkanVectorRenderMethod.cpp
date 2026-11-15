/*============================================================================================================================================
                                                         VULKANVECTORRENDERMETHOD.CPP
============================================================================================================================================*/
// 🧩 Implementation of ThorVG 1.0.0 symbolic SVG + TTF RenderMethod frontend producing GPU stencil-cover fans & stroke triangles for Vulkan .slang.

#include "Engine/GeometricRaster/Vector/VulkanVectorRenderMethod.h"

#include <thorvg.h>
#include "tvgCommon.h"
#include "tvgRender.h"
#include "tvgCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>
#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

namespace Frontier::GeometricRaster
{

namespace
{

constexpr float PiScalar = 3.14159265358979323846f;

std::mutex& AccessThorVGInitMutex() noexcept
{
    static std::mutex Guard;
    return Guard;
}

uint32_t& AccessThorVGInitCount() noexcept
{
    static uint32_t Count = 0u;
    return Count;
}

std::unordered_set<std::string>& AccessLoadedFontKeys()
{
    static std::unordered_set<std::string> Keys;
    return Keys;
}

std::unordered_map<std::string, std::vector<uint8_t>>& AccessFontByteCache()
{
    static std::unordered_map<std::string, std::vector<uint8_t>> Cache;
    return Cache;
}

[[nodiscard]] bool ReadFileBytesExact(const std::filesystem::path& FilePath, std::vector<uint8_t>& OutBytes)
{
    std::ifstream In(FilePath, std::ios::binary | std::ios::ate);
    if (!In.is_open())
    {
        return false;
    }
    const std::streamsize Size = In.tellg();
    if (Size <= 0)
    {
        return false;
    }
    OutBytes.resize(static_cast<std::size_t>(Size));
    In.seekg(0, std::ios::beg);
    return static_cast<bool>(In.read(reinterpret_cast<char*>(OutBytes.data()), Size));
}

[[nodiscard]] uint32_t DecodeUtf8Codepoint(std::string_view Text, std::size_t& Cursor) noexcept
{
    if (Cursor >= Text.size())
    {
        return 0u;
    }
    const uint8_t C0 = static_cast<uint8_t>(Text[Cursor++]);
    if (C0 < 0x80u)
    {
        return C0;
    }
    if ((C0 & 0xE0u) == 0xC0u && Cursor < Text.size())
    {
        const uint8_t C1 = static_cast<uint8_t>(Text[Cursor++]);
        return ((static_cast<uint32_t>(C0 & 0x1Fu)) << 6u) | static_cast<uint32_t>(C1 & 0x3Fu);
    }
    if ((C0 & 0xF0u) == 0xE0u && Cursor + 1u < Text.size())
    {
        const uint8_t C1 = static_cast<uint8_t>(Text[Cursor++]);
        const uint8_t C2 = static_cast<uint8_t>(Text[Cursor++]);
        return ((static_cast<uint32_t>(C0 & 0x0Fu)) << 12u) | ((static_cast<uint32_t>(C1 & 0x3Fu)) << 6u) | static_cast<uint32_t>(C2 & 0x3Fu);
    }
    if ((C0 & 0xF8u) == 0xF0u && Cursor + 2u < Text.size())
    {
        const uint8_t C1 = static_cast<uint8_t>(Text[Cursor++]);
        const uint8_t C2 = static_cast<uint8_t>(Text[Cursor++]);
        const uint8_t C3 = static_cast<uint8_t>(Text[Cursor++]);
        return ((static_cast<uint32_t>(C0 & 0x07u)) << 18u) | ((static_cast<uint32_t>(C1 & 0x3Fu)) << 12u) |
               ((static_cast<uint32_t>(C2 & 0x3Fu)) << 6u) | static_cast<uint32_t>(C3 & 0x3Fu);
    }
    return '?';
}

[[nodiscard]] VectorVertex2D TransformThorVGPoint(const tvg::Point& Pt, const tvg::Matrix& M) noexcept
{
    return {
        Pt.x * M.e11 + Pt.y * M.e12 + M.e13,
        Pt.x * M.e21 + Pt.y * M.e22 + M.e23,
    };
}

[[nodiscard]] VectorVertex2D NormalizeVec2(float Dx, float Dy) noexcept
{
    const float Len = std::hypot(Dx, Dy);
    if (Len <= 1.0e-6f)
    {
        return { 1.0f, 0.0f };
    }
    return { Dx / Len, Dy / Len };
}

void SubdivideCubicAdaptive(
    VectorVertex2D               P0,
    VectorVertex2D               P1,
    VectorVertex2D               P2,
    VectorVertex2D               P3,
    float                        ToleranceSq,
    uint32_t                     Depth,
    std::vector<VectorVertex2D>& OutPoints)
{
    const float Dx  = P3.X - P0.X;
    const float Dy  = P3.Y - P0.Y;
    const float D2  = std::fabs((P1.X - P3.X) * Dy - (P1.Y - P3.Y) * Dx);
    const float D3  = std::fabs((P2.X - P3.X) * Dy - (P2.Y - P3.Y) * Dx);
    const float LenSq = Dx * Dx + Dy * Dy;

    if (Depth >= 7u || ((D2 + D3) * (D2 + D3) <= ToleranceSq * std::max(LenSq, 1.0e-4f)))
    {
        OutPoints.push_back(P3);
        return;
    }

    const VectorVertex2D P01   { 0.5f * (P0.X + P1.X), 0.5f * (P0.Y + P1.Y) };
    const VectorVertex2D P12   { 0.5f * (P1.X + P2.X), 0.5f * (P1.Y + P2.Y) };
    const VectorVertex2D P23   { 0.5f * (P2.X + P3.X), 0.5f * (P2.Y + P3.Y) };
    const VectorVertex2D P012  { 0.5f * (P01.X + P12.X), 0.5f * (P01.Y + P12.Y) };
    const VectorVertex2D P123  { 0.5f * (P12.X + P23.X), 0.5f * (P12.Y + P23.Y) };
    const VectorVertex2D P0123 { 0.5f * (P012.X + P123.X), 0.5f * (P012.Y + P123.Y) };

    SubdivideCubicAdaptive(P0, P01, P012, P0123, ToleranceSq, Depth + 1u, OutPoints);
    SubdivideCubicAdaptive(P0123, P123, P23, P3, ToleranceSq, Depth + 1u, OutPoints);
}

void AppendFanTriangles(
    const std::vector<VectorVertex2D>& Contour,
    std::vector<VectorVertex2D>&       OutTriangles,
    float&                             MinX,
    float&                             MinY,
    float&                             MaxX,
    float&                             MaxY)
{
    if (Contour.size() < 3u)
    {
        return;
    }
    const VectorVertex2D Anchor = Contour.front();
    for (const VectorVertex2D& V : Contour)
    {
        MinX = std::min(MinX, V.X);
        MinY = std::min(MinY, V.Y);
        MaxX = std::max(MaxX, V.X);
        MaxY = std::max(MaxY, V.Y);
    }
    for (std::size_t Idx = 1u; Idx + 1u < Contour.size(); ++Idx)
    {
        OutTriangles.push_back(Anchor);
        OutTriangles.push_back(Contour[Idx]);
        OutTriangles.push_back(Contour[Idx + 1u]);
    }
}

void AppendRoundFanArc(
    VectorVertex2D               Center,
    VectorVertex2D               StartDir,
    VectorVertex2D               EndDir,
    float                        Radius,
    std::vector<VectorVertex2D>& OutTriangles)
{
    float A0 = std::atan2(StartDir.Y, StartDir.X);
    float A1 = std::atan2(EndDir.Y, EndDir.X);
    float Delta = A1 - A0;
    while (Delta > PiScalar)  Delta -= 2.0f * PiScalar;
    while (Delta < -PiScalar) Delta += 2.0f * PiScalar;
    const uint32_t Segments = std::max(3u, static_cast<uint32_t>(std::ceil(std::fabs(Delta) * 8.0f / PiScalar)));
    for (uint32_t Step = 0u; Step < Segments; ++Step)
    {
        const float T0 = A0 + Delta * (static_cast<float>(Step) / static_cast<float>(Segments));
        const float T1 = A0 + Delta * (static_cast<float>(Step + 1u) / static_cast<float>(Segments));
        OutTriangles.push_back(Center);
        OutTriangles.push_back({ Center.X + std::cos(T0) * Radius, Center.Y + std::sin(T0) * Radius });
        OutTriangles.push_back({ Center.X + std::cos(T1) * Radius, Center.Y + std::sin(T1) * Radius });
    }
}

void BuildStrokeTrianglesForPolygons(
    std::span<const std::vector<VectorVertex2D>> Polygons,
    std::span<const uint8_t>                     ClosedLoops,
    float                                        StrokeWidthPx,
    VectorStrokeCapStyle                         CapStyle,
    VectorStrokeJoinStyle                        JoinStyle,
    std::vector<VectorVertex2D>&                 OutTriangles,
    float&                                       MinX,
    float&                                       MinY,
    float&                                       MaxX,
    float&                                       MaxY)
{
    if (StrokeWidthPx <= 1.0e-4f)
    {
        return;
    }
    const float HalfW = StrokeWidthPx * 0.5f;

    for (std::size_t PolyIdx = 0u; PolyIdx < Polygons.size(); ++PolyIdx)
    {
        const auto& Raw = Polygons[PolyIdx];
        if (Raw.size() < 2u)
        {
            continue;
        }
        const bool Closed = (PolyIdx < ClosedLoops.size()) ? (ClosedLoops[PolyIdx] != 0u) : false;

        std::vector<VectorVertex2D> Pts;
        Pts.reserve(Raw.size());
        for (const auto& P : Raw)
        {
            if (Pts.empty() || std::hypot(P.X - Pts.back().X, P.Y - Pts.back().Y) > 1.0e-3f)
            {
                Pts.push_back(P);
            }
        }
        if (Closed && Pts.size() >= 2u && std::hypot(Pts.front().X - Pts.back().X, Pts.front().Y - Pts.back().Y) <= 1.0e-3f)
        {
            Pts.pop_back();
        }
        if (Pts.size() < 2u)
        {
            continue;
        }

        const std::size_t SegCount = Closed ? Pts.size() : (Pts.size() - 1u);
        for (std::size_t Seg = 0u; Seg < SegCount; ++Seg)
        {
            const VectorVertex2D A = Pts[Seg];
            const VectorVertex2D B = Pts[(Seg + 1u) % Pts.size()];
            const VectorVertex2D Dir = NormalizeVec2(B.X - A.X, B.Y - A.Y);
            const VectorVertex2D Nrm { -Dir.Y, Dir.X };

            const VectorVertex2D A0 { A.X + Nrm.X * HalfW, A.Y + Nrm.Y * HalfW };
            const VectorVertex2D A1 { A.X - Nrm.X * HalfW, A.Y - Nrm.Y * HalfW };
            const VectorVertex2D B0 { B.X + Nrm.X * HalfW, B.Y + Nrm.Y * HalfW };
            const VectorVertex2D B1 { B.X - Nrm.X * HalfW, B.Y - Nrm.Y * HalfW };

            OutTriangles.push_back(A0);
            OutTriangles.push_back(B0);
            OutTriangles.push_back(B1);
            OutTriangles.push_back(A0);
            OutTriangles.push_back(B1);
            OutTriangles.push_back(A1);

            if (JoinStyle == VectorStrokeJoinStyle::Round)
            {
                if (Closed || Seg + 1u < SegCount)
                {
                    const VectorVertex2D C = Pts[(Seg + 2u) % Pts.size()];
                    const VectorVertex2D NextDir = NormalizeVec2(C.X - B.X, C.Y - B.Y);
                    const VectorVertex2D NextNrm { -NextDir.Y, NextDir.X };
                    const float Cross = Dir.X * NextDir.Y - Dir.Y * NextDir.X;
                    if (Cross > 1.0e-4f)
                    {
                        AppendRoundFanArc(B, { -Nrm.X, -Nrm.Y }, { -NextNrm.X, -NextNrm.Y }, HalfW, OutTriangles);
                    }
                    else if (Cross < -1.0e-4f)
                    {
                        AppendRoundFanArc(B, Nrm, NextNrm, HalfW, OutTriangles);
                    }
                }
            }
            else if (Closed || Seg + 1u < SegCount)
            {
                const VectorVertex2D C = Pts[(Seg + 2u) % Pts.size()];
                const VectorVertex2D NextDir = NormalizeVec2(C.X - B.X, C.Y - B.Y);
                const VectorVertex2D NextNrm { -NextDir.Y, NextDir.X };
                const float Cross = Dir.X * NextDir.Y - Dir.Y * NextDir.X;
                if (Cross > 1.0e-4f)
                {
                    OutTriangles.push_back(B);
                    OutTriangles.push_back({ B.X - Nrm.X * HalfW, B.Y - Nrm.Y * HalfW });
                    OutTriangles.push_back({ B.X - NextNrm.X * HalfW, B.Y - NextNrm.Y * HalfW });
                }
                else if (Cross < -1.0e-4f)
                {
                    OutTriangles.push_back(B);
                    OutTriangles.push_back({ B.X + Nrm.X * HalfW, B.Y + Nrm.Y * HalfW });
                    OutTriangles.push_back({ B.X + NextNrm.X * HalfW, B.Y + NextNrm.Y * HalfW });
                }
            }
        }

        if (!Closed)
        {
            const VectorVertex2D StartDir = NormalizeVec2(Pts[1].X - Pts[0].X, Pts[1].Y - Pts[0].Y);
            const VectorVertex2D EndDir   = NormalizeVec2(Pts.back().X - Pts[Pts.size() - 2u].X, Pts.back().Y - Pts[Pts.size() - 2u].Y);
            if (CapStyle == VectorStrokeCapStyle::Round)
            {
                AppendRoundFanArc(Pts.front(), { -StartDir.Y, StartDir.X }, { StartDir.Y, -StartDir.X }, HalfW, OutTriangles);
                AppendRoundFanArc(Pts.back(), { EndDir.Y, -EndDir.X }, { -EndDir.Y, EndDir.X }, HalfW, OutTriangles);
            }
            else if (CapStyle == VectorStrokeCapStyle::Square)
            {
                auto AppendStrokeCap = [&](VectorVertex2D Tip, VectorVertex2D OutDir)
                {
                    const VectorVertex2D Nrm { -OutDir.Y, OutDir.X };
                    const VectorVertex2D P0 { Tip.X + Nrm.X * HalfW, Tip.Y + Nrm.Y * HalfW };
                    const VectorVertex2D P1 { Tip.X - Nrm.X * HalfW, Tip.Y - Nrm.Y * HalfW };
                    const VectorVertex2D Q0 { P0.X + OutDir.X * HalfW, P0.Y + OutDir.Y * HalfW };
                    const VectorVertex2D Q1 { P1.X + OutDir.X * HalfW, P1.Y + OutDir.Y * HalfW };
                    OutTriangles.push_back(P0); OutTriangles.push_back(Q0); OutTriangles.push_back(Q1);
                    OutTriangles.push_back(P0); OutTriangles.push_back(Q1); OutTriangles.push_back(P1);
                };
                AppendStrokeCap(Pts.front(), { -StartDir.X, -StartDir.Y });
                AppendStrokeCap(Pts.back(), EndDir);
            }
        }
    }

    for (const VectorVertex2D& V : OutTriangles)
    {
        MinX = std::min(MinX, V.X);
        MinY = std::min(MinY, V.Y);
        MaxX = std::max(MaxX, V.X);
        MaxY = std::max(MaxY, V.Y);
    }
}

[[nodiscard]] uint8_t ResolveThorVGBlendMode(tvg::BlendMethod Method) noexcept
{
    switch (Method)
    {
        case tvg::BlendMethod::Multiply: return 1u;
        case tvg::BlendMethod::Screen:   return 2u;
        case tvg::BlendMethod::Overlay:  return 3u;
        case tvg::BlendMethod::Add:      return 4u;
        case tvg::BlendMethod::Darken:   return 5u;
        default:                         return 0u;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      THORVG VULKAN SYMBOLIC RENDERMETHOD SPI
//------------------------------------------------------------------------------------------------------------------------

struct SymbolicPreparedShape
{
    ExtractedVectorShape             GpuShape   {};
    std::vector<VectorCubicSubpath>  Subpaths   {};
};

struct SymbolicGroupEntry : tvg::RenderCompositor
{
    uint8_t SavedBlend = 0u;
    float   SavedOpacity = 1.0f;
};

class VulkanVectorRenderMethod final : public tvg::RenderMethod
{
public:
    VulkanVectorRenderMethod(uint32_t WidthPx, uint32_t HeightPx)
    {
        MainSurfaceSpec.w = WidthPx;
        MainSurfaceSpec.h = HeightPx;
        vport = { { 0, 0 }, { static_cast<int32_t>(WidthPx), static_cast<int32_t>(HeightPx) } };
    }

    ~VulkanVectorRenderMethod() override
    {
        for (SymbolicPreparedShape* Entry : OwnedShapes)
        {
            delete Entry;
        }
        OwnedShapes.clear();
    }

    bool preUpdate() override { return true; }

    tvg::RenderData prepare(
        const tvg::RenderShape& rshape,
        tvg::RenderData         existing,
        const tvg::Matrix&      transform,
        tvg::Array<tvg::RenderData>& clips,
        uint8_t                 opacity,
        tvg::RenderUpdateFlag   /*flags*/,
        bool                    clipper) override
    {
        (void)clips;
        if (clipper)
        {
            return existing;
        }

        auto* Prepared = static_cast<SymbolicPreparedShape*>(existing);
        if (Prepared == nullptr)
        {
            Prepared = new SymbolicPreparedShape();
            OwnedShapes.push_back(Prepared);
        }

        ExtractedVectorShape& Out = Prepared->GpuShape;
        Out = ExtractedVectorShape{};
        Prepared->Subpaths.clear();

        Out.EvenOddFillRule = (rshape.rule == tvg::FillRule::EvenOdd);
        Out.Opacity         = (static_cast<float>(opacity) / 255.0f) * CompositorOpacityMultiplier;
        Out.BlendOrdinal    = CurrentBlendOrdinal;

        // Extract cubic Bezier subpaths & adaptive flat contours in post-transform coordinates
        std::vector<std::vector<VectorVertex2D>> FlatPolygons;
        std::vector<uint8_t>                     ClosedLoops;

        const tvg::PathCommand* Cmds = rshape.path.cmds.data;
        const uint32_t          CmdCount = rshape.path.cmds.count;
        const tvg::Point*       Pts = rshape.path.pts.data;
        const uint32_t          PtCount = rshape.path.pts.count;
        uint32_t                PtCursor = 0u;

        VectorCubicSubpath       ActiveCubicSubpath {};
        std::vector<VectorVertex2D> ActiveFlat {};
        bool                     HaveActiveSubpath = false;
        VectorVertex2D           CurrPt { 0.0f, 0.0f };

        auto FlushSubpath = [&](bool Closed)
        {
            if (HaveActiveSubpath && !ActiveFlat.empty())
            {
                ActiveCubicSubpath.ClosedLoop = Closed;
                Prepared->Subpaths.push_back(std::move(ActiveCubicSubpath));
                FlatPolygons.push_back(std::move(ActiveFlat));
                ClosedLoops.push_back(Closed ? 1u : 0u);
            }
            ActiveCubicSubpath = VectorCubicSubpath{};
            ActiveFlat.clear();
            HaveActiveSubpath = false;
        };

        for (uint32_t CmdIdx = 0u; CmdIdx < CmdCount; ++CmdIdx)
        {
            switch (Cmds[CmdIdx])
            {
                case tvg::PathCommand::MoveTo:
                {
                    if (PtCursor < PtCount)
                    {
                        FlushSubpath(false);
                        CurrPt = TransformThorVGPoint(Pts[PtCursor++], transform);
                        HaveActiveSubpath = true;
                        ActiveFlat.push_back(CurrPt);
                        ActiveCubicSubpath.Knots.push_back({ CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, false });
                    }
                    break;
                }
                case tvg::PathCommand::LineTo:
                {
                    if (PtCursor < PtCount)
                    {
                        const VectorVertex2D Next = TransformThorVGPoint(Pts[PtCursor++], transform);
                        if (!HaveActiveSubpath)
                        {
                            HaveActiveSubpath = true;
                            ActiveFlat.push_back(CurrPt);
                            ActiveCubicSubpath.Knots.push_back({ CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, false });
                        }
                        ActiveFlat.push_back(Next);
                        ActiveCubicSubpath.Knots.push_back({ Next.X, Next.Y, Next.X, Next.Y, Next.X, Next.Y, false });
                        CurrPt = Next;
                    }
                    break;
                }
                case tvg::PathCommand::CubicTo:
                {
                    if (PtCursor + 2u < PtCount)
                    {
                        const VectorVertex2D C1 = TransformThorVGPoint(Pts[PtCursor], transform);
                        const VectorVertex2D C2 = TransformThorVGPoint(Pts[PtCursor + 1u], transform);
                        const VectorVertex2D P3 = TransformThorVGPoint(Pts[PtCursor + 2u], transform);
                        PtCursor += 3u;
                        if (!HaveActiveSubpath)
                        {
                            HaveActiveSubpath = true;
                            ActiveFlat.push_back(CurrPt);
                            ActiveCubicSubpath.Knots.push_back({ CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, CurrPt.X, CurrPt.Y, true });
                        }
                        ActiveCubicSubpath.Knots.back().OutHandleX = C1.X;
                        ActiveCubicSubpath.Knots.back().OutHandleY = C1.Y;
                        ActiveCubicSubpath.Knots.back().Smooth     = true;
                        ActiveCubicSubpath.Knots.push_back({ P3.X, P3.Y, C2.X, C2.Y, P3.X, P3.Y, true });
                        SubdivideCubicAdaptive(CurrPt, C1, C2, P3, 0.25f * 0.25f, 0u, ActiveFlat);
                        CurrPt = P3;
                    }
                    break;
                }
                case tvg::PathCommand::Close:
                {
                    FlushSubpath(true);
                    break;
                }
            }
        }
        FlushSubpath(false);

        // Fill extraction
        if (rshape.fill != nullptr)
        {
            Out.FillEnabled = true;
            const tvg::Fill::ColorStop* Stops = nullptr;
            const uint32_t StopCount = rshape.fill->colorStops(&Stops);
            Out.StopCount = std::min<uint32_t>(StopCount, 8u);
            for (uint32_t I = 0u; I < Out.StopCount; ++I)
            {
                Out.Stops[I].Offset = Stops[I].offset;
                Out.Stops[I].Tint   = { Stops[I].r, Stops[I].g, Stops[I].b, Stops[I].a };
            }
            if (Out.StopCount > 0u)
            {
                Out.FillTint = Out.Stops[0].Tint;
            }

            const tvg::Matrix FillMat = transform * rshape.fill->transform();
            if (rshape.fill->type() == tvg::Type::LinearGradient)
            {
                Out.GradientPreset = VectorGradientPreset::Linear;
                float X1 = 0.0f, Y1 = 0.0f, X2 = 1.0f, Y2 = 1.0f;
                static_cast<const tvg::LinearGradient*>(rshape.fill)->linear(&X1, &Y1, &X2, &Y2);
                const VectorVertex2D G0 = TransformThorVGPoint({ X1, Y1 }, FillMat);
                const VectorVertex2D G1 = TransformThorVGPoint({ X2, Y2 }, FillMat);
                Out.GradientStart[0] = G0.X; Out.GradientStart[1] = G0.Y;
                Out.GradientEnd[0]   = G1.X; Out.GradientEnd[1]   = G1.Y;
            }
            else if (rshape.fill->type() == tvg::Type::RadialGradient)
            {
                Out.GradientPreset = VectorGradientPreset::Radial;
                float Cx = 0.0f, Cy = 0.0f, R = 1.0f, Fx = 0.0f, Fy = 0.0f, Fr = 0.0f;
                static_cast<const tvg::RadialGradient*>(rshape.fill)->radial(&Cx, &Cy, &R, &Fx, &Fy, &Fr);
                const VectorVertex2D Center = TransformThorVGPoint({ Cx, Cy }, FillMat);
                const VectorVertex2D EdgePt = TransformThorVGPoint({ Cx + R, Cy }, FillMat);
                const float WorldRadius = std::hypot(EdgePt.X - Center.X, EdgePt.Y - Center.Y);
                Out.GradientStart[0] = Center.X;
                Out.GradientStart[1] = Center.Y;
                Out.GradientEnd[0]   = EdgePt.X;
                Out.GradientEnd[1]   = EdgePt.Y;
                Out.GradientRadius0  = std::max(0.0f, Fr * tvg::scaling(FillMat));
                Out.GradientRadius1  = std::max(1.0e-3f, WorldRadius);
            }
        }
        else if (rshape.color.a > 0u)
        {
            Out.FillEnabled    = true;
            Out.GradientPreset = VectorGradientPreset::Solid;
            Out.FillTint       = { rshape.color.r, rshape.color.g, rshape.color.b, rshape.color.a };
        }
        else
        {
            Out.FillEnabled = false;
        }

        // Stroke extraction
        const float ScaleFactor = std::max(1.0e-4f, tvg::scaling(transform));
        const float WidthPx     = rshape.strokeWidth() * ScaleFactor;
        uint8_t Sr = 0u, Sg = 0u, Sb = 0u, Sa = 0u;
        const bool StrokeTintActive = rshape.strokeFill(&Sr, &Sg, &Sb, &Sa) && (Sa > 0u || rshape.strokeFill() != nullptr);
        if (WidthPx > 1.0e-4f && StrokeTintActive)
        {
            Out.StrokeEnabled = true;
            Out.StrokeWidthPx = WidthPx;
            Out.StrokeTint    = { Sr, Sg, Sb, Sa > 0u ? Sa : uint8_t{ 255u } };
        }

        float MinX = 1.0e30f, MinY = 1.0e30f, MaxX = -1.0e30f, MaxY = -1.0e30f;
        if (Out.FillEnabled)
        {
            for (const auto& Poly : FlatPolygons)
            {
                AppendFanTriangles(Poly, Out.FillFanTriangles, MinX, MinY, MaxX, MaxY);
            }
        }
        if (Out.StrokeEnabled)
        {
            const auto Cap  = static_cast<VectorStrokeCapStyle>(static_cast<uint8_t>(rshape.strokeCap()));
            const auto Join = static_cast<VectorStrokeJoinStyle>(static_cast<uint8_t>(rshape.strokeJoin()));
            BuildStrokeTrianglesForPolygons(FlatPolygons, ClosedLoops, WidthPx, Cap, Join, Out.StrokeTriangles, MinX, MinY, MaxX, MaxY);
        }

        if (MinX <= MaxX && MinY <= MaxY)
        {
            Out.BoundsMinX = MinX;
            Out.BoundsMinY = MinY;
            Out.BoundsMaxX = MaxX;
            Out.BoundsMaxY = MaxY;
        }
        return Prepared;
    }

    tvg::RenderData prepare(
        tvg::RenderSurface* /*surface*/,
        tvg::RenderData     existing,
        const tvg::Matrix&  /*transform*/,
        tvg::Array<tvg::RenderData>& /*clips*/,
        uint8_t             /*opacity*/,
        tvg::RenderUpdateFlag /*flags*/) override
    {
        return existing;
    }

    bool postUpdate() override { return true; }
    bool preRender() override  { EmittedShapes.clear(); EmittedLayers.clear(); return true; }

    bool renderShape(tvg::RenderData rd) override
    {
        const auto* Prepared = static_cast<const SymbolicPreparedShape*>(rd);
        if (Prepared == nullptr)
        {
            return true;
        }
        if (Prepared->GpuShape.FillFanTriangles.empty() && Prepared->GpuShape.StrokeTriangles.empty())
        {
            return true;
        }

        ExtractedVectorShape Copy = Prepared->GpuShape;
        Copy.ShapeOrdinal = static_cast<uint32_t>(EmittedShapes.size());
        EmittedShapes.push_back(std::move(Copy));

        ExtractedSvgLayer LayerEntry {};
        LayerEntry.Title           = "SVG Shape " + std::to_string(EmittedLayers.size() + 1u);
        LayerEntry.EvenOddFillRule = Prepared->GpuShape.EvenOddFillRule;
        LayerEntry.FillEnabled     = Prepared->GpuShape.FillEnabled;
        LayerEntry.GradientPreset  = Prepared->GpuShape.GradientPreset;
        LayerEntry.FillTint        = Prepared->GpuShape.FillTint;
        LayerEntry.StrokeEnabled   = Prepared->GpuShape.StrokeEnabled;
        LayerEntry.StrokeWidthPx   = Prepared->GpuShape.StrokeWidthPx;
        LayerEntry.StrokeTint      = Prepared->GpuShape.StrokeTint;
        LayerEntry.Opacity         = Prepared->GpuShape.Opacity;
        LayerEntry.BlendOrdinal    = Prepared->GpuShape.BlendOrdinal;
        if (Prepared->GpuShape.StopCount > 0u)
        {
            LayerEntry.Stops.assign(Prepared->GpuShape.Stops.begin(), Prepared->GpuShape.Stops.begin() + Prepared->GpuShape.StopCount);
            LayerEntry.FillTintB = LayerEntry.Stops.back().Tint;
        }
        const float W = std::max(1.0f, Prepared->GpuShape.BoundsMaxX - Prepared->GpuShape.BoundsMinX);
        const float H = std::max(1.0f, Prepared->GpuShape.BoundsMaxY - Prepared->GpuShape.BoundsMinY);
        LayerEntry.CenterX = 0.5f * (Prepared->GpuShape.BoundsMinX + Prepared->GpuShape.BoundsMaxX);
        LayerEntry.CenterY = 0.5f * (Prepared->GpuShape.BoundsMinY + Prepared->GpuShape.BoundsMaxY);
        LayerEntry.WidthPx  = W;
        LayerEntry.HeightPx = H;
        LayerEntry.Subpaths = Prepared->Subpaths;
        EmittedLayers.push_back(std::move(LayerEntry));
        return true;
    }

    bool renderImage(tvg::RenderData /*rd*/) override { return true; }
    bool postRender() override { return true; }
    void dispose(tvg::RenderData /*rd*/) override {}

    tvg::RenderRegion region(tvg::RenderData rd) override
    {
        const auto* Prepared = static_cast<const SymbolicPreparedShape*>(rd);
        if (Prepared == nullptr || Prepared->GpuShape.BoundsMaxX <= Prepared->GpuShape.BoundsMinX)
        {
            return vport;
        }
        return {
            { static_cast<int32_t>(std::floor(Prepared->GpuShape.BoundsMinX)), static_cast<int32_t>(std::floor(Prepared->GpuShape.BoundsMinY)) },
            { static_cast<int32_t>(std::ceil(Prepared->GpuShape.BoundsMaxX)), static_cast<int32_t>(std::ceil(Prepared->GpuShape.BoundsMaxY)) }
        };
    }

    bool bounds(tvg::RenderData rd, tvg::Point* pt4, const tvg::Matrix& m) override
    {
        const auto* Prepared = static_cast<const SymbolicPreparedShape*>(rd);
        if (Prepared == nullptr || pt4 == nullptr)
        {
            return false;
        }
        const auto& S = Prepared->GpuShape;
        const std::array<tvg::Point, 4u> Corners {{
            { S.BoundsMinX, S.BoundsMinY },
            { S.BoundsMaxX, S.BoundsMinY },
            { S.BoundsMaxX, S.BoundsMaxY },
            { S.BoundsMinX, S.BoundsMaxY },
        }};
        for (std::size_t I = 0u; I < 4u; ++I)
        {
            const VectorVertex2D T = TransformThorVGPoint(Corners[I], m);
            pt4[I] = { T.X, T.Y };
        }
        return true;
    }

    bool blend(tvg::BlendMethod method) override
    {
        CurrentBlendOrdinal = ResolveThorVGBlendMode(method);
        return true;
    }

    tvg::ColorSpace colorSpace() override { return tvg::ColorSpace::ABGR8888S; }
    const tvg::RenderSurface* mainSurface() override { return &MainSurfaceSpec; }
    bool clear() override { EmittedShapes.clear(); EmittedLayers.clear(); return true; }
    bool sync() override  { return true; }
    bool intersectsShape(tvg::RenderData /*rd*/, const tvg::RenderRegion& /*region*/) override { return true; }
    bool intersectsImage(tvg::RenderData /*rd*/, const tvg::RenderRegion& /*region*/) override { return false; }

    tvg::RenderCompositor* target(const tvg::RenderRegion& /*region*/, tvg::ColorSpace /*cs*/, tvg::CompositionFlag /*flags*/) override
    {
        return new SymbolicGroupEntry();
    }

    bool beginComposite(tvg::RenderCompositor* cmp, tvg::MaskMethod /*method*/, uint8_t opacity) override
    {
        auto* Entry = static_cast<SymbolicGroupEntry*>(cmp);
        if (Entry != nullptr)
        {
            Entry->SavedBlend   = CurrentBlendOrdinal;
            Entry->SavedOpacity = CompositorOpacityMultiplier;
        }
        CompositorOpacityMultiplier *= static_cast<float>(opacity) / 255.0f;
        return true;
    }

    bool endComposite(tvg::RenderCompositor* cmp) override
    {
        auto* Entry = static_cast<SymbolicGroupEntry*>(cmp);
        if (Entry != nullptr)
        {
            CurrentBlendOrdinal         = Entry->SavedBlend;
            CompositorOpacityMultiplier = Entry->SavedOpacity;
            delete Entry;
        }
        return true;
    }

    void prepare(tvg::RenderEffect* /*effect*/, const tvg::Matrix& /*transform*/) override {}
    bool region(tvg::RenderEffect* /*effect*/) override { return false; }
    bool render(tvg::RenderCompositor* /*cmp*/, const tvg::RenderEffect* /*effect*/, bool /*direct*/) override { return true; }
    void dispose(tvg::RenderEffect* /*effect*/) override {}
    void damage(tvg::RenderData /*rd*/, const tvg::RenderRegion& /*region*/) override {}
    bool partial(bool /*disable*/) override { return true; }

    std::vector<ExtractedVectorShape> EmittedShapes {};
    std::vector<ExtractedSvgLayer>    EmittedLayers {};

private:
    tvg::RenderSurface                  MainSurfaceSpec            {};
    std::vector<SymbolicPreparedShape*> OwnedShapes                {};
    uint8_t                             CurrentBlendOrdinal        = 0u;
    float                               CompositorOpacityMultiplier = 1.0f;
};

struct VulkanVectorCanvas final : tvg::Canvas
{
    explicit VulkanVectorCanvas(VulkanVectorRenderMethod* BoundMethod)
    {
        BoundMethod->ref();
        pImpl->renderer = BoundMethod;
    }
};

void EnsureThorVGInitialized() noexcept
{
    std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
    if (AccessThorVGInitCount() == 0u)
    {
        (void)tvg::Initializer::init(0u);
    }
    ++AccessThorVGInitCount();
}

void ReleaseThorVGReference() noexcept
{
    std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
    if (AccessThorVGInitCount() > 0u)
    {
        --AccessThorVGInitCount();
        if (AccessThorVGInitCount() == 0u)
        {
            AccessLoadedFontKeys().clear();
            AccessFontByteCache().clear();
            (void)tvg::Initializer::term();
        }
    }
}

[[nodiscard]] bool BuildStbFallbackShapes(
    const std::vector<uint8_t>&        FontBytes,
    std::string_view                   Utf8Text,
    float                              FontSizePx,
    ColorRgba                          FillTint,
    float                              StrokeWidthPx,
    ColorRgba                          StrokeTint,
    VulkanVectorRenderMethod&          Method)
{
    stbtt_fontinfo FontHeader {};
    const int Offset = stbtt_GetFontOffsetForIndex(FontBytes.data(), 0);
    if (Offset < 0 || stbtt_InitFont(&FontHeader, FontBytes.data(), Offset) == 0)
    {
        return false;
    }

    const float Scale = stbtt_ScaleForPixelHeight(&FontHeader, std::max(6.0f, FontSizePx));
    auto*       Shape = tvg::Shape::gen();
    if (Shape == nullptr)
    {
        return false;
    }

    float PenX = 0.0f;
    uint32_t PrevCodepoint = 0u;
    std::size_t Cursor = 0u;
    while (Cursor < Utf8Text.size())
    {
        const uint32_t Cp = DecodeUtf8Codepoint(Utf8Text, Cursor);
        if (Cp == 0u)
        {
            break;
        }
        if (PrevCodepoint != 0u)
        {
            PenX += static_cast<float>(stbtt_GetCodepointKernAdvance(&FontHeader, static_cast<int>(PrevCodepoint), static_cast<int>(Cp))) * Scale;
        }

        int AdvWidth = 0;
        int LeftSideBearing = 0;
        stbtt_GetCodepointHMetrics(&FontHeader, static_cast<int>(Cp), &AdvWidth, &LeftSideBearing);

        stbtt_vertex* Verts = nullptr;
        const int VertCount = stbtt_GetCodepointShape(&FontHeader, static_cast<int>(Cp), &Verts);
        float CurX = PenX;
        float CurY = 0.0f;
        bool ContourOpen = false;
        for (int I = 0; I < VertCount; ++I)
        {
            const stbtt_vertex& V = Verts[I];
            const float X = PenX + static_cast<float>(V.x) * Scale;
            const float Y = -static_cast<float>(V.y) * Scale;
            if (V.type == STBTT_vmove)
            {
                if (ContourOpen)
                {
                    (void)Shape->close();
                }
                (void)Shape->moveTo(X, Y);
                ContourOpen = true;
            }
            else if (V.type == STBTT_vline)
            {
                (void)Shape->lineTo(X, Y);
            }
            else if (V.type == STBTT_vcurve)
            {
                const float Cx = PenX + static_cast<float>(V.cx) * Scale;
                const float Cy = -static_cast<float>(V.cy) * Scale;
                const float C1x = CurX + (2.0f / 3.0f) * (Cx - CurX);
                const float C1y = CurY + (2.0f / 3.0f) * (Cy - CurY);
                const float C2x = X + (2.0f / 3.0f) * (Cx - X);
                const float C2y = Y + (2.0f / 3.0f) * (Cy - Y);
                (void)Shape->cubicTo(C1x, C1y, C2x, C2y, X, Y);
            }
            else if (V.type == STBTT_vcubic)
            {
                const float C1x = PenX + static_cast<float>(V.cx) * Scale;
                const float C1y = -static_cast<float>(V.cy) * Scale;
                const float C2x = PenX + static_cast<float>(V.cx1) * Scale;
                const float C2y = -static_cast<float>(V.cy1) * Scale;
                (void)Shape->cubicTo(C1x, C1y, C2x, C2y, X, Y);
            }
            CurX = X;
            CurY = Y;
        }
        if (ContourOpen)
        {
            (void)Shape->close();
        }
        if (Verts != nullptr)
        {
            stbtt_FreeShape(&FontHeader, Verts);
        }
        PenX += static_cast<float>(AdvWidth) * Scale;
        PrevCodepoint = Cp;
    }

    (void)Shape->fill(FillTint.Red, FillTint.Green, FillTint.Blue, FillTint.Alpha);
    if (StrokeWidthPx > 0.01f)
    {
        (void)Shape->strokeWidth(StrokeWidthPx);
        (void)Shape->strokeFill(StrokeTint.Red, StrokeTint.Green, StrokeTint.Blue, StrokeTint.Alpha);
    }

    VulkanVectorCanvas Canvas(&Method);
    (void)Canvas.add(Shape);
    (void)Canvas.update();
    (void)Canvas.draw(true);
    (void)Canvas.sync();
    return !Method.EmittedShapes.empty();
}

[[nodiscard]] std::string NormalizeSvgInputIfFragment(std::string_view RawInput)
{
    std::string Trimmed(RawInput);
    while (!Trimmed.empty() && (Trimmed.front() == ' ' || Trimmed.front() == '\n' || Trimmed.front() == '\r' || Trimmed.front() == '\t'))
    {
        Trimmed.erase(Trimmed.begin());
    }
    if (Trimmed.find("<svg") != std::string::npos)
    {
        return Trimmed;
    }
    if (Trimmed.find("<path") != std::string::npos || Trimmed.find("<rect") != std::string::npos ||
        Trimmed.find("<circle") != std::string::npos || Trimmed.find("<polygon") != std::string::npos ||
        Trimmed.find("<g") != std::string::npos)
    {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\">" + Trimmed + "</svg>";
    }
    // Raw path data string ("M ... Z")
    return "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 512 512\"><path d=\"" + Trimmed + "\" fill=\"#EFF4CF\"/></svg>";
}

[[nodiscard]] bool ExecuteSvgExtraction(
    std::string_view                SvgSourceOrPath,
    bool                            SourceIsFilePath,
    float                           TargetBoxExtentPx,
    float                           CenterX,
    float                           CenterY,
    VulkanVectorRenderMethod&       Method,
    std::string*                    OutDiagnostic)
{
    VulkanVectorCanvas Canvas(&Method);
    auto* Picture = tvg::Picture::gen();
    if (Picture == nullptr)
    {
        if (OutDiagnostic != nullptr) *OutDiagnostic = "Failed to allocate ThorVG Picture.";
        return false;
    }

    tvg::Result LoadRes = tvg::Result::InvalidArguments;
    std::string OwnedSvg;
    if (SourceIsFilePath)
    {
        OwnedSvg = std::string(SvgSourceOrPath);
        LoadRes  = Picture->load(OwnedSvg.c_str());
    }
    else
    {
        OwnedSvg = NormalizeSvgInputIfFragment(SvgSourceOrPath);
        LoadRes  = Picture->load(OwnedSvg.c_str(), static_cast<uint32_t>(OwnedSvg.size()), "svg", nullptr, true);
    }

    if (LoadRes != tvg::Result::Success)
    {
        tvg::Paint::rel(Picture);
        if (OutDiagnostic != nullptr) *OutDiagnostic = "ThorVG SVG parser rejected SVG source.";
        return false;
    }

    float NatW = 512.0f, NatH = 512.0f;
    (void)Picture->size(&NatW, &NatH);
    const float MaxDim = std::max({ 1.0f, NatW, NatH });
    const float Scale  = (TargetBoxExtentPx > 1.0f) ? (TargetBoxExtentPx / MaxDim) : 1.0f;
    const float FinalW = std::max(1.0f, NatW * Scale);
    const float FinalH = std::max(1.0f, NatH * Scale);
    (void)Picture->size(FinalW, FinalH);
    (void)Picture->origin(0.5f, 0.5f);
    (void)Picture->translate(CenterX, CenterY);

    (void)Canvas.add(Picture);
    (void)Canvas.update();
    (void)Canvas.draw(true);
    (void)Canvas.sync();
    return true;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                  THORVG VECTOR FRONTEND
//------------------------------------------------------------------------------------------------------------------------

ThorVGVectorFrontend::ThorVGVectorFrontend() noexcept
{
    EnsureThorVGInitialized();
}

ThorVGVectorFrontend::~ThorVGVectorFrontend() noexcept
{
    ReleaseThorVGReference();
}

bool ThorVGVectorFrontend::RegisterTypefaceFile(const std::filesystem::path& FontPath)
{
    EnsureThorVGInitialized();
    const std::string PathStr = FontPath.string();
    const std::string StemStr = FontPath.stem().string();
    if (PathStr.empty() || !std::filesystem::exists(FontPath))
    {
        ReleaseThorVGReference();
        return false;
    }
    bool AlreadyLoaded = false;
    {
        std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
        AlreadyLoaded = (AccessLoadedFontKeys().count(PathStr) > 0u);
    }
    if (AlreadyLoaded)
    {
        ReleaseThorVGReference();
        return true;
    }

    std::vector<uint8_t> Bytes;
    if (!ReadFileBytesExact(FontPath, Bytes))
    {
        ReleaseThorVGReference();
        return false;
    }

    const tvg::Result ResPath = tvg::Text::load(
        PathStr.c_str(),
        reinterpret_cast<const char*>(Bytes.data()),
        static_cast<uint32_t>(Bytes.size()),
        "ttf",
        true);
    if (!StemStr.empty() && StemStr != PathStr)
    {
        (void)tvg::Text::load(
            StemStr.c_str(),
            reinterpret_cast<const char*>(Bytes.data()),
            static_cast<uint32_t>(Bytes.size()),
            "ttf",
            true);
    }

    stbtt_fontinfo StbCheck {};
    const int FontOff = stbtt_GetFontOffsetForIndex(Bytes.data(), 0);
    const bool StbValid = (FontOff >= 0 && stbtt_InitFont(&StbCheck, Bytes.data(), FontOff) != 0);

    const bool LoadedOk = (ResPath == tvg::Result::Success) || StbValid;
    if (LoadedOk)
    {
        std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
        AccessLoadedFontKeys().insert(PathStr);
        if (!StemStr.empty())
        {
            AccessLoadedFontKeys().insert(StemStr);
        }
        AccessFontByteCache()[PathStr] = Bytes;
        if (!StemStr.empty())
        {
            AccessFontByteCache()[StemStr] = std::move(Bytes);
        }
    }
    ReleaseThorVGReference();
    return LoadedOk;
}

bool ThorVGVectorFrontend::RegisterTypefaceMemory(
    std::string_view         FontKey,
    std::span<const uint8_t> FontBytes,
    std::string_view         FormatTag)
{
    if (FontKey.empty() || FontBytes.empty())
    {
        return false;
    }
    EnsureThorVGInitialized();
    const std::string KeyStr(FontKey);
    const std::string MimeStr(FormatTag);
    const tvg::Result Res = tvg::Text::load(
        KeyStr.c_str(),
        reinterpret_cast<const char*>(FontBytes.data()),
        static_cast<uint32_t>(FontBytes.size()),
        MimeStr.c_str(),
        true);

    stbtt_fontinfo StbCheck {};
    const int FontOff = stbtt_GetFontOffsetForIndex(FontBytes.data(), 0);
    const bool StbValid = (FontOff >= 0 && stbtt_InitFont(&StbCheck, FontBytes.data(), FontOff) != 0);
    const bool LoadedOk = (Res == tvg::Result::Success) || StbValid;
    if (LoadedOk)
    {
        std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
        AccessLoadedFontKeys().insert(KeyStr);
        AccessFontByteCache()[KeyStr] = std::vector<uint8_t>(FontBytes.begin(), FontBytes.end());
    }
    ReleaseThorVGReference();
    return LoadedOk;
}

bool ThorVGVectorFrontend::CompileSvgToGpuShapes(
    std::string_view                   SvgSourceOrPath,
    bool                               SourceIsFilePath,
    float                              TargetBoxExtentPx,
    float                              CenterX,
    float                              CenterY,
    std::vector<ExtractedVectorShape>& OutShapes,
    std::string*                       OutDiagnostic) const
{
    OutShapes.clear();
    auto* Method = new VulkanVectorRenderMethod(4096u, 4096u);
    Method->ref();
    const bool Ok = ExecuteSvgExtraction(SvgSourceOrPath, SourceIsFilePath, TargetBoxExtentPx, CenterX, CenterY, *Method, OutDiagnostic);
    if (Ok)
    {
        OutShapes = std::move(Method->EmittedShapes);
    }
    if (Method->unref() == 0u)
    {
        delete Method;
    }
    return Ok && !OutShapes.empty();
}

bool ThorVGVectorFrontend::ExtractSvgToEditableLayers(
    std::string_view                SvgSourceOrPath,
    bool                            SourceIsFilePath,
    float                           TargetBoxExtentPx,
    float                           DropCenterX,
    float                           DropCenterY,
    std::vector<ExtractedSvgLayer>& OutLayers,
    std::string*                    OutDiagnostic) const
{
    OutLayers.clear();
    auto* Method = new VulkanVectorRenderMethod(4096u, 4096u);
    Method->ref();
    const bool Ok = ExecuteSvgExtraction(SvgSourceOrPath, SourceIsFilePath, TargetBoxExtentPx, DropCenterX, DropCenterY, *Method, OutDiagnostic);
    if (Ok)
    {
        OutLayers = std::move(Method->EmittedLayers);
    }
    if (Method->unref() == 0u)
    {
        delete Method;
    }
    return Ok && !OutLayers.empty();
}

bool ThorVGVectorFrontend::CompileTextToGpuShapes(
    const std::filesystem::path&       FontFilePath,
    std::string_view                   FontRegisteredKey,
    std::string_view                   Utf8Text,
    float                              FontSizePx,
    float                              CenterX,
    float                              CenterY,
    ColorRgba                          FillTint,
    float                              StrokeWidthPx,
    ColorRgba                          StrokeTint,
    std::vector<ExtractedVectorShape>& OutShapes,
    float*                             OutWidthPx,
    float*                             OutHeightPx,
    std::string*                       OutDiagnostic) const
{
    OutShapes.clear();
    if (Utf8Text.empty())
    {
        return true;
    }

    std::string FontKey(FontRegisteredKey);
    if (!FontFilePath.empty() && std::filesystem::exists(FontFilePath))
    {
        if (!RegisterTypefaceFile(FontFilePath))
        {
            if (OutDiagnostic != nullptr) *OutDiagnostic = "Failed to load TTF/OTF typeface: " + FontFilePath.string();
            return false;
        }
        FontKey = FontFilePath.string();
    }

    auto* Method = new VulkanVectorRenderMethod(4096u, 4096u);
    Method->ref();
    {
        VulkanVectorCanvas Canvas(Method);

        auto* Txt = tvg::Text::gen();
        if (Txt == nullptr)
        {
            if (Method->unref() == 0u) delete Method;
            return false;
        }

        if (!FontKey.empty())
        {
            // Try exact key first, then filename stem if registered by stem
            if (Txt->font(FontKey.c_str()) != tvg::Result::Success && !FontFilePath.empty())
            {
                const std::string Stem = FontFilePath.stem().string();
                (void)Txt->font(Stem.c_str());
            }
        }
        (void)Txt->size(std::max(6.0f, FontSizePx));
        const std::string OwnedText(Utf8Text);
        (void)Txt->text(OwnedText.c_str());
        (void)Txt->fill(FillTint.Red, FillTint.Green, FillTint.Blue);
        if (StrokeWidthPx > 0.01f)
        {
            (void)Txt->outline(StrokeWidthPx, StrokeTint.Red, StrokeTint.Green, StrokeTint.Blue);
        }

        (void)Canvas.add(Txt);
        (void)Canvas.update();
        (void)Canvas.draw(true);
        (void)Canvas.sync();
    }

    if (Method->EmittedShapes.empty())
    {
        std::vector<uint8_t> CachedBytes;
        {
            std::lock_guard<std::mutex> Lock(AccessThorVGInitMutex());
            const auto& ByteCache = AccessFontByteCache();
            if (auto It = ByteCache.find(FontKey); It != ByteCache.end())
            {
                CachedBytes = It->second;
            }
            else if (!FontFilePath.empty())
            {
                if (auto ItStem = ByteCache.find(FontFilePath.stem().string()); ItStem != ByteCache.end())
                {
                    CachedBytes = ItStem->second;
                }
            }
        }
        if (!CachedBytes.empty())
        {
            (void)BuildStbFallbackShapes(CachedBytes, Utf8Text, FontSizePx, FillTint, StrokeWidthPx, StrokeTint, *Method);
        }
    }

    if (Method->EmittedShapes.empty())
    {
        if (Method->unref() == 0u) delete Method;
        if (OutDiagnostic != nullptr) *OutDiagnostic = "ThorVG Text produced no outlines for font key: " + FontKey;
        return false;
    }

    float MinX = 1.0e30f, MinY = 1.0e30f, MaxX = -1.0e30f, MaxY = -1.0e30f;
    for (const auto& ShapeEntry : Method->EmittedShapes)
    {
        MinX = std::min(MinX, ShapeEntry.BoundsMinX);
        MinY = std::min(MinY, ShapeEntry.BoundsMinY);
        MaxX = std::max(MaxX, ShapeEntry.BoundsMaxX);
        MaxY = std::max(MaxY, ShapeEntry.BoundsMaxY);
    }

    const float CurCx = 0.5f * (MinX + MaxX);
    const float CurCy = 0.5f * (MinY + MaxY);
    const float Dx    = CenterX - CurCx;
    const float Dy    = CenterY - CurCy;

    OutShapes = std::move(Method->EmittedShapes);
    if (Method->unref() == 0u)
    {
        delete Method;
    }
    for (auto& ShapeEntry : OutShapes)
    {
        ShapeEntry.FillTint.Alpha = FillTint.Alpha;
        for (auto& V : ShapeEntry.FillFanTriangles)
        {
            V.X += Dx;
            V.Y += Dy;
        }
        for (auto& V : ShapeEntry.StrokeTriangles)
        {
            V.X += Dx;
            V.Y += Dy;
        }
        ShapeEntry.BoundsMinX += Dx;
        ShapeEntry.BoundsMaxX += Dx;
        ShapeEntry.BoundsMinY += Dy;
        ShapeEntry.BoundsMaxY += Dy;
    }

    if (OutWidthPx != nullptr)  *OutWidthPx  = std::max(8.0f, MaxX - MinX);
    if (OutHeightPx != nullptr) *OutHeightPx = std::max(8.0f, MaxY - MinY);
    return true;
}

void ThorVGVectorFrontend::CompileCubicSubpathsToGpuShape(
    std::span<const VectorCubicSubpath> Subpaths,
    bool                                EvenOddFillRule,
    bool                                FillEnabled,
    ColorRgba                           FillTint,
    bool                                StrokeEnabled,
    float                               StrokeWidthPx,
    ColorRgba                           StrokeTint,
    VectorStrokeCapStyle                CapStyle,
    VectorStrokeJoinStyle               JoinStyle,
    ExtractedVectorShape&               OutShape) noexcept
{
    std::vector<std::vector<VectorVertex2D>> Polygons;
    std::vector<uint8_t>                     ClosedLoops;
    Polygons.reserve(Subpaths.size());
    ClosedLoops.reserve(Subpaths.size());

    for (const auto& Sub : Subpaths)
    {
        if (Sub.Knots.empty())
        {
            continue;
        }
        std::vector<VectorVertex2D> Flat;
        Flat.push_back({ Sub.Knots[0].AnchorX, Sub.Knots[0].AnchorY });

        const std::size_t SegCount = Sub.ClosedLoop ? Sub.Knots.size() : (Sub.Knots.size() - 1u);
        for (std::size_t I = 0u; I < SegCount; ++I)
        {
            const auto& K0 = Sub.Knots[I];
            const auto& K1 = Sub.Knots[(I + 1u) % Sub.Knots.size()];
            const VectorVertex2D P0 { K0.AnchorX, K0.AnchorY };
            const VectorVertex2D C1 { K0.OutHandleX, K0.OutHandleY };
            const VectorVertex2D C2 { K1.InHandleX, K1.InHandleY };
            const VectorVertex2D P3 { K1.AnchorX, K1.AnchorY };

            const float H1Len = std::hypot(C1.X - P0.X, C1.Y - P0.Y);
            const float H2Len = std::hypot(C2.X - P3.X, C2.Y - P3.Y);
            if (H1Len <= 1.0e-3f && H2Len <= 1.0e-3f)
            {
                Flat.push_back(P3);
            }
            else
            {
                SubdivideCubicAdaptive(P0, C1, C2, P3, 0.25f * 0.25f, 0u, Flat);
            }
        }
        if (Flat.size() >= 2u)
        {
            Polygons.push_back(std::move(Flat));
            ClosedLoops.push_back(Sub.ClosedLoop ? 1u : 0u);
        }
    }

    TessellatePolygonsToGpuShape(
        Polygons, ClosedLoops, EvenOddFillRule, FillEnabled, FillTint,
        StrokeEnabled, StrokeWidthPx, StrokeTint, CapStyle, JoinStyle, OutShape);
}

void ThorVGVectorFrontend::TessellatePolygonsToGpuShape(
    std::span<const std::vector<VectorVertex2D>> Polygons,
    std::span<const uint8_t>                     ClosedLoops,
    bool                                         EvenOddFillRule,
    bool                                         FillEnabled,
    ColorRgba                                    FillTint,
    bool                                         StrokeEnabled,
    float                                        StrokeWidthPx,
    ColorRgba                                    StrokeTint,
    VectorStrokeCapStyle                         CapStyle,
    VectorStrokeJoinStyle                        JoinStyle,
    ExtractedVectorShape&                        OutShape) noexcept
{
    OutShape = ExtractedVectorShape{};
    OutShape.EvenOddFillRule = EvenOddFillRule;
    OutShape.FillEnabled     = FillEnabled;
    OutShape.FillTint        = FillTint;
    OutShape.StrokeEnabled   = StrokeEnabled && StrokeWidthPx > 1.0e-4f;
    OutShape.StrokeWidthPx   = StrokeWidthPx;
    OutShape.StrokeTint      = StrokeTint;

    float MinX = 1.0e30f, MinY = 1.0e30f, MaxX = -1.0e30f, MaxY = -1.0e30f;
    if (FillEnabled)
    {
        for (std::size_t I = 0u; I < Polygons.size(); ++I)
        {
            const bool Closed = (I < ClosedLoops.size()) ? ClosedLoops[I] : true;
            if (Closed)
            {
                AppendFanTriangles(Polygons[I], OutShape.FillFanTriangles, MinX, MinY, MaxX, MaxY);
            }
        }
    }
    if (OutShape.StrokeEnabled)
    {
        BuildStrokeTrianglesForPolygons(
            Polygons, ClosedLoops, StrokeWidthPx, CapStyle, JoinStyle,
            OutShape.StrokeTriangles, MinX, MinY, MaxX, MaxY);
    }
    if (MinX <= MaxX && MinY <= MaxY)
    {
        OutShape.BoundsMinX = MinX;
        OutShape.BoundsMinY = MinY;
        OutShape.BoundsMaxX = MaxX;
        OutShape.BoundsMaxY = MaxY;
    }
}

} // namespace Frontier::GeometricRaster
