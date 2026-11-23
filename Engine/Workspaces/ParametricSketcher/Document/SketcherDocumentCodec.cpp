/*====================================================================================================================================
                                                      SOLIDARCDOCUMENTCODEC.CPP
====================================================================================================================================*/
// 🧩 Sketcher document files (.sketcher): parameters, steps, sketches, references, captions (toml++)

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDocumentCodec.h"

#include "Engine/GeometrySpace/Parametric/Diagnostics/OperationTrace.h"
#include "Engine/GeometrySpace/Parametric/Exchange/ExchangePath.h"

#include "Engine/Workspaces/ParametricSketcher/Document/SketcherDrawingSheet.h"
#include "Engine/Workspaces/ParametricSketcher/Document/SketcherImagePlaneToml.h"

#include <toml++/toml.hpp>

#include <cmath>
#include <fstream>
#include <sstream>

namespace Frontier::Workspaces::Sketcher
{

using namespace Frontier::GeometrySpace::Parametric;

namespace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr int64_t FormatVersion = 1;                                                         // [-] - Bumped on an incompatible change

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENCODING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] toml::array EncodeVector(const Vector3& Value) { return toml::array{ Value.X, Value.Y, Value.Z }; }

[[nodiscard]] toml::table EncodeValue(const FeatureScalar& Value)
{
    toml::table Table{ { "Literal", Value.Literal } };
    if (!Value.Parameter.empty())
    {
        Table.insert("Parameter", Value.Parameter);
        Table.insert("Factor", Value.Factor);
        Table.insert("Offset", Value.Offset);
    }
    if (!Value.SecondParameter.empty())
    {
        Table.insert("SecondParameter", Value.SecondParameter);
        Table.insert("SecondFactor", Value.SecondFactor);
    }
    return Table;
}

[[nodiscard]] toml::array EncodeReferences(const std::vector<ConnectivityReference>& References)
{
    toml::array Entries;
    for (const ConnectivityReference& Reference : References)
    {
        toml::table Entry{ { "Kind", static_cast<int64_t>(Reference.Kind) }, { "Name", Reference.Name } };
        if (Reference.Signature.CapturedCondition)
        {
            Entry.insert("Position", EncodeVector(Reference.Signature.Position));
            Entry.insert("Direction", EncodeVector(Reference.Signature.Direction));
            Entry.insert("Category", Reference.Signature.Category);
            Entry.insert("Size", Reference.Signature.Size);
        }
        Entries.push_back(std::move(Entry));
    }
    return Entries;
}

[[nodiscard]] toml::table EncodeSketch(const SketchModel& Sketch)
{
    toml::array Points;
    for (const SketchPoint& Point : Sketch.Points) Points.push_back(toml::array{ Point.Position.X, Point.Position.Y });
    toml::array Curves;
    for (const SketchCurve& Curve : Sketch.Curves)
    {
        toml::table Entry{ { "Kind", static_cast<int64_t>(Curve.Kind) }, { "Identifier", Curve.Identifier },
                           { "Start", Curve.Start }, { "End", Curve.End }, { "Centre", Curve.Centre },
                           { "Radius", Curve.Radius }, { "Construction", Curve.Construction } };
        if (Curve.Kind == SketchCurveCategory::Spline)
        {
            toml::array Indices;
            for (int Index : Curve.Points) Indices.push_back(Index);
            Entry.insert("Form", static_cast<int64_t>(Curve.Form));
            Entry.insert("FormLabel", SketchSplineFormLabel(Curve.Form));
            Entry.insert("Points", std::move(Indices));
            if (!Curve.Knots.empty())
            {
                toml::array Knots;
                for (double Knot : Curve.Knots) Knots.push_back(Knot);
                Entry.insert("Knots", std::move(Knots));
            }
        }
        if (!Curve.Link.Sketch.empty())
        {
            Entry.insert("LinkSketch", Curve.Link.Sketch);
            Entry.insert("LinkIndex", Curve.Link.Index);
        }
        Curves.push_back(std::move(Entry));
    }
    toml::array Constraints;
    for (const SketchConstraint& Constraint : Sketch.Constraints)
    {
        toml::table Entry{ { "Kind", static_cast<int64_t>(Constraint.Kind) }, { "First", Constraint.First }, { "Second", Constraint.Second },
                           { "Third", Constraint.Third }, { "Value", Constraint.Value }, { "SecondValue", Constraint.SecondValue },
                           { "Label", Constraint.Label } };
        if (!Constraint.Parameter.empty()) Entry.insert("Parameter", Constraint.Parameter);
        if (Constraint.DrivenCondition) Entry.insert("Reference", true);
        Constraints.push_back(std::move(Entry));
    }
    toml::table Table{ { "Points", std::move(Points) }, { "Curves", std::move(Curves) }, { "Constraints", std::move(Constraints) } };
    // Links = [{ Point, Sketch, Index }]: points projected from earlier sketches; written only when there are any.
    toml::array Links;
    for (size_t Point = 0; Point < Sketch.Points.size(); ++Point)
        if (!Sketch.Points[Point].Link.Sketch.empty())
            Links.push_back(toml::table{ { "Point", static_cast<int64_t>(Point) }, { "Sketch", Sketch.Points[Point].Link.Sketch }, { "Index", Sketch.Points[Point].Link.Index } });
    if (!Links.empty()) Table.insert("Links", std::move(Links));
    return Table;
}

