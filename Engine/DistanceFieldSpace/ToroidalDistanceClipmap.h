/*============================================================================================================================================
                                                        TOROIDALDISTANCECLIPMAP.H
============================================================================================================================================*/
// 🧩 Exact port of arena/f2d37e4b-frontier (Experimental/DistanceClipmap) algorithms:
//    - Production TriangleVisibilitySpace double-precision + Float32 Packed BVH (FacetAtlas)
//    - Baked local unsigned distance volume with 1-Lipschitz corner-corrected lower intervals (BakedFacetDistance)
//    - 3-tier 48^3 toroidal DistanceClipmap with dirty-region rigid-motion repair and SignedBox static union
//    - 32D Joe-Kuo Sobol Quadrature + Spherical Cap / Cosine Hemisphere samplers

#pragma once

#include "Engine/DistanceFieldSpace/SobolDirections.h"
#include "Engine/SpatialAcceleration/TriangleVisibilitySpace.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace Frontier::DistanceFieldSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       SPATIAL ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

inline constexpr float ClipmapPi = 3.14159265358979323846f;

struct ClipVector
{
    float X = 0.0f, Y = 0.0f, Z = 0.0f;
    [[nodiscard]] float operator[](unsigned Axis) const noexcept
    {
        return Axis == 0u ? X : Axis == 1u ? Y : Z;
    }
};

