/*==========================================================================================================================================
                                                    DISTANCE FIELD DEFINITION
==========================================================================================================================================*/

#include "DistanceField.h"

#include "DiscrepancySequence.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }

SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}

SpatialVector Normalised(SpatialVector A) noexcept
{
    const double Magnitude = Length(A);
    return Magnitude > 0.0 ? Mul(A, 1.0 / Magnitude) : SpatialVector{};
}

double PrimitiveDistance(const DistancePrimitive& Primitive, SpatialVector Point) noexcept
{
    const SpatialVector Local = Sub(Point, Primitive.Centre);
    switch (Primitive.Kind)
    {
        case DistancePrimitiveKind::Sphere:
            return Length(Local) - Primitive.Extents.X;
        case DistancePrimitiveKind::Box:
        {
            const SpatialVector Q{std::abs(Local.X) - Primitive.Extents.X, std::abs(Local.Y) - Primitive.Extents.Y,
                                  std::abs(Local.Z) - Primitive.Extents.Z};
            const SpatialVector Clamped{std::max(Q.X, 0.0), std::max(Q.Y, 0.0), std::max(Q.Z, 0.0)};
            return Length(Clamped) + std::min(std::max(Q.X, std::max(Q.Y, Q.Z)), 0.0);
        }
        case DistancePrimitiveKind::HalfSpace:
            return Dot(Local, Primitive.Extents);   // Extents carries the unit normal
    }
    return 0.0;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                           SCENE
//------------------------------------------------------------------------------------------------------------------------

DistanceFieldSample DistanceFieldScene::Evaluate(SpatialVector Point) const noexcept
{
    DistanceFieldSample Result;
    Result.Distance = 1e30;
    for (uint32_t Index = 0; Index < Primitives_.size(); ++Index)
    {
        const double D = PrimitiveDistance(Primitives_[Index], Point);
        if (D < Result.Distance)
        {
            Result.Distance  = D;
            Result.Primitive = Index;
        }
    }
    return Result;
}

SpatialVector DistanceFieldScene::NormalAt(SpatialVector Point) const noexcept
{
    // Tetrahedron gradient (4 taps) — the standard SDF normal estimator.
    constexpr double     H = 1e-4;
    const SpatialVector  K1{1, -1, -1}, K2{-1, -1, 1}, K3{-1, 1, -1}, K4{1, 1, 1};
    SpatialVector        Gradient{};
    Gradient = Add(Gradient, Mul(K1, Evaluate(Add(Point, Mul(K1, H))).Distance));
    Gradient = Add(Gradient, Mul(K2, Evaluate(Add(Point, Mul(K2, H))).Distance));
    Gradient = Add(Gradient, Mul(K3, Evaluate(Add(Point, Mul(K3, H))).Distance));
    Gradient = Add(Gradient, Mul(K4, Evaluate(Add(Point, Mul(K4, H))).Distance));
    return Normalised(Gradient);
}

DistanceTraceResult DistanceFieldScene::Trace(SpatialVector Origin, SpatialVector Direction, double MaxDistance,
                                              double SurfaceEpsilon, uint32_t MaxSteps) const noexcept
{
    DistanceTraceResult Result;
    double Travelled = 0.0;
    for (uint32_t Step = 0; Step < MaxSteps; ++Step)
    {
        Result.StepsUsed         = Step + 1;
        const SpatialVector At   = Add(Origin, Mul(Direction, Travelled));
        const DistanceFieldSample Sample = Evaluate(At);
        if (Sample.Distance <= SurfaceEpsilon)
        {
            Result.Hit       = true;
            Result.Distance  = Travelled;
            Result.Primitive = Sample.Primitive;
            return Result;
        }
        Travelled += Sample.Distance;
        if (Travelled >= MaxDistance)
        {
            Result.Distance = MaxDistance;
            return Result;
        }
    }
    Result.Distance = Travelled;
    return Result;
}

DistanceTraceResult DistanceFieldScene::IntersectAnalytic(SpatialVector Origin, SpatialVector Direction,
                                                          double MaxDistance) const noexcept
{
    DistanceTraceResult Result;
    Result.Distance = MaxDistance;
    const auto Finite = [](SpatialVector V) { return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z); };
    const double A = Dot(Direction, Direction);
    if (!Finite(Origin) || !Finite(Direction) || !std::isfinite(A) || !std::isfinite(MaxDistance) ||
        MaxDistance <= 0.0 || A <= 1e-24)
        return Result;
    for (uint32_t Index = 0; Index < Primitives_.size(); ++Index)
    {
        ++Result.StepsUsed;
        const auto& Primitive = Primitives_[Index];
        if (!Finite(Primitive.Centre) || !Finite(Primitive.Extents)) continue;
        const SpatialVector Local = Sub(Origin, Primitive.Centre);
        double Hit = MaxDistance;
        if (Primitive.Kind == DistancePrimitiveKind::Sphere && Primitive.Extents.X > 0.0)
        {
            const double B = Dot(Local, Direction);
            const double C = Dot(Local, Local) - Primitive.Extents.X * Primitive.Extents.X;
            if (C <= 0.0) Hit = 0.0;
            else
            {
                const double Discriminant = B*B - A*C;
                if (B < 0.0 && Discriminant >= 0.0)
                    Hit = C / (-B + std::sqrt(Discriminant)); // Avoid subtractive cancellation near a sphere.
            }
        }
        else if (Primitive.Kind == DistancePrimitiveKind::Box && Primitive.Extents.X >= 0.0 &&
                 Primitive.Extents.Y >= 0.0 && Primitive.Extents.Z >= 0.0)
        {
            const double OriginAxis[3] = {Local.X, Local.Y, Local.Z};
            const double DirectionAxis[3] = {Direction.X, Direction.Y, Direction.Z};
            const double Extent[3] = {Primitive.Extents.X, Primitive.Extents.Y, Primitive.Extents.Z};
            double Near = 0.0, Far = MaxDistance;
            bool Intersects = true;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                if (std::abs(DirectionAxis[Axis]) < 1e-15)
                {
                    if (OriginAxis[Axis] < -Extent[Axis] || OriginAxis[Axis] > Extent[Axis]) Intersects = false;
                    continue;
                }
                double Enter = (-Extent[Axis] - OriginAxis[Axis]) / DirectionAxis[Axis];
                double Exit = (Extent[Axis] - OriginAxis[Axis]) / DirectionAxis[Axis];
                if (Enter > Exit) std::swap(Enter, Exit);
                Near = std::max(Near, Enter); Far = std::min(Far, Exit);
                if (Near > Far) { Intersects = false; break; }
            }
            if (Intersects) Hit = Near;
        }
        else if (Primitive.Kind == DistancePrimitiveKind::HalfSpace && Dot(Primitive.Extents, Primitive.Extents) > 0.0)
        {
            const double Signed = Dot(Local, Primitive.Extents);
            const double Speed = Dot(Direction, Primitive.Extents);
            if (Signed <= 0.0) Hit = 0.0;
            else if (Speed < 0.0) Hit = -Signed / Speed;
        }
        if (Hit >= 0.0 && Hit < Result.Distance)
        {
            Result.Hit = true;
            Result.Distance = Hit;
            Result.Primitive = Index;
        }
    }
    return Result;
}

