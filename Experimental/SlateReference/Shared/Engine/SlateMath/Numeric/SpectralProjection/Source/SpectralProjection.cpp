//============================================================================================================================================
//                                                        SPECTRALPROJECTION.CPP
//============================================================================================================================================
// 🧩 The nine-lobe fit, and the normalisation derived from it rather than beside it.

#include "SlateMath/Numeric/SpectralProjection/Api/SpectralProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       ONE LOBE
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 A Gaussian with a different width on each side of its centre. The matching functions are asymmetric — every
//    one of them falls away faster on one flank than the other — and a symmetric Gaussian fit either overshoots
//    the narrow flank or undershoots the wide one, whichever the fit was weighted toward.
double Lobe(double Wavelength, double Centre, double LowerWidth, double UpperWidth)
{
    const double Width = Wavelength < Centre ? LowerWidth : UpperWidth;

    if (Width <= 0.0)
        return 0.0;

    const double Departure = (Wavelength - Centre) / Width;

    return std::exp(-0.5 * Departure * Departure);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                             THE COLOUR-MATCHING FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

TristimulusCoordinate ProjectWavelength(double Wavelength)
{
    TristimulusCoordinate Response;

    // 📝 Zero outside the declared interval rather than extrapolated. A Gaussian tail extrapolated past the
    //    interval it was fitted over is a response the eye does not have, and it would appear as an atmosphere
    //    tinted by ultraviolet.
    if (Wavelength < SpectralLowerWavelength || Wavelength > SpectralUpperWavelength)
        return Response;

    // 📐 The long-wavelength response has two lobes and one subtracted correction: the response genuinely dips
    //    below the sum of its two peaks in the cyan region, and a fit of positive lobes alone cannot reach it.
    Response.MagnitudeX =  1.056 * Lobe(Wavelength, 599.8, 37.9, 31.0)
                        +  0.362 * Lobe(Wavelength, 442.0, 16.0, 26.7)
                        -  0.065 * Lobe(Wavelength, 501.1, 20.4, 26.2);

    Response.MagnitudeY =  0.821 * Lobe(Wavelength, 568.8, 46.9, 40.5)
                        +  0.286 * Lobe(Wavelength, 530.9, 16.3, 31.1);

    Response.MagnitudeZ =  1.217 * Lobe(Wavelength, 437.0, 11.8, 36.0)
                        +  0.681 * Lobe(Wavelength, 459.0, 26.0, 13.8);

    return Response;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE NORMALISATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<double> LuminanceNormalisation(const QuadratureRule& Rule)
{
    if (!Rule.Derived())
        return Deliver<double>::Refuse({ RefusalReason::ContentUnsupported, "the rule has not been derived" });

    const double Integrated = Rule.IntegrateInterval(SpectralLowerWavelength,
                                                     SpectralUpperWavelength,
                                                     [](double Wavelength)
                                                     {
                                                         return ProjectWavelength(Wavelength).MagnitudeY;
                                                     });

    return Deliver<double>::Result(Integrated);
}

}   // namespace Slate
