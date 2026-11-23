/*==========================================================================================================================================
                                                 TYRESOLIDCONSTITUTIVE.SLANG
==========================================================================================================================================*/
// 🧩 Clamped-J compressible Neo-Hookean first Piola stress used by the retained tyre solid.
//    Rows describe the deformation gradient; positive-J material behaviour is checked separately.
struct TyreSolidStress
{
    float3 Row0;
    float3 Row1;
    float3 Row2;
    float Jacobian;
};

TyreSolidStress EvaluateTyreSolidStress(float3 F0, float3 F1, float3 F2, float Mu, float Lambda)
{
    float3 C0 = cross(F1, F2), C1 = cross(F2, F0), C2 = cross(F0, F1);
    float Jacobian = clamp(dot(F0, C0), 0.30, 2.20);
    float Coefficient = (Lambda * Jacobian * (Jacobian - 1.0) - Mu) / Jacobian;
    TyreSolidStress Stress;
    Stress.Row0 = Mu * F0 + Coefficient * C0;
    Stress.Row1 = Mu * F1 + Coefficient * C1;
    Stress.Row2 = Mu * F2 + Coefficient * C2;
    Stress.Jacobian = Jacobian;
    return Stress;
}
