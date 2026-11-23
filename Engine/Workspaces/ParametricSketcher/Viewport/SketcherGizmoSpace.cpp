/*==========================================================================================================================================
                                                               GIZMOSPACE.CPP
==========================================================================================================================================*/
// 🧩 The gizmo's handle triangles (Slate GizmoPRO look), their pick and the drag maths.

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Viewport/SketcherGizmoSpace.h"
#include <numbers>

#include <algorithm>
#include <array>
#include <cmath>

namespace Frontier
{

using GeometrySpace::Parametric::Cross;
using GeometrySpace::Parametric::Dot;
using GeometrySpace::Parametric::Length;
using GeometrySpace::Parametric::Normalize;
using GeometrySpace::Parametric::Vector3;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Slate References/Gizmo.html (L = 1): colours, and the handle geometry in units of L.
constexpr uint32_t AxisColours[3]  = { 0xe01414u, 0x12d40au, 0x1560e0u };                    // [-]   - X red, Y green, Z blue (0xRRGGBB)
constexpr uint32_t PlaneColours[3] = { 0x1fc7c7u, 0xc81ec8u, 0xe0cd12u };                    // [-]   - YZ cyan, XZ magenta, XY yellow
constexpr uint32_t RingColour      = 0xffffffu;                                              // [-]   - Centre ring
constexpr uint32_t GlowColour      = 0x555555u;                                              // [-]   - Hover emissive

constexpr double Tip         = 0.95;                                                         // [L]   - Cone centre along its axis
constexpr double ConeRadius  = 0.06;                                                         // [L]   - Cone base radius
constexpr double ConeHeight  = 0.18;                                                         // [L]   - Cone height (centred on Tip)
constexpr double ScaleRadius = 0.06;                                                         // [L]   - Scale cylinder radius
constexpr double ScaleHeight = 0.14;                                                         // [L]   - Scale cylinder length
constexpr double ScaleCentre = Tip - 0.28;                                                   // [L]   - Scale cylinder centre
constexpr double PlaneHalf   = 0.08;                                                         // [L]   - Plane quad half side
constexpr double PlaneCentre = Tip - PlaneHalf;                                              // [L]   - Quad centre along u and v
constexpr double ArcRadius   = 0.62 * Tip;                                                   // [L]   - Rotate arc centre line
constexpr double ArcBand     = 0.038;                                                        // [L]   - Arc half width
constexpr double DegreesToRadians(double Deg) noexcept { return Deg * (std::numbers::pi / 180.0); }
constexpr double ArcSweep    = DegreesToRadians(31.0);                                     // [rad] - Arc span
constexpr double RingRadius  = 0.16;                                                         // [L]   - Centre ring radius
constexpr double RingTube    = 0.008;                                                        // [L]   - Ring tube radius
constexpr double StrokePixels = 1.6;                                                         // [px]  - Quad edge stroke width
constexpr int    RoundSegments = 24;                                                         // [-]   - Cone / cylinder facets
constexpr int    ArcSegments   = 24;                                                         // [-]   - Arc pieces
constexpr int    RingSegments  = 64;                                                         // [-]   - Ring pieces

constexpr uint8_t QuadAlpha      = 71;                                                       // [-] - Opacity 0.28
constexpr uint8_t QuadHoverAlpha = 140;                                                      // [-] - Opacity 0.55 on hover

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 FromArray(const std::array<double, 3>& Values) { return { Values[0], Values[1], Values[2] }; }

// 0xRRGGBB (+ glow, clamped per channel) and alpha → RGBA8 with R in the low byte.
[[nodiscard]] uint32_t PackColour(uint32_t Rgb, bool GlowCondition, uint8_t Alpha = 255)
{
    uint32_t Channels[3] = { (Rgb >> 16) & 0xffu, (Rgb >> 8) & 0xffu, Rgb & 0xffu };
    if (GlowCondition)
        for (int Channel = 0; Channel < 3; ++Channel) Channels[Channel] = std::min(255u, Channels[Channel] + ((GlowColour >> (16 - 8 * Channel)) & 0xffu));
    return Channels[0] | (Channels[1] << 8) | (Channels[2] << 16) | (uint32_t(Alpha) << 24);
}

// Unit vector perpendicular to Direction (any).
[[nodiscard]] Vector3 Perpendicular(Vector3 Direction)
{
    const Vector3 ReferenceAxis = std::abs(Direction.X) < 0.9 ? Vector3{ 1.0, 0.0, 0.0 } : Vector3{ 0.0, 1.0, 0.0 };
    return Normalize(Cross(Direction, ReferenceAxis));
}

class GizmoBuilder
{
public:
    GizmoBuilder(const GizmoPose& Pose, std::vector<GizmoVertex>& Vertices) : Pose(Pose), Vertices(Vertices) {}

