/*====================================================================================================================================
                                                        SURFACEALGEBRA.CPP
====================================================================================================================================*/
// 🧩 Degree elevation by Greville collocation, knot unions, pole rows / columns, skinning with end-derivative conditions

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceAlgebra.h"
#include "Engine/GeometrySpace/Parametric/Surfacing/LinearSystem.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // Clamped vector from interior (value, multiplicity) pairs on [0, 1].
    [[nodiscard]] std::vector<double> ClampedFrom(const std::vector<std::pair<double, int>>& Interior, int Degree)
    {
        std::vector<double> Knots(Degree + 1, 0.0);
        for (const auto& [Value, Count] : Interior) Knots.insert(Knots.end(), Count, Value);
        Knots.insert(Knots.end(), Degree + 1, 1.0);
        return Knots;
    }

    // Interior knots by averaging Degree consecutive parameters (P&T 9.8 applied to a list that may repeat its ends).
    [[nodiscard]] std::vector<double> AveragedKnots(const std::vector<double>& Parameters, int Degree)
    {
        const int Count = static_cast<int>(Parameters.size());
        std::vector<double> Knots(Degree + 1, 0.0);
        for (int Index = 1; Index <= Count - Degree - 1; ++Index)
        {
            double Sum = 0.0;
            for (int Offset = Index; Offset < Index + Degree; ++Offset) Sum += Parameters[Offset];
            Knots.push_back(Sum / Degree);
        }
        Knots.insert(Knots.end(), Degree + 1, 1.0);
        return Knots;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

NurbsCurveDefinition MapCurveOntoUnit(const NurbsCurveDefinition& Definition)
{
    NurbsCurveDefinition Result = Definition;
    const NurbsDomain Domain = MeasureNurbsCurveDomain(Definition);
    const double Width = Domain.High - Domain.Low;
    for (double& Knot : Result.Knots) Knot = (Knot - Domain.Low) / Width;
    for (int Index = 0; Index <= Result.Degree; ++Index)
    {
        Result.Knots[Index] = 0.0;
        Result.Knots[Result.Knots.size() - 1 - Index] = 1.0;
    }
    Result.Period = 0.0;
    return Result;
}

std::vector<std::pair<double, int>> CollectInteriorKnots(const std::vector<double>& Knots, int Degree)
{
    std::vector<std::pair<double, int>> Interior;
    for (size_t Index = Degree + 1; Index + Degree + 1 < Knots.size(); ++Index)
    {
        if (!Interior.empty() && Interior.back().first == Knots[Index]) { ++Interior.back().second; continue; }
        Interior.push_back({ Knots[Index], 1 });
    }
    return Interior;
}

std::vector<double> MissingInteriorKnots(const std::vector<double>& Knots, const std::vector<double>& Target, int Degree)
{
    std::map<double, int> Present;
    for (const auto& [Value, Count] : CollectInteriorKnots(Knots, Degree)) Present[Value] = Count;
    std::vector<double> Missing;
    for (const auto& [Value, Count] : CollectInteriorKnots(Target, Degree))
    {
        const auto Found = Present.find(Value);
        for (int Copy = Found == Present.end() ? 0 : Found->second; Copy < Count; ++Copy) Missing.push_back(Value);
    }
    return Missing;
}

std::vector<double> UniteKnotVectors(const std::vector<std::vector<double>>& Vectors, int Degree)
{
    std::map<double, int> Union;
    for (const std::vector<double>& Knots : Vectors)
    {
        for (const auto& [Value, Count] : CollectInteriorKnots(Knots, Degree)) Union[Value] = std::max(Union[Value], Count);
    }
    return ClampedFrom(std::vector<std::pair<double, int>>(Union.begin(), Union.end()), Degree);
}

std::optional<NurbsCurveDefinition> ElevateNurbsCurveDegree(const NurbsCurveDefinition& Definition, int Degree)
{
    if (Degree == Definition.Degree) return Definition;
    if (Degree < Definition.Degree || Degree > NurbsDegreeLimit) return std::nullopt;
    const NurbsCurveDefinition Source = MapCurveOntoUnit(Definition);
    std::vector<std::pair<double, int>> Interior = CollectInteriorKnots(Source.Knots, Source.Degree);
    for (auto& [Value, Count] : Interior) Count += Degree - Source.Degree;
    const std::vector<double> Knots = ClampedFrom(Interior, Degree);
    const int Count = static_cast<int>(Knots.size()) - Degree - 1;
    const int SourceCount = static_cast<int>(Source.Poles.size());

    // ① Greville abscissae ξᵢ = (τᵢ₊₁ + … + τᵢ₊ₚ) / p of the elevated space; ② homogeneous samples of the curve there.
    DenseLinearArray Matrix(Count, Count), RightSides(Count, 4);
    for (int Row = 0; Row < Count; ++Row)
    {
        double Greville = 0.0;
        for (int Offset = 1; Offset <= Degree; ++Offset) Greville += Knots[Row + Offset];
        Greville /= Degree;
        const NurbsBasisSample Target = EvaluateNurbsBasis(Knots, Degree, Count, Greville);
        for (int Column = 0; Column <= Degree; ++Column) Matrix.At(Row, Target.First + Column) = Target.Values[0][Column];
        const NurbsBasisSample Original = EvaluateNurbsBasis(Source.Knots, Source.Degree, SourceCount, Greville);
        for (int Column = 0; Column <= Source.Degree; ++Column)
        {
            const int    Pole   = Original.First + Column;
            const double Scaled = Original.Values[0][Column] * Source.Weights[Pole];
            RightSides.At(Row, 0) += Scaled * Source.Poles[Pole].X;
            RightSides.At(Row, 1) += Scaled * Source.Poles[Pole].Y;
            RightSides.At(Row, 2) += Scaled * Source.Poles[Pole].Z;
            RightSides.At(Row, 3) += Scaled;
        }
    }
    const std::optional<DenseLinearArray> Solution = SolveDenseSystem(std::move(Matrix), std::move(RightSides));
    if (!Solution) return std::nullopt;
    NurbsCurveDefinition Result;
    Result.Degree = Degree;
    Result.Knots  = Knots;
    for (int Pole = 0; Pole < Count; ++Pole)
    {
        const double Weight = Solution->At(Pole, 3);
        if (!(Weight > 0.0)) return std::nullopt;
        Result.Poles.push_back(Vector3{ Solution->At(Pole, 0), Solution->At(Pole, 1), Solution->At(Pole, 2) } * (1.0 / Weight));
        Result.Weights.push_back(Weight);
    }
    // Ends are interpolated exactly by a clamped curve; pin them so endpoints stay bit-identical.
    Result.Poles.front() = Source.Poles.front(); Result.Weights.front() = Source.Weights.front();
    Result.Poles.back()  = Source.Poles.back();  Result.Weights.back()  = Source.Weights.back();
    return Result;
}

NurbsCurveDefinition ExtractPoleRow(const NurbsSurfaceDefinition& Definition, int Row)
{
    NurbsCurveDefinition Curve;
    Curve.Degree = Definition.DegreeU;
    Curve.Knots  = Definition.KnotsU;
    for (int Column = 0; Column < Definition.CountU; ++Column)
    {
        Curve.Poles.push_back(Definition.Poles[static_cast<size_t>(Column) * Definition.CountV + Row]);
        Curve.Weights.push_back(Definition.Weights[static_cast<size_t>(Column) * Definition.CountV + Row]);
    }
    return Curve;
}

NurbsCurveDefinition ExtractPoleColumn(const NurbsSurfaceDefinition& Definition, int Column)
{
    NurbsCurveDefinition Curve;
    Curve.Degree = Definition.DegreeV;
    Curve.Knots  = Definition.KnotsV;
    const size_t First = static_cast<size_t>(Column) * Definition.CountV;
    Curve.Poles.assign(Definition.Poles.begin() + First, Definition.Poles.begin() + First + Definition.CountV);
    Curve.Weights.assign(Definition.Weights.begin() + First, Definition.Weights.begin() + First + Definition.CountV);
    return Curve;
}

NurbsSurfaceDefinition AssembleFromRows(const std::vector<NurbsCurveDefinition>& Rows, int DegreeV, const std::vector<double>& KnotsV)
{
    NurbsSurfaceDefinition Surface;
    Surface.DegreeU = Rows.front().Degree;
    Surface.DegreeV = DegreeV;
    Surface.CountU  = static_cast<int>(Rows.front().Poles.size());
    Surface.CountV  = static_cast<int>(Rows.size());
    Surface.KnotsU  = Rows.front().Knots;
    Surface.KnotsV  = KnotsV;
    Surface.Poles.resize(static_cast<size_t>(Surface.CountU) * Surface.CountV);
    Surface.Weights.resize(Surface.Poles.size());
    for (int Row = 0; Row < Surface.CountV; ++Row)
    {
        for (int Column = 0; Column < Surface.CountU; ++Column)
        {
            Surface.Poles[static_cast<size_t>(Column) * Surface.CountV + Row]   = Rows[Row].Poles[Column];
            Surface.Weights[static_cast<size_t>(Column) * Surface.CountV + Row] = Rows[Row].Weights[Column];
        }
    }
    return Surface;
}

NurbsSurfaceDefinition AssembleFromColumns(const std::vector<NurbsCurveDefinition>& Columns, int DegreeU, const std::vector<double>& KnotsU)
{
    NurbsSurfaceDefinition Surface;
    Surface.DegreeU = DegreeU;
    Surface.DegreeV = Columns.front().Degree;
    Surface.CountU  = static_cast<int>(Columns.size());
    Surface.CountV  = static_cast<int>(Columns.front().Poles.size());
    Surface.KnotsU  = KnotsU;
    Surface.KnotsV  = Columns.front().Knots;
    for (const NurbsCurveDefinition& Column : Columns)
    {
        Surface.Poles.insert(Surface.Poles.end(), Column.Poles.begin(), Column.Poles.end());
        Surface.Weights.insert(Surface.Weights.end(), Column.Weights.begin(), Column.Weights.end());
    }
    return Surface;
}

std::optional<NurbsSurfaceDefinition> ElevateNurbsSurfaceDegree(const NurbsSurfaceDefinition& Definition, int DegreeU, int DegreeV)
{
    NurbsSurfaceDefinition Surface = Definition;
    if (DegreeU != Surface.DegreeU)
    {
        std::vector<NurbsCurveDefinition> Rows;
        for (int Row = 0; Row < Surface.CountV; ++Row)
        {
            std::optional<NurbsCurveDefinition> Elevated = ElevateNurbsCurveDegree(ExtractPoleRow(Surface, Row), DegreeU);
            if (!Elevated) return std::nullopt;
            Rows.push_back(std::move(*Elevated));
        }
        Surface = AssembleFromRows(Rows, Surface.DegreeV, Surface.KnotsV);
    }
    if (DegreeV != Surface.DegreeV)
    {
        std::vector<NurbsCurveDefinition> Columns;
        for (int Column = 0; Column < Surface.CountU; ++Column)
        {
            std::optional<NurbsCurveDefinition> Elevated = ElevateNurbsCurveDegree(ExtractPoleColumn(Surface, Column), DegreeV);
            if (!Elevated) return std::nullopt;
            Columns.push_back(std::move(*Elevated));
        }
        Surface = AssembleFromColumns(Columns, Surface.DegreeU, Surface.KnotsU);
    }
    return Surface;
}

NurbsSurfaceDefinition RefineNurbsSurfaceKnots(const NurbsSurfaceDefinition& Definition, const std::vector<double>& InsertedU, const std::vector<double>& InsertedV)
{
    NurbsSurfaceDefinition Surface = Definition;
    if (!InsertedU.empty())
    {
        std::vector<NurbsCurveDefinition> Rows;
        for (int Row = 0; Row < Surface.CountV; ++Row) Rows.push_back(RefineNurbsCurveKnots(ExtractPoleRow(Surface, Row), InsertedU));
        Surface = AssembleFromRows(Rows, Surface.DegreeV, Surface.KnotsV);
    }
    if (!InsertedV.empty())
    {
        std::vector<NurbsCurveDefinition> Columns;
        for (int Column = 0; Column < Surface.CountU; ++Column) Columns.push_back(RefineNurbsCurveKnots(ExtractPoleColumn(Surface, Column), InsertedV));
        Surface = AssembleFromColumns(Columns, Surface.DegreeU, Surface.KnotsU);
    }
    return Surface;
}

NurbsSurfaceDefinition TransposeNurbsSurface(const NurbsSurfaceDefinition& Definition)
{
    std::vector<NurbsCurveDefinition> Columns;
    for (int Row = 0; Row < Definition.CountV; ++Row) Columns.push_back(ExtractPoleRow(Definition, Row));
    return AssembleFromColumns(Columns, Definition.DegreeV, Definition.KnotsV);
}

NurbsSurfaceDefinition ReverseNurbsSurface(const NurbsSurfaceDefinition& Definition, bool AlongU)
{
    NurbsSurfaceDefinition Surface = Definition;
    auto Flip = [](std::vector<double>& Knots) { std::reverse(Knots.begin(), Knots.end()); const double Sum = Knots.front() + Knots.back(); for (double& Knot : Knots) Knot = Sum - Knot; };
    if (AlongU)
    {
        Flip(Surface.KnotsU);
        for (int Column = 0; Column < Surface.CountU; ++Column)
        {
            for (int Row = 0; Row < Surface.CountV; ++Row)
            {
                const size_t From = static_cast<size_t>(Surface.CountU - 1 - Column) * Surface.CountV + Row;
                Surface.Poles[static_cast<size_t>(Column) * Surface.CountV + Row]   = Definition.Poles[From];
                Surface.Weights[static_cast<size_t>(Column) * Surface.CountV + Row] = Definition.Weights[From];
            }
        }
        return Surface;
    }
    Flip(Surface.KnotsV);
    for (int Column = 0; Column < Surface.CountU; ++Column)
    {
        for (int Row = 0; Row < Surface.CountV; ++Row)
        {
            const size_t From = static_cast<size_t>(Column) * Surface.CountV + (Surface.CountV - 1 - Row);
            Surface.Poles[static_cast<size_t>(Column) * Surface.CountV + Row]   = Definition.Poles[From];
            Surface.Weights[static_cast<size_t>(Column) * Surface.CountV + Row] = Definition.Weights[From];
        }
    }
    return Surface;
}

std::optional<NurbsSurface> FinishNurbsSurface(const NurbsSurfaceDefinition& Definition)
{
    return ConstructNurbsSurface(Definition.DegreeU, Definition.DegreeV, Definition.CountU, Definition.CountV, Definition.KnotsU, Definition.KnotsV,
                                 Definition.Poles, Definition.Weights);
}

std::optional<NurbsSurfaceDefinition> SkinCurveFamily(const std::vector<NurbsCurveDefinition>& Curves, const CurveGroupSkinning& Skinning, std::string& Refusal)
{
    const int CountRows = static_cast<int>(Curves.size());
    if (CountRows < 2 || static_cast<int>(Skinning.Parameters.size()) != CountRows) { Refusal = "needs at least 2 curves, one parameter each"; return std::nullopt; }
    const int CountU = static_cast<int>(Curves.front().Poles.size());
    for (const NurbsCurveDefinition& Curve : Curves)
    {
        if (Curve.Degree != Curves.front().Degree || Curve.Knots != Curves.front().Knots) { Refusal = "curves are not compatible (degree / knots)"; return std::nullopt; }
    }
    auto Conditions = [&](const CurveGroupEndCondition& Condition) { return (Condition.First.empty() ? 0 : 1) + (Condition.Second.empty() ? 0 : 1); };
    const int StartCount = Conditions(Skinning.Start), EndCount = Conditions(Skinning.End);
    if ((!Skinning.Start.Second.empty() && Skinning.Start.First.empty()) || (!Skinning.End.Second.empty() && Skinning.End.First.empty()))
    {
        Refusal = "a second-derivative condition needs the first-derivative condition too";
        return std::nullopt;
    }
    for (const CurveGroupEndCondition* Condition : { &Skinning.Start, &Skinning.End })
    {
        for (const std::vector<Vector3>* Vectors : { &Condition->First, &Condition->Second })
        {
            if (!Vectors->empty() && static_cast<int>(Vectors->size()) != CountU) { Refusal = "an end condition does not match the pole count"; return std::nullopt; }
        }
    }
    if (StartCount + EndCount > 0)
    {
        for (const NurbsCurveDefinition& Curve : Curves)
        {
            for (double Weight : Curve.Weights)
            {
                if (Weight != 1.0) { Refusal = "derivative conditions on rational curves (🚧)"; return std::nullopt; }
            }
        }
    }
    const int Count = CountRows + StartCount + EndCount;
    const int Degree = std::clamp(Skinning.Degree, 1, std::min(NurbsDegreeLimit, Count - 1));

    // ① Parameter list with the conditioned ends repeated → averaged 𝑣-knots (one more span per condition).
    std::vector<double> Augmented;
    Augmented.insert(Augmented.end(), StartCount, Skinning.Parameters.front());
    Augmented.insert(Augmented.end(), Skinning.Parameters.begin(), Skinning.Parameters.end());
    Augmented.insert(Augmented.end(), EndCount, Skinning.Parameters.back());
    const std::vector<double> Knots = AveragedKnots(Augmented, Degree);

    // ② Rows: interpolation at every 𝑣ₖ, then N′ / N″ at the conditioned ends.
    DenseLinearArray Matrix(Count, Count), RightSides(Count, 4 * CountU);
    int Row = 0;
    for (int Curve = 0; Curve < CountRows; ++Curve, ++Row)
    {
        const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, Degree, Count, Skinning.Parameters[Curve]);
        for (int Column = 0; Column <= Degree; ++Column) Matrix.At(Row, Basis.First + Column) = Basis.Values[0][Column];
        for (int Pole = 0; Pole < CountU; ++Pole)
        {
            const double  Weight = Curves[Curve].Weights[Pole];
            const Vector3 Point  = Curves[Curve].Poles[Pole];
            RightSides.At(Row, 4 * Pole + 0) = Weight * Point.X;
            RightSides.At(Row, 4 * Pole + 1) = Weight * Point.Y;
            RightSides.At(Row, 4 * Pole + 2) = Weight * Point.Z;
            RightSides.At(Row, 4 * Pole + 3) = Weight;
        }
    }
    auto AddDerivative = [&](double Parameter, int Order, const std::vector<Vector3>& Vectors)
    {
        const NurbsBasisSample Basis = EvaluateNurbsBasis(Knots, Degree, Count, Parameter);
        for (int Column = 0; Column <= Degree; ++Column) Matrix.At(Row, Basis.First + Column) = Basis.Values[Order][Column];
        for (int Pole = 0; Pole < CountU; ++Pole)
        {
            RightSides.At(Row, 4 * Pole + 0) = Vectors[Pole].X;
            RightSides.At(Row, 4 * Pole + 1) = Vectors[Pole].Y;
            RightSides.At(Row, 4 * Pole + 2) = Vectors[Pole].Z;
            RightSides.At(Row, 4 * Pole + 3) = 0.0;                                         // w ≡ 1: its derivatives vanish
        }
        ++Row;
    };
    if (!Skinning.Start.First.empty())  AddDerivative(Skinning.Parameters.front(), 1, Skinning.Start.First);
    if (!Skinning.Start.Second.empty()) AddDerivative(Skinning.Parameters.front(), 2, Skinning.Start.Second);
    if (!Skinning.End.First.empty())    AddDerivative(Skinning.Parameters.back(), 1, Skinning.End.First);
    if (!Skinning.End.Second.empty())   AddDerivative(Skinning.Parameters.back(), 2, Skinning.End.Second);
    const std::optional<DenseLinearArray> Solution = SolveDenseSystem(std::move(Matrix), std::move(RightSides));
    if (!Solution) { Refusal = "singular interpolation across the curves"; return std::nullopt; }

    // ③ Back to Euclidean poles (polynomial curves keep w ≡ 1 exactly: the weight column solves to 1 ± ε).
    bool RationalCondition = false;
    for (const NurbsCurveDefinition& Curve : Curves) for (double Weight : Curve.Weights) RationalCondition = RationalCondition || Weight != 1.0;
    NurbsSurfaceDefinition Surface;
    Surface.DegreeU = Curves.front().Degree;
    Surface.DegreeV = Degree;
    Surface.CountU  = CountU;
    Surface.CountV  = Count;
    Surface.KnotsU  = Curves.front().Knots;
    Surface.KnotsV  = Knots;
    Surface.Poles.resize(static_cast<size_t>(CountU) * Count);
    Surface.Weights.resize(Surface.Poles.size());
    for (int Pole = 0; Pole < CountU; ++Pole)
    {
        for (int Index = 0; Index < Count; ++Index)
        {
            const double Weight = RationalCondition ? Solution->At(Index, 4 * Pole + 3) : 1.0;
            if (!(Weight > 0.0)) { Refusal = "interpolated weight ≤ 0 at pole column " + std::to_string(Pole) + " (curves with very different weights)"; return std::nullopt; }
            const size_t Slot = static_cast<size_t>(Pole) * Count + Index;
            Surface.Poles[Slot]   = Vector3{ Solution->At(Index, 4 * Pole + 0), Solution->At(Index, 4 * Pole + 1), Solution->At(Index, 4 * Pole + 2) } * (1.0 / Weight);
            Surface.Weights[Slot] = Weight;
        }
    }
    // A clamped skin's end pole rows ARE the end curves (S(𝑢, 0) = row 0): pin them so neighbours share bit-identical rows.
    for (int Pole = 0; Pole < CountU; ++Pole)
    {
        Surface.Poles[static_cast<size_t>(Pole) * Count]               = Curves.front().Poles[Pole];
        Surface.Weights[static_cast<size_t>(Pole) * Count]             = Curves.front().Weights[Pole];
        Surface.Poles[static_cast<size_t>(Pole) * Count + Count - 1]   = Curves.back().Poles[Pole];
        Surface.Weights[static_cast<size_t>(Pole) * Count + Count - 1] = Curves.back().Weights[Pole];
    }
    return Surface;
}

