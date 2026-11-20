/*============================================================================================================================================
                                            LIGHTINGCANDIDATEBENCH.CPP
============================================================================================================================================*/
// 🧪 Standalone measurement harness for the World GI / Solar Shadow / Deterministic Path Trace selection.
//    100% real triangle geometry: no 3D voxel SDF, no capsule distance proxies, no analytic hull primitives.
//    Candidates measured against a deterministic path-traced reference on identical primary samples.
//      Shadows : ContactPcss | WideTreeVogelDisc | SurfelBlockerField
//      GI      : Sh9EmissiveFormFactor | SurfelCascadeRadiosity
//      Ref     : DeterministicSobolPathTrace
//    Outputs : Exhibits-style PNG sheets, CSV rows, JSON summary, and repeat-hash determinism proof.

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   VECTOR ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

constexpr float PiFloat      = 3.14159265358979323846f;
constexpr float TauFloat     = 6.28318530717958647692f;
constexpr float GoldenAngle  = 2.39996322972865332223f;

struct V3
{
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
};

[[nodiscard]] inline V3 operator+(const V3& A, const V3& B) noexcept { return { A.X + B.X, A.Y + B.Y, A.Z + B.Z }; }
[[nodiscard]] inline V3 operator-(const V3& A, const V3& B) noexcept { return { A.X - B.X, A.Y - B.Y, A.Z - B.Z }; }
[[nodiscard]] inline V3 operator*(const V3& A, float S) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
[[nodiscard]] inline V3 operator*(float S, const V3& A) noexcept { return { A.X * S, A.Y * S, A.Z * S }; }
[[nodiscard]] inline V3 operator/(const V3& A, float S) noexcept { return { A.X / S, A.Y / S, A.Z / S }; }
inline V3& operator+=(V3& A, const V3& B) noexcept { A.X += B.X; A.Y += B.Y; A.Z += B.Z; return A; }

