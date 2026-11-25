/*============================================================================================================================================
                                                    DEFORMABLEPRESETREGISTRY.CPP
============================================================================================================================================*/
// 🧩 Phase D03 Deterministic C++20 preset registry replicating all 9 XPBD + 3D FEM studio presets (tires, plants, webs, cables, cloth)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/ConflictColorPartitioner.h"
#include "Engine/SimulationSpace/Deformable/PneumaticTireSolver.h"
#include "Engine/SimulationSpace/Deformable/TetrahedralSolidBuilder.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

namespace
{

constexpr float kPi    = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

[[nodiscard]] float DeterministicHash3(float x, float y, float z) noexcept
{
    const float s = std::sin(x * 127.1f + y * 311.7f + z * 74.7f) * 43758.5453123f;
    return s - std::floor(s);
}

DeformableSimulationConfiguration MakeConfig(
    uint32_t substeps,
    float gravY,
    float wind,
    float turb,
    float stretch,
    float bend,
    float damp,
    float tear,
    float psi = 30.0f,
    float load = 1.35f,
    float spin = 3.0f,
    float young = 2.4f,
    float poisson = 0.44f,
    bool underwater = false) noexcept
{
    DeformableSimulationConfiguration c {};
    c.Substeps          = substeps;
    c.GravityY          = gravY;
    c.WindStrength      = wind;
    c.Turbulence        = turb;
    c.StretchStiffness  = stretch;
    c.BendStiffness     = bend;
    c.Damping           = damp;
    c.TearRatio         = tear;
    c.TirePressurePsi   = psi;
    c.TireAxleLoad      = load;
    c.TireSpinRadPerSec = spin;
    c.FemYoungMpa       = young;
    c.FemPoissonNu      = poisson;
    c.UnderwaterEnabled = underwater;
    c.ColliderEnabled   = true;
    return c;
}

const std::array<DeformablePresetDescriptor, 9u> kPresets = {{
    { DeformablePresetKind::SportTireFem,       "fem_tyre",   "SportTireFem",       "3D FEM Soft-Body Tire", "Neo-Hookean",
      MakeConfig(6u, -9.8f, 0.0f, 0.0f, 1600.0f, 0.55f, 0.985f, 0.0f, 30.0f, 1.35f, 3.0f, 2.4f, 0.44f, false),
      -1.78f, { 1.55f, -1.62f, 0.0f }, 0.42f },
    { DeformablePresetKind::SportTirePneumatic, "tyre",       "SportTirePneumatic", "Pneumatic Tire XPBD",   "Pneumatic",
      MakeConfig(6u, -9.8f, 0.0f, 0.0f, 1600.0f, 0.55f, 0.985f, 0.0f, 32.0f, 1.15f, 3.5f, 2.4f, 0.44f, false),
      -1.78f, { 1.55f, -1.62f, 0.0f }, 0.42f },
    { DeformablePresetKind::BotanicalFerns,     "botanical",  "BotanicalFerns",     "Botanical Ferns",       "Botanical",
      MakeConfig(5u, -6.2f, 2.6f, 1.4f, 2000.0f, 0.65f, 0.990f, 0.0f),
      -1.78f, { 0.85f, -0.35f, 0.55f }, 0.46f },
    { DeformablePresetKind::OrbSpiderweb,       "spiderweb",  "OrbSpiderweb",       "Orb Spiderweb",         "Silk & Dew",
      MakeConfig(5u, -4.5f, 2.0f, 1.2f, 1500.0f, 0.20f, 0.992f, 1.9f),
      -1.78f, { 0.0f, 0.15f, 0.95f }, 0.42f },
    { DeformablePresetKind::CablesChains,       "cables",     "CablesChains",       "Cables & Chains",       "Heavy Rigid",
      MakeConfig(6u, -11.0f, 1.4f, 0.8f, 2800.0f, 0.30f, 0.993f, 0.0f),
      -1.78f, { 0.0f, -0.25f, 0.65f }, 0.46f },
    { DeformablePresetKind::HangingVines,       "vines",      "HangingVines",       "Hanging Vines",         "Foliage",
      MakeConfig(5u, -8.5f, 3.2f, 1.6f, 1600.0f, 0.35f, 0.992f, 2.6f),
      -1.78f, { 0.35f, -0.25f, 0.55f }, 0.48f },
    { DeformablePresetKind::MarshReeds,         "reeds",      "MarshReeds",         "Marsh Reeds",           "Grass & Stems",
      MakeConfig(5u, -7.2f, 3.8f, 1.9f, 2200.0f, 0.72f, 0.990f, 0.0f),
      -1.78f, { 0.55f, -0.45f, 0.50f }, 0.46f },
    { DeformablePresetKind::UnderwaterKelp,     "kelp",       "UnderwaterKelp",     "Underwater Kelp",       "Buoyant",
      MakeConfig(5u, 4.4f, 2.6f, 1.7f, 1600.0f, 0.25f, 0.985f, 0.0f, 30.0f, 1.35f, 0.0f, 2.4f, 0.44f, true),
      -1.78f, { 0.45f, -0.20f, 0.45f }, 0.48f },
    { DeformablePresetKind::WovenCloth,         "cloth",      "WovenCloth",         "Woven Cloth",           "Tearable",
      MakeConfig(6u, -9.2f, 3.6f, 1.6f, 1800.0f, 0.40f, 0.992f, 2.65f),
      -1.78f, { 0.0f, 0.25f, 0.85f }, 0.45f }
}};

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 DEFORMABLE PRESET REGISTRY
//------------------------------------------------------------------------------------------------------------------------

std::span<const DeformablePresetDescriptor> DeformablePresetRegistry::EnumeratePresets() noexcept
{
    return kPresets;
}

const DeformablePresetDescriptor* DeformablePresetRegistry::FindById(std::string_view presetIdOrStem) noexcept
{
    for (const DeformablePresetDescriptor& desc : kPresets)
    {
        if (presetIdOrStem == desc.PresetId || presetIdOrStem == desc.FileStem)
        {
            return &desc;
        }
    }
    return nullptr;
}

void DeformablePresetRegistry::AppendFemSoftBodyTire(
    DeformableBody& body,
    float hx, float hy, float hz,
    float rimR, float outerR, float halfW, uint32_t sectors)
{
    const float wallThick = 0.11f;
    const std::vector<std::array<float, 2>> outerPts = {
        { rimR + 0.01f,                        -halfW * 0.78f },
        { rimR + (outerR - rimR) * 0.52f,      -halfW * 1.02f },
        { outerR - 0.015f,                     -halfW * 0.76f },
        { outerR,                               0.0f          },
        { outerR - 0.015f,                      halfW * 0.76f },
        { rimR + (outerR - rimR) * 0.52f,       halfW * 1.02f },
        { rimR + 0.01f,                         halfW * 0.78f }
    };
    const std::vector<std::array<float, 2>> innerPts = {
        { rimR + 0.01f,                        -halfW * 0.60f },
        { rimR + (outerR - rimR) * 0.48f,      -halfW * 0.78f },
        { outerR - wallThick - 0.01f,          -halfW * 0.58f },
        { outerR - wallThick,                   0.0f          },
        { outerR - wallThick - 0.01f,           halfW * 0.58f },
        { rimR + (outerR - rimR) * 0.48f,       halfW * 0.78f },
        { rimR + 0.01f,                         halfW * 0.60f }
    };

    const uint32_t P = static_cast<uint32_t>(outerPts.size());
    const uint32_t startIdx = body.PointCount;
    const uint32_t startTet = body.TetCount;

    for (uint32_t si = 0u; si < sectors; ++si)
    {
        const float a = (static_cast<float>(si) / static_cast<float>(sectors)) * kTwoPi;
        const float ca = std::cos(a), sa = std::sin(a);
        for (uint32_t pi = 0u; pi < P; ++pi)
        {
            const bool isBead  = (pi == 0u || pi + 1u == P);
            const bool isTread = (pi >= 2u && pi <= 4u);
            for (uint32_t lay = 0u; lay < 2u; ++lay)
            {
                const float r    = (lay == 0u) ? innerPts[pi][0] : outerPts[pi][0];
                const float zOff = (lay == 0u) ? innerPts[pi][1] : outerPts[pi][1];
                const float invM = isBead ? 0.0f : (isTread ? 0.85f : 1.0f);
                body.AppendMassPoint(hx + ca * r, hy + sa * r, hz + zOff, invM, 0.025f, 0.03f);
            }
        }
    }

    const auto pointAt = [&](uint32_t si, uint32_t pi, uint32_t lay) -> uint32_t
    {
        return startIdx + ((si % sectors) * P + pi) * 2u + lay;
    };

    for (uint32_t si = 0u; si < sectors; ++si)
    {
        const uint32_t sNext = (si + 1u) % sectors;
        for (uint32_t pi = 0u; pi + 1u < P; ++pi)
        {
            const std::array<uint32_t, 8> hex = {
                pointAt(si, pi, 0u), pointAt(sNext, pi, 0u), pointAt(sNext, pi + 1u, 0u), pointAt(si, pi + 1u, 0u),
                pointAt(si, pi, 1u), pointAt(sNext, pi, 1u), pointAt(sNext, pi + 1u, 1u), pointAt(si, pi + 1u, 1u)
            };
            TetrahedralSolidBuilder::AppendHexCellAsFiveTets(body, hex, (si + pi) & 1u);
        }
    }

    const float restVol = PneumaticTireSolver::ComputeCavityVolume(
        body.RestPositions.data(), startIdx, sectors, P, 2u, hx, hy, hz, rimR);

    FemTireSolid ft {};
    ft.StartPointIndex = startIdx;
    ft.StartTetIndex   = startTet;
    ft.TetCount        = body.TetCount - startTet;
    ft.SectorCount     = sectors;
    ft.ProfileCount    = P;
    ft.InnerStations   = innerPts;
    ft.OuterStations   = outerPts;
    ft.AxleX           = hx;
    ft.AxleY           = hy;
    ft.AxleZ           = hz;
    ft.RimRadius       = rimR;
    ft.OuterRadius     = outerR;
    ft.BeadHalfWidthZ  = halfW * 0.78f;
    ft.RestCavityVol   = std::max(0.25f, std::fabs(restVol));
    body.FemTires.push_back(ft);
}

void DeformablePresetRegistry::AppendPneumaticXpbdTire(
    DeformableBody& body,
    float hx, float hy, float hz,
    float rimR, float outerR, float halfW, uint32_t sectors)
{
    const float H = outerR - rimR;
    const std::vector<std::array<float, 2>> profilePts = {
        { rimR + 0.005f,       -halfW * 0.76f },
        { rimR + H * 0.34f,    -halfW * 0.99f },
        { rimR + H * 0.70f,    -halfW * 0.95f },
        { outerR - 0.018f,     -halfW * 0.74f },
        { outerR,               0.0f          },
        { outerR - 0.018f,      halfW * 0.74f },
        { rimR + H * 0.70f,     halfW * 0.95f },
        { rimR + H * 0.34f,     halfW * 0.99f },
        { rimR + 0.005f,        halfW * 0.76f }
    };
    const uint32_t P = static_cast<uint32_t>(profilePts.size());
    const uint32_t startIdx = body.PointCount;

    for (uint32_t si = 0u; si < sectors; ++si)
    {
        const float a = (static_cast<float>(si) / static_cast<float>(sectors)) * kTwoPi;
        const float ca = std::cos(a), sa = std::sin(a);
        for (uint32_t pi = 0u; pi < P; ++pi)
        {
            const bool isBead  = (pi == 0u || pi + 1u == P);
            const bool isTread = (pi >= 3u && pi <= 5u);
            const float invM   = isBead ? 0.0f : (isTread ? 0.85f : 1.0f);
            body.AppendMassPoint(hx + ca * profilePts[pi][0], hy + sa * profilePts[pi][0], hz + profilePts[pi][1], invM, 0.025f, 0.05f);
        }
    }

    for (uint32_t si = 0u; si < sectors; ++si)
    {
        const uint32_t sNext = (si + 1u) % sectors;
        const uint32_t s2    = (si + 2u) % sectors;
        for (uint32_t pi = 0u; pi < P; ++pi)
        {
            const uint32_t i00 = startIdx + si * P + pi;
            const uint32_t i10 = startIdx + sNext * P + pi;
            const bool isTread = (pi >= 3u && pi <= 5u);
            if (pi > 0u && pi + 1u < P)
            {
                body.AppendDistanceConstraint(i00, i10, isTread ? 2400.0f : 1350.0f, 0.0f, true);
                body.AppendBendConstraint(i00, startIdx + s2 * P + pi, isTread ? 85.0f : 32.0f);
            }
            if (pi + 1u < P)
            {
                const uint32_t i01 = startIdx + si * P + pi + 1u;
                const uint32_t i11 = startIdx + sNext * P + pi + 1u;
                body.AppendDistanceConstraint(i00, i01, 1750.0f, 0.0f, true);
                body.AppendDistanceConstraint(i00, i11, 780.0f, 0.0f, false);
                body.AppendDistanceConstraint(i10, i01, 780.0f, 0.0f, false);
            }
            if (pi + 2u < P)
            {
                body.AppendBendConstraint(i00, startIdx + si * P + pi + 2u, 45.0f);
            }
        }
    }

    const float restVol = PneumaticTireSolver::ComputeCavityVolume(
        body.RestPositions.data(), startIdx, sectors, P, 1u, hx, hy, hz, rimR);

    PneumaticTireCarcass ty {};
    ty.StartPointIndex = startIdx;
    ty.SectorCount     = sectors;
    ty.ProfileCount    = P;
    ty.ProfileStations = profilePts;
    ty.AxleX           = hx;
    ty.AxleY           = hy;
    ty.AxleZ           = hz;
    ty.RimRadius       = rimR;
    ty.OuterRadius     = outerR;
    ty.BeadHalfWidthZ  = halfW * 0.76f;
    ty.RestCavityVol   = std::max(0.25f, std::fabs(restVol));
    body.PneumaticTires.push_back(ty);
}

void DeformablePresetRegistry::AppendFernCluster(DeformableBody& body, float ox, float oy, float oz, uint32_t frondCount)
{
    body.StaticProps.push_back({ { ox, oy - 0.05f, oz }, { 0.32f, 0.14f, 0.32f }, { 0.18f, 0.15f, 0.11f }, { 0.88f, 0.0f, 0.0f } });
    for (uint32_t f = 0u; f < frondCount; ++f)
    {
        const float ff   = static_cast<float>(f);
        const float az   = (ff / static_cast<float>(frondCount)) * kTwoPi + DeterministicHash3(ff, ox, oz) * 0.4f;
        const float len  = 1.35f + DeterministicHash3(ff, 2.0f, 3.0f) * 0.65f;
        const float elev = 0.72f + DeterministicHash3(ff, 5.0f, 7.0f) * 0.45f;
        constexpr uint32_t segs = 10u;

        StrandTubeAttachment tube {};
        tube.RadiusStart = 0.024f;
        tube.RadiusEnd   = 0.005f;
        tube.RingSides   = 5u;
        tube.ColourStart = { 0.14f, 0.34f, 0.12f };
        tube.ColourEnd   = { 0.26f, 0.58f, 0.18f };
        tube.MaterialPbr = { 0.48f, 0.25f, 0.0f };

        for (uint32_t i = 0u; i <= segs; ++i)
        {
            const float u     = static_cast<float>(i) / static_cast<float>(segs);
            const float rOut  = u * len * 0.85f;
            const float archY = std::sin(u * kPi * 0.78f) * len * elev * 0.62f - u * u * 0.32f;
            const uint32_t pid = body.AppendMassPoint(
                ox + std::cos(az) * rOut, oy + std::max(0.02f, archY), oz + std::sin(az) * rOut,
                (i <= 1u) ? 0.0f : 1.0f, 0.028f, 1.15f);
            tube.PointIndices.push_back(pid);
            if (i > 0u) tube.EdgeOrdinals.push_back(body.AppendDistanceConstraint(tube.PointIndices[i - 1u], pid, 2000.0f, 0.0f, true));
            if (i > 1u) body.AppendBendConstraint(tube.PointIndices[i - 2u], pid, 55.0f);
            if (i >= 2u) body.AppendPostureConstraint(pid, 18.0f * (1.2f - u * 0.7f));
        }
        for (uint32_t i = 2u; i < segs; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(segs);
            const float pinnaLen = std::sin(u * kPi) * 0.34f + 0.08f;
            for (int32_t side : { -1, 1 })
            {
                body.FoliageBlades.push_back({
                    tube.PointIndices[i], tube.PointIndices[i + 1u], side, 0.0f, 0.25f,
                    pinnaLen, pinnaLen * 0.26f, 0.28f,
                    { 0.09f, 0.42f, 0.14f }, { 0.34f, 0.76f, 0.22f }, { 0.32f, 0.78f, 0.0f }
                });
            }
        }
        body.StrandTubes.push_back(std::move(tube));
    }
}

void DeformablePresetRegistry::AppendOrbSpiderweb(
    DeformableBody& body,
    float ox, float oy, float oz, float webRadius, uint32_t spokes, uint32_t rings)
{
    const uint32_t centerPt = body.AppendMassPoint(ox, oy, oz, 1.0f, 0.02f, 1.4f);
    std::vector<std::vector<uint32_t>> grid(spokes);

    for (uint32_t s = 0u; s < spokes; ++s)
    {
        const float a  = (static_cast<float>(s) / static_cast<float>(spokes)) * kTwoPi + 0.12f;
        const float ca = std::cos(a), sa = std::sin(a);
        grid[s].push_back(centerPt);

        StrandTubeAttachment spoke {};
        spoke.PointIndices.push_back(centerPt);
        spoke.RadiusStart = 0.013f;
        spoke.RadiusEnd   = 0.010f;
        spoke.RingSides   = 5u;
        spoke.ColourStart = { 0.88f, 0.93f, 0.98f };
        spoke.ColourEnd   = { 0.78f, 0.86f, 0.94f };
        spoke.MaterialPbr = { 0.18f, 0.35f, 0.12f };

        for (uint32_t r = 1u; r <= rings; ++r)
        {
            const float frac = std::pow(static_cast<float>(r) / static_cast<float>(rings), 1.08f);
            const float rad  = webRadius * frac;
            const bool isAnchor = (r == rings);
            const float px = ox + ca * rad;
            const float py = oy + sa * rad;
            const float pz = oz + std::sin(a * 2.0f) * 0.04f * frac;
            const uint32_t pid = body.AppendMassPoint(px, py, pz, isAnchor ? 0.0f : 1.0f, 0.018f, 1.5f);
            grid[s].push_back(pid);
            spoke.PointIndices.push_back(pid);
            spoke.EdgeOrdinals.push_back(body.AppendDistanceConstraint(grid[s][r - 1u], pid, 1600.0f, 1.9f, true, 0.99f));

            if (!isAnchor && ((s + r) % 3u == 0u))
            {
                body.PointBeads.push_back({
                    pid, { 0.026f, 0.032f, 0.026f }, { 0.0f, -0.015f, 0.0f },
                    { 0.76f, 0.92f, 1.0f }, { 0.07f, 0.45f, 0.15f }
                });
            }
            if (isAnchor)
            {
                body.StaticProps.push_back({ { px, py, pz }, { 0.045f, 0.045f, 0.045f }, { 0.32f, 0.28f, 0.24f }, { 0.75f, 0.0f, 0.1f } });
            }
        }
        body.StrandTubes.push_back(std::move(spoke));
    }

    for (uint32_t r = 1u; r < rings; ++r)
    {
        StrandTubeAttachment ring {};
        ring.RadiusStart = 0.009f;
        ring.RadiusEnd   = 0.009f;
        ring.RingSides   = 4u;
        ring.ColourStart = { 0.85f, 0.92f, 1.0f };
        ring.ColourEnd   = { 0.85f, 0.92f, 1.0f };
        ring.MaterialPbr = { 0.15f, 0.42f, 0.10f };
        for (uint32_t s = 0u; s < spokes; ++s)
        {
            const uint32_t a = grid[s][r];
            const uint32_t b = grid[(s + 1u) % spokes][r];
            ring.PointIndices.push_back(a);
            ring.EdgeOrdinals.push_back(body.AppendDistanceConstraint(a, b, 950.0f, 1.75f, true, 0.995f));
        }
        ring.PointIndices.push_back(grid[0][r]);
        body.StrandTubes.push_back(std::move(ring));
    }
}

void DeformablePresetRegistry::AppendHangingVine(
    DeformableBody& body,
    float x, float yTop, float z, uint32_t segs, float length, uint32_t leafStride)
{
    StrandTubeAttachment tube {};
    tube.RadiusStart = 0.024f;
    tube.RadiusEnd   = 0.008f;
    tube.RingSides   = 6u;
    tube.ColourStart = { 0.22f, 0.32f, 0.14f };
    tube.ColourEnd   = { 0.16f, 0.46f, 0.17f };
    tube.MaterialPbr = { 0.55f, 0.22f, 0.0f };

    for (uint32_t i = 0u; i <= segs; ++i)
    {
        const float u = static_cast<float>(i) / static_cast<float>(segs);
        const uint32_t pid = body.AppendMassPoint(
            x + std::sin(u * 3.0f + x) * 0.08f, yTop - u * length, z + std::cos(u * 2.5f + z) * 0.08f,
            (i == 0u) ? 0.0f : 1.0f, 0.025f, 1.25f);
        tube.PointIndices.push_back(pid);
        if (i > 0u) tube.EdgeOrdinals.push_back(body.AppendDistanceConstraint(tube.PointIndices[i - 1u], pid, 1500.0f, 2.8f, true));
        if (i > 1u) body.AppendBendConstraint(tube.PointIndices[i - 2u], pid, 18.0f);
        if (i >= 1u && i < segs && (i % std::max(1u, leafStride) == 0u))
        {
            const int32_t side = (i & 1u) ? 1 : -1;
            body.FoliageBlades.push_back({
                pid, tube.PointIndices[i - 1u], side, static_cast<float>(i) * 1.15f + x, -0.32f,
                0.24f + 0.11f * (1.0f - std::fabs(u - 0.5f)), 0.095f, 0.35f,
                { 0.09f, 0.38f, 0.14f }, { 0.28f, 0.68f, 0.22f }, { 0.34f, 0.72f, 0.0f }
            });
            body.FoliageBlades.push_back({
                pid, tube.PointIndices[i - 1u], -side, static_cast<float>(i) * 1.15f + x + 0.45f, -0.25f,
                0.20f + 0.08f * (1.0f - u), 0.08f, 0.38f,
                { 0.08f, 0.34f, 0.12f }, { 0.22f, 0.60f, 0.19f }, { 0.36f, 0.68f, 0.0f }
            });
        }
    }
    body.StrandTubes.push_back(std::move(tube));
}

void DeformablePresetRegistry::AppendReedCluster(DeformableBody& body, float ox, float oy, float oz, uint32_t reedCount)
{
    body.StaticProps.push_back({ { ox, oy - 0.04f, oz }, { 0.75f, 0.09f, 0.65f }, { 0.16f, 0.17f, 0.12f }, { 0.90f, 0.0f, 0.0f } });
    for (uint32_t r = 0u; r < reedCount; ++r)
    {
        const float fr    = static_cast<float>(r);
        const float a     = DeterministicHash3(fr, 11.0f, 2.0f) * kTwoPi;
        const float dist  = std::sqrt(DeterministicHash3(fr, 3.0f, 9.0f)) * 0.68f;
        const float rx    = ox + std::cos(a) * dist;
        const float rz    = oz + std::sin(a) * dist;
        const float h     = 1.65f + DeterministicHash3(fr, 7.0f, 13.0f) * 1.15f;
        const float leanX = (DeterministicHash3(fr, 4.0f, 8.0f) - 0.5f) * 0.35f;
        const float leanZ = (DeterministicHash3(fr, 5.0f, 1.0f) - 0.5f) * 0.35f;
        constexpr uint32_t segs = 9u;

        StrandTubeAttachment stalk {};
        stalk.RadiusStart = 0.024f;
        stalk.RadiusEnd   = 0.008f;
        stalk.RingSides   = 5u;
        stalk.ColourStart = { 0.14f, 0.36f, 0.11f };
        stalk.ColourEnd   = { 0.42f, 0.64f, 0.18f };
        stalk.MaterialPbr = { 0.42f, 0.45f, 0.0f };

        for (uint32_t i = 0u; i <= segs; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(segs);
            const uint32_t pid = body.AppendMassPoint(
                rx + leanX * u * u, oy + u * h, rz + leanZ * u * u,
                (i <= 1u) ? 0.0f : 1.0f, 0.022f, 1.3f);
            stalk.PointIndices.push_back(pid);
            if (i > 0u) stalk.EdgeOrdinals.push_back(body.AppendDistanceConstraint(stalk.PointIndices[i - 1u], pid, 2200.0f, 0.0f, true));
            if (i > 1u) body.AppendBendConstraint(stalk.PointIndices[i - 2u], pid, 48.0f * (1.2f - u * 0.6f));
            if (i >= 2u) body.AppendPostureConstraint(pid, 24.0f * (1.15f - u * 0.72f));
        }

        if ((r & 1u) == 0u)
        {
            body.PointBeads.push_back({
                stalk.PointIndices[segs - 1u], { 0.044f, 0.14f, 0.044f }, { 0.0f, -0.05f, 0.0f },
                { 0.27f, 0.15f, 0.08f }, { 0.85f, 0.05f, 0.0f }
            });
        }
        else
        {
            body.FoliageBlades.push_back({
                stalk.PointIndices[3], stalk.PointIndices[4], 1, a, 0.65f, 0.75f, 0.055f, 0.55f,
                { 0.16f, 0.44f, 0.13f }, { 0.48f, 0.72f, 0.22f }, { 0.38f, 0.62f, 0.0f }
            });
        }
        body.StrandTubes.push_back(std::move(stalk));
    }
}

void DeformablePresetRegistry::AppendKelpForest(DeformableBody& body, float ox, float oy, float oz, uint32_t kelpCount)
{
    body.StaticProps.push_back({ { ox, oy - 0.06f, oz }, { 1.1f, 0.14f, 0.9f }, { 0.14f, 0.18f, 0.16f }, { 0.92f, 0.0f, 0.0f } });
    for (uint32_t k = 0u; k < kelpCount; ++k)
    {
        const float fk   = static_cast<float>(k);
        const float a    = (fk / static_cast<float>(kelpCount)) * kTwoPi + DeterministicHash3(fk, 3.0f, 7.0f) * 0.5f;
        const float dist = 0.18f + DeterministicHash3(fk, 9.0f, 2.0f) * 0.85f;
        const float kx   = ox + std::cos(a) * dist;
        const float kz   = oz + std::sin(a) * dist;
        const float h    = 2.1f + DeterministicHash3(fk, 5.0f, 11.0f) * 1.15f;
        constexpr uint32_t segs = 13u;

        StrandTubeAttachment stipe {};
        stipe.RadiusStart = 0.026f;
        stipe.RadiusEnd   = 0.011f;
        stipe.RingSides   = 5u;
        stipe.ColourStart = { 0.10f, 0.30f, 0.16f };
        stipe.ColourEnd   = { 0.22f, 0.56f, 0.24f };
        stipe.MaterialPbr = { 0.28f, 0.55f, 0.0f };

        for (uint32_t i = 0u; i <= segs; ++i)
        {
            const float u = static_cast<float>(i) / static_cast<float>(segs);
            const uint32_t pid = body.AppendMassPoint(
                kx + std::sin(u * 4.0f + fk) * 0.12f, oy + u * h, kz + std::cos(u * 3.0f + fk) * 0.12f,
                (i == 0u) ? 0.0f : 1.0f, 0.03f, 1.45f);
            stipe.PointIndices.push_back(pid);
            if (i > 0u) stipe.EdgeOrdinals.push_back(body.AppendDistanceConstraint(stipe.PointIndices[i - 1u], pid, 1600.0f, 0.0f, true));
            if (i > 1u) body.AppendBendConstraint(stipe.PointIndices[i - 2u], pid, 16.0f);
            if (i >= 2u && i < segs && ((i & 1u) == 0u))
            {
                body.PointBeads.push_back({
                    pid, { 0.036f, 0.044f, 0.036f }, { 0.03f, 0.0f, 0.0f },
                    { 0.36f, 0.58f, 0.18f }, { 0.22f, 0.65f, 0.0f }
                });
                body.FoliageBlades.push_back({
                    pid, stipe.PointIndices[i - 1u], (i & 2u) ? 1 : -1, static_cast<float>(i) * 1.4f + fk, 0.15f,
                    0.46f, 0.095f, 0.42f,
                    { 0.08f, 0.34f, 0.16f }, { 0.30f, 0.64f, 0.22f }, { 0.26f, 0.78f, 0.0f }
                });
            }
        }
        body.StrandTubes.push_back(std::move(stipe));
    }
}

void DeformablePresetRegistry::AppendWovenCloth(DeformableBody& body, uint32_t cols, uint32_t rows, float width, float height)
{
    const uint32_t startIdx = body.PointCount;
    for (uint32_t r = 0u; r < rows; ++r)
    {
        for (uint32_t c = 0u; c < cols; ++c)
        {
            const float u = static_cast<float>(c) / static_cast<float>(cols - 1u);
            const float v = static_cast<float>(r) / static_cast<float>(rows - 1u);
            const bool pin = (r == 0u && (c == 0u || c == 4u || c == 9u || c == 14u || c + 1u == cols));
            body.AppendMassPoint(-width * 0.5f + u * width, 1.45f - v * height, std::sin(u * kPi) * 0.04f,
                                 pin ? 0.0f : 1.0f, 0.03f, 1.35f);
        }
    }

    for (uint32_t k = 0u; k <= 8u; ++k)
    {
        body.StaticProps.push_back({
            { -1.4f + static_cast<float>(k) * 0.35f, 1.48f, 0.0f },
            { 0.20f, 0.035f, 0.035f }, { 0.55f, 0.58f, 0.65f }, { 0.25f, 0.0f, 0.85f }
        });
    }

    ClothSheetAttachment sheet {};
    sheet.Columns = cols;
    sheet.Rows    = rows;
    sheet.StartPointIndex = startIdx;
    sheet.HorizontalEdges.assign(static_cast<size_t>(rows) * (cols - 1u), -1);
    sheet.VerticalEdges.assign(static_cast<size_t>(rows - 1u) * cols, -1);

    for (uint32_t r = 0u; r < rows; ++r)
    {
        for (uint32_t c = 0u; c < cols; ++c)
        {
            const uint32_t i = startIdx + r * cols + c;
            const bool topHem = (r == 0u);
            const float hStiff = topHem ? 3800.0f : 1800.0f;
            const float hBreak = topHem ? 0.0f : 2.35f;
            const float vStiff = topHem ? 3200.0f : 1800.0f;
            const float vBreak = topHem ? 0.0f : 2.35f;
            if (c + 1u < cols) sheet.HorizontalEdges[r * (cols - 1u) + c] = body.AppendDistanceConstraint(i, i + 1u, hStiff, hBreak, true);
            if (r + 1u < rows) sheet.VerticalEdges[r * cols + c]          = body.AppendDistanceConstraint(i, i + cols, vStiff, vBreak, true);
            if (c + 1u < cols && r + 1u < rows)
            {
                body.AppendDistanceConstraint(i, i + cols + 1u, 650.0f, topHem ? 0.0f : 2.6f, false);
                body.AppendDistanceConstraint(i + 1u, i + cols, 650.0f, topHem ? 0.0f : 2.6f, false);
            }
            if (c + 2u < cols) body.AppendBendConstraint(i, i + 2u, 22.0f);
            if (r + 2u < rows) body.AppendBendConstraint(i, i + cols * 2u, 22.0f);
        }
    }
    body.ClothSheets.push_back(std::move(sheet));
}

bool DeformablePresetRegistry::BuildPreset(
    DeformablePresetKind kind,
    DeformableBody& outBody,
    DeformableSimulationConfiguration& outConfiguration)
{
    const size_t ordinal = static_cast<size_t>(kind);
    if (ordinal >= kPresets.size())
    {
        return false;
    }

    const DeformablePresetDescriptor& desc = kPresets[ordinal];
    outBody.ResetAll();
    outBody.PresetId          = desc.PresetId;
    outBody.DisplayTitle      = desc.DisplayTitle;
    outBody.PresetKind        = kind;
    outBody.GroundY           = desc.GroundY;
    outBody.UnderwaterEnabled = desc.DefaultConfig.UnderwaterEnabled;
    outBody.Collider.CenterMeters = desc.ColliderPos;
    outBody.Collider.RadiusMeters = desc.ColliderRadius;
    outBody.Collider.Active       = desc.DefaultConfig.ColliderEnabled;
    outConfiguration              = desc.DefaultConfig;

    switch (kind)
    {
    case DeformablePresetKind::SportTireFem:
        AppendFemSoftBodyTire(outBody, 0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f, 36u);
        break;
    case DeformablePresetKind::SportTirePneumatic:
        AppendPneumaticXpbdTire(outBody, 0.0f, -0.62f, 0.0f, 0.66f, 1.14f, 0.42f, 48u);
        break;
    case DeformablePresetKind::BotanicalFerns:
        AppendFernCluster(outBody, 0.0f, -1.72f, 0.0f, 11u);
        AppendFernCluster(outBody, -1.35f, -1.72f, -0.45f, 7u);
        AppendFernCluster(outBody, 1.35f, -1.72f, -0.35f, 7u);
        break;
    case DeformablePresetKind::OrbSpiderweb:
        AppendOrbSpiderweb(outBody, 0.0f, 0.15f, 0.0f, 1.75f, 16u, 8u);
        break;
    case DeformablePresetKind::CablesChains:
    {
        // Two heavy steel anchor posts + 3 hanging catenary cables + 2 heavy interlocking chain strands
        outBody.StaticProps.push_back({ { -2.2f, 0.0f, 0.0f }, { 0.09f, 1.85f, 0.09f }, { 0.36f, 0.39f, 0.44f }, { 0.32f, 0.0f, 0.85f } });
        outBody.StaticProps.push_back({ {  2.2f, 0.0f, 0.0f }, { 0.09f, 1.85f, 0.09f }, { 0.36f, 0.39f, 0.44f }, { 0.32f, 0.0f, 0.85f } });

        for (uint32_t c = 0u; c < 3u; ++c)
        {
            const float yA = 1.45f - static_cast<float>(c) * 0.48f;
            const float zA = (static_cast<float>(c) - 1.0f) * 0.32f;
            constexpr uint32_t segs = 22u;
            StrandTubeAttachment cable {};
            cable.RadiusStart = 0.032f;
            cable.RadiusEnd   = 0.032f;
            cable.RingSides   = 7u;
            cable.ColourStart = { 0.15f, 0.17f, 0.20f };
            cable.ColourEnd   = { 0.22f, 0.24f, 0.28f };
            cable.MaterialPbr = { 0.36f, 0.0f, 0.65f };
            for (uint32_t i = 0u; i <= segs; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(segs);
                const float sag = std::sin(u * kPi) * (0.45f + static_cast<float>(c) * 0.18f);
                const uint32_t pid = outBody.AppendMassPoint(-2.2f + u * 4.4f, yA - sag, zA,
                                                             (i == 0u || i == segs) ? 0.0f : 1.0f, 0.035f, 0.55f);
                cable.PointIndices.push_back(pid);
                if (i > 0u) cable.EdgeOrdinals.push_back(outBody.AppendDistanceConstraint(cable.PointIndices[i - 1u], pid, 2600.0f, 0.0f, true));
                if (i > 1u) outBody.AppendBendConstraint(cable.PointIndices[i - 2u], pid, 24.0f);
            }
            outBody.StrandTubes.push_back(std::move(cable));
        }
        for (uint32_t ch = 0u; ch < 2u; ++ch)
        {
            const float yA = 0.35f - static_cast<float>(ch) * 0.55f;
            const float zA = (ch == 0u) ? 0.42f : -0.42f;
            constexpr uint32_t segs = 18u;
            ChainStrandAttachment chain {};
            chain.LinkRadius = 0.068f;
            for (uint32_t i = 0u; i <= segs; ++i)
            {
                const float u = static_cast<float>(i) / static_cast<float>(segs);
                const float sag = std::sin(u * kPi) * 0.62f;
                const uint32_t pid = outBody.AppendMassPoint(-2.2f + u * 4.4f, yA - sag, zA,
                                                             (i == 0u || i == segs) ? 0.0f : 0.65f, 0.055f, 0.35f);
                chain.PointIndices.push_back(pid);
                if (i > 0u) chain.EdgeOrdinals.push_back(outBody.AppendDistanceConstraint(chain.PointIndices[i - 1u], pid, 3200.0f, 0.0f, true));
            }
            outBody.ChainStrands.push_back(std::move(chain));
        }
        break;
    }
    case DeformablePresetKind::HangingVines:
    {
        for (int32_t k = -5; k <= 5; ++k)
        {
            const float fk = static_cast<float>(k);
            const float x  = fk * 0.38f;
            const float z  = std::sin(fk * 1.3f) * 0.42f;
            const float len = 2.05f + DeterministicHash3(fk, 2.0f, 5.0f) * 0.95f;
            AppendHangingVine(outBody, x, 1.75f, z, 13u, len, 1u);
        }
        break;
    }
    case DeformablePresetKind::MarshReeds:
        AppendReedCluster(outBody, 0.0f, -1.75f, 0.0f, 18u);
        AppendReedCluster(outBody, -1.2f, -1.75f, -0.35f, 12u);
        break;
    case DeformablePresetKind::UnderwaterKelp:
        AppendKelpForest(outBody, 0.0f, -1.74f, 0.0f, 12u);
        break;
    case DeformablePresetKind::WovenCloth:
        AppendWovenCloth(outBody, 19u, 15u, 2.7f, 2.1f);
        break;
    }

    if (outBody.TetCount > 0u)
    {
        TetrahedralSolidBuilder::PrecomputeRestMatrices(outBody);
    }
    ConflictColorPartitioner::PartitionAllConstraints(outBody);
    return true;
}

} // namespace Frontier::SimulationSpace::Deformable
