/*============================================================================================================================================
                                                         VECTOR3CAPSULEINPUT.CPP
============================================================================================================================================*/
// 🧩 Implementation of Vector3CapsuleInput RGB X/Y/Z capsules matching HtmlReference Transform rows

#include "Engine/Components/Controls/Vector3CapsuleInput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace Frontier
{

namespace
{

constexpr std::array<const char*, 3> AxisLetters {{ "X", "Y", "Z" }};
constexpr std::array<ColorRgba, 3> AxisColors {{
    ColorRgba { 224u,  91u,  82u, 255u }, // #e05b52 X red
    ColorRgba {  84u, 196u, 104u, 255u }, // #54c468 Y green
    ColorRgba {  84u, 141u, 224u, 255u }, // #548de0 Z blue
}};

void FormatCompactFloat(float V, char* Out, std::size_t Cap) noexcept
{
    if (std::abs(V) < 1.0e-4f)
    {
        std::snprintf(Out, Cap, "0");
        return;
    }
    std::snprintf(Out, Cap, "%.4f", V);
    std::size_t Len = std::strlen(Out);
    while (Len > 0u && Out[Len - 1u] == '0')
    {
        Out[--Len] = '\0';
    }
    if (Len > 0u && Out[Len - 1u] == '.')
    {
        Out[--Len] = '\0';
    }
}

} // namespace

bool Vector3CapsuleInput::Construct(
    PixelSpace&             Surface,
    Rectangle2D             Bounds,
    const char*             Caption,
    const char*             UnitSuffix,
    float&                  InOutX,
    float&                  InOutY,
    float&                  InOutZ,
    Vector3CapsuleSession&  Session,
    const PaneInputReading& Input,
    bool                    ShowUniformLock,
    float                   ScrubSensitivity,
    double                  DeltaSeconds) noexcept
{
    bool Changed = false;
    const ControlPointer& Ptr = Input.PrimaryPointer;

    // Header row: Label + Unit + Optional Lock toggle
    WorkspaceAppearance::ConstructWeightedText(
        Surface, FontWeightClass::Medium, { Bounds.X, Bounds.Y }, 10.6f, WorkspaceSurfaceColors::SecondaryInk, Caption);
    if (UnitSuffix != nullptr && UnitSuffix[0] != '\0')
    {
        const float Lw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Medium, 10.6f, Caption);
        WorkspaceAppearance::ConstructWeightedText(
            Surface, FontWeightClass::Regular, { Bounds.X + Lw + 6.0f, Bounds.Y + 0.5f }, 9.6f, WorkspaceSurfaceColors::FaintInk, UnitSuffix);
    }

    if (ShowUniformLock)
    {
        const Rectangle2D LockRect { Bounds.Right() - 20.0f, Bounds.Y - 2.0f, 18.0f, 16.0f };
        const bool LockHov = ControlAppearance::QueryPointerOver(LockRect, Ptr);
        const ColorRgba LockInk = Session.UniformLocked
            ? ColorRgba { 222u, 222u, 219u, 255u }
            : (LockHov ? WorkspaceSurfaceColors::SecondaryInk : WorkspaceSurfaceColors::FaintInk);
        Surface.StrokeCircle({ LockRect.Center().X - 2.5f, LockRect.Center().Y }, 3.0f, LockInk, 1.2f);
        Surface.StrokeCircle({ LockRect.Center().X + 2.5f, LockRect.Center().Y }, 3.0f, LockInk, 1.2f);
        if (LockHov && Ptr.Pressed)
        {
            Session.UniformLocked = !Session.UniformLocked;
        }
    }

    constexpr float Gap = 6.0f;
    const float RowY = Bounds.Y + 18.0f;
    const float CapH = 27.0f;
    const float CapW = (Bounds.Width - Gap * 2.0f) * (1.0f / 3.0f);

    float* AxisPtrs[3] = { &InOutX, &InOutY, &InOutZ };

    for (int A = 0; A < 3; ++A)
    {
        const Rectangle2D CapRect { Bounds.X + static_cast<float>(A) * (CapW + Gap), RowY, CapW, CapH };
        const Rectangle2D AxisStripRect { CapRect.X, CapRect.Y, 15.0f, CapH };
        const bool Hovered  = ControlAppearance::QueryPointerOver(CapRect, Ptr);
        const bool Editing  = Session.EditingText && (Session.ActiveAxis == A);
        const bool Dragging = Session.Dragging && (Session.ActiveAxis == A);

        Surface.FillRectangle(CapRect, ColorRgba { 13u, 13u, 15u, 255u }, 6.0f);
        Surface.PushClip(AxisStripRect);
        Surface.FillRectangle(CapRect, AxisColors[A], 6.0f);
        Surface.PopClip();
        Surface.StrokeRectangle(
            CapRect,
            (Editing || Dragging) ? AxisColors[A] : (Hovered ? ColorRgba { 72u, 72u, 78u, 255u } : ColorRgba { 40u, 40u, 44u, 255u }),
            6.0f,
            1.0f);

        const float Bw = WorkspaceAppearance::MeasureWeightedTextWidth(Surface, FontWeightClass::Bold, 9.5f, AxisLetters[A]);
        WorkspaceAppearance::ConstructWeightedText(
            Surface,
            FontWeightClass::Bold,
            { AxisStripRect.Center().X - Bw * 0.5f, AxisStripRect.Y + 8.0f },
            9.5f,
            ColorRgba { 16u, 16u, 18u, 255u },
            AxisLetters[A]);

        const Rectangle2D TextRect { CapRect.X + 18.0f, CapRect.Y + 2.0f, CapW - 21.0f, CapH - 4.0f };
        if (Editing)
        {
            WorkspaceAppearance::ForwardInputToTextBuffer(Session.EditBuffer, Input, DeltaSeconds);
            Surface.PushClip(TextRect);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { TextRect.X + 2.0f, CapRect.Y + 7.0f }, 10.6f, WorkspaceSurfaceColors::PrimaryInk,
                Session.EditBuffer.Text, false);
            Surface.PopClip();

            if (Input.EnterPressed || (Ptr.Pressed && !Hovered))
            {
                char* EndPtr = nullptr;
                const float Parsed = std::strtof(Session.EditBuffer.Text, &EndPtr);
                if (EndPtr != Session.EditBuffer.Text && std::isfinite(Parsed))
                {
                    if (ShowUniformLock && Session.UniformLocked && std::abs(*AxisPtrs[A]) > 1.0e-5f)
                    {
                        const float Ratio = Parsed / *AxisPtrs[A];
                        InOutX *= Ratio; InOutY *= Ratio; InOutZ *= Ratio;
                    }
                    else
                    {
                        *AxisPtrs[A] = Parsed;
                    }
                    Changed = true;
                }
                Session.EditingText = false;
                Session.ActiveAxis  = -1;
            }
            else if (Input.EscapePressed)
            {
                Session.EditingText = false;
                Session.ActiveAxis  = -1;
            }
        }
        else
        {
            char ValBuf[32] {};
            FormatCompactFloat(*AxisPtrs[A], ValBuf, sizeof(ValBuf));
            Surface.PushClip(TextRect);
            WorkspaceAppearance::ConstructWeightedText(
                Surface, FontWeightClass::Regular, { TextRect.X + 2.0f, CapRect.Y + 7.0f }, 10.6f, WorkspaceSurfaceColors::PrimaryInk,
                ValBuf, false);
            Surface.PopClip();

            if (Hovered && Input.DoubleClicked)
            {
                Session.EditingText        = true;
                Session.Dragging           = false;
                Session.ActiveAxis         = A;
                Session.EditBuffer.Begin(ValBuf);
                Session.EditBuffer.SelectAll();
            }
            else if (Hovered && Ptr.Pressed)
            {
                Session.Dragging        = true;
                Session.EditingText     = false;
                Session.ActiveAxis      = A;
                Session.DragAnchorX     = Ptr.X;
                Session.DragStartScalar = *AxisPtrs[A];
            }
        }

        if (Dragging)
        {
            if (Ptr.Down)
            {
                const float DeltaPx = Ptr.X - Session.DragAnchorX;
                if (std::abs(DeltaPx) > 2.0f)
                {
                    const float NextVal = Session.DragStartScalar + DeltaPx * ScrubSensitivity;
                    if (ShowUniformLock && Session.UniformLocked && std::abs(Session.DragStartScalar) > 1.0e-5f)
                    {
                        const float Ratio = NextVal / std::max(std::abs(*AxisPtrs[A]), 1.0e-5f);
                        InOutX *= Ratio; InOutY *= Ratio; InOutZ *= Ratio;
                    }
                    else
                    {
                        *AxisPtrs[A] = NextVal;
                    }
                    Changed = true;
                }
            }
            else
            {
                if (std::abs(Ptr.X - Session.DragAnchorX) <= 2.0f && Hovered)
                {
                    char ValBuf[32] {};
                    FormatCompactFloat(*AxisPtrs[A], ValBuf, sizeof(ValBuf));
                    Session.EditingText = true;
                    Session.EditBuffer.Begin(ValBuf);
                    Session.EditBuffer.SelectAll();
                }
                Session.Dragging = false;
            }
        }
    }
    return Changed;
}

} // namespace Frontier
