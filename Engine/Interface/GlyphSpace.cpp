//============================================================================================================================================
//                                                        GLYPHSPACE.CPP
//============================================================================================================================================
// 🧩 SVG path flattening and stroking for lucide glyphs — see GlyphSpace.h.

#include "GlyphSpace.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <string>

namespace Frontier {

namespace {

constexpr float Pi = 3.14159265358979f;

//------------------------------------------------------------------------------------------------------------------------
//                                                     PATH SCANNER
//------------------------------------------------------------------------------------------------------------------------

struct PathScanner
{
    std::string_view Text;
    size_t           Cursor = 0;

    void SkipSeparators() noexcept
    {
        while (Cursor < Text.size() && (std::isspace(static_cast<unsigned char>(Text[Cursor])) || Text[Cursor] == ','))
            ++Cursor;
    }

    [[nodiscard]] bool AtEnd() noexcept { SkipSeparators(); return Cursor >= Text.size(); }

    [[nodiscard]] bool PeekCommand(char& Out) noexcept
    {
        SkipSeparators();
        if (Cursor >= Text.size()) return false;
        const char C = Text[Cursor];
        if (std::isalpha(static_cast<unsigned char>(C)) && C != 'e' && C != 'E') { Out = C; return true; }
        return false;
    }

    // SVG numbers: optional sign, digits, optional fraction, optional exponent. Two numbers may abut
    //    ("1.5.5" → 1.5 and .5; "-1-2" → -1 and -2), which is why a hand scanner is used instead of strtod alone.
    [[nodiscard]] bool Number(float& Out) noexcept
    {
        SkipSeparators();
        const size_t Start = Cursor;
        size_t I = Cursor;
        if (I < Text.size() && (Text[I] == '+' || Text[I] == '-')) ++I;
        bool Digits = false, Dot = false;
        while (I < Text.size())
        {
            const char C = Text[I];
            if (std::isdigit(static_cast<unsigned char>(C))) { Digits = true; ++I; }
            else if (C == '.' && !Dot)                        { Dot = true;    ++I; }
            else break;
        }
        if (!Digits) { Cursor = Start; return false; }
        if (I < Text.size() && (Text[I] == 'e' || Text[I] == 'E'))
        {
            size_t J = I + 1;
            if (J < Text.size() && (Text[J] == '+' || Text[J] == '-')) ++J;
            if (J < Text.size() && std::isdigit(static_cast<unsigned char>(Text[J])))
            {
                while (J < Text.size() && std::isdigit(static_cast<unsigned char>(Text[J]))) ++J;
                I = J;
            }
        }
        Out    = std::strtof(std::string(Text.substr(Start, I - Start)).c_str(), nullptr);
        Cursor = I;
        return true;
    }