    // Gizmo units (L, along Pose.Axes) → world.
    [[nodiscard]] Vector3 Place(Vector3 Local) const
    {
        return Pose.Origin + (Pose.Axes[0] * Local.X + Pose.Axes[1] * Local.Y + Pose.Axes[2] * Local.Z) * Pose.Size;
    }
    [[nodiscard]] Vector3 Turn(Vector3 Local) const { return Pose.Axes[0] * Local.X + Pose.Axes[1] * Local.Y + Pose.Axes[2] * Local.Z; }

    void Triangle(Vector3 First, Vector3 Second, Vector3 Third, Vector3 Normal, uint32_t Colour, GizmoHandle Handle)
    {
        for (const Vector3& Corner : { First, Second, Third })
        {
            GizmoVertex Vertex;
            Vertex.Position[0] = float(Corner.X); Vertex.Position[1] = float(Corner.Y); Vertex.Position[2] = float(Corner.Z);
            Vertex.Normal[0]   = float(Normal.X); Vertex.Normal[1]   = float(Normal.Y); Vertex.Normal[2]   = float(Normal.Z);
            Vertex.Colour      = Colour;
            Vertex.Handle      = uint32_t(Handle);
            Vertices.push_back(Vertex);
        }
    }

    // A round solid along Axis (local unit vector): radius tapering from BaseRadius at Start to TopRadius at End.
    void Round(Vector3 Axis, double Start, double End, double BaseRadius, double TopRadius, uint32_t Colour, GizmoHandle Handle)
    {
        const Vector3 First  = Perpendicular(Axis);
        const Vector3 Second = Cross(Axis, First);
        const Vector3 Bottom = Place(Axis * Start), Top = Place(Axis * End);
        const Vector3 Up     = Turn(Axis);
        const double  Slope  = (BaseRadius - TopRadius) / (End - Start);                     // [-] - Side normal tilt
        for (int Index = 0; Index < RoundSegments; ++Index)
        {
            const double  Angle[2] = { 2.0 * std::numbers::pi * Index / RoundSegments, 2.0 * std::numbers::pi * (Index + 1) / RoundSegments };
            Vector3       Spoke[2], Low[2], High[2];
            for (int Side = 0; Side < 2; ++Side)
            {
                Spoke[Side] = First * std::cos(Angle[Side]) + Second * std::sin(Angle[Side]);
                Low[Side]   = Place(Axis * Start + Spoke[Side] * BaseRadius);
                High[Side]  = Place(Axis * End + Spoke[Side] * TopRadius);
            }
            const Vector3 Middle = Normalize(Turn(Spoke[0] + Spoke[1]) + Up * Slope);
            Triangle(Low[0], Low[1], High[1], Middle, Colour, Handle);
            if (TopRadius > 0.0) Triangle(Low[0], High[1], High[0], Middle, Colour, Handle);
            else                 Triangle(Low[0], High[1], Top, Middle, Colour, Handle);
            Triangle(Bottom, Low[1], Low[0], Up * -1.0, Colour, Handle);
            if (TopRadius > 0.0) Triangle(Top, High[0], High[1], Up, Colour, Handle);
        }
    }

    // Flat band (u, v local): radii Inner … Outer over angles Low … High, unlit.
    void Band(Vector3 U, Vector3 V, double Inner, double Outer, double Low, double High, int Segments, uint32_t Colour, GizmoHandle Handle)
    {
        for (int Index = 0; Index < Segments; ++Index)
        {
            const double First = Low + (High - Low) * Index / Segments, Second = Low + (High - Low) * (Index + 1) / Segments;
            const Vector3 InnerFirst  = Place((U * std::cos(First) + V * std::sin(First)) * Inner);
            const Vector3 OuterFirst  = Place((U * std::cos(First) + V * std::sin(First)) * Outer);
            const Vector3 InnerSecond = Place((U * std::cos(Second) + V * std::sin(Second)) * Inner);
            const Vector3 OuterSecond = Place((U * std::cos(Second) + V * std::sin(Second)) * Outer);
            Triangle(InnerFirst, OuterFirst, OuterSecond, {}, Colour, Handle);
            Triangle(InnerFirst, OuterSecond, InnerSecond, {}, Colour, Handle);
        }
    }