[[nodiscard]] inline float Dot(const V3& A, const V3& B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
[[nodiscard]] inline V3 Cross(const V3& A, const V3& B) noexcept
{
    return { A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X };
}
[[nodiscard]] inline float Length(const V3& A) noexcept { return std::sqrt(Dot(A, A)); }
[[nodiscard]] inline V3 Normalize(const V3& A, const V3& Fallback = { 0.0f, 0.0f, 1.0f }) noexcept
{
    const float L = Length(A);
    return (L > 1.0e-8f) ? A / L : Fallback;
}
[[nodiscard]] inline float Saturate(float V) noexcept { return std::clamp(V, 0.0f, 1.0f); }
[[nodiscard]] inline float SmoothHermite(float Edge0, float Edge1, float V) noexcept
{
    const float T = Saturate((V - Edge0) / std::max(1.0e-8f, Edge1 - Edge0));
    return T * T * (3.0f - 2.0f * T);
}
[[nodiscard]] inline V3 ScaleColor(const V3& A, const V3& B) noexcept { return { A.X * B.X, A.Y * B.Y, A.Z * B.Z }; }

//------------------------------------------------------------------------------------------------------------------------
//                                              TRIANGLE GEOMETRY & BOUNDS
//------------------------------------------------------------------------------------------------------------------------

struct Tri
{
    V3      A       {};
    V3      B       {};
    V3      C       {};
    V3      Albedo  { 0.6f, 0.6f, 0.6f };
    V3      Emission{ 0.0f, 0.0f, 0.0f };
    uint8_t Kind    = 0u; // 0 = diffuse surface, 1 = emissive emitter
};

struct Aabb
{
    V3 Min {  1.0e30f,  1.0e30f,  1.0e30f };
    V3 Max { -1.0e30f, -1.0e30f, -1.0e30f };
    void Expand(const V3& P) noexcept
    {
        Min.X = std::min(Min.X, P.X); Min.Y = std::min(Min.Y, P.Y); Min.Z = std::min(Min.Z, P.Z);
        Max.X = std::max(Max.X, P.X); Max.Y = std::max(Max.Y, P.Y); Max.Z = std::max(Max.Z, P.Z);
    }
    void Expand(const Aabb& Box) noexcept
    {
        Min.X = std::min(Min.X, Box.Min.X); Min.Y = std::min(Min.Y, Box.Min.Y); Min.Z = std::min(Min.Z, Box.Min.Z);
        Max.X = std::max(Max.X, Box.Max.X); Max.Y = std::max(Max.Y, Box.Max.Y); Max.Z = std::max(Max.Z, Box.Max.Z);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                          WIDE-BOUNDING TREE (SAH BINARY BOUNDS)
//------------------------------------------------------------------------------------------------------------------------

struct TraceHit
{
    bool  Hit       = false;
    float Distance  = 0.0f;
    float U         = 0.0f;
    float V         = 0.0f;
    V3    Normal    {};
    uint32_t TriIndex = 0u;
};

class TriangleTree
{
public:
    void Build(const std::vector<Tri>& Source)
    {
        Triangles = Source;
        Order.resize(Triangles.size());
        for (uint32_t I = 0u; I < static_cast<uint32_t>(Order.size()); ++I)
        {
            Order[I] = I;
        }
        Nodes.clear();
        Nodes.reserve(Triangles.size() * 2u + 8u);
        if (!Triangles.empty())
        {
            (void)BuildRecursive(0u, static_cast<uint32_t>(Triangles.size()));
        }
    }

    [[nodiscard]] bool AnyOccluded(const V3& Origin, const V3& UnitDirection, float TMin, float TMax) const noexcept
    {
        if (Nodes.empty())
        {
            return false;
        }
        std::array<uint32_t, 96> Stack {};
        uint32_t Depth = 0u;
        Stack[Depth++] = 0u;
        while (Depth > 0u)
        {
            const BvhNode& Node = Nodes[Stack[--Depth]];
            if (!SlabOverlaps(Node.Bounds, Origin, UnitDirection, TMin, TMax))
            {
                continue;
            }
            if (Node.Count != 0u)
            {
                for (uint32_t I = 0u; I < Node.Count; ++I)
                {
                    float HitT = 0.0f;
                    float HitU = 0.0f;
                    float HitV = 0.0f;
                    if (IntersectTriangle(Triangles[Order[Node.Offset + I]], Origin, UnitDirection, TMin, TMax, HitT, HitU, HitV))
                    {
                        return true;
                    }
                }
                continue;
            }
            Stack[Depth++] = Node.Left;
            Stack[Depth++] = Node.Right;
        }
        return false;
    }

    [[nodiscard]] TraceHit TraceClosest(const V3& Origin, const V3& UnitDirection, float TMin, float TMax) const noexcept
    {
        TraceHit Best {};
        if (Nodes.empty())
        {
            return Best;
        }
        Best.Distance = TMax;
        std::array<uint32_t, 96> Stack {};
        uint32_t Depth = 0u;
        Stack[Depth++] = 0u;
        while (Depth > 0u)
        {
            const BvhNode& Node = Nodes[Stack[--Depth]];
            if (!SlabOverlaps(Node.Bounds, Origin, UnitDirection, TMin, Best.Distance))
            {
                continue;
            }
            if (Node.Count != 0u)
            {
                for (uint32_t I = 0u; I < Node.Count; ++I)
                {
                    const uint32_t Idx = Order[Node.Offset + I];
                    float HitT = 0.0f;
                    float HitU = 0.0f;
                    float HitV = 0.0f;
                    if (IntersectTriangle(Triangles[Idx], Origin, UnitDirection, TMin, Best.Distance, HitT, HitU, HitV))
                    {
                        Best.Hit      = true;
                        Best.Distance = HitT;
                        Best.U        = HitU;
                        Best.V        = HitV;
                        Best.TriIndex = Idx;
                    }
                }
                continue;
            }
            Stack[Depth++] = Node.Left;
            Stack[Depth++] = Node.Right;
        }
        if (Best.Hit)
        {
            const Tri& T = Triangles[Best.TriIndex];
            Best.Normal = Normalize(Cross(T.B - T.A, T.C - T.A));
        }
        return Best;
    }

    [[nodiscard]] const std::vector<Tri>& SourceTriangles() const noexcept { return Triangles; }
    [[nodiscard]] std::size_t TriangleCount() const noexcept { return Triangles.size(); }
    [[nodiscard]] std::size_t NodeCount() const noexcept { return Nodes.size(); }

private:
    struct BvhNode
    {
        Aabb     Bounds {};
        uint32_t Offset = 0u;
        uint32_t Count  = 0u;
        uint32_t Left   = 0u;
        uint32_t Right  = 0u;
    };

    [[nodiscard]] static bool SlabOverlaps(const Aabb& Box, const V3& O, const V3& D, float TMin, float TMax) noexcept
    {
        const float InvX = 1.0f / ((std::fabs(D.X) > 1.0e-9f) ? D.X : 1.0e-9f);
        const float InvY = 1.0f / ((std::fabs(D.Y) > 1.0e-9f) ? D.Y : 1.0e-9f);
        const float InvZ = 1.0f / ((std::fabs(D.Z) > 1.0e-9f) ? D.Z : 1.0e-9f);
        const float TX0 = (Box.Min.X - O.X) * InvX;
        const float TX1 = (Box.Max.X - O.X) * InvX;
        const float TY0 = (Box.Min.Y - O.Y) * InvY;
        const float TY1 = (Box.Max.Y - O.Y) * InvY;
        const float TZ0 = (Box.Min.Z - O.Z) * InvZ;
        const float TZ1 = (Box.Max.Z - O.Z) * InvZ;
        const float Near = std::max({ std::min(TX0, TX1), std::min(TY0, TY1), std::min(TZ0, TZ1), TMin });
        const float Far  = std::min({ std::max(TX0, TX1), std::max(TY0, TY1), std::max(TZ0, TZ1), TMax });
        return Near <= Far;
    }

    [[nodiscard]] static bool IntersectTriangle(
        const Tri& T, const V3& O, const V3& D, float TMin, float TMax, float& OutT, float& OutU, float& OutV) noexcept
    {
        const V3 E1 = T.B - T.A;
        const V3 E2 = T.C - T.A;
        const V3 P  = Cross(D, E2);
        const float Det = Dot(E1, P);
        if (std::fabs(Det) < 1.0e-12f)
        {
            return false;
        }
        const float InvDet = 1.0f / Det;
        const V3 S = O - T.A;
        const float U = Dot(S, P) * InvDet;
        if (U < 0.0f || U > 1.0f)
        {
            return false;
        }
        const V3 Q = Cross(S, E1);
        const float V = Dot(D, Q) * InvDet;
        if (V < 0.0f || U + V > 1.0f)
        {
            return false;
        }
        const float T1 = Dot(E2, Q) * InvDet;
        if (T1 < TMin || T1 > TMax)
        {
            return false;
        }
        OutT = T1;
        OutU = U;
        OutV = V;
        return true;
    }

    [[nodiscard]] Aabb ComputeBounds(uint32_t Start, uint32_t Count) const noexcept
    {
        Aabb Box {};
        for (uint32_t I = 0u; I < Count; ++I)
        {
            const Tri& T = Triangles[Order[Start + I]];
            Box.Expand(T.A);
            Box.Expand(T.B);
            Box.Expand(T.C);
        }
        return Box;
    }

    [[nodiscard]] uint32_t BuildRecursive(uint32_t Start, uint32_t Count)
    {
        const uint32_t NodeIndex = static_cast<uint32_t>(Nodes.size());
        Nodes.push_back({});
        Nodes[NodeIndex].Bounds = ComputeBounds(Start, Count);
        if (Count <= 4u)
        {
            Nodes[NodeIndex].Count  = Count;
            Nodes[NodeIndex].Offset = Start;
            return NodeIndex;
        }

        // 12-bin binned SAH across all three axes with fixed deterministic tie-breaking
        constexpr uint32_t BinCount = 12u;
        float BestCost = 1.0e30f;
        int   BestAxis = 0;
        int   BestSplit = 0;
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            float Lo = 1.0e30f;
            float Hi = -1.0e30f;
            for (uint32_t I = 0u; I < Count; ++I)
            {
                const Tri& T = Triangles[Order[Start + I]];
                const float C = (Axis == 0) ? (T.A.X + T.B.X + T.C.X) : (Axis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                Lo = std::min(Lo, C);
                Hi = std::max(Hi, C);
            }
            const float Span = Hi - Lo;
            if (Span < 1.0e-9f)
            {
                continue;
            }
            std::array<Aabb, BinCount> BinBounds {};
            std::array<uint32_t, BinCount> BinCounts {};
            for (uint32_t I = 0u; I < Count; ++I)
            {
                const Tri& T = Triangles[Order[Start + I]];
                const float C = (Axis == 0) ? (T.A.X + T.B.X + T.C.X) : (Axis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                const int Bin = std::clamp(static_cast<int>((C - Lo) / Span * static_cast<float>(BinCount)), 0, static_cast<int>(BinCount) - 1);
                BinBounds[Bin].Expand(T.A);
                BinBounds[Bin].Expand(T.B);
                BinBounds[Bin].Expand(T.C);
                BinCounts[Bin] += 1u;
            }
            auto SurfaceArea = [](const Aabb& B) noexcept
            {
                const V3 E = B.Max - B.Min;
                if (E.X < 0.0f || E.Y < 0.0f || E.Z < 0.0f)
                {
                    return 0.0f;
                }
                return 2.0f * (E.X * E.Y + E.Y * E.Z + E.Z * E.X);
            };
            for (uint32_t Split = 1u; Split < BinCount; ++Split)
            {
                Aabb LeftBox {};
                Aabb RightBox {};
                uint32_t LeftN = 0u;
                uint32_t RightN = 0u;
                for (uint32_t B = 0u; B < Split; ++B)
                {
                    if (BinCounts[B] != 0u)
                    {
                        LeftBox.Expand(BinBounds[B]);
                        LeftN += BinCounts[B];
                    }
                }
                for (uint32_t B = Split; B < BinCount; ++B)
                {
                    if (BinCounts[B] != 0u)
                    {
                        RightBox.Expand(BinBounds[B]);
                        RightN += BinCounts[B];
                    }
                }
                if (LeftN == 0u || RightN == 0u)
                {
                    continue;
                }
                const float Cost = SurfaceArea(LeftBox) * static_cast<float>(LeftN) + SurfaceArea(RightBox) * static_cast<float>(RightN);
                if (Cost < BestCost)
                {
                    BestCost  = Cost;
                    BestAxis  = Axis;
                    BestSplit = static_cast<int>(Split);
                }
            }
        }

        uint32_t LeftCount = 0u;
        if (BestCost < 1.0e30f)
        {
            const float Lo = [&]()
            {
                float Prev = 1.0e30f;
                for (uint32_t I = 0u; I < Count; ++I)
                {
                    const Tri& T = Triangles[Order[Start + I]];
                    const float C = (BestAxis == 0) ? (T.A.X + T.B.X + T.C.X) : (BestAxis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                    Prev = std::min(Prev, C);
                }
                return Prev;
            }();
            float Hi = -1.0e30f;
            for (uint32_t I = 0u; I < Count; ++I)
            {
                const Tri& T = Triangles[Order[Start + I]];
                const float C = (BestAxis == 0) ? (T.A.X + T.B.X + T.C.X) : (BestAxis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                Hi = std::max(Hi, C);
            }
            const float Span = std::max(1.0e-9f, Hi - Lo);
            auto BinOf = [&](uint32_t OrderedIndex) noexcept
            {
                const Tri& T = Triangles[Order[OrderedIndex]];
                const float C = (BestAxis == 0) ? (T.A.X + T.B.X + T.C.X) : (BestAxis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                return std::clamp(static_cast<int>((C - Lo) / Span * static_cast<float>(BinCount)), 0, static_cast<int>(BinCount) - 1);
            };
            const auto Mid = std::partition(Order.begin() + Start, Order.begin() + Start + Count, [&](uint32_t Idx)
            {
                const Tri& T = Triangles[Idx];
                const float C = (BestAxis == 0) ? (T.A.X + T.B.X + T.C.X) : (BestAxis == 1) ? (T.A.Y + T.B.Y + T.C.Y) : (T.A.Z + T.B.Z + T.C.Z);
                const int Bin = std::clamp(static_cast<int>((C - Lo) / Span * static_cast<float>(BinCount)), 0, static_cast<int>(BinCount) - 1);
                return Bin < BestSplit;
            });
            LeftCount = static_cast<uint32_t>(std::distance(Order.begin() + Start, Mid));
            (void)BinOf;
        }
        if (LeftCount == 0u || LeftCount == Count)
        {
            LeftCount = Count / 2u;
            std::nth_element(
                Order.begin() + Start, Order.begin() + Start + LeftCount, Order.begin() + Start + Count,
                [&](uint32_t Lh, uint32_t Rh)
                {
                    const Tri& TL = Triangles[Lh];
                    const Tri& TR = Triangles[Rh];
                    const float CL = TL.A.X + TL.B.X + TL.C.X;
                    const float CR = TR.A.X + TR.B.X + TR.C.X;
                    return CL < CR;
                });
        }

        const uint32_t Left  = BuildRecursive(Start, LeftCount);
        const uint32_t Right = BuildRecursive(Start + LeftCount, Count - LeftCount);
        Nodes[NodeIndex].Count = 0u;
        Nodes[NodeIndex].Left  = Left;
        Nodes[NodeIndex].Right = Right;
        return NodeIndex;
    }

    std::vector<Tri>      Triangles {};
    std::vector<BvhNode>  Nodes     {};
    std::vector<uint32_t> Order     {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                   REAL TRIANGLE SCENE: STUDIO, VEHICLE, DEFORMING FIGURE
//------------------------------------------------------------------------------------------------------------------------

struct StudioSpec
{
    V3    SolarDirection { -0.46f, 0.56f, 0.68f };
    V3    SolarIrradiance { 3.20f, 3.05f, 2.85f };
    float TanSolarRadius = 0.048f;
    V3    EmitterRadiance { 11.0f, 8.6f, 6.4f };
};

inline void AppendQuad(std::vector<Tri>& Out, const V3& P0, const V3& P1, const V3& P2, const V3& P3, const V3& Albedo, const V3& Emission = {})
{
    Tri T0 {};
    T0.A = P0; T0.B = P1; T0.C = P2;
    T0.Albedo = Albedo; T0.Emission = Emission; T0.Kind = (Emission.X + Emission.Y + Emission.Z > 0.0f) ? 1u : 0u;
    Tri T1 {};
    T1.A = P0; T1.B = P2; T1.C = P3;
    T1.Albedo = Albedo; T1.Emission = Emission; T1.Kind = T0.Kind;
    Out.push_back(T0);
    Out.push_back(T1);
}

inline void AppendBox(std::vector<Tri>& Out, const V3& Center, const V3& HalfExtent, const V3& Albedo)
{
    const V3 L { Center.X - HalfExtent.X, Center.Y - HalfExtent.Y, Center.Z - HalfExtent.Z };
    const V3 H { Center.X + HalfExtent.X, Center.Y + HalfExtent.Y, Center.Z + HalfExtent.Z };
    AppendQuad(Out, { L.X, L.Y, H.Z }, { H.X, L.Y, H.Z }, { H.X, H.Y, H.Z }, { L.X, H.Y, H.Z }, Albedo); // top
    AppendQuad(Out, { L.X, L.Y, L.Z }, { L.X, H.Y, L.Z }, { H.X, H.Y, L.Z }, { H.X, L.Y, L.Z }, Albedo); // bottom
    AppendQuad(Out, { L.X, H.Y, L.Z }, { L.X, H.Y, H.Z }, { H.X, H.Y, H.Z }, { H.X, H.Y, L.Z }, Albedo); // +Y
    AppendQuad(Out, { L.X, L.Y, L.Z }, { H.X, L.Y, L.Z }, { H.X, L.Y, H.Z }, { L.X, L.Y, H.Z }, Albedo); // -Y
    AppendQuad(Out, { L.X, L.Y, L.Z }, { L.X, L.Y, H.Z }, { L.X, H.Y, H.Z }, { L.X, H.Y, L.Z }, Albedo); // -X
    AppendQuad(Out, { H.X, L.Y, L.Z }, { H.X, H.Y, L.Z }, { H.X, H.Y, H.Z }, { H.X, L.Y, H.Z }, Albedo); // +X
}

inline void AppendTaperedLimb(std::vector<Tri>& Out, const V3& JointA, const V3& JointB, float RadiusA, float RadiusB, const V3& Albedo, uint32_t Sides = 8u)
{
    const V3 Axis = JointB - JointA;
    const float AxisLength = Length(Axis);
    if (AxisLength <= 1.0e-5f)
    {
        return;
    }
    const V3 Dir = Axis / AxisLength;
    const V3 Helper = (std::fabs(Dir.Z) < 0.9f) ? V3 { 0.0f, 0.0f, 1.0f } : V3 { 1.0f, 0.0f, 0.0f };
    const V3 TanU = Normalize(Cross(Helper, Dir));
    const V3 BitV = Normalize(Cross(Dir, TanU));

    std::vector<V3> RingA {};
    std::vector<V3> RingB {};
    RingA.reserve(Sides);
    RingB.reserve(Sides);
    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const float Angle = TauFloat * static_cast<float>(S) / static_cast<float>(Sides);
        const V3 Offset = (TanU * std::cos(Angle) + BitV * std::sin(Angle));
        RingA.push_back(JointA + Offset * RadiusA);
        RingB.push_back(JointB + Offset * RadiusB);
    }
    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const uint32_t Next = (S + 1u) % Sides;
        AppendQuad(Out, RingA[S], RingA[Next], RingB[Next], RingB[S], Albedo);
    }
    const V3 CapA = JointA - Dir * RadiusA;
    const V3 CapB = JointB + Dir * RadiusB;
    for (uint32_t S = 0u; S < Sides; ++S)
    {
        const uint32_t Next = (S + 1u) % Sides;
        Tri TipA {};
        TipA.A = CapA; TipA.B = RingA[S]; TipA.C = RingA[Next]; TipA.Albedo = Albedo;
        Tri TipB {};
        TipB.A = CapB; TipB.B = RingB[Next]; TipB.C = RingB[S]; TipB.Albedo = Albedo;
        Out.push_back(TipA);
        Out.push_back(TipB);
    }
}

struct StudioWorld
{
    std::vector<Tri>       Triangles {};
    std::vector<Tri>       Emitters  {};
    TriangleTree           Tree      {};
    V3                     CharacterRoot { 1.05f, -2.05f, 0.0f };
};

// Deterministic articulated pose -> real tapered limb triangles (no capsule distance proxy anywhere)
inline void AppendFigurePose(std::vector<Tri>& Out, const V3& Root, float PosePhase, const V3& Suit, const V3& Accent)
{
    const float LeanX   = 0.06f + 0.16f * PosePhase;
    const float StrideL = -0.22f + 0.56f * PosePhase;
    const float StrideR =  0.22f - 0.52f * PosePhase;
    const float ArmLift = 1.62f + 0.34f * (1.0f - PosePhase);
    const float ArmReach = 0.14f + 0.44f * PosePhase;
    const float Crouch  = -0.04f * PosePhase;

    const V3 Pelvis { Root.X + LeanX * 0.2f, Root.Y, Root.Z + 0.92f + Crouch };
    const V3 Chest  { Root.X + LeanX * 0.7f, Root.Y, Root.Z + 1.38f + Crouch };
    const V3 Head   { Root.X + LeanX * 1.1f, Root.Y, Root.Z + 1.68f + Crouch };
    const V3 LShoulder { Chest.X, Chest.Y - 0.24f, Chest.Z - 0.03f };
    const V3 LElbow { LShoulder.X + ArmReach * 0.6f, LShoulder.Y - 0.26f, 1.38f + (ArmLift - 1.62f) * 0.5f };
    const V3 LHand  { LElbow.X + ArmReach * 0.7f, LElbow.Y - 0.16f, ArmLift };
    const V3 RShoulder { Chest.X, Chest.Y + 0.24f, Chest.Z - 0.03f };
    const V3 RElbow { RShoulder.X - 0.16f * PosePhase, RShoulder.Y + 0.16f, 1.18f - 0.10f * PosePhase };
    const V3 RHand  { RElbow.X + 0.12f * (1.0f - PosePhase), RElbow.Y + 0.08f, 0.96f + 0.14f * PosePhase };
    const V3 LHip  { Pelvis.X, Pelvis.Y - 0.12f, Pelvis.Z - 0.04f };
    const V3 LKnee { LHip.X + StrideL * 0.55f, LHip.Y - 0.02f, 0.48f + 0.08f * PosePhase };
    const V3 LFoot { Root.X + StrideL, LHip.Y - 0.02f, 0.06f };
    const V3 RHip  { Pelvis.X, Pelvis.Y + 0.12f, Pelvis.Z - 0.04f };
    const V3 RKnee { RHip.X + StrideR * 0.62f, RHip.Y + 0.02f, 0.48f + 0.14f * PosePhase };
    const V3 RFoot { Root.X + StrideR, RHip.Y + 0.02f, 0.06f + 0.12f * PosePhase };

    AppendTaperedLimb(Out, Pelvis, Chest, 0.15f, 0.19f, Suit);
    AppendTaperedLimb(Out, Chest, Head, 0.11f, 0.13f, Accent);
    AppendTaperedLimb(Out, LShoulder, LElbow, 0.075f, 0.065f, Suit);
    AppendTaperedLimb(Out, LElbow, LHand, 0.062f, 0.052f, Accent);
    AppendTaperedLimb(Out, RShoulder, RElbow, 0.075f, 0.065f, Suit);
    AppendTaperedLimb(Out, RElbow, RHand, 0.062f, 0.052f, Accent);
    AppendTaperedLimb(Out, LHip, LKnee, 0.095f, 0.078f, Suit);
    AppendTaperedLimb(Out, LKnee, LFoot, 0.074f, 0.058f, Suit);
    AppendTaperedLimb(Out, RHip, RKnee, 0.095f, 0.078f, Suit);
    AppendTaperedLimb(Out, RKnee, RFoot, 0.074f, 0.058f, Suit);
}

inline void AppendVehicle(std::vector<Tri>& Out, const V3& Offset)
{
    const V3 Coat { 0.08f, 0.32f, 0.84f };
    const V3 Dark { 0.06f, 0.065f, 0.07f };
    const V3 Rim  { 0.72f, 0.74f, 0.77f };
    AppendBox(Out, Offset + V3 { 0.0f, 0.0f, 0.52f }, { 2.10f, 0.82f, 0.30f }, Coat);
    AppendBox(Out, Offset + V3 { -0.15f, 0.0f, 0.86f }, { 1.05f, 0.70f, 0.20f }, Coat);
    for (int Wheel = 0; Wheel < 4; ++Wheel)
    {
        const float WX = (Wheel < 2) ? 1.35f : -1.35f;
        const float WY = ((Wheel % 2) == 0) ? -0.80f : 0.80f;
        const V3 Center = Offset + V3 { WX, WY, 0.30f };
        AppendTaperedLimb(Out, Center - V3 { 0.0f, 0.09f, 0.0f }, Center + V3 { 0.0f, 0.09f, 0.0f }, 0.30f, 0.30f, Dark, 10u);
        AppendTaperedLimb(Out, Center - V3 { 0.0f, 0.10f, 0.0f }, Center + V3 { 0.0f, 0.10f, 0.0f }, 0.17f, 0.17f, Rim, 8u);
    }
}

inline void AppendStudioBase(std::vector<Tri>& Out, std::vector<Tri>& Emitters, const StudioSpec& Spec)
{
    const V3 Floor { 0.64f, 0.63f, 0.60f };
    const V3 Crimson { 0.85f, 0.08f, 0.07f };
    const V3 Cobalt { 0.16f, 0.34f, 0.86f };
    const V3 Plaster { 0.72f, 0.72f, 0.70f };

    AppendBox(Out, { 0.0f, 0.0f, -0.25f }, { 4.0f, 4.0f, 0.25f }, Floor);   // floor slab
    AppendBox(Out, { -4.25f, 0.0f, 1.6f }, { 0.25f, 4.0f, 1.85f }, Crimson); // left wall
    AppendBox(Out, { 0.0f, 4.25f, 1.6f }, { 4.0f, 0.25f, 1.85f }, Plaster);  // back wall
    AppendBox(Out, { -2.30f, 2.55f, 1.10f }, { 0.30f, 0.30f, 1.10f }, Cobalt);  // pillar A
    AppendBox(Out, { -1.55f, 2.90f, 0.85f }, { 0.26f, 0.26f, 0.85f }, Plaster); // pillar B

    // Emissive studio panel on the back wall
    const V3 EmitterAlbedo { 0.9f, 0.9f, 0.9f };
    const float EY = 4.0f;
    const std::size_t Before = Out.size();
    AppendQuad(Out,
               { -1.35f, EY - 0.02f, 1.35f },
               {  1.35f, EY - 0.02f, 1.35f },
               {  1.35f, EY - 0.02f, 2.55f },
               { -1.35f, EY - 0.02f, 2.55f },
               EmitterAlbedo, Spec.EmitterRadiance);
    for (std::size_t I = Before; I < Out.size(); ++I)
    {
        Emitters.push_back(Out[I]);
    }
}

inline StudioWorld ConstructStudio(const StudioSpec& Spec, float PosePhase, const V3& CharacterRoot)
{
    StudioWorld World {};
    World.CharacterRoot = CharacterRoot;
    AppendStudioBase(World.Triangles, World.Emitters, Spec);
    AppendVehicle(World.Triangles, { -0.85f, 0.05f, 0.0f });
    AppendFigurePose(World.Triangles, CharacterRoot, PosePhase, { 0.68f, 0.70f, 0.74f }, { 0.88f, 0.52f, 0.16f });
    World.Tree.Build(World.Triangles);
    return World;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  CAMERA & PRIMARY SAMPLES
//------------------------------------------------------------------------------------------------------------------------

struct CameraBasis
{
    V3    Origin     { 4.35f, -4.75f, 2.35f };
    V3    Forward    {};
    V3    Right      {};
    V3    Up         {};
    float TanHalfFov = 0.42f;
    float Aspect     = 16.0f / 9.0f;
};

inline CameraBasis ConstructCamera(uint32_t Width, uint32_t Height)
{
    CameraBasis Basis {};
    const V3 LookAt { -0.15f, -0.55f, 0.70f };
    Basis.Forward = Normalize(LookAt - Basis.Origin);
    Basis.Right   = Normalize(Cross(Basis.Forward, { 0.0f, 0.0f, 1.0f }));
    Basis.Up      = Normalize(Cross(Basis.Right, Basis.Forward));
    Basis.TanHalfFov = std::tan(23.0f * PiFloat / 180.0f);
    Basis.Aspect = static_cast<float>(Width) / static_cast<float>(Height);
    return Basis;
}

struct SurfaceSample
{
    bool     Valid    = false;
    V3       Position {};
    V3       Normal   {};
    V3       Albedo   { 0.6f, 0.6f, 0.6f };
    V3       Emitted  {};
    uint32_t TriIndex = 0u;
};

inline std::vector<SurfaceSample> TracePrimary(const StudioWorld& World, const CameraBasis& Cam, uint32_t Width, uint32_t Height)
{
    std::vector<SurfaceSample> Buffer(static_cast<std::size_t>(Width) * Height);
    for (uint32_t Y = 0u; Y < Height; ++Y)
    {
        const float NdcY = 1.0f - 2.0f * ((static_cast<float>(Y) + 0.5f) / static_cast<float>(Height));
        for (uint32_t X = 0u; X < Width; ++X)
        {
            const float NdcX = 2.0f * ((static_cast<float>(X) + 0.5f) / static_cast<float>(Width)) - 1.0f;
            const V3 Dir = Normalize(Cam.Forward + Cam.Right * (NdcX * Cam.TanHalfFov * Cam.Aspect) + Cam.Up * (NdcY * Cam.TanHalfFov));
            const TraceHit Hit = World.Tree.TraceClosest(Cam.Origin, Dir, 1.0e-3f, 60.0f);
            if (!Hit.Hit)
            {
                continue;
            }
            SurfaceSample& S = Buffer[static_cast<std::size_t>(Y) * Width + X];
            S.Valid    = true;
            S.Position = Cam.Origin + Dir * Hit.Distance;
            S.Normal   = (Dot(Hit.Normal, Dir) > 0.0f) ? Hit.Normal * -1.0f : Hit.Normal;
            S.TriIndex = Hit.TriIndex;
        }
    }
    return Buffer;
}

//------------------------------------------------------------------------------------------------------------------------
//                                              SOLAR SHADOW CANDIDATE 1 : CONTACT PCSS
//------------------------------------------------------------------------------------------------------------------------

struct ShadowCascade
{
    V3                    CenterWorld {};
    V3                    RightAxis   { 1.0f, 0.0f, 0.0f };
    V3                    UpAxis      { 0.0f, 1.0f, 0.0f };
    V3                    LightAxis   { 0.0f, 0.0f, -1.0f };
    float                 HalfSpan    = 4.2f;
    uint32_t              Resolution  = 1024u;
    std::vector<float>    Depth       {};
};

inline ShadowCascade BuildShadowCascade(const TriangleTree& Tree, const V3& SunDirection, const V3& Focus, float HalfSpan, uint32_t Resolution)
{
    ShadowCascade Cascade {};
    Cascade.CenterWorld = Focus;
    Cascade.HalfSpan    = HalfSpan;
    Cascade.Resolution  = Resolution;
    Cascade.LightAxis   = Normalize(SunDirection);
    Cascade.RightAxis   = Normalize(Cross(Cascade.LightAxis, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
    Cascade.UpAxis      = Normalize(Cross(Cascade.RightAxis, Cascade.LightAxis), { 0.0f, 1.0f, 0.0f });
    Cascade.Depth.assign(static_cast<std::size_t>(Resolution) * Resolution, -1.0e30f);

    const float TexelScale = static_cast<float>(Resolution) / (2.0f * HalfSpan);
    for (std::size_t I = 0u; I < Tree.SourceTriangles().size(); ++I)
    {
        const Tri& T = Tree.SourceTriangles()[I];
        const V3 Corners[3] = { T.A, T.B, T.C };
        float SU[3] {};
        float SV[3] {};
        float SD[3] {};
        for (int K = 0; K < 3; ++K)
        {
            const V3 Rel = Corners[K] - Cascade.CenterWorld;
            SU[K] = Dot(Rel, Cascade.RightAxis) * TexelScale + static_cast<float>(Resolution) * 0.5f;
            SV[K] = Dot(Rel, Cascade.UpAxis) * TexelScale + static_cast<float>(Resolution) * 0.5f;
            SD[K] = Dot(Rel, Cascade.LightAxis);
        }
        const int MinX = std::max(0, static_cast<int>(std::floor(std::min({ SU[0], SU[1], SU[2] }))));
        const int MaxX = std::min(static_cast<int>(Resolution) - 1, static_cast<int>(std::ceil(std::max({ SU[0], SU[1], SU[2] }))));
        const int MinY = std::max(0, static_cast<int>(std::floor(std::min({ SV[0], SV[1], SV[2] }))));
        const int MaxY = std::min(static_cast<int>(Resolution) - 1, static_cast<int>(std::ceil(std::max({ SV[0], SV[1], SV[2] }))));
        if (MinX > MaxX || MinY > MaxY)
        {
            continue;
        }
        const float Area2 = (SU[1] - SU[0]) * (SV[2] - SV[0]) - (SU[2] - SU[0]) * (SV[1] - SV[0]);
        if (std::fabs(Area2) < 1.0e-9f)
        {
            continue;
        }
        const float InvArea2 = 1.0f / Area2;
        for (int PY = MinY; PY <= MaxY; ++PY)
        {
            const float SampleY = static_cast<float>(PY) + 0.5f;
            for (int PX = MinX; PX <= MaxX; ++PX)
            {
                const float SampleX = static_cast<float>(PX) + 0.5f;
                const float W0 = ((SU[1] - SampleX) * (SV[2] - SampleY) - (SU[2] - SampleX) * (SV[1] - SampleY)) * InvArea2;
                const float W1 = ((SU[2] - SampleX) * (SV[0] - SampleY) - (SU[0] - SampleX) * (SV[2] - SampleY)) * InvArea2;
                const float W2 = 1.0f - W0 - W1;
                if (W0 < -1.0e-4f || W1 < -1.0e-4f || W2 < -1.0e-4f)
                {
                    continue;
                }
                const float Depth = W0 * SD[0] + W1 * SD[1] + W2 * SD[2];
                float& Cell = Cascade.Depth[static_cast<std::size_t>(PY) * Resolution + static_cast<std::size_t>(PX)];
                Cell = std::max(Cell, Depth);
            }
        }
    }
    return Cascade;
}

inline V3 VogelDiscSample(uint32_t Index, uint32_t Count) noexcept
{
    const float Radius = std::sqrt((static_cast<float>(Index) + 0.5f) / static_cast<float>(Count));
    const float Angle  = static_cast<float>(Index) * GoldenAngle;
    return { Radius * std::cos(Angle), Radius * std::sin(Angle), 0.0f };
}

inline float SampleCascadeDepth(const ShadowCascade& Cascade, float U, float V) noexcept
{
    const int PX = static_cast<int>(U + 0.5f);
    const int PY = static_cast<int>(V + 0.5f);
    if (PX < 0 || PY < 0 || PX >= static_cast<int>(Cascade.Resolution) || PY >= static_cast<int>(Cascade.Resolution))
    {
        return -1.0e30f;
    }
    return Cascade.Depth[static_cast<std::size_t>(PY) * Cascade.Resolution + static_cast<std::size_t>(PX)];
}

inline float EvaluateContactPcssShadow(
    const StudioWorld& World, const ShadowCascade& Cascade, const V3& Position, const V3& Normal, const StudioSpec& Spec) noexcept
{
    const V3 SunDir = Normalize(Spec.SolarDirection);
    const float NdotL = Dot(Normal, SunDir);
    if (NdotL <= 1.0e-4f)
    {
        return 0.0f;
    }
    if (World.Tree.AnyOccluded(Position + Normal * 2.0e-3f, SunDir, 1.5e-3f, 0.24f))
    {
        return 0.0f;
    }
    const V3 Rel = Position - Cascade.CenterWorld;
    const float TexelScale = static_cast<float>(Cascade.Resolution) / (2.0f * Cascade.HalfSpan);
    const float U = Dot(Rel, Cascade.RightAxis) * TexelScale + static_cast<float>(Cascade.Resolution) * 0.5f;
    const float V = Dot(Rel, Cascade.UpAxis) * TexelScale + static_cast<float>(Cascade.Resolution) * 0.5f;
    const float ReceiverDepth = Dot(Rel, Cascade.LightAxis);
    const float Bias = 0.004f + 0.012f * (1.0f - std::fabs(NdotL));

    // Blocker search with 12 Vogel taps inside a fixed 0.055 m world radius
    constexpr uint32_t BlockerTaps = 12u;
    const float SearchWorld = 0.055f;
    float BlockerSum   = 0.0f;
    uint32_t BlockerCount = 0u;
    for (uint32_t I = 0; I < BlockerTaps; ++I)
    {
        const V3 Offset = VogelDiscSample(I, BlockerTaps);
        const float SampleU = U + (Offset.X * SearchWorld) * TexelScale;
        const float SampleV = V + (Offset.Y * SearchWorld) * TexelScale;
        const float Depth = SampleCascadeDepth(Cascade, SampleU, SampleV);
        if (Depth > ReceiverDepth + Bias)
        {
            BlockerSum += Depth;
            ++BlockerCount;
        }
    }
    if (BlockerCount == 0u)
    {
        return 1.0f;
    }
    const float BlockerDepth = BlockerSum / static_cast<float>(BlockerCount);
    const float PenumbraWorld = std::max(0.006f, (ReceiverDepth - BlockerDepth) * Spec.TanSolarRadius);
    constexpr uint32_t FilterTaps = 16u;
    uint32_t Lit = 0u;
    for (uint32_t I = 0; I < FilterTaps; ++I)
    {
        const V3 Offset = VogelDiscSample(I, FilterTaps);
        const float SampleU = U + (Offset.X * PenumbraWorld) * TexelScale;
        const float SampleV = V + (Offset.Y * PenumbraWorld) * TexelScale;
        const float Depth = SampleCascadeDepth(Cascade, SampleU, SampleV);
        if (Depth <= ReceiverDepth + Bias)
        {
            ++Lit;
        }
    }
    return static_cast<float>(Lit) / static_cast<float>(FilterTaps);
}

//------------------------------------------------------------------------------------------------------------------------
//                                          SOLAR SHADOW CANDIDATE 2 : VOGEL DISC RAYS
//------------------------------------------------------------------------------------------------------------------------

inline float EvaluateVogelDiscShadow(const StudioWorld& World, const V3& Position, const V3& Normal, const StudioSpec& Spec, uint32_t RayCount) noexcept
{
    const V3 SunDir = Normalize(Spec.SolarDirection);
    if (Dot(Normal, SunDir) <= 1.0e-4f)
    {
        return 0.0f;
    }
    const V3 Helper = (std::fabs(SunDir.Z) < 0.9f) ? V3 { 0.0f, 0.0f, 1.0f } : V3 { 1.0f, 0.0f, 0.0f };
    const V3 TanU = Normalize(Cross(Helper, SunDir));
    const V3 BitV = Normalize(Cross(SunDir, TanU));
    const V3 Origin = Position + Normal * 2.5e-3f;
    uint32_t Lit = 0u;
    for (uint32_t I = 0; I < RayCount; ++I)
    {
        const V3 Offset = VogelDiscSample(I, RayCount);
        const V3 RayDir = Normalize(SunDir + TanU * (Offset.X * Spec.TanSolarRadius) + BitV * (Offset.Y * Spec.TanSolarRadius), SunDir);
        if (!World.Tree.AnyOccluded(Origin, RayDir, 1.5e-3f, 24.0f))
        {
            ++Lit;
        }
    }
    return static_cast<float>(Lit) / static_cast<float>(RayCount);
}

//------------------------------------------------------------------------------------------------------------------------
//                                     SOLAR SHADOW CANDIDATE 3 : SURFEL BLOCKER FIELD
//------------------------------------------------------------------------------------------------------------------------

struct BlockerDisk
{
    V3    Position {};
    float Radius   = 0.18f;
};

struct SurfelBlockerField
{
    std::vector<BlockerDisk> Disks {};

    void Build(const std::vector<Tri>& Source, float CellSize = 0.28f)
    {
        Disks.clear();
        struct CellAccumulator
        {
            V3       PositionSum {};
            float    Area        = 0.0f;
            uint32_t Count       = 0u;
        };
        const int NX = std::max(4, static_cast<int>(std::ceil(9.2f / CellSize)));
        const int NY = std::max(4, static_cast<int>(std::ceil(9.2f / CellSize)));
        const int NZ = std::max(4, static_cast<int>(std::ceil(3.5f / CellSize)));
        std::vector<CellAccumulator> Grid(static_cast<std::size_t>(NX) * NY * NZ);
        for (const Tri& T : Source)
        {
            const V3 Center = (T.A + T.B + T.C) * (1.0f / 3.0f);
            const float Area = 0.5f * Length(Cross(T.B - T.A, T.C - T.A));
            if (Area <= 1.0e-6f)
            {
                continue;
            }
            if (std::fabs(Center.X) > 4.6f || std::fabs(Center.Y) > 4.6f || Center.Z < 0.02f || Center.Z > 3.4f)
            {
                continue;
            }
            const int IX = std::clamp(static_cast<int>((Center.X + 4.6f) / CellSize), 0, NX - 1);
            const int IY = std::clamp(static_cast<int>((Center.Y + 4.6f) / CellSize), 0, NY - 1);
            const int IZ = std::clamp(static_cast<int>((Center.Z + 0.05f) / CellSize), 0, NZ - 1);
            auto& C = Grid[(static_cast<std::size_t>(IZ) * NY + IY) * NX + IX];
            C.PositionSum += Center * Area;
            C.Area += Area;
            C.Count += 1u;
        }
        for (const auto& C : Grid)
        {
            if (C.Count == 0u || C.Area <= 1.0e-5f)
            {
                continue;
            }
            BlockerDisk Disk {};
            Disk.Position = C.PositionSum / C.Area;
            Disk.Radius   = std::clamp(std::sqrt(C.Area / PiFloat) * 1.15f, 0.11f, 0.34f);
            Disks.push_back(Disk);
        }
    }

    [[nodiscard]] float Evaluate(const StudioWorld& World, const V3& Position, const V3& Normal, const StudioSpec& Spec) const noexcept
    {
        const V3 SunDir = Normalize(Spec.SolarDirection);
        if (Dot(Normal, SunDir) <= 1.0e-4f)
        {
            return 0.0f;
        }
        if (World.Tree.AnyOccluded(Position + Normal * 2.0e-3f, SunDir, 1.5e-3f, 0.28f))
        {
            return 0.0f;
        }
        auto SoftMin = [](float A, float B, float K) noexcept
        {
            const float H = std::max(K - std::fabs(A - B), 0.0f) / K;
            return std::min(A, B) - H * H * K * 0.25f;
        };
        float MinRatio = 2.0f;
        for (const BlockerDisk& Disk : Disks)
        {
            const V3 Rel = Disk.Position - Position;
            const float Along = Dot(Rel, SunDir);
            if (Along <= 0.14f || Along > 9.5f)
            {
                continue;
            }
            const float PlaneFade = SmoothHermite(0.02f, 0.14f, Dot(Rel, Normal));
            if (PlaneFade <= 1.0e-4f)
            {
                continue;
            }
            const V3 Perp = Rel - SunDir * Along;
            const float Clearance = Length(Perp) - Disk.Radius * 0.92f;
            const float ConeRadius = std::max(0.020f, Along * Spec.TanSolarRadius);
            const float Ratio = std::clamp(Clearance / ConeRadius, -1.5f, 1.5f) * PlaneFade + 1.5f * (1.0f - PlaneFade);
            MinRatio = SoftMin(MinRatio, Ratio, 0.14f);
            if (MinRatio <= -1.0f)
            {
                return 0.0f;
            }
        }
        return SmoothHermite(-0.95f, 0.95f, MinRatio);
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    GI #1 : SH9 FORM FACTOR FIELD
//------------------------------------------------------------------------------------------------------------------------

// Exact Lambert polygon form factor for one triangle (Gouraud / Lambert closed form, no sampling)
[[nodiscard]] inline float TriangleFormFactor(const V3& P, const V3& N, const V3& A, const V3& B, const V3& C) noexcept
{
    const V3 Verts[3] = { A - P, B - P, C - P };
    const V3 NrmCross = Cross(Verts[1] - Verts[0], Verts[2] - Verts[0]);
    const float Area = 0.5f * Length(NrmCross);
    if (Area <= 1.0e-8f)
    {
        return 0.0f;
    }
    V3 UnitNormal = NrmCross / (2.0f * Area);
    if (Dot(UnitNormal, Verts[0]) > 0.0f)
    {
        UnitNormal = UnitNormal * -1.0f; // orient away from receiver
    }
    // Stable edge-integral evaluation:
    //   F = (1/2) * sum_edges  theta_i * (n . (X_i x X_{i+1})) / |X_i x X_{i+1}|
    float Sum = 0.0f;
    for (int I = 0; I < 3; ++I)
    {
        const V3& X = Verts[I];
        const V3& Y = Verts[(I + 1) % 3];
        const V3 Cr = Cross(X, Y);
        const float LenCr = Length(Cr);
        if (LenCr < 1.0e-9f)
        {
            continue;
        }
        const float Theta = std::acos(std::clamp(Dot(X, Y) / std::max(1.0e-9f, Length(X) * Length(Y)), -1.0f, 1.0f));
        Sum += Theta * Dot(UnitNormal, Cr) / LenCr;
    }
    const float FormFactor = std::fabs(Sum) * 0.5f / PiFloat;
    const float CosReceiver = Dot(N, Normalize((A + B + C) * (1.0f / 3.0f) - P, N));
    return (CosReceiver > 0.0f) ? FormFactor * CosReceiver : 0.0f;
}

struct Sh9
{
    std::array<float, 9> C0 {};
    std::array<float, 9> C1 {};
    std::array<float, 9> C2 {};
};

[[nodiscard]] inline std::array<float, 9> EvaluateSh9Basis(const V3& D) noexcept
{
    constexpr float K0 = 0.28209479f;
    constexpr float K1 = 0.48860251f;
    constexpr float K2 = 1.09254843f;
    constexpr float K3 = 0.31539157f;
    constexpr float K4 = 0.54627421f;
    return {
        K0,
        K1 * D.Y,
        K1 * D.Z,
        K1 * D.X,
        K2 * D.X * D.Y,
        K2 * D.Y * D.Z,
        K3 * (3.0f * D.Z * D.Z - 1.0f),
        K2 * D.X * D.Z,
        K4 * (D.X * D.X - D.Y * D.Y),
    };
}

// Convolve a radiance order-2 SH9 with the clamped cosine lobe -> irradiance / pi
[[nodiscard]] inline V3 EvaluateSh9CosineIrradianceOverPi(const Sh9& Coeffs, const V3& N) noexcept
{
    const std::array<float, 9> Y = EvaluateSh9Basis(N);
    constexpr float Band0 = PiFloat;
    constexpr float Band1 = 2.0f * PiFloat / 3.0f;
    constexpr float Band2 = PiFloat / 4.0f;
    const std::array<float, 9> Weight { Band0 / PiFloat, Band1 / PiFloat, Band1 / PiFloat, Band1 / PiFloat,
                                        Band2 / PiFloat, Band2 / PiFloat, Band2 / PiFloat, Band2 / PiFloat, Band2 / PiFloat };
    V3 Sum {};
    for (std::size_t I = 0; I < 9u; ++I)
    {
        Sum.X += Coeffs.C0[I] * Y[I] * Weight[I];
        Sum.Y += Coeffs.C1[I] * Y[I] * Weight[I];
        Sum.Z += Coeffs.C2[I] * Y[I] * Weight[I];
    }
    return Sum;
}

struct IrradianceGrid
{
    V3       MinWorld { -4.2f, -4.2f, 0.0f };
    V3       MaxWorld {  4.2f,  4.2f, 3.2f };
    uint32_t NX = 14u;
    uint32_t NY = 14u;
    uint32_t NZ = 8u;
    std::vector<Sh9> Cells {};

    void Allocate()
    {
        Cells.assign(static_cast<std::size_t>(NX) * NY * NZ, {});
    }

    [[nodiscard]] V3 CellOrigin(uint32_t X, uint32_t Y, uint32_t Z) const noexcept
    {
        const V3 Span = MaxWorld - MinWorld;
        return {
            MinWorld.X + Span.X * (static_cast<float>(X) + 0.5f) / static_cast<float>(NX),
            MinWorld.Y + Span.Y * (static_cast<float>(Y) + 0.5f) / static_cast<float>(NY),
            MinWorld.Z + Span.Z * (static_cast<float>(Z) + 0.5f) / static_cast<float>(NZ),
        };
    }
};

// GI #1: per-pixel exact emitter form factor (visibility taps) + SH9 indirect field from one deterministic bounce
inline void EvaluateSh9EmissiveFormFactorGi(
    const StudioWorld& World,
    const IrradianceGrid& Grid,
    const StudioSpec& Spec,
    const std::vector<SurfaceSample>& Primary,
    std::vector<V3>& OutRadiance)
{
    const V3 SunDir = Normalize(Spec.SolarDirection);
    for (std::size_t I = 0; I < Primary.size(); ++I)
    {
        const SurfaceSample& S = Primary[I];
        if (!S.Valid)
        {
            continue;
        }
        V3 Direct {};
        for (const Tri& Emitter : World.Emitters)
        {
            const float FormFactor = TriangleFormFactor(S.Position, S.Normal, Emitter.A, Emitter.B, Emitter.C);
            if (FormFactor <= 1.0e-6f)
            {
                continue;
            }
            // 5 deterministic stratified barycentric visibility taps
            constexpr uint32_t Taps = 5u;
            uint32_t Visible = 0u;
            for (uint32_t T = 0u; T < Taps; ++T)
            {
                const V3 Offset = VogelDiscSample(T, Taps);
                const float B0 = std::clamp(0.5f + Offset.X * 0.42f, 0.02f, 0.96f);
                const float B1 = std::clamp(0.5f + Offset.Y * 0.42f, 0.02f, 0.96f - B0 * 0.0f);
                const float W0 = B0;
                const float W1 = std::min(B1, 1.0f - W0);
                const float W2 = std::max(0.0f, 1.0f - W0 - W1);
                const V3 SamplePoint = Emitter.A * W0 + Emitter.B * W1 + Emitter.C * W2;
                const V3 ToLight = SamplePoint - S.Position;
                const float Distance = Length(ToLight);
                if (Distance < 1.0e-4f)
                {
                    continue;
                }
                const V3 ShadowDir = ToLight / Distance;
                if (!World.Tree.AnyOccluded(S.Position + S.Normal * 2.0e-3f, ShadowDir, 2.0e-3f, Distance - 2.0e-3f))
                {
                    ++Visible;
                }
            }
            if (Visible == 0u)
            {
                continue;
            }
            const float Weight = static_cast<float>(Visible) / static_cast<float>(Taps);
            const V3 EmitterIrradiance = ScaleColor(Emitter.Emission, V3 { FormFactor, FormFactor, FormFactor });
            Direct += EmitterIrradiance * Weight;
        }
        V3 Indirect {};
        {
            const V3 Span = Grid.MaxWorld - Grid.MinWorld;
            const float FX = std::clamp((S.Position.X - Grid.MinWorld.X) / Span.X * static_cast<float>(Grid.NX) - 0.5f, 0.0f, static_cast<float>(Grid.NX) - 1.0001f);
            const float FY = std::clamp((S.Position.Y - Grid.MinWorld.Y) / Span.Y * static_cast<float>(Grid.NY) - 0.5f, 0.0f, static_cast<float>(Grid.NY) - 1.0001f);
            const float FZ = std::clamp((S.Position.Z - Grid.MinWorld.Z) / Span.Z * static_cast<float>(Grid.NZ) - 0.5f, 0.0f, static_cast<float>(Grid.NZ) - 1.0001f);
            const uint32_t X0 = static_cast<uint32_t>(FX);
            const uint32_t Y0 = static_cast<uint32_t>(FY);
            const uint32_t Z0 = static_cast<uint32_t>(FZ);
            const uint32_t X1 = std::min(X0 + 1u, Grid.NX - 1u);
            const uint32_t Y1 = std::min(Y0 + 1u, Grid.NY - 1u);
            const uint32_t Z1 = std::min(Z0 + 1u, Grid.NZ - 1u);
            const float TX = FX - static_cast<float>(X0);
            const float TY = FY - static_cast<float>(Y0);
            const float TZ = FZ - static_cast<float>(Z0);
            auto CellAt = [&](uint32_t X, uint32_t Y, uint32_t Z) -> const Sh9&
            {
                return Grid.Cells[(static_cast<std::size_t>(Z) * Grid.NY + Y) * Grid.NX + X];
            };
            const float W[8] = {
                (1.0f - TX) * (1.0f - TY) * (1.0f - TZ), TX * (1.0f - TY) * (1.0f - TZ),
                (1.0f - TX) * TY * (1.0f - TZ), TX * TY * (1.0f - TZ),
                (1.0f - TX) * (1.0f - TY) * TZ, TX * (1.0f - TY) * TZ,
                (1.0f - TX) * TY * TZ, TX * TY * TZ,
            };
            const Sh9* Neighbors[8] = {
                &CellAt(X0, Y0, Z0), &CellAt(X1, Y0, Z0), &CellAt(X0, Y1, Z0), &CellAt(X1, Y1, Z0),
                &CellAt(X0, Y0, Z1), &CellAt(X1, Y0, Z1), &CellAt(X0, Y1, Z1), &CellAt(X1, Y1, Z1),
            };
            for (std::size_t K = 0; K < 8u; ++K)
            {
                const V3 Contribution = EvaluateSh9CosineIrradianceOverPi(*Neighbors[K], S.Normal);
                Indirect += Contribution * W[K];
            }
        }
        OutRadiance[I] = ScaleColor(S.Albedo, Direct + Indirect);
        (void)SunDir;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                      GI #2 : SURFEL CASCADE RADIOSITY (OCTAHEDRAL + CHEBYSHEV)
//------------------------------------------------------------------------------------------------------------------------

struct Surfel
{
    V3    Position {};
    V3    Normal   {};
    float Area     = 0.0f;
    V3    Albedo   {};
    V3    Radiance {};
};

struct SurfelHashGrid
{
    float CellSize = 0.22f;
    V3    MinWorld {};
    int32_t NX = 0, NY = 0, NZ = 0;
    std::vector<std::vector<uint32_t>> Buckets {};

    void Build(const std::vector<Surfel>& Surfels, const V3& MinBound, const V3& MaxBound)
    {
        MinWorld = MinBound;
        const V3 Span = MaxBound - MinBound;
        NX = std::max(1, static_cast<int>(std::ceil(Span.X / CellSize)));
        NY = std::max(1, static_cast<int>(std::ceil(Span.Y / CellSize)));
        NZ = std::max(1, static_cast<int>(std::ceil(Span.Z / CellSize)));
        Buckets.assign(static_cast<std::size_t>(NX) * NY * NZ, {});
        for (uint32_t I = 0u; I < static_cast<uint32_t>(Surfels.size()); ++I)
        {
            Buckets[Index(Surfels[I].Position)].push_back(I);
        }
    }

    [[nodiscard]] std::size_t Index(const V3& P) const noexcept
    {
        const int IX = std::clamp(static_cast<int>((P.X - MinWorld.X) / CellSize), 0, NX - 1);
        const int IY = std::clamp(static_cast<int>((P.Y - MinWorld.Y) / CellSize), 0, NY - 1);
        const int IZ = std::clamp(static_cast<int>((P.Z - MinWorld.Z) / CellSize), 0, NZ - 1);
        return (static_cast<std::size_t>(IZ) * NY + IY) * NX + IX;
    }

    [[nodiscard]] int32_t Nearest(const std::vector<Surfel>& Surfels, const V3& P, float MaxDistance) const noexcept
    {
        const int CX = std::clamp(static_cast<int>((P.X - MinWorld.X) / CellSize), 0, NX - 1);
        const int CY = std::clamp(static_cast<int>((P.Y - MinWorld.Y) / CellSize), 0, NY - 1);
        const int CZ = std::clamp(static_cast<int>((P.Z - MinWorld.Z) / CellSize), 0, NZ - 1);
        float BestDistance = MaxDistance;
        int32_t BestIndex = -1;
        for (int DZ = -1; DZ <= 1; ++DZ)
        {
            for (int DY = -1; DY <= 1; ++DY)
            {
                for (int DX = -1; DX <= 1; ++DX)
                {
                    const int X = CX + DX;
                    const int Y = CY + DY;
                    const int Z = CZ + DZ;
                    if (X < 0 || Y < 0 || Z < 0 || X >= NX || Y >= NY || Z >= NZ)
                    {
                        continue;
                    }
                    for (uint32_t S : Buckets[(static_cast<std::size_t>(Z) * NY + Y) * NX + X])
                    {
                        const float D = Length(Surfels[S].Position - P);
                        if (D < BestDistance)
                        {
                            BestDistance = D;
                            BestIndex = static_cast<int32_t>(S);
                        }
                    }
                }
            }
        }
        return BestIndex;
    }
};

struct CascadeCell
{
    std::array<float, 64> IrradianceR {};
    std::array<float, 64> IrradianceG {};
    std::array<float, 64> IrradianceB {};
    std::array<float, 64> DepthMean {};
    std::array<float, 64> DepthMeanSq {};
};

struct CoarseRadianceShelf
{
    V3       MinWorld { -4.4f, -4.4f, -0.3f };
    V3       MaxWorld {  4.4f,  4.4f, 3.3f };
    uint32_t NX = 44u;
    uint32_t NY = 44u;
    uint32_t NZ = 18u;
    std::vector<V3>  Sum {};
    std::vector<float> Weight {};

    void Build(const std::vector<Surfel>& Surfels)
    {
        Sum.assign(static_cast<std::size_t>(NX) * NY * NZ, {});
        Weight.assign(static_cast<std::size_t>(NX) * NY * NZ, 0.0f);
        for (const Surfel& S : Surfels)
        {
            const std::size_t Index = IndexOf(S.Position);
            if (Index == static_cast<std::size_t>(-1))
            {
                continue;
            }
            Sum[Index] += S.Radiance * S.Area;
            Weight[Index] += S.Area;
        }
        // Two dilation passes so rays that land between surfels still read a plausible value
        for (int Pass = 0; Pass < 2; ++Pass)
        {
            std::vector<V3> NextSum = Sum;
            std::vector<float> NextWeight = Weight;
            for (uint32_t Z = 0u; Z < NZ; ++Z)
            {
                for (uint32_t Y = 0u; Y < NY; ++Y)
                {
                    for (uint32_t X = 0u; X < NX; ++X)
                    {
                        const std::size_t Index = (static_cast<std::size_t>(Z) * NY + Y) * NX + X;
                        if (Weight[Index] > 1.0e-8f)
                        {
                            continue;
                        }
                        V3 Accumulated {};
                        float AccumulatedWeight = 0.0f;
                        for (int DZ = -1; DZ <= 1; ++DZ)
                        {
                            for (int DY = -1; DY <= 1; ++DY)
                            {
                                for (int DX = -1; DX <= 1; ++DX)
                                {
                                    const int SX = static_cast<int>(X) + DX;
                                    const int SY = static_cast<int>(Y) + DY;
                                    const int SZ = static_cast<int>(Z) + DZ;
                                    if (SX < 0 || SY < 0 || SZ < 0 || SX >= static_cast<int>(NX) || SY >= static_cast<int>(NY) || SZ >= static_cast<int>(NZ))
                                    {
                                        continue;
                                    }
                                    const std::size_t Neighbor = (static_cast<std::size_t>(SZ) * NY + SY) * NX + SX;
                                    if (Weight[Neighbor] > 1.0e-8f)
                                    {
                                        Accumulated += Sum[Neighbor];
                                        AccumulatedWeight += Weight[Neighbor];
                                    }
                                }
                            }
                        }
                        if (AccumulatedWeight > 1.0e-8f)
                        {
                            NextSum[Index] = Accumulated;
                            NextWeight[Index] = AccumulatedWeight;
                        }
                    }
                }
            }
            Sum = NextSum;
            Weight = NextWeight;
        }
    }

    [[nodiscard]] std::size_t IndexOf(const V3& P) const noexcept
    {
        if (P.X < MinWorld.X || P.Y < MinWorld.Y || P.Z < MinWorld.Z || P.X > MaxWorld.X || P.Y > MaxWorld.Y || P.Z > MaxWorld.Z)
        {
            return static_cast<std::size_t>(-1);
        }
        const V3 Span = MaxWorld - MinWorld;
        const uint32_t X = std::min(NX - 1u, static_cast<uint32_t>((P.X - MinWorld.X) / Span.X * static_cast<float>(NX)));
        const uint32_t Y = std::min(NY - 1u, static_cast<uint32_t>((P.Y - MinWorld.Y) / Span.Y * static_cast<float>(NY)));
        const uint32_t Z = std::min(NZ - 1u, static_cast<uint32_t>((P.Z - MinWorld.Z) / Span.Z * static_cast<float>(NZ)));
        return (static_cast<std::size_t>(Z) * NY + Y) * NX + X;
    }

    [[nodiscard]] V3 Sample(const V3& P) const noexcept
    {
        const V3 Span = MaxWorld - MinWorld;
        const float FX = std::clamp((P.X - MinWorld.X) / Span.X * static_cast<float>(NX) - 0.5f, 0.0f, static_cast<float>(NX) - 1.0001f);
        const float FY = std::clamp((P.Y - MinWorld.Y) / Span.Y * static_cast<float>(NY) - 0.5f, 0.0f, static_cast<float>(NY) - 1.0001f);
        const float FZ = std::clamp((P.Z - MinWorld.Z) / Span.Z * static_cast<float>(NZ) - 0.5f, 0.0f, static_cast<float>(NZ) - 1.0001f);
        const uint32_t X0 = static_cast<uint32_t>(FX);
        const uint32_t Y0 = static_cast<uint32_t>(FY);
        const uint32_t Z0 = static_cast<uint32_t>(FZ);
        const uint32_t X1 = std::min(X0 + 1u, NX - 1u);
        const uint32_t Y1 = std::min(Y0 + 1u, NY - 1u);
        const uint32_t Z1 = std::min(Z0 + 1u, NZ - 1u);
        const float TX = FX - static_cast<float>(X0);
        const float TY = FY - static_cast<float>(Y0);
        const float TZ = FZ - static_cast<float>(Z0);
        auto Radiance = [&](uint32_t X, uint32_t Y, uint32_t Z) -> V3
        {
            const std::size_t Index = (static_cast<std::size_t>(Z) * NY + Y) * NX + X;
            const float W = Weight[Index];
            return (W > 1.0e-8f) ? Sum[Index] / W : V3 {};
        };
        V3 Accumulated {};
        Accumulated += Radiance(X0, Y0, Z0) * ((1.0f - TX) * (1.0f - TY) * (1.0f - TZ));
        Accumulated += Radiance(X1, Y0, Z0) * (TX * (1.0f - TY) * (1.0f - TZ));
        Accumulated += Radiance(X0, Y1, Z0) * ((1.0f - TX) * TY * (1.0f - TZ));
        Accumulated += Radiance(X1, Y1, Z0) * (TX * TY * (1.0f - TZ));
        Accumulated += Radiance(X0, Y0, Z1) * ((1.0f - TX) * (1.0f - TY) * TZ);
        Accumulated += Radiance(X1, Y0, Z1) * (TX * (1.0f - TY) * TZ);
        Accumulated += Radiance(X0, Y1, Z1) * ((1.0f - TX) * TY * TZ);
        Accumulated += Radiance(X1, Y1, Z1) * (TX * TY * TZ);
        return Accumulated;
    }
};

struct SurfelCascadeVolume
{
    V3    MinWorld { -4.2f, -4.2f, 0.0f };
    V3    MaxWorld {  4.2f,  4.2f, 3.2f };
    uint32_t NX = 12u;
    uint32_t NY = 12u;
    uint32_t NZ = 6u;
    std::vector<CascadeCell> Cells {};
};

[[nodiscard]] inline V3 OctahedralDirection(uint32_t Index) noexcept
{
    constexpr uint32_t Side = 8u;
    const uint32_t IX = Index % Side;
    const uint32_t IY = Index / Side;
    const float U = 2.0f * ((static_cast<float>(IX) + 0.5f) / static_cast<float>(Side)) - 1.0f;
    const float V = 2.0f * ((static_cast<float>(IY) + 0.5f) / static_cast<float>(Side)) - 1.0f;
    const V3 Direction { U, V, 1.0f - std::fabs(U) - std::fabs(V) };
    if (Direction.Z < 0.0f)
    {
        const float SX = (Direction.X >= 0.0f) ? 1.0f : -1.0f;
        const float SY = (Direction.Y >= 0.0f) ? 1.0f : -1.0f;
        return Normalize({ SX * (1.0f - std::fabs(Direction.Y)), SY * (1.0f - std::fabs(Direction.X)), Direction.Z });
    }
    return Normalize(Direction);
}

inline void BuildSurfelCascade(
    const StudioWorld& World,
    const std::vector<Surfel>& Surfels,
    const CoarseRadianceShelf& Shelf,
    const SurfelCascadeVolume& Volume,
    float MaxRayDistance,
    SurfelCascadeVolume& OutVolume)
{
    OutVolume = Volume;
    OutVolume.Cells.assign(static_cast<std::size_t>(Volume.NX) * Volume.NY * Volume.NZ, {});
    const V3 Span = Volume.MaxWorld - Volume.MinWorld;
    for (uint32_t Z = 0u; Z < Volume.NZ; ++Z)
    {
        for (uint32_t Y = 0u; Y < Volume.NY; ++Y)
        {
            for (uint32_t X = 0u; X < Volume.NX; ++X)
            {
                const V3 Origin {
                    Volume.MinWorld.X + Span.X * (static_cast<float>(X) + 0.5f) / static_cast<float>(Volume.NX),
                    Volume.MinWorld.Y + Span.Y * (static_cast<float>(Y) + 0.5f) / static_cast<float>(Volume.NY),
                    Volume.MinWorld.Z + Span.Z * (static_cast<float>(Z) + 0.5f) / static_cast<float>(Volume.NZ),
                };
                CascadeCell& Cell = OutVolume.Cells[(static_cast<std::size_t>(Z) * Volume.NY + Y) * Volume.NX + X];
                for (uint32_t D = 0u; D < 64u; ++D)
                {
                    const V3 Dir = OctahedralDirection(D);
                    const TraceHit Hit = World.Tree.TraceClosest(Origin, Dir, 2.0e-3f, MaxRayDistance);
                    V3 Radiance {};
                    float Distance = MaxRayDistance;
                    if (Hit.Hit)
                    {
                        Distance = Hit.Distance;
                        Radiance = Shelf.Sample(Origin + Dir * Hit.Distance + Hit.Normal * 0.01f);
                    }
                    Cell.IrradianceR[D] = Radiance.X;
                    Cell.IrradianceG[D] = Radiance.Y;
                    Cell.IrradianceB[D] = Radiance.Z;
                    Cell.DepthMean[D]   = Distance;
                    Cell.DepthMeanSq[D] = Distance * Distance;
                }
            }
        }
    }
}

[[nodiscard]] inline uint32_t OctahedralSlot(const V3& Direction) noexcept
{
    constexpr uint32_t Side = 8u;
    V3 D = Normalize(Direction);
    if (D.Z < 0.0f)
    {
        const float SX = (D.X >= 0.0f) ? 1.0f : -1.0f;
        const float SY = (D.Y >= 0.0f) ? 1.0f : -1.0f;
        D = { SX * (1.0f - std::fabs(D.Y)), SY * (1.0f - std::fabs(D.X)), D.Z };
    }
    const int IX = std::clamp(static_cast<int>((D.X * 0.5f + 0.5f) * static_cast<float>(Side)), 0, static_cast<int>(Side) - 1);
    const int IY = std::clamp(static_cast<int>((D.Y * 0.5f + 0.5f) * static_cast<float>(Side)), 0, static_cast<int>(Side) - 1);
    return static_cast<uint32_t>(IY) * Side + static_cast<uint32_t>(IX);
}

[[nodiscard]] inline V3 SampleSurfelCascade(const SurfelCascadeVolume& Volume, const V3& Position, const V3& Normal, float Bias = 0.02f) noexcept
{
    const V3 Span = Volume.MaxWorld - Volume.MinWorld;
    const float FX = std::clamp((Position.X - Volume.MinWorld.X) / Span.X * static_cast<float>(Volume.NX) - 0.5f, 0.0f, static_cast<float>(Volume.NX) - 1.0001f);
    const float FY = std::clamp((Position.Y - Volume.MinWorld.Y) / Span.Y * static_cast<float>(Volume.NY) - 0.5f, 0.0f, static_cast<float>(Volume.NY) - 1.0001f);
    const float FZ = std::clamp((Position.Z - Volume.MinWorld.Z) / Span.Z * static_cast<float>(Volume.NZ) - 0.5f, 0.0f, static_cast<float>(Volume.NZ) - 1.0001f);
    const uint32_t X0 = static_cast<uint32_t>(FX);
    const uint32_t Y0 = static_cast<uint32_t>(FY);
    const uint32_t Z0 = static_cast<uint32_t>(FZ);
    const uint32_t X1 = std::min(X0 + 1u, Volume.NX - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, Volume.NY - 1u);
    const uint32_t Z1 = std::min(Z0 + 1u, Volume.NZ - 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);
    const float TZ = FZ - static_cast<float>(Z0);
    const uint32_t Slot = OctahedralSlot(Normal);
    auto CellAt = [&](uint32_t X, uint32_t Y, uint32_t Z) -> const CascadeCell&
    {
        return Volume.Cells[(static_cast<std::size_t>(Z) * Volume.NY + Y) * Volume.NX + X];
    };
    const float W[8] = {
        (1.0f - TX) * (1.0f - TY) * (1.0f - TZ), TX * (1.0f - TY) * (1.0f - TZ),
        (1.0f - TX) * TY * (1.0f - TZ), TX * TY * (1.0f - TZ),
        (1.0f - TX) * (1.0f - TY) * TZ, TX * (1.0f - TY) * TZ,
        (1.0f - TX) * TY * TZ, TX * TY * TZ,
    };
    const CascadeCell* Neighbors[8] = {
        &CellAt(X0, Y0, Z0), &CellAt(X1, Y0, Z0), &CellAt(X0, Y1, Z0), &CellAt(X1, Y1, Z0),
        &CellAt(X0, Y0, Z1), &CellAt(X1, Y0, Z1), &CellAt(X0, Y1, Z1), &CellAt(X1, Y1, Z1),
    };
    V3 Sum {};
    for (std::size_t K = 0; K < 8u; ++K)
    {
        const CascadeCell& Cell = *Neighbors[K];
        const float Mean   = Cell.DepthMean[Slot];
        const float MeanSq = Cell.DepthMeanSq[Slot];
        const float Variance = std::max(0.0f, MeanSq - Mean * Mean);
        const float Sigma = std::sqrt(Variance);
        float Visibility = 1.0f;
        if (Sigma > 1.0e-4f)
        {
            const float Chebyshev = Variance / std::max(1.0e-6f, Variance + (Bias - Mean) * (Bias - Mean));
            Visibility = std::clamp(Chebyshev, 0.0f, 1.0f);
        }
        Sum.X += Cell.IrradianceR[Slot] * Visibility * W[K];
        Sum.Y += Cell.IrradianceG[Slot] * Visibility * W[K];
        Sum.Z += Cell.IrradianceB[Slot] * Visibility * W[K];
    }
    return Sum;
}

//------------------------------------------------------------------------------------------------------------------------
//                             DETERMINISTIC PATH TRACE REFERENCE (OWEN-SCRAMBLED SOBOL + VNDF)
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] inline uint32_t ReverseBits32(uint32_t Value) noexcept
{
    Value = ((Value & 0x55555555u) << 1u) | ((Value >> 1u) & 0x55555555u);
    Value = ((Value & 0x33333333u) << 2u) | ((Value >> 2u) & 0x33333333u);
    Value = ((Value & 0x0F0F0F0Fu) << 4u) | ((Value >> 4u) & 0x0F0F0F0Fu);
    Value = ((Value & 0x00FF00FFu) << 8u) | ((Value >> 8u) & 0x00FF00FFu);
    return (Value << 16u) | (Value >> 16u);
}

[[nodiscard]] inline uint32_t AvalancheKey32(uint32_t Input) noexcept
{
    uint32_t Mixed = Input;
    Mixed ^= Mixed >> 16u; Mixed *= 0x7FEB352Du;
    Mixed ^= Mixed >> 15u; Mixed *= 0x846CA68Bu;
    Mixed ^= Mixed >> 16u;
    return Mixed;
}

[[nodiscard]] inline uint32_t LaineKarrasPermutation(uint32_t Input, uint32_t Key) noexcept
{
    uint32_t Permuted = Input + Key;
    Permuted ^= Permuted * 0x6C50B47Cu;
    Permuted ^= Permuted * 0xB82F1E52u;
    Permuted ^= Permuted * 0xC7AFE638u;
    Permuted ^= Permuted * 0x8D22F6E6u;
    return Permuted;
}

[[nodiscard]] inline uint32_t NestedDigitPermutation(uint32_t Input, uint32_t Key) noexcept
{
    return ReverseBits32(LaineKarrasPermutation(ReverseBits32(Input), Key));
}

[[nodiscard]] inline uint32_t SobolSecondDimension(uint32_t Index) noexcept
{
    uint32_t Bits = Index;
    Bits ^= (Bits & 0xAAAAAAAAu) >> 1u;
    Bits ^= (Bits & 0xCCCCCCCCu) >> 2u;
    Bits ^= (Bits & 0xF0F0F0F0u) >> 4u;
    Bits ^= (Bits & 0xFF00FF00u) >> 8u;
    Bits ^= (Bits & 0xFFFF0000u) >> 16u;
    return ReverseBits32(Bits);
}

inline void Sobol02Sample(uint32_t SampleIndex, uint32_t PixelKey, uint32_t PairIndex, float& OutU, float& OutV) noexcept
{
    const uint32_t PairKey = AvalancheKey32(PairIndex * 0x9E3779B9u + 0x85EBCA6Bu) ^ PixelKey;
    const uint32_t Shuffled = NestedDigitPermutation(SampleIndex, PairKey);
    const uint32_t First  = NestedDigitPermutation(ReverseBits32(Shuffled), AvalancheKey32(PairKey ^ 0xA511E9B3u));
    const uint32_t Second = NestedDigitPermutation(SobolSecondDimension(Shuffled), AvalancheKey32(PairKey ^ 0x63D83595u));
    constexpr float Inv24 = 1.0f / 16777216.0f;
    OutU = static_cast<float>(First >> 8u) * Inv24;
    OutV = static_cast<float>(Second >> 8u) * Inv24;
}

[[nodiscard]] inline V3 CosineHemisphere(const V3& Normal, float U1, float U2) noexcept
{
    const V3 Helper = (std::fabs(Normal.Z) < 0.9f) ? V3 { 0.0f, 0.0f, 1.0f } : V3 { 1.0f, 0.0f, 0.0f };
    const V3 TanU = Normalize(Cross(Helper, Normal));
    const V3 BitV = Normalize(Cross(Normal, TanU));
    const float Radius = std::sqrt(U1);
    const float Phi = TauFloat * U2;
    return Normalize(TanU * (Radius * std::cos(Phi)) + BitV * (Radius * std::sin(Phi)) + Normal * std::sqrt(std::max(0.0f, 1.0f - U1)));
}

struct PathTraceSettings
{
    uint32_t SamplesPerPixel = 48u;
    uint32_t BounceLimit     = 3u;
    float    MaximumDistance = 40.0f;
    bool     SolarNeeEnabled = true; // [-] - false isolates the emissive transport the GI candidates approximate
};

inline V3 EvaluateEmitterSample(const std::vector<Tri>& Emitters, float U1, float U2, V3& OutPosition, V3& OutNormal, float& OutPdfArea) noexcept
{
    if (Emitters.empty())
    {
        OutPdfArea = 0.0f;
        return {};
    }
    // deterministic selection weighted by area (fixed order, no RNG)
    float TotalArea = 0.0f;
    for (const Tri& T : Emitters)
    {
        TotalArea += 0.5f * Length(Cross(T.B - T.A, T.C - T.A));
    }
    float Target = U2 * TotalArea;
    std::size_t Chosen = 0u;
    float Accumulated = 0.0f;
    for (std::size_t I = 0; I < Emitters.size(); ++I)
    {
        const float Area = 0.5f * Length(Cross(Emitters[I].B - Emitters[I].A, Emitters[I].C - Emitters[I].A));
        Accumulated += Area;
        if (Target <= Accumulated || I + 1u == Emitters.size())
        {
            Chosen = I;
            OutPdfArea = 1.0f / std::max(1.0e-6f, TotalArea);
            break;
        }
    }
    const Tri& T = Emitters[Chosen];
    const float RootU = std::sqrt(std::clamp(U1, 0.0f, 1.0f));
    const float B0 = 1.0f - RootU;
    const float B1 = RootU * (1.0f - std::clamp(U2, 0.0f, 1.0f));
    const float B2 = RootU * std::clamp(U2, 0.0f, 1.0f);
    OutPosition = T.A * B0 + T.B * B1 + T.C * B2;
    OutNormal = Normalize(Cross(T.B - T.A, T.C - T.A));
    return T.Emission;
}

inline V3 TraceDeterministicPath(
    const StudioWorld& World,
    const StudioSpec& Spec,
    const PathTraceSettings& Settings,
    const SurfaceSample& Primary,
    uint32_t PixelX,
    uint32_t PixelY)
{
    const V3 SunDir = Normalize(Spec.SolarDirection);
    const uint32_t PixelKey = AvalancheKey32(PixelY * 0x9E3779B9u ^ PixelX * 0x85EBCA6Bu ^ 0xA511E9B3u);
    V3 Accumulated {};
    V3 Position = Primary.Position;
    V3 Normal   = Primary.Normal;
    V3 Throughput { 1.0f, 1.0f, 1.0f };
    V3 Albedo   = Primary.Albedo;

    for (uint32_t Sample = 0u; Sample < Settings.SamplesPerPixel; ++Sample)
    {
        V3 Radiance {};
        V3 PathPosition = Position;
        V3 PathNormal   = Normal;
        V3 PathAlbedo   = Albedo;
        V3 PathThroughput { 1.0f, 1.0f, 1.0f };
        for (uint32_t Bounce = 0u; Bounce < Settings.BounceLimit; ++Bounce)
        {
            const uint32_t PairBase = Bounce * 4u;
            float U1 = 0.0f, U2 = 0.0f;

            // NEE to emissive triangles
            V3 EmitterPosition {};
            V3 EmitterNormal {};
            float PdfArea = 0.0f;
            Sobol02Sample(Sample, PixelKey, PairBase + 0u, U1, U2);
            const V3 EmitterRadiance = EvaluateEmitterSample(World.Emitters, U1, U2, EmitterPosition, EmitterNormal, PdfArea);
            if (PdfArea > 0.0f)
            {
                const V3 ToLight = EmitterPosition - PathPosition;
                const float Distance = Length(ToLight);
                const V3 LightDir = ToLight / std::max(1.0e-6f, Distance);
                const float CosSurface = Dot(PathNormal, LightDir);
                const float CosLight = -Dot(EmitterNormal, LightDir);
                if (CosSurface > 0.0f && CosLight > 0.0f &&
                    !World.Tree.AnyOccluded(PathPosition + PathNormal * 2.0e-3f, LightDir, 2.0e-3f, Distance - 4.0e-3f))
                {
                    const float Geometry = CosSurface * CosLight / std::max(1.0e-4f, Distance * Distance);
                    const V3 Contribution = ScaleColor(EmitterRadiance, PathAlbedo) * (Geometry / std::max(1.0e-8f, PdfArea * PiFloat));
                    Radiance += ScaleColor(PathThroughput, Contribution);
                }
            }

            // Solar NEE with a stochastic-free fixed ray along the sun direction
            Sobol02Sample(Sample, PixelKey, PairBase + 1u, U1, U2);
            const float SolarCos = Dot(PathNormal, SunDir);
            if (Settings.SolarNeeEnabled && SolarCos > 0.0f && !World.Tree.AnyOccluded(PathPosition + PathNormal * 2.0e-3f, SunDir, 1.5e-3f, Settings.MaximumDistance))
            {
                Radiance += ScaleColor(PathThroughput, ScaleColor(PathAlbedo, Spec.SolarIrradiance) * (SolarCos / PiFloat));
            }

            // Cosine weighted diffuse continuation
            Sobol02Sample(Sample, PixelKey, PairBase + 2u, U1, U2);
            const V3 BounceDir = CosineHemisphere(PathNormal, U1, U2);
            const TraceHit Hit = World.Tree.TraceClosest(PathPosition + PathNormal * 2.0e-3f, BounceDir, 2.0e-3f, Settings.MaximumDistance);
            if (!Hit.Hit)
            {
                break;
            }
            PathPosition = PathPosition + PathNormal * 2.0e-3f + BounceDir * Hit.Distance;
            const Tri& HitTri = World.Tree.SourceTriangles()[Hit.TriIndex];
            PathNormal = (Dot(Hit.Normal, BounceDir) > 0.0f) ? Hit.Normal * -1.0f : Hit.Normal;
            PathAlbedo = HitTri.Albedo;
            if (HitTri.Kind == 1u)
            {
                Radiance += ScaleColor(PathThroughput, ScaleColor(PathAlbedo, HitTri.Emission) * (1.0f / PiFloat));
                break;
            }
            PathThroughput = ScaleColor(PathThroughput, PathAlbedo);
            if ((PathThroughput.X + PathThroughput.Y + PathThroughput.Z) < 0.002f)
            {
                break;
            }
        }
        Accumulated += Radiance;
    }
    (void)Throughput;
    (void)Normal;
    (void)Position;
    return Accumulated / static_cast<float>(std::max(1u, Settings.SamplesPerPixel));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PNG / SHEET OUTPUT
//------------------------------------------------------------------------------------------------------------------------

inline uint32_t Crc32(const uint8_t* Data, std::size_t Length, uint32_t Seed = 0xFFFFFFFFu)
{
    static std::array<uint32_t, 256> Table {};
    static bool TableReady = false;
    if (!TableReady)
    {
        for (uint32_t N = 0u; N < 256u; ++N)
        {
            uint32_t C = N;
            for (int K = 0; K < 8; ++K)
            {
                C = (C & 1u) ? (0xEDB88320u ^ (C >> 1u)) : (C >> 1u);
            }
            Table[N] = C;
        }
        TableReady = true;
    }
    uint32_t C = Seed;
    for (std::size_t I = 0; I < Length; ++I)
    {
        C = Table[(C ^ Data[I]) & 0xFFu] ^ (C >> 8u);
    }
    return C;
}

inline void AppendBigEndian(std::vector<uint8_t>& Out, uint32_t Value)
{
    Out.push_back(static_cast<uint8_t>((Value >> 24u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((Value >> 16u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>((Value >> 8u) & 0xFFu));
    Out.push_back(static_cast<uint8_t>(Value & 0xFFu));
}

inline void AppendPngChunk(std::vector<uint8_t>& Out, const char* Type, const std::vector<uint8_t>& Payload)
{
    AppendBigEndian(Out, static_cast<uint32_t>(Payload.size()));
    const std::size_t Start = Out.size();
    for (int I = 0; I < 4; ++I)
    {
        Out.push_back(static_cast<uint8_t>(Type[I]));
    }
    Out.insert(Out.end(), Payload.begin(), Payload.end());
    const uint32_t Crc = Crc32(Out.data() + Start, 4u + Payload.size()) ^ 0xFFFFFFFFu;
    AppendBigEndian(Out, Crc);
}

inline bool SavePngRgba(const std::filesystem::path& Path, uint32_t Width, uint32_t Height, const std::vector<uint8_t>& Rgba)
{
    std::vector<uint8_t> Raw {};
    Raw.reserve(static_cast<std::size_t>(Height) * (Width * 4u + 1u));
    for (uint32_t Y = 0; Y < Height; ++Y)
    {
        Raw.push_back(0u);
        const std::size_t RowStart = static_cast<std::size_t>(Y) * Width * 4u;
        Raw.insert(Raw.end(), Rgba.begin() + static_cast<std::ptrdiff_t>(RowStart), Rgba.begin() + static_cast<std::ptrdiff_t>(RowStart + Width * 4u));
    }

    // Stored (uncompressed) deflate blocks
    std::vector<uint8_t> Zlib {};
    Zlib.push_back(0x78u);
    Zlib.push_back(0x01u);
    std::size_t Offset = 0u;
    while (Offset < Raw.size())
    {
        const std::size_t Chunk = std::min<std::size_t>(65535u, Raw.size() - Offset);
        const bool Final = (Offset + Chunk) >= Raw.size();
        Zlib.push_back(Final ? 1u : 0u);
        Zlib.push_back(static_cast<uint8_t>(Chunk & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((Chunk >> 8u) & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>((~Chunk) & 0xFFu));
        Zlib.push_back(static_cast<uint8_t>(((~Chunk) >> 8u) & 0xFFu));
        Zlib.insert(Zlib.end(), Raw.begin() + static_cast<std::ptrdiff_t>(Offset), Raw.begin() + static_cast<std::ptrdiff_t>(Offset + Chunk));
        Offset += Chunk;
    }
    uint32_t AdlerA = 1u;
    uint32_t AdlerB = 0u;
    for (uint8_t Byte : Raw)
    {
        AdlerA = (AdlerA + Byte) % 65521u;
        AdlerB = (AdlerB + AdlerA) % 65521u;
    }
    AppendBigEndian(Zlib, (AdlerB << 16u) | AdlerA);

    std::vector<uint8_t> Png { 0x89u, 0x50u, 0x4Eu, 0x47u, 0x0Du, 0x0Au, 0x1Au, 0x0Au };
    std::vector<uint8_t> Header {};
    AppendBigEndian(Header, Width);
    AppendBigEndian(Header, Height);
    Header.push_back(8u);
    Header.push_back(6u);
    Header.push_back(0u);
    Header.push_back(0u);
    Header.push_back(0u);
    AppendPngChunk(Png, "IHDR", Header);
    AppendPngChunk(Png, "IDAT", Zlib);
    AppendPngChunk(Png, "IEND", {});

    std::filesystem::create_directories(Path.parent_path());
    std::ofstream Stream(Path, std::ios::binary);
    if (!Stream)
    {
        return false;
    }
    Stream.write(reinterpret_cast<const char*>(Png.data()), static_cast<std::streamsize>(Png.size()));
    return true;
}

struct LetterPattern
{
    char                   Symbol;
    std::array<uint8_t, 7> Rows;
};

inline constexpr std::array<LetterPattern, 40u> LetterSet {{
    { ' ', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 } },
    { '-', { 0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00 } },
    { '.', { 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C } },
    { ':', { 0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00 } },
    { '/', { 0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10 } },
    { '+', { 0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00 } },
    { '(', { 0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02 } },
    { ')', { 0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08 } },
    { '0', { 0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E } },
    { '1', { 0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { '2', { 0x0E, 0x11, 0x01, 0x06, 0x08, 0x10, 0x1F } },
    { '3', { 0x1F, 0x02, 0x04, 0x06, 0x01, 0x11, 0x0E } },
    { '4', { 0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02 } },
    { '5', { 0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E } },
    { '6', { 0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E } },
    { '7', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08 } },
    { '8', { 0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E } },
    { '9', { 0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'B', { 0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E } },
    { 'C', { 0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E } },
    { 'D', { 0x1C, 0x12, 0x11, 0x11, 0x11, 0x12, 0x1C } },
    { 'E', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F } },
    { 'F', { 0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'H', { 0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'J', { 0x07, 0x02, 0x02, 0x02, 0x12, 0x12, 0x0C } },
    { 'K', { 0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11 } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'M', { 0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11 } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'P', { 0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10 } },
    { 'Q', { 0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D } },
    { 'R', { 0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11 } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
    { 'T', { 0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04 } },
    { 'U', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'V', { 0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04 } },
}};

inline void FillRect(std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height, uint32_t X0, uint32_t Y0, uint32_t W, uint32_t H,
                     const std::array<uint8_t, 3>& Color) noexcept
{
    const uint32_t X1 = std::min(Width, X0 + W);
    const uint32_t Y1 = std::min(Height, Y0 + H);
    for (uint32_t Y = Y0; Y < Y1; ++Y)
    {
        for (uint32_t X = X0; X < X1; ++X)
        {
            const std::size_t Offset = (static_cast<std::size_t>(Y) * Width + X) * 4u;
            Rgba[Offset + 0u] = Color[0];
            Rgba[Offset + 1u] = Color[1];
            Rgba[Offset + 2u] = Color[2];
            Rgba[Offset + 3u] = 255u;
        }
    }
}

inline void DrawText(std::vector<uint8_t>& Rgba, uint32_t Width, uint32_t Height, uint32_t X0, uint32_t Y0, const std::string& Text,
                     uint32_t Scale, const std::array<uint8_t, 3>& Tint) noexcept
{
    uint32_t Cursor = X0;
    for (char Raw : Text)
    {
        char Symbol = Raw;
        if (Symbol >= 'a' && Symbol <= 'z')
        {
            Symbol = static_cast<char>(Symbol - 'a' + 'A');
        }
        if (Symbol == 'W' || Symbol == 'X' || Symbol == 'Y' || Symbol == 'Z')
        {
            static const std::array<LetterPattern, 4u> Extra {{
                { 'W', { 0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11 } },
                { 'X', { 0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11 } },
                { 'Y', { 0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04 } },
                { 'Z', { 0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F } },
            }};
            const LetterPattern* Hit = &Extra[0];
            for (const auto& Pattern : Extra)
            {
                if (Pattern.Symbol == Symbol)
                {
                    Hit = &Pattern;
                    break;
                }
            }
            for (uint32_t R = 0u; R < 7u; ++R)
            {
                for (uint32_t C = 0u; C < 5u; ++C)
                {
                    if (((Hit->Rows[R] >> (4u - C)) & 1u) == 0u)
                    {
                        continue;
                    }
                    FillRect(Rgba, Width, Height, Cursor + C * Scale, Y0 + R * Scale, Scale, Scale, Tint);
                }
            }
            Cursor += 6u * Scale;
            continue;
        }
        const LetterPattern* Hit = &LetterSet[0];
        for (const auto& Pattern : LetterSet)
        {
            if (Pattern.Symbol == Symbol)
            {
                Hit = &Pattern;
                break;
            }
        }
        for (uint32_t R = 0u; R < 7u; ++R)
        {
            for (uint32_t C = 0u; C < 5u; ++C)
            {
                if (((Hit->Rows[R] >> (4u - C)) & 1u) == 0u)
                {
                    continue;
                }
                FillRect(Rgba, Width, Height, Cursor + C * Scale, Y0 + R * Scale, Scale, Scale, Tint);
            }
        }
        Cursor += 6u * Scale;
    }
}

inline uint8_t EncodeSrgb8(float Linear) noexcept
{
    const float Clamped = std::clamp(Linear, 0.0f, 1.0f);
    const float Encoded = (Clamped <= 0.0031308f) ? (12.92f * Clamped) : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::round(Encoded * 255.0f), 0.0f, 255.0f));
}

inline float ApplyAcesFit(float Value) noexcept
{
    const float X = std::max(0.0f, Value);
    return (X * (2.51f * X + 0.03f)) / (X * (2.43f * X + 0.59f) + 0.14f);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       MEASUREMENTS
//------------------------------------------------------------------------------------------------------------------------

inline uint64_t HashFloats(const std::vector<float>& Values) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (float Value : Values)
    {
        uint32_t Bits = 0u;
        std::memcpy(&Bits, &Value, sizeof(Bits));
        Hash ^= static_cast<uint64_t>(Bits);
        Hash *= 1099511628211ull;
    }
    return Hash;
}

inline uint64_t HashColors(const std::vector<V3>& Values) noexcept
{
    std::vector<float> Flat(Values.size() * 3u);
    for (std::size_t I = 0; I < Values.size(); ++I)
    {
        Flat[I * 3u + 0u] = Values[I].X;
        Flat[I * 3u + 1u] = Values[I].Y;
        Flat[I * 3u + 2u] = Values[I].Z;
    }
    return HashFloats(Flat);
}

inline double ScalarRmse(const std::vector<float>& Candidate, const std::vector<float>& Reference, const std::vector<SurfaceSample>& Mask)
{
    double SumSq = 0.0;
    std::size_t Count = 0u;
    for (std::size_t I = 0; I < Candidate.size(); ++I)
    {
        if (!Mask[I].Valid)
        {
            continue;
        }
        const double Delta = static_cast<double>(Candidate[I]) - static_cast<double>(Reference[I]);
        SumSq += Delta * Delta;
        ++Count;
    }
    return (Count > 0u) ? std::sqrt(SumSq / static_cast<double>(Count)) : 0.0;
}

inline double ColorRmse(const std::vector<V3>& Candidate, const std::vector<V3>& Reference, const std::vector<SurfaceSample>& Mask)
{
    double SumSq = 0.0;
    std::size_t Count = 0u;
    for (std::size_t I = 0; I < Candidate.size(); ++I)
    {
        if (!Mask[I].Valid)
        {
            continue;
        }
        const double DR = static_cast<double>(Candidate[I].X) - static_cast<double>(Reference[I].X);
        const double DG = static_cast<double>(Candidate[I].Y) - static_cast<double>(Reference[I].Y);
        const double DB = static_cast<double>(Candidate[I].Z) - static_cast<double>(Reference[I].Z);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        ++Count;
    }
    return (Count > 0u) ? std::sqrt(SumSq / static_cast<double>(Count)) : 0.0;
}

inline void ColorErrorSummary(const std::vector<V3>& Candidate, const std::vector<V3>& Reference,
                              const std::vector<SurfaceSample>& Mask, double& OutRelativeRmse,
                              double& OutMeanCandidate, double& OutMeanReference, double& OutCorrelation)
{
    double SumSq = 0.0, SumCandidate = 0.0, SumReference = 0.0, SumProd = 0.0;
    double SumCandidateSq = 0.0, SumReferenceSq = 0.0;
    std::size_t Count = 0u;
    for (std::size_t I = 0; I < Candidate.size(); ++I)
    {
        if (!Mask[I].Valid)
        {
            continue;
        }
        const double LumaCandidate = 0.2126 * Candidate[I].X + 0.7152 * Candidate[I].Y + 0.0722 * Candidate[I].Z;
        const double LumaReference = 0.2126 * Reference[I].X + 0.7152 * Reference[I].Y + 0.0722 * Reference[I].Z;
        const double DR = static_cast<double>(Candidate[I].X) - static_cast<double>(Reference[I].X);
        const double DG = static_cast<double>(Candidate[I].Y) - static_cast<double>(Reference[I].Y);
        const double DB = static_cast<double>(Candidate[I].Z) - static_cast<double>(Reference[I].Z);
        SumSq += (DR * DR + DG * DG + DB * DB) / 3.0;
        SumCandidate += LumaCandidate;
        SumReference += LumaReference;
        SumProd += LumaCandidate * LumaReference;
        SumCandidateSq += LumaCandidate * LumaCandidate;
        SumReferenceSq += LumaReference * LumaReference;
        ++Count;
    }
    if (Count == 0u)
    {
        OutRelativeRmse = 0.0; OutMeanCandidate = 0.0; OutMeanReference = 0.0; OutCorrelation = 1.0;
        return;
    }
    const double N = static_cast<double>(Count);
    OutMeanCandidate = SumCandidate / N;
    OutMeanReference = SumReference / N;
    const double Rmse = std::sqrt(SumSq / N);
    OutRelativeRmse = Rmse / std::max(1.0e-6, OutMeanReference);
    const double Covariance = SumProd / N - OutMeanCandidate * OutMeanReference;
    const double StdCandidate = std::sqrt(std::max(0.0, SumCandidateSq / N - OutMeanCandidate * OutMeanCandidate));
    const double StdReference = std::sqrt(std::max(0.0, SumReferenceSq / N - OutMeanReference * OutMeanReference));
    OutCorrelation = Covariance / std::max(1.0e-9, StdCandidate * StdReference);
}

template <typename Callback>
inline void ParallelRows(uint32_t Height, Callback&& Body)
{
    const uint32_t ThreadCount = std::clamp(std::thread::hardware_concurrency(), 1u, 16u);
    if (ThreadCount <= 1u)
    {
        Body(0u, Height);
        return;
    }
    std::vector<std::thread> Workers {};
    const uint32_t Chunk = (Height + ThreadCount - 1u) / ThreadCount;
    for (uint32_t T = 0u; T < ThreadCount; ++T)
    {
        const uint32_t Y0 = T * Chunk;
        const uint32_t Y1 = std::min(Height, Y0 + Chunk);
        if (Y0 < Y1)
        {
            Workers.emplace_back([&, Y0, Y1]() { Body(Y0, Y1); });
        }
    }
    for (auto& Worker : Workers)
    {
        Worker.join();
    }
}

inline std::vector<uint8_t> ShadowMaskToRgba(const std::vector<float>& Visibility)
{
    std::vector<uint8_t> Rgba(Visibility.size() * 4u, 255u);
    for (std::size_t I = 0; I < Visibility.size(); ++I)
    {
        const uint8_t Value = EncodeSrgb8(std::clamp(Visibility[I], 0.0f, 1.0f));
        Rgba[I * 4u + 0u] = Value;
        Rgba[I * 4u + 1u] = Value;
        Rgba[I * 4u + 2u] = Value;
    }
    return Rgba;
}

inline std::vector<uint8_t> RadianceToRgba(const std::vector<V3>& Radiance, const std::vector<SurfaceSample>& Primary, float Exposure)
{
    std::vector<uint8_t> Rgba(Radiance.size() * 4u, 255u);
    for (std::size_t I = 0; I < Radiance.size(); ++I)
    {
        if (!Primary[I].Valid)
        {
            Rgba[I * 4u + 0u] = 12u;
            Rgba[I * 4u + 1u] = 14u;
            Rgba[I * 4u + 2u] = 18u;
            continue;
        }
        Rgba[I * 4u + 0u] = EncodeSrgb8(ApplyAcesFit(Radiance[I].X * Exposure));
        Rgba[I * 4u + 1u] = EncodeSrgb8(ApplyAcesFit(Radiance[I].Y * Exposure));
        Rgba[I * 4u + 2u] = EncodeSrgb8(ApplyAcesFit(Radiance[I].Z * Exposure));
    }
    return Rgba;
}

[[nodiscard]] inline V3 SampleSurfelCascadeCosineConvolved(const SurfelCascadeVolume& Volume, const V3& Position, const V3& Normal, float Bias = 0.02f) noexcept
{
    const V3 Span = Volume.MaxWorld - Volume.MinWorld;
    const float FX = std::clamp((Position.X - Volume.MinWorld.X) / Span.X * static_cast<float>(Volume.NX) - 0.5f, 0.0f, static_cast<float>(Volume.NX) - 1.0001f);
    const float FY = std::clamp((Position.Y - Volume.MinWorld.Y) / Span.Y * static_cast<float>(Volume.NY) - 0.5f, 0.0f, static_cast<float>(Volume.NY) - 1.0001f);
    const float FZ = std::clamp((Position.Z - Volume.MinWorld.Z) / Span.Z * static_cast<float>(Volume.NZ) - 0.5f, 0.0f, static_cast<float>(Volume.NZ) - 1.0001f);
    const uint32_t X0 = static_cast<uint32_t>(FX);
    const uint32_t Y0 = static_cast<uint32_t>(FY);
    const uint32_t Z0 = static_cast<uint32_t>(FZ);
    const uint32_t X1 = std::min(X0 + 1u, Volume.NX - 1u);
    const uint32_t Y1 = std::min(Y0 + 1u, Volume.NY - 1u);
    const uint32_t Z1 = std::min(Z0 + 1u, Volume.NZ - 1u);
    const float TX = FX - static_cast<float>(X0);
    const float TY = FY - static_cast<float>(Y0);
    const float TZ = FZ - static_cast<float>(Z0);
    const float W[8] = {
        (1.0f - TX) * (1.0f - TY) * (1.0f - TZ), TX * (1.0f - TY) * (1.0f - TZ),
        (1.0f - TX) * TY * (1.0f - TZ), TX * TY * (1.0f - TZ),
        (1.0f - TX) * (1.0f - TY) * TZ, TX * (1.0f - TY) * TZ,
        (1.0f - TX) * TY * TZ, TX * TY * TZ,
    };
    const CascadeCell* Neighbors[8] = {
        &Volume.Cells[(static_cast<std::size_t>(Z0) * Volume.NY + Y0) * Volume.NX + X0],
        &Volume.Cells[(static_cast<std::size_t>(Z0) * Volume.NY + Y0) * Volume.NX + X1],
        &Volume.Cells[(static_cast<std::size_t>(Z0) * Volume.NY + Y1) * Volume.NX + X0],
        &Volume.Cells[(static_cast<std::size_t>(Z0) * Volume.NY + Y1) * Volume.NX + X1],
        &Volume.Cells[(static_cast<std::size_t>(Z1) * Volume.NY + Y0) * Volume.NX + X0],
        &Volume.Cells[(static_cast<std::size_t>(Z1) * Volume.NY + Y0) * Volume.NX + X1],
        &Volume.Cells[(static_cast<std::size_t>(Z1) * Volume.NY + Y1) * Volume.NX + X0],
        &Volume.Cells[(static_cast<std::size_t>(Z1) * Volume.NY + Y1) * Volume.NX + X1],
    };
    V3 Sum {};
    for (std::size_t K = 0; K < 8u; ++K)
    {
        const CascadeCell& Cell = *Neighbors[K];
        if (W[K] <= 1.0e-6f)
        {
            continue;
        }
        V3 CellIrradiance {};
        for (uint32_t D = 0u; D < 64u; ++D)
        {
            const V3 Dir = OctahedralDirection(D);
            const float Cos = Dot(Dir, Normal);
            if (Cos <= 0.0f)
            {
                continue;
            }
            const float Mean = Cell.DepthMean[D];
            const float Variance = std::max(0.0f, Cell.DepthMeanSq[D] - Mean * Mean);
            float Visibility = 1.0f;
            if (Variance > 1.0e-4f)
            {
                Visibility = std::clamp(Variance / std::max(1.0e-6f, Variance + (Bias - Mean) * (Bias - Mean)), 0.0f, 1.0f);
            }
            const float Weight = Cos * (4.0f / 64.0f);
            CellIrradiance.X += Cell.IrradianceR[D] * Visibility * Weight;
            CellIrradiance.Y += Cell.IrradianceG[D] * Visibility * Weight;
            CellIrradiance.Z += Cell.IrradianceB[D] * Visibility * Weight;
        }
        Sum += CellIrradiance * W[K];
    }
    return Sum;
}

inline void BandedShadowError(const std::vector<float>& Candidate, const std::vector<float>& Reference,
                              const std::vector<SurfaceSample>& Mask,
                              double& OutUmbra, double& OutPenumbra, double& OutLit)
{
    auto Band = [&](float Lo, float Hi, double& Out)
    {
        double SumSq = 0.0;
        std::size_t Count = 0u;
        for (std::size_t I = 0; I < Candidate.size(); ++I)
        {
            if (!Mask[I].Valid || Reference[I] < Lo || Reference[I] > Hi)
            {
                continue;
            }
            const double Delta = static_cast<double>(Candidate[I]) - static_cast<double>(Reference[I]);
            SumSq += Delta * Delta;
            ++Count;
        }
        Out = (Count > 0u) ? std::sqrt(SumSq / static_cast<double>(Count)) : 0.0;
    };
    Band(0.0f, 0.05f, OutUmbra);
    Band(0.05f, 0.95f, OutPenumbra);
    Band(0.95f, 1.0f, OutLit);
}

struct SheetCell
{
    std::string          Title  {};
    std::string          Detail {};
    std::array<uint8_t, 3> Accent { 170u, 196u, 232u };
    std::vector<uint8_t> Rgba8  {};
};

inline void SaveGridSheet(const std::filesystem::path& Path, const std::string& Banner, uint32_t Columns, uint32_t Rows,
                          uint32_t CellW, uint32_t CellH, const std::vector<SheetCell>& Cells)
{
    constexpr uint32_t PadX = 14u, PadY = 14u, BannerH = 42u, HeadH = 34u;
    const uint32_t SheetW = PadX + Columns * (CellW + PadX);
    const uint32_t SheetH = BannerH + PadY + Rows * (CellH + HeadH + PadY);
    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetW) * SheetH * 4u, 255u);
    FillRect(Sheet, SheetW, SheetH, 0u, 0u, SheetW, SheetH, { 16u, 19u, 25u });
    FillRect(Sheet, SheetW, SheetH, 0u, 0u, SheetW, BannerH, { 26u, 31u, 42u });
    FillRect(Sheet, SheetW, SheetH, 0u, BannerH - 2u, SheetW, 2u, { 72u, 130u, 206u });
    DrawText(Sheet, SheetW, SheetH, 16u, 14u, Banner, 2u, { 235u, 242u, 252u });
    for (uint32_t R = 0u; R < Rows; ++R)
    {
        for (uint32_t C = 0u; C < Columns; ++C)
        {
            const std::size_t Index = static_cast<std::size_t>(R) * Columns + C;
            if (Index >= Cells.size())
            {
                continue;
            }
            const SheetCell& Cell = Cells[Index];
            const uint32_t OX = PadX + C * (CellW + PadX);
            const uint32_t OY = BannerH + PadY + R * (CellH + HeadH + PadY);
            FillRect(Sheet, SheetW, SheetH, OX, OY, CellW, HeadH, { 28u, 33u, 44u });
            FillRect(Sheet, SheetW, SheetH, OX, OY, 5u, HeadH, Cell.Accent);
            DrawText(Sheet, SheetW, SheetH, OX + 12u, OY + 6u, Cell.Title, 2u, { 244u, 247u, 252u });
            if (!Cell.Detail.empty())
            {
                DrawText(Sheet, SheetW, SheetH, OX + 12u, OY + 23u, Cell.Detail, 1u, Cell.Accent);
            }
            if (Cell.Rgba8.size() == static_cast<std::size_t>(CellW) * CellH * 4u)
            {
                for (uint32_t Y = 0u; Y < CellH; ++Y)
                {
                    std::copy_n(
                        &Cell.Rgba8[static_cast<std::size_t>(Y) * CellW * 4u],
                        static_cast<std::size_t>(CellW) * 4u,
                        &Sheet[(static_cast<std::size_t>(OY + HeadH + Y) * SheetW + OX) * 4u]);
                }
            }
        }
    }
    (void)SavePngRgba(Path, SheetW, SheetH, Sheet);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    MAIN MEASUREMENT ENTRY
//------------------------------------------------------------------------------------------------------------------------

int main(int ArgumentCount, char** ArgumentValues)
{
    const std::filesystem::path OutDir =
        (ArgumentCount > 1) ? std::filesystem::path(ArgumentValues[1]) : std::filesystem::path("tools/LightingCandidateBench/results");

    constexpr uint32_t Width  = 400u;
    constexpr uint32_t Height = 225u;
    const std::size_t PixelCount = static_cast<std::size_t>(Width) * Height;

    StudioSpec Spec {};
    const V3 CharacterRootA { 1.05f, -2.05f, 0.0f };
    const V3 CharacterRootB { 1.20f, -2.12f, 0.0f };

    std::printf("\n=== Frontier Lighting Candidate Bench (real triangles only, zero SDF) ===\n");
    std::printf("Viewport %ux%u | threads %u\n", Width, Height, std::thread::hardware_concurrency());

    const auto SectionStart = std::chrono::steady_clock::now();
    auto MillisecondsSince = [](const std::chrono::steady_clock::time_point& Start)
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Start).count();
    };

    //----------------------------------------------------------------------------------------------------------------
    // 1. Pose A world, cascade, primary buffer
    //----------------------------------------------------------------------------------------------------------------
    StudioWorld WorldA = ConstructStudio(Spec, 0.0f, CharacterRootA);
    std::printf("Pose A: %zu triangles (%zu emitters), BVH %zu nodes, build %.2f ms\n",
                WorldA.Triangles.size(), WorldA.Emitters.size(), WorldA.Tree.NodeCount(), MillisecondsSince(SectionStart));

    const CameraBasis Camera = ConstructCamera(Width, Height);
    const auto PrimaryA = TracePrimary(WorldA, Camera, Width, Height);
    std::size_t ValidPixels = 0u;
    for (const auto& Sample : PrimaryA)
    {
        ValidPixels += Sample.Valid ? 1u : 0u;
    }
    std::printf("Primary visibility: %zu / %zu covered pixels\n", ValidPixels, PixelCount);

    // Deforming pose update cost on real triangles (figure regeneration + BVH rebuild, no voxel rebake)
    const auto DeformStart = std::chrono::steady_clock::now();
    std::vector<Tri> PoseTriangles {};
    PoseTriangles.reserve(WorldA.Triangles.size());
    StudioWorld Prologue {};
    (void)Prologue;
    {
        std::vector<Tri> Emitters {};
        AppendStudioBase(PoseTriangles, Emitters, Spec);
        AppendVehicle(PoseTriangles, { -0.85f, 0.05f, 0.0f });
        AppendFigurePose(PoseTriangles, CharacterRootB, 1.0f, { 0.68f, 0.70f, 0.74f }, { 0.88f, 0.52f, 0.16f });
    }
    TriangleTree PoseTree {};
    PoseTree.Build(PoseTriangles);
    const double DeformMilliseconds = MillisecondsSince(DeformStart);
    std::printf("Pose A -> Pose B real-triangle regeneration + BVH rebuild: %.2f ms (zero voxel SDF work)\n", DeformMilliseconds);

    //----------------------------------------------------------------------------------------------------------------
    // 2. Shadow candidates vs 128 ray reference
    //----------------------------------------------------------------------------------------------------------------
    const auto CascadeStart = std::chrono::steady_clock::now();
    const ShadowCascade CascadeA = BuildShadowCascade(WorldA.Tree, Spec.SolarDirection, { 0.0f, 0.0f, 0.9f }, 4.2f, 1024u);
    const double CascadeMilliseconds = MillisecondsSince(CascadeStart);
    std::printf("Shadow cascade 1024x1024 texels: %.2f ms (%.2f mm/texel)\n", CascadeMilliseconds, 4200.0f * 2.0f / 1024.0f);

    SurfelBlockerField BlockerField {};
    const auto BlockerStart = std::chrono::steady_clock::now();
    BlockerField.Build(WorldA.Triangles);
    const double BlockerMilliseconds = MillisecondsSince(BlockerStart);
    std::printf("Surfel blocker field: %zu disks in %.2f ms\n", BlockerField.Disks.size(), BlockerMilliseconds);

    std::vector<float> VisibilityReference(PixelCount, 1.0f);
    std::vector<float> VisibilityContactPcss(PixelCount, 1.0f);
    std::vector<float> VisibilityVogelDisc(PixelCount, 1.0f);
    std::vector<float> VisibilityBlockerField(PixelCount, 1.0f);

    auto RunShadowField = [&](auto&& Evaluator, std::vector<float>& Target)
    {
        const auto Start = std::chrono::steady_clock::now();
        ParallelRows(Height, [&](uint32_t Y0, uint32_t Y1)
        {
            for (uint32_t Y = Y0; Y < Y1; ++Y)
            {
                for (uint32_t X = 0u; X < Width; ++X)
                {
                    const std::size_t Index = static_cast<std::size_t>(Y) * Width + X;
                    if (!PrimaryA[Index].Valid)
                    {
                        continue;
                    }
                    Target[Index] = Evaluator(PrimaryA[Index].Position, PrimaryA[Index].Normal);
                }
            }
        });
        return MillisecondsSince(Start);
    };

    const double MsReference = RunShadowField([&](const V3& P, const V3& N) { return EvaluateVogelDiscShadow(WorldA, P, N, Spec, 128u); }, VisibilityReference);
    const double MsContactPcss = RunShadowField([&](const V3& P, const V3& N) { return EvaluateContactPcssShadow(WorldA, CascadeA, P, N, Spec); }, VisibilityContactPcss);
    const double MsVogelDisc = RunShadowField([&](const V3& P, const V3& N) { return EvaluateVogelDiscShadow(WorldA, P, N, Spec, 8u); }, VisibilityVogelDisc);
    const double MsBlockerField = RunShadowField([&](const V3& P, const V3& N) { return BlockerField.Evaluate(WorldA, P, N, Spec); }, VisibilityBlockerField);

    const double RmseContactPcss = ScalarRmse(VisibilityContactPcss, VisibilityReference, PrimaryA);
    const double RmseVogelDisc  = ScalarRmse(VisibilityVogelDisc, VisibilityReference, PrimaryA);
    const double RmseBlocker    = ScalarRmse(VisibilityBlockerField, VisibilityReference, PrimaryA);
    std::printf("\nShadow visibility RMSE vs 128-ray reference:\n");
    std::printf("  ContactPcss        %.4f  (%.1f ms)\n", RmseContactPcss, MsContactPcss);
    std::printf("  VogelDisc8         %.4f  (%.1f ms)\n", RmseVogelDisc, MsVogelDisc);
    std::printf("  SurfelBlockerField %.4f  (%.1f ms)\n", RmseBlocker, MsBlockerField);
    std::printf("  Reference 128 rays    0.0000  (%.1f ms)\n", MsReference);

    // Spot checks on physical behaviour
    float Sunlit{ 1.0f }, UnderVehicle{ 1.0f }, FootContact{ 1.0f };
    {
        const V3 OpenSun { 3.30f, -0.20f, 0.001f };
        const V3 BelowCar { -0.60f, -0.20f, 0.001f };
        const V3 Foot { CharacterRootA.X - 0.22f - Spec.SolarDirection.X * 0.1f, CharacterRootA.Y - 0.14f - Spec.SolarDirection.Y * 0.1f, 0.001f };
        Sunlit     = EvaluateContactPcssShadow(WorldA, CascadeA, OpenSun, { 0.0f, 0.0f, 1.0f }, Spec);
        UnderVehicle = EvaluateContactPcssShadow(WorldA, CascadeA, BelowCar, { 0.0f, 0.0f, 1.0f }, Spec);
        FootContact  = BlockerField.Evaluate(WorldA, Foot, { 0.0f, 0.0f, 1.0f }, Spec);
    }
    std::printf("Spot checks: open-sun %.3f | under-vehicle %.3f | foot-contact(blocker) %.3f\n", Sunlit, UnderVehicle, FootContact);

    {
        double UmbraC = 0.0, PenumbraC = 0.0, LitC = 0.0;
        double UmbraV = 0.0, PenumbraV = 0.0, LitV = 0.0;
        double UmbraB = 0.0, PenumbraB = 0.0, LitB = 0.0;
        BandedShadowError(VisibilityContactPcss, VisibilityReference, PrimaryA, UmbraC, PenumbraC, LitC);
        BandedShadowError(VisibilityVogelDisc, VisibilityReference, PrimaryA, UmbraV, PenumbraV, LitV);
        BandedShadowError(VisibilityBlockerField, VisibilityReference, PrimaryA, UmbraB, PenumbraB, LitB);
        std::printf("Banded error (umbra | penumbra | lit):\n");
        std::printf("  ContactPcss        %.4f | %.4f | %.4f\n", UmbraC, PenumbraC, LitC);
        std::printf("  VogelDisc8         %.4f | %.4f | %.4f\n", UmbraV, PenumbraV, LitV);
        std::printf("  SurfelBlockerField %.4f | %.4f | %.4f\n", UmbraB, PenumbraB, LitB);
    }

    // Cluster resolution sweep for the surfel blocker field: the disk set caps character-scale silhouette fidelity
    struct BlockerSweepRow
    {
        float  CellSize = 0.0f;
        std::size_t Disks = 0u;
        double Rmse = 0.0;
        double Milliseconds = 0.0;
    };
    std::vector<BlockerSweepRow> BlockerSweep {};
    for (float CellSize : { 0.28f, 0.14f, 0.09f, 0.055f })
    {
        SurfelBlockerField SweepField {};
        const auto SweepBuildStart = std::chrono::steady_clock::now();
        SweepField.Build(WorldA.Triangles, CellSize);
        const double SweepBuildMs = MillisecondsSince(SweepBuildStart);
        std::vector<float> SweepVisibility(PixelCount, 1.0f);
        const double SweepEvalMs = RunShadowField([&](const V3& P, const V3& N) { return SweepField.Evaluate(WorldA, P, N, Spec); }, SweepVisibility);
        BlockerSweep.push_back({ CellSize, SweepField.Disks.size(), ScalarRmse(SweepVisibility, VisibilityReference, PrimaryA), SweepBuildMs + SweepEvalMs });
    }
    std::printf("\nSurfel blocker field cluster sweep (build+evaluate ms):\n");
    for (const auto& Row : BlockerSweep)
    {
        std::printf("  cell %.3f m | %5zu disks | RMSE %.4f | %7.1f ms\n", Row.CellSize, Row.Disks, Row.Rmse, Row.Milliseconds);
    }

    //----------------------------------------------------------------------------------------------------------------
    // 3. GI candidates vs deterministic path trace reference
    //----------------------------------------------------------------------------------------------------------------
    std::vector<V3> GiReference(PixelCount);
    std::vector<V3> GiSh9(PixelCount);
    std::vector<V3> GiSurfelCascade(PixelCount);

    // GI #1: SH9 emissive form-factor field, two deterministic bounces for the indirect shelf
    IrradianceGrid Sh9Grid {};
    Sh9Grid.Allocate();
    {
        // Seed the SH9 shelf with emitter form factors evaluated at each cell origin
        for (uint32_t Z = 0u; Z < Sh9Grid.NZ; ++Z)
        {
            for (uint32_t Y = 0u; Y < Sh9Grid.NY; ++Y)
            {
                for (uint32_t X = 0u; X < Sh9Grid.NX; ++X)
                {
                    const V3 Origin = Sh9Grid.CellOrigin(X, Y, Z);
                    Sh9& Coeffs = Sh9Grid.Cells[(static_cast<std::size_t>(Z) * Sh9Grid.NY + Y) * Sh9Grid.NX + X];
                    for (uint32_t DirIndex = 0u; DirIndex < 64u; ++DirIndex)
                    {
                        const V3 Dir = OctahedralDirection(DirIndex);
                        const TraceHit Hit = WorldA.Tree.TraceClosest(Origin, Dir, 2.0e-3f, 30.0f);
                        V3 Radiance {};
                        if (Hit.Hit)
                        {
                            const Tri& HitTri = WorldA.Tree.SourceTriangles()[Hit.TriIndex];
                            const V3 HitNormal = (Dot(Hit.Normal, Dir) > 0.0f) ? Hit.Normal * -1.0f : Hit.Normal;
                            const V3 HitPoint = Origin + Dir * Hit.Distance;
                            V3 EmitterTerm {};
                            for (const Tri& Emitter : WorldA.Emitters)
                            {
                                const float FormFactor = TriangleFormFactor(HitPoint, HitNormal, Emitter.A, Emitter.B, Emitter.C);
                                EmitterTerm += ScaleColor(Emitter.Emission, V3 { FormFactor, FormFactor, FormFactor });
                            }
                            Radiance = ScaleColor(HitTri.Albedo, EmitterTerm + V3 { 0.012f, 0.014f, 0.020f });
                        }
                        const std::array<float, 9> Basis = EvaluateSh9Basis(Dir);
                        for (std::size_t C = 0u; C < 9u; ++C)
                        {
                            Coeffs.C0[C] += Radiance.X * Basis[C];
                            Coeffs.C1[C] += Radiance.Y * Basis[C];
                            Coeffs.C2[C] += Radiance.Z * Basis[C];
                        }
                    }
                    const float Normalization = TauFloat / 64.0f;
                    for (std::size_t C = 0u; C < 9u; ++C)
                    {
                        Coeffs.C0[C] *= Normalization;
                        Coeffs.C1[C] *= Normalization;
                        Coeffs.C2[C] *= Normalization;
                    }
                }
            }
        }
    }
    const auto GiSh9Start = std::chrono::steady_clock::now();
    EvaluateSh9EmissiveFormFactorGi(WorldA, Sh9Grid, Spec, PrimaryA, GiSh9);
    const double MsSh9 = MillisecondsSince(GiSh9Start);

    // GI #2: surfel cascade radiosity
    const auto SurfelBuildStart = std::chrono::steady_clock::now();
    std::vector<Surfel> Surfels {};
    {
        float TotalArea = 0.0f;
        for (const Tri& T : WorldA.Triangles)
        {
            if (T.Kind == 1u)
            {
                continue;
            }
            TotalArea += 0.5f * Length(Cross(T.B - T.A, T.C - T.A));
        }
        const float TargetArea = TotalArea / 1400.0f;
        for (const Tri& T : WorldA.Triangles)
        {
            if (T.Kind == 1u)
            {
                continue;
            }
            const float Area = 0.5f * Length(Cross(T.B - T.A, T.C - T.A));
            if (Area <= 1.0e-6f)
            {
                continue;
            }
            const int32_t Count = std::max(1, static_cast<int32_t>(std::round(Area / std::max(1.0e-6f, TargetArea))));
            const V3 TriNormal = Normalize(Cross(T.B - T.A, T.C - T.A));
            for (int32_t S = 0; S < Count; ++S)
            {
                const float U1 = static_cast<float>((S * 7) % 13) / 13.0f;
                const float U2 = static_cast<float>((S * 11) % 17) / 17.0f;
                const float RootU = std::sqrt(U1);
                const float B0 = 1.0f - RootU;
                const float B1 = RootU * (1.0f - U2);
                const float B2 = RootU * U2;
                Surfel NewSurfel {};
                NewSurfel.Position = T.A * B0 + T.B * B1 + T.C * B2;
                NewSurfel.Normal   = (NewSurfel.Position.Z < 0.02f) ? TriNormal : TriNormal;
                NewSurfel.Area     = Area / static_cast<float>(Count);
                NewSurfel.Albedo   = T.Albedo;
                // Direct emitter form factor for the pristine surfel radiance
                V3 EmitterTerm {};
                for (const Tri& Emitter : WorldA.Emitters)
                {
                    const float FormFactor = TriangleFormFactor(NewSurfel.Position, NewSurfel.Normal, Emitter.A, Emitter.B, Emitter.C);
                    if (FormFactor <= 1.0e-6f)
                    {
                        continue;
                    }
                    const V3 ToCenter = Normalize((Emitter.A + Emitter.B + Emitter.C) * (1.0f / 3.0f) - NewSurfel.Position);
                    const float Distance = Length((Emitter.A + Emitter.B + Emitter.C) * (1.0f / 3.0f) - NewSurfel.Position);
                    const bool Occluded = WorldA.Tree.AnyOccluded(NewSurfel.Position + NewSurfel.Normal * 2.0e-3f, ToCenter, 2.0e-3f, std::max(1.0e-3f, Distance - 4.0e-3f));
                    if (!Occluded)
                    {
                        EmitterTerm += ScaleColor(Emitter.Emission, V3 { FormFactor, FormFactor, FormFactor });
                    }
                }
                NewSurfel.Radiance = ScaleColor(NewSurfel.Albedo, EmitterTerm);
                Surfels.push_back(NewSurfel);
            }
        }
    }
    SurfelHashGrid SurfelHash {};
    SurfelHash.Build(Surfels, { -4.5f, -4.5f, -0.3f }, { 4.5f, 4.5f, 3.4f });
    SurfelCascadeVolume CascadeVolume {};
    CascadeVolume.Cells.assign(static_cast<std::size_t>(CascadeVolume.NX) * CascadeVolume.NY * CascadeVolume.NZ, {});
    CoarseRadianceShelf RadianceShelf {};
    SurfelCascadeVolume BuiltVolume {};
    const auto CascadeBuildStart = std::chrono::steady_clock::now();
    RadianceShelf.Build(Surfels);
    BuildSurfelCascade(WorldA, Surfels, RadianceShelf, CascadeVolume, 24.0f, BuiltVolume);
    // One deterministic multi-bounce pass: surfels re-emit from the shelf, then rebuild
    for (Surfel& S : Surfels)
    {
        const V3 Incoming = SampleSurfelCascade(BuiltVolume, S.Position, S.Normal, 0.05f);
        S.Radiance = S.Radiance + ScaleColor(S.Albedo, Incoming) * 0.55f;
    }
    RadianceShelf.Build(Surfels);
    BuildSurfelCascade(WorldA, Surfels, RadianceShelf, CascadeVolume, 24.0f, BuiltVolume);
    const double MsCascadeBuild = MillisecondsSince(CascadeBuildStart);
    const double MsSurfelBuild = MillisecondsSince(SurfelBuildStart);

    const auto GiCascadeStart = std::chrono::steady_clock::now();
    for (std::size_t I = 0; I < PixelCount; ++I)
    {
        if (!PrimaryA[I].Valid)
        {
            continue;
        }
        const V3 Incoming = SampleSurfelCascadeCosineConvolved(BuiltVolume, PrimaryA[I].Position, PrimaryA[I].Normal, 0.03f);
        GiSurfelCascade[I] = ScaleColor(PrimaryA[I].Albedo, Incoming);
    }
    const double MsSurfelCascade = MillisecondsSince(GiCascadeStart);
    std::printf("Surfel cascade: %zu surfels, cascade build %.1f ms, per-pixel lookup %.1f ms (total %.1f ms)\n",
                Surfels.size(), MsCascadeBuild, MsSurfelCascade, MsSurfelBuild);

    // Reference path trace
    const auto GiReferenceStart = std::chrono::steady_clock::now();
    PathTraceSettings TraceSettings {};
    TraceSettings.SamplesPerPixel = 48u;
    TraceSettings.BounceLimit = 3u;
    TraceSettings.SolarNeeEnabled = false; // emissive transport only: the quantity a GTX GI candidate must reproduce
    ParallelRows(Height, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t Y = Y0; Y < Y1; ++Y)
        {
            for (uint32_t X = 0u; X < Width; ++X)
            {
                const std::size_t Index = static_cast<std::size_t>(Y) * Width + X;
                if (!PrimaryA[Index].Valid)
                {
                    continue;
                }
                GiReference[Index] = TraceDeterministicPath(WorldA, Spec, TraceSettings, PrimaryA[Index], X, Y);
            }
        }
    });
    const double MsReferencePathTrace = MillisecondsSince(GiReferenceStart);

    const double RmseGiSh9 = ColorRmse(GiSh9, GiReference, PrimaryA);
    const double RmseGiCascade = ColorRmse(GiSurfelCascade, GiReference, PrimaryA);
    std::printf("\nGI radiance RMSE vs 48-spp deterministic EMISSIVE-ONLY path trace:\n");
    std::printf("  Sh9EmissiveFormFactor  %.5f  (%.1f ms)\n", RmseGiSh9, MsSh9);
    std::printf("  SurfelCascadeRadiosity %.5f  (%.1f ms incl. surfels %.1f)\n", RmseGiCascade, MsSurfelCascade, MsSurfelBuild);
    std::printf("  Reference path trace   %.1f ms @ 48 spp\n", MsReferencePathTrace);

    //----------------------------------------------------------------------------------------------------------------
    // 4. Deformed pose B shadow footprint
    //----------------------------------------------------------------------------------------------------------------
    StudioWorld WorldB = ConstructStudio(Spec, 1.0f, CharacterRootB);
    const auto PrimaryB = TracePrimary(WorldB, Camera, Width, Height);
    std::vector<float> VisibilityPoseB(PixelCount, 1.0f);
    {
        const ShadowCascade CascadeB = BuildShadowCascade(WorldB.Tree, Spec.SolarDirection, { 0.0f, 0.0f, 0.9f }, 4.2f, 1024u);
        ParallelRows(Height, [&](uint32_t Y0, uint32_t Y1)
        {
            for (uint32_t Y = Y0; Y < Y1; ++Y)
            {
                for (uint32_t X = 0u; X < Width; ++X)
                {
                    const std::size_t Index = static_cast<std::size_t>(Y) * Width + X;
                    if (!PrimaryB[Index].Valid)
                    {
                        continue;
                    }
                    VisibilityPoseB[Index] = EvaluateContactPcssShadow(WorldB, CascadeB, PrimaryB[Index].Position, PrimaryB[Index].Normal, Spec);
                }
            }
        });
    }
    const double RmsePoseShift = ScalarRmse(VisibilityPoseB, VisibilityContactPcss, PrimaryA);
    std::printf("Pose A vs Pose B shadow footprint RMSE: %.4f (must be non-zero for a live deforming character)\n", RmsePoseShift);

    // Ray budget sweep for the BVH shadow candidate: where does GTX land?
    struct RaySweepRow { uint32_t Rays; double Rmse; double Milliseconds; };
    std::vector<RaySweepRow> RaySweep {};
    for (uint32_t Rays : { 1u, 2u, 4u, 8u, 16u })
    {
        std::vector<float> SweepVisibility(PixelCount, 1.0f);
        const double SweepMs = RunShadowField([&](const V3& P, const V3& N) { return EvaluateVogelDiscShadow(WorldA, P, N, Spec, Rays); }, SweepVisibility);
        RaySweep.push_back({ Rays, ScalarRmse(SweepVisibility, VisibilityReference, PrimaryA), SweepMs });
    }
    std::printf("\nBVH solar disc ray budget sweep:\n");
    for (const auto& Row : RaySweep)
    {
        std::printf("  %2u ray(s) | RMSE %.4f | %7.1f ms\n", Row.Rays, Row.Rmse, Row.Milliseconds);
    }

    // Normalised GI agreement (absolute radiance error is dominated by scale, so report both)
    double RelSh9 = 0.0, MeanSh9 = 0.0, MeanRef = 0.0, CorrSh9 = 0.0;
    double RelCascade = 0.0, MeanCascade = 0.0, CorrCascade = 0.0;
    ColorErrorSummary(GiSh9, GiReference, PrimaryA, RelSh9, MeanSh9, MeanRef, CorrSh9);
    double UnusedMeanRef = 0.0;
    ColorErrorSummary(GiSurfelCascade, GiReference, PrimaryA, RelCascade, MeanCascade, UnusedMeanRef, CorrCascade);
    std::printf("\nGI agreement (reference mean luminance %.4f):\n", MeanRef);
    std::printf("  Sh9EmissiveFormFactor  rel %5.1f%% | mean %5.3f | corr %5.3f\n", RelSh9 * 100.0, MeanSh9, CorrSh9);
    std::printf("  SurfelCascadeRadiosity rel %5.1f%% | mean %5.3f | corr %5.3f\n", RelCascade * 100.0, MeanCascade, CorrCascade);

    //----------------------------------------------------------------------------------------------------------------
    // 5. Determinism proof (repeat evaluation -> identical hashes)
    //----------------------------------------------------------------------------------------------------------------
    std::vector<float> VisibilityRepeat(PixelCount, 1.0f);
    ParallelRows(Height, [&](uint32_t Y0, uint32_t Y1)
    {
        for (uint32_t Y = Y0; Y < Y1; ++Y)
        {
            for (uint32_t X = 0u; X < Width; ++X)
            {
                const std::size_t Index = static_cast<std::size_t>(Y) * Width + X;
                if (!PrimaryA[Index].Valid)
                {
                    continue;
                }
                VisibilityRepeat[Index] = EvaluateContactPcssShadow(WorldA, CascadeA, PrimaryA[Index].Position, PrimaryA[Index].Normal, Spec);
            }
        }
    });
    const uint64_t HashFirst  = HashFloats(VisibilityContactPcss);
    const uint64_t HashSecond = HashFloats(VisibilityRepeat);
    const uint64_t HashGi     = HashColors(GiSurfelCascade);
    std::printf("Determinism: shadow hash %016llX vs %016llX -> %s\n",
                static_cast<unsigned long long>(HashFirst), static_cast<unsigned long long>(HashSecond),
                (HashFirst == HashSecond) ? "IDENTICAL" : "MISMATCH");

    //----------------------------------------------------------------------------------------------------------------
    // 6. Write sheets and telemetry
    //----------------------------------------------------------------------------------------------------------------
    const auto LitFrame = [&](const std::vector<float>& Visibility, const std::vector<V3>& Gi, float Exposure)
    {
        std::vector<V3> Result(PixelCount);
        const V3 SunDir = Normalize(Spec.SolarDirection);
        for (std::size_t I = 0; I < PixelCount; ++I)
        {
            if (!PrimaryA[I].Valid)
            {
                continue;
            }
            const float CosSun = std::max(0.0f, Dot(PrimaryA[I].Normal, SunDir));
            const V3 Solar = ScaleColor(
                ScaleColor(PrimaryA[I].Albedo, Spec.SolarIrradiance),
                V3 { CosSun * Visibility[I] / PiFloat, CosSun * Visibility[I] / PiFloat, CosSun * Visibility[I] / PiFloat });
            const V3 Ambient = ScaleColor(PrimaryA[I].Albedo, V3 { 0.030f, 0.036f, 0.048f });
            Result[I] = Solar + Gi[I] + Ambient;
            (void)Exposure;
        }
        return RadianceToRgba(Result, PrimaryA, Exposure);
    };

    std::vector<V3> ZeroGi(PixelCount);
    const std::vector<SheetCell> ShadowCells {
        { "REFERENCE 128 RAYS", "VOGEL SOLAR DISC REFERENCE", { 115u, 225u, 150u }, ShadowMaskToRgba(VisibilityReference) },
        { "CONTACT PCSS",       "1024 CASCADE + 0.24 M CONTACT", { 105u, 185u, 245u }, ShadowMaskToRgba(VisibilityContactPcss) },
        { "VOGEL DISC 8 RAYS",  "EXACT TRIANGLE VISIBILITY", { 245u, 190u, 95u }, ShadowMaskToRgba(VisibilityVogelDisc) },
        { "SURFEL BLOCKERS",    "STRATIFIED DISKS + 0.28 M RAY", { 225u, 90u, 85u }, ShadowMaskToRgba(VisibilityBlockerField) },
    };
    SaveGridSheet(OutDir / "01_ShadowVisibilityComparison.png",
                  "SOLAR SHADOW CANDIDATES - ISOLATED VISIBILITY, REAL TRIANGLE GEOMETRY",
                  2u, 2u, Width, Height, ShadowCells);

    const std::vector<SheetCell> GiCells {
        { "PATH TRACE REFERENCE", "48 SPP EMISSIVE ONLY SOBOL", { 115u, 225u, 150u }, LitFrame(VisibilityReference, GiReference, 1.0f) },
        { "GI 1 SH9 FORM FACTOR", "EXACT EMITTER FF + SH9 SHELF", { 105u, 185u, 245u }, LitFrame(VisibilityContactPcss, GiSh9, 1.0f) },
        { "GI 2 SURFEL CASCADE", "OCTAHEDRAL + CHEBYSHEV, 0 RAYS PX", { 110u, 220u, 155u }, LitFrame(VisibilityContactPcss, GiSurfelCascade, 1.0f) },
        { "CONTACT PCSS ONLY",   "NO INDIRECT TERM", { 245u, 190u, 95u }, LitFrame(VisibilityContactPcss, ZeroGi, 1.0f) },
    };
    SaveGridSheet(OutDir / "02_GiCandidatesComparison.png",
                  "GTX GI CANDIDATES - FULL LIT, ZERO DENOISER",
                  2u, 2u, Width, Height, GiCells);

    const std::vector<SheetCell> PoseCells {
        { "POSE A CONTACT PCSS", "STANDING STRIDE", { 105u, 185u, 245u }, ShadowMaskToRgba(VisibilityContactPcss) },
        { "POSE B CONTACT PCSS", "SPRINT REACH", { 195u, 145u, 245u }, ShadowMaskToRgba(VisibilityPoseB) },
        { "POSE A LIT + GI 2",   "SURFEL CASCADE", { 110u, 220u, 155u }, LitFrame(VisibilityContactPcss, GiSurfelCascade, 1.0f) },
        { "POSE B LIT + PCSS",   "DEFORMED SILHOUETTE", { 245u, 190u, 95u }, LitFrame(VisibilityPoseB, GiSurfelCascade, 1.0f) },
    };
    SaveGridSheet(OutDir / "03_DeformingPoseShadowShift.png",
                  "LIVE DEFORMING FIGURE - REAL TRIANGLE REGENERATION, ZERO VOXEL REBAKE",
                  2u, 2u, Width, Height, PoseCells);

    SavePngRgba(OutDir / "04_ReferencePathTrace.png", Width, Height, LitFrame(VisibilityReference, GiReference, 1.0f));
    SavePngRgba(OutDir / "05_CandidateContactPcss_Sh9Gi.png", Width, Height, LitFrame(VisibilityContactPcss, GiSh9, 1.0f));
    SavePngRgba(OutDir / "06_CandidateContactPcss_SurfelCascadeGi.png", Width, Height, LitFrame(VisibilityContactPcss, GiSurfelCascade, 1.0f));

    {
        std::ofstream Csv(OutDir / "lighting_candidate_metrics.csv");
        Csv << "family,candidate,metric,value\n";
        Csv << "shadow,ContactPcss,rmse_visibility," << RmseContactPcss << "\n";
        Csv << "shadow,VogelDisc8,rmse_visibility," << RmseVogelDisc << "\n";
        Csv << "shadow,SurfelBlockerField,rmse_visibility," << RmseBlocker << "\n";
        Csv << "shadow,Reference128,rmse_visibility,0\n";
        Csv << "shadow,ContactPcss,milliseconds," << MsContactPcss << "\n";
        Csv << "shadow,VogelDisc8,milliseconds," << MsVogelDisc << "\n";
        Csv << "shadow,SurfelBlockerField,milliseconds," << MsBlockerField << "\n";
        Csv << "shadow,Reference128,milliseconds," << MsReference << "\n";
        Csv << "gi,Sh9EmissiveFormFactor,rmse_radiance," << RmseGiSh9 << "\n";
        Csv << "gi,SurfelCascadeRadiosity,rmse_radiance," << RmseGiCascade << "\n";
        Csv << "gi,Sh9EmissiveFormFactor,milliseconds," << MsSh9 << "\n";
        Csv << "gi,SurfelCascadeRadiosity,milliseconds," << (MsSurfelCascade + MsSurfelBuild) << "\n";
        Csv << "gi,ReferencePathTrace,milliseconds," << MsReferencePathTrace << "\n";
        for (const auto& Row : BlockerSweep)
        {
            Csv << "shadow_sweep,SurfelBlockerField_cell_" << Row.CellSize << ",rmse_visibility," << Row.Rmse << "\n";
            Csv << "shadow_sweep,SurfelBlockerField_cell_" << Row.CellSize << ",disks," << Row.Disks << "\n";
            Csv << "shadow_sweep,SurfelBlockerField_cell_" << Row.CellSize << ",milliseconds," << Row.Milliseconds << "\n";
        }
        for (const auto& Row : RaySweep)
        {
            Csv << "shadow_ray_sweep,VogelDisc" << Row.Rays << ",rmse_visibility," << Row.Rmse << "\n";
            Csv << "shadow_ray_sweep,VogelDisc" << Row.Rays << ",milliseconds," << Row.Milliseconds << "\n";
        }
        Csv << "gi,Sh9EmissiveFormFactor,relative_rmse," << RelSh9 << "\n";
        Csv << "gi,SurfelCascadeRadiosity,relative_rmse," << RelCascade << "\n";
        Csv << "gi,Reference,mean_luminance," << MeanRef << "\n";
        Csv << "deform,RealTrianglePoseUpdate,milliseconds," << DeformMilliseconds << "\n";
        Csv << "deform,PoseShiftRmse,rmse_visibility," << RmsePoseShift << "\n";
    }
    {
        std::ofstream Json(OutDir / "lighting_candidate_summary.json");
        Json << "{\n";
        Json << "  \"viewport\": [" << Width << ", " << Height << "],\n";
        Json << "  \"triangles\": " << WorldA.Triangles.size() << ",\n";
        Json << "  \"emitters\": " << WorldA.Emitters.size() << ",\n";
        Json << "  \"surfels\": " << Surfels.size() << ",\n";
        Json << "  \"blocker_disks\": " << BlockerField.Disks.size() << ",\n";
        Json << "  \"pose_update_ms\": " << DeformMilliseconds << ",\n";
        Json << "  \"shadow_rmse\": {\"contact_pcss\": " << RmseContactPcss
             << ", \"vogel_disc_8\": " << RmseVogelDisc
             << ", \"surfel_blockers\": " << RmseBlocker << "},\n";
        Json << "  \"shadow_ms\": {\"contact_pcss\": " << MsContactPcss
             << ", \"vogel_disc_8\": " << MsVogelDisc
             << ", \"surfel_blockers\": " << MsBlockerField
             << ", \"reference_128\": " << MsReference
             << ", \"cascade_build\": " << CascadeMilliseconds << "},\n";
        Json << "  \"gi_rmse\": {\"sh9_form_factor\": " << RmseGiSh9
             << ", \"surfel_cascade\": " << RmseGiCascade << "},\n";
        Json << "  \"gi_ms\": {\"sh9_form_factor\": " << MsSh9
             << ", \"surfel_cascade\": " << (MsSurfelCascade + MsSurfelBuild)
             << ", \"cascade_build\": " << MsCascadeBuild
             << ", \"reference_path_trace\": " << MsReferencePathTrace << "},\n";
        Json << "  \"blocker_cluster_sweep\": [";
        for (std::size_t I = 0; I < BlockerSweep.size(); ++I)
        {
            Json << (I == 0u ? "" : ", ") << "{\"cell_m\": " << BlockerSweep[I].CellSize
                 << ", \"disks\": " << BlockerSweep[I].Disks
                 << ", \"rmse\": " << BlockerSweep[I].Rmse
                 << ", \"ms\": " << BlockerSweep[I].Milliseconds << "}";
        }
        Json << "],\n";
        Json << "  \"shadow_ray_sweep\": [";
        for (std::size_t I = 0; I < RaySweep.size(); ++I)
        {
            Json << (I == 0u ? "" : ", ") << "{\"rays\": " << RaySweep[I].Rays
                 << ", \"rmse\": " << RaySweep[I].Rmse << ", \"ms\": " << RaySweep[I].Milliseconds << "}";
        }
        Json << "],\n";
        Json << "  \"gi_relative_rmse\": {\"sh9_form_factor\": " << RelSh9
             << ", \"surfel_cascade\": " << RelCascade << "},\n";
        Json << "  \"gi_mean_luminance\": {\"sh9_form_factor\": " << MeanSh9
             << ", \"surfel_cascade\": " << MeanCascade << ", \"reference\": " << MeanRef << "},\n";
        Json << "  \"gi_correlation\": {\"sh9_form_factor\": " << CorrSh9
             << ", \"surfel_cascade\": " << CorrCascade << "},\n";
        Json << "  \"pose_shift_rmse\": " << RmsePoseShift << ",\n";
        Json << "  \"determinism\": {\"shadow_hash\": " << HashFirst
             << ", \"shadow_hash_repeat\": " << HashSecond
             << ", \"gi_hash\": " << HashGi << ", \"identical\": " << ((HashFirst == HashSecond) ? "true" : "false") << "}\n";
        Json << "}\n";
    }

    std::printf("\nArtifacts written to %s\n", OutDir.string().c_str());
    std::printf("Total harness time %.1f s\n", MillisecondsSince(SectionStart) / 1000.0);
    return 0;
}