std::optional<std::pair<NurbsCurveDefinition, NurbsCurveDefinition>> SplitNurbsCurve(const NurbsCurveDefinition& Definition, double τ)
{
    const int Degree = Definition.Degree;
    const int Count  = static_cast<int>(Definition.Poles.size());
    if (Degree < 1 || Count < Degree + 1 || static_cast<int>(Definition.Knots.size()) != Count + Degree + 1) return std::nullopt;
    if (!(τ > Definition.Knots[Degree] && τ < Definition.Knots[Count])) return std::nullopt;
    // ① τ to multiplicity p (RefineNurbsCurveKnots skips values already at p)
    const int Present = static_cast<int>(std::count(Definition.Knots.begin(), Definition.Knots.end(), τ));
    NurbsCurveDefinition Refined = Definition;
    if (Present < Degree) Refined = RefineNurbsCurveKnots(Definition, std::vector<double>(static_cast<size_t>(Degree - Present), τ));
    const auto First = std::find(Refined.Knots.begin(), Refined.Knots.end(), τ);
    if (First == Refined.Knots.end() || static_cast<int>(std::count(Refined.Knots.begin(), Refined.Knots.end(), τ)) < Degree) return std::nullopt;
    // ② s = index of the first τ: the first half owns poles 0 … s − 1, the second s − 1 … n (pole s − 1 lies on τ)
    const int Start = static_cast<int>(First - Refined.Knots.begin());
    std::pair<NurbsCurveDefinition, NurbsCurveDefinition> Halves;
    NurbsCurveDefinition& Low  = Halves.first;
    NurbsCurveDefinition& High = Halves.second;
    Low.Degree = High.Degree = Degree;
    Low.Knots.assign(Refined.Knots.begin(), Refined.Knots.begin() + Start + Degree);
    Low.Knots.push_back(τ);
    Low.Poles.assign(Refined.Poles.begin(), Refined.Poles.begin() + Start);
    Low.Weights.assign(Refined.Weights.begin(), Refined.Weights.begin() + Start);
    High.Knots.push_back(τ);
    High.Knots.insert(High.Knots.end(), Refined.Knots.begin() + Start, Refined.Knots.end());
    High.Poles.assign(Refined.Poles.begin() + Start - 1, Refined.Poles.end());
    High.Weights.assign(Refined.Weights.begin() + Start - 1, Refined.Weights.end());
    return Halves;
}

