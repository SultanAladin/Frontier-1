/*============================================================================================================================================
                                                   VEHICLEDEFORMABLECOUPLING.CPP
============================================================================================================================================*/
// 🧩 Phase D18 Native 4-wheel VehicleReading -> VehicleTireDeformBridge coupling & world-space wheel surface assembly

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/VehicleDeformableCoupling.h"
#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

namespace
{

constexpr float kPi = 3.14159265358979323846f;

void AppendTransformedWheelGeometry(
    const GeometrySpace::Deformation::HighPolySurfaceRest& restSurface,
    const GeometrySpace::Deformation::DeformedSurfaceSample& liveSample,
    const VehicleWheelPlacement& placement,
    float spinAngleRad,
    GeometrySpace::Deformation::ProceduralRenderSurface& outSurface)
{
    using namespace GeometrySpace::Deformation;

    const float scale = placement.WheelScale;
    const float cy    = std::cos(placement.SteerYawRad);
    const float sy    = std::sin(placement.SteerYawRad);
    const float cc    = std::cos(placement.CamberRollRad);
    const float sc    = std::sin(placement.CamberRollRad);
    const float zSign = placement.RightSideMirror ? -1.0f : 1.0f;

    // Canonical tire axle rests at (0, -0.62, 0) with radius ~1.15m
    constexpr float canonicalAxleY = -0.62f;

    auto transformPoint = [&](float lx, float ly, float lz) -> std::array<float, 3>
    {
        // Local tire space centered at axle (X=longitudinal, Y=vertical, Z=lateral)
        float px = lx * scale;
        float py = (ly - canonicalAxleY) * scale;
        float pz = lz * scale * zSign;

        // Camber roll around longitudinal X
        const float py1 = py * cc - pz * sc;
        const float pz1 = py * sc + pz * cc;

        // Steering yaw around vertical Y
        const float px2 = px * cy + pz1 * sy;
        const float pz2 = -px * sy + pz1 * cy;

        return {
            placement.WheelCenterMeters[0] + px2,
            placement.WheelCenterMeters[1] + py1,
            placement.WheelCenterMeters[2] + pz2
        };
    };

    auto transformNormal = [&](float nx, float ny, float nz) -> std::array<float, 3>
    {
        float rx = nx;
        float ry = ny;
        float rz = nz * zSign;

        const float ry1 = ry * cc - rz * sc;
        const float rz1 = ry * sc + rz * cc;

        const float rx2 = rx * cy + rz1 * sy;
        const float rz2 = -rx * sy + rz1 * cy;

        const float l = std::sqrt(rx2 * rx2 + ry1 * ry1 + rz2 * rz2);
        return (l > 1.0e-6f) ? std::array<float, 3> { rx2 / l, ry1 / l, rz2 / l } : std::array<float, 3> { 0.0f, 1.0f, 0.0f };
    };

    // Derive exact live axle center from the deformed tire's inner bead ring (rest radius ~0.66m) so rim stays 100% concentric
    double beadSumX = 0.0, beadSumY = 0.0;
    uint32_t beadCount = 0u;
    const uint32_t vCount = restSurface.VertexCount();
    for (uint32_t i = 0u; i < vCount; ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        const float rRest = std::hypot(restSurface.RestPositions[i3], restSurface.RestPositions[i3 + 1u] - canonicalAxleY);
        if (rRest < 0.72f)
        {
            beadSumX += liveSample.Positions[i3];
            beadSumY += liveSample.Positions[i3 + 1u];
            ++beadCount;
        }
    }
    const float liveAxleX = (beadCount > 0u) ? static_cast<float>(beadSumX / static_cast<double>(beadCount)) : 0.0f;
    const float liveAxleY = (beadCount > 0u) ? static_cast<float>(beadSumY / static_cast<double>(beadCount)) : canonicalAxleY;

    // 1. Deformed high-poly tire tread & sidewall surface
    const uint32_t vStart = outSurface.VertexCount();
    for (uint32_t i = 0u; i < vCount; ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        const auto wp = transformPoint(liveSample.Positions[i3], liveSample.Positions[i3 + 1u], liveSample.Positions[i3 + 2u]);
        const auto wn = transformNormal(liveSample.Normals[i3], liveSample.Normals[i3 + 1u], liveSample.Normals[i3 + 2u]);

        outSurface.Positions.push_back(wp[0]);
        outSurface.Positions.push_back(wp[1]);
        outSurface.Positions.push_back(wp[2]);
        outSurface.Normals.push_back(wn[0]);
        outSurface.Normals.push_back(wn[1]);
        outSurface.Normals.push_back(wn[2]);

        // Subtle heat/strain highlight on peak contact patch rubber
        const float strain = (i < liveSample.Strain.size()) ? std::clamp(liveSample.Strain[i] * 0.22f, 0.0f, 0.16f) : 0.0f;
        outSurface.AlbedoRgb.push_back(std::min(1.0f, restSurface.AlbedoRgb[i3 + 0u] + strain * 0.85f));
        outSurface.AlbedoRgb.push_back(std::max(0.0f, restSurface.AlbedoRgb[i3 + 1u] - strain * 0.10f));
        outSurface.AlbedoRgb.push_back(std::max(0.0f, restSurface.AlbedoRgb[i3 + 2u] - strain * 0.15f));

        outSurface.MaterialPbr.push_back(restSurface.MaterialPbr[i3 + 0u]);
        outSurface.MaterialPbr.push_back(restSurface.MaterialPbr[i3 + 1u]);
        outSurface.MaterialPbr.push_back(restSurface.MaterialPbr[i3 + 2u]);
    }

    const size_t tCount = restSurface.Indices.size() / 3u;
    for (size_t t = 0u; t < tCount; ++t)
    {
        const uint32_t i0 = vStart + restSurface.Indices[t * 3u + 0u];
        const uint32_t i1 = vStart + restSurface.Indices[t * 3u + 1u];
        const uint32_t i2 = vStart + restSurface.Indices[t * 3u + 2u];
        if (placement.RightSideMirror)
        {
            outSurface.Indices.push_back(i0);
            outSurface.Indices.push_back(i2);
            outSurface.Indices.push_back(i1);
        }
        else
        {
            outSurface.Indices.push_back(i0);
            outSurface.Indices.push_back(i1);
            outSurface.Indices.push_back(i2);
        }
    }

    // 2. Forged 5-spoke alloy rim locked concentrically to the deformed tire's live bead center
    ProceduralRenderSurface localRim {};
    DeformableSurfaceBuilder::AppendForgedAlloyRim(localRim, liveAxleX, liveAxleY, 0.0f, spinAngleRad, 0.66f, 0.43f);

    const uint32_t rimStart = outSurface.VertexCount();
    for (uint32_t i = 0u; i < localRim.VertexCount(); ++i)
    {
        const size_t i3 = static_cast<size_t>(i) * 3u;
        const auto wp = transformPoint(localRim.Positions[i3], localRim.Positions[i3 + 1u], localRim.Positions[i3 + 2u]);
        const auto wn = transformNormal(localRim.Normals[i3], localRim.Normals[i3 + 1u], localRim.Normals[i3 + 2u]);
        outSurface.Positions.push_back(wp[0]);
        outSurface.Positions.push_back(wp[1]);
        outSurface.Positions.push_back(wp[2]);
        outSurface.Normals.push_back(wn[0]);
        outSurface.Normals.push_back(wn[1]);
        outSurface.Normals.push_back(wn[2]);
        outSurface.AlbedoRgb.push_back(localRim.AlbedoRgb[i3 + 0u]);
        outSurface.AlbedoRgb.push_back(localRim.AlbedoRgb[i3 + 1u]);
        outSurface.AlbedoRgb.push_back(localRim.AlbedoRgb[i3 + 2u]);
        outSurface.MaterialPbr.push_back(localRim.MaterialPbr[i3 + 0u]);
        outSurface.MaterialPbr.push_back(localRim.MaterialPbr[i3 + 1u]);
        outSurface.MaterialPbr.push_back(localRim.MaterialPbr[i3 + 2u]);
    }
    for (size_t t = 0u; t < localRim.Indices.size() / 3u; ++t)
    {
        const uint32_t i0 = rimStart + localRim.Indices[t * 3u + 0u];
        const uint32_t i1 = rimStart + localRim.Indices[t * 3u + 1u];
        const uint32_t i2 = rimStart + localRim.Indices[t * 3u + 2u];
        if (placement.RightSideMirror)
        {
            outSurface.Indices.push_back(i0);
            outSurface.Indices.push_back(i2);
            outSurface.Indices.push_back(i1);
        }
        else
        {
            outSurface.Indices.push_back(i0);
            outSurface.Indices.push_back(i1);
            outSurface.Indices.push_back(i2);
        }
    }
}

void AppendWorldBox(
    const std::array<float, 3>& center,
    const std::array<float, 3>& halfExtents,
    const std::array<float, 3>& albedo,
    const std::array<float, 3>& pbr,
    GeometrySpace::Deformation::ProceduralRenderSurface& outSurface)
{
    DeformableBody boxBody {};
    boxBody.StaticProps.push_back({
        center,
        { halfExtents[0] * 2.0f, halfExtents[1] * 2.0f, halfExtents[2] * 2.0f },
        albedo,
        pbr
    });
    GeometrySpace::Deformation::DeformableSurfaceBuilder::AppendBodyAttachments(boxBody, outSurface, true, false);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                 VEHICLE DEFORMABLE COUPLING
//------------------------------------------------------------------------------------------------------------------------

bool VehicleDeformableCoupling::Initialize(
    const VehicleTireLodPolicy& policy,
    const std::filesystem::path& optionalVertexAtlasPath,
    std::string* outError)
{
    LoadedAtlas_ = {};
    if (!optionalVertexAtlasPath.empty())
    {
        Codecs::Simulation::VertexAtlasDocumentModel doc {};
        if (!Codecs::Simulation::VertexAtlasCodec::DecodeFromFile(optionalVertexAtlasPath, doc, outError))
        {
            return false;
        }
        LoadedAtlas_ = std::move(doc.Atlas);
    }

    TireDriver_.Initialize(policy, LoadedAtlas_.Ready() ? &LoadedAtlas_ : nullptr);
    LastSummary_ = {};
    return TireDriver_.Initialized();
}

void VehicleDeformableCoupling::InitializeWithAtlas(
    const VehicleTireLodPolicy& policy,
    const GeometrySpace::Deformation::VertexMotionAtlas* sharedAtlas)
{
    LoadedAtlas_ = {};
    TireDriver_.Initialize(policy, sharedAtlas);
    LastSummary_ = {};
}

VehicleCouplingSummary VehicleDeformableCoupling::StepVehicle(
    const VehicleChassisCouplingInput& input,
    float dtSeconds,
    float simTimeSeconds,
    const std::array<float, 3>& cameraPositionMeters)
{
    const float dx = input.ChassisOriginMeters[0] - cameraPositionMeters[0];
    const float dy = input.ChassisOriginMeters[1] - cameraPositionMeters[1];
    const float dz = input.ChassisOriginMeters[2] - cameraPositionMeters[2];
    const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

    TireDriver_.StepAllWheels(input.CornerInputs, dtSeconds, simTimeSeconds, dist);

    VehicleCouplingSummary summary {};
    for (uint32_t w = 0u; w < 4u; ++w)
    {
        const auto& tel = TireDriver_.InspectTelemetry(static_cast<WheelCornerOrdinal>(w));
        summary.ActiveTiers[w]            = tel.ActiveTier;
        summary.EffectivePressurePsi[w]   = tel.EffectivePressurePsi;
        summary.RadialDeflectionMeters[w] = tel.RadialDeflectionMeters;
        summary.PeakVehicleTireStrain     = std::max(summary.PeakVehicleTireStrain, tel.PeakStrain);
        if (tel.ActiveTier == WheelSolverTier::LiveFem3D) ++summary.LiveFemWheelCount;
        else if (tel.ActiveTier == WheelSolverTier::LivePneumaticXpbd) ++summary.LiveXpbdWheelCount;
        else ++summary.BakedVatWheelCount;
    }

    LastSummary_ = summary;
    return summary;
}

void VehicleDeformableCoupling::BuildWorldVehicleSurface(
    const VehicleChassisCouplingInput& input,
    GeometrySpace::Deformation::ProceduralRenderSurface& outSurface,
    bool appendChassisBody) const
{
    if (!TireDriver_.Initialized())
    {
        return;
    }

    const auto& restSurface = TireDriver_.InspectSharedTireSurface();
    for (uint32_t w = 0u; w < 4u; ++w)
    {
        GeometrySpace::Deformation::DeformedSurfaceSample live {};
        TireDriver_.EvaluateWheelSurface(static_cast<WheelCornerOrdinal>(w), live);
        AppendTransformedWheelGeometry(
            restSurface,
            live,
            input.CornerPlacements[w],
            input.CornerInputs[w].SpinAngleRad,
            outSurface);
    }

    if (appendChassisBody)
    {
        const auto& c = input.ChassisOriginMeters;
        // Sleek carbon-fiber sport monocoque floorpan + front/rear suspension wishbone beams + rear wing
        AppendWorldBox({ c[0], c[1] + 0.08f, c[2] },        { 1.75f, 0.09f, 0.52f }, { 0.14f, 0.16f, 0.20f }, { 0.35f, 0.0f, 0.65f }, outSurface);
        AppendWorldBox({ c[0] - 0.10f, c[1] + 0.32f, c[2] },{ 0.85f, 0.18f, 0.44f }, { 0.78f, 0.16f, 0.14f }, { 0.22f, 0.0f, 0.40f }, outSurface);
        AppendWorldBox({ c[0] + 1.37f, c[1] + 0.02f, c[2] },{ 0.06f, 0.05f, 0.78f }, { 0.42f, 0.45f, 0.50f }, { 0.28f, 0.0f, 0.85f }, outSurface);
        AppendWorldBox({ c[0] - 1.37f, c[1] + 0.02f, c[2] },{ 0.06f, 0.05f, 0.78f }, { 0.42f, 0.45f, 0.50f }, { 0.28f, 0.0f, 0.85f }, outSurface);
        AppendWorldBox({ c[0] - 1.72f, c[1] + 0.56f, c[2] },{ 0.14f, 0.03f, 0.86f }, { 0.12f, 0.13f, 0.16f }, { 0.30f, 0.0f, 0.55f }, outSurface);
        AppendWorldBox({ c[0] - 1.66f, c[1] + 0.34f, c[2] - 0.34f },{ 0.05f, 0.22f, 0.025f }, { 0.18f, 0.20f, 0.24f }, { 0.32f, 0.0f, 0.72f }, outSurface);
        AppendWorldBox({ c[0] - 1.66f, c[1] + 0.34f, c[2] + 0.34f },{ 0.05f, 0.22f, 0.025f }, { 0.18f, 0.20f, 0.24f }, { 0.32f, 0.0f, 0.72f }, outSurface);
    }
}

VehicleChassisCouplingInput VehicleDeformableCoupling::BuildCanonicalSportCarInput(
    const std::array<float, 3>& chassisOriginMeters)
{
    VehicleChassisCouplingInput in {};
    in.ChassisOriginMeters = chassisOriginMeters;
    in.RoadPlaneY          = chassisOriginMeters[1] - 0.39f;

    constexpr float halfWheelbase = 1.37f;
    constexpr float halfTrack     = 0.83f;
    const float cx = chassisOriginMeters[0];
    const float cy = chassisOriginMeters[1];
    const float cz = chassisOriginMeters[2];

    // 0 = FL (+X front, -Z left), 1 = FR (+X front, +Z right), 2 = RL (-X rear, -Z left), 3 = RR (-X rear, +Z right)
    in.CornerPlacements[0] = { { cx + halfWheelbase, cy, cz - halfTrack }, 0.0f, -0.025f, 0.34f, false };
    in.CornerPlacements[1] = { { cx + halfWheelbase, cy, cz + halfTrack }, 0.0f,  0.025f, 0.34f, true  };
    in.CornerPlacements[2] = { { cx - halfWheelbase, cy, cz - halfTrack }, 0.0f, -0.020f, 0.34f, false };
    in.CornerPlacements[3] = { { cx - halfWheelbase, cy, cz + halfTrack }, 0.0f,  0.020f, 0.34f, true  };

    for (uint32_t w = 0u; w < 4u; ++w)
    {
        in.CornerInputs[w].InContact           = true;
        in.CornerInputs[w].VerticalLoadNewtons = (w < 2u) ? 4100.0f : 3650.0f;
        in.CornerInputs[w].ColdPressurePsi     = 32.0f;
        in.CornerInputs[w].CarcassTempKelvin   = 312.0f;
    }

    (void)kPi;
    return in;
}

} // namespace Frontier::SimulationSpace::Deformable
