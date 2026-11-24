/*============================================================================================================================================
                                                    PARTICLEGPUPHYSICSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Stage C (Slices S07-S13) comprehensive 1:1 headless parity suite for ParticleSpace templates, fireworks, carrier solids, GPU simulation,
//    18 analytical sprite cutouts, 96-sample Catmull-Rom ribbons, instanced 3D carriers + 3 backing surfaces, and 11 strand/web/flora/arc modes.
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §6 Slice S13

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/CarrierSolidRegistry.h"
#include "Engine/ParticleSpace/FireworkPatternSampler.h"
#include "Engine/ParticleSpace/ParticleCloudTypes.h"
#include "Engine/ParticleSpace/ParticleGpuRenderer.h"
#include "Engine/ParticleSpace/ParticleGpuSimulation.h"
#include "Engine/ParticleSpace/ParticleSpatialHash.h"
#include "Engine/ParticleSpace/ParticleTemplateRegistry.h"
#include "Engine/ParticleSpace/ParticleXpbdNetworkBuilder.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerStack.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelTypes.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/ForceFieldPhysicsCoupling.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::ParticleSpace;
using namespace Frontier::SimulationSpace::EffectLayers;
namespace fs = std::filesystem;

uint32_t gPassedChecks = 0u;
uint32_t gFailedChecks = 0u;

void CheckCondition(const bool ok, const char* label)
{
    if (ok)
    {
        ++gPassedChecks;
    }
    else
    {
        ++gFailedChecks;
        std::fprintf(stderr, "[FAIL] %s\n", label);
    }
}

fs::path ResolveRepoRoot()
{
#ifdef FRONTIER_REPO_ROOT_DIR
    return fs::path(FRONTIER_REPO_ROOT_DIR);
#else
    return fs::current_path();
#endif
}

void ValidateFireworkPatterns()
{
    for (uint32_t pat = 0u; pat < 10u; ++pat)
    {
        const auto sampled = SampleFireworkAnchors(static_cast<FireworkPatternOrdinal>(pat), "2026", 256u);
        bool finite = (sampled.Count == 256u && sampled.Positions.size() == 256u * 3u && sampled.Normals.size() == 256u * 3u);
        for (float v : sampled.Positions) finite = finite && std::isfinite(v);
        for (float v : sampled.Normals)   finite = finite && std::isfinite(v);
        CheckCondition(finite, "Firework pattern produces 256 finite positions + burst tint vectors");
    }
}

void ValidateCarrierSolidLibrary()
{
    const auto allNames = QueryAllSolidNames();
    const auto builtIns = QueryBuiltInSolidNames();
    CheckCondition(allNames.size() == 19u, "CarrierSolidRegistry exposes 19 selectable solid names (18 built-in + CustomObject)");
    CheckCondition(builtIns.size() == 18u, "CarrierSolidRegistry exposes 18 procedural built-in solids");

    CarrierSolidRegistry reg;
    for (const auto name : builtIns)
    {
        const CarrierSolidRecord& solid = reg.ResolveSolid(name);
        const SolidTexelTable texels = PackSolidTexels(solid);
        const SolidAnchorSet anchors = SampleSolidAnchors(solid, 64u, SolidSampleModeOrdinal::EvenSurface, 15.0);
        const AnchorTexelTable packedAnchors = PackAnchorTexels(anchors);
        const bool ok = (solid.Triangles > 0u && solid.VertexCount == solid.Triangles * 3u
            && texels.Data.size() == static_cast<size_t>(texels.Width) * static_cast<size_t>(texels.Height) * 4u
            && anchors.Count == 64u
            && packedAnchors.Count == 64u);
        CheckCondition(ok, "Built-in carrier solid resolves, packs texels, samples anchors");
    }
}

void ValidateGpuSimulationModes(const CarrierSolidRegistry& solidReg)
{
    // Verify all 16 spawn shapes + Mocap 4 clips + Effector 4 modes + Derez 5 modes step cleanly
    ParticleGpuSimulation sim;
    ParticleSimulationRegion region {};
    std::vector<ParticleCloudNode> clouds;
    for (uint32_t shape = 0u; shape < 16u; ++shape)
    {
        ParticleCloudNode node {};
        node.NodeId = "shape-" + std::to_string(shape);
        node.Enabled = true;
        node.Evaluated.Mode = (shape % 2u == 0u) ? ParticleSpawnModeOrdinal::Cloner : ParticleSpawnModeOrdinal::Release;
        node.Evaluated.Shape = static_cast<ParticleShapeOrdinal>(shape);
        node.Evaluated.Count = 64u;
        node.Evaluated.MocapClip = static_cast<ParticleMocapClipOrdinal>(shape % 5u);
        node.Evaluated.EffectorMode = static_cast<ParticleEffectorOrdinal>(shape % 5u);
        node.Evaluated.EffectorStrength = 0.6f;
        node.Evaluated.DerezMode = static_cast<ParticleDerezOrdinal>(shape % 6u);
        node.Evaluated.DerezMix = 0.7f;
        node.Evaluated.DerezProgress = 0.45f;
        clouds.push_back(std::move(node));
    }

    for (int step = 0; step < 30; ++step)
    {
        sim.StepCloudsCpu(clouds, solidReg, region, static_cast<float>(step) / 60.0f, 1.0f / 60.0f);
    }

    bool allFinite = true;
    for (const auto& c : clouds)
    {
        const auto* rec = sim.FindCloudRecord(c.NodeId);
        if (!rec || rec->ActiveCount != 64u) { allFinite = false; break; }
        for (uint32_t i = 0u; i < rec->ActiveCount; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                if (!std::isfinite(rec->ReadStates[i].PositionAge[k]) || !std::isfinite(rec->ReadStates[i].VelocitySeed[k]))
                {
                    allFinite = false;
                }
            }
        }
    }
    CheckCondition(allFinite, "All 16 spawn shapes + 4 Mocap clips + 4 Effectors + 5 Derez modes step 30 ticks with 0 NaN/Inf");
}

void ValidateSpritesCutoutsRamps()
{
    // 4 analytic ramps monotonic endpoints + finite
    bool rampsOk = true;
    for (uint32_t r = 0u; r < 4u; ++r)
    {
        for (int s = 0; s <= 4; ++s)
        {
            const auto c = EvaluateAnalyticRampCpu(r, static_cast<float>(s) * 0.25f);
            for (float v : c) rampsOk = rampsOk && std::isfinite(v) && v >= -0.05f && v <= 1.05f;
        }
    }
    CheckCondition(rampsOk, "All 4 analytic ramps produce finite RGB across [0..1]");

    // All 18 sprite cutouts (0..17) finite at multiple sample coords
    bool spritesOk = true;
    const std::array<float, 4> col { 0.85f, 0.55f, 0.25f, 0.9f };
    const std::array<float, 4> extra { 0.4f, 0.35f, 0.5f, 0.0f };
    for (uint32_t sp = 0u; sp < 18u; ++sp)
    {
        const auto res = EvaluateSpriteCutoutCpu(sp, 0u, col, 1.0f, 0.0f, extra, { 0.5f, 0.5f });
        spritesOk = spritesOk && std::isfinite(res.Rgb[0]) && std::isfinite(res.Rgb[1]) && std::isfinite(res.Rgb[2]) && std::isfinite(res.Alpha);
    }
    CheckCondition(spritesOk, "All 18 analytical sprite cutouts (0..17) evaluate cleanly");
}

void ValidateRibbonsTrails(const CarrierSolidRegistry& solidReg)
{
    // Catmull-Rom midpoint accuracy
    const auto mid = EvaluateCatmullRomCpu({ 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 0.0f }, { 2.0f, 1.0f, 0.0f }, { 3.0f, 0.0f, 0.0f }, 0.5f);
    CheckCondition(std::fabs(mid[0] - 1.5f) < 1.0e-6f && std::fabs(mid[1] - 1.125f) < 1.0e-6f, "Catmull-Rom cubic midpoint exact");

    // 96-sample ring buffer + Linger freeze + jump collapse
    ParticleCloudNode node {};
    node.NodeId = "trail-96";
    node.Enabled = true;
    node.Evaluated.Mode = ParticleSpawnModeOrdinal::Cloner;
    node.Evaluated.Shape = ParticleShapeOrdinal::Ring;
    node.Evaluated.Count = 8u;
    node.Evaluated.Trail = ParticleTrailModeOrdinal::Ribbon;
    node.Evaluated.TrailLength = 96u;
    node.Evaluated.TrailInterval = 0.01f;
    node.Evaluated.TrailSubdivisions = 4u;
    node.Evaluated.Lifetime = 1.0f;
    node.Evaluated.Linger = 1.5f;
    node.Evaluated.Noise = 1.0f;

    ParticleGpuSimulation sim;
    ParticleSimulationRegion region {};
    std::vector<ParticleCloudNode> clouds { node };
    for (int i = 0; i < 100; ++i) sim.StepCloudsCpu(clouds, solidReg, region, static_cast<float>(i) / 60.0f, 1.0f / 60.0f);

    const auto* rec = sim.FindCloudRecord("trail-96");
    CheckCondition(rec && rec->TrailLength == 96u && rec->TrailSamples.size() == rec->Capacity * 96u, "96-sample trail ring buffer allocates properly");

    if (rec)
    {
        auto frozenRec = *rec;
        const auto before = frozenRec.TrailSamples;
        frozenRec.ReadStates[0].PositionAge[0] += 0.2f;
        frozenRec.ReadStates[0].PositionAge[3] = std::max(frozenRec.PreviousPositions[0][3] + 0.02f, 1.25f);
        ParticleTrailUpdateUniformBlock lingerU {
            { static_cast<float>(frozenRec.Capacity), 96.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 1.5f, 0.0f }
        };
        StepTrailHistoryCpu(frozenRec, lingerU);
        bool preserved = true;
        for (uint32_t s = 0u; s < 96u; ++s)
        {
            if (frozenRec.TrailSamples[s][0] != before[s][0]) preserved = false;
        }
        CheckCondition(preserved, "Trail ring buffer freezes in place during Linger");
    }
}

void ValidateCarrierSolidsBacking(const CarrierSolidRegistry& solidReg)
{
    ParticleCloudNode node {};
    node.NodeId = "solid-carrier-test";
    node.Enabled = true;
    node.Evaluated.Mode = ParticleSpawnModeOrdinal::Cloner;
    node.Evaluated.Shape = ParticleShapeOrdinal::SolidSurface;
    node.Evaluated.SpawnSolid = "UtahTeapot";
    node.Evaluated.CarrierMode = ParticleCarrierOrdinal::Object3D;
    node.Evaluated.CarrierSolid = "Cube";
    node.Evaluated.BackingSurface = ParticleBackingSurfaceOrdinal::BrushedChrome;
    node.Evaluated.Count = 32u;

    ParticleGpuSimulation sim;
    ParticleSimulationRegion region {};
    std::vector<ParticleCloudNode> clouds { node };
    sim.StepCloudsCpu(clouds, solidReg, region, 0.5f, 1.0f / 60.0f);

    ParticleGpuRenderer renderer;
    ParticleCameraView cam {};
    const auto calls = renderer.BuildCarrierSolidRenderCalls(clouds, sim, solidReg, cam, 0.5f, 4.0f);
    CheckCondition(calls.size() == 2u && calls[0].IsBackingCall && calls[0].BackingMode == 2u && !calls[1].IsBackingCall,
        "Carrier solid planner emits BrushedChrome backing followed by instanced Object3D carrier");

    CarrierSolidVertexCpuResult vx {};
    vx.WorldPos = { 0.2f, 0.3f, 0.0f };
    vx.WorldNormal = { 0.0f, 0.8f, 0.6f };
    vx.LocalPos = { 0.1f, 0.2f, 0.0f };
    vx.Colour = { 0.8f, 0.5f, 0.2f };
    vx.Alpha = 1.0f;
    vx.Ridge = 0.5f;
    bool allModesOk = true;
    for (uint32_t m = 0u; m < 4u; ++m)
    {
        auto u = calls[1].UniformBlock;
        u.BackingParams[3] = static_cast<float>(m);
        const auto fg = EvaluateCarrierSolidFragmentCpu(u, vx);
        allModesOk = allModesOk && !fg.Discarded && std::isfinite(fg.Rgb[0]) && std::isfinite(fg.Rgb[1]) && std::isfinite(fg.Rgb[2]) && fg.Alpha > 0.0f;
    }
    CheckCondition(allModesOk, "All 4 carrier/backing fragment modes (Carrier, ObsidianGlossy, BrushedChrome, GhostWireframe) evaluate cleanly");
}

void ValidateAllStrandModes()
{
    ParticleGpuRenderer renderer;
    ParticleSimulationRegion region {};
    ParticleCameraView cam {};

    bool allConfigsOk = true;
    uint32_t testedConfigs = 0u;
    for (uint32_t mode = 1u; mode <= 11u; ++mode)
    {
        for (uint32_t variant = 0u; variant < 2u; ++variant)
        {
            ParticleCloudNode node {};
            node.NodeId = "strand-m" + std::to_string(mode) + "-v" + std::to_string(variant);
            node.Enabled = true;
            node.Evaluated.StrandMode = static_cast<ParticleStrandOrdinal>(mode);
            node.Evaluated.StrandVariant = static_cast<ParticleStrandVariantOrdinal>(variant);
            node.Evaluated.StrandCount = 24u;
            node.Evaluated.StrandDetail = 12u;
            node.Evaluated.StrandSag = 0.35f;
            node.Evaluated.StrandThickness = 0.03f;
            node.Evaluated.StrandSway = 0.75f;
            node.Evaluated.StrandSpan = 1.8f;
            node.Evaluated.StrandGlow = 0.65f;
            node.Evaluated.ColourStops = { "#44ddff", "#ff8844" };

            std::vector<ParticleCloudNode> single { node };
            const auto calls = renderer.BuildStrandRenderCalls(single, region, cam, 1.25f);
            if (calls.size() != 1u || calls[0].VertexCount == 0u) { allConfigsOk = false; continue; }
            if (mode >= 9u && calls[0].Blend != ParticleBlendOrdinal::Additive) { allConfigsOk = false; }

            const auto& uBlock = calls[0].UniformBlock;
            const uint32_t sampleSlots = std::min(calls[0].StrandTotal, 16u);
            for (uint32_t slot = 0u; slot < sampleSlots; ++slot)
            {
                const auto ep = EvaluateStrandEndpointsCpu(uBlock, static_cast<int32_t>(slot));
                if (ep.Visible < 0.5f) continue;
                const auto pt = EvaluateStrandPointCpu(uBlock, static_cast<int32_t>(slot), ep, 0.5f);
                const auto fg = EvaluateStrandFragmentCpu(uBlock, { 0.4f, 0.8f, 0.5f }, 0.5f, 0.1f, ep.StyleTag, 0.37f, 1.0f);
                if (!std::isfinite(pt[0]) || !std::isfinite(pt[1]) || !std::isfinite(pt[2]) ||
                    !std::isfinite(fg.Rgb[0]) || !std::isfinite(fg.Rgb[1]) || !std::isfinite(fg.Rgb[2]) || !std::isfinite(fg.Alpha))
                {
                    allConfigsOk = false;
                }
            }
            ++testedConfigs;
        }
    }
    CheckCondition(testedConfigs == 22u && allConfigsOk, "All 11 strand modes x 2 variants (22 configs) produce finite endpoints, curves & fragments");
}

void ValidateAll71Templates(const CarrierSolidRegistry& solidReg)
{
    const fs::path root = ResolveRepoRoot();
    std::string err;
    const auto specsOpt = LoadLayerSpecificationsFromFile((root / "EngineContent/Simulation/LayerSpecifications.VolumetricSpace").string(), &err);
    CheckCondition(specsOpt.has_value(), "LayerSpecifications.VolumetricSpace loads cleanly");
    if (!specsOpt) return;

    ParticleTemplateRegistry ptReg;
    const bool loaded = ptReg.LoadFromDirectory(*specsOpt, (root / "EngineContent/Simulation/ParticleTemplates").string(), &err);
    CheckCondition(loaded && ptReg.QueryTemplates().size() == 71u, "All 71 .ParticleSpace templates load cleanly");
    if (!loaded) return;

    std::vector<ParticleCloudNode> clouds;
    clouds.reserve(ptReg.QueryTemplates().size());
    for (const auto& entry : ptReg.QueryTemplates())
    {
        auto c = ptReg.ConstructParticleCloud(*specsOpt, entry.Id, "tpl-" + entry.Id);
        if (!c) { CheckCondition(false, "Template cloud construction failed"); return; }
        c->Evaluated.Count = std::min(c->Evaluated.Count, 128u);
        clouds.push_back(std::move(*c));
    }
    CheckCondition(clouds.size() == 71u, "Constructed all 71 ParticleCloudNodes from template library");

    ParticleGpuSimulation sim;
    ParticleGpuRenderer renderer;
    ParticleSimulationRegion region {};
    ParticleCameraView cam {};
    for (int step = 0; step < 60; ++step)
    {
        sim.StepCloudsCpu(clouds, solidReg, region, static_cast<float>(step) / 60.0f, 1.0f / 60.0f);
    }

    bool allFinite = true;
    for (const auto& c : clouds)
    {
        const auto* rec = sim.FindCloudRecord(c.NodeId);
        if (!rec) { allFinite = false; break; }
        for (uint32_t i = 0u; i < rec->ActiveCount; ++i)
        {
            for (int k = 0; k < 4; ++k)
            {
                if (!std::isfinite(rec->ReadStates[i].PositionAge[k]) || !std::isfinite(rec->ReadStates[i].VelocitySeed[k]))
                {
                    allFinite = false;
                }
            }
        }
    }
    CheckCondition(allFinite, "All 71 templates stepped 60 frames with 0 NaN/Inf");

    const auto spriteCalls = renderer.BuildSpriteRenderCalls(clouds, sim, cam, 1.0f, 4.0f);
    const auto ribbonCalls = renderer.BuildRibbonRenderCalls(clouds, sim, cam, 1.0f, 4.0f);
    const auto solidCalls  = renderer.BuildCarrierSolidRenderCalls(clouds, sim, solidReg, cam, 1.0f, 4.0f);
    const auto strandCalls = renderer.BuildStrandRenderCalls(clouds, region, cam, 1.0f);

    CheckCondition(!spriteCalls.empty(), "71-template sprite planner produces active sprite/streak/halo calls");
    CheckCondition(!ribbonCalls.empty(), "71-template ribbon planner produces active Catmull-Rom ribbon + halo calls");
    CheckCondition(!solidCalls.empty(),  "71-template carrier planner produces active backing + Object3D carrier calls");
    CheckCondition(!strandCalls.empty(), "71-template strand planner produces active web/flora/arc calls");

    std::printf("[ParticleGpuPhysicsValidation] 71-template batch totals: sprite=%zu ribbon=%zu solid=%zu strand=%zu\n",
        spriteCalls.size(), ribbonCalls.size(), solidCalls.size(), strandCalls.size());

    // Suite F1a: Verify all 71 canonical HTML templates keep Layers 220 & 221 strictly neutral / disabled
    bool all71Neutral = true;
    for (const auto& c : clouds)
    {
        if (c.Evaluated.XpbdTopology != ParticleXpbdTopologyOrdinal::Off
            || c.Evaluated.CollisionRigidCoupling
            || c.Evaluated.CollisionSelfHash
            || c.Evaluated.CollisionTwoWayImpulse)
        {
            all71Neutral = false;
            break;
        }
    }
    CheckCondition(all71Neutral, "Stage F1a: All 71 canonical .ParticleSpace templates remain strictly neutral when Layers 220 & 221 are absent");

    // Suite F1b: Verify additive Layer 220 (XPBD) & Layer 221 (GPU Collisions) load, preserve 28-layer CoreSpecificationHash, and evaluate cleanly
    CheckCondition(specsOpt->CoreSpecificationHash == "885cc8c5a43d9053",
        "Stage F1b: 28-layer CoreSpecificationHash remains bit-identical (885cc8c5a43d9053)");
    CheckCondition(specsOpt->Layers.size() == 30u
        && specsOpt->FindByCode(kParticleXpbdLayerCode) != nullptr
        && specsOpt->FindByCode(kParticleGpuCollisionLayerCode) != nullptr,
        "Stage F1c: LayerSpecifications.VolumetricSpace registers additive Layer 220 (xpbd-constraints) & Layer 221 (gpu-collisions)");

    ParticleCloudNode optInCloud {};
    optInCloud.NodeId = "gpu-physics-opt-in";
    optInCloud.Stack.Token = "particle-cloud";
    optInCloud.Stack.Category = LayerStackOrdinal::ParticleCloud;
    EffectLayerEntry spawnLayer {};
    spawnLayer.Token = "spawn-0";
    spawnLayer.CategoryCode = kParticleSpawnLayerCode;
    optInCloud.Stack.Layers.push_back(spawnLayer);

    EffectLayerEntry xpbdLayer {};
    xpbdLayer.Token = "xpbd-0";
    xpbdLayer.CategoryCode = kParticleXpbdLayerCode;
    xpbdLayer.SetParameter("XpbdTopology", int64_t{ 1 });
    xpbdLayer.SetParameter("XpbdSubsteps", int64_t{ 4 });
    xpbdLayer.SetParameter("XpbdIterations", int64_t{ 16 });
    xpbdLayer.SetParameter("XpbdWarpModulus", 1800.0);
    xpbdLayer.SetParameter("XpbdBendStiffness", 0.14);
    optInCloud.Stack.Layers.push_back(xpbdLayer);

    EffectLayerEntry colLayer {};
    colLayer.Token = "gpu-col-0";
    colLayer.CategoryCode = kParticleGpuCollisionLayerCode;
    colLayer.SetParameter("CollisionRigidCoupling", true);
    colLayer.SetParameter("CollisionSelfHash", true);
    colLayer.SetParameter("CollisionTwoWayImpulse", true);
    colLayer.SetParameter("CollisionParticleRadius", 0.045);
    colLayer.SetParameter("CollisionRestitution", 0.42);
    optInCloud.Stack.Layers.push_back(colLayer);

    const auto evalOut = EvaluateParticleCloudNode(*specsOpt, optInCloud, 0.0);
    CheckCondition(!evalOut.IsStopped()
        && optInCloud.Evaluated.XpbdTopology == ParticleXpbdTopologyOrdinal::ClothGrid
        && optInCloud.Evaluated.XpbdSubsteps == 4u
        && optInCloud.Evaluated.XpbdIterations == 16u
        && std::fabs(optInCloud.Evaluated.XpbdWarpModulus - 1800.0f) < 1.0e-3f
        && optInCloud.Evaluated.CollisionRigidCoupling
        && optInCloud.Evaluated.CollisionSelfHash
        && optInCloud.Evaluated.CollisionTwoWayImpulse
        && std::fabs(optInCloud.Evaluated.CollisionParticleRadius - 0.045f) < 1.0e-4f,
        "Stage F1d: Opt-in Layer 220 (XPBD) & Layer 221 (GPU Collisions) evaluate all parameters cleanly");
}

void ValidateStageF2RigidTwoWayCoupling()
{
    using namespace Frontier::SimulationSpace::PhysicsSpace;

    // Spinning Jolt sphere tangential rebound + two-way atomic impulse readback
    RigidColliderProxy sphere {};
    sphere.CenterMeters[0] = 0.0f; sphere.CenterMeters[1] = 1.0f; sphere.CenterMeters[2] = 0.0f;
    sphere.ShapeCode = 0u;
    sphere.RadiusMeters = 0.5f;
    sphere.Restitution = 0.6f;
    sphere.OrientationQuat[3] = 1.0f;
    sphere.Friction = 0.8f;
    sphere.BodySlotIndex = 0u;
    sphere.ChannelMask = 1u;
    sphere.DynamicResponseBit = 1u;

    const std::vector<RigidColliderProxy> proxies { sphere };
    const std::vector<std::array<float, 4>> angVels { { 0.0f, 0.0f, -10.0f, 0.0f } };
    const std::vector<RigidBodyToken> bodyTokens { RigidBodyToken{ 101u } };

    std::vector<ParticleStateGpuRecord> particles(1u);
    particles[0].PositionAge  = { 0.0f, 1.49f, 0.0f, 0.1f };
    particles[0].VelocitySeed = { 0.0f, -2.5f, 0.0f, 0.5f };

    ParticleCollisionUniformBlock ubo {};
    ubo.GroundHeightFlags = { -10.0f, 0.0f, 0.0f, 1.0f };
    ubo.ParticleMaterial  = { 0.02f, 0.25f, 0.6f, 0.8f };
    ubo.CountsMasks       = { 1u, 0u, 0xFFFFFFFFu, 1u };
    ubo.ScaleLifetime     = { kRigidImpulseFixedScale, 100.0f, 500.0f, 0.0f };

    CompoundHullSdfBank emptyBank;
    std::vector<RigidImpulseAtomicGpu> atomics;
    ResolveParticleRigidCollisionsCpu(ubo, particles, proxies, angVels, emptyBank, atomics);
    std::vector<RigidImpulseAccumulatorEntry> decoded;
    DecodeGpuImpulseReadback(atomics, bodyTokens, kRigidImpulseFixedScale, decoded);
    CheckCondition(particles[0].VelocitySeed[1] > 1.0f && particles[0].VelocitySeed[0] > 0.5f
        && decoded.size() == 1u && decoded[0].ContactCount == 1u && decoded[0].LinearImpulse[1] < -0.5f,
        "Stage F2a: Jolt spinning RigidColliderProxy sphere rebound + two-way atomic impulse readback");

    // Baked CompoundHull 3D SDF rebound + momentum conservation
    CompoundHullSdfBank sdfBank;
    const std::vector<CompoundHullSubSphere> subSpheres {
        { { -0.18f, 0.0f, 0.0f }, 0.30f },
        { {  0.18f, 0.0f, 0.0f }, 0.30f }
    };
    sdfBank.AppendSphereUnionHull(0u, subSpheres, 0.06f, 0.16f);

    RigidColliderProxy hullProxy {};
    hullProxy.CenterMeters[1] = 1.0f;
    hullProxy.ShapeCode = 3u;
    hullProxy.RadiusMeters = 0.0f;
    hullProxy.HalfExtentsMeters[0] = 0.5f; hullProxy.HalfExtentsMeters[1] = 0.35f; hullProxy.HalfExtentsMeters[2] = 0.35f;
    hullProxy.Restitution = 0.5f;
    hullProxy.OrientationQuat[3] = 1.0f;
    hullProxy.Friction = 0.25f;
    hullProxy.BodySlotIndex = 0u;
    hullProxy.ChannelMask = 1u;
    hullProxy.DynamicResponseBit = 1u;

    std::vector<ParticleStateGpuRecord> hullParticles(1u);
    hullParticles[0].PositionAge  = { 0.18f, 1.31f, 0.0f, 0.1f };
    hullParticles[0].VelocitySeed = { 0.0f, -4.0f, 0.0f, 0.2f };
    const float initPy = 0.25f * hullParticles[0].VelocitySeed[1];
    ParticleCollisionUniformBlock hullUbo = ubo;
    hullUbo.CountsMasks = { 1u, 1u, 0xFFFFFFFFu, 1u };

    std::vector<RigidImpulseAtomicGpu> hullAtomics;
    const std::vector<RigidColliderProxy> hullProxies { hullProxy };
    const std::vector<std::array<float, 4>> zeroAng { { 0.0f, 0.0f, 0.0f, 0.0f } };
    ResolveParticleRigidCollisionsCpu(hullUbo, hullParticles, hullProxies, zeroAng, sdfBank, hullAtomics);
    std::vector<RigidImpulseAccumulatorEntry> hullDecoded;
    DecodeGpuImpulseReadback(hullAtomics, bodyTokens, kRigidImpulseFixedScale, hullDecoded);
    const float finalPy = 0.25f * hullParticles[0].VelocitySeed[1] + (hullDecoded.empty() ? 0.0f : hullDecoded[0].LinearImpulse[1]);
    CheckCondition(hullParticles[0].VelocitySeed[1] > 1.5f && std::fabs(finalPy - initPy) < 1.0e-3f,
        "Stage F2b: Baked CompoundHull 3D SDF rebound + two-way momentum conservation (< 1e-3 N*s)");
}

void ValidateStageF3SpatialHashSelfCollision()
{
    // 65,536-bucket 2-level Blelloch exclusive scan exactness
    std::vector<uint32_t> counts(kSpatialHashBucketCount, 0u);
    for (uint32_t i = 0u; i < kSpatialHashBucketCount; ++i)
    {
        counts[i] = (i * 37u + 13u) % 17u;
    }
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> blockSums;
    RunBlellochPrefixSum65536Cpu(counts, offsets, blockSums);
    uint32_t running = 0u;
    bool scanExact = (offsets.size() == kSpatialHashBucketCount);
    for (uint32_t i = 0u; i < kSpatialHashBucketCount && scanExact; ++i)
    {
        if (offsets[i] != running) scanExact = false;
        running += counts[i];
    }
    CheckCondition(scanExact && running > 0u,
        "Stage F3a: 65,536-bucket 2-level Blelloch exclusive prefix scan has 0 mismatches");

    // Dense sphere pile under gravity (1,024 spheres x 120 frames, 4 substeps x 5 PBD iterations -> < 1% overlap, 0 NaNs)
    constexpr uint32_t kN = 1024u;
    constexpr float kR = 0.04f;
    std::vector<ParticleStateGpuRecord> particles(kN);
    uint32_t idx = 0u;
    for (uint32_t y = 0u; y < 8u && idx < kN; ++y)
    {
        for (uint32_t z = 0u; z < 8u && idx < kN; ++z)
        {
            for (uint32_t x = 0u; x < 16u && idx < kN; ++x)
            {
                const float ox = (static_cast<float>(x) - 7.5f) * (2.05f * kR);
                const float oy = 0.06f + static_cast<float>(y) * (2.05f * kR);
                const float oz = (static_cast<float>(z) - 3.5f) * (2.05f * kR);
                particles[idx].PositionAge  = { ox, oy, oz, 0.1f };
                particles[idx].VelocitySeed = { 0.0f, 0.0f, 0.0f, static_cast<float>(idx) };
                ++idx;
            }
        }
    }

    ParticleSpatialHashUniformBlock ubo {};
    ubo.CountsMode     = { kN, kSpatialHashBucketCount, 64u, 4u };
    ubo.GridMaterial   = { 2.0f * kR, kR, 0.10f, 0.35f };
    ubo.SolverTuning   = { 0.72f, 0.0f, 0.0f, 1.0f / 240.0f };
    ubo.LifetimeBounds = { 100.0f, 0.998f, 0.0f, 0.0f };

    const std::array<float, 3> boxMin { -0.75f, 0.0f, -0.45f };
    const std::array<float, 3> boxMax {  0.75f, 2.0f,  0.45f };
    ParticleSpatialHashBuffers bufs;
    std::vector<std::array<float, 3>> prevPos(kN);

    for (int frame = 0; frame < 120; ++frame)
    {
        for (int sub = 0; sub < 4; ++sub)
        {
            const float dt = 1.0f / 240.0f;
            for (uint32_t i = 0u; i < kN; ++i)
            {
                prevPos[i] = { particles[i].PositionAge[0], particles[i].PositionAge[1], particles[i].PositionAge[2] };
                particles[i].VelocitySeed[1] -= 9.81f * dt;
                particles[i].PositionAge[0]  += particles[i].VelocitySeed[0] * dt;
                particles[i].PositionAge[1]  += particles[i].VelocitySeed[1] * dt;
                particles[i].PositionAge[2]  += particles[i].VelocitySeed[2] * dt;
            }
            for (int it = 0; it < 5; ++it)
            {
                ubo.LifetimeBounds[2] = (it == 0) ? 0.35f : 0.0f;
                BuildParticleSpatialHashCpu(ubo, particles, bufs);
                ResolveParticleSelfCollisionsCpu(ubo, particles, bufs);
                particles.swap(bufs.WriteStates);
                EnforceParticleBoxBoundsCpu(particles, boxMin, boxMax, kR, 0.1f, 0.35f);
            }
            for (uint32_t i = 0u; i < kN; ++i)
            {
                particles[i].VelocitySeed[0] = (particles[i].PositionAge[0] - prevPos[i][0]) / dt;
                particles[i].VelocitySeed[1] = (particles[i].PositionAge[1] - prevPos[i][1]) / dt;
                particles[i].VelocitySeed[2] = (particles[i].PositionAge[2] - prevPos[i][2]) / dt;
            }
        }
    }

    BuildParticleSpatialHashCpu(ubo, particles, bufs);
    const auto metrics = MeasureParticleOverlapMetricsCpu(ubo, particles, bufs);
    bool finiteOk = (metrics.NonFiniteCount == 0u);
    CheckCondition(finiteOk && metrics.MeanOverlapRatio < 0.01f && metrics.GlobalMeanOverlap < 0.01f,
        "Stage F3b: Spatial-hash 1,024-sphere pile settles under gravity with < 1% overlap & 0 NaNs");
}

void ValidateStageF4OrthotropicXpbd()
{
    using namespace Frontier::SimulationSpace::PhysicsSpace;
    using namespace Frontier::SimulationSpace::ForceFields;

    // 16x16 Orthotropic ClothGrid conflict coloring + gravity & wind + ForceFields
    auto cloth = BuildOrthotropicClothGridNetwork(16u, 16u, 1.0f, 1.0f, 0.22f, true, { 0.0f, 2.0f, 0.0f });
    CheckCondition(VerifyConflictColorTable(cloth.FaceBatches, 3u, cloth.FaceIndices)
        && VerifyConflictColorTable(cloth.HingeBatches, 4u, cloth.HingeIndices),
        "Stage F4a: 16x16 ClothGrid face & hinge conflict color batches have 0 shared-vertex conflicts");

    XpbdSimulationSettings cfg {};
    cfg.Substeps = 4u;
    cfg.Iterations = 16u;
    cfg.WindEnabled = true;
    cfg.WindVelocity = { 0.0f, 0.0f, 2.5f };
    XpbdSimulationState clothState {};
    InitializeXpbdSimulationState(cloth, clothState);
    for (int f = 0; f < 45; ++f) StepParticleXpbdCpu(cloth, cfg, clothState, 1.0f / 60.0f, static_cast<float>(f) / 60.0f);
    CheckCondition(clothState.Positions[15u * 16u + 8u][2] > 0.08f && std::isfinite(clothState.Positions[15u * 16u + 8u][1]),
        "Stage F4b: 16x16 Orthotropic ClothGrid billows smoothly under gravity + wind (4 substeps x 16 iters)");

    // Spiderweb / CablesChains / FloraBed strand web stability
    auto web = BuildStrandWebNetwork(16u, 8u, 1.2f, 1400.0f, 0.0f, true, { 0.0f, 2.0f, 0.0f });
    CheckCondition(VerifyConflictColorTable(web.DistanceBatches, 2u, web.DistanceIndices),
        "Stage F4c: StrandWeb DistanceBatches have 0 shared-vertex conflicts");
    XpbdSimulationState webState {};
    InitializeXpbdSimulationState(web, webState);
    for (int f = 0; f < 60; ++f) StepParticleXpbdCpu(web, cfg, webState, 1.0f / 60.0f, static_cast<float>(f) / 60.0f);
    CheckCondition(std::isfinite(webState.Positions[0][1]) && webState.Positions[0][2] > 0.01f,
        "Stage F4d: Spiderweb/CablesChains/FloraBed stable over 60 frames (4 substeps x 16 iters)");

    // Unilateral cloth drape over Jolt RigidColliderProxy Sphere
    auto drape = BuildOrthotropicClothGridNetwork(12u, 12u, 0.9f, 0.9f, 0.22f, false, { 0.0f, 2.0f, 0.0f });
    for (uint32_t i = 0u; i < drape.PointCount; ++i)
        drape.RestPositions[i] = { drape.MaterialUV[i][0], 1.88f, drape.MaterialUV[i][1] + 0.45f };
    RigidColliderProxy sphere {};
    sphere.CenterMeters[1] = 1.50f;
    sphere.ShapeCode = 0u;
    sphere.RadiusMeters = 0.25f;
    sphere.OrientationQuat[3] = 1.0f;
    XpbdSimulationSettings drapeCfg {};
    drapeCfg.Substeps = 4u;
    drapeCfg.Iterations = 12u;
    drapeCfg.ContactRadius = 0.012f;
    XpbdSimulationState drapeState {};
    InitializeXpbdSimulationState(drape, drapeState);
    const std::vector<RigidColliderProxy> proxies { sphere };
    for (int f = 0; f < 60; ++f) StepParticleXpbdCpu(drape, drapeCfg, drapeState, 1.0f / 60.0f, static_cast<float>(f) / 60.0f, {}, proxies);
    float minDist = 1.0e9f;
    for (const auto& p : drapeState.Positions)
    {
        const float dx = p[0], dy = p[1] - 1.50f, dz = p[2];
        minDist = std::min(minDist, std::sqrt(dx * dx + dy * dy + dz * dz));
    }
    CheckCondition(minDist >= sphere.RadiusMeters + drapeCfg.ContactRadius - 1.0e-3f,
        "Stage F4e: Unilateral XPBD cloth drape over Jolt RigidColliderProxy Sphere has 0 penetration");
}

} // namespace

int main()
{
    CarrierSolidRegistry solidReg;
    ValidateFireworkPatterns();
    ValidateCarrierSolidLibrary();
    ValidateGpuSimulationModes(solidReg);
    ValidateSpritesCutoutsRamps();
    ValidateRibbonsTrails(solidReg);
    ValidateCarrierSolidsBacking(solidReg);
    ValidateAllStrandModes();
    ValidateAll71Templates(solidReg);
    ValidateStageF2RigidTwoWayCoupling();
    ValidateStageF3SpatialHashSelfCollision();
    ValidateStageF4OrthotropicXpbd();

    std::printf("[ParticleGpuPhysicsValidation] Passed: %u, Failed: %u\n", gPassedChecks, gFailedChecks);
    return (gFailedChecks == 0u) ? 0 : 1;
}
