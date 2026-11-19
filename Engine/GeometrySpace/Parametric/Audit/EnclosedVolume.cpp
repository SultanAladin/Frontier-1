/*====================================================================================================================================
                                                       ENCLOSEDVOLUME.CPP
====================================================================================================================================*/
// 🧩 Exact enclosed volume and parametric face area by Green's theorem over SP-curves (no tessellation)

#include "Engine/GeometrySpace/Parametric/Audit/EnclosedVolume.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"

#include "Engine/GeometrySpace/Parametric/Geometry/NurbsCarrier.h"
#include "Engine/GeometrySpace/Parametric/Geometry/RollingBallCarrier.h"
#include "Engine/GeometrySpace/Parametric/Intersection/IntersectionCurve.h"

#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    INTERNAL FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 8-point Gauss–Legendre on [−1, 1]: exact for polynomials of degree ≤ 15 per panel.
    constexpr std::array<double, 8> GaussAbscissae = {
        -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
         0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363 };
    constexpr std::array<double, 8> GaussWeights = {
         0.1012285362903763,  0.2223810344533745,  0.3137066458778873,  0.3626837833783620,
         0.3626837833783620,  0.3137066458778873,  0.2223810344533745,  0.1012285362903763 };
    constexpr double PanelSpan  = std::numbers::pi / 16.0;                                   // [-] - Max parameter width per panel
    // ⚠️ PanelSpan is an angle; plane charts and line parameters are lengths, so a body at 10³ cut them into ~10⁵ panels
    //    per sample. 64 × π/16 = 4π leaves every angular span untouched.
    constexpr int    PanelLimit = 64;                                                        // [-] - Initial panels per interval
    // ⚠️ Safety net: a hard cap on the halvings of one refined interval. A traced intersection whose C′ was inconsistent
    //    with C near a tangential crossing (fixed in EvaluateIntersectionCurve) kept every gap above the floor; the
    //    bisection spread to depth 24 across a whole neighbourhood: 1 to 330 s per volume, and the Sketcher bevel preview
    //    froze the workspace. The cap bounds the work whatever the integrand; the stagnation floor below normally
    //    accepts noisy panels long before the cap is reached.
    constexpr long   SplitLimit      = 2048;                                                 // [-] - Halvings per refined interval
    constexpr double StagnationRatio = 1.0e3;                                                // [-] - Stagnation floor / acceptance floor

    // Composite Gauss–Legendre of a scalar integrand over [Low, High].
    template <typename Integrand>
    [[nodiscard]] double IntegrateComposite(double Low, double High, double PanelWidth, const Integrand& Function)
    {
        const double Span       = High - Low;                                                // [-] - Signed interval width
        const int    PanelCount = std::clamp(static_cast<int>(std::ceil(std::fabs(Span) / PanelWidth)), 1, PanelLimit);
        const double Width      = Span / PanelCount;                                         // [-] - Signed panel width
        double       Total      = 0.0;                                                       // [-] - Accumulated integral
        for (int PanelIndex = 0; PanelIndex < PanelCount; ++PanelIndex)
        {
            const double Centre = Low + Width * (PanelIndex + 0.5);                          // [-] - Panel midpoint
            for (size_t AbscissaIndex = 0; AbscissaIndex < GaussAbscissae.size(); ++AbscissaIndex)
            {
                Total += GaussWeights[AbscissaIndex] * Function(Centre + GaussAbscissae[AbscissaIndex] * Width * 0.5);
            }
        }
        return Total * Width * 0.5;
    }

    // 📝 One panel's integral and ∫|f| (same nodes): the magnitude makes the acceptance test scale-free.
    struct PanelEstimate
    {
        double Value     = 0.0;                                                              // [-] - ∫ f
        double Magnitude = 0.0;                                                              // [-] - ∫ |f|
    };

    // 8-point Gauss–Legendre on one panel.
    template <typename Integrand>
    [[nodiscard]] PanelEstimate IntegratePanel(double Low, double High, const Integrand& Function)
    {
        const double  Centre = 0.5 * (Low + High), Half = 0.5 * (High - Low);                // [-] - Panel midpoint, half width
        PanelEstimate Total;
        for (size_t AbscissaIndex = 0; AbscissaIndex < GaussAbscissae.size(); ++AbscissaIndex)
        {
            const double Sample = Function(Centre + GaussAbscissae[AbscissaIndex] * Half);
            Total.Value     += GaussWeights[AbscissaIndex] * Sample;
            Total.Magnitude += GaussWeights[AbscissaIndex] * std::fabs(Sample);
        }
        return { Total.Value * Half, Total.Magnitude * std::fabs(Half) };
    }

    // Adaptive composite Gauss–Legendre: a panel is accepted when it agrees with its two halves to 10⁻¹⁴ of ∫|f|.
    // ⚠️ Projected SP-curves passing near a chart pole swing 𝑢 by ~π over a tiny parameter range; fixed panels miss it.
    // ⚠️ The test is relative to ∫|f| (not max(1, |∫f|)): an absolute floor made bodies at 10³ recurse to depth 24
    //    everywhere (round-off never meets it) and bodies at 10⁻³ stop at ~10⁻⁶ relative accuracy.
    template <typename Integrand>
    // ⚠️ Floor (absolute, from the whole face) accepts panels whose integrand is analytically zero but numerically noise
    //    (𝑣′ ~ 10⁻¹⁷ along a projected SP-curve): their relative disagreement never shrinks.
    // ⚠️ Round-off stagnation: a procedural carrier (traced spine, projected rail) evaluates to ~10⁻¹² only, so its
    //    gaps stop shrinking (smooth panels shrink ~2⁻¹⁶ per level, kinked ones ~⅛) and the refinement ran to depth 24
    //    on every panel. A gap already inside the noise band (≤ 10⁻⁹ of ∫|f|) shrinking slower than ¼ per level (noise,
    //    or a sub-tolerance jump whose error is the gap itself) is accepted; larger
    //    gaps (an unresolved pole swing) always refine.
    // ⚠️ Noisy carrier: a gap that stagnates (shrinks slower than ¼ per level) below StagnationRatio × Floor is noise in
    //    the integrand (absolute, so a tiny panel's relative gap may be large); refining it only spends evaluations.
    [[nodiscard]] double IntegrateAdaptive(double Low, double High, double Coarse, const Integrand& Function, double Floor, long& Splits, int Depth,
                                           double ParentGap = std::numeric_limits<double>::infinity())
    {
        const double        Middle = 0.5 * (Low + High);
        const PanelEstimate Left   = IntegratePanel(Low, Middle, Function);
        const PanelEstimate Right  = IntegratePanel(Middle, High, Function);
        const double        Gap    = std::fabs(Left.Value + Right.Value - Coarse);           // [-] - Halves vs whole
        const double        Scale  = Left.Magnitude + Right.Magnitude;                       // [-] - ∫|f| over the panel
        if (Depth >= 24 || Splits <= 0 || Gap <= 1.0e-14 * Scale || Gap <= Floor) return Left.Value + Right.Value;
        if (Gap > 0.25 * ParentGap && (Gap <= 1.0e-9 * Scale || Gap <= StagnationRatio * Floor)) return Left.Value + Right.Value;
        --Splits;
        const double LeftTotal = IntegrateAdaptive(Low, Middle, Left.Value, Function, Floor, Splits, Depth + 1, Gap);
        return LeftTotal + IntegrateAdaptive(Middle, High, Right.Value, Function, Floor, Splits, Depth + 1, Gap);
    }

    template <typename Integrand>
    [[nodiscard]] double IntegrateRefined(double Low, double High, double PanelWidth, const Integrand& Function, double Floor)
    {
        const int    PanelCount = std::clamp(static_cast<int>(std::ceil(std::fabs(High - Low) / PanelWidth)), 1, PanelLimit);
        const double Width      = (High - Low) / PanelCount;                                 // [-] - Signed panel width
        double       Total      = 0.0;
        long         Splits     = SplitLimit;                                                // [-] - Halvings left for this interval
        for (int PanelIndex = 0; PanelIndex < PanelCount; ++PanelIndex)
        {
            const double PanelLow = Low + Width * PanelIndex, PanelHigh = PanelLow + Width;
            Total += IntegrateAdaptive(PanelLow, PanelHigh, IntegratePanel(PanelLow, PanelHigh, Function).Value, Function, Floor, Splits, 0);
        }
        return Total;
    }

    // ∫|f| over the initial panels of IntegrateRefined (the floor's scale).
    template <typename Integrand>
    [[nodiscard]] double EstimateMagnitude(double Low, double High, double PanelWidth, const Integrand& Function)
    {
        const int    PanelCount = std::clamp(static_cast<int>(std::ceil(std::fabs(High - Low) / PanelWidth)), 1, PanelLimit);
        const double Width      = (High - Low) / PanelCount;                                 // [-] - Signed panel width
        double       Total      = 0.0;
        for (int PanelIndex = 0; PanelIndex < PanelCount; ++PanelIndex) Total += IntegratePanel(Low + Width * PanelIndex, Low + Width * (PanelIndex + 1), Function).Magnitude;
        return Total;
    }

    // 📝 Derivative noise of a face's integrand: its carrier's, and that of every boundary curve traced on a procedural
    //    carrier (a mitre between two rolling-ball blends, a wall section of one). Such a curve evaluates to ~10⁻⁹ only,
    //    so its SP-curve carries that noise into the outer integral even on an analytic face (a torus blend next to a
    //    traced one): a floor from the carrier alone (0) left those panels refining to the split cap (~0.8 s a face).
    [[nodiscard]] double MeasureFaceNoise(const ModelPartition& Partition, FaceToken Face)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Face);
        double Noise = Profile.Surface.PresentCondition() ? MeasureDerivativeNoise(Partition.Surfaces.Resolve(Profile.Surface)) : 0.0;
        for (LoopToken Loop : Profile.Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin)
            {
                const EdgeProfile& Edge = Partition.Edges.Resolve(Partition.Fins.Resolve(Fin).Edge);
                if (!Edge.Curve.PresentCondition()) return;
                const CurveGeometry& Curve = Partition.Curves.Resolve(Edge.Curve);
                if (std::holds_alternative<ContactCurve>(Curve))
                {
                    Noise = std::max(Noise, RollingBallDerivativeNoise);
                    return;
                }
                const auto* Traced = std::get_if<IntersectionCurve>(&Curve);
                if (Traced == nullptr || !Traced->Trace) return;
                Noise = std::max({ Noise, MeasureDerivativeNoise(Traced->Trace->First), MeasureDerivativeNoise(Traced->Trace->Second) });
                if (ProceduralSurfaceCondition(Traced->Trace->First) || ProceduralSurfaceCondition(Traced->Trace->Second))
                    Noise = std::max(Noise, RollingBallDerivativeNoise);
            });
        }
        return Noise;
    }

    // ⅓ P·(∂P/∂𝑢 × ∂P/∂𝑣) — the flux density of the position field over the surface chart.
    [[nodiscard]] double FluxDensity(const SurfaceGeometry& Surface, Vector2 Parameter)
    {
        const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
        return Dot(Sample.Position, Cross(Sample.DerivativeU, Sample.DerivativeV)) / 3.0;
    }

    // Line integral of G(q) dq_𝑣 along one fin, where G = ∫_0^𝑢 Density(s, 𝑣) ds (Density supplied by the caller).
    // Magnitude mode returns ∫|G 𝑣′| dτ over the initial panels instead (no adaptive refinement).
    // ⚠️ Knots: a traced rolling-ball spine is only C¹ at its knots, so the flux density (through T′) jumps there; the
    //    inner 𝑢 integral is split at the chart's breakpoints (Chart may be null: density without a surface).
    // 📝 How a density depends on 𝑢 along one chart row (fixed 𝑣): the inner integral ∫ density d𝑢 is then closed form.
    //    Constant: a plane's flux density P·(P𝑢 × P𝑣) (P𝑢 × P𝑣 is the constant normal, P·n the constant distance) and the
    //    unit density of the parametric area. Trigonometric: the analytic carriers of revolution (cylinder, cone, sphere,
    //    torus) put 𝑢 only into cos 𝑢·X + sin 𝑢·Y, so P, P𝑢 and P𝑣 are trigonometric of degree 1 in 𝑢 and the flux density
    //    is a trigonometric polynomial of degree ≤ 3: seven equally spaced samples give its coefficients exactly (discrete
    //    Fourier, 7 > 2·3) and its primitive is closed form. Both replace up to 64 Gauss panels (512 evaluations) per
    //    outer node with 1 or 7 and agree with them to round-off.
    enum class RowForm : uint8_t
    {
        General             = 0,                                                             // Composite Gauss–Legendre
        Constant            = 1,                                                             // density(𝑢, 𝑣) = c(𝑣)
        Trigonometric       = 2,                                                             // degree ≤ 3 in 𝑢
        Across              = 3,                                                             // degree ≤ 3 in 𝑣: rows run along 𝑣
        PiecewisePolynomial = 4,                                                             // degree ≤ 15 in 𝑢 per knot span (1 Gauss panel)
    };

    // Illinois secant-bracket root finder on a continuous bracket [Left, Right] with LeftGap * RightGap < 0.
    template <typename ScalarFunction>
    [[nodiscard]] double PolishBracketRoot(double Left, double Right, double LeftGap, double RightGap, const ScalarFunction& Evaluate)
    {
        const double Tolerance = 1.0e-14 * std::max(1.0, std::fabs(Left) + std::fabs(Right));
        int Side = 0;                                                                        // [-] - Illinois side counter
        for (int Step = 0; Step < 60 && std::fabs(Right - Left) > Tolerance; ++Step)
        {
            const double Denominator = RightGap - LeftGap;
            double       Candidate   = 0.5 * (Left + Right);
            if (std::fabs(Denominator) > 1.0e-300)
            {
                const double Secant = Left - LeftGap * (Right - Left) / Denominator;
                const double Margin = 0.05 * (Right - Left);
                if ((Right >= Left && Secant > Left + Margin && Secant < Right - Margin) || (Right < Left && Secant < Left + Margin && Secant > Right - Margin))
                    Candidate = Secant;
            }
            const double Gap = Evaluate(Candidate);
            if (Gap == 0.0) return Candidate;
            if (Gap * LeftGap > 0.0)
            {
                Left    = Candidate;
                LeftGap = Gap;
                if (Side == -1) RightGap *= 0.5;
                Side = -1;
            }
            else
            {
                Right    = Candidate;
                RightGap = Gap;
                if (Side == 1) LeftGap *= 0.5;
                Side = 1;
            }
        }
        return 0.5 * (Left + Right);
    }

    // ∫_{Low}^{High} Row(𝑢) d𝑢 of a trigonometric polynomial of degree ≤ 3 from its values at 2πk/7.
    template <typename Row>
    [[nodiscard]] double IntegrateTrigonometricRow(double Low, double High, const Row& Function)
    {
        constexpr int SampleCount = 7;                                                       // [-] - 2·3 + 1 nodes
        static const std::array<std::array<double, 2>, SampleCount * 3> Basis = []
        {
            std::array<std::array<double, 2>, SampleCount * 3> Table{};
            for (int Index = 0; Index < SampleCount; ++Index)
                for (int Order = 1; Order <= 3; ++Order)
                {
                    const double Angle = Order * 2.0 * std::numbers::pi * Index / SampleCount;
                    Table[Index * 3 + Order - 1] = { std::cos(Angle), std::sin(Angle) };
                }
            return Table;
        }();
        double Mean = 0.0;                                                                   // [-] - a₀
        std::array<double, 3> Cosine{}, Sine{};                                              // [-] - a_k, b_k
        for (int Index = 0; Index < SampleCount; ++Index)
        {
            const double Value = Function(2.0 * std::numbers::pi * Index / SampleCount);
            Mean += Value;
            for (int Order = 0; Order < 3; ++Order)
            {
                Cosine[Order] += Value * Basis[Index * 3 + Order][0];
                Sine[Order]   += Value * Basis[Index * 3 + Order][1];
            }
        }
        Mean /= SampleCount;
        const auto Primitive = [&](double Angle)
        {
            double Total = Mean * Angle;
            for (int Order = 0; Order < 3; ++Order)
            {
                const double Frequency = Order + 1.0;
                Total += (2.0 / SampleCount) * (Cosine[Order] * std::sin(Frequency * Angle) - Sine[Order] * std::cos(Frequency * Angle)) / Frequency;
            }
            return Total;
        };
        return Primitive(High) - Primitive(Low);
    }

    template <typename Density>
    [[nodiscard]] double IntegrateFin(const ModelPartition& Partition, FinToken Fin, const Density& Function, double Floor, bool MagnitudeMode = false, const SurfaceGeometry* Chart = nullptr,
                                      RowForm Form = RowForm::General)
    {
        const FinProfile& Use = Partition.Fins.Resolve(Fin);
        if (!Use.SurfaceCurve.has_value()) return std::nan("");
        const ParameterCurve& Curve = *Use.SurfaceCurve;
        // 📝 Any constant base works (∮ H(𝑣) d𝑣 = 0 on a closed chart loop); a NURBS patch starts at its 𝑢-domain so the
        //    inner integral never extrapolates the patch.
        double LowerLimit = 0.0;                                                             // [-] - Inner lower limit
        if (Chart != nullptr) if (const NurbsSurface* Spline = ResolveNurbsChart(*Chart)) LowerLimit = MeasureNurbsSurfaceDomain(*Spline->Definition, true).Low;
        // ⚠️ An open rolling-ball spine (a ball-law spine extended past its edge) exists only over its range: below it the
        //    chart clamps 𝑢, so the inner integral starts at the spine's low end.
        if (Chart != nullptr) if (const auto* Ball = std::get_if<RollingBallSurface>(Chart); Ball && Ball->Definition->Period == 0.0 && std::isfinite(Ball->Definition->SpineLow))
            LowerLimit = Ball->Definition->SpineLow;
        const auto Integrand = [&](double τ)
        {
            const ParameterSample Sample = EvaluateParameterCurve(Curve, τ);
            if (Form == RowForm::Across)
            {
                // ∬ f d𝑢 d𝑣 = ∮ −Q d𝑢 with Q(𝑢, 𝑣) = ∫₀^𝑣 f(𝑢, t) dt (Green, the 𝑣-primitive instead of the 𝑢-primitive).
                if (Sample.Derivative.X == 0.0) return 0.0;
                const auto Column = [&](double t) { return Function(Vector2{ Sample.Position.X, t }); };
                return -IntegrateTrigonometricRow(0.0, Sample.Position.Y, Column) * Sample.Derivative.X;
            }
            if (Sample.Derivative.Y == 0.0) return 0.0;
            const auto   Row   = [&](double s) { return Function(Vector2{ s, Sample.Position.Y }); };
            const double Reach = Sample.Position.X;                                          // [-] - Inner upper limit
            if (Form == RowForm::Constant) return Row(0.5 * (LowerLimit + Reach)) * (Reach - LowerLimit) * Sample.Derivative.Y;
            if (Form == RowForm::Trigonometric) return IntegrateTrigonometricRow(LowerLimit, Reach, Row) * Sample.Derivative.Y;
            std::vector<double> Knots = Chart != nullptr ? CollectSurfaceBreakpoints(*Chart, std::min(LowerLimit, Reach), std::max(LowerLimit, Reach)) : std::vector<double>{};
            if (Reach < LowerLimit) std::reverse(Knots.begin(), Knots.end());
            double Primitive = 0.0, From = LowerLimit;                                       // [-] - ∫_lower^𝑢 density, running piece start
            if (Form == RowForm::PiecewisePolynomial)
            {
                for (double Knot : Knots) { Primitive += IntegratePanel(From, Knot, Row).Value; From = Knot; }
                Primitive += IntegratePanel(From, Reach, Row).Value;
                return Primitive * Sample.Derivative.Y;
            }
            for (double Knot : Knots) { Primitive += IntegrateComposite(From, Knot, PanelSpan, Row); From = Knot; }
            Primitive += IntegrateComposite(From, Reach, PanelSpan, Row);
            return Primitive * Sample.Derivative.Y;
        };
        // 📝 Split at the carrier's breakpoints (intersection-curve knots, where it is only C¹) so every panel sees an
        //    analytic integrand: Gauss–Legendre then converges spectrally instead of O(h²) across a kink.
        const double Start = Partition.FinParameterStart(Fin), End = Partition.FinParameterEnd(Fin);
        std::vector<double> Stops = { Start };
        const EdgeProfile& Edge = Partition.Edges.Resolve(Use.Edge);
        if (Edge.Curve.PresentCondition())
        {
            std::vector<double> Breakpoints = CollectCurveBreakpoints(Partition.Curves.Resolve(Edge.Curve), Start, End);
            if (Start > End) std::reverse(Breakpoints.begin(), Breakpoints.end());
            Stops.insert(Stops.end(), Breakpoints.begin(), Breakpoints.end());
        }
        if (Chart != nullptr && Form == RowForm::Across)
        {
            // ⚠️ A traced spine is only C¹ at its knots, so the column integral jumps in slope where the SP-curve's 𝑢
            //    crosses one: stop there (64 samples once, Illinois secant-bracket on the bracketing intervals only).
            constexpr int Samples = 64;
            const auto Width = [&](double τ) { return EvaluateParameterCurve(Curve, τ).Position.X; };
            std::array<double, Samples + 1> Values{};
            double LowU = INFINITY, HighU = -INFINITY;
            for (int Index = 0; Index <= Samples; ++Index) { Values[Index] = Width(Start + (End - Start) * Index / Samples); LowU = std::min(LowU, Values[Index]); HighU = std::max(HighU, Values[Index]); }
            for (double Knot : CollectSurfaceBreakpoints(*Chart, LowU, HighU))
            {
                for (int Index = 0; Index < Samples; ++Index)
                {
                    const double LeftGap  = Values[Index] - Knot;
                    const double RightGap = Values[Index + 1] - Knot;
                    if (LeftGap == 0.0 || LeftGap * RightGap >= 0.0) continue;
                    const double Left  = Start + (End - Start) * Index / Samples;
                    const double Right = Start + (End - Start) * (Index + 1) / Samples;
                    Stops.push_back(PolishBracketRoot(Left, Right, LeftGap, RightGap, [&](double τ) { return Width(τ) - Knot; }));
                }
            }
            std::sort(Stops.begin() + 1, Stops.end(), [&](double First, double Second) { return Start <= End ? First < Second : First > Second; });
        }
        if (Chart != nullptr && ResolveNurbsChart(*Chart) != nullptr)
        {
            // ⚠️ The density is only C⁰ across a 𝑣-knot: stop where the SP-curve crosses one (64 cached samples, Illinois polish).
            constexpr int Samples = 64;
            const auto Height = [&](double τ) { return EvaluateParameterCurve(Curve, τ).Position.Y; };
            std::array<double, Samples + 1> Values{};
            double LowV = INFINITY, HighV = -INFINITY;
            for (int Index = 0; Index <= Samples; ++Index) { Values[Index] = Height(Start + (End - Start) * Index / Samples); LowV = std::min(LowV, Values[Index]); HighV = std::max(HighV, Values[Index]); }
            for (double Knot : CollectSurfaceBreakpointsAlongV(*Chart, LowV, HighV))
            {
                for (int Index = 0; Index < Samples; ++Index)
                {
                    const double LeftGap  = Values[Index] - Knot;
                    const double RightGap = Values[Index + 1] - Knot;
                    if (LeftGap == 0.0 || LeftGap * RightGap >= 0.0) continue;
                    const double Left  = Start + (End - Start) * Index / Samples;
                    const double Right = Start + (End - Start) * (Index + 1) / Samples;
                    Stops.push_back(PolishBracketRoot(Left, Right, LeftGap, RightGap, [&](double τ) { return Height(τ) - Knot; }));
                }
            }
            std::sort(Stops.begin() + 1, Stops.end(), [&](double First, double Second) { return Start <= End ? First < Second : First > Second; });
        }
        Stops.push_back(End);
        // 📝 Closed-form rows on simple SP-curves make the outer integrand itself simple: a constant row along a chart line
        //    is a polynomial of degree ≤ 2 in τ (one 8-point Gauss panel is exact to degree 15), along a chart circle a
        //    trigonometric polynomial of degree ≤ 2 in τ; a revolved row along a line of constant 𝑢 is constant. The
        //    adaptive refinement (≥ 24 evaluations per initial panel) then only re-confirms an exact value.
        //    ⚠️ Only cylinders and cones: their density is a polynomial (degree ≤ 2) in 𝑣; a sphere's or a torus's is
        //    trigonometric in 𝑣, so their meridians keep the adaptive rule.
        const ParameterLine* Line         = std::get_if<ParameterLine>(&Curve);
        const bool           Ruled        = Chart != nullptr && (std::holds_alternative<CylinderSurface>(*Chart) || std::holds_alternative<ConeSurface>(*Chart));
        const bool           Polynomial   = !MagnitudeMode && Line != nullptr && (Form == RowForm::Constant || (Form == RowForm::Trigonometric && Ruled && Line->Direction.X == 0.0));
        const bool           Trigonometric = !MagnitudeMode && Form == RowForm::Constant && std::holds_alternative<ParameterEllipse>(Curve);
        double Total = 0.0;
        for (size_t Index = 0; Index + 1 < Stops.size(); ++Index)
        {
            if (Polynomial) { Total += IntegratePanel(Stops[Index], Stops[Index + 1], Integrand).Value; continue; }
            if (Trigonometric) { Total += IntegrateTrigonometricRow(Stops[Index], Stops[Index + 1], Integrand); continue; }
            Total += MagnitudeMode ? EstimateMagnitude(Stops[Index], Stops[Index + 1], PanelSpan, Integrand) : IntegrateRefined(Stops[Index], Stops[Index + 1], PanelSpan, Integrand, Floor);
        }
        return Total;
    }

    // Σ over a face's fins: integral (absolute acceptance floor supplied) or, in magnitude mode, Σ∫|G 𝑣′| dτ.
    template <typename Density>
    [[nodiscard]] double IntegrateFaceLoops(const ModelPartition& Partition, FaceToken Face, const Density& Function, double Floor, bool MagnitudeMode, const SurfaceGeometry* Chart = nullptr,
                                            RowForm Form = RowForm::General)
    {
        double Total = 0.0;
        for (LoopToken Loop : Partition.Faces.Resolve(Face).Loops)
        {
            Partition.EnumerateLoopFins(Loop, [&](FinToken Fin) { Total += IntegrateFin(Partition, Fin, Function, Floor, MagnitudeMode, Chart, Form); });
        }
        return Total;
    }

    // Row form of the flux density on a carrier (see RowForm).
    [[nodiscard]] RowForm ResolveFluxRowForm(const SurfaceGeometry& Surface) noexcept
    {
        if (std::holds_alternative<PlaneSurface>(Surface)) return RowForm::Constant;
        if (std::holds_alternative<CylinderSurface>(Surface) || std::holds_alternative<ConeSurface>(Surface) || std::holds_alternative<SphereSurface>(Surface) ||
            std::holds_alternative<TorusSurface>(Surface)) return RowForm::Trigonometric;
        // 📝 A circular rolling-ball carrier is C(𝑢) + r(𝑢)(E₁ cos 𝑣 + E₂ sin 𝑣): P, P𝑢 and P𝑣 are trigonometric of degree 1
        //    in 𝑣, so a column of fixed 𝑢 is closed form (7 samples) where a row along a traced spine crossed every knot
        //    piece (~90 × 8 Gauss nodes per outer node).
        if (const auto* Ball = std::get_if<RollingBallSurface>(&Surface); Ball && Ball->Definition->Section == RollingBallSection::Circle) return RowForm::Across;
        if (const auto* Spline = std::get_if<NurbsSurface>(&Surface); Spline && Spline->Definition && Spline->Definition->DegreeU <= 5 && !Spline->Definition->Weights.empty())
        {
            // 📝 On a non-rational B-spline surface of 𝑢-degree p ≤ 5, S · (S𝑢 × S𝑣) has degree 3p − 1 ≤ 14 on each 𝑢-knot
            //    span, so one 8-point Gauss–Legendre panel per span integrates the inner row to exact machine precision.
            const double ReferenceWeight  = Spline->Definition->Weights.front();
            bool         UniformCondition = ReferenceWeight > 0.0;
            for (double Weight : Spline->Definition->Weights)
                if (std::fabs(Weight - ReferenceWeight) > 1.0e-15 * ReferenceWeight) { UniformCondition = false; break; }
            if (UniformCondition) return RowForm::PiecewisePolynomial;
        }
        return RowForm::General;
    }

    [[nodiscard]] double IntegrateFaceFlux(const ModelPartition& Partition, FaceToken Face, double Floor, bool MagnitudeMode)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Face);
        if (!Profile.Surface.PresentCondition()) return std::nan("");
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
        return IntegrateFaceLoops(Partition, Face, [&](Vector2 Parameter) { return FluxDensity(Surface, Parameter); }, Floor, MagnitudeMode, &Surface,
                                  ResolveFluxRowForm(Surface));
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

