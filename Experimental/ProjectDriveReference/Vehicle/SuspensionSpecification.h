//============================================================================================================================================
//                                                     SUSPENSIONSPECIFICATION.H
//============================================================================================================================================
// 📦 Per-corner strut schema — rest length and axis from the authored sockets, rate and damping from natural frequency.
//
//    This is GRIT's suspension model, carried over whole. GRIT drove the same vehicle through the same control
//    integrator, so the tuning values below are its values, not new guesses: 2.0 Hz natural frequency, 0.6 damping
//    ratio, a digressive spring with a 1.5 hardening factor, 2 cm of bump and 8 cm of droop, and an anti-roll bar at
//    80 kN/m. GRIT's line-trace contact search is carried over too (TraceContactTyre.h) — the wheel contact already
//    resolves the ground with its own particles, so the strut here ends at the hub and the tyre takes it from there.
//
//    📐 Derivation. A quarter car of sprung mass 𝑚 on a spring of rate 𝑘 has ω = √(𝑘/𝑚), so specifying the ride
//       frequency 𝑓 fixes the rate for whatever mass the corner actually carries:
//
//           ω  = 2π𝑓                          [rad/s]
//           𝑘  = 𝑚 ω²                         [N/m]
//           𝑐  = 2 ζ √(𝑘 𝑚) = 2 ζ 𝑚 ω         [N·s/m]
//
//       That is why SpringRate and DampingRate are outputs here and not tuning knobs: change the mass distribution
//       and the corner re-rates itself instead of silently going soft. Static sag follows from the frequency alone,
//       𝑥₀ = g/ω², which at 2.0 Hz is 62 mm — the number the authored ride height has to be consistent with.
//
//    📐 Progressive rate. Real springs are not Hookean over their whole travel, so the force is a cubic in
//       compression 𝑥, with the instantaneous rate its derivative:
//
//           F(𝑥)  = k₁𝑥 + k₂𝑥² + k₃𝑥³         [N]
//           k(𝑥)  = k₁ + 2k₂𝑥 + 3k₃𝑥²         [N/m]
//
//       Digressive (GRIT's default) starts stiff and softens; progressive does the reverse. The coefficients are
//       solved from the hardening factor and the travel range rather than being typed in.
//
//    ⚠️ Units are METRES and newtons throughout. GRIT stores several of these in centimetres because Unreal does;
//       every such value is converted here and its original spelling is quoted in the comment beside it.

#pragma once

#include <cmath>
#include <cstdint>

namespace Frontier::Vehicle {

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPRING CURVE SHAPE
//------------------------------------------------------------------------------------------------------------------------

enum class SuspensionCurve : uint8_t
{
    Linear,        // constant rate
    Progressive,   // rising rate — soft off the bump stop, stiff at full compression
    Digressive,    // falling rate — stiff initially, softening into the travel
    DualRate,      // two-stage linear
};

enum class SuspensionArchitecture : uint8_t
{
    Telescopic,        // vertical spring/damper; roll centre at the mount
    DoubleWishbone,    // A-arms; roll centre from the instant centre
    MacPhersonStrut,   // roll centre from the strut axis and lower arm
    MultiLink,         // roll centre from a virtual instant centre
    SolidAxle,         // roll centre at the axle centre
    Manual,            // roll-centre height specified directly
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CUBIC SPRING COEFFICIENTS
//------------------------------------------------------------------------------------------------------------------------

struct ProgressiveSpringCoefficients
{
    float k1 = 0.0f;   // [N/m]   - linear stiffness
    float k2 = 0.0f;   // [N/m²]  - quadratic hardening
    float k3 = 0.0f;   // [N/m³]  - cubic hardening

