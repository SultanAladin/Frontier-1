/*============================================================================================================================================
                                               COMPONENTCLASSSIMULATIONASSEMBLER.CPP
============================================================================================================================================*/
// 🧩 Phase D28 Prefab-to-Simulation World Assembler implementation

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/ComponentClassSimulationAssembler.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Frontier::SimulationSpace
{

namespace
{

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

std::array<float, 3> RotateYzx(const std::array<float, 3>& v, const Mathematics::Axis3D& deg) noexcept
{
    const float ry = deg.Y * kDegToRad;
    const float rz = deg.Z * kDegToRad;
    const float rx = deg.X * kDegToRad;

    // X rotation
    const float cx = std::cos(rx), sx = std::sin(rx);
    float x1 = v[0];
    float y1 = v[1] * cx - v[2] * sx;
    float z1 = v[1] * sx + v[2] * cx;

    // Z rotation
    const float cosZ = std::cos(rz), sinZ = std::sin(rz);
    float x2 = x1 * cosZ - y1 * sinZ;
    float y2 = x1 * sinZ + y1 * cosZ;
    float z2 = z1;

    // Y rotation
    const float cy = std::cos(ry), sy = std::sin(ry);
    return {
        x2 * cy + z2 * sy,
        y2,
        -x2 * sy + z2 * cy
    };
}

std::array<float, 3> ApplyComponentTransform(
    const std::array<float, 3>& p,
    const ComponentClass::ComponentTransform& xf) noexcept
{
    const std::array<float, 3> scaled {
        p[0] * xf.ScaleFactors.X,
        p[1] * xf.ScaleFactors.Y,
        p[2] * xf.ScaleFactors.Z
    };
    const std::array<float, 3> rotated = RotateYzx(scaled, xf.RotationDegrees);
    return {
        rotated[0] + xf.LocationMeters.X,
        rotated[1] + xf.LocationMeters.Y,
        rotated[2] + xf.LocationMeters.Z
    };
}

uint64_t PackClassTokenKey(ComponentClass::ComponentClassToken tok) noexcept
{
    return (static_cast<uint64_t>(tok.Generation) << 32u) | static_cast<uint64_t>(tok.SlotIndex);
}

bool QueryEffectiveVisibility(
    const ComponentClass::ComponentInstanceHeader&         hdr,
    const ComponentClass::ComponentWorldPlacementRegister& placementRegister,
    ComponentClass::WorldPlacementToken                    placementToken) noexcept
{
    if (hdr.HiddenInPlay) return false;
    if (const auto* ov = placementRegister.InspectOverride(placementToken, hdr.Token); ov != nullptr && ov->OverrideVisibility)
    {
        return ov->VisibilityEnabled;
    }
    return hdr.VisibilityEnabled;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                           COMPONENTCLASS SIMULATION ASSEMBLER
//------------------------------------------------------------------------------------------------------------------------

ComponentClass::ComponentClassDefinition ComponentClassSimulationAssembler::BuildTracksideRigComponentClass()
{
    using namespace ComponentClass;

    ComponentClassDefinition def {};
    def.Title          = "TracksideSimulationRig";
    def.ClassArchetype = ComponentClassArchetype::DynamicRig;
    def.Storage.ResetWithSpatialOrigin("RigOrigin");

    const auto origin = def.Storage.QueryOriginToken();

    // 1. Directional wind + local vortex force fields
    const auto windTok = def.Storage.AppendInstance(
        ComponentCategory::ForceFieldInstance, "CrosswindField", origin,
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* ff = def.Storage.ResolveForceField(windTok))
    {
        ff->ModelCode    = static_cast<uint8_t>(ForceFields::ForceModelOrdinal::DirectionalWind);
        ff->Direction    = { 1.0f, 0.15f, 0.25f };
        ff->Strength     = 4.6f;
        ff->RadiusMeters = 12.0f;
        ff->FalloffCode  = static_cast<uint8_t>(ForceFields::ForceFalloffOrdinal::Constant);
    }

    // 2. Campfire anchor pivot -> nested FluidRegion + ParticleCloud
    const auto firePivot = def.Storage.AppendInstance(
        ComponentCategory::TransformPivot, "CampfireAnchor", origin,
        { { -1.95f, 0.42f, -1.55f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });

    const auto fluidTok = def.Storage.AppendInstance(
        ComponentCategory::FluidRegionInstance, "CampfirePlume", firePivot, {});
    if (auto* fl = def.Storage.ResolveFluidRegion(fluidTok))
    {
        fl->RadiusMeters = 0.42f;
        fl->VelocityMps  = { 0.25f, 2.40f, 0.10f };
        fl->Density      = 1.65f;
        fl->Temperature  = 2.85f;
        fl->Fuel         = 1.20f;
    }

    const auto cloudTok = def.Storage.AppendInstance(
        ComponentCategory::ParticleCloudInstance, "CampfireEmbers", firePivot,
        { { 0.0f, 0.06f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* pc = def.Storage.ResolveParticleCloud(cloudTok))
    {
        pc->RadiusMeters   = 0.36f;
        pc->ParticleCount  = 256u;
        pc->ReleaseRate    = 12.0f;
        pc->SpeedMps       = 1.60f;
        pc->LifetimeSec    = 2.40f;
        pc->GravityY       = 0.15f;
        pc->GasInfluence   = 0.90f;
        pc->FieldInfluence = 0.85f;
    }

    // 3. WovenCloth banner + intersecting spherical RigidCollider proxy
    const auto bannerTok = def.Storage.AppendInstance(
        ComponentCategory::SoftBodyInstance, "TrackBannerCloth", origin,
        { { 0.30f, 1.78f, -2.05f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* sb = def.Storage.ResolveSoftBody(bannerTok))
    {
        sb->SoftBodyAssetPath = "EngineContent/Simulation/Deformables/WovenCloth.SoftBody";
        sb->VertexAtlasPath   = "EngineContent/Simulation/Deformables/SportTireFem.VertexAtlas";
    }

    const auto colliderTok = def.Storage.AppendInstance(
        ComponentCategory::CollisionVolume, "BannerBumperSphere", origin,
        { { 0.30f, 2.05f, -1.95f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* col = def.Storage.ResolveCollision(colliderTok))
    {
        col->Form         = CollisionVolumeCategory::Sphere;
        col->RadiusMeters = 0.42f;
    }

    // 4. BotanicalFerns deformable cluster
    const auto fernsTok = def.Storage.AppendInstance(
        ComponentCategory::SoftBodyInstance, "TracksideFerns", origin,
        { { 2.35f, 1.78f, -1.35f }, { 0.0f, 0.0f, 0.0f }, { 1.0f, 1.0f, 1.0f } });
    if (auto* sb = def.Storage.ResolveSoftBody(fernsTok))
    {
        sb->SoftBodyAssetPath = "EngineContent/Simulation/Deformables/BotanicalFerns.SoftBody";
    }

    def.FinalizeCompiledRevision();
    return def;
}

std::array<float, 3> ComponentClassSimulationAssembler::ComputeEffectiveWorldPosition(
    const ComponentClass::ComponentClassDefinition&        definition,
    const ComponentClass::ComponentWorldPlacementRegister& placementRegister,
    ComponentClass::WorldPlacementToken                    placementToken,
    ComponentClass::ComponentInstanceToken                 componentToken) noexcept
{
    std::vector<ComponentClass::ComponentInstanceToken> chain;
    const auto* cursor = definition.Storage.InspectHeader(componentToken);
    while (cursor != nullptr)
    {
        chain.push_back(cursor->Token);
        if (cursor->EnclosingIndex == ComponentClass::InvalidNestingIndex) break;
        cursor = definition.Storage.InspectHeaderByIndex(cursor->EnclosingIndex);
    }

    std::array<float, 3> pos { 0.0f, 0.0f, 0.0f };
    for (const auto tok : chain)
    {
        const auto xf = placementRegister.EvaluateEffectiveLocalTransform(definition, placementToken, tok);
        pos = ApplyComponentTransform(pos, xf);
    }

    if (const auto* placement = placementRegister.InspectPlacement(placementToken))
    {
        pos = ApplyComponentTransform(pos, placement->WorldTransform);
    }
    return pos;
}

SimulationSceneDocument ComponentClassSimulationAssembler::BuildSceneFromPlacements(
    const std::unordered_map<uint64_t, const ComponentClass::ComponentClassDefinition*>& classCatalog,
    const ComponentClass::ComponentWorldPlacementRegister&                               placementRegister,
    std::string_view                                                                     sceneTitle,
    PrefabAssemblyReport*                                                                outReport)
{
    SimulationSceneDocument doc {};
    doc.Title               = std::string(sceneTitle);
    doc.SchemaVersion       = 1u;
    doc.FixedStepHz         = 60.0f;
    doc.FluidGridCells      = { 16u, 16u, 16u };
    doc.FluidCellSizeMeters = 0.22f;
    doc.FluidBuoyancy       = 2.1f;
    doc.FluidCombustion     = 1.25f;
    doc.FluidPressureIters  = 12u;

    PrefabAssemblyReport report {};
    uint32_t nextColliderSlot = 100u;
    bool fluidCentreAssigned = false;

    const auto livePlacements = placementRegister.CollectLiveTokens();
    for (const auto placementTok : livePlacements)
    {
        const auto* placement = placementRegister.InspectPlacement(placementTok);
        if (placement == nullptr) continue;

        const auto it = classCatalog.find(PackClassTokenKey(placement->ClassToken));
        if (it == classCatalog.end() || it->second == nullptr) continue;
        const auto& def = *it->second;
        ++report.PlacementsProcessed;

        def.Storage.WalkSpatialNesting([&](const ComponentClass::ComponentInstanceHeader& hdr)
        {
            if (!QueryEffectiveVisibility(hdr, placementRegister, placementTok))
            {
                ++report.SkippedHiddenCount;
                return;
            }

            const std::array<float, 3> worldPos = ComputeEffectiveWorldPosition(def, placementRegister, placementTok, hdr.Token);
            const std::string scopedName = placement->InstanceTitle + "::" + hdr.Title;

            switch (hdr.Category)
            {
                case ComponentClass::ComponentCategory::ForceFieldInstance:
                {
                    if (const auto* ff = def.Storage.InspectForceField(hdr.Token))
                    {
                        const auto worldDir = RotateYzx(
                            { ff->Direction.X, ff->Direction.Y, ff->Direction.Z },
                            placement->WorldTransform.RotationDegrees);
                        doc.Forces.push_back({
                            scopedName,
                            static_cast<ForceFields::ForceModelOrdinal>(ff->ModelCode),
                            worldPos,
                            worldDir,
                            ff->Strength,
                            ff->RadiusMeters,
                            static_cast<ForceFields::ForceFalloffOrdinal>(ff->FalloffCode)
                        });
                        ++report.ForcesAssembled;
                    }
                    break;
                }
                case ComponentClass::ComponentCategory::FluidRegionInstance:
                {
                    if (const auto* fl = def.Storage.InspectFluidRegion(hdr.Token))
                    {
                        doc.FluidSources.push_back({
                            scopedName,
                            worldPos,
                            fl->RadiusMeters,
                            { fl->VelocityMps.X, fl->VelocityMps.Y, fl->VelocityMps.Z },
                            fl->Density,
                            fl->Temperature,
                            fl->Fuel
                        });
                        if (!fluidCentreAssigned)
                        {
                            doc.FluidCentreMeters = { worldPos[0], worldPos[1] + 1.33f, worldPos[2] };
                            fluidCentreAssigned = true;
                        }
                        ++report.FluidSourcesAssembled;
                    }
                    break;
                }
                case ComponentClass::ComponentCategory::ParticleCloudInstance:
                {
                    if (const auto* pc = def.Storage.InspectParticleCloud(hdr.Token))
                    {
                        doc.ParticleClouds.push_back({
                            scopedName,
                            worldPos,
                            pc->RadiusMeters,
                            pc->ParticleCount,
                            pc->ReleaseRate,
                            pc->SpeedMps,
                            pc->LifetimeSec,
                            pc->GravityY,
                            pc->GasInfluence,
                            pc->FieldInfluence
                        });
                        ++report.CloudsAssembled;
                    }
                    break;
                }
                case ComponentClass::ComponentCategory::SoftBodyInstance:
                {
                    if (const auto* sb = def.Storage.InspectSoftBody(hdr.Token))
                    {
                        doc.SoftBodies.push_back({ scopedName, sb->SoftBodyAssetPath, worldPos });
                        if (doc.ShippedVertexAtlasPath.empty() && !sb->VertexAtlasPath.empty())
                        {
                            doc.ShippedVertexAtlasPath = sb->VertexAtlasPath;
                        }
                        ++report.SoftBodiesAssembled;
                    }
                    break;
                }
                case ComponentClass::ComponentCategory::CollisionVolume:
                {
                    if (const auto* col = def.Storage.InspectCollision(hdr.Token); col != nullptr && !col->TriggerOverlapOnly)
                    {
                        const float radius = (col->Form == ComponentClass::CollisionVolumeCategory::Box)
                            ? std::max({ col->HalfExtentsMeters.X, col->HalfExtentsMeters.Y, col->HalfExtentsMeters.Z })
                            : col->RadiusMeters;
                        doc.RigidColliders.push_back({ nextColliderSlot++, worldPos, radius, true });
                        ++report.CollidersAssembled;
                    }
                    break;
                }
                default:
                    break;
            }
        });
    }

    if (outReport != nullptr) *outReport = report;
    return doc;
}

bool ComponentClassSimulationAssembler::AssembleIntoCoordinator(
    ComponentClass::ComponentClassToken                    classToken,
    const ComponentClass::ComponentClassDefinition&        definition,
    const ComponentClass::ComponentWorldPlacementRegister& placementRegister,
    SimulationCoordinatorExtension&                        coordinator,
    PrefabAssemblyReport*                                  outReport,
    std::string*                                           outError)
{
    std::unordered_map<uint64_t, const ComponentClass::ComponentClassDefinition*> catalog;
    catalog.emplace(PackClassTokenKey(classToken), &definition);

    const SimulationSceneDocument doc = BuildSceneFromPlacements(catalog, placementRegister, definition.Title, outReport);
    return SimulationSceneCodec::MountIntoCoordinator(doc, coordinator, outError);
}

} // namespace Frontier::SimulationSpace
