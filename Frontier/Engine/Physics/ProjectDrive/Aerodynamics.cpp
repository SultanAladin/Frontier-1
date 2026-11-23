//============================================================================================================================================
// 📦 Aerodynamics.cpp — faithful port of GRIT AVehicleSolver::ComputeAerodynamicForces()
//   Source: SultanAladin/GRIT@source-only (e61b15c, 2026-09-29), VehicleFramework/VehicleSolver.cpp (~L2789).
//   The formulae, coefficients and clamps below are reproduced verbatim; only UE math (FMath/FVector) is
//   translated to the self-contained Vec3 helpers. The dead `if (false && ...)` underbody branch in the
//   original is omitted (it never executes); the ACTIVE Bernoulli-venturi underbody model is ported.
//============================================================================================================================================

#include "Aerodynamics.h"

namespace Frontier::Vehicle {

namespace {
constexpr float kPi = 3.14159265358979323846f;
inline float Clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerpf(float a, float b, float t) noexcept { return a + (b - a) * t; }
inline float Deg2Rad(float d) noexcept { return d * (kPi / 180.0f); }
inline Vec3  SafeNormal(const Vec3& v, const Vec3& fallback) noexcept
{
    const float l = v.Length();
    return (l > 1e-8f) ? Vec3{v.x / l, v.y / l, v.z / l} : fallback;
}
} // namespace

AeroForces ComputeAerodynamicForces(
    const AerodynamicPackage& Aero,
    const Vec3& VehicleVelocity_WorldMs,
    float RideHeight_m,
    const Vec3& BodyForward_World,
    const Vec3& BodyRight_World,
    const Vec3& BodyUp_World,
    float BrakeInput,
    float HandbrakeInput,
    float ThrottleInput,
    float YawRate_rads) noexcept
{
    AeroForces Forces;

    if (!Aero.Enabled) { return Forces; }

    const float VehicleSpeed_ms = VehicleVelocity_WorldMs.Length();
    if (VehicleSpeed_ms < 0.5f) { return Forces; } // aero negligible below 0.5 m/s

    const Vec3 VelocityDir_World = VehicleVelocity_WorldMs * (1.0f / VehicleSpeed_ms);
    const Vec3 BodyForward = SafeNormal(BodyForward_World, {1.0f, 0.0f, 0.0f});
    const Vec3 BodyRight   = SafeNormal(BodyRight_World,   {0.0f, 1.0f, 0.0f});
    const Vec3 BodyUp      = SafeNormal(BodyUp_World,      {0.0f, 0.0f, 1.0f});

    const float Rho = Aero.AirDensity_kgm3;
    const float V   = VehicleSpeed_ms;
    const float h   = std::max(RideHeight_m, 0.005f);
    Forces.UnderbodyRideHeight_m = h;
    Forces.UnderbodyRideHeightFactor = 1.0f;

    const float Vx = Dot(VehicleVelocity_WorldMs, BodyForward);
    const float Vy = Dot(VehicleVelocity_WorldMs, BodyRight);
    const float Vz = Dot(VehicleVelocity_WorldMs, BodyUp);
    const float Beta_rad = std::atan2(Vy, std::max(std::fabs(Vx), 1.0f));
    const float BetaAbs  = std::fabs(Beta_rad);
    const float SideslipDownforceFactor = Clampf(1.0f - BetaAbs * 0.18f, 0.82f, 1.0f);
    const float V2 = V * V;
    const float q  = 0.5f * Rho * V2;                 // dynamic pressure [Pa]

    // Aero-brake activation gate
    const bool bAeroBrakeActive = (BrakeInput >= Aero.AeroBrakeThreshold) && (HandbrakeInput < 0.1f);

    //------------------------------------------------------------------ REAR WING (lifting line, Prandtl)
    if (Aero.RearWing.Enabled)
    {
        const AeroWing& Wing = Aero.RearWing;
        float EffectiveAngle = Wing.CurrentAngle_deg;
        if (Wing.IsAdaptive)
        {
            if (V > Wing.AdaptiveSpeedThreshold_ms)
                EffectiveAngle = Lerpf(Wing.CurrentAngle_deg, Wing.MinAngle_deg,
                                       Clampf((V - Wing.AdaptiveSpeedThreshold_ms) / 20.0f, 0.0f, 1.0f) * 0.5f);
            if (bAeroBrakeActive)
                EffectiveAngle = Clampf(EffectiveAngle + Wing.BrakeDeployAngle_deg * BrakeInput, Wing.MinAngle_deg, Wing.MaxAngle_deg);
        }
        const float CL_alpha = 0.11f;                          // lift-curve slope [1/deg]
        const float CL = Wing.BaseCoeffLift + CL_alpha * EffectiveAngle;
        Forces.WingDownforce_N = -q * Wing.Area_m2 * CL * SideslipDownforceFactor; // negative lift = downforce

        const float e_oswald = 0.82f;
        const float CD_induced = (CL * CL) / (kPi * Wing.AspectRatio * e_oswald);
        const float CD_total   = Wing.BaseCoeffDrag + CD_induced;
        Forces.WingDrag_N = q * Wing.Area_m2 * CD_total;
    }

    //------------------------------------------------------------------ CANARDS (lifting line)
    for (const AeroCanard& Canard : Aero.Canards)
    {
        if (!Canard.Enabled) { continue; }
        float EffectiveAngle = Canard.CurrentAngle_deg;
        if (Canard.IsAdaptive)
        {
            if (V > Canard.AdaptiveSpeedThreshold_ms)
                EffectiveAngle = Lerpf(Canard.CurrentAngle_deg, Canard.MinAngle_deg,
                                       Clampf((V - Canard.AdaptiveSpeedThreshold_ms) / 30.0f, 0.0f, 1.0f) * 0.6f);
            if (bAeroBrakeActive)
                EffectiveAngle = Clampf(EffectiveAngle + Canard.BrakeDeployAngle_deg * BrakeInput, Canard.MinAngle_deg, Canard.MaxAngle_deg);
            if (!bAeroBrakeActive && ThrottleInput > 0.5f)
                EffectiveAngle = Clampf(EffectiveAngle - Canard.ThrottleReductionAngle_deg * (ThrottleInput - 0.5f) / 0.5f,
                                        Canard.MinAngle_deg, Canard.MaxAngle_deg);
        }
        const float DamageFactor = 1.0f - Canard.DamageLevel;
        const float CL_alpha = 0.10f;
        const float CL = (Canard.BaseCoeffLift + CL_alpha * EffectiveAngle) * DamageFactor;
        const float e_oswald = 0.78f;
        const float CD_induced = (CL * CL) / (kPi * Canard.AspectRatio * e_oswald);
        const float CD_total   = (Canard.BaseCoeffDrag + CD_induced) * DamageFactor;

        Forces.TotalCanardDownforce_N += -q * Canard.Area_m2 * CL * SideslipDownforceFactor;
        Forces.TotalCanardDrag_N      +=  q * Canard.Area_m2 * CD_total;
    }

    //------------------------------------------------------------------ FRONT SPLITTER (pressure coeff + ground effect)
    if (Aero.FrontSplitter.Enabled)
    {
        const AeroSplitter& Splitter = Aero.FrontSplitter;
        const float h_ref = std::max(Splitter.AirDamHeight_m * 1.5f, 0.05f);
        const float GroundProximityFactor = std::pow(h_ref / std::max(h, 0.005f), Splitter.RideHeightSensitivity * 0.08f);
        const float GroundProximityFactorClamped = Clampf(GroundProximityFactor, 0.6f, 2.8f);
        Forces.SplitterDownforce_N = q * Splitter.Area_m2 * Splitter.CoeffPressure * GroundProximityFactorClamped * SideslipDownforceFactor;
        Forces.SplitterDrag_N = q * (Splitter.AirDamHeight_m * 1.8f) * Splitter.AirDamDragCoeff *
                                Clampf(1.0f + 0.15f * YawRate_rads * YawRate_rads, 1.0f, 1.2f);
    }

    //------------------------------------------------------------------ UNDERBODY / DIFFUSER (Bernoulli venturi + expansion)
    if (Aero.FloorDiffuser.Enabled)
    {
        const AeroUnderbody& Under = Aero.FloorDiffuser;
        const float DiffuserAngleRad = Deg2Rad(Under.DiffuserAngle_deg);
        const float h_optimal_theory = 0.7f * DiffuserAngleRad * Under.DiffuserArea_m2 / 1.5f;
        const float h_opt  = std::max(Under.RideHeightOptimum_m, h_optimal_theory * 0.5f);
        const float h_crit = Under.RideHeightCritical_m;

        float RideHeightFactor = 1.0f;
        if (h > h_opt)
        {
            RideHeightFactor = std::pow(h_opt / h, 0.6f);
            RideHeightFactor = Clampf(RideHeightFactor, 0.4f, 1.0f);
        }
        else if (h < h_crit)
        {
            RideHeightFactor = (h / h_crit) * (h / h_crit);
            RideHeightFactor = Clampf(RideHeightFactor, 0.15f, 1.0f);
        }
        else
        {
            const float t = (h - h_crit) / (h_opt - h_crit + 0.001f);
            RideHeightFactor = Lerpf(0.65f, 1.0f, t);
        }
        Forces.UnderbodyRideHeightFactor = RideHeightFactor;

        // Floor downforce from Bernoulli: Cp = 1 − (u/u∞)²
        const float VelocityRatio = 1.5f;
        const float Cp_floor = 1.0f - (VelocityRatio * VelocityRatio);
        Forces.UnderbodyDownforce_N = q * Under.FloorArea_m2 * std::fabs(Cp_floor) * RideHeightFactor;

        // Diffuser pressure recovery (Cooper et al. 1998)
        const float ExpansionRatio = Under.DiffuserArea_m2 / (Under.FloorArea_m2 * h / h_opt);
        const float ExpansionRatioClamped = Clampf(ExpansionRatio, 0.8f, 2.5f);
        const float DiffuserCp = std::sin(DiffuserAngleRad) * Under.DiffuserEfficiency * ExpansionRatioClamped * 0.8f;
        Forces.UnderbodyDownforce_N += q * Under.DiffuserArea_m2 * DiffuserCp * RideHeightFactor;

        // Vortex generator enhancement
        if (Aero.VortexGens.Enabled && (Aero.VortexGens.EnhancesDiffuser || Aero.VortexGens.EnhancesUnderbody))
        {
            Forces.VortexGenDownforceBonus_N = Forces.UnderbodyDownforce_N *
                (Aero.VortexGens.DiffuserEnhancementFactor - 1.0f) * Aero.VortexGens.VortexStrength;
            Forces.UnderbodyDownforce_N += Forces.VortexGenDownforceBonus_N;
        }
        Forces.UnderbodyDrag_N = q * Under.DiffuserArea_m2 * Under.DiffuserDragCoeff;
    }

    //------------------------------------------------------------------ SIDE SKIRTS (ground-effect sealing)
    {
        const float h_ref = 0.10f;
        const float BlockageRatio = h / h_ref;
        auto ComputeSideSkirt = [&](const AeroSideSkirt& Skirt, float& OutDownforce_N, float& OutDrag_N)
        {
            if (!Skirt.Enabled) { OutDownforce_N = 0.0f; OutDrag_N = 0.0f; return; }
            const float DamageFactor = 1.0f - Skirt.DamageLevel;
            const float GroundProximityFactor = 1.0f / std::pow(BlockageRatio, Skirt.RideHeightSensitivity * 0.12f);
            const float GroundProximityFactorClamped = Clampf(GroundProximityFactor, 0.4f, 2.8f) * DamageFactor;
            const float SkirtArea_m2 = Skirt.Length_m * Skirt.Height_m;
            const float Cp_effective = std::fabs(Skirt.CoeffPressure) * GroundProximityFactorClamped * Skirt.UnderbodySealingEfficiency;
            OutDownforce_N = q * SkirtArea_m2 * Cp_effective * SideslipDownforceFactor;
            OutDrag_N      = q * SkirtArea_m2 * Skirt.DragCoeff * DamageFactor;
        };
        ComputeSideSkirt(Aero.LeftSideSkirt,  Forces.LeftSideSkirtDownforce_N,  Forces.LeftSideSkirtDrag_N);
        ComputeSideSkirt(Aero.RightSideSkirt, Forces.RightSideSkirtDownforce_N, Forces.RightSideSkirtDrag_N);
    }

    //------------------------------------------------------------------ VORTEX GENERATORS (drag penalty)
    if (Aero.VortexGens.Enabled)
    {
        Forces.VortexGenDrag_N = q * Aero.VortexGens.TotalArea_m2 * Aero.VortexGens.DragPenalty;
    }

    //------------------------------------------------------------------ BODY AERO (3D component drag)
    Vec3 BodyResistanceWorld{};
    if (Aero.VehicleBody.Enabled)
    {
        const AeroBody& Body = Aero.VehicleBody;
        // Body-axis projections keep drag correct while yawed.
        const float BodyLongitudinalDrag_N = -0.5f * Rho * Vx * std::fabs(Vx) * Body.FrontalArea_m2 * Body.CoeffDrag;
        const float BodyLateralForce_N     = -0.5f * Rho * Vy * std::fabs(Vy) * Body.SideArea_m2   * Body.CoeffSideForce;
        const float BodyVerticalDrag_N     = -0.5f * Rho * Vz * std::fabs(Vz) * Body.FrontalArea_m2 * (Body.CoeffDrag * 0.35f);

        BodyResistanceWorld = BodyForward * BodyLongitudinalDrag_N + BodyUp * BodyVerticalDrag_N;
        Forces.BodyDrag_N   = BodyResistanceWorld.Length();
        Forces.BodyLift_N   = q * Body.FrontalArea_m2 * Body.CoeffLift;
        Forces.SideForce_N  = BodyLateralForce_N;
    }

    //------------------------------------------------------------------ AGGREGATE
    const float ScalarDrag_N = Forces.WingDrag_N + Forces.TotalCanardDrag_N + Forces.SplitterDrag_N +
                               Forces.UnderbodyDrag_N + Forces.LeftSideSkirtDrag_N + Forces.RightSideSkirtDrag_N +
                               Forces.VortexGenDrag_N;
    Forces.TotalDownforce_N = Forces.WingDownforce_N + Forces.TotalCanardDownforce_N + Forces.SplitterDownforce_N +
                              Forces.UnderbodyDownforce_N + Forces.LeftSideSkirtDownforce_N + Forces.RightSideSkirtDownforce_N -
                              Forces.BodyLift_N;
    Forces.FrontDownforce_N = Forces.TotalCanardDownforce_N + Forces.SplitterDownforce_N + Forces.UnderbodyDownforce_N * 0.45f;
    Forces.RearDownforce_N  = Forces.WingDownforce_N + Forces.UnderbodyDownforce_N * 0.55f;

    //------------------------------------------------------------------ FORCE VECTORS (world)
    Forces.DragForceWorld = (VelocityDir_World * (-ScalarDrag_N)) + BodyResistanceWorld;
    Forces.TotalDrag_N    = Forces.DragForceWorld.Length();
    Forces.LiftForceWorld = BodyUp * (-Forces.TotalDownforce_N);
    Forces.SideForceWorld = BodyRight * Forces.SideForce_N;

    //------------------------------------------------------------------ MOMENTS (about CoM)
    Forces.PitchMoment_Nm = 0.0f;
    if (Aero.RearWing.Enabled)      { Forces.PitchMoment_Nm += (-Forces.WingDownforce_N)      * Aero.RearWing.ForceApplicationPoint_COM.x; }
    if (Aero.FrontSplitter.Enabled) { Forces.PitchMoment_Nm += (-Forces.SplitterDownforce_N) * Aero.FrontSplitter.ForceApplicationPoint_COM.x; }
    if (Aero.FloorDiffuser.Enabled) { Forces.PitchMoment_Nm += (-Forces.UnderbodyDownforce_N)* Aero.FloorDiffuser.ForceApplicationPoint_COM.x; }
    if (!Aero.Canards.empty())
    {
        const float perCanard = Forces.TotalCanardDownforce_N / static_cast<float>(std::max<size_t>(Aero.Canards.size(), 1));
        for (const AeroCanard& c : Aero.Canards)
        {
            if (!c.Enabled) { continue; }
            Forces.PitchMoment_Nm += (-perCanard) * c.ForceApplicationPoint_COM.x;
        }
    }
    if (Aero.VehicleBody.Enabled)
    {
        Forces.YawMoment_Nm  = Forces.SideForce_N * Aero.VehicleBody.ForceApplicationPoint_COM.x;
        Forces.RollMoment_Nm = Forces.SideForce_N * Aero.VehicleBody.ForceApplicationPoint_COM.z;
    }

    Forces.AeroEfficiency = (Forces.TotalDrag_N > 1.0f) ? (Forces.TotalDownforce_N / Forces.TotalDrag_N) : 0.0f;
    return Forces;
}

} // namespace Frontier::Vehicle