    // World stroke From → To of width Width (world), facing the eye (Toward = unit eye direction).
    void Stroke(Vector3 From, Vector3 To, double Width, Vector3 Toward, uint32_t Colour, GizmoHandle Handle)
    {
        Vector3 Side = Cross(To - From, Toward);
        if (Length(Side) < 1.0e-12) return;
        Side = Normalize(Side) * (0.5 * Width);
        Triangle(From - Side, From + Side, To + Side, {}, Colour, Handle);
        Triangle(From - Side, To + Side, To - Side, {}, Colour, Handle);
    }

    const GizmoPose&           Pose;
    std::vector<GizmoVertex>&  Vertices;
};

[[nodiscard]] bool HiddenCondition(const GizmoPose& Pose, GizmoHandle Handle)
{
    return (Pose.Hidden >> uint32_t(Handle)) & 1u;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

OrbitCameraBasis ResolveGizmoCamera(const std::array<double, 3>& Eye, const std::array<double, 3>& Forward, const std::array<double, 3>& Up,
                                   double VerticalFieldOfView, double Width, double Height)
{
    // 📝 Vulkan clip space as the Sketcher camera writes it: x right, y down, depth in [0, 1] between Near and Far.
    constexpr double Near = 0.05, Far = 1.0e5;                                               // [len]
    const Vector3 Ahead   = Normalize(FromArray(Forward));
    const Vector3 Across  = Normalize(Cross(Ahead, FromArray(Up)));
    const Vector3 Upward  = Cross(Across, Ahead);
    const Vector3 Station = FromArray(Eye);
    const double  Tangent = std::tan(0.5 * VerticalFieldOfView);
    const double  Aspect  = Height > 0.0 ? Width / Height : 1.0;
    const double  Depth   = Far / (Far - Near);
    const double  Rows[4][4] = {
        { Across.X / (Tangent * Aspect), Across.Y / (Tangent * Aspect), Across.Z / (Tangent * Aspect), -Dot(Across, Station) / (Tangent * Aspect) },
        { -Upward.X / Tangent, -Upward.Y / Tangent, -Upward.Z / Tangent, Dot(Upward, Station) / Tangent },
        { Ahead.X * Depth, Ahead.Y * Depth, Ahead.Z * Depth, -Dot(Ahead, Station) * Depth - Near * Depth },
        { Ahead.X, Ahead.Y, Ahead.Z, -Dot(Ahead, Station) },
    };
    OrbitCameraBasis Camera;
    for (int Row = 0; Row < 4; ++Row)
        for (int Column = 0; Column < 4; ++Column) Camera.ClipFromWorld[Column * 4 + Row] = Rows[Row][Column];
    Camera.Eye           = Eye;
    Camera.Right         = { Across.X, Across.Y, Across.Z };
    Camera.Up            = { Upward.X, Upward.Y, Upward.Z };
    Camera.Forward       = { Ahead.X, Ahead.Y, Ahead.Z };
    Camera.WorldPerPixel = 2.0 * Tangent / std::max(1.0, Height);                            // [len] - At unit depth
    Camera.Width         = Width;
    Camera.Height        = Height;
    return Camera;
}

double MeasureGizmoSize(const OrbitCameraBasis& Frame, Vector3 Origin, double Pixels)
{
    // Project Origin and Origin + Right: their pixel distance is the screen scale at Origin's depth.
    const auto Project = [&](Vector3 Point, double& X, double& Y)
    {
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Point.X + Frame.ClipFromWorld[1 * 4 + Row] * Point.Y + Frame.ClipFromWorld[2 * 4 + Row] * Point.Z + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 1.0e-12) return false;
        X = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
        Y = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
        return true;
    };
    double FirstX = 0.0, FirstY = 0.0, SecondX = 0.0, SecondY = 0.0;
    if (!Project(Origin, FirstX, FirstY) || !Project(Origin + FromArray(Frame.Right), SecondX, SecondY)) return Pixels * Frame.WorldPerPixel;
    const double PerUnit = std::hypot(SecondX - FirstX, SecondY - FirstY);                   // [px/len]
    return PerUnit > 1.0e-9 ? Pixels / PerUnit : Pixels * Frame.WorldPerPixel;
}

GizmoGeometry BuildGizmoGeometry(const GizmoPose& Pose, const OrbitCameraBasis& Frame, GizmoHandle Hover, GizmoHandle Grabbed)
{
    GizmoGeometry Geometry;
    if (Pose.Modes == 0) return Geometry;
    GizmoBuilder  Builder(Pose, Geometry.Vertices);
    const Vector3 Units[3] = { { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } };
    const Vector3 Toward   = FromArray(Frame.Forward) * -1.0;                                 // [-] - Towards the eye
    const auto Glow = [&](GizmoHandle Handle) { return Handle == Hover || Handle == Grabbed; };
    const auto Shown = [&](GizmoHandle Handle) { return !HiddenCondition(Pose, Handle); };

    // ① opaque: cones, scale cylinders, rotate arcs, quad edges, centre ring
    for (int Axis = 0; Axis < 3; ++Axis)
    {
        const Vector3 OtherU = Axis == 0 ? Units[1] : Units[0];                              // [-] - Slate OTHERS[axis].u
        const Vector3 OtherV = Axis == 2 ? Units[1] : Units[2];                              // [-] - Slate OTHERS[axis].v
        const auto Translate = GizmoHandle(uint8_t(GizmoHandle::TranslateX) + Axis);
        const auto Rotate    = GizmoHandle(uint8_t(GizmoHandle::RotateX) + Axis);
        const auto Scale     = GizmoHandle(uint8_t(GizmoHandle::ScaleX) + Axis);
        const auto Plane     = GizmoHandle(uint8_t(GizmoHandle::PlaneYZ) + Axis);
        if ((Pose.Modes & GizmoTranslate) && Shown(Translate))
            Builder.Round(Units[Axis], Tip - 0.5 * ConeHeight, Tip + 0.5 * ConeHeight, ConeRadius, 0.0, PackColour(AxisColours[Axis], Glow(Translate)), Translate);
        if ((Pose.Modes & GizmoScale) && Shown(Scale))
            Builder.Round(Units[Axis], ScaleCentre - 0.5 * ScaleHeight, ScaleCentre + 0.5 * ScaleHeight, ScaleRadius, ScaleRadius, PackColour(AxisColours[Axis], Glow(Scale)), Scale);
        if ((Pose.Modes & GizmoRotate) && Shown(Rotate))
        {
            const double Centre = 0.25 * std::numbers::pi;                                   // [rad] - Bisector of u and v
            Builder.Band(OtherU, OtherV, ArcRadius - ArcBand, ArcRadius + ArcBand, Centre - 0.5 * ArcSweep, Centre + 0.5 * ArcSweep, ArcSegments,
                         PackColour(AxisColours[Axis], Glow(Rotate)), Rotate);
        }
        if ((Pose.Modes & GizmoTranslate) && Shown(Plane))
        {
            const double  Width = StrokePixels / GizmoPixels * Pose.Size;                    // [len]
            const Vector3 Outer = Builder.Place((OtherU + OtherV) * (PlaneCentre + PlaneHalf));
            Builder.Stroke(Builder.Place(OtherU * (PlaneCentre - PlaneHalf) + OtherV * (PlaneCentre + PlaneHalf)), Outer, Width, Toward, PackColour(PlaneColours[Axis], Glow(Plane)), Plane);
            Builder.Stroke(Builder.Place(OtherV * (PlaneCentre - PlaneHalf) + OtherU * (PlaneCentre + PlaneHalf)), Outer, Width, Toward, PackColour(PlaneColours[Axis], Glow(Plane)), Plane);
        }
    }
    // Centre ring: a band facing the eye (Slate billboards a thin torus).
    if (Shown(GizmoHandle::Centre))
    {
        const Vector3 Right = FromArray(Frame.Right), Up = FromArray(Frame.Up);
        const Vector3 LocalRight{ Dot(Right, Pose.Axes[0]), Dot(Right, Pose.Axes[1]), Dot(Right, Pose.Axes[2]) };
        const Vector3 LocalUp{ Dot(Up, Pose.Axes[0]), Dot(Up, Pose.Axes[1]), Dot(Up, Pose.Axes[2]) };
        const double  Tube = std::max(RingTube, 0.75 * StrokePixels / GizmoPixels);          // [L] - At least a visible stroke
        Builder.Band(LocalRight, LocalUp, RingRadius - Tube, RingRadius + Tube, 0.0, 2.0 * std::numbers::pi, RingSegments,
                     PackColour(RingColour, Glow(GizmoHandle::Centre)), GizmoHandle::Centre);
    }
    Geometry.OpaqueCount = static_cast<uint32_t>(Geometry.Vertices.size());

    // ② translucent plane quads (drawn last, blended)
    if (Pose.Modes & GizmoTranslate)
    {
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            const auto Plane = GizmoHandle(uint8_t(GizmoHandle::PlaneYZ) + Axis);
            if (!Shown(Plane)) continue;
            const Vector3 OtherU = Axis == 0 ? Units[1] : Units[0];
            const Vector3 OtherV = Axis == 2 ? Units[1] : Units[2];
            const Vector3 Centre = (OtherU + OtherV) * PlaneCentre;
            const Vector3 Corners[4] = { Builder.Place(Centre - OtherU * PlaneHalf - OtherV * PlaneHalf), Builder.Place(Centre + OtherU * PlaneHalf - OtherV * PlaneHalf),
                                         Builder.Place(Centre + OtherU * PlaneHalf + OtherV * PlaneHalf), Builder.Place(Centre - OtherU * PlaneHalf + OtherV * PlaneHalf) };
            const uint32_t Colour = PackColour(PlaneColours[Axis], false, Glow(Plane) ? QuadHoverAlpha : QuadAlpha);
            Builder.Triangle(Corners[0], Corners[1], Corners[2], {}, Colour, Plane);
            Builder.Triangle(Corners[0], Corners[2], Corners[3], {}, Colour, Plane);
        }
    }
    return Geometry;
}

