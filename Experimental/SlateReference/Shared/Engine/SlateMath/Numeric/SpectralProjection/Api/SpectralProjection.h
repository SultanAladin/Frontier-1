//============================================================================================================================================
//                                                         SPECTRALPROJECTION.H
//============================================================================================================================================
// 🧩 Wavelength domain to tristimulus — the colour-matching functions, analytic, never three sampled wavelengths.

#pragma once

#include "Foundation/DeliveryGuarantee.h"
#include "Foundation/PrecisionGuarantee.h"
#include "SlateMath/Numeric/QuadratureIntegrator/Api/QuadratureIntegrator.h"

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE DECLARED DOMAIN
//------------------------------------------------------------------------------------------------------------------------

// 📝 The interval the matching functions are declared over. Read by this component alone, so `00` §2 keeps the
//    two here rather than in `Foundation/`; they are a declared domain rather than a tolerance, so `02` §8's gate
//    does not reach them either.
inline constexpr double SpectralLowerWavelength = 360.0;   // [nm]
inline constexpr double SpectralUpperWavelength = 830.0;   // [nm]

//------------------------------------------------------------------------------------------------------------------------
//                                                    ONE COORDINATE
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 One tristimulus coordinate, before any space's primaries are applied.
/// note  ⚠️ Deliberately not a `ColourSpecification`. `36` §1 requires a colour to carry the space it is a
///        coordinate in, and tristimulus is not a space's coordinate — it is what a projection into one starts
///        from. Spelling it as a colour would let a caller store it and have `36`'s rule appear satisfied by a
///        coordinate in no space at all.
/// tag   nonallocating, nonthrowing
struct TristimulusCoordinate
{
    double  MagnitudeX = 0.0;   // [-] - the long-wavelength matching response
    double  MagnitudeY = 0.0;   // [-] - luminance
    double  MagnitudeZ = 0.0;   // [-] - the short-wavelength matching response
};

//------------------------------------------------------------------------------------------------------------------------
//                                             THE COLOUR-MATCHING FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 The three colour-matching responses at one wavelength.
/// in    Wavelength  [nm]  outside the declared interval every response is zero
/// out   Coordinate  [-]   the matching functions, unnormalised
/// note  📐 A multi-lobe piecewise-Gaussian fit rather than a transcribed measurement set. The measured set is
///        four hundred and seventy entries per response and would be four hundred and seventy chances to
///        mistype; the fit is nine lobes and reproduces the set to within a fraction of a percent everywhere,
///        which is far inside the Bounded guarantee this component claims.
/// note  🔴 This is what `28` §3 means by "not by sampling three fixed wavelengths". Three samples is a
///        three-point quadrature of an integral whose integrand has a sharp lobe structure, and it is why an
///        atmosphere computed that way has a twilight of the wrong hue rather than of the wrong brightness.
/// cost  🚩
/// tag   api, nonallocating, nonthrowing
TristimulusCoordinate ProjectWavelength(double Wavelength);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

/// 🧩 The integral of the luminance response over the declared interval — the normalisation a projection divides by.
/// in    Rule     [-]  a derived rule; the integral is taken against it
/// out   Result  [-]  refuses with ContentUnsupported before the rule is derived
/// note  📝 Derived on demand rather than declared as a number, for the reason `ColourProjection` derives its
///        primaries from chromaticities: a stored normalisation is a second representation of the matching
///        functions and drifts from them the moment the fit is amended.
/// cost  🚩
/// tag   api, nonthrowing
Deliver<double> LuminanceNormalisation(const QuadratureRule& Rule);
SLATE_DECLARES_PRECISION(PrecisionGuarantee::Bounded, PrecisionGuarantee::Bounded);

//------------------------------------------------------------------------------------------------------------------------
//                                                   THE PROJECTION
//------------------------------------------------------------------------------------------------------------------------

/// 🧩 Projects one spectral quantity onto tristimulus, normalised so a flat spectrum of unit magnitude has unit luminance.
/// in    Rule       [-]  a derived rule, integrated over the declared wavelength interval
/// in    Evaluate   [-]  the spectral quantity; called once per abscissa with a wavelength in nanometres
/// out   Result    [-]  refuses with ContentUnsupported before the rule is derived, and when the luminance
///                       normalisation vanishes — which is a fit that no longer describes a luminance response
/// note  🔴 The three responses are accumulated **side by side in one walk**, in ordinal order. Three separate
///        integrations would evaluate the caller's spectrum three times, and a spectrum that reads a medium
///        profile is not cheap enough for that to be a matter of taste.
/// note  ⚠️ Projecting a per-wavelength *rate* — an extinction coefficient, say — and then exponentiating it is
///        not the same as exponentiating per wavelength and projecting the result. `28` does the former, because
///        the latter needs a spectral transmittance surface rather than a tristimulus one; the discrepancy grows
///        with optical depth and is visible only at grazing angles through the whole atmosphere. Declared here so
///        that whoever measures it later finds the reason rather than the symptom.
/// cost  🔴
/// tag   api, nonthrowing
template <typename Spectrum>
Deliver<TristimulusCoordinate> ProjectSpectrum(const QuadratureRule& Rule, Spectrum Evaluate)
{
    if (!Rule.Derived())
    {
        return Deliver<TristimulusCoordinate>::Refuse(
            { RefusalReason::ContentUnsupported, "the rule has not been derived" });
    }

    const Deliver<double> Normalisation = LuminanceNormalisation(Rule);

    if (!Normalisation.Resolved)
        return Deliver<TristimulusCoordinate>::Refuse(Normalisation.Error);

    if (Normalisation.Resolve() <= 0.0)
    {
        return Deliver<TristimulusCoordinate>::Refuse(
            { RefusalReason::ContentUnsupported, "the luminance response integrates to nothing" });
    }

    TristimulusCoordinate Projected;

    for (std::uint32_t Index = 0u; Index < Rule.DeclaredCount(); ++Index)
    {
        double Wavelength = 0.0;
        double Weighting  = 0.0;

        if (!Rule.Project(Index, SpectralLowerWavelength, SpectralUpperWavelength, Wavelength, Weighting)
                 .Resolved)
        {
            continue;
        }

        const TristimulusCoordinate Response = ProjectWavelength(Wavelength);
        const double                Magnitude = Evaluate(Wavelength) * Weighting;

        Projected.MagnitudeX += Response.MagnitudeX * Magnitude;
        Projected.MagnitudeY += Response.MagnitudeY * Magnitude;
        Projected.MagnitudeZ += Response.MagnitudeZ * Magnitude;
    }

    const double Reciprocal = 1.0 / Normalisation.Resolve();

    Projected.MagnitudeX *= Reciprocal;
    Projected.MagnitudeY *= Reciprocal;
    Projected.MagnitudeZ *= Reciprocal;

    return Deliver<TristimulusCoordinate>::Result(Projected);
}

}   // namespace Slate
