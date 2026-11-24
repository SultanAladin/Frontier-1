/*============================================================================================================================================
                                                    SIMULATIONSCENEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Phase D24-D26 Validation of unified 5-domain SimulationSceneDocument (.VolumetricSpace) round-trip,
//    one-call SimulationCoordinatorExtension mounting, and static CMake <-> .slang entry-point parity check.

#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/SimulationSceneDocument.h"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

std::string ReadWholeText(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main()
{
    using namespace Frontier::Lifecycle;
    using namespace Frontier::SimulationSpace;

    // ---------------------------------------------------------------------------------------------------------
    // D25: Encode shipped canonical 5-domain scene & verify byte-identical pure-TOML round-trip
    // ---------------------------------------------------------------------------------------------------------
    const std::filesystem::path scenePath = "EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace";
    const auto originalDoc = SimulationSceneCodec::BuildTracksideShowcaseDocument();

    std::string err;
    Check(SimulationSceneCodec::EncodeToFile(scenePath, originalDoc, &err),
          "D25: Encode canonical 5-domain scene to EngineContent/Simulation/Scenes/TracksideCoupledShowcase.VolumetricSpace");

    SimulationSceneDocument decodedDoc {};
    Check(SimulationSceneCodec::DecodeFromFile(scenePath, decodedDoc, &err),
          "D25: Decode TracksideCoupledShowcase.VolumetricSpace cleanly from disk");

    const std::string textA = SimulationSceneCodec::EncodeToTomlText(originalDoc);
    const std::string textB = SimulationSceneCodec::EncodeToTomlText(decodedDoc);
    Check(textA == textB, "D25: Pure-TOML multi-domain scene round-trip is 100% byte-identical");
    Check(decodedDoc.Forces.size() == 2u && decodedDoc.FluidSources.size() == 1u &&
          decodedDoc.ParticleClouds.size() == 1u && decodedDoc.SoftBodies.size() == 2u &&
          decodedDoc.RigidColliders.size() == 1u,
          "D25: Decoded document preserves all 5 domain sections accurately");

    // ---------------------------------------------------------------------------------------------------------
    // D25/D26: One-call MountIntoCoordinator & deterministic 25-tick coupled execution
    // ---------------------------------------------------------------------------------------------------------
    SimulationCoordinatorExtension coord(SimulationSceneCodec::BuildCoordinatorParameters(decodedDoc));
    LifecycleSequence seq {};
    Check(seq.RegisterParticipant("SceneCoordinator", coord) == 0u && seq.Initialize(),
          "D26: LifecycleSequence initializes coordinator built directly from .VolumetricSpace scene");
    Check(SimulationSceneCodec::MountIntoCoordinator(decodedDoc, coord, &err),
          "D26: SimulationSceneCodec::MountIntoCoordinator loads all 5 domains in one call");
    Check(coord.InspectDeformables().InspectTelemetry().HasShippedVertexAtlas,
          "D26: Mounted coordinator loads shipped SportTireFem.VertexAtlas referenced in scene");

    for (uint32_t step = 0u; step < 25u; ++step)
    {
        seq.Tick(1.0 / 60.0);
    }

    const auto& tel = coord.InspectTelemetry();
    Check(tel.CompletedTicks >= 24ull && tel.ActiveForceCount == 2u &&
          tel.FluidPeakDensity > 0.10f && tel.ActiveParticleCount > 100u &&
          tel.ActiveDeformableCount == 2u && tel.DeformableRigidContacts > 0u,
          "D26: Mounted .VolumetricSpace scene runs all 5 coupled domains cleanly with two-way Jolt contacts");
    seq.Finalize();

    // ---------------------------------------------------------------------------------------------------------
    // D24: Verify all 9 Deformable .slang shader files & entry points match CMakeLists.txt SPIR-V build rules
    // ---------------------------------------------------------------------------------------------------------
    const std::string cmakeText = ReadWholeText("CMakeLists.txt");
    struct ShaderEntryCheck { const char* FileName; const char* EntryName; };
    const ShaderEntryCheck entries[9] = {
        { "XpbdPredictIntegrate.slang",         "CsXpbdPredictIntegrate"    },
        { "XpbdDistanceBatch.slang",            "CsXpbdDistanceBatch"       },
        { "XpbdBendBatch.slang",                "CsXpbdBendBatch"           },
        { "NeoHookeanTetBatch.slang",           "CsNeoHookeanTetBatch"      },
        { "DeformableContactUpdate.slang",      "CsDeformableContactUpdate" },
        { "VertexMotionAtlasBakeCompute.slang", "CsVertexMotionAtlasBake"   },
        { "CageSkinningVertex.slang",           "VsCageSkinning"            },
        { "VertexMotionAtlasSampling.slang",    "VsVertexMotionAtlas"       },
        { "TireCarcassMaterial.slang",          "FsTireCarcassMaterial"     }
    };

    for (const auto& checkItem : entries)
    {
        const std::filesystem::path slangPath = std::filesystem::path("Engine/Shaders/Deformable") / checkItem.FileName;
        const std::string src = ReadWholeText(slangPath);
        const bool inSlang = !src.empty() && (src.find(checkItem.EntryName) != std::string::npos);
        const bool inCmake = (cmakeText.find(checkItem.FileName) != std::string::npos) &&
                             (cmakeText.find(checkItem.EntryName) != std::string::npos);
        Check(inSlang && inCmake,
              std::string("D24: CMake SPIR-V rule & .slang entry-point verified: ") + checkItem.FileName + ":" + checkItem.EntryName);
    }

    std::cout << "SimulationSceneValidation: " << gPassed << " passed, " << gFailed << " failed.\n";
    return (gFailed == 0) ? 0 : 1;
}
