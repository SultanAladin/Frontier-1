/*==========================================================================================================================================
                                                      DIELECTRICREFRACTION.SLANG
==========================================================================================================================================*/
// 🧩 Snell boundary evaluation; include ScatteringEvaluation.slang first for exact dielectric Fresnel.

struct DielectricRefraction
{
    float3 Direction;
    float Reflectance, RadianceScale;
    bool TotalInternalReflection, Valid;
};
DielectricRefraction EvaluateDielectricRefraction(float3 Incident, float3 Normal, float IncidentIor, float TransmittedIor)
{
    DielectricRefraction R = (DielectricRefraction)0;
    R.Reflectance = 1;
    if (!isfinite(IncidentIor) || !isfinite(TransmittedIor) || IncidentIor <= 0 || TransmittedIor <= 0 ||
        !all(isfinite(Incident)) || !all(isfinite(Normal)) || abs(dot(Incident, Incident) - 1) > 1e-4 ||
        abs(dot(Normal, Normal) - 1) > 1e-4 || dot(Incident, Normal) > 1e-6) return R;
    float Cosine = saturate(-dot(Incident, Normal)), η = IncidentIor / TransmittedIor;
    if (!isfinite(η * η)) return R;
    R.Valid = true;
    R.RadianceScale = η * η;
    float Squared = 1 - η * η * (1 - Cosine * Cosine);
    if (Squared < 0)
    {
        R.TotalInternalReflection = true;
        return R;
    }
    R.Reflectance = EvaluateExactDielectricReflectance(Cosine, IncidentIor, TransmittedIor);
    R.Direction = normalize(η * Incident + (η * Cosine - sqrt(Squared)) * Normal);
    return R;
}
