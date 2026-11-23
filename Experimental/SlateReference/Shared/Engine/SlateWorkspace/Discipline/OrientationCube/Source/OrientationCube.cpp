//============================================================================================================================================
//                                                        ORIENTATIONCUBE.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/OrientationCube/Api/OrientationCube.h"

#include <algorithm>
#include <cmath>

namespace Slate
{

CubeBasis CubeBasisFromYawPitch(double YawDegrees, double PitchDegrees)
{
    const double Yaw = YawDegrees * 3.14159265358979323846 / 180.0;
    const double Pitch = PitchDegrees * 3.14159265358979323846 / 180.0;
    const double CosP = std::cos(Pitch);
    const double SinP = std::sin(Pitch);
    const double SinY = std::sin(Yaw);
    const double CosY = std::cos(Yaw);

    CubeBasis Basis;
    Basis.Forward[0] = CosP * SinY;
    Basis.Forward[1] = SinP;
    Basis.Forward[2] = CosP * CosY;
    Basis.Right[0] = CosY;
    Basis.Right[1] = 0.0;
    Basis.Right[2] = -SinY;
    Basis.Up[0] = -SinP * SinY;
    Basis.Up[1] = CosP;
    Basis.Up[2] = -SinP * CosY;
    return Basis;
}

double CubeAxisDepth(const CubeBasis& Basis, const double Axis[3])
{
    // The HTML reference (References/Cad/js/viewport3d.js::vp3Basis/vp3Gizmo) uses
    // camera-forward = target - eye for depth ordering. the hosts' yaw-pitch stores
    // the eye ray in Basis.Forward, while Parametric passes an already camera-forward
    // frame, so the public gizmo projection always normalizes depth through this helper
    // instead of letting each host guess front/back differently.
    return -(Axis[0] * Basis.Forward[0] + Axis[1] * Basis.Forward[1] + Axis[2] * Basis.Forward[2]);
}

void CubeAxisPoint(const CubeBasis& Basis,
                                           const PlaneExtent& Extent,
                                           const double Axis[3],
                                           float& X,
                                           float& Y,
                                           double& Depth)
{
    constexpr float Radius = 34.0f;
    const float CentreX = Extent.MaximumX - 70.0f;
    const float CentreY = Extent.MinimumY + 58.0f;
    const double SX = Axis[0] * Basis.Right[0] + Axis[1] * Basis.Right[1] + Axis[2] * Basis.Right[2];
    const double SY = Axis[0] * Basis.Up[0] + Axis[1] * Basis.Up[1] + Axis[2] * Basis.Up[2];
    Depth = CubeAxisDepth(Basis, Axis);
    X = CentreX + static_cast<float>(SX) * Radius;
    Y = CentreY - static_cast<float>(SY) * Radius;
}

void RecordOrientationBall(RecordingSurface& Surface,
                                                 const PlaneExtent& Extent,
                                                 const CubeBasis& Basis)
{
    struct AxisRecord
    {
        double Axis[3];
        ThemeToken Colour;
        bool Positive;
        const char* Label;
    };

    const AxisRecord Axes[6] =
    {
        { {  1.0,  0.0,  0.0 }, ThemeToken{ 0xFCu, 0x5Au, 0x5Au, 255u }, true,  "X" },
        { { -1.0,  0.0,  0.0 }, ThemeToken{ 0xFCu, 0x5Au, 0x5Au, 255u }, false, ""  },
        { {  0.0,  1.0,  0.0 }, ThemeToken{ 0x7Bu, 0xD6u, 0x6Au, 255u }, true,  "Y" },
        { {  0.0, -1.0,  0.0 }, ThemeToken{ 0x7Bu, 0xD6u, 0x6Au, 255u }, false, ""  },
        { {  0.0,  0.0,  1.0 }, ThemeToken{ 0x5Au, 0x8Bu, 0xFCu, 255u }, true,  "Z" },
        { {  0.0,  0.0, -1.0 }, ThemeToken{ 0x5Au, 0x8Bu, 0xFCu, 255u }, false, ""  },
    };

    struct ProjectedAxis
    {
        AxisRecord Axis;
        float X;
        float Y;
        double Depth;
    };

    ProjectedAxis Projected[6] = {};
    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        Projected[Index].Axis = Axes[Index];
        CubeAxisPoint(Basis, Extent, Axes[Index].Axis,
                                       Projected[Index].X, Projected[Index].Y, Projected[Index].Depth);
    }

