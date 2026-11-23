/*==========================================================================================================================================
                                                       MATERIALPREVIEWGEOMETRY.H
==========================================================================================================================================*/
// 🧩 Shared UV/tangent sphere for material viewports and GPU thumbnail baking.

#pragma once
#include "SurfaceGeometry.h"
#include "DrapedClothPositions.h"
#include <algorithm>
#include <numbers>

namespace Frontier
{
inline SurfaceGeometry ConstructMaterialPreviewSphere()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Slices = 128, Rings = 64;
    for (uint32_t Ring = 0; Ring <= Rings; ++Ring)
        for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
        {
            const double θ = std::numbers::pi * Ring / Rings;
            const double φ = 2 * std::numbers::pi * Slice / Slices;
            const std::array<float, 3> Normal{float(std::sin(θ) * std::cos(φ)), float(std::sin(θ) * std::sin(φ)), float(std::cos(θ))};
            SurfaceCorner Corner;
            Corner.Position = {Normal[0] * .85f, Normal[1] * .85f, Normal[2] * .85f};
            Corner.Normal = Normal;
            Corner.Coordinates = {float(Slice) / Slices, float(Ring) / Rings};
            Corner.Tangent = {float(-std::sin(φ)), float(std::cos(φ)), 0, -1};
            Geometry.Corners.push_back(Corner);
        }
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Ring * (Slices + 1) + Slice;
            for (const auto Index : {First, First + 1, First + Slices + 1, First + 1, First + Slices + 2, First + Slices + 1})
                Geometry.TriangleCorners.push_back(Index);
        }
    Geometry.MaterialCoordinatesEnabled = true;
    return Geometry;
}
// 📝 Profile equations and angular resolution follow the HTML createBallGeometry implementation.
inline SurfaceGeometry ConstructMaterialPreviewShaderBall()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Rings = 340, Slices = 192;
    const auto Radius = [](double θ)
    {
        const double Y = 1.43 * std::cos(θ);
        const auto Groove = [Y](double Centre, double Width, double Depth)
        { return Depth * std::exp(-std::pow((Y - Centre) / Width, 6)); };
        return std::max(.001, 1.43 * std::sin(θ) - Groove(.69, .021, .032) - Groove(.6, .014, .023) -
            Groove(-.81, .018, .028) - Groove(-.89, .018, .028) - Groove(-.97, .018, .028));
    };
    const auto Rotate = [](std::array<float, 3> Direction)
    {
        const float X = Direction[0] * std::cos(-.27f) - Direction[1] * std::sin(-.27f);
        const float Y = Direction[0] * std::sin(-.27f) + Direction[1] * std::cos(-.27f);
        return std::array<float, 3>{X * std::cos(-.4f) + Direction[2] * std::sin(-.4f), Y,
            -X * std::sin(-.4f) + Direction[2] * std::cos(-.4f)};
    };
    for (uint32_t Ring = 0; Ring <= Rings; ++Ring)
    {
        const double θ = std::numbers::pi * Ring / Rings;
        const double Derivative = (Radius(θ + .00001) - Radius(θ - .00001)) / .00002;
        const double YDerivative = -1.43 * std::sin(θ);
        const double Length = std::max(.00001, std::hypot(Derivative, YDerivative));
        for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
        {
            const double φ = 2 * std::numbers::pi * Slice / Slices;
            SurfaceCorner Corner;
            Corner.Position = Rotate({float(Radius(θ) * std::sin(φ) * .85 / 1.43), float(.85 * std::cos(θ)), float(Radius(θ) * std::cos(φ) * .85 / 1.43)});
            Corner.Normal = Rotate({float(-YDerivative * std::sin(φ) / Length), float(Derivative / Length), float(-YDerivative * std::cos(φ) / Length)});
            if (Ring == 0 || Ring == Rings) Corner.Normal = Rotate({0, Ring == 0 ? 1.f : -1.f, 0});
            Corner.Coordinates = {float(Slice) / Slices, float(Ring) / Rings};
            const auto Tangent = Rotate({float(std::cos(φ)), 0, float(-std::sin(φ))});
            Corner.Tangent = {Tangent[0], Tangent[1], Tangent[2], 1};
            Geometry.Corners.push_back(Corner);
        }
    }
    for (uint32_t Ring = 0; Ring < Rings; ++Ring)
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Ring * (Slices + 1) + Slice;
            for (uint32_t Index : {First, First + Slices + 1, First + 1, First + 1, First + Slices + 1, First + Slices + 2})
                Geometry.TriangleCorners.push_back(Index);
        }
    Geometry.MaterialCoordinatesEnabled = true;
    return Geometry;
}
// 📝 Keep the isolated ball reusable. The library's specimen adds separately classified, GPU-shaded pedestal surfaces.
inline SurfaceGeometry ConstructMaterialPreviewSpecimen()
{
    auto Geometry = ConstructMaterialPreviewShaderBall();
    constexpr float Scale = .55f, Centre = 1.65f * Scale - .85f;
    // 📝 No specimen-group pitch: in the HTML scene the stand, foot and ring are unrotated group
    // siblings of the posed ball, so the pedestal lathe axis stays world-up and orbiting the
    // camera can never roll the stage. The ball keeps its intrinsic HTML pose from its builder.
    for (auto &Corner : Geometry.Corners)
    {
        for (float &Coefficient : Corner.Position) Coefficient *= 1.43f / .85f * Scale;
        Corner.Position[1] += Centre;
    }
    constexpr uint32_t Slices = 128;
    const auto AppendSegment = [&](float FirstRadius, float FirstHeight, float SecondRadius, float SecondHeight, uint32_t Role,
                                   std::array<float, 2> FirstNormal, std::array<float, 2> SecondNormal)
    {
        const uint32_t Offset = uint32_t(Geometry.Corners.size());
        for (uint32_t Ring = 0; Ring < 2; ++Ring)
            for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
            {
                const float Angle = 2.f * std::numbers::pi_v<float> * float(Slice) / Slices;
                const float Radius = Ring ? SecondRadius : FirstRadius, Height = Ring ? SecondHeight : FirstHeight;
                const auto Normal = Ring ? SecondNormal : FirstNormal;
                SurfaceCorner Corner;
                Corner.Position = {Radius * std::sin(Angle) * Scale, Height * Scale - .85f, Radius * std::cos(Angle) * Scale};
                Corner.Normal = {Normal[0] * std::sin(Angle), Normal[1], Normal[0] * std::cos(Angle)};
                Corner.Tangent = {std::cos(Angle), 0, -std::sin(Angle), -1};
                Corner.Coordinates = {-float(Role), float(Ring)};
                Geometry.Corners.push_back(Corner);
            }
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Offset + Slice;
            if (FirstRadius > 0)
                for (uint32_t Index : {First, First + 1, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
            if (SecondRadius > 0)
                for (uint32_t Index : {First + 1, First + Slices + 2, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
        }
    };
    const auto AppendCylinder = [&](float Top, float Bottom, float Height, float Elevation, uint32_t Role)
    {
        const float Slope = Bottom - Top, Length = std::hypot(Height, Slope);
        const std::array<float, 2> Side{Height / Length, Slope / Length};
        AppendSegment(0, Elevation - Height * .5f, Bottom, Elevation - Height * .5f, Role, {0,-1}, {0,-1});
        AppendSegment(Bottom, Elevation - Height * .5f, Top, Elevation + Height * .5f, Role, Side, Side);
        AppendSegment(Top, Elevation + Height * .5f, 0, Elevation + Height * .5f, Role, {0,1}, {0,1});
    };
    AppendCylinder(1.2f, 1.26f, .18f, .115f, 1);
    AppendCylinder(1.26f, 1.22f, .055f, .032f, 2);
    for (uint32_t Segment = 0; Segment < 12; ++Segment)
    {
        const float First = 2.f * std::numbers::pi_v<float> * float(Segment) / 12;
        const float Second = 2.f * std::numbers::pi_v<float> * float(Segment + 1) / 12;
        AppendSegment(1.215f + .012f * std::cos(First), .2f + .012f * std::sin(First),
            1.215f + .012f * std::cos(Second), .2f + .012f * std::sin(Second), 3,
            {std::cos(First), std::sin(First)}, {std::cos(Second), std::sin(Second)});
    }
    return Geometry;
}
// 📝 Draped cloth ported 1:1 from the HTML frozen PBD simulation: same baked grid, UVs, indices and
// edge strip as createDrapedClothGeometry, uniformly scaled/translated to the ±.85 inspection stage.
// The native preview frame matches three.js (+X right, +Y up, +Z toward the viewer), so no mirroring.
inline SurfaceGeometry ConstructMaterialPreviewCloth()
{
    SurfaceGeometry Geometry;
    constexpr uint32_t Grid = DrapedClothGrid;
    std::array<float, 3> Minimum{1e30f, 1e30f, 1e30f}, Maximum{-1e30f, -1e30f, -1e30f};
    for (size_t Vertex = 0; Vertex < Grid * Grid; ++Vertex)
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Minimum[Axis] = std::min(Minimum[Axis], DrapedClothPositions[Vertex * 3 + Axis]);
            Maximum[Axis] = std::max(Maximum[Axis], DrapedClothPositions[Vertex * 3 + Axis]);
        }
    const std::array<float, 3> Centre{(Minimum[0] + Maximum[0]) / 2, (Minimum[1] + Maximum[1]) / 2, (Minimum[2] + Maximum[2]) / 2};
    const float Scale = .85f / std::max({(Maximum[0] - Minimum[0]) / 2, (Maximum[1] - Minimum[1]) / 2, (Maximum[2] - Minimum[2]) / 2});
    for (uint32_t Row = 0; Row < Grid; ++Row)
        for (uint32_t Column = 0; Column < Grid; ++Column)
        {
            const size_t Vertex = size_t(Row) * Grid + Column;
            SurfaceCorner Corner;
            Corner.Position = {(DrapedClothPositions[Vertex * 3] - Centre[0]) * Scale,
                (DrapedClothPositions[Vertex * 3 + 1] - Centre[1]) * Scale,
                (DrapedClothPositions[Vertex * 3 + 2] - Centre[2]) * Scale};
            Corner.Coordinates = {float(Column) / (Grid - 1), 1 - float(Row) / (Grid - 1)};
            Geometry.Corners.push_back(Corner);
            if (Row < Grid - 1 && Column < Grid - 1)
                for (const auto Index : {uint32_t(Vertex), uint32_t(Vertex + Grid), uint32_t(Vertex + 1),
                    uint32_t(Vertex + 1), uint32_t(Vertex + Grid), uint32_t(Vertex + Grid + 1)})
                    Geometry.TriangleCorners.push_back(Index);
        }
    // 📝 three.js computeVertexNormals: unnormalised (c-b)x(a-b) accumulation, then one normalisation.
    std::vector<std::array<double, 3>> Normals(Geometry.Corners.size(), {0, 0, 0});
    for (size_t Triangle = 0; Triangle < Geometry.TriangleCorners.size(); Triangle += 3)
    {
        const auto Corner = [&](uint32_t Index) { return Geometry.Corners[Index].Position; };
        const auto A = Corner(Geometry.TriangleCorners[Triangle]), B = Corner(Geometry.TriangleCorners[Triangle + 1]),
            C = Corner(Geometry.TriangleCorners[Triangle + 2]);
        const std::array<double, 3> Edge1{double(C[0] - B[0]), double(C[1] - B[1]), double(C[2] - B[2])};
        const std::array<double, 3> Edge2{double(A[0] - B[0]), double(A[1] - B[1]), double(A[2] - B[2])};
        const std::array<double, 3> Face{Edge1[1] * Edge2[2] - Edge1[2] * Edge2[1],
            Edge1[2] * Edge2[0] - Edge1[0] * Edge2[2], Edge1[0] * Edge2[1] - Edge1[1] * Edge2[0]};
        for (size_t CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            for (size_t Axis = 0; Axis < 3; ++Axis)
                Normals[Geometry.TriangleCorners[Triangle + CornerIndex]][Axis] += Face[Axis];
    }
    for (size_t Vertex = 0; Vertex < Geometry.Corners.size(); ++Vertex)
    {
        const double Length = std::max(1e-30, std::hypot(Normals[Vertex][0], Normals[Vertex][1], Normals[Vertex][2]));
        Geometry.Corners[Vertex].Normal = {float(Normals[Vertex][0] / Length), float(Normals[Vertex][1] / Length),
            float(Normals[Vertex][2] / Length)};
    }
    // 📝 Narrow physical edge hanging below the sheet, as in the HTML boundary walk (closed loop).
    std::vector<uint32_t> Boundary;
    for (uint32_t Column = 0; Column < Grid; ++Column) Boundary.push_back(Column);
    for (uint32_t Row = 1; Row < Grid; ++Row) Boundary.push_back(Row * Grid + Grid - 1);
    for (uint32_t Column = Grid - 2; Column + 1 > 0; --Column) Boundary.push_back((Grid - 1) * Grid + Column);
    for (uint32_t Row = Grid - 2; Row > 0; --Row) Boundary.push_back(Row * Grid);
    const uint32_t Strip = uint32_t(Geometry.Corners.size());
    for (const auto Source : Boundary)
    {
        const auto Origin = Geometry.Corners[Source];
        SurfaceCorner Inner = Origin, Outer = Origin;
        Outer.Position = {Origin.Position[0] - Origin.Normal[0] * .003f * Scale,
            Origin.Position[1] - Origin.Normal[1] * .003f * Scale, Origin.Position[2] - Origin.Normal[2] * .003f * Scale};
        Geometry.Corners.push_back(Inner);
        Geometry.Corners.push_back(Outer);
    }
    for (size_t Edge = 0; Edge < Boundary.size(); ++Edge)
    {
        const uint32_t First = Strip + uint32_t(Edge) * 2, Second = Strip + uint32_t((Edge + 1) % Boundary.size()) * 2;
        for (const auto Index : {First, First + 1, Second, Second, First + 1, Second + 1})
            Geometry.TriangleCorners.push_back(Index);
    }
    // 📝 The HTML recomputes normals after appending the strip; ribbon corners keep ribbon normals.
    Normals.resize(Geometry.Corners.size(), {0, 0, 0});
    for (size_t Triangle = (Grid - 1) * (Grid - 1) * 6; Triangle < Geometry.TriangleCorners.size(); Triangle += 3)
    {
        const auto Corner = [&](uint32_t Index) { return Geometry.Corners[Index].Position; };
        const auto A = Corner(Geometry.TriangleCorners[Triangle]), B = Corner(Geometry.TriangleCorners[Triangle + 1]),
            C = Corner(Geometry.TriangleCorners[Triangle + 2]);
        const std::array<double, 3> Edge1{double(C[0] - B[0]), double(C[1] - B[1]), double(C[2] - B[2])};
        const std::array<double, 3> Edge2{double(A[0] - B[0]), double(A[1] - B[1]), double(A[2] - B[2])};
        const std::array<double, 3> Face{Edge1[1] * Edge2[2] - Edge1[2] * Edge2[1],
            Edge1[2] * Edge2[0] - Edge1[0] * Edge2[2], Edge1[0] * Edge2[1] - Edge1[1] * Edge2[0]};
        for (size_t CornerIndex = 0; CornerIndex < 3; ++CornerIndex)
            for (size_t Axis = 0; Axis < 3; ++Axis)
                Normals[Geometry.TriangleCorners[Triangle + CornerIndex]][Axis] += Face[Axis];
    }
    for (size_t Vertex = Strip; Vertex < Geometry.Corners.size(); ++Vertex)
    {
        const double Length = std::max(1e-30, std::hypot(Normals[Vertex][0], Normals[Vertex][1], Normals[Vertex][2]));
        Geometry.Corners[Vertex].Normal = {float(Normals[Vertex][0] / Length), float(Normals[Vertex][1] / Length),
            float(Normals[Vertex][2] / Length)};
    }
    // 📝 Lengyel UV-gradient tangents over the cloth grid; the degenerate-UV strip copies its source tangent.
    std::vector<std::array<double, 3>> Tangents(Grid * Grid, {0, 0, 0}), Bitangents(Grid * Grid, {0, 0, 0});
    for (uint32_t Row = 0; Row < Grid - 1; ++Row)
        for (uint32_t Column = 0; Column < Grid - 1; ++Column)
        {
            const std::array<uint32_t, 6> Quad{Row * Grid + Column, (Row + 1) * Grid + Column, Row * Grid + Column + 1,
                Row * Grid + Column + 1, (Row + 1) * Grid + Column, (Row + 1) * Grid + Column + 1};
            for (size_t Triangle = 0; Triangle < 2; ++Triangle)
            {
                const auto P = [&](uint32_t Index) { return Geometry.Corners[Index].Position; };
                const auto U = [&](uint32_t Index) { return Geometry.Corners[Index].Coordinates; };
                const auto I0 = Quad[Triangle * 3], I1 = Quad[Triangle * 3 + 1], I2 = Quad[Triangle * 3 + 2];
                const std::array<double, 3> E1{double(P(I1)[0] - P(I0)[0]), double(P(I1)[1] - P(I0)[1]), double(P(I1)[2] - P(I0)[2])};
                const std::array<double, 3> E2{double(P(I2)[0] - P(I0)[0]), double(P(I2)[1] - P(I0)[1]), double(P(I2)[2] - P(I0)[2])};
                const double S1 = double(U(I1)[0] - U(I0)[0]), T1 = double(U(I1)[1] - U(I0)[1]);
                const double S2 = double(U(I2)[0] - U(I0)[0]), T2 = double(U(I2)[1] - U(I0)[1]);
                const double Determinant = S1 * T2 - S2 * T1;
                if (std::abs(Determinant) < 1e-30) continue;
                const double Reciprocal = 1 / Determinant;
                for (const auto Index : {I0, I1, I2})
                    for (size_t Axis = 0; Axis < 3; ++Axis)
                    {
                        Tangents[Index][Axis] += (E1[Axis] * T2 - E2[Axis] * T1) * Reciprocal;
                        Bitangents[Index][Axis] += (E2[Axis] * S1 - E1[Axis] * S2) * Reciprocal;
                    }
            }
        }
    for (uint32_t Vertex = 0; Vertex < Grid * Grid; ++Vertex)
    {
        const auto &Normal = Geometry.Corners[Vertex].Normal;
        std::array<double, 3> Tangent{Tangents[Vertex][0], Tangents[Vertex][1], Tangents[Vertex][2]};
        const double Along = Tangent[0] * Normal[0] + Tangent[1] * Normal[1] + Tangent[2] * Normal[2];
        for (size_t Axis = 0; Axis < 3; ++Axis) Tangent[Axis] -= Along * Normal[Axis];
        const double Length = std::hypot(Tangent[0], Tangent[1], Tangent[2]);
        if (Length < 1e-12) Tangent = {1, 0, 0};
        else for (double &Coefficient : Tangent) Coefficient /= Length;
        const double Handed = (Normal[1] * Tangent[2] - Normal[2] * Tangent[1]) * Bitangents[Vertex][0] +
            (Normal[2] * Tangent[0] - Normal[0] * Tangent[2]) * Bitangents[Vertex][1] +
            (Normal[0] * Tangent[1] - Normal[1] * Tangent[0]) * Bitangents[Vertex][2];
        Geometry.Corners[Vertex].Tangent = {float(Tangent[0]), float(Tangent[1]), float(Tangent[2]), Handed < 0 ? -1.f : 1.f};
    }
    for (size_t Edge = 0; Edge < Boundary.size(); ++Edge)
        for (const auto Vertex : {Strip + Edge * 2, Strip + Edge * 2 + 1})
        {
            // 📝 Ribbon corners keep ribbon normals, so re-orthogonalise the inherited tangent.
            auto &Corner = Geometry.Corners[Vertex];
            const auto &Source = Geometry.Corners[Boundary[Edge]];
            std::array<double, 3> Tangent{Source.Tangent[0], Source.Tangent[1], Source.Tangent[2]};
            const double Along = Tangent[0] * Corner.Normal[0] + Tangent[1] * Corner.Normal[1] + Tangent[2] * Corner.Normal[2];
            for (size_t Axis = 0; Axis < 3; ++Axis) Tangent[Axis] -= Along * Corner.Normal[Axis];
            double Length = std::hypot(Tangent[0], Tangent[1], Tangent[2]);
            // 📝 A near-vanishing projection means the inherited tangent opposing the ribbon normal is
            // rounding noise; fall back to a constructed perpendicular instead of amplifying it.
            if (Length < 1e-3)
            {
                Tangent = {double(-Corner.Normal[1]), double(Corner.Normal[0]), 0};
                Length = std::hypot(Tangent[0], Tangent[1], Tangent[2]);
                if (Length < 1e-6) Tangent = {0, double(-Corner.Normal[2]), double(Corner.Normal[1])};
                Length = std::hypot(Tangent[0], Tangent[1], Tangent[2]);
            }
            Corner.Tangent = {float(Tangent[0] / Length), float(Tangent[1] / Length), float(Tangent[2] / Length), Source.Tangent[3]};
        }
    Geometry.MaterialCoordinatesEnabled = true;
    return Geometry;
}
// 📝 Cloth inspection scene 1:1 with the HTML viewport: frozen drape over its support ball on the
// studio stand. Ball and stand reuse the baked drape frame (same centre/scale as the drape), so the
// drape-over-ball contact and the ring rest match the web scene; support surfaces carry negative-U roles.
inline SurfaceGeometry ConstructMaterialPreviewClothScene()
{
    auto Geometry = ConstructMaterialPreviewCloth();
    // Normalization replica: must match ConstructMaterialPreviewCloth exactly.
    constexpr uint32_t Grid = DrapedClothGrid;
    std::array<float, 3> Minimum{1e30f, 1e30f, 1e30f}, Maximum{-1e30f, -1e30f, -1e30f};
    for (size_t Vertex = 0; Vertex < Grid * Grid; ++Vertex)
        for (size_t Axis = 0; Axis < 3; ++Axis)
        {
            Minimum[Axis] = std::min(Minimum[Axis], DrapedClothPositions[Vertex * 3 + Axis]);
            Maximum[Axis] = std::max(Maximum[Axis], DrapedClothPositions[Vertex * 3 + Axis]);
        }
    const std::array<float, 3> Centre{(Minimum[0] + Maximum[0]) / 2, (Minimum[1] + Maximum[1]) / 2, (Minimum[2] + Maximum[2]) / 2};
    const float Scale = .85f / std::max({(Maximum[0] - Minimum[0]) / 2, (Maximum[1] - Minimum[1]) / 2, (Maximum[2] - Minimum[2]) / 2});
    const auto MapHeight = [&](float Height) { return (Height - Centre[1]) * Scale; };
    const uint32_t BallOffset = uint32_t(Geometry.Corners.size());
    auto Ball = ConstructMaterialPreviewSphere();
    const float BallScale = DrapedClothSupportRadius * Scale / .85f;
    const std::array<float, 3> BallCentre{(DrapedClothSupportCentre[0] - Centre[0]) * Scale,
        (DrapedClothSupportCentre[1] - Centre[1]) * Scale, (DrapedClothSupportCentre[2] - Centre[2]) * Scale};
    for (auto &Corner : Ball.Corners)
    {
        for (size_t Axis = 0; Axis < 3; ++Axis) Corner.Position[Axis] = Corner.Position[Axis] * BallScale + BallCentre[Axis];
        Corner.Coordinates = {-4.f, Corner.Coordinates[1]};
        Geometry.Corners.push_back(Corner);
    }
    for (auto Index : Ball.TriangleCorners) Geometry.TriangleCorners.push_back(BallOffset + Index);
    // Stand and ring reuse the web cylinder parameters mapped into the drape frame without the specimen
    // stage pitch: the HTML cloth scene is axis-aligned.
    constexpr uint32_t Slices = 128;
    const auto AppendSegment = [&](float FirstRadius, float FirstHeight, float SecondRadius, float SecondHeight, uint32_t Role,
                                   std::array<float, 2> FirstNormal, std::array<float, 2> SecondNormal)
    {
        const uint32_t Offset = uint32_t(Geometry.Corners.size());
        for (uint32_t Ring = 0; Ring < 2; ++Ring)
            for (uint32_t Slice = 0; Slice <= Slices; ++Slice)
            {
                const float Angle = 2.f * std::numbers::pi_v<float> * float(Slice) / Slices;
                const float Radius = Ring ? SecondRadius : FirstRadius, Height = Ring ? SecondHeight : FirstHeight;
                const auto Normal = Ring ? SecondNormal : FirstNormal;
                SurfaceCorner Corner;
                Corner.Position = {Radius * std::sin(Angle) * Scale, MapHeight(Height), Radius * std::cos(Angle) * Scale};
                Corner.Normal = {Normal[0] * std::sin(Angle), Normal[1], Normal[0] * std::cos(Angle)};
                Corner.Tangent = {std::cos(Angle), 0, -std::sin(Angle), -1};
                Corner.Coordinates = {-float(Role), float(Ring)};
                Geometry.Corners.push_back(Corner);
            }
        for (uint32_t Slice = 0; Slice < Slices; ++Slice)
        {
            const uint32_t First = Offset + Slice;
            if (FirstRadius > 0)
                for (uint32_t Index : {First, First + 1, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
            if (SecondRadius > 0)
                for (uint32_t Index : {First + 1, First + Slices + 2, First + Slices + 1}) Geometry.TriangleCorners.push_back(Index);
        }
    };
    const auto AppendCylinder = [&](float Top, float Bottom, float Height, float Elevation, uint32_t Role)
    {
        const float Slope = Bottom - Top, Length = std::hypot(Height, Slope);
        const std::array<float, 2> Side{Height / Length, Slope / Length};
        AppendSegment(0, Elevation - Height * .5f, Bottom, Elevation - Height * .5f, Role, {0,-1}, {0,-1});
        AppendSegment(Bottom, Elevation - Height * .5f, Top, Elevation + Height * .5f, Role, Side, Side);
        AppendSegment(Top, Elevation + Height * .5f, 0, Elevation + Height * .5f, Role, {0,1}, {0,1});
    };
    AppendCylinder(1.2f, 1.26f, .18f, .115f, 1);
    AppendCylinder(1.26f, 1.22f, .055f, .032f, 2);
    for (uint32_t Segment = 0; Segment < 12; ++Segment)
    {
        const float First = 2.f * std::numbers::pi_v<float> * float(Segment) / 12;
        const float Second = 2.f * std::numbers::pi_v<float> * float(Segment + 1) / 12;
        AppendSegment(1.215f + .012f * std::cos(First), .2f + .012f * std::sin(First),
            1.215f + .012f * std::cos(Second), .2f + .012f * std::sin(Second), 3,
            {std::cos(First), std::sin(First)}, {std::cos(Second), std::sin(Second)});
    }
    return Geometry;
}
// 📝 Separated inspection surfaces share one immutable upload; projection selects one without replacing
// descriptors. Separation alone does NOT hide the other surfaces: orbiting far enough sweeps a
// neighbouring specimen (16 units away) into the frustum, so callers must also restrict each draw to
// the selected shape's index range below.
struct MaterialInspectionGeometry
{
    SurfaceGeometry Geometry;
    // 📝 [first index, index count] per MaterialPreviewShape ordinal; partitions the whole index list.
    std::array<std::array<uint32_t, 2>, 5> ShapeIndexRanges{};
};
inline MaterialInspectionGeometry ConstructMaterialInspectionGeometryWithRanges()
{
    MaterialInspectionGeometry Inspection;
    auto &Geometry = Inspection.Geometry;
    Geometry = ConstructMaterialPreviewSpecimen();
    Inspection.ShapeIndexRanges[0] = {0, uint32_t(Geometry.TriangleCorners.size())};
    size_t ShapeOrdinal = 1;
    const auto Append = [&](SurfaceGeometry Surface, float Translation)
    {
        const auto Offset = uint32_t(Geometry.Corners.size());
        const auto FirstIndex = uint32_t(Geometry.TriangleCorners.size());
        for (auto Corner : Surface.Corners)
        {
            Corner.Position[0] += Translation;
            Geometry.Corners.push_back(Corner);
        }
        for (auto Index : Surface.TriangleCorners) Geometry.TriangleCorners.push_back(Offset + Index);
        Inspection.ShapeIndexRanges[ShapeOrdinal++] =
            {FirstIndex, uint32_t(Surface.TriangleCorners.size())};
    };
    Append(ConstructMaterialPreviewSphere(), 16);
    SurfaceGeometry Plane;
    Plane.MaterialCoordinatesEnabled = true;
    for (const auto &Position : std::array<std::array<float, 3>, 4>{{{-.8f,-.8f,0},{.8f,-.8f,0},{.8f,.8f,0},{-.8f,.8f,0}}})
    {
        SurfaceCorner Corner;
        Corner.Position = Position;
        Corner.Normal = {0,0,1};
        Corner.Coordinates = {(Position[0] + .8f) / 1.6f, (Position[1] + .8f) / 1.6f};
        Plane.Corners.push_back(Corner);
    }
    Plane.TriangleCorners = {0,1,2,0,2,3};
    Append(Plane, 32);
    SurfaceGeometry Cube;
    Cube.MaterialCoordinatesEnabled = true;
    for (uint32_t Axis = 0; Axis < 3; ++Axis)
        for (float Sign : {-1.f, 1.f})
        {
            const uint32_t Offset = uint32_t(Cube.Corners.size());
            for (const auto &Coordinate : std::array<std::array<float, 2>,4>{{{-1,-1},{1,-1},{1,1},{-1,1}}})
            {
                SurfaceCorner Corner;
                Corner.Position[Axis] = Sign * .62f;
                Corner.Position[(Axis + 1) % 3] = Coordinate[0] * .62f;
                Corner.Position[(Axis + 2) % 3] = Coordinate[1] * .62f;
                Corner.Normal[Axis] = Sign;
                Corner.Tangent = {0,0,0,Sign};
                Corner.Tangent[(Axis + 1) % 3] = 1;
                Corner.Coordinates = {(Coordinate[0] + 1) / 2, (Coordinate[1] + 1) / 2};
                // 📝 Tilt the specimen to expose three faces, retaining orthogonal unit normals/tangents.
                const auto Rotate = [](std::array<float, 3> Direction)
                {
                    const float X = Direction[0] * .8660254f + Direction[2] * .5f;
                    const float Z = -Direction[0] * .5f + Direction[2] * .8660254f;
                    return std::array<float, 3>{X, Direction[1] * .9396926f - Z * .3420201f,
                        Direction[1] * .3420201f + Z * .9396926f};
                };
                Corner.Position = Rotate(Corner.Position);
                Corner.Normal = Rotate(Corner.Normal);
                auto Tangent = Rotate({Corner.Tangent[0],Corner.Tangent[1],Corner.Tangent[2]});
                std::copy(Tangent.begin(), Tangent.end(), Corner.Tangent.begin());
                Cube.Corners.push_back(Corner);
            }
            for (auto Index : Sign > 0 ? std::array<uint32_t,6>{0,1,2,0,2,3} : std::array<uint32_t,6>{0,2,1,0,3,2})
                Cube.TriangleCorners.push_back(Offset + Index);
        }
    Append(Cube, 48);
    Append(ConstructMaterialPreviewClothScene(), 64);
    return Inspection;
}
// 📝 Geometry-only view retained for acceptance/projection tooling that does not draw.
inline SurfaceGeometry ConstructMaterialInspectionGeometry()
{
    return ConstructMaterialInspectionGeometryWithRanges().Geometry;
}
} // namespace Frontier
