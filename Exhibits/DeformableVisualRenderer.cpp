/*============================================================================================================================================
                                                    DEFORMABLEVISUALRENDERER.CPP
============================================================================================================================================*/
// 🧩 Phase D16 Direct C++20 software PBR triangle rasterizer rendering ALL 9 .SoftBody presets directly from disk
//    into individual PNGs and a 3x3 contact sheet (Exhibits/DeformableRenders/ContactSheet_All9SoftBodyPresets.png).

#include "Engine/Codecs/Simulation/SoftBodyCodec.h"
#include "Engine/GeometrySpace/Deformation/DeformableSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace
{

struct Vec3 { float x = 0.0f, y = 0.0f, z = 0.0f; };

Vec3 Sub(Vec3 a, Vec3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3 Cross(Vec3 a, Vec3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
Vec3 Normalize(Vec3 a)
{
    const float l = std::sqrt(Dot(a, a));
    return (l > 1.0e-6f) ? Vec3 { a.x / l, a.y / l, a.z / l } : Vec3 { 0.0f, 1.0f, 0.0f };
}

float AcesFilmicCurve(float x)
{
    x = std::max(0.0f, x);
    const float num = x * (2.51f * x + 0.03f);
    const float den = x * (2.43f * x + 0.59f) + 0.14f;
    return std::pow(std::clamp(num / den, 0.0f, 1.0f), 1.0f / 2.2f);
}

struct Camera
{
    Vec3 eye, right, up, forward;
    float focal;
};

Camera BuildCamera(Vec3 eye, Vec3 target, float fovDeg, uint32_t width, uint32_t height)
{
    Camera c {};
    c.eye     = eye;
    c.forward = Normalize(Sub(target, eye));
    c.right   = Normalize(Cross(c.forward, { 0.0f, 1.0f, 0.0f }));
    c.up      = Cross(c.right, c.forward);
    const float fovRad = fovDeg * 3.14159265f / 180.0f;
    c.focal = (0.5f * static_cast<float>(height)) / std::tan(0.5f * fovRad);
    (void)width;
    return c;
}

bool Project(const Camera& c, Vec3 p, uint32_t w, uint32_t h, float& sx, float& sy, float& depth)
{
    const Vec3 d = Sub(p, c.eye);
    depth = Dot(d, c.forward);
    if (depth < 0.15f) return false;
    const float rx = Dot(d, c.right);
    const float ry = Dot(d, c.up);
    sx = 0.5f * static_cast<float>(w) + (rx / depth) * c.focal;
    sy = 0.5f * static_cast<float>(h) - (ry / depth) * c.focal;
    return true;
}

void RenderSurfaceTile(
    const Frontier::GeometrySpace::Deformation::ProceduralRenderSurface& surf,
    const Camera& cam,
    float groundY,
    bool underwater,
    uint32_t w,
    uint32_t h,
    std::vector<uint8_t>& outRgba)
{
    outRgba.assign(static_cast<size_t>(w) * h * 4u, 255u);
    std::vector<float> zBuf(static_cast<size_t>(w) * h, 1.0e9f);

    // Studio sky gradient + procedural ground grid plane
    for (uint32_t y = 0u; y < h; ++y)
    {
        const float vy = static_cast<float>(y) / static_cast<float>(h);
        const float ny = (0.5f * static_cast<float>(h) - static_cast<float>(y)) / cam.focal;
        for (uint32_t x = 0u; x < w; ++x)
        {
            const float nx = (static_cast<float>(x) - 0.5f * static_cast<float>(w)) / cam.focal;
            const Vec3 ray = Normalize({
                cam.forward.x + cam.right.x * nx + cam.up.x * ny,
                cam.forward.y + cam.right.y * nx + cam.up.y * ny,
                cam.forward.z + cam.right.z * nx + cam.up.z * ny
            });

            float r = underwater ? (0.04f + 0.08f * (1.0f - vy)) : (0.08f + 0.14f * (1.0f - vy));
            float g = underwater ? (0.14f + 0.22f * (1.0f - vy)) : (0.10f + 0.16f * (1.0f - vy));
            float b = underwater ? (0.20f + 0.28f * (1.0f - vy)) : (0.14f + 0.22f * (1.0f - vy));

            if (ray.y < -1.0e-4f)
            {
                const float t = (groundY - cam.eye.y) / ray.y;
                if (t > 0.1f && t < 30.0f)
                {
                    const float gx = cam.eye.x + ray.x * t;
                    const float gz = cam.eye.z + ray.z * t;
                    const float dist = std::hypot(gx, gz);
                    const float fade = std::exp(-dist * dist * 0.025f);
                    const float cell = (std::fabs(gx - std::round(gx)) < 0.025f || std::fabs(gz - std::round(gz)) < 0.025f) ? 1.0f : 0.0f;
                    const float floorTint = underwater ? 0.08f : 0.13f;
                    const float gridGlow  = cell * (underwater ? 0.14f : 0.18f);
                    r = r * (1.0f - fade) + (floorTint + gridGlow) * fade;
                    g = g * (1.0f - fade) + (floorTint * (underwater ? 1.5f : 1.05f) + gridGlow) * fade;
                    b = b * (1.0f - fade) + (floorTint * (underwater ? 1.8f : 1.15f) + gridGlow * 1.15f) * fade;
                    zBuf[static_cast<size_t>(y) * w + x] = t;
                }
            }

            const size_t px = (static_cast<size_t>(y) * w + x) * 4u;
            outRgba[px + 0u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(r) * 255.0f, 0.0f, 255.0f));
            outRgba[px + 1u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(g) * 255.0f, 0.0f, 255.0f));
            outRgba[px + 2u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(b) * 255.0f, 0.0f, 255.0f));
            outRgba[px + 3u] = 255u;
        }
    }

    const Vec3 keyLight  = Normalize({ 0.58f, 0.72f, 0.38f });
    const Vec3 fillLight = Normalize({ -0.65f, 0.35f, -0.55f });

    const uint32_t triCount = surf.TriangleCount();
    for (uint32_t ti = 0u; ti < triCount; ++ti)
    {
        const uint32_t i0 = surf.Indices[static_cast<size_t>(ti) * 3u + 0u];
        const uint32_t i1 = surf.Indices[static_cast<size_t>(ti) * 3u + 1u];
        const uint32_t i2 = surf.Indices[static_cast<size_t>(ti) * 3u + 2u];

        const Vec3 p0 { surf.Positions[i0 * 3u], surf.Positions[i0 * 3u + 1u], surf.Positions[i0 * 3u + 2u] };
        const Vec3 p1 { surf.Positions[i1 * 3u], surf.Positions[i1 * 3u + 1u], surf.Positions[i1 * 3u + 2u] };
        const Vec3 p2 { surf.Positions[i2 * 3u], surf.Positions[i2 * 3u + 1u], surf.Positions[i2 * 3u + 2u] };

        float x0, y0, z0, x1, y1, z1, x2, y2, z2;
        if (!Project(cam, p0, w, h, x0, y0, z0) ||
            !Project(cam, p1, w, h, x1, y1, z1) ||
            !Project(cam, p2, w, h, x2, y2, z2))
        {
            continue;
        }

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
                const Vec3 worldP {
                    p0.x * w0 + p1.x * w1 + p2.x * w2,
                    p0.y * w0 + p1.y * w1 + p2.y * w2,
                    p0.z * w0 + p1.z * w1 + p2.z * w2
                };
                const Vec3 V = Normalize(Sub(cam.eye, worldP));
                if (Dot(N, V) < 0.0f) N = { -N.x, -N.y, -N.z }; // two-sided cloth/foliage shading

                const float ar = surf.AlbedoRgb[i0 * 3u] * w0 + surf.AlbedoRgb[i1 * 3u] * w1 + surf.AlbedoRgb[i2 * 3u] * w2;
                const float ag = surf.AlbedoRgb[i0 * 3u + 1u] * w0 + surf.AlbedoRgb[i1 * 3u + 1u] * w1 + surf.AlbedoRgb[i2 * 3u + 1u] * w2;
                const float ab = surf.AlbedoRgb[i0 * 3u + 2u] * w0 + surf.AlbedoRgb[i1 * 3u + 2u] * w1 + surf.AlbedoRgb[i2 * 3u + 2u] * w2;

                const float rough = std::clamp(surf.MaterialPbr[i0 * 3u] * w0 + surf.MaterialPbr[i1 * 3u] * w1 + surf.MaterialPbr[i2 * 3u] * w2, 0.08f, 0.95f);
                const float sss   = surf.MaterialPbr[i0 * 3u + 1u] * w0 + surf.MaterialPbr[i1 * 3u + 1u] * w1 + surf.MaterialPbr[i2 * 3u + 1u] * w2;
                const float metal = surf.MaterialPbr[i0 * 3u + 2u] * w0 + surf.MaterialPbr[i1 * 3u + 2u] * w1 + surf.MaterialPbr[i2 * 3u + 2u] * w2;

                const float ndl  = std::max(0.0f, Dot(N, keyLight));
                const float fill = std::max(0.0f, Dot(N, fillLight));
                const Vec3  H    = Normalize({ keyLight.x + V.x, keyLight.y + V.y, keyLight.z + V.z });
                const float specExp = 12.0f + (1.0f - rough) * 90.0f;
                const float spec    = std::pow(std::max(0.0f, Dot(N, H)), specExp) * (0.12f + 0.65f * metal + 0.25f * (1.0f - rough));
                const float rim     = std::pow(1.0f - std::max(0.0f, Dot(N, V)), 3.2f) * (0.18f + 0.35f * sss);
                const float hemi    = 0.32f + 0.26f * (0.5f + 0.5f * N.y);

                const float diffScale = (1.0f - 0.55f * metal);
                const float rCol = ar * (hemi + ndl * 1.45f * diffScale + fill * 0.40f + sss * 0.22f) + spec * (0.55f + 0.45f * ar) + rim * 0.32f;
                const float gCol = ag * (hemi + ndl * 1.42f * diffScale + fill * 0.42f + sss * 0.26f) + spec * (0.55f + 0.45f * ag) + rim * 0.36f;
                const float bCol = ab * (hemi + ndl * 1.38f * diffScale + fill * 0.48f + sss * 0.20f) + spec * (0.58f + 0.42f * ab) + rim * 0.42f;

                const size_t o = idx * 4u;
                outRgba[o + 0u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(rCol) * 255.0f, 0.0f, 255.0f));
                outRgba[o + 1u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(gCol) * 255.0f, 0.0f, 255.0f));
                outRgba[o + 2u] = static_cast<uint8_t>(std::clamp(AcesFilmicCurve(bCol) * 255.0f, 0.0f, 255.0f));
                outRgba[o + 3u] = 255u;
            }
        }
    }
}

