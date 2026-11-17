/*====================================================================================================================================
                                                      BLENDCATALOGUE.CPP
====================================================================================================================================*/
// 🧩 Blend specimens: bodies, edge selections, closed-form volume changes and membership oracles (validation + viewer)

#include "Engine/GeometrySpace/Parametric/Blend/BlendCatalogue.h"

#include "Engine/GeometrySpace/Parametric/Blend/EdgeBlend.h"
#include "Engine/GeometrySpace/Parametric/Boolean/GeneralFuse.h"
#include "Engine/GeometrySpace/Parametric/Construction/PrimitiveConstruction.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace Frontier::GeometrySpace::Parametric
{

namespace
{
    constexpr double π = std::numbers::pi;                                                   // [rad] - π

    // 📝 Centroid factor of the fillet spandrel (square minus quarter disc): centroid k·r from the corner, k = (10 − 3π)/(12 − 3π).
    constexpr double SpandrelFactor = (10.0 - 3.0 * π) / (12.0 - 3.0 * π);                   // [-]

    [[nodiscard]] OrthonormalFrame UprightFrame(Vector3 Origin) { return { Origin, { 1.0, 0.0, 0.0 }, { 0.0, 1.0, 0.0 }, { 0.0, 0.0, 1.0 } }; }

    // Removed volume of a convex 90° fillet of radius r along a straight edge of length L: (1 − π/4) r² L.
    [[nodiscard]] double StraightFilletLoss(double Radius, double Span) { return (1.0 - π / 4.0) * Radius * Radius * Span; }

    // 📝 Top perimeter P of a box filleted with mitres: four prisms minus four corner overlaps ∫(r − √(r² − t²))² dt = r³(5/3 − π/2).
    [[nodiscard]] double MitredFilletLoss(double Radius, double Perimeter) { return StraightFilletLoss(Radius, Perimeter) - 4.0 * Radius * Radius * Radius * (5.0 / 3.0 - π / 2.0); }

    // Top perimeter chamfered with mitres: prisms d²/2 · P minus four corner overlaps d³/3.
    [[nodiscard]] double MitredChamferLoss(double Setback, double Perimeter) { return 0.5 * Setback * Setback * Perimeter - 4.0 * Setback * Setback * Setback / 3.0; }

    // 📝 Box a × b × c with all twelve edges filleted (sphere corners): V loses (4 − π) r² Σ(edge − 2r) + (8 − 4π/3) r³.
    [[nodiscard]] double RoundedBoxLoss(Vector3 Extent, double Radius)
    {
        return (4.0 - π) * Radius * Radius * ((Extent.X - 2.0 * Radius) + (Extent.Y - 2.0 * Radius) + (Extent.Z - 2.0 * Radius)) + (8.0 - 4.0 / 3.0 * π) * Radius * Radius * Radius;
    }

    // All twelve edges chamfered (triple mitres): prisms d²/2 · 4(a + b + c), minus 24 pair overlaps d³/3, plus 8 triple overlaps d³/4.
    [[nodiscard]] double ChamferedBoxLoss(Vector3 Extent, double Setback)
    {
        return 0.5 * Setback * Setback * 4.0 * (Extent.X + Extent.Y + Extent.Z) - 8.0 * Setback * Setback * Setback + 2.0 * Setback * Setback * Setback;
    }

    // Signed clearance of a box with all edges filleted: it is the inner box ⊕ ball(r).
    [[nodiscard]] std::function<double(Vector3)> RoundedBoxClearance(Vector3 Corner, Vector3 Extent, double Radius)
    {
        return [=](Vector3 Point)
        {
            const auto Axis = [&](double Value, double Low, double High) { return std::max({ Low + Radius - Value, 0.0, Value - (High - Radius) }); };
            const double Gap = std::sqrt(std::pow(Axis(Point.X, Corner.X, Corner.X + Extent.X), 2) + std::pow(Axis(Point.Y, Corner.Y, Corner.Y + Extent.Y), 2)
                                         + std::pow(Axis(Point.Z, Corner.Z, Corner.Z + Extent.Z), 2));
            if (Gap > 0.0) return Radius - Gap;
            return std::min({ Point.X - Corner.X, Corner.X + Extent.X - Point.X, Point.Y - Corner.Y, Corner.Y + Extent.Y - Point.Y, Point.Z - Corner.Z, Corner.Z + Extent.Z - Point.Z });
        };
    }

    // 📝 Box with all edges chamfered by d: the box cut by twelve half-spaces s₁ + s₂ ≥ d (s = inward face distances).
    [[nodiscard]] std::function<double(Vector3)> ChamferedBoxClearance(Vector3 Corner, Vector3 Extent, double Setback)
    {
        return [=](Vector3 Point)
        {
            const double Gaps[6] = { Point.X - Corner.X, Corner.X + Extent.X - Point.X, Point.Y - Corner.Y, Corner.Y + Extent.Y - Point.Y, Point.Z - Corner.Z, Corner.Z + Extent.Z - Point.Z };
            double Clearance = *std::min_element(Gaps, Gaps + 6);
            for (int First = 0; First < 6; ++First)
            {
                for (int Second = First + 1; Second < 6; ++Second)
                {
                    if (First / 2 == Second / 2) continue;                                    // Opposite faces share no edge
                    Clearance = std::min(Clearance, (Gaps[First] + Gaps[Second] - Setback) / std::sqrt(2.0));
                }
            }
            return Clearance;
        };
    }

    // Cylinder (axis Z through the origin, radius R, height h) with its top rim filleted by r.
    [[nodiscard]] std::function<double(Vector3)> RoundedRimClearance(double CylinderRadius, double Height, double Radius)
    {
        return [=](Vector3 Point)
        {
            const double Reach = std::hypot(Point.X, Point.Y);                                // [len] - ρ
            if (Reach > CylinderRadius - Radius && Point.Z > Height - Radius)
            {
                return Radius - std::hypot(Reach - (CylinderRadius - Radius), Point.Z - (Height - Radius));
            }
            return std::min({ CylinderRadius - Reach, Point.Z, Height - Point.Z });
        };
    }

    // ---- Cliff overflow closed forms ---------------------------------------------------------------------------------------
    // 📝 A convex blend wider than the face it rests on (depth t < blend reach) runs off the far edge and is trimmed by the
    //    cliff face. In the cross-section (p = depth from the consumed face, q = depth from the kept face) the removed
    //    region is the corner {p < r, q < t, outside the ball} for a fillet, {p + q < c, q < t} for a chamfer. With
    //    w = r − q: b(w) = r − √(r² − w²) is its width, A = ∫ b = r·t − ΔF, F(w) = ½(w√(r² − w²) + r² asin(w/r)).

    [[nodiscard]] double CliffCircleIntegral(double Radius, double Depth)
    {
        const auto Primitive = [&](double Level) { return 0.5 * (Level * std::sqrt(std::max(Radius * Radius - Level * Level, 0.0)) + Radius * Radius * std::asin(std::clamp(Level / Radius, -1.0, 1.0))); };
        return Primitive(Radius) - Primitive(Radius - Depth);                               // ∫_{r−t}^{r} √(r² − w²) dw
    }

    // Removed cross-section area: fillet r·t − ΔF, chamfer ½(c² − (c − t)²).
    [[nodiscard]] double CliffArea(double Size, double Depth, BlendProfile Profile)
    {
        if (Profile == BlendProfile::Chamfer) return 0.5 * (Size * Size - (Size - Depth) * (Size - Depth));
        return Size * Depth - CliffCircleIntegral(Size, Depth);
    }

    // ∫ b² over the removed cross-section (second moment along p): fillet 2r²t − 2rΔF − (r³ − (r − t)³)/3, chamfer (c³ − (c − t)³)/3.
    [[nodiscard]] double CliffSquareIntegral(double Size, double Depth, BlendProfile Profile)
    {
        const double Lower = Size - Depth;                                                   // [len] - r − t
        if (Profile == BlendProfile::Chamfer) return (Size * Size * Size - Lower * Lower * Lower) / 3.0;
        return 2.0 * Size * Size * Depth - 2.0 * Size * CliffCircleIntegral(Size, Depth) - (Size * Size * Size - Lower * Lower * Lower) / 3.0;
    }

    // Rim of revolution (outer radius R): Pappus over the removed region, ΔV = −2π ∫∫ (R − p) dA = −2π (R·A − ½∫b²).
    [[nodiscard]] double CliffRimLoss(double CylinderRadius, double Size, double Depth, BlendProfile Profile)
    {
        return 2.0 * π * (CylinderRadius * CliffArea(Size, Depth, Profile) - 0.5 * CliffSquareIntegral(Size, Depth, Profile));
    }

    // Corner term of the clearance in cross-section depths (p, q): negative inside the removed region.
    [[nodiscard]] double CliffCornerClearance(double Across, double Down, double Size, BlendProfile Profile)
    {
        if (Profile == BlendProfile::Chamfer) return (Across + Down - Size) / std::sqrt(2.0);
        if (Across < Size && Down < Size) return Size - std::hypot(Across - Size, Down - Size);
        return 1.0e300;
    }

    // Box [0, Extent] with its top front edge (y = 0, z = Extent.Z) blended past the front face.
    [[nodiscard]] std::function<double(Vector3)> CliffBoxClearance(Vector3 Extent, double Size, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const double Box = std::min({ Point.X, Extent.X - Point.X, Point.Y, Extent.Y - Point.Y, Point.Z, Extent.Z - Point.Z });
            return std::min(Box, CliffCornerClearance(Point.Y, Extent.Z - Point.Z, Size, Profile));
        };
    }

    // Disc (axis Z, radius R, height t) with its top rim blended past the side face.
    [[nodiscard]] std::function<double(Vector3)> CliffDiscClearance(double CylinderRadius, double Height, double Size, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const double Reach = std::hypot(Point.X, Point.Y);                                // [len] - ρ
            const double Disc  = std::min({ CylinderRadius - Reach, Point.Z, Height - Point.Z });
            return std::min(Disc, CliffCornerClearance(CylinderRadius - Reach, Height - Point.Z, Size, Profile));
        };
    }

    // Notch: the removed corner is the cliff's {p < r, q < t, outside the ball}, but the material past the far edge
    // (q ≥ t) stays, so the kept body is Body \ (corner ∩ {q < t}). Inside = union of two boxes (step) or plate ∪ boss.
    [[nodiscard]] double NotchCornerClearance(double Across, double Down, double Depth, double Size, BlendProfile Profile)
    {
        return std::max(CliffCornerClearance(Across, Down, Size, Profile), Down - Depth);
    }

    [[nodiscard]] double BoxClearance(Vector3 Point, Vector3 Low, Vector3 High)
    {
        return std::min({ Point.X - Low.X, High.X - Point.X, Point.Y - Low.Y, High.Y - Point.Y, Point.Z - Low.Z, High.Z - Point.Z });
    }

    // Step prism: ledge [0, 1] × [0, 1.5] × [0, 1] and block [1, 2] × [0, 1.5] × [0, 1.2]; riser x = 1, z 1 → 1.2.
    [[nodiscard]] std::function<double(Vector3)> NotchStepClearance(double Size, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const double Body = std::max(BoxClearance(Point, { 0.0, 0.0, 0.0 }, { 1.0, 1.5, 1.0 }), BoxClearance(Point, { 1.0, 0.0, 0.0 }, { 2.0, 1.5, 1.2 }));
            return std::min(Body, NotchCornerClearance(Point.X - 1.0, 1.2 - Point.Z, 0.2, Size, Profile));
        };
    }

    // Plate [−1.5, 1.5]² × [0, 0.5] ∪ boss (axis Z, radius R, z 0.5 → 0.5 + t).
    [[nodiscard]] std::function<double(Vector3)> NotchBossClearance(double CylinderRadius, double Height, double Size, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const double Reach = std::hypot(Point.X, Point.Y);                                // [len] - ρ
            const double Boss  = std::min({ CylinderRadius - Reach, Point.Z - 0.25, 0.5 + Height - Point.Z });
            const double Body  = std::max(BoxClearance(Point, { -1.5, -1.5, 0.0 }, { 1.5, 1.5, 0.5 }), Boss);
            return std::min(Body, NotchCornerClearance(CylinderRadius - Reach, 0.5 + Height - Point.Z, Height, Size, Profile));
        };
    }

    // ---- Smooth overflow closed form ----------------------------------------------------------------------------------------
    // 📝 Box 4 × 2 × H whose front bottom edge is already rounded r₀ (corner centre (r₀, r₀) in the (y, z) section), so the
    //    front face is flat only for H − r₀. A top front blend of ball radius r wider than that rolls onto the r₀ fillet:
    //    the ball touches z = H (centre height H − r) and the r₀ circle from inside, |c − (r₀, r₀)| = r₀ − r. The removed
    //    section is bounded by the front face, the r₀ arc to the contact K, the ball arc (fillet) or the chord (chamfer) to
    //    the top contact T, and the top line; its area is ½∮(y dz − z dy), exact for segments and circular arcs.
    struct SmoothSection
    {
        double  Height = 1.0, Rounded = 0.85, Radius = 0.3;                                  // [len] - H, r₀, r
        Vector2 Centre = {}, Contact = {}, Top = {};                                         // [len] - c, K, T in (y, z)
    };

    [[nodiscard]] SmoothSection MeasureSmoothSection(double Height, double Rounded, double Radius)
    {
        SmoothSection Section{ Height, Rounded, Radius };
        const double Drop = Height - Radius - Rounded;                                       // [len] - c.z − r₀
        Section.Centre  = { Rounded - std::sqrt((Rounded - Radius) * (Rounded - Radius) - Drop * Drop), Height - Radius };
        const Vector2 Out = (Section.Centre - Vector2{ Rounded, Rounded }) * (1.0 / (Rounded - Radius));
        Section.Contact = Vector2{ Rounded, Rounded } + Out * Rounded;
        Section.Top     = { Section.Centre.X, Height };
        return Section;
    }

    [[nodiscard]] double SmoothArea(const SmoothSection& Section, BlendProfile Profile)
    {
        const auto Segment = [](Vector2 P, Vector2 Q) { return 0.5 * (P.X * Q.Y - P.Y * Q.X); };
        const auto Arc     = [](Vector2 Centre, double Radius, double From, double To)
        {
            return 0.5 * (Radius * Radius * (To - From) + Centre.X * Radius * (std::sin(To) - std::sin(From)) - Centre.Y * Radius * (std::cos(To) - std::cos(From)));
        };
        const Vector2 Corner{ Section.Rounded, Section.Rounded };
        const double  AngleK = std::atan2(Section.Contact.Y - Corner.Y, Section.Contact.X - Corner.X) + 2.0 * π;   // [rad] - K about r₀ centre, in (π, 3π/2)
        double Area = Segment({ 0.0, Section.Height }, { 0.0, Section.Rounded }) + Arc(Corner, Section.Rounded, π, AngleK);
        if (Profile == BlendProfile::Chamfer) Area += Segment(Section.Contact, Section.Top);
        else Area += Arc(Section.Centre, Section.Radius, AngleK, 0.5 * π);                  // clockwise round the corner side
        return Area + Segment(Section.Top, { 0.0, Section.Height });
    }

    [[nodiscard]] std::function<double(Vector3)> SmoothClearance(const SmoothSection& Section, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const Vector2 P{ Point.Y, Point.Z };
            const double  Box     = std::min({ Point.X, 4.0 - Point.X, Point.Y, 2.0 - Point.Y, Point.Z, Section.Height - Point.Z });
            const double  Rounded = P.X < Section.Rounded && P.Y < Section.Rounded ? Section.Rounded - Length(P - Vector2{ Section.Rounded, Section.Rounded }) : 1.0e300;
            double Kept = 0.0;                                                               // [len] - > 0 outside the removed region
            if (Profile == BlendProfile::Chamfer)
            {
                const Vector2 Along = (Section.Top - Section.Contact) * (1.0 / Length(Section.Top - Section.Contact));
                Kept = Dot(P - Section.Contact, Vector2{ Along.Y, -Along.X });                // away from the corner
            }
            else
            {
                const Vector2 ToTop = { 0.0, 1.0 }, ToContact = (Section.Contact - Section.Centre) * (1.0 / Section.Radius);
                const double  Wedge = std::max(Dot(P - Section.Centre, Vector2{ ToTop.Y, -ToTop.X }), Dot(P - Section.Centre, Vector2{ -ToContact.Y, ToContact.X }));
                Kept = std::max(Section.Radius - Length(P - Section.Centre), Wedge);
            }
            return std::min({ Box, Rounded, Kept });
        };
    }

    // ---- Variable blends ---------------------------------------------------------------------------------------------
    // 📝 Front top edge of the 4 × 3 × 2 box (y = 0, z = 2, x ∈ [0, L]), size linear from Low at x = 0 to High at x = L. The
    //    ball centres C(x) = (x, r, 2 − r) form a line; the fillet is the cone of the balls with apex P = (x*, 0, 2),
    //    x* = −Low·L/(High − Low), on the edge line. By the divergence theorem with the origin at P the cone (generators
    //    through P) and both box faces (they contain P) contribute nothing, so ΔV = −(x*·A₀ + (L − x*)·A_L)/3 with A the
    //    corner area of each end cap. A is ½|∫ x̂·((Q − E) × Q′) dψ| along the exact cone ∩ end-plane arc, Q(ψ) on the
    //    generator of angle ψ — an analytic integrand, integrated by 64-point Gauss–Legendre to rounding.
    struct VariableCone
    {
        Vector3 Apex = {}, Axis = {}, First = {}, Second = {};                               // [len], [-] - Apex, axis, ⟂ basis
        double  Sine = 0.0, Cosine = 1.0;                                                    // [-]        - sin β, cos β
    };

    [[nodiscard]] VariableCone MeasureVariableCone(double Length, double Low, double High)
    {
        VariableCone Cone;
        const double Rise = High - Low;                                                      // [len] - Radius change
        Cone.Apex   = { -Low * Length / Rise, 0.0, 2.0 };
        Cone.Axis   = Normalize(Vector3{ Length, Rise, -Rise }) * (Rise > 0.0 ? 1.0 : -1.0); // towards growing radius
        Cone.Sine   = std::fabs(Rise) / std::sqrt(Length * Length + 2.0 * Rise * Rise);
        Cone.Cosine = std::sqrt(1.0 - Cone.Sine * Cone.Sine);
        Cone.First  = Normalize(Cross(Cone.Axis, Vector3{ 1.0, 0.0, 0.0 }));
        Cone.Second = Cross(Cone.Axis, Cone.First);
        return Cone;
    }

    template <typename Integrand>
    [[nodiscard]] double IntegrateGaussLegendre(double Low, double High, const Integrand& Function)
    {
        constexpr int Order = 64;
        static const auto Rule = []
        {
            std::array<std::pair<double, double>, Order> Nodes{};                            // (abscissa, weight)
            for (int Index = 0; Index < Order; ++Index)
            {
                double X = std::cos(std::numbers::pi * (Index + 0.75) / (Order + 0.5));      // [-] - Tricomi start
                double Slope = 0.0;
                for (int Step = 0; Step < 100; ++Step)
                {
                    double Previous = 1.0, Current = X;                                      // P₀, P₁
                    for (int Degree = 2; Degree <= Order; ++Degree)
                    {
                        const double Next = ((2.0 * Degree - 1.0) * X * Current - (Degree - 1.0) * Previous) / Degree;
                        Previous = Current; Current = Next;
                    }
                    Slope = Order * (X * Current - Previous) / (X * X - 1.0);
                    const double Shift = Current / Slope;
                    X -= Shift;
                    if (std::fabs(Shift) < 1.0e-16) break;
                }
                Nodes[Index] = { X, 2.0 / ((1.0 - X * X) * Slope * Slope) };
            }
            return Nodes;
        }();
        double Total = 0.0;
        for (const auto& [Abscissa, Weight] : Rule) Total += Weight * Function(0.5 * (Low + High) + 0.5 * (High - Low) * Abscissa);
        return 0.5 * (High - Low) * Total;
    }

    // Corner area of the end cap at x = Plane, radius Radius there.
    [[nodiscard]] double VariableCapArea(const VariableCone& Cone, double Plane, double Radius)
    {
        const Vector3 Corner = { Plane, 0.0, 2.0 };
        const auto Angle = [&](Vector3 Point) { const Vector3 Offset = Point - Cone.Apex; return std::atan2(Dot(Offset, Cone.Second), Dot(Offset, Cone.First)); };
        double Start = Angle({ Plane, 0.0, 2.0 - Radius }), End = Angle({ Plane, Radius, 2.0 });   // contact generators
        const double Facing = Angle(Cone.Apex + Vector3{ 0.0, -1.0, 1.0 });                  // towards the corner
        // Unwrap so that [Start, End] runs through the facing direction.
        const auto Wrap = [](double Value, double Reference) { while (Value - Reference > std::numbers::pi) Value -= 2.0 * std::numbers::pi; while (Value - Reference < -std::numbers::pi) Value += 2.0 * std::numbers::pi; return Value; };
        Start = Wrap(Start, Facing);
        End   = Wrap(End, Facing);
        const double Swept = IntegrateGaussLegendre(Start, End, [&](double ψ)
        {
            const Vector3 Turn  = Cone.First * std::cos(ψ) + Cone.Second * std::sin(ψ);
            const Vector3 Spin  = Cone.Second * std::cos(ψ) - Cone.First * std::sin(ψ);
            const Vector3 Ray   = Cone.Axis * Cone.Cosine + Turn * Cone.Sine;                 // g(ψ)
            const Vector3 Bend  = Spin * Cone.Sine;                                          // g′(ψ)
            const double  Reach = (Plane - Cone.Apex.X) / Ray.X;                             // t(ψ)
            const double  Pace  = -Reach * Bend.X / Ray.X;                                   // t′(ψ)
            const Vector3 Point = Cone.Apex + Ray * Reach;
            const Vector3 Speed = Ray * Pace + Bend * Reach;
            return Cross(Point - Corner, Speed).X;
        });
        return 0.5 * std::fabs(Swept);
    }

    [[nodiscard]] double VariableFilletLoss(double Length, double Low, double High)
    {
        const VariableCone Cone = MeasureVariableCone(Length, Low, High);
        return (Cone.Apex.X * VariableCapArea(Cone, 0.0, Low) + (Length - Cone.Apex.X) * VariableCapArea(Cone, Length, High)) / 3.0;
    }

    // Box 4 × 3 × 2 minus the removed corner: cone (fillet) or plane y + (2 − z) = d(x) (chamfer).
    [[nodiscard]] std::function<double(Vector3)> VariableClearance(double Length, double Low, double High, BlendProfile Profile)
    {
        return [=](Vector3 Point)
        {
            const double Box  = std::min({ Point.X, Length - Point.X, Point.Y, 3.0 - Point.Y, Point.Z, 2.0 - Point.Z });
            const double Size = Low + (High - Low) * Point.X / Length;                       // [len] - d(x) or r(x)
            if (Profile == BlendProfile::Chamfer)
            {
                const double Slope = (High - Low) / Length;
                return std::min(Box, (Point.Y + 2.0 - Point.Z - Size) / std::sqrt(2.0 + Slope * Slope));
            }
            // Envelope distance ρ·cos β − r(τ) from the spine line; the wedge between the planes through the spine and each
            // contact line bounds the removed region.
            const Vector3 Start = { 0.0, Low, 2.0 - Low }, Finish = { Length, High, 2.0 - High };
            const Vector3 Along = Normalize(Finish - Start);
            const double  Rate  = (High - Low) / Distance(Start, Finish);
            const double  τ     = Dot(Point - Start, Along);
            const double  Reach = Distance(Point, Start + Along * τ);
            const double  Envelope = Reach * std::sqrt(1.0 - Rate * Rate) - (Low + Rate * τ);   // > 0 outside the cone
            const Vector3 Apex = { -Low * Length / (High - Low), 0.0, 2.0 };
            const Vector3 Middle = { 0.5 * Length, 0.0, 2.0 };
            double Wedge = -1.0e300;
            for (const Vector3 Contact : { Vector3{ Length, 0.0, 2.0 - High }, Vector3{ Length, High, 2.0 } })
            {
                Vector3 Normal = Normalize(Cross(Contact - Apex, Along));
                if (Dot(Normal, Middle - Apex) > 0.0) Normal = -Normal;                       // positive away from the corner
                Wedge = std::max(Wedge, Dot(Point - Apex, Normal));
            }
            return std::min(Box, std::max(-Envelope, Wedge));
        };
    }

    // 📝 Variable blend on the top half-rim of a D (cylinder R, axis ẑ, top z = H, cut by the plane x = 0; ψ ∈ [−π/2, π/2]
    //    the polar angle, x = ρ·cos ψ). The size runs linearly in ψ (∝ arc length) from Low at ψ = −π/2 to High at +π/2;
    //    the volume is symmetric under ψ → −ψ, so the edge direction does not matter. Origin O = (0, 0, H): the top face and
    //    both end faces (x = 0) contain O, so only the cylinder strip (X·n = R) and the blend face carry flux:
    //        V = ⅓·(R·A_cyl + ∬ (P − O)·n dA),   A_cyl = R·∫ r dψ   (removed region, n out of it, towards the balls).
    //    Chamfer: every ruling lies in its meridian plane (centre in the edge's normal plane), so the section is the
    //    triangle of legs d and Pappus per unit ψ gives V = ∫ ½d²·(R − d/3) dψ, a polynomial in ψ.
    //    Fillet: the canal of the balls C(ψ) = (R − r)·ρ̂ − r·ẑ (relative to O). Its characteristic circles lean out of the
    //    meridian planes (n·T = −ρ′), so the face meets x = 0 along a curve of the chart P(ψ, w) = C + r·n(ψ, w·α_B(ψ)),
    //    w ∈ [0, 1] from the top foot (n = ẑ) to the cylinder foot (n = ρ̂). For each w the ψ-limits are the roots of
    //    x(P) = 0 near ±π/2; the integrand is analytic in both, so 64 × 64 Gauss–Legendre reaches rounding; P_ψ by a
    //    sixth-order central difference (h = 5·10⁻³: truncation ~10⁻¹⁶, rounding ~10⁻¹⁴). r constant gives Pappus.
    [[nodiscard]] double MeasureVariableArcChamferVolume(double Radius, double Low, double High)
    {
        // d(ψ) = Low + k·(ψ + π/2), k = (High − Low)/π; ∫ d² dψ = (High³ − Low³)/(3k), ∫ d³ dψ = (High⁴ − Low⁴)/(4k).
        const double Rate = (High - Low) / π;
        const double Square = std::fabs(Rate) < 1.0e-15 ? π * Low * Low : (High * High * High - Low * Low * Low) / (3.0 * Rate);
        const double Cube   = std::fabs(Rate) < 1.0e-15 ? π * Low * Low * Low : (High * High * High * High - Low * Low * Low * Low) / (4.0 * Rate);
        return -(0.5 * Radius * Square - Cube / 6.0);
    }

    [[nodiscard]] double MeasureVariableArcFilletVolume(double Radius, double Low, double High)
    {
        const Vector3 Up = { 0.0, 0.0, 1.0 };
        const double  Rate = (High - Low) / π;                                               // [len] - r′ = dr/dψ
        const auto Ball = [&](double ψ) { return Low + Rate * (ψ + 0.5 * π); };
        const auto Chart = [&](double ψ, double w, Vector3* Across)
        {
            const double  r = Ball(ψ);
            const Vector3 Radial = { std::cos(ψ), std::sin(ψ), 0.0 }, Around = { -std::sin(ψ), std::cos(ψ), 0.0 };
            const Vector3 Centre = Radial * (Radius - r) - Up * r;
            const Vector3 Speed  = Radial * -Rate + Around * (Radius - r) - Up * Rate;       // C′
            const Vector3 Tangent = Normalize(Speed);
            const double  Slope = Rate / Length(Speed), Lean = std::sqrt(1.0 - Slope * Slope);   // ρ′, √(1 − ρ′²)
            const Vector3 First  = Normalize(Up - Tangent * Dot(Up, Tangent));
            const Vector3 Planar = Radial - Tangent * Dot(Radial, Tangent);
            const Vector3 Second = Normalize(Planar - First * Dot(Planar, First));
            const double  Sweep  = std::atan2(Dot(Planar, Second), Dot(Planar, First));      // α_B
            const double  α = w * Sweep;
            if (Across) *Across = (Second * std::cos(α) - First * std::sin(α)) * (r * Lean * Sweep);   // P_w
            return Centre + (Tangent * -Slope + (First * std::cos(α) + Second * std::sin(α)) * Lean) * r;
        };
        const auto Limit = [&](double w, double Seed)
        {
            double Before = Seed, Lower = Chart(Before, w, nullptr).X, ψ = Seed + 1.0e-3;
            for (int Iteration = 0; Iteration < 60; ++Iteration)
            {
                const double Current = Chart(ψ, w, nullptr).X;
                if (Current == Lower) break;
                const double Next = ψ - Current * (ψ - Before) / (Current - Lower);
                Before = ψ; Lower = Current; ψ = Next;
                if (std::fabs(ψ - Before) < 1.0e-16) break;
            }
            return ψ;
        };
        // Sign making P_ψ × P_w point at the ball centre (out of the removed region), probed mid-face.
        Vector3 Probe;
        const Vector3 Middle = Chart(0.0, 0.5, &Probe);
        const Vector3 Along  = (Chart(1.0e-6, 0.5, nullptr) - Chart(-1.0e-6, 0.5, nullptr)) * 0.5e6;
        const double  Orientation = Dot(Cross(Along, Probe), Vector3{ Radius - Ball(0.0), 0.0, -Ball(0.0) } - Middle) > 0.0 ? 1.0 : -1.0;
        const double Flux = IntegrateGaussLegendre(0.0, 1.0, [&](double w)
        {
            return IntegrateGaussLegendre(Limit(w, -0.5 * π), Limit(w, 0.5 * π), [&](double ψ)
            {
                constexpr double h = 5.0e-3;
                Vector3 Across;
                const Vector3 Point = Chart(ψ, w, &Across);
                const auto Difference = [&](int k) { return Chart(ψ + k * h, w, nullptr) - Chart(ψ - k * h, w, nullptr); };
                const Vector3 Along = (Difference(1) * 45.0 - Difference(2) * 9.0 + Difference(3)) * (1.0 / (60.0 * h));
                return Dot(Point, Cross(Along, Across));
            });
        }) * Orientation;
        const double Strip = Radius * Radius * π * 0.5 * (Low + High);                        // [len³] - R·A_cyl
        return -(Strip + Flux) / 3.0;
    }

    // 📝 Chamfer d on the rim where the plane z = 0 meets the cylinder of radius R about a = (sin α, 0, cos α) through the
    //    origin — independent of the kernel's ball-law code: own analytic edge, normals, Newton and exact r′.
    //    Side κ = −1: through-bore (material below z = 0 and outside the cylinder, convex rim, region removed);
    //    κ = +1: boss base (block below z = 0, boss inside the cylinder, concave rim, region added).
    //    Edge E(φ) = R·ρ̂(φ) + R·tan α·cos φ·a, ρ̂ = cos φ·e₁ + sin φ·e₂ (e₁ = (cos α, 0, −sin α), e₂ = ŷ); cos θ = −κ·ẑ·ρ̂;
    //    r = d·tan(θ/2). Centre C: C·ẑ = κ·r, distance from the axis R + r, E′·(C − E) = 0; feet A on the plane, B on the
    //    cylinder; ruled chamfer P = A + v(B − A). The divergence theorem about the origin (on the axis) over the region
    //    between the old faces and the chamfer: the plane strip has X·n = 0, the cylinder strip X·n = −R, so
    //        V = ⅓·(−R·Area_cyl + ∬ X·(P_φ × P_v) dv dφ)   (chamfer flux oriented towards C, i.e. out of the region)
    //    Area_cyl = R·|∮ s dφ| between the edge (∮ s_E dφ = 0) and rail B (s = axial coordinate, φ_B its polar angle).
    //    The flux is quadratic in v (2-point Gauss is exact) and periodic analytic in φ (the trapezoid rule converges
    //    exponentially). Returns ΔV = κ·V.
    [[nodiscard]] double MeasureTiltedRimChamferVolume(double Radius, double Tilt, double Setback, double Side)
    {
        const Vector3 Axis = { std::sin(Tilt), 0.0, std::cos(Tilt) };
        const Vector3 First = { std::cos(Tilt), 0.0, -std::sin(Tilt) }, Second = { 0.0, 1.0, 0.0 }, Up = { 0.0, 0.0, 1.0 };
        struct Ruling { Vector3 Near, Far, NearRate, FarRate, Centre; double AxialB, TurnB; };
        const auto RulingAt = [&](double φ)
        {
            const Vector3 Radial = First * std::cos(φ) + Second * std::sin(φ), RadialRate = Second * std::cos(φ) - First * std::sin(φ);
            const double  Lean   = Radius * std::tan(Tilt);                                     // [len] - R·tan α
            const Vector3 Edge   = Radial * Radius + Axis * (Lean * std::cos(φ));
            const Vector3 Speed  = RadialRate * Radius - Axis * (Lean * std::sin(φ));
            const Vector3 Bend   = Radial * -Radius - Axis * (Lean * std::cos(φ));
            const double  Cosine = -Side * Dot(Up, Radial), CosineRate = -Side * Dot(Up, RadialRate);   // [-] - cos θ and its φ-derivative
            const double  Ball   = Setback * std::sqrt((1.0 - Cosine) / (1.0 + Cosine));       // [len] - r
            const double  BallRate = -Setback * CosineRate / ((1.0 + Cosine) * std::sqrt(1.0 - Cosine * Cosine));
            Vector3 Centre = Edge;
            Vector3 Unit   = Radial;
            for (int Iteration = 0; Iteration < 60; ++Iteration)
            {
                const Vector3 Across = Centre - Axis * Dot(Centre, Axis);                       // [len] - w
                Unit = Normalize(Across);
                const double  Residual[3] = { Dot(Centre, Up) - Side * Ball, Length(Across) - (Radius + Ball), Dot(Speed, Centre - Edge) };
                const double  Determinant = Dot(Up, Cross(Unit, Speed));
                const Vector3 Step = (Cross(Unit, Speed) * Residual[0] + Cross(Speed, Up) * Residual[1] + Cross(Up, Unit) * Residual[2]) * (1.0 / Determinant);
                Centre = Centre - Step;
                if (Length(Step) < 1.0e-17) break;
            }
            const Vector3 Across = Centre - Axis * Dot(Centre, Axis);
            Unit = Normalize(Across);
            const double  Third = Dot(Speed, Speed) - Dot(Bend, Centre - Edge);
            const Vector3 Rate  = (Cross(Unit, Speed) * (Side * BallRate) + Cross(Speed, Up) * BallRate + Cross(Up, Unit) * Third) * (1.0 / Dot(Up, Cross(Unit, Speed)));
            const Vector3 AcrossRate = Rate - Axis * Dot(Rate, Axis);
            Ruling Entry;
            Entry.Centre   = Centre;
            Entry.Near     = Centre - Up * Dot(Centre, Up);
            Entry.NearRate = Rate - Up * Dot(Rate, Up);
            Entry.Far      = Axis * Dot(Centre, Axis) + Unit * Radius;
            Entry.FarRate  = Axis * Dot(Rate, Axis) + (AcrossRate - Unit * Dot(Unit, AcrossRate)) * (Radius / Length(Across));
            Entry.AxialB   = Dot(Centre, Axis);
            Entry.TurnB    = Dot(Cross(Across, AcrossRate), Axis) / Dot(Across, Across);
            return Entry;
        };
        constexpr int Samples = 4096;
        const double  Gauss[2] = { 0.5 - 0.5 / std::sqrt(3.0), 0.5 + 0.5 / std::sqrt(3.0) };
        double Flux = 0.0, Winding = 0.0, Orientation = 0.0;
        for (int Index = 0; Index < Samples; ++Index)
        {
            const Ruling Entry = RulingAt(2.0 * π * Index / Samples);
            for (double v : Gauss)
            {
                const Vector3 Point  = Entry.Near + (Entry.Far - Entry.Near) * v;
                const Vector3 Normal = Cross(Entry.NearRate + (Entry.FarRate - Entry.NearRate) * v, Entry.Far - Entry.Near);
                Flux += 0.5 * Dot(Point, Normal);
                if (Index == 0) Orientation += Dot(Normal, Entry.Centre - Point);
            }
            Winding += Entry.AxialB * Entry.TurnB;
        }
        Flux    *= (2.0 * π / Samples) * (Orientation > 0.0 ? 1.0 : -1.0);
        Winding *= 2.0 * π / Samples;
        const double Area = Radius * std::fabs(Winding);                                       // [len²] - Cylinder strip
        return Side * (-Radius * Area + Flux) / 3.0;
    }

    using EdgeTest = std::function<bool(Vector3, Vector3, const CurveGeometry&)>;

    [[nodiscard]] std::vector<EdgeToken> SelectEdges(const ModelPartition& Partition, BodyToken Body, const EdgeTest& Test)
    {
        std::vector<EdgeToken> Selection;
        for (EdgeToken Edge : Partition.CollectInventory(Body).Edges)
        {
            const EdgeProfile& Profile = Partition.Edges.Resolve(Edge);
            if (Test(Partition.Vertices.Resolve(Profile.Start).Position, Partition.Vertices.Resolve(Profile.End).Position, Partition.Curves.Resolve(Profile.Curve))) Selection.push_back(Edge);
        }
        return Selection;
    }

    // The body's vertex at Point (the nearest one; Point is always a corner in the specimens).
    [[nodiscard]] VertexToken SelectVertex(const ModelPartition& Partition, BodyToken Body, Vector3 Point)
    {
        VertexToken Nearest  = {};
        double      Shortest = std::numeric_limits<double>::infinity();                      // [len] - Best distance so far
        for (VertexToken Vertex : Partition.CollectInventory(Body).Vertices)
        {
            const double Gap = Distance(Partition.Vertices.Resolve(Vertex).Position, Point);
            if (Gap < Shortest) { Shortest = Gap; Nearest = Vertex; }
        }
        return Nearest;
    }

    [[nodiscard]] EdgeTest FrontBottom()
    {
        return [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z < 1e-9 && B.Z < 1e-9; };
    }

    [[nodiscard]] EdgeTest FrontTop()
    {
        return [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 0.99 && B.Z > 0.99; };
    }

    [[nodiscard]] EdgeTest RiserTop()
    {
        return [](Vector3 A, Vector3 B, const CurveGeometry&) { return std::fabs(A.X - 1.0) < 1e-9 && std::fabs(B.X - 1.0) < 1e-9 && A.Z > 1.19 && B.Z > 1.19; };
    }

    [[nodiscard]] EdgeTest RimAt(double Height)
    {
        return [=](Vector3 Start, Vector3, const CurveGeometry& Curve) { return std::fabs(Start.Z - Height) < 1.0e-9 && !std::holds_alternative<StraightLine>(Curve); };
    }

    [[nodiscard]] EdgeTest TopEdges(double Height)
    {
        return [=](Vector3 Start, Vector3 End, const CurveGeometry& Curve) { return std::holds_alternative<StraightLine>(Curve) && std::fabs(Start.Z - Height) < 1.0e-9 && std::fabs(End.Z - Height) < 1.0e-9; };
    }

    [[nodiscard]] EdgeTest AnyEdge() { return [](Vector3, Vector3, const CurveGeometry&) { return true; }; }

    [[nodiscard]] BodyToken Fuse(ModelPartition& Partition, BodyToken First, BodyToken Second, BooleanOperation Operation)
    {
        return ComputeBoolean(Partition, First, Second, Operation).Body;
    }

    using SpecimenBuilder = std::function<BlendSpecimen(ModelPartition&)>;

    [[nodiscard]] BlendSpecimen Make(const char* Name, const char* Family, const char* Description, BodyToken Body, std::vector<EdgeToken> Edges, BlendProfile Profile, double Size,
                                     double Expected, int Faces = -1)
    {
        BlendSpecimen Specimen{ Name, Family, Description, Body, std::move(Edges), { Profile, Size }, Expected, Faces };
        Specimen.RefusalExpected = std::isnan(Expected) && Faces == 0;
        return Specimen;
    }

    [[nodiscard]] BlendSpecimen MakeRefusal(const char* Name, const char* Family, const char* Description, BodyToken Body, std::vector<EdgeToken> Edges, BlendProfile Profile, double Size)
    {
        BlendSpecimen Specimen = Make(Name, Family, Description, Body, std::move(Edges), Profile, Size, std::nan(""), 0);
        Specimen.RefusalExpected = true;
        return Specimen;
    }

    [[nodiscard]] const std::vector<std::pair<std::string, SpecimenBuilder>>& Specimens()
    {
        static const Vector3 BoxExtent = { 4.0, 3.0, 2.0 };
        static const std::vector<std::pair<std::string, SpecimenBuilder>> Table = {
            // ---- Harness: the old RollingBallBlend harness, same bodies, selections and sizes (r = 0.3 on the box).
            { "BoxEdgeFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                return Make("BoxEdgeFillet", "Harness", "One top edge, r 0.3: cylinder strip, two circular caps", Body,
                            SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; }),
                            BlendProfile::Fillet, 0.3, -StraightFilletLoss(0.3, 4.0), 7);
            } },
            { "BoxEdgeChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                return Make("BoxEdgeChamfer", "Harness", "One top edge, d 0.3: planar strip, straight caps", Body,
                            SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; }),
                            BlendProfile::Chamfer, 0.3, -0.5 * 0.3 * 0.3 * 4.0, 7);
            } },
            { "BoxTopFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                return Make("BoxTopFillet", "Harness", "Four top edges, r 0.3: four elliptical mitres", Body, SelectEdges(Partition, Body, TopEdges(2.0)),
                            BlendProfile::Fillet, 0.3, -MitredFilletLoss(0.3, 14.0), 10);
            } },
            { "BoxTopChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                return Make("BoxTopChamfer", "Harness", "Four top edges, d 0.3: straight mitres", Body, SelectEdges(Partition, Body, TopEdges(2.0)),
                            BlendProfile::Chamfer, 0.3, -MitredChamferLoss(0.3, 14.0), 10);
            } },
            { "BoxCornerFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                const Vector3   Corner = BoxExtent;
                const double    Radius = 0.3;
                return Make("BoxCornerFillet", "Harness", "Three edges at a corner, r 0.3: sphere corner + three caps", Body,
                            SelectEdges(Partition, Body, [=](Vector3 A, Vector3 B, const CurveGeometry&) { return Distance(A, Corner) < 1e-9 || Distance(B, Corner) < 1e-9; }),
                            BlendProfile::Fillet, Radius,
                            -(StraightFilletLoss(Radius, (4.0 - Radius) + (3.0 - Radius) + (2.0 - Radius)) + Radius * Radius * Radius * (1.0 - π / 6.0)), 10);
            } },
            { "BoxAllFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                BlendSpecimen Specimen = Make("BoxAllFillet", "Harness", "All twelve edges, r 0.3: eight sphere corners", Body, SelectEdges(Partition, Body, AnyEdge()),
                                              BlendProfile::Fillet, 0.3, -RoundedBoxLoss(BoxExtent, 0.3), 26);
                Specimen.Clearance = RoundedBoxClearance({}, BoxExtent, 0.3);
                return Specimen;
            } },
            { "BoxAllChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                BlendSpecimen Specimen = Make("BoxAllChamfer", "Harness", "All twelve edges, d 0.3: eight triple mitres to an apex", Body, SelectEdges(Partition, Body, AnyEdge()),
                                              BlendProfile::Chamfer, 0.3, -ChamferedBoxLoss(BoxExtent, 0.3), 18);
                Specimen.Clearance = ChamferedBoxClearance({}, BoxExtent, 0.3);
                return Specimen;
            } },
            { "CylinderRimFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 1.0, 2.0).Value;
                BlendSpecimen Specimen = Make("CylinderRimFillet", "Harness", "Top rim r 0.3: torus, seam continuation", Body, SelectEdges(Partition, Body, RimAt(2.0)),
                                              BlendProfile::Fillet, 0.3, -(2.0 * π * (1.0 - 0.3 * SpandrelFactor) * (1.0 - π / 4.0) * 0.09), 4);
                Specimen.Clearance = RoundedRimClearance(1.0, 2.0, 0.3);
                return Specimen;
            } },
            { "CylinderRimsChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 1.0, 2.0).Value;
                const EdgeTest  Rims = [](Vector3, Vector3, const CurveGeometry& Curve) { return !std::holds_alternative<StraightLine>(Curve); };
                return Make("CylinderRimsChamfer", "Harness", "Both rims d 0.3: two cones, seam trimmed at both ends", Body, SelectEdges(Partition, Body, Rims),
                            BlendProfile::Chamfer, 0.3, -2.0 * (2.0 * π * (1.0 - 0.3 / 3.0) * 0.5 * 0.09), 5);
            } },
            { "BossRootFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, {}, BoxExtent).Value, ConstructCylinder(Partition, UprightFrame({ 2.0, 1.5, 1.0 }), 0.6, 2.0).Value, BooleanOperation::Union);
                return Make("BossRootFillet", "Harness", "Concave boss root r 0.2: material added (reversed torus)", Body, SelectEdges(Partition, Body, RimAt(2.0)),
                            BlendProfile::Fillet, 0.2, 2.0 * π * (0.6 + 0.2 * SpandrelFactor) * (1.0 - π / 4.0) * 0.04, 9);
            } },
            { "BossRimFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, {}, BoxExtent).Value, ConstructCylinder(Partition, UprightFrame({ 2.0, 1.5, 1.0 }), 0.6, 2.0).Value, BooleanOperation::Union);
                return Make("BossRimFillet", "Harness", "Boss top rim r 0.2 on a Boolean result", Body, SelectEdges(Partition, Body, RimAt(3.0)),
                            BlendProfile::Fillet, 0.2, -(2.0 * π * (0.6 - 0.2 * SpandrelFactor) * (1.0 - π / 4.0) * 0.04), 9);
            } },
            { "HoleRimFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, {}, BoxExtent).Value, ConstructCylinder(Partition, UprightFrame({ 2.0, 1.5, -1.0 }), 0.6, 4.0).Value, BooleanOperation::Difference);
                return Make("HoleRimFillet", "Harness", "Convex rim of a through hole r 0.2 (genus 1)", Body, SelectEdges(Partition, Body, RimAt(2.0)),
                            BlendProfile::Fillet, 0.2, -(2.0 * π * (0.6 + 0.2 * SpandrelFactor) * (1.0 - π / 4.0) * 0.04), 8);
            } },
            { "FrustumRimFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructFrustum(Partition, UprightFrame({}), 1.2, 0.6, 1.5).Value;
                const Vector2   Down = Normalize(Vector2{ 1.2 - 0.6, -1.5 });
                return Make("FrustumRimFillet", "Harness", "Cone top rim r 0.15 (old harness: no closed form; now Pappus)", Body, SelectEdges(Partition, Body, RimAt(1.5)),
                            BlendProfile::Fillet, 0.15, MeasureRimBlendVolume({ 0.6, 1.5 }, { -1.0, 0.0 }, Down, 0.15, BlendProfile::Fillet, true), 4);
            } },
            { "CylinderChamferOversize", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 0.5, 2.0).Value;
                return MakeRefusal("CylinderChamferOversize", "Harness", "Chamfer 0.6 on a rim of radius 0.5 (must refuse)", Body, SelectEdges(Partition, Body, RimAt(2.0)), BlendProfile::Chamfer, 0.6);
            } },
            { "CylinderFilletOversize", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 0.5, 2.0).Value;
                return MakeRefusal("CylinderFilletOversize", "Harness", "Fillet 0.6 on a rim of radius 0.5 (must refuse)", Body, SelectEdges(Partition, Body, RimAt(2.0)), BlendProfile::Fillet, 0.6);
            } },

            // ---- BevelLab: the old editor proof scene (same coordinates).
            { "RoundedBlock", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, { 2.0, 1.5, 1.0 }).Value;
                BlendSpecimen Specimen = Make("RoundedBlock", "BevelLab", "All 12 edges fillet 0.2: 8 sphere corners", Body, SelectEdges(Partition, Body, AnyEdge()),
                                              BlendProfile::Fillet, 0.2, -RoundedBoxLoss({ 2.0, 1.5, 1.0 }, 0.2), 26);
                Specimen.Clearance = RoundedBoxClearance({}, { 2.0, 1.5, 1.0 }, 0.2);
                return Specimen;
            } },
            { "ChamferBlock", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, { 3.0, 0.0, 0.0 }, { 2.0, 1.5, 1.0 }).Value;
                return Make("ChamferBlock", "BevelLab", "All 12 edges chamfer 0.15: 8 triple mitres", Body, SelectEdges(Partition, Body, AnyEdge()),
                            BlendProfile::Chamfer, 0.15, -ChamferedBoxLoss({ 2.0, 1.5, 1.0 }, 0.15), 18);
            } },
            { "BossPlate", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, { 6.0, 0.0, 0.0 }, { 3.0, 2.0, 0.6 }).Value, ConstructCylinder(Partition, UprightFrame({ 7.5, 1.0, 0.3 }), 0.5, 1.0).Value, BooleanOperation::Union);
                std::vector<EdgeToken> Edges = SelectEdges(Partition, Body, TopEdges(0.6));
                for (double Height : { 0.6, 1.3 }) for (EdgeToken Edge : SelectEdges(Partition, Body, RimAt(Height))) Edges.push_back(Edge);
                const double Radius = 0.15;
                const double Delta  = -MitredFilletLoss(Radius, 10.0)
                                    + MeasureRimBlendVolume({ 0.5, 0.6 }, { 1.0, 0.0 }, { 0.0, 1.0 }, Radius, BlendProfile::Fillet, false)
                                    + MeasureRimBlendVolume({ 0.5, 1.3 }, { -1.0, 0.0 }, { 0.0, -1.0 }, Radius, BlendProfile::Fillet, true);
                return Make("BossPlate", "BevelLab", "Plate top edges (mitres) + concave boss root + boss rim, fillet 0.15", Body, Edges, BlendProfile::Fillet, Radius, Delta);
            } },
            { "Puck", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({ 11.0, 1.0, 0.0 }), 0.7, 1.2).Value;
                const EdgeTest  Rims = [](Vector3, Vector3, const CurveGeometry& Curve) { return !std::holds_alternative<StraightLine>(Curve); };
                const double    Each = MeasureRimBlendVolume({ 0.7, 1.2 }, { -1.0, 0.0 }, { 0.0, -1.0 }, 0.25, BlendProfile::Fillet, true);
                return Make("Puck", "BevelLab", "Both rims fillet 0.25 (seam trimmed at both ends)", Body, SelectEdges(Partition, Body, Rims), BlendProfile::Fillet, 0.25, 2.0 * Each, 5);
            } },
            { "HoleSlab", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, { 13.0, 0.0, 0.0 }, { 3.0, 2.0, 0.6 }).Value, ConstructCylinder(Partition, UprightFrame({ 14.5, 1.0, -0.5 }), 0.45, 2.0).Value, BooleanOperation::Difference);
                std::vector<EdgeToken> Edges = SelectEdges(Partition, Body, TopEdges(0.6));
                for (EdgeToken Edge : SelectEdges(Partition, Body, RimAt(0.6))) Edges.push_back(Edge);
                const double Radius = 0.12;
                const double Delta  = -MitredFilletLoss(Radius, 10.0) + MeasureRimBlendVolume({ 0.45, 0.6 }, { 1.0, 0.0 }, { 0.0, -1.0 }, Radius, BlendProfile::Fillet, true);
                return Make("HoleSlab", "BevelLab", "Slab top edges (mitres) + hole rim, fillet 0.12", Body, Edges, BlendProfile::Fillet, Radius, Delta);
            } },
            { "Frustum", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructFrustum(Partition, UprightFrame({ 18.0, 1.0, 0.0 }), 0.9, 0.45, 1.1).Value;
                const EdgeTest  Rims = [](Vector3, Vector3, const CurveGeometry& Curve) { return !std::holds_alternative<StraightLine>(Curve); };
                const Vector2   Up   = Normalize(Vector2{ 0.45 - 0.9, 1.1 });
                const double    Delta = MeasureRimBlendVolume({ 0.45, 1.1 }, { -1.0, 0.0 }, -Up, 0.12, BlendProfile::Chamfer, true)
                                      + MeasureRimBlendVolume({ 0.9, 0.0 }, { -1.0, 0.0 }, Up, 0.12, BlendProfile::Chamfer, true);
                return Make("Frustum", "BevelLab", "Both cone rims chamfer 0.12 (angles 112° / 68°)", Body, SelectEdges(Partition, Body, Rims), BlendProfile::Chamfer, 0.12, Delta, 5);
            } },

            // ---- Extra: wedge angles ≠ 90° and caps that are ellipses.
            { "PrismAcuteFillet", [](ModelPartition& Partition)
            {
                const double Side = 3.0;
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 0.0 }, { Side, 0.0, 0.0 }, { 0.5 * Side, 0.5 * Side * std::sqrt(3.0), 0.0 } }, { 0.0, 0.0, 2.0 }).Value.Body;
                const double Radius = 0.2;
                return Make("PrismAcuteFillet", "Extra", "60° edge of an equilateral prism, r 0.2: area r²(√3 − π/3)", Body,
                            SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return std::hypot(A.X, A.Y) < 1e-9 && std::hypot(B.X, B.Y) < 1e-9; }),
                            BlendProfile::Fillet, Radius, -Radius * Radius * (std::sqrt(3.0) - π / 3.0) * 2.0, 6);
            } },
            { "HexagonPrismChamfer", [](ModelPartition& Partition)
            {
                std::vector<Vector3> Profile;
                for (int Corner = 0; Corner < 6; ++Corner) Profile.push_back({ 1.5 * std::cos(Corner * π / 3.0), 1.5 * std::sin(Corner * π / 3.0), 0.0 });
                const BodyToken Body = ConstructPrism(Partition, Profile, { 0.0, 0.0, 1.5 }).Value.Body;
                const double Setback = 0.2;
                return Make("HexagonPrismChamfer", "Extra", "120° edge of a hexagonal prism, d 0.2: area ½d² sin 120°", Body,
                            SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return std::fabs(A.X - 1.5) < 1e-9 && std::fabs(B.X - 1.5) < 1e-9 && std::fabs(A.Y) < 1e-9 && std::fabs(B.Y) < 1e-9; }),
                            BlendProfile::Chamfer, Setback, -0.5 * Setback * Setback * std::sin(2.0 * π / 3.0) * 1.5, 9);
            } },
            { "SkewPrismFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 0.0 }, { 4.0, 0.0, 0.0 }, { 5.0, 3.0, 0.0 }, { 1.0, 3.0, 0.0 } }, { 0.0, 0.0, 2.0 }).Value.Body;
                return Make("SkewPrismFillet", "Extra", "Edge ending on slanted (parallel) faces, r 0.3: two elliptical caps", Body,
                            SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; }),
                            BlendProfile::Fillet, 0.3, -StraightFilletLoss(0.3, 4.0), 7);
            } },

            // ---- Overflow: blends wider than a support run off its far edge and are trimmed by the cliff face (Parasolid
            //      cliff overflow); partial, notch and overlapping overflows must still refuse.
            { "CliffBoxFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                BlendSpecimen Specimen = Make("CliffBoxFillet", "Overflow", "Top edge r 2.5 on a box 2 high: front face consumed, the bottom face trims the blend", Body,
                                              SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; }),
                                              BlendProfile::Fillet, 2.5, -BoxExtent.X * CliffArea(2.5, BoxExtent.Z, BlendProfile::Fillet), 6);
                Specimen.Clearance = CliffBoxClearance(BoxExtent, 2.5, BlendProfile::Fillet);
                return Specimen;
            } },
            { "CliffPlateFillet", [](ModelPartition& Partition)
            {
                const Vector3   Plate = { 4.0, 2.0, 0.2 };
                const BodyToken Body  = ConstructBlock(Partition, {}, Plate).Value;
                BlendSpecimen Specimen = Make("CliffPlateFillet", "Overflow", "Plate 0.2 thick, front top edge r 0.3: the blend runs off the front face onto the bottom", Body,
                                              SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 0.19 && B.Z > 0.19; }),
                                              BlendProfile::Fillet, 0.3, -Plate.X * CliffArea(0.3, Plate.Z, BlendProfile::Fillet), 6);
                Specimen.Clearance = CliffBoxClearance(Plate, 0.3, BlendProfile::Fillet);
                return Specimen;
            } },
            { "CliffPlateChamfer", [](ModelPartition& Partition)
            {
                const Vector3   Plate = { 4.0, 2.0, 0.2 };
                const BodyToken Body  = ConstructBlock(Partition, {}, Plate).Value;
                BlendSpecimen Specimen = Make("CliffPlateChamfer", "Overflow", "Plate 0.2 thick, front top edge chamfer 0.3: the chamfer plane is trimmed by the bottom face", Body,
                                              SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 0.19 && B.Z > 0.19; }),
                                              BlendProfile::Chamfer, 0.3, -Plate.X * CliffArea(0.3, Plate.Z, BlendProfile::Chamfer), 6);
                Specimen.Clearance = CliffBoxClearance(Plate, 0.3, BlendProfile::Chamfer);
                return Specimen;
            } },
            { "CliffPlateFlush", [](ModelPartition& Partition)
            {
                const Vector3   Plate = { 4.0, 2.0, 0.2 };
                const BodyToken Body  = ConstructBlock(Partition, {}, Plate).Value;
                BlendSpecimen Specimen = Make("CliffPlateFlush", "Overflow", "Plate 0.2 thick, front top edge r 0.2 exactly: the rail lands on the far edge (boundary case)", Body,
                                              SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 0.19 && B.Z > 0.19; }),
                                              BlendProfile::Fillet, 0.2, -StraightFilletLoss(0.2, Plate.X), 6);
                Specimen.Clearance = CliffBoxClearance(Plate, 0.2, BlendProfile::Fillet);
                return Specimen;
            } },
            { "CliffDiscFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 1.0, 0.2).Value;
                BlendSpecimen Specimen = Make("CliffDiscFillet", "Overflow", "Disc 0.2 thick, top rim r 0.3: the side band is consumed, the torus meets the bottom in a circle", Body,
                                              SelectEdges(Partition, Body, RimAt(0.2)), BlendProfile::Fillet, 0.3, -CliffRimLoss(1.0, 0.3, 0.2, BlendProfile::Fillet), 3);
                Specimen.Clearance = CliffDiscClearance(1.0, 0.2, 0.3, BlendProfile::Fillet);
                return Specimen;
            } },
            { "CliffDiscChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 1.0, 0.2).Value;
                BlendSpecimen Specimen = Make("CliffDiscChamfer", "Overflow", "Disc 0.2 thick, top rim chamfer 0.3: the cone meets the bottom in a circle", Body,
                                              SelectEdges(Partition, Body, RimAt(0.2)), BlendProfile::Chamfer, 0.3, -CliffRimLoss(1.0, 0.3, 0.2, BlendProfile::Chamfer), 3);
                Specimen.Clearance = CliffDiscClearance(1.0, 0.2, 0.3, BlendProfile::Chamfer);
                return Specimen;
            } },
            { "CliffPartial", [](ModelPartition& Partition)
            {
                // Front face 0.45 high at x = 0 and 0.1 high at x = 4: r 0.3 runs off it at one end only.
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 0.5 }, { 4.0, 0.0, 0.5 }, { 4.0, 0.0, 0.4 }, { 0.0, 0.0, 0.05 } }, { 0.0, 2.0, 0.0 }).Value.Body;
                return MakeRefusal("CliffPartial", "Refusal", "Tapering front face, top edge r 0.3: wider than the face at one end only (partial overflow, 🚧)", Body,
                                   SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 0.49 && B.Z > 0.49; }), BlendProfile::Fillet, 0.3);
            } },
            { "CliffOverlap", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, { 4.0, 2.0, 0.2 }).Value;
                return MakeRefusal("CliffOverlap", "Refusal", "Plate 0.2 thick, top AND bottom front edges r 0.3: both blends would consume the front face", Body,
                                   SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && std::fabs(A.Z - B.Z) < 1e-9; }), BlendProfile::Fillet, 0.3);
            } },
            { "NotchStepFillet", [](ModelPartition& Partition)
            {
                // Step: riser 0.2 high at x = 1 above a ledge at z = 1; the riser's lower edge is concave.
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 1.0 }, { 1.0, 0.0, 1.2 }, { 2.0, 0.0, 1.2 }, { 2.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } }, { 0.0, 1.5, 0.0 }).Value.Body;
                BlendSpecimen Specimen = Make("NotchStepFillet", "Overflow", "Riser 0.2 high, its top edge r 0.3: the riser is consumed, the ledge trims the blend and grows under it (notch)", Body,
                                              SelectEdges(Partition, Body, RiserTop()), BlendProfile::Fillet, 0.3, -1.5 * CliffArea(0.3, 0.2, BlendProfile::Fillet), 8);
                Specimen.Clearance = NotchStepClearance(0.3, BlendProfile::Fillet);
                return Specimen;
            } },
            { "NotchStepChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 1.0 }, { 1.0, 0.0, 1.0 }, { 1.0, 0.0, 1.2 }, { 2.0, 0.0, 1.2 }, { 2.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } }, { 0.0, 1.5, 0.0 }).Value.Body;
                BlendSpecimen Specimen = Make("NotchStepChamfer", "Overflow", "Riser 0.2 high, its top edge chamfer 0.3: the chamfer plane lands on the ledge (notch)", Body,
                                              SelectEdges(Partition, Body, RiserTop()), BlendProfile::Chamfer, 0.3, -1.5 * CliffArea(0.3, 0.2, BlendProfile::Chamfer), 8);
                Specimen.Clearance = NotchStepClearance(0.3, BlendProfile::Chamfer);
                return Specimen;
            } },
            { "NotchBossFillet", [](ModelPartition& Partition)
            {
                // Boss R 0.8, 0.2 high on a plate; top rim r 0.3: the torus meets the plate top in a circle inside the boss foot.
                const BodyToken Plate = ConstructBlock(Partition, { -1.5, -1.5, 0.0 }, { 3.0, 3.0, 0.5 }).Value;
                const BodyToken Boss  = ConstructCylinder(Partition, UprightFrame({ 0.0, 0.0, 0.5 }), 0.8, 0.2).Value;
                const BodyToken Body  = ComputeBoolean(Partition, Plate, Boss, BooleanOperation::Union).Body;
                BlendSpecimen Specimen = Make("NotchBossFillet", "Overflow", "Boss 0.2 high, top rim r 0.3: the side band is consumed, the plate top grows inward to the torus (closed notch)", Body,
                                              SelectEdges(Partition, Body, RimAt(0.7)), BlendProfile::Fillet, 0.3, -CliffRimLoss(0.8, 0.3, 0.2, BlendProfile::Fillet), 8);
                Specimen.Clearance = NotchBossClearance(0.8, 0.2, 0.3, BlendProfile::Fillet);
                return Specimen;
            } },
            { "SmoothFillet", [](ModelPartition& Partition)
            {
                // Front bottom edge already r 0.85 (front face flat for 0.15); top front edge r 0.3 rolls onto that fillet.
                const BodyToken Box  = ConstructBlock(Partition, {}, { 4.0, 2.0, 1.0 }).Value;
                const BodyToken Body = BlendEdges(Partition, Box, SelectEdges(Partition, Box, FrontBottom()), { BlendProfile::Fillet, 0.85 }).Body;
                const SmoothSection Section = MeasureSmoothSection(1.0, 0.85, 0.3);
                BlendSpecimen Specimen = Make("SmoothFillet", "Overflow", "Front face 0.15 tall above an r 0.85 fillet, top edge r 0.3: the ball rolls onto the fillet (smooth overflow)", Body,
                                              SelectEdges(Partition, Body, FrontTop()), BlendProfile::Fillet, 0.3, -4.0 * SmoothArea(Section, BlendProfile::Fillet), 7);
                Specimen.Clearance = SmoothClearance(Section, BlendProfile::Fillet);
                return Specimen;
            } },
            { "SmoothChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Box  = ConstructBlock(Partition, {}, { 4.0, 2.0, 1.0 }).Value;
                const BodyToken Body = BlendEdges(Partition, Box, SelectEdges(Partition, Box, FrontBottom()), { BlendProfile::Fillet, 0.85 }).Body;
                const SmoothSection Section = MeasureSmoothSection(1.0, 0.85, 0.3);                 // 90° edge: ball radius = setback
                BlendSpecimen Specimen = Make("SmoothChamfer", "Overflow", "Same body, top edge chamfer 0.3: the chord of the ball lands on the fillet", Body,
                                              SelectEdges(Partition, Body, FrontTop()), BlendProfile::Chamfer, 0.3, -4.0 * SmoothArea(Section, BlendProfile::Chamfer), 7);
                Specimen.Clearance = SmoothClearance(Section, BlendProfile::Chamfer);
                return Specimen;
            } },
            { "SmoothTight", [](ModelPartition& Partition)
            {
                // Ball r 0.9 cannot touch the r 0.85 fillet from inside: the smooth overflow refuses.
                const BodyToken Box  = ConstructBlock(Partition, {}, { 4.0, 2.0, 1.0 }).Value;
                const BodyToken Body = BlendEdges(Partition, Box, SelectEdges(Partition, Box, FrontBottom()), { BlendProfile::Fillet, 0.85 }).Body;
                return MakeRefusal("SmoothTight", "Refusal", "Smooth overflow onto a fillet that the ball cannot touch from inside (r 0.9 > 0.85)", Body,
                                   SelectEdges(Partition, Body, FrontTop()), BlendProfile::Fillet, 0.9);
            } },

            // ---- Variable: size linear along the edge (start vertex → end vertex).
            { "VariableFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body  = ConstructBlock(Partition, {}, BoxExtent).Value;
                const auto      Edges = SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; });
                const bool   Forward = !Edges.empty() && Partition.Vertices.Resolve(Partition.Edges.Resolve(Edges.front()).Start).Position.X < 2.0;
                const double Low = Forward ? 0.1 : 0.4, High = Forward ? 0.4 : 0.1;          // [len] - r at x = 0, x = 4
                BlendSpecimen Specimen = Make("VariableFillet", "Variable", "Front top edge r 0.1 → 0.4: exact cone tangent to both faces, elliptical caps", Body, Edges,
                                              BlendProfile::Fillet, 0.1, -VariableFilletLoss(4.0, Low, High), 7);
                Specimen.Settings.EndSize = 0.4;
                Specimen.Clearance        = VariableClearance(4.0, Low, High, BlendProfile::Fillet);
                return Specimen;
            } },
            { "VariableChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body  = ConstructBlock(Partition, {}, BoxExtent).Value;
                const auto      Edges = SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; });
                const bool   Forward = !Edges.empty() && Partition.Vertices.Resolve(Partition.Edges.Resolve(Edges.front()).Start).Position.X < 2.0;
                const double Low = Forward ? 0.1 : 0.4, High = Forward ? 0.4 : 0.1;          // [len] - d at x = 0, x = 4
                BlendSpecimen Specimen = Make("VariableChamfer", "Variable", "Front top edge d 0.1 → 0.4: the contact lines meet at the apex, so the chamfer is a plane", Body, Edges,
                                              BlendProfile::Chamfer, 0.1, -4.0 * (Low * Low + Low * High + High * High) / 6.0, 7);
                Specimen.Settings.EndSize = 0.4;
                Specimen.Clearance        = VariableClearance(4.0, Low, High, BlendProfile::Chamfer);
                return Specimen;
            } },
            { "VariableShared", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                BlendSpecimen Specimen = MakeRefusal("VariableShared", "Refusal", "Four top edges r 0.1 → 0.3: variable sizes meeting at a vertex (🚧)", Body, SelectEdges(Partition, Body, TopEdges(2.0)),
                                                     BlendProfile::Fillet, 0.1);
                Specimen.Settings.EndSize = 0.3;
                return Specimen;
            } },
            // ---- Size anchors: the vertex that takes Size is named, so the result no longer depends on the edge sense.
            { "VariableAnchored", [](ModelPartition& Partition)
            {
                const BodyToken Body  = ConstructBlock(Partition, {}, BoxExtent).Value;
                const auto      Edges = SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; });
                BlendSpecimen Specimen = Make("VariableAnchored", "Variable", "Front top edge r 0.1 anchored at x = 4 → 0.4 at x = 0: the anchor, not the edge sense, places the sizes", Body, Edges,
                                              BlendProfile::Fillet, 0.1, -VariableFilletLoss(4.0, 0.4, 0.1), 7);
                Specimen.Settings.EndSize     = 0.4;
                Specimen.Settings.SizeAnchors = { SelectVertex(Partition, Body, { 4.0, 0.0, 2.0 }) };
                Specimen.Clearance            = VariableClearance(4.0, 0.4, 0.1, BlendProfile::Fillet);
                return Specimen;
            } },
            { "VariableUnanchored", [](ModelPartition& Partition)
            {
                const BodyToken Body  = ConstructBlock(Partition, {}, BoxExtent).Value;
                const auto      Edges = SelectEdges(Partition, Body, [](Vector3 A, Vector3 B, const CurveGeometry&) { return A.Y < 1e-9 && B.Y < 1e-9 && A.Z > 1.99 && B.Z > 1.99; });
                BlendSpecimen Specimen = MakeRefusal("VariableUnanchored", "Refusal", "Front top edge r 0.1 → 0.4 anchored at a vertex off the edge: which end takes 0.1 is unknown", Body, Edges,
                                                     BlendProfile::Fillet, 0.1);
                Specimen.Settings.EndSize     = 0.4;
                Specimen.Settings.SizeAnchors = { SelectVertex(Partition, Body, { 0.0, 3.0, 0.0 }) };
                return Specimen;
            } },
            // ---- Curved-edge chamfers: ball-law spine on the exact edge, chord carrier between the rails.
            { "TiltedBoreChamfer", [](ModelPartition& Partition)
            {
                const double Tilt = 20.0 * π / 180.0;
                const Vector3 Axis = { std::sin(Tilt), 0.0, std::cos(Tilt) };
                const OrthonormalFrame Frame{ Axis * -3.0, { std::cos(Tilt), 0.0, -std::sin(Tilt) }, { 0.0, 1.0, 0.0 }, Axis };
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, { -2.0, -2.0, -1.0 }, { 4.0, 4.0, 1.0 }).Value, ConstructCylinder(Partition, Frame, 0.5, 6.0).Value, BooleanOperation::Difference);
                return Make("TiltedBoreChamfer", "CurvedChamfer", "Elliptical rim of a 20° tilted bore R 0.5, d 0.1: wedge 70°–110°, ball-law spine, chord carrier", Body, SelectEdges(Partition, Body, RimAt(0.0)),
                            BlendProfile::Chamfer, 0.1, MeasureTiltedRimChamferVolume(0.5, Tilt, 0.1, -1.0), 8);
            } },
            { "TiltedBossChamfer", [](ModelPartition& Partition)
            {
                const double Tilt = 20.0 * π / 180.0;
                const Vector3 Axis = { std::sin(Tilt), 0.0, std::cos(Tilt) };
                const OrthonormalFrame Frame{ Axis * -0.5, { std::cos(Tilt), 0.0, -std::sin(Tilt) }, { 0.0, 1.0, 0.0 }, Axis };
                const BodyToken Body = Fuse(Partition, ConstructBlock(Partition, { -2.0, -2.0, -1.0 }, { 4.0, 4.0, 1.0 }).Value, ConstructCylinder(Partition, Frame, 0.5, 1.5).Value, BooleanOperation::Union);
                return Make("TiltedBossChamfer", "CurvedChamfer", "Concave base rim of a 20° tilted boss R 0.5, d 0.1: material added, ball outside", Body, SelectEdges(Partition, Body, RimAt(0.0)),
                            BlendProfile::Chamfer, 0.1, MeasureTiltedRimChamferVolume(0.5, Tilt, 0.1, 1.0), 9);
            } },
            { "VariableRim", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructCylinder(Partition, UprightFrame({}), 1.0, 2.0).Value;
                BlendSpecimen Specimen = MakeRefusal("VariableRim", "Refusal", "Cylinder top rim r 0.1 → 0.3: variable size on a closed edge (the size would jump at its only vertex)", Body,
                                                     SelectEdges(Partition, Body, RimAt(2.0)), BlendProfile::Fillet, 0.1);
                Specimen.Settings.EndSize = 0.3;
                return Specimen;
            } },
            // ---- Variable size on a curved edge: ball-law spine with r(t) linear, canal (fillet) or chord (chamfer) carrier.
            //      D body: cylinder R 1, H 1 cut by x = 0, seam at x = −1 (removed); the top half-rim ends on two plane caps.
            { "VariableArcFillet", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructCylinder(Partition, { {}, { -1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 1.0, 1.0).Value,
                                            ConstructBlock(Partition, { 0.0, -2.0, -1.0 }, { 2.0, 4.0, 3.0 }).Value, BooleanOperation::Intersection);
                BlendSpecimen Specimen = Make("VariableArcFillet", "Variable", "Half-rim of a D, r 0.1 → 0.3: canal surface of the balls (circles lean by dr/ds), plane caps", Body,
                                              SelectEdges(Partition, Body, RimAt(1.0)), BlendProfile::Fillet, 0.1, MeasureVariableArcFilletVolume(1.0, 0.1, 0.3), 5);
                Specimen.Settings.EndSize = 0.3;
                return Specimen;
            } },
            { "VariableArcChamfer", [](ModelPartition& Partition)
            {
                const BodyToken Body = Fuse(Partition, ConstructCylinder(Partition, { {}, { -1.0, 0.0, 0.0 }, { 0.0, -1.0, 0.0 }, { 0.0, 0.0, 1.0 } }, 1.0, 1.0).Value,
                                            ConstructBlock(Partition, { 0.0, -2.0, -1.0 }, { 2.0, 4.0, 3.0 }).Value, BooleanOperation::Intersection);
                BlendSpecimen Specimen = Make("VariableArcChamfer", "Variable", "Half-rim of a D, d 0.1 → 0.3: ball-law spine, chord carrier, every ruling in its meridian plane", Body,
                                              SelectEdges(Partition, Body, RimAt(1.0)), BlendProfile::Chamfer, 0.1, MeasureVariableArcChamferVolume(1.0, 0.1, 0.3), 5);
                Specimen.Settings.EndSize = 0.3;
                return Specimen;
            } },

            // ---- Refusal: configurations the operation must refuse atomically.
            { "FilletRailsCross", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructBlock(Partition, {}, BoxExtent).Value;
                return MakeRefusal("FilletRailsCross", "Refusal", "All edges r 1.2 on a box 2 high: vertical contact rails run backwards", Body, SelectEdges(Partition, Body, AnyEdge()), BlendProfile::Fillet, 1.2);
            } },
            { "MixedConvexity", [](ModelPartition& Partition)
            {
                const BodyToken Body = ConstructPrism(Partition, { { 0.0, 0.0, 0.0 }, { 3.0, 0.0, 0.0 }, { 3.0, 1.0, 0.0 }, { 1.0, 1.0, 0.0 }, { 1.0, 3.0, 0.0 }, { 0.0, 3.0, 0.0 } }, { 0.0, 0.0, 1.0 }).Value.Body;
                const EdgeTest Pick = [](Vector3 A, Vector3 B, const CurveGeometry&)
                {
                    const bool Inner = std::fabs(A.X - 1.0) < 1e-9 && std::fabs(A.Y - 1.0) < 1e-9 && std::fabs(B.X - 1.0) < 1e-9 && std::fabs(B.Y - 1.0) < 1e-9;
                    const bool Rim   = A.Z > 0.99 && B.Z > 0.99 && std::fabs(A.X - 1.0) < 1e-9 && std::fabs(B.X - 1.0) < 1e-9;
                    return Inner || Rim;
                };
                return MakeRefusal("MixedConvexity", "Refusal", "Concave inner edge + convex top edge of an L block meet beside a sharp edge (mixed convexity with a sharp edge, 🚧)", Body, SelectEdges(Partition, Body, Pick), BlendProfile::Fillet, 0.2);
            } },
        };
        return Table;
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

