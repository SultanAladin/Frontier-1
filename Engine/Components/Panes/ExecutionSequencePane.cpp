/*============================================================================================================================================
                                                         ASSEMBLYSEQUENCEPANE.CPP
============================================================================================================================================*/
// 🧩 Implementation of ExecutionSequencePane (16/128px grid, Event + Call + Target pill blocks, cubic Bezier exec & instance wires)

#include "Engine/Components/Panes/ExecutionSequencePane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace Frontier
{

namespace
{

enum class SequenceBlockCategory : uint8_t
{
    EventHeader = 0u,
    CallDirective = 1u,
    TargetChip  = 2u,
};

struct SequenceBlock
{
    uint32_t                               Id = 0u;
    SequenceBlockCategory                  Form = SequenceBlockCategory::CallDirective;
    ComponentClass::ComponentInstanceToken InstanceToken {};
    std::string                            Title {};
    std::string                            Subtitle {};
    std::string                            Detail {};
    float                                  X = 0.0f;
    float                                  Y = 0.0f;
    float                                  W = 190.0f;
    float                                  H = 66.0f;
    Point2D                                ExecInLocal  {};
    Point2D                                ExecOutLocal {};
    Point2D                                SocketInLocal {};
    Point2D                                ChipOutLocal {};
};

struct SequenceWireLink
{
    uint32_t FromBlockId = 0u;
    uint32_t ToBlockId   = 0u;
    bool     ExecutionWire  = true;
};

void EmitCubicBezierWire(
    PixelSpace& Surface,
    Point2D     P0,
    Point2D     P3,
    ColorRgba   Color,
    float       Thickness) noexcept
{
    const float Span = std::max(44.0f, std::abs(P3.X - P0.X) * 0.55f);
    const Point2D P1 { P0.X + Span, P0.Y };
    const Point2D P2 { P3.X - Span, P3.Y };

    constexpr int Steps = 20;
    std::array<Point2D, Steps + 1> Poly {};
    for (int I = 0; I <= Steps; ++I)
    {
        const float T  = static_cast<float>(I) / static_cast<float>(Steps);
        const float U  = 1.0f - T;
        const float B0 = U * U * U;
        const float B1 = 3.0f * U * U * T;
        const float B2 = 3.0f * U * T * T;
        const float B3 = T * T * T;
        Poly[I] = {
            B0 * P0.X + B1 * P1.X + B2 * P2.X + B3 * P3.X,
            B0 * P0.Y + B1 * P1.Y + B2 * P2.Y + B3 * P3.Y,
        };
    }
    Surface.StrokePolyline(Poly, Color, Thickness, false);
}

void BuildSequenceBlocks(
    const ComponentClass::ComponentClassDefinition& ClassDef,
    std::vector<SequenceBlock>&                     OutBlocks,
    std::vector<SequenceWireLink>&                  OutWires)
{
    OutBlocks.clear();
    OutWires.clear();

    SequenceBlock RootEvent {};
    RootEvent.Id           = 1u;
    RootEvent.Form         = SequenceBlockCategory::EventHeader;
    RootEvent.Title        = "Assembly Sequence";
    RootEvent.Subtitle     = "Event";
    RootEvent.X            = 0.0f;
    RootEvent.Y            = 0.0f;
    RootEvent.W            = 176.0f;
    RootEvent.H            = 64.0f;
    RootEvent.ExecOutLocal = { RootEvent.W, 38.0f };
    OutBlocks.push_back(RootEvent);

    uint32_t NextId      = 2u;
    uint32_t PrevExecId  = RootEvent.Id;
    float    CursorX     = 230.0f;

    ClassDef.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& H)
    {
        if (H.Category == ComponentClass::ComponentCategory::SpatialOrigin)
        {
            return;
        }

        std::string ActionTitle = "Attach Instance";
        std::string DetailLine  = "Spatial mount";
        if (H.Category == ComponentClass::ComponentCategory::GeometryInstance)
        {
            const auto* G = ClassDef.Storage.InspectGeometry(H.Token);
            ActionTitle = "Assign Geometry";
            DetailLine  = (G != nullptr) ? ("Geometry: " + G->AssetIdentifier) : "Geometry";
        }
        else if (H.Category == ComponentClass::ComponentCategory::ConstraintInstance)
        {
            const auto* C = ClassDef.Storage.InspectConstraint(H.Token);
            ActionTitle = "Construct Constraint";
            if (C != nullptr)
            {
                char Buf[64] {};
                switch (C->Form)
                {
                    case ComponentClass::PhysicsConstraintCategory::Spring:
                        ActionTitle = "Construct Spring Constraint";
                        std::snprintf(Buf, sizeof(Buf), "%.2f Hz · zeta %.2f", C->NaturalFrequencyHz, C->DampingRatioZeta);
                        break;
                    case ComponentClass::PhysicsConstraintCategory::Hinge:
                        ActionTitle = "Construct Hinge Constraint";
                        std::snprintf(Buf, sizeof(Buf), "%.0f° .. %.0f°", C->MinAngleDegrees, C->MaxAngleDegrees);
                        break;
                    case ComponentClass::PhysicsConstraintCategory::Dock:
                        ActionTitle = "Construct Dock Constraint";
                        std::snprintf(Buf, sizeof(Buf), "Key \"%s\" · %.0f N", C->DockKey.c_str(), C->BreakForceNewtons);
                        break;
                    case ComponentClass::PhysicsConstraintCategory::Tether:
                        ActionTitle = "Construct Tether Constraint";
                        std::snprintf(Buf, sizeof(Buf), "%.2f m · %.0f N", C->RopeLengthMeters, C->BreakForceNewtons);
                        break;
                    case ComponentClass::PhysicsConstraintCategory::Spin:
                        ActionTitle = "Construct Spin Constraint";
                        std::snprintf(Buf, sizeof(Buf), "%.0f rpm", C->TargetRpm);
                        break;
                    default:
                        std::snprintf(Buf, sizeof(Buf), "Axis %u", static_cast<unsigned>(C->Axis));
                        break;
                }
                DetailLine = Buf;
            }
        }
        else if (H.Category == ComponentClass::ComponentCategory::LightInstance)
        {
            const auto* L = ClassDef.Storage.InspectLight(H.Token);
            ActionTitle = "Assign Light";
            char Buf[48] {};
            if (L != nullptr)
            {
                std::snprintf(Buf, sizeof(Buf), "%.0f cd · %.1f m", L->IntensityCandela, L->AttenuationRangeMeters);
                DetailLine = Buf;
            }
        }
        else if (H.Category == ComponentClass::ComponentCategory::CollisionVolume)
        {
            ActionTitle = "Assign Collision";
            DetailLine  = "Trigger overlap box";
        }
        else if (H.Category == ComponentClass::ComponentCategory::CameraInstance)
        {
            ActionTitle = "Assign Camera";
            DetailLine  = "Optics viewpoint";
        }

        const uint32_t CallId = NextId++;
        const uint32_t ChipId = NextId++;

        SequenceBlock Call {};
        Call.Id            = CallId;
        Call.Form          = SequenceBlockCategory::CallDirective;
        Call.InstanceToken = H.Token;
        Call.Title         = ActionTitle;
        Call.Subtitle      = "Target: " + H.Title;
        Call.Detail        = DetailLine;
        Call.X             = CursorX;
        Call.Y             = (CallId % 4u == 0u) ? 44.0f : 0.0f;
        Call.W             = 212.0f;
        Call.H             = 76.0f;
        Call.ExecInLocal   = { 0.0f, 38.0f };
        Call.ExecOutLocal  = { Call.W, 38.0f };
        Call.SocketInLocal = { 0.0f, 58.0f };

        SequenceBlock Chip {};
        Chip.Id            = ChipId;
        Chip.Form          = SequenceBlockCategory::TargetChip;
        Chip.InstanceToken = H.Token;
        Chip.Title         = H.Title;
        Chip.X             = CursorX - 36.0f;
        Chip.Y             = Call.Y + 132.0f;
        Chip.W             = 128.0f;
        Chip.H             = 32.0f;
        Chip.ChipOutLocal  = { Chip.W - 12.0f, 16.0f };

        OutBlocks.push_back(Call);
        OutBlocks.push_back(Chip);
        OutWires.push_back({ PrevExecId, CallId, true });
        OutWires.push_back({ ChipId, CallId, false });

        PrevExecId = CallId;
        CursorX += 248.0f;
    });
}

} // namespace

