/*============================================================================================================================================
                                                           VECTORICONREPOSITORY.CPP
============================================================================================================================================*/
// 🧩 SVG 1.1 path flattening, round-cap polyline stroking, and built-in vector icon lookup entries.

#include "Engine/Components/VectorIcons/VectorIconRepository.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace Frontier
{

namespace
{

constexpr float PiConstant = 3.14159265358979f;

struct SvgPathScanner
{
    std::string_view Text;
    std::size_t      Cursor = 0;

    void SkipSeparators() noexcept
    {
        while (Cursor < Text.size() && (std::isspace(static_cast<unsigned char>(Text[Cursor])) || Text[Cursor] == ','))
            ++Cursor;
    }

    [[nodiscard]] bool AtEnd() noexcept
    {
        SkipSeparators();
        return Cursor >= Text.size();
    }

    [[nodiscard]] bool PeekCommand(char &Out) noexcept
    {
        SkipSeparators();
        if (Cursor >= Text.size())
            return false;
        const char Ch = Text[Cursor];
        if (std::isalpha(static_cast<unsigned char>(Ch)) && Ch != 'e' && Ch != 'E')
        {
            Out = Ch;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ReadScalar(float &Out) noexcept
    {
        SkipSeparators();
        const std::size_t Start = Cursor;
        std::size_t       Pos   = Cursor;
        if (Pos < Text.size() && (Text[Pos] == '+' || Text[Pos] == '-'))
            ++Pos;
        bool AnyDigits = false;
        bool SeenDot   = false;
        while (Pos < Text.size())
        {
            const char Ch = Text[Pos];
            if (std::isdigit(static_cast<unsigned char>(Ch)))
            {
                AnyDigits = true;
                ++Pos;
            }
            else if (Ch == '.' && !SeenDot)
            {
                SeenDot = true;
                ++Pos;
            }
            else
            {
                break;
            }
        }
        if (!AnyDigits)
        {
            Cursor = Start;
            return false;
        }
        if (Pos < Text.size() && (Text[Pos] == 'e' || Text[Pos] == 'E'))
        {
            std::size_t ExpPos = Pos + 1u;
            if (ExpPos < Text.size() && (Text[ExpPos] == '+' || Text[ExpPos] == '-'))
                ++ExpPos;
            if (ExpPos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[ExpPos])))
            {
                while (ExpPos < Text.size() && std::isdigit(static_cast<unsigned char>(Text[ExpPos])))
                    ++ExpPos;
                Pos = ExpPos;
            }
        }
        Out    = std::strtof(std::string(Text.substr(Start, Pos - Start)).c_str(), nullptr);
        Cursor = Pos;
        return true;
    }

    [[nodiscard]] bool ReadArcBit(bool &Out) noexcept
    {
        SkipSeparators();
        if (Cursor >= Text.size())
            return false;
        if (Text[Cursor] == '0')
        {
            Out = false;
            ++Cursor;
            return true;
        }
        if (Text[Cursor] == '1')
        {
            Out = true;
            ++Cursor;
            return true;
        }
        return false;
    }
};

void AppendCubicBezier(std::vector<Point2D> &Out, Point2D P0, Point2D P1, Point2D P2, Point2D P3, int Segments) noexcept
{
    for (int Step = 1; Step <= Segments; ++Step)
    {
        const float T = static_cast<float>(Step) / static_cast<float>(Segments);
        const float U = 1.0f - T;
        const float A = U * U * U;
        const float B = 3.0f * U * U * T;
        const float C = 3.0f * U * T * T;
        const float D = T * T * T;
        Out.push_back({A * P0.X + B * P1.X + C * P2.X + D * P3.X, A * P0.Y + B * P1.Y + C * P2.Y + D * P3.Y});
    }
}

void AppendQuadraticBezier(std::vector<Point2D> &Out, Point2D P0, Point2D P1, Point2D P2, int Segments) noexcept
{
    for (int Step = 1; Step <= Segments; ++Step)
    {
        const float T = static_cast<float>(Step) / static_cast<float>(Segments);
        const float U = 1.0f - T;
        Out.push_back({U * U * P0.X + 2.0f * U * T * P1.X + T * T * P2.X,
                       U * U * P0.Y + 2.0f * U * T * P1.Y + T * T * P2.Y});
    }
}

void AppendEllipticalArc(std::vector<Point2D> &Out, Point2D P0, float Rx, float Ry, float RotationDegrees,
                         bool LargeArc, bool Sweep, Point2D P1, float StepRadians) noexcept
{
    if (Rx == 0.0f || Ry == 0.0f || (P0.X == P1.X && P0.Y == P1.Y))
    {
        Out.push_back(P1);
        return;
    }
    Rx = std::fabs(Rx);
    Ry = std::fabs(Ry);

    const float Phi    = RotationDegrees * PiConstant / 180.0f;
    const float CosPhi = std::cos(Phi);
    const float SinPhi = std::sin(Phi);
    const float Dx     = (P0.X - P1.X) * 0.5f;
    const float Dy     = (P0.Y - P1.Y) * 0.5f;
    const float X1p    = CosPhi * Dx + SinPhi * Dy;
    const float Y1p    = -SinPhi * Dx + CosPhi * Dy;

    const float Lambda = (X1p * X1p) / (Rx * Rx) + (Y1p * Y1p) / (Ry * Ry);
    if (Lambda > 1.0f)
    {
        const float ScaleUp = std::sqrt(Lambda);
        Rx *= ScaleUp;
        Ry *= ScaleUp;
    }

    const float Rx2       = Rx * Rx;
    const float Ry2       = Ry * Ry;
    float       Numerator = Rx2 * Ry2 - Rx2 * Y1p * Y1p - Ry2 * X1p * X1p;
    if (Numerator < 0.0f)
        Numerator = 0.0f;
    float Coefficient = std::sqrt(Numerator / (Rx2 * Y1p * Y1p + Ry2 * X1p * X1p));
    if (LargeArc == Sweep)
        Coefficient = -Coefficient;

    const float Cxp = Coefficient * (Rx * Y1p / Ry);
    const float Cyp = Coefficient * -(Ry * X1p / Rx);
    const float Cx  = CosPhi * Cxp - SinPhi * Cyp + (P0.X + P1.X) * 0.5f;
    const float Cy  = SinPhi * Cxp + CosPhi * Cyp + (P0.Y + P1.Y) * 0.5f;

    auto ComputeAngle = [](float Ux, float Uy, float Vx, float Vy) noexcept -> float {
        const float Dot = Ux * Vx + Uy * Vy;
        const float Len = std::sqrt((Ux * Ux + Uy * Uy) * (Vx * Vx + Vy * Vy));
        float       Ang = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Dot / Len)));
        if (Ux * Vy - Uy * Vx < 0.0f)
            Ang = -Ang;
        return Ang;
    };

    const float Theta1     = ComputeAngle(1.0f, 0.0f, (X1p - Cxp) / Rx, (Y1p - Cyp) / Ry);
    float       DeltaTheta = ComputeAngle((X1p - Cxp) / Rx, (Y1p - Cyp) / Ry, (-X1p - Cxp) / Rx, (-Y1p - Cyp) / Ry);
    if (!Sweep && DeltaTheta > 0.0f)
        DeltaTheta -= 2.0f * PiConstant;
    if (Sweep && DeltaTheta < 0.0f)
        DeltaTheta += 2.0f * PiConstant;

    const int Segments = std::max(1, static_cast<int>(std::ceil(std::fabs(DeltaTheta) / StepRadians)));
    for (int Step = 1; Step <= Segments; ++Step)
    {
        const float Angle = Theta1 + DeltaTheta * static_cast<float>(Step) / static_cast<float>(Segments);
        const float Ex    = Rx * std::cos(Angle);
        const float Ey    = Ry * std::sin(Angle);
        Out.push_back({CosPhi * Ex - SinPhi * Ey + Cx, SinPhi * Ex + CosPhi * Ey + Cy});
    }
}

