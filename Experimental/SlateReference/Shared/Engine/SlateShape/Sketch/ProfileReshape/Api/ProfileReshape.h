//============================================================================================================================================
//                                                          PROFILERESHAPE.H
//============================================================================================================================================
// 🧩 Curve/profile reshape verbs for the two-dimensional editor: trim, cut and inset. These edit the exact
//    sketch/profile declarations rather than screen-space approximations.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/Sketch/SketchStructure/Api/SketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class ReshapeDisposition : std::uint32_t
{
    NotRequested = 0u,
    InvalidSpecification = 1u,
    UnsupportedGeometry = 2u,
    Produced = 3u
};

Deliver<SketchCurveName> TrimCurve(SketchStructure& Declared,
                                   SketchCurveName Subject,
                                   const SpatialPoint& Position,
                                   bool KeepStart);
Deliver<std::vector<SketchCurveName>> CutCurve(SketchStructure& Declared,
                                               SketchCurveName Subject,
                                               const SpatialPoint& Position);
Deliver<bool> CutProfile(SketchStructure& Declared,
                         ProfileNameInFeature Subject,
                         std::uint32_t LoopIndex,
                         std::uint32_t EdgeIndex,
                         const SpatialPoint& Position,
                         std::vector<SketchCurveName>& Produced);

ReshapeDisposition EvaluateProfileInset(const SketchStructure& Declared,
                                        ProfileNameInFeature Subject,
                                        double Distance);
Deliver<ProfileNameInFeature> ApplyProfileInset(SketchStructure& Declared,
                                                ProfileNameInFeature Subject,
                                                double Distance);

} // namespace Slate
