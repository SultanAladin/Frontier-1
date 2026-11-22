/*============================================================================================================================================
                                                          COLLISIONDETECTION.CPP
============================================================================================================================================*/
// 🧩 Implementation of narrow-phase ContactImpulseJournal tracking peak impulses and impact energy per simulation step

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/PhysicsSpace/CollisionDetection.h"

#include <algorithm>

namespace Frontier::SimulationSpace::PhysicsSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   IMPLEMENTATION
//------------------------------------------------------------------------------------------------------------------------

void CollisionDetection::ClearTickJournal() noexcept
{
    Contacts.clear();
    PeakImpulseNs = 0.0f;
}

void CollisionDetection::AppendContactReading(const ContactImpulseReading& Reading)
{
    Contacts.push_back(Reading);
    PeakImpulseNs = std::max(PeakImpulseNs, Reading.NormalImpulseNs);
}

std::span<const ContactImpulseReading> CollisionDetection::InspectContacts() const noexcept
{
    return Contacts;
}

float CollisionDetection::QueryPeakImpulseNs() const noexcept
{
    return PeakImpulseNs;
}

uint32_t CollisionDetection::QueryContactCount() const noexcept
{
    return static_cast<uint32_t>(Contacts.size());
}

} // namespace Frontier::SimulationSpace::PhysicsSpace