GizmoHandle PickGizmoHandle(const GizmoGeometry& Geometry, const OrbitCameraBasis& Frame, double PixelX, double PixelY, double Reach)
{
    struct Projected { double X = 0.0, Y = 0.0, Depth = 0.0; bool Valid = false; };
    const auto Project = [&](const GizmoVertex& Vertex)
    {
        Projected Point;
        double Clip[4] = {};
        for (int Row = 0; Row < 4; ++Row)
            Clip[Row] = Frame.ClipFromWorld[0 * 4 + Row] * Vertex.Position[0] + Frame.ClipFromWorld[1 * 4 + Row] * Vertex.Position[1]
                      + Frame.ClipFromWorld[2 * 4 + Row] * Vertex.Position[2] + Frame.ClipFromWorld[3 * 4 + Row];
        if (Clip[3] <= 1.0e-12) return Point;
        Point.X = (Clip[0] / Clip[3] * 0.5 + 0.5) * Frame.Width;
        Point.Y = (Clip[1] / Clip[3] * 0.5 + 0.5) * Frame.Height;
        Point.Depth = Clip[2] / Clip[3];
        Point.Valid = true;
        return Point;
    };
    // Segment distance in pixels.
    const auto Gap = [&](const Projected& From, const Projected& To)
    {
        const double AlongX = To.X - From.X, AlongY = To.Y - From.Y, Square = AlongX * AlongX + AlongY * AlongY;
        const double Share  = Square > 0.0 ? std::clamp(((PixelX - From.X) * AlongX + (PixelY - From.Y) * AlongY) / Square, 0.0, 1.0) : 0.0;
        return std::hypot(From.X + AlongX * Share - PixelX, From.Y + AlongY * Share - PixelY);
    };
    GizmoHandle Best            = GizmoHandle::None;
    double              BestGap         = Reach;
    bool                BestTranslucent = true;
    double              BestDepth       = 1e300;
    for (size_t Index = 0; Index + 2 < Geometry.Vertices.size(); Index += 3)
    {
        const Projected Corner[3] = { Project(Geometry.Vertices[Index]), Project(Geometry.Vertices[Index + 1]), Project(Geometry.Vertices[Index + 2]) };
        if (!Corner[0].Valid || !Corner[1].Valid || !Corner[2].Valid) continue;
        // Barycentric inside test (either winding)
        const double Area = (Corner[1].X - Corner[0].X) * (Corner[2].Y - Corner[0].Y) - (Corner[2].X - Corner[0].X) * (Corner[1].Y - Corner[0].Y);
        double Distance = 0.0, Depth = (Corner[0].Depth + Corner[1].Depth + Corner[2].Depth) / 3.0;
        if (std::abs(Area) > 1.0e-12)
        {
            const double First  = ((Corner[1].X - PixelX) * (Corner[2].Y - PixelY) - (Corner[2].X - PixelX) * (Corner[1].Y - PixelY)) / Area;
            const double Second = ((Corner[2].X - PixelX) * (Corner[0].Y - PixelY) - (Corner[0].X - PixelX) * (Corner[2].Y - PixelY)) / Area;
            const double Third  = 1.0 - First - Second;
            if (First >= 0.0 && Second >= 0.0 && Third >= 0.0) Depth = First * Corner[0].Depth + Second * Corner[1].Depth + Third * Corner[2].Depth;
            else Distance = std::min({ Gap(Corner[0], Corner[1]), Gap(Corner[1], Corner[2]), Gap(Corner[2], Corner[0]) });
        }
        else Distance = std::min({ Gap(Corner[0], Corner[1]), Gap(Corner[1], Corner[2]), Gap(Corner[2], Corner[0]) });
        if (Distance > BestGap) continue;
        // 📝 Ranking: inside beats near (smaller pixel gap first); then an opaque handle beats a see-through plane quad
        //    (the cone seen through a quad stays grabbable); then the nearest depth.
        const bool Translucent = Index >= Geometry.OpaqueCount;
        const bool Better = Distance < BestGap || (Distance == BestGap && ((!Translucent && BestTranslucent) || (Translucent == BestTranslucent && Depth < BestDepth)));
        if (Better || Best == GizmoHandle::None)
        {
            BestGap         = Distance;
            BestTranslucent = Translucent;
            BestDepth       = Depth;
            Best            = GizmoHandle(Geometry.Vertices[Index].Handle);
        }
    }
    return Best;
}