[[nodiscard]] toml::table EncodeStep(const FeatureStep& Step, const std::map<std::string, std::string>& Captions)
{
    toml::table Table{ { "Kind", static_cast<int64_t>(Step.Kind) }, { "KindLabel", FeatureStepKindLabel(Step.Kind) }, { "Label", Step.Label } };
    const auto Found = Captions.find(Step.Label);
    if (Found != Captions.end()) Table.insert("Caption", Found->second);
    if (Step.ReverseNormals) Table.insert("ReverseNormals", true);
    switch (Step.Kind)
    {
    case FeatureStepCategory::Sketch:
        Table.insert("PlaneOrigin", EncodeVector(Step.Plane.Origin));
        Table.insert("PlaneXAxis", EncodeVector(Step.Plane.XAxis));
        Table.insert("PlaneYAxis", EncodeVector(Step.Plane.YAxis));
        Table.insert("PlaneZAxis", EncodeVector(Step.Plane.ZAxis));
        Table.insert("Elevation", EncodeValue(Step.Elevation));
        if (!Step.FillCondition) Table.insert("Fill", false);                                // Absent: filled (older files)
        Table.insert("Sketch", EncodeSketch(Step.Sketch));
        break;
    case FeatureStepCategory::Extrude:
    case FeatureStepCategory::Revolve:
        Table.insert("Profile", Step.Profile);
        Table.insert("Extent", EncodeValue(Step.Extent));
        if (Step.Start.Literal != 0.0 || !Step.Start.Parameter.empty()) Table.insert("Start", EncodeValue(Step.Start));   // Absent: 0 (older files)
        Table.insert("Combination", static_cast<int64_t>(Step.Combination));
        Table.insert("AxisIdentifier", Step.AxisIdentifier);
        Table.insert("Angle", EncodeValue(Step.Angle));
        if (Step.Kind == FeatureStepCategory::Revolve && Step.AxisIdentifier < 0)              // Axis without a line (absent in older files)
        {
            Table.insert("AxisOrigin", toml::array{ Step.AxisOrigin.X, Step.AxisOrigin.Y });
            Table.insert("AxisDirection", toml::array{ Step.AxisDirection.X, Step.AxisDirection.Y });
        }
        break;
    case FeatureStepCategory::Evolution:
        // The profile sketch and the path (a one-curve sketch or a SpaceCurve).
        Table.insert("Profile", Step.Profile);
        Table.insert("Path", Step.Path);
        Table.insert("Combination", static_cast<int64_t>(Step.Combination));
        break;
    case FeatureStepCategory::Blend:
        Table.insert("Shape", static_cast<int64_t>(Step.Shape));
        Table.insert("Size", EncodeValue(Step.Size));
        if (Step.EndSize)
        {
            Table.insert("EndSize", EncodeValue(*Step.EndSize));
            Table.insert("SizeAnchors", EncodeReferences(Step.SizeAnchors));
        }
        else if (!Step.AnchorSizes.empty())
        {
            toml::array Sizes;                                                               // Per-vertex sizes (blend handles), one per anchor
            for (const FeatureScalar& Size : Step.AnchorSizes) Sizes.push_back(EncodeValue(Size));
            Table.insert("SizeAnchors", EncodeReferences(Step.SizeAnchors));
            Table.insert("AnchorSizes", std::move(Sizes));
        }
        break;
    case FeatureStepCategory::FaceOperation:
        Table.insert("Operation", static_cast<int64_t>(Step.Operation));
        Table.insert("Amount", EncodeValue(Step.Amount));
        if (Step.Operation == FaceOperationCategory::Inset) Table.insert("Extent", EncodeValue(Step.Extent));
        if (Step.Operation == FaceOperationCategory::Reshape)
        {
            // Reshape: the map carrying the picked vertices / edge ends (x′ = X·x + Y·y + Z·z + Shift).
            Table.insert("PlacementX", EncodeVector(Step.Placement.ColumnX));
            Table.insert("PlacementY", EncodeVector(Step.Placement.ColumnY));
            Table.insert("PlacementZ", EncodeVector(Step.Placement.ColumnZ));
            Table.insert("PlacementShift", EncodeVector(Step.Placement.Shift));
        }
        if (!Step.Points.empty())
        {
            toml::array Points;                                                              // Tweak direction / rotation axis (gizmo)
            for (const Vector3& Point : Step.Points) Points.push_back(EncodeVector(Point));
            Table.insert("Points", std::move(Points));
        }
        break;
    case FeatureStepCategory::SpaceCurve:
    {
        toml::array Points;
        for (const Vector3& Point : Step.Points) Points.push_back(EncodeVector(Point));
        Table.insert("Points", std::move(Points));
        Table.insert("Closed", Step.ClosedCondition);
        if (Step.PlanarCondition) Table.insert("Planar", true);
        Table.insert("Tolerance", EncodeValue(Step.Tolerance));
        if (!Step.CurveKnots.empty())                                                        // Exact curve: Points are its poles
        {
            toml::array Knots;
            for (double Knot : Step.CurveKnots) Knots.push_back(Knot);
            Table.insert("CurveKnots", std::move(Knots));
        }
        if (!Step.CurveOffsetGuide.empty()) {
            Table.insert("CurveOffsetGuide",Step.CurveOffsetGuide); Table.insert("CurveOffsetBody",Step.CurveOffsetBody); Table.insert("CurveOffsetOrigin",EncodeVector(Step.CurveOffsetOrigin));
            Table.insert("CurveOffsetDistance",Step.CurveOffsetDistance); Table.insert("CurveOffsetTolerance",Step.CurveOffsetTolerance);
        }
        if (!Step.PoleEdits.empty())
        {
            toml::array Edits;
            for (const FeaturePoleEdit& Edit : Step.PoleEdits) Edits.push_back(toml::table{ { "Index", static_cast<int64_t>(Edit.Index) }, { "Position", EncodeVector(Edit.Position) } });
            Table.insert("PoleEdits", std::move(Edits));
        }
        break;
    }
    case FeatureStepCategory::Loft:
    case FeatureStepCategory::Network:
    {
        toml::array Sections;
        for (const std::string& Section : Step.Sections) Sections.push_back(Section);
        Table.insert("Sections", std::move(Sections));
        if (Step.Continuity != 0) Table.insert("Continuity", static_cast<int64_t>(Step.Continuity));
        if (Step.ClosedCondition) Table.insert("Closed", true);                            // Solid loft (written only when set: older bytes stay)
        break;
    }
    case FeatureStepCategory::Mirror:
        Table.insert("PlaneOrigin", EncodeVector(Step.Plane.Origin));
        Table.insert("PlaneZAxis", EncodeVector(Step.Plane.ZAxis));
        break;
    case FeatureStepCategory::Close:
        break;
    case FeatureStepCategory::Combine:
        Table.insert("Combination", static_cast<int64_t>(Step.Combination));
        Table.insert("Tool", Step.Tool);
        if (Step.Offset.Literal != 0.0 || !Step.Offset.Parameter.empty() || !Step.Offset.SecondParameter.empty() || Step.Offset.Offset != 0.0)
        {
            Table.insert("Offset", EncodeValue(Step.Offset));
            Table.insert("OffsetAxis", EncodeVector(Step.OffsetAxis));
        }
        break;
    case FeatureStepCategory::Cut:
        // A sketch cutter (Profile), or a SpaceCurve projected along PlaneZAxis (Sections).
        if (!Step.Profile.empty()) Table.insert("Profile", Step.Profile);
        if (!Step.Sections.empty())
        {
            toml::array Sections;
            for (const std::string& Section : Step.Sections) Sections.push_back(Section);
            Table.insert("Sections", std::move(Sections));
            Table.insert("PlaneOrigin", EncodeVector(Step.Plane.Origin));
            Table.insert("PlaneXAxis", EncodeVector(Step.Plane.XAxis));
            Table.insert("PlaneZAxis", EncodeVector(Step.Plane.ZAxis));
        }
        break;
    case FeatureStepCategory::Remove:
        break;
    case FeatureStepCategory::Joint:
    {
        // Target moves onto Tool; References = moving, fixed joint origin; every value may bind a parameter.
        const FeatureJoint& Joint = Step.Joint;
        Table.insert("Tool", Step.Tool);
        Table.insert("JointKind", static_cast<int64_t>(Joint.Kind));
        Table.insert("JointAngle", EncodeValue(Joint.Angle));
        Table.insert("JointSlide", EncodeValue(Joint.Slide));
        Table.insert("JointSecondSlide", EncodeValue(Joint.SecondSlide));
        Table.insert("JointPitch", EncodeValue(Joint.Pitch));
        Table.insert("JointRoll", EncodeValue(Joint.Roll));
        Table.insert("JointOffset", EncodeValue(Joint.Offset));
        Table.insert("JointFlipped", Joint.FlippedCondition);
        if (Joint.AngleRange) Table.insert("JointAngleRange", toml::array{ Joint.AngleRange->Low, Joint.AngleRange->High });
        if (Joint.SlideRange) Table.insert("JointSlideRange", toml::array{ Joint.SlideRange->Low, Joint.SlideRange->High });
        break;
    }
    case FeatureStepCategory::Transform:
    case FeatureStepCategory::Duplicate:
        // The affine map as its three linear columns and the shift (x′ = X·x + Y·y + Z·z + Shift).
        Table.insert("PlacementX", EncodeVector(Step.Placement.ColumnX));
        Table.insert("PlacementY", EncodeVector(Step.Placement.ColumnY));
        Table.insert("PlacementZ", EncodeVector(Step.Placement.ColumnZ));
        Table.insert("PlacementShift", EncodeVector(Step.Placement.Shift));
        if (!Step.Source.empty()) Table.insert("Source", Step.Source);                     // Paste: the copy-time step
        if (Step.Offset.Literal != 0.0 || !Step.Offset.Parameter.empty() || !Step.Offset.SecondParameter.empty() || Step.Offset.Offset != 0.0)
        {
            Table.insert("Offset", EncodeValue(Step.Offset));
            Table.insert("OffsetAxis", EncodeVector(Step.OffsetAxis));
        }
        break;
    case FeatureStepCategory::Flip:
        // Only the body key (Target, below): the sheet is turned over as it stands.
        break;
    case FeatureStepCategory::Imprint:
        // The outline sketch and the side the tool stands on (Extent's sign; the body key is Target, below).
        Table.insert("Profile", Step.Profile);
        Table.insert("Extent", EncodeValue(Step.Extent));
        break;
    case FeatureStepCategory::Import:
        // The kernel's canonical STEP text of the body (millimetres) and the model unit it is read at.
        Table.insert("Exchange", Step.Exchange);
        Table.insert("ExchangeUnit", Step.ExchangeUnit);
        break;
    case FeatureStepCategory::Pipe:
        // The path: a sketch (Profile) or a SpaceCurve (Sections); the radius is Size.
        if (!Step.Profile.empty()) Table.insert("Profile", Step.Profile);
        if (!Step.Sections.empty())
        {
            toml::array Sections;
            for (const std::string& Section : Step.Sections) Sections.push_back(Section);
            Table.insert("Sections", std::move(Sections));
        }
        Table.insert("Size", EncodeValue(Step.Size));
        Table.insert("Combination", static_cast<int64_t>(Step.Combination));
        break;
    }
    if (!Step.Target.empty()) Table.insert("Target", Step.Target);                                // Absent: the active body (older files)
    if (!Step.References.empty()) Table.insert("References", EncodeReferences(Step.References));
    return Table;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    DECODING
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] Vector3 DecodeVector(const toml::node_view<const toml::node> View, Vector3 Fallback)
{
    const toml::array* Array = View.as_array();
    if (!Array || Array->size() != 3) return Fallback;
    return { (*Array)[0].value_or(Fallback.X), (*Array)[1].value_or(Fallback.Y), (*Array)[2].value_or(Fallback.Z) };
}

