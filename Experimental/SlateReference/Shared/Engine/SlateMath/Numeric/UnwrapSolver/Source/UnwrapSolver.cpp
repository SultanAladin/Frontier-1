//============================================================================================================================================
//                                                            UNWRAPSOLVER.CPP
//============================================================================================================================================
// 🧩 Chord-length boundary mapping, mean-value interior weights, and relaxation against a declared criterion.

#include "SlateMath/Numeric/UnwrapSolver/Api/UnwrapSolver.h"

#include "Foundation/NumericTolerance.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPATIAL HELPERS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

double SpatialDistance(DocumentPosition Earlier, DocumentPosition Later)
{
    const double SpanX = Later.PositionX - Earlier.PositionX;
    const double SpanY = Later.PositionY - Earlier.PositionY;
    const double SpanZ = Later.PositionZ - Earlier.PositionZ;

    return std::sqrt(SpanX * SpanX + SpanY * SpanY + SpanZ * SpanZ);
}

// 📐 The angle at Corner, between the two edges leaving it. Taken from the cosine of the normalised edge
//    directions and bounded into the representable domain, because a triangle with two coincident corners
//    produces a cosine a hair outside [-1,1] and the inverse then reports a value that is not a number.
double CornerAngle(DocumentPosition Corner, DocumentPosition Earlier, DocumentPosition Later)
{
    const double EarlierLength = SpatialDistance(Corner, Earlier);
    const double LaterLength   = SpatialDistance(Corner, Later);

    if (EarlierLength <= 0.0 || LaterLength <= 0.0)
        return 0.0;

    const double EarlierX = (Earlier.PositionX - Corner.PositionX) / EarlierLength;
    const double EarlierY = (Earlier.PositionY - Corner.PositionY) / EarlierLength;
    const double EarlierZ = (Earlier.PositionZ - Corner.PositionZ) / EarlierLength;

    const double LaterX = (Later.PositionX - Corner.PositionX) / LaterLength;
    const double LaterY = (Later.PositionY - Corner.PositionY) / LaterLength;
    const double LaterZ = (Later.PositionZ - Corner.PositionZ) / LaterLength;

    double Alignment = EarlierX * LaterX + EarlierY * LaterY + EarlierZ * LaterZ;

    Alignment = Alignment < -1.0 ? -1.0 : (Alignment > 1.0 ? 1.0 : Alignment);

    return std::acos(Alignment);
}

double PlanarAngle(PlanarPosition Corner, PlanarPosition Earlier, PlanarPosition Later)
{
    const double EarlierX = Earlier.PositionX - Corner.PositionX;
    const double EarlierY = Earlier.PositionY - Corner.PositionY;
    const double LaterX   = Later.PositionX   - Corner.PositionX;
    const double LaterY   = Later.PositionY   - Corner.PositionY;

    const double EarlierLength = std::sqrt(EarlierX * EarlierX + EarlierY * EarlierY);
    const double LaterLength   = std::sqrt(LaterX   * LaterX   + LaterY   * LaterY);

    if (EarlierLength <= 0.0 || LaterLength <= 0.0)
        return 0.0;

    double Alignment = (EarlierX * LaterX + EarlierY * LaterY) / (EarlierLength * LaterLength);

    Alignment = Alignment < -1.0 ? -1.0 : (Alignment > 1.0 ? 1.0 : Alignment);

    return std::acos(Alignment);
}

double SpatialArea(DocumentPosition Alpha, DocumentPosition Beta, DocumentPosition Gamma)
{
    const double FirstX = Beta.PositionX - Alpha.PositionX;
    const double FirstY = Beta.PositionY - Alpha.PositionY;
    const double FirstZ = Beta.PositionZ - Alpha.PositionZ;

    const double SecondX = Gamma.PositionX - Alpha.PositionX;
    const double SecondY = Gamma.PositionY - Alpha.PositionY;
    const double SecondZ = Gamma.PositionZ - Alpha.PositionZ;

    const double CrossX = FirstY * SecondZ - FirstZ * SecondY;
    const double CrossY = FirstZ * SecondX - FirstX * SecondZ;
    const double CrossZ = FirstX * SecondY - FirstY * SecondX;

    return 0.5 * std::sqrt(CrossX * CrossX + CrossY * CrossY + CrossZ * CrossZ);
}