bool DistanceFieldScene::SegmentVisible(SpatialVector A, SpatialVector B, double EndpointEpsilon) const noexcept
{
    const SpatialVector Delta     = Sub(B, A);
    const double        LengthAB  = Length(Delta);
    if (LengthAB <= 2.0 * EndpointEpsilon)
    {
        return true;
    }
    const SpatialVector Direction = Mul(Delta, 1.0 / LengthAB);
    const SpatialVector Start     = Add(A, Mul(Direction, EndpointEpsilon));
    const DistanceTraceResult Hit = Trace(Start, Direction, LengthAB - 2.0 * EndpointEpsilon);
    return !Hit.Hit;
}

double DistanceFieldScene::AmbientOcclusion(SpatialVector Point, SpatialVector Normal, double Radius) const noexcept
{
    if (Radius <= 0.0)
    {
        return 1.0;
    }
    double Occlusion  = 0.0;
    double WeightSum  = 0.0;
    double Weight     = 1.0;
    for (uint32_t Tap = 1; Tap <= 5; ++Tap)
    {
        const double H        = Radius * static_cast<double>(Tap) / 5.0;
        const double Distance = Evaluate(Add(Point, Mul(Normal, H))).Distance;
        Occlusion += Weight * std::max(H - Distance, 0.0);
        WeightSum += Weight;
        Weight *= 0.9;
    }
    // Normalise by the fully-occluded response (taps returning zero distance throughout).
    const double Normaliser = Radius * WeightSum * 0.6;
    return std::clamp(1.0 - Occlusion / Normaliser, 0.0, 1.0);
}