[[nodiscard]] FeatureScalar DecodeValue(const toml::node_view<const toml::node> View, FeatureScalar Fallback = {})
{
    if (!View.is_table()) return Fallback;
    FeatureScalar Value;
    Value.Literal         = View["Literal"].value_or(0.0);
    Value.Parameter       = View["Parameter"].value_or(std::string{});
    Value.Factor          = View["Factor"].value_or(1.0);
    Value.Offset          = View["Offset"].value_or(0.0);
    Value.SecondParameter = View["SecondParameter"].value_or(std::string{});
    Value.SecondFactor    = View["SecondFactor"].value_or(0.0);
    return Value;
}

[[nodiscard]] std::vector<ConnectivityReference> DecodeReferences(const toml::node_view<const toml::node> View)
{
    std::vector<ConnectivityReference> References;
    const toml::array* Entries = View.as_array();
    if (!Entries) return References;
    for (const toml::node& Entry : *Entries)
    {
        const toml::node_view<const toml::node> Reference{ Entry };
        ConnectivityReference Decoded;
        Decoded.Kind = static_cast<ReferenceCategory>(Reference["Kind"].value_or(int64_t{ 1 }));
        Decoded.Name = Reference["Name"].value_or(std::string{});
        if (Reference["Position"])
        {
            Decoded.Signature.CapturedCondition = true;
            Decoded.Signature.Position  = DecodeVector(Reference["Position"], {});
            Decoded.Signature.Direction = DecodeVector(Reference["Direction"], {});
            Decoded.Signature.Category  = Reference["Category"].value_or(std::string{});
            Decoded.Signature.Size      = Reference["Size"].value_or(0.0);
        }
        References.push_back(std::move(Decoded));
    }
    return References;
}

[[nodiscard]] SketchModel DecodeSketch(const toml::node_view<const toml::node> View)
{
    SketchModel Sketch;
    if (const toml::array* Points = View["Points"].as_array())
    {
        for (const toml::node& Point : *Points)
        {
            const toml::array* Pair = Point.as_array();
            if (Pair && Pair->size() == 2) Sketch.Points.push_back({ { (*Pair)[0].value_or(0.0), (*Pair)[1].value_or(0.0) } });
        }
    }
    if (const toml::array* Curves = View["Curves"].as_array())
    {
        for (const toml::node& Entry : *Curves)
        {
            const toml::node_view<const toml::node> Curve{ Entry };
            SketchCurve Decoded;
            Decoded.Kind         = static_cast<SketchCurveCategory>(Curve["Kind"].value_or(int64_t{ 0 }));
            Decoded.Identifier   = static_cast<int>(Curve["Identifier"].value_or(int64_t{ 0 }));
            Decoded.Start        = static_cast<int>(Curve["Start"].value_or(int64_t{ -1 }));
            Decoded.End          = static_cast<int>(Curve["End"].value_or(int64_t{ -1 }));
            Decoded.Centre       = static_cast<int>(Curve["Centre"].value_or(int64_t{ -1 }));
            Decoded.Radius       = Curve["Radius"].value_or(1.0);
            Decoded.Construction = Curve["Construction"].value_or(false);
            Decoded.Form         = static_cast<SketchSplineForm>(Curve["Form"].value_or(int64_t{ 4 }));
            if (const toml::array* Indices = Curve["Points"].as_array()) for (const toml::node& Index : *Indices) Decoded.Points.push_back(static_cast<int>(Index.value_or(int64_t{ -1 })));
            if (const toml::array* Knots = Curve["Knots"].as_array()) for (const toml::node& Knot : *Knots) Decoded.Knots.push_back(Knot.value_or(0.0));
            Decoded.Link.Sketch  = Curve["LinkSketch"].value_or(std::string{});
            Decoded.Link.Index   = static_cast<int>(Curve["LinkIndex"].value_or(int64_t{ -1 }));
            Sketch.Curves.push_back(Decoded);
        }
    }
    if (const toml::array* Constraints = View["Constraints"].as_array())
    {
        for (const toml::node& Entry : *Constraints)
        {
            const toml::node_view<const toml::node> Constraint{ Entry };
            SketchConstraint Decoded;
            Decoded.Kind        = static_cast<SketchConstraintCategory>(Constraint["Kind"].value_or(int64_t{ 0 }));
            Decoded.First       = static_cast<int>(Constraint["First"].value_or(int64_t{ -1 }));
            Decoded.Second      = static_cast<int>(Constraint["Second"].value_or(int64_t{ -1 }));
            Decoded.Third       = static_cast<int>(Constraint["Third"].value_or(int64_t{ -1 }));
            Decoded.Value       = Constraint["Value"].value_or(0.0);
            Decoded.SecondValue = Constraint["SecondValue"].value_or(0.0);
            Decoded.Parameter   = Constraint["Parameter"].value_or(std::string{});
            Decoded.Label       = Constraint["Label"].value_or(std::string{});
            Decoded.DrivenCondition = Constraint["Reference"].value_or(false);
            Sketch.Constraints.push_back(std::move(Decoded));
        }
    }
    if (const toml::array* Links = View["Links"].as_array())
    {
        for (const toml::node& Entry : *Links)
        {
            const toml::node_view<const toml::node> Link{ Entry };
            const int64_t Point = Link["Point"].value_or(int64_t{ -1 });
            if (Point < 0 || Point >= static_cast<int64_t>(Sketch.Points.size())) continue;
            Sketch.Points[Point].Link = { Link["Sketch"].value_or(std::string{}), static_cast<int>(Link["Index"].value_or(int64_t{ -1 })) };
        }
    }
    return Sketch;
}

// 📝 [Appearance]: BodyMatcap (default for every body), Bodies = [{ Body, Matcap }], Faces = [{ Face, Matcap }],
//    Matcaps = [{ Name, Path }]. Written only when it differs from the default (Studio, no bodies, no faces, no pictures), so
//    documents without matcaps keep their exact bytes.
[[nodiscard]] bool DefaultAppearanceCondition(const SketcherAppearance& Appearance)
{
    return Appearance.BodyMatcap == SketcherStudioMatcap && Appearance.BodyMatcaps.empty() && Appearance.FaceMatcaps.empty() && Appearance.HiddenBodies.empty()
        && Appearance.CustomMatcaps.empty() && Appearance.HiddenCurves.empty() && Appearance.LockedFigures.empty()
        && Appearance.RemovedCurves.empty() && Appearance.LiveCutters.empty();
}

[[nodiscard]] toml::table EncodeAppearance(const SketcherAppearance& Appearance)
{
    toml::array Bodies;
    for (const auto& [Body, Matcap] : Appearance.BodyMatcaps) Bodies.push_back(toml::table{ { "Body", Body }, { "Matcap", Matcap } });
    toml::array Faces;
    for (const auto& [Face, Matcap] : Appearance.FaceMatcaps) Faces.push_back(toml::table{ { "Face", Face }, { "Matcap", Matcap } });
    toml::array Matcaps;
    for (const SketcherCustomMatcap& Custom : Appearance.CustomMatcaps) Matcaps.push_back(toml::table{ { "Name", Custom.Name }, { "Path", Custom.Path } });
    toml::table Table{ { "BodyMatcap", Appearance.BodyMatcap } };
    if (!Bodies.empty()) Table.insert("Bodies", std::move(Bodies));
    if (!Faces.empty()) Table.insert("Faces", std::move(Faces));
    if (!Matcaps.empty()) Table.insert("Matcaps", std::move(Matcaps));
    if (!Appearance.HiddenBodies.empty())
    {
        toml::array Hidden;
        for (const std::string& Key : Appearance.HiddenBodies) Hidden.push_back(Key);
        Table.insert("Hidden", std::move(Hidden));
    }
    if (!Appearance.HiddenCurves.empty())
    {
        toml::array Hidden;
        for (const std::string& Label : Appearance.HiddenCurves) Hidden.push_back(Label);
        Table.insert("HiddenCurves", std::move(Hidden));
    }
    if (!Appearance.RemovedCurves.empty())
    {
        toml::array Removed;
        for (const std::string& Label : Appearance.RemovedCurves) Removed.push_back(Label);
        Table.insert("RemovedCurves", std::move(Removed));
    }
    if (!Appearance.LockedFigures.empty())
    {
        toml::array Locked;
        for (const std::string& Key : Appearance.LockedFigures) Locked.push_back(Key);
        Table.insert("Locked", std::move(Locked));
    }
    if (!Appearance.LiveCutters.empty())
    {
        toml::array Cutters;
        for (const std::string& Key : Appearance.LiveCutters) Cutters.push_back(Key);
        Table.insert("LiveCutters", std::move(Cutters));
    }
    return Table;
}

