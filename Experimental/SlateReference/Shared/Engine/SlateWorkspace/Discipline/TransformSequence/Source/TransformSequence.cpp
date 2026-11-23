//============================================================================================================================================
//                                                       TRANSFORMSEQUENCE.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/TransformSequence/Api/TransformSequence.h"

#include <cstdio>
#include <cstdlib>

namespace Slate
{

void AppendTransformNumericRun(char* Target, std::size_t Capacity, const char* Source)
{
    std::size_t Occupied = 0u;
    while (Occupied + 1u < Capacity && Target[Occupied] != '\0')
        ++Occupied;

    for (std::size_t Index = 0u; Source[Index] != '\0' && Occupied + 1u < Capacity; ++Index)
        if (NumericCharacter(Source[Index]))
            Target[Occupied++] = Source[Index];

    Target[Occupied] = '\0';
}

TransformCommandIntake ResolveTransformCommand(const char* Intake,
                                               std::uint32_t IntakeCount,
                                               bool Engaged,
                                               TransformManner Current)
{
    TransformCommandIntake Resolved = {};

    // 🔴 `WorkingManner` is what a restriction letter would apply to, which is NOT the same as the
    //    standing manner: a frame reading `G X` starts a move and restricts it in one pass, so the X must
    //    see the move this frame started. `MannerStanding` is what makes a bare `X` do nothing.
    TransformManner WorkingManner = Engaged ? Current : TransformManner::Move;
    bool            MannerStanding = Engaged;
    std::size_t     NumericTaken   = 0u;

    for (std::uint32_t Index = 0u; Index < IntakeCount; ++Index)
    {
        const char Character = Intake[Index];

        if (Character == 'g' || Character == 'G')
        {
            ++Resolved.MoveTapCount;
            if (!Engaged && !Resolved.StartRequested)
            {
                Resolved.StartRequested = true;
                Resolved.StartManner    = TransformManner::Move;
                WorkingManner           = TransformManner::Move;
                MannerStanding          = true;
            }
            continue;
        }

        if ((Character == 'r' || Character == 'R') && !Engaged && !Resolved.StartRequested)
        {
            Resolved.StartRequested = true;
            Resolved.StartManner    = TransformManner::Rotate;
            WorkingManner           = TransformManner::Rotate;
            MannerStanding          = true;
            continue;
        }

        if ((Character == 's' || Character == 'S') && !Engaged && !Resolved.StartRequested)
        {
            Resolved.StartRequested = true;
            Resolved.StartManner    = TransformManner::Scale;
            WorkingManner           = TransformManner::Scale;
            MannerStanding          = true;
            continue;
        }

        if ((Character == 'x' || Character == 'X') && MannerStanding &&
            (WorkingManner == TransformManner::Move || WorkingManner == TransformManner::Scale))
        {
            Resolved.RestrictionRequested = true;
            Resolved.Restriction          = TransformRestriction::AxisX;
            continue;
        }

        if ((Character == 'z' || Character == 'Z') && MannerStanding &&
            (WorkingManner == TransformManner::Move || WorkingManner == TransformManner::Scale))
        {
            Resolved.RestrictionRequested = true;
            Resolved.Restriction          = TransformRestriction::AxisZ;
            continue;
        }

        // 🔴 `R Y 35` TYPED ITS AXIS AND HAD IT THROWN AWAY. Y matched no branch, fell past the numeric
        //    test, and vanished — the rotation then happened anyway, about the plane normal, because that
        //    is the only rotation a planar sketch has. Correct by accident and silent about it: the
        //    readout said "R 35", so the artist could not tell whether the axis had been understood.
        //
        // 🔴 AND Y IS A TRANSLATION AXIS TOO, NOW THAT THE SKETCH IS HELD IN THE WORLD. It was refused
        //    for Move and Scale on the grounds that "a planar sketch cannot travel along its own normal",
        //    which was true when a sketch was a pair of plane coordinates. `WorldSketchStructure` holds
        //    real `SpatialPoint`s, `ResolveAxisDirection` already answers `Basis.Normal` for `AxisY`, and
        //    `ResolveWorldOffset` already projects onto it — every piece was in place except this gate,
        //    so `G Y 30` read the G, dropped the Y, spent the 30 on nothing and moved not at all.
        //
        // 📝 Lifting a loop off its plane is exactly what makes it non-coplanar, and a non-coplanar loop
        //    cannot be filled. Nothing has to be switched off by hand for that: `AnalyzeWorldSketch`
        //    measures the deviation each time it runs and sets `FillEligible = Coplanar`, so the fill
        //    withdraws itself the moment the geometry stops supporting one, and returns if the artist
        //    moves it back. Refusing the axis to protect the fill would have been the wrong guard.
        if ((Character == 'y' || Character == 'Y') && MannerStanding &&
            (WorkingManner == TransformManner::Rotate ||
             WorkingManner == TransformManner::Move ||
             WorkingManner == TransformManner::Scale))
        {
            Resolved.RestrictionRequested = true;
            Resolved.Restriction          = TransformRestriction::AxisY;
            continue;
        }

        if (NumericCharacter(Character) && NumericTaken + 1u < sizeof(Resolved.NumericAppend))
            Resolved.NumericAppend[NumericTaken++] = Character;
    }

    Resolved.NumericAppend[NumericTaken] = '\0';
    return Resolved;
}

void RetractTransformCommand(TransformStanding& Standing)
{
    std::size_t Occupied = 0u;
    while (Occupied + 1u < sizeof(Standing.Numeric) && Standing.Numeric[Occupied] != '\0')
        ++Occupied;

    if (Occupied > 0u)
    {
        Standing.Numeric[Occupied - 1u] = '\0';
        return;
    }

    if (Standing.Restriction != TransformRestriction::Free &&
        Standing.Restriction != TransformRestriction::Screen)
    {
        Standing.Restriction     = TransformRestriction::Free;
        Standing.SlideAlongCurve = false;
    }
}

void ClearTransformNumeric(TransformStanding& Standing)
{
    Standing.Numeric[0] = '\0';
}

bool ResolveNumericOverride(const TransformStanding& Standing, double& Value)
{
    if (Standing.Numeric[0] == '\0')
        return false;

    char* End = nullptr;
    Value = std::strtod(Standing.Numeric, &End);
    return End != Standing.Numeric;
}

bool ResolveSlideRequested(std::uint32_t MoveTapCount,
                           double Elapsed,
                           double LastTapped,
                           bool CurveAvailable)
{
    return CurveAvailable
        && (MoveTapCount >= 2u
         || (MoveTapCount > 0u && (Elapsed - LastTapped) <= 350.0));
}

const char* TransformMannerText(TransformManner Manner)
{
    switch (Manner)
    {
        case TransformManner::Move:   return "Move";
        case TransformManner::Rotate: return "Rotate";
        case TransformManner::Scale:  return "Scale";
    }
    return "Move";
}

const char* TransformRestrictionText(TransformRestriction Restriction)
{
    switch (Restriction)
    {
        case TransformRestriction::Free:   return "Free";
        case TransformRestriction::AxisX:  return "X";
        case TransformRestriction::AxisZ:  return "Z";
        case TransformRestriction::AxisY:  return "Y";
        case TransformRestriction::Screen: return "Screen";
        case TransformRestriction::Curve:  return "Curve";
    }
    return "Free";
}

const char* TransformCommandToken(TransformManner Manner)
{
    switch (Manner)
    {
        case TransformManner::Move:   return "G";
        case TransformManner::Rotate: return "R";
        case TransformManner::Scale:  return "S";
    }
    return "G";
}

void FormatTransformCommand(const TransformStanding& Standing, char* Delivered, std::size_t Capacity)
{
    if (Capacity == 0u)
        return;

    Delivered[0] = '\0';

    const bool ShowAxisRestriction = Standing.Restriction == TransformRestriction::AxisX
                                  || Standing.Restriction == TransformRestriction::AxisZ
                                  || Standing.Restriction == TransformRestriction::AxisY;

    // 🔴 The slide reads as `G G` rather than `G Curve`, because that is what the artist pressed. A
    //    readout that renamed the gesture would stop matching the keys that produced it.
    if (Standing.Manner == TransformManner::Move && Standing.SlideAlongCurve)
    {
        if (Standing.Numeric[0] != '\0')
            std::snprintf(Delivered, Capacity, "G G %s", Standing.Numeric);
        else
            std::snprintf(Delivered, Capacity, "G G");
        return;
    }

    if (Standing.Numeric[0] != '\0' && ShowAxisRestriction)
        std::snprintf(Delivered, Capacity, "%s %s %s",
                      TransformCommandToken(Standing.Manner),
                      TransformRestrictionText(Standing.Restriction),
                      Standing.Numeric);
    else if (Standing.Numeric[0] != '\0')
        std::snprintf(Delivered, Capacity, "%s %s",
                      TransformCommandToken(Standing.Manner),
                      Standing.Numeric);
    else if (ShowAxisRestriction)
        std::snprintf(Delivered, Capacity, "%s %s",
                      TransformCommandToken(Standing.Manner),
                      TransformRestrictionText(Standing.Restriction));
    else
        std::snprintf(Delivered, Capacity, "%s",
                      TransformCommandToken(Standing.Manner));
}

}   // namespace Slate