    [[nodiscard]] bool IsProgressive() const noexcept
    { return std::abs(k2) > 1.0e-6f || std::abs(k3) > 1.0e-6f; }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                   ANTI-ROLL BAR
//------------------------------------------------------------------------------------------------------------------------

// Torsion bar tying the two corners of one axle: it resists the DIFFERENCE in their compressions and does nothing
//    when both wheels move together, so it trades roll stiffness against warp compliance without altering ride rate.
struct AntiRollBar
{
    bool     Enabled     = true;        // [-]      - bar fitted to this axle
    float    Stiffness   = 80000.0f;    // [N/m]    - roll stiffness against differential compression
    float    Damping     = 1500.0f;     // [N·s/m]  - damping against differential compression rate
    uint32_t LeftWheel   = 0u;          // [-]      - wheel index on the +Y side
    uint32_t RightWheel  = 1u;          // [-]      - wheel index on the −Y side
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 STRUT SPECIFICATION
//------------------------------------------------------------------------------------------------------------------------

struct SuspensionSpecification
{
    // ---- Tuning: the two numbers that actually get tuned ----
    float NaturalFrequency = 2.0f;      // [Hz]     - ride frequency; sets SpringRate for the corner's sprung mass
    float DampingRatio     = 0.6f;      // [-]      - ζ; sets DampingRate. 1.0 is critical, road cars run 0.2–0.7

    // ---- Rate and damping: DERIVED from the two above by Calibrate(). Never authored. ----
    float SpringRate       = 250000.0f; // [N/m]    - 𝑘 = 𝑚ω². The literal is GRIT's placeholder, overwritten on setup
    float DampingRate      = 3000.0f;   // [N·s/m]  - 𝑐 = 2ζ𝑚ω. Likewise a placeholder

    // ---- Travel envelope ----
    float MaxTravel        = 0.35f;     // [m]      - hard limit on strut stroke
    float BumpTravel       = 0.02f;     // [m]      - compression available past static (GRIT MinRaise, 2 cm)
    float DroopTravel      = 0.08f;     // [m]      - extension available past static (GRIT MaxDrop, 8 cm)
    // ⚠️ Preload is DERIVED, not copied. GRIT's literal 5000 N goes with its 250 kN/m spring — 2 cm of preload on a
    //    vehicle whose corners weigh ~15 kN. Copying the newtons onto a 1.3 t car would preload each corner harder
    //    than the corner weighs, parking it on the droop stops. Calibrate() carries over the 2 cm instead.
    float Preload          = 5000.0f;   // [N]      - preload force; = SpringRate × kPreloadCompression after Calibrate
    float StaticPreload    = 0.05f;     // [m]      - compression carried at rest (GRIT StaticPreload, 5 cm)
    float StaticCompression = 0.0f;     // [m]      - solved ride compression for the load this corner carries

    // ---- Geometry, resolved from the authored sockets ----
    float FreeLength       = 0.0f;      // [m]      - uncompressed mount-to-hub distance; see ResolveFromSockets
    float MountLength      = 0.35f;     // [m]      - physical length of the mount hardware (GRIT, 35 cm)
    float AxisX            = 0.0f;      // [-]      - strut axis in chassis-local space, pointing DOWN in compression
    float AxisY            = 0.0f;      // [-]        (0,0,−1) is a vertical telescopic strut; Y is mirrored per side
    float AxisZ            = -1.0f;     // [-]

    // ---- Progressive spring ----
    bool                          Progressive    = true;                         // [-] - enable the cubic model
    SuspensionCurve               Curve          = SuspensionCurve::Digressive;  // [-] - curve shape
    float                         HardeningFactor = 1.5f;                        // [-] - rate multiplier at full travel
    ProgressiveSpringCoefficients Coefficients{};                                // [-] - solved by Calibrate()

