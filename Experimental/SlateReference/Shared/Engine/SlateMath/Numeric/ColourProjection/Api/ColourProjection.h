//============================================================================================================================================
//                                                            COLOURPROJECTION.H
//============================================================================================================================================
// 🧩 A coordinate and the space it is a coordinate in — never a bare triple, and never an assumed encoding.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "Foundation/NumericTolerance.h"

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE TRANSFERS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The encoding transfer a colour space applies between its linear light and its stored code.
/// note  🔴 `66` §4 applies the output transfer exactly once in the whole engine. A space that declares
///        `Linear` here carries no transfer, which is what lets a working space be wide and linear while a
///        display space is neither.
/// tag   guarantee
enum class TransferSubject : std::uint32_t
{
    Linear        = 0u,   // [-] - no transfer; the working space
    Companded     = 1u,   // [-] - the piecewise 2.4 curve of the common display spaces
    PureExponent  = 2u,   // [-] - a single declared exponent, no linear segment
    TransferCount = 3u    // [-] - the closed count, never a transfer
};

//------------------------------------------------------------------------------------------------------------------------
//                                                     ONE SPACE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One colour space: three primaries, a white point, and the transfer it stores its code through.
/// note  📐 Primaries and white are chromaticities, so the projection between two spaces is derived from these
///        rather than declared as a matrix. A stored matrix is a second representation of the primaries and it
///        drifts from them the moment either is amended.
/// note  🔴 Every space carries an identity, and the identity is what `36` §7 compares at Exact. Two spaces that
///        differ are never treated as one because their chromaticities happened to agree to six places.
/// tag   nonallocating, nonthrowing
struct ColourSpaceSpecification
{
    std::uint32_t    SpaceIdentity = 0u;                        // [-] - zero declares the space undeclared
    double           RedX          = 0.640;                     // [-] - chromaticity of the red primary
    double           RedY          = 0.330;                     // [-]
    double           GreenX        = 0.300;                     // [-] - chromaticity of the green primary
    double           GreenY        = 0.600;                     // [-]
    double           BlueX         = 0.150;                     // [-] - chromaticity of the blue primary
    double           BlueY         = 0.060;                     // [-]
    double           WhiteX        = 0.3127;                    // [-] - chromaticity of the white point
    double           WhiteY        = 0.3290;                    // [-]
    TransferSubject  Transfer      = TransferSubject::Linear;    // [-] - how stored code relates to linear light
    double           TransferExponent = 2.4;                    // [-] - read by Companded and PureExponent

    /// 🧩 Whether this specification names a space at all.
    /// cost  ✔️
    constexpr bool SpaceDeclared() const { return SpaceIdentity != 0u; }
};

// 📝 The two spaces every build has. `36` §2 declares the working space **per document** and stores it, so these
//    are the defaults a document is created with and never an assumption a reader may make on its own.
inline constexpr std::uint32_t WorkingSpaceIdentity = 1u;   // [-] - wide, linear; every computation above `66` ⑧
inline constexpr std::uint32_t DisplaySpaceIdentity = 2u;   // [-] - the machine's; never stored in the document

/// 🧩 The wide linear working space a document is created with.
/// note  Wide enough that a saturated illuminant does not clip on entry — `36` §2. The primaries are the common
///        wide-gamut set; `36` §9 leaves which set open and this is a constant, not a shape.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr ColourSpaceSpecification DeclaredWorkingSpace()
{
    ColourSpaceSpecification Declaring;
    Declaring.SpaceIdentity = WorkingSpaceIdentity;
    Declaring.RedX          = 0.7347;
    Declaring.RedY          = 0.2653;
    Declaring.GreenX        = 0.1596;
    Declaring.GreenY        = 0.8404;
    Declaring.BlueX         = 0.0366;
    Declaring.BlueY         = 0.0001;
    Declaring.WhiteX        = 0.32168;
    Declaring.WhiteY        = 0.33767;
    Declaring.Transfer      = TransferSubject::Linear;

    return Declaring;
}