int GizmoHandleAxis(GizmoHandle Handle) noexcept
{
    const int Code = int(Handle);
    if (Code >= 1 && Code <= 12) return (Code - 1) % 3;
    return -1;
}

const char* GizmoHandleLabel(GizmoHandle Handle) noexcept
{
    switch (Handle)
    {
    case GizmoHandle::None:       return "none";
    case GizmoHandle::TranslateX: return "X move";
    case GizmoHandle::TranslateY: return "Y move";
    case GizmoHandle::TranslateZ: return "Z move";
    case GizmoHandle::PlaneYZ:    return "X-plane move";
    case GizmoHandle::PlaneXZ:    return "Y-plane move";
    case GizmoHandle::PlaneXY:    return "Z-plane move";
    case GizmoHandle::RotateX:    return "X rotate";
    case GizmoHandle::RotateY:    return "Y rotate";
    case GizmoHandle::RotateZ:    return "Z rotate";
    case GizmoHandle::ScaleX:     return "X scale";
    case GizmoHandle::ScaleY:     return "Y scale";
    case GizmoHandle::ScaleZ:     return "Z scale";
    case GizmoHandle::Centre:     return "centre";
    }
    return "?";
}

std::optional<double> MeasureAxisParameter(const OrbitRay& Ray, Vector3 Origin, Vector3 Axis)
{
    // Closest points of the skew lines R(s) = O + s·d and A(t) = P + t·a: solve the 2 × 2 normal equations for t.
    const Vector3 RayOrigin = FromArray(Ray.Origin), Direction = FromArray(Ray.Direction);
    const Vector3 Unit      = Normalize(Axis);
    const Vector3 Between   = Origin - RayOrigin;
    const double  Facing    = Dot(Direction, Unit);
    const double  Determinant = 1.0 - Facing * Facing;
    if (Determinant < 1.0e-9) return std::nullopt;                                           // Ray along the axis
    const double Along = (Facing * Dot(Between, Direction) - Dot(Between, Unit)) / Determinant;
    return Along;
}

