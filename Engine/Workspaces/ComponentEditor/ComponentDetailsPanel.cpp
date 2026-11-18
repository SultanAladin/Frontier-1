/*============================================================================================================================================
                                                        COMPONENTDETAILSPANEL.CPP
============================================================================================================================================*/
// 🧩 Implementation of ComponentDetailsPanel matching HtmlReference/01-Viewport-BPControlVehicle.png & 02-SocketDetails-XRay.png

#include "Engine/Workspaces/ComponentEditor/ComponentDetailsPanel.h"
#include "Engine/Components/Controls/CapsuleButton.h"
#include "Engine/Components/Controls/ConstraintTilePicker.h"
#include "Engine/Components/Controls/ScalarSlider.h"
#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Frontier::Workspaces::ComponentEditor
{

namespace
{

bool MatchesDetailsFilter(std::string_view SectionName, std::string_view Filter) noexcept
{
    if (Filter.empty()) return true;
    auto It = std::search(
        SectionName.begin(), SectionName.end(),
        Filter.begin(), Filter.end(),
        [](char A, char B) { return std::tolower(static_cast<unsigned char>(A)) == std::tolower(static_cast<unsigned char>(B)); });
    return It != SectionName.end();
}

} // namespace

void ComponentDetailsPanel::Render(
    PixelSpace&                Surface,
    Rectangle2D                Bounds,
    ComponentWorkspaceSession& Session,
    const PaneInputReading&    Input,
    double                     DeltaSeconds) noexcept
{
    auto* Doc = Session.AccessActiveDocument();
    if (Doc == nullptr)
    {
        return;
    }

    const ControlPointer& Ptr = Input.PrimaryPointer;
    const float PadX = 12.0f;
    const float InnerW = Bounds.Width - PadX * 2.0f;
    float Y = Bounds.Y + 10.0f;

    // 1. Breadcrumb path
    std::string Breadcrumb = Doc->ClassDefinition.Title;
    if (const auto* Sel = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken))
    {
        Breadcrumb += " / " + Sel->Title;
    }
    WorkspaceAppearance::ConstructTruncatedText(
        Surface, FontWeightClass::Regular, { Bounds.X + PadX, Y }, InnerW, 9.2f,
        WorkspaceSurfaceColors::FaintInk, Breadcrumb);
    Y += 18.0f;

    // 2. Selected Header well: Icon + Title + Duplicate / Delete
    const auto* SelHdr = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken);
    if (SelHdr != nullptr)
    {
        const Rectangle2D HeaderIconTile { Bounds.X + PadX, Y + 2.0f, 32.0f, 32.0f };
        Surface.FillRectangle(HeaderIconTile, ColorRgba { 28u, 28u, 32u, 255u }, 8.0f);
        Surface.StrokeRectangle(HeaderIconTile, ColorRgba { 52u, 52u, 58u, 255u }, 8.0f, 1.0f);
        Surface.FillCircle(HeaderIconTile.Center(), 6.0f, ColorRgba { 255u, 178u, 56u, 255u });

        const float RightBtnsX = Bounds.Right() - PadX - 60.0f;
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::SemiBold, { HeaderIconTile.Right() + 10.0f, Y + 1.0f },
            std::max(40.0f, RightBtnsX - HeaderIconTile.Right() - 14.0f), 14.5f, WorkspaceSurfaceColors::PrimaryInk, SelHdr->Title);
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { HeaderIconTile.Right() + 10.0f, Y + 20.0f },
            std::max(40.0f, RightBtnsX - HeaderIconTile.Right() - 14.0f), 9.6f, WorkspaceSurfaceColors::MutedInk,
            SelHdr->Category == ComponentClass::ComponentCategory::ConstraintInstance ? "Constraint component" : "Component instance");

        if (SelHdr->Category != ComponentClass::ComponentCategory::SpatialOrigin)
        {
            const Rectangle2D DupBtn { RightBtnsX, Y + 4.0f, 26.0f, 26.0f };
            const Rectangle2D DelBtn { DupBtn.Right() + 6.0f, Y + 4.0f, 26.0f, 26.0f };
            if (CapsuleButton::ConstructRoundIcon(Surface, DupBtn.Center().X, DupBtn.Center().Y, ControlIcon::CopyDuplicate, Ptr).Clicked)
            {
                Session.DuplicateSelectedComponent();
            }
            if (CapsuleButton::ConstructRoundIcon(Surface, DelBtn.Center().X, DelBtn.Center().Y, ControlIcon::TrashDelete, Ptr).Clicked)
            {
                Session.RemoveSelectedComponent();
            }
        }
        Y += 44.0f;
    }

    // 3. Search details
    const Rectangle2D SearchRect { Bounds.X + PadX, Y, InnerW, 30.0f };
    Surface.FillRectangle(SearchRect, WorkspaceSurfaceColors::ControlWell, 15.0f);
    Surface.StrokeRectangle(SearchRect, WorkspaceSurfaceColors::ControlWellBorder, 15.0f, 1.0f);
    if (ControlAppearance::QueryPointerOver(SearchRect, Ptr) && Ptr.Pressed && !Session.DetailsSearch.Active)
    {
        Session.DetailsSearch.Begin(Session.DetailsSearch.Text);
    }
    if (Session.DetailsSearch.Active)
    {
        WorkspaceAppearance::ForwardInputToTextBuffer(Session.DetailsSearch, Input, DeltaSeconds);
        (void)TextInputField::Construct(Surface, SearchRect.InsetXY(8.0f, 3.0f), Session.DetailsSearch, Ptr, 11.0f);
    }
    else if (Session.DetailsSearch.Length == 0u)
    {
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { SearchRect.X + 12.0f, SearchRect.Y + 8.0f }, 10.2f,
            WorkspaceSurfaceColors::FaintInk, "Search details");
    }
    else
    {
        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { SearchRect.X + 12.0f, SearchRect.Y + 8.0f }, SearchRect.Width - 20.0f, 10.2f,
            WorkspaceSurfaceColors::PrimaryInk, Session.DetailsSearch.Text);
    }
    Y += 38.0f;

    const Rectangle2D ScrollArea { Bounds.X, Y, Bounds.Width, std::max(60.0f, Bounds.Bottom() - Y - 4.0f) };
    if (ScrollArea.Contains({ Ptr.X, Ptr.Y }) && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        ScrollOffsetPx = std::max(0.0f, ScrollOffsetPx - Input.WheelDeltaY * 32.0f);
    }

    Surface.PushClip(ScrollArea);
    float CY = ScrollArea.Y - ScrollOffsetPx;
    const std::string_view Filter = Session.DetailsSearch.Text;

    auto DrawSectionHeader = [&](const char* Title)
    {
        CY += 6.0f;
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::SemiBold, { Bounds.X + PadX, CY }, 9.6f,
            WorkspaceSurfaceColors::FaintInk, Title);
        CY += 20.0f;
    };

    // Re-fetch header safely
    SelHdr = Doc->ClassDefinition.Storage.InspectHeader(Doc->SelectedToken);
    if (SelHdr == nullptr)
    {
        Surface.PopClip();
        return;
    }

    // A. CLASS DEFAULTS & COMPILER RESULTS (shown when SpatialOrigin is selected)
    if (SelHdr->Category == ComponentClass::ComponentCategory::SpatialOrigin && MatchesDetailsFilter("CLASS DEFAULTS", Filter))
    {
        DrawSectionHeader("CLASS DEFAULTS");
        const auto Metrics = Doc->ClassDefinition.Storage.ComputeSummaryMetrics();

        // 128x128 baked class thumbnail + 2x2 metrics grid
        const Rectangle2D ThumbRect { Bounds.X + PadX, CY, 88.0f, 88.0f };
        Surface.FillRectangle(ThumbRect, WorkspaceSurfaceColors::CheckerDark, 10.0f);
        if (!Doc->ClassDefinition.BakedIconRgba8.empty() && Doc->ClassDefinition.BakedIconWidth > 0u)
        {
            if (ClassThumbTexture.Pixels == nullptr ||
                ClassThumbTexture.Width != static_cast<int>(Doc->ClassDefinition.BakedIconWidth))
            {
                ClassThumbTexture.DestroyPixels();
                ClassThumbTexture.SetStatus(ImTextureStatus_Destroyed);
                ClassThumbTexture.Create(
                    ImTextureFormat_RGBA32,
                    static_cast<int>(Doc->ClassDefinition.BakedIconWidth),
                    static_cast<int>(Doc->ClassDefinition.BakedIconHeight));
            }
            std::memcpy(ClassThumbTexture.GetPixels(), Doc->ClassDefinition.BakedIconRgba8.data(), Doc->ClassDefinition.BakedIconRgba8.size());
            if (ClassThumbTexture.Status == ImTextureStatus_OK)
            {
                ClassThumbTexture.UpdateRect = { 0u, 0u, static_cast<unsigned short>(ClassThumbTexture.Width), static_cast<unsigned short>(ClassThumbTexture.Height) };
                ClassThumbTexture.SetStatus(ImTextureStatus_WantUpdates);
            }
            Surface.FillTexturedRectangle(ThumbRect, &ClassThumbTexture, { 0.0f, 0.0f }, { 1.0f, 1.0f }, 10.0f);
        }
        Surface.StrokeRectangle(ThumbRect, ColorRgba { 48u, 48u, 54u, 255u }, 10.0f, 1.0f);

        const float GridX = ThumbRect.Right() + 8.0f;
        const float CellW = (Bounds.Right() - PadX - GridX - 6.0f) * 0.5f;
        const float CellH = 41.0f;
        auto DrawMiniMetric = [&](float X, float Ym, uint32_t Val, const char* Cap)
        {
            const Rectangle2D MR { X, Ym, CellW, CellH };
            Surface.FillRectangle(MR, ColorRgba { 23u, 23u, 26u, 255u }, 8.0f);
            char B[16] {};
            std::snprintf(B, sizeof(B), "%u", Val);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::SemiBold, { MR.X + 8.0f, MR.Y + 4.0f }, 12.5f, WorkspaceSurfaceColors::PrimaryInk, B);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { MR.X + 8.0f, MR.Y + 23.0f }, 7.8f, WorkspaceSurfaceColors::FaintInk, Cap);
        };
        DrawMiniMetric(GridX, CY, Metrics.GeometryCount, "GEOMETRIES");
        DrawMiniMetric(GridX + CellW + 6.0f, CY, Metrics.LightCount, "LIGHTS");
        DrawMiniMetric(GridX, CY + CellH + 6.0f, Metrics.CollisionCount, "COLLIDERS");
        DrawMiniMetric(GridX + CellW + 6.0f, CY + CellH + 6.0f, Metrics.BehaviourCount, "BEHAVIOURS");
        CY += 98.0f;

        // Compiler Results
        DrawSectionHeader("COMPILER RESULTS");
        const Rectangle2D DiagnosticWell { Bounds.X + PadX, CY, InnerW, 38.0f };
        Surface.FillRectangle(DiagnosticWell, ColorRgba { 22u, 25u, 23u, 255u }, 8.0f);
        Surface.FillRectangle({ DiagnosticWell.X, DiagnosticWell.Y + 6.0f, 3.0f, DiagnosticWell.Height - 12.0f },
            Doc->CanCompile ? ColorRgba { 88u, 214u, 128u, 255u } : ColorRgba { 240u, 82u, 72u, 255u }, 1.5f);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { DiagnosticWell.X + 12.0f, DiagnosticWell.Y + 12.0f }, 10.0f,
            WorkspaceSurfaceColors::SecondaryInk,
            Doc->Diagnostics.empty()
                ? "No issues. The class compiles cleanly."
                : Doc->Diagnostics.front().Summary);
        CY += 48.0f;
    }

    // B. TRANSFORM (all Spatial components except Behaviours)
    if (SelHdr->Category != ComponentClass::ComponentCategory::VehicleDynamics &&
        SelHdr->Category != ComponentClass::ComponentCategory::RotationalMotion &&
        MatchesDetailsFilter("TRANSFORM", Filter))
    {
        DrawSectionHeader("TRANSFORM");
        if (auto* MutH = Doc->ClassDefinition.Storage.ResolveHeader(Doc->SelectedToken))
        {
            if (Vector3CapsuleInput::Construct(
                    Surface, { Bounds.X + PadX, CY, InnerW, 46.0f },
                    "Location", "m",
                    MutH->LocalTransform.LocationMeters.X,
                    MutH->LocalTransform.LocationMeters.Y,
                    MutH->LocalTransform.LocationMeters.Z,
                    LocSession, Input, false, 0.01f, DeltaSeconds))
            {
                Session.MarkActiveDirty();
            }
            CY += 54.0f;

            if (Vector3CapsuleInput::Construct(
                    Surface, { Bounds.X + PadX, CY, InnerW, 46.0f },
                    "Rotation", "deg",
                    MutH->LocalTransform.RotationDegrees.X,
                    MutH->LocalTransform.RotationDegrees.Y,
                    MutH->LocalTransform.RotationDegrees.Z,
                    RotSession, Input, false, 0.25f, DeltaSeconds))
            {
                Session.MarkActiveDirty();
            }
            CY += 54.0f;

            if (Vector3CapsuleInput::Construct(
                    Surface, { Bounds.X + PadX, CY, InnerW, 46.0f },
                    "Scale", "x",
                    MutH->LocalTransform.ScaleFactors.X,
                    MutH->LocalTransform.ScaleFactors.Y,
                    MutH->LocalTransform.ScaleFactors.Z,
                    ScaleSession, Input, true, 0.005f, DeltaSeconds))
            {
                Session.MarkActiveDirty();
            }
            CY += 54.0f;
        }
    }

    // C. CONSTRAINT section
    if (SelHdr->Category == ComponentClass::ComponentCategory::ConstraintInstance && MatchesDetailsFilter("CONSTRAINT", Filter))
    {
        DrawSectionHeader("CONSTRAINT");
        if (auto* Con = Doc->ClassDefinition.Storage.ResolveConstraint(Doc->SelectedToken))
        {
            if (ConstraintTilePicker::Construct(
                    Surface, { Bounds.X + PadX, CY, InnerW, ConstraintTilePicker::RecommendedHeightPx },
                    Con->Kind, Ptr, 1.0f))
            {
                Session.MarkActiveDirty();
            }
            CY += ConstraintTilePicker::RecommendedHeightPx + 8.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { Bounds.X + PadX, CY }, 9.6f, WorkspaceSurfaceColors::MutedInk,
                ConstraintTilePicker::QueryKindSummary(Con->Kind));
            CY += 24.0f;

            // Axis X | Y | Z
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { Bounds.X + PadX, CY }, 10.2f, WorkspaceSurfaceColors::SecondaryInk, "Axis");
            CY += 18.0f;
            static constexpr std::array<const char*, 3> AxisNames {{ "X", "Y", "Z" }};
            uint32_t AxisIdx = static_cast<uint32_t>(Con->Axis);
            uint32_t NextAxis = AxisIdx;
            if (CapsuleButton::ConstructSegmented(Surface, { Bounds.X + PadX, CY, InnerW, 28.0f }, AxisNames.data(), 3u, AxisIdx, Ptr, NextAxis).Clicked)
            {
                Con->Axis = static_cast<ComponentClass::ConstraintAxis>(NextAxis);
                Session.MarkActiveDirty();
            }
            CY += 36.0f;

            auto EmitSlider = [&](uint32_t Sid, const char* Label, const char* Unit, float& Val, float Lo, float Hi, const char* Fmt)
            {
                WorkspaceAppearance::ConstructWeightedText(
                    Surface, FontWeightClass::Medium, { Bounds.X + PadX, CY }, 10.0f, WorkspaceSurfaceColors::SecondaryInk, Label);
                char NumBuf[24] {};
                std::snprintf(NumBuf, sizeof(NumBuf), Fmt, Val);
                float NextVal = Val;
                const auto C = ScalarSlider::ConstructWithReadout(
                    Surface, { Bounds.X + PadX, CY + 15.0f, InnerW, 22.0f },
                    NumBuf, Unit, Lo, Hi, Val, DraggingSlider == Sid, Ptr, NextVal);
                if (C.Dragging)
                {
                    DraggingSlider = Sid;
                    Val = NextVal;
                    Session.MarkActiveDirty();
                }
                else if (DraggingSlider == Sid && !Ptr.Down)
                {
                    DraggingSlider = 0u;
                }
                CY += 48.0f;
            };

            if (Con->Kind == ComponentClass::PhysicsConstraintKind::Spring)
            {
                EmitSlider(1u, "Natural frequency", "Hz", Con->NaturalFrequencyHz, 0.5f, 8.0f, "%.2f");
                EmitSlider(2u, "Damping ratio", "zeta", Con->DampingRatioZeta, 0.05f, 1.5f, "%.2f");
                EmitSlider(3u, "Travel (each way)", "m", Con->TravelEachWayMeters, 0.02f, 0.40f, "%.3f");

                CY += 4.0f;
                const Rectangle2D KickBtn { Bounds.X + PadX, CY, (InnerW - 8.0f) * 0.5f, 30.0f };
                const Rectangle2D RstBtn  { KickBtn.Right() + 8.0f, CY, (InnerW - 8.0f) * 0.5f, 30.0f };
                if (CapsuleButton::Construct(Surface, KickBtn, ButtonDescriptor{ "Kick", ButtonTone::Secondary, 10.5f, 8.0f }, Ptr).Clicked)
                {
                    Session.TriggerConstraintKick(Doc->SelectedToken);
                }
                if (CapsuleButton::Construct(Surface, RstBtn, ButtonDescriptor{ "Reset motion", ButtonTone::Secondary, 10.5f, 8.0f }, Ptr).Clicked)
                {
                    Session.ResetMotionState();
                }
                CY += 36.0f;
            }
            else if (Con->Kind == ComponentClass::PhysicsConstraintKind::Dock)
            {
                EmitSlider(4u, "Break force", "N", Con->BreakForceNewtons, 100.0f, 8000.0f, "%.0f");
                const Rectangle2D DockBtn { Bounds.X + PadX, CY, InnerW, 28.0f };
                if (CapsuleButton::Construct(
                        Surface, DockBtn,
                        ButtonDescriptor{ Con->DockConnected ? "Release Dock" : "Connect Dock", ButtonTone::Secondary, 10.5f, 10.0f },
                        Ptr).Clicked)
                {
                    Con->DockConnected = !Con->DockConnected;
                    Session.RenderOptions.DockIntactFlags[Doc->SelectedToken.SlotIndex] = Con->DockConnected;
                    Session.MarkActiveDirty();
                }
                CY += 36.0f;
            }
            else if (Con->Kind == ComponentClass::PhysicsConstraintKind::Hinge)
            {
                EmitSlider(5u, "Edit pose", "norm", Con->NormalizedEditPose, 0.0f, 1.0f, "%.2f");
            }
        }
    }

    // D. LIGHT section
    if (SelHdr->Category == ComponentClass::ComponentCategory::LightInstance && MatchesDetailsFilter("LIGHT", Filter))
    {
        DrawSectionHeader("LIGHT");
        if (auto* L = Doc->ClassDefinition.Storage.ResolveLight(Doc->SelectedToken))
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { Bounds.X + PadX, CY }, 10.0f, WorkspaceSurfaceColors::SecondaryInk, "Intensity");
            char B1[24] {};
            std::snprintf(B1, sizeof(B1), "%.1f", L->IntensityCandela);
            float NextI = L->IntensityCandela;
            const auto C1 = ScalarSlider::ConstructWithReadout(
                Surface, { Bounds.X + PadX, CY + 15.0f, InnerW, 22.0f }, B1, "cd", 0.0f, 120.0f, L->IntensityCandela, DraggingSlider == 6u, Ptr, NextI);
            if (C1.Dragging) { DraggingSlider = 6u; L->IntensityCandela = NextI; Session.MarkActiveDirty(); }
            else if (DraggingSlider == 6u && !Ptr.Down) DraggingSlider = 0u;
            CY += 42.0f;

            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { Bounds.X + PadX, CY }, 10.0f, WorkspaceSurfaceColors::SecondaryInk, "Cone angle");
            char B2[24] {};
            std::snprintf(B2, sizeof(B2), "%.0f", L->ConeAngleDegrees);
            float NextA = L->ConeAngleDegrees;
            const auto C2 = ScalarSlider::ConstructWithReadout(
                Surface, { Bounds.X + PadX, CY + 15.0f, InnerW, 22.0f }, B2, "deg", 5.0f, 80.0f, L->ConeAngleDegrees, DraggingSlider == 7u, Ptr, NextA);
            if (C2.Dragging) { DraggingSlider = 7u; L->ConeAngleDegrees = NextA; Session.MarkActiveDirty(); }
            else if (DraggingSlider == 7u && !Ptr.Down) DraggingSlider = 0u;
            CY += 42.0f;
        }
    }

    // E. VEHICLE DYNAMICS section
    if (SelHdr->Category == ComponentClass::ComponentCategory::VehicleDynamics && MatchesDetailsFilter("VEHICLE", Filter))
    {
        DrawSectionHeader("VEHICLE DYNAMICS");
        if (auto* V = Doc->ClassDefinition.Storage.ResolveVehicle(Doc->SelectedToken))
        {
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Medium, { Bounds.X + PadX, CY }, 10.0f, WorkspaceSurfaceColors::SecondaryInk, "Chassis mass");
            char B1[24] {};
            std::snprintf(B1, sizeof(B1), "%.0f", V->ChassisMassKg);
            float NextM = V->ChassisMassKg;
            const auto C1 = ScalarSlider::ConstructWithReadout(
                Surface, { Bounds.X + PadX, CY + 15.0f, InnerW, 22.0f }, B1, "kg", 400.0f, 4000.0f, V->ChassisMassKg, DraggingSlider == 8u, Ptr, NextM);
            if (C1.Dragging) { DraggingSlider = 8u; V->ChassisMassKg = NextM; Session.MarkActiveDirty(); }
            else if (DraggingSlider == 8u && !Ptr.Down) DraggingSlider = 0u;
            CY += 42.0f;

            const Rectangle2D CurveBtn { Bounds.X + PadX, CY, InnerW, 28.0f };
            if (CapsuleButton::Construct(Surface, CurveBtn, ButtonDescriptor{ "Open Engine Torque Curve", ButtonTone::Secondary, 10.5f, 10.0f }, Ptr).Clicked)
            {
                Session.DrawerExpanded  = true;
                Session.ActiveDrawerTab = BottomDrawerTab::ResponseCurves;
            }
            CY += 36.0f;
        }
    }

    // F. RENDERING toggle
    if (SelHdr->Category != ComponentClass::ComponentCategory::VehicleDynamics &&
        SelHdr->Category != ComponentClass::ComponentCategory::RotationalMotion &&
        MatchesDetailsFilter("RENDERING", Filter))
    {
        DrawSectionHeader("RENDERING");
        if (auto* MutH = Doc->ClassDefinition.Storage.ResolveHeader(Doc->SelectedToken))
        {
            if (WorkspaceAppearance::ConstructCapsuleToggleRow(
                    Surface, { Bounds.X + PadX, CY, InnerW, 28.0f }, "Visible",
                    MutH->VisibilityEnabled, { Ptr.X, Ptr.Y }, Ptr.Pressed))
            {
                Session.MarkActiveDirty();
            }
            CY += 34.0f;
        }
    }

    Surface.PopClip();
}

} // namespace Frontier::Workspaces::ComponentEditor
