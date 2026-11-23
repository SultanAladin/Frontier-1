//============================================================================================================================================
//                                                      PROFILESPECIFICATION.H
//============================================================================================================================================
// 🧩 One planar modelling profile — a working plane plus one outer loop and any number of inner loops. This is
//    the correct authority for feature operations such as extrusion, rather than a single curve identity.

#pragma once

#include "SlateShape/Geometry/CurveSpecification/Api/CurveSpecification.h"

#include <cstdint>
#include <vector>

namespace Slate
{

struct ProfileName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class ProfileLoopOrientation : std::uint32_t
{
    Outer = 0u,
    Inner = 1u
};

struct ProfileCurveUse
{
    CurveName TraversedCurve = {};
    bool SameSense = true;
};

struct ProfileLoop
{
    ProfileLoopOrientation Orientation = ProfileLoopOrientation::Outer;
    std::vector<ProfileCurveUse> Traversal = {};
};

struct ProfilePlane
{
    SpatialPoint Origin = {};
    SpatialDirection Normal = {};
    SpatialDirection AlongDirection = {};
};

class ProfileSpecification
{
public:
    void DeclarePlane(const ProfilePlane& Incoming) { Plane = Incoming; }
    void DeclareLoop(const ProfileLoop& Incoming) { Loops.push_back(Incoming); }

    const ProfilePlane& HeldPlane() const { return Plane; }
    const std::vector<ProfileLoop>& HeldLoops() const { return Loops; }
    bool Declared() const;

private:
    ProfilePlane Plane = {};
    std::vector<ProfileLoop> Loops = {};
};

} // namespace Slate
