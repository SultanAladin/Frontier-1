/*============================================================================================================================================
                                               DEFORMABLERUNTIMEEXTENSIONVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D17-D20 End-to-end validation of shipped .VertexAtlas assets, VehicleDeformableCoupling,
//    DeformableExtension (registered inside Frontier::Lifecycle::LifecycleSequence), and direct C++20
//    4-wheel deformed vehicle tire PBR render proof (Exhibits/DeformableRenders/Vehicle4WheelTireDeformation.png).

#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasPlayer.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/Deformable/DeformableExtension.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const std::string& label)
{
    if (cond) ++gPassed;
    else
    {
        ++gFailed;
        std::cerr << "[FAIL] " << label << "\n";
    }
}

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };
Vec3 Sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3 Normalize(Vec3 a)
{
    const float l = std::sqrt(Dot(a, a));
    return (l > 1.0e-6f) ? Vec3 { a.x / l, a.y / l, a.z / l } : Vec3 { 0.0f, 1.0f, 0.0f };
}

float AcesCurve(float x)
{
    x = std::max(0.0f, x);
    const float num = x * (2.51f * x + 0.03f);
    const float den = x * (2.43f * x + 0.59f) + 0.14f;
    return std::pow(std::clamp(num / den, 0.0f, 1.0f), 1.0f / 2.2f);
}

void RenderVehicleProofPng(
    const Frontier::GeometrySpace::Deformation::ProceduralRenderSurface& surf,
    const std::filesystem::path& targetPath,
    uint32_t w,
    uint32_t h)
{
    const Vec3 eye    { 3.35f, 1.55f, 3.65f };
    const Vec3 target { 0.10f, 0.42f, 0.05f };
    const Vec3 fwd    = Normalize(Sub(target, eye));
    const Vec3 right  = Normalize(Cross(fwd, { 0.0f, 1.0f, 0.0f }));
    const Vec3 up     = Cross(right, fwd);
    const float focal = (0.5f * static_cast<float>(h)) / std::tan(0.5f * 38.0f * 3.14159265f / 180.0f);

    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u, 255u);
    std::vector<float> zBuf(static_cast<size_t>(w) * h, 1.0e9f);

    // Asphalt test-track studio ground + sky backdrop
    for (uint32_t y = 0u; y < h; ++y)
    {
        const float vy = static_cast<float>(y) / static_cast<float>(h);
        const float ny = (0.5f * static_cast<float>(h) - static_cast<float>(y)) / focal;
        for (uint32_t x = 0u; x < w; ++x)
        {
            const float nx = (static_cast<float>(x) - 0.5f * static_cast<float>(w)) / focal;
            const Vec3 ray = Normalize({
                fwd.x + right.x * nx + up.x * ny,
                fwd.y + right.y * nx + up.y * ny,
                fwd.z + right.z * nx + up.z * ny
            });
            float r = 0.07f + 0.15f * (1.0f - vy);
            float g = 0.09f + 0.17f * (1.0f - vy);
            float b = 0.13f + 0.23f * (1.0f - vy);
            if (ray.y < -1.0e-4f)
            {
                const float t = (0.0f - eye.y) / ray.y;
                if (t > 0.1f && t < 40.0f)
                {
                    const float gx = eye.x + ray.x * t;
                    const float gz = eye.z + ray.z * t;
                    const float dist = std::hypot(gx, gz);
                    const float fade = std::exp(-dist * dist * 0.015f);
                    const float grid = (std::fabs(gx - std::round(gx)) < 0.025f || std::fabs(gz - std::round(gz)) < 0.025f) ? 1.0f : 0.0f;
                    const float stripe = (std::fabs(gz - 0.85f) < 0.04f && std::fmod(std::fabs(gx), 0.7f) < 0.35f) ? 0.22f : 0.0f;
                    r = r * (1.0f - fade) + (0.11f + grid * 0.14f + stripe * 1.1f) * fade;
                    g = g * (1.0f - fade) + (0.115f + grid * 0.15f + stripe * 0.35f) * fade;
                    b = b * (1.0f - fade) + (0.125f + grid * 0.17f + stripe * 0.15f) * fade;
                    zBuf[static_cast<size_t>(y) * w + x] = t;
                }
            }
            const size_t px = (static_cast<size_t>(y) * w + x) * 4u;
            rgba[px + 0u] = static_cast<uint8_t>(std::clamp(AcesCurve(r) * 255.0f, 0.0f, 255.0f));
            rgba[px + 1u] = static_cast<uint8_t>(std::clamp(AcesCurve(g) * 255.0f, 0.0f, 255.0f));
            rgba[px + 2u] = static_cast<uint8_t>(std::clamp(AcesCurve(b) * 255.0f, 0.0f, 255.0f));
        }
    }

    auto project = [&](Vec3 p, float& sx, float& sy, float& depth) -> bool
    {
        const Vec3 d = Sub(p, eye);
        depth = Dot(d, fwd);
        if (depth < 0.15f) return false;
        sx = 0.5f * static_cast<float>(w) + (Dot(d, right) / depth) * focal;
        sy = 0.5f * static_cast<float>(h) - (Dot(d, up) / depth) * focal;
        return true;
    };

    const Vec3 keyLight  = Normalize({ 0.55f, 0.78f, 0.42f });
    const Vec3 fillLight = Normalize({ -0.60f, 0.38f, -0.52f });

    for (uint32_t ti = 0u; ti < surf.TriangleCount(); ++ti)
    {
        const uint32_t i0 = surf.Indices[ti * 3u + 0u];
        const uint32_t i1 = surf.Indices[ti * 3u + 1u];
        const uint32_t i2 = surf.Indices[ti * 3u + 2u];
        const Vec3 p0 { surf.Positions[i0 * 3u], surf.Positions[i0 * 3u + 1u], surf.Positions[i0 * 3u + 2u] };
        const Vec3 p1 { surf.Positions[i1 * 3u], surf.Positions[i1 * 3u + 1u], surf.Positions[i1 * 3u + 2u] };
        const Vec3 p2 { surf.Positions[i2 * 3u], surf.Positions[i2 * 3u + 1u], surf.Positions[i2 * 3u + 2u] };

        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        if (!project(p0, x0, y0, z0) || !project(p1, x1, y1, z1) || !project(p2, x2, y2, z2)) continue;
        const float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (std::fabs(area) < 0.05f) continue;

        const int minX = std::max(0, static_cast<int>(std::floor(std::min({ x0, x1, x2 }))));
        const int maxX = std::min(static_cast<int>(w) - 1, static_cast<int>(std::ceil(std::max({ x0, x1, x2 }))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({ y0, y1, y2 }))));
        const int maxY = std::min(static_cast<int>(h) - 1, static_cast<int>(std::ceil(std::max({ y0, y1, y2 }))));

        const Vec3 n0 { surf.Normals[i0 * 3u], surf.Normals[i0 * 3u + 1u], surf.Normals[i0 * 3u + 2u] };
        const Vec3 n1 { surf.Normals[i1 * 3u], surf.Normals[i1 * 3u + 1u], surf.Normals[i1 * 3u + 2u] };
        const Vec3 n2 { surf.Normals[i2 * 3u], surf.Normals[i2 * 3u + 1u], surf.Normals[i2 * 3u + 2u] };

        const float invArea = 1.0f / area;
        for (int py = minY; py <= maxY; ++py)
        {
            const float fy = static_cast<float>(py) + 0.5f;
            for (int px = minX; px <= maxX; ++px)
            {
                const float fx = static_cast<float>(px) + 0.5f;
                const float w0 = ((x1 - fx) * (y2 - fy) - (y1 - fy) * (x2 - fx)) * invArea;
                const float w1 = ((x2 - fx) * (y0 - fy) - (y2 - fy) * (x0 - fx)) * invArea;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                const float depth = w0 * z0 + w1 * z1 + w2 * z2;
                const size_t idx = static_cast<size_t>(py) * w + static_cast<size_t>(px);
                if (depth >= zBuf[idx]) continue;
                zBuf[idx] = depth;

                Vec3 N = Normalize({
                    n0.x * w0 + n1.x * w1 + n2.x * w2,
                    n0.y * w0 + n1.y * w1 + n2.y * w2,
                    n0.z * w0 + n1.z * w1 + n2.z * w2
                });
                const Vec3 wp { p0.x * w0 + p1.x * w1 + p2.x * w2, p0.y * w0 + p1.y * w1 + p2.y * w2, p0.z * w0 + p1.z * w1 + p2.z * w2 };
                const Vec3 V = Normalize(Sub(eye, wp));
                if (Dot(N, V) < 0.0f) N = { -N.x, -N.y, -N.z };

                const float ar = surf.AlbedoRgb[i0 * 3u] * w0 + surf.AlbedoRgb[i1 * 3u] * w1 + surf.AlbedoRgb[i2 * 3u] * w2;
                const float ag = surf.AlbedoRgb[i0 * 3u + 1u] * w0 + surf.AlbedoRgb[i1 * 3u + 1u] * w1 + surf.AlbedoRgb[i2 * 3u + 1u] * w2;
                const float ab = surf.AlbedoRgb[i0 * 3u + 2u] * w0 + surf.AlbedoRgb[i1 * 3u + 2u] * w1 + surf.AlbedoRgb[i2 * 3u + 2u] * w2;
                const float rough = std::clamp(surf.MaterialPbr[i0 * 3u] * w0 + surf.MaterialPbr[i1 * 3u] * w1 + surf.MaterialPbr[i2 * 3u] * w2, 0.08f, 0.95f);
                const float metal = surf.MaterialPbr[i0 * 3u + 2u] * w0 + surf.MaterialPbr[i1 * 3u + 2u] * w1 + surf.MaterialPbr[i2 * 3u + 2u] * w2;

                const float ndl  = std::max(0.0f, Dot(N, keyLight));
                const float fill = std::max(0.0f, Dot(N, fillLight));
                const Vec3 H = Normalize({ keyLight.x + V.x, keyLight.y + V.y, keyLight.z + V.z });
                const float spec = std::pow(std::max(0.0f, Dot(N, H)), 14.0f + (1.0f - rough) * 90.0f) * (0.14f + 0.65f * metal + 0.25f * (1.0f - rough));
                const float hemi = 0.30f + 0.28f * (0.5f + 0.5f * N.y);
                const float dSc  = 1.0f - 0.55f * metal;

                const size_t o = idx * 4u;
                rgba[o + 0u] = static_cast<uint8_t>(std::clamp(AcesCurve(ar * (hemi + ndl * 1.48f * dSc + fill * 0.40f) + spec * (0.55f + 0.45f * ar)) * 255.0f, 0.0f, 255.0f));
                rgba[o + 1u] = static_cast<uint8_t>(std::clamp(AcesCurve(ag * (hemi + ndl * 1.44f * dSc + fill * 0.42f) + spec * (0.55f + 0.45f * ag)) * 255.0f, 0.0f, 255.0f));
                rgba[o + 2u] = static_cast<uint8_t>(std::clamp(AcesCurve(ab * (hemi + ndl * 1.40f * dSc + fill * 0.46f) + spec * (0.58f + 0.42f * ab)) * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    std::filesystem::create_directories(targetPath.parent_path());
    const auto png = Frontier::GeometrySpace::Deformation::VertexMotionAtlasExporter::EncodeRgba8Png(w, h, rgba);
    std::ofstream f(targetPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace

int main()
{
    using namespace Frontier::Codecs::Simulation;
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::Lifecycle;
    using namespace Frontier::SimulationSpace::Deformable;

    // ---------------------------------------------------------------------------------------------------------
    // D17: Verify BOTH shipped binary .VertexAtlas assets decode cleanly with FNV-1a integrity & 0-CPU playback
    // ---------------------------------------------------------------------------------------------------------
    for (const char* pathStr : {
             "EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas",
             "EngineContent/Simulation/Deformables/SportTirePneumatic.VertexAtlas" })
    {
        VertexAtlasDocumentModel doc {};
        std::string err;
        const bool ok = VertexAtlasCodec::DecodeFromFile(pathStr, doc, &err);
        Check(ok && doc.Atlas.Ready(), std::string("D17: Shipped binary .VertexAtlas decodes cleanly: ") + pathStr);
        Check(doc.Atlas.VertexCount == 7552u && doc.Atlas.SliceCount == 16u,
              std::string("D17: Expected 7552 verts x 16 slices in ") + pathStr);

        VertexMotionAtlasPlayer player {};
        player.SetAtlas(&doc.Atlas);
        player.ScrubToSlice(7.4f);
        const auto reading = player.AdvanceAndSamplePose(1.0f / 60.0f);
        DeformedSurfaceSample vatSample {};
        VertexMotionAtlasPlayer::SampleInterpolatedSurface(doc.Atlas, reading, vatSample);
        Check(vatSample.Positions.size() == 7552u * 3u && reading.Slice0 < doc.Atlas.SliceCount && std::isfinite(reading.Pose.AxleY),
              std::string("D17: Zero-CPU VertexMotionAtlasPlayer evaluates smoothly for ") + pathStr);
    }

    // ---------------------------------------------------------------------------------------------------------
    // D18 & D19: Register DeformableExtension inside Frontier::Lifecycle::LifecycleSequence
    // ---------------------------------------------------------------------------------------------------------
    DeformableExtensionParameters params {};
    params.FixedStepHz            = 60.0f;
    params.HeroExchangePreset     = DeformablePresetKind::SportTireFem;
    params.ShippedVertexAtlasPath = "EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas";
    params.EnableVehicleCoupling  = true;

    DeformableExtension ext(params);
    LifecycleSequence seq {};
    const uint32_t slotId = seq.RegisterParticipant("DeformableExtension", ext);
    Check(slotId == 0u, "D19: RegisterParticipant registers DeformableExtension into LifecycleSequence");
    Check(seq.Initialize() && ext.QueryInitialized(), "D19: LifecycleSequence::Initialize initializes DeformableExtension");
    Check(ext.InspectTelemetry().HasShippedVertexAtlas, "D19: DeformableExtension auto-loads shipped SportTireFem.VertexAtlas");

    // Spawn 2 world .SoftBody instances alongside the 4-wheel coupled sport vehicle
    std::string spawnErr;
    const uint32_t fernId  = ext.AccessWorldRegistry().SpawnFromSoftBodyFile(
        "EngineContent/Simulation/Deformables/BotanicalFerns.SoftBody", { 4.0f, 0.0f, -2.5f }, "TracksideFerns", &spawnErr);
    const uint32_t clothId = ext.AccessWorldRegistry().SpawnFromSoftBodyFile(
        "EngineContent/Simulation/Deformables/WovenCloth.SoftBody", { -5.0f, 0.0f, -2.0f }, "TracksideBanner", &spawnErr);
    Check(fernId != 0u && clothId != 0u, "D19: World registry spawns trackside .SoftBody instances inside DeformableExtension");

    // Configure 4-wheel sport car cornering right (+14 deg steer) with heated front-right tire striking a curb
    auto car = VehicleDeformableCoupling::BuildCanonicalSportCarInput({ 0.0f, 0.39f, 0.0f });
    car.CornerPlacements[0].SteerYawRad = 0.24f;
    car.CornerPlacements[1].SteerYawRad = 0.24f;
    car.CornerInputs[0].SteerAngleRad   = 0.24f;
    car.CornerInputs[1].SteerAngleRad   = 0.24f;
    car.CornerInputs[1].VerticalLoadNewtons   = 5600.0f; // Heavy outside-front weight transfer
    car.CornerInputs[1].CarcassTempKelvin     = 348.0f;  // Hot race tire -> Ideal Gas pressure surge
    car.CornerInputs[1].RoadBumpHeightMeters  = 0.16f;   // Riding apex curb!
    car.CornerInputs[1].RoadBumpOffsetXMeters = 0.18f;
    for (uint32_t w = 0u; w < 4u; ++w)
    {
        car.CornerInputs[w].AngularVelocityRadS = 12.0f;
        car.CornerInputs[w].SpinAngleRad        = 0.65f;
    }
    ext.AssignVehicleChassisInput(car);
    ext.AssignCameraWorldPosition({ 3.35f, 1.55f, 3.65f }); // Close camera -> LiveFem3D

    for (uint32_t tick = 0u; tick < 35u; ++tick)
    {
        seq.Tick(1.0 / 60.0);
    }

    const auto closeTel = ext.InspectTelemetry();
    Check(closeTel.CompletedTicks >= 34ull, "D19: LifecycleSequence::Tick advances DeformableExtension fixed steps");
    Check(closeTel.LastVehicleSummary.LiveFemWheelCount == 4u, "D18: Close camera drives all 4 wheels in LiveFem3D");
    Check(closeTel.LastVehicleSummary.EffectivePressurePsi[1] > 37.0f,
          "D18: Heated front-right carcass (348 K) elevates Ideal Gas inflation pressure > 37 psi");
    Check(closeTel.LastVehicleSummary.PeakVehicleTireStrain > 0.02f,
          "D18: Curb contact produces measurable non-zero rubber strain");

    // Verify wait-free hero read lease
    {
        const auto lease = ext.AcquireHeroReadLease();
        Check(lease.InspectHeader().GenerationSequence >= 34ull && lease->PointCount > 400u,
              "D19: Wait-free AcquireHeroReadLease exposes fresh simulation generation");
    }

    // Build full 4-wheel world vehicle ProceduralRenderSurface & render visual proof PNG!
    ProceduralRenderSurface vehicleSurf {};
    ext.InspectVehicleCoupling().BuildWorldVehicleSurface(car, vehicleSurf, true);
    // Add the orange apex curb under the front-right wheel so it's visible in the render
    {
        DeformableBody curbBody {};
        curbBody.StaticProps.push_back({
            { car.CornerPlacements[1].WheelCenterMeters[0] + 0.14f, 0.055f, car.CornerPlacements[1].WheelCenterMeters[2] },
            { 0.64f, 0.11f, 0.36f },
            { 0.94f, 0.42f, 0.14f },
            { 0.55f, 0.0f, 0.05f }
        });
        DeformableSurfaceBuilder::AppendBodyAttachments(curbBody, vehicleSurf, true, false);
    }
    Check(vehicleSurf.VertexCount() > 32000u && vehicleSurf.TriangleCount() > 53000u,
          "D18: 4-wheel world vehicle ProceduralRenderSurface contains >32k verts & >53k tris");

    const std::filesystem::path proofPng = "Exhibits/DeformableRenders/Vehicle4WheelTireDeformation.png";
    RenderVehicleProofPng(vehicleSurf, proofPng, 960u, 540u);
    Check(std::filesystem::exists(proofPng) && std::filesystem::file_size(proofPng) > 50000u,
          "D20: Rendered direct C++20 4-wheel vehicle tire deformation PNG proof");

    // Verify mid-distance LivePneumaticXpbd and far-distance 0-CPU BakedVatZeroCpu LOD transitions!
    ext.AssignCameraWorldPosition({ 30.0f, 5.0f, 0.0f });
    seq.Tick(1.0 / 60.0);
    Check(ext.InspectTelemetry().LastVehicleSummary.LiveXpbdWheelCount == 4u,
          "D18: Mid-range camera (30 m) transitions all 4 wheels cleanly to LivePneumaticXpbd");

    ext.AssignCameraWorldPosition({ 85.0f, 12.0f, 0.0f });
    seq.Tick(1.0 / 60.0);
    Check(ext.InspectTelemetry().LastVehicleSummary.BakedVatWheelCount == 4u,
          "D18: Far camera (85 m) transitions all 4 wheels cleanly to 0-CPU shipped .VertexAtlas playback");

    // Verify Suspend / Resume / Finalize lifecycle semantics
    const uint64_t ticksBeforeSuspend = ext.InspectTelemetry().CompletedTicks;
    Check(seq.Suspend() && ext.QuerySuspended(), "D19: LifecycleSequence::Suspend suspends DeformableExtension");
    seq.Tick(1.0 / 60.0);
    seq.Tick(1.0 / 60.0);
    Check(ext.InspectTelemetry().CompletedTicks == ticksBeforeSuspend,
          "D19: Suspended DeformableExtension freezes tick counter cleanly");
    const bool resumed = seq.Resume();
    seq.Tick(1.0 / 60.0);
    Check(resumed && !ext.QuerySuspended() && ext.InspectTelemetry().CompletedTicks > ticksBeforeSuspend,
          "D19: LifecycleSequence::Resume resumes deterministic stepping");
    seq.Finalize();
    Check(!ext.QueryInitialized(), "D19: LifecycleSequence::Finalize tears down DeformableExtension cleanly");

    std::cout << "DeformableRuntimeExtensionValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
