//============================================================================================================================================
//                                                    PARAMETRICTOOLSSPECIFICATION.H
//============================================================================================================================================
// 🧩 Host-owned CAD construction-catalogue state: the active band/tool, the catalogue/detail slide, and the
//    document facts the tool field filters against.

#pragma once

#include <cstdint>

namespace Slate
{

enum class ParametricToolPage : std::uint32_t
{
    Catalogue = 0u,
    Settings = 1u,
    PageCount = 2u
};

enum class ParametricToolDimension : std::uint32_t
{
    Nothing = 0u,
    Vertex = 1u,
    Edge = 2u,
    Wire = 3u,
    Face = 4u,
    Shell = 5u,
    Solid = 6u,
    DimensionCount = 7u
};

enum class ParametricToolSubject : std::uint32_t
{
    Select = 0u,
    Workplane = 1u,
    Line = 2u,
    Polyline = 3u,
    Rectangle = 4u,
    Circle = 5u,
    Arc = 6u,
    Point = 7u,
    Fillet = 8u,
    Chamfer = 9u,
    Trim = 10u,
    Extend = 11u,
    Offset = 12u,
    Extrude = 13u,
    Revolve = 14u,
    Sweep = 15u,
    Loft = 16u,
    Boss = 17u,
    Interpolate = 18u,
    Approximate = 19u,
    Helix = 20u,
    PlanarFace = 21u,
    FillFace = 22u,
    Union = 23u,
    Cut = 24u,
    LinearArray = 25u,
    Mirror = 26u,
    // 🔴 27u WAS `DatumPlane`, AND IT WAS THE SAME TOOL AS `Workplane`. Every consumer treated the two
    //    identically — `SketchInteraction` tested for either and did one thing, the panel listed both
    //    under one group. The only stated difference was its "Defined By: 3 Points" property, which is a
    //    METHOD OF CONSTRUCTION, not a different subject: a plane through three points is a workplane,
    //    placed a particular way. Shape and method are separate axes, and one tool per method is exactly
    //    the redundancy this pass exists to remove.
    // ⚠️ 27u is deliberately left unused rather than renumbered. These values are written into saved
    //    documents; shifting `DatumAxis` from 28 to 27 would silently reinterpret every stored tool.
    DatumAxis = 28u,
    ImportStep = 29u,
    MeshToSolid = 30u,
    TraceImage = 31u,
    PointLight = 32u,
    Camera = 33u,
    LinearDimension = 34u,
    LeaderNote = 35u,
    Ellipse = 36u,
    HorizontalConstraint = 37u,
    VerticalConstraint = 38u,
    CoincidentConstraint = 39u,
    ParallelConstraint = 40u,
    PerpendicularConstraint = 41u,
    TangentConstraint = 42u,
    EqualConstraint = 43u,
    MidpointConstraint = 44u,
    SymmetryConstraint = 45u,
    ConcentricConstraint = 46u,
    BasisSpline = 47u,
    ConstructionLine = 48u,
    CenterRectangle = 49u,
    ThreePointRectangle = 50u,
    DiameterCircle = 51u,
    ThreePointCircle = 52u,
    CenterStartEndArc = 53u,
    TangentArc = 54u,
    Polygon = 55u,
    Slot = 56u,
    BezierCurve = 57u,
    HermiteCurve = 58u,
    RationalSpline = 59u,
    // 📝 `LinearDimension` at 34u already existed; the other two kinds of dimension did not, so the
    //    Annotation band had no subject to report for its Angular and Radial tiles. Appended rather than
    //    slotted in beside 34u, because these values are written into saved documents and renumbering
    //    would silently reinterpret every stored tool.
    AngularDimension = 60u,
    RadialDimension = 61u,
    SubjectCount = 62u
};

struct ParametricToolsContext
{
    static constexpr std::uint32_t BandLimit = 16u;
    static constexpr std::uint32_t TileLimit = 32u;

    ParametricToolPage Page = ParametricToolPage::Catalogue;
    std::uint32_t ActiveBand = 0u;
    std::uint32_t ActiveTool = 0u;
    ParametricToolSubject ActiveSubject = ParametricToolSubject::Select;
    bool ShowGated = true;

    ParametricToolDimension ActiveDimension = ParametricToolDimension::Nothing;
    std::uint32_t SelectedCount = 0u;
    std::uint32_t ProfileCount = 0u;
    std::uint32_t PerimeterEdgeCount = 0u;
    std::uint32_t ExistingCircleCount = 0u;
    std::uint32_t SolidCount = 0u;

    bool WorkplaneActivation = false;
    bool ClosedProfileCondition = false;
    bool PlanarProfileCondition = true;
    bool AxisAvailability = false;
    bool PathAvailability = false;
    bool UniformClosureCondition = true;
    bool PendingGeometryCondition = false;
    bool SupportMaterialCondition = false;
    bool TangentEndpointCondition = false;
    bool OpeningCondition = false;
    bool ReferencePlaneCondition = false;
    bool SourceImageryCondition = false;
    bool MeasurableCondition = false;

    bool ConstructionGeometry = false;
    // 🔴 WHETHER A SHAPE THAT CLOSES IS A FILLED REGION OR A RUN OF WIRE. A closed polyline drew four
    //    lines that happened to meet, so nothing knew it enclosed anything: it could not be shaded
    //    and could not be extruded or lofted. On, it seals as a profile. Off, it stays open wire --
    //    which is what a sweep path or a construction boundary wants.
    bool ClosedProfileFill = true;
    bool LineLengthAssist = false;
    bool LineAngleAssist = false;
    double LineLength = 100.0;
    double LineAngleDegrees = 0.0;
    bool RectangleDimensionAssist = false;
    double RectangleWidth = 120.0;
    double RectangleHeight = 80.0;
    bool CircleRadiusAssist = false;
    bool CircleDiameterMode = false;
    double CircleRadius = 40.0;
};

const char* ParametricToolDimensionText(ParametricToolDimension Subject);

} // namespace Slate