// Refuses malformed entries and names that are neither Studio, a built-in nor one of the file's own pictures.
[[nodiscard]] std::string DecodeAppearance(const toml::table* Table, SketcherAppearance& Appearance)
{
    Appearance = {};
    if (!Table) return {};
    if (const toml::array* Matcaps = (*Table)["Matcaps"].as_array())
    {
        for (const toml::node& Entry : *Matcaps)
        {
            const toml::node_view<const toml::node> View{ Entry };
            SketcherCustomMatcap Custom{ View["Name"].value_or(std::string{}), View["Path"].value_or(std::string{}) };
            if (Custom.Name.empty() || Custom.Path.empty()) return "Appearance: every matcap picture needs a Name and a Path";
            if (SketcherMatcapKnownCondition(Appearance, Custom.Name)) return "Appearance: matcap " + Custom.Name + " is named twice";
            Appearance.CustomMatcaps.push_back(std::move(Custom));
        }
    }
    Appearance.BodyMatcap = (*Table)["BodyMatcap"].value_or(std::string(SketcherStudioMatcap));
    if (!SketcherMatcapKnownCondition(Appearance, Appearance.BodyMatcap)) return "Appearance: no matcap named " + Appearance.BodyMatcap;
    if (const toml::array* Bodies = (*Table)["Bodies"].as_array())
    {
        for (const toml::node& Entry : *Bodies)
        {
            const toml::node_view<const toml::node> View{ Entry };
            const std::string Body   = View["Body"].value_or(std::string{});
            const std::string Matcap = View["Matcap"].value_or(std::string{});
            if (Body.empty()) return "Appearance: a body entry has no Body key";
            if (!SketcherMatcapKnownCondition(Appearance, Matcap)) return "Appearance: no matcap named " + Matcap + " (body " + Body + ")";
            Appearance.BodyMatcaps[Body] = Matcap;
        }
    }
    if (const toml::array* Faces = (*Table)["Faces"].as_array())
    {
        for (const toml::node& Entry : *Faces)
        {
            const toml::node_view<const toml::node> View{ Entry };
            const std::string Face   = View["Face"].value_or(std::string{});
            const std::string Matcap = View["Matcap"].value_or(std::string{});
            if (Face.empty()) return "Appearance: a face entry has no Face name";
            if (!SketcherMatcapKnownCondition(Appearance, Matcap)) return "Appearance: no matcap named " + Matcap + " (face " + Face + ")";
            Appearance.FaceMatcaps[Face] = Matcap;
        }
    }
    if (const toml::array* Hidden = (*Table)["Hidden"].as_array())
    {
        for (const toml::node& Entry : *Hidden)
        {
            const std::string Key = Entry.value_or(std::string{});
            if (Key.empty()) return "Appearance: a hidden entry is not a body key";
            Appearance.HiddenBodies.insert(Key);
        }
    }
    if (const toml::array* Hidden = (*Table)["HiddenCurves"].as_array())
    {
        for (const toml::node& Entry : *Hidden)
        {
            const std::string Label = Entry.value_or(std::string{});
            if (Label.empty()) return "Appearance: a hidden curve entry is not a step label";
            Appearance.HiddenCurves.insert(Label);
        }
    }
    if (const toml::array* Removed = (*Table)["RemovedCurves"].as_array())
    {
        for (const toml::node& Entry : *Removed)
        {
            const std::string Label = Entry.value_or(std::string{});
            if (Label.empty()) return "Appearance: a removed curve entry is not a step label";
            Appearance.RemovedCurves.insert(Label);
            Appearance.HiddenCurves.insert(Label);                                           // Removed ⊂ hidden
        }
    }
    if (const toml::array* Locked = (*Table)["Locked"].as_array())
    {
        for (const toml::node& Entry : *Locked)
        {
            const std::string Key = Entry.value_or(std::string{});
            if (Key.empty()) return "Appearance: a locked entry is not a body key or step label";
            Appearance.LockedFigures.insert(Key);
        }
    }
    if (const toml::array* Cutters = (*Table)["LiveCutters"].as_array())
    {
        for (const toml::node& Entry : *Cutters)
        {
            const std::string Key = Entry.value_or(std::string{});
            if (Key.empty()) return "Appearance: a live cutter entry is not a body key";
            Appearance.LiveCutters.insert(Key);
        }
    }
    return {};
}

// 📝 [DrawingSheet] (only when not the defaults): Paper "A4" … "A0", Angle "First" / "Third", Scale (0 = automatic),
//    Isometric, Section, SectionAxis "X" / "Y" / "Z" / "Face", SectionOffset, SectionFlipped, SectionFace, HiddenLines,
//    TangentEdges, Dimensions and the title block fields.
[[nodiscard]] toml::table EncodeDrawingSheet(const SketcherDrawingSheetSettings& Settings)
{
    static const char* Axes[] = { "X", "Y", "Z", "Face" };
    const SketcherTitleFields& Title = Settings.Title;
    return toml::table{ { "Paper", std::string(GeometrySpace::Parametric::DrawingPaperLabel(Settings.Paper)) }, { "Angle", std::string(Settings.Angle == GeometrySpace::Parametric::DrawingProjectionAngle::First ? "First" : "Third") },
                        { "Scale", Settings.Scale }, { "Isometric", Settings.IsometricEnabled }, { "Section", Settings.SectionEnabled },
                        { "SectionAxis", std::string(Axes[static_cast<int>(Settings.Section.Axis)]) }, { "SectionOffset", Settings.Section.Offset },
                        { "SectionFlipped", Settings.Section.Flipped }, { "SectionFace", Settings.Section.Face }, { "HiddenLines", Settings.HiddenLinesEnabled },
                        { "TangentEdges", Settings.TangentEdgesEnabled }, { "Dimensions", Settings.DimensionsEnabled }, { "Title", Title.Title },
                        { "DrawingNumber", Title.DrawingNumber }, { "Author", Title.Author }, { "Date", Title.Date }, { "Material", Title.Material },
                        { "Revision", Title.Revision }, { "Company", Title.Company } };
}

