/*====================================================================================================================================
                                                       MASSPROPERTIES.CPP
====================================================================================================================================*/
// 🧩 Exact body and face measures: volume, surface area, centroid and inertia tensor by Green / divergence integrals

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Measurement/MassProperties.h"
#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Solid shells of a body (every shell of every solid region; a cavity shell is oriented inwards).
    [[nodiscard]] std::vector<SolidShellToken> CollectSolidShells(const ModelPartition& Partition, BodyToken Body)
    {
        std::vector<SolidShellToken> Shells;
        for (RegionToken Region : Partition.Bodies.Resolve(Body).Regions)
        {
            if (Partition.Regions.Resolve(Region).Material != RegionMaterial::Solid) continue;
            for (SolidShellToken Shell : Partition.Regions.Resolve(Region).Shells) Shells.push_back(Shell);
        }
        return Shells;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

BodyMassProperties MeasureBodyMass(const ModelPartition& Partition, BodyToken Body)
{
    BodyMassProperties Measure;
    const std::vector<SolidShellToken> Shells = CollectSolidShells(Partition, Body);
    if (Shells.empty()) { Measure.Refusal = "Mass: the body has no solid region (a sheet)"; return Measure; }

    // ---- reference point: the mean vertex
    const BodyInventory Inventory = Partition.CollectInventory(Body);
    Vector3 Reference = {};                                                                  // [len] - Moment origin
    for (VertexToken Vertex : Inventory.Vertices) Reference = Reference + Partition.Vertices.Resolve(Vertex).Position;
    if (!Inventory.Vertices.empty()) Reference = Reference * (1.0 / static_cast<double>(Inventory.Vertices.size()));

    // ---- 10 fluxes: F with div F = 1, 𝑥, 𝑦, 𝑧, 𝑥², 𝑦², 𝑧², 𝑥𝑦, 𝑦𝑧, 𝑧𝑥 (r = P − Reference)
    const auto Flux = [&](auto Field)
    {
        double Total = 0.0;
        for (SolidShellToken Shell : Shells) Total += IntegrateShellFlux(Partition, Shell, [&](Vector3 Position) { return Field(Position - Reference); });
        return Total;
    };
    const double Volume = Flux([](Vector3 r) { return r * (1.0 / 3.0); });
    if (!(Volume > 0.0)) { Measure.Refusal = "Mass: non-positive volume"; return Measure; }
    const double FirstX  = Flux([](Vector3 r) { return Vector3{ 0.5 * r.X * r.X, 0.0, 0.0 }; });
    const double FirstY  = Flux([](Vector3 r) { return Vector3{ 0.0, 0.5 * r.Y * r.Y, 0.0 }; });
    const double FirstZ  = Flux([](Vector3 r) { return Vector3{ 0.0, 0.0, 0.5 * r.Z * r.Z }; });
    const double SecondXX = Flux([](Vector3 r) { return Vector3{ r.X * r.X * r.X / 3.0, 0.0, 0.0 }; });
    const double SecondYY = Flux([](Vector3 r) { return Vector3{ 0.0, r.Y * r.Y * r.Y / 3.0, 0.0 }; });
    const double SecondZZ = Flux([](Vector3 r) { return Vector3{ 0.0, 0.0, r.Z * r.Z * r.Z / 3.0 }; });
    const double SecondXY = Flux([](Vector3 r) { return Vector3{ 0.5 * r.X * r.X * r.Y, 0.0, 0.0 }; });
    const double SecondYZ = Flux([](Vector3 r) { return Vector3{ 0.0, 0.5 * r.Y * r.Y * r.Z, 0.0 }; });
    const double SecondZX = Flux([](Vector3 r) { return Vector3{ 0.0, 0.0, 0.5 * r.Z * r.Z * r.X }; });

    // ---- centroid and the parallel-axis shift
    const Vector3 Offset = { FirstX / Volume, FirstY / Volume, FirstZ / Volume };            // [len] - Centroid − Reference
    const double  XX = SecondXX - Volume * Offset.X * Offset.X;                              // [len⁵] - ∭ 𝑥² about the centroid
    const double  YY = SecondYY - Volume * Offset.Y * Offset.Y;
    const double  ZZ = SecondZZ - Volume * Offset.Z * Offset.Z;
    const double  XY = SecondXY - Volume * Offset.X * Offset.Y;
    const double  YZ = SecondYZ - Volume * Offset.Y * Offset.Z;
    const double  ZX = SecondZX - Volume * Offset.Z * Offset.X;
    Measure.Volume   = Volume;
    Measure.Centroid = Reference + Offset;
    Measure.Inertia  = { YY + ZZ, ZZ + XX, XX + YY, -XY, -YZ, -ZX };

    // ---- surface area of the solid shells
    for (SolidShellToken Shell : Shells)
    {
        for (FaceToken Face : Partition.Shells.Resolve(Shell).Faces) Measure.SurfaceArea += IntegrateFaceSurface(Partition, Face, [](Vector3) { return 1.0; });
    }
    return Measure;
}

FaceAreaMeasure MeasureFaceArea(const ModelPartition& Partition, FaceToken Face)
{
    // 📝 The centroid moments use the mean of the face's vertices as origin (fewer cancelled digits far from 0).
    Vector3 Reference = {};                                                                  // [len] - Moment origin
    int     Count     = 0;
    for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
    {
        Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
        {
            const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
            Reference = Reference + Partition.Vertices.Resolve(Edge.Start).Position;
            ++Count;
        });
    }
    if (Count > 0) Reference = Reference * (1.0 / Count);
    FaceAreaMeasure Measure;
    Measure.Area = IntegrateFaceSurface(Partition, Face, [](Vector3) { return 1.0; });
    if (!(Measure.Area > 0.0)) return Measure;
    const double MomentX = IntegrateFaceSurface(Partition, Face, [&](Vector3 Position) { return Position.X - Reference.X; });
    const double MomentY = IntegrateFaceSurface(Partition, Face, [&](Vector3 Position) { return Position.Y - Reference.Y; });
    const double MomentZ = IntegrateFaceSurface(Partition, Face, [&](Vector3 Position) { return Position.Z - Reference.Z; });
    Measure.Centroid = Reference + Vector3{ MomentX, MomentY, MomentZ } * (1.0 / Measure.Area);
    return Measure;
}