double PlanarArea(PlanarPosition Alpha, PlanarPosition Beta, PlanarPosition Gamma)
{
    const double Doubled = (Beta.PositionX  - Alpha.PositionX) * (Gamma.PositionY - Alpha.PositionY)
                         - (Gamma.PositionX - Alpha.PositionX) * (Beta.PositionY  - Alpha.PositionY);

    return 0.5 * std::fabs(Doubled);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                      THE SOLVE
//------------------------------------------------------------------------------------------------------------------------

Deliver<ConvergentResult<std::vector<PlanarPosition>>> Solve(const UnwrapSpecification& Declaring)
{
    using Result = ConvergentResult<std::vector<PlanarPosition>>;

    const std::size_t VertexSpan = Declaring.Positions.size();

    if (VertexSpan < 3u)
        return Deliver<Result>::Refuse({ RefusalReason::ExtentExhausted, "a chart of fewer than three positions" });

    if (Declaring.TriangleCorners.size() % 3u != 0u || Declaring.TriangleCorners.empty())
        return Deliver<Result>::Refuse({ RefusalReason::ContentUnsupported, "the triangulation is not whole" });

    for (const std::uint32_t Corner : Declaring.TriangleCorners)
    {
        if (Corner >= VertexSpan)
            return Deliver<Result>::Refuse({ RefusalReason::ContentUnsupported, "a corner addresses no position" });
    }

    if (Declaring.ContourLoop.size() < 3u)
        return Deliver<Result>::Refuse({ RefusalReason::ContentUnsupported, "the boundary loop is not a loop" });

    std::vector<bool> DividerHeld(VertexSpan, false);

    for (const std::uint32_t Index : Declaring.ContourLoop)
    {
        if (Index >= VertexSpan)
            return Deliver<Result>::Refuse({ RefusalReason::ContentUnsupported, "the boundary addresses no position" });

        DividerHeld[Index] = true;
    }

    // 📐 The boundary is mapped by chord length rather than by equal steps, so a long edge occupies a
    //    proportionate arc. Equal steps compress every long edge into the same arc a short one gets, and the
    //    compression is visible as the artist's texel density changing along a single straight boundary.
    const std::size_t LoopSpan = Declaring.ContourLoop.size();

    std::vector<double> Accumulated(LoopSpan + 1u, 0.0);

    for (std::size_t Index = 0u; Index < LoopSpan; ++Index)
    {
        const DocumentPosition& Earlier = Declaring.Positions[Declaring.ContourLoop[Index]];
        const DocumentPosition& Later   = Declaring.Positions[Declaring.ContourLoop[(Index + 1u) % LoopSpan]];

        Accumulated[Index + 1u] = Accumulated[Index] + SpatialDistance(Earlier, Later);
    }

    const double Perimeter = Accumulated[LoopSpan];

    if (Perimeter <= 0.0)
        return Deliver<Result>::Refuse({ RefusalReason::ContentUnsupported, "the boundary encloses no extent" });

    std::vector<PlanarPosition> Flattened(VertexSpan);

    const double Radius = 0.5;

    for (std::size_t Index = 0u; Index < LoopSpan; ++Index)
    {
        const double Angle = 2.0 * Pi * Accumulated[Index] / Perimeter;

        Flattened[Declaring.ContourLoop[Index]].PositionX = 0.5 + Radius * std::cos(Angle);
        Flattened[Declaring.ContourLoop[Index]].PositionY = 0.5 + Radius * std::sin(Angle);
    }

    // 📐 Mean-value weights: w(i,j) accumulates (tan(θ/2) / ‖e(i,j)‖) for each triangle corner θ at i whose
    //    edge is (i,j). Every weight is strictly positive, which is what makes the relaxation converge to an
    //    embedding rather than merely to a fixed point.
    std::vector<std::vector<std::uint32_t>>  Neighbours(VertexSpan);
    std::vector<std::vector<double>>         Weights(VertexSpan);

    const std::size_t TriangleSpan = Declaring.TriangleCorners.size() / 3u;

    for (std::size_t TriangleIndex = 0u; TriangleIndex < TriangleSpan; ++TriangleIndex)
    {
        const std::uint32_t Corners[3] =
        {
            Declaring.TriangleCorners[TriangleIndex * 3u],
            Declaring.TriangleCorners[TriangleIndex * 3u + 1u],
            Declaring.TriangleCorners[TriangleIndex * 3u + 2u]
        };

        for (std::uint32_t Passed = 0u; Passed < 3u; ++Passed)
        {
            const std::uint32_t Subject = Corners[Passed];

            if (DividerHeld[Subject])
                continue;

            const std::uint32_t Earlier = Corners[(Passed + 2u) % 3u];
            const std::uint32_t Later   = Corners[(Passed + 1u) % 3u];

            const double Angle = CornerAngle(Declaring.Positions[Subject],
                                             Declaring.Positions[Earlier],
                                             Declaring.Positions[Later]);

            const double HalfTangent = std::tan(Angle * 0.5);

            const std::uint32_t Reached[2] = { Earlier, Later };

            for (std::uint32_t Side = 0u; Side < 2u; ++Side)
            {
                const double Length = SpatialDistance(Declaring.Positions[Subject],
                                                      Declaring.Positions[Reached[Side]]);

                if (Length <= 0.0)
                    continue;

                const double Contribution = HalfTangent / Length;

                std::size_t Located = Neighbours[Subject].size();

                for (std::size_t Index = 0u; Index < Neighbours[Subject].size(); ++Index)
                {
                    if (Neighbours[Subject][Index] == Reached[Side])
                    {
                        Located = Index;
                        break;
                    }
                }

                if (Located == Neighbours[Subject].size())
                {
                    Neighbours[Subject].push_back(Reached[Side]);
                    Weights[Subject].push_back(Contribution);
                }
                else
                {
                    Weights[Subject][Located] += Contribution;
                }
            }
        }
    }

    // 📝 Interior positions begin at the boundary's centre rather than at the origin, so the first relaxation
    //    step is already inside the disc and the residual falls monotonically from the outset.
    for (std::size_t Index = 0u; Index < VertexSpan; ++Index)
    {
        if (DividerHeld[Index])
            continue;

        Flattened[Index].PositionX = 0.5;
        Flattened[Index].PositionY = 0.5;
    }

    Result Produced;
    Produced.Cause = TerminationCause::CriterionSatisfied;

    const double Threshold = Declaring.ConvergenceCriterion * Radius;

    std::uint32_t IterationIndex = 0u;
    double        Residual         = 0.0;

    for (; IterationIndex < Declaring.IterationLimit; ++IterationIndex)
    {
        Residual = 0.0;

        for (std::size_t Index = 0u; Index < VertexSpan; ++Index)
        {
            if (DividerHeld[Index] || Neighbours[Index].empty())
                continue;

            double AccumulatedX = 0.0;
            double AccumulatedY = 0.0;
            double AccumulatedW = 0.0;

            for (std::size_t Reached = 0u; Reached < Neighbours[Index].size(); ++Reached)
            {
                const double Weight = Weights[Index][Reached];

                AccumulatedX += Weight * Flattened[Neighbours[Index][Reached]].PositionX;
                AccumulatedY += Weight * Flattened[Neighbours[Index][Reached]].PositionY;
                AccumulatedW += Weight;
            }

            if (AccumulatedW <= 0.0)
                continue;

            const double SolvedX     = AccumulatedX / AccumulatedW;
            const double SolvedY     = AccumulatedY / AccumulatedW;
            const double Displaced   = std::fabs(SolvedX - Flattened[Index].PositionX)
                                     + std::fabs(SolvedY - Flattened[Index].PositionY);

            Flattened[Index].PositionX = SolvedX;
            Flattened[Index].PositionY = SolvedY;

            if (Displaced > Residual)
                Residual = Displaced;
        }

        if (Residual <= Threshold)
            break;
    }

    // 🔴 The ceiling is reported and never silently accepted. `02` §5 states the general reason and `68` §4 the
    //    specific one: the last iterate is the best available result, not a converged one, and the difference
    //    is the difference between a measured distortion and an unmeasured one.
    if (IterationIndex >= Declaring.IterationLimit && Residual > Threshold)
        Produced.Cause = TerminationCause::LimitReached;

    Produced.Approximation  = Flattened;
    Produced.ResidualNorm   = Residual;
    Produced.IterationCount = IterationIndex;

    return Deliver<Result>::Result(Produced);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE DISTORTION
//------------------------------------------------------------------------------------------------------------------------

DistortionMeasure Measure(const std::vector<DocumentPosition>&  Positions,
                          const std::vector<std::uint32_t>&     TriangleCorners,
                          const std::vector<PlanarPosition>&    Flattened)
{
    DistortionMeasure Measured;

    if (TriangleCorners.size() % 3u != 0u || Positions.size() != Flattened.size())
        return Measured;

    const std::size_t TriangleSpan = TriangleCorners.size() / 3u;

    double AccumulatedSpatial = 0.0;
    double AccumulatedPlanar  = 0.0;

    for (std::size_t TriangleIndex = 0u; TriangleIndex < TriangleSpan; ++TriangleIndex)
    {
        const std::uint32_t Alpha = TriangleCorners[TriangleIndex * 3u];
        const std::uint32_t Beta  = TriangleCorners[TriangleIndex * 3u + 1u];
        const std::uint32_t Gamma = TriangleCorners[TriangleIndex * 3u + 2u];

        AccumulatedSpatial += SpatialArea(Positions[Alpha], Positions[Beta], Positions[Gamma]);
        AccumulatedPlanar  += PlanarArea(Flattened[Alpha], Flattened[Beta], Flattened[Gamma]);
    }

    if (AccumulatedSpatial <= 0.0 || AccumulatedPlanar <= 0.0)
        return Measured;

    // 📐 Normalised by the chart's own mean ratio, so the measure reports the flattening's uniformity and not
    //    the scale the packing later applies. An unnormalised ratio would call a correctly flattened chart
    //    distorted purely because it was packed small.
    const double MeanRatio = AccumulatedPlanar / AccumulatedSpatial;

    for (std::size_t TriangleIndex = 0u; TriangleIndex < TriangleSpan; ++TriangleIndex)
    {
        const std::uint32_t Corners[3] =
        {
            TriangleCorners[TriangleIndex * 3u],
            TriangleCorners[TriangleIndex * 3u + 1u],
            TriangleCorners[TriangleIndex * 3u + 2u]
        };

        const double Spatial = SpatialArea(Positions[Corners[0]], Positions[Corners[1]], Positions[Corners[2]]);
        const double Planar  = PlanarArea(Flattened[Corners[0]], Flattened[Corners[1]], Flattened[Corners[2]]);

        if (Spatial <= 0.0 || Planar <= 0.0)
            continue;

        const double Ratio    = (Planar / Spatial) / MeanRatio;
        const double Departure = Ratio >= 1.0 ? Ratio : 1.0 / Ratio;

        if (!Measured.MeasureDeclared || Departure > Measured.MaximumAreaRatio)
        {
            Measured.MaximumAreaRatio = Departure;
            Measured.WorstAreaTriangle = static_cast<std::uint32_t>(TriangleIndex);
        }

        for (std::uint32_t Passed = 0u; Passed < 3u; ++Passed)
        {
            const std::uint32_t Subject = Corners[Passed];
            const std::uint32_t Earlier = Corners[(Passed + 2u) % 3u];
            const std::uint32_t Later   = Corners[(Passed + 1u) % 3u];

            const double SpatialRadians = CornerAngle(Positions[Subject], Positions[Earlier], Positions[Later]);
            const double PlanarRadians  = PlanarAngle(Flattened[Subject], Flattened[Earlier], Flattened[Later]);

            const double Deviation = std::fabs(PlanarRadians - SpatialRadians) * 180.0 / Pi;

            if (!Measured.MeasureDeclared || Deviation > Measured.MaximumAngleDeviation)
            {
                Measured.MaximumAngleDeviation = Deviation;
                Measured.WorstAngleTriangle     = static_cast<std::uint32_t>(TriangleIndex);
            }
        }

        Measured.MeasureDeclared = true;
    }

    return Measured;
}

}   // namespace Slate