SolidVolumeMeasure MeasureShellVolume(const ModelPartition& Partition, SolidShellToken SolidShell)
{
    // ⚠️ The noise floor 10⁻¹⁵ · Σ∫|G 𝑣′| is taken over the whole shell, never one face: a plane through the origin
    //    has flux density P·n ≡ 0, so its own magnitude is round-off and a per-face floor never stops the refinement.
    //    A procedural carrier's face is refined only down to its own derivative noise (× its magnitude): below that the
    //    panel gaps are round-off and the refinement would run to the depth cap.
    const std::vector<FaceToken>& Faces = Partition.Shells.Resolve(SolidShell).Faces;
    std::vector<double> FaceMagnitudes(Faces.size(), 0.0);                                   // [len³] - Per face ∫|G 𝑣′| dτ
    double Magnitude = 0.0;                                                                  // [len³] - Σ∫|G 𝑣′| dτ
    for (size_t Index = 0; Index < Faces.size(); ++Index) Magnitude += FaceMagnitudes[Index] = IntegrateFaceFlux(Partition, Faces[Index], 0.0, true);
    SolidVolumeMeasure Measure;
    for (size_t Index = 0; Index < Faces.size(); ++Index)
    {
        const FaceToken Face  = Faces[Index];
        const double Noise = MeasureFaceNoise(Partition, Face);
        const double Flux  = IntegrateFaceFlux(Partition, Face, std::max(1.0e-15 * Magnitude, Noise * FaceMagnitudes[Index]), false);  // [len³] - Face flux
        Measure.Volume        += Flux;
        Measure.FluxMagnitude += std::fabs(Flux);
    }
    return Measure;
}