    // Arc flags are single characters that may abut the next number ("1 1 0 0 1 8 0" or "0018 0").
    [[nodiscard]] bool Flag(bool& Out) noexcept
    {
        SkipSeparators();
        if (Cursor >= Text.size()) return false;
        if (Text[Cursor] == '0') { Out = false; ++Cursor; return true; }
        if (Text[Cursor] == '1') { Out = true;  ++Cursor; return true; }
        return false;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    CURVE FLATTENING
//------------------------------------------------------------------------------------------------------------------------

void AppendCubic(std::vector<PlanePoint>& Out, PlanePoint P0, PlanePoint P1, PlanePoint P2, PlanePoint P3, int Segments) noexcept
{
    for (int I = 1; I <= Segments; ++I)
    {
        const float T = static_cast<float>(I) / Segments, U = 1.0f - T;
        const float A = U * U * U, B = 3.0f * U * U * T, C = 3.0f * U * T * T, D = T * T * T;
        Out.push_back({ A * P0.X + B * P1.X + C * P2.X + D * P3.X,
                        A * P0.Y + B * P1.Y + C * P2.Y + D * P3.Y });
    }
}

void AppendQuadratic(std::vector<PlanePoint>& Out, PlanePoint P0, PlanePoint P1, PlanePoint P2, int Segments) noexcept
{
    for (int I = 1; I <= Segments; ++I)
    {
        const float T = static_cast<float>(I) / Segments, U = 1.0f - T;
        Out.push_back({ U * U * P0.X + 2.0f * U * T * P1.X + T * T * P2.X,
                        U * U * P0.Y + 2.0f * U * T * P1.Y + T * T * P2.Y });
    }
}

// SVG 1.1 §F.6.5 — endpoint to centre parameterisation, then uniform angular flattening.
void AppendArc(std::vector<PlanePoint>& Out, PlanePoint P0, float Rx, float Ry, float RotationDegrees,
               bool LargeArc, bool Sweep, PlanePoint P1, float StepRadians) noexcept
{
    if (Rx == 0.0f || Ry == 0.0f || (P0.X == P1.X && P0.Y == P1.Y)) { Out.push_back(P1); return; }
    Rx = std::fabs(Rx); Ry = std::fabs(Ry);

    const float Phi = RotationDegrees * Pi / 180.0f;
    const float CosPhi = std::cos(Phi), SinPhi = std::sin(Phi);
    const float Dx = (P0.X - P1.X) * 0.5f, Dy = (P0.Y - P1.Y) * 0.5f;
    const float X1p =  CosPhi * Dx + SinPhi * Dy;
    const float Y1p = -SinPhi * Dx + CosPhi * Dy;

    // Scale radii up if the endpoints are too far apart for them (F.6.6.2).
    const float Lambda = (X1p * X1p) / (Rx * Rx) + (Y1p * Y1p) / (Ry * Ry);
    if (Lambda > 1.0f) { const float S = std::sqrt(Lambda); Rx *= S; Ry *= S; }

    const float Rx2 = Rx * Rx, Ry2 = Ry * Ry;
    float Numerator = Rx2 * Ry2 - Rx2 * Y1p * Y1p - Ry2 * X1p * X1p;
    if (Numerator < 0.0f) Numerator = 0.0f;
    float Coefficient = std::sqrt(Numerator / (Rx2 * Y1p * Y1p + Ry2 * X1p * X1p));
    if (LargeArc == Sweep) Coefficient = -Coefficient;

    const float Cxp =  Coefficient * (Rx * Y1p / Ry);
    const float Cyp =  Coefficient * -(Ry * X1p / Rx);
    const float Cx  = CosPhi * Cxp - SinPhi * Cyp + (P0.X + P1.X) * 0.5f;
    const float Cy  = SinPhi * Cxp + CosPhi * Cyp + (P0.Y + P1.Y) * 0.5f;

    auto Angle = [](float Ux, float Uy, float Vx, float Vy)
    {
        const float Dot = Ux * Vx + Uy * Vy;
        const float Len = std::sqrt((Ux * Ux + Uy * Uy) * (Vx * Vx + Vy * Vy));
        float A = std::acos(std::fmax(-1.0f, std::fmin(1.0f, Dot / Len)));
        if (Ux * Vy - Uy * Vx < 0.0f) A = -A;
        return A;
    };

    const float Theta1 = Angle(1.0f, 0.0f, (X1p - Cxp) / Rx, (Y1p - Cyp) / Ry);
    float DeltaTheta   = Angle((X1p - Cxp) / Rx, (Y1p - Cyp) / Ry, (-X1p - Cxp) / Rx, (-Y1p - Cyp) / Ry);
    if (!Sweep && DeltaTheta > 0.0f) DeltaTheta -= 2.0f * Pi;
    if ( Sweep && DeltaTheta < 0.0f) DeltaTheta += 2.0f * Pi;

    const int Segments = std::max(1, static_cast<int>(std::ceil(std::fabs(DeltaTheta) / StepRadians)));
    for (int I = 1; I <= Segments; ++I)
    {
        const float T  = Theta1 + DeltaTheta * static_cast<float>(I) / Segments;
        const float Ex = Rx * std::cos(T), Ey = Ry * std::sin(T);
        Out.push_back({ CosPhi * Ex - SinPhi * Ey + Cx, SinPhi * Ex + CosPhi * Ey + Cy });
    }
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                        FLATTEN
//------------------------------------------------------------------------------------------------------------------------

std::vector<GlyphSpace::Contour> GlyphSpace::Flatten(std::string_view SvgPath) noexcept
{
    std::vector<Contour> Contours;
    PathScanner Scan{ SvgPath };

    PlanePoint Current{}, SubpathStart{}, LastControl{};
    char Command = 0, PreviousCommand = 0;

    auto Begin = [&](PlanePoint Start)
    {
        Contours.push_back({});
        Contours.back().Points.push_back(Start);
        SubpathStart = Start;
    };
    auto Ensure = [&]()
    {
        if (Contours.empty() || Contours.back().Closed) Begin(Current);
    };

    while (!Scan.AtEnd())
    {
        char Next;
        if (Scan.PeekCommand(Next)) { Command = Next; ++Scan.Cursor; }
        else if (Command == 0) break;
        // After M/m, implicit repeats are L/l.
        else if (Command == 'M') Command = 'L';
        else if (Command == 'm') Command = 'l';

        const bool Relative = std::islower(static_cast<unsigned char>(Command)) != 0;
        const PlanePoint Base = Relative ? Current : PlanePoint{};

        switch (std::toupper(static_cast<unsigned char>(Command)))
        {
            case 'M':
            {
                float X, Y; if (!Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Current = { Base.X + X, Base.Y + Y };
                Begin(Current);
                break;
            }
            case 'L':
            {
                float X, Y; if (!Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure(); Current = { Base.X + X, Base.Y + Y }; Contours.back().Points.push_back(Current);
                break;
            }
            case 'H':
            {
                float X; if (!Scan.Number(X)) return Contours;
                Ensure(); Current.X = Base.X + X; Contours.back().Points.push_back(Current);
                break;
            }
            case 'V':
            {
                float Y; if (!Scan.Number(Y)) return Contours;
                Ensure(); Current.Y = Base.Y + Y; Contours.back().Points.push_back(Current);
                break;
            }
            case 'C':
            {
                float X1, Y1, X2, Y2, X, Y;
                if (!Scan.Number(X1) || !Scan.Number(Y1) || !Scan.Number(X2) || !Scan.Number(Y2) || !Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure();
                const PlanePoint P1{ Base.X + X1, Base.Y + Y1 }, P2{ Base.X + X2, Base.Y + Y2 }, P3{ Base.X + X, Base.Y + Y };
                AppendCubic(Contours.back().Points, Current, P1, P2, P3, CurveSegments);
                LastControl = P2; Current = P3;
                break;
            }
            case 'S':
            {
                float X2, Y2, X, Y;
                if (!Scan.Number(X2) || !Scan.Number(Y2) || !Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure();
                const char P = static_cast<char>(std::toupper(static_cast<unsigned char>(PreviousCommand)));
                const PlanePoint P1 = (P == 'C' || P == 'S') ? PlanePoint{ 2.0f * Current.X - LastControl.X, 2.0f * Current.Y - LastControl.Y } : Current;
                const PlanePoint P2{ Base.X + X2, Base.Y + Y2 }, P3{ Base.X + X, Base.Y + Y };
                AppendCubic(Contours.back().Points, Current, P1, P2, P3, CurveSegments);
                LastControl = P2; Current = P3;
                break;
            }
            case 'Q':
            {
                float X1, Y1, X, Y;
                if (!Scan.Number(X1) || !Scan.Number(Y1) || !Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure();
                const PlanePoint P1{ Base.X + X1, Base.Y + Y1 }, P2{ Base.X + X, Base.Y + Y };
                AppendQuadratic(Contours.back().Points, Current, P1, P2, CurveSegments);
                LastControl = P1; Current = P2;
                break;
            }
            case 'T':
            {
                float X, Y; if (!Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure();
                const char P = static_cast<char>(std::toupper(static_cast<unsigned char>(PreviousCommand)));
                const PlanePoint P1 = (P == 'Q' || P == 'T') ? PlanePoint{ 2.0f * Current.X - LastControl.X, 2.0f * Current.Y - LastControl.Y } : Current;
                const PlanePoint P2{ Base.X + X, Base.Y + Y };
                AppendQuadratic(Contours.back().Points, Current, P1, P2, CurveSegments);
                LastControl = P1; Current = P2;
                break;
            }
            case 'A':
            {
                float Rx, Ry, Rot, X, Y; bool Large, Sweep;
                if (!Scan.Number(Rx) || !Scan.Number(Ry) || !Scan.Number(Rot) || !Scan.Flag(Large) || !Scan.Flag(Sweep) || !Scan.Number(X) || !Scan.Number(Y)) return Contours;
                Ensure();
                const PlanePoint End{ Base.X + X, Base.Y + Y };
                AppendArc(Contours.back().Points, Current, Rx, Ry, Rot, Large, Sweep, End, ArcStepRadians);
                Current = End;
                break;
            }
            case 'Z':
            {
                if (!Contours.empty()) Contours.back().Closed = true;
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

//------------------------------------------------------------------------------------------------------------------------
//                                                     STROKE / FILL
//------------------------------------------------------------------------------------------------------------------------

void GlyphSpace::Stroke(PixelSpace& Surface, std::string_view SvgPath, const GlyphPlacement& Placement, float ViewBox) noexcept
{
    const float Scale = Placement.Size / ViewBox;
    const float Thickness = Placement.StrokeWidth * Scale;

    for (const Contour& C : Flatten(SvgPath))
    {
        if (C.Points.empty()) continue;

        std::vector<PlanePoint> Scaled;
        Scaled.reserve(C.Points.size());
        for (const PlanePoint& P : C.Points)
            Scaled.push_back({ Placement.X + P.X * Scale, Placement.Y + P.Y * Scale });

        if (Scaled.size() == 1u)
        {
            // Zero-length sub-path ("M12 20h.01" in lucide wifi) is a round dot under stroke-linecap:round.
            std::vector<PlanePoint> Dot;
            constexpr int Segments = 12;
            for (int I = 0; I < Segments; ++I)
            {
                const float A = 2.0f * Pi * static_cast<float>(I) / Segments;
                Dot.push_back({ Scaled[0].X + 0.5f * Thickness * std::cos(A), Scaled[0].Y + 0.5f * Thickness * std::sin(A) });
            }
            Surface.FillPolygon(Dot.data(), static_cast<uint32_t>(Dot.size()), Placement.Colour);
            continue;
        }

        Surface.StrokePolyline(Scaled.data(), static_cast<uint32_t>(Scaled.size()), Placement.Colour, Thickness, C.Closed);

        // lucide is stroke-linecap:round / stroke-linejoin:round; ImGui polylines are butt-capped with mitred-ish
        //    joins. Round caps are restored by capping each open end with a disc of the stroke radius.
        if (!C.Closed)
        {
            for (const PlanePoint& End : { Scaled.front(), Scaled.back() })
            {
                std::vector<PlanePoint> Cap;
                constexpr int Segments = 10;
                for (int I = 0; I < Segments; ++I)
                {
                    const float A = 2.0f * Pi * static_cast<float>(I) / Segments;
                    Cap.push_back({ End.X + 0.5f * Thickness * std::cos(A), End.Y + 0.5f * Thickness * std::sin(A) });
                }
                Surface.FillPolygon(Cap.data(), static_cast<uint32_t>(Cap.size()), Placement.Colour);
            }
        }
    }
}

void GlyphSpace::Fill(PixelSpace& Surface, std::string_view SvgPath, const GlyphPlacement& Placement, float ViewBox) noexcept
{
    const float Scale = Placement.Size / ViewBox;
    for (const Contour& C : Flatten(SvgPath))
    {
        if (!C.Closed || C.Points.size() < 3u) continue;
        std::vector<PlanePoint> Scaled;
        Scaled.reserve(C.Points.size());
        for (const PlanePoint& P : C.Points)
            Scaled.push_back({ Placement.X + P.X * Scale, Placement.Y + P.Y * Scale });
        Surface.FillPolygon(Scaled.data(), static_cast<uint32_t>(Scaled.size()), Placement.Colour);
    }
}

} // namespace Frontier
