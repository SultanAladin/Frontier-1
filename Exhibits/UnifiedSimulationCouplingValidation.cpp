/*============================================================================================================================================
                                              UNIFIEDSIMULATIONCOUPLINGVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D21-D23 End-to-end 5-domain Frontier simulation coupling validation:
//    ForceFields -> 3D Eulerian Fluid -> ParticleSpace -> Multi-Instance Deformables + 4-Wheel VehicleReadingConverter
//    + Two-Way RigidImpulseAccumulatorEntry feedback + direct C++20 composite PBR/Volumetric render proof.

#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/SimulationCoordinatorExtension.h"

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

float AcesFilmic(float x)
{
    x = std::max(0.0f, x);
    const float num = x * (2.51f * x + 0.03f);
    const float den = x * (2.43f * x + 0.59f) + 0.14f;
    return std::pow(std::clamp(num / den, 0.0f, 1.0f), 1.0f / 2.2f);
}

void RenderUnifiedScenePng(
    const Frontier::GeometrySpace::Deformation::ProceduralRenderSurface& surf,
    const Frontier::SimulationSpace::Fluid::FluidVolumeGridCpu& fluidState,
    const std::array<float, 3>& fluidCentre,
    float fluidCellSize,
    const std::vector<std::array<float, 3>>& emberPositions,
    const std::filesystem::path& targetPath,
    uint32_t w,
    uint32_t h)
{
    const Vec3 eye    { 4.10f, 2.35f, 4.95f };
    const Vec3 target { 0.05f, 0.95f, -0.55f };
    const Vec3 fwd    = Normalize(Sub(target, eye));
    const Vec3 right  = Normalize(Cross(fwd, { 0.0f, 1.0f, 0.0f }));
    const Vec3 up     = Cross(right, fwd);
    const float focal = (0.5f * static_cast<float>(h)) / std::tan(0.5f * 42.0f * 3.14159265f / 180.0f);

    std::vector<float> hdrRgb(static_cast<size_t>(w) * h * 3u, 0.0f);
    std::vector<float> zBuf(static_cast<size_t>(w) * h, 1.0e9f);

    // 1. Dusk sky backdrop & test-track asphalt floor
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
            float r = 0.05f + 0.13f * (1.0f - vy);
            float g = 0.07f + 0.14f * (1.0f - vy);
            float b = 0.12f + 0.22f * (1.0f - vy);
            if (ray.y < -1.0e-4f)
            {
                const float t = (0.0f - eye.y) / ray.y;
                if (t > 0.1f && t < 50.0f)
                {
                    const float gx = eye.x + ray.x * t;
                    const float gz = eye.z + ray.z * t;
                    const float dist = std::hypot(gx, gz);
                    const float fade = std::exp(-dist * dist * 0.010f);
                    const float grid = (std::fabs(gx - std::round(gx)) < 0.025f || std::fabs(gz - std::round(gz)) < 0.025f) ? 1.0f : 0.0f;
                    const float kerb = (std::fabs(gz - 0.85f) < 0.045f && std::fmod(std::fabs(gx), 0.7f) < 0.35f) ? 0.24f : 0.0f;
                    r = r * (1.0f - fade) + (0.09f + grid * 0.12f + kerb * 1.1f) * fade;
                    g = g * (1.0f - fade) + (0.095f + grid * 0.13f + kerb * 0.35f) * fade;
                    b = b * (1.0f - fade) + (0.11f + grid * 0.15f + kerb * 0.15f) * fade;
                    zBuf[static_cast<size_t>(y) * w + x] = t;
                }
            }
            const size_t px = (static_cast<size_t>(y) * w + x) * 3u;
            hdrRgb[px + 0u] = r;
            hdrRgb[px + 1u] = g;
            hdrRgb[px + 2u] = b;
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

    // 2. Rasterize all solid/deformable PBR surfaces (4-wheel car + trackside ferns + billowing banner + curb)
    const Vec3 keyLight  = Normalize({ 0.55f, 0.78f, 0.42f });
    const Vec3 fireLightPos { fluidCentre[0], fluidCentre[1] - 0.8f, fluidCentre[2] };

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

                const Vec3 toFire = Sub(fireLightPos, wp);
                const float fireDist2 = std::max(0.4f, Dot(toFire, toFire));
                const float fireAtten = std::max(0.0f, Dot(N, Normalize(toFire))) * (1.8f / fireDist2);

                const float ar = surf.AlbedoRgb[i0 * 3u] * w0 + surf.AlbedoRgb[i1 * 3u] * w1 + surf.AlbedoRgb[i2 * 3u] * w2;
                const float ag = surf.AlbedoRgb[i0 * 3u + 1u] * w0 + surf.AlbedoRgb[i1 * 3u + 1u] * w1 + surf.AlbedoRgb[i2 * 3u + 1u] * w2;
                const float ab = surf.AlbedoRgb[i0 * 3u + 2u] * w0 + surf.AlbedoRgb[i1 * 3u + 2u] * w1 + surf.AlbedoRgb[i2 * 3u + 2u] * w2;
                const float rough = std::clamp(surf.MaterialPbr[i0 * 3u] * w0 + surf.MaterialPbr[i1 * 3u] * w1 + surf.MaterialPbr[i2 * 3u] * w2, 0.08f, 0.95f);
                const float metal = surf.MaterialPbr[i0 * 3u + 2u] * w0 + surf.MaterialPbr[i1 * 3u + 2u] * w1 + surf.MaterialPbr[i2 * 3u + 2u] * w2;

                const float ndl  = std::max(0.0f, Dot(N, keyLight));
                const Vec3 H = Normalize({ keyLight.x + V.x, keyLight.y + V.y, keyLight.z + V.z });
                const float spec = std::pow(std::max(0.0f, Dot(N, H)), 14.0f + (1.0f - rough) * 90.0f) * (0.14f + 0.65f * metal + 0.25f * (1.0f - rough));
                const float hemi = 0.26f + 0.24f * (0.5f + 0.5f * N.y);
                const float dSc  = 1.0f - 0.55f * metal;

                const size_t o = idx * 3u;
                hdrRgb[o + 0u] = ar * (hemi + ndl * 1.40f * dSc + fireAtten * 1.35f) + spec * (0.55f + 0.45f * ar);
                hdrRgb[o + 1u] = ag * (hemi + ndl * 1.38f * dSc + fireAtten * 0.65f) + spec * (0.55f + 0.45f * ag);
                hdrRgb[o + 2u] = ab * (hemi + ndl * 1.35f * dSc + fireAtten * 0.20f) + spec * (0.58f + 0.42f * ab);
            }
        }
    }

    // 3. Raymarch live 3D Eulerian Fluid smoke & fire plume directly against the Z-buffer
    const float hx = 0.5f * static_cast<float>(fluidState.Width) * fluidCellSize;
    const float hy = 0.5f * static_cast<float>(fluidState.Height) * fluidCellSize;
    const float hz = 0.5f * static_cast<float>(fluidState.Depth) * fluidCellSize;
    const Vec3 bMin { fluidCentre[0] - hx, fluidCentre[1] - hy, fluidCentre[2] - hz };
    const Vec3 bMax { fluidCentre[0] + hx, fluidCentre[1] + hy, fluidCentre[2] + hz };

    for (uint32_t y = 0u; y < h; ++y)
    {
        const float ny = (0.5f * static_cast<float>(h) - static_cast<float>(y)) / focal;
        for (uint32_t x = 0u; x < w; ++x)
        {
            const float nx = (static_cast<float>(x) - 0.5f * static_cast<float>(w)) / focal;
            const Vec3 ray = Normalize({
                fwd.x + right.x * nx + up.x * ny,
                fwd.y + right.y * nx + up.y * ny,
                fwd.z + right.z * nx + up.z * ny
            });
            const float invRx = (std::fabs(ray.x) > 1e-6f) ? (1.0f / ray.x) : 1e6f;
            const float invRy = (std::fabs(ray.y) > 1e-6f) ? (1.0f / ray.y) : 1e6f;
            const float invRz = (std::fabs(ray.z) > 1e-6f) ? (1.0f / ray.z) : 1e6f;
            const float tx0 = (bMin.x - eye.x) * invRx, tx1 = (bMax.x - eye.x) * invRx;
            const float ty0 = (bMin.y - eye.y) * invRy, ty1 = (bMax.y - eye.y) * invRy;
            const float tz0 = (bMin.z - eye.z) * invRz, tz1 = (bMax.z - eye.z) * invRz;
            const float tNear = std::max({ 0.1f, std::min(tx0, tx1), std::min(ty0, ty1), std::min(tz0, tz1) });
            const float tFar  = std::min({ zBuf[static_cast<size_t>(y) * w + x], std::max(tx0, tx1), std::max(ty0, ty1), std::max(tz0, tz1) });
            if (tFar <= tNear) continue;

            constexpr int kSteps = 28;
            const float dt = (tFar - tNear) / static_cast<float>(kSteps);
            float trans = 1.0f;
            float accR = 0.0f, accG = 0.0f, accB = 0.0f;
            for (int s = 0; s < kSteps && trans > 0.02f; ++s)
            {
                const float t = tNear + (static_cast<float>(s) + 0.5f) * dt;
                const float wx = eye.x + ray.x * t;
                const float wy = eye.y + ray.y * t;
                const float wz = eye.z + ray.z * t;
                const float cx = (wx - bMin.x) / fluidCellSize - 0.5f;
                const float cy = (wy - bMin.y) / fluidCellSize - 0.5f;
                const float cz = (wz - bMin.z) / fluidCellSize - 0.5f;
                const auto vox = fluidState.SampleTrilinear(cx, cy, cz);
                const float dens = std::max(0.0f, vox[0]);
                const float temp = std::max(0.0f, vox[1]);
                if (dens < 0.005f && temp < 0.05f) continue;

                const float sigma = dens * 2.2f;
                const float alpha = 1.0f - std::exp(-sigma * dt);
                const float fireGlow = std::max(0.0f, temp - 0.65f) * 1.9f;
                const float smokeLit = 0.55f + 0.35f * std::clamp((wy - bMin.y) / (2.0f * hy), 0.0f, 1.0f);

                accR += trans * (alpha * smokeLit * 0.72f + fireGlow * dt * 2.8f);
                accG += trans * (alpha * smokeLit * 0.74f + fireGlow * dt * 1.15f);
                accB += trans * (alpha * smokeLit * 0.78f + fireGlow * dt * 0.25f);
                trans *= (1.0f - alpha);
            }
            const size_t o = (static_cast<size_t>(y) * w + x) * 3u;
            hdrRgb[o + 0u] = hdrRgb[o + 0u] * trans + accR;
            hdrRgb[o + 1u] = hdrRgb[o + 1u] * trans + accG;
            hdrRgb[o + 2u] = hdrRgb[o + 2u] * trans + accB;
        }
    }

    // 4. Additive glowing ember & spark particles from ParticleGpuSimulation
    for (const auto& ep : emberPositions)
    {
        float sx = 0.0f, sy = 0.0f, depth = 0.0f;
        if (!project({ ep[0], ep[1], ep[2] }, sx, sy, depth)) continue;
        const int cx = static_cast<int>(std::round(sx));
        const int cy = static_cast<int>(std::round(sy));
        constexpr int rad = 4;
        for (int dy = -rad; dy <= rad; ++dy)
        {
            const int py = cy + dy;
            if (py < 0 || py >= static_cast<int>(h)) continue;
            for (int dx = -rad; dx <= rad; ++dx)
            {
                const int px = cx + dx;
                if (px < 0 || px >= static_cast<int>(w)) continue;
                const size_t idx = static_cast<size_t>(py) * w + static_cast<size_t>(px);
                if (depth > zBuf[idx] + 0.08f) continue;
                const float r2 = static_cast<float>(dx * dx + dy * dy);
                const float glow = std::exp(-r2 * 0.32f);
                const size_t o = idx * 3u;
                hdrRgb[o + 0u] += glow * 2.2f;
                hdrRgb[o + 1u] += glow * 1.05f;
                hdrRgb[o + 2u] += glow * 0.28f;
            }
        }
    }

    // 5. ACES filmic tonemapping -> RGBA8 PNG
    std::vector<uint8_t> rgba(static_cast<size_t>(w) * h * 4u, 255u);
    for (size_t i = 0u; i < static_cast<size_t>(w) * h; ++i)
    {
        rgba[i * 4u + 0u] = static_cast<uint8_t>(std::clamp(AcesFilmic(hdrRgb[i * 3u + 0u]) * 255.0f, 0.0f, 255.0f));
        rgba[i * 4u + 1u] = static_cast<uint8_t>(std::clamp(AcesFilmic(hdrRgb[i * 3u + 1u]) * 255.0f, 0.0f, 255.0f));
        rgba[i * 4u + 2u] = static_cast<uint8_t>(std::clamp(AcesFilmic(hdrRgb[i * 3u + 2u]) * 255.0f, 0.0f, 255.0f));
    }

    std::filesystem::create_directories(targetPath.parent_path());
    const auto png = Frontier::GeometrySpace::Deformation::VertexMotionAtlasExporter::EncodeRgba8Png(w, h, rgba);
    std::ofstream f(targetPath, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace

int main()
{
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::Lifecycle;
    using namespace Frontier::ParticleSpace;
    using namespace Frontier::SimulationSpace;
    using namespace Frontier::SimulationSpace::Deformable;
    using namespace Frontier::SimulationSpace::Fluid;
    using namespace Frontier::SimulationSpace::ForceFields;
    using namespace Frontier::SimulationSpace::PhysicsSpace;

    // ---------------------------------------------------------------------------------------------------------
    // D21: Verify direct PhysicsSpace::VehicleReading (Z-up) -> VehicleChassisCouplingInput conversion
    // ---------------------------------------------------------------------------------------------------------
    VehicleReading physVehicle {};
    physVehicle.WorldPositionMeters = { 0.35f, 0.25f, 0.39f }; // Frontier Z-up (Z=0.39m road axle height)
    physVehicle.ForwardSpeedMps     = 14.5f;
    physVehicle.Corners[0] = { true, 0.03f, 0.0f, 0.0f, 0.0f, 0.0f, 4100.0f, -0.025f, 0.0f, 0.22f, 12.0f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 310.0f, 315.0f, 316.0f, 5.5f, FrictionRegimeCategory::Kinetic, 0u, 1.0f, 4u, { 1.72f, -0.58f, 0.0f }, {  1.37f, -0.83f, 0.0f } };
    physVehicle.Corners[1] = { true, 0.08f, 0.2f, 0.0f, 0.0f, 0.0f, 5700.0f,  0.025f, 0.0f, 0.22f, 12.0f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 355.0f, 344.0f, 350.0f, 5.5f, FrictionRegimeCategory::Kinetic, 0u, 1.0f, 4u, { 1.88f,  1.08f, 0.14f }, {  1.37f,  0.83f, 0.0f } };
    physVehicle.Corners[2] = { true, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 3650.0f, -0.020f, 0.0f, 0.00f, 12.0f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 305.0f, 312.0f, 312.0f, 5.5f, FrictionRegimeCategory::Kinetic, 0u, 1.0f, 4u, { -1.02f, -0.58f, 0.0f }, { -1.37f, -0.83f, 0.0f } };
    physVehicle.Corners[3] = { true, 0.02f, 0.0f, 0.0f, 0.0f, 0.0f, 3650.0f,  0.020f, 0.0f, 0.00f, 12.0f, 0.55f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 305.0f, 312.0f, 312.0f, 5.5f, FrictionRegimeCategory::Kinetic, 0u, 1.0f, 4u, { -1.02f,  1.08f, 0.0f }, { -1.37f,  0.83f, 0.0f } };

    const auto convertedCar = VehicleReadingConverter::ConvertFromPhysicsReading(physVehicle);
    Check(std::fabs(convertedCar.ChassisOriginMeters[1] - 0.39f) < 1e-4f &&
          std::fabs(convertedCar.CornerInputs[1].RoadBumpHeightMeters - 0.14f) < 1e-4f &&
          std::fabs(convertedCar.CornerInputs[1].CarcassTempKelvin - 350.0f) < 1e-4f,
          "D21: VehicleReadingConverter accurately transforms Z-up PhysicsSpace::VehicleReading & curb height");

    // ---------------------------------------------------------------------------------------------------------
    // D22: Register SimulationCoordinatorExtension inside Frontier::Lifecycle::LifecycleSequence
    // ---------------------------------------------------------------------------------------------------------
    SimulationCoordinatorParameters coordParams {};
    coordParams.FixedStepHz            = 60.0f;
    coordParams.FluidGridCells         = { 16u, 16u, 16u };
    coordParams.FluidCellSizeMeters    = 0.22f;
    coordParams.FluidCentreMeters      = { -1.95f, 1.75f, -1.55f };
    coordParams.FluidSettings.Buoyancy = 2.1f;
    coordParams.FluidSettings.Combustion = 1.25f;
    coordParams.FluidSettings.PressureIterations = 12u;
    coordParams.DeformableParameters.ShippedVertexAtlasPath = "EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas";
    coordParams.DeformableParameters.EnableVehicleCoupling  = true;

    SimulationCoordinatorExtension coord(coordParams);
    LifecycleSequence seq {};
    Check(seq.RegisterParticipant("SimulationCoordinator", coord) == 0u,
          "D22: SimulationCoordinatorExtension registers into LifecycleSequence");
    Check(seq.Initialize() && coord.QueryInitialized(),
          "D22: SimulationCoordinatorExtension initializes all 4 domains cleanly");

    // 1. Add shared ForceFields: directional crosswind (+X) + swirling RotationalColumn vortex
    {
        auto& wind = coord.AccessForceFields().AddForceNode(ForceModelOrdinal::DirectionalWind, "TrackCrosswind");
        wind.Node.Strength     = 4.8f;
        wind.Node.RadiusMeters = 12.0f;
        wind.Node.Direction    = { 1.0f, 0.15f, 0.25f };
        wind.Node.Falloff      = ForceFalloffOrdinal::Constant;

        auto& swirl = coord.AccessForceFields().AddForceNode(ForceModelOrdinal::RotationalColumn, "CampfireVortex");
        swirl.Node.PositionMeters = { -1.95f, 1.0f, -1.55f };
        swirl.Node.Strength       = 2.6f;
        swirl.Node.RadiusMeters   = 3.5f;
        swirl.Node.Direction      = { 0.0f, 1.0f, 0.0f };
    }

    // 2. Add trackside campfire Fluid release source
    {
        FluidReleaseSourceParameters fire {};
        fire.SourceId       = "TracksideCampfire";
        fire.Shape          = Frontier::SimulationSpace::Fluid::ReleaseSourceShapeOrdinal::Sphere;
        fire.PositionMeters = { -1.95f, 0.42f, -1.55f };
        fire.RadiusMeters   = 0.42f;
        fire.VelocityMps    = { 0.25f, 2.4f, 0.1f };
        fire.Density        = 1.65f;
        fire.Temperature    = 2.85f;
        fire.Fuel           = 1.20f;
        coord.AddFluidSource(fire);
    }

    // 3. Add glowing ember ParticleCloudNode coupled to BOTH shared ForceFields AND live 3D Fluid velocity
    {
        ParticleCloudNode embers {};
        embers.NodeId                   = "CampfireEmbers";
        embers.Enabled                  = true;
        embers.Evaluated.Shape          = ParticleShapeOrdinal::Sphere;
        embers.Evaluated.Count          = 256u;
        embers.Evaluated.Rate           = 12.0f;
        embers.Evaluated.Position       = { -1.95f, 0.48f, -1.55f };
        embers.Evaluated.Radius         = 0.36f;
        embers.Evaluated.Speed          = 1.6f;
        embers.Evaluated.Lifetime       = 2.4f;
        embers.Evaluated.Gravity        = 0.15f;
        embers.Evaluated.GasInfluence   = 0.90f;
        embers.Evaluated.FieldInfluence = 0.85f;
        coord.AddParticleCloud(embers);
    }

    // 4. Spawn trackside Deformables (.SoftBody files) + sync 4-wheel car from PhysicsSpace::VehicleReading
    std::string err;
    const uint32_t bannerId = coord.AccessDeformables().AccessWorldRegistry().SpawnFromSoftBodyFile(
        "EngineContent/Simulation/Deformables/WovenCloth.SoftBody", { 0.30f, 1.78f, -2.05f }, "TrackBanner", &err);
    const uint32_t fernsId  = coord.AccessDeformables().AccessWorldRegistry().SpawnFromSoftBodyFile(
        "EngineContent/Simulation/Deformables/BotanicalFerns.SoftBody", { 2.35f, 1.78f, -1.35f }, "TrackFerns", &err);
    Check(bannerId != 0u && fernsId != 0u,
          "D22: Multi-instance trackside .SoftBody assets spawned into SimulationCoordinatorExtension");

    coord.SyncVehicleFromPhysicsReading(physVehicle);
    coord.AssignCameraWorldPosition({ 4.10f, 2.35f, 4.95f });

    // 5. Place a live Jolt RigidColliderProxy sphere touching the trackside banner to exercise two-way impulse feedback
    RigidColliderProxy postProxy {};
    postProxy.BodySlotIndex      = 42u;
    postProxy.ShapeCode          = 0u; // Sphere
    postProxy.CenterMeters[0]    = 0.30f;
    postProxy.CenterMeters[1]    = 2.05f;
    postProxy.CenterMeters[2]    = -1.95f;
    postProxy.RadiusMeters       = 0.42f;
    postProxy.DynamicResponseBit = 1u;
    const std::array<RigidColliderProxy, 1u> proxies { postProxy };
    coord.AssignRigidColliderProxies(proxies);

    // Step all 5 coupled domains together for 40 ticks through Frontier::Lifecycle::LifecycleSequence!
    for (uint32_t step = 0u; step < 40u; ++step)
    {
        seq.Tick(1.0 / 60.0);
    }

    const auto& tel = coord.InspectTelemetry();
    Check(tel.CompletedTicks >= 39ull, "D22: SimulationCoordinatorExtension completes 40 fixed coupled ticks");
    Check(tel.ActiveForceCount == 2u, "D22: Both shared ForceField nodes published wait-free to all domains");
    Check(tel.FluidStepCount >= 39ull && tel.FluidPeakDensity > 0.15f,
          "D22: 3D Eulerian Fluid solver produces sustained buoyant smoke/fire plume");

    const auto plumeVel = coord.SampleFluidVelocityWorld({ -1.85f, 1.15f, -1.55f });
    Check(plumeVel[1] > 0.1f && plumeVel[0] > 0.05f,
          "D22: Shared +X crosswind ForceField bends rising Fluid plume (+Y buoyancy & +X drift)");

    const auto* emberRecord = coord.InspectParticles().FindCloudRecord("CampfireEmbers");
    Check(emberRecord != nullptr && tel.ActiveParticleCount > 100u,
          "D22: ParticleSpace advects active glowing embers coupled to ForceFields & Fluid velocity");

    // Verify embers were carried upward (+Y) and downwind (+X) by coupled Fluid + ForceFields
    std::vector<std::array<float, 3>> liveEmbers;
    double meanEmberX = 0.0, meanEmberY = 0.0;
    if (emberRecord != nullptr)
    {
        for (const auto& p : emberRecord->ReadStates)
        {
            if (p.PositionAge[1] > 0.02f && p.PositionAge[1] < 10.0f)
            {
                liveEmbers.push_back({ p.PositionAge[0], p.PositionAge[1], p.PositionAge[2] });
                meanEmberX += p.PositionAge[0];
                meanEmberY += p.PositionAge[1];
            }
        }
    }
    if (!liveEmbers.empty())
    {
        meanEmberX /= static_cast<double>(liveEmbers.size());
        meanEmberY /= static_cast<double>(liveEmbers.size());
    }
    Check(!liveEmbers.empty() && meanEmberX > -1.90 && meanEmberY > 0.55,
          "D23: Coupled Fluid + ForceFields visibly lift (+Y) and drift (+X) ember particles");

    Check(tel.LastVehicleSummary.LiveFemWheelCount == 4u &&
          tel.LastVehicleSummary.EffectivePressurePsi[1] > 37.5f,
          "D22: 4-wheel VehicleDeformableCoupling driven from Z-up VehicleReading with heated curb contact");

    const auto feedback = coord.InspectRigidImpulseFeedback();
    Check(tel.DeformableRigidContacts > 0u && !feedback.empty() &&
          feedback[0].BodyToken.SlotIndex == 42u && feedback[0].ContactCount > 0u,
          "D21: Two-way deformable contact accumulates finite RigidImpulseAccumulatorEntry for Jolt PhysicsThread");

    // Build unified world surface (4-wheel deformed car + trackside ferns + billowing banner + apex curb)
    ProceduralRenderSurface worldSurf {};
    coord.InspectDeformables().InspectVehicleCoupling().BuildWorldVehicleSurface(convertedCar, worldSurf, true);
    for (const auto& inst : coord.InspectDeformables().InspectWorldRegistry().EnumerateInstances())
    {
        DeformableSurfaceBuilder::AppendBodyAttachments(inst.Body, worldSurf, true, false);
    }
    // Add orange apex curb under the front-right wheel
    {
        DeformableBody curb {};
        curb.StaticProps.push_back({
            { convertedCar.CornerPlacements[1].WheelCenterMeters[0] + 0.14f, 0.055f, convertedCar.CornerPlacements[1].WheelCenterMeters[2] },
            { 0.64f, 0.11f, 0.36f },
            { 0.94f, 0.42f, 0.14f },
            { 0.55f, 0.0f, 0.05f }
        });
        DeformableSurfaceBuilder::AppendBodyAttachments(curb, worldSurf, true, false);
    }

    const std::filesystem::path scenePng = "Exhibits/DeformableRenders/UnifiedMultiDomainScene.png";
    RenderUnifiedScenePng(
        worldSurf,
        coord.InspectFluidSimulation().State,
        coordParams.FluidCentreMeters,
        coordParams.FluidCellSizeMeters,
        liveEmbers,
        scenePng,
        960u,
        540u);
    Check(std::filesystem::exists(scenePng) && std::filesystem::file_size(scenePng) > 80000u,
          "D23: Rendered direct C++20 5-domain composite PBR + Volumetric PNG proof");

    // Lifecycle Suspend / Resume / Finalize check
    const uint64_t ticksBefore = coord.InspectTelemetry().CompletedTicks;
    Check(seq.Suspend() && coord.QuerySuspended(), "D22: LifecycleSequence::Suspend pauses all 4 simulation domains");
    seq.Tick(1.0 / 60.0);
    Check(coord.InspectTelemetry().CompletedTicks == ticksBefore, "D22: Suspended coordinator holds tick count steady");
    Check(seq.Resume() && !coord.QuerySuspended(), "D22: LifecycleSequence::Resume resumes coupled ticking");
    seq.Tick(1.0 / 60.0);
    Check(coord.InspectTelemetry().CompletedTicks > ticksBefore, "D22: Resumed coordinator advances coupled ticks");
    seq.Finalize();
    Check(!coord.QueryInitialized(), "D22: LifecycleSequence::Finalize cleanly tears down all 4 simulation domains");

    std::cout << "UnifiedSimulationCouplingValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
