//============================================================================================================================================
//                                                       PARAMETRICTOOLSPANEL.CPP
//============================================================================================================================================

#include "SlateUI/Interface/ParametricTools/Api/ParametricToolsPanel.h"

#include <algorithm>
#include <array>
#include <cstdio>

namespace Slate
{

namespace
{

constexpr double HoverOver = 120.0;
constexpr float RunLeading = 1.30f;
constexpr float LeftPaneX = 196.0f;
// 🔴 FOUR BANDS: DRAWING, OPERATIONS, DIMENSIONS, CONSTRAINTS. The annotation tiles were once a single
//    table referenced NOWHERE -- the bands array listed only two, so every one of those tiles was
//    unreachable from the catalogue. Nothing failed to compile and no gate went red, because a table
//    nobody indexes into is still perfectly valid C++. They are now reachable AND split, because a
//    dimension and a constraint are different kinds of statement about a drawing.
constexpr std::uint32_t BandCount = 4u;
constexpr std::uint32_t PresetCount = 9u;

struct OptionEntry
{
    const char* Label = "";
    const char* Value = "";
};

struct ToolEntry
{
    const char* Naming = "";
    const char* Accelerator = "";
    SymbolSubject Glyph = SymbolSubject::PlaceholderMark;
    ParametricToolDimension MinimumDimension = ParametricToolDimension::Nothing;
    bool Raising = false;
    bool Workplane = false;
    bool Closed = false;
    bool Planar = false;
    bool Axis = false;
    bool Path = false;
    bool Uniform = false;
    bool Pending = false;
    bool Support = false;
    bool Tangent = false;
    bool Opening = false;
    bool Reference = false;
    bool Imagery = false;
    bool Measurable = false;
    std::uint32_t MinimumSelection = 0u;
    std::uint32_t MinimumProfile = 0u;
    std::uint32_t MinimumSolid = 0u;
    std::uint32_t ExactSolid = 0u;
    std::uint32_t MinimumCircle = 0u;
    std::uint32_t MinimumPerimeter = 0u;
    std::uint32_t MaximumPerimeter = 0u;
    OptionEntry Options[6] = {};
    std::uint32_t OptionCount = 0u;
};

struct BandEntry
{
    const char* Naming = "";
    SymbolSubject Glyph = SymbolSubject::PlaceholderMark;
    const ToolEntry* Tools = nullptr;
    std::uint32_t ToolCount = 0u;
};

struct PresetEntry
{
    const char* Naming = "";
    const char* Note = "";
};

constexpr ThemeToken Faded(ThemeToken Declared, float Fraction)
{
    const float Held = Fraction < 0.0f ? 0.0f : (Fraction > 1.0f ? 1.0f : Fraction);
    Declared.Opacity = static_cast<std::uint8_t>(static_cast<float>(Declared.Opacity) * Held + 0.5f);
    return Declared;
}

[[maybe_unused]] constexpr std::uint32_t DimensionValue(ParametricToolDimension Subject)
{
    return static_cast<std::uint32_t>(Subject);
}

ParametricToolDimension RaisedDimension(ParametricToolDimension Subject, bool Closed)
{
    switch (Subject)
    {
        case ParametricToolDimension::Vertex: return ParametricToolDimension::Edge;
        case ParametricToolDimension::Edge:   return ParametricToolDimension::Face;
        case ParametricToolDimension::Wire:   return Closed ? ParametricToolDimension::Solid
                                                            : ParametricToolDimension::Shell;
        case ParametricToolDimension::Face:   return ParametricToolDimension::Solid;
        case ParametricToolDimension::Shell:  return Closed ? ParametricToolDimension::Solid
                                                            : ParametricToolDimension::Nothing;
        case ParametricToolDimension::Solid:
        case ParametricToolDimension::Nothing:
        case ParametricToolDimension::DimensionCount:
            return ParametricToolDimension::Nothing;
    }
    return ParametricToolDimension::Nothing;
}

const ToolEntry SolidPrimitiveTools[] =
{
    { "Box", "⇧B", SymbolSubject::CubeSolid, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Length X", "100 mm" }, { "Length Y", "100 mm" }, { "Height Z", "100 mm" },
        { "Placement", "Corner" }, { "Align", "Free" }, { "Snap", "Grid" } }, 6u },

    { "Cylinder", "⇧C", SymbolSubject::CubeSolid, ParametricToolDimension::Nothing,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "50 mm" }, { "Height", "200 mm" }, { "Sweep", "360°" },
        { "Axis", "Z" }, { "Snap", "Grid" } }, 5u },

    { "Sphere", "⇧S", SymbolSubject::CubeSolid, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "80 mm" }, { "Extent", "Full" }, { "Snap", "Grid" } }, 3u },

    { "Cone", "", SymbolSubject::CubeSolid, ParametricToolDimension::Nothing,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "60 mm" }, { "Height", "180 mm" }, { "Sweep", "360°" }, { "Axis", "Z" } }, 4u },

    { "Tube", "", SymbolSubject::CubeSolid, ParametricToolDimension::Nothing,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Outer Radius", "50 mm" }, { "Inner Radius", "38 mm" }, { "Height", "200 mm" }, { "Axis", "Z" } }, 4u },
};

