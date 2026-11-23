//============================================================================================================================================
//                                                     WORLDSKETCHEDITING.H
//============================================================================================================================================
// 🧩 Edit and move verbs for the world-space sketch. These amend the exact 3D declarations in place, and
//    when one picked edge or vertex belongs to a closed loop they move the connected world points too so
//    the same loop deforms rather than splitting open into duplicate geometry.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "SlateShape/World/WorldSketchPicking/Api/WorldSketchPicking.h"

#include <vector>

namespace Slate
{

struct WorldPlacementSubject
{
    bool ControlPlacement = false;
    WorldPointName Point = {};
    WorldControlName Control = {};
    SpatialPoint Position = {};
};

bool ResolveWorldSketchPointPosition(const WorldSketchStructure& Declared,
                                    WorldPointName Subject,
                                    SpatialPoint& Position);

void AppendWorldPlacementUnique(std::vector<WorldPlacementSubject>& Placements,
                                const WorldPlacementSubject& Placement);
void CollectWorldCurvePlacements(const WorldSketchStructure& Declared,
                                 WorldCurveName Curve,
                                 std::vector<WorldPlacementSubject>& Placements);
void CollectWorldLoopPlacements(const WorldSketchStructure& Declared,
                                WorldLoopName Loop,
                                std::vector<WorldPlacementSubject>& Placements);

Deliver<bool> EnforceWorldSketchPoint(WorldSketchStructure& Declared,
                                     WorldPointName Subject,
                                     const SpatialPoint& Position);
Deliver<bool> EnforceWorldSketchControl(WorldSketchStructure& Declared,
                                       WorldControlName Subject,
                                       const SpatialPoint& Position);

Deliver<bool> MoveWorldSketchPoint(WorldSketchStructure& Declared,
                                  WorldPointName Subject,
                                  const SpatialDirection& Offset);
Deliver<bool> MoveWorldSketchCurve(WorldSketchStructure& Declared,
                                  WorldCurveName Subject,
                                  const SpatialDirection& Offset);
Deliver<bool> MoveWorldSketchLoop(WorldSketchStructure& Declared,
                                 WorldLoopName Subject,
                                 const SpatialDirection& Offset);
Deliver<bool> MoveWorldSketchPick(WorldSketchStructure& Declared,
                                 const WorldPick& Subject,
                                 const SpatialDirection& Offset);

} // namespace Slate
