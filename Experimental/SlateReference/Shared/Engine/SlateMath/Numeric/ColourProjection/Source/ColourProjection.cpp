//============================================================================================================================================
//                                                           COLOURPROJECTION.CPP
//============================================================================================================================================
// 🧩 Primaries derived from chromaticities, the transfers, von Kries adaptation, and the Planckian locus.

#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                  PRIMARIES TO XYZ
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 Nine coefficients, row-major: the projection from a space's own coordinates into tristimulus. Derived from
//    the chromaticities on every call rather than stored, because a stored matrix is a second representation of
//    the primaries and drifts from them the moment either is amended.
struct TristimulusProjection
{
    double  Coefficient[9] = { 1.0, 0.0, 0.0,
                               0.0, 1.0, 0.0,
                               0.0, 0.0, 1.0 };   // [-] - row-major
    bool    Derived        = false;               // [-] - the primaries were not degenerate
};

bool Invert(const TristimulusProjection& Forward, TristimulusProjection& Inverted)
{
    const double* Held = Forward.Coefficient;

    const double Determinant = Held[0] * (Held[4] * Held[8] - Held[5] * Held[7])
                             - Held[1] * (Held[3] * Held[8] - Held[5] * Held[6])
                             + Held[2] * (Held[3] * Held[7] - Held[4] * Held[6]);

    if (std::fabs(Determinant) < QuaternionRenormalise)
        return false;

    const double Reciprocal = 1.0 / Determinant;

    Inverted.Coefficient[0] = (Held[4] * Held[8] - Held[5] * Held[7]) * Reciprocal;
    Inverted.Coefficient[1] = (Held[2] * Held[7] - Held[1] * Held[8]) * Reciprocal;
    Inverted.Coefficient[2] = (Held[1] * Held[5] - Held[2] * Held[4]) * Reciprocal;
    Inverted.Coefficient[3] = (Held[5] * Held[6] - Held[3] * Held[8]) * Reciprocal;
    Inverted.Coefficient[4] = (Held[0] * Held[8] - Held[2] * Held[6]) * Reciprocal;
    Inverted.Coefficient[5] = (Held[2] * Held[3] - Held[0] * Held[5]) * Reciprocal;
    Inverted.Coefficient[6] = (Held[3] * Held[7] - Held[4] * Held[6]) * Reciprocal;
    Inverted.Coefficient[7] = (Held[1] * Held[6] - Held[0] * Held[7]) * Reciprocal;
    Inverted.Coefficient[8] = (Held[0] * Held[4] - Held[1] * Held[3]) * Reciprocal;
    Inverted.Derived        = true;

    return true;
}

void Apply(const TristimulusProjection& Projection,
           double  LeftTerm, double MiddleTerm, double RightTerm,
           double& FirstOut, double& SecondOut,  double& ThirdOut)
{
    FirstOut  = Projection.Coefficient[0] * LeftTerm + Projection.Coefficient[1] * MiddleTerm
              + Projection.Coefficient[2] * RightTerm;
    SecondOut = Projection.Coefficient[3] * LeftTerm + Projection.Coefficient[4] * MiddleTerm
              + Projection.Coefficient[5] * RightTerm;
    ThirdOut  = Projection.Coefficient[6] * LeftTerm + Projection.Coefficient[7] * MiddleTerm
              + Projection.Coefficient[8] * RightTerm;
}