double DistanceFieldScene::IndirectIrradiance(SpatialVector Point, SpatialVector Normal, uint32_t SampleCount,
                                              DeterministicRandom& Random, double MaxDistance) const noexcept
{
    if (SampleCount == 0)
    {
        return 0.0;
    }
    const SpatialVector N = Normalised(Normal);
    const SpatialVector Helper = std::abs(N.Y) < 0.99 ? SpatialVector{0, 1, 0} : SpatialVector{1, 0, 0};
    const SpatialVector T = Normalised(Cross(Helper, N));
    const SpatialVector B = Cross(N, T);
    const double ShiftX = Random.NextUnit(), ShiftY = Random.NextUnit();
    constexpr double Pi = 3.14159265358979323846;

    double Sum = 0.0;
    for (uint32_t Sample = 0; Sample < SampleCount; ++Sample)
    {
        const SequencePoint Q = CranleyPatterson(HammersleyPoint(Sample, SampleCount), ShiftX, ShiftY);
        // Cosine-weighted hemisphere: r = sqrt(u), z = sqrt(1-u) (pdf = cos/pi).
        const double R     = std::sqrt(Q.X);
        const double Phi   = 2.0 * Pi * Q.Y;
        const double LocalZ = std::sqrt(std::max(1.0 - Q.X, 0.0));
        const SpatialVector Direction = Normalised(Add(Add(Mul(T, R * std::cos(Phi)), Mul(B, R * std::sin(Phi))),
                                                         Mul(N, LocalZ)));
        const SpatialVector Origin = Add(Point, Mul(N, 1e-3));
        const DistanceTraceResult Hit = Trace(Origin, Direction, MaxDistance);
        if (Hit.Hit && Hit.Primitive < Primitives_.size())
        {
            Sum += Pi * Primitives_[Hit.Primitive].EmissiveRadiance;   // L*cos / (cos/pi)
        }
    }
    return Sum / static_cast<double>(SampleCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       BAKED RECORDS
//------------------------------------------------------------------------------------------------------------------------

bool BakedDistanceField::Bake(const DistanceFieldScene& Scene, SpatialVector BoundsMin, SpatialVector BoundsMax,
                              uint32_t ResolutionX, uint32_t ResolutionY, uint32_t ResolutionZ)
{
    Values_.clear();
    if (ResolutionX < 2 || ResolutionY < 2 || ResolutionZ < 2 || BoundsMax.X <= BoundsMin.X ||
        BoundsMax.Y <= BoundsMin.Y || BoundsMax.Z <= BoundsMin.Z)
    {
        ResolutionX_ = ResolutionY_ = ResolutionZ_ = 0;
        return false;
    }
    BoundsMin_   = BoundsMin;
    BoundsMax_   = BoundsMax;
    ResolutionX_ = ResolutionX;
    ResolutionY_ = ResolutionY;
    ResolutionZ_ = ResolutionZ;
    Values_.resize(static_cast<size_t>(ResolutionX) * ResolutionY * ResolutionZ);
    for (uint32_t Z = 0; Z < ResolutionZ; ++Z)
    {
        for (uint32_t Y = 0; Y < ResolutionY; ++Y)
        {
            for (uint32_t X = 0; X < ResolutionX; ++X)
            {
                const SpatialVector P{BoundsMin.X + (BoundsMax.X - BoundsMin.X) * X / (ResolutionX - 1),
                                      BoundsMin.Y + (BoundsMax.Y - BoundsMin.Y) * Y / (ResolutionY - 1),
                                      BoundsMin.Z + (BoundsMax.Z - BoundsMin.Z) * Z / (ResolutionZ - 1)};
                Values_[(static_cast<size_t>(Z) * ResolutionY + Y) * ResolutionX + X] = Scene.Evaluate(P).Distance;
            }
        }
    }
    return true;
}

double BakedDistanceField::Sample(SpatialVector Point) const noexcept
{
    if (Values_.empty())
    {
        return 1e30;
    }
    const auto Axis = [](double Value, double Min, double Max, uint32_t Resolution, uint32_t& Cell, double& Frac) {
        const double T = std::clamp((Value - Min) / (Max - Min), 0.0, 1.0) * (Resolution - 1);
        Cell = std::min(static_cast<uint32_t>(T), Resolution - 2);
        Frac = T - Cell;
    };
    uint32_t CX, CY, CZ;
    double   FX, FY, FZ;
    Axis(Point.X, BoundsMin_.X, BoundsMax_.X, ResolutionX_, CX, FX);
    Axis(Point.Y, BoundsMin_.Y, BoundsMax_.Y, ResolutionY_, CY, FY);
    Axis(Point.Z, BoundsMin_.Z, BoundsMax_.Z, ResolutionZ_, CZ, FZ);
    const auto At = [&](uint32_t X, uint32_t Y, uint32_t Z) {
        return Values_[(static_cast<size_t>(Z) * ResolutionY_ + Y) * ResolutionX_ + X];
    };
    const double C00 = At(CX, CY, CZ) * (1 - FX) + At(CX + 1, CY, CZ) * FX;
    const double C10 = At(CX, CY + 1, CZ) * (1 - FX) + At(CX + 1, CY + 1, CZ) * FX;
    const double C01 = At(CX, CY, CZ + 1) * (1 - FX) + At(CX + 1, CY, CZ + 1) * FX;
    const double C11 = At(CX, CY + 1, CZ + 1) * (1 - FX) + At(CX + 1, CY + 1, CZ + 1) * FX;
    const double C0  = C00 * (1 - FY) + C10 * FY;
    const double C1  = C01 * (1 - FY) + C11 * FY;
    return C0 * (1 - FZ) + C1 * FZ;
}

} // namespace Frontier
