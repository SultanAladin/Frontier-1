//============================================================================================================================================
//                                                     WORLDSKETCHPICKING.H
//============================================================================================================================================
// 🧩 Semantic picking over the world-space sketch authoring model: the named points, controls, curves and
//    loops that a pick can answer, and the exact placement/pivot resolution those names need. Selection
//    answers from exact 3D curves and derived planar loops rather than from one global sketch plane, so
//    overlapping planes can coexist in one view.
//
// 🔴 This unit is deliberately camera-free. It answers "what is this name, and where does it stand?" and
//    nothing about where the artist is pointing — the screen-space ray that turns a pointer into one of
//    these names is a viewport concern and lives in
//    `SlateWorkspace/Discipline/WorldSketchPicking/Api/WorldSketchScreenPicking.h`, which may reach this header.

#pragma once

#include "SlateShape/World/WorldSketchStructure/Api/WorldSketchStructure.h"

#include <cstdint>
#include <vector>

namespace Slate
{

enum class WorldControlSubject : std::uint32_t
{
    Centre = 0u,
    Radius = 1u,
    MajorAxis = 2u,
    MinorAxis = 3u,
    Through = 4u,
    ControlPoint = 5u,
    StartTangent = 6u,
    EndTangent = 7u,
    SubjectCount = 8u
};

struct WorldPointName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct WorldControlName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct WorldPointPlacement
{
    WorldPointName Name = {};
    WorldCurveName SourceCurve = {};
    SpatialPoint Position = {};
};

struct WorldControlPlacement
{
    WorldControlName Name = {};
    WorldCurveName SourceCurve = {};
    WorldControlSubject Subject = WorldControlSubject::ControlPoint;
    std::uint32_t LocalIndex = 0u;
    SpatialPoint Position = {};
};

enum class WorldPickSubject : std::uint32_t
{
    None = 0u,
    Point = 1u,
    Control = 2u,
    Curve = 3u,
    Loop = 4u
};

struct WorldPick
{
    WorldPickSubject Subject = WorldPickSubject::None;
    WorldPointName Point = {};
    WorldControlName Control = {};
    WorldCurveName Curve = {};
    WorldLoopName Loop = {};
    SpatialPoint Position = {};

    bool Standing() const { return Subject != WorldPickSubject::None; }
};

bool ResolveWorldSketchPoints(const WorldSketchStructure& Declared,
                             WorldCurveName SourceCurve,
                             std::vector<WorldPointPlacement>& Resolved);
bool ResolveWorldSketchControls(const WorldSketchStructure& Declared,
                               WorldCurveName SourceCurve,
                               std::vector<WorldControlPlacement>& Resolved);

bool ResolveWorldCurvePivot(const WorldSketchStructure& Declared,
                            WorldCurveName Curve,
                            SpatialPoint& Pivot);
bool ResolveWorldLoopPivot(const WorldSketchStructure& Declared,
                           WorldLoopName Loop,
                           SpatialPoint& Pivot);

} // namespace Slate
