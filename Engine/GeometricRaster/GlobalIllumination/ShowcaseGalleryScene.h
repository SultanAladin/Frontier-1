/*============================================================================================================================================
                                                            SHOWCASEGALLERYSCENE.H
============================================================================================================================================*/
// 🧩 Canonical Architectural Cornell-Studio Gallery World Scene, EditorCamera, and OutlinerPanel bidirectional registration

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Components/Panes/OutlinerPanel.h"
#include "Engine/GeometricRaster/GlobalIllumination/TieredWorldRenderSequence.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::GeometricRaster
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ShowcasePrimitiveShape : uint8_t
{
    Box      = 0u, // [-] - Analytical box / slab primitive
    Sphere   = 1u, // [-] - Analytical UV sphere primitive
    Cylinder = 2u, // [-] - Analytical cylinder frustum (equal top & foot radii)
    Cone     = 3u, // [-] - Analytical cone frustum (tapered apex)
    Torus    = 4u, // [-] - Analytical torus ring primitive
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ShowcaseGeometryAsset
{
    GeometrySpace::PolygonNetwork       Network {}; // [-] - Authored polygon network primitive
    SpatialAcceleration::ClusterPyramid Pyramid {}; // [-] - Built 3-level cluster hierarchy
};

struct ShowcaseWorldObjectEntry
{
    std::string            Title                {};                          // [-]   - Outliner & HTML mirror entry title
    std::string            StableIdentifier     {};                          // [-]   - 1:1 HTML mirror row identifier
    std::string            MaterialTitle        {};                          // [-]   - Assigned OpenPBR material identifier
    std::string            Subtitle             {};                          // [-]   - Outliner secondary summary caption
    std::string            AuthorNotes          {};                          // [-]   - Authored inspector documentation
    Mathematics::Axis3D    CenterWorld          {};                          // [m]   - World-space center coordinate
    Mathematics::Scalar3D  DimensionsMeters     {};                          // [m]   - Full X/Y/Z extent in meters
    Mathematics::Scalar3D  AuthoredDimensions   {};                          // [m]   - Base unscaled X/Y/Z extent in meters
    uint16_t               MaterialSlot         = 0u;                        // [idx] - CompactMaterialRegister slot
    uint32_t               ClusterInstanceIndex = 0u;                        // [idx] - Index in Scene.ClusterInstances
    bool                   GlassShell           = false;                     // [-]   - True for 2-layer Abbe glass peel
    bool                   SphericalPrimitive   = false;                     // [-]   - Sphere vs box primitive marker
    bool                   DedicatedMaterial    = false;                     // [-]   - True once given an isolated material slot
    ShowcasePrimitiveShape PrimitiveShape       = ShowcasePrimitiveShape::Box; // [-] - Underlying analytical primitive shape
    OutlinerSlotToken      SlotToken            {};                          // [id]  - Registered OutlinerPanel slot token
};

struct ShowcaseAreaLightBinding
{
    OutlinerSlotToken      SlotToken            {};                        // [id]  - Bound OutlinerPanel light slot token
    uint32_t               AreaLightIndex       = 0u;                      // [idx] - Index in Scene.AreaLights
    Mathematics::LinearRGB BaseEmittedRadiance  { 7.5f, 6.6f, 5.4f };      // [nit] - Enabled radiance when visible
};

struct ShowcaseWorldSceneBundle
{
    std::array<ShowcaseGeometryAsset, 8u>                  GeometryAssets       {}; // [-] - Persistent canonical cluster pyramid owners
    std::vector<std::unique_ptr<ShowcaseGeometryAsset>>    DynamicGeometryAssets{}; // [-] - Heap-stable dynamically constructed geometry
    WorldSceneSpecification                                Scene                {}; // [-] - Complete world scene specification
    PerspectiveCameraFrame                                 EditorCamera         {}; // [-] - Canonical EditorCamera navigation frame
    std::vector<ShowcaseWorldObjectEntry>                  Objects              {}; // [-] - Registered gallery & constructed geometry
    std::vector<ShowcaseAreaLightBinding>                  AreaLightBindings    {}; // [-] - Registered LTC polygonal area lights

    OutlinerSlotToken                                      ShowcaseFolderToken  {}; // [id] - "Showcase" collection folder token
    OutlinerSlotToken                                      CamerasFolderToken   {}; // [id] - "Cameras" collection folder token
    OutlinerSlotToken                                      WorldFolderToken     {}; // [id] - "World" collection folder token
    OutlinerSlotToken                                      LightingFolderToken  {}; // [id] - "Lighting" collection folder token
    OutlinerSlotToken                                      EditorCameraToken    {}; // [id] - "Editor Camera" entry token
    OutlinerSlotToken                                      SunEmitterToken      {}; // [id] - "Sun" directional emitter token
    OutlinerSlotToken                                      CeilingSoftboxToken  {}; // [id] - "Ceiling Softbox" LTC quad light token
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

// Constructs the canonical Architectural Cornell-Studio Gallery World Scene and EditorCamera (1:1 with HTML mirror)
void BuildShowcaseGalleryScene(
    ShowcaseWorldSceneBundle& OutBundle,
    uint32_t                  ViewportWidthPx  = 640u,
    uint32_t                  ViewportHeightPx = 336u);

// Registers all Showcase Gallery folders, 10 geometry entries, EditorCamera, Sun, and Ceiling Softbox in OutlinerPanel
void RegisterShowcaseGalleryInOutliner(
    OutlinerPanel&            TargetOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      PreserveConstructTemplates = false);

// Synchronizes OutlinerPanel visibility, shadow-caster switches, and transforms back into ShowcaseWorldSceneBundle
bool SynchronizeShowcaseGalleryFromOutliner(
    const OutlinerPanel&      SourceOutliner,
    ShowcaseWorldSceneBundle& InOutBundle);

// Live bidirectional sync: adopts newly constructed Outliner entries, prunes deleted entries, and reports SDF/BVH rebuild need
bool SynchronizeShowcaseGallerySceneLive(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    bool&                     OutRequiresAccelerationRebuild,
    bool                      RebuildSdfVolume = true);

// Rebuilds SceneTree (WideBoundingTree) and optionally SceneSdf (SignedDistanceVolume) across all visible opaque geometry
void RebuildShowcaseSceneAcceleration(
    ShowcaseWorldSceneBundle& InOutBundle,
    bool                      RebuildSdfVolume = true);

// Constructs a new analytical primitive directly in Showcase folder + OutlinerPanel and compiles its geometry
OutlinerSlotToken ConstructShowcasePrimitiveInOutliner(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    ShowcasePrimitiveShape    Shape,
    Mathematics::Axis3D       WorldPosition);

// Assigns an sRGB ColourCodex swatch to the target entry's OpenPBR BaseColor (or FolderTint on folders)
bool AssignShowcaseEntryBaseColor(
    OutlinerPanel&            InOutOutliner,
    ShowcaseWorldSceneBundle& InOutBundle,
    OutlinerSlotToken         TargetSlot,
    ColorRgba                 SrgbSwatchColor,
    std::string_view          SwatchAlias = {});

// Analytical 3D ray pick against visible Showcase geometry and area lights for NormalizedU/V in [0, 1]
[[nodiscard]] OutlinerSlotToken PickShowcaseGalleryEntryAtUv(
    const OutlinerPanel&            SourceOutliner,
    const ShowcaseWorldSceneBundle& Bundle,
    float                           NormalizedU,
    float                           NormalizedV) noexcept;

// Raycasts the gallery floor / scene surfaces at NormalizedU/V in [0, 1] for Z-up (+Z) surface placement
[[nodiscard]] Mathematics::Axis3D RaycastShowcasePlacementAtUv(
    const ShowcaseWorldSceneBundle& Bundle,
    float                           NormalizedU,
    float                           NormalizedV,
    float                           HalfHeightMeters) noexcept;

} // namespace Frontier::GeometricRaster