    std::sort(Projected, Projected + 6u, [](const ProjectedAxis& Left, const ProjectedAxis& Right)
    {
        return Left.Depth < Right.Depth;
    });

    const float CentreX = Extent.MaximumX - 70.0f;
    const float CentreY = Extent.MinimumY + 58.0f;
    Surface.Confine(Extent);
    for (const ProjectedAxis& Point : Projected)
    {
        if (!Point.Axis.Positive)
            continue;
        const float X[2] = { CentreX, Point.X };
        const float Y[2] = { CentreY, Point.Y };
        Surface.Polyline(X, Y, 2u, ThemeToken{ 255u, 255u, 255u, 36u }, 2.0f);
    }

    for (const ProjectedAxis& Point : Projected)
    {
        const float Radius = Point.Axis.Positive ? 9.0f : 7.0f;
        if (Point.Axis.Positive)
        {
            ThemeToken Fill = Point.Axis.Colour;
            Fill.Opacity = Point.Depth > 0.0 ? 255u : 190u;
            Surface.Medallion(Point.X, Point.Y, Radius, Fill);
            Surface.TextRun(Point.X - 3.2f, Point.Y - 5.1f,
                            ThemeToken{ 0u, 0u, 0u, 218u }, Point.Axis.Label, 10.0f, 0.0f, true);
        }
        else
        {
            Surface.Medallion(Point.X, Point.Y, Radius, ThemeToken{ 10u, 12u, 16u, 230u });
            Surface.Medallion(Point.X, Point.Y, Radius - 3.0f, Point.Axis.Colour);
        }
    }
    Surface.Release();
}

void ProjectCubeAxisPoint(const CubeBasis& Basis,
                                           const double Axis[3],
                                           float Scale,
                                           float& X,
                                           float& Y,
                                           double& Depth)
{
    const double SX = Axis[0] * Basis.Right[0] + Axis[1] * Basis.Right[1] + Axis[2] * Basis.Right[2];
    const double SY = Axis[0] * Basis.Up[0] + Axis[1] * Basis.Up[1] + Axis[2] * Basis.Up[2];
    Depth = CubeAxisDepth(Basis, Axis);
    X = static_cast<float>(SX) * Scale;
    Y = -static_cast<float>(SY) * Scale;
}

Deliver<ViewportOrientation> HitOrientationBall(const PlaneExtent& Extent,
                                                                   const CubeBasis& Basis,
                                                                   float PointerX,
                                                                   float PointerY)
{
    struct HitAxis
    {
        double Axis[3];
        ViewportOrientation Orientation;
    };

    const HitAxis Axes[6] =
    {
        { {  1.0,  0.0,  0.0 }, ViewportOrientation::Right },
        { { -1.0,  0.0,  0.0 }, ViewportOrientation::Left },
        { {  0.0,  1.0,  0.0 }, ViewportOrientation::Top },
        { {  0.0, -1.0,  0.0 }, ViewportOrientation::Bottom },
        { {  0.0,  0.0,  1.0 }, ViewportOrientation::Front },
        { {  0.0,  0.0, -1.0 }, ViewportOrientation::Back },
    };

    ViewportOrientation Best = ViewportOrientation::Isometric;
    bool Struck = false;
    float BestDistance = 13.0f * 13.0f;
    for (const HitAxis& Axis : Axes)
    {
        float X = 0.0f;
        float Y = 0.0f;
        double Depth = 0.0;
        CubeAxisPoint(Basis, Extent, Axis.Axis, X, Y, Depth);
        static_cast<void>(Depth);
        const float DX = PointerX - X;
        const float DY = PointerY - Y;
        const float Distance = DX * DX + DY * DY;
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Axis.Orientation;
            Struck = true;
        }
    }

    return Struck ? Deliver<ViewportOrientation>::Result(Best)
                  : Deliver<ViewportOrientation>::Refuse({ RefusalReason::ContentUnsupported,
                                                           "the pointer struck no face of the widget" });
}

