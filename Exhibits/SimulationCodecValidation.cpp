/*============================================================================================================================================
                                                      SIMULATIONCODECVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Stage F (S28) Phase 5 end-to-end validation: byte-identical .ForceField, .FluidGrid, .VolumetricSpace, and .ParticleSpace round-trips,
//    zero-loss 54-preset + 71-template import, Tier-2 64B-aligned binary checksums, and unified cross-subsystem simulation pipeline proof.
// Reference: UnifiedFluidParticleForceFieldPortPlan.md §5.1 & §6 (S28)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Codecs/Simulation/FluidGridCodec.h"
#include "Engine/Codecs/Simulation/ForceFieldCodec.h"
#include "Engine/Codecs/Simulation/ParticleSpaceCodec.h"
#include "Engine/ParticleSpace/ParticleTemplateRegistry.h"
#include "Engine/ParticleSpace/ParticleXpbdNetworkBuilder.h"
#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/Fluid/FluidPresetRegistry.h"
#include "Engine/SimulationSpace/ForceFields/ForceFieldRegister.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

using namespace Frontier::Codecs::Simulation;
using namespace Frontier::ParticleSpace;
using namespace Frontier::SimulationSpace::EffectLayers;
using namespace Frontier::SimulationSpace::Fluid;
using namespace Frontier::SimulationSpace::ForceFields;
namespace fs = std::filesystem;

uint32_t gPassed = 0u;
uint32_t gFailed = 0u;

void CheckCondition(const bool ok, const char* label)
{
    if (ok)
    {
        ++gPassed;
    }
    else
    {
        ++gFailed;
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

void ValidateForceFieldCodecRoundTrip()
{
    ForceFieldDocumentModel doc {};
    doc.DocumentTitle = "StormVortexField";

    auto wind = ConstructDefaultForceNode(ForceModelOrdinal::DirectionalWind, "wind-0");
    wind.Name = "Crosswind";
    wind.PositionMeters = { 0.5f, 1.5f, -0.25f };
    wind.Direction = { 0.0f, 0.0f, 1.0f };
    wind.Strength = 11.5f;
    wind.RadiusMeters = 8.0f;
    wind.Falloff = ForceFalloffOrdinal::SmoothStep;
    doc.Forces.push_back(wind);

    auto vortex = ConstructDefaultForceNode(ForceModelOrdinal::RotationalColumn, "vortex-1");
    vortex.Name = "Cyclone";
    vortex.Strength = 16.0f;
    vortex.Frequency = 1.4f;
    vortex.Secondary = 0.45f;
    vortex.TargetChannels = static_cast<uint32_t>(ForceTargetChannelBits::Gas)
                          | static_cast<uint32_t>(ForceTargetChannelBits::Particle);
    doc.Forces.push_back(vortex);

    KeyTrack tr {};
    tr.Path = "wind-0/Strength";
    tr.Extrapolation = KeyExtrapolationOrdinal::PingPong;
    tr.PeriodSeconds = 2.0;
    tr.Keys.push_back(TrackKey{ 0.0, 4.0,  KeyEaseOrdinal::Smooth, { 0.0, 0.0, 0.0 }, { 2.0, 0.0, 0.0 } });
    tr.Keys.push_back(TrackKey{ 1.0, 14.0, KeyEaseOrdinal::Bezier, { 1.5, 0.0, 0.0 }, { -1.5, 0.0, 0.0 } });
    tr.Keys.push_back(TrackKey{ 2.0, 6.0,  KeyEaseOrdinal::Linear, { 0.0, 0.0, 0.0 }, { 0.0, 0.0, 0.0 } });
    doc.Tracks.push_back(tr);

    doc.VectorGridBytes.resize(96u);
    for (std::size_t i = 0u; i < doc.VectorGridBytes.size(); ++i)
    {
        doc.VectorGridBytes[i] = static_cast<std::byte>((i * 29u + 7u) & 0xFFu);
    }

    std::vector<std::byte> bytes1, bytes2;
    std::string err;
    CheckCondition(ForceFieldCodec::EncodeToBytes(doc, bytes1, &err), "T1a: .ForceField encodes cleanly");

    ForceFieldDocumentModel decoded {};
    CheckCondition(ForceFieldCodec::DecodeFromBytes(bytes1, decoded, &err), "T1b: .ForceField decodes cleanly");
    CheckCondition(ForceFieldCodec::EncodeToBytes(decoded, bytes2, &err) && bytes1 == bytes2,
        "T1c: .ForceField round-trip is strictly byte-identical (TOML + 64B Tier-2 binary)");
    CheckCondition(decoded.Forces.size() == 2u && decoded.Tracks.size() == 1u
        && decoded.Tracks[0].Keys.size() == 3u && decoded.VectorGridBytes == doc.VectorGridBytes,
        "T1d: .ForceField preserves all force models, KeyTracks, and Tier-2 binary bytes");

    // Corrupt a single payload byte and verify FNV-1a checksum rejection
    std::vector<std::byte> corrupted = bytes1;
    corrupted[corrupted.size() - 65u] ^= std::byte{ 0x55u };
    ForceFieldDocumentModel badDoc {};
    CheckCondition(!ForceFieldCodec::DecodeFromBytes(corrupted, badDoc, &err),
        "T1e: .ForceField rejects corrupted Tier-2 binary checksum");
}

void ValidateFluidGridVolumetricCodec(const EffectLayerSpecificationSet& specs, const fs::path& root)
{
    FluidPresetRegistry presetReg;
    std::string err;
    CheckCondition(presetReg.LoadFromDirectory((root / "EngineContent/Simulation/Presets").string(), specs, &err)
        && presetReg.QueryPresets().size() == 54u,
        "T2a: Zero-loss import of all 54 EngineContent/Simulation/Presets/*.FluidGrid");

    // Round-trip both .FluidGrid and .VolumetricSpace with 64B-aligned Tier-2 warm-start RGBA16F checkpoint
    const auto& preset0 = presetReg.QueryPresets()[0];
    FluidGridDocumentModel doc {};
    doc.DocumentTitle   = preset0.Name;
    doc.ContainerSuffix = ".FluidGrid";
    doc.Grid            = preset0.Grid;
    doc.GasStack        = preset0.GasStack;
    doc.Sources         = preset0.ReleaseSources;
    doc.Forces.push_back(ConstructDefaultForceNode(ForceModelOrdinal::DirectionalWind, "preset-wind-0"));
    doc.HasWarmStart    = true;
    doc.WarmStart.SimulationSeconds = 1.25;
    doc.WarmStart.StepIndex         = 75u;
    doc.WarmStart.GridResolution    = { 8u, 8u, 8u };
    doc.WarmStart.VelocityRgba.assign(8u * 8u * 8u * 4u, 0.25f);
    doc.WarmStart.StateRgba.assign(8u * 8u * 8u * 4u, 0.5f);
    doc.WarmStart.PressureLightRgba.assign(8u * 8u * 8u * 4u, 0.125f);

    std::vector<std::byte> fg1, fg2;
    CheckCondition(FluidGridCodec::EncodeToBytes(specs, doc, fg1, &err), "T2b: .FluidGrid encodes with Tier-2 warm-start");
    FluidGridDocumentModel fgDecoded {};
    CheckCondition(FluidGridCodec::DecodeFromBytes(specs, fg1, fgDecoded, &err), "T2c: .FluidGrid decodes with Tier-2 warm-start");
    CheckCondition(FluidGridCodec::EncodeToBytes(specs, fgDecoded, fg2, &err) && fg1 == fg2,
        "T2d: .FluidGrid round-trip is strictly byte-identical");
    CheckCondition(fgDecoded.HasWarmStart && fgDecoded.WarmStart.StepIndex == 75u
        && fgDecoded.WarmStart.VelocityRgba == doc.WarmStart.VelocityRgba,
        "T2e: .FluidGrid preserves exact Tier-2 warm-start RGBA16F state");

    // .VolumetricSpace suffix round-trip
    doc.ContainerSuffix = ".VolumetricSpace";
    std::vector<std::byte> vs1, vs2;
    FluidGridDocumentModel vsDecoded {};
    CheckCondition(FluidGridCodec::EncodeToBytes(specs, doc, vs1, &err)
        && FluidGridCodec::DecodeFromBytes(specs, vs1, vsDecoded, &err)
        && FluidGridCodec::EncodeToBytes(specs, vsDecoded, vs2, &err)
        && vs1 == vs2 && vsDecoded.ContainerSuffix == ".VolumetricSpace",
        "T2f: .VolumetricSpace round-trip is strictly byte-identical");

    // Round-trip all 54 imported presets through FluidGridCodec
    uint32_t identicalPresets = 0u;
    for (const auto& p : presetReg.QueryPresets())
    {
        FluidGridDocumentModel pm {};
        pm.DocumentTitle = p.Name;
        pm.Grid          = p.Grid;
        pm.GasStack      = p.GasStack;
        pm.Sources       = p.ReleaseSources;
        std::vector<std::byte> b1, b2;
        FluidGridDocumentModel dec {};
        if (FluidGridCodec::EncodeToBytes(specs, pm, b1, &err)
            && FluidGridCodec::DecodeFromBytes(specs, b1, dec, &err)
            && FluidGridCodec::EncodeToBytes(specs, dec, b2, &err)
            && b1 == b2)
        {
            ++identicalPresets;
        }
    }
    CheckCondition(identicalPresets == 54u, "T2g: All 54/54 .FluidGrid presets round-trip strictly byte-identical");
}

void ValidateParticleSpaceCodec(const EffectLayerSpecificationSet& specs, const fs::path& root)
{
    ParticleTemplateRegistry ptReg;
    std::string err;
    CheckCondition(ptReg.LoadFromDirectory(specs, (root / "EngineContent/Simulation/ParticleTemplates").string(), &err)
        && ptReg.QueryTemplates().size() == 71u,
        "T3a: Zero-loss import of all 71 EngineContent/Simulation/ParticleTemplates/*.ParticleSpace");

    // Round-trip a ParticleSpace with opt-in additive Layer 220 (XPBD) + Layer 221 (GPU Collisions) + 64B-aligned packed XPBD tables
    ParticleSpaceDocumentModel doc {};
    doc.DocumentTitle = "OrthotropicClothFlag";
    doc.NodeId        = "cloth-flag-0";
    doc.TemplateId    = "custom-cloth";
    doc.Stack.Token   = "particle-cloud";
    doc.Stack.Category = LayerStackOrdinal::ParticleCloud;

    EffectLayerEntry spawn {};
    spawn.Token        = "spawn-0";
    spawn.CategoryCode = kParticleSpawnLayerCode;
    doc.Stack.Layers.push_back(spawn);

    EffectLayerEntry xpbd {};
    xpbd.Token        = "xpbd-0";
    xpbd.CategoryCode = kParticleXpbdLayerCode;
    xpbd.SetParameter("XpbdTopology", int64_t{ 1 });
    xpbd.SetParameter("XpbdSubsteps", int64_t{ 4 });
    xpbd.SetParameter("XpbdIterations", int64_t{ 16 });
    xpbd.SetParameter("XpbdWarpModulus", 1600.0);
    xpbd.SetParameter("XpbdWeftModulus", 1400.0);
    doc.Stack.Layers.push_back(xpbd);

    EffectLayerEntry gpuCol {};
    gpuCol.Token        = "gpu-col-0";
    gpuCol.CategoryCode = kParticleGpuCollisionLayerCode;
    gpuCol.SetParameter("CollisionRigidCoupling", true);
    gpuCol.SetParameter("CollisionSelfHash", true);
    gpuCol.SetParameter("CollisionParticleRadius", 0.04);
    doc.Stack.Layers.push_back(gpuCol);

    const auto net = BuildOrthotropicClothGridNetwork(16u, 16u, 1.0f, 1.0f, 0.22f, true);
    doc.HasXpbdTables = true;
    doc.XpbdTables    = PackXpbdGpuTables(net);
    doc.CustomSolidBytes.assign(128u, std::byte{ 0x3Cu });

    std::vector<std::byte> b1, b2;
    ParticleSpaceDocumentModel decoded {};
    CheckCondition(ParticleSpaceCodec::EncodeToBytes(specs, doc, b1, &err), "T3b: .ParticleSpace encodes with Layers 220/221 + XPBD binary");
    CheckCondition(ParticleSpaceCodec::DecodeFromBytes(specs, b1, decoded, &err), "T3c: .ParticleSpace decodes cleanly");
    CheckCondition(ParticleSpaceCodec::EncodeToBytes(specs, decoded, b2, &err) && b1 == b2,
        "T3d: .ParticleSpace round-trip is strictly byte-identical");
    CheckCondition(decoded.HasXpbdTables
        && decoded.XpbdTables.U32Topology == doc.XpbdTables.U32Topology
        && decoded.XpbdTables.F32Rest == doc.XpbdTables.F32Rest
        && decoded.CustomSolidBytes == doc.CustomSolidBytes,
        "T3e: .ParticleSpace preserves exact pre-colored XPBD GPU tables & custom solid payload");

    // Round-trip all 71 imported templates through ParticleSpaceCodec
    uint32_t identicalTemplates = 0u;
    for (const auto& entry : ptReg.QueryTemplates())
    {
        auto cloud = ptReg.ConstructParticleCloud(specs, entry.Id, "rt-" + entry.Id);
        if (!cloud) continue;
        ParticleSpaceDocumentModel pm {};
        pm.DocumentTitle = entry.Name;
        pm.NodeId        = cloud->NodeId;
        pm.TemplateId    = entry.Id;
        pm.Stack         = cloud->Stack;
        pm.Tracks        = cloud->Tracks;
        std::vector<std::byte> t1, t2;
        ParticleSpaceDocumentModel dec {};
        if (ParticleSpaceCodec::EncodeToBytes(specs, pm, t1, &err)
            && ParticleSpaceCodec::DecodeFromBytes(specs, t1, dec, &err)
            && ParticleSpaceCodec::EncodeToBytes(specs, dec, t2, &err)
            && t1 == t2)
        {
            ++identicalTemplates;
        }
    }
    CheckCondition(identicalTemplates == 71u, "T3f: All 71/71 .ParticleSpace templates round-trip strictly byte-identical");
}

void ValidateEndToEndCrossSubsystemPipeline()
{
    // Build a live ForceField -> Orthotropic XPBD pipeline, serialize + reload + execute!
    ForceFieldDocumentModel ffDoc {};
    auto wind = ConstructDefaultForceNode(ForceModelOrdinal::DirectionalWind, "gust-0");
    wind.Direction = { 0.0f, 0.0f, 1.0f };
    wind.Strength  = 10.0f;
    wind.RadiusMeters = 12.0f;
    wind.Falloff = ForceFalloffOrdinal::Constant;
    ffDoc.Forces.push_back(wind);

    std::vector<std::byte> ffBytes;
    ForceFieldDocumentModel ffReloaded {};
    std::string err;
    CheckCondition(ForceFieldCodec::EncodeToBytes(ffDoc, ffBytes, &err)
        && ForceFieldCodec::DecodeFromBytes(ffBytes, ffReloaded, &err),
        "T4a: End-to-end ForceField reload succeeds");

    ForceFieldRegister reg;
    for (const auto& f : ffReloaded.Forces)
    {
        ManagedForceEntry entry {};
        entry.Node = f;
        (void)reg.AddForceEntry(std::move(entry));
    }
    (void)reg.PublishEvaluatedReading(0.0, 1u);
    auto lease = reg.AcquirePublishedReading();
    CheckCondition(lease.Valid() && lease->ActiveCount == 1u, "T4b: PublishedForceFieldReading active");

    auto cloth = BuildOrthotropicClothGridNetwork(12u, 12u, 0.9f, 0.9f, 0.22f, true);
    XpbdSimulationSettings cfg {};
    cfg.Substeps = 4u;
    cfg.Iterations = 12u;
    XpbdSimulationState state {};
    InitializeXpbdSimulationState(cloth, state);
    const std::span<const ForceModelGpuRecord> forceSpan(lease->Records.data(), lease->ActiveCount);
    for (int f = 0; f < 30; ++f)
    {
        StepParticleXpbdCpu(cloth, cfg, state, 1.0f / 60.0f, static_cast<float>(f) / 60.0f, forceSpan);
    }
    CheckCondition(state.Positions[11u * 12u + 6u][2] > 0.15f && std::isfinite(state.Positions[11u * 12u + 6u][1]),
        "T4c: Reloaded .ForceField wind drives Orthotropic XPBD cloth end-to-end");
}

} // namespace

int main()
{
    const fs::path root = ResolveRepoRoot();
    std::string err;
    const auto specsOpt = LoadLayerSpecificationsFromFile(
        (root / "EngineContent/Simulation/LayerSpecifications.VolumetricSpace").string(), &err);
    CheckCondition(specsOpt.has_value()
        && specsOpt->CoreSpecificationHash == "885cc8c5a43d9053"
        && specsOpt->Layers.size() == 30u,
        "T0: LayerSpecifications.VolumetricSpace loads 30 layers with intact CoreSpecificationHash");

    if (specsOpt.has_value())
    {
        ValidateForceFieldCodecRoundTrip();
        ValidateFluidGridVolumetricCodec(*specsOpt, root);
        ValidateParticleSpaceCodec(*specsOpt, root);
        ValidateEndToEndCrossSubsystemPipeline();
    }

    std::printf("[SimulationCodecValidation] Passed: %u, Failed: %u\n", gPassed, gFailed);
    return (gFailed == 0u) ? 0 : 1;
}
