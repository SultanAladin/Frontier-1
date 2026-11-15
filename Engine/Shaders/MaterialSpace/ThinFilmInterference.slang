/*============================================================================================================================================
                                                          THINFILMINTERFERENCE.SLANG
============================================================================================================================================*/
// 🧩 Lobe 4: Belcour & Barla (SIGGRAPH 2017) nanometer Airy multi-beam wave-optics thin-film interference

module ThinFilmInterference;

public float EvaluateMonochromaticAiry(
    float CosThetaIncident,
    float WavelengthNm,
    float FilmThicknessNm,
    float ExteriorIOR,
    float FilmIOR,
    float SubstratePowerReflectance)
{
    float Cos0   = saturate(CosThetaIncident);
    float Sin1Sq = (ExteriorIOR * ExteriorIOR / max(1.0e-4, FilmIOR * FilmIOR)) * (1.0 - Cos0 * Cos0);
    if (Sin1Sq >= 1.0)
    {
        return 1.0;
    }

    float Cos1 = sqrt(max(1.0e-6, 1.0 - Sin1Sq));
    float R01s = (ExteriorIOR * Cos0 - FilmIOR * Cos1) / max(1.0e-6, ExteriorIOR * Cos0 + FilmIOR * Cos1);
    float R01p = (FilmIOR * Cos0 - ExteriorIOR * Cos1) / max(1.0e-6, FilmIOR * Cos0 + ExteriorIOR * Cos1);
    float R01  = 0.5 * (R01s + R01p);

    float R12Mag = sqrt(clamp(SubstratePowerReflectance, 0.0, 0.9999));
    float SubIor = (1.0 + R12Mag) / max(1.0e-3, 1.0 - R12Mag);
    float Sign12 = (SubIor >= FilmIOR) ? -1.0 : 1.0;
    float R12    = Sign12 * R12Mag;

    float OpdNm  = 2.0 * FilmIOR * FilmThicknessNm * Cos1;
    float Phase  = 6.28318530718 * (OpdNm / max(360.0, WavelengthNm));
    float CosPhi = cos(Phase);

    float R01Sq  = R01 * R01;
    float R12Sq  = R12 * R12;
    float Numer  = R01Sq + R12Sq + 2.0 * R01 * R12 * CosPhi;
    float Denom  = max(1.0e-6, 1.0 + R01Sq * R12Sq + 2.0 * R01 * R12 * CosPhi);
    return saturate(Numer / Denom);
}

public float3 EvaluateSpectralThinFilmFresnel(
    float  CosThetaD,
    float  FilmThicknessNm,
    float  FilmIOR,
    float  ExteriorIOR,
    float3 BaseSubstrateFresnel,
    float  ThinFilmWeight,
    bool   SecondaryBounceRgbMode)
{
    float Weight = saturate(ThinFilmWeight);
    if (Weight <= 1.0e-5 || FilmThicknessNm <= 0.5)
    {
        return BaseSubstrateFresnel;
    }

    if (SecondaryBounceRgbMode)
    {
        float3 AiryRgb = float3(
            EvaluateMonochromaticAiry(CosThetaD, 630.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.r),
            EvaluateMonochromaticAiry(CosThetaD, 532.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.g),
            EvaluateMonochromaticAiry(CosThetaD, 465.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.b));
        return lerp(BaseSubstrateFresnel, AiryRgb, Weight);
    }

    float B440 = EvaluateMonochromaticAiry(CosThetaD, 440.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.b);
    float B475 = EvaluateMonochromaticAiry(CosThetaD, 475.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.b);
    float B515 = EvaluateMonochromaticAiry(CosThetaD, 515.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.g);
    float B555 = EvaluateMonochromaticAiry(CosThetaD, 555.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.g);
    float B600 = EvaluateMonochromaticAiry(CosThetaD, 600.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.r);
    float B645 = EvaluateMonochromaticAiry(CosThetaD, 645.0, FilmThicknessNm, ExteriorIOR, FilmIOR, BaseSubstrateFresnel.r);

    float3 SpectralRgb = float3(
        0.02 * B515 + 0.18 * B555 + 0.44 * B600 + 0.36 * B645,
        0.02 * B440 + 0.16 * B475 + 0.42 * B515 + 0.34 * B555 + 0.06 * B600,
        0.56 * B440 + 0.44 * B475);
    return saturate(lerp(BaseSubstrateFresnel, SpectralRgb, Weight));
}