const std::array<VectorIconEntry, static_cast<std::size_t>(NavigationIcon::Count)> NavigationEntries = {{
    {"ArrowUp", "M12 19V5M5 12l7-7 7 7", 24.0f, 2.0f},
    {"ArrowDown", "M12 5v14M19 12l-7 7-7-7", 24.0f, 2.0f},
    {"ArrowLeft", "M19 12H5M12 19l-7-7 7-7", 24.0f, 2.0f},
    {"ArrowRight", "M5 12h14M12 5l7 7-7 7", 24.0f, 2.0f},
    {"ChevronUp", "M18 15l-6-6-6 6", 24.0f, 2.0f},
    {"ChevronDown", "M6 9l6 6 6-6", 24.0f, 2.0f},
    {"ChevronLeft", "M15 18l-6-6 6-6", 24.0f, 2.0f},
    {"ChevronRight", "M9 18l6-6-6-6", 24.0f, 2.0f},
    {"ChevronsUp", "M17 11l-5-5-5 5M17 18l-5-5-5 5", 24.0f, 2.0f},
    {"ChevronsDown", "M7 13l5 5 5-5M7 6l5 5 5-5", 24.0f, 2.0f},
    {"ChevronsLeft", "M11 17l-5-5 5-5M18 17l-5-5 5-5", 24.0f, 2.0f},
    {"ChevronsRight", "M13 17l5-5-5-5M6 17l5-5-5-5", 24.0f, 2.0f},
    {"CornerDownRight", "M15 10l5 5-5 5M4 4v7a4 4 0 0 0 4 4h12", 24.0f, 2.0f},
    {"CornerDownLeft", "M9 10l-5 5 5 5M20 4v7a4 4 0 0 1-4 4H4", 24.0f, 2.0f},
    {"RotateClockwise", "M21 12a9 9 0 1 1-9-9c2.52 0 4.93 1 6.74 2.74L21 8M21 3v5h-5", 24.0f, 2.0f},
    {"RotateCounterClockwise", "M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8M3 3v5h5", 24.0f, 2.0f},
    {"ExpandDiagonal", "M15 3h6v6M9 21H3v-6M21 3l-7 7M3 21l7-7", 24.0f, 2.0f},
    {"CollapseDiagonal", "M4 14h6v6M20 10h-6V4M14 10l7-7M3 21l7-7", 24.0f, 2.0f},
}};

