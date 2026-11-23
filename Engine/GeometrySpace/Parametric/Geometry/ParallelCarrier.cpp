/*====================================================================================================================================
                                                      PARALLELCARRIER.CPP
====================================================================================================================================*/
// 🧩 Exact offset (parallel surface) of a rational B-spline surface: evaluation, normals, inversion, curvature, fold margin

#include "Engine/GeometrySpace/Parametric/Geometry/ParallelCarrier.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // A point just inside the domain, for the limit at a collapsed pole row (sphere pole, cone apex).
    [[nodiscard]] Vector2 NudgeInside(const NurbsSurfaceDefinition& Definition, Vector2 Parameter) noexcept
    {
        const NurbsDomain U = MeasureNurbsSurfaceDomain(Definition, true), V = MeasureNurbsSurfaceDomain(Definition, false);
        return { Parameter.X + (Parameter.X <= 0.5 * (U.Low + U.High) ? 1.0 : -1.0) * 1.0e-6 * (U.High - U.Low),
                 Parameter.Y + (Parameter.Y <= 0.5 * (V.Low + V.High) ? 1.0 : -1.0) * 1.0e-6 * (V.High - V.Low) };
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

ParallelSurface ConstructParallelSurface(const NurbsSurface& Basis, double Distance)
{
    auto Definition      = std::make_shared<ParallelDefinition>();
    Definition->Basis    = Basis;
    Definition->Distance = Distance;
    return ParallelSurface{ std::move(Definition) };
}

SurfaceSample EvaluateParallelSurface(const ParallelSurface& Surface, Vector2 Parameter) noexcept
{
    const ParallelDefinition&     Definition = *Surface.Definition;
    const NurbsSurfaceDerivatives S          = EvaluateNurbsSurfaceDerivatives(*Definition.Basis.Definition, Parameter);
    const Vector3 n      = Cross(S.U, S.V);                                                  // [len²] - Natural normal of the basis
    const double  Length = std::sqrt(Dot(n, n));                                             // [len²] - |n|
    if (!(Length > 1.0e-14 * (Dot(S.U, S.U) + Dot(S.V, S.V))))
    {
        // Singular basis chart (collapsed row): the offset point uses the closed-form limit normal; ∂ are the basis's.
        return { S.Position + EvaluateNurbsNormal(Definition.Basis, Parameter) * Definition.Distance, S.U, S.V };
    }
    const Vector3 N        = n * (1.0 / Length);                                                    // [-]   - Unit normal
    const Vector3 TurnU    = Cross(S.UU, S.V) + Cross(S.U, S.UV);                            // [len²] - n𝑢
    const Vector3 TurnV    = Cross(S.UV, S.V) + Cross(S.U, S.VV);                            // [len²] - n𝑣
    const Vector3 NormalU  = (TurnU - N * Dot(N, TurnU)) * (1.0 / Length);                          // [-]   - N𝑢
    const Vector3 NormalV  = (TurnV - N * Dot(N, TurnV)) * (1.0 / Length);                          // [-]   - N𝑣
    return { S.Position + N * Definition.Distance, S.U + NormalU * Definition.Distance, S.V + NormalV * Definition.Distance };
}

Vector3 EvaluateParallelNormal(const ParallelSurface& Surface, Vector2 Parameter) noexcept
{
    // 📝 O𝑢 × O𝑣 = (1 − dκ₁)(1 − dκ₂)·n: the basis normal wherever the offset is regular.
    return EvaluateNurbsNormal(Surface.Definition->Basis, Parameter);
}

Vector2 ProjectOntoParallelSurface(const ParallelSurface& Surface, Vector3 Position, Vector2 Reference) noexcept
{
    // 📝 X − O ∥ N ⇔ X − S ∥ N at the same (𝑢, 𝑣): the stationary points coincide, so the basis foot is the offset foot.
    return ProjectOntoNurbsSurface(Surface.Definition->Basis, Position, Reference);
}

bool MeasureNurbsPrincipalCurvatures(const NurbsSurfaceDefinition& Definition, Vector2 Parameter, std::array<double, 2>& Curvatures) noexcept
{
    const auto Measure = [&](Vector2 At)
    {
        const NurbsSurfaceDerivatives S = EvaluateNurbsSurfaceDerivatives(Definition, At);
        const double E = Dot(S.U, S.U), F = Dot(S.U, S.V), G = Dot(S.V, S.V);
        const double Gram = E * G - F * F;
        if (!(Gram > 1.0e-24 * E * G) || !(E > 0.0) || !(G > 0.0)) return false;
        const Vector3 N = Normalize(Cross(S.U, S.V));
        const double L = Dot(S.UU, N), M = Dot(S.UV, N), Q = Dot(S.VV, N);
        // 📝 Eigenvalues of the Weingarten map W = I⁻¹·II = [[a, b], [c, d]] / Gram. ((a − d)/2)² + b·c is formed from
        //    differences, so an umbilic (sphere: a = d, b = c = 0) gives exactly κ₁ = κ₂ — H² − K would cancel to √ε noise.
        const double A = G * L - F * M, B = G * M - F * Q, C = E * M - F * L, D = E * Q - F * M;
        const double Mean   = 0.5 * (A + D) / Gram;                                          // [1/len]  - H
        const double Half   = 0.5 * (A - D);
        const double Spread = std::sqrt(std::max(Half * Half + B * C, 0.0)) / Gram;          // [1/len]  - (κ₁ − κ₂)/2
        Curvatures = { Mean + Spread, Mean - Spread };
        return true;
    };
    return Measure(Parameter) || Measure(NudgeInside(Definition, Parameter));
}

double MeasureParallelCurvature(const ParallelSurface& Surface, Vector2 Parameter) noexcept
{
    // 📝 Along a principal direction O𝑢 = (1 − dκ)·S𝑢 with the same normal, so the offset curvature is κ / (1 − dκ).
    std::array<double, 2> Curvatures{};
    if (!MeasureNurbsPrincipalCurvatures(*Surface.Definition->Basis.Definition, Parameter, Curvatures)) return INFINITY;
    double Peak = 0.0;                                                                       // [1/len] - Largest |κ′|
    for (const double κ : Curvatures)
    {
        const double Stretch = 1.0 - Surface.Definition->Distance * κ;                       // [-] - 1 − dκ
        if (!(Stretch > 0.0)) return INFINITY;
        Peak = std::max(Peak, std::fabs(κ) / Stretch);
    }
    return Peak;
}

ParallelFold MeasureParallelFold(const ParallelSurface& Surface, Vector2 Low, Vector2 High, int Count) noexcept
{
    ParallelFold Fold;
    Fold.Margin    = std::numeric_limits<double>::max();
    Fold.Parameter = (Low + High) * 0.5;
    const NurbsSurfaceDefinition& Definition = *Surface.Definition->Basis.Definition;
    const auto Visit = [&](Vector2 At)
    {
        std::array<double, 2> Curvatures{};
        if (!MeasureNurbsPrincipalCurvatures(Definition, At, Curvatures)) return;
        const double Margin = std::min(1.0 - Surface.Definition->Distance * Curvatures[0], 1.0 - Surface.Definition->Distance * Curvatures[1]);
        if (Margin < Fold.Margin) { Fold.Margin = Margin; Fold.Parameter = At; }
    };
    Count = std::max(Count, 1);
    for (int Row = 0; Row <= Count; ++Row)
        for (int Column = 0; Column <= Count; ++Column)
            Visit({ Low.X + (High.X - Low.X) * Column / Count, Low.Y + (High.Y - Low.Y) * Row / Count });
    if (Fold.Margin == std::numeric_limits<double>::max()) Fold.Margin = 1.0;
    return Fold;
}

NurbsCurve ExtractParallelIsoPolyline(const ParallelSurface& Surface, bool FixedU, double Value, double RunLow, double RunHigh)
{
    // Running knots of the basis inside the run decide the density (16 points per span, ≥ 64 overall).
    std::vector<double> Stops = CollectNurbsSurfaceKnots(Surface.Definition->Basis, !FixedU, std::min(RunLow, RunHigh), std::max(RunLow, RunHigh));
    const int Spans = static_cast<int>(Stops.size()) + 1;
    const int Count = std::clamp(16 * Spans, 64, 4096);
    std::vector<double>  Knots   = { RunLow, RunLow };
    std::vector<Vector3> Poles;
    std::vector<double>  Weights;
    for (int Index = 0; Index <= Count; ++Index)
    {
        const double Run = RunLow + (RunHigh - RunLow) * Index / Count;                      // [-] - Running chart parameter
        Poles.push_back(EvaluateParallelSurface(Surface, FixedU ? Vector2{ Value, Run } : Vector2{ Run, Value }).Position);
        Weights.push_back(1.0);
        if (Index > 0 && Index < Count) Knots.push_back(Run);
    }
    Knots.push_back(RunHigh);
    Knots.push_back(RunHigh);
    const std::optional<NurbsCurve> Curve = ConstructNurbsCurve(1, std::move(Knots), std::move(Poles), std::move(Weights));
    return Curve ? *Curve : NurbsCurve{};
}

} // namespace Frontier::GeometrySpace::Parametric
