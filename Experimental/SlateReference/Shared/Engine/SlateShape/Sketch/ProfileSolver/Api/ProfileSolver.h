//============================================================================================================================================
//                                                          PROFILESOLVER.H
//============================================================================================================================================
// 🧩 Sketch-to-profile resolution seam. At this stage it resolves explicitly declared profiles against the held
//    sketch curve set, so later modelling operations can consume one validated exact profile set without leaning
//    on the older polygon pipeline.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class ProfileDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSketch = 1u,
    ImplementationAbsent = 2u,
    Produced = 3u
};

struct ResolvedProfileSet
{
    std::vector<CurveSpecification> Curves = {};
    std::vector<ProfileSpecification> Profiles = {};

    bool Declared() const { return !Curves.empty() && !Profiles.empty(); }
};

ProfileDisposition EvaluateProfiles(const SketchStructure& Declared);
Deliver<ResolvedProfileSet> ResolveProfiles(const SketchStructure& Declared);
Deliver<const ProfileSpecification*> ResolveProfile(const SketchStructure& Declared,
                                                    ProfileNameInFeature Profile);

} // namespace Slate