double IntegrateShellVolume(const ModelPartition& Partition, SolidShellToken SolidShell)
{
    return MeasureShellVolume(Partition, SolidShell).Volume;
}

static double IntegrateBodyVolumeUntraced(const ModelPartition& Partition, BodyToken Body)
{
    double Total = 0.0;                                                                      // [len³] - Body volume
    for (RegionToken Region : Partition.Bodies.Resolve(Body).Regions)
    {
        if (Partition.Regions.Resolve(Region).Material != RegionMaterial::Solid) continue;
        for (SolidShellToken Shell : Partition.Regions.Resolve(Region).Shells) Total += IntegrateShellVolume(Partition, Shell);
    }
    return Total;
}

// 📝 Traced entry (Diagnostics/OperationTrace.h): the request on entry, the outcome before the duration line.

double IntegrateBodyVolume(const ModelPartition& Partition, BodyToken Body)
{
    FRONTIER_TRACE_SCOPE("Volume", "IntegrateBodyVolume · body " + std::to_string(Body.Slot));
    const double Volume = IntegrateBodyVolumeUntraced(Partition, Body);
    FRONTIER_TRACE("Volume", "V = " + TraceNumber(Volume));
    return Volume;
}


double IntegrateShellFlux(const ModelPartition& Partition, SolidShellToken SolidShell, const std::function<Vector3(Vector3)>& Field)
{
    // 📝 MeasureShellVolume with a general field: the floor is again taken over the whole shell (a field may vanish on
    //    a whole face, as 𝑥² does on 𝑥 = 0).
    // ⚠️ The floor's scale integrates |F·N|, never F·N: a moment field cancels over a full turn (∫ cos³ = 0 on a
    //    cylinder), so its signed inner integral along the seam is round-off, a floor scaled by it is round-off too,
    //    and every panel recursed to the depth cap (2²⁴ panels per fin).
    const std::vector<FaceToken>& Faces = Partition.Shells.Resolve(SolidShell).Faces;
    const auto Density = [&](const SurfaceGeometry& Surface)
    {
        return [&Surface, &Field](Vector2 Parameter)
        {
            const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
            return Dot(Field(Sample.Position), Cross(Sample.DerivativeU, Sample.DerivativeV));
        };
    };
    const auto Absolute = [&](const SurfaceGeometry& Surface)
    {
        return [&Surface, &Field](Vector2 Parameter)
        {
            const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
            return std::fabs(Dot(Field(Sample.Position), Cross(Sample.DerivativeU, Sample.DerivativeV)));
        };
    };
    std::vector<double> FaceMagnitudes(Faces.size(), 0.0);                                   // [-] - Per face ∫|G 𝑣′| dτ
    double Magnitude = 0.0;                                                                  // [-] - Σ∫|G 𝑣′| dτ
    for (size_t Index = 0; Index < Faces.size(); ++Index)
    {
        const FaceProfile& Profile = Partition.Faces.Resolve(Faces[Index]);
        if (!Profile.Surface.PresentCondition()) return std::nan("");
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
        Magnitude += FaceMagnitudes[Index] = IntegrateFaceLoops(Partition, Faces[Index], Absolute(Surface), 0.0, true, &Surface);
    }
    double Total = 0.0;
    for (size_t Index = 0; Index < Faces.size(); ++Index)
    {
        const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Partition.Faces.Resolve(Faces[Index]).Surface);
        const double Floor = std::max(1.0e-15 * Magnitude, MeasureFaceNoise(Partition, Faces[Index]) * FaceMagnitudes[Index]);
        Total += IntegrateFaceLoops(Partition, Faces[Index], Density(Surface), Floor, false, &Surface);
    }
    return Total;
}

