/*============================================================================================================================================
                                                       CONTROLVEHICLECLASSBUILDER.H
============================================================================================================================================*/
// 🧩 Deterministic ControlVehicle ComponentClass & FCT1 asset repository builder matching Vehicle Lab & ComponentViewer specs

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

struct CompiledGeometryCatalogEntry
{
    std::string                            AssetIdentifier {}; // [-] - Key referenced by GeometryComponentSettings
    std::string                            DisplayTitle    {}; // [-] - Human-readable title
    std::string                            SourceFormat    {}; // [-] - FCT1 / Procedural / OBJ / glTF
    GeometrySpace::ConstructedSampleAsset  Asset           {}; // [-] - Compiled PolygonNetwork & FacePartitionRegister
};

struct ControlVehicleBuildResult
{
    ComponentClassDefinition                  ClassDefinition {}; // [-] - Assembled ControlVehicle ComponentClass
    std::vector<CompiledGeometryCatalogEntry> GeometryCatalog {}; // [-] - 4 compiled vehicle assets (Body, Tyre, Rim, RearWing)
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ControlVehicleClassBuilder
{
public:
    [[nodiscard]] static GeometrySpace::ConstructedSampleAsset ConstructProceduralRearWing();

    [[nodiscard]] static bool ConstructControlVehicle(
        const std::filesystem::path& ModelsDirectory,
        ControlVehicleBuildResult&   OutResult,
        std::string&                 OutDiagnostic);
};

} // namespace Frontier::ComponentClass
