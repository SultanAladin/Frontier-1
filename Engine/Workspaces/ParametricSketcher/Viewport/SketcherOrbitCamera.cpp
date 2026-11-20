/*====================================================================================================================================
                                                           ORBITCAMERA.CPP
====================================================================================================================================*/
// 🧩 Shared Z-up orbit camera (see header)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherOrbitCamera.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace Frontier
{

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

using Triple = std::array<double, 3>;
constexpr double HalfPi = 0.5 * std::numbers::pi;

[[nodiscard]] double Dot(const Triple& Left, const Triple& Right) { return Left[0] * Right[0] + Left[1] * Right[1] + Left[2] * Right[2]; }

[[nodiscard]] CameraMatrix Multiply(const CameraMatrix& Left, const CameraMatrix& Right)
{
    CameraMatrix Product{};
    for (int Column = 0; Column < 4; ++Column)
        for (int Row = 0; Row < 4; ++Row)
        {
            double Sum = 0.0;
            for (int Inner = 0; Inner < 4; ++Inner) Sum += Left[Inner * 4 + Row] * Right[Column * 4 + Inner];
            Product[Column * 4 + Row] = Sum;
        }
    return Product;
}

[[nodiscard]] std::array<double, 4> Transform(const CameraMatrix& Matrix, const std::array<double, 4>& Point)
{
    std::array<double, 4> Result{};
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 4; ++Column) Result[Row] += Matrix[Column * 4 + Row] * Point[Column];
    return Result;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

CameraMatrix InvertCameraMatrix(const CameraMatrix& Source)
{
    // 📝 Gauss–Jordan with partial pivoting on an augmented [M | I] (row-major working copy).
    double Work[4][8];
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 4; ++Column)
        {
            Work[Row][Column]     = Source[Column * 4 + Row];
            Work[Row][Column + 4] = Row == Column ? 1.0 : 0.0;
        }
    for (int Pivot = 0; Pivot < 4; ++Pivot)
    {
        int Best = Pivot;
        for (int Row = Pivot + 1; Row < 4; ++Row) if (std::abs(Work[Row][Pivot]) > std::abs(Work[Best][Pivot])) Best = Row;
        if (Best != Pivot) for (int Column = 0; Column < 8; ++Column) std::swap(Work[Pivot][Column], Work[Best][Column]);
        const double Scale = Work[Pivot][Pivot];
        if (std::abs(Scale) < 1.0e-300) return CameraMatrix{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        for (int Column = 0; Column < 8; ++Column) Work[Pivot][Column] /= Scale;
        for (int Row = 0; Row < 4; ++Row)
        {
            if (Row == Pivot) continue;
            const double Factor = Work[Row][Pivot];
            for (int Column = 0; Column < 8; ++Column) Work[Row][Column] -= Factor * Work[Pivot][Column];
        }
    }
    CameraMatrix Inverse{};
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 4; ++Column) Inverse[Column * 4 + Row] = Work[Row][Column + 4];
    return Inverse;
}

OrbitCameraBasis FrameOrbitCamera(const OrbitCamera& Camera, double Width, double Height)
{
    OrbitCameraBasis Frame;
    Frame.Width  = std::max(1.0, Width);
    Frame.Height = std::max(1.0, Height);
    Frame.OrthographicEnabled = Camera.OrthographicEnabled;
    const double Aspect = Frame.Width / Frame.Height;
    const Triple Toward = { std::cos(Camera.Elevation) * std::cos(Camera.Azimuth), std::cos(Camera.Elevation) * std::sin(Camera.Azimuth), std::sin(Camera.Elevation) };
    Frame.Eye     = { Camera.Target[0] + Camera.Distance * Toward[0], Camera.Target[1] + Camera.Distance * Toward[1], Camera.Target[2] + Camera.Distance * Toward[2] };
    Frame.Forward = { -Toward[0], -Toward[1], -Toward[2] };
    // 📝 Right depends on the yaw only, so Top / Bottom (pitch ±π/2) need no special case.
    Frame.Right   = { -std::sin(Camera.Azimuth), std::cos(Camera.Azimuth), 0.0 };
    Frame.Up      = { Frame.Right[1] * Frame.Forward[2] - Frame.Right[2] * Frame.Forward[1],
                      Frame.Right[2] * Frame.Forward[0] - Frame.Right[0] * Frame.Forward[2],
                      Frame.Right[0] * Frame.Forward[1] - Frame.Right[1] * Frame.Forward[0] };

    // ① view: world → camera (x right, y up, looking down −z)
    CameraMatrix View{};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        View[Axis * 4 + 0] = Frame.Right[Axis];
        View[Axis * 4 + 1] = Frame.Up[Axis];
        View[Axis * 4 + 2] = -Frame.Forward[Axis];
    }
    View[12] = -Dot(Frame.Right, Frame.Eye);
    View[13] = -Dot(Frame.Up, Frame.Eye);
    View[14] = Dot(Frame.Forward, Frame.Eye);
    View[15] = 1.0;

    // ② projection: Vulkan clip (y down, depth 0 near → 1 far); the far plane leaves room for the grid to fade out
    const double Radius   = std::max(Camera.SceneRadius, 1.0e-3);
    const double Far      = Camera.Distance * 12.0 + Radius * 4.0;
    const double HalfView = Camera.Distance * std::tan(0.5 * Camera.FieldOfView);           // [len] - Half height at the target
    CameraMatrix Projection{};
    if (Camera.OrthographicEnabled)
    {
        const double Near = -(Camera.Distance + Radius * 4.0);
        Projection[0]  = 1.0 / (HalfView * Aspect);
        Projection[5]  = -1.0 / HalfView;
        Projection[10] = -1.0 / (Far - Near);
        Projection[14] = -Near / (Far - Near);
        Projection[15] = 1.0;
    }
    else
    {
        const double Near  = std::max(Camera.Distance * 0.01, 1.0e-4);
        const double Focal = 1.0 / std::tan(0.5 * Camera.FieldOfView);
        Projection[0]  = Focal / Aspect;
        Projection[5]  = -Focal;
        Projection[10] = Far / (Near - Far);
        Projection[11] = -1.0;
        Projection[14] = Near * Far / (Near - Far);
    }
    Frame.ClipFromWorld = Multiply(Projection, View);
    Frame.WorldFromClip = InvertCameraMatrix(Frame.ClipFromWorld);
    Frame.WorldPerPixel = 2.0 * HalfView / Frame.Height;
    return Frame;
}