[[nodiscard]] std::string DecodeDrawingSheet(const toml::table* Table, SketcherDrawingSheetSettings& Settings)
{
    Settings = {};
    if (!Table) return {};
    const toml::table& Root = *Table;
    const std::string Paper = Root["Paper"].value_or(std::string("A3")), Angle = Root["Angle"].value_or(std::string("First")), Axis = Root["SectionAxis"].value_or(std::string("Y"));
    bool Known = false;
    for (int Index = 0; Index < 5; ++Index)
    {
        if (Paper == GeometrySpace::Parametric::DrawingPaperLabel(static_cast<GeometrySpace::Parametric::DrawingPaperSize>(Index))) { Settings.Paper = static_cast<GeometrySpace::Parametric::DrawingPaperSize>(Index); Known = true; }
    }
    if (!Known) return "DrawingSheet: unknown paper " + Paper + " (A4 … A0)";
    if (Angle != "First" && Angle != "Third") return "DrawingSheet: unknown projection angle " + Angle + " (First / Third)";
    Settings.Angle = Angle == "First" ? GeometrySpace::Parametric::DrawingProjectionAngle::First : GeometrySpace::Parametric::DrawingProjectionAngle::Third;
    Settings.Scale = Root["Scale"].value_or(0.0);
    if (!(Settings.Scale >= 0.0)) return "DrawingSheet: the scale must be 0 (automatic) or positive";
    static const char* Axes[] = { "X", "Y", "Z", "Face" };
    Known = false;
    for (int Index = 0; Index < 4; ++Index) if (Axis == Axes[Index]) { Settings.Section.Axis = static_cast<SketcherSectionAxis>(Index); Known = true; }
    if (!Known) return "DrawingSheet: unknown section axis " + Axis + " (X / Y / Z / Face)";
    Settings.IsometricEnabled    = Root["Isometric"].value_or(true);
    Settings.SectionEnabled      = Root["Section"].value_or(false);
    Settings.Section.Offset      = Root["SectionOffset"].value_or(0.0);
    Settings.Section.Flipped     = Root["SectionFlipped"].value_or(false);
    Settings.Section.Face        = Root["SectionFace"].value_or(std::string{});
    Settings.HiddenLinesEnabled  = Root["HiddenLines"].value_or(true);
    Settings.TangentEdgesEnabled = Root["TangentEdges"].value_or(true);
    Settings.DimensionsEnabled   = Root["Dimensions"].value_or(true);
    SketcherTitleFields& Title = Settings.Title;
    const SketcherTitleFields Defaults;
    Title.Title         = Root["Title"].value_or(Defaults.Title);
    Title.DrawingNumber = Root["DrawingNumber"].value_or(Defaults.DrawingNumber);
    Title.Author        = Root["Author"].value_or(Defaults.Author);
    Title.Date          = Root["Date"].value_or(Defaults.Date);
    Title.Material      = Root["Material"].value_or(Defaults.Material);
    Title.Revision      = Root["Revision"].value_or(Defaults.Revision);
    Title.Company       = Root["Company"].value_or(Defaults.Company);
    return {};
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string EncodeSketcherDocument(const SketcherDocument& Document)
{
    toml::array Parameters;
    for (const FeatureParameter& Parameter : Document.Tree.Parameters)
    {
        Parameters.push_back(toml::table{ { "Name", Parameter.Name }, { "Value", Parameter.Value }, { "Low", Parameter.Low },
                                          { "High", Parameter.High }, { "Description", Parameter.Description } });
    }
    toml::array Steps;
    for (const FeatureStep& Step : Document.Tree.Steps) Steps.push_back(EncodeStep(Step, Document.Captions));
    toml::table Root{ { "Format", "Sketcher" }, { "FormatVersion", FormatVersion }, { "Title", Document.Title },
                      { "Description", Document.Tree.Description }, { "Parameters", std::move(Parameters) }, { "Steps", std::move(Steps) } };
    if (!Document.Tree.EdgeGroups.empty())
    {
        toml::array Groups;
        for (const FeatureEdgeGroup& Group : Document.Tree.EdgeGroups)
        {
            toml::array Entries;
            for (const EdgeWeightEntry& Item : Group.Entries)
            {
                toml::array RefArray = EncodeReferences({ Item.Edge });
                toml::table Entry = *RefArray.front().as_table();
                Entry.insert("Weight", EncodeValue(Item.Weight));
                Entries.push_back(std::move(Entry));
            }
            toml::table Table{ { "Name", Group.Name }, { "Entries", std::move(Entries) } };
            if (!Group.Body.empty()) Table.insert("Body", Group.Body);
            Groups.push_back(std::move(Table));
        }
        Root.insert("EdgeGroups", std::move(Groups));
    }
    if (!Document.Tree.Modifiers.empty())
    {
        toml::array Modifiers;
        for (const FeatureModifier& Modifier : Document.Tree.Modifiers)
        {
            toml::table Table{ { "Label", Modifier.Label }, { "Kind", static_cast<int64_t>(Modifier.Kind) },
                               { "KindLabel", ModifierKindLabel(Modifier.Kind) }, { "Enabled", Modifier.EnabledCondition } };
            const auto CaptionIt = Document.Captions.find(Modifier.Label);
            if (CaptionIt != Document.Captions.end()) Table.insert("Caption", CaptionIt->second);
            if (!Modifier.Target.empty()) Table.insert("Target", Modifier.Target);
            if (!Modifier.Tool.empty()) Table.insert("Tool", Modifier.Tool);
            Table.insert("Combination", static_cast<int64_t>(Modifier.Combination));
            Table.insert("Offset", EncodeValue(Modifier.Offset));
            Table.insert("OffsetAxis", EncodeVector(Modifier.OffsetAxis));
            Table.insert("KeepTool", Modifier.KeepToolCondition);
            Table.insert("Shape", static_cast<int64_t>(Modifier.Shape));
            Table.insert("LimitMode", static_cast<int64_t>(Modifier.LimitMode));
            if (!Modifier.EdgeGroup.empty()) Table.insert("EdgeGroup", Modifier.EdgeGroup);
            if (!Modifier.References.empty()) Table.insert("References", EncodeReferences(Modifier.References));
            Table.insert("Size", EncodeValue(Modifier.Size));
            Table.insert("Angle", EncodeValue(Modifier.Angle));
            Table.insert("Count", EncodeValue(Modifier.Count));
            Table.insert("SecondaryCount", EncodeValue(Modifier.SecondaryCount));
            Table.insert("Direction", EncodeVector(Modifier.Direction));
            Table.insert("Distance", EncodeValue(Modifier.Distance));
            Table.insert("SecondaryDirection", EncodeVector(Modifier.SecondaryDirection));
            Table.insert("SecondaryDistance", EncodeValue(Modifier.SecondaryDistance));
            Table.insert("AxisOrigin", EncodeVector(Modifier.AxisOrigin));
            Table.insert("AxisDirection", EncodeVector(Modifier.AxisDirection));
            if (!Modifier.Curve.empty()) Table.insert("Curve", Modifier.Curve);
            Table.insert("DeformAxis", static_cast<int64_t>(Modifier.DeformAxis));
            Table.insert("Align", Modifier.AlignCondition);
            Table.insert("Stretch", Modifier.StretchCondition);
            Table.insert("Merge", Modifier.MergeCondition);
            Table.insert("Margin", EncodeValue(Modifier.Margin));
            Modifiers.push_back(std::move(Table));
        }
        Root.insert("Modifiers", std::move(Modifiers));
    }
    if (!Document.ImagePlanes.empty()) Root.insert("ImagePlanes", EncodeSketcherImagePlanes(Document.ImagePlanes));
    if (!Document.Dimensions.empty())
    {
        toml::array Dimensions;
        for (const SketcherDimension& Dim : Document.Dimensions)
        {
            toml::table Table{ { "Id", Dim.Id },
                               { "Kind", static_cast<int64_t>(Dim.Kind) },
                               { "FirstKind", static_cast<int64_t>(Dim.FirstKind) },
                               { "FirstName", Dim.FirstName },
                               { "SecondKind", static_cast<int64_t>(Dim.SecondKind) },
                               { "SecondName", Dim.SecondName },
                               { "FirstPoint", EncodeVector(Dim.FirstPoint) },
                               { "SecondPoint", EncodeVector(Dim.SecondPoint) },
                               { "PlaneNormal", EncodeVector(Dim.PlaneNormal) },
                               { "Direction", EncodeVector(Dim.Direction) },
                               { "Offset", Dim.Offset },
                               { "Value", Dim.Value },
                               { "AxisFilter", static_cast<int64_t>(Dim.AxisFilter) },
                               { "SlideAxis", static_cast<int64_t>(Dim.SlideAxis) },
                               { "Visible", Dim.VisibleCondition },
                               { "Note", Dim.Note } };
            Dimensions.push_back(std::move(Table));
        }
        Root.insert("Dimensions", std::move(Dimensions));
    }
    if (!Document.PreviewPngBytes.empty())
    {
        Root.insert("PreviewPng", EncodeSketcherRadix64(Document.PreviewPngBytes.data(), Document.PreviewPngBytes.size()));
    }
    if (!Document.ReferenceSurfaces.empty())
    {
        toml::array Surfaces;
        for (const SketcherReferenceSurface& Surf : Document.ReferenceSurfaces)
        {
            toml::table SurfaceEntry{
                { "Name", Surf.Name },
                { "SourcePath", Surf.SourcePath },
                { "SummaryNote", Surf.SummaryNote },
                { "ScaleFactor", Surf.ScaleFactor },
                { "Opacity", double(Surf.Opacity) },
                { "MatcapLayer", static_cast<int64_t>(Surf.MatcapLayer) },
                { "Visible", Surf.VisibleCondition },
                { "Wireframe", Surf.WireframeCondition },
                { "Locked", Surf.LockedCondition },
                { "MinBound", toml::array{ Surf.MinBound[0], Surf.MinBound[1], Surf.MinBound[2] } },
                { "MaxBound", toml::array{ Surf.MaxBound[0], Surf.MaxBound[1], Surf.MaxBound[2] } },
            };
            if (Surf.Geometry)
            {
                const std::string Blob = EncodeReferenceGeometryBlob(*Surf.Geometry);
                if (!Blob.empty()) SurfaceEntry.insert("GeometryBlob", Blob);
            }
            toml::array PartRows;
            for (const SketcherReferencePart& Part : Surf.Parts)
            {
                PartRows.push_back(toml::table{
                    { "Name", Part.Name },
                    { "Collection", Part.Collection },
                    { "ModifierSummary", Part.ModifierSummary },
                    { "FirstCorner", static_cast<int64_t>(Part.FirstCorner) },
                    { "CornerCount", static_cast<int64_t>(Part.CornerCount) },
                    { "FirstSegment", static_cast<int64_t>(Part.FirstSegment) },
                    { "SegmentCount", static_cast<int64_t>(Part.SegmentCount) },
                    { "SurfaceColour", toml::array{ double(Part.SurfaceColour[0]), double(Part.SurfaceColour[1]), double(Part.SurfaceColour[2]) } },
                    { "MinBound", toml::array{ Part.MinBound[0], Part.MinBound[1], Part.MinBound[2] } },
                    { "MaxBound", toml::array{ Part.MaxBound[0], Part.MaxBound[1], Part.MaxBound[2] } },
                    { "Visible", Part.VisibleCondition },
                    { "Locked", Part.LockedCondition },
                });
            }
            SurfaceEntry.insert("Parts", std::move(PartRows));
            Surfaces.push_back(std::move(SurfaceEntry));
        }
        Root.insert("ReferenceSurfaces", std::move(Surfaces));
    }
    if (!DefaultAppearanceCondition(Document.Appearance)) Root.insert("Appearance", EncodeAppearance(Document.Appearance));
    if (!DefaultSketcherDrawingSheetCondition(Document.DrawingSheet)) Root.insert("DrawingSheet", EncodeDrawingSheet(Document.DrawingSheet));
    std::ostringstream Stream;
    Stream << "# Sketcher document — feature tree of GeometryProcessing (regenerated on load)\n";
    Stream << Root << "\n";
    return Stream.str();
}

SketcherDocumentFile DecodeSketcherDocument(const std::string& Text)
{
    SketcherDocumentFile File;
    toml::table          Root;
    try
    {
        Root = toml::parse(Text);
    }
    catch (const toml::parse_error& Failure)
    {
        File.Refusal = std::string("TOML: ") + std::string(Failure.description());
        return File;
    }
    // 📝 "DraftingWorkspace" is the format name before the Sketcher rename (2026-10-23): those documents still open.
    const std::string Format = Root["Format"].value_or(std::string{});
    if (Format != "Sketcher" && Format != "DraftingWorkspace") { File.Refusal = "not a Sketcher document"; return File; }
    if (Root["FormatVersion"].value_or(int64_t{ 0 }) > FormatVersion) { File.Refusal = "document written by a newer Sketcher"; return File; }
    File.Title            = Root["Title"].value_or(std::string{ "Untitled" });
    File.Tree.Name        = File.Title;
    File.Tree.Description = Root["Description"].value_or(std::string{});
    if (std::string Refusal = DecodeSketcherImagePlanes(Root["ImagePlanes"].as_array(), 1.0, File.ImagePlanes); !Refusal.empty()) { File.Refusal = Refusal; return File; }
    if (std::string Refusal = DecodeAppearance(Root["Appearance"].as_table(), File.Appearance); !Refusal.empty()) { File.Refusal = Refusal; return File; }
    if (std::string Refusal = DecodeDrawingSheet(Root["DrawingSheet"].as_table(), File.DrawingSheet); !Refusal.empty()) { File.Refusal = Refusal; return File; }
    if (const toml::array* Parameters = Root["Parameters"].as_array())
    {
        for (const toml::node& Entry : *Parameters)
        {
            const toml::node_view<const toml::node> Parameter{ Entry };
            File.Tree.Parameters.push_back({ Parameter["Name"].value_or(std::string{}), Parameter["Value"].value_or(0.0),
                                             Parameter["Low"].value_or(0.0), Parameter["High"].value_or(1.0),
                                             Parameter["Description"].value_or(std::string{}) });
        }
    }
    if (const toml::array* Steps = Root["Steps"].as_array())
    {
        for (const toml::node& Entry : *Steps)
        {
            const toml::node_view<const toml::node> View{ Entry };
            FeatureStep Step;
            const int64_t Kind = View["Kind"].value_or(int64_t{ 0 });
            if (Kind < 0 || Kind > static_cast<int64_t>(FeatureStepCategory::Imprint)) { File.Refusal = "a step has an unknown kind " + std::to_string(Kind) + " (newer Sketcher?)"; return File; }
            Step.Kind  = static_cast<FeatureStepCategory>(Kind);
            Step.Label = View["Label"].value_or(std::string{});
            if (Step.Label.empty()) { File.Refusal = "a step has no label"; return File; }
            if (const auto Caption = View["Caption"].value<std::string>()) File.Captions[Step.Label] = *Caption;
            Step.Plane.Origin   = DecodeVector(View["PlaneOrigin"], Step.Plane.Origin);
            Step.Plane.XAxis    = DecodeVector(View["PlaneXAxis"], Step.Plane.XAxis);
            Step.Plane.YAxis    = DecodeVector(View["PlaneYAxis"], Step.Plane.YAxis);
            Step.Plane.ZAxis    = DecodeVector(View["PlaneZAxis"], Step.Plane.ZAxis);
            Step.Elevation      = DecodeValue(View["Elevation"]);
            Step.FillCondition  = View["Fill"].value_or(true);
            Step.Sketch         = DecodeSketch(View["Sketch"]);
            Step.Profile        = View["Profile"].value_or(std::string{});
            Step.Extent         = DecodeValue(View["Extent"]);
            Step.Start          = DecodeValue(View["Start"]);
            const int64_t Combination = View["Combination"].value_or(int64_t{ 0 });
            if (Combination < 0 || Combination > static_cast<int64_t>(FeatureCombination::Separate))
            {
                File.Refusal = Step.Label + " has an unknown combination " + std::to_string(Combination);
                return File;
            }
            Step.Combination    = static_cast<FeatureCombination>(Combination);
            Step.Target         = View["Target"].value_or(std::string{});
            Step.Tool           = View["Tool"].value_or(std::string{});
            Step.AxisIdentifier = static_cast<int>(View["AxisIdentifier"].value_or(int64_t{ -1 }));
            Step.Angle          = DecodeValue(View["Angle"], Step.Angle);
            const auto Planar = [&](const char* Key, Vector2 Fallback)
            {
                const toml::array* Pair = View[Key].as_array();
                if (!Pair || Pair->size() != 2) return Fallback;
                return Vector2{ (*Pair)[0].value_or(Fallback.X), (*Pair)[1].value_or(Fallback.Y) };
            };
            Step.AxisOrigin     = Planar("AxisOrigin", Step.AxisOrigin);
            Step.AxisDirection  = Planar("AxisDirection", Step.AxisDirection);
            Step.Path           = View["Path"].value_or(std::string{});
            Step.Shape          = static_cast<BlendProfile>(View["Shape"].value_or(int64_t{ 0 }));
            Step.Size           = DecodeValue(View["Size"]);
            Step.Operation      = static_cast<FaceOperationCategory>(View["Operation"].value_or(int64_t{ 1 }));
            Step.Amount         = DecodeValue(View["Amount"]);
            Step.Placement.ColumnX = DecodeVector(View["PlacementX"], Step.Placement.ColumnX);
            Step.Placement.ColumnY = DecodeVector(View["PlacementY"], Step.Placement.ColumnY);
            Step.Placement.ColumnZ = DecodeVector(View["PlacementZ"], Step.Placement.ColumnZ);
            Step.Placement.Shift   = DecodeVector(View["PlacementShift"], Step.Placement.Shift);
            Step.Source            = View["Source"].value_or(std::string{});
            if (Step.Kind == FeatureStepCategory::Joint)
            {
                const int64_t JointCode = View["JointKind"].value_or(int64_t{ 0 });
                if (JointCode < 0 || JointCode > static_cast<int64_t>(JointCategory::Ball)) { File.Refusal = Step.Label + " has an unknown joint kind " + std::to_string(JointCode); return File; }
                FeatureJoint& Joint = Step.Joint;
                Joint.Kind             = static_cast<JointCategory>(JointCode);
                Joint.Angle            = DecodeValue(View["JointAngle"]);
                Joint.Slide            = DecodeValue(View["JointSlide"]);
                Joint.SecondSlide      = DecodeValue(View["JointSecondSlide"]);
                Joint.Pitch            = DecodeValue(View["JointPitch"]);
                Joint.Roll             = DecodeValue(View["JointRoll"]);
                Joint.Offset           = DecodeValue(View["JointOffset"]);
                Joint.FlippedCondition = View["JointFlipped"].value_or(false);
                const auto Range = [&](const char* Key) -> std::optional<JointRange>
                {
                    const toml::array* Pair = View[Key].as_array();
                    if (!Pair || Pair->size() != 2) return std::nullopt;
                    return JointRange{ (*Pair)[0].value_or(0.0), (*Pair)[1].value_or(0.0) };
                };
                Joint.AngleRange = Range("JointAngleRange");
                Joint.SlideRange = Range("JointSlideRange");
            }
            if (Step.Kind == FeatureStepCategory::Import)
            {
                Step.Exchange     = View["Exchange"].value_or(std::string{});
                Step.ExchangeUnit = View["ExchangeUnit"].value_or(1.0);
                if (Step.Exchange.empty()) { File.Refusal = Step.Label + " has no embedded STEP text"; return File; }
            }
            Step.References = DecodeReferences(View["References"]);
            if (View["EndSize"])
            {
                Step.EndSize     = DecodeValue(View["EndSize"]);
                Step.SizeAnchors = DecodeReferences(View["SizeAnchors"]);
            }
            else if (const toml::array* Sizes = View["AnchorSizes"].as_array())
            {
                Step.SizeAnchors = DecodeReferences(View["SizeAnchors"]);
                for (const toml::node& Size : *Sizes) Step.AnchorSizes.push_back(DecodeValue(toml::node_view<const toml::node>{ Size }));
                if (Step.AnchorSizes.size() != Step.SizeAnchors.size()) { File.Refusal = Step.Label + " has " + std::to_string(Step.AnchorSizes.size()) + " vertex sizes for " + std::to_string(Step.SizeAnchors.size()) + " vertices"; return File; }
            }
            if (const toml::array* Points = View["Points"].as_array())
            {
                for (const toml::node& Point : *Points) Step.Points.push_back(DecodeVector(toml::node_view<const toml::node>{ Point }, Vector3{ std::nan(""), 0.0, 0.0 }));
            }
            Step.ClosedCondition = View["Closed"].value_or(false);
            Step.PlanarCondition = View["Planar"].value_or(false);
            Step.ReverseNormals = View["ReverseNormals"].value_or(false);
            Step.CurveOffsetGuide=View["CurveOffsetGuide"].value_or(std::string{});
            Step.CurveOffsetBody=View["CurveOffsetBody"].value_or(std::string{});
            Step.CurveOffsetOrigin=DecodeVector(View["CurveOffsetOrigin"],Vector3{});
            Step.CurveOffsetDistance=View["CurveOffsetDistance"].value_or(0.05);
            Step.CurveOffsetTolerance=View["CurveOffsetTolerance"].value_or(0.002);
            Step.Continuity      = static_cast<int>(View["Continuity"].value_or(int64_t{ 0 }));
            Step.Tolerance       = DecodeValue(View["Tolerance"], Step.Tolerance);
            if (const toml::array* Knots = View["CurveKnots"].as_array())
                for (const toml::node& Knot : *Knots) Step.CurveKnots.push_back(Knot.value_or(0.0));
            if (const toml::array* Edits = View["PoleEdits"].as_array())
            {
                for (const toml::node& Edit : *Edits)
                {
                    const toml::node_view<const toml::node> Entry{ Edit };
                    Step.PoleEdits.push_back({ static_cast<int>(Entry["Index"].value_or(int64_t{ -1 })), DecodeVector(Entry["Position"], Vector3{}) });
                }
            }
            if (const toml::array* Sections = View["Sections"].as_array())
            {
                for (const toml::node& Section : *Sections) Step.Sections.push_back(Section.value_or(std::string{}));
            }
            if (View["Offset"]) Step.Offset = DecodeValue(View["Offset"]);
            Step.OffsetAxis = DecodeVector(View["OffsetAxis"], Step.OffsetAxis);
            File.Tree.Steps.push_back(std::move(Step));
        }
    }
    if (const toml::array* Groups = Root["EdgeGroups"].as_array())
    {
        for (const toml::node& Node : *Groups)
        {
            const toml::node_view<const toml::node> View{ Node };
            FeatureEdgeGroup Group;
            Group.Name = View["Name"].value_or(std::string{});
            Group.Body = View["Body"].value_or(std::string{});
            if (Group.Name.empty()) { File.Refusal = "an EdgeGroup has no Name"; return File; }
            if (const toml::array* Entries = View["Entries"].as_array())
            {
                for (const toml::node& RawEntry : *Entries)
                {
                    const toml::node_view<const toml::node> Item{ RawEntry };
                    toml::array SingleRef;
                    if (const toml::table* Table = RawEntry.as_table()) SingleRef.push_back(*Table);
                    std::vector<ConnectivityReference> DecodedRefs = DecodeReferences(toml::node_view<const toml::node>{ SingleRef });
                    if (DecodedRefs.empty()) continue;
                    Group.Entries.push_back({ std::move(DecodedRefs.front()), DecodeValue(Item["Weight"], FeatureScalar{ 1.0 }) });
                }
            }
            File.Tree.EdgeGroups.push_back(std::move(Group));
        }
    }
    if (const toml::array* Modifiers = Root["Modifiers"].as_array())
    {
        for (const toml::node& Node : *Modifiers)
        {
            const toml::node_view<const toml::node> View{ Node };
            FeatureModifier Modifier;
            const int64_t Kind = View["Kind"].value_or(int64_t{ 0 });
            if (Kind < 0 || Kind > static_cast<int64_t>(ModifierCategory::CurveBend))
            {
                File.Refusal = "a modifier has an unknown kind " + std::to_string(Kind);
                return File;
            }
            Modifier.Kind             = static_cast<ModifierCategory>(Kind);
            Modifier.Label            = View["Label"].value_or(std::string{});
            if (Modifier.Label.empty()) { File.Refusal = "a modifier has no Label"; return File; }
            if (const auto Caption = View["Caption"].value<std::string>()) File.Captions[Modifier.Label] = *Caption;
            Modifier.EnabledCondition = View["Enabled"].value_or(true);
            Modifier.Target           = View["Target"].value_or(std::string{});
            Modifier.Tool             = View["Tool"].value_or(std::string{});
            Modifier.Combination      = static_cast<FeatureCombination>(View["Combination"].value_or(int64_t{ 2 }));
            Modifier.Offset           = DecodeValue(View["Offset"], Modifier.Offset);
            Modifier.OffsetAxis       = DecodeVector(View["OffsetAxis"], Modifier.OffsetAxis);
            Modifier.KeepToolCondition = View["KeepTool"].value_or(true);
            Modifier.Shape            = static_cast<BlendProfile>(View["Shape"].value_or(int64_t{ 0 }));
            Modifier.LimitMode        = static_cast<BevelLimitMode>(View["LimitMode"].value_or(int64_t{ 0 }));
            Modifier.EdgeGroup        = View["EdgeGroup"].value_or(std::string{});
            Modifier.References       = DecodeReferences(View["References"]);
            Modifier.Size             = DecodeValue(View["Size"], Modifier.Size);
            Modifier.Angle            = DecodeValue(View["Angle"], Modifier.Angle);
            Modifier.Count            = DecodeValue(View["Count"], Modifier.Count);
            Modifier.SecondaryCount   = DecodeValue(View["SecondaryCount"], Modifier.SecondaryCount);
            Modifier.Direction        = DecodeVector(View["Direction"], Modifier.Direction);
            Modifier.Distance         = DecodeValue(View["Distance"], Modifier.Distance);
            Modifier.SecondaryDirection = DecodeVector(View["SecondaryDirection"], Modifier.SecondaryDirection);
            Modifier.SecondaryDistance  = DecodeValue(View["SecondaryDistance"], Modifier.SecondaryDistance);
            Modifier.AxisOrigin       = DecodeVector(View["AxisOrigin"], Modifier.AxisOrigin);
            Modifier.AxisDirection    = DecodeVector(View["AxisDirection"], Modifier.AxisDirection);
            Modifier.Curve            = View["Curve"].value_or(std::string{});
            Modifier.DeformAxis       = static_cast<int>(View["DeformAxis"].value_or(int64_t{ -1 }));
            Modifier.AlignCondition   = View["Align"].value_or(true);
            Modifier.StretchCondition = View["Stretch"].value_or(true);
            Modifier.MergeCondition   = View["Merge"].value_or(false);
            Modifier.Margin           = DecodeValue(View["Margin"], Modifier.Margin);
            File.Tree.Modifiers.push_back(std::move(Modifier));
        }
    }
    if (const toml::array* Dimensions = Root["Dimensions"].as_array())
    {
        for (const toml::node& Node : *Dimensions)
        {
            const toml::node_view<const toml::node> View{ Node };
            SketcherDimension Dim;
            Dim.Id = View["Id"].value_or(std::string{});
            if (Dim.Id.empty()) { File.Refusal = "a dimension entry has no Id"; return File; }
            const int64_t Kind = View["Kind"].value_or(int64_t{ 0 });
            if (Kind < 0 || Kind > static_cast<int64_t>(SketcherDimensionCategory::Angular))
            {
                File.Refusal = "a dimension has an unknown Kind " + std::to_string(Kind);
                return File;
            }
            Dim.Kind             = static_cast<SketcherDimensionCategory>(Kind);
            Dim.FirstKind        = static_cast<SketcherSelectionCategory>(std::clamp<int64_t>(View["FirstKind"].value_or(int64_t{ 0 }), 0, 3));
            Dim.FirstName        = View["FirstName"].value_or(std::string{});
            Dim.SecondKind       = static_cast<SketcherSelectionCategory>(std::clamp<int64_t>(View["SecondKind"].value_or(int64_t{ 0 }), 0, 3));
            Dim.SecondName       = View["SecondName"].value_or(std::string{});
            Dim.FirstPoint       = DecodeVector(View["FirstPoint"], Dim.FirstPoint);
            Dim.SecondPoint      = DecodeVector(View["SecondPoint"], Dim.SecondPoint);
            Dim.PlaneNormal      = DecodeVector(View["PlaneNormal"], Dim.PlaneNormal);
            Dim.Direction        = DecodeVector(View["Direction"], Dim.Direction);
            Dim.Offset           = View["Offset"].value_or(0.35);
            Dim.Value            = View["Value"].value_or(0.0);
            Dim.AxisFilter       = static_cast<int>(View["AxisFilter"].value_or(int64_t{ -1 }));
            Dim.SlideAxis        = static_cast<int>(std::clamp<int64_t>(View["SlideAxis"].value_or(int64_t{ -1 }), -1, 2));
            Dim.VisibleCondition = View["Visible"].value_or(true);
            Dim.Note             = View["Note"].value_or(std::string{});
            File.Dimensions.push_back(std::move(Dim));
        }
    }
    if (const std::string PreviewB64 = Root["PreviewPng"].value_or(std::string{}); !PreviewB64.empty())
    {
        bool PreviewOk = false;
        std::vector<uint8_t> Decoded = DecodeSketcherRadix64(PreviewB64, &PreviewOk);
        if (PreviewOk && !Decoded.empty()) File.PreviewPngBytes = std::move(Decoded);
    }
    if (const toml::array* RefSurfaces = Root["ReferenceSurfaces"].as_array())
    {
        auto ReadArray3 = [](const toml::node_view<const toml::node>& ArrView, std::array<double, 3> Fallback) -> std::array<double, 3>
        {
            if (const toml::array* Arr = ArrView.as_array(); Arr && Arr->size() >= 3u)
            {
                return { (*Arr)[0].value_or(Fallback[0]), (*Arr)[1].value_or(Fallback[1]), (*Arr)[2].value_or(Fallback[2]) };
            }
            return Fallback;
        };

        for (const toml::node& Entry : *RefSurfaces)
        {
            const toml::node_view<const toml::node> View{ Entry };
            SketcherReferenceSurface Surf;
            Surf.Name               = View["Name"].value_or(std::string{ "Reference" });
            Surf.SourcePath         = View["SourcePath"].value_or(std::string{});
            Surf.SummaryNote        = View["SummaryNote"].value_or(std::string{});
            Surf.ScaleFactor        = View["ScaleFactor"].value_or(1.0);
            Surf.Opacity            = static_cast<float>(std::clamp(View["Opacity"].value_or(0.88), 0.05, 1.0));
            Surf.MatcapLayer        = static_cast<uint32_t>(std::max<int64_t>(0, View["MatcapLayer"].value_or(int64_t{ 0 })));
            Surf.VisibleCondition   = View["Visible"].value_or(true);
            Surf.WireframeCondition = View["Wireframe"].value_or(true);
            Surf.LockedCondition    = View["Locked"].value_or(false);
            Surf.MinBound           = ReadArray3(View["MinBound"], { 0.0, 0.0, 0.0 });
            Surf.MaxBound           = ReadArray3(View["MaxBound"], { 0.0, 0.0, 0.0 });

            const std::string Blob = View["GeometryBlob"].value_or(std::string{});
            if (!Blob.empty())
            {
                std::string BlobRefusal;
                Surf.Geometry = DecodeReferenceGeometryBlob(Blob, BlobRefusal);
                if (!Surf.Geometry)
                {
                    File.Refusal = "ReferenceSurface '" + Surf.Name + "': " + BlobRefusal;
                    return File;
                }
            }

            if (const toml::array* PartArr = View["Parts"].as_array())
            {
                Surf.Parts.reserve(PartArr->size());
                for (const toml::node& PEntry : *PartArr)
                {
                    const toml::node_view<const toml::node> PView{ PEntry };
                    SketcherReferencePart Part;
                    Part.Name             = PView["Name"].value_or(std::string{});
                    Part.Collection       = PView["Collection"].value_or(std::string{ "Scene" });
                    Part.ModifierSummary  = PView["ModifierSummary"].value_or(std::string{});
                    Part.FirstCorner      = static_cast<uint32_t>(std::max<int64_t>(0, PView["FirstCorner"].value_or(int64_t{ 0 })));
                    Part.CornerCount      = static_cast<uint32_t>(std::max<int64_t>(0, PView["CornerCount"].value_or(int64_t{ 0 })));
                    Part.FirstSegment     = static_cast<uint32_t>(std::max<int64_t>(0, PView["FirstSegment"].value_or(int64_t{ 0 })));
                    Part.SegmentCount     = static_cast<uint32_t>(std::max<int64_t>(0, PView["SegmentCount"].value_or(int64_t{ 0 })));
                    const auto Col        = ReadArray3(PView["SurfaceColour"], { 0.68, 0.72, 0.76 });
                    Part.SurfaceColour    = { float(Col[0]), float(Col[1]), float(Col[2]) };
                    Part.MinBound         = ReadArray3(PView["MinBound"], { 0.0, 0.0, 0.0 });
                    Part.MaxBound         = ReadArray3(PView["MaxBound"], { 0.0, 0.0, 0.0 });
                    Part.VisibleCondition = PView["Visible"].value_or(true);
                    Part.LockedCondition  = PView["Locked"].value_or(false);
                    Surf.Parts.push_back(std::move(Part));
                }
            }
            File.ReferenceSurfaces.push_back(std::move(Surf));
        }
    }
    return File;
}

bool SaveSketcherDocument(const SketcherDocument& Document, const std::string& Path)
{
    FRONTIER_TRACE_SCOPE("File", "Save · " + Path);
    // 📝 The folder is created on first save (a fresh install has no autosave folder yet); failure shows as a closed stream.
    const std::filesystem::path Target = GeometrySpace::Parametric::ResolveUtf8Path(Path);
    std::error_code Failure;
    if (Target.has_parent_path()) std::filesystem::create_directories(Target.parent_path(), Failure);
    std::ofstream Stream(Target, std::ios::binary);
    if (!Stream) return false;
    Stream << EncodeSketcherDocument(Document);
    return static_cast<bool>(Stream);
}

SketcherDocumentFile LoadSketcherDocumentFile(const std::string& Path)
{
    FRONTIER_TRACE_SCOPE("File", "Load · " + Path);
    std::ifstream Stream(GeometrySpace::Parametric::ResolveUtf8Path(Path), std::ios::binary);
    if (!Stream)
    {
        SketcherDocumentFile File;
        File.Refusal = "cannot open " + Path;
        return File;
    }
    std::ostringstream Text;
    Text << Stream.rdbuf();
    SketcherDocumentFile File = DecodeSketcherDocument(Text.str());
    for (SketcherImagePlane& Plane : File.ImagePlanes) Plane.Path = ResolveSketcherImagePath(Plane.Path, Path);
    for (SketcherCustomMatcap& Custom : File.Appearance.CustomMatcaps) Custom.Path = ResolveSketcherImagePath(Custom.Path, Path);
    return File;
}

bool LoadSketcherPreviewHeader(
    const std::string&    Path,
    std::string&          OutTitle,
    std::string&          OutDescription,
    std::vector<uint8_t>& OutPreviewPng)
{
    OutTitle.clear();
    OutDescription.clear();
    OutPreviewPng.clear();

    std::ifstream Stream(GeometrySpace::Parametric::ResolveUtf8Path(Path), std::ios::binary);
    if (!Stream) return false;

    // Read only top-level key/value lines before the first [[...]] table section (fast < 0.3 ms even for multi-MB embedded vehicles)
    std::string HeaderText;
    HeaderText.reserve(65536u);
    std::string Line;
    size_t BytesRead = 0u;
    constexpr size_t MaxHeaderBytes = 512u * 1024u;
    while (BytesRead < MaxHeaderBytes && std::getline(Stream, Line))
    {
        if (!Line.empty() && Line[0] == '[') break;
        HeaderText.append(Line);
        HeaderText.push_back('\n');
        BytesRead += Line.size() + 1u;
    }

    try
    {
        const toml::table Root = toml::parse(HeaderText);
        OutTitle       = Root["Title"].value_or(std::string{});
        OutDescription = Root["Description"].value_or(std::string{});
        const std::string PreviewB64 = Root["PreviewPng"].value_or(std::string{});
        if (!PreviewB64.empty())
        {
            bool Ok = false;
            std::vector<uint8_t> Decoded = DecodeSketcherRadix64(PreviewB64, &Ok);
            if (Ok && !Decoded.empty()) OutPreviewPng = std::move(Decoded);
        }
        return !OutTitle.empty() || !OutDescription.empty() || !OutPreviewPng.empty();
    }
    catch (const toml::parse_error&)
    {
        return false;
    }
}

} // namespace Frontier::Workspaces::Sketcher