void RecordOrientationCube(RecordingSurface& Surface,
                                        const PlaneExtent& Extent,
                                        const CubeBasis& Basis)
{
    // 📝 The last four are filled by `FillFace` below, after the six faces are declared — they are given
    //    defaults so the declaration list does not have to state eight zeroes it does not mean.
    struct FaceRecord
    {
        ViewportOrientation Orientation = ViewportOrientation::Front;
        double              Normal[3]   = {};
        ThemeToken          Colour      = {};
        const char*         Label       = "";
        float               Corners[8]  = {};
        float               CentreX     = 0.0f;
        float               CentreY     = 0.0f;
        double              Depth       = 0.0;
    };

    const float CentreX = Extent.MaximumX - 70.0f;
    const float CentreY = Extent.MinimumY + 58.0f;
    const float Scale = 28.0f;
    const double AxisX[3] = { 1.0, 0.0, 0.0 };
    const double AxisY[3] = { 0.0, 1.0, 0.0 };
    const double AxisZ[3] = { 0.0, 0.0, 1.0 };
    float VX = 0.0f, VY = 0.0f, UX = 0.0f, UY = 0.0f, WX = 0.0f, WY = 0.0f;
    double DX = 0.0, DY = 0.0, DZ = 0.0;
    ProjectCubeAxisPoint(Basis, AxisX, Scale, VX, VY, DX);
    ProjectCubeAxisPoint(Basis, AxisY, Scale, UX, UY, DY);
    ProjectCubeAxisPoint(Basis, AxisZ, Scale, WX, WY, DZ);
    const float OriginX = CentreX - (VX + UX + WX) * 0.5f;
    const float OriginY = CentreY - (VY + UY + WY) * 0.5f;

    const auto Point = [&](float XMul, float YMul, float ZMul, float& X, float& Y)
    {
        X = OriginX + VX * XMul + UX * YMul + WX * ZMul;
        Y = OriginY + VY * XMul + UY * YMul + WY * ZMul;
    };
    const auto FillFace = [&](FaceRecord& Face,
                              float A0, float A1, float A2,
                              float B0, float B1, float B2,
                              float C0, float C1, float C2,
                              float D0, float D1, float D2)
    {
        Point(A0, A1, A2, Face.Corners[0], Face.Corners[1]);
        Point(B0, B1, B2, Face.Corners[2], Face.Corners[3]);
        Point(C0, C1, C2, Face.Corners[4], Face.Corners[5]);
        Point(D0, D1, D2, Face.Corners[6], Face.Corners[7]);
        Face.CentreX = (Face.Corners[0] + Face.Corners[2] + Face.Corners[4] + Face.Corners[6]) * 0.25f;
        Face.CentreY = (Face.Corners[1] + Face.Corners[3] + Face.Corners[5] + Face.Corners[7]) * 0.25f;
        Face.Depth = CubeAxisDepth(Basis, Face.Normal);
    };

    FaceRecord Faces[6] =
    {
        { ViewportOrientation::Right,  {  1.0,  0.0,  0.0 }, ThemeToken{ 0xFCu, 0x5Au, 0x5Au, 150u }, "Right" },
        { ViewportOrientation::Left,   { -1.0,  0.0,  0.0 }, ThemeToken{ 0xFCu, 0x5Au, 0x5Au, 110u }, "Left"  },
        { ViewportOrientation::Top,    {  0.0,  1.0,  0.0 }, ThemeToken{ 0xF8u, 0xFAu, 0xFCu, 150u }, "Top"   },
        { ViewportOrientation::Bottom, {  0.0, -1.0,  0.0 }, ThemeToken{ 0xD8u, 0xDEu, 0xEAu, 105u }, "Bottom"},
        { ViewportOrientation::Front,  {  0.0,  0.0,  1.0 }, ThemeToken{ 0x5Bu, 0x8Cu, 0xFFu, 150u }, "Front" },
        { ViewportOrientation::Back,   {  0.0,  0.0, -1.0 }, ThemeToken{ 0x5Bu, 0x8Cu, 0xFFu, 110u }, "Back"  },
    };

    FillFace(Faces[0], 1,0,0, 1,1,0, 1,1,1, 1,0,1);
    FillFace(Faces[1], 0,0,0, 0,0,1, 0,1,1, 0,1,0);
    FillFace(Faces[2], 0,1,0, 0,1,1, 1,1,1, 1,1,0);
    FillFace(Faces[3], 0,0,0, 1,0,0, 1,0,1, 0,0,1);
    FillFace(Faces[4], 0,0,1, 1,0,1, 1,1,1, 0,1,1);
    FillFace(Faces[5], 0,0,0, 0,1,0, 1,1,0, 1,0,0);

    std::sort(Faces, Faces + 6u, [](const FaceRecord& Left, const FaceRecord& Right)
    {
        return Left.Depth > Right.Depth;
    });

    const auto DrawFaceLabel = [&](const FaceRecord& Face)
    {
        // CAD cube labels are vector strokes projected into the actual face parallelogram. They are not
        // screen-space TextRun overlays; every stroke endpoint is interpolated from the face's corners so
        // the word lives on the same projected 3D face in perspective/orthographic views.
        const auto FacePoint = [&](float U, float V, float& X, float& Y)
        {
            const float Ax = Face.Corners[0];
            const float Ay = Face.Corners[1];
            const float Bx = Face.Corners[2];
            const float By = Face.Corners[3];
            const float Dx = Face.Corners[6];
            const float Dy = Face.Corners[7];
            X = Ax + (Bx - Ax) * U + (Dx - Ax) * V;
            Y = Ay + (By - Ay) * U + (Dy - Ay) * V;
        };
        const auto Stroke = [&](float X0, float Y0, float X1, float Y1)
        {
            float SX0 = 0.0f, SY0 = 0.0f, SX1 = 0.0f, SY1 = 0.0f;
            FacePoint(X0, Y0, SX0, SY0);
            FacePoint(X1, Y1, SX1, SY1);
            const float Xs[2] = { SX0, SX1 };
            const float Ys[2] = { SY0, SY1 };
            Surface.Polyline(Xs, Ys, 2u, ThemeToken{ 16u, 18u, 24u, 225u }, 1.25f);
        };
        const auto Glyph = [&](char C, float X, float Y, float W, float H)
        {
            const auto L = [&](float X0, float Y0, float X1, float Y1)
            {
                Stroke(X + X0 * W, Y + Y0 * H, X + X1 * W, Y + Y1 * H);
            };
            switch (C)
            {
                case 'A': L(0,1,0.5f,0); L(1,1,0.5f,0); L(0.22f,0.55f,0.78f,0.55f); break;
                case 'B': L(0,0,0,1); L(0,0,0.72f,0); L(0.72f,0,0.9f,0.22f); L(0.9f,0.22f,0.72f,0.48f); L(0,0.48f,0.72f,0.48f); L(0.72f,0.48f,0.9f,0.74f); L(0.9f,0.74f,0.72f,1); L(0.72f,1,0,1); break;
                case 'C': L(1,0.12f,0.82f,0); L(0.82f,0,0.18f,0); L(0.18f,0,0,0.2f); L(0,0.2f,0,0.8f); L(0,0.8f,0.18f,1); L(0.18f,1,0.82f,1); L(0.82f,1,1,0.88f); break;
                case 'E': L(1,0,0,0); L(0,0,0,1); L(0,0.5f,0.78f,0.5f); L(0,1,1,1); break;
                case 'F': L(0,0,0,1); L(0,0,1,0); L(0,0.5f,0.78f,0.5f); break;
                case 'G': L(1,0.14f,0.82f,0); L(0.82f,0,0.18f,0); L(0.18f,0,0,0.2f); L(0,0.2f,0,0.8f); L(0,0.8f,0.18f,1); L(0.18f,1,0.82f,1); L(0.82f,1,1,0.82f); L(1,0.82f,1,0.58f); L(1,0.58f,0.58f,0.58f); break;
                case 'H': L(0,0,0,1); L(1,0,1,1); L(0,0.5f,1,0.5f); break;
                case 'I': L(0,0,1,0); L(0.5f,0,0.5f,1); L(0,1,1,1); break;
                case 'K': L(0,0,0,1); L(1,0,0,0.5f); L(0,0.5f,1,1); break;
                case 'L': L(0,0,0,1); L(0,1,1,1); break;
                case 'M': L(0,1,0,0); L(0,0,0.5f,0.55f); L(0.5f,0.55f,1,0); L(1,0,1,1); break;
                case 'N': L(0,1,0,0); L(0,0,1,1); L(1,1,1,0); break;
                case 'O': L(0.18f,0,0.82f,0); L(0.82f,0,1,0.18f); L(1,0.18f,1,0.82f); L(1,0.82f,0.82f,1); L(0.82f,1,0.18f,1); L(0.18f,1,0,0.82f); L(0,0.82f,0,0.18f); L(0,0.18f,0.18f,0); break;
                case 'P': L(0,1,0,0); L(0,0,0.78f,0); L(0.78f,0,1,0.24f); L(1,0.24f,0.78f,0.5f); L(0.78f,0.5f,0,0.5f); break;
                case 'R': L(0,1,0,0); L(0,0,0.78f,0); L(0.78f,0,1,0.24f); L(1,0.24f,0.78f,0.5f); L(0.78f,0.5f,0,0.5f); L(0.45f,0.5f,1,1); break;
                case 'T': L(0,0,1,0); L(0.5f,0,0.5f,1); break;
                default: break;
            }
        };
        const char* Text = Face.Label;
        std::uint32_t Count = 0u;
        while (Text[Count] != '\0') ++Count;
        const float Gap = 0.018f;
        const float LetterW = std::min(0.135f, (0.78f - Gap * static_cast<float>(Count > 0u ? Count - 1u : 0u)) / std::max(1.0f, static_cast<float>(Count)));
        const float Height = LetterW * 1.5f;
        const float Total = LetterW * static_cast<float>(Count) + Gap * static_cast<float>(Count > 0u ? Count - 1u : 0u);
        const float StartX = 0.5f - Total * 0.5f;
        const float StartY = 0.5f - Height * 0.5f;
        for (std::uint32_t Index = 0u; Index < Count; ++Index)
            Glyph(Text[Index], StartX + static_cast<float>(Index) * (LetterW + Gap), StartY, LetterW, Height);
    };

    Surface.Confine(Extent);
    for (const FaceRecord& Face : Faces)
    {
        const float T0[6] = { Face.Corners[0], Face.Corners[1], Face.Corners[2], Face.Corners[3], Face.Corners[4], Face.Corners[5] };
        const float T1[6] = { Face.Corners[0], Face.Corners[1], Face.Corners[4], Face.Corners[5], Face.Corners[6], Face.Corners[7] };
        Surface.Tongue(T0, 3u, Face.Colour);
        Surface.Tongue(T1, 3u, Face.Colour);
        const float X0[2] = { Face.Corners[0], Face.Corners[2] }; const float Y0[2] = { Face.Corners[1], Face.Corners[3] };
        const float X1[2] = { Face.Corners[2], Face.Corners[4] }; const float Y1[2] = { Face.Corners[3], Face.Corners[5] };
        const float X2[2] = { Face.Corners[4], Face.Corners[6] }; const float Y2[2] = { Face.Corners[5], Face.Corners[7] };
        const float X3[2] = { Face.Corners[6], Face.Corners[0] }; const float Y3[2] = { Face.Corners[7], Face.Corners[1] };
        Surface.Polyline(X0, Y0, 2u, ThemeToken{ 255u, 255u, 255u, 190u }, 1.2f);
        Surface.Polyline(X1, Y1, 2u, ThemeToken{ 255u, 255u, 255u, 190u }, 1.2f);
        Surface.Polyline(X2, Y2, 2u, ThemeToken{ 255u, 255u, 255u, 190u }, 1.2f);
        Surface.Polyline(X3, Y3, 2u, ThemeToken{ 255u, 255u, 255u, 190u }, 1.2f);

        if (Face.Depth <= 0.25)
            DrawFaceLabel(Face);
    }
    Surface.Release();
}

