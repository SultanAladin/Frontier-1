/*==========================================================================================================================================
                                                            SURFACENORMALPROJECTION.H
==========================================================================================================================================*/
// 🧩 Camera-space normal projection, bounded display-referred MatCap pixels and a minimum-limit 128-byte shader ABI.

#pragma once
#include "SurfaceGeometry.h"
#include <algorithm>
#include <stdexcept>

namespace Frontier
{
//------------------------------------------------------------------------------------------------------------------------
//                                                     TEXTURE AND PROJECTION CONTRACTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Top row first, RGBA8 display-referred RGB; alpha is ignored by opaque MatCap shading. No sRGB decode or tone map.
struct SurfaceTextureImage
{
    uint32_t Width  = 0;
    uint32_t Height = 0;
    std::vector<uint8_t> Rgba;
};

struct alignas(16) SurfaceNormalProjection
{
    std::array<float, 16> ClipFromSurface{};
    std::array<float, 4> ViewNormalRow0{1,0,0,0};
    std::array<float, 4> ViewNormalRow1{0,1,0,0};
    std::array<float, 4> ViewNormalRow2{0,0,1,0};
    std::array<float, 4> SurfaceTint{1,1,1,1};
};

static_assert(sizeof(SurfaceNormalProjection) == 128);
static_assert(offsetof(SurfaceNormalProjection, ViewNormalRow0) == 64);
static_assert(offsetof(SurfaceNormalProjection, ViewNormalRow1) == 80);
static_assert(offsetof(SurfaceNormalProjection, ViewNormalRow2) == 96);
static_assert(offsetof(SurfaceNormalProjection, SurfaceTint) == 112);

inline bool ValidateSurfaceTextureImage(const SurfaceTextureImage &Image)
{
    return Image.Width && Image.Height && Image.Width <= 4096 && Image.Height <= 4096 &&
           Image.Rgba.size() == size_t(Image.Width) * Image.Height * 4;
}

inline bool ValidateSurfaceNormalProjection(const SurfaceNormalProjection &Projection)
{
    for (const auto Coefficient : Projection.ClipFromSurface)
        if (!std::isfinite(Coefficient)) return false;
    const auto &First  = Projection.ViewNormalRow0;
    const auto &Second = Projection.ViewNormalRow1;
    const auto &Third  = Projection.ViewNormalRow2;
    for (const auto *Row : {&First, &Second, &Third})
    {
        for (const auto Coefficient : *Row)
            if (!std::isfinite(Coefficient) || std::abs(Coefficient) > 1) return false;
        if ((*Row)[3] != 0) return false;
    }
    const double Determinant = double(First[0]) * (double(Second[1])*Third[2] - double(Second[2])*Third[1]) -
                               double(First[1]) * (double(Second[0])*Third[2] - double(Second[2])*Third[0]) +
                               double(First[2]) * (double(Second[0])*Third[1] - double(Second[1])*Third[0]);
    if (std::abs(Determinant) <= 1e-12) return false;
    for (const auto Channel : Projection.SurfaceTint)
        if (!std::isfinite(Channel) || Channel < 0 || Channel > 1) return false;
    return Projection.SurfaceTint[3] == 1;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     INVERSE TRANSPOSE NORMAL TRANSFORM
//------------------------------------------------------------------------------------------------------------------------

// 📝 ViewFromSurface is column-major affine, right-handed camera space (positive Y up, camera looks down -Z).
// Normalize the cofactors by a common positive scale to keep float products bounded without changing normal directions.
inline SurfaceNormalProjection DeriveSurfaceNormalProjection(
    const SurfaceProjection     &Projection,
    const std::array<double, 16> &ViewFromSurface)
{
    for (const auto Coefficient : ViewFromSurface)
        if (!std::isfinite(Coefficient)) throw std::runtime_error("Nonfinite surface view transform refused.");
    if (ViewFromSurface[3] != 0 || ViewFromSurface[7] != 0 || ViewFromSurface[11] != 0 || ViewFromSurface[15] != 1)
        throw std::runtime_error("surface normals require an affine view-from-surface transform.");
    double Linear[3][3]{};
    double Maximum = 0;
    for (size_t Row = 0; Row < 3; ++Row)
        for (size_t Column = 0; Column < 3; ++Column)
            Maximum = std::max(Maximum,std::abs(ViewFromSurface[Column*4+Row]));
    if (Maximum == 0) throw std::runtime_error("Singular surface normal transform refused.");
    for (size_t Row = 0; Row < 3; ++Row)
        for (size_t Column = 0; Column < 3; ++Column)
            Linear[Row][Column] = ViewFromSurface[Column*4+Row] / Maximum;
    double Cofactors[3][3]{};
    double CofactorMaximum = 0;
    for (size_t Row = 0; Row < 3; ++Row)
        for (size_t Column = 0; Column < 3; ++Column)
        {
            const size_t NextRow = (Row+1)%3, LastRow = (Row+2)%3;
            const size_t NextColumn = (Column+1)%3, LastColumn = (Column+2)%3;
            Cofactors[Row][Column] = Linear[NextRow][NextColumn]*Linear[LastRow][LastColumn] -
                                     Linear[NextRow][LastColumn]*Linear[LastRow][NextColumn];
            CofactorMaximum = std::max(CofactorMaximum,std::abs(Cofactors[Row][Column]));
        }
    const double Determinant = Linear[0][0]*Cofactors[0][0] + Linear[0][1]*Cofactors[0][1] + Linear[0][2]*Cofactors[0][2];
    if (std::abs(Determinant) <= 1e-12 || CofactorMaximum == 0)
        throw std::runtime_error("Singular or ill-conditioned surface normal transform refused.");
    SurfaceNormalProjection Result;
    Result.ClipFromSurface = Projection.ClipFromSurface;
    const double SignedScale = (Determinant < 0 ? -1 : 1) / CofactorMaximum;
    const std::array<std::array<float,4> *,3> Rows{&Result.ViewNormalRow0,&Result.ViewNormalRow1,&Result.ViewNormalRow2};
    for (size_t Row = 0; Row < 3; ++Row)
        for (size_t Column = 0; Column < 3; ++Column)
            (*Rows[Row])[Column] = float(Cofactors[Row][Column]*SignedScale);
    if (!ValidateSurfaceNormalProjection(Result)) throw std::runtime_error("Unrepresentable surface normal projection refused.");
    return Result;
}
} // namespace Frontier
