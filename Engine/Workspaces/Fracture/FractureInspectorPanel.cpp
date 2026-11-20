/*============================================================================================================================================
                                                        FRACTUREINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureInspectorPanel matching Port 8091 HTML Fracture Editor & FrontierEditor Project Zero styling 1:1

#include "Engine/Workspaces/Fracture/FractureInspectorPanel.h"

#include "Engine/Components/Controls/ExclusiveSelector.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/Panes/StudioPaneAppearance.h"
#include "Engine/Workspaces/Fracture/FractureIcons.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace StudioAppearance;
using GeometrySpace::Fracture::FractureExecutionMode;
using GeometrySpace::Fracture::FractureMaterialRepository;
using GeometrySpace::Fracture::FractureSolverMethod;

[[nodiscard]] ColorRgba MaterialLinearToSrgbSwatch(Mathematics::Scalar3D Lin) noexcept
{
    auto ToSrgb = [](float V) -> uint8_t
    {
        const float C = std::clamp(V, 0.0f, 1.0f);
        const float S = (C <= 0.0031308f) ? (12.92f * C) : (1.055f * std::pow(C, 1.0f / 2.4f) - 0.055f);
        return static_cast<uint8_t>(std::clamp(std::lround(S * 255.0f), 0l, 255l));
    };
    return { ToSrgb(Lin.X), ToSrgb(Lin.Y), ToSrgb(Lin.Z), 255u };
}

void DrawHeroChart(
    PixelSpace&                     Surface,
    Rectangle2D                     Card,
    const FractureWorkspaceSession& Session,
    int32_t                         SelectedFragment) noexcept
{
    Surface.FillRectangle(Card, ColorRgba { 24u, 26u, 30u, 255u }, 22.0f);
    Surface.StrokeRectangle(Card, ColorRgba { 255u, 255u, 255u, 22u }, 22.0f, 1.0f);

    const bool IsSim = (Session.ExecutionPhase != ExecutionStripPhase::Edit) && !Session.KineticCurveSamples.empty();
    Surface.Text({ Card.X + 16.0f, Card.Y + 13.0f }, 10.2f, ColorRgba { 166u, 182u, 204u, 255u },
        IsSim ? (SelectedFragment >= 0 ? "KINETIC · FRAGMENT" : "KINETIC ENERGY") : "ENERGY SHARE");

    // Legend top-right
    float Lx = Card.Right() - 14.0f;
    if (!IsSim)
    {
        Lx -= 54.0f;
        Surface.StrokeLine({ Lx, Card.Y + 18.0f }, { Lx + 10.0f, Card.Y + 18.0f }, ColorRgba { 255u, 255u, 255u, 185u }, 1.2f);
        Surface.Text({ Lx + 13.0f, Card.Y + 13.0f }, 9.8f, ColorRgba { 166u, 174u, 184u, 255u }, "Threshold");
        Lx -= 38.0f;
        Surface.StrokeCircle({ Lx + 3.0f, Card.Y + 18.0f }, 3.4f, ColorRgba { 79u, 143u, 217u, 255u }, 1.3f);
        Surface.Text({ Lx + 9.0f, Card.Y + 13.0f }, 9.8f, ColorRgba { 166u, 174u, 184u, 255u }, "Held");
        Lx -= 56.0f;
        Surface.FillCircle({ Lx + 3.0f, Card.Y + 18.0f }, 3.4f, ColorRgba { 255u, 138u, 61u, 255u });
        Surface.Text({ Lx + 9.0f, Card.Y + 13.0f }, 9.8f, ColorRgba { 166u, 174u, 184u, 255u }, "Released");
    }

    // Big metric + subtitle
    char BigBuf[48] {}, SubLeft[48] {}, SubRight[32] {};
    if (SelectedFragment >= 0 && static_cast<size_t>(SelectedFragment) < Session.PartitionResult.Fragments.size())
    {
        const auto& Frag = Session.PartitionResult.Fragments[static_cast<size_t>(SelectedFragment)];
        std::snprintf(BigBuf, sizeof(BigBuf), "%.1f kg · %s", Frag.MassKg, Session.QueryFragmentReleased(Frag.Index) ? "released" : "held");
        std::snprintf(SubLeft, sizeof(SubLeft), "RANK %u OF %zu BY DISTANCE", Frag.Index + 1u, Session.PartitionResult.Fragments.size());
        std::snprintf(SubRight, sizeof(SubRight), "%.0f J", Session.QueryFragmentReleaseShareJ(Frag.Index));
    }
    else
    {
        std::snprintf(BigBuf, sizeof(BigBuf), "%zu fragments", Session.PartitionResult.Fragments.size());
        const double KineticJ = std::max(0.0, (Session.ImpactEnergyJ - Session.PartitionResult.Receipt.SpentSurfaceEnergyJ) * Session.TransferRatio);
        std::snprintf(SubLeft, sizeof(SubLeft), "%.2f kJ IMPACT · %.0f J KINETIC", Session.ImpactEnergyJ / 1000.0, KineticJ);
        std::snprintf(SubRight, sizeof(SubRight), "%u RELEASED", Session.CountReleasedFragments());
    }
    Surface.Text({ Card.X + 16.0f, Card.Y + 32.0f }, 24.0f, ColorRgba { 244u, 244u, 242u, 255u }, BigBuf);
    Surface.Text({ Card.X + 16.0f, Card.Y + 64.0f }, 9.8f, ColorRgba { 150u, 158u, 168u, 255u }, SubLeft);
    Surface.TextRightAligned({ Card.X, Card.Y + 64.0f, Card.Width - 16.0f, 14.0f }, 9.8f, ColorRgba { 184u, 184u, 182u, 255u }, SubRight);

    // Plot area inside card
    const Rectangle2D Plot { Card.X + 16.0f, Card.Y + 94.0f, Card.Width - 62.0f, Card.Height - 124.0f };
    for (int G = 0; G <= 3; ++G)
    {
        const float Gy = Plot.Y + (Plot.Height * static_cast<float>(G)) / 3.0f;
        Surface.StrokeLine({ Plot.X, Gy }, { Plot.Right(), Gy },
            (G == 3) ? ColorRgba { 255u, 255u, 255u, 42u } : ColorRgba { 255u, 255u, 255u, 18u }, 1.0f);
    }

    if (!IsSim)
    {
        const size_t N = Session.PartitionResult.Fragments.size();
        if (N >= 2u)
        {
            std::vector<float> Energies(N);
            float PeakJ = 1.0f;
            for (size_t I = 0u; I < N; ++I)
            {
                Energies[I] = Session.QueryFragmentReleaseShareJ(static_cast<uint32_t>(I));
                PeakJ = std::max(PeakJ, Energies[I]);
            }
            std::sort(Energies.begin(), Energies.end(), std::greater<float>());

            auto MapY = [&](float V) -> float
            {
                const float Norm = std::clamp(std::log10(std::max(0.2f, V) / 0.2f) / std::max(0.5f, std::log10(PeakJ / 0.2f)), 0.0f, 1.0f);
                return Plot.Bottom() - Norm * Plot.Height;
            };

            const uint32_t DrawCount = static_cast<uint32_t>(std::min<size_t>(N, 48u));

            // Gradient area fill under the ranked energy curve
            for (uint32_t K = 1u; K < DrawCount; ++K)
            {
                const float U0 = static_cast<float>(K - 1u) / static_cast<float>(DrawCount - 1u);
                const float U1 = static_cast<float>(K)      / static_cast<float>(DrawCount - 1u);
                const float X0 = Plot.X + 4.0f + U0 * (Plot.Width - 8.0f);
                const float X1 = Plot.X + 4.0f + U1 * (Plot.Width - 8.0f);
                const float Y0 = MapY(Energies[K - 1u]);
                const float Y1 = MapY(Energies[K]);
                uint8_t R = 0u, G = 0u, B = 0u;
                FractureViewportRenderer::SampleEnergySwatchSrgb(1.0f - (U0 + U1) * 0.5f, R, G, B);
                const std::array<Point2D, 4> Quad {{
                    { X0, Y0 }, { X1, Y1 }, { X1, Plot.Bottom() }, { X0, Plot.Bottom() }
                }};
                Surface.FillPolygon(Quad, ColorRgba { R, G, B, 46u });
            }

            // Release threshold dashed line + pill
            const float RelY = std::clamp(MapY(static_cast<float>(Session.ReleaseThresholdJ)), Plot.Y + 10.0f, Plot.Bottom() - 10.0f);
            for (float Dx = Plot.X; Dx < Plot.Right(); Dx += 9.0f)
            {
                Surface.StrokeLine({ Dx, RelY }, { std::min(Plot.Right(), Dx + 5.0f), RelY }, ColorRgba { 255u, 255u, 255u, 185u }, 1.1f);
            }
            char RelTag[28] {};
            std::snprintf(RelTag, sizeof(RelTag), "release %.1f J", Session.ReleaseThresholdJ);
            const float PillW = Surface.MeasureTextWidth(9.6f, RelTag) + 14.0f;
            const Rectangle2D RelPill { Plot.X + 4.0f, RelY - 9.0f, PillW, 18.0f };
            Surface.FillRectangle(RelPill, ColorRgba { 16u, 18u, 21u, 240u }, 9.0f);
            Surface.StrokeRectangle(RelPill, ColorRgba { 255u, 255u, 255u, 44u }, 9.0f, 0.9f);
            Surface.TextCentered(RelPill, 9.6f, ColorRgba { 242u, 242u, 240u, 255u }, RelTag);

            // Curve line + colored nodes
            Point2D Prev {};
            for (uint32_t K = 0u; K < DrawCount; ++K)
            {
                const float U = static_cast<float>(K) / static_cast<float>(DrawCount - 1u);
                const float Px = Plot.X + 4.0f + U * (Plot.Width - 8.0f);
                const float Py = MapY(Energies[K]);
                if (K > 0u)
                {
                    Surface.StrokeLine(Prev, { Px, Py }, ColorRgba { 255u, 255u, 255u, 145u }, 1.25f);
                }
                uint8_t R = 0u, G = 0u, B = 0u;
                FractureViewportRenderer::SampleEnergySwatchSrgb(1.0f - U, R, G, B);
                const bool Rel = (Energies[K] >= static_cast<float>(Session.ReleaseThresholdJ));
                if (Rel)
                {
                    Surface.FillCircle({ Px, Py }, 2.8f, { R, G, B, 255u });
                }
                else
                {
                    Surface.FillCircle({ Px, Py }, 2.8f, ColorRgba { 20u, 21u, 24u, 255u });
                    Surface.StrokeCircle({ Px, Py }, 2.8f, { R, G, B, 255u }, 1.3f);
                }
                Prev = { Px, Py };
            }
        }
        Surface.Text({ Plot.Right() + 7.0f, Plot.Y + 2.0f }, 9.6f, ColorRgba { 140u, 148u, 158u, 255u }, "10 J");
        Surface.Text({ Plot.Right() + 7.0f, Plot.Y + Plot.Height * 0.52f }, 9.6f, ColorRgba { 140u, 148u, 158u, 255u }, "1 J");
        Surface.Text({ Plot.X, Card.Bottom() - 20.0f }, 9.6f, ColorRgba { 132u, 138u, 146u, 255u }, "highest share");
        char MidLbl[32] {};
        std::snprintf(MidLbl, sizeof(MidLbl), "%zu fragments ranked", N);
        Surface.TextCentered({ Plot.X, Card.Bottom() - 20.0f, Plot.Width, 12.0f }, 9.6f, ColorRgba { 132u, 138u, 146u, 255u }, MidLbl);
        Surface.TextRightAligned({ Plot.X, Card.Bottom() - 20.0f, Plot.Width, 12.0f }, 9.6f, ColorRgba { 132u, 138u, 146u, 255u }, "lowest");
    }
    else
    {
        const auto& Samples = Session.KineticCurveSamples;
        float Peak = 1.0f;
        for (float V : Samples) Peak = std::max(Peak, V);
        Point2D Prev {};
        for (size_t K = 0u; K < Samples.size(); ++K)
        {
            const float U = (Samples.size() > 1u) ? (static_cast<float>(K) / static_cast<float>(Samples.size() - 1u)) : 1.0f;
            const float Px = Plot.X + U * Plot.Width;
            const float Py = Plot.Bottom() - std::clamp(Samples[K] / (Peak * 1.08f), 0.0f, 1.0f) * Plot.Height;
            if (K > 0u)
            {
                const std::array<Point2D, 4> Quad {{ Prev, { Px, Py }, { Px, Plot.Bottom() }, { Prev.X, Plot.Bottom() } }};
                Surface.FillPolygon(Quad, ColorRgba { 255u, 140u, 64u, 38u });
                Surface.StrokeLine(Prev, { Px, Py }, ColorRgba { 244u, 244u, 242u, 235u }, 1.6f);
            }
            Prev = { Px, Py };
        }
        Surface.FillCircle(Prev, 4.0f, ColorRgba { 255u, 255u, 255u, 255u });
        Surface.StrokeCircle(Prev, 4.0f, ColorRgba { 20u, 21u, 24u, 255u }, 1.6f);
        Surface.Text({ Plot.X, Card.Bottom() - 20.0f }, 9.6f, ColorRgba { 132u, 138u, 146u, 255u }, "0 s");
        Surface.TextRightAligned({ Plot.X, Card.Bottom() - 20.0f, Plot.Width, 12.0f }, 9.6f, ColorRgba { 132u, 138u, 146u, 255u }, "4.0 s");
    }
}

void DrawKpiGrid(
    PixelSpace&                     Surface,
    float                           X,
    float                           Y,
    float                           Width,
    const FractureWorkspaceSession& Session,
    int32_t                         SelectedFragment) noexcept
{
    const float Gap = 8.0f;
    const float W   = (Width - Gap) * 0.5f;
    const float H   = 74.0f;
    const auto& Rec = Session.PartitionResult.Receipt;

    auto DrawTile = [&](uint32_t Col, uint32_t Row, const char* Big, const char* DimPart, const char* Unit, const char* Label)
    {
        const Rectangle2D Tile { X + static_cast<float>(Col) * (W + Gap), Y + static_cast<float>(Row) * (H + Gap), W, H };
        Surface.FillRectangle(Tile, ColorRgba { 23u, 23u, 24u, 255u }, 17.0f);
        Surface.StrokeRectangle(Tile, ColorRgba { 255u, 255u, 255u, 20u }, 17.0f, 1.0f);
        FractureIcons::DrawArrowNeGlyph(Surface, { Tile.Right() - 22.0f, Tile.Y + 10.0f, 13.0f, 13.0f }, ColorRgba { 118u, 118u, 116u, 255u });

        float Nx = Tile.X + 14.0f;
        const float Ny = Tile.Y + 13.0f;
        Surface.Text({ Nx, Ny }, 24.0f, ColorRgba { 242u, 242u, 240u, 255u }, Big);
        Nx += Surface.MeasureTextWidth(24.0f, Big);
        if (DimPart[0] != '\0')
        {
            Surface.Text({ Nx, Ny + 4.0f }, 19.0f, ColorRgba { 126u, 126u, 124u, 255u }, DimPart);
            Nx += Surface.MeasureTextWidth(19.0f, DimPart);
        }
        if (Unit[0] != '\0')
        {
            Surface.Text({ Nx + 3.0f, Ny + 10.0f }, 11.5f, ColorRgba { 154u, 154u, 152u, 255u }, Unit);
        }
        Surface.Text({ Tile.X + 14.0f, Tile.Bottom() - 20.0f }, 9.8f, ColorRgba { 134u, 134u, 132u, 255u }, Label);
    };

    if (SelectedFragment >= 0 && static_cast<size_t>(SelectedFragment) < Session.PartitionResult.Fragments.size())
    {
        const auto& Frag = Session.PartitionResult.Fragments[static_cast<size_t>(SelectedFragment)];
        char MassB[16] {}, VolB[16] {}, EngB[16] {}, DistB[16] {};
        std::snprintf(MassB, sizeof(MassB), "%.2f", Frag.MassKg);
        std::snprintf(VolB, sizeof(VolB), "%.2f", Frag.VolumeM3 * 1000.0);
        std::snprintf(EngB, sizeof(EngB), "%.0f", Session.QueryFragmentReleaseShareJ(Frag.Index));
        const double D = std::hypot(Frag.Centroid.X - Session.ImpactPointLocal.X,
            std::hypot(Frag.Centroid.Y - Session.ImpactPointLocal.Y, Frag.Centroid.Z - Session.ImpactPointLocal.Z));
        std::snprintf(DistB, sizeof(DistB), "%.2f", D);
        DrawTile(0u, 0u, MassB, "", "kg", "MASS");
        DrawTile(1u, 0u, VolB,  "", "L",  "VOLUME");
        DrawTile(0u, 1u, EngB,  "", "J",  "ENERGY SHARE");
        DrawTile(1u, 1u, DistB, "", "m",  "FROM IMPACT");
    }
    else
    {
        char CellB[16] {}, CeilD[16] {}, AreaB[16] {}, MassB[16] {}, BakeB[16] {}, BakeLbl[32] {};
        std::snprintf(CellB, sizeof(CellB), "%zu", Session.PartitionResult.Fragments.size());
        std::snprintf(CeilD, sizeof(CeilD), " / %u", Session.FragmentCeiling);
        std::snprintf(AreaB, sizeof(AreaB), "%.2f", Rec.TotalCrackAreaM2);
        const double TotalMassKg = Session.QueryExactVolumeM3() * Session.ActiveMaterialSpec().DensityKgPerM3;
        std::snprintf(MassB, sizeof(MassB), "%.0f", TotalMassKg);
        std::snprintf(BakeB, sizeof(BakeB), "%.0f", Session.LastTimings.TotalMs);
        std::snprintf(BakeLbl, sizeof(BakeLbl), "BAKE · %zu CELLS", Session.PartitionResult.Fragments.size());
        DrawTile(0u, 0u, CellB, CeilD, "", "CELLS / CEILING");
        DrawTile(1u, 0u, AreaB, "", "m2", "NEW CRACK AREA");
        DrawTile(0u, 1u, MassB, "", "kg", "MASS");
        DrawTile(1u, 1u, BakeB, "", "ms", BakeLbl);
    }
}

} // namespace

void FractureInspectorPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     WheelDeltaY) noexcept
{
    if (Bounds.Width < 240.0f || Bounds.Height < 220.0f)
    {
        return;
    }

    const ColorRgba DockBg { 18u, 18u, 18u, 255u };
    Surface.FillRectangle(Bounds, DockBg, 0.0f);

    const float PadX   = 22.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    constexpr float FootH = 45.0f;
    const Rectangle2D FootRect   { Bounds.X, Bounds.Bottom() - FootH, Bounds.Width, FootH };
    const Rectangle2D ScrollRect { Bounds.X, Bounds.Y, Bounds.Width, std::max(100.0f, FootRect.Y - Bounds.Y) };

    if (ControlAppearance::QueryPointerOver(ScrollRect, Pointer) && std::abs(WheelDeltaY) > 0.001f)
    {
        Session.InspectorScrollY = std::max(0.0f, Session.InspectorScrollY - WheelDeltaY * 32.0f);
    }

    Surface.PushClip(ScrollRect);
    const float X = Bounds.X + PadX;
    float Cy = Bounds.Y + 22.0f - Session.InspectorScrollY;

    const auto& Prims = FractureWorkspaceSession::InspectPrimitives();
    const auto& Prim  = Prims[static_cast<uint32_t>(Session.ActiveShape) % FractureWorkspaceSession::PrimitiveCount];
    const int32_t SelFrag = (Session.RenderOptions.SelectedFragmentIndex >= 0 &&
                             static_cast<size_t>(Session.RenderOptions.SelectedFragmentIndex) < Session.PartitionResult.Fragments.size())
                            ? Session.RenderOptions.SelectedFragmentIndex : -1;

    // 1. Inspector Heading (102px): Breadcrumb + [Reroll seed / Back] + Large Title + Subtitle
    {
        char Crumb[80] {};
        if (SelFrag >= 0)
        {
            std::snprintf(Crumb, sizeof(Crumb), "Inspector / Fragments / %s",
                Session.PartitionResult.Fragments[static_cast<size_t>(SelFrag)].Token.c_str());
        }
        else
        {
            std::snprintf(Crumb, sizeof(Crumb), "Inspector / Fracture / %s", Prim.Title);
        }
        Surface.Text({ X, Cy + 6.0f }, 9.8f, ColorRgba { 156u, 156u, 154u, 255u }, Crumb);

        const char* TopBtnLabel = (SelFrag >= 0) ? "Back to Solid" : "Reroll seed";
        const float BtnW = 102.0f;
        const Rectangle2D TopBtn { X + InnerW - BtnW, Cy, BtnW, 28.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(TopBtn, Pointer);
        Surface.StrokeRectangle(TopBtn, Hov ? ColorRgba { 98u, 98u, 98u, 255u } : ColorRgba { 72u, 72u, 72u, 255u }, 14.0f, 1.0f);
        if (SelFrag < 0)
        {
            FractureIcons::DrawDiceGlyph(Surface, { TopBtn.X + 10.0f, TopBtn.Center().Y - 6.5f, 13.0f, 13.0f }, ColorRgba { 176u, 176u, 174u, 255u });
        }
        Surface.Text({ TopBtn.X + (SelFrag >= 0 ? 14.0f : 27.0f), TopBtn.Y + 8.0f }, 10.2f,
            Hov ? ColorRgba { 236u, 236u, 234u, 255u } : ColorRgba { 176u, 176u, 174u, 255u }, TopBtnLabel);
        if (Hov && Pointer.Pressed)
        {
            if (SelFrag >= 0)
            {
                Session.RenderOptions.SelectedFragmentIndex = -1;
            }
            else
            {
                Session.Seed = (Session.Seed * 1664525u + 1013904223u) % 9999u + 1u;
                Session.RebuildFracturePartition();
            }
        }

        if (SelFrag >= 0)
        {
            const auto& Frag = Session.PartitionResult.Fragments[static_cast<size_t>(SelFrag)];
            Surface.Text({ X, Cy + 28.0f }, 24.0f, ColorRgba { 232u, 232u, 230u, 255u }, Frag.Token.c_str());
            char Sub[80] {};
            std::snprintf(Sub, sizeof(Sub), "Piece of %s · %s", Prim.Title, Session.ActiveMaterialSpec().ShortLabel);
            Surface.Text({ X, Cy + 62.0f }, 9.6f, ColorRgba { 154u, 154u, 152u, 255u }, Sub);
        }
        else
        {
            Surface.Text({ X, Cy + 28.0f }, 24.0f, ColorRgba { 232u, 232u, 230u, 255u }, Prim.Title);
            char Sub[80] {};
            std::snprintf(Sub, sizeof(Sub), "Analytical primitive · %s · %s",
                Session.ActiveMaterialSpec().ShortLabel, Session.FormatPrimitiveSubtitle().c_str());
            Surface.Text({ X, Cy + 62.0f }, 9.6f, ColorRgba { 154u, 154u, 152u, 255u }, Sub);
        }
    }
    Cy += 84.0f;

    // 2. Hero Chart (228px)
    DrawHeroChart(Surface, { X, Cy, InnerW, 228.0f }, Session, SelFrag);
    Cy += 238.0f;

    // 3. 2x2 KPI Grid (156px)
    DrawKpiGrid(Surface, X, Cy, InnerW, Session, SelFrag);
    Cy += 170.0f;

    auto DrawSectionHeader = [&](uint32_t SecIdx, const char* Title, const char* Note) -> bool
    {
        const Rectangle2D Head { X, Cy, InnerW, 22.0f };
        const bool Hov = ControlAppearance::QueryPointerOver(Head, Pointer) && ScrollRect.Contains({ Pointer.X, Pointer.Y });
        Surface.Text({ Head.X, Head.Y + 4.0f }, 10.4f, ColorRgba { 174u, 174u, 172u, 255u }, Title);
        Surface.TextRightAligned({ Head.X, Head.Y + 4.0f, Head.Width - 18.0f, 14.0f }, 9.8f, ColorRgba { 132u, 132u, 130u, 255u }, Note);
        if (Session.SectionExpanded[SecIdx])
        {
            FractureIcons::DrawChevronDownGlyph(Surface, { Head.Right() - 13.0f, Head.Y + 4.0f, 13.0f, 13.0f }, ColorRgba { 95u, 95u, 93u, 255u });
        }
        else
        {
            FractureIcons::DrawChevronRightGlyph(Surface, { Head.Right() - 13.0f, Head.Y + 4.0f, 13.0f, 13.0f }, ColorRgba { 95u, 95u, 93u, 255u });
        }
        if (Hov && Pointer.Pressed)
        {
            Session.SectionExpanded[SecIdx] = !Session.SectionExpanded[SecIdx];
        }
        Cy += 28.0f;
        return Session.SectionExpanded[SecIdx];
    };

    auto DrawScalarField = [&](int32_t SliderId, const char* Label, const char* NumTxt, const char* Unit,
                               float MinV, float MaxV, float Val) -> float
    {
        Surface.Text({ X, Cy }, 10.2f, ColorRgba { 163u, 163u, 161u, 255u }, Label);
        Cy += 16.0f;
        const Rectangle2D RowRect { X, Cy, InnerW, 30.0f };
        float Updated = Val;
        const ControlContact Sc = ScalarSlider::ConstructWithReadout(
            Surface, RowRect, NumTxt, Unit, MinV, MaxV, Val,
            Session.ActiveSliderId == SliderId, Pointer, Updated);
        if (Pointer.Pressed && Sc.Hovered) Session.ActiveSliderId = SliderId;
        if (!Pointer.Down && Session.ActiveSliderId == SliderId) Session.ActiveSliderId = -1;
        Cy += 44.0f;
        return Updated;
    };

    if (SelFrag >= 0)
    {
        // Fragment Inspector View
        const auto& Frag = Session.PartitionResult.Fragments[static_cast<size_t>(SelFrag)];
        if (DrawSectionHeader(0u, "RELEASE", "Per-fragment override"))
        {
            static constexpr std::array<const char*, 3> OvLabels {{ "Auto", "Hold", "Release" }};
            const Rectangle2D Bar { X, Cy, InnerW, 28.0f };
            Surface.FillRectangle(Bar, ColorRgba { 24u, 24u, 25u, 255u }, 14.0f);
            Surface.StrokeRectangle(Bar, ColorRgba { 255u, 255u, 255u, 20u }, 14.0f, 1.0f);
            const float SegW = (Bar.Width - 6.0f) / 3.0f;
            for (uint32_t K = 0u; K < 3u; ++K)
            {
                const Rectangle2D Seg { Bar.X + 3.0f + static_cast<float>(K) * SegW, Bar.Y + 3.0f, SegW, Bar.Height - 6.0f };
                const bool Act = (Session.FragmentOverrideMode == static_cast<int32_t>(K));
                const bool Hov = ControlAppearance::QueryPointerOver(Seg, Pointer);
                if (Act || Hov)
                {
                    Surface.FillRectangle(Seg, Act ? ColorRgba { 46u, 46u, 48u, 255u } : ColorRgba { 34u, 34u, 36u, 255u }, 11.0f);
                }
                Surface.TextCentered(Seg, 9.2f, Act ? ColorRgba { 240u, 240u, 238u, 255u } : ColorRgba { 145u, 145u, 143u, 255u }, OvLabels[K]);
                if (Hov && Pointer.Pressed)
                {
                    Session.FragmentOverrideMode = static_cast<int32_t>(K);
                }
            }
            Cy += 36.0f;
        }
        if (DrawSectionHeader(1u, "GEOMETRY", "Closed convex cell"))
        {
            const Rectangle2D Card { X, Cy, InnerW, 114.0f };
            Surface.FillRectangle(Card, ColorRgba { 32u, 32u, 32u, 255u }, 18.0f);
            Surface.StrokeRectangle(Card, ColorRgba { 44u, 44u, 44u, 255u }, 18.0f, 1.0f);
            auto Row = [&](float Ry, const char* K, const char* V)
            {
                Surface.Text({ Card.X + 14.0f, Ry }, 10.0f, ColorRgba { 133u, 133u, 131u, 255u }, K);
                Surface.TextRightAligned({ Card.X, Ry, Card.Width - 14.0f, 12.0f }, 10.0f, ColorRgba { 212u, 212u, 210u, 255u }, V);
            };
            char F1[24] {}, F2[36] {}, F3[24] {}, F4[24] {};
            std::snprintf(F1, sizeof(F1), "%zu", Frag.Polyhedron.Faces.size());
            std::snprintf(F2, sizeof(F2), "%.0f × %.0f × %.0f cm", Frag.Span.X * 100.0f, Frag.Span.Y * 100.0f, Frag.Span.Z * 100.0f);
            std::snprintf(F3, sizeof(F3), "%.0f cm2", Frag.CrackAreaM2 * 1.0e4);
            std::snprintf(F4, sizeof(F4), "%.3f L", Frag.VolumeM3 * 1000.0);
            Row(Card.Y + 14.0f, "Faces", F1);
            Row(Card.Y + 38.0f, "Bounds", F2);
            Row(Card.Y + 62.0f, "Crack surface", F3);
            Row(Card.Y + 86.0f, "Volume", F4);
            Cy += 126.0f;
        }
    }
    else
    {
        // 4. FRACTURE METHOD (Baked vs Realtime cards + 6-Algorithm grid)
        if (DrawSectionHeader(0u, "FRACTURE METHOD",
                Session.MethodMode == FractureMethodMode::Baked ? "Pre-fractured in the editor" : "Cut on impact within budget"))
        {
            auto DrawMethodCard = [&](FractureMethodMode Mode, const char* Title, const char* Lead,
                                      const char* K1, const char* V1,
                                      const char* K2, const char* V2, bool V2Good,
                                      const char* K3, const char* V3,
                                      const char* K4, const char* V4)
            {
                const bool Active = (Session.MethodMode == Mode);
                const Rectangle2D Card { X, Cy, InnerW, 128.0f };
                const bool Hov = ControlAppearance::QueryPointerOver(Card, Pointer);
                Surface.FillRectangle(Card, Active ? ColorRgba { 28u, 33u, 30u, 255u } : (Hov ? ColorRgba { 32u, 32u, 33u, 255u } : ColorRgba { 26u, 26u, 27u, 255u }), 20.0f);
                Surface.StrokeRectangle(Card, Active ? ColorRgba { 52u, 199u, 89u, 130u } : ColorRgba { 48u, 48u, 48u, 255u }, 20.0f, Active ? 1.25f : 1.0f);

                const Rectangle2D IconSeat { Card.X + 14.0f, Card.Y + 13.0f, 28.0f, 28.0f };
                if (Mode == FractureMethodMode::Baked)
                {
                    FractureIcons::DrawBakeIcon(Surface, IconSeat);
                }
                else
                {
                    FractureIcons::DrawLightningIcon(Surface, IconSeat, ColorRgba { 246u, 197u, 94u, 255u });
                }

                Surface.Text({ Card.X + 50.0f, Card.Y + 12.0f }, 13.5f, ColorRgba { 240u, 240u, 238u, 255u }, Title);
                Surface.Text({ Card.X + 50.0f, Card.Y + 29.0f }, 10.2f, ColorRgba { 146u, 146u, 144u, 255u }, Lead);

                if (Active)
                {
                    const Point2D CheckC { Card.Right() - 22.0f, Card.Y + 25.0f };
                    Surface.FillCircle(CheckC, 9.5f, ColorRgba { 52u, 199u, 89u, 255u });
                    FractureIcons::DrawCheckGlyph(Surface, { CheckC.X - 5.5f, CheckC.Y - 5.5f, 11.0f, 11.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.2f);
                }

                const float Cw = (Card.Width - 28.0f) * 0.5f;
                auto DrawFact = [&](float Fx, float Fy, const char* K, const char* V, ColorRgba VCol)
                {
                    Surface.Text({ Fx, Fy }, 9.2f, ColorRgba { 122u, 122u, 120u, 255u }, K);
                    Surface.Text({ Fx, Fy + 13.0f }, 10.8f, VCol, V);
                };
                DrawFact(Card.X + 14.0f,      Card.Y + 52.0f, K1, V1, ColorRgba { 222u, 222u, 220u, 255u });
                DrawFact(Card.X + 14.0f + Cw, Card.Y + 52.0f, K2, V2, V2Good ? ColorRgba { 91u, 219u, 135u, 255u } : ColorRgba { 222u, 222u, 220u, 255u });
                DrawFact(Card.X + 14.0f,      Card.Y + 86.0f, K3, V3, ColorRgba { 222u, 222u, 220u, 255u });
                DrawFact(Card.X + 14.0f + Cw, Card.Y + 86.0f, K4, V4, ColorRgba { 222u, 222u, 220u, 255u });

                if (Hov && Pointer.Pressed && !Session.MaterialOutsideOpen && !Session.MaterialInsideOpen)
                {
                    Session.MethodMode = Mode;
                    Session.ExecutionMode = (Mode == FractureMethodMode::Realtime)
                        ? FractureExecutionMode::Realtime : FractureExecutionMode::Baked;
                    if (Mode == FractureMethodMode::Realtime)
                    {
                        Session.ApplySolverMethod(FractureSolverMethod::FastRuntimeBoussinesq);
                    }
                    else
                    {
                        Session.RebuildFracturePartition();
                    }
                }
                Cy += 136.0f;
            };

            char StoredBuf[40] {}, BakeBuf[40] {};
            std::snprintf(StoredBuf, sizeof(StoredBuf), "%zu pieces · Hulls", Session.PartitionResult.Fragments.size());
            std::snprintf(BakeBuf, sizeof(BakeBuf), "%.0f ms, ahead of time", Session.LastTimings.TotalMs);
            DrawMethodCard(FractureMethodMode::Baked, "Baked", "Pre-fractured in the editor",
                "PATTERN", "Fixed at bake", "AT IMPACT", "Release only · 0 ms", true,
                "STORED", StoredBuf, "BAKE", BakeBuf);

            char RtBuf[40] {}, PiecesBuf[40] {};
            std::snprintf(RtBuf, sizeof(RtBuf), "1.2 of %u ms", Session.RealtimeBudgetMs);
            std::snprintf(PiecesBuf, sizeof(PiecesBuf), "<= %u cells", Session.FragmentCeiling);
            DrawMethodCard(FractureMethodMode::Realtime, "Realtime", "Fractured at the moment of impact",
                "PATTERN", "Follows every hit", "AT IMPACT", RtBuf, true,
                "STORED", "Shape only", "PIECES", PiecesBuf);

            // 6-Algorithm Grid
            Surface.Text({ X, Cy + 2.0f }, 10.4f, ColorRgba { 160u, 160u, 158u, 255u }, "Algorithm");
            Surface.TextRightAligned({ X, Cy + 2.0f, InnerW, 14.0f }, 9.8f, ColorRgba { 126u, 126u, 124u, 255u }, "6 solvers");
            Cy += 22.0f;

            struct SolverOptionEntry { FractureSolverMethod M; uint32_t GlyphIdx; const char* Name; const char* Sub; };
            static constexpr std::array<SolverOptionEntry, 6> Algs {{
                { FractureSolverMethod::ClusteredVoronoi,      0u, "Clustered Voronoi", "Dense near hit" },
                { FractureSolverMethod::FastRuntimeBoussinesq, 1u, "Uniform Voronoi",   "Even cells" },
                { FractureSolverMethod::LefmWavefront,         2u, "Radial",            "Rays + rings" },
                { FractureSolverMethod::GriffithStressBudget,  3u, "Planar slices",     "Parallel cuts" },
                { FractureSolverMethod::EngineeringBakedSolid, 4u, "Brick",             "Running bond" },
                { FractureSolverMethod::DendriticSpallHull,    5u, "Recursive split",   "BSP cuts" },
            }};
            const float Gap = 6.0f;
            const float Aw  = (InnerW - Gap) * 0.5f;
            const float Ah  = 56.0f;
            for (size_t I = 0u; I < Algs.size(); ++I)
            {
                const uint32_t Col = static_cast<uint32_t>(I % 2u);
                const uint32_t Row = static_cast<uint32_t>(I / 2u);
                const Rectangle2D Tile { X + static_cast<float>(Col) * (Aw + Gap), Cy + static_cast<float>(Row) * (Ah + Gap), Aw, Ah };
                const bool Active = (Session.SolverMethod == Algs[I].M);
                const bool Hov    = ControlAppearance::QueryPointerOver(Tile, Pointer);
                Surface.FillRectangle(Tile, Active ? ColorRgba { 32u, 30u, 26u, 255u } : (Hov ? ColorRgba { 30u, 30u, 31u, 255u } : ColorRgba { 24u, 24u, 25u, 255u }), 14.0f);
                Surface.StrokeRectangle(Tile, Active ? ColorRgba { 242u, 169u, 59u, 125u } : ColorRgba { 255u, 255u, 255u, 20u }, 14.0f, Active ? 1.2f : 1.0f);
                FractureIcons::DrawAlgorithmGlyph(Surface, { Tile.X + 10.0f, Tile.Y + 13.0f, 22.0f, 22.0f }, Algs[I].GlyphIdx,
                    Active ? ColorRgba { 228u, 198u, 150u, 255u } : ColorRgba { 164u, 164u, 162u, 255u });
                Surface.Text({ Tile.X + 38.0f, Tile.Y + 10.0f }, 10.6f, ColorRgba { 232u, 232u, 230u, 255u }, Algs[I].Name);
                Surface.Text({ Tile.X + 38.0f, Tile.Y + 28.0f }, 9.2f, ColorRgba { 132u, 132u, 130u, 255u }, Algs[I].Sub);
                if (Hov && Pointer.Pressed)
                {
                    Session.ApplySolverMethod(Algs[I].M);
                }
            }
            Cy += 3.0f * (Ah + Gap) + 12.0f;
        }

        // 5. SHAPE
        if (DrawSectionHeader(1u, "SHAPE", Prim.Formula))
        {
            const Rectangle2D Panel { X, Cy, InnerW, 134.0f };
            Surface.FillRectangle(Panel, ColorRgba { 32u, 32u, 32u, 255u }, 20.0f);
            Surface.StrokeRectangle(Panel, ColorRgba { 44u, 44u, 44u, 255u }, 20.0f, 1.0f);
            Cy += 12.0f;

            char DimABuf[24] {}, DimBBuf[24] {};
            std::snprintf(DimABuf, sizeof(DimABuf), "%.2f", Session.DimPrimaryA);
            const char* LabelA = (Session.ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? "Ring radius" : "Primary extent";
            const float NewA = DrawScalarField(0, LabelA, DimABuf, "m", 0.15f, 1.60f, Session.DimPrimaryA);
            if (std::abs(NewA - Session.DimPrimaryA) > 0.005f)
            {
                Session.DimPrimaryA = NewA;
                Session.RebuildSourceGeometry();
                Session.RebuildFracturePartition();
            }

            std::snprintf(DimBBuf, sizeof(DimBBuf), "%.2f", Session.DimPrimaryB);
            const char* LabelB = (Session.ActiveShape == FractureIcons::PrimitiveShapeId::Torus) ? "Tube radius" : "Height / span";
            const float NewB = DrawScalarField(1, LabelB, DimBBuf, "m", 0.05f, 1.50f, Session.DimPrimaryB);
            if (std::abs(NewB - Session.DimPrimaryB) > 0.005f)
            {
                Session.DimPrimaryB = NewB;
                Session.RebuildSourceGeometry();
                Session.RebuildFracturePartition();
            }
            Cy += 8.0f;
        }

        // 6. MATERIAL (Outside & Interior dual picker cards)
        Rectangle2D OutsideCard {}, InsideCard {};
        if (DrawSectionHeader(2u, "MATERIAL", "Outside and interior"))
        {
            auto DrawMatPicker = [&](bool Outside, Rectangle2D& OutCard)
            {
                const auto& Mat = Outside ? Session.ActiveSurfaceMaterialSpec() : Session.ActiveMaterialSpec();
                const Rectangle2D Card { X, Cy, InnerW, 68.0f };
                OutCard = Card;
                const bool Hov = ControlAppearance::QueryPointerOver(Card, Pointer);
                Surface.FillRectangle(Card, Hov ? ColorRgba { 36u, 36u, 36u, 255u } : ColorRgba { 31u, 31u, 31u, 255u }, 20.0f);
                Surface.StrokeRectangle(Card, ColorRgba { 48u, 48u, 48u, 255u }, 20.0f, 1.0f);

                const ColorRgba SwRgb = MaterialLinearToSrgbSwatch(Outside ? Mat.ExteriorLinearRgb : Mat.InteriorLinearRgb);
                FractureIcons::DrawMaterialSphereIcon(Surface, { Card.X + 12.0f, Card.Y + 12.0f, 44.0f, 44.0f }, SwRgb, !Outside);

                Surface.Text({ Card.X + 68.0f, Card.Y + 12.0f }, 7.6f, ColorRgba { 123u, 123u, 121u, 255u },
                    Outside ? "OUTSIDE · ORIGINAL SURFACE" : "INTERIOR · CUT FACES AND BULK");
                Surface.Text({ Card.X + 68.0f, Card.Y + 25.0f }, 13.5f, ColorRgba { 239u, 239u, 237u, 255u }, Mat.ShortLabel);
                char MatDesc[72] {};
                if (Outside)
                {
                    std::snprintf(MatDesc, sizeof(MatDesc), "Surface finish · colours the uncut shell");
                }
                else
                {
                    std::snprintf(MatDesc, sizeof(MatDesc), "Gc %.0f J/m2 · %.0f kg/m3 · sets the cracks",
                        Mat.FractureToughnessGc, Mat.DensityKgPerM3);
                }
                Surface.Text({ Card.X + 68.0f, Card.Y + 44.0f }, 8.6f, ColorRgba { 138u, 138u, 136u, 255u }, MatDesc);
                FractureIcons::DrawChevronDownGlyph(Surface, { Card.Right() - 26.0f, Card.Center().Y - 7.0f, 14.0f, 14.0f }, ColorRgba { 138u, 138u, 136u, 255u });

                if (Hov && Pointer.Pressed)
                {
                    if (Outside)
                    {
                        Session.MaterialOutsideOpen = !Session.MaterialOutsideOpen;
                        Session.MaterialInsideOpen  = false;
                    }
                    else
                    {
                        Session.MaterialInsideOpen  = !Session.MaterialInsideOpen;
                        Session.MaterialOutsideOpen = false;
                    }
                }
                Cy += 76.0f;
            };

            DrawMatPicker(true, OutsideCard);
            DrawMatPicker(false, InsideCard);
            Cy += 6.0f;
        }

        // 7. IMPACTOR & PATTERN & QUALITY
        if (DrawSectionHeader(3u, "IMPACTOR", "Energy, size, direction"))
        {
            char EnBuf[24] {};
            std::snprintf(EnBuf, sizeof(EnBuf), "%.0f", Session.ImpactEnergyJ);
            const float NewE = DrawScalarField(2, "Impact energy", EnBuf, "J", 100.0f, 25000.0f, static_cast<float>(Session.ImpactEnergyJ));
            if (std::abs(NewE - static_cast<float>(Session.ImpactEnergyJ)) > 15.0f)
            {
                Session.ImpactEnergyJ = static_cast<double>(NewE);
                Session.RebuildFracturePartition();
            }
        }
        if (DrawSectionHeader(4u, "PATTERN", FractureWorkspaceSession::QuerySolverShortLabel(Session.SolverMethod)))
        {
            char CeilBuf[24] {};
            std::snprintf(CeilBuf, sizeof(CeilBuf), "%u", Session.FragmentCeiling);
            const float NewC = DrawScalarField(3, "Piece ceiling", CeilBuf, "pcs", 4.0f, 120.0f, static_cast<float>(Session.FragmentCeiling));
            if (std::abs(NewC - static_cast<float>(Session.FragmentCeiling)) > 0.6f)
            {
                Session.FragmentCeiling = static_cast<uint32_t>(std::lround(NewC));
                Session.RebuildFracturePartition();
            }
        }
        if (DrawSectionHeader(5u, "QUALITY", "Cells and shading"))
        {
            const float Gap = 8.0f;
            const float Tw  = (InnerW - Gap) * 0.5f;
            const float Th  = 92.0f;
            auto DrawQTile = [&](uint32_t Col, uint32_t Row, const char* Label, bool& Flag, int GlyphKind)
            {
                const Rectangle2D Tile { X + static_cast<float>(Col) * (Tw + Gap), Cy + static_cast<float>(Row) * (Th + Gap), Tw, Th };
                const bool Hov = ControlAppearance::QueryPointerOver(Tile, Pointer);
                Surface.FillRectangle(Tile, Flag ? ColorRgba { 27u, 29u, 27u, 255u } : ColorRgba { 24u, 24u, 25u, 255u }, 17.0f);
                Surface.StrokeRectangle(Tile, Flag ? ColorRgba { 91u, 219u, 135u, 110u } : ColorRgba { 255u, 255u, 255u, 18u }, 17.0f, 1.0f);

                const Point2D SeatC { Tile.Center().X, Tile.Y + 30.0f };
                Surface.FillRectangle({ SeatC.X - 18.0f, SeatC.Y - 18.0f, 36.0f, 36.0f },
                    Flag ? ColorRgba { 36u, 58u, 42u, 255u } : ColorRgba { 38u, 38u, 38u, 255u }, 12.0f);
                const ColorRgba Ink = Flag ? ColorRgba { 91u, 219u, 135u, 255u } : ColorRgba { 160u, 160u, 158u, 255u };
                const Rectangle2D GBox { SeatC.X - 8.5f, SeatC.Y - 8.5f, 17.0f, 17.0f };
                if (GlyphKind == 0) FractureIcons::DrawTriangleGlyph(Surface, GBox, Ink);
                else if (GlyphKind == 1) FractureIcons::DrawWireCubeGlyph(Surface, GBox, Ink);
                else if (GlyphKind == 2) FractureIcons::DrawGridGlyph(Surface, GBox, Ink);
                else FractureIcons::DrawSphereWireGlyph(Surface, GBox, Ink);

                Surface.TextCentered({ Tile.X, Tile.Y + 54.0f, Tile.Width, 14.0f }, 10.2f, ColorRgba { 220u, 220u, 218u, 255u }, Label);
                Surface.TextCentered({ Tile.X, Tile.Y + 71.0f, Tile.Width, 12.0f }, 7.8f, Flag ? ColorRgba { 91u, 219u, 135u, 255u } : ColorRgba { 111u, 111u, 109u, 255u },
                    Flag ? "ON" : "OFF");

                if (Hov && Pointer.Pressed)
                {
                    Flag = !Flag;
                    Session.RebuildFracturePartition();
                }
            };
            DrawQTile(0u, 0u, "Needle rejection", Session.NeedleRejection, 0);
            DrawQTile(1u, 0u, "Closed caps",      Session.ClosedCaps,      1);
            DrawQTile(0u, 1u, "Quality fans",     Session.QualityFans,     2);
            DrawQTile(1u, 1u, "Exact normals",    Session.ExactNormals,    3);
            Cy += 2.0f * (Th + Gap) + 8.0f;
        }

        // 8. COLLISION (Convex hull vs exact Collision shape)
        if (DrawSectionHeader(6u, "COLLISION",
                Session.CollisionShapeMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh
                    ? "Exact fragment mesh" : "Convex hull"))
        {
            const Rectangle2D Card { X, Cy, InnerW, 92.0f };
            Surface.FillRectangle(Card, ColorRgba { 25u, 25u, 27u, 255u }, 18.0f);
            Surface.StrokeRectangle(Card, ColorRgba { 48u, 48u, 48u, 255u }, 18.0f, 1.0f);

            Surface.Text({ Card.X + 14.0f, Card.Y + 12.0f }, 10.4f, ColorRgba { 216u, 216u, 214u, 255u }, "Fragment collision shape");
            const Rectangle2D Track { Card.X + 12.0f, Card.Y + 32.0f, Card.Width - 24.0f, 30.0f };
            Surface.FillRectangle(Track, ColorRgba { 16u, 16u, 18u, 255u }, 15.0f);
            Surface.StrokeRectangle(Track, ColorRgba { 255u, 255u, 255u, 18u }, 15.0f, 0.9f);
            static constexpr std::array<const char*, 2> ColliderLabels {{ "Convex hull", "Collision shape" }};
            const float HalfW = (Track.Width - 4.0f) * 0.5f;
            for (uint32_t Opt = 0u; Opt < 2u; ++Opt)
            {
                const bool Active = (Opt == 0u)
                    ? (Session.CollisionShapeMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::ConvexHull)
                    : (Session.CollisionShapeMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh);
                const Rectangle2D Pill { Track.X + 2.0f + static_cast<float>(Opt) * HalfW, Track.Y + 2.0f, HalfW, Track.Height - 4.0f };
                const bool Hov = ControlAppearance::QueryPointerOver(Pill, Pointer);
                if (Active)
                {
                    Surface.FillRectangle(Pill, ColorRgba { 48u, 52u, 60u, 255u }, 13.0f);
                    Surface.StrokeRectangle(Pill, ColorRgba { 91u, 219u, 135u, 120u }, 13.0f, 1.0f);
                }
                else if (Hov)
                {
                    Surface.FillRectangle(Pill, ColorRgba { 255u, 255u, 255u, 10u }, 13.0f);
                }
                Surface.TextCentered(Pill, 10.0f, Active ? ColorRgba { 244u, 244u, 242u, 255u } : ColorRgba { 150u, 150u, 148u, 255u }, ColliderLabels[Opt]);
                if (Hov && Pointer.Pressed && !Active)
                {
                    Session.CollisionShapeMode = (Opt == 1u)
                        ? SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh
                        : SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::ConvexHull;
                    Session.RebuildFracturePartition();
                }
            }

            const auto Tel = Session.SimRunner.InspectTelemetry();
            char ColNote[72] {};
            std::snprintf(ColNote, sizeof(ColNote), "JoltPhysics · %u bodies · %u sub-shapes",
                Tel.FragmentCount, std::max(Tel.FragmentCount, Tel.TotalSubShapes));
            Surface.Text({ Card.X + 14.0f, Card.Bottom() - 21.0f }, 9.4f, ColorRgba { 142u, 142u, 140u, 255u }, ColNote);
            Cy += 100.0f;
        }

        // Floating Material Picker Popover List (if opened)
        if (Session.MaterialOutsideOpen || Session.MaterialInsideOpen)
        {
            const bool Outside = Session.MaterialOutsideOpen;
            const Rectangle2D Anchor = Outside ? OutsideCard : InsideCard;
            const uint32_t MatCount  = FractureMaterialRepository::QueryMaterialCount();
            const Rectangle2D Pop { X, Anchor.Bottom() + 6.0f, InnerW, static_cast<float>(MatCount) * 42.0f + 12.0f };
            Surface.FillRectangle(Pop, ColorRgba { 28u, 28u, 29u, 246u }, 18.0f);
            Surface.StrokeRectangle(Pop, ColorRgba { 255u, 255u, 255u, 32u }, 18.0f, 1.0f);

            for (uint32_t I = 0u; I < MatCount; ++I)
            {
                const auto& M = FractureMaterialRepository::LookupMaterialByIndex(I);
                const Rectangle2D Opt { Pop.X + 6.0f, Pop.Y + 6.0f + static_cast<float>(I) * 42.0f, Pop.Width - 12.0f, 40.0f };
                const bool Active = Outside ? (Session.SurfaceMaterialId == M.Identifier) : (Session.MaterialId == M.Identifier);
                const bool Hov    = ControlAppearance::QueryPointerOver(Opt, Pointer);
                if (Hov || Active)
                {
                    Surface.FillRectangle(Opt, ColorRgba { 255u, 255u, 255u, static_cast<uint8_t>(Active ? 16u : 10u) }, 12.0f);
                }
                const ColorRgba Sw = MaterialLinearToSrgbSwatch(Outside ? M.ExteriorLinearRgb : M.InteriorLinearRgb);
                FractureIcons::DrawMaterialSphereIcon(Surface, { Opt.X + 9.0f, Opt.Center().Y - 13.0f, 26.0f, 26.0f }, Sw, !Outside);
                Surface.Text({ Opt.X + 46.0f, Opt.Y + 7.0f }, 11.2f, Active ? ColorRgba { 255u, 255u, 255u, 255u } : ColorRgba { 226u, 226u, 224u, 255u }, M.ShortLabel);
                char Sub[56] {};
                std::snprintf(Sub, sizeof(Sub), "Gc %.0f J/m2 · %.0f kg/m3", M.FractureToughnessGc, M.DensityKgPerM3);
                Surface.Text({ Opt.X + 46.0f, Opt.Y + 22.0f }, 8.0f, ColorRgba { 123u, 123u, 121u, 255u }, Sub);
                if (Active)
                {
                    const Point2D Ck { Opt.Right() - 16.0f, Opt.Center().Y };
                    Surface.FillCircle(Ck, 8.0f, ColorRgba { 52u, 199u, 89u, 255u });
                    FractureIcons::DrawCheckGlyph(Surface, { Ck.X - 4.5f, Ck.Y - 4.5f, 9.0f, 9.0f }, ColorRgba { 11u, 36u, 18u, 255u }, 2.6f);
                }
                if (Hov && Pointer.Pressed)
                {
                    if (Outside) Session.SurfaceMaterialId = M.Identifier;
                    else Session.MaterialId = M.Identifier;
                    Session.MaterialOutsideOpen = false;
                    Session.MaterialInsideOpen  = false;
                    Session.RebuildFracturePartition();
                }
            }
        }
    }

    Surface.PopClip();

    // 8. 45px Inspector Footer
    Surface.FillRectangle(FootRect, ColorRgba { 21u, 21u, 21u, 255u }, 0.0f);
    Surface.StrokeLine({ FootRect.X, FootRect.Y }, { FootRect.Right(), FootRect.Y }, ColorRgba { 255u, 255u, 255u, 14u }, 1.0f);
    Surface.Text({ FootRect.X + 16.0f, FootRect.Y + 15.0f }, 9.6f, ColorRgba { 104u, 104u, 104u, 255u }, Prim.FooterTitle);
    Surface.TextRightAligned({ FootRect.X, FootRect.Y + 15.0f, FootRect.Width - 16.0f, 14.0f }, 9.6f, ColorRgba { 104u, 104u, 104u, 255u }, "Native C++");
}

} // namespace Frontier::Workspaces::Fracture
