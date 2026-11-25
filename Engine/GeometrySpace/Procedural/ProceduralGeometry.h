/*============================================================================================================================================
                                                           PROCEDURALGEOMETRY.H
============================================================================================================================================*/
// 🧩 Extensible procedural geometry framework (IProceduralGeometryGenerator + ProceduralGeometryRegistry + ProceduralGeometryAsset)
//    Designed for analytical shapes, parametric surfaces, and complex domain generators (trees, buildings, tyres, custom plugins).

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Network/FacePartitionRegister.h"
#include "Engine/GeometrySpace/Network/PolygonNetwork.h"
#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SpatialAcceleration/ClusterPyramid.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Frontier::GeometrySpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class ProceduralGeometryDomain : uint8_t
{
    Analytic          = 0u, // [-] - Analytical parametric primitives
    ParametricSurface = 1u, // [-] - Sweeps, terrains, splines, Voronoi, compound solids
    Botanical         = 2u, // [-] - Procedural trees, branches, bark, foliage canopies
    Architectural     = 3u, // [-] - Procedural buildings, facades, roofs, stairs, modular kits
    Automotive        = 4u, // [-] - Procedural tyres, tread blocks, rims, brake assemblies
    Custom            = 5u, // [-] - User or project-registered custom procedural generators
};

enum class ProceduralParameterType : uint8_t
{
    Float = 0u,
    Int   = 1u,
    Bool  = 2u,
    Enum  = 3u,
    Seed  = 4u,
    Vec3  = 5u,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ProceduralParameterDescriptor
{
    std::string             Key          {};                             // [-]   - Stable programmatic key (e.g. "tyre.width_m")
    std::string             Label        {};                             // [-]   - Human-readable inspector label
    std::string             Unit         {};                             // [-]   - Display unit suffix ("m", "deg", "mm", "")
    std::string             Group        { "Parameters" };               // [-]   - UI grouping section
    ProceduralParameterType Type         = ProceduralParameterType::Float;
    float                   MinValue     = 0.0f;
    float                   MaxValue     = 1.0f;
    float                   DefaultFloat = 0.0f;
    int32_t                 DefaultInt   = 0;
    bool                    DefaultBool  = false;
    Mathematics::Scalar3D   DefaultVec3  { 1.0f, 1.0f, 1.0f };
    std::vector<std::string> EnumLabels  {};
};

struct ProceduralParameterValue
{
    ProceduralParameterType Type       = ProceduralParameterType::Float;
    float                   FloatValue = 0.0f;
    int32_t                 IntValue   = 0;
    bool                    BoolValue  = false;
    Mathematics::Scalar3D   Vec3Value  { 1.0f, 1.0f, 1.0f };
};

class ProceduralParameterSet
{
public:
    ProceduralParameterSet() = default;

    void SetFloat(std::string_view Key, float Value);
    void SetInt(std::string_view Key, int32_t Value);
    void SetSeed(std::string_view Key, uint32_t Seed);
    void SetBool(std::string_view Key, bool Value);
    void SetEnum(std::string_view Key, uint32_t Index);
    void SetVec3(std::string_view Key, Mathematics::Scalar3D Value);

    [[nodiscard]] float                 GetFloat(std::string_view Key, float Fallback = 0.0f) const noexcept;
    [[nodiscard]] int32_t               GetInt(std::string_view Key, int32_t Fallback = 0) const noexcept;
    [[nodiscard]] uint32_t              GetSeed(std::string_view Key, uint32_t Fallback = 42u) const noexcept;
    [[nodiscard]] bool                  GetBool(std::string_view Key, bool Fallback = false) const noexcept;
    [[nodiscard]] uint32_t              GetEnum(std::string_view Key, uint32_t Fallback = 0u) const noexcept;
    [[nodiscard]] Mathematics::Scalar3D GetVec3(std::string_view Key, Mathematics::Scalar3D Fallback = { 1.0f, 1.0f, 1.0f }) const noexcept;