void WritePngFile(const std::filesystem::path& path, uint32_t w, uint32_t h, const std::vector<uint8_t>& rgba)
{
    const auto png = Frontier::GeometrySpace::Deformation::VertexMotionAtlasExporter::EncodeRgba8Png(w, h, rgba);
    std::ofstream f(path, std::ios::binary);
    f.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
}

} // namespace

int main()
{
    using namespace Frontier::Codecs::Simulation;
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::SimulationSpace::Deformable;

    namespace fs = std::filesystem;
    const fs::path outDir = "Exhibits/DeformableRenders";
    fs::create_directories(outDir);

    constexpr uint32_t tileW = 480u;
    constexpr uint32_t tileH = 360u;
    constexpr uint32_t sheetW = tileW * 3u;
    constexpr uint32_t sheetH = tileH * 3u;
    std::vector<uint8_t> contactSheet(static_cast<size_t>(sheetW) * sheetH * 4u, 255u);

    const auto presets = DeformablePresetRegistry::EnumeratePresets();
    for (size_t idx = 0u; idx < presets.size() && idx < 9u; ++idx)
    {
        const auto& desc = presets[idx];
        const fs::path sbPath = fs::path("EngineContent/Simulation/Deformables") / (std::string(desc.FileStem) + ".SoftBody");

        SoftBodyDocumentModel doc {};
        std::string err;
        if (!SoftBodyCodec::DecodeFromFile(sbPath, doc, &err))
        {
            std::cerr << "Failed to decode " << sbPath << ": " << err << "\n";
            return 1;
        }

        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(doc.PresetKind, body, cfg);
        cfg = doc.Configuration;

        // Embed sculpted sport tire mesh at rest pose BEFORE stepping
        HighPolySurfaceRest tireRest {};
        CageEmbeddedSurface tireEmb {};
        if (doc.PresetKind == DeformablePresetKind::SportTireFem)
        {
            tireRest = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);
            tireEmb  = TetrahedralCageEmbedder::EmbedIntoFemSolid(body, tireRest);
        }
        else if (doc.PresetKind == DeformablePresetKind::SportTirePneumatic)
        {
            tireRest = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.14f, 0.42f);
            tireEmb  = SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(body, tireRest);
        }

        // Place curb beneath tire presets for dramatic contact bulge in the still frame
        if (doc.PresetKind == DeformablePresetKind::SportTireFem || doc.PresetKind == DeformablePresetKind::SportTirePneumatic)
        {
            body.Collider.Active = true;
            body.Collider.CenterMeters = { 0.35f, body.GroundY + 0.14f, 0.0f };
        }

        for (uint32_t step = 0u; step < 90u; ++step)
        {
            XpbdConstraintSolver::Step(body, 1.0f / 60.0f, cfg, static_cast<float>(step) / 60.0f, 0.0f);
        }

        ProceduralRenderSurface surf {};
        if (doc.PresetKind == DeformablePresetKind::SportTireFem)
        {
            DeformedSurfaceSample live {};
            TetrahedralCageEmbedder::EvaluateLiveSurface(body, tireEmb, live);
            DeformableSurfaceBuilder::AppendSkinnedSurface(tireRest, live, surf);
        }
        else if (doc.PresetKind == DeformablePresetKind::SportTirePneumatic)
        {
            DeformedSurfaceSample live {};
            SurfaceQuadCageEmbedder::EvaluateLiveSurface(body, tireEmb, live);
            DeformableSurfaceBuilder::AppendSkinnedSurface(tireRest, live, surf);
        }
        DeformableSurfaceBuilder::AppendBodyAttachments(body, surf, true, true);

        // Compute tight bounding box for well-framed studio camera per preset
        float minX = 1e9f, minY = 1e9f, minZ = 1e9f;
        float maxX = -1e9f, maxY = -1e9f, maxZ = -1e9f;
        for (uint32_t v = 0u; v < surf.VertexCount(); ++v)
        {
            minX = std::min(minX, surf.Positions[v * 3u]);
            minY = std::min(minY, surf.Positions[v * 3u + 1u]);
            minZ = std::min(minZ, surf.Positions[v * 3u + 2u]);
            maxX = std::max(maxX, surf.Positions[v * 3u]);
            maxY = std::max(maxY, surf.Positions[v * 3u + 1u]);
            maxZ = std::max(maxZ, surf.Positions[v * 3u + 2u]);
        }
        const Vec3 center { 0.5f * (minX + maxX), 0.5f * (minY + maxY), 0.5f * (minZ + maxZ) };
        const float radius = std::max(1.2f, 0.5f * std::sqrt((maxX - minX) * (maxX - minX) + (maxY - minY) * (maxY - minY) + (maxZ - minZ) * (maxZ - minZ)));
        const float dist = radius * 2.15f;
        const Camera cam = BuildCamera(
            { center.x + dist * 0.48f, center.y + dist * 0.28f, center.z + dist * 0.88f },
            center, 40.0f, tileW, tileH);

        std::vector<uint8_t> tileRgba;
        RenderSurfaceTile(surf, cam, body.GroundY, cfg.UnderwaterEnabled, tileW, tileH, tileRgba);

        const fs::path tilePath = outDir / (std::string(desc.FileStem) + ".png");
        WritePngFile(tilePath, tileW, tileH, tileRgba);

        // Blit into 3x3 contact sheet
        const uint32_t col = static_cast<uint32_t>(idx % 3u);
        const uint32_t row = static_cast<uint32_t>(idx / 3u);
        for (uint32_t y = 0u; y < tileH; ++y)
        {
            for (uint32_t x = 0u; x < tileW; ++x)
            {
                const size_t srcPx = (static_cast<size_t>(y) * tileW + x) * 4u;
                const size_t dstPx = (static_cast<size_t>(row * tileH + y) * sheetW + (col * tileW + x)) * 4u;
                contactSheet[dstPx + 0u] = tileRgba[srcPx + 0u];
                contactSheet[dstPx + 1u] = tileRgba[srcPx + 1u];
                contactSheet[dstPx + 2u] = tileRgba[srcPx + 2u];
                contactSheet[dstPx + 3u] = 255u;
            }
        }
        std::cout << "Rendered " << desc.FileStem << " (" << surf.VertexCount() << " verts, " << surf.TriangleCount() << " tris)\n";
    }

    const fs::path sheetPath = outDir / "ContactSheet_All9SoftBodyPresets.png";
    WritePngFile(sheetPath, sheetW, sheetH, contactSheet);
    std::cout << "Wrote 3x3 contact sheet: " << sheetPath.string() << "\n";
    return 0;
}
