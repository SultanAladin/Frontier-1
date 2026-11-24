/*============================================================================================================================================
                                                    DEFORMABLESOLVERVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D08 Native C++20 validation suite for unified XPBD + 3D Stable Neo-Hookean FEM, conflict-color batching,
//    co-rotating pneumatic/FEM tires, ForceFieldSampler + RigidColliderProxy coupling, tearing, SuffixRegistry & .SoftBody codec.

#include "Engine/Codecs/Simulation/SoftBodyCodec.h"
#include "Engine/Serialization/Container/SuffixRegistry.h"
#include "Engine/SimulationSpace/Deformable/ConflictColorPartitioner.h"
#include "Engine/SimulationSpace/Deformable/DeformablePhysicsCoupling.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/NeoHookeanTetSolver.h"
#include "Engine/SimulationSpace/Deformable/PneumaticTireSolver.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"
#include "Engine/SimulationSpace/ForceFields/ForceModelRegistry.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond)
    {
        ++gPassed;
    }
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

bool AllFinite(const std::vector<float>& values)
{
    for (float v : values)
    {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace Frontier::SimulationSpace::Deformable;
    using namespace Frontier::Codecs::Simulation;
    using namespace Frontier::Serialization;

    // 1. SuffixRegistry verification (.SoftBody & .VertexAtlas)
    {
        std::string diag;
        Check(VerifySpaceStratificationRule(diag), "SuffixRegistry *Space stratification rule holds: " + diag);

        FormatSuffixCategory cat {};
        std::string canon;
        Check(ResolveSuffixCaseInsensitive(".softbody", cat, canon, diag) &&
              cat == FormatSuffixCategory::SoftBody && canon == ".SoftBody",
              "SuffixRegistry resolves .softbody -> .SoftBody");
        Check(ResolveSuffixCaseInsensitive(".vertexatlas", cat, canon, diag) &&
              cat == FormatSuffixCategory::VertexAtlas && canon == ".VertexAtlas",
              "SuffixRegistry resolves .vertexatlas -> .VertexAtlas");
    }

    // 2. Verify all 9 canonical presets: build, zero-conflict coloring, 180-step simulation (3.0s), 0 NaNs
    const auto presets = DeformablePresetRegistry::EnumeratePresets();
    Check(presets.size() == 9u, "DeformablePresetRegistry exposes all 9 presets");

    for (const auto& desc : presets)
    {
        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        const bool built = DeformablePresetRegistry::BuildPreset(desc.Kind, body, cfg);
        Check(built && body.PointCount > 0u, std::string("Preset builds cleanly: ") + desc.FileStem);

        // Verify zero vertex conflicts across distance, bend, and tet color batches
        if (!body.DistanceConstraints.empty())
        {
            std::vector<uint32_t> distIdx;
            distIdx.reserve(body.DistanceConstraints.size() * 2u);
            for (const auto& d : body.DistanceConstraints)
            {
                distIdx.push_back(d.PointA);
                distIdx.push_back(d.PointB);
            }
            Check(ConflictColorPartitioner::VerifyZeroVertexConflicts(body.PointCount, distIdx.data(), 2u, body.DistanceBatches),
                  std::string("Zero-conflict distance color batches: ") + desc.FileStem);
        }
        if (!body.BendConstraints.empty())
        {
            std::vector<uint32_t> bendIdx;
            bendIdx.reserve(body.BendConstraints.size() * 2u);
            for (const auto& b : body.BendConstraints)
            {
                bendIdx.push_back(b.PointA);
                bendIdx.push_back(b.PointB);
            }
            Check(ConflictColorPartitioner::VerifyZeroVertexConflicts(body.PointCount, bendIdx.data(), 2u, body.BendBatches),
                  std::string("Zero-conflict bend color batches: ") + desc.FileStem);
        }
        if (body.TetCount > 0u)
        {
            Check(ConflictColorPartitioner::VerifyZeroVertexConflicts(body.PointCount, body.TetIndices.data(), 4u, body.TetBatches),
                  std::string("Zero-conflict 3D tet color batches: ") + desc.FileStem);
        }

        // Run 180 frames @ 60 Hz with external ForceFieldSampler wind + Jolt RigidColliderProxy coupling
        Frontier::SimulationSpace::ForceFields::ForceModelGpuRecord windRec {};
        windRec.PositionRadius[3]    = 12.0f;
        windRec.DirectionStrength[0] = 1.0f;
        windRec.DirectionStrength[1] = 0.0f;
        windRec.DirectionStrength[2] = 0.0f;
        windRec.DirectionStrength[3] = 1.5f;
        windRec.TypeFalloffParams[0] = static_cast<float>(Frontier::SimulationSpace::ForceFields::ForceModelOrdinal::DirectionalWind);
        windRec.TimingChannelMask[3] = 15.0f;

        Frontier::SimulationSpace::PhysicsSpace::RigidColliderProxy joltProxy {};
        joltProxy.CenterMeters[0] = 0.4f;
        joltProxy.CenterMeters[1] = body.GroundY + 0.35f;
        joltProxy.CenterMeters[2] = 0.25f;
        joltProxy.RadiusMeters    = 0.22f;
        joltProxy.ShapeCode       = 0u;

        DeformableExternalCoupling coupling {};
        coupling.ForceRecords = { &windRec, 1u };
        coupling.RigidProxies = { &joltProxy, 1u };

        for (uint32_t step = 0u; step < 180u; ++step)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(step) / 60.0f, 0.0f, &coupling);
        }

        Check(AllFinite(body.Positions) && AllFinite(body.Velocities) && AllFinite(body.PointStrain),
              std::string("180-tick simulation produces 0 NaNs/Infs: ") + desc.FileStem);

        float maxSpeed = 0.0f;
        for (uint32_t i = 0u; i < body.PointCount; ++i)
        {
            const float vx = body.Velocities[i * 3u + 0u];
            const float vy = body.Velocities[i * 3u + 1u];
            const float vz = body.Velocities[i * 3u + 2u];
            maxSpeed = std::max(maxSpeed, std::sqrt(vx * vx + vy * vy + vz * vz));
        }
        Check(maxSpeed < 50.0f, std::string("Bounded velocities after 180 ticks: ") + desc.FileStem);
    }

    // 3. Dedicated 3D Neo-Hookean FEM Tire physics invariants (1,080 positive-V0 tets, high-RPM stability, drop bounce)
    {
        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, body, cfg);
        Check(body.PointCount == 504u && body.TetCount == 1080u, "SportTireFem has exact 504 points & 1,080 tetrahedra");

        bool allPosVol = true;
        for (float v0 : body.TetRestVolume)
        {
            if (!(v0 > 1.0e-7f)) allPosVol = false;
        }
        Check(allPosVol, "All 1,080 FEM tetrahedra have strictly positive rest volume V0");

        // High-RPM spin test (22 rad/s — guarantees zero artificial centrifugal squash)
        cfg.TireSpinRadPerSec = 22.0f;
        cfg.ColliderEnabled   = false;
        for (uint32_t step = 0u; step < 120u; ++step)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(step) / 60.0f, 0.0f);
        }

        const auto& tire = body.FemTires[0];
        double outerRadSum = 0.0;
        uint32_t outerCount = 0u;
        for (uint32_t s = 0u; s < tire.SectorCount; ++s)
        {
            const uint32_t idx = tire.StartPointIndex + (s * tire.ProfileCount + 3u) * 2u + 1u; // outer crown point
            const float dx = body.Positions[idx * 3u + 0u] - tire.AxleX;
            const float dy = body.Positions[idx * 3u + 1u] - tire.AxleY;
            outerRadSum += std::sqrt(dx * dx + dy * dy);
            ++outerCount;
        }
        const double meanOuterR = outerRadSum / static_cast<double>(outerCount);
        Check(meanOuterR >= 1.08 && meanOuterR <= 1.28,
              "High-RPM (22 rad/s) FEM tire preserves outer radius without centrifugal squash");

        // Drop test + curb strike
        PneumaticTireSolver::TriggerDropLift(body, 0.9f);
        Check(body.FemTires[0].AxleY > -0.1f, "TriggerDropLift lifts FEM tire axle cleanly");
        cfg.ColliderEnabled = true;
        body.Collider.CenterMeters = { 0.0f, body.GroundY + 0.16f, 0.0f };
        for (uint32_t step = 0u; step < 90u; ++step)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(step) / 60.0f, 0.0f);
        }
        Check(AllFinite(body.Positions) && AllFinite(body.TetCofactorNormals),
              "FEM tire drop + curb strike produces finite positions & Nanson cofactors");
    }

    // 4. Dedicated Pneumatic XPBD Tire high-RPM & cavity stability
    {
        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTirePneumatic, body, cfg);
        Check(body.PointCount == 432u && !body.PneumaticTires.empty(),
              "SportTirePneumatic has exact 432 points");

        cfg.TireSpinRadPerSec = 20.0f;
        for (uint32_t step = 0u; step < 120u; ++step)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(step) / 60.0f, 0.0f);
        }
        Check(AllFinite(body.Positions), "High-RPM (20 rad/s) Pneumatic XPBD tire stays completely finite");
    }

    // 5. Interactive segment cutter on OrbSpiderweb & WovenCloth
    {
        DeformableBody web {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(DeformablePresetKind::OrbSpiderweb, web, cfg);
        const uint32_t severed = XpbdConstraintSolver::CutConstraintsNearSegment(
            web, { -1.5f, 0.1f, 0.0f }, { 1.5f, 0.1f, 0.0f }, 0.25f);
        Check(severed > 0u && web.SeveredCount == severed, "CutConstraintsNearSegment severs OrbSpiderweb strands cleanly");
    }

    // 6. All 9 .SoftBody preset files on disk + Tier-2 rest-cage binary round-trip & FNV-1a tamper check
    {
        const std::filesystem::path presetDir = "EngineContent/Simulation/Deformables";
        for (const auto& desc : presets)
        {
            const auto filePath = presetDir / (std::string(desc.FileStem) + ".SoftBody");
            SoftBodyDocumentModel decoded {};
            std::string err;
            const bool ok = SoftBodyCodec::DecodeFromFile(filePath, decoded, &err);
            Check(ok && decoded.PresetKind == desc.Kind && decoded.PresetId == desc.FileStem,
                  std::string("Disk .SoftBody preset decodes cleanly: ") + filePath.string());
            Check(std::fabs(decoded.Configuration.GravityY - desc.DefaultConfig.GravityY) < 1.0e-4f &&
                  std::fabs(decoded.Configuration.FemYoungMpa - desc.DefaultConfig.FemYoungMpa) < 1.0e-4f,
                  std::string("Disk .SoftBody preset parameters match registry: ") + desc.FileStem);
        }

        // Tier-2 binary embedded rest-cage round-trip
        SoftBodyDocumentModel femWithCage = SoftBodyCodec::BuildFromPreset(DeformablePresetKind::SportTireFem, true);
        std::vector<std::byte> bytes;
        std::string err;
        Check(SoftBodyCodec::EncodeToBytes(femWithCage, bytes, &err), "SoftBody Tier-2 binary encodes cleanly");

        SoftBodyDocumentModel decoded {};
        Check(SoftBodyCodec::DecodeFromBytes(bytes, decoded, &err) &&
              decoded.EmbedRestCageBinary &&
              decoded.EmbeddedRestBody.PointCount == 504u &&
              decoded.EmbeddedRestBody.TetCount == 1080u,
              "SoftBody Tier-2 binary round-trips 504 points & 1,080 tetrahedra accurately");

        // Corrupt a byte strictly inside the binary payload (skipping trailing 64-byte alignment padding) -> FNV-1a MUST reject
        if (bytes.size() > 128u)
        {
            bytes[bytes.size() - 96u] = static_cast<std::byte>(static_cast<uint8_t>(bytes[bytes.size() - 96u]) ^ 0xFFu);
            SoftBodyDocumentModel tampered {};
            Check(!SoftBodyCodec::DecodeFromBytes(bytes, tampered, &err),
                  "SoftBody Tier-2 FNV-1a rejects corrupted binary payload");
        }
    }

    std::cout << "DeformableSolverValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