    [[nodiscard]] bool                  Contains(std::string_view Key) const noexcept;
    [[nodiscard]] uint64_t              ComputeHash() const noexcept;
    [[nodiscard]] const std::unordered_map<std::string, ProceduralParameterValue>& Entries() const noexcept { return Values; }

private:
    std::unordered_map<std::string, ProceduralParameterValue> Values {};
};

struct ProceduralSubPartTag
{
    std::string Name             {};      // [-]    - Semantic part name ("Trunk", "Canopy", "Tread", "Rim", "Facade", "Roof")
    std::string MaterialSlotName {};      // [-]    - Suggested OpenPBR material slot name
    uint16_t    PartGroupOrdinal = 0u;    // [idx]  - Matching PolygonNetwork face PartGroupOrdinal
    uint32_t    UdimTile         = 1001u; // [code] - Target UDIM tile code
};

struct ProceduralGeometryOutput
{
    PolygonNetwork                    Network           {}; // [-]   - Generated half-edge mesh with linked edges & normals
    FacePartitionRegister             Partitions        {}; // [-]   - Optional UV / material partition register
    std::vector<ProceduralSubPartTag> SubParts          {}; // [-]   - Semantic sub-parts for multi-material assignment
    Mathematics::Axis3D               BoundsMin         {}; // [m]   - Local-space AABB minimum
    Mathematics::Axis3D               BoundsMax         {}; // [m]   - Local-space AABB maximum
    uint32_t                          VertexCount       = 0u;
    uint32_t                          TriangleCount     = 0u;
    uint64_t                          ContentHash       = 0ull;
    std::string                       DiagnosticMessage {};

