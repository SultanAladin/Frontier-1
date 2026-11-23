/*==========================================================================================================================================
                                                            MATCAPPROJECTION.H
==========================================================================================================================================*/
// 🧩 Compatibility names for the shared texture and camera-normal projection contracts used by CAD successors.

#pragma once
#include "SurfaceNormalProjection.h"

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     MATCAP COMPATIBILITY
//------------------------------------------------------------------------------------------------------------------------

using MatCapImage = SurfaceTextureImage;
using MatCapProjection = SurfaceNormalProjection;
inline bool ValidateMatCapImage(const MatCapImage &Image)
{
    return ValidateSurfaceTextureImage(Image);
}
inline bool ValidateMatCapProjection(const MatCapProjection &Projection)
{
    return ValidateSurfaceNormalProjection(Projection);
}
inline MatCapProjection DeriveMatCapProjection(const SurfaceProjection &Projection, const std::array<double,16> &ViewFromSurface)
{
    return DeriveSurfaceNormalProjection(Projection,ViewFromSurface);
}
} // namespace Frontier