double IntegrateFaceSurface(const ModelPartition& Partition, FaceToken Face, const std::function<double(Vector3)>& Weight)
{
    // ⚠️ The sense comes from the parametric area (density 1), never from the weighted total: a weight that changes
    //    sign over the face (a centred coordinate) may integrate to either sign on a correctly oriented face.
    const FaceProfile& Profile = Partition.Faces.Resolve(Face);
    if (!Profile.Surface.PresentCondition()) return std::nan("");
    const SurfaceGeometry& Surface = Partition.Surfaces.Resolve(Profile.Surface);
    const auto Density = [&](Vector2 Parameter)
    {
        const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
        return Weight(Sample.Position) * Length(Cross(Sample.DerivativeU, Sample.DerivativeV));
    };
    // ⚠️ The floor's scale integrates |w| (see IntegrateShellFlux): a centred weight cancels over a full turn.
    const auto Absolute = [&](Vector2 Parameter)
    {
        const SurfaceSample Sample = EvaluateSurface(Surface, Parameter);
        return std::fabs(Weight(Sample.Position)) * Length(Cross(Sample.DerivativeU, Sample.DerivativeV));
    };
    const double Sense     = IntegrateParametricArea(Partition, Face) < 0.0 ? -1.0 : 1.0;    // [-] - Loop sense in (𝑢, 𝑣)
    const double Magnitude = IntegrateFaceLoops(Partition, Face, Absolute, 0.0, true, &Surface);
    const double Floor     = std::max(1.0e-15, MeasureFaceNoise(Partition, Face)) * Magnitude;
    return Sense * IntegrateFaceLoops(Partition, Face, Density, Floor, false, &Surface);
}