inline ClipVector operator+(ClipVector A, ClipVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
inline ClipVector operator-(ClipVector A, ClipVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
inline ClipVector operator-(ClipVector A) noexcept               { return {-A.X, -A.Y, -A.Z}; }
inline ClipVector operator*(ClipVector A, float S) noexcept      { return {A.X * S, A.Y * S, A.Z * S}; }
inline ClipVector operator*(float S, ClipVector A) noexcept      { return A * S; }
inline ClipVector operator*(ClipVector A, ClipVector B) noexcept { return {A.X * B.X, A.Y * B.Y, A.Z * B.Z}; }
inline ClipVector operator/(ClipVector A, float S) noexcept      { return A * (1.0f / S); }
inline ClipVector& operator+=(ClipVector& A, ClipVector B) noexcept { A = A + B; return A; }

[[nodiscard]] inline float Dot(ClipVector A, ClipVector B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}
[[nodiscard]] inline ClipVector Cross(ClipVector A, ClipVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
[[nodiscard]] inline float Length(ClipVector A) noexcept
{
    return std::sqrt(Dot(A, A));
}
[[nodiscard]] inline ClipVector Normalize(ClipVector A) noexcept
{
    const float M = Length(A);
    return M > 0.0f ? A / M : ClipVector{};
}
[[nodiscard]] inline ClipVector Minimum(ClipVector A, ClipVector B) noexcept
{
    return {std::min(A.X, B.X), std::min(A.Y, B.Y), std::min(A.Z, B.Z)};
}
[[nodiscard]] inline ClipVector Maximum(ClipVector A, ClipVector B) noexcept
{
    return {std::max(A.X, B.X), std::max(A.Y, B.Y), std::max(A.Z, B.Z)};
}
[[nodiscard]] inline ClipVector Absolute(ClipVector A) noexcept
{
    return {std::abs(A.X), std::abs(A.Y), std::abs(A.Z)};
}
[[nodiscard]] inline float Luminance(ClipVector A) noexcept
{
    return Dot(A, {0.2126f, 0.7152f, 0.0722f});
}
[[nodiscard]] inline SpatialVector Expand(ClipVector A) noexcept
{
    return {A.X, A.Y, A.Z};
}
[[nodiscard]] inline ClipVector Reduce(SpatialVector A) noexcept
{
    return {float(A.X), float(A.Y), float(A.Z)};
}

inline void RequireCondition(bool Condition, const std::string& Description)
{
    if (!Condition) throw std::runtime_error(Description);
}

inline void RunParallel(unsigned Count, const std::function<void(unsigned)>& Evaluate)
{
    std::atomic<unsigned>    Cursor{0u};
    const unsigned           Threads = std::min(4u, std::max(1u, std::thread::hardware_concurrency()));
    std::vector<std::thread> Pending;
    Pending.reserve(Threads);
    for (unsigned Thread = 0u; Thread < Threads; ++Thread)
    {
        Pending.emplace_back([&]()
        {
            for (;;)
            {
                const unsigned Index = Cursor.fetch_add(1u, std::memory_order_relaxed);
                if (Index >= Count) break;
                Evaluate(Index);
            }
        });
    }
    for (auto& Execution : Pending) Execution.join();
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIXED SOBOL QUADRATURE
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline std::uint32_t AvalancheHash(std::uint32_t Bits) noexcept
{
    Bits ^= Bits >> 16;
    Bits *= 0x7feb352du;
    Bits ^= Bits >> 15;
    Bits *= 0x846ca68bu;
    return Bits ^ (Bits >> 16);
}

[[nodiscard]] inline std::array<float, 2> SobolQuadrature(unsigned Index, unsigned Pair, unsigned Pixel = 0u)
{
    RequireCondition(Pair < 16u, "Sobol dimension budget exceeded");
    const unsigned Key   = AvalancheHash(Pair * 0x9e3779b9u + 0x85ebca6bu) ^ AvalancheHash(Pixel);
    unsigned       First = AvalancheHash(Key ^ 0xa511e9b3u);
    unsigned       Second = AvalancheHash(Key ^ 0x63d83595u);
    unsigned       Gray = Index ^ (Index >> 1u), Digit = 0u;
    while (Gray)
    {
        if (Gray & 1u)
        {
            First ^= SobolDirections[Pair * 2u][Digit];
            Second ^= SobolDirections[Pair * 2u + 1u][Digit];
        }
        Gray >>= 1u;
        ++Digit;
    }
    return {float(First >> 8) * (1.0f / 16777216.0f), float(Second >> 8) * (1.0f / 16777216.0f)};
}

inline void ConstructOrthonormalBasis(ClipVector N, ClipVector& Tangent, ClipVector& Bitangent) noexcept
{
    Tangent   = Normalize(Cross(std::abs(N.Z) < 0.95f ? ClipVector{0.0f, 0.0f, 1.0f} : ClipVector{0.0f, 1.0f, 0.0f}, N));
    Bitangent = Cross(N, Tangent);
}

[[nodiscard]] inline ClipVector SampleCosineHemisphere(ClipVector N, std::array<float, 2> Sample) noexcept
{
    ClipVector Tangent{}, Bitangent{};
    ConstructOrthonormalBasis(N, Tangent, Bitangent);
    const float Radius = std::sqrt(Sample[0]), Phi = 2.0f * ClipmapPi * Sample[1];
    return Normalize(Tangent * (Radius * std::cos(Phi)) + Bitangent * (Radius * std::sin(Phi)) +
                     N * std::sqrt(std::max(0.0f, 1.0f - Sample[0])));
}

[[nodiscard]] inline ClipVector SampleUniformCapDirection(ClipVector Axis, float AngularRadius, std::array<float, 2> Sample) noexcept
{
    ClipVector Tangent{}, Bitangent{};
    ConstructOrthonormalBasis(Axis, Tangent, Bitangent);
    const float OneMinusCosine = 2.0f * std::pow(std::sin(0.5f * AngularRadius), 2.0f) * Sample[0];
    const float Cosine         = 1.0f - OneMinusCosine;
    const float Sine           = std::sqrt(std::max(0.0f, OneMinusCosine * (2.0f - OneMinusCosine)));
    const float Phi            = 2.0f * ClipmapPi * Sample[1];
    return Normalize(Axis * Cosine + (Tangent * std::cos(Phi) + Bitangent * std::sin(Phi)) * Sine);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    RIGID POSE & FACETS
//------------------------------------------------------------------------------------------------------------------------

struct RigidPose
{
    ClipVector Translation{};
    float      Cosine = 1.0f, Sine = 0.0f;

    [[nodiscard]] ClipVector Rotate(ClipVector P) const noexcept
    {
        return {Cosine * P.X - Sine * P.Y, Sine * P.X + Cosine * P.Y, P.Z};
    }
    [[nodiscard]] ClipVector Unrotate(ClipVector P) const noexcept
    {
        return {Cosine * P.X + Sine * P.Y, -Sine * P.X + Cosine * P.Y, P.Z};
    }
    [[nodiscard]] ClipVector ToWorld(ClipVector P) const noexcept
    {
        return Rotate(P) + Translation;
    }
    [[nodiscard]] ClipVector ToLocal(ClipVector P) const noexcept
    {
        return Unrotate(P - Translation);
    }
};

struct Facet
{
    std::array<ClipVector, 3> Corner{};
    std::array<ClipVector, 3> Normal{};
    unsigned                  Material = 0u;
};

struct Strike
{
    float    Travel     = INFINITY;
    float    U          = 0.0f;
    float    V          = 0.0f;
    unsigned FacetIndex = UINT32_MAX;

    [[nodiscard]] bool Confirmed() const noexcept
    {
        return FacetIndex != UINT32_MAX;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                              PRODUCTION BOUNDS / FLOAT32 QUERIES
//------------------------------------------------------------------------------------------------------------------------

class FacetAtlas
{
public:
    TriangleVisibilitySpace             Geometry{};
    TriangleVisibilitySpace::FloatAtlas Packed{};
    std::vector<Facet>                  Surfaces{};
    ClipVector                          Low{}, High{};

    void Construct(std::vector<Facet> Triangles)
    {
        RequireCondition(!Triangles.empty(), "Empty facet atlas");
        Surfaces = std::move(Triangles);
        std::vector<std::array<SpatialVector, 3>> Positions;
        Positions.reserve(Surfaces.size());
        for (const auto& Triangle : Surfaces)
        {
            Positions.push_back({Expand(Triangle.Corner[0]), Expand(Triangle.Corner[1]), Expand(Triangle.Corner[2])});
        }
        Geometry.Build(std::move(Positions));
        Packed                = Geometry.PackFloat32();
        const auto& Enclosure = Packed.Enclosures[0];
        Low                   = {Enclosure.Minimum[0], Enclosure.Minimum[1], Enclosure.Minimum[2]};
        High                  = {Enclosure.Maximum[0], Enclosure.Maximum[1], Enclosure.Maximum[2]};
    }

    static bool IntersectEnclosure(ClipVector Origin,
                                   ClipVector Direction,
                                   ClipVector BoxLow,
                                   ClipVector BoxHigh,
                                   float      MinimumTravel,
                                   float      MaximumTravel,
                                   float&     Near) noexcept
    {
        Near      = MinimumTravel;
        float Far = MaximumTravel;
        for (unsigned Axis = 0u; Axis < 3u; ++Axis)
        {
            if (Direction[Axis] == 0.0f)
            {
                if (Origin[Axis] < BoxLow[Axis] || Origin[Axis] > BoxHigh[Axis]) return false;
            }
            else
            {
                float A = (BoxLow[Axis] - Origin[Axis]) / Direction[Axis];
                float B = (BoxHigh[Axis] - Origin[Axis]) / Direction[Axis];
                if (A > B) std::swap(A, B);
                Near = std::max(Near, A);
                Far  = std::min(Far, B);
            }
        }
        return Near <= Far;
    }

    bool IntersectRegion(unsigned Index, ClipVector Origin, ClipVector Direction, float Limit, float& Near) const noexcept
    {
        const auto& Region = Packed.Enclosures[Index];
        return IntersectEnclosure(Origin,
                                  Direction,
                                  {Region.Minimum[0], Region.Minimum[1], Region.Minimum[2]},
                                  {Region.Maximum[0], Region.Maximum[1], Region.Maximum[2]},
                                  0.0f,
                                  Limit,
                                  Near);
    }

    Strike Trace(ClipVector Origin, ClipVector Direction, float Limit, bool Any = false) const noexcept
    {
        Strike Result;
        if (Packed.Enclosures.empty()) return Result;
        std::array<unsigned, 64> Pending{};
        unsigned                 Count = 1u;
        while (Count)
        {
            const auto& Region = Packed.Enclosures[Pending[--Count]];
            float       Near   = 0.0f;
            if (!IntersectEnclosure(Origin,
                                    Direction,
                                    {Region.Minimum[0], Region.Minimum[1], Region.Minimum[2]},
                                    {Region.Maximum[0], Region.Maximum[1], Region.Maximum[2]},
                                    0.0f,
                                    Limit,
                                    Near))
                continue;
            if (!Region.Links[2])
            {
                float      LowerNear = 0.0f, UpperNear = 0.0f;
                const bool LowerHit = IntersectRegion(Region.Links[0], Origin, Direction, Limit, LowerNear);
                const bool UpperHit = IntersectRegion(Region.Links[1], Origin, Direction, Limit, UpperNear);
                if (LowerHit && UpperHit)
                {
                    Pending[Count++] = Region.Links[LowerNear < UpperNear ? 1u : 0u];
                    Pending[Count++] = Region.Links[LowerNear < UpperNear ? 0u : 1u];
                }
                else if (LowerHit)
                    Pending[Count++] = Region.Links[0];
                else if (UpperHit)
                    Pending[Count++] = Region.Links[1];
                continue;
            }
            for (unsigned Slot = Region.Links[0]; Slot < Region.Links[0] + Region.Links[1]; ++Slot)
            {
                const auto&      Triangle = Packed.Corners[Slot];
                const ClipVector A{Triangle.A[0], Triangle.A[1], Triangle.A[2]};
                const ClipVector B{Triangle.B[0], Triangle.B[1], Triangle.B[2]};
                const ClipVector C{Triangle.C[0], Triangle.C[1], Triangle.C[2]};
                const ClipVector EdgeAB        = B - A;
                const ClipVector EdgeAC        = C - A;
                const ClipVector Perpendicular = Cross(Direction, EdgeAC);
                const float      Determinant   = Dot(EdgeAB, Perpendicular);
                if (std::abs(Determinant) < 1e-12f) continue;
                const float      Reciprocal   = 1.0f / Determinant;
                const ClipVector Displacement = Origin - A;
                const float      U            = Dot(Displacement, Perpendicular) * Reciprocal;
                if (U < 0.0f || U > 1.0f) continue;
                const ClipVector SecondCross = Cross(Displacement, EdgeAB);
                const float      V           = Dot(Direction, SecondCross) * Reciprocal;
                if (V < 0.0f || U + V > 1.0f) continue;
                const float Travel = Dot(EdgeAC, SecondCross) * Reciprocal;
                if (Travel > 1e-5f && Travel < Limit)
                {
                    Result = {Travel, U, V, Packed.OriginalFacets[Slot]};
                    if (Any) return Result;
                    Limit = Travel;
                }
            }
        }
        return Result;
    }

    static float TriangleDistanceSquared(ClipVector P, ClipVector A, ClipVector B, ClipVector C) noexcept
    {
        const ClipVector EdgeAB = B - A, EdgeAC = C - A, Difference = P - A;
        const ClipVector Perpendicular = Cross(EdgeAB, EdgeAC);
        const float      NormalSquared = Dot(Perpendicular, Perpendicular);
        if (NormalSquared > 1e-22f && Dot(Cross(EdgeAB, Difference), Perpendicular) >= 0.0f &&
            Dot(Cross(C - B, P - B), Perpendicular) >= 0.0f && Dot(Cross(A - C, P - C), Perpendicular) >= 0.0f)
        {
            const float Height = Dot(Difference, Perpendicular);
            return Height * Height / NormalSquared;
        }
        float            Squared   = INFINITY;
        const ClipVector Corners[] = {A, B, C};
        for (unsigned Edge = 0u; Edge < 3u; ++Edge)
        {
            const ClipVector Direction   = Corners[(Edge + 1u) % 3u] - Corners[Edge];
            const ClipVector Offset      = P - Corners[Edge];
            const float      Denominator = Dot(Direction, Direction);
            const float      Fraction    = Denominator > 0.0f ? std::clamp(Dot(Offset, Direction) / Denominator, 0.0f, 1.0f) : 0.0f;
            const ClipVector Separation  = Offset - Direction * Fraction;
            Squared                      = std::min(Squared, Dot(Separation, Separation));
        }
        return Squared;
    }

    float RegionDistanceSquared(unsigned Index, ClipVector P) const noexcept
    {
        const auto&      Region     = Packed.Enclosures[Index];
        const ClipVector Separation = Maximum(
            Maximum(ClipVector{Region.Minimum[0], Region.Minimum[1], Region.Minimum[2]} - P,
                    P - ClipVector{Region.Maximum[0], Region.Maximum[1], Region.Maximum[2]}),
            {});
        return Dot(Separation, Separation);
    }

    float Nearest(ClipVector P) const noexcept
    {
        if (Packed.Enclosures.empty()) return 1.0e6f;
        float                    BestSquared = INFINITY;
        std::array<unsigned, 64> Pending{};
        unsigned                 Count = 1u;
        while (Count)
        {
            const unsigned Index = Pending[--Count];
            if (RegionDistanceSquared(Index, P) > BestSquared) continue;
            const auto& Region = Packed.Enclosures[Index];
            if (!Region.Links[2])
            {
                const float Lower = RegionDistanceSquared(Region.Links[0], P);
                const float Upper = RegionDistanceSquared(Region.Links[1], P);
                if (std::max(Lower, Upper) < BestSquared) Pending[Count++] = Region.Links[Lower < Upper ? 1u : 0u];
                if (std::min(Lower, Upper) < BestSquared) Pending[Count++] = Region.Links[Lower < Upper ? 0u : 1u];
                continue;
            }
            for (unsigned Slot = Region.Links[0]; Slot < Region.Links[0] + Region.Links[1]; ++Slot)
            {
                const auto& Triangle = Packed.Corners[Slot];
                BestSquared = std::min(
                    BestSquared,
                    TriangleDistanceSquared(P,
                                            {Triangle.A[0], Triangle.A[1], Triangle.A[2]},
                                            {Triangle.B[0], Triangle.B[1], Triangle.B[2]},
                                            {Triangle.C[0], Triangle.C[1], Triangle.C[2]}));
            }
        }
        return std::sqrt(BestSquared);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ANALYTIC CLOSED PRIMITIVES
//------------------------------------------------------------------------------------------------------------------------

struct ClosedBox
{
    ClipVector Centre{};
    ClipVector HalfExtent{};
    unsigned   Material = 0u;
};

[[nodiscard]] inline float SignedBoxDistance(ClipVector P, const ClosedBox& Box) noexcept
{
    const ClipVector Q = Absolute(P - Box.Centre) - Box.HalfExtent;
    return Length(Maximum(Q, {})) + std::min(std::max({Q.X, Q.Y, Q.Z}), 0.0f);
}

struct DistanceInterval
{
    float Raw = 0.0f, Lower = 0.0f;
};

[[nodiscard]] inline float EnclosureDistance(ClipVector P, ClipVector Low, ClipVector High) noexcept
{
    return Length(Maximum(Maximum(Low - P, P - High), {}));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       LOCAL DISTANCE BAKE
//------------------------------------------------------------------------------------------------------------------------

class BakedFacetDistance
{
public:
    ClipVector              Low{}, High{}, Spacing{};
    std::array<unsigned, 3> Extent{80u, 40u, 32u};
    std::vector<float>      Samples{};

    [[nodiscard]] std::size_t Address(unsigned X, unsigned Y, unsigned Z) const noexcept
    {
        return (std::size_t(Z) * Extent[1] + Y) * Extent[0] + X;
    }

    void Bake(const FacetAtlas& Atlas)
    {
        Low     = Atlas.Low - ClipVector{0.15f, 0.15f, 0.15f};
        High    = Atlas.High + ClipVector{0.15f, 0.15f, 0.15f};
        Spacing = {
            (High.X - Low.X) / float(Extent[0] - 1u),
            (High.Y - Low.Y) / float(Extent[1] - 1u),
            (High.Z - Low.Z) / float(Extent[2] - 1u)};
        Samples.resize(std::size_t(Extent[0]) * Extent[1] * Extent[2]);
        RunParallel(Extent[2], [&](unsigned Z)
        {
            for (unsigned Y = 0u; Y < Extent[1]; ++Y)
            {
                for (unsigned X = 0u; X < Extent[0]; ++X)
                {
                    Samples[Address(X, Y, Z)] =
                        Atlas.Nearest(Low + ClipVector{float(X) * Spacing.X, float(Y) * Spacing.Y, float(Z) * Spacing.Z});
                }
            }
        });
    }

    [[nodiscard]] DistanceInterval Sample(ClipVector P) const noexcept
    {
        if (Samples.empty()) return {1.0f, 0.0f};
        const float Outside = EnclosureDistance(P, Low, High);
        if (Outside > 0.0f) return {Outside, Outside};
        const ClipVector Coordinate{(P.X - Low.X) / Spacing.X, (P.Y - Low.Y) / Spacing.Y, (P.Z - Low.Z) / Spacing.Z};
        std::array<unsigned, 3> Cell{};
        ClipVector              Fraction{};
        float*                  Fractions[] = {&Fraction.X, &Fraction.Y, &Fraction.Z};
        for (unsigned Axis = 0u; Axis < 3u; ++Axis)
        {
            const float Clamped = std::clamp(Coordinate[Axis], 0.0f, float(Extent[Axis] - 1u));
            Cell[Axis]          = std::min(unsigned(Clamped), Extent[Axis] - 2u);
            *Fractions[Axis]    = Clamped - float(Cell[Axis]);
        }
        float Raw = 0.0f, Correction = 0.0f;
        for (unsigned Z = 0u; Z < 2u; ++Z)
        {
            for (unsigned Y = 0u; Y < 2u; ++Y)
            {
                for (unsigned X = 0u; X < 2u; ++X)
                {
                    const float Weight = (X ? Fraction.X : 1.0f - Fraction.X) * (Y ? Fraction.Y : 1.0f - Fraction.Y) *
                                         (Z ? Fraction.Z : 1.0f - Fraction.Z);
                    Raw += Weight * Samples[Address(Cell[0] + X, Cell[1] + Y, Cell[2] + Z)];
                    Correction += Weight * Length(ClipVector{(float(X) - Fraction.X) * Spacing.X,
                                                             (float(Y) - Fraction.Y) * Spacing.Y,
                                                             (float(Z) - Fraction.Z) * Spacing.Z});
                }
            }
        }
        // 📝 For a 1-Lipschitz unsigned distance, d(P) >= Σ w_c [d(c) - |P-c|].
        return {Raw, std::max(0.0f, Raw - Correction - 2e-5f)};
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 TOROIDAL CLIPMAP ADDRESSING
//------------------------------------------------------------------------------------------------------------------------

struct alignas(16) ClipTexel
{
    float Raw = 1.0f, Lower = 1.0f, SignedStatic = 1.0f, Occupancy = 0.0f;
};
static_assert(sizeof(ClipTexel) == 16u);

class DistanceClipmap
{
public:
    static constexpr unsigned Resolution = 48u;
    static constexpr unsigned LevelCount = 3u;
    static constexpr float    Truncation = 1.0f;
    struct Level
    {
        std::array<int, 3>     Origin{};
        float                  Spacing     = 0.0f;
        bool                   Initialized = false;
        std::vector<ClipTexel> Texels{};
    };
    std::array<Level, LevelCount>    Levels{};
    const BakedFacetDistance*        Local               = nullptr;
    std::vector<ClosedBox>           ClosedSurfaces{};
    const FacetAtlas*                StaticTriangleAtlas = nullptr; // Optional fallback for arbitrary static meshes
    RigidPose                        PreviousPose{};
    bool                             PreviousInitialized = false;
    std::array<unsigned, LevelCount> Rewritten{};

    [[nodiscard]] static unsigned Wrap(int Coordinate) noexcept
    {
        const int Remainder = Coordinate % int(Resolution);
        return unsigned(Remainder < 0 ? Remainder + int(Resolution) : Remainder);
    }
    [[nodiscard]] static std::size_t Address(int X, int Y, int Z) noexcept
    {
        return (std::size_t(Wrap(Z)) * Resolution + Wrap(Y)) * Resolution + Wrap(X);
    }
    [[nodiscard]] static bool Contains(const std::array<int, 3>& Origin, int X, int Y, int Z) noexcept
    {
        return X >= Origin[0] && Y >= Origin[1] && Z >= Origin[2] &&
               X < Origin[0] + int(Resolution) && Y < Origin[1] + int(Resolution) && Z < Origin[2] + int(Resolution);
    }
    [[nodiscard]] bool Influenced(ClipVector P, const RigidPose& Pose) const noexcept
    {
        if (Local == nullptr || Local->Samples.empty()) return false;
        return EnclosureDistance(Pose.ToLocal(P), Local->Low, Local->High) <= Truncation + 0.001f;
    }
    [[nodiscard]] ClipTexel Evaluate(ClipVector P, const RigidPose& Pose) const noexcept
    {
        ClipTexel Result{Truncation, Truncation, Truncation, 0.0f};
        if (Local != nullptr && !Local->Samples.empty())
        {
            const auto Distance = Local->Sample(Pose.ToLocal(P));
            Result.Raw          = std::min(Truncation, Distance.Raw);
            Result.Lower        = std::min(Truncation, Distance.Lower);
        }
        for (const auto& Box : ClosedSurfaces)
        {
            const float Signed  = SignedBoxDistance(P, Box);
            Result.SignedStatic = std::min(Result.SignedStatic, Signed);
            Result.Raw          = std::min(Result.Raw, std::abs(Signed));
            Result.Lower        = std::min(Result.Lower, std::max(0.0f, std::abs(Signed) - 2e-5f));
        }
        if (ClosedSurfaces.empty() && StaticTriangleAtlas != nullptr && !StaticTriangleAtlas->Surfaces.empty())
        {
            const float StaticDist = StaticTriangleAtlas->Nearest(P);
            Result.Raw             = std::min(Result.Raw, StaticDist);
            Result.Lower           = std::min(Result.Lower, std::max(0.0f, StaticDist - 2e-5f));
        }
        Result.Occupancy = Result.Raw < 0.08f ? 1.0f : 0.0f;
        return Result;
    }

    void Relocate(const RigidPose& Pose, ClipVector Focus, bool Force = false)
    {
        const bool Moved = !PreviousInitialized || Pose.Translation.X != PreviousPose.Translation.X ||
                           Pose.Translation.Y != PreviousPose.Translation.Y ||
                           Pose.Translation.Z != PreviousPose.Translation.Z || Pose.Cosine != PreviousPose.Cosine ||
                           Pose.Sine != PreviousPose.Sine;
        for (unsigned Tier = 0u; Tier < LevelCount; ++Tier)
        {
            auto& Grid   = Levels[Tier];
            Grid.Spacing = 0.125f * float(1u << Tier);
            const std::array<int, 3> NewOrigin{
                int(std::floor(Focus.X / Grid.Spacing)) - int(Resolution / 2u),
                int(std::floor(Focus.Y / Grid.Spacing)) - int(Resolution / 2u),
                int(std::floor(Focus.Z / Grid.Spacing)) - int(Resolution / 2u)};
            const auto OldOrigin = Grid.Origin;
            if (Grid.Texels.empty()) Grid.Texels.resize(std::size_t(Resolution) * Resolution * Resolution);
            std::atomic<unsigned> RewrittenCount{0u};
            RunParallel(Resolution, [&](unsigned Z)
            {
                unsigned LocalRewritten = 0u;
                for (unsigned Y = 0u; Y < Resolution; ++Y)
                {
                    for (unsigned X = 0u; X < Resolution; ++X)
                    {
                        const int        WX = NewOrigin[0] + int(X), WY = NewOrigin[1] + int(Y), WZ = NewOrigin[2] + int(Z);
                        const ClipVector P{float(WX) * Grid.Spacing, float(WY) * Grid.Spacing, float(WZ) * Grid.Spacing};
                        if (Force || !Grid.Initialized || !Contains(OldOrigin, WX, WY, WZ) ||
                            (Moved && (Influenced(P, Pose) || Influenced(P, PreviousPose))))
                        {
                            Grid.Texels[Address(WX, WY, WZ)] = Evaluate(P, Pose);
                            ++LocalRewritten;
                        }
                    }
                }
                if (LocalRewritten > 0u) RewrittenCount.fetch_add(LocalRewritten, std::memory_order_relaxed);
            });
            Rewritten[Tier]  = RewrittenCount.load();
            Grid.Origin      = NewOrigin;
            Grid.Initialized = true;
        }
        PreviousPose        = Pose;
        PreviousInitialized = true;
    }

    [[nodiscard]] DistanceInterval Sample(ClipVector P, unsigned* SelectedTier = nullptr) const noexcept
    {
        for (unsigned Tier = 0u; Tier < LevelCount; ++Tier)
        {
            const auto&      Grid = Levels[Tier];
            if (!Grid.Initialized || Grid.Texels.empty()) continue;
            const ClipVector Q = P / Grid.Spacing;
            const int        X = int(std::floor(Q.X)), Y = int(std::floor(Q.Y)), Z = int(std::floor(Q.Z));
            if (!Contains(Grid.Origin, X, Y, Z) || !Contains(Grid.Origin, X + 1, Y + 1, Z + 1)) continue;
            if (SelectedTier) *SelectedTier = Tier;
            const ClipVector Fraction{Q.X - float(X), Q.Y - float(Y), Q.Z - float(Z)};
            float            Raw = 0.0f, Lower = 0.0f;
            for (unsigned DZ = 0u; DZ < 2u; ++DZ)
            {
                for (unsigned DY = 0u; DY < 2u; ++DY)
                {
                    for (unsigned DX = 0u; DX < 2u; ++DX)
                    {
                        const auto& Texel  = Grid.Texels[Address(X + int(DX), Y + int(DY), Z + int(DZ))];
                        const float Weight = (DX ? Fraction.X : 1.0f - Fraction.X) *
                                             (DY ? Fraction.Y : 1.0f - Fraction.Y) *
                                             (DZ ? Fraction.Z : 1.0f - Fraction.Z);
                        const float Correction = Length(ClipVector{float(DX) - Fraction.X, float(DY) - Fraction.Y, float(DZ) - Fraction.Z}) *
                                                 Grid.Spacing;
                        Raw += Weight * Texel.Raw;
                        Lower += Weight * (Texel.Lower - Correction);
                    }
                }
            }
            return {Raw, std::max(0.0f, Lower - 2e-5f)};
        }
        if (SelectedTier) *SelectedTier = UINT32_MAX;
        return {0.0f, 0.0f};
    }
};

} // namespace Frontier::DistanceFieldSpace
