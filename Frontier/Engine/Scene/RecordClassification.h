/*==========================================================================================================================================
                                                          RECORD CLASSIFICATION
                                     ------------------------------------------------------------------
                   What kind of scene object a record is: its Outliner category tab and its default display name.
==========================================================================================================================================*/

// 🧩 What kind of scene object a record is: its Outliner category tab and its default display name.

#pragma once

#include <cstdint>

namespace Frontier
{

//------------------------------------------------------------------------------------------------------------------------
//                                                         TYPES
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fixed for a record's whole lifetime (chosen at construction). The Scene Directory groups and filters by it;
//    the vocabulary follows the scene the VehicleHMI port builds (vehicle + tyres, environment, geometry,
//    illumination, cameras, physics bodies, interface and content objects).
enum class RecordClassification : uint8_t
{
    Vehicle,
    Tyre,
    Environment,
    Geometry,
    Illumination,
    Camera,
    Physics,
    Interface,
    Content,
    Unclassified,

    Count   // [-] - Sentinel: number of classifications, not a classification itself
};

[[nodiscard]] constexpr const char* RecordClassificationName(RecordClassification Classification) noexcept
{
    switch (Classification)
    {
        case RecordClassification::Vehicle:      return "Vehicle";
        case RecordClassification::Tyre:         return "Tyre";
        case RecordClassification::Environment:  return "Environment";
        case RecordClassification::Geometry:     return "Geometry";
        case RecordClassification::Illumination: return "Illumination";
        case RecordClassification::Camera:       return "Camera";
        case RecordClassification::Physics:      return "Physics";
        case RecordClassification::Interface:    return "Interface";
        case RecordClassification::Content:      return "Content";
        case RecordClassification::Unclassified: return "Unclassified";
        case RecordClassification::Count:        break;
    }
    return "Unclassified";
}

} // namespace Frontier