const std::array<VectorIconEntry, static_cast<std::size_t>(ControlIcon::Count)> ControlEntries = {{
    {"SettingsGear", "M9.671 4.136a2.34 2.34 0 0 1 4.659 0 2.34 2.34 0 0 0 3.319 1.915 2.34 2.34 0 0 1 2.33 4.033 2.34 2.34 0 0 0 0 3.831 2.34 2.34 0 0 1-2.33 4.033 2.34 2.34 0 0 0-3.319 1.915 2.34 2.34 0 0 1-4.659 0 2.34 2.34 0 0 0-3.32-1.915 2.34 2.34 0 0 1-2.33-4.033 2.34 2.34 0 0 0 0-3.831A2.34 2.34 0 0 1 6.35 6.051a2.34 2.34 0 0 0 3.319-1.915M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z", 24.0f, 2.0f},
    {"AppearanceSwatches", "M12 22a1 1 0 0 1 0-20 10 9 0 0 1 10 9 5 5 0 0 1-5 5h-2.25a1.75 1.75 0 0 0-1.4 2.8l.3.4a1.75 1.75 0 0 1-1.4 2.8zM14 6.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM18 10.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM7 12.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0zM9 7.5a.5.5 0 1 1-1 0 .5.5 0 0 1 1 0z", 24.0f, 2.0f},
    {"DisplayMonitor", "M4 3h16a2 2 0 0 1 2 2v10a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM8 21h8M12 17v4", 24.0f, 2.0f},
    {"InputDevices", "M12 22s8-4 8-10V5l-8-3-8 3v7c0 6 8 10 8 10z", 24.0f, 2.0f},
    {"NotificationsBell", "M10.268 21a2 2 0 0 0 3.464 0M3.262 15.326A1 1 0 0 0 4 17h16a1 1 0 0 0 .74-1.673C19.41 13.956 18 12.499 18 8A6 6 0 0 0 6 8c0 4.499-1.411 5.956-2.738 7.326", 24.0f, 2.0f},
    {"WirelessSignal", "M12 20h.01M2 8.82a15 15 0 0 1 20 0M5 12.859a10 10 0 0 1 14 0M8.5 16.429a5 5 0 0 1 7 0", 24.0f, 2.0f},
    {"BluetoothSymbol", "M7 7l10 10-5 5V2l5 5L7 17", 24.0f, 2.0f},
    {"MoonDisturbance", "M21 12.79A9 9 0 1 1 11.21 3 7 7 0 0 0 21 12.79z", 24.0f, 2.0f},
    {"VolumeSpeaker", "M11 5L6 9H2v6h4l5 4V5zm8.07-.07a10 10 0 0 1 0 14.14M15.54 8.46a5 5 0 0 1 0 7.07", 24.0f, 2.0f},
    {"SunIllumination", "M16 12a4 4 0 1 1-8 0 4 4 0 0 1 8 0zM12 2v2M12 20v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2M20 12h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41", 24.0f, 2.0f},
    {"SparklesAntiAliasing", "M11.017 2.814a1 1 0 0 1 1.966 0l1.051 5.558a2 2 0 0 0 1.594 1.594l5.558 1.051a1 1 0 0 1 0 1.966l-5.558 1.051a2 2 0 0 0-1.594 1.594l-1.051 5.558a1 1 0 0 1-1.966 0l-1.051-5.558a2 2 0 0 0-1.594-1.594l-5.558-1.051a1 1 0 0 1 0-1.966l5.558-1.051a2 2 0 0 0 1.594-1.594zM20 2v4M22 4h-4M6 20a2 2 0 1 1-4 0 2 2 0 0 1 4 0z", 24.0f, 2.0f},
    {"GaugeRate", "m12 14 4-4M3.34 19a10 10 0 1 1 17.32 0", 24.0f, 2.0f},
    {"SlidersQuality", "M10 5H3M12 19H3M14 3v4M16 17v4M21 12h-9M21 19h-5M21 5h-7M8 10v4M8 12H3", 24.0f, 2.0f},
    {"VideoRenderScale", "m16 13 5.223 3.482a.5.5 0 0 0 .777-.416V7.87a.5.5 0 0 0-.752-.432L16 10.5M4 6h10a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2H4a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2z", 24.0f, 2.0f},
    {"CloseCross", "M18 6 6 18M6 6l12 12", 24.0f, 2.0f},
    {"ChevronBack", "m15 18-6-6 6-6", 24.0f, 2.0f},
    {"ChevronForward", "m9 18 6-6-6-6", 24.0f, 2.0f},
    {"ShieldInput", "M20 13c0 5-3.5 7.5-7.66 8.95a1 1 0 0 1-.67-.01C7.5 20.5 4 18 4 13V6a1 1 0 0 1 1-1c2 0 4.5-1.2 6.24-2.72a1.17 1.17 0 0 1 1.52 0C14.51 3.81 17 5 19 5a1 1 0 0 1 1 1z", 24.0f, 2.0f},
    {"ChevronDown", "m6 9 6 6 6-6", 24.0f, 2.0f},
    {"ChevronUp", "m18 15-6-6-6 6", 24.0f, 2.0f},
    {"TriangleAlert", "m21.73 18-8-14a2 2 0 0 0-3.48 0l-8 14A2 2 0 0 0 4 21h16a2 2 0 0 0 1.73-3ZM12 9v4M12 17h.01", 24.0f, 2.0f},
    {"CircleCheck", "M22 11.08V12a10 10 0 1 1-5.93-9.14M9 11l3 3L22 4", 24.0f, 2.0f},
    {"CircleNotice", "M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zM12 16v-4M12 8h.01", 24.0f, 2.0f},
    {"OctagonAlert", "M12 16h.01M12 8v4M15.312 2a2 2 0 0 1 1.414.586l4.688 4.688A2 2 0 0 1 22 8.688v6.624a2 2 0 0 1-.586 1.414l-4.688 4.688a2 2 0 0 1-1.414.586H8.688a2 2 0 0 1-1.414-.586l-4.688-4.688A2 2 0 0 1 2 15.312V8.688a2 2 0 0 1 .586-1.414l4.688-4.688A2 2 0 0 1 8.688 2z", 24.0f, 2.0f},
    {"EyeVisible", "M2.062 12.348a1 1 0 0 1 0-.696 10.75 10.75 0 0 1 19.876 0 1 1 0 0 1 0 .696 10.75 10.75 0 0 1-19.876 0M15 12a3 3 0 1 1-6 0 3 3 0 0 1 6 0z", 24.0f, 2.0f},
    {"EyeHidden", "M10.733 5.076a10.744 10.744 0 0 1 11.205 6.575 1 1 0 0 1 0 .696 10.747 10.747 0 0 1-1.444 2.49M14.084 14.158a3 3 0 0 1-4.242-4.242M17.479 17.499a10.75 10.75 0 0 1-15.417-5.151 1 1 0 0 1 0-.696 10.75 10.75 0 0 1 4.446-5.143M2 2l20 20", 24.0f, 2.0f},
    {"LockClosed", "M5 13a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2zM7 11V7a5 5 0 0 1 10 0v4", 24.0f, 2.0f},
    {"LockOpen", "M5 13a2 2 0 0 1 2-2h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2zM7 11V7a5 5 0 0 1 9.9-1", 24.0f, 2.0f},
    {"MotionActivity", "M22 12h-2.48a2 2 0 0 0-1.93 1.46l-2.35 8.36a.25.25 0 0 1-.48 0L9.24 2.18a.25.25 0 0 0-.48 0l-2.35 8.36A2 2 0 0 1 4.49 12H2", 24.0f, 2.0f},
    {"FolderClosed", "M20 20a2 2 0 0 0 2-2V8a2 2 0 0 0-2-2h-7.9a2 2 0 0 1-1.69-.9L9.6 3.9A2 2 0 0 0 7.93 3H4a2 2 0 0 0-2 2v13a2 2 0 0 0 2 2z", 24.0f, 2.0f},
    {"FolderOpen", "M2 7.5V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H20a2 2 0 0 1 2 2v1.5M2.239 18.578A2 2 0 0 0 4 20h16a2 2 0 0 0 1.964-1.618l1.402-7A1 1 0 0 0 22.386 10H1.614a1 1 0 0 0-.98 1.382z", 24.0f, 2.0f},
    {"CubeShape", "M21 8a2 2 0 0 0-1-1.73l-7-4a2 2 0 0 0-2 0l-7 4A2 2 0 0 0 3 8v8a2 2 0 0 0 1 1.73l7 4a2 2 0 0 0 2 0l7-4A2 2 0 0 0 21 16zM3.3 7l8.7 5 8.7-5M12 22V12", 24.0f, 2.0f},
    {"SearchGlass", "M18 11a7 7 0 1 1-14 0 7 7 0 0 1 14 0zM21 21l-4.35-4.35", 24.0f, 2.0f},
    {"PlusAdd", "M5 12h14M12 5v14", 24.0f, 2.0f},
    {"TrashDelete", "M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2M19 6l-1 14a2 2 0 0 1-2 2H8a2 2 0 0 1-2-2L5 6M10 11v6M14 11v6", 24.0f, 2.0f},
    {"LayoutSplit", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM12 3v18", 24.0f, 2.0f},
    {"LayoutPanelLeft", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM9 3v18", 24.0f, 2.0f},
    {"LayoutPanelRight", "M5 3h14a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM15 3v18", 24.0f, 2.0f},
    {"CameraBody", "M16 8a2 2 0 0 0-2-2H4a2 2 0 0 0-2 2v8a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2zM16 10l5.24-3.14a.5.5 0 0 1 .76.43v9.42a.5.5 0 0 1-.76.43L16 14", 24.0f, 2.0f},
    {"LayersSlabs", "M12.83 2.18a2 2 0 0 0-1.66 0L2.6 6.08a1 1 0 0 0 0 1.83l8.58 3.91a2 2 0 0 0 1.66 0l8.58-3.9a1 1 0 0 0 0-1.83zM2 12a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 12M2 17a1 1 0 0 0 .58.91l8.6 3.91a2 2 0 0 0 1.65 0l8.58-3.9A1 1 0 0 0 22 17", 24.0f, 2.0f},
    {"SaveDisk", "M15.2 3a2 2 0 0 1 1.4.6l3.8 3.8a2 2 0 0 1 .6 1.4V19a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2zM17 21v-7a1 1 0 0 0-1-1H8a1 1 0 0 0-1 1v7M7 3v4a1 1 0 0 0 1 1h7", 24.0f, 2.0f},
    {"ExternalOpen", "M15 3h6v6M10 14L21 3M18 13v6a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V8a2 2 0 0 1 2-2h6", 24.0f, 2.0f},
    {"LightbulbHint", "M15 14c.2-1 .7-1.7 1.5-2.5 1-.9 1.5-2.2 1.5-3.5A6 6 0 0 0 6 8c0 1 .2 2.2 1.5 3.5.7.7 1.3 1.5 1.5 2.5M9 18h6M10 22h4", 24.0f, 2.0f},
    {"QuestionCircle", "M12 2a10 10 0 1 1 0 20 10 10 0 0 1 0-20zM9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3M12 17h.01", 24.0f, 2.0f},
    {"WrenchRepair", "M14.7 6.3a1 1 0 0 0 0 1.4l1.6 1.6a1 1 0 0 0 1.4 0l3.77-3.77a6 6 0 0 1-7.94 7.94l-6.91 6.91a2.12 2.12 0 0 1-3-3l6.91-6.91a6 6 0 0 1 7.94-7.94l-3.76 3.76z", 24.0f, 2.0f},
    {"RefreshCycle", "M3 12a9 9 0 0 1 15-6.7L21 8M21 3v5h-5M21 12a9 9 0 0 1-15 6.7L3 16M3 21v-5h5", 24.0f, 2.0f},
    {"LayoutGrid", "M4 4h6a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM14 4h6a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-6a1 1 0 0 1-1-1V5a1 1 0 0 1 1-1zM4 14h6a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1H4a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1zM14 14h6a1 1 0 0 1 1 1v5a1 1 0 0 1-1 1h-6a1 1 0 0 1-1-1v-5a1 1 0 0 1 1-1z", 24.0f, 2.0f},
    {"LayoutList", "M4 6h.01M9 6h11M4 12h.01M9 12h11M4 18h.01M9 18h11", 24.0f, 2.0f},
    {"LayoutColumns", "M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM9.5 4v16M14.5 4v16", 24.0f, 2.0f},
    {"LayoutRows", "M5 4h14a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2zM3 9.5h18M3 14.5h18", 24.0f, 2.0f},
}};

} // namespace

std::vector<VectorIconRepository::Contour> VectorIconRepository::FlattenSvgPath(std::string_view SvgPath) noexcept
{
    std::vector<Contour> Contours;
    SvgPathScanner       Scanner{SvgPath};

    Point2D Current{};
    Point2D SubpathStart{};
    Point2D LastControl{};
    char    Command         = 0;
    char    PreviousCommand = 0;

    auto BeginContour = [&](Point2D Start) {
        Contours.push_back({});
        Contours.back().Points.push_back(Start);
        SubpathStart = Start;
    };
    auto EnsureContour = [&]() {
        if (Contours.empty() || Contours.back().Closed)
            BeginContour(Current);
    };

    while (!Scanner.AtEnd())
    {
        char Next = 0;
        if (Scanner.PeekCommand(Next))
        {
            Command = Next;
            ++Scanner.Cursor;
        }
        else if (Command == 0)
        {
            break;
        }
        else if (Command == 'M')
        {
            Command = 'L';
        }
        else if (Command == 'm')
        {
            Command = 'l';
        }

        const bool    Relative = std::islower(static_cast<unsigned char>(Command)) != 0;
        const Point2D OriginPt = Relative ? Current : Point2D{};

        switch (std::toupper(static_cast<unsigned char>(Command)))
        {
        case 'M':
        {
            float X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            Current = {OriginPt.X + X, OriginPt.Y + Y};
            BeginContour(Current);
            break;
        }
        case 'L':
        {
            float X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            Current = {OriginPt.X + X, OriginPt.Y + Y};
            Contours.back().Points.push_back(Current);
            break;
        }
        case 'H':
        {
            float X = 0.0f;
            if (!Scanner.ReadScalar(X))
                return Contours;
            EnsureContour();
            Current.X = OriginPt.X + X;
            Contours.back().Points.push_back(Current);
            break;
        }
        case 'V':
        {
            float Y = 0.0f;
            if (!Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            Current.Y = OriginPt.Y + Y;
            Contours.back().Points.push_back(Current);
            break;
        }
        case 'C':
        {
            float X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f, X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X1) || !Scanner.ReadScalar(Y1) || !Scanner.ReadScalar(X2) ||
                !Scanner.ReadScalar(Y2) || !Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            const Point2D P1{OriginPt.X + X1, OriginPt.Y + Y1};
            const Point2D P2{OriginPt.X + X2, OriginPt.Y + Y2};
            const Point2D P3{OriginPt.X + X, OriginPt.Y + Y};
            AppendCubicBezier(Contours.back().Points, Current, P1, P2, P3, CurveSegments);
            LastControl = P2;
            Current     = P3;
            break;
        }
        case 'S':
        {
            float X2 = 0.0f, Y2 = 0.0f, X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X2) || !Scanner.ReadScalar(Y2) || !Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            const char    PrevUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(PreviousCommand)));
            const Point2D P1        = (PrevUpper == 'C' || PrevUpper == 'S')
                                          ? Point2D{2.0f * Current.X - LastControl.X, 2.0f * Current.Y - LastControl.Y}
                                          : Current;
            const Point2D P2{OriginPt.X + X2, OriginPt.Y + Y2};
            const Point2D P3{OriginPt.X + X, OriginPt.Y + Y};
            AppendCubicBezier(Contours.back().Points, Current, P1, P2, P3, CurveSegments);
            LastControl = P2;
            Current     = P3;
            break;
        }
        case 'Q':
        {
            float X1 = 0.0f, Y1 = 0.0f, X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X1) || !Scanner.ReadScalar(Y1) || !Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            const Point2D P1{OriginPt.X + X1, OriginPt.Y + Y1};
            const Point2D P2{OriginPt.X + X, OriginPt.Y + Y};
            AppendQuadraticBezier(Contours.back().Points, Current, P1, P2, CurveSegments);
            LastControl = P1;
            Current     = P2;
            break;
        }
        case 'T':
        {
            float X = 0.0f, Y = 0.0f;
            if (!Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            const char    PrevUpper = static_cast<char>(std::toupper(static_cast<unsigned char>(PreviousCommand)));
            const Point2D P1        = (PrevUpper == 'Q' || PrevUpper == 'T')
                                          ? Point2D{2.0f * Current.X - LastControl.X, 2.0f * Current.Y - LastControl.Y}
                                          : Current;
            const Point2D P2{OriginPt.X + X, OriginPt.Y + Y};
            AppendQuadraticBezier(Contours.back().Points, Current, P1, P2, CurveSegments);
            LastControl = P1;
            Current     = P2;
            break;
        }
        case 'A':
        {
            float Rx = 0.0f, Ry = 0.0f, Rot = 0.0f, X = 0.0f, Y = 0.0f;
            bool  Large = false, Sweep = false;
            if (!Scanner.ReadScalar(Rx) || !Scanner.ReadScalar(Ry) || !Scanner.ReadScalar(Rot) ||
                !Scanner.ReadArcBit(Large) || !Scanner.ReadArcBit(Sweep) || !Scanner.ReadScalar(X) || !Scanner.ReadScalar(Y))
                return Contours;
            EnsureContour();
            const Point2D EndPt{OriginPt.X + X, OriginPt.Y + Y};
            AppendEllipticalArc(Contours.back().Points, Current, Rx, Ry, Rot, Large, Sweep, EndPt, ArcStepRadians);
            Current = EndPt;
            break;
        }
        case 'Z':
        {
            if (!Contours.empty())
                Contours.back().Closed = true;
            Current = SubpathStart;
            break;
        }
        default:
            return Contours;
        }
        PreviousCommand = Command;
    }
    return Contours;
}

