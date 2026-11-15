/*============================================================================================================================================
                                                      PARTICLESIMULATIONCOORDINATOR.CPP
============================================================================================================================================*/
// 🧩 Implementation of ParticleSimulationCoordinator delegating simulation to modular ParticleSoftwareExecutor + ForceFieldAccumulator + ColourAssetCoordinator

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/ParticleSimulationCoordinator.h"

#include "Engine/ParticleSpace/Origins/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/Origins/SurfaceAnchorSampler.h"
#include "Engine/ParticleSpace/Simulation/ParticleSoftwareExecutor.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Frontier::ParticleSpace
{

bool ParticleSimulationCoordinator::Initialize(const ParticleDocument& InitialDocument)
{
    (void)ColourCoordinator.Initialize();
    Document    = InitialDocument;
    Initialized = true;
    Suspended   = false;
    ResetSimulation();
    return true;
}

void ParticleSimulationCoordinator::Suspend() noexcept
{
    Suspended = true;
}

void ParticleSimulationCoordinator::Resume() noexcept
{
    Suspended = false;
}

void ParticleSimulationCoordinator::Finalize() noexcept
{
    ParticleSlots.clear();
    StrandVertices.clear();
    AnchorBuffer.clear();
    RigidColliderCache.clear();
    RigSlices.clear();
    AttachedPhysics = nullptr;
    ColourCoordinator.Finalize();
    Initialized = false;
    Suspended   = false;
}

void ParticleSimulationCoordinator::AttachPhysicsExtension(SimulationSpace::PhysicsSpace::PhysicsExtension* Extension) noexcept
{
    AttachedPhysics = Extension;
    RigidColliderCache.clear();
}

void ParticleSimulationCoordinator::LoadDocument(const ParticleDocument& NextDocument)
{
    Document = NextDocument;
    if (!Initialized)
    {
        (void)Initialize(NextDocument);
        return;
    }
    ResetSimulation();
}

void ParticleSimulationCoordinator::ResetSimulation()
{
    Rng = DeterministicPcgRng(Document.Seed);
    Document.Clock.ElapsedSeconds = 0.0;
    Telemetry = {};
    ForceAccumulator.AssignSources(Document.ForceSources);
    RebuildRuntimeAllocations();
}

void ParticleSimulationCoordinator::RebuildRuntimeAllocations()
{
    RigSlices.clear();
    AnchorBuffer.clear();
    ParticleSlots.clear();
    StrandVertices.clear();

    uint32_t RunningSlotOffset   = 0u;
    uint32_t RunningStrandOffset = 0u;
    RigSlices.reserve(Document.Rigs.size());

    for (std::size_t I = 0u; I < Document.Rigs.size(); ++I)
    {
        const ParticleRig& Rig = Document.Rigs[I];
        RigRuntimeSlice Slice {};
        Slice.SlotOffset   = RunningSlotOffset;
        Slice.SlotCapacity = Rig.SlotCapacity;
        Slice.AnchorOffset = static_cast<uint32_t>(AnchorBuffer.size());
        Slice.StrandOffset = RunningStrandOffset;

        if (const auto* Rib = std::get_if<RibbonStrandCarrier>(&Rig.Carrier))
        {
            const uint32_t VPer = std::clamp(Rib->VerticesPerStrand, 2u, 32u);
            Slice.StrandVertexCount = Rig.SlotCapacity * VPer;
            RunningStrandOffset    += Slice.StrandVertexCount;
        }

        const uint32_t AnchorTargetCount = std::min(Rig.SlotCapacity, 1024u);
        if (const auto* Surf = std::get_if<SurfaceAnchorOrigin>(&Rig.Origin))
        {
            auto Anchors = SampleProceduralSurfaceAnchors(*Surf, AnchorTargetCount, Rig.LocalSeed);
            AnchorBuffer.insert(AnchorBuffer.end(), Anchors.begin(), Anchors.end());
        }
        else if (const auto* Fw = std::get_if<FireworkOrigin>(&Rig.Origin))
        {
            auto Anchors = SampleFireworkPatternAnchors(*Fw, AnchorTargetCount, 0u, Rig.LocalSeed);
            AnchorBuffer.insert(AnchorBuffer.end(), Anchors.begin(), Anchors.end());
        }
        else if (std::holds_alternative<SurfaceLatticeOrigin>(Rig.Origin)
              || std::holds_alternative<SpiderwebOrigin>(Rig.Origin)
              || std::holds_alternative<ClothGridOrigin>(Rig.Origin)
              || std::holds_alternative<BotanicalRootOrigin>(Rig.Origin))
        {
            auto Anchors = BuildStructuralOriginAnchors(Rig.Origin, AnchorTargetCount, Rig.LocalSeed);
            AnchorBuffer.insert(AnchorBuffer.end(), Anchors.begin(), Anchors.end());
        }

        Slice.AnchorCount = static_cast<uint32_t>(AnchorBuffer.size()) - Slice.AnchorOffset;
        RunningSlotOffset += Rig.SlotCapacity;
        RigSlices.push_back(Slice);
    }

    ParticleSlots.resize(RunningSlotOffset);
    StrandVertices.resize(RunningStrandOffset);
    for (auto& Slot : ParticleSlots)
    {
        Slot.AgeSeconds   = -1.0f;
        Slot.PackedTraits = 0u;
    }
    Telemetry.TotalSlotCapacity = RunningSlotOffset;
    Telemetry.ActiveForceCount  = ForceAccumulator.QueryActiveSourceCount();
}

void ParticleSimulationCoordinator::SpawnSingleSlot(std::size_t RigIdx, RigRuntimeSlice& Slice)
{
    if (Slice.SlotCapacity == 0u) return;
    const ParticleRig& Rig = Document.Rigs[RigIdx];

    const uint32_t LocalSlot = Slice.SpawnCursor % Slice.SlotCapacity;
    Slice.SpawnCursor = (Slice.SpawnCursor + 1u) % Slice.SlotCapacity;
    ParticleSlot& Slot = ParticleSlots[Slice.SlotOffset + LocalSlot];

    OriginSpawnPoint Spawn {};
    if (Slice.AnchorCount > 0u)
    {
        // Preserve exact deterministic topological ordering for structural grids, webs, cloth, stalks & firework glyphs
        const bool OrderedTopology = std::holds_alternative<SurfaceLatticeOrigin>(Rig.Origin)
                                  || std::holds_alternative<SpiderwebOrigin>(Rig.Origin)
                                  || std::holds_alternative<ClothGridOrigin>(Rig.Origin)
                                  || std::holds_alternative<BotanicalRootOrigin>(Rig.Origin)
                                  || std::holds_alternative<FireworkOrigin>(Rig.Origin);
        const uint32_t LocalAnchor = OrderedTopology
            ? (LocalSlot % Slice.AnchorCount)
            : (Rng.NextUint32() % Slice.AnchorCount);
        const uint32_t AIdx = Slice.AnchorOffset + LocalAnchor;
        const ParticleAnchorSample& Anc = AnchorBuffer[AIdx];
        for (int K = 0; K < 3; ++K)
        {
            Spawn.PositionMeters[K] = Anc.PositionMeters[K];
            Spawn.NormalUnit[K]     = Anc.NormalUnit[K];
        }
    }
    else
    {
        Spawn = SampleAnalyticOrigin(Rig.Origin, Rng);
    }

    for (int K = 0; K < 3; ++K)
    {
        Slot.PositionMeters[K] = Spawn.PositionMeters[K];
        Slot.OriginAnchor[K]   = Spawn.PositionMeters[K];
    }

    SampleConeVelocity(
        Rig.Kinematics.LaunchDirection,
        Rig.Kinematics.SpreadRadians,
        Rig.Kinematics.SpeedMinMps,
        Rig.Kinematics.SpeedMaxMps,
        Rng,
        Slot.VelocityMps);

    Slot.AgeSeconds      = 0.0f;
    Slot.LifespanSeconds = std::max(0.05f, Rng.NextRange(Rig.Kinematics.LifeMinSeconds, std::max(Rig.Kinematics.LifeMinSeconds, Rig.Kinematics.LifeMaxSeconds)));
    Slot.SizeMeters      = Rig.Kinematics.SizeBirthMeters * Rig.Kinematics.SizeMultiplier;
    Slot.SeedScalar      = Rng.NextUnitFloat();
    Slot.SpinRadians     = Rng.NextUnitFloat() * 6.2831853f;
    Slot.AuxScalar       = 0.0f;
    Slot.PackedTraits    = ParticleTraitAliveBit | ((static_cast<uint32_t>(RigIdx) & 0xFFu) << 8u);

    // Initialize ribbon history chain at spawn position so newly born ribbons don't stretch from origin (0,0,0)
    if (Slice.StrandVertexCount > 0u)
    {
        const uint32_t VPer = Slice.StrandVertexCount / Slice.SlotCapacity;
        const std::size_t StrandStart = static_cast<std::size_t>(Slice.StrandOffset) + static_cast<std::size_t>(LocalSlot) * VPer;
        for (uint32_t V = 0u; V < VPer && (StrandStart + V) < StrandVertices.size(); ++V)
        {
            for (int K = 0; K < 3; ++K) StrandVertices[StrandStart + V].PositionMeters[K] = Slot.PositionMeters[K];
            StrandVertices[StrandStart + V].NormalisedSpanU = (VPer > 1u) ? (static_cast<float>(V) / static_cast<float>(VPer - 1u)) : 0.0f;
        }
    }

    ParticleTintEvaluationContext TintCtx {};
    TintCtx.OriginMeters[0] = Spawn.PositionMeters[0];
    TintCtx.OriginMeters[1] = Spawn.PositionMeters[1];
    TintCtx.OriginMeters[2] = Spawn.PositionMeters[2];
    TintCtx.ElapsedSeconds  = static_cast<float>(Document.Clock.ElapsedSeconds);

    const ColourSpace::LinearRgba Tint = EvaluateParticleTint(Rig.TintDriver, Rig.ColourProfile, Slot, TintCtx);
    Slot.ColourLinear[0] = Tint.Red;
    Slot.ColourLinear[1] = Tint.Green;
    Slot.ColourLinear[2] = Tint.Blue;
    Slot.ColourLinear[3] = Tint.Alpha;
}

void ParticleSimulationCoordinator::TriggerBurst(std::size_t RigIndex, uint32_t Count)
{
    if (!Initialized || RigIndex >= Document.Rigs.size() || RigIndex >= RigSlices.size()) return;
    if (Document.Rigs[RigIndex].Dynamics.Strand.has_value()) return;
    RigRuntimeSlice& Slice = RigSlices[RigIndex];
    const uint32_t N = std::min(Count, Slice.SlotCapacity);
    for (uint32_t I = 0u; I < N; ++I)
    {
        SpawnSingleSlot(RigIndex, Slice);
    }
}

void ParticleSimulationCoordinator::StepSingleRig(std::size_t RigIdx, RigRuntimeSlice& Slice, float DtSec)
{
    const ParticleRig& Rig = Document.Rigs[RigIdx];
    if (!Rig.Visible || Slice.SlotCapacity == 0u) return;

    if (!Rig.Dynamics.Strand.has_value())
    {
        Slice.SpawnAccumulator += Rig.ReleaseRatePerSec * DtSec;
        const uint32_t SpawnBudget = std::min<uint32_t>(static_cast<uint32_t>(Slice.SpawnAccumulator), Slice.SlotCapacity);
        Slice.SpawnAccumulator -= static_cast<float>(SpawnBudget);
        for (uint32_t S = 0u; S < SpawnBudget; ++S)
        {
            SpawnSingleSlot(RigIdx, Slice);
        }
    }

    const ParticleStepContext StepCtx {
        Document,
        ForceAccumulator,
        DtSec,
        static_cast<float>(Document.Clock.ElapsedSeconds),
        RigidColliderCache,
        AttachedPhysics,
        0.025f
    };
    const std::span<ParticleSlot> RigSlots(ParticleSlots.data() + Slice.SlotOffset, Slice.SlotCapacity);
    const std::span<StrandVertex> RigStrands = (Slice.StrandVertexCount > 0u)
        ? std::span<StrandVertex>(StrandVertices.data() + Slice.StrandOffset, Slice.StrandVertexCount)
        : std::span<StrandVertex> {};

    Slice.AliveCount = ExecuteParticleRigStepCpu(Rig, StepCtx, RigSlots, RigStrands);
}

void ParticleSimulationCoordinator::Tick(double DeltaSeconds)
{
    if (!Initialized || Suspended) return;

    const auto T0 = std::chrono::steady_clock::now();
    const float ScaledDt = static_cast<float>(std::clamp(DeltaSeconds, 0.0, 0.05) * static_cast<double>(Document.Clock.TimeScale));
    if (ScaledDt <= 0.0f) return;

    Document.Clock.ElapsedSeconds += static_cast<double>(ScaledDt);
    ForceAccumulator.AssignSources(Document.ForceSources);
    ForceAccumulator.StepAnimation(Document.Clock.ElapsedSeconds, true);
    if (AttachedPhysics != nullptr && AttachedPhysics->QueryInitialized())
    {
        AttachedPhysics->ExportRigidColliderProxies(RigidColliderCache);
    }
    else
    {
        RigidColliderCache.clear();
    }

    uint32_t TotalAlive = 0u;
    for (std::size_t I = 0u; I < Document.Rigs.size() && I < RigSlices.size(); ++I)
    {
        StepSingleRig(I, RigSlices[I], ScaledDt);
        TotalAlive += RigSlices[I].AliveCount;
    }

    bool HasLive = false;
    float BMin[3] = { 0.0f, 0.0f, 0.0f };
    float BMax[3] = { 0.0f, 0.0f, 0.0f };
    for (const auto& Slot : ParticleSlots)
    {
        if (!QueryParticleSlotAlive(Slot)) continue;
        if (!HasLive)
        {
            for (int K = 0; K < 3; ++K) { BMin[K] = Slot.PositionMeters[K]; BMax[K] = Slot.PositionMeters[K]; }
            HasLive = true;
        }
        else
        {
            for (int K = 0; K < 3; ++K)
            {
                BMin[K] = std::min(BMin[K], Slot.PositionMeters[K]);
                BMax[K] = std::max(BMax[K], Slot.PositionMeters[K]);
            }
        }
    }

    const auto T1 = std::chrono::steady_clock::now();
    Telemetry.TotalAliveParticles = TotalAlive;
    Telemetry.TotalSlotCapacity   = static_cast<uint32_t>(ParticleSlots.size());
    Telemetry.ActiveForceCount    = ForceAccumulator.QueryActiveSourceCount();
    Telemetry.StepCounter        += 1u;
    Telemetry.ElapsedSeconds      = Document.Clock.ElapsedSeconds;
    Telemetry.LastStepMillis      = std::chrono::duration<double, std::milli>(T1 - T0).count();
    for (int K = 0; K < 3; ++K)
    {
        Telemetry.BoundsMinMeters[K] = BMin[K];
        Telemetry.BoundsMaxMeters[K] = BMax[K];
    }
}

const ParticleDocument& ParticleSimulationCoordinator::InspectDocument() const noexcept { return Document; }
ParticleDocument& ParticleSimulationCoordinator::AccessDocument() noexcept { return Document; }
const SimulationSpace::ForceFields::ForceFieldAccumulator& ParticleSimulationCoordinator::InspectForceAccumulator() const noexcept { return ForceAccumulator; }
const ColourSpace::ColourAssetCoordinator& ParticleSimulationCoordinator::InspectColourCoordinator() const noexcept { return ColourCoordinator; }
std::span<const ParticleSlot> ParticleSimulationCoordinator::InspectParticleSlots() const noexcept { return ParticleSlots; }
std::span<const StrandVertex> ParticleSimulationCoordinator::InspectStrandVertices() const noexcept { return StrandVertices; }

std::span<const ParticleAnchorSample> ParticleSimulationCoordinator::InspectRigAnchors(std::size_t RigIndex) const noexcept
{
    if (RigIndex >= RigSlices.size()) return {};
    const RigRuntimeSlice& Slice = RigSlices[RigIndex];
    if (Slice.AnchorCount == 0u || Slice.AnchorOffset + Slice.AnchorCount > AnchorBuffer.size()) return {};
    return std::span<const ParticleAnchorSample>(AnchorBuffer.data() + Slice.AnchorOffset, Slice.AnchorCount);
}

std::span<const SimulationSpace::PhysicsSpace::RigidColliderProxy> ParticleSimulationCoordinator::InspectRigidColliders() const noexcept
{
    return RigidColliderCache;
}

const ParticleTelemetrySnapshot& ParticleSimulationCoordinator::InspectTelemetry() const noexcept { return Telemetry; }
bool ParticleSimulationCoordinator::QueryInitialized() const noexcept { return Initialized; }
bool ParticleSimulationCoordinator::QuerySuspended() const noexcept { return Suspended; }

} // namespace Frontier::ParticleSpace