OrbitRay CastOrbitRay(const OrbitCameraBasis& Frame, double PixelX, double PixelY)
{
    const double DeviceX = 2.0 * PixelX / Frame.Width - 1.0;
    const double DeviceY = 2.0 * PixelY / Frame.Height - 1.0;
    const std::array<double, 4> Near = Transform(Frame.WorldFromClip, { DeviceX, DeviceY, 0.0, 1.0 });
    const std::array<double, 4> Far  = Transform(Frame.WorldFromClip, { DeviceX, DeviceY, 1.0, 1.0 });
    const Triple From = { Near[0] / Near[3], Near[1] / Near[3], Near[2] / Near[3] };
    const Triple To   = { Far[0] / Far[3], Far[1] / Far[3], Far[2] / Far[3] };
    Triple Direction  = { To[0] - From[0], To[1] - From[1], To[2] - From[2] };
    const double Length = std::sqrt(Dot(Direction, Direction));
    for (double& Component : Direction) Component /= Length;
    return { From, Direction };
}

void RevolveOrbitCamera(OrbitCamera& Camera, double DeltaX, double DeltaY)
{
    Camera.Azimuth  -= DeltaX * 0.008;
    Camera.Elevation = std::clamp(Camera.Elevation + DeltaY * 0.008, -HalfPi, HalfPi);
}

void PanOrbitCamera(OrbitCamera& Camera, const OrbitCameraBasis& Frame, double DeltaX, double DeltaY)
{
    for (int Axis = 0; Axis < 3; ++Axis)
        Camera.Target[Axis] += (-DeltaX * Frame.Right[Axis] + DeltaY * Frame.Up[Axis]) * Frame.WorldPerPixel;
}

void DollyOrbitCamera(OrbitCamera& Camera, double WheelSteps)
{
    Camera.Distance = std::clamp(Camera.Distance * std::exp(-0.12 * WheelSteps), 1.0e-3, 1.0e5);
}

void PresentOrbitView(OrbitCamera& Camera, OrbitViewPreset Preset)
{
    switch (Preset)
    {
    case OrbitViewPreset::Isometric: Camera.Azimuth = -0.25 * std::numbers::pi; Camera.Elevation = std::atan(1.0 / std::sqrt(2.0)); break;
    case OrbitViewPreset::Top:       Camera.Azimuth = -HalfPi;                  Camera.Elevation = HalfPi;                          break;
    case OrbitViewPreset::Bottom:    Camera.Azimuth = -HalfPi;                  Camera.Elevation = -HalfPi;                         break;
    case OrbitViewPreset::Front:     Camera.Azimuth = -HalfPi;                  Camera.Elevation = 0.0;                             break;
    case OrbitViewPreset::Back:      Camera.Azimuth = HalfPi;                   Camera.Elevation = 0.0;                             break;
    case OrbitViewPreset::Right:     Camera.Azimuth = 0.0;                      Camera.Elevation = 0.0;                             break;
    case OrbitViewPreset::Left:      Camera.Azimuth = std::numbers::pi;         Camera.Elevation = 0.0;                             break;
    }
}

void FitOrbitCamera(OrbitCamera& Camera, const std::array<double, 3>& Low, const std::array<double, 3>& High)
{
    Triple Span{};
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        Camera.Target[Axis] = 0.5 * (Low[Axis] + High[Axis]);
        Span[Axis]          = High[Axis] - Low[Axis];
    }
    Camera.SceneRadius = std::max(0.5 * std::sqrt(Dot(Span, Span)), 1.0e-3);
    Camera.Distance    = Camera.SceneRadius / std::sin(0.5 * Camera.FieldOfView) * 1.08;
}

const char* OrbitViewLabel(OrbitViewPreset Preset) noexcept
{
    switch (Preset)
    {
    case OrbitViewPreset::Isometric: return "Isometric";
    case OrbitViewPreset::Top:       return "Top";
    case OrbitViewPreset::Bottom:    return "Bottom";
    case OrbitViewPreset::Front:     return "Front";
    case OrbitViewPreset::Back:      return "Back";
    case OrbitViewPreset::Right:     return "Right";
    case OrbitViewPreset::Left:      return "Left";
    }
    return "View";
}

} // namespace Frontier
