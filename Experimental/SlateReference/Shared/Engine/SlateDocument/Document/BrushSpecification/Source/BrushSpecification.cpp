//============================================================================================================================================
//                                                         BRUSHSPECIFICATION.CPP
//============================================================================================================================================
// 🧩 Declared progressions, the fallback an absent axis takes, and the stroke-seeded sequence both sides share.

#include "SlateDocument/Document/BrushSpecification/Api/BrushSpecification.h"

#include "Shared/SampleProjection.slang.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROGRESSIONS
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 Each progression carries the closed unit interval to itself and fixes both ends, so a dynamic at its low end
//    gives the declared lower factor exactly and at its high end the upper one. A progression that did not fix
//    the ends would make a brush at full pressure texture something other than its declared maximum.
double Progress(ProgressionSubject Declared, double Fraction)
{
    const double Bounded = Fraction < 0.0 ? 0.0 : (Fraction > 1.0 ? 1.0 : Fraction);

    if (Declared == ProgressionSubject::Quadratic)
        return Bounded * Bounded;

    if (Declared == ProgressionSubject::Radical)
        return std::sqrt(Bounded);

    if (Declared == ProgressionSubject::Sigmoid)
        return Bounded * Bounded * (3.0 - 2.0 * Bounded);

    return Bounded;
}

// 📝 The axis's own reading, or its dynamic's declared fallback where the device reported nothing. The fallback
//    is a **factor** rather than an axis reading, so it is applied after the progression rather than through it.
bool AxisReported(const ResolvedAxes& Axes, DynamicAxis Axis, double& Reading)
{
    switch (Axis)
    {
        case DynamicAxis::Pressure:
            Reading = Axes.Pressure;
            return Axes.PressureReported;

        case DynamicAxis::Tilt:
            Reading = Axes.Tilt;
            return Axes.TiltReported;

        case DynamicAxis::Rotation:
            Reading = Axes.Rotation;
            return Axes.RotationReported;

        case DynamicAxis::Speed:
            Reading = Axes.Speed;
            return true;

        case DynamicAxis::PathDistance:
            Reading = Axes.PathDistance;
            return true;

        case DynamicAxis::AxisCount:
            break;
    }

    Reading = 0.0;
    return false;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                     DECLARATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> BrushSpecification::DeclareShape(const ImpressionShape& Declaring)
{
    if (Declaring.Source == ShapeSource::SourceCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such shape source" });

    if (Declaring.Source == ShapeSource::Analytic && Declaring.Profile == ProfileSubject::ProfileCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an analytic shape declares no profile" });

    if (Declaring.Rotated == RotationSubject::RotationCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such rotation behaviour" });

    DeclaredShape = Declaring;

    return Deliver<bool>::Result(true);
}

Deliver<bool> BrushSpecification::DeclareExtent(double Extent)
{
    if (Extent <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "an impression of no extent" });

    DeclaredExtent = Extent;

    return Deliver<bool>::Result(true);
}

Deliver<bool> BrushSpecification::DeclareSpacing(double RelativeSpacing)
{
    if (RelativeSpacing <= 0.0)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a spacing of nothing never advances" });

    // 🔴 The floor is applied and recorded, never applied silently — `58` §5. It bounds the impression count and
    //    therefore the work `22` §2 does per stroke, which is what stops a brush from stalling a stroke.
    if (RelativeSpacing < ImpressionSpacingFloor)
    {
        DeclaredSpacing.RelativeSpacing = ImpressionSpacingFloor;
        DeclaredSpacing.FloorReached    = true;
        FloorReported                   = false;

        return Deliver<bool>::Result(true);
    }

    DeclaredSpacing.RelativeSpacing = RelativeSpacing;
    DeclaredSpacing.FloorReached    = false;

    return Deliver<bool>::Result(true);
}

Deliver<bool> BrushSpecification::DeclareChannel(const BrushChannelValue& Declaring)
{
    if (Declaring.Channel == ChannelSubject::ChannelCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such channel" });

    // 🔴 `36` §1: a colour without its space is rejected rather than assumed to be in the working space.
    if (Declaring.ColourDeclared && !Declaring.ColourValue.ColourDeclared())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a brush colour declares no space" });

    for (const BrushChannelValue& Held : DeclaredChannels)
    {
        if (Held.Channel == Declaring.Channel)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "the brush already writes that channel" });
        }
    }

    DeclaredChannels.push_back(Declaring);

    return Deliver<bool>::Result(true);
}

Deliver<bool> BrushSpecification::DeclareDynamic(const DynamicSpecification& Declaring)
{
    if (Declaring.Axis == DynamicAxis::AxisCount || Declaring.Parameter == DynamicParameter::ParameterCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "no such axis or parameter" });

    // 🔴 `58` §4 and §10: every dynamic declares its progression, and none is linear by assumption. The
    //    undeclared value is rejected rather than defaulted, so the rule is enforced by the type rather than by a
    //    reviewer noticing.
    if (Declaring.Progression == ProgressionSubject::ProgressionCount)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the dynamic declares no progression" });

    for (const DynamicSpecification& Held : DeclaredDynamics)
    {
        if (Held.Parameter == Declaring.Parameter)
        {
            return Deliver<bool>::Refuse(
                { RefusalReason::ContentUnsupported, "that parameter already carries a dynamic" });
        }
    }

    DeclaredDynamics.push_back(Declaring);

    return Deliver<bool>::Result(true);
}

