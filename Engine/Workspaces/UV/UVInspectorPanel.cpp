/*============================================================================================================================================
                                                          UVINSPECTORPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of UVInspectorPanel matching Experimental/UVEditor across Pack, Unwrap, UDIM, Shading & Check tabs

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/UV/UVInspectorPanel.h"

#include "Engine/Workspaces/UV/UVPaneAppearance.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace Frontier::Workspaces
{

namespace
{

void ConstructMetricTrio(
    PixelSpace&                        Surface,
    Rectangle2D                        Box,
    const char*                        Val0, const char* Cap0,
    const char*                        Val1, const char* Cap1,
    const char*                        Val2, const char* Cap2,
    const UVAppearance::UVThemeColors& Theme)
{
    Surface.FillRectangle(Box, Theme.SectionBg, 16.0f);
    Surface.StrokeRectangle(Box, Theme.BorderSubtle, 16.0f, 1.0f);

    const float ColW = Box.Width / 3.0f;
    Surface.StrokeLine({ Box.X + ColW, Box.Y + 10.0f }, { Box.X + ColW, Box.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);
    Surface.StrokeLine({ Box.X + ColW * 2.0f, Box.Y + 10.0f }, { Box.X + ColW * 2.0f, Box.Bottom() - 10.0f }, Theme.BorderSubtle, 1.0f);

    const std::array<std::pair<const char*, const char*>, 3u> Cols {{
        { Val0, Cap0 },
        { Val1, Cap1 },
        { Val2, Cap2 },
    }};
    for (uint32_t Idx = 0u; Idx < 3u; ++Idx)
    {
        const float CellX = Box.X + ColW * static_cast<float>(Idx);
        const float VW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 13.5f, Cols[Idx].first);
        const float CW    = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 8.8f, Cols[Idx].second);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Medium,
            { CellX + (ColW - VW) * 0.5f, Box.Y + 10.0f },
            13.5f,
            (Idx == 0u) ? Theme.AccentFill : Theme.TextPrimary,
            Cols[Idx].first);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface,
            FontWeightClass::Regular,
            { CellX + std::max(4.0f, (ColW - CW) * 0.5f), Box.Y + 33.0f },
            ColW - 8.0f,
            8.8f,
            Theme.TextMuted,
            Cols[Idx].second);
    }
}

[[nodiscard]] float EvaluateUDIMFillPercent(const GeometrySpace::PolygonNetwork& Net, uint32_t TargetUDIM) noexcept
{
    double AreaSum = 0.0;
    for (uint32_t FaceIdx = 0u; FaceIdx < static_cast<uint32_t>(Net.Faces.size()); ++FaceIdx)
    {
        const auto& Face = Net.Faces[FaceIdx];
        if (Face.BoundaryLoopEdgeIndex == GeometrySpace::InvalidNetworkIndex)
        {
            continue;
        }
        const auto UV = Net.Edges[Face.BoundaryLoopEdgeIndex].ParameterUV;
        const int  TU = std::clamp(static_cast<int>(std::floor(UV.U)), 0, 9);
        const int  TV = std::max(0, static_cast<int>(std::floor(UV.V)));
        const uint32_t Code = static_cast<uint32_t>(1001 + TU + 10 * TV);
        if (Code == TargetUDIM)
        {
            AreaSum += std::max(0.0f, Net.EvaluateFaceSignedAreaUV(FaceIdx));
        }
    }
    return static_cast<float>(std::clamp(AreaSum * 100.0, 0.0, 100.0));
}

} // namespace

void UVInspectorPanel::AttachSession(UVWorkspaceSession* TargetSession) noexcept
{
    Session       = TargetSession;
    ScrollOffsetY = 0.0f;
}

float UVInspectorPanel::RenderPackingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    const auto* ActiveLayer = Session->Asset.Layers.InspectLayer(Session->SelectedLayerOrdinal);
    const float ActiveShare = (ActiveLayer != nullptr) ? ActiveLayer->TargetSharePercent : Session->Asset.Layers.DeriveRemainderSharePercent();
    const float FillPct     = EvaluateUDIMFillPercent(Session->Asset.Network, Session->ActiveUDIMNumber);
    const float RemPct      = Session->Asset.Layers.DeriveRemainderSharePercent();

    char ShareStr[24]{}, FillStr[24]{}, RemStr[24]{}, UdimFillCap[32]{};
    std::snprintf(ShareStr, sizeof(ShareStr), "%.0f%%", ActiveShare);
    std::snprintf(FillStr, sizeof(FillStr), "%.1f%%", FillPct);
    std::snprintf(RemStr, sizeof(RemStr), "%.0f%%", RemPct);
    std::snprintf(UdimFillCap, sizeof(UdimFillCap), "UDIM %u Fill", Session->ActiveUDIMNumber);

    ConstructMetricTrio(
        Surface,
        { LeftX, CurY, InnerW, 56.0f },
        ShareStr, "Layer Budget",
        FillStr,  UdimFillCap,
        RemStr,   "Auto Remainder",
        Theme);
    CurY += 66.0f;

    // Section 1: Importance Space Allocation (.pz-section)
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 204.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Importance Space Allocation", Theme);
        SY += 24.0f;

        // Active Layer selector capsules row
        const std::string LayerLabel = (ActiveLayer != nullptr) ? ActiveLayer->LayerTitle : std::string("Unassigned Remainder");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, SY + 6.0f }, 9.8f, Theme.TextMuted, "Active Layer");
        const Rectangle2D LayerCycleBtn{ SX + 88.0f, SY, SW - 88.0f, 26.0f };
        if (ConstructCapsuleButton(Surface, LayerCycleBtn, LayerLabel, Theme, Cursor, Pressed, false, false, 9.8f, 10.0f))
        {
            const auto Layers = Session->Asset.Layers.InspectLayers();
            if (!Layers.empty())
            {
                std::size_t NextIdx = 0u;
                for (std::size_t Idx = 0u; Idx < Layers.size(); ++Idx)
                {
                    if (Layers[Idx].LayerOrdinal == Session->SelectedLayerOrdinal)
                    {
                        NextIdx = (Idx + 1u) % (Layers.size() + 1u);
                        break;
                    }
                }
                const uint16_t NextOrd = (NextIdx < Layers.size()) ? Layers[NextIdx].LayerOrdinal : 0u;
                Session->SelectPriorityLayer(NextOrd);
            }
        }
        SY += 34.0f;

        // Space Share Slider (0..100%)
        if (ActiveLayer != nullptr)
        {
            float NormShare = ActiveLayer->TargetSharePercent * 0.01f;
            char  ShareVal[24]{};
            std::snprintf(ShareVal, sizeof(ShareVal), "%.0f%%", ActiveLayer->TargetSharePercent);
            if (ConstructSliderControl(
                    Surface,
                    { SX, SY, SW, 28.0f },
                    "Space Share",
                    ShareVal,
                    NormShare,
                    Theme,
                    Cursor,
                    Down,
                    Pressed,
                    LinearTintToRgba(ActiveLayer->LinearTintRGB)))
            {
                Session->AdjustSelectedLayerShare(NormShare * 100.0f);
            }
        }
        else
        {
            float FixedNorm = RemPct * 0.01f;
            ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Remainder Share (Auto)", RemStr, FixedNorm, Theme, Cursor, false, false);
        }
        SY += 36.0f;

        // Layer Colour Swatch Strip
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, SY + 4.0f }, 9.8f, Theme.TextMuted, "Layer Colour");
        constexpr std::array<Mathematics::Scalar3D, 6u> Swatches {{
            { 0.937f, 0.957f, 0.812f }, // #eff4cf Ivory
            { 0.620f, 0.812f, 1.000f }, // #9ecfff Sky
            { 0.608f, 0.890f, 0.769f }, // #9be3c4 Mint
            { 0.949f, 0.757f, 0.490f }, // #f2c17d Amber
            { 0.898f, 0.596f, 0.608f }, // #e5989b Coral
            { 0.769f, 0.655f, 0.906f }, // #c4a7e7 Lavender
        }};
        float SwX = SX + SW - static_cast<float>(Swatches.size()) * 24.0f;
        for (const auto& Tint : Swatches)
        {
            const ColorRgba   Col    = LinearTintToRgba(Tint);
            const Point2D     Center { SwX + 10.0f, SY + 11.0f };
            const Rectangle2D Hit    { SwX, SY + 1.0f, 20.0f, 20.0f };
            Surface.FillCircle(Center, 8.0f, Col);
            Surface.StrokeCircle(Center, 8.5f, ColorRgba{ 255, 255, 255, 48 }, 1.0f);
            if (Hit.Contains(Cursor) && Pressed && ActiveLayer != nullptr)
            {
                Session->AssignSelectedLayerTint(Tint);
            }
            SwX += 24.0f;
        }
        SY += 30.0f;

        // Bind Selected Faces | Unassign Selected
        const float HalfW = (SW - 8.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Bind Selected Faces", Theme, Cursor, Pressed, false, false, 9.6f))
        {
            if (Session->SelectedLayerOrdinal != 0u)
            {
                Session->BindSelectionToPriorityLayer(Session->SelectedLayerOrdinal);
            }
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 8.0f, SY, HalfW, 28.0f }, "Unassign Selected", Theme, Cursor, Pressed, false, false, 9.6f))
        {
            Session->BindSelectionToPriorityLayer(0u);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Concave Packing Engine (.pz-section)
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 238.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Concave Packing Engine", Theme, "EXACT SHAPE");
        SY += 24.0f;

        auto& Prefs = Session->WorkspaceConfig.AccessUVPreferences();

        // Strict Ratio | Fill Pockets 2-way toggle
        const float HalfW = (SW - 6.0f) * 0.5f;
        const bool  StrictOn = (Prefs.BudgetMode == Configuration::UVBudgetEnforcementMode::StrictRatio);
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 26.0f }, "Strict Ratio", Theme, Cursor, Pressed, StrictOn, false, 9.6f, 9.0f))
        {
            Prefs.BudgetMode            = Configuration::UVBudgetEnforcementMode::StrictRatio;
            Session->FillPocketsEnabled = false;
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 26.0f }, "Fill Pockets", Theme, Cursor, Pressed, !StrictOn, false, 9.6f, 9.0f))
        {
            Prefs.BudgetMode            = Configuration::UVBudgetEnforcementMode::FillRemainderPockets;
            Session->FillPocketsEnabled = true;
        }
        SY += 34.0f;

        // Island Margin Padding (0..64 px)
        float MarginNorm = static_cast<float>(Prefs.IslandMarginPixels) / 64.0f;
        char  MarginTxt[24]{};
        std::snprintf(MarginTxt, sizeof(MarginTxt), "%u px", Prefs.IslandMarginPixels);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Island Margin Padding", MarginTxt, MarginNorm, Theme, Cursor, Down, Pressed))
        {
            Prefs.IslandMarginPixels = static_cast<uint32_t>(std::round(MarginNorm * 64.0f));
        }
        SY += 36.0f;

        // Rotation Steps: 0° | 90° | 45° | 15°
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { SX, SY + 5.0f }, 9.8f, Theme.TextMuted, "Rotation Steps");
        constexpr std::array<std::pair<const char*, uint32_t>, 4u> RotSteps {{
            { "0°",  0u  },
            { "90°", 90u },
            { "45°", 45u },
            { "15°", 15u },
        }};
        const float StepW = (SW - 96.0f - 9.0f) / 4.0f;
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            const Rectangle2D StepBtn{ SX + 96.0f + static_cast<float>(Idx) * (StepW + 3.0f), SY, StepW, 24.0f };
            if (ConstructCapsuleButton(
                    Surface,
                    StepBtn,
                    RotSteps[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    Prefs.RotationStepDegrees == RotSteps[Idx].second,
                    false,
                    9.2f,
                    7.0f))
            {
                Prefs.RotationStepDegrees = RotSteps[Idx].second;
            }
        }
        SY += 32.0f;

        // Equalize Intra-Group Scale switch
        (void)ConstructToggleRow(Surface, { SX, SY, SW, 24.0f }, "Equalize Intra-Group Scale", Prefs.EqualizeIntraLayerScale, Theme, Cursor, Pressed);
        SY += 32.0f;

        // Primary CTA: Pack with Importance Budgets (Shift+P)
        if (ConstructCapsuleButton(
                Surface,
                { SX, SY, SW, 34.0f },
                "Pack with Importance Budgets (Shift+P)",
                Theme,
                Cursor,
                Pressed,
                false,
                true,
                10.5f,
                17.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderUnwrapTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    // Section 1: Unwrap, Quads & Projections
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 198.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Unwrap, Quads & Stacking", Theme);
        SY += 24.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 30.0f }, "LSCM Unwrap (U)", Theme, Cursor, Pressed, false, true, 10.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnwrapConformal, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 30.0f }, "Smart UV Project", Theme, Cursor, Pressed, false, false, 10.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::ProjectSmartUV, nullptr);
        }
        SY += 36.0f;

        const float ThirdW = (SW - 8.0f) / 3.0f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Follow Quads", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::FollowActiveQuads, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Stack Similar", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::StackSimilarIslands, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Unstack", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnstackOverlapping, nullptr);
        }
        SY += 32.0f;

        const float FourthW = (SW - 9.0f) / 4.0f;
        const std::array<std::pair<const char*, UVCommandDirective>, 4u> Projs {{
            { "Cube",     UVCommandDirective::ProjectCubicBox    },
            { "Cylinder", UVCommandDirective::ProjectCylindrical },
            { "Sphere",   UVCommandDirective::ProjectSpherical   },
            { "Camera",   UVCommandDirective::ProjectCameraView  },
        }};
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (FourthW + 3.0f), SY, FourthW, 25.0f },
                    Projs[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    false,
                    false,
                    9.0f,
                    8.0f))
            {
                (void)Session->ExecuteDirective(Projs[Idx].second, nullptr);
            }
        }
        SY += 33.0f;

        float AngleNorm = (Session->SharpAngleThreshold - 15.0f) / 74.0f;
        char  AngleTxt[24]{};
        std::snprintf(AngleTxt, sizeof(AngleTxt), "%.0f°", Session->SharpAngleThreshold);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Smart Seam Angle Limit", AngleTxt, AngleNorm, Theme, Cursor, Down, Pressed))
        {
            Session->SharpAngleThreshold = 15.0f + AngleNorm * 74.0f;
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Polygon Network, Pins & Seams
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 134.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Pin, Stitch & Seam Operations", Theme);
        SY += 24.0f;

        const float ThirdW = (SW - 8.0f) / 3.0f;
        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Pin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::PinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Unpin UVs", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::UnpinSelectedUVs, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Rectify", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RectifySelectedQuads, nullptr);
        }
        SY += 32.0f;

        if (ConstructCapsuleButton(Surface, { SX, SY, ThirdW, 26.0f }, "Mark Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::MarkSharpSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + ThirdW + 4.0f, SY, ThirdW, 26.0f }, "Clear Seams", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::ClearSelectedSeams, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + (ThirdW + 4.0f) * 2.0f, SY, ThirdW, 26.0f }, "Stitch", Theme, Cursor, Pressed, false, false, 9.2f, 9.0f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::StitchSelectedSeams, nullptr);
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 3: Relax & Texel Density
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 146.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "GPU Stretch Relax & Texel Density", Theme);
        SY += 24.0f;

        float IterNorm = static_cast<float>(Session->RelaxIterations) / 64.0f;
        char  IterTxt[24]{};
        std::snprintf(IterTxt, sizeof(IterTxt), "%u passes", Session->RelaxIterations);
        if (ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Relax Passes", IterTxt, IterNorm, Theme, Cursor, Down, Pressed))
        {
            Session->RelaxIterations = std::max(1u, static_cast<uint32_t>(std::round(IterNorm * 64.0f)));
        }
        SY += 36.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Relax Stretch GPU", Theme, Cursor, Pressed, false, true, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Equalize Density", Theme, Cursor, Pressed, false, false, 9.8f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::EqualizeTexelDensity, nullptr);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderUDIMTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;

    float CurY = StartY;

    char ActBuf[24]{}, CountBuf[24]{}, DensBuf[24]{};
    std::snprintf(ActBuf, sizeof(ActBuf), "%u", Session->ActiveUDIMNumber);
    std::snprintf(CountBuf, sizeof(CountBuf), "%zu", Session->Asset.UDIMTiles.size());
    std::snprintf(DensBuf, sizeof(DensBuf), "%.1f px/cm", Session->WorkspaceConfig.InspectUVPreferences().TargetTexelDensity * 0.01f);

    ConstructMetricTrio(
        Surface,
        { LeftX, CurY, InnerW, 56.0f },
        ActBuf,   "Active UDIM",
        CountBuf, "Atlas Tiles",
        DensBuf,  "Texel Density",
        Theme);
    CurY += 66.0f;

    // Section 1: UDIM Tiles grid (.pz-udim-grid)
    {
        const float TileListHeight = static_cast<float>(Session->Asset.UDIMTiles.size()) * 36.0f;
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 88.0f + TileListHeight };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "UDIM Tiles", Theme);
        SY += 24.0f;

        const float HalfW = (SW - 6.0f) * 0.5f;
        if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 26.0f }, "+ Add UDIM Tile", Theme, Cursor, Pressed, false, true, 9.6f))
        {
            (void)Session->ConstructNextUDIMTile();
        }
        if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 26.0f }, "Distribute UDIMs", Theme, Cursor, Pressed, false, false, 9.6f))
        {
            (void)Session->ExecuteDirective(UVCommandDirective::AutoDistributeUDIMs, nullptr);
        }
        SY += 34.0f;

        for (const uint32_t TileCode : Session->Asset.UDIMTiles)
        {
            const bool        IsAct = (Session->ActiveUDIMNumber == TileCode);
            const auto        Cfg   = Session->QueryUDIMTileSetting(TileCode);
            const float       Occ   = EvaluateUDIMFillPercent(Session->Asset.Network, TileCode);
            const Rectangle2D Row   { SX, SY, SW, 32.0f };
            const bool        Hov   = Row.Contains(Cursor);

            Surface.FillRectangle(Row, IsAct ? Theme.AccentSoft : (Hov ? Theme.HoverBg : Theme.WellBg), 8.0f);
            Surface.StrokeRectangle(Row, IsAct ? Theme.AccentBorder : Theme.BorderSubtle, 8.0f, 1.0f);

            Surface.FillCircle({ Row.X + 12.0f, Row.Y + 14.0f }, 3.5f, IsAct ? Theme.AccentFill : Theme.TextMuted);

            char RowTxt[64]{};
            const uint32_t TU = (TileCode - 1001u) % 10u;
            const uint32_t TV = (TileCode - 1001u) / 10u;
            std::snprintf(RowTxt, sizeof(RowTxt), "UDIM %u   (%u,%u) · %.1f%%", TileCode, TU, TV, Occ);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Row.X + 24.0f, Row.Y + 7.5f }, 9.8f, Theme.TextPrimary, RowTxt);

            char ResolutionTag[16]{};
            if (Cfg.ResolutionPx >= 1024u)
            {
                std::snprintf(ResolutionTag, sizeof(ResolutionTag), "%uK", Cfg.ResolutionPx / 1024u);
            }
            else
            {
                std::snprintf(ResolutionTag, sizeof(ResolutionTag), "%u", Cfg.ResolutionPx);
            }
            const float RW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.0f, ResolutionTag);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Row.Right() - RW - 10.0f, Row.Y + 8.0f }, 9.0f, Theme.AccentFill, ResolutionTag);

            // Bottom 2px fill bar
            if (Occ > 0.2f)
            {
                Surface.FillRectangle({ Row.X + 4.0f, Row.Bottom() - 3.0f, (Row.Width - 8.0f) * (Occ * 0.01f), 2.0f }, Theme.AccentFill, 1.0f);
            }

            if (Hov && Pressed)
            {
                Session->ActiveUDIMNumber  = TileCode;
                Session->FramebuffersDirty = true;
            }
            SY += 36.0f;
        }

        CurY += SecRect.Height + 10.0f;
    }

    // Section 2: Active UDIM Resolution & Backing
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 132.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        char SecTitle[48]{};
        std::snprintf(SecTitle, sizeof(SecTitle), "UDIM %u Settings", Session->ActiveUDIMNumber);
        ConstructSectionHeading(Surface, SX, SY, SW, SecTitle, Theme);
        SY += 24.0f;

        auto& TileCfg = Session->AccessUDIMTileSetting(Session->ActiveUDIMNumber);

        // 5-column resolution deck: 512 | 1K | 2K | 4K | 8K
        constexpr std::array<std::pair<const char*, uint32_t>, 5u> ResDeck {{
            { "512", 512u  },
            { "1K",  1024u },
            { "2K",  2048u },
            { "4K",  4096u },
            { "8K",  8192u },
        }};
        const float ResW = (SW - 12.0f) / 5.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (ResW + 3.0f), SY, ResW, 26.0f },
                    ResDeck[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    TileCfg.ResolutionPx == ResDeck[Idx].second,
                    false,
                    9.2f,
                    8.0f))
            {
                TileCfg.ResolutionPx      = ResDeck[Idx].second;
                Session->FramebuffersDirty = true;
            }
        }
        SY += 34.0f;

        // 4-column Backing selector
        constexpr std::array<std::pair<const char*, GeometricRaster::UVBackdropPreset>, 4u> Backings {{
            { "Slate",      GeometricRaster::UVBackdropPreset::None             },
            { "ColorCheck", GeometricRaster::UVBackdropPreset::ColorChecker     },
            { "Trim Atlas", GeometricRaster::UVBackdropPreset::TrimSheetAtlas   },
            { "Aniso Pack", GeometricRaster::UVBackdropPreset::BrushedAnisoPack },
        }};
        const float BackW = (SW - 9.0f) / 4.0f;
        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            if (ConstructCapsuleButton(
                    Surface,
                    { SX + static_cast<float>(Idx) * (BackW + 3.0f), SY, BackW, 26.0f },
                    Backings[Idx].first,
                    Theme,
                    Cursor,
                    Pressed,
                    TileCfg.BackdropPreset == Backings[Idx].second,
                    false,
                    8.8f,
                    8.0f))
            {
                TileCfg.BackdropPreset     = Backings[Idx].second;
                Session->FramebuffersDirty = true;
            }
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderShadingTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;
    const bool          Down    = Input.PrimaryPointer.Down;

    float CurY = StartY;

    // 1. .pz-shading-hero Banner
    {
        const Rectangle2D Hero{ LeftX, CurY, InnerW, 76.0f };
        Surface.FillRectangle(Hero, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(Hero, Theme.BorderStrong, 20.0f, 1.0f);

        ConstructMatCapOrbPreview(Surface, { Hero.X + 34.0f, Hero.Y + 38.0f }, 20.0f, SelectedMatCap, true, Theme);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium, { Hero.X + 66.0f, Hero.Y + 16.0f }, 12.5f, Theme.TextPrimary, "Studio + Multi-MatCap");
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 66.0f, Hero.Y + 36.0f }, 9.2f, Theme.TextMuted, "GGX Direct Lit + 16 Procedural MatCaps");

        char SubLine[64]{};
        std::snprintf(SubLine, sizeof(SubLine), "%s tris · UDIM %u", FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str(), Session->ActiveUDIMNumber);
        WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Hero.X + 66.0f, Hero.Y + 52.0f }, 8.8f, Theme.AccentFill, SubLine);

        CurY += Hero.Height + 10.0f;
    }

    // 2. 2x2 Bento Telemetry Grid (.pz-bento-grid)
    {
        const float Gap   = 8.0f;
        const float TileW = (InnerW - Gap) * 0.5f;
        const float TileH = 54.0f;

        char OvStr[32]{}, StrStr[32]{}, ResStr[32]{}, SurfStr[32]{};
        std::snprintf(OvStr, sizeof(OvStr), "%u Overlaps", Session->LatestMetrics.OverlapFaceCount);
        std::snprintf(StrStr, sizeof(StrStr), "%.2fx", Session->LatestMetrics.MeanL2Stretch);
        std::snprintf(ResStr, sizeof(ResStr), "%u px", Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber).ResolutionPx);
        std::snprintf(SurfStr, sizeof(SurfStr), "%s f", FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str());

        const std::array<std::pair<const char*, const char*>, 4u> Bento {{
            { "UV Health",    OvStr   },
            { "Mean Stretch", StrStr  },
            { "Atlas Res",    ResStr  },
            { "Surface",      SurfStr },
        }};

        for (uint32_t Idx = 0u; Idx < 4u; ++Idx)
        {
            const float BX = LeftX + static_cast<float>(Idx % 2u) * (TileW + Gap);
            const float BY = CurY  + static_cast<float>(Idx / 2u) * (TileH + Gap);
            const Rectangle2D Cell{ BX, BY, TileW, TileH };
            Surface.FillRectangle(Cell, Theme.SectionBg, 14.0f);
            Surface.StrokeRectangle(Cell, Theme.BorderSubtle, 14.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Regular, { Cell.X + 12.0f, Cell.Y + 9.0f }, 8.8f, Theme.TextMuted, Bento[Idx].first);
            WorkspaceAppearance::ConstructWeightedText(Surface, FontWeightClass::Medium,  { Cell.X + 12.0f, Cell.Y + 26.0f }, 12.5f, (Idx == 0u && Session->LatestMetrics.OverlapFaceCount == 0u) ? Theme.ReadyGreen : Theme.TextPrimary, Bento[Idx].second);
        }
        CurY += (TileH * 2.0f + Gap) + 10.0f;
    }

    // 3. Surface Finish & 16-Orb MatCap color set
    {
        const Rectangle2D SecRect{ LeftX, CurY, InnerW, 236.0f };
        Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
        Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

        const float SX = SecRect.X + 16.0f;
        const float SW = SecRect.Width - 32.0f;
        float       SY = SecRect.Y + 14.0f;

        ConstructSectionHeading(Surface, SX, SY, SW, "Surface Finish & MatCaps", Theme);
        SY += 26.0f;

        // 8x2 MatCap Sphere Grid
        const float ColStep = SW / 8.0f;
        for (uint32_t Slot = 0u; Slot < 16u; ++Slot)
        {
            const uint32_t Col = Slot % 8u;
            const uint32_t Row = Slot / 8u;
            const Point2D  Ctr { SX + ColStep * (static_cast<float>(Col) + 0.5f), SY + 13.0f + static_cast<float>(Row) * 32.0f };
            ConstructMatCapOrbPreview(Surface, Ctr, 11.5f, Slot, SelectedMatCap == Slot, Theme);

            const Rectangle2D Hit{ Ctr.X - 13.0f, Ctr.Y - 13.0f, 26.0f, 26.0f };
            if (Hit.Contains(Cursor) && Pressed)
            {
                SelectedMatCap = static_cast<uint8_t>(Slot);
                Session->ApplyMatCapToActiveSelection(SelectedMatCap);
            }
        }
        SY += 70.0f;

        char MetTxt[24]{}, RghTxt[24]{}, BldTxt[24]{};
        std::snprintf(MetTxt, sizeof(MetTxt), "%.2f", SurfaceMetallic);
        std::snprintf(RghTxt, sizeof(RghTxt), "%.2f", SurfaceRoughness);
        std::snprintf(BldTxt, sizeof(BldTxt), "%.0f%%", SurfaceMatBlend * 100.0f);

        bool MaterialDirty = false;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Metallic", MetTxt, SurfaceMetallic, Theme, Cursor, Down, Pressed);
        SY += 34.0f;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "Roughness", RghTxt, SurfaceRoughness, Theme, Cursor, Down, Pressed);
        SY += 34.0f;
        MaterialDirty |= ConstructSliderControl(Surface, { SX, SY, SW, 28.0f }, "MatCap Blend", BldTxt, SurfaceMatBlend, Theme, Cursor, Down, Pressed);

        if (MaterialDirty)
        {
            Session->ApplySurfaceRoughnessMetallic(SurfaceRoughness, SurfaceMetallic, SurfaceMatBlend);
        }

        CurY += SecRect.Height + 12.0f;
    }

    return CurY - StartY;
}

float UVInspectorPanel::RenderCheckTab(
    PixelSpace&             Surface,
    float                   LeftX,
    float                   StartY,
    float                   InnerW,
    const PaneInputReading& Input)
{
    using namespace UVAppearance;
    const UVThemeColors Theme   = QueryThemeColors();
    const Point2D       Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool          Pressed = Input.PrimaryPointer.Pressed;

    float CurY = StartY;

    char OvStr[24]{}, FlpStr[24]{}, StrStr[24]{};
    std::snprintf(OvStr, sizeof(OvStr), "%u", Session->LatestMetrics.OverlapFaceCount);
    std::snprintf(FlpStr, sizeof(FlpStr), "%u", Session->LatestMetrics.FlippedFaceCount);
    std::snprintf(StrStr, sizeof(StrStr), "%.2f", Session->LatestMetrics.MeanL2Stretch);

    ConstructMetricTrio(
        Surface,
        { LeftX, CurY, InnerW, 56.0f },
        OvStr,  "Overlapping Faces",
        FlpStr, "Flipped Winding",
        StrStr, "Mean L2 Stretch",
        Theme);
    CurY += 66.0f;

    const Rectangle2D SecRect{ LeftX, CurY, InnerW, 148.0f };
    Surface.FillRectangle(SecRect, Theme.SectionBg, 20.0f);
    Surface.StrokeRectangle(SecRect, Theme.BorderSubtle, 20.0f, 1.0f);

    const float SX = SecRect.X + 16.0f;
    const float SW = SecRect.Width - 32.0f;
    float       SY = SecRect.Y + 14.0f;

    ConstructSectionHeading(Surface, SX, SY, SW, "UV Integrity & Repair", Theme, "VULKAN SAT");
    SY += 26.0f;

    const float HalfW = (SW - 6.0f) * 0.5f;
    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 28.0f }, "Select Overlaps", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectOverlappingFaces, nullptr);
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 28.0f }, "Select Flipped", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::SelectFlippedFaces, nullptr);
    }
    SY += 34.0f;

    if (ConstructCapsuleButton(Surface, { SX, SY, HalfW, 30.0f }, "Run GPU Inspection", Theme, Cursor, Pressed, false, false, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::ValidateDistortionGPU, nullptr);
    }
    if (ConstructCapsuleButton(Surface, { SX + HalfW + 6.0f, SY, HalfW, 30.0f }, "Auto-Relax & Repack", Theme, Cursor, Pressed, false, true, 9.6f))
    {
        (void)Session->ExecuteDirective(UVCommandDirective::RelaxStretchGPU, nullptr);
        (void)Session->ExecuteDirective(UVCommandDirective::PackUVsConcaveSkyline, nullptr);
    }

    CurY += SecRect.Height + 12.0f;
    return CurY - StartY;
}

void UVInspectorPanel::Render(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const PaneInputReading& Input,
    double                  Δt)
{
    (void)Δt;
    using namespace UVAppearance;
    const UVThemeColors Theme = QueryThemeColors();

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, Theme.PanelBg);

    if (Session == nullptr)
    {
        Surface.PopClip();
        return;
    }

    const Point2D Cursor  { Input.PrimaryPointer.X, Input.PrimaryPointer.Y };
    const bool    Pressed = Input.PrimaryPointer.Pressed;

    const float PadX   = 18.0f;
    const float LeftX  = Bounds.X + PadX;
    const float InnerW = std::max(150.0f, Bounds.Width - PadX * 2.0f);
    float       CurY   = Bounds.Y + 14.0f;

    // 1. Breadcrumb + "+ Export OBJ" + "Reset"
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY + 4.0f },
        InnerW - 140.0f,
        9.0f,
        Theme.TextMuted,
        "Inspector / Surface & MatCap");

    const Rectangle2D ResetBtn  { LeftX + InnerW - 48.0f, CurY, 48.0f, 22.0f };
    const Rectangle2D ExportBtn { ResetBtn.X - 84.0f, CurY, 78.0f, 22.0f };
    if (ConstructCapsuleButton(Surface, ExportBtn, "+ Export OBJ", Theme, Cursor, Pressed, false, false, 8.8f))
    {
        std::string Diag {};
        (void)Session->ExportExternalGeometry(std::filesystem::temp_directory_path() / "FrontierExport.obj", Diag);
    }
    if (ConstructCapsuleButton(Surface, ResetBtn, "Reset", Theme, Cursor, Pressed, false, false, 8.8f))
    {
        Session->LoadSamplePreset(Session->ActivePreset);
    }
    CurY += 28.0f;

    // 2. Large 23px Light Selection / Asset Heading + Subtitle (matching InspectorPanel.cpp & Experimental/UVEditor)
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Light,
        { LeftX, CurY },
        InnerW,
        23.0f,
        Theme.TextPrimary,
        Session->Asset.Network.NetworkTitle);
    CurY += 29.0f;

    char SubMeta[112]{};
    const auto TileCfg = Session->QueryUDIMTileSetting(Session->ActiveUDIMNumber);
    std::snprintf(
        SubMeta,
        sizeof(SubMeta),
        "UDIM %u · %s faces · %u px",
        Session->ActiveUDIMNumber,
        FormatCountWithCommas(Session->Asset.Network.Faces.size()).c_str(),
        TileCfg.ResolutionPx);
    WorkspaceAppearance::ConstructTruncatedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX, CurY },
        InnerW,
        9.8f,
        Theme.TextMuted,
        SubMeta);
    CurY += 22.0f;

    // 3. 5-Tab Segmented Bar (.inspector-tabs: Pack | Unwrap | UDIM | Shading | Check)
    {
        const Rectangle2D TabBar{ LeftX, CurY, InnerW, 34.0f };
        Surface.FillRectangle(TabBar, Theme.WellBg, 11.0f);
        Surface.StrokeRectangle(TabBar, Theme.BorderSubtle, 11.0f, 1.0f);

        constexpr std::array<const char*, 5u> TabNames {{ "Pack", "Unwrap", "UDIM", "Shading", "Check" }};
        const float SegW = (InnerW - 6.0f) / 5.0f;
        for (uint32_t Idx = 0u; Idx < 5u; ++Idx)
        {
            const Rectangle2D Seg{ TabBar.X + 3.0f + static_cast<float>(Idx) * SegW, TabBar.Y + 3.0f, SegW, 28.0f };
            const auto        TargetTab = static_cast<UVInspectorTab>(Idx);
            const bool        Active    = (Session->ActiveInspectorTab == TargetTab);
            const bool        Hovered   = Seg.Contains(Cursor);

            if (Active)
            {
                Surface.FillRectangle(Seg, Theme.RaisedBg, 8.0f);
                Surface.StrokeRectangle(Seg, Theme.AccentBorder, 8.0f, 1.0f);
            }
            else if (Hovered)
            {
                Surface.FillRectangle(Seg, Theme.HoverBg, 8.0f);
            }

            const float TW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.8f, TabNames[Idx]);
            WorkspaceAppearance::ConstructWeightedText(
                Surface,
                FontWeightClass::Regular,
                { Seg.X + (Seg.Width - TW) * 0.5f, Seg.Y + 7.0f },
                9.8f,
                Active ? Theme.TextPrimary : Theme.TextMuted,
                TabNames[Idx]);

            if (Hovered && Pressed)
            {
                Session->ActiveInspectorTab = TargetTab;
                ScrollOffsetY               = 0.0f;
            }
        }
        CurY += 44.0f;
    }

    // 4. Scrollable Active Tab Body above Sticky 40px Inspector Footer
    constexpr float   FooterH = 40.0f;
    const Rectangle2D ScrollRect{
        Bounds.X,
        CurY,
        Bounds.Width,
        std::max(40.0f, Bounds.Bottom() - FooterH - CurY)
    };

    if (ScrollRect.Contains(Cursor) && std::abs(Input.WheelDeltaY) > 1.0e-4f)
    {
        ScrollOffsetY = std::max(0.0f, ScrollOffsetY - Input.WheelDeltaY * 28.0f);
    }

    Surface.PushClip(ScrollRect);
    const float BodyStartY = ScrollRect.Y - ScrollOffsetY;
    float       ContentH   = 0.0f;

    switch (Session->ActiveInspectorTab)
    {
        case UVInspectorTab::Packing: ContentH = RenderPackingTab(Surface, LeftX, BodyStartY, InnerW, Input); break;
        case UVInspectorTab::Unwrap:  ContentH = RenderUnwrapTab(Surface, LeftX, BodyStartY, InnerW, Input);  break;
        case UVInspectorTab::UDIM:    ContentH = RenderUDIMTab(Surface, LeftX, BodyStartY, InnerW, Input);    break;
        case UVInspectorTab::Shading: ContentH = RenderShadingTab(Surface, LeftX, BodyStartY, InnerW, Input); break;
        case UVInspectorTab::Check:   ContentH = RenderCheckTab(Surface, LeftX, BodyStartY, InnerW, Input);   break;
    }

    const float MaxScroll = std::max(0.0f, ContentH - ScrollRect.Height);
    if (ScrollOffsetY > MaxScroll)
    {
        ScrollOffsetY = MaxScroll;
    }
    Surface.PopClip();

    // 5. Sticky 40px Inspector Footer (.pz-inspector-foot)
    const Rectangle2D FootRect{ Bounds.X, Bounds.Bottom() - FooterH, Bounds.Width, FooterH };
    Surface.FillRectangle(FootRect, Theme.PanelBg);
    Surface.StrokeLine({ FootRect.X, FootRect.Y }, { FootRect.Right(), FootRect.Y }, Theme.BorderSubtle, 1.0f);

    Surface.FillCircle({ LeftX + 5.0f, FootRect.Y + 20.0f }, 3.0f, Theme.ReadyGreen);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + 14.0f, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        "Studio + Multi-MatCap");

    char FootRight[48]{};
    std::snprintf(FootRight, sizeof(FootRight), "UDIM %u · %u px", Session->ActiveUDIMNumber, TileCfg.ResolutionPx);
    const float FRW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Regular, 9.5f, FootRight);
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { LeftX + InnerW - FRW, FootRect.Y + 13.0f },
        9.5f,
        Theme.TextMuted,
        FootRight);

    Surface.PopClip();
}

void UVInspectorPanel::PresentInImGuiPane(double Δt, float ScaleFactor)
{
    const float  SafeScale = std::max(0.25f, ScaleFactor);
    const ImVec2 WinPos    = ImGui::GetWindowPos();
    const ImVec2 WinSize   = ImGui::GetWindowSize();
    const ImVec2 CursorPos = ImGui::GetCursorScreenPos();
    const float  TopY      = CursorPos.y - ImGui::GetStyle().WindowPadding.y;
    const float  PaneH     = std::max(1.0f, (WinPos.y + WinSize.y) - TopY);

    ImGui::InvisibleButton(
        "##UVInspectorPaneCanvas",
        ImVec2(std::max(1.0f, ImGui::GetContentRegionAvail().x), std::max(1.0f, ImGui::GetContentRegionAvail().y)),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    const Rectangle2D ScreenBounds{ WinPos.x, TopY, WinSize.x, PaneH };
    const Rectangle2D LogicalBounds{ 0.0f, 0.0f, WinSize.x / SafeScale, PaneH / SafeScale };

    PixelSpace             Surface(ImGui::GetWindowDrawList(), { ScreenBounds.X, ScreenBounds.Y }, SafeScale);
    const PaneInputReading Input = WorkspaceAppearance::CaptureImGuiPaneInput(ScreenBounds, SafeScale);
    Render(Surface, LogicalBounds, Input, Δt);
}

} // namespace Frontier::Workspaces