std::optional<Vector3> IntersectGizmoPlane(const OrbitRay& Ray, Vector3 Origin, Vector3 Normal)
{
    const Vector3 RayOrigin = FromArray(Ray.Origin), Direction = FromArray(Ray.Direction);
    const double  Facing    = Dot(Direction, Normal);
    if (std::abs(Facing) < 1.0e-9 * std::max(1.0, Length(Normal))) return std::nullopt;
    const double Distance = Dot(Origin - RayOrigin, Normal) / Facing;
    if (!(Distance > 0.0)) return std::nullopt;
    return RayOrigin + Direction * Distance;
}

std::optional<double> MeasureGizmoAngle(const OrbitRay& Ray, Vector3 Origin, Vector3 Axis, Vector3 Reference)
{
    const Vector3 Unit = Normalize(Axis);
    const std::optional<Vector3> Hit = IntersectGizmoPlane(Ray, Origin, Unit);
    if (!Hit) return std::nullopt;
    const Vector3 U = Normalize(Reference - Unit * Dot(Reference, Unit));
    const Vector3 V = Cross(Unit, U);
    const Vector3 Offset = *Hit - Origin;
    if (Length(Offset) < 1.0e-12) return std::nullopt;
    return std::atan2(Dot(Offset, V), Dot(Offset, U));
}

} // namespace Frontier
