//============================================================================================================================================
//                                                        MEASUREDISPLAY.H
//============================================================================================================================================
// 🧩 Turning a stored measure into something an artist reads, and reading their typing back.
//
// 🔴 THE MODEL IS MILLIMETRES EVERYWHERE AND ALWAYS. This header converts at the two edges where a human
//    is involved -- the label that is drawn and the box that is typed into -- and nowhere else. Switching
//    the display unit must never touch geometry: it re-renders labels and, mid-edit, rewrites the box.
//    A conversion that leaked into the model would silently rescale the drawing the first time somebody
//    changed unit, and the damage would be indistinguishable from a solver bug.
//
// 🔴 ONE FACTOR, ONE DIRECTION EACH. `ToDisplay` and `ToMillimetres` are exact inverses by construction
//    rather than by two hand-written constants that could drift apart. Round-tripping a value through
//    both returns it unchanged, which is the property the edit box depends on: type nothing, change
//    nothing.
//
// 📝 Header-only and constexpr. It has no state, so there is nothing to construct, and a unit choice is
//    just an enum the caller keeps wherever it already keeps display preferences.

#pragma once

#include <cstdint>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       THE UNITS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The units a measure may be shown in.
/// note  📝 Length only. Angles are degrees when shown and radians when stored, which is a fixed pair
///        rather than a choice, so it needs no entry here.
enum class MeasureUnit : std::uint32_t
{
    Millimetre = 0u,
    Centimetre = 1u,
    Metre      = 2u,
    Inch       = 3u,
    UnitCount  = 4u
};

/// 🧩 How many millimetres one of the unit is.
/// note  🔴 THE ONLY TABLE. Both directions of conversion read this, so they cannot disagree, and adding
///        a unit is one line rather than two that must be kept in step.
constexpr double MillimetresPer(MeasureUnit Unit)
{
    switch (Unit)
    {
        case MeasureUnit::Millimetre: return 1.0;
        case MeasureUnit::Centimetre: return 10.0;
        case MeasureUnit::Metre:      return 1000.0;
        case MeasureUnit::Inch:       return 25.4;
        case MeasureUnit::UnitCount:  break;
    }
    return 1.0;
}

/// 🧩 The suffix drawn after a figure in the given unit.
constexpr const char* MeasureUnitSuffix(MeasureUnit Unit)
{
    switch (Unit)
    {
        case MeasureUnit::Millimetre: return "mm";
        case MeasureUnit::Centimetre: return "cm";
        case MeasureUnit::Metre:      return "m";
        case MeasureUnit::Inch:       return "in";
        case MeasureUnit::UnitCount:  break;
    }
    return "mm";
}

/// 🧩 How many decimal places suit the unit by default.
/// note  📝 Bigger units need more places to say the same thing: 4200 mm is 4.2 m, and one decimal place
///        in metres is a whole centimetre of precision lost. The figures here keep roughly a millimetre
///        of resolution visible whichever unit is chosen.
constexpr std::uint32_t MeasureUnitPlaces(MeasureUnit Unit)
{
    switch (Unit)
    {
        case MeasureUnit::Millimetre: return 2u;
        case MeasureUnit::Centimetre: return 2u;
        case MeasureUnit::Metre:      return 3u;
        case MeasureUnit::Inch:       return 3u;
        case MeasureUnit::UnitCount:  break;
    }
    return 2u;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE CONVERSION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 A stored measure, in millimetres, as it should be shown.
/// tag   api, nonthrowing
constexpr double ToDisplay(double Millimetres, MeasureUnit Unit)
{
    return Millimetres / MillimetresPer(Unit);
}

/// 🧩 A figure the artist typed, in the shown unit, as it must be stored.
/// note  🔴 THE EXACT INVERSE of `ToDisplay`, so typing 4.2 while showing metres stores 4200 and showing
///        it again reads 4.2. Anything else and a dimension would creep every time it was looked at.
/// tag   api, nonthrowing
constexpr double ToMillimetres(double Shown, MeasureUnit Unit)
{
    return Shown * MillimetresPer(Unit);
}

} // namespace Slate