void VectorIconRepository::StrokeSvgPath(PixelSpace &Surface, std::string_view SvgPath, const VectorIconPlacement &Placement,
                                      float ViewBox) noexcept
{
    const float Scale     = Placement.Size / ViewBox;
    const float Thickness = Placement.StrokeWidth * Scale;

    for (const Contour &PathContour : FlattenSvgPath(SvgPath))
    {
        if (PathContour.Points.empty())
            continue;

        std::vector<Point2D> Scaled;
        Scaled.reserve(PathContour.Points.size());
        for (const Point2D &Pt : PathContour.Points)
            Scaled.push_back({Placement.X + Pt.X * Scale, Placement.Y + Pt.Y * Scale});

        const float DeltaSq = (Scaled.back().X - Scaled.front().X) * (Scaled.back().X - Scaled.front().X) +
                              (Scaled.back().Y - Scaled.front().Y) * (Scaled.back().Y - Scaled.front().Y);
        if (Scaled.size() == 1u || (Scaled.size() == 2u && DeltaSq < 0.04f))
        {
            Point2D Dot[12]{};
            for (int Step = 0; Step < 12; ++Step)
            {
                const float Angle = 2.0f * PiConstant * static_cast<float>(Step) / 12.0f;
                Dot[Step]         = {Scaled[0].X + 0.5f * Thickness * std::cos(Angle),
                                     Scaled[0].Y + 0.5f * Thickness * std::sin(Angle)};
            }
            Surface.FillPolygon(Dot, Placement.Ink);
            continue;
        }

        Surface.StrokePolyline(Scaled, Placement.Ink, Thickness, PathContour.Closed);

        if (!PathContour.Closed)
        {
            for (const Point2D &EndPt : {Scaled.front(), Scaled.back()})
            {
                Point2D Cap[10]{};
                for (int Step = 0; Step < 10; ++Step)
                {
                    const float Angle = 2.0f * PiConstant * static_cast<float>(Step) / 10.0f;
                    Cap[Step]         = {EndPt.X + 0.5f * Thickness * std::cos(Angle),
                                         EndPt.Y + 0.5f * Thickness * std::sin(Angle)};
                }
                Surface.FillPolygon(Cap, Placement.Ink);
            }
        }
    }
}

