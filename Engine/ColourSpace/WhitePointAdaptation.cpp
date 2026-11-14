/*============================================================================================================================================
                                                         WHITEPOINTADAPTATION.CPP
============================================================================================================================================*/
// 🎨 Implementation of Bradford & CAT02 chromatic adaptation transforms (D65, D60, D50)

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/ColourSpace/WhitePointAdaptation.h"

#include <algorithm>

namespace Frontier::ColourSpace
{

namespace
{

using Transform3x3 = std::array<float, 9u>;

constexpr Transform3x3 BradfordForward = {
     0.8951000f,  0.2664000f, -0.1614000f,
    -0.7502000f,  1.7135000f,  0.0367000f,
     0.0389000f, -0.0685000f,  1.0296000f,
};

constexpr Transform3x3 BradfordInverse = {
     0.9869929f, -0.1470543f,  0.1599627f,
     0.4323053f,  0.5183603f,  0.0492912f,
    -0.0085287f,  0.0400428f,  0.9684867f,
};

constexpr Transform3x3 Cat02Forward = {
     0.7328000f,  0.4296000f, -0.1624000f,
    -0.7036000f,  1.6975000f,  0.0061000f,
     0.0030000f,  0.0136000f,  0.9834000f,
};

constexpr Transform3x3 Cat02Inverse = {
     1.0961238f, -0.2788690f,  0.1827452f,
     0.4543690f,  0.4735332f,  0.0720978f,
    -0.0096276f, -0.0056980f,  1.0153256f,
};

[[nodiscard]] CieXyzTriplet MultiplyTransformVector(const Transform3x3& M, CieXyzTriplet V) noexcept
{
    return {
        M[0] * V.X + M[1] * V.Y + M[2] * V.Z,
        M[3] * V.X + M[4] * V.Y + M[5] * V.Z,
        M[6] * V.X + M[7] * V.Y + M[8] * V.Z,
    };
}

[[nodiscard]] Transform3x3 MultiplyTransform3x3(const Transform3x3& A, const Transform3x3& B) noexcept
{
    Transform3x3 Out {};
    for (int Row = 0; Row < 3; ++Row)
    {
        for (int Col = 0; Col < 3; ++Col)
        {
            Out[Row * 3 + Col] = A[Row * 3 + 0] * B[0 * 3 + Col]
                               + A[Row * 3 + 1] * B[1 * 3 + Col]
                               + A[Row * 3 + 2] * B[2 * 3 + Col];
        }
    }
    return Out;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              ILLUMINANTS & CHROMATIC ADAPTATION
//------------------------------------------------------------------------------------------------------------------------

CieXyzTriplet QueryIlluminantWhiteXyz(StandardIlluminant Illuminant) noexcept
{
    switch (Illuminant)
    {
        case StandardIlluminant::D65: return { 0.950470f, 1.000000f, 1.088830f };
        case StandardIlluminant::D60: return { 0.952646f, 1.000000f, 1.008825f };
        case StandardIlluminant::D50: return { 0.964220f, 1.000000f, 0.825210f };
        default:                      return { 0.950470f, 1.000000f, 1.088830f };
    }
}

float QueryIlluminantKelvin(StandardIlluminant Illuminant) noexcept
{
    switch (Illuminant)
    {
        case StandardIlluminant::D65: return 6504.0f;
        case StandardIlluminant::D60: return 6000.0f;
        case StandardIlluminant::D50: return 5003.0f;
        default:                      return 6504.0f;
    }
}

std::array<float, 9u> ConstructAdaptationTransform3x3(
    StandardIlluminant        SourceIlluminant,
    StandardIlluminant        TargetIlluminant,
    ChromaticAdaptationMethod Method) noexcept
{
    if (SourceIlluminant == TargetIlluminant)
    {
        return { 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f };
    }

    const Transform3x3& Forward = (Method == ChromaticAdaptationMethod::Cat02) ? Cat02Forward : BradfordForward;
    const Transform3x3& Inverse = (Method == ChromaticAdaptationMethod::Cat02) ? Cat02Inverse : BradfordInverse;

    const CieXyzTriplet SourceCone = MultiplyTransformVector(Forward, QueryIlluminantWhiteXyz(SourceIlluminant));
    const CieXyzTriplet TargetCone = MultiplyTransformVector(Forward, QueryIlluminantWhiteXyz(TargetIlluminant));

    const Transform3x3 ScaleDiag = {
        TargetCone.X / std::max(1.0e-6f, SourceCone.X), 0.0f, 0.0f,
        0.0f, TargetCone.Y / std::max(1.0e-6f, SourceCone.Y), 0.0f,
        0.0f, 0.0f, TargetCone.Z / std::max(1.0e-6f, SourceCone.Z),
    };

    return MultiplyTransform3x3(Inverse, MultiplyTransform3x3(ScaleDiag, Forward));
}

CieXyzTriplet AdaptXyzWhitePoint(
    CieXyzTriplet             SourceXyz,
    StandardIlluminant        SourceIlluminant,
    StandardIlluminant        TargetIlluminant,
    ChromaticAdaptationMethod Method) noexcept
{
    if (SourceIlluminant == TargetIlluminant)
    {
        return SourceXyz;
    }
    const Transform3x3 Adapt = ConstructAdaptationTransform3x3(SourceIlluminant, TargetIlluminant, Method);
    return MultiplyTransformVector(Adapt, SourceXyz);
}

} // namespace Frontier::ColourSpace