/// 🧩 The display space, companded, as a build default until `36` §9's open row is answered.
/// note  🔴 Queried or declared per `36` §9 and **never assumed to be the working space**. Assuming they match
///        produces an image that is correct on exactly one monitor.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr ColourSpaceSpecification DeclaredDisplaySpace()
{
    ColourSpaceSpecification Declaring;
    Declaring.SpaceIdentity = DisplaySpaceIdentity;
    Declaring.Transfer      = TransferSubject::Companded;

    return Declaring;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE COLOUR
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A coordinate together with the space it is expressed in.
/// note  🔴 `36` §1: there is no bare triple anywhere in Slate. A colour without its space is a number that
///        three subsystems will each interpret differently, and all three will look plausible.
/// note  Coordinates are held at 64-bit and are not clamped. An emission channel is unbounded above — `18` §2 —
///        and clamping here would compress a radiance before `66` had a chance to project it.
/// tag   nonallocating, nonthrowing
struct ColourSpecification
{
    double         RedCoordinate   = 0.0;   // [-] - in the declared space, at its declared transfer
    double         GreenCoordinate = 0.0;   // [-]
    double         BlueCoordinate  = 0.0;   // [-]
    std::uint32_t  SpaceIdentity   = 0u;    // [-] - zero declares the colour's space undeclared

    /// 🧩 Whether this colour names the space it is a coordinate in.
    /// cost  ✔️
    constexpr bool ColourDeclared() const { return SpaceIdentity != 0u; }
};

/// 🧩 Whether two colours are expressed in the same space.
/// note  An integer comparison at Exact — `36` §7. A mistaken match converts nothing and is therefore silent.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
constexpr bool SpacesAgree(ColourSpecification LeftColour, ColourSpecification RightColour)
{
    return LeftColour.SpaceIdentity == RightColour.SpaceIdentity && LeftColour.ColourDeclared();
}
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Exact, PrecisionGuarantee::Exact);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROJECTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects one colour into a declared space, transfers and white point included.
/// in    Incoming  [-]  the colour, carrying the space it is a coordinate in
/// in    Target    [-]  the space to express it in
/// out   Result   [-]  refuses with ContentUnsupported when either space is undeclared
/// note  🔴 The whole conversion in one call: decode the incoming transfer, project the primaries, adapt the
///        white point, encode the target transfer. Exposing the four apart invites a caller to omit one, and the
///        omission that matters — the transfer — produces an image that is merely "a bit washed out".
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<ColourSpecification> Project(ColourSpecification             Incoming,
                                    const ColourSpaceSpecification& IncomingSpace,
                                    const ColourSpaceSpecification& Target);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Projects one tristimulus coordinate into a declared space, encoding its transfer.
/// in    TristimulusX  [-]  as `SpectralProjection` produced it
/// in    TristimulusY  [-]
/// in    TristimulusZ  [-]
/// in    Target        [-]  the space to express it in
/// out   Result       [-]  refuses with ContentUnsupported for an undeclared or degenerate target space
/// note  🔴 No white adaptation is applied. A tristimulus coordinate is absolute and carries no white of its
///        own; whether it needs adapting is a fact about the spectrum that produced it, which this routine
///        cannot see. `ProjectTemperature` adapts before calling here, because a locus coordinate **is** a
///        white point — and that is the one case where the adaptation is knowable at this depth.
/// note  📝 Declared so that `28` may resolve a spectrally projected extinction coefficient into the working
///        space without re-deriving the primaries. `AdaptWhite` already crosses this seam in tristimulus, so
///        nothing new is exposed by it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<ColourSpecification> ProjectTristimulus(double                          TristimulusX,
                                                double                          TristimulusY,
                                                double                          TristimulusZ,
                                                const ColourSpaceSpecification& Target);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Applies one space's encoding transfer to a linear coordinate.
/// in    LinearMagnitude  [-]  linear light; negative magnitudes are transferred by odd reflection
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
double Encode(const ColourSpaceSpecification& Space, double LinearMagnitude);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Removes one space's encoding transfer, returning linear light.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
double Decode(const ColourSpaceSpecification& Space, double StoredCode);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Adapts a tristimulus coordinate from one white point to another.
/// note  📐 Von Kries adaptation in a declared cone response space. Adapting by scaling tristimulus directly
///        shifts hue on every saturated colour, which is visible exactly where an artist notices it.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
void AdaptWhite(double IncomingWhiteX, double IncomingWhiteY,
                double TargetWhiteX,   double TargetWhiteY,
                double& TristimulusX,  double& TristimulusY, double& TristimulusZ);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 Derives a white point coordinate from a declared correlated colour temperature.
/// in    Temperature  [K]  1667 to 25000; outside that the locus approximation is rejected
/// out   Result      [-]  refuses with ContentUnsupported outside the declared interval
/// note  🔴 `36` §5: the temperature is retained as the authored value by whoever declared it. An artist who set
///        5600 expects to see 5600 when they return, and a coordinate cannot be inverted back to it exactly.
/// cost  ✔️
/// tag   api, nonallocating, nonthrowing
Deliver<ColourSpecification> ProjectTemperature(double                          Temperature,
                                               const ColourSpaceSpecification& Target);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

}   // namespace Slate
