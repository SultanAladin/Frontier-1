/*==========================================================================================================================================
                                                         BROWSERPREVIEWGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Reference actuator triangles, studio enclosure, drafting contours and EON lookup transcribed from ReferenceRenderer.js.

#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <string>
#include <unordered_map>
#include <vector>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     REFERENCE SURFACES
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPreviewSurface
{
    const char *Title;
    const char *Albedo;
    double Specular;
    double Metallic;
    double Roughness;
    const char *Description;
};

inline const std::array<BrowserPreviewSurface, 5> &QueryBrowserPreviewSurfaces()
{
    static constexpr std::array<BrowserPreviewSurface, 5> Surfaces{{
        {"Actuator housing", "#87b2a3", .5, .65, .3, "Machined housing and radial cooling fins."},
        {"Copper windings", "#d99565", .5, 1, .24, "Exposed copper winding rings and terminals."},
        {"Shaft & fasteners", "#c5cdd2", .5, 1, .18, "Steel shaft, flange bolts and tie rods."},
        {"Graphite seals", "#282c2b", .5, 0, .7, "Insulating gaskets and recessed bearing seals."},
        {"Mounting plate", "#747f82", .5, .85, .4, "Mounting plate and structural support brackets."},
    }};
    return Surfaces;
}

inline std::array<float, 3> ConvertBrowserSrgb(const char *Hex)
{
    std::array<float, 3> Linear{};
    for (size_t Offset = 0; Offset < 3; ++Offset)
    {
        const std::string Pair(Hex + 1 + Offset * 2, 2);
        const double Srgb = std::stoi(Pair, nullptr, 16) / 255.0;
        Linear[Offset] = float(Srgb <= .04045 ? Srgb / 12.92 : std::pow((Srgb + .055) / 1.055, 2.4));
    }
    return Linear;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     ACTUATOR TRIANGLES
//------------------------------------------------------------------------------------------------------------------------

namespace BrowserPreviewDetail
{
inline void PushTriangle(std::vector<float> &Vertices, const std::array<std::array<double, 3>, 3> &Corners,
    const std::array<std::array<double, 3>, 3> &Normals, double Surface, const std::array<double, 3> &Separation)
{
    for (size_t Index = 0; Index < 3; ++Index)
    {
        Vertices.push_back(float(Corners[Index][0]));
        Vertices.push_back(float(Corners[Index][1]));
        Vertices.push_back(float(Corners[Index][2]));
        Vertices.push_back(float(Normals[Index][0]));
        Vertices.push_back(float(Normals[Index][1]));
        Vertices.push_back(float(Normals[Index][2]));
        Vertices.push_back(float(Surface));
        Vertices.push_back(float(Separation[0]));
        Vertices.push_back(float(Separation[1]));
        Vertices.push_back(float(Separation[2]));
    }
}

inline void PushQuad(std::vector<float> &Vertices, const std::array<std::array<double, 3>, 4> &Corners,
    const std::array<std::array<double, 3>, 4> &Normals, double Surface, const std::array<double, 3> &Separation)
{
    PushTriangle(Vertices, {Corners[0], Corners[1], Corners[2]}, {Normals[0], Normals[1], Normals[2]}, Surface, Separation);
    PushTriangle(Vertices, {Corners[0], Corners[2], Corners[3]}, {Normals[0], Normals[2], Normals[3]}, Surface, Separation);
}

inline void PushBox(std::vector<float> &Vertices, const std::array<double, 3> &Centre, const std::array<double, 3> &Span,
    double Surface, const std::array<double, 3> &Separation = {0, 0, 0})
{
    for (size_t Axis = 0; Axis < 3; ++Axis)
        for (const double Sign : {-1.0, 1.0})
        {
            const size_t Tangent = (Axis + 1) % 3, Bitangent = (Axis + 2) % 3;
            std::array<double, 3> Normal{0, 0, 0};
            Normal[Axis] = Sign;
            std::array<std::array<double, 3>, 4> Corners{};
            size_t Corner = 0;
            for (const auto Pair : {std::array<double, 2>{-1, -1}, {1, -1}, {1, 1}, {-1, 1}})
            {
                auto Point = Centre;
                Point[Axis] += Sign * Span[Axis] / 2;
                Point[Tangent] += Pair[0] * Span[Tangent] / 2;
                Point[Bitangent] += Pair[1] * Span[Bitangent] / 2;
                Corners[Corner++] = Point;
            }
            PushQuad(Vertices, Corners, {Normal, Normal, Normal, Normal}, Surface, Separation);
        }
}

inline void PushTube(std::vector<float> &Vertices, const std::array<double, 3> &Centre, double Radius, double Bore,
    double Length, double Surface, const std::array<double, 3> &Separation = {0, 0, 0}, size_t Axis = 0, size_t Segments = 48)
{
    const size_t Tangent = (Axis + 1) % 3, Bitangent = (Axis + 2) % 3;
    const auto Point = [&](double Angle, double Radial, double Depth)
    {
        auto Position = Centre;
        Position[Axis] += Depth;
        Position[Tangent] += std::cos(Angle) * Radial;
        Position[Bitangent] += std::sin(Angle) * Radial;
        return Position;
    };
    const auto RadialNormal = [&](double Angle, double Sign)
    {
        std::array<double, 3> Normal{0, 0, 0};
        Normal[Tangent] = std::cos(Angle) * Sign;
        Normal[Bitangent] = std::sin(Angle) * Sign;
        return Normal;
    };
    for (size_t Segment = 0; Segment < Segments; ++Segment)
    {
        const double A = Segment * std::numbers::pi * 2 / Segments, B = (Segment + 1) * std::numbers::pi * 2 / Segments;
        for (const auto [Radial, Sign] : {std::array<double, 2>{Radius, 1}, {Bore, -1}})
        {
            if (!Radial) continue;
            PushQuad(Vertices,
                {Point(A, Radial, -Length / 2), Point(B, Radial, -Length / 2), Point(B, Radial, Length / 2),
                    Point(A, Radial, Length / 2)},
                {RadialNormal(A, Sign), RadialNormal(B, Sign), RadialNormal(B, Sign), RadialNormal(A, Sign)}, Surface,
                Separation);
        }
        for (const double Sign : {-1.0, 1.0})
        {
            std::array<double, 3> Normal{0, 0, 0};
            Normal[Axis] = Sign;
            PushQuad(Vertices,
                {Point(A, Bore, Sign * Length / 2), Point(B, Bore, Sign * Length / 2), Point(B, Radius, Sign * Length / 2),
                    Point(A, Radius, Sign * Length / 2)},
                {Normal, Normal, Normal, Normal}, Surface, Separation);
        }
    }
}
} // namespace BrowserPreviewDetail

inline std::vector<float> GenerateBrowserActuatorTriangles()
{
    using namespace BrowserPreviewDetail;
    std::vector<float> Vertices;
    PushBox(Vertices, {0, -.06, 0}, {16, .08, 16}, 6);
    for (int Line = -8; Line <= 8; ++Line)
    {
        PushBox(Vertices, {double(Line), 0, 0}, {.012, .004, 16}, 5);
        PushBox(Vertices, {0, 0, double(Line)}, {16, .004, .012}, 5);
    }
    PushBox(Vertices, {0, .2, 0}, {6.2, .32, 3.2}, 4, {0, -.15, 0});
    for (const double X : {-1.65, 1.65})
    {
        PushBox(Vertices, {X, .73, 0}, {.58, 1.05, 1.8}, 4, {0, -.2, 0});
        PushBox(Vertices, {X, .46, 0}, {1, .18, 2.7}, 4, {0, -.2, 0});
        for (const double Z : {-1.15, 1.15}) PushTube(Vertices, {X, .59, Z}, .13, .045, .12, 2, {0, -.2, 0}, 1, 6);
    }
    constexpr double Height = 1.8;
    PushTube(Vertices, {0, Height, 0}, .89, .3, 2.7, 0);
    PushTube(Vertices, {-.25, Height, 0}, .93, .89, 1.8, 3);
    for (size_t Index = 0; Index < 12; ++Index) PushTube(Vertices, {-.98 + Index * .14, Height, 0}, 1.03, .88, .055, 0);
    for (const double X : {-1.28, 1.28})
    {
        const double Sign = X > 0 ? 1 : -1;
        PushTube(Vertices, {X, Height, 0}, .98, .3, .19, 1, {Sign * .45, 0, 0});
        PushTube(Vertices, {X * 1.17, Height, 0}, 1.08, .3, .17, 0, {Sign * .8, 0, 0});
        PushTube(Vertices, {X * 1.29, Height, 0}, .69, .25, .08, 3, {Sign * 1.0, 0, 0});
        PushTube(Vertices, {X * 1.34, Height, 0}, .48, .25, .12, 2, {Sign * 1.1, 0, 0});
        for (size_t Bolt = 0; Bolt < 8; ++Bolt)
        {
            const double Angle = Bolt * std::numbers::pi / 4;
            PushTube(Vertices, {X * 1.27, Height + .86 * std::cos(Angle), .86 * std::sin(Angle)}, .105, .035, .13, 2,
                {Sign * 1.15, 0, 0}, 0, 6);
        }
    }
    PushTube(Vertices, {2, Height, 0}, .25, 0, 1.5, 2, {1.7, 0, 0});
    PushTube(Vertices, {2.75, Height, 0}, .34, .12, .18, 2, {1.9, 0, 0});
    PushBox(Vertices, {2.25, Height + .26, 0}, {.6, .06, .11}, 3, {1.7, 0, 0});
    PushTube(Vertices, {-1.95, Height, 0}, .3, 0, .55, 2, {-1.6, 0, 0});
    for (const double Angle : {std::numbers::pi / 4, 3 * std::numbers::pi / 4, 5 * std::numbers::pi / 4, 7 * std::numbers::pi / 4})
        PushTube(Vertices, {0, Height + .79 * std::cos(Angle), .79 * std::sin(Angle)}, .045, 0, 3.2, 2);
    PushBox(Vertices, {.5, 2.92, 0}, {.9, .38, .8}, 3, {0, .7, 0});
    PushBox(Vertices, {.5, 3.14, 0}, {1.02, .09, .92}, 0, {0, 1, 0});
    for (const double X : {.25, .75}) PushTube(Vertices, {X, 3.29, 0}, .12, .045, .24, 1, {0, 1.2, 0}, 1, 16);
    for (const double X : {-2.65, 2.65})
        for (const double Z : {-1.25, 1.25}) PushTube(Vertices, {X, .41, Z}, .13, .05, .1, 2, {0, -.15, 0}, 1, 6);
    return Vertices;
}

// 📝 Category/revision fixtures: copper/shaft omission for older revisions, housing-only geometry,
// smaller housing proportions and the project studio enclosure with its revision-gated props.
inline std::vector<float> GenerateBrowserPreviewTriangles(const std::vector<float> &Actuator, int Category, size_t Revision)
{
    using namespace BrowserPreviewDetail;
    std::vector<float> Sequence;
    for (size_t Offset = 0; Offset < Actuator.size(); Offset += 30)
    {
        const float Surface = Actuator[Offset + 6];
        if (Revision >= 1 && Surface == 1) continue;
        if (Revision >= 2 && Surface == 2) continue;
        if (Category == 2 && Surface < 5 && Surface != 0) continue;
        if (Category == 2 && Surface < 5 && Revision > 0)
        {
            const double Scale = 1 - Revision * .12;
            for (const size_t Shift : {size_t(0), size_t(10), size_t(20)})
            {
                Sequence.push_back(float(Actuator[Offset + Shift] * Scale));
                Sequence.push_back(float(1.8 + (Actuator[Offset + Shift + 1] - 1.8) * Scale));
                Sequence.push_back(float(Actuator[Offset + Shift + 2] * Scale));
                for (size_t Component = 3; Component < 10; ++Component) Sequence.push_back(Actuator[Offset + Shift + Component]);
            }
        }
        else
            for (size_t Component = 0; Component < 30; ++Component) Sequence.push_back(Actuator[Offset + Component]);
    }
    if (Category == 0)
    {
        const auto Box = [&](const std::array<double, 3> &Centre, const std::array<double, 3> &Span, double Surface)
        { PushBox(Sequence, Centre, Span, Surface); };
        Box({0, -.2, 0}, {11, .2, 9}, 4);
        for (const double X : {-4.8, 4.8})
        {
            Box({X, 1.7, -3.8}, {.28, 3.7, .28}, 2);
            Box({X, 1.7, 3.8}, {.28, 3.7, .28}, 2);
        }
        Box({0, 3.5, -3.8}, {9.8, .24, .25}, 0);
        Box({0, 3.5, 3.8}, {9.8, .24, .25}, 0);
        for (const double X : {-4.8, 4.8}) Box({X, 3.5, 0}, {.25, .24, 7.8}, 0);
        if (Revision < 2)
        {
            Box({-3.8, .5, -2.2}, {1.5, 1, 1.5}, 0);
            Box({3.7, .35, 2.2}, {1.6, .7, 1.2}, 1);
        }
    }
    return Sequence;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     EON LOOKUP AND CONTOURS
//------------------------------------------------------------------------------------------------------------------------

inline std::vector<uint8_t> GenerateBrowserEonLookup()
{
    std::vector<uint8_t> Pixels(64 * 32 * 4, 0);
    for (size_t Row = 0; Row < 32; ++Row)
        for (size_t Column = 0; Column < 64; ++Column)
        {
            const double Roughness = double(Row) / 31, Cosine = std::max(.0001, double(Column) / 63);
            const double Sine = std::sqrt(std::max(0.0, 1 - Cosine * Cosine));
            const double A = 1 / (1 + (.5 - 2 / (3 * std::numbers::pi)) * Roughness);
            const double G = Sine * (std::acos(Cosine) - Sine * Cosine) + 2 * ((Sine / Cosine) * (1 - Sine * Sine * Sine) - Sine) / 3;
            const double Missing = std::max(0.0, 1 - (A + Roughness * A * G / std::numbers::pi));
            const size_t Offset = (Row * 64 + Column) * 4;
            Pixels[Offset] = uint8_t(std::round(std::min(1.0, Missing * 4) * 255));
            Pixels[Offset + 3] = 255;
        }
    return Pixels;
}

inline std::vector<float> GenerateBrowserDraftingContours(const std::vector<float> &Vertices)
{
    struct Edge
    {
        std::array<float, 3> Start{}, End{}, Separation{};
        std::vector<std::array<double, 3>> Normals;
    };
    const auto CoordinateKey = [](const std::array<float, 3> &Position)
    {
        // 📝 Math.round rounds half up; replicate exactly (llround rounds half away from zero).
        std::string Key;
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            if (Axis) Key += ",";
            Key += std::to_string(int64_t(std::floor(Position[Axis] * 100000 + .5)));
        }
        return Key;
    };
    std::unordered_map<std::string, size_t> Lookup;
    std::vector<Edge> Edges;
    for (size_t Offset = 0; Offset + 30 <= Vertices.size(); Offset += 30)
    {
        if (Vertices[Offset + 6] >= 5) continue;
        std::array<std::array<float, 3>, 3> Corners{};
        for (size_t Corner = 0; Corner < 3; ++Corner)
            for (size_t Axis = 0; Axis < 3; ++Axis) Corners[Corner][Axis] = Vertices[Offset + Corner * 10 + Axis];
        std::array<double, 3> EdgeA{}, EdgeB{};
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            EdgeA[Axis] = Corners[1][Axis] - Corners[0][Axis];
            EdgeB[Axis] = Corners[2][Axis] - Corners[0][Axis];
        }
        const std::array<double, 3> Cross{EdgeA[1] * EdgeB[2] - EdgeA[2] * EdgeB[1],
            EdgeA[2] * EdgeB[0] - EdgeA[0] * EdgeB[2], EdgeA[0] * EdgeB[1] - EdgeA[1] * EdgeB[0]};
        if (std::hypot(Cross[0], Cross[1], Cross[2]) < 1e-9) continue;
        const double Length = std::hypot(Cross[0], Cross[1], Cross[2]);
        std::array<double, 3> Normal{Cross[0] / Length, Cross[1] / Length, Cross[2] / Length};
        std::array<double, 3> Authored{};
        for (size_t Axis = 0; Axis < 3; ++Axis)
            Authored[Axis] = Vertices[Offset + 3 + Axis] + Vertices[Offset + 13 + Axis] + Vertices[Offset + 23 + Axis];
        if (Normal[0] * Authored[0] + Normal[1] * Authored[1] + Normal[2] * Authored[2] < 0)
            for (double &Component : Normal) Component = -Component;
        std::array<float, 3> Separation{Vertices[Offset + 7], Vertices[Offset + 8], Vertices[Offset + 9]};
        const std::string Group = std::to_string(int(Vertices[Offset + 6])) + ":" + CoordinateKey(Separation) + ":";
        for (size_t Side = 0; Side < 3; ++Side)
        {
            std::array<std::string, 2> Keys{CoordinateKey(Corners[Side]), CoordinateKey(Corners[(Side + 1) % 3])};
            if (Keys[1] < Keys[0]) std::swap(Keys[0], Keys[1]);
            const std::string Token = Group + Keys[0] + "/" + Keys[1];
            const auto Found = Lookup.find(Token);
            if (Found != Lookup.end()) Edges[Found->second].Normals.push_back(Normal);
            else
            {
                Lookup.emplace(Token, Edges.size());
                Edges.push_back({Corners[Side], Corners[(Side + 1) % 3], Separation, {Normal}});
            }
        }
    }
    std::vector<float> Contours;
    for (const auto &Edge : Edges)
    {
        const auto &NormalA = Edge.Normals[0];
        const auto &NormalB = Edge.Normals.size() > 1 ? Edge.Normals[1] : Edge.Normals[0];
        const double Cosine = NormalA[0] * NormalB[0] + NormalA[1] * NormalB[1] + NormalA[2] * NormalB[2];
        if (Edge.Normals.size() == 2 && Cosine > .9999) continue;
        const float Crease =
            (Edge.Normals.size() != 2 || Cosine < std::cos(25 * std::numbers::pi / 180)) ? 1.f : 0.f;
        for (const auto &Position : {Edge.Start, Edge.End})
        {
            Contours.push_back(Position[0]);
            Contours.push_back(Position[1]);
            Contours.push_back(Position[2]);
            Contours.push_back(Edge.Separation[0]);
            Contours.push_back(Edge.Separation[1]);
            Contours.push_back(Edge.Separation[2]);
            Contours.push_back(float(NormalA[0]));
            Contours.push_back(float(NormalA[1]));
            Contours.push_back(float(NormalA[2]));
            Contours.push_back(float(NormalB[0]));
            Contours.push_back(float(NormalB[1]));
            Contours.push_back(float(NormalB[2]));
            Contours.push_back(Crease);
        }
    }
    return Contours;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     STATISTICS, UV AND CAMERAS
//------------------------------------------------------------------------------------------------------------------------

struct BrowserPreviewStatistics
{
    size_t Triangles = 0;
    std::array<double, 3> Extents{};
};

inline BrowserPreviewStatistics QueryBrowserPreviewStatistics(const std::vector<float> &Vertices)
{
    BrowserPreviewStatistics Statistics;
    std::array<double, 3> Minimum{1e30, 1e30, 1e30}, Maximum{-1e30, -1e30, -1e30};
    for (size_t Offset = 0; Offset + 30 <= Vertices.size(); Offset += 30)
    {
        if (Vertices[Offset + 6] >= 5) continue;
        ++Statistics.Triangles;
        for (const size_t Shift : {size_t(0), size_t(10), size_t(20)})
            for (size_t Axis = 0; Axis < 3; ++Axis)
            {
                Minimum[Axis] = std::min(Minimum[Axis], double(Vertices[Offset + Shift + Axis]));
                Maximum[Axis] = std::max(Maximum[Axis], double(Vertices[Offset + Shift + Axis]));
            }
    }
    for (size_t Axis = 0; Axis < 3; ++Axis)
        Statistics.Extents[Axis] = std::round((Maximum[Axis] - Minimum[Axis]) * 1000) / 1000;
    return Statistics;
}

struct BrowserUvSegment
{
    std::array<std::array<double, 2>, 2> Ends;
};

inline std::vector<BrowserUvSegment> GenerateBrowserCylindricalUv(const std::vector<float> &Vertices)
{
    std::vector<BrowserUvSegment> Segments;
    for (size_t Offset = 0; Offset + 30 <= Vertices.size(); Offset += 30)
    {
        if (Vertices[Offset + 6] >= 5) continue;
        std::array<std::array<double, 2>, 3> Corners{};
        for (size_t Corner = 0; Corner < 3; ++Corner)
        {
            const size_t Shift = Offset + Corner * 10;
            Corners[Corner] = {
                (std::atan2(double(Vertices[Shift + 1]) - 1.8, double(Vertices[Shift + 2])) + std::numbers::pi) /
                    (2 * std::numbers::pi),
                (double(Vertices[Shift]) + 3.2) / 6.4};
        }
        for (size_t Side = 0; Side < 3; ++Side) Segments.push_back({{Corners[Side], Corners[(Side + 1) % 3]}});
    }
    return Segments;
}

struct BrowserPreviewCamera
{
    double Azimuth = .85;
    double Elevation = .43;
    double Distance = 11;
    std::array<double, 3> Target{0, 1.1, 0};
};

inline BrowserPreviewCamera ConfigureBrowserCamera(int Category, size_t View, bool Sketch)
{
    BrowserPreviewCamera Camera;
    constexpr std::array<double, 3> Azimuths{.85, -.65, 1.6};
    Camera.Azimuth = Azimuths[View % 3];
    Camera.Elevation = Category == 0 ? .55 : .43;
    Camera.Distance = Category == 0 ? 17 : 10.5;
    Camera.Target = {0, 1.3, 0};
    if (Sketch) Camera = {View % 2 ? std::numbers::pi / 2 : 0, 0, 10.5, {0, 1.65, 0}};
    return Camera;
}

inline void OrbitBrowserCamera(BrowserPreviewCamera &Camera, double DeltaX, double DeltaY, bool Pan)
{
    if (Pan)
    {
        // The reference contract refuses |target| > 100; clamp here so sustained panning can never strand an open
        // render pass (a mid-record throw poisons the submit and loses the device).
        Camera.Target[0] = std::clamp(Camera.Target[0] - DeltaX * Camera.Distance * .0015, -100.0, 100.0);
        Camera.Target[1] = std::clamp(Camera.Target[1] + DeltaY * Camera.Distance * .0015, -100.0, 100.0);
    }
    else
    {
        Camera.Azimuth -= DeltaX * .008;
        Camera.Elevation = std::clamp(Camera.Elevation + DeltaY * .006, -1.4, 1.4);
    }
}

inline void ZoomBrowserCamera(BrowserPreviewCamera &Camera, double Wheel)
{
    Camera.Distance =
        std::clamp(Camera.Distance * std::exp(std::clamp(Wheel, -100.0, 100.0) * .001), 4.0, 28.0);
}

// 📝 GL-style clip convention, matching the reference; the Vulkan renderer remaps depth and Y on upload.
inline std::array<float, 16> ConstructBrowserViewProjection(
    const BrowserPreviewCamera &Camera, double Aspect, bool Drafting, std::array<float, 3> &Eye)
{
    const std::array<double, 3> Offset{std::cos(Camera.Elevation) * std::sin(Camera.Azimuth), std::sin(Camera.Elevation),
        std::cos(Camera.Elevation) * std::cos(Camera.Azimuth)};
    std::array<double, 3> EyePosition{};
    for (size_t Axis = 0; Axis < 3; ++Axis) EyePosition[Axis] = Camera.Target[Axis] + Camera.Distance * Offset[Axis];
    Eye = {float(EyePosition[0]), float(EyePosition[1]), float(EyePosition[2])};
    std::array<double, 3> Z{ EyePosition[0] - Camera.Target[0], EyePosition[1] - Camera.Target[1], EyePosition[2] - Camera.Target[2]};
    const double ZLength = std::hypot(Z[0], Z[1], Z[2]);
    for (double &Component : Z) Component /= ZLength;
    std::array<double, 3> X{Z[2], 0, -Z[0]};
    const double XLength = std::hypot(X[0], X[1], X[2]);
    for (double &Component : X) Component /= XLength;
    const std::array<double, 3> Y{Z[1] * X[2] - Z[2] * X[1], Z[2] * X[0] - Z[0] * X[2], Z[0] * X[1] - Z[1] * X[0]};
    const std::array<double, 16> View{X[0], Y[0], Z[0], 0, X[1], Y[1], Z[1], 0, X[2], Y[2], Z[2], 0,
        -(X[0] * EyePosition[0] + X[1] * EyePosition[1] + X[2] * EyePosition[2]),
        -(Y[0] * EyePosition[0] + Y[1] * EyePosition[1] + Y[2] * EyePosition[2]),
        -(Z[0] * EyePosition[0] + Z[1] * EyePosition[1] + Z[2] * EyePosition[2]), 1};
    constexpr double Near = .1, Far = 100;
    const double Focal = 1 / std::tan(std::numbers::pi / 8);
    const double Span = Camera.Distance / Focal / std::min(1.0, Aspect);
    const std::array<double, 16> Projection = Drafting
        ? std::array<double, 16>{1 / (Span * Aspect), 0, 0, 0, 0, 1 / Span, 0, 0, 0, 0, -2 / (Far - Near), 0, 0, -.15,
              -(Far + Near) / (Far - Near), 1}
        : std::array<double, 16>{Focal / Aspect, 0, 0, 0, 0, Focal, 0, 0, 0, 0, (Far + Near) / (Near - Far), -1, 0, 0,
              2 * Far * Near / (Near - Far), 0};
    std::array<float, 16> Product{};
    for (size_t Column = 0; Column < 4; ++Column)
        for (size_t Row = 0; Row < 4; ++Row)
            for (size_t Axis = 0; Axis < 4; ++Axis) Product[Column * 4 + Row] += float(Projection[Axis * 4 + Row] * View[Column * 4 + Axis]);
    return Product;
}
} // namespace Frontier