Deliver<ViewportOrientation> HitOrientationCube(const PlaneExtent& Extent,
                                                          const CubeBasis& Basis,
                                                          float PointerX,
                                                          float PointerY)
{
    const float CentreX = Extent.MaximumX - 70.0f;
    const float CentreY = Extent.MinimumY + 58.0f;
    const float Scale = 28.0f;
    const double Axis[6][3] =
    {
        {  1.0,  0.0,  0.0 }, { -1.0,  0.0,  0.0 },
        {  0.0,  1.0,  0.0 }, {  0.0, -1.0,  0.0 },
        {  0.0,  0.0,  1.0 }, {  0.0,  0.0, -1.0 }
    };
    const ViewportOrientation Orientation[6] =
    {
        ViewportOrientation::Right, ViewportOrientation::Left,
        ViewportOrientation::Top, ViewportOrientation::Bottom,
        ViewportOrientation::Front, ViewportOrientation::Back
    };

    ViewportOrientation Best = ViewportOrientation::Isometric;
    bool Struck = false;
    float BestDistance = 24.0f * 24.0f;
    for (std::uint32_t Index = 0u; Index < 6u; ++Index)
    {
        float X = 0.0f;
        float Y = 0.0f;
        double Depth = 0.0;
        ProjectCubeAxisPoint(Basis, Axis[Index], Scale * 0.58f, X, Y, Depth);
        X += CentreX;
        Y += CentreY;
        const float DX = PointerX - X;
        const float DY = PointerY - Y;
        const float Distance = DX * DX + DY * DY;
        if (Distance < BestDistance)
        {
            BestDistance = Distance;
            Best = Orientation[Index];
            Struck = true;
        }
    }

    return Struck ? Deliver<ViewportOrientation>::Result(Best)
                  : Deliver<ViewportOrientation>::Refuse({ RefusalReason::ContentUnsupported,
                                                           "the pointer struck no face of the widget" });
}

void RecordOrientationWidget(RecordingSurface& Surface,
                                      const PlaneExtent& Extent,
                                      const CubeBasis& Basis,
                                      bool CadMode)
{
    if (CadMode)
        RecordOrientationCube(Surface, Extent, Basis);
    else
        RecordOrientationBall(Surface, Extent, Basis);
}

Deliver<ViewportOrientation> HitOrientationWidget(const PlaneExtent& Extent,
                                                        const CubeBasis& Basis,
                                                        float PointerX,
                                                        float PointerY,
                                                        bool CadMode)
{
    return CadMode ? HitOrientationCube(Extent, Basis, PointerX, PointerY)
                   : HitOrientationBall(Extent, Basis, PointerX, PointerY);
}

}   // namespace Slate
