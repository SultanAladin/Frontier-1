/*============================================================================================================================================
                                                      VERTEXATLASBAKETOOL.CPP
============================================================================================================================================*/
// 🧩 Phase D17 Pure C++20 offline Vertex Motion Atlas baker generating shipped binary .VertexAtlas files
//    under EngineContent/Simulation/Deformables/{SportTireFem,SportTirePneumatic}.VertexAtlas (FNV-1a verified).

#include "Engine/Codecs/Simulation/VertexAtlasCodec.h"
#include "Engine/GeometrySpace/Deformation/SurfaceQuadCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TetrahedralCageEmbedder.h"
#include "Engine/GeometrySpace/Deformation/TireSurfaceBuilder.h"
#include "Engine/GeometrySpace/Deformation/VertexMotionAtlasBaker.h"
#include "Engine/SimulationSpace/Deformable/DeformablePresetRegistry.h"

#include <filesystem>
#include <iostream>
#include <string>

int main()
{
    using namespace Frontier::Codecs::Simulation;
    using namespace Frontier::GeometrySpace::Deformation;
    using namespace Frontier::SimulationSpace::Deformable;

    namespace fs = std::filesystem;
    const fs::path outDir = "EngineContent/Simulation/Deformables";
    fs::create_directories(outDir);

    struct BakeJob
    {
        DeformablePresetKind Preset;
        const char*          Title;
        const char*          FileName;
        float                OuterRadius;
        float                HalfWidth;
        bool                 IsFem;
    };

    const BakeJob jobs[2] = {
        { DeformablePresetKind::SportTireFem,       "SportTireFemVertexAtlas",       "SportTireFem.VertexAtlas",       1.15f, 0.43f, true  },
        { DeformablePresetKind::SportTirePneumatic, "SportTirePneumaticVertexAtlas", "SportTirePneumatic.VertexAtlas", 1.14f, 0.42f, false }
    };

    for (const BakeJob& job : jobs)
    {
        DeformableBody body {};
        DeformableSimulationConfiguration cfg {};
        DeformablePresetRegistry::BuildPreset(job.Preset, body, cfg);

        const HighPolySurfaceRest rest = TireSurfaceBuilder::ConstructSportTireSurface(
            0.0f, -0.62f, 0.0f, 0.66f, job.OuterRadius, job.HalfWidth);

        const CageEmbeddedSurface embedded = job.IsFem
            ? TetrahedralCageEmbedder::EmbedIntoFemSolid(body, rest)
            : SurfaceQuadCageEmbedder::EmbedIntoPneumaticCarcass(body, rest);

        VertexMotionAtlasBakeSettings settings {};
        settings.SliceCount       = 16u;
        settings.SimStepsPerSlice = 3u;
        settings.StepDeltaSeconds = 1.0f / 60.0f;

        VertexAtlasDocumentModel doc {};
        doc.DocumentTitle = job.Title;
        doc.Atlas = VertexMotionAtlasBaker::BakeFromDeformable(body, cfg, embedded, settings);

        const fs::path targetPath = outDir / job.FileName;
        std::string err;
        if (!VertexAtlasCodec::EncodeToFile(targetPath, doc, &err))
        {
            std::cerr << "[FAIL] Could not encode " << targetPath << ": " << err << "\n";
            return 1;
        }

        // Immediate FNV-1a round-trip verification
        VertexAtlasDocumentModel verifyDoc {};
        if (!VertexAtlasCodec::DecodeFromFile(targetPath, verifyDoc, &err) || !verifyDoc.Atlas.Ready())
        {
            std::cerr << "[FAIL] Verification decode failed for " << targetPath << ": " << err << "\n";
            return 1;
        }

        const uintmax_t bytes = fs::file_size(targetPath);
        std::cout << "Baked & verified " << targetPath.string()
                  << " (" << verifyDoc.Atlas.VertexCount << " verts x "
                  << verifyDoc.Atlas.SliceCount << " slices, "
                  << verifyDoc.Atlas.AtlasWidth << "x" << verifyDoc.Atlas.AtlasHeight
                  << " RGBA32F, " << (bytes / 1024u) << " KiB)\n";
    }

    return 0;
}
