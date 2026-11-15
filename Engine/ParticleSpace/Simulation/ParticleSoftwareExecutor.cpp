/*============================================================================================================================================
                                                         PARTICLESOFTWAREEXECUTOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of modular CPU particle executor dispatching active dynamics bitmask, ForceFieldAccumulator & ColourSpace tinting

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"

#include "Engine/ParticleSpace/Dynamics/CurlManifoldDynamics.h"
#include "Engine/ParticleSpace/Dynamics/DynamicStrandSolver.h"
#include "Engine/ParticleSpace/Dynamics/EffectorGridSolver.h"
#include "Engine/ParticleSpace/Dynamics/FluidGranularDynamics.h"
#include "Engine/ParticleSpace/Dynamics/MolecularReactionDynamics.h"
#include "Engine/ParticleSpace/Dynamics/OrbitalElectromagneticDynamics.h"
#include "Engine/ParticleSpace/Dynamics/SpecialEffectsDynamics.h"
#include "Engine/ParticleSpace/Dynamics/SplineEnsembleSolver.h"
#include "Engine/ParticleSpace/Dynamics/SwarmFlockingDynamics.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <algorithm>
#include <cmath>

namespace Frontier::ParticleSpace
{

namespace
{

constexpr float DegToRad = 3.14159265358979323846f / 180.0f;

void ExtractOriginCenter(const ReleaseOriginSpec& Origin, float OutCenter[3]) noexcept
{
    OutCenter[0] = 0.0f; OutCenter[1] = 0.0f; OutCenter[2] = 0.0f;
    if (const auto* Pt = std::get_if<AnalyticPointOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Pt->CenterMeters[K];
    else if (const auto* Sph = std::get_if<AnalyticSphereOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Sph->CenterMeters[K];
    else if (const auto* Disc = std::get_if<AnalyticDiscOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Disc->CenterMeters[K];
    else if (const auto* Box = std::get_if<AnalyticBoxOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Box->CenterMeters[K];
    else if (const auto* Surf = std::get_if<SurfaceAnchorOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Surf->CenterMeters[K];
    else if (const auto* Fw = std::get_if<FireworkOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Fw->ShellOrigin[K];
    else if (const auto* Lat = std::get_if<SurfaceLatticeOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Lat->CenterMeters[K];
    else if (const auto* Web = std::get_if<SpiderwebOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Web->CenterMeters[K];
    else if (const auto* Cloth = std::get_if<ClothGridOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Cloth->OriginMeters[K];
    else if (const auto* Elec = std::get_if<DualElectrodeOrigin>(&Origin))
        for (int K = 0; K < 3; ++K) OutCenter[K] = Elec->CathodeMeters[K];
}

} // namespace

uint32_t ExecuteParticleRigStepCpu(
    const ParticleRig&         Rig,
    const ParticleStepContext& Context,
    std::span<ParticleSlot>    RigSlots,
    std::span<StrandVertex>    OptionalStrandHistory)
{
    const float DtSec = std::clamp(Context.DeltaSeconds, 0.0f, 0.05f);
    if (!Rig.Visible || RigSlots.empty() || DtSec <= 0.0f) return 0u;

    float OriginCenter[3] {};
    ExtractOriginCenter(Rig.Origin, OriginCenter);

    // 1. Pre-age & expire dead particles
    for (auto& Slot : RigSlots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;
        Slot.AgeSeconds += DtSec;
        if (Slot.AgeSeconds >= Slot.LifespanSeconds)
        {
            Slot.PackedTraits &= ~ParticleTraitAliveBit;
        }
    }

    // 2. Dispatch ONLY the active modular dynamics subsystems composed into Rig.Dynamics!
    const auto& D = Rig.Dynamics;
    if (D.Curl.has_value() && !std::holds_alternative<RibbonStrandCarrier>(Rig.Carrier))
        StepCurlManifoldDynamics(*D.Curl, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Orbital.has_value())     StepOrbitalGravityDynamics(*D.Orbital, OriginCenter, RigSlots, DtSec);
    if (D.Electro.has_value())     StepElectromagneticDynamics(*D.Electro, OriginCenter, RigSlots, DtSec);
    if (D.Harmonic.has_value())    StepHarmonicWaveDynamics(*D.Harmonic, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Attractor.has_value())   StepStrangeAttractorDynamics(*D.Attractor, OriginCenter, RigSlots, DtSec);
    if (D.Swarm.has_value())       StepSwarmFlockingDynamics(*D.Swarm, OriginCenter, RigSlots, DtSec);
    if (D.SpeciesLife.has_value()) StepSpeciesLifeDynamics(*D.SpeciesLife, OriginCenter, RigSlots, DtSec);
    if (D.BotanicalGrowth.has_value())      StepOrganicGrowthDynamics(*D.BotanicalGrowth, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.FluidSph.has_value())    StepFluidSphDynamics(*D.FluidSph, OriginCenter, RigSlots, DtSec);
    if (D.Thermal.has_value())     StepThermalFireworkDynamics(*D.Thermal, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Molecular.has_value())   StepMolecularReactionDynamics(*D.Molecular, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Splash.has_value())      StepShallowWaterSplashDynamics(*D.Splash, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Transition.has_value())  StepTransitionDerezDynamics(*D.Transition, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);

    // 3. Ambient wind vector
    float AmbientV[3] = { 0.0f, 0.0f, 0.0f };
    if (Context.Document.AmbientWind.Enabled)
    {
        const float Az   = Context.Document.AmbientWind.AzimuthDegrees * DegToRad;
        const float Gust = 1.0f + Context.Document.AmbientWind.GustAmplitude * std::sin(Context.ElapsedSeconds * 1.7f);
        AmbientV[0] = std::cos(Az) * Context.Document.AmbientWind.SpeedMps * Gust;
        AmbientV[2] = std::sin(Az) * Context.Document.AmbientWind.SpeedMps * Gust;
    }

    // Dedicated 20-vertex dynamic strand solver (Cables/Chains, Leafy Vines, Flags, Ship Sails, Beaded Curtains)
    if (D.Strand.has_value())
    {
        StepDynamicStrandEnsemble(
            Rig,
            *D.Strand,
            OriginCenter,
            AmbientV,
            Context.ForceAccumulator,
            Context.Document.Collider,
            RigSlots,
            OptionalStrandHistory,
            Context.ElapsedSeconds,
            DtSec);
        uint32_t AliveStrands = 0u;
        for (const auto& Slot : RigSlots)
        {
            if (QueryParticleSlotAlive(Slot)) ++AliveStrands;
        }
        return AliveStrands;
    }

    // Dedicated Curl Stream Ribbon & Bokeh Object Surface Pour solver (curlMode 1, 2, 3 with RibbonStrandCarrier)
    if (D.Curl.has_value() && std::holds_alternative<RibbonStrandCarrier>(Rig.Carrier) && !OptionalStrandHistory.empty())
    {
        StepCurlStreamRibbonEnsemble(
            Rig,
            *D.Curl,
            OriginCenter,
            AmbientV,
            Context.ForceAccumulator,
            RigSlots,
            OptionalStrandHistory,
            Context.ElapsedSeconds,
            DtSec);
        uint32_t AliveCount = 0u;
        for (const auto& Slot : RigSlots)
        {
            if (QueryParticleSlotAlive(Slot)) ++AliveCount;
        }
        return AliveCount;
    }

    // Dedicated 20-vertex Spiderweb Catenary Spline solver (Orb-Weave & Messy Attic Cobwebs)
    if (D.Spiderweb.has_value() && std::holds_alternative<RibbonStrandCarrier>(Rig.Carrier) && !OptionalStrandHistory.empty())
    {
        StepSpiderwebSplineEnsemble(
            Rig,
            *D.Spiderweb,
            OriginCenter,
            AmbientV,
            Context.ForceAccumulator,
            Context.Document.Collider,
            RigSlots,
            OptionalStrandHistory,
            Context.ElapsedSeconds,
            DtSec);
        uint32_t AliveCount = 0u;
        for (const auto& Slot : RigSlots)
        {
            if (QueryParticleSlotAlive(Slot)) ++AliveCount;
        }
        return AliveCount;
    }

    // Dedicated Dielectric Lightning / Plasma Cage / Storm Web ionization spark solver
    if (D.DielectricArc.has_value() && std::holds_alternative<DualElectrodeOrigin>(Rig.Origin))
    {
        const auto& Elec = std::get<DualElectrodeOrigin>(Rig.Origin);
        StepDielectricSparkEnsemble(
            Rig,
            *D.DielectricArc,
            Elec.CathodeMeters,
            Elec.AnodeMeters,
            RigSlots,
            Context.ElapsedSeconds,
            DtSec);
        uint32_t AliveCount = 0u;
        for (const auto& Slot : RigSlots)
        {
            if (QueryParticleSlotAlive(Slot)) ++AliveCount;
        }
        return AliveCount;
    }

    // Dedicated Motion & Grids Effector solver (Kinetic Hex Torus & Control Vehicle, Obsidian Halftone & Tiles, Height Cubes, Placeable 3D Effectors)
    if (D.Effector.has_value())
    {
        StepEffectorGridEnsemble(
            Rig,
            *D.Effector,
            OriginCenter,
            Context.ForceAccumulator,
            Context.Document.Collider,
            {},
            RigSlots,
            Context.ElapsedSeconds);
        uint32_t AliveCount = 0u;
        for (const auto& Slot : RigSlots)
        {
            if (QueryParticleSlotAlive(Slot)) ++AliveCount;
        }
        return AliveCount;
    }

    const float GravityY = -9.81f * D.Ballistic.GravityScale + D.Ballistic.BuoyancyMps2;
    const float DragMult = std::exp(-std::max(0.0f, D.Ballistic.AerodynamicDrag) * DtSec);
    const float WindRate = std::max(0.0f, D.ForceCouple.WindCouplingRate);

    const bool OverrideKinematicPos = D.DielectricArc.has_value()
                                   || D.FieldMatrix.has_value()
                                   || D.Botanical.has_value()
                                   || D.Ferrofluid.has_value()
                                   || D.Fracture.has_value()
                                   || D.Shield.has_value()
                                   || D.Spiderweb.has_value()
                                   || D.Effector.has_value();

    for (auto& Slot : RigSlots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;

        if (!OverrideKinematicPos)
        {
            const SimulationSpace::ForceFields::SpatialVector3 Pt  { Slot.PositionMeters[0], Slot.PositionMeters[1], Slot.PositionMeters[2] };
            const SimulationSpace::ForceFields::SpatialVector3 Vel { Slot.VelocityMps[0], Slot.VelocityMps[1], Slot.VelocityMps[2] };
            const auto ForceSample = Context.ForceAccumulator.QueryForceFieldSample(Pt, Vel, static_cast<double>(Context.ElapsedSeconds));

            Slot.VelocityMps[0] += ForceSample.LinearAcceleration.X * DtSec;
            Slot.VelocityMps[1] += (GravityY + ForceSample.LinearAcceleration.Y) * DtSec;
            Slot.VelocityMps[2] += ForceSample.LinearAcceleration.Z * DtSec;

            if (WindRate > 1e-4f)
            {
                const float Blend = 1.0f - std::exp(-WindRate * DtSec);
                Slot.VelocityMps[0] += ((AmbientV[0] + ForceSample.AdvectionVelocity.X) - Slot.VelocityMps[0]) * Blend;
                Slot.VelocityMps[1] += ((AmbientV[1] + ForceSample.AdvectionVelocity.Y) - Slot.VelocityMps[1]) * Blend;
                Slot.VelocityMps[2] += ((AmbientV[2] + ForceSample.AdvectionVelocity.Z) - Slot.VelocityMps[2]) * Blend;
            }

            if (D.Ballistic.FlutterAmplitude > 1e-4f)
            {
                const float FPhase = Context.ElapsedSeconds * 5.2f + Slot.SeedScalar * 31.4f;
                Slot.VelocityMps[0] += std::sin(FPhase) * D.Ballistic.FlutterAmplitude * DtSec;
                Slot.VelocityMps[2] += std::cos(FPhase * 1.13f) * D.Ballistic.FlutterAmplitude * DtSec;
            }

            const float TotalDamp = DragMult * std::exp(-std::max(0.0f, ForceSample.DragDecayRate) * DtSec);
            Slot.VelocityMps[0] *= TotalDamp;
            Slot.VelocityMps[1] *= TotalDamp;
            Slot.VelocityMps[2] *= TotalDamp;

            Slot.PositionMeters[0] += Slot.VelocityMps[0] * DtSec;
            Slot.PositionMeters[1] += Slot.VelocityMps[1] * DtSec;
            Slot.PositionMeters[2] += Slot.VelocityMps[2] * DtSec;
        }
    }

    // 4. Post-advance positional constraints (Granular, Strands, Arcs, Field Matrix, Botanical, Ferrofluid, Fracture, Shield, Spiderweb, Effectors)
    if (D.Granular.has_value())      StepGranularCollisionDynamics(*D.Granular, OriginCenter, RigSlots, DtSec);
    if (D.ElasticStrand.has_value()) StepElasticStrandDynamics(*D.ElasticStrand, RigSlots, DtSec);
    if (D.DielectricArc.has_value()) StepDielectricArcDynamics(*D.DielectricArc, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.FieldMatrix.has_value())   StepFieldMatrixDynamics(*D.FieldMatrix, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Botanical.has_value())     StepBotanicalSwayDynamics(*D.Botanical, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Ferrofluid.has_value())    StepFerrofluidMeniscusDynamics(*D.Ferrofluid, OriginCenter, Context.Document.Collider, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Fracture.has_value())      StepFractureFissureDynamics(*D.Fracture, OriginCenter, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Shield.has_value())        StepShieldBarrierDynamics(*D.Shield, OriginCenter, Context.Document.Collider, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Spiderweb.has_value())     StepSpiderwebWeaveDynamics(*D.Spiderweb, RigSlots, Context.ElapsedSeconds, DtSec);
    if (D.Effector.has_value())      StepEffectorLatticeDynamics(*D.Effector, OriginCenter, Context.Document.Collider, RigSlots, Context.ElapsedSeconds, DtSec);

    // 5. Floor bounce, interactive sphere collider, size curve, ribbon trail shift & ColourSpace tinting
    const uint32_t StrandVerts = [&]() -> uint32_t {
        if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        {
            return std::clamp(Rib->VerticesPerStrand, 2u, 32u);
        }
        return 0u;
    }();

    ParticleTintEvaluationContext TintCtx {};
    TintCtx.ElapsedSeconds = Context.ElapsedSeconds;

    uint32_t LiveCount = 0u;
    for (std::size_t I = 0u; I < RigSlots.size(); ++I)
    {
        ParticleSlot& Slot = RigSlots[I];
        if (!QueryParticleSlotAlive(Slot)) continue;

        // Floor bounce
        if (D.Ballistic.FloorRestitution > 0.0f && Slot.PositionMeters[1] < D.Ballistic.FloorElevation)
        {
            Slot.PositionMeters[1] = D.Ballistic.FloorElevation;
            if (Slot.VelocityMps[1] < 0.0f)
            {
                Slot.VelocityMps[1] = -Slot.VelocityMps[1] * D.Ballistic.FloorRestitution;
                Slot.VelocityMps[0] *= 0.88f;
                Slot.VelocityMps[2] *= 0.88f;
            }
        }

        // Interactive chrome sphere collider
        const auto& Coll = Context.Document.Collider;
        if (Coll.BallActive)
        {
            const float Dx = Slot.PositionMeters[0] - Coll.BallCenterM[0];
            const float Dy = Slot.PositionMeters[1] - Coll.BallCenterM[1];
            const float Dz = Slot.PositionMeters[2] - Coll.BallCenterM[2];
            const float DistSq = Dx * Dx + Dy * Dy + Dz * Dz;
            const float MinR   = Coll.BallRadiusMeters + Slot.SizeMeters;
            if (DistSq > 1e-8f && DistSq < MinR * MinR)
            {
                const float Dist = std::sqrt(DistSq);
                const float Nx = Dx / Dist, Ny = Dy / Dist, Nz = Dz / Dist;
                Slot.PositionMeters[0] = Coll.BallCenterM[0] + Nx * MinR;
                Slot.PositionMeters[1] = Coll.BallCenterM[1] + Ny * MinR;
                Slot.PositionMeters[2] = Coll.BallCenterM[2] + Nz * MinR;
                const float RelVn = (Slot.VelocityMps[0] - Coll.BallVelocityMps[0]) * Nx
                                  + (Slot.VelocityMps[1] - Coll.BallVelocityMps[1]) * Ny
                                  + (Slot.VelocityMps[2] - Coll.BallVelocityMps[2]) * Nz;
                if (RelVn < 0.0f)
                {
                    const float Imp = -(1.0f + Coll.BallRestitution) * RelVn;
                    Slot.VelocityMps[0] += Nx * Imp;
                    Slot.VelocityMps[1] += Ny * Imp;
                    Slot.VelocityMps[2] += Nz * Imp;
                }
            }
        }

        // PhysicsSpace rigid-body collision proxies (Sphere, Oriented Box, Oriented +Z Capsule + two-way dynamic impulse feedback)
        if (!Context.RigidColliders.empty())
        {
            const auto Feedback = SimulationSpace::PhysicsSpace::BroadphaseSolver::ResolveParticleAgainstProxies(
                Context.RigidColliders,
                Slot.SizeMeters,
                Context.ParticleMassKg,
                Slot.PositionMeters,
                Slot.VelocityMps);
            if (Feedback.Collided && Feedback.DynamicResponse != 0u && Context.PhysicsRuntime != nullptr)
            {
                const Mathematics::Axis3D ReactionImpulse {
                    -Feedback.ParticleImpulseNs.X,
                    -Feedback.ParticleImpulseNs.Y,
                    -Feedback.ParticleImpulseNs.Z
                };
                Context.PhysicsRuntime->ApplyParticleImpactImpulse(
                    Feedback.BodySlotIndex,
                    Feedback.ContactPoint,
                    ReactionImpulse);
            }
        }

        // Ribbon trail history shift when RibbonStrandCarrier is active
        if (StrandVerts >= 2u && OptionalStrandHistory.size() >= (I + 1u) * StrandVerts)
        {
            const std::size_t StrandStart = I * StrandVerts;
            for (uint32_t V = StrandVerts - 1u; V > 0u; --V)
            {
                OptionalStrandHistory[StrandStart + V].PositionMeters[0] = OptionalStrandHistory[StrandStart + V - 1u].PositionMeters[0];
                OptionalStrandHistory[StrandStart + V].PositionMeters[1] = OptionalStrandHistory[StrandStart + V - 1u].PositionMeters[1];
                OptionalStrandHistory[StrandStart + V].PositionMeters[2] = OptionalStrandHistory[StrandStart + V - 1u].PositionMeters[2];
                OptionalStrandHistory[StrandStart + V].NormalisedSpanU   = static_cast<float>(V) / static_cast<float>(StrandVerts - 1u);
            }
            OptionalStrandHistory[StrandStart].PositionMeters[0] = Slot.PositionMeters[0];
            OptionalStrandHistory[StrandStart].PositionMeters[1] = Slot.PositionMeters[1];
            OptionalStrandHistory[StrandStart].PositionMeters[2] = Slot.PositionMeters[2];
            OptionalStrandHistory[StrandStart].NormalisedSpanU   = 0.0f;
        }

        // Size progression + ColourSpace tint evaluation
        const float NormT = std::clamp(Slot.AgeSeconds / std::max(1e-5f, Slot.LifespanSeconds), 0.0f, 1.0f);
        Slot.SizeMeters   = (Rig.Kinematics.SizeBirthMeters + (Rig.Kinematics.SizeDeathMeters - Rig.Kinematics.SizeBirthMeters) * NormT) * Rig.Kinematics.SizeMultiplier;
        Slot.SpinRadians += 1.4f * DtSec;

        TintCtx.OriginMeters[0] = Slot.OriginAnchor[0];
        TintCtx.OriginMeters[1] = Slot.OriginAnchor[1];
        TintCtx.OriginMeters[2] = Slot.OriginAnchor[2];
        const ColourSpace::LinearRgba Tint = EvaluateParticleTint(Rig.TintDriver, Rig.ColourProfile, Slot, TintCtx);
        Slot.ColourLinear[0] = Tint.Red;
        Slot.ColourLinear[1] = Tint.Green;
        Slot.ColourLinear[2] = Tint.Blue;
        Slot.ColourLinear[3] = Tint.Alpha;

        ++LiveCount;
    }

    return LiveCount;
}

} // namespace Frontier::ParticleSpace
