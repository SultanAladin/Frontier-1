/*====================================================================================================================================
                                                          REPLAYDIGEST.CPP
====================================================================================================================================*/
// 🧩 Byte-exact step digests: fields in declaration order, then the bound parameter values

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Feature/ReplayDigest.h"

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   PRIVATE FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{
    // 📝 Appends fixed-width little-endian words; strings carry their length so concatenations cannot collide.
    class DigestWriter
    {
    public:
        explicit DigestWriter(const FeatureTree& Tree) : TreeReference(Tree) {}

        void Word(uint64_t Value)
        {
            for (int Byte = 0; Byte < 8; ++Byte) Text.push_back(static_cast<char>((Value >> (8 * Byte)) & 0xFFu));
        }
        void Number(double Value)
        {
            uint64_t Bits = 0;
            std::memcpy(&Bits, &Value, sizeof(Bits));
            Word(Bits);
        }
        void Integer(long long Value) { Word(static_cast<uint64_t>(Value)); }
        void Flag(bool Value)         { Word(Value ? 1u : 0u); }
        template <typename Enumeration> void Kind(Enumeration Value) { Word(static_cast<uint64_t>(static_cast<std::underlying_type_t<Enumeration>>(Value))); }
        void Phrase(const std::string& Value)
        {
            Word(Value.size());
            Text.append(Value);
        }
        void Point(const Vector2& Value) { Number(Value.X); Number(Value.Y); }
        void Point(const Vector3& Value) { Number(Value.X); Number(Value.Y); Number(Value.Z); }
        void Frame(const OrthonormalBasis& Value) { Point(Value.Origin); Point(Value.XAxis); Point(Value.YAxis); Point(Value.ZAxis); }
        void Map(const AffineMapping& Value) { Point(Value.ColumnX); Point(Value.ColumnY); Point(Value.ColumnZ); Point(Value.Shift); }

        // A bound parameter contributes its name and current value (absent: a sentinel, so creating it changes the digest).
        void Binding(const std::string& Name)
        {
            Phrase(Name);
            if (Name.empty()) return;
            for (const FeatureParameter& Parameter : TreeReference.Parameters)
            {
                if (Parameter.Name != Name) continue;
                Flag(true);
                Number(Parameter.Value);
                return;
            }
            Flag(false);
        }
        void Value(const FeatureScalar& Value)
        {
            Number(Value.Literal);
            Binding(Value.Parameter);
            Number(Value.Factor);
            Number(Value.Offset);
            Binding(Value.SecondParameter);
            Number(Value.SecondFactor);
        }
        void Reference(const ConnectivityReference& Value)
        {
            Kind(Value.Kind);
            Phrase(Value.Name);
            Flag(Value.Signature.CapturedCondition);
            Point(Value.Signature.Position);
            Point(Value.Signature.Direction);
            Phrase(Value.Signature.Category);
            Number(Value.Signature.Size);
        }
        void Link(const SketchLink& Value) { Phrase(Value.Sketch); Integer(Value.Index); }
        void Sketch(const SketchModel& Model)
        {
            Word(Model.Points.size());
            for (const SketchPoint& Point : Model.Points) { this->Point(Point.Position); Link(Point.Link); }
            Word(Model.Curves.size());
            for (const SketchCurve& Curve : Model.Curves)
            {
                Kind(Curve.Kind);
                Integer(Curve.Identifier);
                Integer(Curve.Start);
                Integer(Curve.End);
                Integer(Curve.Centre);
                Number(Curve.Radius);
                Flag(Curve.Construction);
                Kind(Curve.Form);
                Word(Curve.Points.size());
                for (int Index : Curve.Points) Integer(Index);
                Link(Curve.Link);
                if (!Curve.Knots.empty())                                                    // Older digests stay as they were
                {
                    Word(Curve.Knots.size());
                    for (double Knot : Curve.Knots) Number(Knot);
                }
            }
            Word(Model.Constraints.size());
            for (const SketchConstraint& Constraint : Model.Constraints)
            {
                Kind(Constraint.Kind);
                Integer(Constraint.First);
                Integer(Constraint.Second);
                Integer(Constraint.Third);
                Number(Constraint.Value);
                Number(Constraint.SecondValue);
                Binding(Constraint.Parameter);
                Phrase(Constraint.Label);
                Flag(Constraint.DrivenCondition);
            }
        }

        std::string Text;                                                                    // [-] - Digest so far

    private:
        const FeatureTree& TreeReference;                                                    // [-] - Parameter values
    };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

std::string DigestFeatureStep(const FeatureTree& Tree, const FeatureStep& Step)
{
    DigestWriter Writer(Tree);
    Writer.Kind(Step.Kind);
    Writer.Phrase(Step.Label);
    Writer.Sketch(Step.Sketch);
    Writer.Frame(Step.Plane);
    Writer.Value(Step.Elevation);
    Writer.Phrase(Step.Profile);
    Writer.Value(Step.Extent);
    if (Step.Start.Literal != 0.0 || !Step.Start.Parameter.empty()) Writer.Value(Step.Start);   // Absent before start offsets: digests unchanged
    Writer.Kind(Step.Combination);
    Writer.Integer(Step.AxisIdentifier);
    Writer.Point(Step.AxisOrigin);
    Writer.Point(Step.AxisDirection);
    Writer.Phrase(Step.Path);
    Writer.Value(Step.Angle);
    Writer.Kind(Step.Shape);
    Writer.Value(Step.Size);
    Writer.Flag(Step.EndSize.has_value());
    if (Step.EndSize) Writer.Value(*Step.EndSize);
    Writer.Word(Step.SizeAnchors.size());
    for (const ConnectivityReference& Anchor : Step.SizeAnchors) Writer.Reference(Anchor);
    Writer.Word(Step.AnchorSizes.size());
    for (const FeatureScalar& Size : Step.AnchorSizes) Writer.Value(Size);
    Writer.Kind(Step.Operation);
    Writer.Value(Step.Amount);
    Writer.Word(Step.References.size());
    for (const ConnectivityReference& Reference : Step.References) Writer.Reference(Reference);
    Writer.Word(Step.Points.size());
    for (const Vector3& Point : Step.Points) Writer.Point(Point);
    Writer.Flag(Step.ClosedCondition);
    if (!Step.FillCondition) Writer.Phrase("Unfilled");                                     // Absent when filled: older digests hold
    Writer.Flag(Step.PlanarCondition);
    Writer.Flag(Step.ReverseNormals);
    Writer.Value(Step.Tolerance);
    if (!Step.CurveKnots.empty())                                                            // Absent for fitted curves: older digests hold
    {
        Writer.Phrase("Exact");
        Writer.Word(Step.CurveKnots.size());
        for (double Knot : Step.CurveKnots) Writer.Number(Knot);
    }
    if (!Step.CurveOffsetGuide.empty()) {
        Writer.Phrase("SurfaceDistanceOffset"); Writer.Phrase(Step.CurveOffsetGuide); Writer.Phrase(Step.CurveOffsetBody); Writer.Point(Step.CurveOffsetOrigin);
        Writer.Number(Step.CurveOffsetDistance); Writer.Number(Step.CurveOffsetTolerance);
    }
    Writer.Word(Step.PoleEdits.size());
    for (const FeaturePoleEdit& Edit : Step.PoleEdits) { Writer.Integer(Edit.Index); Writer.Point(Edit.Position); }
    Writer.Word(Step.Sections.size());
    for (const std::string& Section : Step.Sections) Writer.Phrase(Section);
    Writer.Integer(Step.Continuity);
    Writer.Phrase(Step.Target);
    Writer.Phrase(Step.Tool);
    Writer.Map(Step.Placement);
    Writer.Phrase(Step.Source);
    const FeatureJoint& Joint = Step.Joint;
    Writer.Kind(Joint.Kind);
    for (const FeatureScalar* Value : { &Joint.Angle, &Joint.Slide, &Joint.SecondSlide, &Joint.Pitch, &Joint.Roll, &Joint.Offset }) Writer.Value(*Value);
    Writer.Flag(Joint.FlippedCondition);
    for (const std::optional<JointRange>* Range : { &Joint.AngleRange, &Joint.SlideRange })
    {
        Writer.Flag(Range->has_value());
        if (Range->has_value()) { Writer.Number((*Range)->Low); Writer.Number((*Range)->High); }
    }
    Writer.Phrase(Step.Exchange);
    Writer.Number(Step.ExchangeUnit);
    if (Step.Kind == FeatureStepCategory::SpaceCurve)
    {
        for (const FeatureStep& Other : Tree.Steps) if (Other.Kind == FeatureStepCategory::Mirror) Writer.Frame(Other.Plane);
    }
    if (Step.Offset.Literal != 0.0 || !Step.Offset.Parameter.empty() || !Step.Offset.SecondParameter.empty() || Step.Offset.Offset != 0.0)
    {
        Writer.Phrase("StepOffset");
        Writer.Value(Step.Offset);
        Writer.Point(Step.OffsetAxis);
    }
    return std::move(Writer.Text);
}

std::string DigestModifiers(const FeatureTree& Tree)
{
    if (Tree.Modifiers.empty() && Tree.EdgeGroups.empty()) return {};
    DigestWriter Writer(Tree);
    Writer.Word(Tree.EdgeGroups.size());
    for (const FeatureEdgeGroup& Group : Tree.EdgeGroups)
    {
        Writer.Phrase(Group.Name);
        Writer.Phrase(Group.Body);
        Writer.Word(Group.Entries.size());
        for (const EdgeWeightEntry& Entry : Group.Entries)
        {
            Writer.Reference(Entry.Edge);
            Writer.Value(Entry.Weight);
        }
    }
    Writer.Word(Tree.Modifiers.size());
    for (const FeatureModifier& Modifier : Tree.Modifiers)
    {
        Writer.Phrase(Modifier.Label);
        Writer.Kind(Modifier.Kind);
        Writer.Flag(Modifier.EnabledCondition);
        Writer.Phrase(Modifier.Target);
        Writer.Phrase(Modifier.Tool);
        Writer.Kind(Modifier.Combination);
        Writer.Value(Modifier.Offset);
        Writer.Point(Modifier.OffsetAxis);
        Writer.Flag(Modifier.KeepToolCondition);
        Writer.Kind(Modifier.Shape);
        Writer.Kind(Modifier.LimitMode);
        Writer.Phrase(Modifier.EdgeGroup);
        Writer.Word(Modifier.References.size());
        for (const ConnectivityReference& Reference : Modifier.References) Writer.Reference(Reference);
        Writer.Value(Modifier.Size);
        Writer.Value(Modifier.Angle);
        Writer.Value(Modifier.Count);
        Writer.Value(Modifier.SecondaryCount);
        Writer.Point(Modifier.Direction);
        Writer.Value(Modifier.Distance);
        Writer.Point(Modifier.SecondaryDirection);
        Writer.Value(Modifier.SecondaryDistance);
        Writer.Point(Modifier.AxisOrigin);
        Writer.Point(Modifier.AxisDirection);
        Writer.Phrase(Modifier.Curve);
        Writer.Integer(Modifier.DeformAxis);
        Writer.Flag(Modifier.AlignCondition);
        Writer.Flag(Modifier.StretchCondition);
        Writer.Flag(Modifier.MergeCondition);
        Writer.Value(Modifier.Margin);
    }
    return std::move(Writer.Text);
}

} // namespace Frontier::GeometrySpace::Parametric
