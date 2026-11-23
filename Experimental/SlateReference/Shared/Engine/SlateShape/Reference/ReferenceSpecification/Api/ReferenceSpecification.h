//============================================================================================================================================
//                                                    REFERENCESPECIFICATION.H
//============================================================================================================================================
// 🧩 One authoring reference into sketch or solid content. A feature names these rather than reaching directly
//    into document/session structures, which keeps the feature graph headless and replayable.

#pragma once

#include <cstdint>

namespace Slate
{

struct FeatureName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct ReferenceName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SketchName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SketchCurveName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SketchPointName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SketchControlName
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct ProfileNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct SolidNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct OccurrenceNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct FaceNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct LoopNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct EdgeNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct EdgeSpanNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

struct VertexNameInFeature
{
    std::uint32_t IssuedIndex = 0u;
    bool Assigned() const { return IssuedIndex != 0u; }
};

enum class ReferenceSubject : std::uint32_t
{
    Sketch = 0u,
    SketchPoint = 1u,
    SketchControl = 2u,
    SketchCurve = 3u,
    Profile = 4u,
    Solid = 5u,
    Occurrence = 6u,
    Face = 7u,
    Loop = 8u,
    Edge = 9u,
    EdgeSpan = 10u,
    Vertex = 11u,
    SubjectCount = 12u
};

enum class ReferenceStanding : std::uint32_t
{
    Resolved = 0u,
    Migrated = 1u,
    Ambiguous = 2u,
    Stale = 3u
};

struct ReferenceSpecification
{
    ReferenceSubject Subject = ReferenceSubject::Sketch;
    SketchName Sketch = {};
    SketchPointName SketchPoint = {};
    SketchControlName SketchControl = {};
    SketchCurveName SketchCurve = {};
    ProfileNameInFeature Profile = {};
    SolidNameInFeature Solid = {};
    OccurrenceNameInFeature Occurrence = {};
    FaceNameInFeature Face = {};
    LoopNameInFeature Loop = {};
    EdgeNameInFeature Edge = {};
    EdgeSpanNameInFeature EdgeSpan = {};
    VertexNameInFeature Vertex = {};

    bool Declared() const;
};

} // namespace Slate