// 📐 Each primary's chromaticity gives its tristimulus direction, so the three directions are the columns of one
//    projection. The luminance weights are then the single solve that carries the declared white through it —
//    `Direction ⋅ weights = white`, which is `weights = Direction⁻¹ ⋅ white`.
// 📝 🔴 Solved through the inverse rather than by three hand-expanded determinants. The transcribed form carried
//    two sign errors and one normalisation error at once, and each of the three was individually plausible: the
//    matrix stopped carrying (1,1,1) to the declared white, so a neutral coordinate no longer projected to a
//    neutral coordinate and no round trip could recover. One solve has one place to be wrong.
TristimulusProjection DeriveProjection(const ColourSpaceSpecification& Space)
{
    TristimulusProjection Derived;

    if (Space.RedY == 0.0 || Space.GreenY == 0.0 || Space.BlueY == 0.0 || Space.WhiteY == 0.0)
        return Derived;

    TristimulusProjection Direction;
    Direction.Coefficient[0] = Space.RedX   / Space.RedY;
    Direction.Coefficient[1] = Space.GreenX / Space.GreenY;
    Direction.Coefficient[2] = Space.BlueX  / Space.BlueY;
    Direction.Coefficient[3] = 1.0;
    Direction.Coefficient[4] = 1.0;
    Direction.Coefficient[5] = 1.0;
    Direction.Coefficient[6] = (1.0 - Space.RedX   - Space.RedY)   / Space.RedY;
    Direction.Coefficient[7] = (1.0 - Space.GreenX - Space.GreenY) / Space.GreenY;
    Direction.Coefficient[8] = (1.0 - Space.BlueX  - Space.BlueY)  / Space.BlueY;
    Direction.Derived        = true;

    TristimulusProjection DirectionInverse;

    if (!Invert(Direction, DirectionInverse))
        return Derived;

    // 📝 The white is normalised to unit luminance here, which is why the middle row's right-hand side below is
    //    one and not the declared WhiteY. Reading WhiteY a second time double-normalises the blue weight.
    const double WhiteRatioX = Space.WhiteX / Space.WhiteY;
    const double WhiteRatioZ = (1.0 - Space.WhiteX - Space.WhiteY) / Space.WhiteY;

    double RedWeight   = 0.0;
    double GreenWeight = 0.0;
    double BlueWeight  = 0.0;

    Apply(DirectionInverse, WhiteRatioX, 1.0, WhiteRatioZ, RedWeight, GreenWeight, BlueWeight);

    Derived.Coefficient[0] = Direction.Coefficient[0] * RedWeight;
    Derived.Coefficient[1] = Direction.Coefficient[1] * GreenWeight;
    Derived.Coefficient[2] = Direction.Coefficient[2] * BlueWeight;

    Derived.Coefficient[3] = RedWeight;
    Derived.Coefficient[4] = GreenWeight;
    Derived.Coefficient[5] = BlueWeight;

    Derived.Coefficient[6] = Direction.Coefficient[6] * RedWeight;
    Derived.Coefficient[7] = Direction.Coefficient[7] * GreenWeight;
    Derived.Coefficient[8] = Direction.Coefficient[8] * BlueWeight;

    Derived.Derived = true;

    return Derived;
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFERS
//------------------------------------------------------------------------------------------------------------------------

double Encode(const ColourSpaceSpecification& Space, double LinearMagnitude)
{
    if (Space.Transfer == TransferSubject::Linear)
        return LinearMagnitude;

    // 📝 Negative magnitudes are transferred by odd reflection rather than clamped. A working space wider than
    //    the display produces negative display coordinates legitimately, and clamping here would lose the sign
    //    before `66` had projected it.
    const double Signum    = LinearMagnitude < 0.0 ? -1.0 : 1.0;
    const double Magnitude = std::fabs(LinearMagnitude);

    if (Space.Transfer == TransferSubject::PureExponent)
        return Signum * std::pow(Magnitude, 1.0 / Space.TransferExponent);

    if (Magnitude <= 0.0031308)
        return Signum * Magnitude * 12.92;

    return Signum * (1.055 * std::pow(Magnitude, 1.0 / Space.TransferExponent) - 0.055);
}

double Decode(const ColourSpaceSpecification& Space, double StoredCode)
{
    if (Space.Transfer == TransferSubject::Linear)
        return StoredCode;

    const double Signum    = StoredCode < 0.0 ? -1.0 : 1.0;
    const double Magnitude = std::fabs(StoredCode);

    if (Space.Transfer == TransferSubject::PureExponent)
        return Signum * std::pow(Magnitude, Space.TransferExponent);

    if (Magnitude <= 0.04045)
        return Signum * Magnitude / 12.92;

    return Signum * std::pow((Magnitude + 0.055) / 1.055, Space.TransferExponent);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  WHITE ADAPTATION
//------------------------------------------------------------------------------------------------------------------------

void AdaptWhite(double IncomingWhiteX, double IncomingWhiteY,
                double TargetWhiteX,   double TargetWhiteY,
                double& TristimulusX,  double& TristimulusY, double& TristimulusZ)
{
    if (IncomingWhiteY == 0.0 || TargetWhiteY == 0.0)
        return;

    if (IncomingWhiteX == TargetWhiteX && IncomingWhiteY == TargetWhiteY)
        return;

    // 📐 Bradford cone response. The adaptation is a scale in cone space and not in tristimulus, because
    //    scaling tristimulus directly shifts hue on every saturated colour.
    constexpr double ConeForward[9] =
    {
         0.8951,  0.2664, -0.1614,
        -0.7502,  1.7135,  0.0367,
         0.0389, -0.0685,  1.0296
    };

    // 🔴 The inverse is derived here rather than transcribed. A transcribed inverse is accurate only to its
    //    own last digit, so the product with the forward matrix differs from the identity in the seventh
    //    place — and a coordinate projected into a space and back then returns wrong in its eighth. `36` §7
    //    declares this Bounded, not Perceptual, and a Bounded round trip has to close. Same reasoning as
    //    DeriveProjection's single solve: one inversion has one place to be wrong.
    TristimulusProjection Cone;

    for (std::uint32_t Index = 0u; Index < 9u; ++Index)
        Cone.Coefficient[Index] = ConeForward[Index];

    Cone.Derived = true;

    TristimulusProjection ConeInverted;

    if (!Invert(Cone, ConeInverted))
        return;

    const double* ConeInverse = ConeInverted.Coefficient;

    const double IncomingX = IncomingWhiteX / IncomingWhiteY;
    const double IncomingZ = (1.0 - IncomingWhiteX - IncomingWhiteY) / IncomingWhiteY;
    const double TargetX   = TargetWhiteX / TargetWhiteY;
    const double TargetZ   = (1.0 - TargetWhiteX - TargetWhiteY) / TargetWhiteY;

    const double IncomingCone[3] =
    {
        ConeForward[0] * IncomingX + ConeForward[1] * 1.0 + ConeForward[2] * IncomingZ,
        ConeForward[3] * IncomingX + ConeForward[4] * 1.0 + ConeForward[5] * IncomingZ,
        ConeForward[6] * IncomingX + ConeForward[7] * 1.0 + ConeForward[8] * IncomingZ
    };

    const double TargetCone[3] =
    {
        ConeForward[0] * TargetX + ConeForward[1] * 1.0 + ConeForward[2] * TargetZ,
        ConeForward[3] * TargetX + ConeForward[4] * 1.0 + ConeForward[5] * TargetZ,
        ConeForward[6] * TargetX + ConeForward[7] * 1.0 + ConeForward[8] * TargetZ
    };

    double SubjectCone[3] =
    {
        ConeForward[0] * TristimulusX + ConeForward[1] * TristimulusY + ConeForward[2] * TristimulusZ,
        ConeForward[3] * TristimulusX + ConeForward[4] * TristimulusY + ConeForward[5] * TristimulusZ,
        ConeForward[6] * TristimulusX + ConeForward[7] * TristimulusY + ConeForward[8] * TristimulusZ
    };

    for (std::uint32_t Index = 0u; Index < 3u; ++Index)
    {
        if (IncomingCone[Index] != 0.0)
            SubjectCone[Index] *= TargetCone[Index] / IncomingCone[Index];
    }

    TristimulusX = ConeInverse[0] * SubjectCone[0] + ConeInverse[1] * SubjectCone[1]
                 + ConeInverse[2] * SubjectCone[2];
    TristimulusY = ConeInverse[3] * SubjectCone[0] + ConeInverse[4] * SubjectCone[1]
                 + ConeInverse[5] * SubjectCone[2];
    TristimulusZ = ConeInverse[6] * SubjectCone[0] + ConeInverse[7] * SubjectCone[1]
                 + ConeInverse[8] * SubjectCone[2];
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

Deliver<ColourSpecification> Project(ColourSpecification             Incoming,
                                    const ColourSpaceSpecification& IncomingSpace,
                                    const ColourSpaceSpecification& Target)
{
    if (!Incoming.ColourDeclared() || !IncomingSpace.SpaceDeclared() || !Target.SpaceDeclared())
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "a colour or a space was undeclared" });
    }

    if (Incoming.SpaceIdentity != IncomingSpace.SpaceIdentity)
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the colour is not a coordinate in the supplied space" });
    }

    // 📝 Identical spaces return the coordinate untouched rather than round-tripping it through tristimulus.
    //    `36` §3's re-conversion rule is the same instinct: a conversion that does nothing must cost nothing and
    //    must not perturb what it was given.
    if (IncomingSpace.SpaceIdentity == Target.SpaceIdentity)
        return Deliver<ColourSpecification>::Result(Incoming);

    const TristimulusProjection Forward = DeriveProjection(IncomingSpace);
    const TristimulusProjection TargetForward = DeriveProjection(Target);

    TristimulusProjection TargetInverse;

    if (!Forward.Derived || !TargetForward.Derived || !Invert(TargetForward, TargetInverse))
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "a space declared degenerate primaries" });
    }

    const double LinearRed   = Decode(IncomingSpace, Incoming.RedCoordinate);
    const double LinearGreen = Decode(IncomingSpace, Incoming.GreenCoordinate);
    const double LinearBlue  = Decode(IncomingSpace, Incoming.BlueCoordinate);

    double TristimulusX = 0.0;
    double TristimulusY = 0.0;
    double TristimulusZ = 0.0;

    Apply(Forward, LinearRed, LinearGreen, LinearBlue, TristimulusX, TristimulusY, TristimulusZ);

    AdaptWhite(IncomingSpace.WhiteX, IncomingSpace.WhiteY,
               Target.WhiteX,        Target.WhiteY,
               TristimulusX, TristimulusY, TristimulusZ);

    double TargetRed   = 0.0;
    double TargetGreen = 0.0;
    double TargetBlue  = 0.0;

    Apply(TargetInverse, TristimulusX, TristimulusY, TristimulusZ, TargetRed, TargetGreen, TargetBlue);

    ColourSpecification Projected;
    Projected.RedCoordinate   = Encode(Target, TargetRed);
    Projected.GreenCoordinate = Encode(Target, TargetGreen);
    Projected.BlueCoordinate  = Encode(Target, TargetBlue);
    Projected.SpaceIdentity   = Target.SpaceIdentity;

    return Deliver<ColourSpecification>::Result(Projected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 TRISTIMULUS TO A SPACE
//------------------------------------------------------------------------------------------------------------------------

Deliver<ColourSpecification> ProjectTristimulus(double                          TristimulusX,
                                                double                          TristimulusY,
                                                double                          TristimulusZ,
                                                const ColourSpaceSpecification& Target)
{
    if (!Target.SpaceDeclared())
        return Deliver<ColourSpecification>::Refuse({ RefusalReason::ContentUnsupported, "the space is undeclared" });

    const TristimulusProjection TargetForward = DeriveProjection(Target);

    TristimulusProjection TargetInverse;

    if (!TargetForward.Derived || !Invert(TargetForward, TargetInverse))
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the target space declared degenerate primaries" });
    }

    double TargetRed   = 0.0;
    double TargetGreen = 0.0;
    double TargetBlue  = 0.0;

    Apply(TargetInverse, TristimulusX, TristimulusY, TristimulusZ, TargetRed, TargetGreen, TargetBlue);

    ColourSpecification Projected;
    Projected.RedCoordinate   = Encode(Target, TargetRed);
    Projected.GreenCoordinate = Encode(Target, TargetGreen);
    Projected.BlueCoordinate  = Encode(Target, TargetBlue);
    Projected.SpaceIdentity   = Target.SpaceIdentity;

    return Deliver<ColourSpecification>::Result(Projected);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TEMPERATURE
//------------------------------------------------------------------------------------------------------------------------

Deliver<ColourSpecification> ProjectTemperature(double                          Temperature,
                                                const ColourSpaceSpecification& Target)
{
    if (Temperature < 1667.0 || Temperature > 25000.0)
    {
        return Deliver<ColourSpecification>::Refuse(
            { RefusalReason::ContentUnsupported, "the temperature lies outside the declared locus interval" });
    }

    if (!Target.SpaceDeclared())
        return Deliver<ColourSpecification>::Refuse({ RefusalReason::ContentUnsupported, "the space is undeclared" });

    // 📐 Kim's cubic approximation of the Planckian locus, in two intervals. The locus itself has no closed
    //    form, so this is Bounded and declared as such rather than presented as exact.
    const double Reciprocal = 1000.0 / Temperature;

    double LocusX = 0.0;

    if (Temperature <= 4000.0)
    {
        LocusX = -0.2661239 * Reciprocal * Reciprocal * Reciprocal
               -  0.2343589 * Reciprocal * Reciprocal
               +  0.8776956 * Reciprocal
               +  0.179910;
    }
    else
    {
        LocusX = -3.0258469 * Reciprocal * Reciprocal * Reciprocal
               +  2.1070379 * Reciprocal * Reciprocal
               +  0.2226347 * Reciprocal
               +  0.240390;
    }

    double LocusY = 0.0;

    if (Temperature <= 2222.0)
    {
        LocusY = -1.1063814 * LocusX * LocusX * LocusX - 1.34811020 * LocusX * LocusX
               +  2.18555832 * LocusX - 0.20219683;
    }
    else if (Temperature <= 4000.0)
    {
        LocusY = -0.9549476 * LocusX * LocusX * LocusX - 1.37418593 * LocusX * LocusX
               +  2.09137015 * LocusX - 0.16748867;
    }
    else
    {
        LocusY =  3.0817580 * LocusX * LocusX * LocusX - 5.87338670 * LocusX * LocusX
               +  3.75112997 * LocusX - 0.37001483;
    }

    if (LocusY == 0.0)
        return Deliver<ColourSpecification>::Refuse({ RefusalReason::ContentUnsupported, "the locus is degenerate" });

    // 📝 Normalised to unit luminance. The illuminant's radiant intensity is `44` §2's separate declaration, so
    //    folding a magnitude in here would give one quantity two owners.
    double TristimulusX = LocusX / LocusY;
    double TristimulusY = 1.0;
    double TristimulusZ = (1.0 - LocusX - LocusY) / LocusY;

    AdaptWhite(LocusX, LocusY, Target.WhiteX, Target.WhiteY, TristimulusX, TristimulusY, TristimulusZ);

    // 📝 The adaptation above is what makes the shared tail correct here: a locus coordinate is a white point,
    //    so it is adapted to the target's white **before** the primaries are applied. Everything after that is
    //    the same arithmetic every tristimulus projection performs, and one copy of it is one place to be wrong.
    return ProjectTristimulus(TristimulusX, TristimulusY, TristimulusZ, Target);
}


}   // namespace Slate
