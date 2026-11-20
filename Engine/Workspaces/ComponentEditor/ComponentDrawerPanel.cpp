/*============================================================================================================================================
                                                        COMPONENTDRAWERPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentDrawerPanel matching HtmlReference/04-Drawer-ResponseCurves-Simulate.png

#include "Engine/Workspaces/ComponentEditor/ComponentDrawerPanel.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/SimulationSpace/PhysicsSpace/VehicleDynamicsSolver.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Frontier::Workspaces::ComponentEditor
{

void ComponentDrawerPanel::Render(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    ComponentWorkspaceSession& Session,
    const PaneInputReading&    Input) noexcept
{
    auto* Doc = Session.AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }
    const ControlPointer& Ptr = Input.PrimaryPointer;

    if (!Session.DrawerExpanded)
    {
        // Collapsed bottom strip with "Compiled payload" handle pill
        Surface.FillRectangle(Bounds, ColorRgba { 14u, 14u, 16u, 255u }, 0.0f);
        Surface.StrokeLine({ Bounds.X, Bounds.Y }, { Bounds.Right(), Bounds.Y }, WorkspaceSurfaceColors::HairlineLight, 1.0f);
        char StripBuf[96] {};
        std::snprintf(
            StripBuf, sizeof(StripBuf), "COMPONENT EDITOR · %s · %s · %zu ASSETS",
            Doc->ClassDefinition.Title.c_str(),
            Session.SimulationActive ? "SIMULATING" : "LIT · MATERIAL SLOTS",
            Session.Repository.size());
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Bounds.X + 14.0f, Bounds.Y + 9.0f }, 8.8f,
            WorkspaceSurfaceColors::FaintInk, StripBuf, true);

        const Rectangle2D HandleButton { Bounds.Center().X - 72.0f, Bounds.Y + 2.0f, 144.0f, 24.0f };
        if (CapsuleButton::Construct(Surface, HandleButton, ButtonDescriptor{ "Compiled payload", ButtonTone::Secondary, 10.2f, 10.0f }, Ptr).Clicked)
        {
            Session.DrawerExpanded = true;
        }
        return;
    }

    // Expanded Drawer
    Surface.FillRectangle(Bounds, ColorRgba { 16u, 16u, 18u, 255u }, 0.0f);
    Surface.StrokeLine({ Bounds.X, Bounds.Y }, { Bounds.Right(), Bounds.Y }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    const float PadX = 16.0f;
    const Rectangle2D TopRow { Bounds.X + PadX, Bounds.Y + 8.0f, Bounds.Width - PadX * 2.0f, 28.0f };
    const char* HeaderTitle = (Session.ActiveDrawerTab == BottomDrawerTab::CompiledPayload)
        ? "Compiled payload · Geometry asset diagnostics"
        : "Response curves · Vehicle Movement";
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { TopRow.X, TopRow.Y + 6.0f }, 11.4f,
        WorkspaceSurfaceColors::PrimaryInk, HeaderTitle);

    const Rectangle2D CollapseBtn { TopRow.Right() - 28.0f, TopRow.Y, 28.0f, 26.0f };
    if (CapsuleButton::Construct(Surface, CollapseBtn, ButtonDescriptor{ "v", ButtonTone::Secondary, 10.5f, 6.0f }, Ptr).Clicked)
    {
        Session.DrawerExpanded = false;
    }

    const Rectangle2D TabSwitch { CollapseBtn.X - 242.0f, TopRow.Y, 232.0f, 26.0f };
    static constexpr std::array<const char*, 2> Tabs {{ "Compiled payload", "Response curves" }};
    uint32_t TabIdx = static_cast<uint32_t>(Session.ActiveDrawerTab);
    uint32_t NextTabIdx = TabIdx;
    if (CapsuleButton::ConstructSegmented(Surface, TabSwitch, Tabs.data(), static_cast<uint32_t>(Tabs.size()), TabIdx, Ptr, NextTabIdx).Clicked)
    {
        Session.ActiveDrawerTab = static_cast<BottomDrawerTab>(NextTabIdx);
    }

    const Rectangle2D BodyRect { Bounds.X + PadX, TopRow.Bottom() + 8.0f, Bounds.Width - PadX * 2.0f, std::max(80.0f, Bounds.Bottom() - TopRow.Bottom() - 16.0f) };

    if (Session.ActiveDrawerTab == BottomDrawerTab::CompiledPayload)
    {
        const float TileW = (BodyRect.Width - 32.0f) / 5.0f;
        const float TileH = std::min(BodyRect.Height, 132.0f);
        struct SummaryTile { const char* Title; const char* Val; const char* Sub; };
        char AssetCountBuf[32] {};
        std::snprintf(AssetCountBuf, sizeof(AssetCountBuf), "%zu assets", Session.Repository.size());
        const std::array<SummaryTile, 5> Tiles {{
            { "Icon",        Doc->IconBaked ? "128x128" : "Pending", "OpenPbrDirect cutout" },
            { "UV charts",   "Ready",                                AssetCountBuf },
            { "Wide BVH4",   "Ready",                                "4-wide ray hierarchy" },
            { "Cluster LOD", "4 tiers",                              "100% -> 25% -> 6%" },
            { "Collision",   "Hulls",                                "JoltPhysics v5.6.0" },
        }};
        for (std::size_t I = 0u; I < Tiles.size(); ++I)
        {
            const Rectangle2D TR { BodyRect.X + static_cast<float>(I) * (TileW + 8.0f), BodyRect.Y, TileW, TileH };
            Surface.FillRectangle(TR, ColorRgba { 22u, 22u, 25u, 255u }, 10.0f);
            Surface.StrokeRectangle(TR, ColorRgba { 44u, 44u, 48u, 255u }, 10.0f, 1.0f);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { TR.X + 10.0f, TR.Y + 10.0f }, 10.4f, WorkspaceSurfaceColors::SecondaryInk, Tiles[I].Title);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { TR.X + 10.0f, TR.Y + 34.0f }, 15.0f, WorkspaceSurfaceColors::PrimaryInk, Tiles[I].Val);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { TR.X + 10.0f, TR.Bottom() - 22.0f }, TR.Width - 20.0f, 9.0f,
                WorkspaceSurfaceColors::MutedInk, Tiles[I].Sub);
        }
        return;
    }

    // Response Curves tab: VehicleDynamics Engine Torque curve
    ComponentClass::VehicleDynamicsSettings* VehPtr = nullptr;
    std::vector<ComponentClass::ComponentInstanceToken> Behaviours {};
    Doc->ClassDefinition.Storage.CollectBehaviourTokens(Behaviours);
    for (const auto BTok : Behaviours)
    {
        if (auto* V = Doc->ClassDefinition.Storage.ResolveVehicle(BTok))
        {
            VehPtr = V;
            break;
        }
    }

    if (VehPtr == nullptr)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { BodyRect.X, BodyRect.Y + 18.0f }, 11.0f,
            WorkspaceSurfaceColors::MutedInk, "Add a VehicleDynamics behaviour component to edit the Engine torque response curve.");
        return;
    }

    // Top controls: Interpolation selector + Reset
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { BodyRect.X, BodyRect.Y + 2.0f }, 11.0f,
        WorkspaceSurfaceColors::PrimaryInk, "Engine torque");

    const Rectangle2D InterpRect { BodyRect.Right() - 260.0f, BodyRect.Y - 2.0f, 240.0f, 24.0f };
    static constexpr std::array<const char*, 4> InterpLabels {{ "Linear", "Smooth", "Ease", "Step" }};
    uint32_t InterpIdx = static_cast<uint32_t>(VehPtr->Interpolation);
    uint32_t NextInterp = InterpIdx;
    if (CapsuleButton::ConstructSegmented(Surface, InterpRect, InterpLabels.data(), static_cast<uint32_t>(InterpLabels.size()), InterpIdx, Ptr, NextInterp).Clicked)
    {
        VehPtr->Interpolation = static_cast<ComponentClass::CurveInterpolationForm>(NextInterp);
        Session.MarkActiveDirty();
    }

    const Rectangle2D PlotRect { BodyRect.X, BodyRect.Y + 28.0f, BodyRect.Width, std::max(72.0f, BodyRect.Height - 30.0f) };
    ScalarCurveProfile Profile {};
    Profile.Knots.clear();
    const float MaxRpmPlot = 9000.0f;
    const float MaxNmPlot  = 650.0f;
    for (const auto& Pt : VehPtr->TorqueCurve)
    {
        Profile.Knots.push_back({
            std::clamp(Pt.Rpm / MaxRpmPlot, 0.0f, 1.0f),
            std::clamp(Pt.TorqueNm / MaxNmPlot, 0.0f, 1.0f),
        });
    }
    if (ScalarCurveEditor::Construct(
            Surface, PlotRect, "N·m vs RPM", Profile, CurveSession, Ptr, 1.0f))
    {
        VehPtr->TorqueCurve.clear();
        for (const auto& K : Profile.Knots)
        {
            VehPtr->TorqueCurve.push_back({ K.PositionT * MaxRpmPlot, K.Amplitude * MaxNmPlot });
        }
        Session.MarkActiveDirty();
    }

    // Redline shaded zone + Live amber RPM needle
    const float RedlineX = PlotRect.X + std::clamp(VehPtr->RedlineRpm / MaxRpmPlot, 0.0f, 1.0f) * PlotRect.Width;
    Surface.FillRectangle(
        { RedlineX, PlotRect.Y + 18.0f, std::max(0.0f, PlotRect.Right() - RedlineX), std::max(0.0f, PlotRect.Height - 24.0f) },
        ColorRgba { 168u, 42u, 42u, 46u }, 0.0f);

    const float LiveRpmX = PlotRect.X + std::clamp(Session.VehicleTelemetry.Rpm / MaxRpmPlot, 0.0f, 1.0f) * PlotRect.Width;
    Surface.StrokeLine(
        { LiveRpmX, PlotRect.Y + 18.0f },
        { LiveRpmX, PlotRect.Bottom() - 6.0f },
        WorkspaceSurfaceColors::SelectionAmber,
        2.0f);
}

} // namespace Frontier::Workspaces::ComponentEditor
