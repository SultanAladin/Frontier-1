/*============================================================================================================================================
                                                        COMPONENTVIEWPORTHOST.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentViewportHost matching HtmlReference/01, 02, 03, 04, 05

#include "Engine/Workspaces/ComponentEditor/ComponentViewportHost.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Panes/AssemblySequencePane.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::ComponentEditor
{

namespace
{

const char* QueryChannelNote(GeometricRaster::ComponentEditor::ComponentInspectionChannel Ch) noexcept
{
    using C = GeometricRaster::ComponentEditor::ComponentInspectionChannel;
    switch (Ch)
    {
        case C::Lit:         return "Lit · material slots";
        case C::Unlit:       return "Unlit · flat albedo";
        case C::Density:     return "Triangle density (sparse -> dense)";
        case C::Normals:     return "World normals (n * 0.5 + 0.5)";
        case C::UvChecker:   return "8x8 UV checker · #7a2d5a missing UV";
        case C::LodClusters: return "Cluster LOD partitions";
        case C::Collision:   return "Compiled collision (green) · component (amber)";
    }
    return "";
}

const char* QueryRoleCaption(ComponentClass::ComponentClassRole Role) noexcept
{
    return ComponentClass::ComponentClassDefinition::QueryRoleLabel(Role);
}

} // namespace

void ComponentViewportHost::AttachRenderer(
    GeometricRaster::ComponentEditor::ComponentViewportRenderer* RendererPtr) noexcept
{
    Renderer = RendererPtr;
}

void ComponentViewportHost::UploadRgbaTexture(const std::vector<uint8_t>& Rgba, uint32_t W, uint32_t H) noexcept
{
    if (W == 0u || H == 0u || Rgba.size() < static_cast<std::size_t>(W) * H * 4u)
    {
        return;
    }
    if (ViewportTexture.Pixels == nullptr ||
        ViewportTexture.Width != static_cast<int>(W) ||
        ViewportTexture.Height != static_cast<int>(H))
    {
        ViewportTexture.DestroyPixels();
        ViewportTexture.SetStatus(ImTextureStatus_Destroyed);
        ViewportTexture.Create(ImTextureFormat_RGBA32, static_cast<int>(W), static_cast<int>(H));
    }
    std::memcpy(ViewportTexture.GetPixels(), Rgba.data(), static_cast<std::size_t>(W) * H * 4u);
    if (ViewportTexture.Status == ImTextureStatus_OK)
    {
        ViewportTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(W), static_cast<unsigned short>(H) };
        ViewportTexture.SetStatus(ImTextureStatus_WantUpdates);
    }
}

void ComponentViewportHost::Render(
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
    const float PadX = 14.0f;

    // 1. Class Header Bar (52px)
    const Rectangle2D HeaderRect { Bounds.X, Bounds.Y, Bounds.Width, 54.0f };
    Surface.FillRectangle(HeaderRect, WorkspaceSurfaceColors::DockBackground, 0.0f);
    Surface.StrokeLine({ HeaderRect.X, HeaderRect.Bottom() }, { HeaderRect.Right(), HeaderRect.Bottom() }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    // Class icon tile + Title + Subtitle
    const Rectangle2D ClassIconTile { HeaderRect.X + PadX, HeaderRect.Y + 12.0f, 24.0f, 24.0f };
    Surface.FillRectangle(ClassIconTile, ColorRgba { 28u, 30u, 34u, 255u }, 6.0f);
    Surface.StrokeRectangle(ClassIconTile, ColorRgba { 58u, 60u, 66u, 255u }, 6.0f, 1.0f);
    VectorIconRepository::ConstructControl(
        Surface, ControlIcon::CubeShape, { ClassIconTile.X + 5.0f, ClassIconTile.Y + 5.0f, 14.0f, WorkspaceSurfaceColors::SecondaryInk, 1.5f });

    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::SemiBold, { ClassIconTile.Right() + 10.0f, HeaderRect.Y + 9.0f }, 13.5f,
        WorkspaceSurfaceColors::PrimaryInk, Doc->ClassDefinition.Title);

    const auto Metrics = Doc->ClassDefinition.Storage.ComputeSummaryMetrics();
    char SubBuf[96] {};
    std::snprintf(
        SubBuf, sizeof(SubBuf), "Component class · Role %s · %u components",
        QueryRoleCaption(Doc->ClassDefinition.ClassRole), Metrics.TotalComponents);
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Regular, { ClassIconTile.Right() + 10.0f, HeaderRect.Y + 28.0f }, 9.8f,
        WorkspaceSurfaceColors::MutedInk, SubBuf);

    // Status chip: COMPILED / DIRTY / ERRORS
    const float TitleW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::SemiBold, 13.5f, Doc->ClassDefinition.Title);
    const char* CompileLabel = !Doc->CanCompile ? "ERRORS" : (Doc->Dirty ? "DIRTY" : "COMPILED");
    const ColorRgba CompileFill = !Doc->CanCompile
        ? ColorRgba { 68u, 24u, 28u, 255u }
        : (Doc->Dirty ? ColorRgba { 66u, 48u, 18u, 255u } : ColorRgba { 20u, 52u, 32u, 255u });
    const ColorRgba CompileInk = !Doc->CanCompile
        ? ColorRgba { 255u, 118u, 118u, 255u }
        : (Doc->Dirty ? WorkspaceSurfaceColors::SelectionAmber : ColorRgba { 112u, 232u, 148u, 255u });
    const Rectangle2D CompileChip { std::max(ClassIconTile.Right() + 10.0f + TitleW + 14.0f, HeaderRect.X + 230.0f), HeaderRect.Y + 12.0f, 72.0f, 22.0f };
    if (CompileChip.Right() < HeaderRect.Right() - 320.0f)
    {
        Surface.FillRectangle(CompileChip, CompileFill, 6.0f);
        Surface.StrokeRectangle(CompileChip, CompileInk.WithAlpha(90u), 6.0f, 1.0f);
        Surface.TextCentered(CompileChip, 9.0f, CompileInk, CompileLabel);
    }

    // Right controls: Compile button + [Viewport | Assembly Sequence] switch
    const Rectangle2D CompileBtn { HeaderRect.Right() - PadX - 88.0f, HeaderRect.Y + 11.0f, 88.0f, 32.0f };
    if (CapsuleButton::Construct(
            Surface, CompileBtn, ButtonDescriptor{ "Compile", ButtonTone::Primary, 11.5f, 14.0f }, Ptr).Clicked)
    {
        Session.CompileActiveDocument();
    }

    const Rectangle2D ViewSwitchRect { CompileBtn.X - 246.0f, HeaderRect.Y + 11.0f, 236.0f, 32.0f };
    static constexpr std::array<const char*, 2> ViewLabels {{ "Viewport", "Assembly Sequence" }};
    uint32_t ActiveViewIdx = static_cast<uint32_t>(Session.ActiveCenterTab);
    uint32_t NextViewIdx   = ActiveViewIdx;
    if (CapsuleButton::ConstructSegmented(Surface, ViewSwitchRect, ViewLabels.data(), 2u, ActiveViewIdx, Ptr, NextViewIdx).Clicked)
    {
        Session.ActiveCenterTab = static_cast<CenterViewTab>(NextViewIdx);
    }

    // 2. Inspection Channel Toolbar (42px)
    const Rectangle2D BarRect { Bounds.X, HeaderRect.Bottom(), Bounds.Width, 42.0f };
    Surface.FillRectangle(BarRect, ColorRgba { 16u, 16u, 18u, 255u }, 0.0f);
    Surface.StrokeLine({ BarRect.X, BarRect.Bottom() }, { BarRect.Right(), BarRect.Bottom() }, WorkspaceSurfaceColors::HairlineLight, 1.0f);

    static constexpr std::array<const char*, 7> ChanLabels {{ "Lit", "Unlit", "Density", "Normals", "UV checker", "LOD clusters", "Collision" }};
    const float ChanAreaW = std::min(530.0f, BarRect.Width - 208.0f);
    uint32_t ActiveChan = static_cast<uint32_t>(Session.RenderOptions.Channel);
    uint32_t NextChan   = ActiveChan;
    if (CapsuleButton::ConstructSegmented(
            Surface, { BarRect.X + PadX, BarRect.Y + 6.0f, std::max(240.0f, ChanAreaW), 30.0f },
            ChanLabels.data(), static_cast<uint32_t>(ChanLabels.size()), ActiveChan, Ptr, NextChan).Clicked)
    {
        Session.RenderOptions.Channel = static_cast<GeometricRaster::ComponentEditor::ComponentInspectionChannel>(NextChan);
    }

    // Grid, Focus, Simulate/Stop buttons
    const Rectangle2D GridBtn { BarRect.Right() - PadX - 28.0f, BarRect.Y + 6.0f, 28.0f, 30.0f };
    const Rectangle2D FocusBtn { GridBtn.X - 34.0f, BarRect.Y + 6.0f, 28.0f, 30.0f };
    const Rectangle2D SimBtn { FocusBtn.X - 92.0f, BarRect.Y + 6.0f, 84.0f, 30.0f };

    // Simulate/Stop button (amber when active)
    {
        const bool SimHov = ControlAppearance::QueryPointerOver(SimBtn, Ptr);
        const ColorRgba Bg = Session.SimulationActive
            ? ColorRgba { 255u, 178u, 56u, 255u }
            : (SimHov ? ColorRgba { 36u, 36u, 40u, 255u } : ColorRgba { 26u, 26u, 29u, 255u });
        const ColorRgba Ink = Session.SimulationActive ? ColorRgba { 18u, 18u, 20u, 255u } : WorkspaceSurfaceColors::PrimaryInk;
        Surface.FillRectangle(SimBtn, Bg, 15.0f);
        Surface.StrokeRectangle(SimBtn, Session.SimulationActive ? Bg : ColorRgba { 58u, 58u, 64u, 255u }, 15.0f, 1.0f);
        Surface.TextCentered(SimBtn, 10.6f, Ink, Session.SimulationActive ? "Stop" : "Simulate");
        if (SimHov && Ptr.Pressed)
        {
            Session.ToggleSimulation();
        }
    }

    if (CapsuleButton::ConstructRoundIcon(Surface, FocusBtn.Center().X, FocusBtn.Center().Y, ControlIcon::EditFocus, Ptr).Clicked)
    {
        Session.FrameSelectedOrClass();
    }
    if (CapsuleButton::ConstructRoundIcon(Surface, GridBtn.Center().X, GridBtn.Center().Y, ControlIcon::LayoutGrid, Ptr).Clicked)
    {
        Session.RenderOptions.ShowDotFloor = !Session.RenderOptions.ShowDotFloor;
    }

    // 3. Main Canvas Area
    const Rectangle2D CanvasRect { Bounds.X, BarRect.Bottom(), Bounds.Width, std::max(140.0f, Bounds.Bottom() - BarRect.Bottom()) };

    if (Session.ActiveCenterTab == CenterViewTab::AssemblySequence)
    {
        Doc->SelectedToken = AssemblySequencePane::ConstructWithInput(
            Surface, CanvasRect, Doc->ClassDefinition, Doc->SelectedToken, Doc->SequenceView, Input);
        return;
    }

    // Viewport Camera Orbit / Zoom / PickID handling
    const bool InsideCanvas = CanvasRect.Contains({ Ptr.X, Ptr.Y });
    if (InsideCanvas && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        const float Zoom = (Input.WheelDeltaY > 0.0f) ? 0.88f : 1.13f;
        Doc->Camera.DistanceMeters = std::clamp(Doc->Camera.DistanceMeters * Zoom, 1.2f, 45.0f);
        Doc->Camera.PilotActive = false;
    }
    if (InsideCanvas && Ptr.Pressed && !Input.AltHeld)
    {
        OrbitDragging = true;
    }
    if (InsideCanvas && (Input.SecondaryPressed || Input.MiddlePressed))
    {
        PanDragging = true;
    }
    if (OrbitDragging && Ptr.Down)
    {
        if (std::abs(Input.PointerDeltaX) > 0.01f || std::abs(Input.PointerDeltaY) > 0.01f)
        {
            Doc->Camera.YawRadians   -= Input.PointerDeltaX * 0.0085f;
            Doc->Camera.PitchRadians  = std::clamp(Doc->Camera.PitchRadians + Input.PointerDeltaY * 0.0065f, -1.25f, 1.35f);
            Doc->Camera.PilotActive   = false;
        }
    }
    else if (OrbitDragging && Ptr.Released)
    {
        OrbitDragging = false;
        if (Renderer != nullptr && CachedWidthPx > 0u && CachedHeightPx > 0u &&
            std::abs(Input.PointerDeltaX) < 1.5f && std::abs(Input.PointerDeltaY) < 1.5f)
        {
            const float NormX = std::clamp((Ptr.X - CanvasRect.X) / std::max(CanvasRect.Width, 1.0f), 0.0f, 0.999f);
            const float NormY = std::clamp((Ptr.Y - CanvasRect.Y) / std::max(CanvasRect.Height, 1.0f), 0.0f, 0.999f);
            const uint32_t Px = static_cast<uint32_t>(NormX * static_cast<float>(CachedWidthPx));
            const uint32_t Py = static_cast<uint32_t>(NormY * static_cast<float>(CachedHeightPx));
            const auto Picked = Renderer->QueryPickAtPixel(Doc->ClassDefinition, Px, Py);
            if (Picked.Valid())
            {
                Doc->SelectedToken = Picked;
                if (Input.DoubleClicked)
                {
                    Session.FrameSelectedOrClass();
                }
            }
        }
    }
    if (PanDragging && (Input.SecondaryDown || Input.MiddleDown))
    {
        const float PanScale = Doc->Camera.DistanceMeters * 0.0018f;
        const float CY = std::cos(Doc->Camera.YawRadians);
        const float SY = std::sin(Doc->Camera.YawRadians);
        Doc->Camera.FocusPointMeters.X += (-SY * Input.PointerDeltaX) * PanScale;
        Doc->Camera.FocusPointMeters.Z += ( CY * Input.PointerDeltaX) * PanScale;
        Doc->Camera.FocusPointMeters.Y += Input.PointerDeltaY * PanScale;
        Doc->Camera.PilotActive = false;
    }
    else
    {
        PanDragging = false;
    }

    // Render Vulkan frame
    Session.RenderOptions.SelectedToken = Doc->SelectedToken;
    const uint32_t RenderW = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Width), 240l, 1280l));
    const uint32_t RenderH = static_cast<uint32_t>(std::clamp(std::lround(CanvasRect.Height), 160l, 800l));
    const uint64_t Sig     = Session.ComputeRenderSignature();

    if (Renderer != nullptr && Renderer->QueryVulkanReady() &&
        (Sig != CachedSignature || RenderW != CachedWidthPx || RenderH != CachedHeightPx || ViewportTexture.Pixels == nullptr))
    {
        const auto Out = Renderer->RenderClassFrame(
            Doc->ClassDefinition,
            Session.Catalog,
            Doc->Camera,
            Session.RenderOptions,
            RenderW,
            RenderH,
            true);
        Session.LastRenderMs   = Out.RenderMillisec;
        Session.LastFps        = (Out.RenderMillisec > 0.1f) ? std::min(120.0f, 1000.0f / Out.RenderMillisec) : 60.0f;
        Session.LastDrawnTris  = Out.DrawnTriangles;
        Session.LastDrawnGeoms = Out.DrawnGeometries;
        UploadRgbaTexture(Out.DisplayRgba8, Out.WidthPx, Out.HeightPx);
        CachedSignature = Sig;
        CachedWidthPx   = RenderW;
        CachedHeightPx  = RenderH;
    }

    Surface.PushClip(CanvasRect);
    Surface.FillRectangle(CanvasRect, ColorRgba { 16u, 17u, 20u, 255u }, 0.0f);
    if (ViewportTexture.Pixels != nullptr)
    {
        Surface.FillTexturedRectangle(CanvasRect, &ViewportTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 0.0f);
    }

    // Top-left mono telemetry
    {
        const auto* SelHdr = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken);
        char Line1[64] {}, Line2[64] {}, Line3[64] {}, Line4[64] {};
        std::snprintf(Line1, sizeof(Line1), "%3.0f fps   %.1f ms", Session.LastFps, Session.LastRenderMs);
        std::snprintf(Line2, sizeof(Line2), "tris      %.1fk", static_cast<double>(Session.LastDrawnTris) / 1000.0);
        std::snprintf(Line3, sizeof(Line3), "geoms     %u · %u components", Session.LastDrawnGeoms, Metrics.TotalComponents);
        std::snprintf(Line4, sizeof(Line4), "select    %s", (SelHdr != nullptr) ? SelHdr->Title.c_str() : "None");

        const float Tx = CanvasRect.X + 16.0f;
        float Ty = CanvasRect.Y + 12.0f;
        for (const char* Line : { Line1, Line2, Line3, Line4 })
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Tx, Ty }, 10.2f, ColorRgba { 208u, 212u, 220u, 225u }, Line, true);
            Ty += 16.0f;
        }
    }

    // Top-right channel note & legend
    WorkspaceAppearance::ConstructRightAlignedText(
        Surface, FontWeightClass::Regular, { CanvasRect.Right() - 16.0f, CanvasRect.Y + 12.0f }, 10.2f,
        ColorRgba { 186u, 190u, 200u, 220u }, QueryChannelNote(Session.RenderOptions.Channel));

    // Bottom-right Axis Orb + 35mm + Turntable
    {
        const Point2D OrbC { CanvasRect.Right() - 48.0f, CanvasRect.Bottom() - 62.0f };
        Surface.FillCircle(OrbC, 26.0f, ColorRgba { 16u, 18u, 22u, 196u });
        Surface.StrokeCircle(OrbC, 26.0f, ColorRgba { 52u, 56u, 64u, 200u }, 1.0f);

        const float CY = std::cos(Doc->Camera.YawRadians), SY = std::sin(Doc->Camera.YawRadians);
        const Point2D XEnd { OrbC.X + CY * 16.0f, OrbC.Y - SY * 6.0f };
        const Point2D YEnd { OrbC.X, OrbC.Y - 16.0f };
        const Point2D ZEnd { OrbC.X - SY * 16.0f, OrbC.Y - CY * 6.0f };
        Surface.StrokeLine(OrbC, XEnd, ColorRgba { 224u, 91u, 82u, 255u }, 1.6f);
        Surface.FillCircle(XEnd, 6.5f, ColorRgba { 224u, 91u, 82u, 255u });
        Surface.StrokeLine(OrbC, YEnd, ColorRgba { 84u, 196u, 104u, 255u }, 1.6f);
        Surface.FillCircle(YEnd, 6.5f, ColorRgba { 84u, 196u, 104u, 255u });
        Surface.StrokeLine(OrbC, ZEnd, ColorRgba { 84u, 141u, 224u, 255u }, 1.6f);
        Surface.FillCircle(ZEnd, 6.5f, ColorRgba { 84u, 141u, 224u, 255u });

        const Rectangle2D LensChip { CanvasRect.Right() - 90.0f, CanvasRect.Bottom() - 28.0f, 44.0f, 20.0f };
        Surface.FillRectangle(LensChip, ColorRgba { 20u, 22u, 26u, 220u }, 10.0f);
        Surface.StrokeRectangle(LensChip, ColorRgba { 52u, 56u, 64u, 210u }, 10.0f, 1.0f);
        Surface.TextCentered(LensChip, 9.2f, WorkspaceSurfaceColors::SecondaryInk, "35 mm");

        const Rectangle2D TurnBtn { LensChip.Right() + 6.0f, LensChip.Y, 20.0f, 20.0f };
        if (CapsuleButton::ConstructRoundIcon(Surface, TurnBtn.Center().X, TurnBtn.Center().Y, ControlIcon::RefreshCycle, Ptr).Clicked)
        {
            Session.RenderOptions.TurntableEnabled = !Session.RenderOptions.TurntableEnabled;
        }
    }

    // Floating ConstraintTag when a ConstraintInstance is selected
    if (const auto* SelHdr = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken);
        SelHdr != nullptr && SelHdr->Category == ComponentClass::ComponentCategory::ConstraintInstance)
    {
        if (const auto* Con = Doc->ClassDefinition.Storage.InspectConstraint(Doc->SelectedToken))
        {
            const float Coord = Session.RenderOptions.LiveConstraintCoordinates.contains(SelHdr->Token.SlotIndex)
                ? Session.RenderOptions.LiveConstraintCoordinates.at(SelHdr->Token.SlotIndex)
                : 0.0f;
            char TagBuf[96] {};
            if (Con->Kind == ComponentClass::PhysicsConstraintKind::Spring)
            {
                std::snprintf(TagBuf, sizeof(TagBuf), "SPRING  %s  %.0f mm · %.1f Hz",
                    SelHdr->Title.c_str(), Coord * 1000.0f, Con->NaturalFrequencyHz);
            }
            else if (Con->Kind == ComponentClass::PhysicsConstraintKind::Dock)
            {
                const bool Intact = Session.RenderOptions.DockIntactFlags.contains(SelHdr->Token.SlotIndex)
                    ? Session.RenderOptions.DockIntactFlags.at(SelHdr->Token.SlotIndex)
                    : Con->DockConnected;
                std::snprintf(TagBuf, sizeof(TagBuf), "DOCK  %s  %s",
                    SelHdr->Title.c_str(), Intact ? "LOCKED" : "RELEASED");
            }
            else
            {
                std::snprintf(TagBuf, sizeof(TagBuf), "CONSTRAINT  %s  %.2f", SelHdr->Title.c_str(), Coord);
            }
            const float TagW = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 9.6f, TagBuf) + 32.0f;
            const Rectangle2D ConstraintChip { CanvasRect.Right() - TagW - 18.0f, CanvasRect.Center().Y + 12.0f, TagW, 24.0f };
            Surface.FillRectangle(ConstraintChip, ColorRgba { 16u, 18u, 22u, 232u }, 12.0f);
            Surface.StrokeRectangle(ConstraintChip, ColorRgba { 58u, 72u, 62u, 220u }, 12.0f, 1.0f);
            Surface.FillCircle({ ConstraintChip.X + 12.0f, ConstraintChip.Center().Y }, 4.0f, ColorRgba { 88u, 214u, 128u, 255u });
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { ConstraintChip.X + 22.0f, ConstraintChip.Y + 6.0f }, 9.6f,
                WorkspaceSurfaceColors::PrimaryInk, TagBuf, false);
        }
    }

    // Simulation telemetry bar when Simulate is active
    if (Session.SimulationActive)
    {
        const float SimBarW = std::min(CanvasRect.Width - 130.0f, 520.0f);
        const Rectangle2D SimBar { CanvasRect.X + 16.0f, CanvasRect.Bottom() - 56.0f, SimBarW, 44.0f };
        Surface.FillRectangle(SimBar, ColorRgba { 16u, 18u, 22u, 236u }, 10.0f);
        Surface.StrokeRectangle(SimBar, ColorRgba { 62u, 64u, 70u, 220u }, 10.0f, 1.0f);

        const Rectangle2D ThrBtn { SimBar.X + 8.0f, SimBar.Y + 8.0f, 68.0f, 26.0f };
        const Rectangle2D BrkBtn { ThrBtn.Right() + 6.0f, SimBar.Y + 8.0f, 56.0f, 26.0f };
        (void)CapsuleButton::ConstructChip(Surface, ThrBtn, "Throttle", Session.ThrottleInput > 0.5f, Ptr);
        if (ControlAppearance::QueryPointerOver(ThrBtn, Ptr) && Ptr.Down)
        {
            Session.ThrottleInput = 1.0f;
        }
        else if (!Ptr.Down)
        {
            Session.ThrottleInput = 0.0f;
        }
        (void)CapsuleButton::ConstructChip(Surface, BrkBtn, "Brake", Session.BrakeInput > 0.5f, Ptr);
        if (ControlAppearance::QueryPointerOver(BrkBtn, Ptr) && Ptr.Down)
        {
            Session.BrakeInput = 1.0f;
        }
        else if (!Ptr.Down)
        {
            Session.BrakeInput = 0.0f;
        }

        char SimBuf[96] {};
        std::snprintf(
            SimBuf, sizeof(SimBuf), "%.0f km/h  %4.0f rpm  %.0f N·m  G%u",
            Session.VehicleTelemetry.SpeedMps * 3.6f,
            Session.VehicleTelemetry.Rpm,
            Session.VehicleTelemetry.EngineTorqueNm,
            Session.VehicleTelemetry.CurrentGearIndex + 1u);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Medium, { BrkBtn.Right() + 12.0f, SimBar.Y + 10.0f }, 10.2f,
            WorkspaceSurfaceColors::PrimaryInk, SimBuf, true);

        const Rectangle2D RpmTrack { BrkBtn.Right() + 12.0f, SimBar.Bottom() - 10.0f, SimBar.Right() - (BrkBtn.Right() + 24.0f), 4.0f };
        const float RpmFrac = std::clamp(Session.VehicleTelemetry.Rpm / 7200.0f, 0.0f, 1.0f);
        Surface.FillRectangle(RpmTrack, ColorRgba { 36u, 38u, 44u, 255u }, 2.0f);
        Surface.FillRectangle(
            { RpmTrack.X, RpmTrack.Y, RpmTrack.Width * RpmFrac, RpmTrack.Height },
            RpmFrac > 0.88f ? ColorRgba { 240u, 82u, 72u, 255u } : WorkspaceSurfaceColors::SelectionAmber,
            2.0f);
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces::ComponentEditor
