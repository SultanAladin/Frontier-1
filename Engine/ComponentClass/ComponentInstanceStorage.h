/*============================================================================================================================================
                                                        COMPONENTINSTANCESTORAGE.H
============================================================================================================================================*/
// 🧩 Cache-contiguous typed component instance storage with EnclosingIndex/FirstNestedIndex/NextPeerIndex hierarchy links

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ComponentClass/ComponentInstanceCategories.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::ComponentClass
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ClassSummaryMetrics
{
    uint32_t TotalComponents    = 0u; // [cnt] - Live Spatial + Behaviour instances
    uint32_t GeometryCount      = 0u; // [cnt] - Live GeometryInstance count
    uint32_t ConstraintCount    = 0u; // [cnt] - Live ConstraintInstance count
    uint32_t LightCount         = 0u; // [cnt] - Live LightInstance count
    uint32_t CollisionCount     = 0u; // [cnt] - Live CollisionVolume count
    uint32_t RigidBodyCount     = 0u; // [cnt] - Live RigidBodyInstance count
    uint32_t BehaviourCount     = 0u; // [cnt] - Live VehicleDynamics + RotationalMotion count
    uint32_t SoftBodyCount      = 0u; // [cnt] - Live SoftBodyInstance count
    uint32_t ForceFieldCount    = 0u; // [cnt] - Live ForceFieldInstance count
    uint32_t FluidRegionCount   = 0u; // [cnt] - Live FluidRegionInstance count
    uint32_t ParticleCloudCount = 0u; // [cnt] - Live ParticleCloudInstance count
};

class ComponentInstanceStorage
{
public:
    ComponentInstanceStorage();

    void ResetWithSpatialOrigin(std::string_view OriginTitle = "Origin");

    [[nodiscard]] ComponentInstanceToken QueryOriginToken() const noexcept { return OriginToken; }
    [[nodiscard]] bool                   AuthenticateToken(ComponentInstanceToken Token) const noexcept;

    [[nodiscard]] ComponentInstanceToken AppendInstance(
        ComponentCategory      Category,
        std::string_view       RequestedTitle,
        ComponentInstanceToken EnclosingToken,
        ComponentTransform     InitialTransform = {});

    [[nodiscard]] bool AttachInstance(ComponentInstanceToken TargetToken, ComponentInstanceToken NewEnclosingToken) noexcept;
    [[nodiscard]] bool RemoveInstance(ComponentInstanceToken TargetToken) noexcept;
    [[nodiscard]] ComponentInstanceToken DuplicateInstance(ComponentInstanceToken TargetToken);
    [[nodiscard]] bool RenameInstance(ComponentInstanceToken TargetToken, std::string_view RequestedTitle);

    [[nodiscard]] const ComponentInstanceHeader* InspectHeader(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] ComponentInstanceHeader*       ResolveHeader(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const ComponentInstanceHeader* InspectHeaderByIndex(uint32_t SlotZeroIndex) const noexcept;
    [[nodiscard]] ComponentInstanceToken         FindTokenByTitle(std::string_view Title) const noexcept;

    [[nodiscard]] GeometryComponentSettings*        ResolveGeometry(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const GeometryComponentSettings*  InspectGeometry(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] ConstraintComponentSettings*      ResolveConstraint(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const ConstraintComponentSettings*InspectConstraint(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] LightComponentSettings*           ResolveLight(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const LightComponentSettings*     InspectLight(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] CameraComponentSettings*          ResolveCamera(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const CameraComponentSettings*    InspectCamera(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] CollisionVolumeSettings*          ResolveCollision(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const CollisionVolumeSettings*    InspectCollision(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] DirectionArrowSettings*           ResolveArrow(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const DirectionArrowSettings*     InspectArrow(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] VehicleDynamicsSettings*          ResolveVehicle(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const VehicleDynamicsSettings*    InspectVehicle(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] RotationalMotionSettings*         ResolveRotation(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const RotationalMotionSettings*   InspectRotation(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] RigidBodyComponentSettings*       ResolveRigidBody(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const RigidBodyComponentSettings* InspectRigidBody(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] SoftBodyComponentSettings*        ResolveSoftBody(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const SoftBodyComponentSettings*  InspectSoftBody(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] ForceFieldComponentSettings*      ResolveForceField(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const ForceFieldComponentSettings* InspectForceField(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] FluidRegionComponentSettings*     ResolveFluidRegion(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const FluidRegionComponentSettings* InspectFluidRegion(ComponentInstanceToken Token) const noexcept;
    [[nodiscard]] ParticleCloudComponentSettings*   ResolveParticleCloud(ComponentInstanceToken Token) noexcept;
    [[nodiscard]] const ParticleCloudComponentSettings* InspectParticleCloud(ComponentInstanceToken Token) const noexcept;

    void WalkSpatialNesting(const std::function<void(const ComponentInstanceHeader&)>& Visitor) const;
    void CollectBehaviourTokens(std::vector<ComponentInstanceToken>& OutTokens) const;
    void CollectNestedTokens(ComponentInstanceToken EnclosingToken, std::vector<ComponentInstanceToken>& OutTokens) const;

    [[nodiscard]] ClassSummaryMetrics ComputeSummaryMetrics() const noexcept;
    [[nodiscard]] uint32_t            QueryRawSlotCount() const noexcept { return static_cast<uint32_t>(Headers.size()); }
    [[nodiscard]] bool                QuerySlotLive(uint32_t SlotZeroIndex) const noexcept;

    [[nodiscard]] static bool QueryDockTagAccepted(std::string_view DockKey, std::string_view TagCsv) noexcept;

private:
    struct SlotEntry
    {
        ComponentInstanceHeader Header   {};
        uint32_t                Gen      = 1u;
        bool                    Occupied = false;
    };

    std::vector<SlotEntry>                  Headers      {};
    std::vector<uint32_t>                    FreeSlots    {};
    ComponentInstanceToken                   OriginToken  {};

    std::vector<GeometryComponentSettings>   Geometries   {};
    std::vector<ConstraintComponentSettings> Constraints  {};
    std::vector<LightComponentSettings>      Lights       {};
    std::vector<CameraComponentSettings>     Cameras      {};
    std::vector<CollisionVolumeSettings>     Collisions   {};
    std::vector<DirectionArrowSettings>      Arrows       {};
    std::vector<VehicleDynamicsSettings>     Vehicles     {};
    std::vector<RotationalMotionSettings>      Rotations      {};
    std::vector<RigidBodyComponentSettings>    RigidBodies    {};
    std::vector<SoftBodyComponentSettings>     SoftBodies     {};
    std::vector<ForceFieldComponentSettings>   ForceFields    {};
    std::vector<FluidRegionComponentSettings>  FluidRegions   {};
    std::vector<ParticleCloudComponentSettings> ParticleClouds {};

    [[nodiscard]] uint32_t    AllocateTypedSlot(ComponentCategory Category);
    [[nodiscard]] std::string ConstructUniqueTitle(std::string_view Desired) const;
    void                      UnlinkFromEnclosing(uint32_t TargetZeroIdx) noexcept;
    void                      LinkUnderEnclosing(uint32_t TargetZeroIdx, uint32_t EnclosingZeroIdx) noexcept;
    void                      RefreshDepthLevels(uint32_t RootZeroIdx, uint16_t Depth) noexcept;
    [[nodiscard]] bool        QueryEnclosingMatch(uint32_t CandidateZeroIdx, uint32_t SampleZeroIdx) const noexcept;
};

} // namespace Frontier::ComponentClass