void VectorIconRepository::FillSvgPath(PixelSpace &Surface, std::string_view SvgPath, const VectorIconPlacement &Placement,
                                    float ViewBox) noexcept
{
    const float Scale = Placement.Size / ViewBox;
    for (const Contour &PathContour : FlattenSvgPath(SvgPath))
    {
        if (!PathContour.Closed || PathContour.Points.size() < 3u)
            continue;
        std::vector<Point2D> Scaled;
        Scaled.reserve(PathContour.Points.size());
        for (const Point2D &Pt : PathContour.Points)
            Scaled.push_back({Placement.X + Pt.X * Scale, Placement.Y + Pt.Y * Scale});
        Surface.FillPolygon(Scaled, Placement.Ink);
    }
}

const VectorIconEntry &VectorIconRepository::QueryNavigation(NavigationIcon Icon) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Icon);
    return Index < NavigationEntries.size() ? NavigationEntries[Index] : NavigationEntries[0];
}

const VectorIconEntry &VectorIconRepository::QueryControl(ControlIcon Icon) noexcept
{
    const std::size_t Index = static_cast<std::size_t>(Icon);
    return Index < ControlEntries.size() ? ControlEntries[Index] : ControlEntries[0];
}

void VectorIconRepository::ConstructNavigation(PixelSpace &Surface, NavigationIcon Icon, const VectorIconPlacement &Placement) noexcept
{
    const VectorIconEntry &Entry = QueryNavigation(Icon);
    StrokeSvgPath(Surface, Entry.SvgPath, Placement, Entry.ViewBox);
}

void VectorIconRepository::ConstructControl(PixelSpace &Surface, ControlIcon Icon, const VectorIconPlacement &Placement) noexcept
{
    const VectorIconEntry &Entry = QueryControl(Icon);
    StrokeSvgPath(Surface, Entry.SvgPath, Placement, Entry.ViewBox);
}

} // namespace Frontier
