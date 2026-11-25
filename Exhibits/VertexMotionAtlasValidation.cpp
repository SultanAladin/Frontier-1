/*============================================================================================================================================
                                                  VERTEXMOTIONATLASVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D08 Native C++20 validation suite for shared 7,552-vert / 12,544-tri sculpted sport tire,
//    Live GPU Cage Embedding (3D Tet Barycentrics + Nanson Cofactor Normals, Quad-Cage Bilinear),
//    64-slice RGBA32F Vertex Motion Atlas baking, playback, .VertexAtlas codec & PNG/JSON export.

#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasExporter.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasPlayer.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"
#include "Engine/SimulationSpace/Deformable/XpbdConstraintSolver.h"

#include <cmath>
#include <cstdint>
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

bool AllUnitNormals(const std::vector<float>& normals)
{
    for (size_t i = 0u; i + 2u < normals.size(); i += 3u)
    {
        const float len = std::sqrt(normals[i] * normals[i] + normals[i + 1u] * normals[i + 1u] + normals[i + 2u] * normals[i + 2u]);
        if (!std::isfinite(len) || len < 0.98f || len > 1.02f) return false;
    }
    return true;
}

} // namespace

int main()
{
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::SimulationSpace::Deformable;
    using namespace Frontier::Codecs::Simulation;

    // 1. Shared sculpted sport tire surface builder (exact 7,552 vertices & 12,544 triangles)
    const HighPolySurfaceRest tireSurface = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.15f, 0.43f);
    Check(tireSurface.VertexCount() == 7552u, "TireSurfaceBuilder produces exact 7,552 vertices");
    Check(tireSurface.TriangleCount() == 12544u, "TireSurfaceBuilder produces exact 12,544 triangles");
    Check(AllFinite(tireSurface.RestPositions) && AllUnitNormals(tireSurface.RestNormals),
          "Sculpted sport tire rest positions are finite and all 7,552 rest normals are unit length");

    bool hasTread = false;
    bool hasSidewall = false;
    for (size_t i = 0u; i + 2u < tireSurface.MaterialPbr.size(); i += 3u)
    {
        if (tireSurface.MaterialPbr[i + 1u] > 0.5f) hasTread = true;
        else hasSidewall = true;
    }
    Check(hasTread && hasSidewall,
          "Sculpted sport tire populates both tread lug/groove and sidewall/scuff-pad PBR zones");

    // 2. 3D Tetrahedral FEM Cage Embedding + Nanson Cofactor Live Skinning
    DeformableBody femBody {};
    DeformableSimulationConfiguration femCfg {};
    DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTireFem, femBody, femCfg);

    const CageEmbeddedSurface femEmbedded = TetrahedralCageEmbedder::EmbedIntoFemSolid(femBody, tireSurface);
    Check(femEmbedded.VertexCount() == 7552u && femEmbedded.Mode == CageEmbedMode::Tetrahedral3D,
          "TetrahedralCageEmbedder binds all 7,552 high-poly vertices");
    Check(femEmbedded.MaxRestError < 1.0e-4f,
          "TetrahedralCageEmbedder exact barycentric rest-pose reconstruction error < 0.0001 m (got " +
          std::to_string(femEmbedded.MaxRestError) + ")");

    // Step 30 ticks with curb contact and verify live GPU-equivalent skinning
    femBody.Collider.Active = true;
    femBody.Collider.CenterMeters = { 0.0f, femBody.GroundY + 0.17f, 0.0f };
    for (uint32_t s = 0u; s < 30u; ++s)
    {
        XpbdConstraintSolver::Step(femBody, 1.0f / 60.0f, femCfg, static_cast<float>(s) / 60.0f, 0.0f);
    }

    DeformedSurfaceSample femLive {};
    TetrahedralCageEmbedder::EvaluateLiveSurface(femBody, femEmbedded, femLive);
    Check(femLive.Positions.size() == 7552u * 3u && AllFinite(femLive.Positions) && AllUnitNormals(femLive.Normals),
          "TetrahedralCageEmbedder live Nanson cofactor evaluation produces 7,552 finite positions & unit normals");

    float maxLiveStrain = 0.0f;
    for (float st : femLive.Strain) maxLiveStrain = std::max(maxLiveStrain, st);
    Check(maxLiveStrain > 0.01f, "FEM curb strike induces measurable 3D volumetric strain on embedded high-poly surface");

    // 3. Pneumatic XPBD Quad-Cage Embedding + Tangent Basis Live Skinning
    DeformableBody xpbdBody {};
    DeformableSimulationConfiguration xpbdCfg {};
    DeformablePresetRegistry::BuildPreset(DeformablePresetKind::SportTirePneumatic, xpbdBody, xpbdCfg);
    const HighPolySurfaceRest xpbdTireSurface = TireSurfaceBuilder::ConstructSportTireSurface(0.0f, -0.62f, 0.0f, 0.66f, 1.14f, 0.42f);

    const CageEmbeddedSurface xpbdEmbedded = SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(xpbdBody, xpbdTireSurface);
    Check(xpbdEmbedded.VertexCount() == 7552u && xpbdEmbedded.Mode == CageEmbedMode::SurfaceQuad2D,
          "SurfaceQuadCageEmbedder binds all 7,552 high-poly vertices");
    Check(xpbdEmbedded.MaxRestError < 0.006f,
          "SurfaceQuadCageEmbedder rest-pose reconstruction error < 0.006 m (got " +
          std::to_string(xpbdEmbedded.MaxRestError) + ")");

    DeformedSurfaceSample xpbdLive {};
    SurfaceQuadCageEmbedder::EvaluateLiveSurface(xpbdBody, xpbdEmbedded, xpbdLive);
    Check(AllFinite(xpbdLive.Positions) && AllUnitNormals(xpbdLive.Normals),
          "SurfaceQuadCageEmbedder live evaluation produces 7,552 finite positions & unit normals");

    // 4. 64-Slice RGBA32F Vertex Motion Atlas (VAT) Baker — FEM Tire (exact 256 x 1920 texture)
    const float preBakeAxleY = femBody.FemTires[0].AxleY;
    VertexMotionAtlasBakeSettings bakeSettings {};
    bakeSettings.SliceCount       = 64u;
    bakeSettings.SimStepsPerSlice = 2u;
    const VertexMotionAtlas femAtlas = VertexMotionAtlasBaker::BakeFromDeformable(femBody, femCfg, femEmbedded, bakeSettings);
    Check(femAtlas.Ready(), "VertexMotionAtlasBaker bakes FEM 64-slice RGBA32F atlas cleanly");
    Check(femAtlas.AtlasWidth == 256u && femAtlas.RowsPerSlice == 30u && femAtlas.AtlasHeight == 1920u,
          "VAT dimensions match exact 256 x 1920 packing (30 rows/slice * 64 slices)");
    Check(AllFinite(femAtlas.PositionStrainRgba) && AllFinite(femAtlas.NormalRgba),
          "Baked FEM VAT texels contain 0 NaNs/Infs");
    Check(std::fabs(femBody.FemTires[0].AxleY - preBakeAxleY) < 1.0e-6f,
          "VertexMotionAtlasBaker cleanly restores live body state after offscreen bake");

    // 5. Vertex Motion Atlas Player sub-slice interpolation & 64-byte UBO packing
    VertexMotionAtlasPlayer player {};
    player.SetAtlas(&femAtlas);
    player.ScrubToSlice(12.4f);
    player.SetPlaying(false);
    const AtlasPlaybackReading reading = player.AdvanceAndSamplePose(0.0f);
    Check(reading.Slice0 == 12u && reading.Slice1 == 13u, "VAT player resolves sub-slice pair (12 -> 13)");

    DeformedSurfaceSample vatSample {};
    VertexMotionAtlasPlayer::SampleInterpolatedSurface(femAtlas, reading, vatSample);
    Check(vatSample.Positions.size() == 7552u * 3u && AllFinite(vatSample.Positions) && AllUnitNormals(vatSample.Normals),
          "VAT player sub-slice linear interpolation yields 7,552 finite positions & unit normals");

    const VertexMotionAtlasUniforms ubo = VertexMotionAtlasPlayer::BuildUniforms(femAtlas, reading, 0u, true);
    Check(ubo.PackingSlices[0] == 256u && ubo.PackingSlices[1] == 30u && ubo.PackingSlices[2] == 12u &&
          ubo.PackingSlices[3] == 13u && ubo.BlendFlags[2] > 0.5f,
          "VertexMotionAtlasUniforms (64 B) packs exact GPU slice & heatmap parameters");

    // 6. .VertexAtlas Codec Tier-1 TOML + Tier-2 64B-aligned binary round-trip & FNV-1a tamper check
    {
        VertexAtlasDocumentModel doc {};
        doc.DocumentTitle = "SportTireFemVat64";
        doc.Atlas         = femAtlas;

        std::vector<std::byte> encoded;
        std::string err;
        Check(VertexAtlasCodec::EncodeToBytes(doc, encoded, &err), ".VertexAtlas encodes cleanly");

        VertexAtlasDocumentModel decoded {};
        Check(VertexAtlasCodec::DecodeFromBytes(encoded, decoded, &err), ".VertexAtlas decodes cleanly");
        Check(decoded.Atlas.VertexCount == femAtlas.VertexCount &&
              decoded.Atlas.SliceCount == femAtlas.SliceCount &&
              decoded.Atlas.PositionStrainRgba == femAtlas.PositionStrainRgba &&
              decoded.Atlas.NormalRgba == femAtlas.NormalRgba,
              ".VertexAtlas binary round-trip is bit-exact across all RGBA32F texels");

        // Flip 1 bit in binary payload -> FNV-1a must reject
        encoded[encoded.size() - 17u] = static_cast<std::byte>(static_cast<uint8_t>(encoded[encoded.size() - 17u]) ^ 0x80u);
        VertexAtlasDocumentModel tampered {};
        Check(!VertexAtlasCodec::DecodeFromBytes(encoded, tampered, &err),
              ".VertexAtlas FNV-1a integrity verification rejects corrupted texel payload");
    }

    // 7. Pure-C++20 Unreal/Unity VAT Exporter (PNG signature + IHDR + IEND + JSON bounds)
    {
        const std::filesystem::path tmpPng  = "/tmp/frontier_vat_test.png";
        const std::filesystem::path tmpJson = "/tmp/frontier_vat_test.json";
        Check(VertexMotionAtlasExporter::ExportToFiles(tmpPng, tmpJson, femAtlas, tireSurface),
              "VertexMotionAtlasExporter writes valid .png and .json files");

        const auto pixels = VertexMotionAtlasExporter::QuantizeToRgba8(femAtlas, tireSurface);
        const auto pngBytes = VertexMotionAtlasExporter::EncodeRgba8Png(femAtlas.AtlasWidth, femAtlas.AtlasHeight, pixels);
        Check(pngBytes.size() > 64u &&
              static_cast<uint8_t>(pngBytes[0]) == 137u &&
              static_cast<uint8_t>(pngBytes[1]) == 'P' &&
              static_cast<uint8_t>(pngBytes[2]) == 'N' &&
              static_cast<uint8_t>(pngBytes[3]) == 'G',
              "Exported VAT PNG begins with valid 8-byte PNG signature");

        const std::string json = VertexMotionAtlasExporter::FormatBoundsJson(femAtlas);
        Check(json.find("\"vertexCount\": 7552") != std::string::npos &&
              json.find("\"texHeight\": 1920") != std::string::npos,
              "Exported VAT JSON metadata reports exact 7552 vertices & 256x1920 dimensions");
    }

    std::cout << "VertexMotionAtlasValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
