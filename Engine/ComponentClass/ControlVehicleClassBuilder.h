/*============================================================================================================================================
                                                       CONTROLVEHICLECLASSBUILDER.H
============================================================================================================================================*/
// 🧩 Deterministic ControlVehicle ComponentClass & .GeometrySpace asset repository builder from ControlledIntegrator.GeometrySpace

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentClassDefinition.h"
#include "Engine/GeometrySpace/Primitives/AnalyticPrimitives.h"

#include <filesystem>
#include <string>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct CompiledGeometryRepositoryEntry
{
    std::string                            AssetIdentifier {}; // [-] - Key referenced by GeometryComponentSettings
    std::string                            DisplayTitle    {}; // [-] - Human-readable title
    std::string                            SourceFormat    {}; // [-] - GeometrySpace / Procedural
    GeometrySpace::ConstructedSampleAsset  Asset           {}; // [-] - Compiled PolygonNetwork & FacePartitionRegister
};

struct ControlVehicleBuildResult
{
    ComponentClassDefinition                     ClassDefinition    {}; // [-] - Assembled ControlVehicle ComponentClass
    std::vector<CompiledGeometryRepositoryEntry> GeometryRepository {}; // [-] - 4 compiled vehicle assets (Body, Tyre, Rim, RearWing)
    uint32_t                                     DecodedSourceFaces = 0u; // [cnt] - Total decoded faces from source .GeometrySpace
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ControlVehicleClassBuilder
{
public:
    [[nodiscard]] static GeometrySpace::ConstructedSampleAsset ConstructProceduralRearWing();

    [[nodiscard]] static bool ConstructFromControlledIntegrator(
        const std::filesystem::path& ControlledIntegratorPath,
        ControlVehicleBuildResult&   OutResult,
        std::string&                 OutDiagnostic);

    [[nodiscard]] static bool ConstructControlVehicle(
        const std::filesystem::path& SourcePathOrDirectory,
        ControlVehicleBuildResult&   OutResult,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::ComponentClass
