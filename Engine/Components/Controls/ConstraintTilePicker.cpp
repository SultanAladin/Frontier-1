/*============================================================================================================================================
                                                         CONSTRAINTTILEPICKER.CPP
============================================================================================================================================*/
// 🧩 Implementation of ConstraintTilePicker 4x2 grid with crisp geometric icons for all 8 physics constraint kinds

#include "Engine/Components/Controls/ConstraintTilePicker.h"
#include "Engine/Components/Panes/WorkspaceAppearance.h"

#include <array>
#include <cmath>

namespace Frontier
{

namespace
{

using Kind = ComponentClass::PhysicsConstraintKind;

constexpr std::array<Kind, 8u> OrderedKinds {{
    Kind::Fixed, Kind::Hinge, Kind::Slider, Kind::Ball,
    Kind::Spring, Kind::Spin, Kind::Tether, Kind::Dock,
}};

void RenderConstraintGlyph(PixelSpace& Surface, Kind K, Point2D C, ColorRgba Ink) noexcept
{
    switch (K)
    {
        case Kind::Fixed:
        {
            std::array<Point2D, 6> Hex {};
            for (int I = 0; I < 6; ++I)
            {
                const float A = static_cast<float>(I) * 1.04719755f;
                Hex[I] = { C.X + std::cos(A) * 6.5f, C.Y + std::sin(A) * 6.5f };
            }
            Surface.StrokePolyline(Hex, Ink, 1.3f, true);
            Surface.StrokeCircle(C, 2.3f, Ink, 1.2f);
            break;
        }
        case Kind::Hinge:
        {
            Surface.StrokeLine({ C.X - 5.0f, C.Y - 4.0f }, { C.X + 5.0f, C.Y + 4.0f }, Ink, 1.4f);
            Surface.StrokeLine({ C.X - 5.0f, C.Y + 4.0f }, { C.X + 5.0f, C.Y - 4.0f }, Ink, 1.4f);
            Surface.FillCircle(C, 2.1f, Ink);
            break;
        }
        case Kind::Slider:
        {
            Surface.StrokeLine({ C.X - 7.0f, C.Y }, { C.X + 7.0f, C.Y }, Ink, 1.3f);
            Surface.StrokeLine({ C.X - 7.0f, C.Y - 4.0f }, { C.X - 7.0f, C.Y + 4.0f }, Ink, 1.4f);
            Surface.StrokeLine({ C.X + 7.0f, C.Y - 4.0f }, { C.X + 7.0f, C.Y + 4.0f }, Ink, 1.4f);
            Surface.FillRectangle({ C.X - 3.0f, C.Y - 3.0f, 6.0f, 6.0f }, Ink, 1.5f);
            break;
        }
        case Kind::Ball:
        {
            Surface.StrokeCircle({ C.X, C.Y + 2.0f }, 4.6f, Ink, 1.3f);
            Surface.StrokeLine({ C.X, C.Y - 2.5f }, { C.X - 4.0f, C.Y - 7.0f }, Ink, 1.2f);
            Surface.StrokeLine({ C.X, C.Y - 2.5f }, { C.X + 4.0f, C.Y - 7.0f }, Ink, 1.2f);
            break;
        }
        case Kind::Spring:
        {
            Surface.StrokeLine({ C.X - 4.5f, C.Y - 6.5f }, { C.X + 4.5f, C.Y - 6.5f }, Ink, 1.4f);
            Surface.StrokeLine({ C.X - 4.5f, C.Y + 6.5f }, { C.X + 4.5f, C.Y + 6.5f }, Ink, 1.4f);
            const std::array<Point2D, 6> Zig {{
                { C.X, C.Y - 6.5f },
                { C.X + 4.0f, C.Y - 4.0f },
                { C.X - 4.0f, C.Y - 1.5f },
                { C.X + 4.0f, C.Y + 1.5f },
                { C.X - 4.0f, C.Y + 4.0f },
                { C.X, C.Y + 6.5f },
            }};
            Surface.StrokePolyline(Zig, Ink, 1.3f, false);
            break;
        }
        case Kind::Spin:
        {
            std::array<Point2D, 10> Arc {};
            for (int I = 0; I < 10; ++I)
            {
                const float A = -2.3f + static_cast<float>(I) * 0.52f;
                Arc[I] = { C.X + std::cos(A) * 5.8f, C.Y + std::sin(A) * 5.8f };
            }
            Surface.StrokePolyline(Arc, Ink, 1.35f, false);
            Surface.FillCircle(C, 2.0f, Ink);
            break;
        }
        case Kind::Tether:
        {
            Surface.StrokeCircle({ C.X - 5.0f, C.Y - 3.0f }, 2.2f, Ink, 1.2f);
            Surface.StrokeCircle({ C.X + 5.0f, C.Y + 3.0f }, 2.2f, Ink, 1.2f);
            const std::array<Point2D, 4> Sag {{
                { C.X - 3.0f, C.Y - 2.0f },
                { C.X - 1.0f, C.Y + 3.5f },
                { C.X + 2.0f, C.Y + 4.5f },
                { C.X + 3.2f, C.Y + 2.5f },
            }};
            Surface.StrokePolyline(Sag, Ink, 1.3f, false);
            break;
        }
        case Kind::Dock:
        {
            Surface.StrokeRectangle({ C.X - 7.0f, C.Y - 4.0f, 5.5f, 8.0f }, Ink, 1.5f, 1.2f);
            Surface.StrokeRectangle({ C.X + 1.5f, C.Y - 4.0f, 5.5f, 8.0f }, Ink, 1.5f, 1.2f);
            Surface.StrokeLine({ C.X - 1.5f, C.Y }, { C.X + 1.5f, C.Y }, Ink, 1.4f);
            break;
        }
    }
}

} // namespace

const char* ConstraintTilePicker::QueryKindLabel(Kind K) noexcept
{
    switch (K)
    {
        case Kind::Fixed:  return "Fixed";
        case Kind::Hinge:  return "Hinge";
        case Kind::Slider: return "Slider";
        case Kind::Ball:   return "Ball";
        case Kind::Spring: return "Spring";
        case Kind::Spin:   return "Spin";
        case Kind::Tether: return "Tether";
        case Kind::Dock:   return "Dock";
    }
    return "Fixed";
}

const char* ConstraintTilePicker::QueryKindSummary(Kind K) noexcept
{
    switch (K)
    {
        case Kind::Fixed:  return "Rigid mount moving rigidly with its enclosing instance.";
        case Kind::Hinge:  return "Single-axis pivot with optional angular limits or steering drive.";
        case Kind::Slider: return "Prismatic linear rail constrained between min/max travel stops.";
        case Kind::Ball:   return "Cone-limited spherical socket for articulated joints.";
        case Kind::Spring: return "A slider with stiffness and damping that reacts to load and road bumps.";
        case Kind::Spin:   return "Continuous motorized axle rotation driven toward target RPM.";
        case Kind::Tether: return "Flexible cable constraining maximum separation distance up to break force.";
        case Kind::Dock:   return "Keyed snap coupler that latches matching tags and detaches past overload.";
    }
    return "";
}

bool ConstraintTilePicker::Construct(
    PixelSpace&           Surface,
    Rectangle2D           Bounds,
    Kind&                 InOutKind,
    const ControlPointer& Pointer,
    float                 Opacity) noexcept
{
    bool Changed = false;
    constexpr float Gap = 6.0f;
    const float TileW = (Bounds.Width - Gap * 3.0f) * 0.25f;
    const float TileH = (Bounds.Height - Gap) * 0.5f;

    for (std::size_t I = 0u; I < OrderedKinds.size(); ++I)
    {
        const uint32_t Col = static_cast<uint32_t>(I % 4u);
        const uint32_t Row = static_cast<uint32_t>(I / 4u);
        const Rectangle2D TileRect {
            Bounds.X + static_cast<float>(Col) * (TileW + Gap),
            Bounds.Y + static_cast<float>(Row) * (TileH + Gap),
            TileW,
            TileH,
        };
        const Kind K         = OrderedKinds[I];
        const bool Active    = (InOutKind == K);
        const bool Hovered   = ControlAppearance::QueryPointerOver(TileRect, Pointer);

        const ColorRgba Fill = Active
            ? ColorRgba { 30u, 32u, 38u, 255u }.ScaledAlpha(Opacity)
            : (Hovered ? ColorRgba { 26u, 26u, 28u, 255u }.ScaledAlpha(Opacity) : ColorRgba { 19u, 19u, 21u, 255u }.ScaledAlpha(Opacity));
        const ColorRgba Border = Active
            ? ColorRgba { 228u, 228u, 232u, 255u }.ScaledAlpha(Opacity)
            : ColorRgba { 48u, 48u, 52u, 255u }.ScaledAlpha(Opacity);
        const ColorRgba Ink = Active
            ? ColorRgba { 242u, 242u, 244u, 255u }.ScaledAlpha(Opacity)
            : ColorRgba { 148u, 148u, 152u, 255u }.ScaledAlpha(Opacity);

        Surface.FillRectangle(TileRect, Fill, 8.0f);
        Surface.StrokeRectangle(TileRect, Border, 8.0f, Active ? 1.5f : 1.0f);

        RenderConstraintGlyph(Surface, K, { TileRect.Center().X, TileRect.Y + TileH * 0.38f }, Ink);
        const char* Label = QueryKindLabel(K);
        const float Tw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.0f, Label);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            Active ? FontWeightClass::SemiBold : FontWeightClass::Medium,
            { TileRect.Center().X - Tw * 0.5f, TileRect.Y + TileH * 0.65f },
            10.0f,
            Ink,
            Label);

        if (Hovered && Pointer.Pressed)
        {
            if (InOutKind != K)
            {
                InOutKind = K;
                Changed   = true;
            }
        }
    }
    return Changed;
}

} // namespace Frontier