    //--------------------------------------------------------------------------------------------------------------------
    // Set the rate and damping this corner needs to ride at NaturalFrequency while carrying SprungMass, then solve the
    //    cubic coefficients for the resulting rate. Call once at build, and again whenever the mass split changes.
    //--------------------------------------------------------------------------------------------------------------------
    void Calibrate(float SprungMass) noexcept
    {
        constexpr float τ = 6.28318530718f;
        constexpr float kPreloadCompression = 0.02f;   // [m] GRIT's 5000 N over its 250 kN/m spring
        const float ω = τ * NaturalFrequency;
        SpringRate    = SprungMass * ω * ω;
        DampingRate   = 2.0f * DampingRatio * SprungMass * ω;
        Preload       = SpringRate * kPreloadCompression;
        SolveCoefficients();
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Compression at which this strut balances SupportedLoad, found by bisecting the force curve.
    //    📐 Solving F(x) = Preload + k₁x + k₂x² + k₃x³ = Load numerically rather than algebraically is what makes
    //    preload and the progressive curve compose. The closed-form 𝑥₀ = g/ω² is only the LINEAR, zero-preload case;
    //    using it with either feature present leaves a standing force error that drives the car off the ground.
    //--------------------------------------------------------------------------------------------------------------------
    [[nodiscard]] float StaticCompressionFor(float SupportedLoad) const noexcept
    {
        if (SupportedLoad <= Preload) return 0.0f;                 // preload alone holds it; strut sits topped out
        float Low = 0.0f, High = MaxTravel;
        for (int Iteration = 0; Iteration < 48; ++Iteration)
        {
            const float Mid = 0.5f * (Low + High);
            if (Preload + SpringForce(Mid) < SupportedLoad) Low = Mid; else High = Mid;
        }
        return 0.5f * (Low + High);
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Free length from the two authored sockets. 💡 The model is posed at REST, so the socket separation is the
    //    STATIC length, already carrying the car's weight — the uncompressed spring is that much longer again.
    //    Getting this backwards sinks the car by exactly one static sag, which is the classic version of this bug.
    //--------------------------------------------------------------------------------------------------------------------
    void ResolveFromSockets(float MountHeight, float AxleHeight, float SupportedLoad) noexcept
    {
        StaticCompression = StaticCompressionFor(SupportedLoad);
        FreeLength        = (MountHeight - AxleHeight) + StaticCompression;
    }

    // Static sag implied by the ride frequency alone: 𝑥₀ = g/ω². Independent of mass, which is the useful part.
    [[nodiscard]] float StaticSag(float Gravity = 9.81f) const noexcept
    {
        constexpr float τ = 6.28318530718f;
        const float ω = τ * NaturalFrequency;
        return (ω > 1.0e-4f) ? Gravity / (ω * ω) : 0.0f;
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Spring force at compression 𝑥, positive in compression.
    //
    // 🐞 GRIT's two spring functions disagree, and this is where that is repaired. Its rate function floors the
    //    stiffness at a tenth of the base rate; its force function evaluates the raw cubic with no floor at all. For
    //    a digressive curve those are different springs. With the shipped numbers — 1.5 hardening over a 0.10 m
    //    range — the raw cubic peaks at 150 mm and then FALLS, so past that the spring pushes the car down harder
    //    the more it is compressed, and every corner slams to its bump stop the moment aero load arrives. Measured
    //    on the scripted run before the fix: 60 % of all samples sitting on the stop.
    //
    // 📐 The repair is to honour the floor that GRIT already declares, by integrating the FLOORED rate rather than
    //    evaluating the unfloored polynomial:
    //
    //        F(𝑥) = ∫₀ˣ max( k(u), 0.1·SpringRate ) du
    //
    //    Below the turning point that is identical to GRIT's cubic, so nothing about the intended feel changes; above
    //    it the spring keeps stiffening gently instead of inverting. The curve stays continuous and monotonic.
    //--------------------------------------------------------------------------------------------------------------------
    [[nodiscard]] float SpringForce(float Compression) const noexcept
    {
        if (!Progressive || !Coefficients.IsProgressive()) return SpringRate * Compression;

        const float Cubic = [&](float x) noexcept
        { return Coefficients.k1 * x + Coefficients.k2 * x * x + Coefficients.k3 * x * x * x; }(Compression);

        const float Floor = SpringRate * 0.1f;
        const float Turn  = RateFloorCompression();
        if (Turn <= 0.0f || Compression <= Turn) return Cubic;

        const float AtTurn = Coefficients.k1 * Turn + Coefficients.k2 * Turn * Turn
                           + Coefficients.k3 * Turn * Turn * Turn;
        return AtTurn + Floor * (Compression - Turn);
    }

    // Compression at which the raw cubic rate falls to the 10 % floor; 0 when it never does (the usual progressive
    //    case). Smallest positive root of 3k₃𝑥² + 2k₂𝑥 + (k₁ − 0.1·SpringRate) = 0.
    [[nodiscard]] float RateFloorCompression() const noexcept
    {
        const float Floor = SpringRate * 0.1f;
        const float a = 3.0f * Coefficients.k3;
        const float b = 2.0f * Coefficients.k2;
        const float c = Coefficients.k1 - Floor;

        if (std::abs(a) < 1.0e-6f)
        {
            if (std::abs(b) < 1.0e-6f) return 0.0f;
            const float Root = -c / b;
            return (Root > 0.0f) ? Root : 0.0f;
        }
        const float Discriminant = b * b - 4.0f * a * c;
        if (Discriminant < 0.0f) return 0.0f;
        const float Sqrt = std::sqrt(Discriminant);
        const float R1 = (-b - Sqrt) / (2.0f * a);
        const float R2 = (-b + Sqrt) / (2.0f * a);
        const float Low  = (R1 < R2) ? R1 : R2;
        const float High = (R1 < R2) ? R2 : R1;
        if (Low  > 0.0f) return Low;
        if (High > 0.0f) return High;
        return 0.0f;
    }

    // Instantaneous rate at compression 𝑥 — the derivative of the above, floored at a tenth of the base rate so a
    //    digressive curve can never invert and start pulling the corner down.
    [[nodiscard]] float EffectiveRate(float Compression) const noexcept
    {
        if (!Progressive || !Coefficients.IsProgressive()) return SpringRate;
        const float Rate = Coefficients.k1 + 2.0f * Coefficients.k2 * Compression
                         + 3.0f * Coefficients.k3 * Compression * Compression;
        return (Rate > SpringRate * 0.1f) ? Rate : SpringRate * 0.1f;   // the floor SpringForce now integrates
    }

    //--------------------------------------------------------------------------------------------------------------------
    // Solve k₁/k₂/k₃ from the hardening factor over the bump-plus-droop range, exactly as GRIT does.
    //--------------------------------------------------------------------------------------------------------------------
    void SolveCoefficients() noexcept
    {
        if (!Progressive)
        {
            Coefficients = { SpringRate, 0.0f, 0.0f };
            return;
        }

        const float Range = BumpTravel + DroopTravel;                  // [m] - full travel the curve is shaped over
        if (Range <= 1.0e-4f) { Coefficients = { SpringRate, 0.0f, 0.0f }; return; }
        const float ForceAtRest = SpringRate * StaticPreload;          // [N]
        const float ForceAtFull = ForceAtRest * HardeningFactor;       // [N]

        switch (Curve)
        {
        case SuspensionCurve::Progressive:
            Coefficients.k1 = SpringRate;
            Coefficients.k2 = 0.0f;
            Coefficients.k3 = (ForceAtFull - SpringRate * Range) / (Range * Range * Range);
            break;

        case SuspensionCurve::Digressive:
            Coefficients.k1 = SpringRate * HardeningFactor;
            Coefficients.k2 = -(SpringRate * (HardeningFactor - 1.0f)) / Range;
            Coefficients.k3 = 0.0f;
            break;

        // 📝 GRIT declares DualRate but its coefficient solver falls through to the linear case, so a DualRate strut
        //    behaves exactly like a Linear one. That behaviour is reproduced rather than invented over.
        case SuspensionCurve::Linear:
        case SuspensionCurve::DualRate:
        default:
            Coefficients = { SpringRate, 0.0f, 0.0f };
            break;
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  ROLL CENTRE GEOMETRY
//------------------------------------------------------------------------------------------------------------------------

// Where the sprung mass pivots when it rolls. Load transfer splits into a GEOMETRIC part that acts instantly through
//    the linkage and an ELASTIC part that has to compress a spring first; the roll-centre height sets the ratio.
//    A low centre routes more of the transfer through the springs and bar, which is what makes the response
//    progressive rather than sharp.
struct RollCentreGeometry
{
    SuspensionArchitecture FrontArchitecture = SuspensionArchitecture::Telescopic;
    SuspensionArchitecture RearArchitecture  = SuspensionArchitecture::Telescopic;
    float FrontHeight               = 0.05f;   // [m] - front roll-centre height above ground
    float RearHeight                = 0.10f;   // [m] - rear roll-centre height; higher than front ⇒ understeer
    float TelescopicCorrection      = 0.15f;   // [-] - scales a telescopic strut's very high raw mount height down
};

} // namespace Frontier::Vehicle
