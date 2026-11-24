/*============================================================================================================================================
                                                      FLUIDSOLVERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Stage D (Slice S18) Headless Fluid Solver Validation Suite
// Verifies: 54 .FluidGrid presets, Fixed/WorldGrid/Infinite SparseTileBank, MacCormack + projection + thermodynamics,
//           8,192 tracers + 8,192 debris (Rubble/Ember/Paper), 192 MiB SimulationCheckpointRing rewind,
//           Volume raymarching + SimulationPostOverlay, and transparent RGBA8 flipbook PNG export.

#include "Engine/SimulationSpace/EffectLayers/EffectLayerSpecification.h"
#include "Engine/SimulationSpace/EffectLayers/SimulationCheckpointRing.h"
#include "Engine/SimulationSpace/Fluid/FluidGpuSolver.h"
#include "Engine/SimulationSpace/Fluid/FluidPresetRegistry.h"
#include "Engine/SimulationSpace/Fluid/FluidSimulationPostOverlay.h"
#include "Engine/SimulationSpace/Fluid/FluidVolumeRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

namespace EL = Frontier::SimulationSpace::EffectLayers;
namespace FL = Frontier::SimulationSpace::Fluid;
namespace FF = Frontier::SimulationSpace::ForceFields;

int gPassed = 0;
int gFailed = 0;

void Check(const bool condition, const char* label)
{
    if (condition)
    {
        ++gPassed;
    }
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

[[nodiscard]] bool GridIsFinite(const FL::FluidVolumeGridCpu& g) noexcept
{
    for (const auto& c : g.Cells)
    {
        if (!std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2]) || !std::isfinite(c[3]))
        {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool GridsBitEqual(const FL::FluidVolumeGridCpu& a, const FL::FluidVolumeGridCpu& b) noexcept
{
    if (a.Width != b.Width || a.Height != b.Height || a.Depth != b.Depth || a.Cells.size() != b.Cells.size())
    {
        return false;
    }
    return std::memcmp(a.Cells.data(), b.Cells.data(), a.Cells.size() * sizeof(std::array<float, 4>)) == 0;
}

} // namespace

int main()
{
    std::cout << "=== Frontier Stage D FluidSolverValidation (Slice S18) ===\n";

    // --------------------------------------------------------------------------------------------------------------------
    // 1. Load LayerSpecifications.VolumetricSpace + all 54 .FluidGrid presets
    // --------------------------------------------------------------------------------------------------------------------
    std::string err;
    const auto specsOpt = EL::LoadLayerSpecificationsFromFile("EngineContent/Simulation/LayerSpecifications.VolumetricSpace", &err);
    Check(specsOpt.has_value(), "Load LayerSpecifications.VolumetricSpace");
    if (!specsOpt.has_value())
    {
        std::cerr << "Error: " << err << "\n";
        return 1;
    }
    const auto& specs = *specsOpt;

    FL::FluidPresetRegistry registry;
    const bool loadedPresets = registry.LoadFromDirectory("EngineContent/Simulation/Presets", specs, &err);
    Check(loadedPresets, "Load FluidPresetRegistry from EngineContent/Simulation/Presets");
    const auto presets = registry.QueryPresets();
    std::cout << "[INFO] Loaded " << presets.size() << " .FluidGrid presets\n";
    Check(presets.size() == 54u, "Exactly 54 .FluidGrid presets loaded");

    uint32_t infinitePresetCount = 0u;
    uint32_t presetStepCleanCount = 0u;
    for (const auto& preset : presets)
    {
        if (preset.Grid.Topology == FL::FluidGridTopologyOrdinal::Infinite)
        {
            ++infinitePresetCount;
        }
        const auto runtime = FL::EvaluateFluidPresetRuntime(specs, preset, 0.25);

        // Run a compact 2-step headless smoke test for every preset (12^3 Fixed proxy centred at origin like FluidStudio)
        const float maxDim = std::max({ preset.Grid.SizeMeters[0], preset.Grid.SizeMeters[1], preset.Grid.SizeMeters[2], 1.0f });
        const float cellSize = maxDim / 12.0f;

        FL::FluidCpuSimulationState sim {};
        sim.InitializeFixed({ 12u, 12u, 12u }, cellSize, { 0.0f, 0.0f, 0.0f });
        auto stepCfg = runtime.StepSettings;
        stepCfg.PressureIterations = 8u;
        for (int s = 0; s < 2; ++s)
        {
            FL::StepFluidSimulationCpu(sim, stepCfg, runtime.ActiveSources, preset.ForceRecords, preset.Colliders, 1.0f / 60.0f);
        }
        if (GridIsFinite(sim.Velocity) && GridIsFinite(sim.State) && GridIsFinite(sim.Pressure) && GridIsFinite(sim.Curl))
        {
            ++presetStepCleanCount;
        }
    }
    std::cout << "[INFO] Presets evaluated & stepped cleanly: " << presetStepCleanCount << "/" << presets.size()
              << " (Infinite presets: " << infinitePresetCount << ")\n";
    Check(presetStepCleanCount == 54u, "All 54 .FluidGrid presets step with 0 NaN/Inf");
    Check(infinitePresetCount == 2u, "Exactly 2 Infinite .FluidGrid presets present");

    // --------------------------------------------------------------------------------------------------------------------
    // 2. Infinite SparseTileBank verification (preset-infinite-plume & preset-infinite-drift)
    // --------------------------------------------------------------------------------------------------------------------
    for (const char* infId : { "preset-infinite-plume", "preset-infinite-drift" })
    {
        const auto* infPreset = registry.FindById(infId);
        Check(infPreset != nullptr, infId);
        if (infPreset != nullptr)
        {
            const auto runtime = FL::EvaluateFluidPresetRuntime(specs, *infPreset, 0.0);
            constexpr uint32_t kTileCells = 8u;
            constexpr uint32_t kMaxSlots  = 16u;
            const float cellSize = infPreset->Grid.TileSizeMeters / static_cast<float>(kTileCells);

            FL::SparseTileBank bank;
            bank.ConfigureInfinite(kTileCells, cellSize, kMaxSlots);
            Check(bank.IsSparse(), "Infinite preset initializes SparseTileBank");

            std::vector<FL::SparsePinnedSource> pinned;
            for (const auto& s : runtime.ActiveSources)
            {
                if (s.Enabled)
                {
                    pinned.push_back(FL::SparsePinnedSource{ s.PositionMeters, s.RadiusMeters });
                }
            }
            bank.ScheduleTiles(pinned, 0u, {});
            const uint32_t initialActive = bank.QueryResidentTileCount();

            // Simulate strong summed occupancy & face flux above growth threshold (0.0025 * bandSamples)
            std::vector<float> occ(static_cast<size_t>(kMaxSlots) * 3u * 4u, 0.0f);
            for (uint32_t s = 0u; s < kMaxSlots; ++s)
            {
                if (bank.QuerySlots()[s].has_value())
                {
                    // Texel 0: (sumDensity, peakDensity, face -X, face +X), Texel 1: (face -Y, face +Y, face -Z, face +Z)
                    const size_t base0 = static_cast<size_t>(s) * 12u;
                    occ[base0 + 0u] = 20.0f; occ[base0 + 1u] = 0.80f; occ[base0 + 2u] = 8.0f; occ[base0 + 3u] = 8.0f;
                    occ[base0 + 4u] =  0.0f; occ[base0 + 5u] = 12.0f; occ[base0 + 6u] = 8.0f; occ[base0 + 7u] = 8.0f;
                }
            }
            bank.ScheduleTiles(pinned, bank.QueryScheduleInterval(), occ);
            const uint32_t finalActive = bank.QueryResidentTileCount();
            std::cout << "[INFO] " << infId << " sparse active tiles: " << initialActive << " -> " << finalActive << "\n";
            Check(initialActive >= 1u && finalActive > initialActive, "Infinite sparse tile bank grows resident tiles");
        }
    }

    // --------------------------------------------------------------------------------------------------------------------
    // 3. Projection divergence drop (ClosedBox 24^3 -> interior max|div| < 1e-2)
    // --------------------------------------------------------------------------------------------------------------------
    {
        FL::FluidCpuSimulationState sim {};
        sim.InitializeFixed({ 24u, 24u, 24u }, 4.0f / 24.0f, { 0.0f, 2.0f, 0.0f });
        const auto vol = sim.TileBank.BuildVolumeUniforms(FL::FluidBoundaryOrdinal::ClosedBox, {});

        // Smooth ClosedBox-compatible compressible velocity v = grad(cos(pi*x)*cos(pi*y)*cos(pi*z))
        // Zero normal velocity on all 6 ClosedBox walls; projection must remove interior divergence (< 1e-2).
        constexpr float kPi = 3.14159265358979323846f;
        for (uint32_t z = 0u; z < 24u; ++z)
        {
            for (uint32_t y = 0u; y < 24u; ++y)
            {
                for (uint32_t x = 0u; x < 24u; ++x)
                {
                    const float nx = (static_cast<float>(x) + 0.5f) / 24.0f;
                    const float ny = (static_cast<float>(y) + 0.5f) / 24.0f;
                    const float nz = (static_cast<float>(z) + 0.5f) / 24.0f;
                    const float vx = 0.45f * std::sin(kPi * nx) * std::cos(kPi * ny) * std::cos(kPi * nz);
                    const float vy = 0.45f * std::cos(kPi * nx) * std::sin(kPi * ny) * std::cos(kPi * nz);
                    const float vz = 0.45f * std::cos(kPi * nx) * std::cos(kPi * ny) * std::sin(kPi * nz);
                    sim.Velocity.Cells[sim.Velocity.LinearIndex(static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z))]
                        = { vx, vy, vz, 0.0f };
                }
            }
        }
        const float divBefore = FL::MeasureMaxFluidDivergenceCpu(vol, sim.Velocity, 2u);

        FL::FluidDivergenceUniformBlock divU {};
        divU.Volume = vol;
        FL::RunFluidDivergenceCpu(divU, sim.Velocity, sim.State, sim.Divergence);

        FL::FluidPressureUniformBlock prsU {};
        prsU.Volume = vol;
        for (uint32_t iter = 0u; iter < 600u; ++iter)
        {
            prsU.PressureControl[0] = (iter == 0u) ? 0.96f : 1.0f;
            FL::RunFluidPressureJacobiCpu(prsU, sim.Pressure, sim.Divergence, sim.ScratchA);
            std::swap(sim.Pressure, sim.ScratchA);
        }

        FL::FluidGradientShiftUniformBlock gradU {};
        gradU.Volume = vol;
        FL::RunFluidGradientCpu(gradU, sim.Pressure, sim.Velocity, sim.ScratchA);
        std::swap(sim.Velocity, sim.ScratchA);

        const float divAfter = FL::MeasureMaxFluidDivergenceCpu(vol, sim.Velocity, 2u);
        std::cout << "[INFO] Projection interior max|div|: " << divBefore << " -> " << divAfter << "\n";
        Check(divBefore > 0.5f && divAfter < 0.01f, "Divergence projection drops interior max|div| below 1e-2");
    }

    // --------------------------------------------------------------------------------------------------------------------
    // 4. 8,192 Tracers + 8,192 Debris (Rubble, Ember, Paper) + 192 MiB SimulationCheckpointRing bit-exact rewind
    // --------------------------------------------------------------------------------------------------------------------
    const auto* campfire = registry.FindById("preset-campfire");
    Check(campfire != nullptr, "Find preset-campfire");

    FL::FluidCpuSimulationState campSim {};
    // Match campfire.FluidGrid (Size = [3, 5, 3], centred at origin -> Y in [-2.5, +2.5], source at [0, -2.3, 0])
    campSim.InitializeFixed({ 20u, 24u, 20u }, 5.0f / 24.0f, { 0.0f, 0.0f, 0.0f });
    const auto campRuntime = FL::EvaluateFluidPresetRuntime(specs, *campfire, 0.0);

    // Prepare 8,192 tracers (128x64) + 8,192 debris (128x64) with all 3 debris kinds
    constexpr uint32_t kBufW = 128u;
    constexpr uint32_t kBufH = 64u;
    static_assert(kBufW * kBufH == 8192u, "8,192 tracer/debris capacity");

    EL::FluidTracerDebrisBufferSet tdBuffers {};
    std::vector<FL::FluidPresetSourceEntry> debrisSources;
    for (uint32_t k = 0u; k < 3u; ++k)
    {
        FL::FluidPresetSourceEntry e {};
        e.Token                 = "debris_" + std::to_string(k);
        e.Source.Enabled        = true;
        e.Source.PositionMeters = { (static_cast<float>(k) - 1.0f) * 0.4f, -1.8f, 0.0f };
        e.Source.VelocityMps    = { 0.0f, 2.5f, 0.0f };
        e.Source.RadiusMeters   = 0.35f;
        e.Source.EnvelopeWeight = 1.0f;
        e.DebrisAmount          = 2.0f;
        e.DebrisSpeed           = 2.8f;
        e.DebrisKind            = static_cast<FL::FluidDebrisKindOrdinal>(k);
        debrisSources.push_back(e);
    }
    FL::FluidDebrisSettings debrisCfg {};
    debrisCfg.Visible  = true;
    debrisCfg.Count    = 8192u;
    debrisCfg.Lifetime = 3.0f;
    debrisCfg.Gravity  = 9.8f;
    debrisCfg.Drag     = 1.2f;
    debrisCfg.Bounce   = 0.35f;

    EL::SimulationCheckpointRing checkpointRing(0.5, EL::kDefaultCheckpointBudgetBytes);
    auto campStep = campRuntime.StepSettings;
    campStep.PressureIterations = 12u;

    constexpr float kDt = 0.05f; // 20 Hz -> 10 steps = 0.5s, 30 steps = 1.5s
    FL::FluidCpuSimulationState snapshotAtHalfSecond {};
    EL::FluidTracerDebrisBufferSet tdAtHalfSecond {};
    bool savedHalfSecond = false;

    for (int step = 0; step < 30; ++step)
    {
        FL::StepFluidSimulationCpu(campSim, campStep, campRuntime.ActiveSources, campfire->ForceRecords, campfire->Colliders, kDt);
        const auto volU = campSim.TileBank.BuildVolumeUniforms(campStep.Boundary, campfire->Colliders);
        FL::StepFluidTracersCpu(volU, campSim.Velocity, kBufW, kBufH, kDt, static_cast<float>(campSim.SimulationTime), 4.0f, tdBuffers);
        FL::StepFluidDebrisCpu(volU, campSim.Velocity, debrisSources, debrisCfg, kBufW, kBufH, kDt, static_cast<float>(campSim.SimulationTime), tdBuffers);

        if (checkpointRing.IsCaptureDue(campSim.SimulationTime, kDt))
        {
            checkpointRing.Capture(campSim, kDt, &tdBuffers);
            if (!savedHalfSecond && std::fabs(campSim.SimulationTime - 0.5) < 0.03)
            {
                snapshotAtHalfSecond = campSim;
                tdAtHalfSecond       = tdBuffers;
                savedHalfSecond      = true;
            }
        }
    }

    // Verify all 8,192 tracers finite + moved, and all 3 debris kinds (Rubble/Ember/Paper) spawned & active
    bool tracersFinite = true;
    uint32_t tracersMoved = 0u;
    for (size_t i = 0u; i < tdBuffers.Tracers.size(); ++i)
    {
        const auto& c = tdBuffers.Tracers[i];
        const auto& p = tdBuffers.TracerPrevious[i];
        if (!std::isfinite(c[0]) || !std::isfinite(c[1]) || !std::isfinite(c[2]) || !std::isfinite(c[3]))
        {
            tracersFinite = false;
        }
        const float dx = c[0] - p[0], dy = c[1] - p[1], dz = c[2] - p[2];
        if (dx * dx + dy * dy + dz * dz > 1.0e-10f) ++tracersMoved;
    }

    uint32_t aliveDebrisByKind[3] { 0u, 0u, 0u };
    bool debrisFinite = true;
    for (size_t i = 0u; i < tdBuffers.DebrisPositions.size(); ++i)
    {
        const auto& p = tdBuffers.DebrisPositions[i];
        const auto& v = tdBuffers.DebrisVelocities[i];
        if (!std::isfinite(p[0]) || !std::isfinite(p[1]) || !std::isfinite(p[2]) || !std::isfinite(p[3])
         || !std::isfinite(v[0]) || !std::isfinite(v[1]) || !std::isfinite(v[2]) || !std::isfinite(v[3]))
        {
            debrisFinite = false;
        }
        if (p[3] < debrisCfg.Lifetime)
        {
            const int kind = std::clamp(static_cast<int>(std::floor(v[3])), 0, 2);
            ++aliveDebrisByKind[kind];
        }
    }
    std::cout << "[INFO] 8,192 Tracers moved=" << tracersMoved
              << " | 8,192 Debris alive: Rubble=" << aliveDebrisByKind[0]
              << " Ember=" << aliveDebrisByKind[1]
              << " Paper=" << aliveDebrisByKind[2] << "\n";
    Check(tdBuffers.Tracers.size() == 8192u && tracersFinite && tracersMoved > 1000u, "8,192 Tracers finite & advected");
    Check(tdBuffers.DebrisPositions.size() == 8192u && debrisFinite, "8,192 Debris finite");
    Check(aliveDebrisByKind[0] > 50u && aliveDebrisByKind[1] > 50u && aliveDebrisByKind[2] > 50u,
          "Debris spawns all 3 kinds (Rubble, Ember, Paper)");

    // Verify CheckpointRing summary, bit-exact restore to 0.5s, and InvalidateAfter
    const auto ringSummary = checkpointRing.QuerySummary(campSim, &tdBuffers);
    std::cout << "[INFO] CheckpointRing: count=" << ringSummary.Count
              << " capacity=" << ringSummary.Capacity
              << " bytesEach=" << ringSummary.BytesEach
              << " bytesUsed=" << ringSummary.BytesUsed << "\n";
    Check(savedHalfSecond, "Captured checkpoint at t=0.5s");
    Check(ringSummary.Count == 3u, "Captured 3 checkpoints (0.5s, 1.0s, 1.5s)");
    Check(ringSummary.BytesUsed <= EL::kDefaultCheckpointBudgetBytes, "CheckpointRing respects 192 MiB budget");

    const auto* cpHalf = checkpointRing.FindNewestAtOrBefore(0.75, campSim, &tdBuffers);
    Check(cpHalf != nullptr && std::fabs(cpHalf->TimeSeconds - 0.5) < 0.03, "FindNewestAtOrBefore(0.75) returns t=0.5s");
    if (cpHalf != nullptr)
    {
        checkpointRing.Restore(*cpHalf, campSim, &tdBuffers);
        Check(GridsBitEqual(campSim.Velocity, snapshotAtHalfSecond.Velocity), "Checkpoint rewind Velocity bit-exact");
        Check(GridsBitEqual(campSim.State,    snapshotAtHalfSecond.State),    "Checkpoint rewind State bit-exact");
        Check(GridsBitEqual(campSim.Pressure, snapshotAtHalfSecond.Pressure), "Checkpoint rewind Pressure bit-exact");
        Check(GridsBitEqual(campSim.Curl,     snapshotAtHalfSecond.Curl),     "Checkpoint rewind Curl bit-exact");
        Check(tdBuffers.Tracers == tdAtHalfSecond.Tracers,                    "Checkpoint rewind 8,192 Tracers bit-exact");
        Check(tdBuffers.DebrisPositions == tdAtHalfSecond.DebrisPositions,    "Checkpoint rewind 8,192 Debris bit-exact");
    }

    checkpointRing.InvalidateAfter(cpHalf != nullptr ? cpHalf->TimeSeconds : 0.501);
    Check(checkpointRing.QuerySummary(campSim, &tdBuffers).Count == 1u, "InvalidateAfter(0.5) keeps only t<=0.5s checkpoint");

    // --------------------------------------------------------------------------------------------------------------------
    // 5. Volume Raymarcher + Transparent RGBA8 Flipbook PNG Sheet Export (4x2 = 8 tiles)
    // --------------------------------------------------------------------------------------------------------------------
    {
        for (int s = 0; s < 10; ++s)
        {
            FL::StepFluidSimulationCpu(campSim, campStep, campRuntime.ActiveSources, campfire->ForceRecords, campfire->Colliders, kDt);
        }

        constexpr uint32_t kTileW = 32u;
        constexpr uint32_t kTileH = 32u;
        constexpr uint32_t kCols  = 4u;
        constexpr uint32_t kRows  = 2u;
        constexpr uint32_t kSheetW = kTileW * kCols;
        constexpr uint32_t kSheetH = kTileH * kRows;
        std::vector<uint8_t> sheetRgba(static_cast<size_t>(kSheetW) * static_cast<size_t>(kSheetH) * 4u, 0u);

        const auto camera = FL::BuildFluidPerspectiveCamera(
            { 0.0f, -0.4f, 5.2f },
            { 0.0f, -1.0f, 0.0f },
            0.733f,
            kTileW,
            kTileH);

        auto renderSettings = campRuntime.RenderSettings;
        renderSettings.Steps       = 32;
        renderSettings.Transparent = true;

        uint8_t cornerAlpha = 255u;
        uint8_t peakAlpha   = 0u;
        for (uint32_t tile = 0u; tile < kCols * kRows; ++tile)
        {
            if (tile > 0u)
            {
                FL::StepFluidSimulationCpu(campSim, campStep, campRuntime.ActiveSources, campfire->ForceRecords, campfire->Colliders, kDt);
            }
            const auto volU = campSim.TileBank.BuildVolumeUniforms(campStep.Boundary, campfire->Colliders);
            const auto ubo  = FL::BuildFluidVolumeRaymarchUniforms(volU, renderSettings, camera, false, tile);
            const uint32_t tx = (tile % kCols) * kTileW;
            const uint32_t ty = (tile / kCols) * kTileH;
            for (uint32_t py = 0u; py < kTileH; ++py)
            {
                for (uint32_t px = 0u; px < kTileW; ++px)
                {
                    const auto pxRgba = FL::RenderFluidVolumePixelCpu(
                        ubo, campSim.State, campSim.Pressure,
                        static_cast<float>(px) + 0.5f,
                        static_cast<float>(kTileH - 1u - py) + 0.5f);
                    const uint8_t r8 = static_cast<uint8_t>(std::clamp(pxRgba[0], 0.0f, 1.0f) * 255.0f + 0.5f);
                    const uint8_t g8 = static_cast<uint8_t>(std::clamp(pxRgba[1], 0.0f, 1.0f) * 255.0f + 0.5f);
                    const uint8_t b8 = static_cast<uint8_t>(std::clamp(pxRgba[2], 0.0f, 1.0f) * 255.0f + 0.5f);
                    const uint8_t a8 = static_cast<uint8_t>(std::clamp(pxRgba[3], 0.0f, 1.0f) * 255.0f + 0.5f);
                    const size_t dst = (static_cast<size_t>(ty + py) * static_cast<size_t>(kSheetW) + static_cast<size_t>(tx + px)) * 4u;
                    sheetRgba[dst + 0u] = r8;
                    sheetRgba[dst + 1u] = g8;
                    sheetRgba[dst + 2u] = b8;
                    sheetRgba[dst + 3u] = a8;
                    if (tile == 0u && px == 0u && py == 0u) cornerAlpha = a8;
                    peakAlpha = std::max(peakAlpha, a8);
                }
            }
        }

        const std::string pngPath = "/tmp/frontier_fluid_flipbook_test.png";
        const bool wrotePng = FL::WriteRgba8PngFile(pngPath, kSheetW, kSheetH, sheetRgba);
        Check(wrotePng, "Write transparent RGBA8 flipbook PNG sheet");

        std::ifstream pngIn(pngPath, std::ios::binary);
        std::vector<uint8_t> pngBytes((std::istreambuf_iterator<char>(pngIn)), std::istreambuf_iterator<char>());
        static constexpr uint8_t kPngSig[8] { 137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u };
        const bool validSig = pngBytes.size() > 64u && std::memcmp(pngBytes.data(), kPngSig, 8u) == 0;
        const bool hasIhdr  = pngBytes.size() > 20u && std::memcmp(pngBytes.data() + 12u, "IHDR", 4u) == 0;
        const bool hasIend  = pngBytes.size() > 12u && std::memcmp(pngBytes.data() + pngBytes.size() - 8u, "IEND", 4u) == 0;
        std::cout << "[INFO] Flipbook PNG (" << kSheetW << "x" << kSheetH << ", " << pngBytes.size()
                  << " bytes): cornerAlpha=" << static_cast<int>(cornerAlpha)
                  << " peakAlpha=" << static_cast<int>(peakAlpha) << "\n";
        Check(validSig && hasIhdr && hasIend, "Flipbook PNG has valid PNG signature, IHDR, and IEND");
        Check(cornerAlpha == 0u && peakAlpha > 100u, "Flipbook PNG preserves transparent alpha corners & opaque plume");
        std::error_code rmEc;
        std::filesystem::remove(pngPath, rmEc);
    }

    // --------------------------------------------------------------------------------------------------------------------
    // 6. GPU UBO Layout ABI checks
    // --------------------------------------------------------------------------------------------------------------------
    {
        static_assert(sizeof(FL::FluidVolumeRaymarchUniformBlock) == 400u, "FluidVolumeRaymarchUniformBlock is 400 bytes");
        static_assert(sizeof(FL::SimulationPostOverlayUniformBlock) == 1088u, "SimulationPostOverlayUniformBlock is 1088 bytes");
        Check(sizeof(FL::FluidVolumeRaymarchUniformBlock) == 400u, "FluidVolumeRaymarchUniformBlock == 400 B");
        Check(sizeof(FL::SimulationPostOverlayUniformBlock) == 1088u, "SimulationPostOverlayUniformBlock == 1088 B");
    }

    std::cout << "=== Result: " << gPassed << "/" << (gPassed + gFailed) << " checks PASSED (" << gFailed << " failed) ===\n";
    return (gFailed == 0) ? 0 : 1;
}
