//============================================================================================================================================
//                                                         TRANSFORMPROJECTION.CPP
//============================================================================================================================================
// 🧩 Quaternion composition, matrix derivation, and the 64-bit rebasing subtraction.

#include "SlateMath/Numeric/TransformProjection/Api/TransformProjection.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                 ROTATION COMPOUNDING
//------------------------------------------------------------------------------------------------------------------------

RotationQuaternion Compound(RotationQuaternion OuterRotation, RotationQuaternion InnerRotation)
{
    RotationQuaternion Compounded;

    Compounded.Real = OuterRotation.Real       * InnerRotation.Real
                    - OuterRotation.ImaginaryX * InnerRotation.ImaginaryX
                    - OuterRotation.ImaginaryY * InnerRotation.ImaginaryY
                    - OuterRotation.ImaginaryZ * InnerRotation.ImaginaryZ;

    Compounded.ImaginaryX = OuterRotation.Real       * InnerRotation.ImaginaryX
                          + OuterRotation.ImaginaryX * InnerRotation.Real
                          + OuterRotation.ImaginaryY * InnerRotation.ImaginaryZ
                          - OuterRotation.ImaginaryZ * InnerRotation.ImaginaryY;

    Compounded.ImaginaryY = OuterRotation.Real       * InnerRotation.ImaginaryY
                          - OuterRotation.ImaginaryX * InnerRotation.ImaginaryZ
                          + OuterRotation.ImaginaryY * InnerRotation.Real
                          + OuterRotation.ImaginaryZ * InnerRotation.ImaginaryX;

    Compounded.ImaginaryZ = OuterRotation.Real       * InnerRotation.ImaginaryZ
                          + OuterRotation.ImaginaryX * InnerRotation.ImaginaryY
                          - OuterRotation.ImaginaryY * InnerRotation.ImaginaryX
                          + OuterRotation.ImaginaryZ * InnerRotation.Real;

    // 📐 ‖q‖ drifts from unity by the accumulated rounding of the products above. Renormalising here is what
    //    bounds compounding depth; skipping it makes drift a function of how deeply the artist nested.
    const double NormSquared = Compounded.Real       * Compounded.Real
                             + Compounded.ImaginaryX * Compounded.ImaginaryX
                             + Compounded.ImaginaryY * Compounded.ImaginaryY
                             + Compounded.ImaginaryZ * Compounded.ImaginaryZ;

    if (std::fabs(NormSquared - 1.0) > QuaternionRenormalise && NormSquared > 0.0)
    {
        const double Reciprocal = 1.0 / std::sqrt(NormSquared);
        Compounded.Real        *= Reciprocal;
        Compounded.ImaginaryX  *= Reciprocal;
        Compounded.ImaginaryY  *= Reciprocal;
        Compounded.ImaginaryZ  *= Reciprocal;
    }

    return Compounded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                TRANSFORM COMPOUNDING
//------------------------------------------------------------------------------------------------------------------------

DecomposedTransform Compound(const DecomposedTransform& OuterTransform,
                             const DecomposedTransform& InnerTransform)
{
    DecomposedTransform Compounded;

    Compounded.Rotation = Compound(OuterTransform.Rotation, InnerTransform.Rotation);
    Compounded.ScaleX   = OuterTransform.ScaleX * InnerTransform.ScaleX;
    Compounded.ScaleY   = OuterTransform.ScaleY * InnerTransform.ScaleY;
    Compounded.ScaleZ   = OuterTransform.ScaleZ * InnerTransform.ScaleZ;

    // 📝 The inner translation is scaled and rotated by the outer transform before it is added. Deriving
    //    the outer matrix once for that rotation is legal — it is not stored back.
    const ProjectedTransform OuterProjected = Project(OuterTransform);

    const double ScaledX = InnerTransform.Translation.PositionX;
    const double ScaledY = InnerTransform.Translation.PositionY;
    const double ScaledZ = InnerTransform.Translation.PositionZ;

    Compounded.Translation.PositionX = OuterProjected.Coefficient[0]  * ScaledX
                                     + OuterProjected.Coefficient[4]  * ScaledY
                                     + OuterProjected.Coefficient[8]  * ScaledZ
                                     + OuterProjected.Coefficient[12];

    Compounded.Translation.PositionY = OuterProjected.Coefficient[1]  * ScaledX
                                     + OuterProjected.Coefficient[5]  * ScaledY
                                     + OuterProjected.Coefficient[9]  * ScaledZ
                                     + OuterProjected.Coefficient[13];

    Compounded.Translation.PositionZ = OuterProjected.Coefficient[2]  * ScaledX
                                     + OuterProjected.Coefficient[6]  * ScaledY
                                     + OuterProjected.Coefficient[10] * ScaledZ
                                     + OuterProjected.Coefficient[14];

    return Compounded;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  MATRIX DERIVATION
//------------------------------------------------------------------------------------------------------------------------

ProjectedTransform Project(const DecomposedTransform& Source)
{
    const double ImaginaryX = Source.Rotation.ImaginaryX;
    const double ImaginaryY = Source.Rotation.ImaginaryY;
    const double ImaginaryZ = Source.Rotation.ImaginaryZ;
    const double Real       = Source.Rotation.Real;

    ProjectedTransform Derived;

    Derived.Coefficient[0]  = (1.0 - 2.0 * (ImaginaryY * ImaginaryY + ImaginaryZ * ImaginaryZ)) * Source.ScaleX;
    Derived.Coefficient[1]  = (2.0 * (ImaginaryX * ImaginaryY + ImaginaryZ * Real))             * Source.ScaleX;
    Derived.Coefficient[2]  = (2.0 * (ImaginaryX * ImaginaryZ - ImaginaryY * Real))             * Source.ScaleX;
    Derived.Coefficient[3]  = 0.0;

    Derived.Coefficient[4]  = (2.0 * (ImaginaryX * ImaginaryY - ImaginaryZ * Real))             * Source.ScaleY;
    Derived.Coefficient[5]  = (1.0 - 2.0 * (ImaginaryX * ImaginaryX + ImaginaryZ * ImaginaryZ)) * Source.ScaleY;
    Derived.Coefficient[6]  = (2.0 * (ImaginaryY * ImaginaryZ + ImaginaryX * Real))             * Source.ScaleY;
    Derived.Coefficient[7]  = 0.0;

    Derived.Coefficient[8]  = (2.0 * (ImaginaryX * ImaginaryZ + ImaginaryY * Real))             * Source.ScaleZ;
    Derived.Coefficient[9]  = (2.0 * (ImaginaryY * ImaginaryZ - ImaginaryX * Real))             * Source.ScaleZ;
    Derived.Coefficient[10] = (1.0 - 2.0 * (ImaginaryX * ImaginaryX + ImaginaryY * ImaginaryY)) * Source.ScaleZ;
    Derived.Coefficient[11] = 0.0;

    Derived.Coefficient[12] = Source.Translation.PositionX;
    Derived.Coefficient[13] = Source.Translation.PositionY;
    Derived.Coefficient[14] = Source.Translation.PositionZ;
    Derived.Coefficient[15] = 1.0;

    return Derived;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       REBASING
//------------------------------------------------------------------------------------------------------------------------

ViewPosition Relative(DocumentPosition Subject, DocumentPosition ViewOrigin)
{
    // 📝 🔴 Both operands are 64-bit and so is the subtraction. Nothing narrows here — the result is still
    //    64-bit and still carries the full difference, and it is the narrowing that is the lossy act.
    ViewPosition Carried;
    Carried.PositionX = Subject.PositionX - ViewOrigin.PositionX;
    Carried.PositionY = Subject.PositionY - ViewOrigin.PositionY;
    Carried.PositionZ = Subject.PositionZ - ViewOrigin.PositionZ;

    return Carried;
}

DevicePosition Narrow(ViewPosition Subject)
{
    // 📝 The narrowing happens once, on a quantity whose magnitude is the distance from the camera rather than
    //    from the document origin. That is the whole of what the rebasing buys, and it is bought here.
    DevicePosition Narrowed;
    Narrowed.PositionX = static_cast<float>(Subject.PositionX);
    Narrowed.PositionY = static_cast<float>(Subject.PositionY);
    Narrowed.PositionZ = static_cast<float>(Subject.PositionZ);

    return Narrowed;
}

DevicePosition Rebase(DocumentPosition Subject, DocumentPosition ViewOrigin)
{
    // 📝 The composition, and not a third arithmetic. A separately written fused form is a second place the
    //    order of the two acts is decided, and the two places disagree the day one of them is amended.
    return Narrow(Relative(Subject, ViewOrigin));
}

}   // namespace Slate
