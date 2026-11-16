/*============================================================================================================================================
                                                        FRACTUREINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of FractureInspectorPanel: Material SI card, 6-Solver controls, pill sliders, SDF memory & Engineering Receipt

#include "Engine/Workspaces/Fracture/FractureInspectorPanel.h"

#include "Engine/Workspaces/Particle/ParticlePaneAppearance.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::Fracture
{

namespace
{

using namespace ParticleAppearance;

const char* QuerySolverShortLabel(GeometrySpace::Fracture::FractureSolverMethod Method) noexcept
{
    using S = GeometrySpace::Fracture::FractureSolverMethod;
    switch (Method)
    {
        case S::FastRuntimeBoussinesq: return "Boussinesq Fast";
        case S::EngineeringBakedSolid: return "Navier-Cauchy Baked";
        case S::GriffithStressBudget:  return "Griffith Energy";
        case S::LefmWavefront:         return "LEFM Wavefront";
        case S::ClusteredVoronoi:      return "Clustered Voronoi";
        case S::DendriticSpallHull:    return "Dendritic Spall";
        default:                       return "Solver";
    }
}

void DrawStatPair(PixelSpace& Surface, float X, float Y, float W, const char* Left, const char* Right, ColorRgba RightCol) noexcept
{
    const auto C = QueryStudioColors();
    Surface.Text({ X, Y }, 9.6f, C.TextMuted, Left);
    Surface.TextRightAligned({ X, Y, W, 14.0f }, 9.6f, RightCol, Right);
}

} // namespace

void FractureInspectorPanel::Render(
    PixelSpace&               Surface,
    Rectangle2D               Bounds,
    FractureWorkspaceSession& Session,
    const ControlPointer&     Pointer,
    float                     Opacity) noexcept
{
    if (Bounds.Width < 220.0f || Bounds.Height < 220.0f || Opacity <= 0.001f)
    {
        return;
    }

    const auto C = QueryStudioColors();
    Surface.FillRectangle(Bounds, C.PanelBg, 22.0f);
    Surface.StrokeRectangle(Bounds, C.Hairline, 22.0f, 1.0f);

    const float PadX   = 15.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 14.0f;

    // Tab strip: Material | Solver | Receipt
    static constexpr std::array<const char*, 3> Tabs = { "Material", "Solver", "Receipt" };
    const float TabW = (InnerW - 8.0f) / 3.0f;
    for (uint32_t T = 0u; T < 3u; ++T)
    {
        const Rectangle2D TabRect { Bounds.X + PadX + static_cast<float>(T) * (TabW + 4.0f), Y, TabW, 25.0f };
        const bool Active  = (Session.ActiveInspectorTab == T);
        const bool Hovered = ControlAppearance::QueryPointerOver(TabRect, Pointer);
        Surface.FillRectangle(TabRect, Active ? C.CardElevated : (Hovered ? C.CardBg : C.WellBg), 12.5f);
        Surface.StrokeRectangle(TabRect, Active ? C.WarmAccent : C.Hairline, 12.5f, 1.0f);
        Surface.TextCentered(TabRect, 9.8f, Active ? C.TextPrimary : C.TextSoft, Tabs[T]);
        if (Hovered && Pointer.Pressed)
        {
            Session.ActiveInspectorTab = T;
        }
    }
    Y += 35.0f;

    const auto& Mat = Session.ActiveMaterialSpec();

    if (Session.ActiveInspectorTab == 0u)
    {
        // MATERIAL TAB
        Surface.Text({ Bounds.X + PadX, Y }, 9.2f, C.TextMuted, "PHYSICAL MATERIAL");
        Y += 15.0f;

        const uint32_t MatCount = GeometrySpace::Fracture::FractureMaterialRepository::QueryMaterialCount();
        const float PillW = (InnerW - 6.0f) * 0.5f;
        const float PillH = 30.0f;
        for (uint32_t I = 0u; I < MatCount; ++I)
        {
            const auto& Entry = GeometrySpace::Fracture::FractureMaterialRepository::LookupMaterialByIndex(I);
            const uint32_t Col = I % 2u;
            const uint32_t Row = I / 2u;
            const Rectangle2D MatRect {
                Bounds.X + PadX + static_cast<float>(Col) * (PillW + 6.0f),
                Y + static_cast<float>(Row) * (PillH + 5.0f),
                PillW, PillH
            };
            const bool Active  = (Session.MaterialId == Entry.Identifier);
            const bool Hovered = ControlAppearance::QueryPointerOver(MatRect, Pointer);
            Surface.FillRectangle(MatRect, Active ? C.CardElevated : (Hovered ? C.CardBg : C.WellBg), 9.0f);
            Surface.StrokeRectangle(MatRect, Active ? C.WarmAccent : C.Hairline, 9.0f, 1.0f);

            const auto LinToByte = [](float V) noexcept -> uint8_t
            {
                return static_cast<uint8_t>(std::clamp(std::lround(std::pow(std::clamp(V, 0.0f, 1.0f), 1.0f / 2.2f) * 255.0f), 0l, 255l));
            };
            const ColorRgba SwatchCol {
                LinToByte(Entry.ExteriorLinearRgb.X),
                LinToByte(Entry.ExteriorLinearRgb.Y),
                LinToByte(Entry.ExteriorLinearRgb.Z),
                255u
            };
            Surface.FillCircle({ MatRect.X + 12.0f, MatRect.Center().Y }, 5.0f, SwatchCol);
            Surface.Text({ MatRect.X + 23.0f, MatRect.Y + 9.0f }, 9.6f, Active ? C.TextPrimary : C.TextSoft, Entry.ShortLabel);

            if (Hovered && Pointer.Pressed)
            {
                Session.MaterialId = Entry.Identifier;
                Session.ExecuteFracturePartitionOnly();
                if (Session.ExecutionPhase != ExecutionStripPhase::Edit)
                {
                    Session.StartOrResetSimulation(Session.Playing);
                }
            }
        }
        Y += static_cast<float>((MatCount + 1u) / 2u) * (PillH + 5.0f) + 10.0f;

        // Constitutive SI Card
        const Rectangle2D CardRect { Bounds.X + PadX, Y, InnerW, 174.0f };
        Surface.FillRectangle(CardRect, C.CardBg, 14.0f);
        Surface.StrokeRectangle(CardRect, C.Hairline, 14.0f, 1.0f);

        float Cy = CardRect.Y + 12.0f;
        const float Cx = CardRect.X + 12.0f;
        const float Cw = CardRect.Width - 24.0f;
        Surface.Text({ Cx, Cy }, 10.8f, C.TextPrimary, Mat.Label);
        Cy += 22.0f;

        char Buf[48] {};
        std::snprintf(Buf, sizeof(Buf), "%.0f kg/m3", Mat.DensityKgPerM3);
        DrawStatPair(Surface, Cx, Cy, Cw, "Density (rho)", Buf, C.TextPrimary);
        Cy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.1f J/m2", Mat.FractureToughnessGc);
        DrawStatPair(Surface, Cx, Cy, Cw, "Toughness (Gc)", Buf, C.WarmAccent);
        Cy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.1f GPa · v=%.2f", Mat.YoungModulusGPa, Mat.PoissonRatio);
        DrawStatPair(Surface, Cx, Cy, Cw, "Elastic (E, nu)", Buf, C.TextPrimary);
        Cy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.1f MPa · m=%.0f", Mat.TensileStrengthMPa, Mat.WeibullModulus);
        DrawStatPair(Surface, Cx, Cy, Cw, "Tensile / Weibull", Buf, C.TextPrimary);
        Cy += 20.0f;

        const double Cr = GeometrySpace::Fracture::FractureMaterialRepository::ComputeRayleighWaveSpeedMps(Mat);
        std::snprintf(Buf, sizeof(Buf), "%.0f m/s", Cr);
        DrawStatPair(Surface, Cx, Cy, Cw, "Rayleigh Speed", Buf, C.TealTint);
        Cy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.0f J/m3", Mat.StoredEnergyJPerM3);
        DrawStatPair(Surface, Cx, Cy, Cw, "Stored Temper Energy", Buf, Mat.StoredEnergyJPerM3 > 0.0f ? C.DangerRed : C.TextMuted);
        Y = CardRect.Bottom() + 12.0f;

        // Grain orientation slider
        float GrainF = static_cast<float>(Session.GrainAngleDeg);
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Grain / Bedding Angle", GrainF, -90.0f, 90.0f, "deg", 0, Pointer, DraggingGrain))
        {
            Session.GrainAngleDeg = static_cast<double>(GrainF);
            Session.ExecuteFracturePartitionOnly();
        }
        Y += 50.0f;

        RasterizeTripletPillsRow(
            Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
            "Impact Anchor", "m",
            Session.ImpactPointLocal.X, Session.ImpactPointLocal.Y, Session.ImpactPointLocal.Z);
    }
    else if (Session.ActiveInspectorTab == 1u)
    {
        // SOLVER TAB
        Surface.Text({ Bounds.X + PadX, Y }, 9.2f, C.TextMuted, "FRACTURE ALGORITHM");
        Y += 15.0f;

        const float SolW = (InnerW - 6.0f) * 0.5f;
        const float SolH = 26.0f;
        for (uint32_t S = 0u; S < 6u; ++S)
        {
            const auto Method = static_cast<GeometrySpace::Fracture::FractureSolverMethod>(S);
            const uint32_t Col = S % 2u;
            const uint32_t Row = S / 2u;
            const Rectangle2D SRect {
                Bounds.X + PadX + static_cast<float>(Col) * (SolW + 6.0f),
                Y + static_cast<float>(Row) * (SolH + 4.0f),
                SolW, SolH
            };
            const bool Active  = (Session.SolverMethod == Method);
            const bool Hovered = ControlAppearance::QueryPointerOver(SRect, Pointer);
            Surface.FillRectangle(SRect, Active ? C.WarmSoft : (Hovered ? C.CardBg : C.WellBg), 8.0f);
            Surface.StrokeRectangle(SRect, Active ? C.WarmAccent : C.Hairline, 8.0f, 1.0f);
            Surface.TextCentered(SRect, 9.0f, Active ? C.WarmAccent : C.TextSoft, QuerySolverShortLabel(Method));
            if (Hovered && Pointer.Pressed)
            {
                Session.SolverMethod = Method;
                Session.ExecuteFracturePartitionOnly();
                if (Session.ExecutionPhase != ExecutionStripPhase::Edit)
                {
                    Session.StartOrResetSimulation(Session.Playing);
                }
            }
        }
        Y += 3.0f * (SolH + 4.0f) + 10.0f;

        float EnergyF = static_cast<float>(Session.ImpactEnergyJ);
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Impact Energy Budget", EnergyF, 150.0f, 12000.0f, "J", 0, Pointer, DraggingEnergy))
        {
            Session.ImpactEnergyJ = static_cast<double>(EnergyF);
            Session.ExecuteFracturePartitionOnly();
        }
        Y += 48.0f;

        float CeilingF = static_cast<float>(Session.FragmentCeiling);
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Fragment Ceiling", CeilingF, 4.0f, 96.0f, "pcs", 0, Pointer, DraggingCeiling))
        {
            Session.FragmentCeiling = static_cast<uint32_t>(std::lround(CeilingF));
            Session.ExecuteFracturePartitionOnly();
        }
        Y += 48.0f;

        float MinSizeCm = static_cast<float>(Session.MinimumSizeM * 100.0);
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Minimum Shard Span", MinSizeCm, 1.8f, 18.0f, "cm", 1, Pointer, DraggingMinSize))
        {
            Session.MinimumSizeM = static_cast<double>(MinSizeCm) * 0.01;
            Session.ExecuteFracturePartitionOnly();
        }
        Y += 48.0f;

        float BondF = static_cast<float>(Session.BondStrengthScale);
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Cohesive Bond Scale", BondF, 0.25f, 3.0f, "x", 2, Pointer, DraggingBondScale))
        {
            Session.BondStrengthScale = static_cast<double>(BondF);
            Session.ExecuteFracturePartitionOnly();
        }
        Y += 48.0f;

        float ExplodePct = Session.RenderOptions.ExplodeFactor * 100.0f;
        if (ConstructSliderPillRow(
                Surface, { Bounds.X + PadX, Y, InnerW, 44.0f },
                "Explode Separation", ExplodePct, 0.0f, 100.0f, "%", 0, Pointer, DraggingExplode))
        {
            Session.RenderOptions.ExplodeFactor = ExplodePct * 0.01f;
        }
        Y += 52.0f;

        // SDF & Non-Planar Fractography toggles
        const Rectangle2D FractoRow { Bounds.X + PadX, Y, InnerW, 24.0f };
        Surface.Text({ FractoRow.X, FractoRow.Y + 5.0f }, 10.0f, C.TextSoft, "Non-Planar Fractography");
        (void)ConstructToggleSwitch(Surface, { FractoRow.Right() - 40.0f, FractoRow.Y + 2.0f, 40.0f, 20.0f },
                                    Session.RenderOptions.UseNonPlanarFractography, Pointer);
        Y += 28.0f;

        const Rectangle2D SdfRow { Bounds.X + PadX, Y, InnerW, 24.0f };
        Surface.Text({ SdfRow.X, SdfRow.Y + 5.0f }, 10.0f, C.TextSoft, "Sparse SDF Collision");
        (void)ConstructToggleSwitch(Surface, { SdfRow.Right() - 40.0f, SdfRow.Y + 2.0f, 40.0f, 20.0f },
                                    Session.SdfEnabled, Pointer);
    }
    else
    {
        // RECEIPT TAB
        const auto& R = Session.PartitionResult.Receipt;
        const Rectangle2D HeroCard { Bounds.X + PadX, Y, InnerW, 74.0f };
        Surface.FillRectangle(HeroCard, C.HighlightBg, 14.0f);
        Surface.Text({ HeroCard.X + 12.0f, HeroCard.Y + 10.0f }, 9.2f, C.HighlightMuted, "VOLUME CONSERVATION");
        char DriftBuf[48] {};
        std::snprintf(DriftBuf, sizeof(DriftBuf), "%.2e", R.VolumeDriftRatio);
        Surface.Text({ HeroCard.X + 12.0f, HeroCard.Y + 26.0f }, 20.0f, C.HighlightInk, DriftBuf);
        Surface.TextRightAligned(
            { HeroCard.X + 12.0f, HeroCard.Y + 32.0f, HeroCard.Width - 24.0f, 18.0f },
            10.2f,
            R.WatertightManifold ? ColorRgba { 22u, 118u, 52u, 255u } : C.DangerRed,
            R.WatertightManifold ? "WATERTIGHT OK" : "DRIFT WARN");
        char VolLine[64] {};
        std::snprintf(VolLine, sizeof(VolLine), "V0=%.4f m3 · Sum=%.4f m3", R.SourceVolumeM3, R.FragmentVolumeSumM3);
        Surface.Text({ HeroCard.X + 12.0f, HeroCard.Y + 53.0f }, 9.0f, C.HighlightMuted, VolLine);
        Y = HeroCard.Bottom() + 12.0f;

        const Rectangle2D StatCard { Bounds.X + PadX, Y, InnerW, 134.0f };
        Surface.FillRectangle(StatCard, C.CardBg, 14.0f);
        Surface.StrokeRectangle(StatCard, C.Hairline, 14.0f, 1.0f);
        float Sy = StatCard.Y + 12.0f;
        const float Sx = StatCard.X + 12.0f;
        const float Sw = StatCard.Width - 24.0f;

        char Buf[64] {};
        std::snprintf(Buf, sizeof(Buf), "%u pcs (%.2f ms)", R.FragmentCount, R.ComputeDurationMs);
        DrawStatPair(Surface, Sx, Sy, Sw, "Fragments & Time", Buf, C.TextPrimary);
        Sy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%u intact / %u", R.IntactBondCount, R.BondCount);
        DrawStatPair(Surface, Sx, Sy, Sw, "Cohesive Bonds", Buf, C.LiveGreen);
        Sy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.3f m2", R.TotalCrackAreaM2);
        DrawStatPair(Surface, Sx, Sy, Sw, "New Crack Surface", Buf, C.WarmAccent);
        Sy += 20.0f;

        std::snprintf(Buf, sizeof(Buf), "%.0f / %.0f J", R.SpentSurfaceEnergyJ, R.TotalEnergyBudgetJ);
        DrawStatPair(Surface, Sx, Sy, Sw, "Griffith Surface Work", Buf, C.TextPrimary);
        Sy += 20.0f;

        const auto Sdf = Session.QuerySdfFootprint();
        std::snprintf(Buf, sizeof(Buf), "%.2f MB (%u^3)", Sdf.Megabytes, Session.SdfResolution);
        DrawStatPair(Surface, Sx, Sy, Sw, "Sparse SDF Footprint", Buf, C.TealTint);
        Y = StatCard.Bottom() + 12.0f;

        if (Session.PartitionResult.HasEngineering)
        {
            const auto& Eng = Session.PartitionResult.EngineeringMetrics;
            const Rectangle2D EngCard { Bounds.X + PadX, Y, InnerW, 152.0f };
            Surface.FillRectangle(EngCard, C.CardBg, 14.0f);
            Surface.StrokeRectangle(EngCard, C.Hairline, 14.0f, 1.0f);
            float Ey = EngCard.Y + 12.0f;
            Surface.Text({ Sx, Ey }, 9.6f, C.WarmAccent, "NAVIER-CAUCHY CONSTITUTIVE");
            Ey += 18.0f;

            std::snprintf(Buf, sizeof(Buf), "%.1f / %.1f MPa", Eng.PeakSigma1MPa, Eng.PeakVonMisesMPa);
            DrawStatPair(Surface, Sx, Ey, Sw, "Peak sigma1 / VM", Buf, C.TextPrimary);
            Ey += 20.0f;

            std::snprintf(Buf, sizeof(Buf), "I:%u · II:%u · T:%u", Eng.ModeICount, Eng.ModeIICount, Eng.TJunctionCount);
            DrawStatPair(Surface, Sx, Ey, Sw, "Mode I / II / T-Junc", Buf, C.TextPrimary);
            Ey += 20.0f;

            std::snprintf(Buf, sizeof(Buf), "%.2f mm RMS", Eng.RoughnessRmsMm);
            DrawStatPair(Surface, Sx, Ey, Sw, "Bouchaud Relief", Buf, C.TealTint);
            Ey += 20.0f;

            Surface.Text({ Sx, Ey }, 8.6f, C.TextMuted, Eng.FractographyLabel);
        }
    }
}

} // namespace Frontier::Workspaces::Fracture