    [[nodiscard]] Mathematics::Scalar3D DimensionsMeters() const noexcept
    {
        return {
            std::max(0.01f, BoundsMax.X - BoundsMin.X),
            std::max(0.01f, BoundsMax.Y - BoundsMin.Y),
            std::max(0.01f, BoundsMax.Z - BoundsMin.Z)
        };
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                GENERATOR INTERFACE
//------------------------------------------------------------------------------------------------------------------------

class IProceduralGeometryGenerator
{
public:
    virtual ~IProceduralGeometryGenerator() = default;

    [[nodiscard]] virtual std::string_view                           QueryTypeId() const noexcept          = 0;
    [[nodiscard]] virtual std::string_view                           QueryDisplayName() const noexcept     = 0;
    [[nodiscard]] virtual ProceduralGeometryDomain                   QueryDomain() const noexcept          = 0;
    [[nodiscard]] virtual std::span<const ProceduralParameterDescriptor> QueryParameterSchema() const noexcept = 0;
    [[nodiscard]] virtual ProceduralParameterSet                     BuildDefaultParameters() const        = 0;

    [[nodiscard]] virtual bool Generate(
        const ProceduralParameterSet& Parameters,
        ProceduralGeometryOutput&     OutResult) const = 0;

    // Optional closed-form signed distance evaluation for exact SDF tracing/baking (returns std::nullopt when mesh-only)
    [[nodiscard]] virtual std::optional<float> EvaluateAnalyticSdf(
        Mathematics::Axis3D           /*LocalPoint*/,
        const ProceduralParameterSet& /*Parameters*/) const noexcept
    {
        return std::nullopt;
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 GENERATOR REGISTRY
//------------------------------------------------------------------------------------------------------------------------

class ProceduralGeometryRegistry
{
public:
    ProceduralGeometryRegistry();

    static ProceduralGeometryRegistry& Global();

    bool RegisterGenerator(std::unique_ptr<IProceduralGeometryGenerator> Generator);

    [[nodiscard]] const IProceduralGeometryGenerator* FindGenerator(std::string_view TypeId) const noexcept;
    [[nodiscard]] const IProceduralGeometryGenerator* FindByIndex(uint32_t Index) const noexcept;
    [[nodiscard]] uint32_t                            GeneratorCount() const noexcept { return static_cast<uint32_t>(OrderedGenerators.size()); }
    [[nodiscard]] std::vector<const IProceduralGeometryGenerator*> ListByDomain(ProceduralGeometryDomain Domain) const;

    // Convenience helper mapping inspector GeneratorIndex [0..7] to canonical generator TypeId
    [[nodiscard]] static std::string_view CanonicalTypeIdForIndex(uint32_t GeneratorIndex) noexcept;
    [[nodiscard]] static const char*      DomainDisplayName(ProceduralGeometryDomain Domain) noexcept;

private:
    void RegisterBuiltinGenerators();

    std::vector<std::unique_ptr<IProceduralGeometryGenerator>>                  OwnedGenerators   {};
    std::vector<const IProceduralGeometryGenerator*>                            OrderedGenerators {};
    std::unordered_map<std::string, const IProceduralGeometryGenerator*>        ByTypeId          {};
};

//------------------------------------------------------------------------------------------------------------------------
//                                              STATEFUL PROCEDURAL ASSET
//------------------------------------------------------------------------------------------------------------------------

class ProceduralGeometryAsset
{
public:
    ProceduralGeometryAsset() = default;
    explicit ProceduralGeometryAsset(std::string TypeId);

    void SetGeneratorTypeId(std::string TypeId, const ProceduralGeometryRegistry& Registry = ProceduralGeometryRegistry::Global());
    [[nodiscard]] const std::string& QueryGeneratorTypeId() const noexcept { return GeneratorTypeId; }

    [[nodiscard]] ProceduralParameterSet&       AccessParameters() noexcept       { return Parameters; }
    [[nodiscard]] const ProceduralParameterSet& QueryParameters() const noexcept  { return Parameters; }

    // Synchronizes common inspector parameters (Seed, Subdivision, Scale, Amplitude) into Parameters
    void SyncCommonInspectorParameters(
        uint32_t GeneratorIndex,
        uint32_t RandomSeed,
        uint32_t SubdivisionLevel,
        float    FeatureScaleMeters,
        float    AmplitudeMeters,
        const ProceduralGeometryRegistry& Registry = ProceduralGeometryRegistry::Global());

    // Rebuilds mesh (and optional Nanite ClusterPyramid) strictly when the parameter/generator hash changes.
    // Returns true if a rebuild occurred; false on cache hit (zero CPU work).
    [[nodiscard]] bool EnsureUpToDate(
        bool                              BuildClusterPyramid = false,
        const ProceduralGeometryRegistry& Registry            = ProceduralGeometryRegistry::Global());

    [[nodiscard]] bool                                 IsValid() const noexcept        { return HasValidOutput; }
    [[nodiscard]] uint32_t                             QueryBuildRevision() const noexcept { return BuildRevision; }
    [[nodiscard]] uint64_t                             QueryCachedHash() const noexcept { return CachedBuildHash; }
    [[nodiscard]] const ProceduralGeometryOutput&      QueryOutput() const noexcept    { return Output; }
    [[nodiscard]] const SpatialAcceleration::ClusterPyramid& QueryClusterPyramid() const noexcept { return ClusterPyramidAsset; }
    [[nodiscard]] bool                                 HasClusterPyramid() const noexcept { return ClusterPyramidBuilt; }

private:
    std::string                        GeneratorTypeId     { "procedural.parametric_sweep" };
    ProceduralParameterSet             Parameters          {};
    ProceduralGeometryOutput           Output              {};
    SpatialAcceleration::ClusterPyramid ClusterPyramidAsset {};
    uint64_t                           CachedBuildHash     = 0ull;
    uint32_t                           BuildRevision       = 0u;
    bool                               HasValidOutput      = false;
    bool                               ClusterPyramidBuilt = false;
};

} // namespace Frontier::GeometrySpace
