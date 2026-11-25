/*============================================================================================================================================
                                                          FORCEFIELDVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Stage B (Slices S01-S06) comprehensive headless validation suite for ForceFields, KeyTracks, EffectLayers, GPU Gizmos & Physics Coupling
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.2 & §6 Slice S06

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldGpuUploader.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldSampler.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"
#include "Engine/SimulationSpace/ForceFields/KeyTrackSampler.h"
#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"

#ifdef FRONTIER_FORCEFIELD_WITH_LIVE_JOLT
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#endif

#include <atomic>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{

using namespace Frontier::SimulationSpace::ForceFields;
using namespace Frontier::SimulationSpace::EffectLayers;
using namespace Frontier::SimulationSpace::PhysicsSpace;
namespace fs = std::filesystem;

uint32_t gPassedAssertions = 0u;
uint32_t gFailedAssertions = 0u;

void CheckCondition(const bool ok, const char* label)
{
    if (ok)
    {
        ++gPassedAssertions;
    }
    else
    {
        ++gFailedAssertions;
        std::fprintf(stderr, "[FAIL] %s\n", label);
    }
}

std::string ReadTextFile(const fs::path& filePath)
{
    std::ifstream in(filePath, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

fs::path ResolveRepoRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return fs::path(FRONTIER_REPO_ROOT_DIR);
#else
    return fs::current_path();
#endif
}

void ValidateForceModelRegistry()
{
    const auto descriptors = QueryAllForceModelDescriptors();
    CheckCondition(descriptors.size() == kForceModelCount, "Registry exposes exactly 20 analytic force models");
    for (uint32_t i = 0u; i < kForceModelCount; ++i)
    {
        const auto ord = static_cast<ForceModelOrdinal>(i);
        const auto* byOrd = FindForceModelByOrdinal(ord);
        CheckCondition(byOrd != nullptr && byOrd->Code == ord, "O(1) ordinal lookup matches code");
        if (byOrd != nullptr)
        {
            const auto* byId = FindForceModelByTomlId(byOrd->TomlId);
            CheckCondition(byId == byOrd, "TOML Id lookup round-trips to same descriptor");
        }
    }
    for (uint32_t f = 0u; f < kForceFalloffCount; ++f)
    {
        const auto falloff = static_cast<ForceFalloffOrdinal>(f);
        const auto token = QueryFalloffTomlToken(falloff);
        const auto parsed = ParseFalloffTomlToken(token);
        CheckCondition(parsed.has_value() && *parsed == falloff, "Falloff token round-trips");
    }
    CheckCondition(!IsForceModelGasVelocitySaturated(ForceModelOrdinal::DragBaffle), "DragBaffle is unsaturated in GasForce");
    CheckCondition(IsForceModelGasVelocitySaturated(ForceModelOrdinal::DirectionalWind), "DirectionalWind is saturated in GasForce");
}

void ValidateKeyTrackEnvelopes()
{
    KeyTrack smooth {};
    smooth.Path = "Strength";
    smooth.Keys.push_back(TrackKey { 0.0, 0.0, KeyEaseOrdinal::Smooth, {}, {} });
    smooth.Keys.push_back(TrackKey { 1.0, 10.0, KeyEaseOrdinal::Smooth, {}, {} });
    smooth.Keys.push_back(TrackKey { 2.0, 10.0, KeyEaseOrdinal::Smooth, {}, {} });
    const double mid = SampleScalarTrack(smooth, 0.5, 0.0);
    const double flat = SampleScalarTrack(smooth, 1.5, 0.0);
    CheckCondition(mid > 0.0 && mid < 10.0, "Fritsch-Carlson Smooth stays monotone inside [0, 10]");
    CheckCondition(std::abs(flat - 10.0) < 1.0e-9, "Fritsch-Carlson Smooth has zero overshoot on flat plateau");

    TimingEnvelopeParameters env { 1.0, 2.0, 0.5, 0.0 };
    const auto envTrack = ConstructEnvelopeWeightTrack(env);
    CheckCondition(envTrack.has_value(), "TimingEnvelope generates valid weight track");
    if (envTrack.has_value())
    {
        CheckCondition(std::abs(SampleScalarTrack(*envTrack, 0.5, -1.0) - 0.0) < 1.0e-6, "Envelope is 0 before StartTime");
        CheckCondition(std::abs(SampleScalarTrack(*envTrack, 1.25, -1.0) - 0.5) < 1.0e-6, "Envelope ramps linearly at fade-in midpoint");
        CheckCondition(std::abs(SampleScalarTrack(*envTrack, 2.0, -1.0) - 1.0) < 1.0e-6, "Envelope holds 1.0 in active window");
        CheckCondition(std::abs(SampleScalarTrack(*envTrack, 3.5, -1.0) - 0.0) < 1.0e-6, "Envelope returns to 0 after Duration");
    }
}

void ValidateEffectLayerSpecifications(const fs::path& root)
{
    std::string err;
    const std::string specPath = (root / "EngineContent/Simulation/LayerSpecifications.VolumetricSpace").string();
    const auto specsOpt = LoadLayerSpecificationsFromFile(specPath, &err);
    CheckCondition(specsOpt.has_value(), "LayerSpecifications.VolumetricSpace loads cleanly");
    if (!specsOpt.has_value()) return;

    CheckCondition(specsOpt->CoreSpecificationHash == "885cc8c5a43d9053", "CoreSpecificationHash matches golden FNV-1a hash 885cc8c5a43d9053");
    CheckCondition(specsOpt->Layers.size() == 30u, "LayerSpecifications defines 30 layers (28 canonical + 2 opt-in Frontier GPU physics layers 220/221)");

    uint32_t presetCount = 0u;
    uint32_t totalLoadedForces = 0u;
    ForceFieldRegister reg;
    for (const auto& entry : fs::directory_iterator(root / "EngineContent/Simulation/Presets"))
    {
        if (entry.path().extension() != ".FluidGrid") continue;
        const auto doc = ParseTomlSubset(ReadTextFile(entry.path()), &err);
        CheckCondition(doc.has_value(), "Preset .FluidGrid parses cleanly");
        if (!doc.has_value()) continue;
        ++presetCount;
        totalLoadedForces += reg.LoadFromTomlTable(*doc);
        const uint32_t published = reg.PublishEvaluatedReading(0.5, presetCount);
        auto lease = reg.AcquirePublishedReading();
        CheckCondition(lease.Valid() && lease->ActiveCount == published, "Wait-free reading lease matches published count");
    }
    CheckCondition(presetCount == 54u, "All 54 Gas Presets/*.FluidGrid loaded");
    CheckCondition(totalLoadedForces > 40u, "Loaded authentic [[Force]] entries across all 54 presets");
}

void ValidateRegisterWaitFreeConcurrency()
{
    ForceFieldRegister reg;
    auto& wind = reg.AddForceNode(ForceModelOrdinal::DirectionalWind, "gust-wind");
    wind.Node.Direction = { 1.0f, 0.0f, 0.0f };
    wind.Node.Strength  = 12.0f;
    wind.Node.Falloff   = ForceFalloffOrdinal::Constant;
    KeyTrack strengthTrack {};
    strengthTrack.Path = "Strength";
    strengthTrack.Keys.push_back(TrackKey { 0.0, 4.0, KeyEaseOrdinal::Linear, {}, {} });
    strengthTrack.Keys.push_back(TrackKey { 2.0, 20.0, KeyEaseOrdinal::Linear, {}, {} });
    wind.Tracks.push_back(strengthTrack);

    const uint32_t cnt = reg.PublishEvaluatedReading(1.0, 1ull);
    CheckCondition(cnt == 1u, "Keyed force published count == 1");
    {
        auto lease = reg.AcquirePublishedReading();
        CheckCondition(lease.Valid() && std::abs(lease->Records[0].DirectionStrength[3] - 12.0f) < 1.0e-4f,
            "Keyed Strength track sampled to 12.0 at t=1.0s");
    }

    // Concurrent multi-reader / single-writer stress test across 2 reader threads
    std::atomic<bool> stopReaders { false };
    std::atomic<uint64_t> readsCompleted { 0ull };
    std::atomic<bool> observedCorrupt { false };

    auto readerWorker = [&]()
    {
        while (!stopReaders.load(std::memory_order_acquire))
        {
            auto lease = reg.AcquirePublishedReading();
            if (!lease.Valid() || lease->ActiveCount != 1u || !std::isfinite(lease->Records[0].DirectionStrength[3]))
            {
                observedCorrupt.store(true, std::memory_order_relaxed);
            }
            readsCompleted.fetch_add(1ull, std::memory_order_relaxed);
        }
    };

    std::thread r1(readerWorker);
    std::thread r2(readerWorker);
    for (uint64_t step = 0ull; step < 500ull; ++step)
    {
        const double t = static_cast<double>(step % 200ull) * 0.01;
        (void)reg.PublishEvaluatedReading(t, step + 2ull);
    }
    stopReaders.store(true, std::memory_order_release);
    r1.join();
    r2.join();

    CheckCondition(!observedCorrupt.load(), "Zero tearing or corruption under concurrent multi-thread PublishedReadingExchange reads");
    CheckCondition(readsCompleted.load() > 100ull, "Concurrent reader threads completed > 100 wait-free acquisitions");
}

void ValidateAll20GpuGizmoBuilders()
{
    const std::array<float, 3> halfExtent { 4.0f, 4.0f, 4.0f };
    for (uint32_t i = 0u; i < kForceModelCount; ++i)
    {
        const ForceModelNode node = ConstructDefaultForceNode(static_cast<ForceModelOrdinal>(i), "gizmo-test");
        std::vector<ForceGizmoVertex> verts;
        AppendForceGizmoLineVertices(node, (i % 2u == 0u), 1.0f, 1.25f, halfExtent, verts);
        bool allFinite = !verts.empty() && (verts.size() % 2u == 0u);
        for (const ForceGizmoVertex& v : verts)
        {
            if (!std::isfinite(v.Position[0]) || !std::isfinite(v.Position[1]) || !std::isfinite(v.Position[2])
                || !std::isfinite(v.Colour[0]) || !std::isfinite(v.Colour[3]))
            {
                allFinite = false;
                break;
            }
        }
        CheckCondition(allFinite, "GPU gizmo builder produces non-empty even-count finite line vertices for force model");
    }
}

void ValidatePhysicsCouplingSuite()
{
    ForceFieldRegister reg;
    auto& wind = reg.AddForceNode(ForceModelOrdinal::DirectionalWind, "crosswind");
    wind.Node.Direction = { 1.0f, 0.0f, 0.0f };
    wind.Node.Strength  = 15.0f;
    wind.Node.Frequency = 0.0f; // zero gust modulation for deterministic check
    wind.Node.Secondary = 0.0f;
    wind.Node.Falloff   = ForceFalloffOrdinal::Constant;
    (void)reg.PublishEvaluatedReading(0.0, 1ull);

    auto lease = reg.AcquirePublishedReading();
    RigidBodyReading body {};
    body.Resident = true;
    body.MassKg   = 10.0f;
    body.PositionMeters = { 0.0f, 0.0f, 2.0f };
    body.LinearVelocity = { 0.0f, 0.0f, 0.0f };

    CoupledRigidBodyEntry accelEntry {};
    accelEntry.CouplingMode = ForceCouplingMode::MassProportionalAcceleration;
    const auto fAccel = ComputeCoupledRigidForceNewtons(accelEntry, *lease, body, 0.0);
    CheckCondition(fAccel.X > 140.0f && std::abs(fAccel.Y) < 1.0e-3f, "Mass-proportional DirectionalWind produces +X force (~150 N on 10 kg)");

    // Vehicle aerodynamic lateral crosswind (wind along +Y pushing 3.5 m^2 side area)
    reg.Clear();
    auto& cross = reg.AddForceNode(ForceModelOrdinal::DirectionalWind, "lateral-gust");
    cross.Node.Direction = { 0.0f, 1.0f, 0.0f };
    cross.Node.Strength  = 20.0f;
    cross.Node.Frequency = 0.0f;
    cross.Node.Secondary = 0.0f;
    cross.Node.Falloff   = ForceFalloffOrdinal::Constant;
    (void)reg.PublishEvaluatedReading(0.0, 2ull);
    auto crossLease = reg.AcquirePublishedReading();

    RigidBodyReading chassis {};
    chassis.Resident = true;
    chassis.MassKg   = 1450.0f;
    chassis.Orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    chassis.LinearVelocity = { 25.0f, 0.0f, 0.0f };
    const auto fWind = ComputeVehicleWindForceNewtons(*crossLease, chassis, 2.2f, 3.6f, 0.34f, 1.225f, 0.0);
    // Expected lateral Fy = 0.5 * 1.225 * 0.34 * 3.6 * 20^2 = 299.88 N
    CheckCondition(std::abs(fWind.Y - 299.88f) < 1.0f && fWind.X < -200.0f,
        "Vehicle chassis aerodynamic crosswind computes exact lateral gust (+299.88 N) and headwind drag");

#ifdef FRONTIER_FORCEFIELD_WITH_LIVE_JOLT
    PhysicsExtension physics;
    CheckCondition(physics.Initialize(), "Live Jolt PhysicsExtension initializes cleanly");
    physics.AssignGravity({ 0.0f, 0.0f, 0.0f });

    RigidBodyDescriptor desc {};
    desc.MotionCategory = RigidMotionCategory::Dynamic;
    desc.PositionMeters = { 0.0f, 0.0f, 2.0f };
    desc.ExplicitMassKg = 10.0f;
    desc.LinearDamping  = 0.0f;
    desc.AllowSleep     = false;
    const RigidBodyToken token = physics.IssueRigidBody(desc);
    CheckCondition(token.Valid(), "Issued live Jolt dynamic rigid body");

    ForceFieldPhysicsCoupling coupling;
    CoupledRigidBodyEntry liveEntry {};
    liveEntry.BodyToken    = token;
    liveEntry.CouplingMode = ForceCouplingMode::MassProportionalAcceleration;
    coupling.RegisterRigidBody(liveEntry);

    for (int step = 0; step < 30; ++step)
    {
        const double t = static_cast<double>(step) / 60.0;
        (void)coupling.ApplyPublishedForceFields(physics, reg, t);
        physics.Tick(1.0 / 60.0);
    }
    const RigidBodyReading after = physics.InspectBodyReading(token);
    CheckCondition(after.PositionMeters.Y > 1.0f && after.LinearVelocity.Y > 5.0f,
        "Live Jolt dynamic body deflected along +Y by published ForceFieldRegister wind");
    physics.Finalize();
#endif
}

} // namespace

int main()
{
    const fs::path root = ResolveRepoRoot();
    ValidateForceModelRegistry();
    ValidateKeyTrackEnvelopes();
    ValidateEffectLayerSpecifications(root);
    ValidateRegisterWaitFreeConcurrency();
    ValidateAll20GpuGizmoBuilders();
    ValidatePhysicsCouplingSuite();

    std::printf("ForceFieldValidation: %u passed, %u failed\n", gPassedAssertions, gFailedAssertions);
    return (gFailedAssertions == 0u) ? 0 : 1;
}
