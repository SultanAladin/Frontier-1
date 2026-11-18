/*============================================================================================================================================
                                                        COMPONENTGIZMOGEOMETRY.H
============================================================================================================================================*/
// 🧩 Procedural & compiled geometry vertex builders for Component Editor .slang pipelines (surface channels, constraints, shells, beams)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceCategories.h"
#include "Engine/ComponentClass/ControlVehicleClassBuilder.h"
#include "Engine/Mathematics/AxisCoordinates.h"

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier::GeometricRaster::ComponentEditor
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ComponentShaderVertex
{
    float PositionMeters[3] {}; // [m]  - Local/World vertex position
    float NormalVector[3]   {}; // [-]  - Unit shading normal
    float ParameterUv[2]    {}; // [uv] - Texture / parametric UV
    float AuxColor[3]       {}; // [-]  - Density heat / Cluster LOD hue / Aux RGB
};

static_assert(sizeof(ComponentShaderVertex) == 44u);

struct GeometrySlotSlice
{
    uint32_t              SlotIdentifier  = 0u;
    uint32_t              FirstVertex     = 0u;
    uint32_t              VertexCount     = 0u;
    Mathematics::Scalar3D AlbedoLinearRGB { 0.56f, 0.58f, 0.63f };
    Mathematics::Scalar3D EmissiveRGB     { 0.0f, 0.0f, 0.0f };
    float                 Roughness       = 0.55f;
    float                 Metalness       = 0.0f;
};

struct CompiledGeometryVisualBatch
{
    std::vector<ComponentShaderVertex> FullDetailVertices {};
    std::vector<ComponentShaderVertex> DensityVertices    {};
    std::vector<ComponentShaderVertex> LodVertices        {};
    std::vector<GeometrySlotSlice>     SlotSlices         {};
    bool                               HasValidUvs        = true;
    uint32_t                           TriangleCount      = 0u;
    uint32_t                           LodTriangleCount   = 0u;
    Mathematics::Axis3D                BoundsMin          { 0.0f, 0.0f, 0.0f };
    Mathematics::Axis3D                BoundsMax          { 0.0f, 0.0f, 0.0f };
};

enum class ConstraintPieceKind : uint8_t
{
    ShadedSolid  = 0u, // [-] - SubMode 0: OpenPbrDirect shaded hardware (graphite/steel/accent)
    RangeFan     = 1u, // [-] - SubMode 1: Additive Hinge/Ball limit fan
    CometBand    = 2u, // [-] - SubMode 2: Additive Spin 3-comet ribbon
    DockIrisDisc = 3u, // [-] - SubMode 3: Additive Dock pulsing iris disc
};

struct ConstraintVisualPiece
{
    ConstraintPieceKind                PieceKind     = ConstraintPieceKind::ShadedSolid;
    std::vector<ComponentShaderVertex> Vertices      {};
    std::array<float, 3>               AlbedoRgb     { 0.6f, 0.6f, 0.6f };
    std::array<float, 3>               EmissiveRgb   { 0.0f, 0.0f, 0.0f };
    float                              Roughness     = 0.35f;
    float                              Metalness     = 0.60f;
    float                              AlphaOrScale  = 1.0f;
    float                              NormCursor    = -1.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class ComponentGizmoGeometry
{
public:
    [[nodiscard]] static CompiledGeometryVisualBatch BuildCompiledAssetBatch(
        const ComponentClass::CompiledGeometryCatalogEntry& Entry);

    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildDotFloorQuad(float HalfSpanMeters = 120.0f);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildUnitBoxShell();
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildUnitSphereShell(uint32_t Stacks = 18u, uint32_t Slices = 32u);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildCapsuleShell(float Radius, float HalfHeight, uint8_t AxisIndex);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildSpotBeamCone(float LengthMeters, float RadiusMeters, uint32_t Sides = 32u);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildCameraFrustumBeam(float LengthMeters, float VerticalFovDeg, float AspectRatio);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildSolidArrow(float LengthMeters);
    [[nodiscard]] static std::vector<ComponentShaderVertex> BuildOriginRing(float RadiusMeters);

    [[nodiscard]] static std::vector<ConstraintVisualPiece> BuildConstraintVisualPieces(
        const ComponentClass::ConstraintComponentSettings& Settings,
        float                                              CurrentCoordinate,
        bool                                               DockIntact,
        bool                                               DockKeyAccepted,
        float                                              TimeSeconds);

    [[nodiscard]] static std::array<float, 3> QueryConstraintAccentLinearRgb(
        ComponentClass::PhysicsConstraintKind Kind) noexcept;
};

} // namespace Frontier::GeometricRaster::ComponentEditor