std::optional<NurbsCurveDefinition> JoinNurbsCurves(const std::vector<NurbsCurveDefinition>& Pieces, const std::vector<double>& Spans, double Join)
{
    if (Pieces.empty() || Spans.size() != Pieces.size()) return std::nullopt;
    // ① one degree (exact elevation)
    int Degree = 1;
    for (const NurbsCurveDefinition& Piece : Pieces) Degree = std::max(Degree, Piece.Degree);
    std::vector<NurbsCurveDefinition> Raised;
    for (const NurbsCurveDefinition& Piece : Pieces)
    {
        if (Piece.Poles.size() < 2 || Piece.Weights.size() != Piece.Poles.size()) return std::nullopt;
        if (Piece.Degree == Degree) { Raised.push_back(Piece); continue; }
        std::optional<NurbsCurveDefinition> Elevated = ElevateNurbsCurveDegree(Piece, Degree);
        if (!Elevated) return std::nullopt;
        Raised.push_back(std::move(*Elevated));
    }
    // ② concatenate: [a₀]^(p+1) I₀ [b₀]^p I₁ [b₁]^p … I_last [b_last]^(p+1); poles P₀ then P_k without their first pole
    NurbsCurveDefinition Result;
    Result.Degree = Degree;
    double Along  = 0.0;                                                                     // [-] - Parameter where piece k starts
    for (size_t Index = 0; Index < Raised.size(); ++Index)
    {
        const NurbsCurveDefinition& Piece = Raised[Index];
        if (!(Spans[Index] > 0.0) || !std::isfinite(Spans[Index])) return std::nullopt;
        const NurbsDomain Domain = MeasureNurbsCurveDomain(Piece);
        const double      Width  = Domain.High - Domain.Low;
        if (!(Width > 0.0)) return std::nullopt;
        const auto Map = [&](double Knot) { return Along + (Knot - Domain.Low) / Width * Spans[Index]; };
        double Scale = 1.0;                                                                  // [-] - Weight rescale of this piece
        if (Index == 0)
        {
            Result.Knots.assign(static_cast<size_t>(Degree + 1), Along);
        }
        else
        {
            if (Length(Piece.Poles.front() - Result.Poles.back()) > Join) return std::nullopt;
            Scale = Result.Weights.back() / Piece.Weights.front();
        }
        const size_t Last = Piece.Knots.size() - static_cast<size_t>(Degree) - 1;            // Index of the first end knot
        for (size_t Knot = static_cast<size_t>(Degree) + 1; Knot < Last; ++Knot) Result.Knots.push_back(Map(Piece.Knots[Knot]));
        Along += Spans[Index];
        for (int Repeat = 0; Repeat < Degree; ++Repeat) Result.Knots.push_back(Along);
        for (size_t Pole = Index == 0 ? 0 : 1; Pole < Piece.Poles.size(); ++Pole)
        {
            Result.Poles.push_back(Piece.Poles[Pole]);
            Result.Weights.push_back(Piece.Weights[Pole] * Scale);
        }
    }
    Result.Knots.push_back(Along);
    if (Result.Knots.size() != Result.Poles.size() + static_cast<size_t>(Degree) + 1) return std::nullopt;
    return Result;
}

} // namespace Frontier::GeometrySpace::Parametric