ComponentClass::ComponentInstanceToken ExecutionSequencePane::ConstructWithInput(
    PixelSpace&                                     Surface,
    Rectangle2D                                     Bounds,
    const ComponentClass::ComponentClassDefinition& ClassDef,
    ComponentClass::ComponentInstanceToken          SelectedToken,
    ExecutionSequenceViewport&                       Viewport,
    const PaneInputReading&                         Input) noexcept
{
    ComponentClass::ComponentInstanceToken ClickedToken = SelectedToken;

    Surface.PushClip(Bounds);
    Surface.FillRectangle(Bounds, ColorRgba { 13u, 14u, 16u, 255u }, 0.0f);

    if (Viewport.SyncedRevision != ClassDef.CompiledRevision)
    {
        Viewport.SyncedRevision = ClassDef.CompiledRevision;
        Viewport.BlockOffsets.clear();
    }

    // Zoom & background pan
    const ControlPointer& Ptr = Input.PrimaryPointer;
    const bool InsidePane = Bounds.Contains({ Ptr.X, Ptr.Y });
    if (InsidePane && std::abs(Input.WheelDeltaY) > 0.01f)
    {
        const float Factor = (Input.WheelDeltaY > 0.0f) ? 1.10f : (1.0f / 1.10f);
        Viewport.ZoomScale = std::clamp(Viewport.ZoomScale * Factor, 0.45f, 1.85f);
    }

    std::vector<SequenceBlock>    Blocks {};
    std::vector<SequenceWireLink> Wires {};
    BuildSequenceBlocks(ClassDef, Blocks, Wires);

    for (auto& B : Blocks)
    {
        if (const auto It = Viewport.BlockOffsets.find(B.Id); It != Viewport.BlockOffsets.end())
        {
            B.X += It->second.X;
            B.Y += It->second.Y;
        }
    }

    const float Z = Viewport.ZoomScale;
    const Point2D OriginPx { Bounds.X + Viewport.PanOffsetPx.X, Bounds.Y + Viewport.PanOffsetPx.Y };

    // 16px minor & 128px major grid
    const float MinorStep = 16.0f * Z;
    const float MajorStep = 128.0f * Z;
    if (MinorStep >= 6.0f)
    {
        const float StartX = std::fmod(Viewport.PanOffsetPx.X, MinorStep);
        for (float X = Bounds.X + StartX; X < Bounds.Right(); X += MinorStep)
        {
            Surface.StrokeLine({ X, Bounds.Y }, { X, Bounds.Bottom() }, ColorRgba { 255u, 255u, 255u, 7u }, 1.0f);
        }
        const float StartY = std::fmod(Viewport.PanOffsetPx.Y, MinorStep);
        for (float Y = Bounds.Y + StartY; Y < Bounds.Bottom(); Y += MinorStep)
        {
            Surface.StrokeLine({ Bounds.X, Y }, { Bounds.Right(), Y }, ColorRgba { 255u, 255u, 255u, 7u }, 1.0f);
        }
    }
    if (MajorStep >= 24.0f)
    {
        const float StartX = std::fmod(Viewport.PanOffsetPx.X, MajorStep);
        for (float X = Bounds.X + StartX; X < Bounds.Right(); X += MajorStep)
        {
            Surface.StrokeLine({ X, Bounds.Y }, { X, Bounds.Bottom() }, ColorRgba { 255u, 255u, 255u, 15u }, 1.0f);
        }
        const float StartY = std::fmod(Viewport.PanOffsetPx.Y, MajorStep);
        for (float Y = Bounds.Y + StartY; Y < Bounds.Bottom(); Y += MajorStep)
        {
            Surface.StrokeLine({ Bounds.X, Y }, { Bounds.Right(), Y }, ColorRgba { 255u, 255u, 255u, 15u }, 1.0f);
        }
    }

    auto ToScreen = [&](float GX, float GY) -> Point2D
    {
        return { OriginPx.X + GX * Z, OriginPx.Y + GY * Z };
    };
    auto FindBlock = [&](uint32_t Id) -> const SequenceBlock*
    {
        for (const auto& B : Blocks)
        {
            if (B.Id == Id) return &B;
        }
        return nullptr;
    };

    // Hit-test blocks for drag/selection
    uint32_t HitBlockId = 0u;
    for (auto It = Blocks.rbegin(); It != Blocks.rend(); ++It)
    {
        const Point2D TL = ToScreen(It->X, It->Y);
        const Rectangle2D R { TL.X, TL.Y, It->W * Z, It->H * Z };
        if (R.Contains({ Ptr.X, Ptr.Y }))
        {
            HitBlockId = It->Id;
            if (Ptr.Pressed && It->InstanceToken.Valid())
            {
                ClickedToken = It->InstanceToken;
            }
            break;
        }
    }

    if (InsidePane && Ptr.Pressed)
    {
        if (HitBlockId != 0u)
        {
            Viewport.DraggedBlockId = HitBlockId;
            Viewport.Panning        = false;
        }
        else
        {
            Viewport.Panning        = true;
            Viewport.DraggedBlockId = 0u;
        }
    }
    if (Ptr.Down)
    {
        if (Viewport.DraggedBlockId != 0u)
        {
            auto& Off = Viewport.BlockOffsets[Viewport.DraggedBlockId];
            Off.X += Input.PointerDeltaX / Z;
            Off.Y += Input.PointerDeltaY / Z;
        }
        else if (Viewport.Panning)
        {
            Viewport.PanOffsetPx.X += Input.PointerDeltaX;
            Viewport.PanOffsetPx.Y += Input.PointerDeltaY;
        }
    }
    else
    {
        Viewport.Panning        = false;
        Viewport.DraggedBlockId = 0u;
    }

    // 1. Bezier wires
    for (const auto& W : Wires)
    {
        const auto* A = FindBlock(W.FromBlockId);
        const auto* B = FindBlock(W.ToBlockId);
        if (A == nullptr || B == nullptr) continue;

        if (W.ExecutionWire)
        {
            const Point2D P0 = ToScreen(A->X + A->ExecOutLocal.X, A->Y + A->ExecOutLocal.Y);
            const Point2D P3 = ToScreen(B->X + B->ExecInLocal.X,  B->Y + B->ExecInLocal.Y);
            EmitCubicBezierWire(Surface, P0, P3, ColorRgba { 232u, 232u, 235u, 225u }, 1.8f * Z);
        }
        else
        {
            const Point2D P0 = ToScreen(A->X + A->ChipOutLocal.X,  A->Y + A->ChipOutLocal.Y);
            const Point2D P3 = ToScreen(B->X + B->SocketInLocal.X, B->Y + B->SocketInLocal.Y);
            EmitCubicBezierWire(Surface, P0, P3, ColorRgba { 88u, 160u, 255u, 220u }, 1.6f * Z);
        }
    }

    // 2. Blocks
    for (const auto& B : Blocks)
    {
        const Point2D TL = ToScreen(B.X, B.Y);
        const Rectangle2D Rect { TL.X, TL.Y, B.W * Z, B.H * Z };
        if (Rect.Right() < Bounds.X - 20.0f || Rect.X > Bounds.Right() + 20.0f ||
            Rect.Bottom() < Bounds.Y - 20.0f || Rect.Y > Bounds.Bottom() + 20.0f)
        {
            continue;
        }
        const bool Selected = (B.InstanceToken.Valid() && B.InstanceToken == ClickedToken);

        if (B.Form == SequenceBlockCategory::TargetChip)
        {
            const float Rad = Rect.Height * 0.5f;
            Surface.FillRectangle(Rect, ColorRgba { 22u, 33u, 52u, 242u }, Rad);
            Surface.StrokeRectangle(
                Rect,
                Selected ? ColorRgba { 255u, 178u, 56u, 255u } : ColorRgba { 66u, 118u, 200u, 210u },
                Rad,
                Selected ? 1.6f : 1.0f);
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Medium, { Rect.X + 14.0f * Z, Rect.Y + 9.0f * Z },
                Rect.Width - 36.0f * Z, 10.6f * Z, ColorRgba { 228u, 236u, 248u, 255u }, B.Title);
            const Point2D Pin = ToScreen(B.X + B.ChipOutLocal.X, B.Y + B.ChipOutLocal.Y);
            Surface.StrokeCircle(Pin, 4.2f * Z, ColorRgba { 98u, 172u, 255u, 255u }, 1.5f);
            continue;
        }

        const float HeaderH = 26.0f * Z;
        const ColorRgba HeaderCol = (B.Form == SequenceBlockCategory::EventHeader)
            ? ColorRgba { 138u, 42u, 42u, 255u }
            : ColorRgba { 44u, 96u, 168u, 255u };

        Surface.FillRectangle(Rect, ColorRgba { 22u, 24u, 28u, 244u }, 8.0f * Z);
        Surface.PushClip({ Rect.X, Rect.Y, Rect.Width, HeaderH });
        Surface.FillRectangle(Rect, HeaderCol, 8.0f * Z);
        Surface.PopClip();
        Surface.StrokeRectangle(
            Rect,
            Selected ? ColorRgba { 255u, 178u, 56u, 255u } : ColorRgba { 58u, 62u, 70u, 255u },
            8.0f * Z,
            Selected ? 1.8f : 1.0f);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::SemiBold, { Rect.X + 12.0f * Z, Rect.Y + 6.0f * Z },
            Rect.Width - 22.0f * Z, 10.6f * Z, ColorRgba { 244u, 244u, 248u, 255u }, B.Title);

        WorkspaceAppearance::ConstructTruncatedText(
            Surface, FontWeightClass::Regular, { Rect.X + 14.0f * Z, Rect.Y + HeaderH + 6.0f * Z },
            Rect.Width - 26.0f * Z, 9.8f * Z, ColorRgba { 196u, 200u, 208u, 255u }, B.Subtitle);

        if (!B.Detail.empty())
        {
            WorkspaceAppearance::ConstructTruncatedText(
                Surface, FontWeightClass::Regular, { Rect.X + 14.0f * Z, Rect.Y + HeaderH + 25.0f * Z },
                Rect.Width - 26.0f * Z, 9.6f * Z, ColorRgba { 150u, 156u, 168u, 255u }, B.Detail);
        }

        if (B.Form != SequenceBlockCategory::EventHeader)
        {
            const Point2D PinIn = ToScreen(B.X + B.ExecInLocal.X, B.Y + B.ExecInLocal.Y);
            Surface.FillCircle(PinIn, 3.6f * Z, ColorRgba { 235u, 235u, 238u, 255u });
            const Point2D SocketIn = ToScreen(B.X + B.SocketInLocal.X, B.Y + B.SocketInLocal.Y);
            Surface.StrokeCircle(SocketIn, 3.6f * Z, ColorRgba { 98u, 172u, 255u, 255u }, 1.4f);
        }
        const Point2D PinOut = ToScreen(B.X + B.ExecOutLocal.X, B.Y + B.ExecOutLocal.Y);
        Surface.FillCircle(PinOut, 3.6f * Z, ColorRgba { 235u, 235u, 238u, 255u });
    }

    // Bottom hint line
    WorkspaceAppearance::ConstructWeightedText(
        Surface,
        FontWeightClass::Regular,
        { Bounds.X + 16.0f, Bounds.Bottom() - 22.0f },
        9.4f,
        ColorRgba { 112u, 114u, 120u, 255u },
        "ASSEMBLY SEQUENCE · synthesized from component hierarchy · drag blocks · drag background to pan · wheel to zoom",
        true);

    Surface.PopClip();
    return ClickedToken;
}

} // namespace Frontier