double MeasureRimBlendVolume(Vector2 Corner, Vector2 AlongFirst, Vector2 AlongSecond, double Size, BlendProfile Profile, bool ConvexCondition)
{
    const Vector2 First  = Normalize(AlongFirst);                                            // [-] - Along face 1
    const Vector2 Second = Normalize(AlongSecond);                                           // [-] - Along face 2
    const double  Wedge  = std::acos(std::clamp(Dot(First, Second), -1.0, 1.0));             // [rad] - θ
    const double  Sign   = ConvexCondition ? -1.0 : 1.0;                                     // [-]   - Removed / added
    // 📝 Pappus: V = 2π ρ̄ A over the cross-section in the meridian half-plane.
    const auto Triangle = [](Vector2 A, Vector2 B, Vector2 C, double& Moment)
    {
        const double Area = 0.5 * std::fabs(Cross(B - A, C - A));                            // [len²] - Area
        Moment += Area * (A.X + B.X + C.X) / 3.0;                                             // [len³] - ∫ρ dA
        return Area;
    };
    double Moment = 0.0;                                                                     // [len³] - ∫ρ dA of the region
    if (Profile == BlendProfile::Chamfer)
    {
        Triangle(Corner, Corner + First * Size, Corner + Second * Size, Moment);
        return Sign * 2.0 * π * Moment;
    }
    const double  Reach    = Size / std::tan(0.5 * Wedge);                                   // [len] - Contact distance from E
    const Vector2 Bisector = Normalize(First + Second);                                      // [-]   - Into the wedge
    const Vector2 Centre   = Corner + Bisector * (Size / std::sin(0.5 * Wedge));             // [len] - Ball centre
    Triangle(Corner, Corner + First * Reach, Centre, Moment);
    Triangle(Corner, Centre, Corner + Second * Reach, Moment);
    const double Sweep    = π - Wedge;                                                       // [rad] - Sector angle
    const double Sector   = 0.5 * Size * Size * Sweep;                                       // [len²] - Sector area
    const double Distance = 4.0 * Size * std::sin(0.5 * Sweep) / (3.0 * Sweep);              // [len]  - Centroid from the centre
    Moment -= Sector * (Centre - Bisector * Distance).X;
    return Sign * 2.0 * π * Moment;
}

double MeasureVariableFilletLoss(double Length, double Low, double High)
{
    if (std::fabs(High - Low) <= 1.0e-12 * std::max(Low, High)) return (1.0 - std::numbers::pi / 4.0) * Low * Low * Length;
    return VariableFilletLoss(Length, Low, High);
}

std::vector<std::string> EnumerateBlendSpecimens()
{
    std::vector<std::string> Names;
    for (const auto& Entry : Specimens()) Names.push_back(Entry.first);
    return Names;
}

BlendSpecimen ConstructBlendSpecimen(ModelPartition& Partition, const std::string& Name)
{
    for (const auto& Entry : Specimens())
    {
        if (Entry.first == Name) return Entry.second(Partition);
    }
    return {};
}

} // namespace Frontier::GeometrySpace::Parametric
