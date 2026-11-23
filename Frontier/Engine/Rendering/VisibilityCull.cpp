/*==========================================================================================================================================
                                                      VISIBILITY CULL DEFINITION
==========================================================================================================================================*/

#include "VisibilityCull.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{

namespace
{

double Dot(const SpatialVector& A, const SpatialVector& B) noexcept
{
    return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
}

SpatialVector Subtract(const SpatialVector& A, const SpatialVector& B) noexcept
{
    return {A.X - B.X, A.Y - B.Y, A.Z - B.Z};
}

SpatialVector Scale(const SpatialVector& A, double S) noexcept
{
    return {A.X * S, A.Y * S, A.Z * S};
}

SpatialVector Add(const SpatialVector& A, const SpatialVector& B) noexcept
{
    return {A.X + B.X, A.Y + B.Y, A.Z + B.Z};
}

double Length(const SpatialVector& A) noexcept
{
    return std::sqrt(Dot(A, A));
}

bool NormalisedOrZero(const SpatialVector& V, SpatialVector& Out) noexcept
{
    const double Magnitude = Length(V);
    if (Magnitude <= 0.0)
    {
        return false;
    }
    Out = Scale(V, 1.0 / Magnitude);
    return true;
}

FrustumPlane PlaneThrough(const SpatialVector& Normal, const SpatialVector& Point) noexcept
{
    FrustumPlane Plane;
    Plane.Normal = Normal;
    Plane.Offset = -Dot(Normal, Point);
    return Plane;
}

// View-space coordinates of a world point: X right, Y up, Z forward (positive in front).
SpatialVector ToView(const CullCamera& Camera, const SpatialVector& World) noexcept
{
    const SpatialVector Relative = Subtract(World, Camera.Position);
    return {Dot(Relative, Camera.Right), Dot(Relative, Camera.Up), Dot(Relative, Camera.Forward)};
}

double NdcDepth(const CullCamera& Camera, double ViewZ) noexcept
{
    const double Range = Camera.FarPlane - Camera.NearPlane;
    if (Range <= 0.0)
    {
        return 0.0;
    }
    return std::clamp((ViewZ - Camera.NearPlane) / Range, 0.0, 1.0);
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                          FRUSTUM
//------------------------------------------------------------------------------------------------------------------------

bool CameraFrustum::Build(const CullCamera& Camera)
{
    Planes_.clear();
    SpatialVector Forward, Right, Up;
    if (!NormalisedOrZero(Camera.Forward, Forward) || !NormalisedOrZero(Camera.Right, Right) ||
        !NormalisedOrZero(Camera.Up, Up) || Camera.FarPlane <= Camera.NearPlane || Camera.NearPlane <= 0.0 ||
        Camera.Aspect <= 0.0)
    {
        return false;
    }
    const double TanHalfY = std::tan(Camera.FovYRadians * 0.5);
    const double TanHalfX = TanHalfY * Camera.Aspect;
    if (TanHalfY <= 0.0)
    {
        return false;
    }

    const SpatialVector& Eye = Camera.Position;
    // Side planes pass through the eye; each inward normal N satisfies N · E = 0 for its edge direction
    // E (e.g. left edge F - R·tanX) and N · F > 0, giving the axis form N = ±axis + F·tan (orthonormal basis).
    SpatialVector LeftNormal, RightNormal, BottomNormal, TopNormal;
    NormalisedOrZero(Add(Right, Scale(Forward, TanHalfX)), LeftNormal);
    NormalisedOrZero(Add(Scale(Right, -1.0), Scale(Forward, TanHalfX)), RightNormal);
    NormalisedOrZero(Add(Up, Scale(Forward, TanHalfY)), BottomNormal);
    NormalisedOrZero(Add(Scale(Up, -1.0), Scale(Forward, TanHalfY)), TopNormal);

    Planes_.push_back(PlaneThrough(LeftNormal, Eye));
    Planes_.push_back(PlaneThrough(RightNormal, Eye));
    Planes_.push_back(PlaneThrough(BottomNormal, Eye));
    Planes_.push_back(PlaneThrough(TopNormal, Eye));
    Planes_.push_back(PlaneThrough(Forward, Add(Eye, Scale(Forward, Camera.NearPlane))));
    Planes_.push_back(PlaneThrough(Scale(Forward, -1.0), Add(Eye, Scale(Forward, Camera.FarPlane))));
    return true;
}

bool CameraFrustum::Contains(const SpatialVector& Point) const noexcept
{
    for (const FrustumPlane& Plane : Planes_)
    {
        if (Dot(Plane.Normal, Point) + Plane.Offset < 0.0)
        {
            return false;
        }
    }
    return true;
}

bool CameraFrustum::Intersects(const BoundingSphere& Sphere) const noexcept
{
    for (const FrustumPlane& Plane : Planes_)
    {
        if (Dot(Plane.Normal, Sphere.Centre) + Plane.Offset < -Sphere.Radius)
        {
            return false;
        }
    }
    return true;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                      HI-Z PYRAMID
//------------------------------------------------------------------------------------------------------------------------

bool HiZPyramid::BuildFromDepth(const double* NdcDepth, uint32_t Width, uint32_t Height)
{
    Levels_.clear();
    if (NdcDepth == nullptr || Width == 0 || Height == 0)
    {
        return false;
    }
    Level Base;
    Base.Width  = Width;
    Base.Height = Height;
    Base.MaxDepth.assign(NdcDepth, NdcDepth + static_cast<size_t>(Width) * Height);
    Levels_.push_back(std::move(Base));

    while (Levels_.back().Width > 1 || Levels_.back().Height > 1)
    {
        const Level& Source = Levels_.back();
        Level        Next;
        Next.Width  = std::max(1u, (Source.Width + 1) / 2);
        Next.Height = std::max(1u, (Source.Height + 1) / 2);
        Next.MaxDepth.resize(static_cast<size_t>(Next.Width) * Next.Height);
        for (uint32_t Y = 0; Y < Next.Height; ++Y)
        {
            for (uint32_t X = 0; X < Next.Width; ++X)
            {
                double Maximum = 0.0;
                for (uint32_t DY = 0; DY < 2; ++DY)
                {
                    for (uint32_t DX = 0; DX < 2; ++DX)
                    {
                        const uint32_t SX = std::min(X * 2 + DX, Source.Width - 1);
                        const uint32_t SY = std::min(Y * 2 + DY, Source.Height - 1);
                        Maximum           = std::max(Maximum, Source.MaxDepth[SY * Source.Width + SX]);
                    }
                }
                Next.MaxDepth[Y * Next.Width + X] = Maximum;
            }
        }
        Levels_.push_back(std::move(Next));
    }
    return true;
}

uint32_t HiZPyramid::LevelWidth(uint32_t Level) const noexcept
{
    return Level < Levels_.size() ? Levels_[Level].Width : 0;
}

uint32_t HiZPyramid::LevelHeight(uint32_t Level) const noexcept
{
    return Level < Levels_.size() ? Levels_[Level].Height : 0;
}

double HiZPyramid::Sample(uint32_t Level, uint32_t X, uint32_t Y) const noexcept
{
    if (Level >= Levels_.size())
    {
        return 1.0;   // No occluder recorded: far plane (never occludes)
    }
    const struct Level& Data = Levels_[Level];
    const uint32_t      CX   = std::min(X, Data.Width - 1);
    const uint32_t      CY   = std::min(Y, Data.Height - 1);
    return Data.MaxDepth[CY * Data.Width + CX];
}

bool HiZPyramid::IsOccluded(const CullCamera& Camera, const BoundingSphere& Sphere) const noexcept
{
    if (Levels_.empty())
    {
        return false;
    }
    const SpatialVector View = ToView(Camera, Sphere.Centre);
    if (View.Z + Sphere.Radius < Camera.NearPlane || View.Z - Sphere.Radius > Camera.FarPlane)
    {
        return false;   // Outside the clip range is the frustum's call, not occlusion's
    }
    const double TanHalfY = std::tan(Camera.FovYRadians * 0.5);
    const double TanHalfX = TanHalfY * Camera.Aspect;
    if (TanHalfY <= 0.0)
    {
        return false;
    }
    const double CentreZ    = std::max(View.Z, Camera.NearPlane);
    const double NdcRadiusX = Sphere.Radius / (CentreZ * TanHalfX);
    const double NdcRadiusY = Sphere.Radius / (CentreZ * TanHalfY);
    const double NdcX       = View.X / (CentreZ * TanHalfX);
    const double NdcY       = View.Y / (CentreZ * TanHalfY);

    // NDC [-1, 1] -> UV [0, 1]; clamp the covered rectangle to the buffer.
    const double U0 = std::clamp((NdcX - NdcRadiusX) * 0.5 + 0.5, 0.0, 1.0);
    const double U1 = std::clamp((NdcX + NdcRadiusX) * 0.5 + 0.5, 0.0, 1.0);
    const double V0 = std::clamp((NdcY - NdcRadiusY) * 0.5 + 0.5, 0.0, 1.0);
    const double V1 = std::clamp((NdcY + NdcRadiusY) * 0.5 + 0.5, 0.0, 1.0);
    if (U1 <= U0 || V1 <= V0)
    {
        return false;
    }

    const double PixelExtent = std::max((U1 - U0) * static_cast<double>(Levels_[0].Width),
                                        (V1 - V0) * static_cast<double>(Levels_[0].Height));
    uint32_t     Level       = 0;
    if (PixelExtent > 1.0)
    {
        Level = static_cast<uint32_t>(std::ceil(std::log2(PixelExtent)));
    }
    Level = std::min(Level, static_cast<uint32_t>(Levels_.size() - 1));

    const uint32_t Width  = Levels_[Level].Width;
    const uint32_t Height = Levels_[Level].Height;
    const uint32_t X0     = std::min(static_cast<uint32_t>(U0 * Width), Width - 1);
    const uint32_t X1     = std::min(static_cast<uint32_t>(U1 * Width), Width - 1);
    const uint32_t Y0     = std::min(static_cast<uint32_t>((1.0 - V1) * Height), Height - 1);
    const uint32_t Y1     = std::min(static_cast<uint32_t>((1.0 - V0) * Height), Height - 1);

    double FarthestOccluder = 0.0;
    for (uint32_t Y = Y0; Y <= Y1; ++Y)
    {
        for (uint32_t X = X0; X <= X1; ++X)
        {
            FarthestOccluder = std::max(FarthestOccluder, Sample(Level, X, Y));
        }
    }
    const double NearestZ = NdcDepth(Camera, View.Z - Sphere.Radius);
    return NearestZ > FarthestOccluder + 1e-9;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       CULL DRIVERS
//------------------------------------------------------------------------------------------------------------------------

void CullCut(const ClusterNesting& Nesting, const std::vector<uint32_t>& Cut, const CameraFrustum& Frustum,
             const CullCamera& Camera, const HiZPyramid* Pyramid, std::vector<uint32_t>& OutVisible)
{
    OutVisible.clear();
    const std::vector<ClusterNode>& Nodes = Nesting.Nodes();
    for (const uint32_t Index : Cut)
    {
        if (Index >= Nodes.size())
        {
            continue;
        }
        const BoundingSphere& Bounds = Nodes[Index].Bounds;
        if (!Frustum.Intersects(Bounds))
        {
            continue;
        }
        if (Pyramid != nullptr && Pyramid->IsOccluded(Camera, Bounds))
        {
            continue;
        }
        OutVisible.push_back(Index);
    }
}

} // namespace Frontier
