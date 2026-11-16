/*============================================================================================================================================
                                                           COLLISIONDETECTION.H
============================================================================================================================================*/
// 🧩 Narrow-phase contact impulse journal capturing peak impact impulses & energies for rigid bodies and fracture triggers

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Mathematics/AxisCoordinates.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <cstdint>
#include <span>
#include <vector>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct ContactImpulseReading
{
    RigidBodyToken      BodyA              {};                   // [id]  - First colliding body token
    RigidBodyToken      BodyB              {};                   // [id]  - Second colliding body token
    Mathematics::Axis3D ContactPoint       { 0.0f, 0.0f, 0.0f }; // [m]   - World-space contact manifold point
    Mathematics::Axis3D ContactNormal      { 0.0f, 0.0f, 1.0f }; // [-]   - World-space normal pointing from A to B
    float               NormalImpulseNs    = 0.0f;               // [N*s] - Estimated normal contact impulse magnitude
    float               PenetrationDepthM  = 0.0f;               // [m]   - Manifold penetration depth
    float               ImpactEnergyJoules = 0.0f;               // [J]   - Estimated relative impact energy
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  FUNCTION PROTOTYPES
//------------------------------------------------------------------------------------------------------------------------

class CollisionDetection
{
public:
    void ClearTickJournal() noexcept;
    void AppendContactReading(const ContactImpulseReading& Reading);

    [[nodiscard]] std::span<const ContactImpulseReading> InspectContacts() const noexcept;
    [[nodiscard]] float                                  QueryPeakImpulseNs() const noexcept;
    [[nodiscard]] uint32_t                               QueryContactCount() const noexcept;

private:
    std::vector<ContactImpulseReading> Contacts          {};   // [-]     - Contacts recorded during current simulation tick
    float                              PeakImpulseNs     = 0.0f; // [N*s] - Maximum impulse seen during current tick
};

} // namespace Frontier::SimulationSpace::PhysicsSpace