void BrushSpecification::DeclareCombination(CombineSpecification Declaring)
{
    DeclaredCombination = Declaring;
}

void BrushSpecification::DeclareVariation(const VaryingSpecification& Declaring)
{
    DeclaredVariation = Declaring;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

ResolvedBrush BrushSpecification::Resolve(const ResolvedAxes& Axes,
                                          std::uint32_t       ImpressionIndex,
                                          std::uint32_t       StrokeSeed) const
{
    ResolvedBrush Resolved;
    Resolved.Extent  = DeclaredExtent;
    Resolved.Spacing = DeclaredSpacing.RelativeSpacing;

    if (DeclaredShape.Rotated == RotationSubject::Fixed)
        Resolved.Rotation = DeclaredShape.FixedRotation;

    for (const DynamicSpecification& Held : DeclaredDynamics)
    {
        double Reading = 0.0;

        // 📝 The fallback is a factor and is taken whole. Progressing an absent axis through its curve would
        //    give the low end of the interval, which is a fabricated reading wearing a declared value's clothes.
        const double Factor = AxisReported(Axes, Held.Axis, Reading)
                            ? Held.LowerScale + (Held.UpperScale - Held.LowerScale)
                                              * Progress(Held.Progression, Reading)
                            : Held.AbsentFallback;

        switch (Held.Parameter)
        {
            case DynamicParameter::Extent:           Resolved.Extent           *= Factor;  break;
            case DynamicParameter::CoverageStrength: Resolved.CoverageStrength *= Factor;  break;
            case DynamicParameter::Rotation:         Resolved.Rotation         += Factor;  break;
            case DynamicParameter::Spacing:          Resolved.Spacing          *= Factor;  break;
            case DynamicParameter::ParameterCount:                                         break;
        }
    }

    // 🔴 Four draws from one stroke-seeded sequence, at four distinct ordinals derived from the impression's
    //    own. Reusing one draw across the four would correlate extent with rotation, and every impression would
    //    lean the same way as it grew — which reads as a brush with a defect rather than as a brush with
    //    variation.
    const std::uint32_t Seeded = StrokeSeed * 0x9E3779B9u + ImpressionIndex * 4u;

    if (DeclaredVariation.ExtentVariation != 0.0)
    {
        const double Draw = ProjectVariation(Seeded, StrokeSeed) * 2.0 - 1.0;
        Resolved.Extent  *= 1.0 + Draw * DeclaredVariation.ExtentVariation;
    }

    if (DeclaredVariation.RotationVariation != 0.0)
    {
        const double Draw  = ProjectVariation(Seeded + 1u, StrokeSeed) * 2.0 - 1.0;
        Resolved.Rotation += Draw * DeclaredVariation.RotationVariation;
    }

    if (DeclaredVariation.CoverageVariation != 0.0)
    {
        const double Draw          = ProjectVariation(Seeded + 2u, StrokeSeed) * 2.0 - 1.0;
        Resolved.CoverageStrength *= 1.0 + Draw * DeclaredVariation.CoverageVariation;
    }

    if (DeclaredVariation.PositionVariation != 0.0)
    {
        const double Angle  = ProjectVariation(Seeded + 3u, StrokeSeed) * 2.0 * Pi;
        const double Radius = ProjectVariation(Seeded + 3u, StrokeSeed + 1u)
                            * DeclaredVariation.PositionVariation * Resolved.Extent;

        Resolved.DisplacementX  = Radius * std::cos(Angle);
        Resolved.DisplacementY = Radius * std::sin(Angle);
    }

    // 📝 Bounded after the dynamics and the variation rather than before, because either may drive a factor past
    //    the interval and it is the resolved impression that must be usable, not the intermediate.
    Resolved.Extent           = Resolved.Extent  > 0.0 ? Resolved.Extent  : DeclaredExtent;
    Resolved.Spacing          = Resolved.Spacing > ImpressionSpacingFloor
                              ? Resolved.Spacing : ImpressionSpacingFloor;
    Resolved.CoverageStrength = Resolved.CoverageStrength < 0.0 ? 0.0
                              : (Resolved.CoverageStrength > 1.0 ? 1.0 : Resolved.CoverageStrength);

    return Resolved;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE REPORT
//------------------------------------------------------------------------------------------------------------------------

void BrushSpecification::Report(ReportSequence& Reporting, TickPoint Sampled)
{
    if (!DeclaredSpacing.FloorReached || FloorReported)
        return;

    ReportSpecification Amended;
    Amended.Origin      = "58 §5 BrushSpecification";
    Amended.Subject     = "SpacingFloor";
    Amended.Detail      = "the declared spacing floor bounded the brush; the stroke is coarser than asked";
    Amended.Verdict = ReportVerdict::Amended;
    Amended.Arrival     = Sampled;

    Reporting.Append(Amended);

    FloorReported = true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     WHAT IS READ
//------------------------------------------------------------------------------------------------------------------------

const ImpressionShape&                   BrushSpecification::Shape() const       { return DeclaredShape;     }
const SpacingSpecification&              BrushSpecification::Spacing() const     { return DeclaredSpacing;   }
const VaryingSpecification&              BrushSpecification::Variation() const   { return DeclaredVariation; }
const std::vector<BrushChannelValue>&    BrushSpecification::Channels() const    { return DeclaredChannels;  }
const std::vector<DynamicSpecification>& BrushSpecification::Dynamics() const    { return DeclaredDynamics;  }

double               BrushSpecification::Extent() const      { return DeclaredExtent;      }
CombineSpecification BrushSpecification::Combination() const { return DeclaredCombination; }

bool BrushSpecification::ChannelDeclared(ChannelSubject Channel) const
{
    for (const BrushChannelValue& Held : DeclaredChannels)
    {
        if (Held.Channel == Channel)
            return true;
    }

    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE BRUSHES
//------------------------------------------------------------------------------------------------------------------------

Deliver<std::uint32_t> BrushIndex::Declare(const std::string& Named, const std::string& Grouping)
{
    if (Declared.size() >= BrushLimit)
        return Deliver<std::uint32_t>::Refuse({ RefusalReason::ExtentExhausted, "the brush ceiling was reached" });

    const std::uint32_t BrushIndex = static_cast<std::uint32_t>(Declared.size());

    Declared.push_back(BrushSpecification{});
    DeclaredNames.push_back(Named);
    DeclaredGroupings.push_back(Grouping);

    return Deliver<std::uint32_t>::Result(BrushIndex);
}

Deliver<const BrushSpecification*> BrushIndex::Resolve(std::uint32_t BrushIndex) const
{
    if (BrushIndex >= Declared.size())
        return Deliver<const BrushSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such brush" });

    return Deliver<const BrushSpecification*>::Result(&Declared[BrushIndex]);
}

Deliver<BrushSpecification*> BrushIndex::Amend(std::uint32_t BrushIndex)
{
    if (BrushIndex >= Declared.size())
        return Deliver<BrushSpecification*>::Refuse({ RefusalReason::ContentUnsupported, "no such brush" });

    return Deliver<BrushSpecification*>::Result(&Declared[BrushIndex]);
}

const std::string& BrushIndex::DeclaredName(std::uint32_t BrushIndex) const
{
    return BrushIndex < DeclaredNames.size() ? DeclaredNames[BrushIndex] : AbsentName;
}

const std::string& BrushIndex::DeclaredGrouping(std::uint32_t BrushIndex) const
{
    return BrushIndex < DeclaredGroupings.size() ? DeclaredGroupings[BrushIndex] : AbsentName;
}

std::uint32_t BrushIndex::DeclaredCount() const
{
    return static_cast<std::uint32_t>(Declared.size());
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 WHAT A STROKE RECORDS
//------------------------------------------------------------------------------------------------------------------------

StrokeBrushRecord RecordBrush(const BrushSpecification& Resolving, std::uint32_t StrokeSeed)
{
    StrokeBrushRecord Recorded;
    Recorded.Shape       = Resolving.Shape();
    Recorded.Spacing     = Resolving.Spacing();
    Recorded.Variation   = Resolving.Variation();
    Recorded.Channels    = Resolving.Channels();
    Recorded.Extent      = Resolving.Extent();
    Recorded.Combination = Resolving.Combination();
    Recorded.StrokeSeed  = StrokeSeed;

    return Recorded;
}

}   // namespace Slate
