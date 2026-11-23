//============================================================================================================================================
//                                                         CADPROJECTION.CPP
//============================================================================================================================================

#include "SlateWorkspace/Discipline/ViewportProjection/Api/CadProjection.h"

#include <cmath>

namespace Slate
{

WorkspaceCadProjection ResolveCadProjection(const SpatialBasis& Basis,
                                            const ViewportStanding& View,
                                            bool Perspective,
                                            const PlaneExtent& LogicalExtent,
                                            const DrawableScale& Drawable,
                                            std::uint32_t DisplayWidth,
                                            std::uint32_t DisplayHeight)
{
    const ViewFrame Frame = ResolveViewportFrame(Basis, View, Perspective);

    // Everything below is in PHYSICAL pixels, matching the DisplayWidth the shader divides by.
    const PlaneExtent Extent = Drawable.ToPhysical(LogicalExtent);
    const double      OrthoScale = View.OrthoScale * Drawable.Factor;

    const float CentreX = Extent.MinimumX + Extent.Width() * 0.5f;
    const float CentreY = Extent.MinimumY + Extent.Height() * 0.5f;

    const auto ScreenProjection = [&](const SpatialPoint& Origin,
                                      const SpatialDirection& Along,
                                      const SpatialDirection& Across,
                                      float* Projection0,
                                      float* Projection1,
                                      float* Projection2)
    {
        if (!Perspective)
        {
            const SpatialDirection FocusToOrigin = Difference(View.Focus, Origin);
            const double BaseX = Dot(FocusToOrigin, Frame.Right);
            const double BaseY = Dot(FocusToOrigin, Frame.Up);
            const double AlongX = Dot(Along, Frame.Right);
            const double AlongY = Dot(Along, Frame.Up);
            const double AcrossX = Dot(Across, Frame.Right);
            const double AcrossY = Dot(Across, Frame.Up);

            Projection0[0] = CentreX + static_cast<float>(BaseX * OrthoScale);
            Projection0[1] = CentreY - static_cast<float>(BaseY * OrthoScale);
            Projection0[2] = 0.0f;
            Projection0[3] = 1.0f;

            Projection1[0] = static_cast<float>(AlongX * OrthoScale);
            Projection1[1] = static_cast<float>(-AlongY * OrthoScale);
            Projection1[2] = 0.0f;
            Projection1[3] = 0.0f;

            Projection2[0] = static_cast<float>(AcrossX * OrthoScale);
            Projection2[1] = static_cast<float>(-AcrossY * OrthoScale);
            Projection2[2] = 0.0f;
            Projection2[3] = 0.0f;
            return;
        }

        // 🔴 The GPU CAD pass gets the standing's lens too, so the rasterised sketch and the
        //    analytic ground are drawn through one camera rather than two.
        const double TanHalf = std::tan(View.FieldOfViewDegrees * 0.5 * ProjectionPi / 180.0);
        const double Focal = (Extent.Height() * 0.5) / TanHalf;
        const SpatialDirection EyeToOrigin = Difference(Frame.Eye, Origin);
        const double BaseX = Dot(EyeToOrigin, Frame.Right);
        const double BaseY = Dot(EyeToOrigin, Frame.Up);
        const double BaseZ = Dot(EyeToOrigin, Frame.Forward);
        const double AlongX = Dot(Along, Frame.Right);
        const double AlongY = Dot(Along, Frame.Up);
        const double AlongZ = Dot(Along, Frame.Forward);
        const double AcrossX = Dot(Across, Frame.Right);
        const double AcrossY = Dot(Across, Frame.Up);
        const double AcrossZ = Dot(Across, Frame.Forward);

        Projection0[0] = static_cast<float>(CentreX * BaseZ + Focal * BaseX);
        Projection0[1] = static_cast<float>(CentreY * BaseZ - Focal * BaseY);
        Projection0[2] = 0.0f;
        Projection0[3] = static_cast<float>(BaseZ);

        Projection1[0] = static_cast<float>(CentreX * AlongZ + Focal * AlongX);
        Projection1[1] = static_cast<float>(CentreY * AlongZ - Focal * AlongY);
        Projection1[2] = 0.0f;
        Projection1[3] = static_cast<float>(AlongZ);

        Projection2[0] = static_cast<float>(CentreX * AcrossZ + Focal * AcrossX);
        Projection2[1] = static_cast<float>(CentreY * AcrossZ - Focal * AcrossY);
        Projection2[2] = 0.0f;
        Projection2[3] = static_cast<float>(AcrossZ);
    };

    WorkspaceCadProjection Projection = {};
    Projection.DisplayWidth = static_cast<float>(DisplayWidth);
    Projection.DisplayHeight = static_cast<float>(DisplayHeight);
    ScreenProjection(Basis.Origin, Basis.Along, Basis.Across,
                     Projection.Projection0, Projection.Projection1, Projection.Projection2);
    return Projection;
}

}   // namespace Slate