const ToolEntry SketchDrawTools[] =
{
    { "Line", "L", SymbolSubject::EdgeSegment, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Length", "100 mm" }, { "Angle", "0°" }, { "Construction", "Off" }, { "Snap", "Grid / Endpoint" } }, 4u },

    { "Polyline", "⇧L", SymbolSubject::EdgeSegment, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Close on Finish", "Off" }, { "Segment", "Line" }, { "Snap", "Grid / Endpoint" } }, 3u },

    { "Rectangle", "R", SymbolSubject::FacePlanar, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Width", "120 mm" }, { "Height", "80 mm" }, { "Corner Radius", "0 mm" }, { "Construction", "Off" } }, 4u },

    { "Circle", "C", SymbolSubject::ConstraintDimension, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "40 mm" }, { "Defined By", "Centre" }, { "Construction", "Off" } }, 3u },

    { "Arc", "A", SymbolSubject::ConstraintDimension, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "50 mm" }, { "Included Angle", "90°" }, { "Direction", "CCW" } }, 3u },

    { "Ellipse", "E", SymbolSubject::ConstraintDimension, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Defined By", "Centre + corner" }, { "Construction", "Off" } }, 2u },

    { "Point", "", SymbolSubject::VertexPoint, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Snap", "Grid" }, { "Construction", "On" } }, 2u },

    { "Bezier", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Controls", "2+" }, { "Finish", "double click" } }, 2u },

    { "Hermite", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Inputs", "start/end/tangents" }, { "Finish", "double click" } }, 2u },

    { "Basis Spline", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Degree", "3" }, { "Finish", "double click" } }, 2u },

    { "NURBS Curve", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Weights", "uniform" }, { "Finish", "double click" } }, 2u },

    { "Construction Line", "", SymbolSubject::EdgeSegment, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Semantic", "construction" } }, 1u },






    { "Tangent Arc", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, true, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Defined By", "tangent chain" } }, 1u },

    { "Polygon", "", SymbolSubject::FacePlanar, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Sides", "6" }, { "Defined By", "centre + radius" } }, 2u },

    { "Slot", "", SymbolSubject::FacePlanar, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Defined By", "endpoints + radius" } }, 1u },
};

const ToolEntry SketchModifyTools[] =
{
    { "Fillet", "", SymbolSubject::FilletRadius, ParametricToolDimension::Vertex,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "5 mm" }, { "Trim Originals", "On" } }, 2u },

    { "Chamfer", "", SymbolSubject::BevelChamfer, ParametricToolDimension::Vertex,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Distance", "5 mm" }, { "Angle", "45°" } }, 2u },

    { "Trim", "", SymbolSubject::CrosshairCentre, ParametricToolDimension::Edge,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Extent", "To nearest" } }, 1u },

    { "Extend", "", SymbolSubject::CrosshairCentre, ParametricToolDimension::Edge,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Distance", "20 mm" }, { "Extend to Limit", "On" } }, 2u },

    { "Offset", "", SymbolSubject::CrosshairCentre, ParametricToolDimension::Edge,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Distance", "5 mm" }, { "Corners", "Arc" }, { "Both Sides", "Off" } }, 3u },

    // 🔴 CUT WAS NAMED IN THE CATALOGUE'S ENUMERATION AND HAD NO TILE. `ParametricToolSubject::Cut`
    //    exists, `CutCurve` and `CutProfile` implement it, and the popup arm already applies it without
    //    asking a parameter — but the artist had no way to reach any of that, because no band listed it.
    //    It belongs beside Trim: both divide existing geometry, and an artist looking for one looks here.
    // 📝 TARGET THE EDGE, THEN CUT AT THE POINTER. The split point comes from the probe on the hovered or
    //    selected curve itself, so the catalogue should ask for the curve the artist means to divide.
    { "Cut", "", SymbolSubject::CrosshairCentre, ParametricToolDimension::Edge,
      false, true, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "At", "Picked edge" } }, 1u },
};

const ToolEntry SweepTools[] =
{
    { "Extrude", "E", SymbolSubject::ExtrudeSpan, ParametricToolDimension::Wire,
      true, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Length", "50 mm" }, { "Direction", "Normal" }, { "Taper", "0°" }, { "Along", "Free" } }, 4u },

    { "Revolve", "⇧R", SymbolSubject::RevolveAxis, ParametricToolDimension::Wire,
      true, false, false, false, true, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Angle", "360°" }, { "Axis", "Mandatory" }, { "Direction", "CCW" } }, 3u },

    { "Sweep", "", SymbolSubject::ExtrudeSpan, ParametricToolDimension::Wire,
      true, false, false, false, false, true, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Orientation", "Corrected" }, { "Scale to Path", "Off" }, { "Tolerance", "0.01 mm" } }, 3u },

    { "Loft", "", SymbolSubject::LoftProfile, ParametricToolDimension::Wire,
      true, false, false, false, false, false, true, false, false, false, false, false, false, false,
      2u, 2u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "2 profiles" }, { "Ruled", "Off" }, { "Close Loop", "Off" } }, 3u },

    { "Boss", "", SymbolSubject::ExtrudeSpan, ParametricToolDimension::Wire,
      true, false, false, false, false, false, false, false, true, false, false, false, false, false,
      1u, 0u, 1u, 0u, 0u, 0u, 0u,
      { { "Length", "20 mm" }, { "Extent", "Blind" }, { "Taper", "0°" } }, 3u },
};

const ToolEntry CurveTools[] =
{
    { "Interpolate", "", SymbolSubject::CodeBrackets, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Degree", "3" }, { "Periodic", "Off" }, { "End Tangent", "Off" } }, 3u },

    { "Approximate", "", SymbolSubject::CurveTangent, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      3u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Tolerance", "0.1 mm" }, { "Max Degree", "3" }, { "Continuity", "C2" } }, 3u },

    { "Helix", "", SymbolSubject::CurveTangent, ParametricToolDimension::Nothing,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Radius", "40 mm" }, { "Pitch", "10 mm" }, { "Turns", "5" }, { "Axis", "Z" } }, 4u },

    { "Offset Curve", "", SymbolSubject::CurveTangent, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Distance", "5 mm" }, { "Corners", "Arc" } }, 2u },
};

const ToolEntry SurfaceTools[] =
{
    { "Planar Face", "", SymbolSubject::FacePlanar, ParametricToolDimension::Wire,
      false, false, true, true, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Tolerance", "0.01 mm" } }, 1u },

    { "Fill Perimeter", "", SymbolSubject::FacePlanar, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 2u, 4u,
      { { "Continuity", "G1" }, { "Tolerance", "0.01 mm" } }, 2u },

    { "Offset Surface", "", SymbolSubject::FacePlanar, ParametricToolDimension::Face,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Distance", "2 mm" }, { "Keep Original", "Off" }, { "Tolerance", "0.01 mm" } }, 3u },

    { "Sew Faces", "", SymbolSubject::FacePlanar, ParametricToolDimension::Face,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Sew Tolerance", "0.01 mm" }, { "Make Solid if Closed", "On" }, { "Report Open Edges", "On" } }, 3u },
};

const ToolEntry BooleanTools[] =
{
    { "Union", "⇧U", SymbolSubject::BooleanUnion, ParametricToolDimension::Solid,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 2u, 0u, 0u, 0u, 0u,
      { { "Keep Operands", "Off" }, { "Refine Result", "On" }, { "Fuzzy Tolerance", "0 mm" } }, 3u },

    { "Cut", "⇧X", SymbolSubject::BooleanUnion, ParametricToolDimension::Solid,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 2u, 0u, 0u, 0u,
      { { "Subtract", "Second from First" }, { "Keep Operands", "Off" } }, 2u },

    { "Intersect", "⇧I", SymbolSubject::BooleanUnion, ParametricToolDimension::Solid,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 2u, 0u, 0u, 0u, 0u,
      { { "Keep Operands", "Off" }, { "Refine Result", "On" } }, 2u },
};

const ToolEntry PatternTools[] =
{
    { "Linear Array", "⇧A", SymbolSubject::MirrorAxis, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Count", "5" }, { "Spacing", "50 mm" }, { "Direction", "X" } }, 3u },

    { "Grid Array", "", SymbolSubject::MirrorAxis, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Count X", "4" }, { "Count Y", "3" }, { "Spacing X", "50 mm" }, { "Spacing Y", "50 mm" } }, 4u },

    { "Polar Array", "", SymbolSubject::MirrorAxis, ParametricToolDimension::Vertex,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Count", "8" }, { "Total Angle", "360°" }, { "Axis", "Z" }, { "Rotate Copies", "On" } }, 4u },

    { "Mirror", "⇧M", SymbolSubject::MirrorAxis, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, true, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Mirror Plane", "Reference" }, { "Keep Source", "On" } }, 2u },
};

const ToolEntry ReferenceTools[] =
{
    { "Workplane", "⇧W", SymbolSubject::SketchPlane, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Base", "XY" }, { "Defined By", "Offset" }, { "Offset", "0 mm" }, { "Rotation", "0°" } }, 4u },

    { "Datum Axis", "", SymbolSubject::CrosshairCentre, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Defined By", "2 Points" }, { "Direction", "Free" } }, 2u },
};

const ToolEntry DataTools[] =
{
    { "Import STEP", "", SymbolSubject::FolderClosed, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Units", "From File" }, { "Sew on Import", "On" }, { "Tolerance", "0.01 mm" } }, 3u },

    { "Mesh to Solid", "", SymbolSubject::FolderClosed, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, true, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Tolerance", "0.1 mm" }, { "Merge Coplanar", "On" } }, 2u },

    { "Trace Image", "", SymbolSubject::MagnifierLens, ParametricToolDimension::Nothing,
      false, true, false, false, false, false, false, false, false, false, false, false, true, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Trace Tolerance", "1 mm" }, { "Output", "Spline" } }, 2u },
};

const ToolEntry IlluminationTools[] =
{
    { "Point Light", "", SymbolSubject::BulbFilament, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Intensity", "1000 lm" }, { "Radius", "10 mm" }, { "Colour", "Swatch" } }, 3u },

    { "Sun Light", "", SymbolSubject::SunDirectional, ParametricToolDimension::Nothing,
      false, false, false, false, true, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Intensity", "100 klx" }, { "Elevation", "45°" }, { "Direction", "Axis" } }, 3u },

    { "Camera", "", SymbolSubject::CameraAperture, ParametricToolDimension::Nothing,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      0u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Focal Length", "50 mm" }, { "Projection", "Perspective" }, { "Show Frustum", "On" } }, 3u },
};

/// 🔴 DIMENSIONS AND CONSTRAINTS ARE TWO CATALOGUES, NOT ONE. A dimension states what a size IS and can
///    drive the geometry to it; a constraint states a relation that must HOLD and drives nothing on its
///    own. Filing them under a single "Annotation" heading put fifteen tiles in one list and asked the
///    artist to remember which of them would move the drawing.
const ToolEntry DimensionTools[] =
{
    { "Linear Dim.", "D", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Along", "Aligned" }, { "Precision", "2 dp" }, { "Show Unit", "On" } }, 3u },

    { "Angular Dim.", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, true, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Precision", "1 dp" }, { "Measure", "Interior" } }, 2u },

    { "Radial Dim.", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, true, false,
      1u, 0u, 0u, 0u, 1u, 0u, 0u,
      { { "Precision", "2 dp" }, { "Centre Mark", "On" } }, 2u },
};

const ToolEntry ConstraintTools[] =
{
    { "Horizontal", "H", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Applies", "selected line" } }, 1u },

    { "Vertical", "V", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Applies", "selected line" } }, 1u },

    { "Coincident", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "two points" } }, 1u },

    { "Parallel", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "two lines" } }, 1u },

    { "Perpendicular", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "two lines" } }, 1u },

    { "Tangent", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, true, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "line + curve" } }, 1u },

    { "Equal", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Needs", "two like curves" } }, 1u },

    { "Midpoint", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Vertex,
      false, false, false, false, false, false, false, false, false, false, false, false, false, false,
      1u, 0u, 0u, 0u, 0u, 0u, 0u,
      { { "Planned", "point on centre" } }, 1u },

    { "Symmetry", "", SymbolSubject::MirrorAxis, ParametricToolDimension::Vertex,
      false, false, false, false, true, false, false, false, false, false, false, true, false, false,
      2u, 0u, 0u, 0u, 0u, 0u, 1u,
      { { "Planned", "mirror about line" } }, 1u },

    { "Concentric", "", SymbolSubject::ConstraintDimension, ParametricToolDimension::Edge,
      false, false, false, false, false, false, false, false, false, true, false, false, false, false,
      2u, 0u, 0u, 0u, 2u, 0u, 0u,
      { { "Planned", "shared centre" } }, 1u },
};

const BandEntry Bands[BandCount] =
{
    { "2DPrimitives", SymbolSubject::SketchPlane, SketchDrawTools,
      static_cast<std::uint32_t>(sizeof(SketchDrawTools) / sizeof(SketchDrawTools[0])) },
    { "Operations", SymbolSubject::FilletRadius, SketchModifyTools,
      static_cast<std::uint32_t>(sizeof(SketchModifyTools) / sizeof(SketchModifyTools[0])) },
    { "Dimensions", SymbolSubject::ConstraintDimension, DimensionTools,
      static_cast<std::uint32_t>(sizeof(DimensionTools) / sizeof(DimensionTools[0])) },
    { "Constraints", SymbolSubject::ConstraintDimension, ConstraintTools,
      static_cast<std::uint32_t>(sizeof(ConstraintTools) / sizeof(ConstraintTools[0])) },
};

const PresetEntry Presets[PresetCount] =
{
    { "Empty document", "nothing selected • no workplane" },
    { "Workplane set", "sketch bands wake" },
    { "Open wire", "raising tools stay live as shell" },
    { "Closed wire", "raising tools now resolve to solid" },
    { "Planar face", "the clean solid-raising case" },
    { "Two profiles", "loft and ruled sweep reach their counts" },
    { "Open shell", "needs sewing before it closes" },
    { "One solid", "profile sweep vanishes — no successor" },
    { "Two solids", "boolean operations reach their counts" },
};

struct ToolTally
{
    std::uint32_t Available = 0u;
    std::uint32_t Gated = 0u;
    std::uint32_t Hidden = 0u;
    std::uint32_t Total = 0u;
};

[[maybe_unused]] bool DimensionAccepted(const ToolEntry& Tool, ParametricToolDimension Active)
{
    static_cast<void>(Tool);
    static_cast<void>(Active);
    return true;
}

bool Gated(const ToolEntry& Tool, const ParametricToolsContext& Applied)
{
    static_cast<void>(Tool);
    static_cast<void>(Applied);
    return false;
}

[[maybe_unused]] bool Hidden(const ToolEntry& Tool, const ParametricToolsContext& Applied)
{
    static_cast<void>(Tool);
    static_cast<void>(Applied);
    return false;
}

bool Presented(const ToolEntry& Tool, const ParametricToolsContext& Applied)
{
    static_cast<void>(Tool);
    static_cast<void>(Applied);
    return true;
}

ParametricToolSubject ToolSubjectOf(std::uint32_t BandIndex, std::uint32_t ToolIndex)
{
    switch (BandIndex)
    {
        case 0u:
            return ToolIndex == 0u ? ParametricToolSubject::Line
                 : ToolIndex == 1u ? ParametricToolSubject::Polyline
                 : ToolIndex == 2u ? ParametricToolSubject::Rectangle
                 : ToolIndex == 3u ? ParametricToolSubject::Circle
                 : ToolIndex == 4u ? ParametricToolSubject::Arc
                 : ToolIndex == 5u ? ParametricToolSubject::Ellipse
                 : ToolIndex == 6u ? ParametricToolSubject::Point
                 : ToolIndex == 7u ? ParametricToolSubject::BezierCurve
                 : ToolIndex == 8u ? ParametricToolSubject::HermiteCurve
                 : ToolIndex == 9u ? ParametricToolSubject::BasisSpline
                 : ToolIndex == 10u ? ParametricToolSubject::RationalSpline
                 : ToolIndex == 11u ? ParametricToolSubject::ConstructionLine
                 : ToolIndex == 12u ? ParametricToolSubject::TangentArc
                 : ToolIndex == 13u ? ParametricToolSubject::Polygon
                                    : ParametricToolSubject::Slot;
        case 1u:
            return ToolIndex == 0u ? ParametricToolSubject::Fillet
                 : ToolIndex == 1u ? ParametricToolSubject::Chamfer
                 : ToolIndex == 2u ? ParametricToolSubject::Trim
                 : ToolIndex == 3u ? ParametricToolSubject::Extend
                 : ToolIndex == 4u ? ParametricToolSubject::Offset
                                    : ParametricToolSubject::Cut;
        // 🔴 THESE CASES WERE MISSING, AND THAT WAS HALF THE DEFECT. The annotation tiles existed and no
        //    band listed them, so they could not be reached; and even once reached, every tile fell
        //    through to `default` and reported `Select`. Both halves had to be wrong for the symptom to
        //    be "the dimension tools do nothing" rather than a crash.
        // 📝 Band 2 is the dimensions, band 3 the constraints. The indices restart at zero per band.
        case 2u:
            return ToolIndex == 0u ? ParametricToolSubject::LinearDimension
                 : ToolIndex == 1u ? ParametricToolSubject::AngularDimension
                                   : ParametricToolSubject::RadialDimension;
        case 3u:
            return ToolIndex == 0u  ? ParametricToolSubject::HorizontalConstraint
                 : ToolIndex == 1u  ? ParametricToolSubject::VerticalConstraint
                 : ToolIndex == 2u  ? ParametricToolSubject::CoincidentConstraint
                 : ToolIndex == 3u  ? ParametricToolSubject::ParallelConstraint
                 : ToolIndex == 4u  ? ParametricToolSubject::PerpendicularConstraint
                 : ToolIndex == 5u  ? ParametricToolSubject::TangentConstraint
                 : ToolIndex == 6u  ? ParametricToolSubject::EqualConstraint
                 : ToolIndex == 7u  ? ParametricToolSubject::MidpointConstraint
                 : ToolIndex == 8u  ? ParametricToolSubject::SymmetryConstraint
                                    : ParametricToolSubject::ConcentricConstraint;
        default:
            return ParametricToolSubject::Select;
    }
}

bool ToolStands(const ParametricToolsContext& Applied,
                std::uint32_t BandIndex,
                std::uint32_t ToolIndex)
{
    return Applied.ActiveSubject != ParametricToolSubject::Select
        && Applied.ActiveBand == BandIndex
        && Applied.ActiveSubject == ToolSubjectOf(BandIndex, ToolIndex);
}

const ToolEntry* ActiveTool(const ParametricToolsContext& Applied)
{
    if (Applied.ActiveBand >= BandCount)
        return nullptr;
    const BandEntry& Band = Bands[Applied.ActiveBand];
    if (Applied.ActiveTool >= Band.ToolCount)
        return nullptr;
    return &Band.Tools[Applied.ActiveTool];
}

std::uint32_t LiveCount(const BandEntry& Band, const ParametricToolsContext& Applied)
{
    static_cast<void>(Applied);
    return Band.ToolCount;
}

std::uint32_t VisibleCount(const BandEntry& Band, const ParametricToolsContext& Applied)
{
    static_cast<void>(Applied);
    return Band.ToolCount;
}

const char* ShortfallText(const ToolEntry& Tool, const ParametricToolsContext& Applied)
{
    static_cast<void>(Tool);
    static_cast<void>(Applied);
    return "";
}

const char* ResultText(const ToolEntry& Tool, const ParametricToolsContext& Applied)
{
    if (!Tool.Raising)
        return nullptr;
    const ParametricToolDimension Result = RaisedDimension(Applied.ActiveDimension,
                                                           Applied.ClosedProfileCondition);
    if (Result == ParametricToolDimension::Nothing)
        return nullptr;
    if (Result == ParametricToolDimension::Shell)
        return "-> shell";
    if (Result == ParametricToolDimension::Solid)
        return "-> solid";
    return nullptr;
}

ToolTally ResolveTally(const ParametricToolsContext& Applied)
{
    static_cast<void>(Applied);
    ToolTally Tally = {};
    for (const BandEntry& Band : Bands)
        Tally.Total += Band.ToolCount;
    Tally.Available = Tally.Total;
    return Tally;
}

std::uint32_t FirstPresentedBand(const ParametricToolsContext& Applied)
{
    static_cast<void>(Applied);
    return 0u;
}

void ApplyPreset(std::uint32_t Index, ParametricToolsContext& Applied)
{
    Applied = ParametricToolsContext{};
    switch (Index)
    {
        case 0u:
            Applied.ActiveDimension = ParametricToolDimension::Nothing;
            Applied.WorkplaneActivation = false;
            break;
        case 1u:
            Applied.ActiveDimension = ParametricToolDimension::Nothing;
            Applied.WorkplaneActivation = true;
            Applied.ReferencePlaneCondition = true;
            break;
        case 2u:
            Applied.ActiveDimension = ParametricToolDimension::Wire;
            Applied.SelectedCount = 1u;
            Applied.ProfileCount = 1u;
            Applied.PerimeterEdgeCount = 5u;
            Applied.WorkplaneActivation = true;
            Applied.AxisAvailability = true;
            Applied.PathAvailability = true;
            Applied.MeasurableCondition = true;
            break;
        case 3u:
            Applied.ActiveDimension = ParametricToolDimension::Wire;
            Applied.SelectedCount = 1u;
            Applied.ProfileCount = 1u;
            Applied.PerimeterEdgeCount = 5u;
            Applied.WorkplaneActivation = true;
            Applied.AxisAvailability = true;
            Applied.PathAvailability = true;
            Applied.MeasurableCondition = true;
            Applied.ClosedProfileCondition = true;
            break;
        case 4u:
            Applied.ActiveDimension = ParametricToolDimension::Face;
            Applied.SelectedCount = 1u;
            Applied.ProfileCount = 1u;
            Applied.WorkplaneActivation = true;
            Applied.AxisAvailability = true;
            Applied.PathAvailability = true;
            Applied.MeasurableCondition = true;
            Applied.SupportMaterialCondition = true;
            Applied.ClosedProfileCondition = true;
            Applied.PlanarProfileCondition = true;
            break;
        case 5u:
            Applied.ActiveDimension = ParametricToolDimension::Wire;
            Applied.SelectedCount = 2u;
            Applied.ProfileCount = 2u;
            Applied.PerimeterEdgeCount = 6u;
            Applied.WorkplaneActivation = true;
            Applied.AxisAvailability = true;
            Applied.PathAvailability = true;
            Applied.MeasurableCondition = true;
            Applied.ClosedProfileCondition = true;
            Applied.UniformClosureCondition = true;
            break;
        case 6u:
            Applied.ActiveDimension = ParametricToolDimension::Shell;
            Applied.SelectedCount = 1u;
            Applied.WorkplaneActivation = true;
            Applied.SupportMaterialCondition = true;
            Applied.MeasurableCondition = true;
            Applied.OpeningCondition = true;
            break;
        case 7u:
            Applied.ActiveDimension = ParametricToolDimension::Solid;
            Applied.SelectedCount = 1u;
            Applied.SolidCount = 1u;
            Applied.WorkplaneActivation = true;
            Applied.SupportMaterialCondition = true;
            Applied.MeasurableCondition = true;
            Applied.AxisAvailability = true;
            Applied.ReferencePlaneCondition = true;
            Applied.ClosedProfileCondition = true;
            break;
        case 8u:
            Applied.ActiveDimension = ParametricToolDimension::Solid;
            Applied.SelectedCount = 2u;
            Applied.SolidCount = 2u;
            Applied.WorkplaneActivation = true;
            Applied.SupportMaterialCondition = true;
            Applied.MeasurableCondition = true;
            Applied.AxisAvailability = true;
            Applied.ReferencePlaneCondition = true;
            Applied.ClosedProfileCondition = true;
            break;
        default:
            break;
    }
}

} // namespace

Deliver<bool> ParametricToolsPanel::ConstructParametricToolsPanel(ControlIndex& IncomingInteraction,
                                                                  MotionIntegrator& Integrator,
                                                                  RecordingSurface& IncomingSurface,
                                                                  const ThemeProfile& Resolved)
{
    if (Interaction != nullptr)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported,
                                       "the parametric tools panel is already constructed" });

    Interaction = &IncomingInteraction;
    Motion = &Integrator;
    Surface = &IncomingSurface;
    Appearance = &Resolved;

    if (!Pages.ConstructSlidingPages(Integrator, 0u).Resolved)
    {
        Reset();
        return Deliver<bool>::Refuse({ RefusalReason::CapabilityAbsent,
                                       "the construction-menu slide travel was rejected" });
    }

    const Deliver<ControlIdentity> Back = IncomingInteraction.Register();
    if (!Back.Resolved)
        return Deliver<bool>::Refuse(Back.Error);
    BackCall = Back.Resolve();

    for (std::uint32_t Index = 0u; Index < ParametricToolsContext::BandLimit; ++Index)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        BandRows[Index] = Registered.Resolve();
    }

    for (std::uint32_t Index = 0u; Index < ParametricToolsContext::TileLimit; ++Index)
    {
        const Deliver<ControlIdentity> Registered = IncomingInteraction.Register();
        if (!Registered.Resolved)
            return Deliver<bool>::Refuse(Registered.Error);
        TileRows[Index] = Registered.Resolve();
    }

    Reapply(Resolved);
    return Deliver<bool>::Result(true);
}

void ParametricToolsPanel::Advance(const PointerCondition& Contact, double Elapsed,
                                   ParametricToolsContext& Applied, bool TabPressed)
{
    static_cast<void>(Elapsed);
    Sampled = Contact;
    if (TabPressed)
        Applied.Page = Applied.Page == ParametricToolPage::Catalogue
                     ? ParametricToolPage::Settings : ParametricToolPage::Catalogue;
    if (Applied.ActiveBand >= BandCount || VisibleCount(Bands[Applied.ActiveBand], Applied) == 0u)
        Applied.ActiveBand = FirstPresentedBand(Applied);
    if (Applied.ActiveTool >= Bands[Applied.ActiveBand].ToolCount)
        Applied.ActiveTool = 0u;
}

void ParametricToolsPanel::Reapply(const ThemeProfile& Resolved)
{
    Appearance = &Resolved;
    Tinted = Resolved.Shell;
    const float Applied = static_cast<float>(Resolved.Measure.DisplayScale)
                        * Resolved.ControlMeasure.ArtistFactor;
    Scaled = ScaleShellLengths(Applied);
}

void ParametricToolsPanel::Reset()
{
    Pages.Reset();
    RailOverflow.Reset();
    GridOverflow.Reset();
    ProbeOverflow.Reset();
    OptionOverflow.Reset();
    Interaction = nullptr;
    Motion = nullptr;
    Surface = nullptr;
    Appearance = nullptr;
    Sampled = {};
    Tinted = {};
    Scaled = {};
}

void ParametricToolsPanel::RecordLeafHeader(const PlaneExtent& Extent, SymbolSubject Glyph,
                                            const ThemeToken& Hue, const char* Titled, const char* Secondary)
{
    Surface->Ground(Extent, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Extent.MinimumX, Extent.MaximumY - 1.0f, Extent.Width(), 1.0f),
                    Tinted.Hairline, 0.0f, CornerNone);

    const float Pad = Scaled.HeaderPadX;
    const float Medallion = Scaled.MedallionExtent;
    const PlaneExtent Crest = Spanning(Extent.MinimumX + Pad,
                                       Extent.MinimumY + (Extent.Height() - Medallion) * 0.5f,
                                       Medallion, Medallion);
    Surface->Ground(Crest, Hue, 6.0f, CornerAll);
    const float Figure = Medallion * 0.62f;
    Surface->Stroke(Glyph,
                    Spanning(Crest.MinimumX + (Medallion - Figure) * 0.5f,
                             Crest.MinimumY + (Medallion - Figure) * 0.5f,
                             Figure, Figure), Covering(0xFFFFFFu));

    const bool HasSecondary = Secondary != nullptr && Secondary[0] != '\0';
    const float PrimaryRun = Scaled.RunPrimary;
    const float SecondaryRun = Scaled.RunFine;
    const float PairHeight = HasSecondary ? (PrimaryRun * RunLeading + SecondaryRun * RunLeading)
                                          : PrimaryRun;
    const float PairLead = Extent.MinimumY + (Extent.Height() - PairHeight) * 0.5f;
    const float RunLead = Crest.MaximumX + Pad;
    Surface->TextRunTruncated(RunLead, PairLead, Extent.MaximumX - RunLead - Pad,
                              Tinted.Primary, Titled, PrimaryRun, true);
    if (HasSecondary)
        Surface->TextRunTruncated(RunLead, PairLead + PrimaryRun * RunLeading,
                                  Extent.MaximumX - RunLead - Pad,
                                  Hue, Secondary, SecondaryRun);
}

void ParametricToolsPanel::RecordBrowsePage(const PlaneExtent& Extent, ParametricToolsContext& Applied)
{
    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);

    if (Applied.ActiveBand >= BandCount || VisibleCount(Bands[Applied.ActiveBand], Applied) == 0u)
        Applied.ActiveBand = FirstPresentedBand(Applied);

    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, SymbolSubject::SketchPlane, Tinted.EntityAccent,
                     "Construction Catalogue", "2D primitives and operations");

    const float LeftWidth = std::min(LeftPaneX, Extent.Width() * 0.40f);
    const PlaneExtent Rail = Spanning(Extent.MinimumX, Header.MaximumY,
                                      LeftWidth, Extent.Height() - Scaled.HeaderHeight - Scaled.FooterHeight);
    const PlaneExtent Grid = Spanning(Rail.MaximumX, Header.MaximumY,
                                      Extent.MaximumX - Rail.MaximumX, Rail.Height());
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);

    Surface->Ground(Spanning(Rail.MaximumX, Rail.MinimumY, 1.0f, Rail.Height()), Tinted.Hairline);

    const float RailScroll = RailOverflow.Advance(Sampled, Rail, BandCount * 34.0f + 20.0f, 56.0f);
    Surface->Confine(Rail);
    float RailY = Rail.MinimumY + 8.0f - RailScroll;
    for (std::uint32_t Index = 0u; Index < BandCount && Index < ParametricToolsContext::BandLimit; ++Index)
    {
        const BandEntry& Band = Bands[Index];
        const std::uint32_t Live = LiveCount(Band, Applied);
        const std::uint32_t Visible = VisibleCount(Band, Applied);
        if (Visible == 0u)
            continue;

        const PlaneExtent Row = Spanning(Rail.MinimumX + 8.0f, RailY, Rail.Width() - 16.0f, 32.0f);
        RailY += 33.0f;
        if (Surface->Excluded(Row))
            continue;

        const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
        const bool Current = Applied.ActiveBand == Index;
        if (Hovered && Sampled.ContactPressed)
            Interaction->Grab(BandRows[Index], ControlPart::Body);
        if (Hovered && Interaction->Released(BandRows[Index]))
        {
            Applied.ActiveBand = Index;
            Applied.ActiveTool = 0u;
            Applied.ActiveSubject = ToolSubjectOf(Index, 0u);
        }
        Interaction->DeclareHovered(BandRows[Index], Hovered, HoverOver);

        if (Current)
            Surface->Ground(Row, Tinted.EntityTaken, Scaled.FieldRadius, CornerAll);
        else if (Hovered)
            Surface->Ground(Row, Tinted.RowHovered, Scaled.FieldRadius, CornerAll);

        Surface->Stroke(Band.Glyph,
                        Spanning(Row.MinimumX + 6.0f, Row.MinimumY + 7.0f, 18.0f, 18.0f),
                        Current ? Tinted.Primary : Tinted.Muted);
        Surface->TextRun(Row.MinimumX + 32.0f,
                         Row.MinimumY + (Row.Height() - Scaled.RunPrimary) * 0.5f,
                         Current ? Tinted.Primary : Tinted.Muted,
                         Band.Naming, Scaled.RunPrimary);

        char Counted[32] = {};
        std::snprintf(Counted, sizeof(Counted), "%u", static_cast<unsigned>(Live));
        Surface->TextRun(Row.MaximumX - 18.0f, Row.MinimumY + (Row.Height() - Scaled.RunFine) * 0.5f,
                         Live == 0u ? Tinted.Faint : Tinted.Primary, Counted, Scaled.RunFine);
        if (Visible > Live)
        {
            char Gated[32] = {};
            std::snprintf(Gated, sizeof(Gated), "+%u", static_cast<unsigned>(Visible - Live));
            Surface->TextRun(Row.MaximumX - 40.0f, Row.MinimumY + (Row.Height() - Scaled.RunFiner) * 0.5f,
                             Covering(0xF59E0Bu), Gated, Scaled.RunFiner);
        }
    }
    Surface->Release();

    const BandEntry& CurrentBand = Bands[Applied.ActiveBand < BandCount ? Applied.ActiveBand : 0u];
    const std::uint32_t Visible = VisibleCount(CurrentBand, Applied);
    const float GridScroll = GridOverflow.Advance(Sampled, Grid,
                          (Visible == 0u ? 120.0f : (static_cast<float>(Visible + 3u) / 4.0f) * 86.0f + 18.0f), 72.0f);
    Surface->Confine(Grid);

    const PlaneExtent GridHead = Spanning(Grid.MinimumX, Grid.MinimumY, Grid.Width(), 53.0f);
    char Secondary[96] = {};
    std::snprintf(Secondary, sizeof(Secondary), "%u of %u available",
                  static_cast<unsigned>(LiveCount(CurrentBand, Applied)),
                  static_cast<unsigned>(Visible));
    RecordLeafHeader(GridHead, CurrentBand.Glyph, Tinted.EntityAccent,
                     CurrentBand.Naming, Secondary);

    if (Visible == 0u)
    {
        Surface->TextRun(Grid.MinimumX + 22.0f, Grid.MinimumY + 84.0f,
                         Tinted.Faint, "No operations are visible for the current document state.",
                         Scaled.RunSecondary);
    }
    else
    {
        std::uint32_t Tile = 0u;
        for (std::uint32_t Index = 0u; Index < CurrentBand.ToolCount && Tile < ParametricToolsContext::TileLimit; ++Index)
        {
            const ToolEntry& Tool = CurrentBand.Tools[Index];
            if (!Presented(Tool, Applied))
                continue;

            const std::uint32_t Column = Tile % 4u;
            const std::uint32_t RowIndex = Tile / 4u;
            const float TileX = Grid.MinimumX + 10.0f + static_cast<float>(Column) * ((Grid.Width() - 38.0f) / 4.0f);
            const float TileY = Grid.MinimumY + 61.0f + static_cast<float>(RowIndex) * 84.0f - GridScroll;
            const PlaneExtent Row = Spanning(TileX, TileY, (Grid.Width() - 46.0f) / 4.0f, 76.0f);
            ++Tile;
            if (Surface->Excluded(Row))
                continue;

            const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
            const bool Current = ToolStands(Applied, Applied.ActiveBand, Index);
            const bool ToolGated = Gated(Tool, Applied);

            if (Hovered && Sampled.ContactPressed)
                Interaction->Grab(TileRows[Index], ControlPart::Body);
            if (Hovered && Interaction->Released(TileRows[Index]))
            {
                Applied.ActiveTool = Index;
                Applied.ActiveSubject = ToolSubjectOf(Applied.ActiveBand, Index);
            }
            if (Hovered && Sampled.ContactDoublePressed && !ToolGated)
            {
                Applied.ActiveTool = Index;
                Applied.ActiveSubject = ToolSubjectOf(Applied.ActiveBand, Index);
                Applied.Page = ParametricToolPage::Settings;
            }
            Interaction->DeclareHovered(TileRows[Index], Hovered, HoverOver);

            Surface->Ground(Row, ToolGated ? Tinted.MenuLower : (Hovered ? Tinted.TileHovered : Tinted.Tile),
                            9.0f, CornerAll);
            Surface->Edge(Row, Current ? Tinted.EntityAccent : Tinted.Hairline,
                          1.0f, 9.0f, CornerAll);
            Surface->Stroke(Tool.Glyph,
                            Spanning(Row.MinimumX + (Row.Width() - 27.0f) * 0.5f,
                                     Row.MinimumY + 10.0f, 27.0f, 27.0f),
                            ToolGated ? Tinted.Faint : Tinted.Primary);
            Surface->TextRunTruncated(Row.MinimumX + 6.0f, Row.MinimumY + 45.0f,
                                      Row.Width() - 12.0f, ToolGated ? Tinted.Faint : Tinted.Muted,
                                      Tool.Naming, Scaled.RunFine);
            if (Tool.Accelerator[0] != '\0' && !ToolGated)
                Surface->TextRun(Row.MaximumX - 22.0f, Row.MinimumY + 6.0f,
                                 Tinted.Faint, Tool.Accelerator, Scaled.RunFiner);
            if (ToolGated)
                Surface->TextRunTruncated(Row.MinimumX + 6.0f, Row.MinimumY + 58.0f,
                                          Row.Width() - 12.0f, Covering(0xF59E0Bu),
                                          ShortfallText(Tool, Applied), Scaled.RunFiner);
            else if (const char* Badge = ResultText(Tool, Applied); Badge != nullptr)
                Surface->TextRun(Row.MinimumX + 6.0f, Row.MinimumY + 58.0f,
                                 Covering(0xF59E0Bu), Badge, Scaled.RunFiner);
        }
    }
    Surface->Release();

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f), Tinted.Hairline);
    char FooterText[128] = {};
    std::snprintf(FooterText, sizeof(FooterText), "%u of %u live • Tab = settings • double-click a tile",
                  static_cast<unsigned>(LiveCount(CurrentBand, Applied)),
                  static_cast<unsigned>(Visible));
    Surface->TextRun(Footer.MinimumX + Scaled.HeaderPadX,
                     Footer.MinimumY + (Footer.Height() - Scaled.RunFine) * 0.5f,
                     Tinted.Muted, FooterText, Scaled.RunFine);
}

void ParametricToolsPanel::RecordDetailPage(const PlaneExtent& Extent, ParametricToolsContext& Applied)
{
    Surface->Ground(Extent, Tinted.Menu, 0.0f, CornerNone);

    const ToolEntry* Tool = Applied.ActiveSubject == ParametricToolSubject::Select
                          ? nullptr
                          : ActiveTool(Applied);
    const PlaneExtent Header = Spanning(Extent.MinimumX, Extent.MinimumY,
                                        Extent.Width(), Scaled.HeaderHeight);
    RecordLeafHeader(Header, Tool != nullptr ? Tool->Glyph : SymbolSubject::SketchPlane,
                     Tinted.EntityAccent,
                     Tool != nullptr ? Tool->Naming : "Construction",
                     Tool != nullptr ? "Tool Settings" : "No tool selected");

    const bool BackHovered = Header.Encloses(Sampled.PositionX, Sampled.PositionY);
    if (BackHovered && Sampled.ContactPressed)
        Interaction->Grab(BackCall, ControlPart::Body);
    if (BackHovered && Interaction->Released(BackCall))
        Applied.Page = ParametricToolPage::Catalogue;
    Interaction->DeclareHovered(BackCall, BackHovered, HoverOver);

    const float LeftWidth = std::min(LeftPaneX, Extent.Width() * 0.40f);
    const PlaneExtent Probe = Spanning(Extent.MinimumX, Header.MaximumY,
                                       LeftWidth, Extent.Height() - Scaled.HeaderHeight - Scaled.FooterHeight);
    const PlaneExtent Options = Spanning(Probe.MaximumX, Header.MaximumY,
                                         Extent.MaximumX - Probe.MaximumX, Probe.Height());
    const PlaneExtent Footer = Spanning(Extent.MinimumX, Extent.MaximumY - Scaled.FooterHeight,
                                        Extent.Width(), Scaled.FooterHeight);
    Surface->Ground(Spanning(Probe.MaximumX, Probe.MinimumY, 1.0f, Probe.Height()), Tinted.Hairline);

    const auto SectionTitle = [&](float X, float Y, const char* Naming)
    {
        Surface->TextRun(X, Y, Tinted.Faint, Naming, Scaled.RunFiner, 0.0f, true);
    };

    const auto ButtonRow = [&](const PlaneExtent& Row, bool Hovered, bool Active) -> ThemeToken
    {
        if (Active)
            Surface->Ground(Row, Tinted.EntityTaken, 7.0f, CornerAll);
        else if (Hovered)
            Surface->Ground(Row, Tinted.RowHovered, 7.0f, CornerAll);
        return Active ? Tinted.Primary : (Hovered ? Tinted.Primary : Tinted.Muted);
    };

    const auto StepRow = [&](float& Sweep, const char* Label, std::uint32_t& Value,
                             std::uint32_t Minimum, std::uint32_t Maximum)
    {
        const PlaneExtent Row = Spanning(Probe.MinimumX + 8.0f, Sweep, Probe.Width() - 16.0f, 24.0f);
        const PlaneExtent Less = Spanning(Row.MaximumX - 52.0f, Row.MinimumY + 3.0f, 18.0f, 18.0f);
        const PlaneExtent More = Spanning(Row.MaximumX - 22.0f, Row.MinimumY + 3.0f, 18.0f, 18.0f);
        const bool LessHovered = Less.Encloses(Sampled.PositionX, Sampled.PositionY);
        const bool MoreHovered = More.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && LessHovered && Value > Minimum) --Value;
        if (Sampled.ContactPressed && MoreHovered && Value < Maximum) ++Value;
        Surface->TextRun(Row.MinimumX, Row.MinimumY + 7.0f, Tinted.Muted, Label, Scaled.RunFine);
        char Reading[16] = {};
        std::snprintf(Reading, sizeof(Reading), "%u", static_cast<unsigned>(Value));
        Surface->TextRun(Less.MinimumX + 5.0f, Less.MinimumY + 4.0f, LessHovered ? Tinted.Primary : Tinted.Faint, "◄", Scaled.RunFine);
        Surface->TextRun(Row.MaximumX - 36.0f, Row.MinimumY + 7.0f, Tinted.Primary, Reading, Scaled.RunFine);
        Surface->TextRun(More.MinimumX + 5.0f, More.MinimumY + 4.0f, MoreHovered ? Tinted.Primary : Tinted.Faint, "►", Scaled.RunFine);
        Sweep += 26.0f;
    };

    const auto ToggleRow = [&](float& Sweep, const char* Label, bool& Value)
    {
        const PlaneExtent Row = Spanning(Probe.MinimumX + 8.0f, Sweep, Probe.Width() - 16.0f, 24.0f);
        const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Hovered)
            Value = !Value;
        ButtonRow(Row, Hovered, Value);
        Surface->TextRun(Row.MinimumX + 4.0f, Row.MinimumY + 7.0f,
                         Hovered ? Tinted.Primary : Tinted.Muted, Label, Scaled.RunFine);
        Surface->TextRun(Row.MaximumX - 18.0f, Row.MinimumY + 7.0f,
                         Value ? Tinted.EntityAccent : Tinted.Faint,
                         Value ? "On" : "Off", Scaled.RunFine);
        Sweep += 26.0f;
    };

    const auto ChipRow = [&](float& Sweep)
    {
        std::array<ParametricToolDimension, 7u> Dimensions = {
            ParametricToolDimension::Nothing, ParametricToolDimension::Vertex,
            ParametricToolDimension::Edge, ParametricToolDimension::Wire,
            ParametricToolDimension::Face, ParametricToolDimension::Shell,
            ParametricToolDimension::Solid };
        float X = Probe.MinimumX + 8.0f;
        float Y = Sweep;
        for (ParametricToolDimension Subject : Dimensions)
        {
            const char* Label = ParametricToolDimensionText(Subject);
            const float Width = Surface->MeasureRun(Label, Scaled.RunFine, 0.0f) + 18.0f;
            if (X + Width > Probe.MaximumX - 8.0f)
            {
                X = Probe.MinimumX + 8.0f;
                Y += 28.0f;
            }
            const PlaneExtent Chip = Spanning(X, Y, Width, 22.0f);
            const bool Current = Applied.ActiveDimension == Subject;
            const bool Hovered = Chip.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Sampled.ContactPressed && Hovered)
                Applied.ActiveDimension = Subject;
            ButtonRow(Chip, Hovered, Current);
            Surface->TextRun(Chip.MinimumX + 8.0f, Chip.MinimumY + 6.0f,
                             Current ? Tinted.Primary : (Hovered ? Tinted.Primary : Tinted.Muted),
                             Label, Scaled.RunFiner);
            X = Chip.MaximumX + 4.0f;
        }
        Sweep = Y + 30.0f;
    };

    const auto PresetRow = [&](float& Sweep, std::uint32_t Index)
    {
        const PlaneExtent Row = Spanning(Probe.MinimumX + 8.0f, Sweep, Probe.Width() - 16.0f, 34.0f);
        const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
        if (Sampled.ContactPressed && Hovered)
            ApplyPreset(Index, Applied);
        ButtonRow(Row, Hovered, false);
        Surface->TextRun(Row.MinimumX + 4.0f, Row.MinimumY + 5.0f,
                         Hovered ? Tinted.Primary : Tinted.Muted,
                         Presets[Index].Naming, Scaled.RunFine, 0.0f, true);
        Surface->TextRun(Row.MinimumX + 4.0f, Row.MinimumY + 18.0f,
                         Tinted.Faint, Presets[Index].Note, Scaled.RunFiner);
        Sweep += 36.0f;
    };

    const float ProbeScroll = ProbeOverflow.Advance(Sampled, Probe, 760.0f, 72.0f);
    Surface->Confine(Probe);
    float Sweep = Probe.MinimumY + 10.0f - ProbeScroll;

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Input Dimension");
    Sweep += 20.0f;
    ChipRow(Sweep);

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Dimension-Raising Law");
    Sweep += 20.0f;
    {
        const PlaneExtent Card = Spanning(Probe.MinimumX + 8.0f, Sweep, Probe.Width() - 16.0f, 52.0f);
        Surface->Ground(Card, Tinted.Tile, 9.0f, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, 9.0f, CornerAll);
        const ParametricToolDimension Raised = RaisedDimension(Applied.ActiveDimension, Applied.ClosedProfileCondition);
        const char* Result = Raised == ParametricToolDimension::Nothing ? "no successor"
                           : ParametricToolDimensionText(Raised);
        char Law[96] = {};
        std::snprintf(Law, sizeof(Law), "%s -> %s",
                      ParametricToolDimensionText(Applied.ActiveDimension), Result);
        Surface->TextRun(Card.MinimumX + 8.0f, Card.MinimumY + 9.0f, Tinted.Primary, Law, Scaled.RunSecondary, 0.0f, true);
        Surface->TextRun(Card.MinimumX + 8.0f, Card.MinimumY + 28.0f,
                         Raised == ParametricToolDimension::Shell ? Covering(0xF59E0Bu) : Tinted.Faint,
                         Raised == ParametricToolDimension::Shell ? "open wire or shell stops one step short of solid"
                                                                  : "dimension raises exactly one step when the input allows it",
                         Scaled.RunFiner);
        Sweep += 60.0f;
    }

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Counts");
    Sweep += 20.0f;
    StepRow(Sweep, "Selected", Applied.SelectedCount, 0u, 12u);
    StepRow(Sweep, "Profiles", Applied.ProfileCount, 0u, 12u);
    StepRow(Sweep, "Solids", Applied.SolidCount, 0u, 12u);
    StepRow(Sweep, "Perimeter Edges", Applied.PerimeterEdgeCount, 0u, 12u);
    StepRow(Sweep, "Circles", Applied.ExistingCircleCount, 0u, 12u);

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Conditions");
    Sweep += 20.0f;
    ToggleRow(Sweep, "Workplane Set", Applied.WorkplaneActivation);
    ToggleRow(Sweep, "Profile Closed", Applied.ClosedProfileCondition);
    ToggleRow(Sweep, "Profile Planar", Applied.PlanarProfileCondition);
    ToggleRow(Sweep, "Axis Available", Applied.AxisAvailability);
    ToggleRow(Sweep, "Path Available", Applied.PathAvailability);
    ToggleRow(Sweep, "Uniform Closure", Applied.UniformClosureCondition);
    ToggleRow(Sweep, "Support Material", Applied.SupportMaterialCondition);
    ToggleRow(Sweep, "Shared Endpoint", Applied.TangentEndpointCondition);
    ToggleRow(Sweep, "Opening Available", Applied.OpeningCondition);
    ToggleRow(Sweep, "Reference Plane", Applied.ReferencePlaneCondition);
    ToggleRow(Sweep, "Imported Source", Applied.SourceImageryCondition);
    ToggleRow(Sweep, "Measurable", Applied.MeasurableCondition);
    ToggleRow(Sweep, "Pending Geometry", Applied.PendingGeometryCondition);

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Presets");
    Sweep += 20.0f;
    for (std::uint32_t Index = 0u; Index < PresetCount; ++Index)
        PresetRow(Sweep, Index);

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Live Tally");
    Sweep += 20.0f;
    {
        const ToolTally Tally = ResolveTally(Applied);
        const PlaneExtent Card = Spanning(Probe.MinimumX + 8.0f, Sweep, Probe.Width() - 16.0f, 52.0f);
        Surface->Ground(Card, Tinted.Tile, 9.0f, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, 9.0f, CornerAll);
        char Runs[96] = {};
        std::snprintf(Runs, sizeof(Runs), "%u available • %u gated • %u hidden",
                      static_cast<unsigned>(Tally.Available),
                      static_cast<unsigned>(Tally.Gated),
                      static_cast<unsigned>(Tally.Hidden));
        Surface->TextRun(Card.MinimumX + 8.0f, Card.MinimumY + 9.0f,
                         Tinted.Primary, Runs, Scaled.RunFine, 0.0f, true);
        char Sum[96] = {};
        std::snprintf(Sum, sizeof(Sum), "%u + %u + %u = %u",
                      static_cast<unsigned>(Tally.Available),
                      static_cast<unsigned>(Tally.Gated),
                      static_cast<unsigned>(Tally.Hidden),
                      static_cast<unsigned>(Tally.Total));
        Surface->TextRun(Card.MinimumX + 8.0f, Card.MinimumY + 28.0f,
                         Tinted.Faint, Sum, Scaled.RunFiner);
        Sweep += 60.0f;
    }

    SectionTitle(Probe.MinimumX + 8.0f, Sweep, "Tile Field");
    Sweep += 20.0f;
    ToggleRow(Sweep, "Show Gated", Applied.ShowGated);
    Surface->Release();

    const float OptionScroll = OptionOverflow.Advance(Sampled, Options,
                       Tool != nullptr ? 360.0f + static_cast<float>(Tool->OptionCount) * 34.0f : 120.0f, 72.0f);
    Surface->Confine(Options);

    if (Tool == nullptr)
    {
        Surface->TextRun(Options.MinimumX + 18.0f, Options.MinimumY + 24.0f,
                         Tinted.Faint, "Double-click a tool tile in the catalogue to inspect settings.",
                         Scaled.RunSecondary);
    }
    else
    {
        float OptionY = Options.MinimumY + 12.0f - OptionScroll;
        if (const char* Badge = ResultText(*Tool, Applied); Badge != nullptr)
        {
            const PlaneExtent Banner = Spanning(Options.MinimumX + 10.0f, OptionY,
                                                Options.Width() - 20.0f, 34.0f);
            Surface->Ground(Banner, Tinted.Tile, 9.0f, CornerAll);
            Surface->Edge(Banner, Covering(0xF59E0Bu), 1.0f, 9.0f, CornerAll);
            Surface->TextRun(Banner.MinimumX + 10.0f, Banner.MinimumY + 10.0f,
                             Covering(0xF59E0Bu), Badge, Scaled.RunFine, 0.0f, true);
            OptionY += 42.0f;
        }

        const PlaneExtent Card = Spanning(Options.MinimumX + 10.0f, OptionY,
                                          Options.Width() - 20.0f,
                                          218.0f + static_cast<float>(Tool->OptionCount) * 30.0f);
        Surface->Ground(Card, Tinted.Desk, Scaled.CardRadius, CornerAll);
        Surface->Edge(Card, Tinted.Hairline, 1.0f, Scaled.CardRadius, CornerAll);
        Surface->Ground(Spanning(Card.MinimumX, Card.MinimumY, Card.Width(), Scaled.ComponentY),
                        Tinted.MenuLower, Scaled.CardRadius,
                        CornerLeadingUpper | CornerTrailingUpper);
        Surface->TextRun(Card.MinimumX + 12.0f,
                         Card.MinimumY + (Scaled.ComponentY - Scaled.RunSmall) * 0.5f,
                         Tinted.Muted, "Settings", Scaled.RunSmall, 0.0f, true);

        float RowY = Card.MinimumY + Scaled.ComponentY + 8.0f;
        for (std::uint32_t Index = 0u; Index < Tool->OptionCount; ++Index)
        {
            const PlaneExtent Row = Spanning(Card.MinimumX + 10.0f, RowY,
                                             Card.Width() - 20.0f, 24.0f);
            const PlaneExtent Value = Spanning(Row.MaximumX - 96.0f, Row.MinimumY, 96.0f, 24.0f);
            Surface->TextRun(Row.MinimumX, Row.MinimumY + 7.0f, Tinted.Muted,
                             Tool->Options[Index].Label, Scaled.RunFine);
            Surface->Ground(Value, Tinted.Tile, 12.0f, CornerAll);
            Surface->TextRun(Value.MinimumX + 8.0f, Value.MinimumY + 7.0f, Tinted.Primary,
                             Tool->Options[Index].Value, Scaled.RunFine);
            RowY += 30.0f;
        }

        const auto ToggleSetting = [&](float& Cursor, const char* Label, bool& Value)
        {
            const PlaneExtent Row = Spanning(Card.MinimumX + 10.0f, Cursor,
                                             Card.Width() - 20.0f, 24.0f);
            const PlaneExtent Switch = Spanning(Row.MaximumX - 58.0f, Row.MinimumY, 58.0f, 24.0f);
            const bool Hovered = Row.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Hovered && Sampled.ContactPressed)
                Value = !Value;
            Surface->TextRun(Row.MinimumX, Row.MinimumY + 7.0f, Tinted.Muted, Label, Scaled.RunFine);
            Surface->Ground(Switch, Value ? Tinted.EntityTaken : Tinted.Tile, 12.0f, CornerAll);
            Surface->Edge(Switch, Value ? Tinted.EntityAccent : Tinted.Hairline, 1.0f, 12.0f, CornerAll);
            Surface->TextRun(Switch.MinimumX + 10.0f, Switch.MinimumY + 7.0f,
                             Value ? Tinted.Primary : Tinted.Faint, Value ? "On" : "Off", Scaled.RunFine);
            Cursor += 30.0f;
        };

        const auto StepSetting = [&](float& Cursor, const char* Label, double& Value,
                                     double Step, double Minimum, double Maximum, const char* Unit)
        {
            const PlaneExtent Row = Spanning(Card.MinimumX + 10.0f, Cursor,
                                             Card.Width() - 20.0f, 24.0f);
            const PlaneExtent Less = Spanning(Row.MaximumX - 112.0f, Row.MinimumY, 24.0f, 24.0f);
            const PlaneExtent More = Spanning(Row.MaximumX - 24.0f, Row.MinimumY, 24.0f, 24.0f);
            const PlaneExtent ValueBox = Spanning(Less.MaximumX + 4.0f, Row.MinimumY, 56.0f, 24.0f);
            const bool LessHovered = Less.Encloses(Sampled.PositionX, Sampled.PositionY);
            const bool MoreHovered = More.Encloses(Sampled.PositionX, Sampled.PositionY);
            if (Sampled.ContactPressed && LessHovered)
                Value = std::max(Minimum, Value - Step);
            if (Sampled.ContactPressed && MoreHovered)
                Value = std::min(Maximum, Value + Step);
            char Reading[32] = {};
            std::snprintf(Reading, sizeof(Reading), Unit[0] == '\0' ? "%.0f" : "%.0f%s", Value, Unit);
            Surface->TextRun(Row.MinimumX, Row.MinimumY + 7.0f, Tinted.Muted, Label, Scaled.RunFine);
            Surface->Ground(Less, LessHovered ? Tinted.TileHovered : Tinted.Tile, 12.0f, CornerAll);
            Surface->TextRun(Less.MinimumX + 7.0f, Less.MinimumY + 7.0f, Tinted.Primary, "-", Scaled.RunFine);
            Surface->Ground(ValueBox, Tinted.Tile, 12.0f, CornerAll);
            Surface->TextRun(ValueBox.MinimumX + 7.0f, ValueBox.MinimumY + 7.0f, Tinted.Primary, Reading, Scaled.RunFine);
            Surface->Ground(More, MoreHovered ? Tinted.TileHovered : Tinted.Tile, 12.0f, CornerAll);
            Surface->TextRun(More.MinimumX + 7.0f, More.MinimumY + 7.0f, Tinted.Primary, "+", Scaled.RunFine);
            Cursor += 30.0f;
        };

        RowY += 8.0f;
        Surface->Ground(Spanning(Card.MinimumX + 10.0f, RowY, Card.Width() - 20.0f, 1.0f), Tinted.Hairline);
        RowY += 10.0f;
        const ParametricToolSubject Subject = ToolSubjectOf(Applied.ActiveBand, Applied.ActiveTool);
        ToggleSetting(RowY, "Construction", Applied.ConstructionGeometry);
        // 📝 Beside Construction because the two answer the same kind of question about the shape
        //    about to be drawn, rather than about one tool.
        ToggleSetting(RowY, "Closed Profile", Applied.ClosedProfileFill);
        if (Subject == ParametricToolSubject::Line)
        {
            ToggleSetting(RowY, "Use Length", Applied.LineLengthAssist);
            StepSetting(RowY, "Length", Applied.LineLength, 10.0, 1.0, 1000.0, "");
            ToggleSetting(RowY, "Use Angle", Applied.LineAngleAssist);
            StepSetting(RowY, "Angle", Applied.LineAngleDegrees, 15.0, -180.0, 180.0, "°");
        }
        else if (Subject == ParametricToolSubject::Rectangle)
        {
            ToggleSetting(RowY, "Use Size", Applied.RectangleDimensionAssist);
            StepSetting(RowY, "Width", Applied.RectangleWidth, 10.0, 1.0, 1000.0, "");
            StepSetting(RowY, "Height", Applied.RectangleHeight, 10.0, 1.0, 1000.0, "");
        }
        else if (Subject == ParametricToolSubject::Circle)
        {
            ToggleSetting(RowY, "Use Radius", Applied.CircleRadiusAssist);
            ToggleSetting(RowY, "Diameter Readout", Applied.CircleDiameterMode);
            double CircleReading = Applied.CircleDiameterMode ? Applied.CircleRadius * 2.0 : Applied.CircleRadius;
            StepSetting(RowY, Applied.CircleDiameterMode ? "Diameter" : "Radius", CircleReading,
                        5.0, 1.0, 2000.0, "");
            Applied.CircleRadius = Applied.CircleDiameterMode ? CircleReading * 0.5 : CircleReading;
        }
    }
    Surface->Release();

    Surface->Ground(Footer, Tinted.MenuLower, 0.0f, CornerNone);
    Surface->Ground(Spanning(Footer.MinimumX, Footer.MinimumY, Footer.Width(), 1.0f), Tinted.Hairline);
    Surface->TextRun(Footer.MinimumX + Scaled.HeaderPadX,
                     Footer.MinimumY + (Footer.Height() - Scaled.RunFine) * 0.5f,
                     Tinted.Muted,
                     "Tab = catalogue • double-click a tool to open settings • click back to return",
                     Scaled.RunFine);
}

void ParametricToolsPanel::Record(const PlaneExtent& Extent, ParametricToolsContext& Applied)
{
    if (Applied.ActiveBand >= BandCount || VisibleCount(Bands[Applied.ActiveBand], Applied) == 0u)
        Applied.ActiveBand = FirstPresentedBand(Applied);

    Pages.Navigate(static_cast<std::uint32_t>(Applied.Page));
    const PlaneExtent CatalogueExtent = Pages.Page(Extent, 0u);
    const PlaneExtent SettingsExtent = Pages.Page(Extent, 1u);
    const PointerCondition LivePointer = Sampled;

    const auto SeatPagePointer = [&](std::uint32_t Page)
    {
        Sampled = LivePointer;
        if (Pages.CurrentPage() != Page)
        {
            Sampled.PositionX = -1000000.0f;
            Sampled.PositionY = -1000000.0f;
            Sampled.ContactHeld = Sampled.ContactPressed = Sampled.ContactReleased = false;
            Sampled.ContactDoublePressed = false;
            Sampled.SecondaryHeld = Sampled.SecondaryPressed = Sampled.SecondaryReleased = false;
            Sampled.WheelY = 0.0f;
        }
    };

    if (!Surface->Excluded(SettingsExtent))
    {
        SeatPagePointer(1u);
        Surface->Confine(Extent);
        RecordDetailPage(SettingsExtent, Applied);
        Surface->Release();
    }

    if (!Surface->Excluded(CatalogueExtent))
    {
        SeatPagePointer(0u);
        Surface->Confine(Extent);
        RecordBrowsePage(CatalogueExtent, Applied);
        Surface->Release();
    }

    Sampled = LivePointer;
}

} // namespace Slate