std::array<double, 3> ResolvePrincipalMoments(const std::array<double, 6>& Inertia)
{
    double Matrix[3][3] = { { Inertia[0], Inertia[3], Inertia[5] }, { Inertia[3], Inertia[1], Inertia[4] }, { Inertia[5], Inertia[4], Inertia[2] } };
    for (int Sweep = 0; Sweep < 64; ++Sweep)
    {
        const double OffDiagonal = std::fabs(Matrix[0][1]) + std::fabs(Matrix[1][2]) + std::fabs(Matrix[0][2]);
        const double Diagonal    = std::fabs(Matrix[0][0]) + std::fabs(Matrix[1][1]) + std::fabs(Matrix[2][2]);
        if (OffDiagonal <= 1.0e-17 * Diagonal) break;
        for (int Row = 0; Row < 2; ++Row)
        {
            for (int Column = Row + 1; Column < 3; ++Column)
            {
                if (Matrix[Row][Column] == 0.0) continue;
                const double θ = 0.5 * std::atan2(2.0 * Matrix[Row][Column], Matrix[Column][Column] - Matrix[Row][Row]);
                const double c = std::cos(θ), s = std::sin(θ);
                for (int Index = 0; Index < 3; ++Index)
                {
                    const double First = Matrix[Index][Row], Second = Matrix[Index][Column];
                    Matrix[Index][Row] = c * First - s * Second; Matrix[Index][Column] = s * First + c * Second;
                }
                for (int Index = 0; Index < 3; ++Index)
                {
                    const double First = Matrix[Row][Index], Second = Matrix[Column][Index];
                    Matrix[Row][Index] = c * First - s * Second; Matrix[Column][Index] = s * First + c * Second;
                }
            }
        }
    }
    std::array<double, 3> Moments = { Matrix[0][0], Matrix[1][1], Matrix[2][2] };
    std::sort(Moments.begin(), Moments.end());
    return Moments;
}

} // namespace Frontier::GeometrySpace::Parametric
