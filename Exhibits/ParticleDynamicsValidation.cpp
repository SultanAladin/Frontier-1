/*============================================================================================================================================
                                                      PARTICLEDYNAMICSVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Deep validation of Molecular/Chemical reaction dynamics & all 7 specialized solvers + visual showcase proof render

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ParticleSpace/Dynamics/MolecularReactionDynamics.h"
#include "Engine/ParticleSpace/Dynamics/SpecialEffectsDynamics.h"
#include "Engine/ParticleSpace/ParticleDocument.h"
#include "Engine/ParticleSpace/ParticleSimulationSession.h"
#include "Engine/ParticleSpace/Presets/ParticlePresetRepository.h"
#include "Engine/ParticleSpace/Simulation/ParticleMetricsCollector.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#endif
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "ExternalPackages/stb/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

using namespace Frontier::ParticleSpace;

namespace
{

int gPass = 0;
int gFail = 0;

void Expect(bool Cond, const char* Label)
{
    if (Cond) { ++gPass; std::cout << "[PASS] " << Label << "\n"; }
    else      { ++gFail; std::cerr << "[FAIL] " << Label << "\n"; }
}

void SplatDisk(std::vector<uint8_t>& Rgba, int W, int H, int X0, int Y0, int CellW, int CellH,
               float Px, float Py, float Radius, float R, float G, float B, float A)
{
    const int Cx = X0 + static_cast<int>(Px);
    const int Cy = Y0 + static_cast<int>(Py);
    const int Rad = std::clamp(static_cast<int>(std::ceil(Radius)), 1, 8);
    for (int Dy = -Rad; Dy <= Rad; ++Dy)
    {
        const int Y = Cy + Dy;
        if (Y < Y0 || Y >= Y0 + CellH || Y < 0 || Y >= H) continue;
        for (int Dx = -Rad; Dx <= Rad; ++Dx)
        {
            const int X = Cx + Dx;
            if (X < X0 || X >= X0 + CellW || X < 0 || X >= W) continue;
            const float Dist = std::sqrt(static_cast<float>(Dx * Dx + Dy * Dy)) / static_cast<float>(Rad);
            if (Dist > 1.0f) continue;
            const float Falloff = (1.0f - Dist * Dist) * A;
            const std::size_t Idx = static_cast<std::size_t>((Y * W + X) * 4);
            Rgba[Idx + 0] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Idx + 0]) + R * 255.0f * Falloff));
            Rgba[Idx + 1] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Idx + 1]) + G * 255.0f * Falloff));
            Rgba[Idx + 2] = static_cast<uint8_t>(std::min(255.0f, static_cast<float>(Rgba[Idx + 2]) + B * 255.0f * Falloff));
            Rgba[Idx + 3] = 255u;
        }
    }
}

void RenderRigIntoTile(const ParticleRig& Rig, const ParticleDocument& Doc,
                       std::vector<uint8_t>& Img, int W, int H, int TileIdx)
{
    const int Col = TileIdx % 4;
    const int Row = TileIdx / 4;
    const int CellW = W / 4;
    const int CellH = H / 2;
    const int X0 = Col * CellW;
    const int Y0 = Row * CellH;

    // Subtle dark gradient backdrop per tile
    for (int Y = 0; Y < CellH; ++Y)
    {
        for (int X = 0; X < CellW; ++X)
        {
            const std::size_t Idx = static_cast<std::size_t>(((Y0 + Y) * W + (X0 + X)) * 4);
            const float V = static_cast<float>(Y) / static_cast<float>(CellH);
            const bool Border = (X == 0 || Y == 0 || X == CellW - 1 || Y == CellH - 1);
            Img[Idx + 0] = Border ? 42u : static_cast<uint8_t>(10.0f + V * 12.0f);
            Img[Idx + 1] = Border ? 52u : static_cast<uint8_t>(13.0f + V * 16.0f);
            Img[Idx + 2] = Border ? 68u : static_cast<uint8_t>(22.0f + V * 24.0f);
            Img[Idx + 3] = 255u;
        }
    }

    ParticleDocument SimDoc = Doc;
    SimDoc.Rigs.clear();
    ParticleRig SimRig = Rig;
    SimRig.SlotCapacity = std::min<uint32_t>(Rig.SlotCapacity, 1200u);
    SimDoc.Rigs.push_back(SimRig);

    ParticleSimulationSession Ext {};
    (void)Ext.Initialize(SimDoc);
    Ext.TriggerBurst(0u, std::min<uint32_t>(SimRig.SlotCapacity, 480u));
    for (uint32_t F = 0u; F < 75u; ++F)
    {
        Ext.Tick(1.0 / 60.0);
    }
    const auto Slots = Ext.InspectParticleSlots();

    const float Yaw = 0.55f, Pitch = 0.36f;
    const float Cy = std::cos(Yaw), Sy = std::sin(Yaw);
    const float Cp = std::cos(Pitch), Sp = std::sin(Pitch);

    // Auto-frame camera around live particle cloud centroid & radius
    float CxSum = 0.0f, CySum = 0.0f, CzSum = 0.0f;
    uint32_t AliveN = 0u;
    for (const auto& S : Slots)
    {
        if (!QueryParticleSlotAlive(S)) continue;
        CxSum += S.PositionMeters[0];
        CySum += S.PositionMeters[1];
        CzSum += S.PositionMeters[2];
        ++AliveN;
    }
    const float MeanX = AliveN ? (CxSum / static_cast<float>(AliveN)) : 0.0f;
    const float MeanY = AliveN ? (CySum / static_cast<float>(AliveN)) : 1.0f;
    const float MeanZ = AliveN ? (CzSum / static_cast<float>(AliveN)) : 0.0f;
    float MaxR = 0.65f;
    for (const auto& S : Slots)
    {
        if (!QueryParticleSlotAlive(S)) continue;
        const float Dx = S.PositionMeters[0] - MeanX;
        const float Dy = S.PositionMeters[1] - MeanY;
        const float Dz = S.PositionMeters[2] - MeanZ;
        MaxR = std::max(MaxR, std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz));
    }
    const float CamDist = std::max(2.6f, MaxR * 2.45f);

    for (const auto& S : Slots)
    {
        if (!QueryParticleSlotAlive(S)) continue;
        const float X = S.PositionMeters[0] - MeanX;
        const float Y = S.PositionMeters[1] - MeanY;
        const float Z = S.PositionMeters[2] - MeanZ;
        const float Rx = X * Cy - Z * Sy;
        const float Rz = X * Sy + Z * Cy;
        const float Ry = Y * Cp - Rz * Sp;
        const float Depth = Rz * Cp + Y * Sp + CamDist;
        if (Depth < 0.35f) continue;

        const float Proj = (static_cast<float>(CellH) * 0.88f) / Depth;
        const float Px = static_cast<float>(CellW) * 0.5f + Rx * Proj;
        const float Py = static_cast<float>(CellH) * 0.52f - Ry * Proj;
        const float Rad = std::clamp(S.SizeMeters * Proj * 0.55f, 1.8f, 6.0f);
        const float R = std::max(S.ColourLinear[0], 0.14f);
        const float G = std::max(S.ColourLinear[1], 0.16f);
        const float B = std::max(S.ColourLinear[2], 0.22f);
        SplatDisk(Img, W, H, X0, Y0, CellW, CellH, Px, Py, Rad,
                  R, G, B, std::clamp(S.ColourLinear[3], 0.45f, 0.95f));
    }
}

} // namespace

int main()
{
    std::cout << "=== ParticleDynamics Specialized Solvers Validation ===\n";

    // 1. Lennard-Jones Molecular & Reversible Chemical Reaction A + B <-> C
    {
        ParticleDocument Doc = ConstructStudioDefaultDocument();
        Doc.Rigs.clear();
        ParticleRig Rig = ConstructDefaultStudioRig();
        Rig.Origin = AnalyticPointOrigin {};
        Rig.SlotCapacity = 320u;
        Rig.ReleaseRatePerSec = 1000.0f;
        Rig.Dynamics.Ballistic.GravityScale = 0.0f;
        Rig.Kinematics.SpeedMinMps = 0.25f;
        Rig.Kinematics.SpeedMaxMps = 0.85f;
        MolecularKineticsSpec Mol {};
        Mol.ChemicalReaction = true;
        Mol.ContactRadiusM   = 0.85f;
        Mol.ForwardRate      = 55.0f;
        Mol.DissociationRate = 0.05f;
        Rig.Dynamics.Molecular = Mol;
        Doc.Rigs.push_back(Rig);

        ParticleSimulationSession Ext {};
        (void)Ext.Initialize(Doc);
        Ext.TriggerBurst(0u, 280u);
        for (uint32_t F = 0u; F < 45u; ++F)
        {
            Ext.Tick(1.0 / 60.0);
        }
        const auto Metrics = ParticleMetricsCollector::CollectFromSlots(Ext.InspectParticleSlots());
        Expect(Metrics.LiveCount > 150u && Metrics.NaNOrInfCount == 0u, "Molecular Lennard-Jones runs stably with 0 NaNs");
        Expect(Metrics.SpeciesCCount > 0u, "Reversible A + B <-> C reaction synthesizes product species C");
    }

    // 2. Test all 7 specialized solver presets end-to-end with interactive collider enabled
    const char* SpecialPresets[8] = {
        "ferrofluid-spikes",
        "ferrofluid-orb",
        "cracked-ice-sheet",
        "ground-crack-quake",
        "car-puddle-splash",
        "derez-cube",
        "energy-shield-hex",
        "effector-grid-cubes"
    };

    const int W = 1280, H = 640;
    std::vector<uint8_t> Showcase(static_cast<std::size_t>(W * H * 4), 255u);

    for (int I = 0; I < 8; ++I)
    {
        ParticleDocument Doc = InstantiateDocumentFromPresetId(SpecialPresets[I]);
        Doc.Collider.BallActive     = true;
        Doc.Collider.BallCenterM[0] = 0.6f;
        Doc.Collider.BallCenterM[1] = 1.4f;
        Doc.Collider.BallCenterM[2] = 0.4f;
        Doc.Rigs[0].SlotCapacity    = std::min<uint32_t>(Doc.Rigs[0].SlotCapacity, 800u);

        ParticleSimulationSession Ext {};
        (void)Ext.Initialize(Doc);
        Ext.TriggerBurst(0u, 320u);
        for (uint32_t F = 0u; F < 30u; ++F)
        {
            Ext.Tick(1.0 / 60.0);
        }
        const auto M = ParticleMetricsCollector::CollectFromSlots(Ext.InspectParticleSlots());
        const std::string Msg = std::string("Specialized preset '") + SpecialPresets[I] + "' active & finite (" + std::to_string(M.LiveCount) + " live)";
        Expect(M.LiveCount > 0u && M.NaNOrInfCount == 0u, Msg.c_str());

        RenderRigIntoTile(Doc.Rigs[0], Doc, Showcase, W, H, I);
    }

    // 3. Verify spiderweb-orb & all 6 EffectorLattice drivers explicitly
    {
        ParticleDocument WebDoc = InstantiateDocumentFromPresetId("spiderweb-orb");
        WebDoc.Rigs[0].SlotCapacity = 400u;
        ParticleSimulationSession Ext {};
        (void)Ext.Initialize(WebDoc);
        Ext.TriggerBurst(0u, 220u);
        for (uint32_t F = 0u; F < 24u; ++F)
        {
            Ext.Tick(1.0 / 60.0);
        }
        const auto M = ParticleMetricsCollector::CollectFromSlots(Ext.InspectParticleSlots());
        Expect(M.LiveCount > 0u && M.NaNOrInfCount == 0u, "SpiderwebWeave catenary solver runs cleanly");
    }

    for (uint8_t Drv = 0u; Drv < 6u; ++Drv)
    {
        ParticleDocument Doc = InstantiateDocumentFromPresetId("effector-object-placer");
        Doc.Collider.BallActive = true;
        Doc.Rigs[0].SlotCapacity = 256u;
        Doc.Rigs[0].Dynamics.Effector->Driver = static_cast<EffectorDriverCategory>(Drv);
        Doc.Rigs[0].Dynamics.Effector->PrimaryVolume.Shape = static_cast<EffectorVolumeShape>(Drv % 5u);
        ParticleSimulationSession Ext {};
        (void)Ext.Initialize(Doc);
        Ext.TriggerBurst(0u, 160u);
        Ext.Tick(1.0 / 60.0);
        const auto M = ParticleMetricsCollector::CollectFromSlots(Ext.InspectParticleSlots());
        Expect(M.LiveCount > 0u && M.NaNOrInfCount == 0u, "EffectorLattice driver + 3D SDF volume active & finite");
    }

    const std::filesystem::path OutPath = "Exhibits/Images/Particles/CppEngine/ParticleSpecialDynamicsShowcase.png";
    std::filesystem::create_directories(OutPath.parent_path());
    const int Wrote = stbi_write_png(OutPath.string().c_str(), W, H, 4, Showcase.data(), W * 4);
    Expect(Wrote != 0, "Wrote ParticleSpecialDynamicsShowcase.png (8 specialized solvers)");

    std::cout << "Summary: " << gPass << " passed, " << gFail << " failed.\n";
    return (gFail == 0) ? 0 : 1;
}
