/*============================================================================================================================================
                                                        REVOLUTIONSWEEPPRIMITIVES.CPP
============================================================================================================================================*/
// 🧩 Implementation of lathe revolution, linear extrusion, and twist-minimized Bishop parallel-transport profile sweeps

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Primitives/RevolutionSweepPrimitives.h"
#include "Engine/Constants/MathConstants.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace Frontier::GeometrySpace
{

namespace
{

[[nodiscard]] float EvaluateContourPerimeter2D(std::span<const Mathematics::Axis2D> Contour, bool ClosedLoop) noexcept
{
    if (Contour.size() < 2u)
    {
        return 1.0f;
    }
    float TotalLength = 0.0f;
    for (std::size_t Idx = 0u; Idx + 1u < Contour.size(); ++Idx)
    {
        const Mathematics::Axis2D Delta = Contour[Idx + 1u] - Contour[Idx];
        TotalLength += std::sqrt(Mathematics::EvaluateDot(Delta, Delta));
    }
    if (ClosedLoop)
    {
        const Mathematics::Axis2D Delta = Contour.front() - Contour.back();
        TotalLength += std::sqrt(Mathematics::EvaluateDot(Delta, Delta));
    }
    return std::max(1.0e-5f, TotalLength);
}

[[nodiscard]] float EvaluateSignedPolygonArea2D(std::span<const Mathematics::Axis2D> Contour) noexcept
{
    if (Contour.size() < 3u)
    {
        return 0.0f;
    }
    float TwiceArea = 0.0f;
    for (std::size_t Idx = 0u; Idx < Contour.size(); ++Idx)
    {
        const auto& Curr = Contour[Idx];
        const auto& Next = Contour[(Idx + 1u) % Contour.size()];
        TwiceArea += Curr.X * Next.Y - Next.X * Curr.Y;
    }
    return 0.5f * TwiceArea;
}

[[nodiscard]] bool QueryPointInsideTriangle2D(
    Mathematics::Axis2D CornerA,
    Mathematics::Axis2D CornerB,
    Mathematics::Axis2D CornerC,
    Mathematics::Axis2D Candidate) noexcept
{
    const auto Cross2 = [](Mathematics::Axis2D Origin, Mathematics::Axis2D Dest, Mathematics::Axis2D Test) noexcept -> float
    {
        return (Dest.X - Origin.X) * (Test.Y - Origin.Y) - (Dest.Y - Origin.Y) * (Test.X - Origin.X);
    };
    const float SideAB = Cross2(CornerA, CornerB, Candidate);
    const float SideBC = Cross2(CornerB, CornerC, Candidate);
    const float SideCA = Cross2(CornerC, CornerA, Candidate);
    return (SideAB >= -1.0e-6f) && (SideBC >= -1.0e-6f) && (SideCA >= -1.0e-6f);
}

// 📝 Ear-clipping triangulation of simple planar 2D polygons (supports concave cross-sections!)
[[nodiscard]] std::vector<std::array<uint32_t, 3u>> TriangulatePlanarContourCCW(
    std::span<const Mathematics::Axis2D> Contour)
{
    std::vector<std::array<uint32_t, 3u>> Triangles;
    if (Contour.size() < 3u)
    {
        return Triangles;
    }

    std::vector<uint32_t> ActiveIndices(Contour.size());
    const bool CounterClockwise = EvaluateSignedPolygonArea2D(Contour) >= 0.0f;
    for (std::size_t Idx = 0u; Idx < Contour.size(); ++Idx)
    {
        ActiveIndices[Idx] = static_cast<uint32_t>(CounterClockwise ? Idx : (Contour.size() - 1u - Idx));
    }

    Triangles.reserve(Contour.size() - 2u);
    uint32_t GuardCounter = 0u;
    const uint32_t MaxGuard = static_cast<uint32_t>(Contour.size() * Contour.size() * 2u + 16u);

    while (ActiveIndices.size() > 3u && GuardCounter < MaxGuard)
    {
        ++GuardCounter;
        bool EarClipped = false;
        const std::size_t Count = ActiveIndices.size();

        for (std::size_t Pos = 0u; Pos < Count; ++Pos)
        {
            const uint32_t PrevIdx = ActiveIndices[(Pos + Count - 1u) % Count];
            const uint32_t CurrIdx = ActiveIndices[Pos];
            const uint32_t NextIdx = ActiveIndices[(Pos + 1u) % Count];

            const Mathematics::Axis2D CornerA = Contour[PrevIdx];
            const Mathematics::Axis2D CornerB = Contour[CurrIdx];
            const Mathematics::Axis2D CornerC = Contour[NextIdx];

            const float TwiceArea = (CornerB.X - CornerA.X) * (CornerC.Y - CornerA.Y) -
                                    (CornerB.Y - CornerA.Y) * (CornerC.X - CornerA.X);
            if (TwiceArea <= 1.0e-7f)
            {
                continue;
            }

            bool ContainsReflexVertex = false;
            for (std::size_t Other = 0u; Other < Count; ++Other)
            {
                const uint32_t TestIdx = ActiveIndices[Other];
                if (TestIdx == PrevIdx || TestIdx == CurrIdx || TestIdx == NextIdx)
                {
                    continue;
                }
                if (QueryPointInsideTriangle2D(CornerA, CornerB, CornerC, Contour[TestIdx]))
                {
                    ContainsReflexVertex = true;
                    break;
                }
            }

            if (!ContainsReflexVertex)
            {
                Triangles.push_back({ PrevIdx, CurrIdx, NextIdx });
                ActiveIndices.erase(ActiveIndices.begin() + static_cast<std::ptrdiff_t>(Pos));
                EarClipped = true;
                break;
            }
        }

        if (!EarClipped)
        {
            // 📝 Fallback fan if numerical self-touching prevents strict ear isolation
            for (std::size_t Pos = 1u; Pos + 1u < ActiveIndices.size(); ++Pos)
            {
                Triangles.push_back({ ActiveIndices[0u], ActiveIndices[Pos], ActiveIndices[Pos + 1u] });
            }
            ActiveIndices.clear();
            break;
        }
    }

    if (ActiveIndices.size() == 3u)
    {
        Triangles.push_back({ ActiveIndices[0u], ActiveIndices[1u], ActiveIndices[2u] });
    }
    return Triangles;
}

void ConstructOrthonormalBasisAroundAxis(
    Mathematics::Axis3D  UnitAxis,
    Mathematics::Axis3D& OutRight,
    Mathematics::Axis3D& OutForward) noexcept
{
    const Mathematics::Axis3D Reference = (std::fabs(UnitAxis.Z) < 0.92f)
        ? Mathematics::Axis3D{ 0.0f, 0.0f, 1.0f }
        : Mathematics::Axis3D{ 1.0f, 0.0f, 0.0f };
    OutRight   = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(UnitAxis, Reference), { 1.0f, 0.0f, 0.0f });
    OutForward = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(OutRight, UnitAxis), { 0.0f, 0.0f, 1.0f });
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    LATHE REVOLUTION
//------------------------------------------------------------------------------------------------------------------------

void AppendLatheRevolutionPrimitive(
    PolygonNetwork&                       TargetNetwork,
    Mathematics::Axis3D                   AxisOrigin,
    Mathematics::Axis3D                   AxisDirection,
    std::span<const LatheProfileWaypoint> ProfileWaypoints,
    uint32_t                              RadialSegments,
    float                                 SweepAngleRadians,
    bool                                  ClosePartialCaps,
    UVPatchBounds                         UVRegion,
    const FaceShadingAssignment&          Shading,
    uint16_t                              PriorityLayerOrdinal,
    uint16_t                              PartGroupOrdinal)
{
    if (ProfileWaypoints.size() < 2u)
    {
        return;
    }

    const Mathematics::Axis3D UnitAxis = Mathematics::NormalizeAxis(AxisDirection, { 0.0f, 1.0f, 0.0f });
    Mathematics::Axis3D RightAxis {};
    Mathematics::Axis3D ForwardAxis {};
    ConstructOrthonormalBasisAroundAxis(UnitAxis, RightAxis, ForwardAxis);

    const float ClampedSweep = std::clamp(std::fabs(SweepAngleRadians), 1.0e-3f, Constants::TauFloat);
    const bool  FullCircle   = (ClampedSweep >= Constants::TauFloat - 1.0e-4f);
    const uint32_t SegCount  = std::max(FullCircle ? 6u : 2u, RadialSegments);
    const uint32_t ColCount  = FullCircle ? SegCount : (SegCount + 1u);

    std::vector<float> CumulativeArc(ProfileWaypoints.size(), 0.0f);
    for (std::size_t Idx = 1u; Idx < ProfileWaypoints.size(); ++Idx)
    {
        const float dR = ProfileWaypoints[Idx].RadialOffsetMeters - ProfileWaypoints[Idx - 1u].RadialOffsetMeters;
        const float dY = ProfileWaypoints[Idx].AxialHeightMeters - ProfileWaypoints[Idx - 1u].AxialHeightMeters;
        CumulativeArc[Idx] = CumulativeArc[Idx - 1u] + std::sqrt(dR * dR + dY * dY);
    }
    const float TotalArc = std::max(1.0e-5f, CumulativeArc.back());

    // 📝 Orient profile bottom-to-top so right-hand CCW quads point outward from the revolution axis
    const float SignedHeightSpan = ProfileWaypoints.back().AxialHeightMeters - ProfileWaypoints.front().AxialHeightMeters;
    const bool  ReverseOrder     = (SignedHeightSpan < 0.0f);

    const float OrigU = UVRegion.Minimum.U + (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.03f;
    const float OrigV = UVRegion.Minimum.V + (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.03f;
    const float SpanU = (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.94f;
    const float SpanV = (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.94f;

    std::vector<std::vector<uint32_t>> RingVertexGrid(ProfileWaypoints.size());

    for (std::size_t RawIdx = 0u; RawIdx < ProfileWaypoints.size(); ++RawIdx)
    {
        const std::size_t SrcIdx = ReverseOrder ? (ProfileWaypoints.size() - 1u - RawIdx) : RawIdx;
        const float Radius = std::max(0.0f, ProfileWaypoints[SrcIdx].RadialOffsetMeters);
        const float AxialY = ProfileWaypoints[SrcIdx].AxialHeightMeters;

        RingVertexGrid[RawIdx].resize(ColCount);
        if (Radius <= 1.0e-6f)
        {
            const uint32_t PoleVertex = TargetNetwork.AppendVertex(AxisOrigin + UnitAxis * AxialY);
            std::fill(RingVertexGrid[RawIdx].begin(), RingVertexGrid[RawIdx].end(), PoleVertex);
        }
        else
        {
            for (uint32_t Col = 0u; Col < ColCount; ++Col)
            {
                const float Fraction = static_cast<float>(Col) / static_cast<float>(SegCount);
                const float Phi      = Fraction * ClampedSweep;
                const auto  Radial   = RightAxis * (Radius * std::cos(Phi)) - ForwardAxis * (Radius * std::sin(Phi));
                RingVertexGrid[RawIdx][Col] = TargetNetwork.AppendVertex(AxisOrigin + UnitAxis * AxialY + Radial);
            }
        }
    }

    for (std::size_t Row = 0u; Row + 1u < ProfileWaypoints.size(); ++Row)
    {
        const std::size_t SrcRow0 = ReverseOrder ? (ProfileWaypoints.size() - 1u - Row) : Row;
        const std::size_t SrcRow1 = ReverseOrder ? (ProfileWaypoints.size() - 2u - Row) : (Row + 1u);

        const float V0 = OrigV + SpanV * (CumulativeArc[SrcRow0] / TotalArc);
        const float V1 = OrigV + SpanV * (CumulativeArc[SrcRow1] / TotalArc);
        const bool  LowerPole = (ProfileWaypoints[SrcRow0].RadialOffsetMeters <= 1.0e-6f);
        const bool  UpperPole = (ProfileWaypoints[SrcRow1].RadialOffsetMeters <= 1.0e-6f);

        if (LowerPole && UpperPole)
        {
            continue;
        }

        for (uint32_t Seg = 0u; Seg < SegCount; ++Seg)
        {
            const uint32_t NextCol = FullCircle ? ((Seg + 1u) % ColCount) : (Seg + 1u);
            const float    U0      = OrigU + SpanU * (static_cast<float>(Seg) / static_cast<float>(SegCount));
            const float    U1      = OrigU + SpanU * (static_cast<float>(Seg + 1u) / static_cast<float>(SegCount));

            uint32_t CreatedFace = InvalidNetworkIndex;
            if (LowerPole)
            {
                const std::array<uint32_t, 3u> Tri {{
                    RingVertexGrid[Row][0u],
                    RingVertexGrid[Row + 1u][NextCol],
                    RingVertexGrid[Row + 1u][Seg],
                }};
                const std::array<Mathematics::UVAxes, 3u> UV {{ { 0.5f * (U0 + U1), V0 }, { U1, V1 }, { U0, V1 } }};
                CreatedFace = TargetNetwork.AppendFace(Tri, UV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            }
            else if (UpperPole)
            {
                const std::array<uint32_t, 3u> Tri {{
                    RingVertexGrid[Row][Seg],
                    RingVertexGrid[Row][NextCol],
                    RingVertexGrid[Row + 1u][0u],
                }};
                const std::array<Mathematics::UVAxes, 3u> UV {{ { U0, V0 }, { U1, V0 }, { 0.5f * (U0 + U1), V1 } }};
                CreatedFace = TargetNetwork.AppendFace(Tri, UV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            }
            else
            {
                const std::array<uint32_t, 4u> Quad {{
                    RingVertexGrid[Row][Seg],
                    RingVertexGrid[Row][NextCol],
                    RingVertexGrid[Row + 1u][NextCol],
                    RingVertexGrid[Row + 1u][Seg],
                }};
                const std::array<Mathematics::UVAxes, 4u> UV {{ { U0, V0 }, { U1, V0 }, { U1, V1 }, { U0, V1 } }};
                CreatedFace = TargetNetwork.AppendFace(Quad, UV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            }

            if (CreatedFace != InvalidNetworkIndex && ProfileWaypoints[SrcRow0].SharpRingCrease && !LowerPole)
            {
                const uint32_t EntryEdge = TargetNetwork.Faces[CreatedFace].BoundaryLoopEdgeIndex;
                TargetNetwork.Edges[EntryEdge].AttributeMask =
                    TargetNetwork.Edges[EntryEdge].AttributeMask | EdgeAttributeMask::EnableSharpCrease;
            }
        }
    }

    if (!FullCircle && ClosePartialCaps && ProfileWaypoints.size() >= 2u)
    {
        std::vector<Mathematics::Axis2D> CapContour;
        CapContour.reserve(ProfileWaypoints.size() + 2u);
        std::vector<uint32_t> StartMeridianVertices;
        std::vector<uint32_t> EndMeridianVertices;

        const std::size_t LastRow = ProfileWaypoints.size() - 1u;
        const std::size_t SrcBot  = ReverseOrder ? LastRow : 0u;
        const std::size_t SrcTop  = ReverseOrder ? 0u : LastRow;

        if (ProfileWaypoints[SrcBot].RadialOffsetMeters > 1.0e-6f)
        {
            const uint32_t BotCenter = TargetNetwork.AppendVertex(AxisOrigin + UnitAxis * ProfileWaypoints[SrcBot].AxialHeightMeters);
            CapContour.push_back({ 0.0f, ProfileWaypoints[SrcBot].AxialHeightMeters });
            StartMeridianVertices.push_back(BotCenter);
            EndMeridianVertices.push_back(BotCenter);
        }

        for (std::size_t Row = 0u; Row < ProfileWaypoints.size(); ++Row)
        {
            const std::size_t SrcIdx = ReverseOrder ? (LastRow - Row) : Row;
            CapContour.push_back({
                std::max(0.0f, ProfileWaypoints[SrcIdx].RadialOffsetMeters),
                ProfileWaypoints[SrcIdx].AxialHeightMeters
            });
            StartMeridianVertices.push_back(RingVertexGrid[Row].front());
            EndMeridianVertices.push_back(RingVertexGrid[Row].back());
        }

        if (ProfileWaypoints[SrcTop].RadialOffsetMeters > 1.0e-6f)
        {
            const uint32_t TopCenter = TargetNetwork.AppendVertex(AxisOrigin + UnitAxis * ProfileWaypoints[SrcTop].AxialHeightMeters);
            CapContour.push_back({ 0.0f, ProfileWaypoints[SrcTop].AxialHeightMeters });
            StartMeridianVertices.push_back(TopCenter);
            EndMeridianVertices.push_back(TopCenter);
        }

        const auto CapTriangles = TriangulatePlanarContourCCW(CapContour);
        for (const auto& Tri : CapTriangles)
        {
            const std::array<uint32_t, 3u> StartLoop {{
                StartMeridianVertices[Tri[0u]],
                StartMeridianVertices[Tri[1u]],
                StartMeridianVertices[Tri[2u]],
            }};
            const std::array<uint32_t, 3u> EndLoop {{
                EndMeridianVertices[Tri[0u]],
                EndMeridianVertices[Tri[2u]],
                EndMeridianVertices[Tri[1u]],
            }};
            if (StartLoop[0u] != StartLoop[1u] && StartLoop[1u] != StartLoop[2u] && StartLoop[2u] != StartLoop[0u])
            {
                const std::array<Mathematics::UVAxes, 3u> CapUV {{ { OrigU, OrigV }, { OrigU + 0.2f * SpanU, OrigV }, { OrigU, OrigV + 0.2f * SpanV } }};
                (void)TargetNetwork.AppendFace(StartLoop, CapUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            }
            if (EndLoop[0u] != EndLoop[1u] && EndLoop[1u] != EndLoop[2u] && EndLoop[2u] != EndLoop[0u])
            {
                const std::array<Mathematics::UVAxes, 3u> CapUV {{ { OrigU, OrigV }, { OrigU + 0.2f * SpanU, OrigV }, { OrigU, OrigV + 0.2f * SpanV } }};
                (void)TargetNetwork.AppendFace(EndLoop, CapUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            }
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    LINEAR EXTRUSION
//------------------------------------------------------------------------------------------------------------------------

void AppendLinearExtrusionPrimitive(
    PolygonNetwork&                      TargetNetwork,
    Mathematics::Axis3D                  SectionOrigin,
    Mathematics::Axis3D                  SectionRightAxis,
    Mathematics::Axis3D                  SectionUpAxis,
    std::span<const Mathematics::Axis2D> ClosedContourCCW,
    Mathematics::Axis3D                  ExtrusionOffset,
    uint32_t                             AxialDivisions,
    float                                TotalTwistRadians,
    float                                EndTaperScale,
    UVPatchBounds                        UVRegion,
    const FaceShadingAssignment&         Shading,
    uint16_t                             PriorityLayerOrdinal,
    uint16_t                             PartGroupOrdinal)
{
    if (ClosedContourCCW.size() < 3u)
    {
        return;
    }

    const Mathematics::Axis3D RightAxis = Mathematics::NormalizeAxis(SectionRightAxis, { 1.0f, 0.0f, 0.0f });
    const Mathematics::Axis3D UpAxis    = Mathematics::NormalizeAxis(SectionUpAxis, { 0.0f, 0.0f, 1.0f });
    Mathematics::Axis3D PlaneNormal     = Mathematics::EvaluateCross(RightAxis, UpAxis);
    const bool FlipContour              = (Mathematics::EvaluateDot(PlaneNormal, ExtrusionOffset) < 0.0f);

    std::vector<Mathematics::Axis2D> OrderedContour(ClosedContourCCW.begin(), ClosedContourCCW.end());
    if (EvaluateSignedPolygonArea2D(OrderedContour) < 0.0f)
    {
        std::reverse(OrderedContour.begin(), OrderedContour.end());
    }
    if (FlipContour)
    {
        std::reverse(OrderedContour.begin(), OrderedContour.end());
    }

    const uint32_t Divisions   = std::max(1u, AxialDivisions);
    const float    ClampedTaper = std::max(0.0f, EndTaperScale);
    const std::size_t LoopSize = OrderedContour.size();

    std::vector<float> PerimeterFrac(LoopSize + 1u, 0.0f);
    const float PerimeterTotal = EvaluateContourPerimeter2D(OrderedContour, true);
    for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
    {
        const auto Delta = OrderedContour[(Idx + 1u) % LoopSize] - OrderedContour[Idx];
        PerimeterFrac[Idx + 1u] = PerimeterFrac[Idx] + std::sqrt(Mathematics::EvaluateDot(Delta, Delta)) / PerimeterTotal;
    }

    const float OrigU = UVRegion.Minimum.U + (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.03f;
    const float OrigV = UVRegion.Minimum.V + (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.03f;
    const float SpanU = (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.94f;
    const float SpanV = (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.94f;

    Mathematics::Axis2D ContourMin = OrderedContour.front();
    Mathematics::Axis2D ContourMax = OrderedContour.front();
    for (const auto& Pt : OrderedContour)
    {
        ContourMin.X = std::min(ContourMin.X, Pt.X);
        ContourMin.Y = std::min(ContourMin.Y, Pt.Y);
        ContourMax.X = std::max(ContourMax.X, Pt.X);
        ContourMax.Y = std::max(ContourMax.Y, Pt.Y);
    }
    const float ExtentX = std::max(1.0e-5f, ContourMax.X - ContourMin.X);
    const float ExtentY = std::max(1.0e-5f, ContourMax.Y - ContourMin.Y);

    const uint32_t FirstRingStart = static_cast<uint32_t>(TargetNetwork.Vertices.size());
    for (uint32_t Slice = 0u; Slice <= Divisions; ++Slice)
    {
        const float Alpha = static_cast<float>(Slice) / static_cast<float>(Divisions);
        const float Scale = 1.0f + (ClampedTaper - 1.0f) * Alpha;
        const float Angle = TotalTwistRadians * Alpha;
        const float CosA  = std::cos(Angle);
        const float SinA  = std::sin(Angle);
        const auto  Center = SectionOrigin + ExtrusionOffset * Alpha;

        for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
        {
            const float LocalX = OrderedContour[Idx].X * Scale;
            const float LocalY = OrderedContour[Idx].Y * Scale;
            const float RotX   = LocalX * CosA - LocalY * SinA;
            const float RotY   = LocalX * SinA + LocalY * CosA;
            (void)TargetNetwork.AppendVertex(Center + RightAxis * RotX + UpAxis * RotY);
        }
    }

    // 📝 Side wall quads along extrusion direction
    for (uint32_t Slice = 0u; Slice < Divisions; ++Slice)
    {
        const uint32_t RowLower = FirstRingStart + Slice * static_cast<uint32_t>(LoopSize);
        const uint32_t RowUpper = FirstRingStart + (Slice + 1u) * static_cast<uint32_t>(LoopSize);
        const float    V0       = OrigV + SpanV * (0.62f * static_cast<float>(Slice) / static_cast<float>(Divisions));
        const float    V1       = OrigV + SpanV * (0.62f * static_cast<float>(Slice + 1u) / static_cast<float>(Divisions));

        for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
        {
            const std::size_t NextIdx = (Idx + 1u) % LoopSize;
            const float U0 = OrigU + SpanU * PerimeterFrac[Idx];
            const float U1 = OrigU + SpanU * PerimeterFrac[Idx + 1u];

            const std::array<uint32_t, 4u> Quad {{
                RowLower + static_cast<uint32_t>(Idx),
                RowLower + static_cast<uint32_t>(NextIdx),
                RowUpper + static_cast<uint32_t>(NextIdx),
                RowUpper + static_cast<uint32_t>(Idx),
            }};
            const std::array<Mathematics::UVAxes, 4u> UV {{ { U0, V0 }, { U1, V0 }, { U1, V1 }, { U0, V1 } }};
            (void)TargetNetwork.AppendFace(Quad, UV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
        }
    }

    // 📝 Planar ear-clipped start & end caps
    std::vector<Mathematics::Axis2D> CanonicalContour = OrderedContour;
    if (FlipContour)
    {
        // OrderedContour was reversed relative to (RightAxis x UpAxis); TriangulatePlanarContourCCW handles orientation
    }
    const auto CapTris = TriangulatePlanarContourCCW(OrderedContour);
    const bool ContourIsCCW = (EvaluateSignedPolygonArea2D(OrderedContour) >= 0.0f);
    const uint32_t TopRowStart = FirstRingStart + Divisions * static_cast<uint32_t>(LoopSize);

    for (const auto& Tri : CapTris)
    {
        // Bottom cap outward normal opposes ExtrusionOffset; Top cap outward normal aligns with ExtrusionOffset
        const std::array<uint32_t, 3u> BotLoop = ContourIsCCW
            ? std::array<uint32_t, 3u>{{ FirstRingStart + Tri[0u], FirstRingStart + Tri[2u], FirstRingStart + Tri[1u] }}
            : std::array<uint32_t, 3u>{{ FirstRingStart + Tri[0u], FirstRingStart + Tri[1u], FirstRingStart + Tri[2u] }};

        const std::array<Mathematics::UVAxes, 3u> BotUV {{
            { OrigU + SpanU * 0.45f * ((OrderedContour[BotLoop[0u] - FirstRingStart].X - ContourMin.X) / ExtentX),
              OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[BotLoop[0u] - FirstRingStart].Y - ContourMin.Y) / ExtentY)) },
            { OrigU + SpanU * 0.45f * ((OrderedContour[BotLoop[1u] - FirstRingStart].X - ContourMin.X) / ExtentX),
              OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[BotLoop[1u] - FirstRingStart].Y - ContourMin.Y) / ExtentY)) },
            { OrigU + SpanU * 0.45f * ((OrderedContour[BotLoop[2u] - FirstRingStart].X - ContourMin.X) / ExtentX),
              OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[BotLoop[2u] - FirstRingStart].Y - ContourMin.Y) / ExtentY)) },
        }};
        (void)TargetNetwork.AppendFace(BotLoop, BotUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);

        if (ClampedTaper > 1.0e-5f)
        {
            const std::array<uint32_t, 3u> TopLoop = ContourIsCCW
                ? std::array<uint32_t, 3u>{{ TopRowStart + Tri[0u], TopRowStart + Tri[1u], TopRowStart + Tri[2u] }}
                : std::array<uint32_t, 3u>{{ TopRowStart + Tri[0u], TopRowStart + Tri[2u], TopRowStart + Tri[1u] }};

            const std::array<Mathematics::UVAxes, 3u> TopUV {{
                { OrigU + SpanU * (0.52f + 0.45f * ((OrderedContour[TopLoop[0u] - TopRowStart].X - ContourMin.X) / ExtentX)),
                  OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[TopLoop[0u] - TopRowStart].Y - ContourMin.Y) / ExtentY)) },
                { OrigU + SpanU * (0.52f + 0.45f * ((OrderedContour[TopLoop[1u] - TopRowStart].X - ContourMin.X) / ExtentX)),
                  OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[TopLoop[1u] - TopRowStart].Y - ContourMin.Y) / ExtentY)) },
                { OrigU + SpanU * (0.52f + 0.45f * ((OrderedContour[TopLoop[2u] - TopRowStart].X - ContourMin.X) / ExtentX)),
                  OrigV + SpanV * (0.66f + 0.32f * ((OrderedContour[TopLoop[2u] - TopRowStart].Y - ContourMin.Y) / ExtentY)) },
            }};
            (void)TargetNetwork.AppendFace(TopLoop, TopUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
        }
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                  TWIST-MINIMIZED BISHOP PARALLEL-TRANSPORT PROFILE SWEEP
//------------------------------------------------------------------------------------------------------------------------

void AppendProfileSweepPrimitive(
    PolygonNetwork&                      TargetNetwork,
    std::span<const Mathematics::Axis2D> ClosedCrossSectionCCW,
    std::span<const SweepSpineWaypoint>  SpineWaypoints,
    bool                                 ClosedSpineLoop,
    bool                                 CapOpenEnds,
    UVPatchBounds                        UVRegion,
    const FaceShadingAssignment&         Shading,
    uint16_t                             PriorityLayerOrdinal,
    uint16_t                             PartGroupOrdinal)
{
    if (ClosedCrossSectionCCW.size() < 3u || SpineWaypoints.size() < (ClosedSpineLoop ? 3u : 2u))
    {
        return;
    }

    std::vector<Mathematics::Axis2D> Section(ClosedCrossSectionCCW.begin(), ClosedCrossSectionCCW.end());
    if (EvaluateSignedPolygonArea2D(Section) < 0.0f)
    {
        std::reverse(Section.begin(), Section.end());
    }

    const std::size_t StationCount = SpineWaypoints.size();
    const std::size_t LoopSize     = Section.size();

    // 📝 Compute unit tangents along the 3D spine
    std::vector<Mathematics::Axis3D> SpineTangents(StationCount);
    std::vector<float>               SpineArc(StationCount + 1u, 0.0f);

    for (std::size_t Idx = 0u; Idx < StationCount; ++Idx)
    {
        Mathematics::Axis3D ForwardDelta {};
        if (ClosedSpineLoop)
        {
            const auto& Prev = SpineWaypoints[(Idx + StationCount - 1u) % StationCount].Coordinates;
            const auto& Next = SpineWaypoints[(Idx + 1u) % StationCount].Coordinates;
            ForwardDelta     = Next - Prev;
        }
        else if (Idx == 0u)
        {
            ForwardDelta = SpineWaypoints[1u].Coordinates - SpineWaypoints[0u].Coordinates;
        }
        else if (Idx + 1u == StationCount)
        {
            ForwardDelta = SpineWaypoints[Idx].Coordinates - SpineWaypoints[Idx - 1u].Coordinates;
        }
        else
        {
            ForwardDelta = SpineWaypoints[Idx + 1u].Coordinates - SpineWaypoints[Idx - 1u].Coordinates;
        }
        SpineTangents[Idx] = Mathematics::NormalizeAxis(ForwardDelta, { 0.0f, 1.0f, 0.0f });

        if (Idx + 1u < StationCount)
        {
            SpineArc[Idx + 1u] = SpineArc[Idx] +
                Mathematics::EvaluateDistance(SpineWaypoints[Idx + 1u].Coordinates, SpineWaypoints[Idx].Coordinates);
        }
    }
    if (ClosedSpineLoop)
    {
        SpineArc[StationCount] = SpineArc[StationCount - 1u] +
            Mathematics::EvaluateDistance(SpineWaypoints.front().Coordinates, SpineWaypoints.back().Coordinates);
    }
    const float TotalSpineLength = std::max(1.0e-5f, ClosedSpineLoop ? SpineArc[StationCount] : SpineArc[StationCount - 1u]);

    // 📝 Double-reflection rotation-minimizing Bishop parallel transport (Wang et al. 2008)
    std::vector<Mathematics::Axis3D> StationRight(StationCount);
    std::vector<Mathematics::Axis3D> StationUp(StationCount);
    ConstructOrthonormalBasisAroundAxis(SpineTangents[0u], StationRight[0u], StationUp[0u]);

    for (std::size_t Idx = 0u; Idx + 1u < StationCount; ++Idx)
    {
        const Mathematics::Axis3D DeltaPos = SpineWaypoints[Idx + 1u].Coordinates - SpineWaypoints[Idx].Coordinates;
        const float               NormSq1  = Mathematics::EvaluateLengthSquared(DeltaPos);
        if (NormSq1 <= 1.0e-12f)
        {
            StationRight[Idx + 1u] = StationRight[Idx];
            StationUp[Idx + 1u]    = StationUp[Idx];
            continue;
        }

        const Mathematics::Axis3D ReflRight1 =
            StationRight[Idx] - DeltaPos * ((2.0f / NormSq1) * Mathematics::EvaluateDot(DeltaPos, StationRight[Idx]));
        const Mathematics::Axis3D ReflTan1 =
            SpineTangents[Idx] - DeltaPos * ((2.0f / NormSq1) * Mathematics::EvaluateDot(DeltaPos, SpineTangents[Idx]));

        const Mathematics::Axis3D Vec2    = SpineTangents[Idx + 1u] - ReflTan1;
        const float               NormSq2 = Mathematics::EvaluateLengthSquared(Vec2);
        Mathematics::Axis3D       NextRight = (NormSq2 > 1.0e-12f)
            ? (ReflRight1 - Vec2 * ((2.0f / NormSq2) * Mathematics::EvaluateDot(Vec2, ReflRight1)))
            : ReflRight1;

        NextRight = NextRight - SpineTangents[Idx + 1u] * Mathematics::EvaluateDot(NextRight, SpineTangents[Idx + 1u]);
        StationRight[Idx + 1u] = Mathematics::NormalizeAxis(NextRight, StationRight[Idx]);
        StationUp[Idx + 1u]    = Mathematics::NormalizeAxis(
            Mathematics::EvaluateCross(SpineTangents[Idx + 1u], StationRight[Idx + 1u]),
            StationUp[Idx]);
    }

    // 📝 Eliminate residual holonomy twist on closed spine loops by distributing closure angle linearly
    if (ClosedSpineLoop)
    {
        const Mathematics::Axis3D ClosingDelta = SpineWaypoints.front().Coordinates - SpineWaypoints.back().Coordinates;
        const float               CloseNormSq  = Mathematics::EvaluateLengthSquared(ClosingDelta);
        Mathematics::Axis3D       TransportedRight = StationRight.back();
        if (CloseNormSq > 1.0e-12f)
        {
            const auto ReflR = TransportedRight - ClosingDelta * ((2.0f / CloseNormSq) * Mathematics::EvaluateDot(ClosingDelta, TransportedRight));
            const auto ReflT = SpineTangents.back() - ClosingDelta * ((2.0f / CloseNormSq) * Mathematics::EvaluateDot(ClosingDelta, SpineTangents.back()));
            const auto Vec2  = SpineTangents.front() - ReflT;
            const float N2   = Mathematics::EvaluateLengthSquared(Vec2);
            TransportedRight = (N2 > 1.0e-12f) ? (ReflR - Vec2 * ((2.0f / N2) * Mathematics::EvaluateDot(Vec2, ReflR))) : ReflR;
        }
        const float HolonomyCos   = std::clamp(Mathematics::EvaluateDot(TransportedRight, StationRight.front()), -1.0f, 1.0f);
        const float HolonomySin   = std::clamp(Mathematics::EvaluateDot(TransportedRight, StationUp.front()), -1.0f, 1.0f);
        const float HolonomyAngle = std::atan2(HolonomySin, HolonomyCos);

        for (std::size_t Idx = 0u; Idx < StationCount; ++Idx)
        {
            const float Correction = -HolonomyAngle * (SpineArc[Idx] / TotalSpineLength);
            const float CosC       = std::cos(Correction);
            const float SinC       = std::sin(Correction);
            const auto  OrigR      = StationRight[Idx];
            const auto  OrigU      = StationUp[Idx];
            StationRight[Idx]      = OrigR * CosC + OrigU * SinC;
            StationUp[Idx]         = OrigU * CosC - OrigR * SinC;
        }
    }

    std::vector<float> PerimeterFrac(LoopSize + 1u, 0.0f);
    const float PerimeterTotal = EvaluateContourPerimeter2D(Section, true);
    for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
    {
        const auto Delta = Section[(Idx + 1u) % LoopSize] - Section[Idx];
        PerimeterFrac[Idx + 1u] = PerimeterFrac[Idx] + std::sqrt(Mathematics::EvaluateDot(Delta, Delta)) / PerimeterTotal;
    }

    const float OrigU = UVRegion.Minimum.U + (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.03f;
    const float OrigV = UVRegion.Minimum.V + (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.03f;
    const float SpanU = (UVRegion.Maximum.U - UVRegion.Minimum.U) * 0.94f;
    const float SpanV = (UVRegion.Maximum.V - UVRegion.Minimum.V) * 0.94f;

    const uint32_t FirstSweepVertex = static_cast<uint32_t>(TargetNetwork.Vertices.size());
    for (std::size_t Station = 0u; Station < StationCount; ++Station)
    {
        const float Scale = std::max(1.0e-5f, SpineWaypoints[Station].CrossSectionScale);
        const float Roll  = SpineWaypoints[Station].RollAngleRadians;
        const float CosR  = std::cos(Roll);
        const float SinR  = std::sin(Roll);
        const auto  Right = StationRight[Station] * CosR + StationUp[Station] * SinR;
        const auto  Up    = StationUp[Station] * CosR - StationRight[Station] * SinR;

        for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
        {
            const auto Offset = Right * (Section[Idx].X * Scale) + Up * (Section[Idx].Y * Scale);
            (void)TargetNetwork.AppendVertex(SpineWaypoints[Station].Coordinates + Offset);
        }
    }

    const std::size_t SegmentCount = ClosedSpineLoop ? StationCount : (StationCount - 1u);
    for (std::size_t Seg = 0u; Seg < SegmentCount; ++Seg)
    {
        const std::size_t NextStation = (Seg + 1u) % StationCount;
        const uint32_t RowA = FirstSweepVertex + static_cast<uint32_t>(Seg * LoopSize);
        const uint32_t RowB = FirstSweepVertex + static_cast<uint32_t>(NextStation * LoopSize);
        const float    V0   = OrigV + SpanV * (SpineArc[Seg] / TotalSpineLength);
        const float    V1   = OrigV + SpanV * (SpineArc[Seg + 1u] / TotalSpineLength);

        for (std::size_t Idx = 0u; Idx < LoopSize; ++Idx)
        {
            const std::size_t NextIdx = (Idx + 1u) % LoopSize;
            const float U0 = OrigU + SpanU * PerimeterFrac[Idx];
            const float U1 = OrigU + SpanU * PerimeterFrac[Idx + 1u];

            const std::array<uint32_t, 4u> Quad {{
                RowA + static_cast<uint32_t>(Idx),
                RowA + static_cast<uint32_t>(NextIdx),
                RowB + static_cast<uint32_t>(NextIdx),
                RowB + static_cast<uint32_t>(Idx),
            }};
            const std::array<Mathematics::UVAxes, 4u> UV {{ { U0, V0 }, { U1, V0 }, { U1, V1 }, { U0, V1 } }};
            (void)TargetNetwork.AppendFace(Quad, UV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
        }
    }

    if (!ClosedSpineLoop && CapOpenEnds)
    {
        const auto CapTris          = TriangulatePlanarContourCCW(Section);
        const uint32_t LastRowStart = FirstSweepVertex + static_cast<uint32_t>((StationCount - 1u) * LoopSize);
        for (const auto& Tri : CapTris)
        {
            const std::array<uint32_t, 3u> StartCap {{ FirstSweepVertex + Tri[0u], FirstSweepVertex + Tri[2u], FirstSweepVertex + Tri[1u] }};
            const std::array<uint32_t, 3u> EndCap   {{ LastRowStart + Tri[0u], LastRowStart + Tri[1u], LastRowStart + Tri[2u] }};
            const std::array<Mathematics::UVAxes, 3u> CapUV {{ { OrigU, OrigV }, { OrigU + 0.15f * SpanU, OrigV }, { OrigU, OrigV + 0.15f * SpanV } }};
            (void)TargetNetwork.AppendFace(StartCap, CapUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
            (void)TargetNetwork.AppendFace(EndCap,   CapUV, Shading, PriorityLayerOrdinal, PartGroupOrdinal);
        }
    }
}

} // namespace Frontier::GeometrySpace