double MeasureEdgeLength(const ModelPartition& Partition, EdgeToken Edge)
{
    const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
    if (!Profile.Curve.PresentCondition()) return std::nan("");
    const CurveGeometry& Curve = Partition.Curves.Resolve(Profile.Curve);
    const auto Speed = [&](double τ) { return Length(EvaluateCurve(Curve, τ).Derivative); };
    std::vector<double> Stops = { Profile.IntervalLow };
    for (double Knot : CollectCurveBreakpoints(Curve, Profile.IntervalLow, Profile.IntervalHigh)) Stops.push_back(Knot);
    Stops.push_back(Profile.IntervalHigh);
    double Total = 0.0;
    for (size_t Index = 0; Index + 1 < Stops.size(); ++Index)
    {
        const double Magnitude = EstimateMagnitude(Stops[Index], Stops[Index + 1], PanelSpan, Speed);
        Total += IntegrateRefined(Stops[Index], Stops[Index + 1], PanelSpan, Speed, 1.0e-15 * Magnitude);
    }
    return std::fabs(Total);
}

double IntegrateParametricArea(const ModelPartition& Partition, FaceToken Face)
{
    // Density 1 never cancels, so the face's own magnitude (× its carrier's derivative noise) is a valid floor here.
    const auto Unit = [](Vector2) { return 1.0; };
    const double Noise = std::max(1.0e-15, MeasureFaceNoise(Partition, Face));
    const double Magnitude = IntegrateFaceLoops(Partition, Face, Unit, 0.0, true, nullptr, RowForm::Constant);
    return IntegrateFaceLoops(Partition, Face, Unit, Noise * Magnitude, false, nullptr, RowForm::Constant);  // Σ ∮ 𝑢 d𝑣
}

} // namespace Frontier::GeometrySpace::Parametric
