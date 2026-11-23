//============================================================================================================================================
//                                                       REFLECTANCEINTEGRATOR.CPP
//============================================================================================================================================
// 🧩 Attributes reconstructed from two ordinals, channels resolved once, the four lobes composed, and the lookup that keeps them energy-correct.

#include "SlateCompute/Compute/ReflectanceIntegrator/Api/ReflectanceIntegrator.h"

#include "Shared/SampleProjection.slang.h"
#include "SlateMath/Numeric/ColourProjection/Api/ColourProjection.h"
#include "SlateMath/Numeric/QuadratureIntegrator/Api/QuadratureIntegrator.h"

#include <cmath>

namespace Slate
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    SPATIAL ARITHMETIC
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 The recording's own name, spelled once. The schedule orders by it and `86` reports by it, and two spellings
//    of one name are two recordings as far as the ordering is concerned.
const char* const ReflectanceRecordingIdentity = "18-ReflectanceIntegrator";

// 📝 Three ordinates at 64 bits, for the reconstruction and the lobes alone. `02` declares no spatial span type
//    and `SurfaceDirection` is 32-bit storage rather than arithmetic, so widening once here is cheaper than
//    narrowing at every product — and the reconstruction differences positions, which is where the width matters.
struct SpatialSpan
{
    double CoordinateX = 0.0;   // [-]
    double CoordinateY = 0.0;   // [-]
    double CoordinateZ = 0.0;   // [-]
};

SpatialSpan Spanned(double CoordinateX, double CoordinateY, double CoordinateZ)
{
    SpatialSpan Held;
    Held.CoordinateX = CoordinateX;
    Held.CoordinateY = CoordinateY;
    Held.CoordinateZ = CoordinateZ;

    return Held;
}

SpatialSpan Spanned(DocumentPosition Later, DocumentPosition Earlier)
{
    return Spanned(Later.PositionX - Earlier.PositionX,
                   Later.PositionY - Earlier.PositionY,
                   Later.PositionZ - Earlier.PositionZ);
}

SpatialSpan Spanned(SurfaceDirection Held)
{
    return Spanned(static_cast<double>(Held.DirectionX),
                   static_cast<double>(Held.DirectionY),
                   static_cast<double>(Held.DirectionZ));
}

double Agreement(SpatialSpan Left, SpatialSpan Right)
{
    return Left.CoordinateX * Right.CoordinateX
         + Left.CoordinateY * Right.CoordinateY
         + Left.CoordinateZ * Right.CoordinateZ;
}

SpatialSpan Perpendicular(SpatialSpan Left, SpatialSpan Right)
{
    return Spanned(Left.CoordinateY * Right.CoordinateZ - Left.CoordinateZ * Right.CoordinateY,
                   Left.CoordinateZ * Right.CoordinateX - Left.CoordinateX * Right.CoordinateZ,
                   Left.CoordinateX * Right.CoordinateY - Left.CoordinateY * Right.CoordinateX);
}

SpatialSpan Accumulated(SpatialSpan Left, SpatialSpan Right)
{
    return Spanned(Left.CoordinateX + Right.CoordinateX,
                   Left.CoordinateY + Right.CoordinateY,
                   Left.CoordinateZ + Right.CoordinateZ);
}

SpatialSpan Weighted(SpatialSpan Held, double Weight)
{
    return Spanned(Held.CoordinateX * Weight, Held.CoordinateY * Weight, Held.CoordinateZ * Weight);
}

SpatialSpan Unitised(SpatialSpan Held)
{
    const double Length = std::sqrt(Agreement(Held, Held));

    if (!(Length > 0.0))
        return Spanned(0.0, 0.0, 0.0);

    return Weighted(Held, 1.0 / Length);
}

SurfaceDirection Narrowed(SpatialSpan Held)
{
    SurfaceDirection Narrowing;
    Narrowing.DirectionX = static_cast<float>(Held.CoordinateX);
    Narrowing.DirectionY = static_cast<float>(Held.CoordinateY);
    Narrowing.DirectionZ = static_cast<float>(Held.CoordinateZ);

    return Narrowing;
}

double Bounded(double Magnitude, double Lower, double Upper)
{
    return Magnitude < Lower ? Lower : (Magnitude > Upper ? Upper : Magnitude);
}

constexpr std::size_t ChannelIndex(ChannelSubject Channel)
{
    return static_cast<std::size_t>(Channel);
}

double ScalarOf(const ResolvedChannelSet& Resolved, ChannelSubject Channel)
{
    return Resolved.Component[ChannelIndex(Channel)][0];
}

SpatialSpan TripleOf(const ResolvedChannelSet& Resolved, ChannelSubject Channel)
{
    const std::size_t Index = ChannelIndex(Channel);

    return Spanned(Resolved.Component[Index][0],
                   Resolved.Component[Index][1],
                   Resolved.Component[Index][2]);
}

}   // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                THE ALBEDO LOOKUP STORAGE
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> DirectionalAlbedoSurface::ConstructDirectionalAlbedoSurface(std::uint32_t Width_, std::uint32_t Height_)
{
    if (Width_ == 0u || Height_ == 0u)
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "a lookup of no extent resolves nothing" });

    SpannedX  = Width_;
    SpannedY = Height_;

    Components.assign(static_cast<std::size_t>(SpannedX)
                    * static_cast<std::size_t>(SpannedY)
                    * static_cast<std::size_t>(ComponentCount), 0.0f);

    return Deliver<bool>::Result(true);
}

void DirectionalAlbedoSurface::Declare(std::uint32_t X,
                                       std::uint32_t Y,
                                       double        Scale,
                                       double        SingleScatter,
                                       double        Charlie)
{
    if (X >= SpannedX || Y >= SpannedY)
        return;

    const std::size_t Writing = (static_cast<std::size_t>(Y) * SpannedX + X) * ComponentCount;

    Components[Writing]      = static_cast<float>(Scale);
    Components[Writing + 1u] = static_cast<float>(SingleScatter);
    Components[Writing + 2u] = static_cast<float>(Charlie);
}

void DirectionalAlbedoSurface::Sample(double  CoordinateX,
                                      double  CoordinateY,
                                      double& Scale,
                                      double& SingleScatter,
                                      double& Charlie) const
{
    Scale         = 0.0;
    SingleScatter = 0.0;
    Charlie       = 0.0;

    if (Components.empty())
        return;

    const double EdgeX  = static_cast<double>(SpannedX);
    const double EdgeY = static_cast<double>(SpannedY);

    double XTexel  = Bounded(CoordinateX,  0.0, 1.0) * EdgeX  - 0.5;
    double YTexel = Bounded(CoordinateY, 0.0, 1.0) * EdgeY - 0.5;

    XTexel  = Bounded(XTexel,  0.0, EdgeX  - 1.0);
    YTexel = Bounded(YTexel, 0.0, EdgeY - 1.0);

    const std::uint32_t MinimumX  = static_cast<std::uint32_t>(XTexel);
    const std::uint32_t MinimumY = static_cast<std::uint32_t>(YTexel);

    const std::uint32_t NextX  = MinimumX  + 1u < SpannedX  ? MinimumX  + 1u : MinimumX;
    const std::uint32_t NextY = MinimumY + 1u < SpannedY ? MinimumY + 1u : MinimumY;

    const double FractionX  = XTexel  - static_cast<double>(MinimumX);
    const double FractionY = YTexel - static_cast<double>(MinimumY);

    const std::size_t LowerLeft  = (static_cast<std::size_t>(MinimumY) * SpannedX + MinimumX) * ComponentCount;
    const std::size_t LowerRight = (static_cast<std::size_t>(MinimumY) * SpannedX + NextX)  * ComponentCount;
    const std::size_t UpperLeft  = (static_cast<std::size_t>(NextY)  * SpannedX + MinimumX) * ComponentCount;
    const std::size_t UpperRight = (static_cast<std::size_t>(NextY)  * SpannedX + NextX)  * ComponentCount;

    double Resolved[ComponentCount] = {};

    for (std::uint32_t Component = 0u; Component < ComponentCount; ++Component)
    {
        const double Lower = static_cast<double>(Components[LowerLeft  + Component]) * (1.0 - FractionX)
                           + static_cast<double>(Components[LowerRight + Component]) * FractionX;

        const double Upper = static_cast<double>(Components[UpperLeft  + Component]) * (1.0 - FractionX)
                           + static_cast<double>(Components[UpperRight + Component]) * FractionX;

        Resolved[Component] = Lower * (1.0 - FractionY) + Upper * FractionY;
    }

    Scale         = Resolved[0];
    SingleScatter = Resolved[1];
    Charlie       = Resolved[2];
}

std::uint32_t DirectionalAlbedoSurface::Width() const  { return SpannedX;  }
std::uint32_t DirectionalAlbedoSurface::Height() const { return SpannedY; }
bool          DirectionalAlbedoSurface::Constructed() const  { return !Components.empty(); }

std::uint64_t DirectionalAlbedoSurface::ResidentBytes() const
{
    return static_cast<std::uint64_t>(Components.size()) * sizeof(float);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 ATTRIBUTE RECONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------

ReconstructedSurface ReconstructSurface(const ReconstructionTriangle& Triangle,
                                        DocumentPosition              Origin,
                                        double                        DirectionX,
                                        double                        DirectionY,
                                        double                        DirectionZ)
{
    ReconstructedSurface Reconstructed;

    const SpatialSpan EdgeAlpha = Spanned(Triangle.Position[1], Triangle.Position[0]);
    const SpatialSpan EdgeBeta  = Spanned(Triangle.Position[2], Triangle.Position[0]);
    const SpatialSpan Facing    = Perpendicular(EdgeAlpha, EdgeBeta);
    const SpatialSpan Viewing   = Spanned(DirectionX, DirectionY, DirectionZ);

    const double Incidence = Agreement(Facing, Viewing);

    // 📝 A ray parallel to the triangle's plane reconstructs nothing rather than dividing by nothing. `16` never
    //    resolves such a pixel to this triangle, so reaching here at all is a caller error rather than geometry.
    if (Incidence == 0.0)
        return Reconstructed;

    const double Parameter = Agreement(Facing, Spanned(Triangle.Position[0], Origin)) / Incidence;

    if (Parameter < 0.0)
        return Reconstructed;

    Reconstructed.Position.PositionX = Origin.PositionX + DirectionX * Parameter;
    Reconstructed.Position.PositionY = Origin.PositionY + DirectionY * Parameter;
    Reconstructed.Position.PositionZ = Origin.PositionZ + DirectionZ * Parameter;

    // 📐 The barycentric weights are taken from areas projected onto the plane the facing is most aligned with,
    //    which is the one projection that cannot collapse the triangle to a segment. Projecting onto a fixed
    //    plane collapses every triangle parallel to it, and the weights then divide by nothing at exactly the
    //    silhouettes where the artist is looking.
    const double MagnitudeX = std::fabs(Facing.CoordinateX);
    const double MagnitudeY = std::fabs(Facing.CoordinateY);
    const double MagnitudeZ = std::fabs(Facing.CoordinateZ);

    std::uint32_t Dominant = 2u;

    if (MagnitudeX >= MagnitudeY && MagnitudeX >= MagnitudeZ)
        Dominant = 0u;
    else if (MagnitudeY >= MagnitudeZ)
        Dominant = 1u;

    double CornerU[3] = {};
    double CornerV[3] = {};

    for (std::uint32_t Corner = 0u; Corner < 3u; ++Corner)
    {
        const DocumentPosition& Held = Triangle.Position[Corner];

        if (Dominant == 0u)      { CornerU[Corner] = Held.PositionY;  CornerV[Corner] = Held.PositionZ; }
        else if (Dominant == 1u) { CornerU[Corner] = Held.PositionZ;  CornerV[Corner] = Held.PositionX; }
        else                     { CornerU[Corner] = Held.PositionX;  CornerV[Corner] = Held.PositionY; }
    }

    double MeetU = 0.0;
    double MeetV = 0.0;

    if (Dominant == 0u)      { MeetU = Reconstructed.Position.PositionY;  MeetV = Reconstructed.Position.PositionZ; }
    else if (Dominant == 1u) { MeetU = Reconstructed.Position.PositionZ;  MeetV = Reconstructed.Position.PositionX; }
    else                     { MeetU = Reconstructed.Position.PositionX;  MeetV = Reconstructed.Position.PositionY; }

    const double AreaWhole = (CornerU[1] - CornerU[0]) * (CornerV[2] - CornerV[0])
                           - (CornerU[2] - CornerU[0]) * (CornerV[1] - CornerV[0]);

    if (AreaWhole == 0.0)
        return Reconstructed;

    const double AreaAlpha = (CornerU[1] - MeetU) * (CornerV[2] - MeetV)
                           - (CornerU[2] - MeetU) * (CornerV[1] - MeetV);
    const double AreaBeta  = (CornerU[2] - MeetU) * (CornerV[0] - MeetV)
                           - (CornerU[0] - MeetU) * (CornerV[2] - MeetV);

    Reconstructed.Weights[0] = AreaAlpha / AreaWhole;
    Reconstructed.Weights[1] = AreaBeta  / AreaWhole;
    Reconstructed.Weights[2] = 1.0 - Reconstructed.Weights[0] - Reconstructed.Weights[1];

    SpatialSpan Orienting = Spanned(0.0, 0.0, 0.0);

    for (std::uint32_t Corner = 0u; Corner < 3u; ++Corner)
    {
        Orienting = Accumulated(Orienting,
                                Weighted(Spanned(Triangle.Orientation[Corner]), Reconstructed.Weights[Corner]));

        Reconstructed.DomainX  += Reconstructed.Weights[Corner]
                                    * static_cast<double>(Triangle.Domain[Corner].CoordinateX);
        Reconstructed.DomainY += Reconstructed.Weights[Corner]
                                    * static_cast<double>(Triangle.Domain[Corner].CoordinateY);
    }

    SpatialSpan Oriented = Unitised(Orienting);

    // 📝 An interpolation of three opposed corner orientations cancels to nothing, which happens on a topology
    //    whose corners were welded across a crease. The face's own facing is the honest fallback: it is what the
    //    positions say, and it is what `38` would have derived for a face carrying no supplied perpendicular.
    if (Agreement(Oriented, Oriented) <= 0.0)
        Oriented = Unitised(Facing);

    Reconstructed.Orientation = Narrowed(Oriented);

    // 🔴 `18` §1.1: the basis is **interpolated** here and **derived** in `38` §4. Where any corner declares no
    //    basis the domain is degenerate at that corner — a chart of no area, a seam vertex — and the pixel is
    //    marked absent rather than orthonormalised from the orientation. `24` §2 rejects a fabricated value for
    //    transfer and it is no better here: what it fabricates is a perturbation direction the artist never
    //    authored, applied to channels they did author.
    Reconstructed.BasisDeclared = Triangle.Basis[0].BasisDeclared
                               && Triangle.Basis[1].BasisDeclared
                               && Triangle.Basis[2].BasisDeclared;

    if (Reconstructed.BasisDeclared)
    {
        SpatialSpan Tangential  = Spanned(0.0, 0.0, 0.0);
        double      Handedness  = 0.0;

        for (std::uint32_t Corner = 0u; Corner < 3u; ++Corner)
        {
            Tangential = Accumulated(Tangential,
                                     Weighted(Spanned(Triangle.Basis[Corner].Tangent),
                                              Reconstructed.Weights[Corner]));

            Handedness += Reconstructed.Weights[Corner]
                        * static_cast<double>(Triangle.Basis[Corner].HandednessSignum);
        }

        // 📐 Re-orthonormalised against the **interpolated** orientation rather than against any corner's. The
        //    two diverge across a triangle wherever the corner orientations do, and a basis left un-orthogonal
        //    tilts every perturbation by the divergence — visible as lighting that shifts across a flat quad.
        const SpatialSpan Projected = Accumulated(Tangential,
                                                  Weighted(Oriented, -Agreement(Tangential, Oriented)));

        const SpatialSpan Unitary = Unitised(Projected);

        if (Agreement(Unitary, Unitary) <= 0.0)
        {
            Reconstructed.BasisDeclared = false;
        }
        else
        {
            Reconstructed.Basis.Tangent          = Narrowed(Unitary);
            Reconstructed.Basis.HandednessSignum = Handedness < 0.0 ? -1.0f : 1.0f;
            Reconstructed.Basis.BasisDeclared    = true;
        }
    }

    // 📐 🔴 The domain gradients are **analytic**, from the triangle's own edges, and are never finite-differenced
    //    across lanes — `18` §1 and §9's second gate. A material's pixel list is spatially scattered, so
    //    neighbouring lanes are not neighbouring pixels; differencing across them produces texture filtering
    //    that is wrong precisely at material boundaries, which is the first place anybody looks.
    //
    // 📝 🚧 The four gradients are expressed against the triangle plane's own two axes rather than against the
    //    display's, because a screen-space gradient needs the ray differentials and the reconstruction is
    //    handed one ray. What is derived here is the whole of the geometric half; the caller multiplies in its
    //    own pixel-to-direction Jacobian, which is two products it already holds. `18` §10 carries the row.
    {
        const SpatialSpan AxisU = Unitised(EdgeAlpha);
        const SpatialSpan AxisV = Unitised(Perpendicular(Oriented, AxisU));

        const double AlphaU = Agreement(EdgeAlpha, AxisU);
        const double AlphaV = Agreement(EdgeAlpha, AxisV);
        const double BetaU  = Agreement(EdgeBeta,  AxisU);
        const double BetaV  = Agreement(EdgeBeta,  AxisV);

        const double Determinant = AlphaU * BetaV - BetaU * AlphaV;

        if (Determinant != 0.0)
        {
            const double AlphaX  = static_cast<double>(Triangle.Domain[1].CoordinateX)
                                     - static_cast<double>(Triangle.Domain[0].CoordinateX);
            const double AlphaY = static_cast<double>(Triangle.Domain[1].CoordinateY)
                                     - static_cast<double>(Triangle.Domain[0].CoordinateY);
            const double BetaX   = static_cast<double>(Triangle.Domain[2].CoordinateX)
                                     - static_cast<double>(Triangle.Domain[0].CoordinateX);
            const double BetaY  = static_cast<double>(Triangle.Domain[2].CoordinateY)
                                     - static_cast<double>(Triangle.Domain[0].CoordinateY);

            const double Reciprocal = 1.0 / Determinant;

            Reconstructed.DomainGradient[0] = (AlphaX  * BetaV - BetaX  * AlphaV) * Reciprocal;
            Reconstructed.DomainGradient[1] = (AlphaU * BetaX  - BetaU * AlphaX)  * Reciprocal;
            Reconstructed.DomainGradient[2] = (AlphaY * BetaV - BetaY * AlphaV) * Reciprocal;
            Reconstructed.DomainGradient[3] = (AlphaU * BetaY - BetaU * AlphaY) * Reciprocal;
        }
    }

    Reconstructed.Reconstructed = true;

    return Reconstructed;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE RECORDING
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ReflectanceIntegrator::Contribute(RenderSchedule& Schedule) const
{
    DeclaredRecording Declared;
    Declared.Identity = ReflectanceRecordingIdentity;

    // 🔴 `18` §6: this **produces** `RadianceSurface` and writes its whole extent, the unoccupied class included.
    //    `62` and `30` amend it afterwards, in that order, and `08` §2's one-producer rule is what makes the
    //    amendment list an ordering rather than a race.
    Declared.Produces = { SharedTarget::RadianceSurface };

    Declared.Reads = { SharedTarget::DepthSurface,
                       SharedTarget::VisibilityIndex,
                       SharedTarget::OccupancySurface,
                       SharedTarget::OcclusionSurface,
                       SharedTarget::DirectOcclusionSurface,
                       SharedTarget::SkyViewSurface };

    Declared.Amends             = {};
    Declared.Command            = RecordingCommand::ComputeDispatch;
    Declared.CapabilityRequired = false;
    Declared.Substitution       = "";
    Declared.DisplayReferred    = false;

    return Schedule.Contribute(Declared);
}

//------------------------------------------------------------------------------------------------------------------------
//                                             THE DIRECTIONAL-ALBEDO DERIVATION
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ReflectanceIntegrator::DeriveDirectionalAlbedo(const QuadratureRule& Rule)
{
    if (!Rule.Derived())
        return Deliver<bool>::Refuse({ RefusalReason::ContentUnsupported, "the rule has not been derived" });

    const Deliver<bool> Constructed = AlbedoLookup.ConstructDirectionalAlbedoSurface(AlbedoExtentX, AlbedoExtentY);

    if (!Constructed.Resolved)
        return Constructed;

    for (std::uint32_t Y = 0u; Y < AlbedoExtentY; ++Y)
    {
        for (std::uint32_t X = 0u; X < AlbedoExtentX; ++X)
        {
            const double CoordinateX  = (static_cast<double>(X)  + 0.5) / AlbedoExtentX;
            const double CoordinateY = (static_cast<double>(Y) + 0.5) / AlbedoExtentY;

            double ViewCosine = 0.0;
            double Roughness  = 0.0;
            ProjectAlbedoParameter(CoordinateX, CoordinateY, ViewCosine, Roughness);

            // 📝 The orientation is the third axis by construction, so the view direction is fixed by its cosine
            //    alone and the whole integral is rotationally symmetric about it. That symmetry is what makes the
            //    lookup two-dimensional rather than three.
            const double ViewSine   = std::sqrt(Bounded(1.0 - ViewCosine * ViewCosine, 0.0, 1.0));
            const SpatialSpan View  = Spanned(ViewSine, 0.0, ViewCosine);

            const double Parameter = Roughness * Roughness;

            double ScaleTerm = 0.0;
            double BiasTerm  = 0.0;

            // 📐 The specular halves are importance-sampled against the distribution rather than integrated on a
            //    fixed rule. GGX carries a lobe that narrows without bound as the roughness vanishes, and a rule
            //    that spends its abscissae evenly over the hemisphere places almost none of them inside it —
            //    which reads as a smooth metal whose reflection is a handful of bright specks.
            for (std::uint32_t Index = 0u; Index < AlbedoSampleCount; ++Index)
            {
                double FirstCoordinate  = 0.0;
                double SecondCoordinate = 0.0;
                ProjectPlanarSample(Index, FirstCoordinate, SecondCoordinate);

                const double Denominator = 1.0 + (Parameter * Parameter - 1.0) * FirstCoordinate;

                if (Denominator <= 0.0)
                    continue;

                const double HalfCosine = std::sqrt(Bounded((1.0 - FirstCoordinate) / Denominator, 0.0, 1.0));
                const double HalfSine   = std::sqrt(Bounded(1.0 - HalfCosine * HalfCosine, 0.0, 1.0));
                const double Azimuth    = 2.0 * Pi * SecondCoordinate;

                const SpatialSpan Half = Spanned(HalfSine * std::cos(Azimuth),
                                                 HalfSine * std::sin(Azimuth),
                                                 HalfCosine);

                const double ViewHalf = Agreement(View, Half);

                const SpatialSpan Incident = Accumulated(Weighted(Half, 2.0 * ViewHalf), Weighted(View, -1.0));

                if (Incident.CoordinateZ <= 0.0 || HalfCosine <= 0.0 || ViewHalf <= 0.0)
                    continue;

                // 📐 The importance weight is 4·(V·H)·G_vis·(N·L)/(N·H), which is what remains once the sampling
                //    density cancels against the distribution. `ProjectVisibilitySmith` has already folded the
                //    four cosines in, so the four here is the only one that survives.
                const double Visibility = ProjectVisibilitySmith(Parameter, ViewCosine, Incident.CoordinateZ);
                const double Weight     = 4.0 * ViewHalf * Visibility * Incident.CoordinateZ / HalfCosine;

                const double Departure = 1.0 - ViewHalf;
                const double Squared   = Departure * Departure;
                const double Fresnel   = Squared * Squared * Departure;

                ScaleTerm += (1.0 - Fresnel) * Weight;
                BiasTerm  += Fresnel * Weight;
            }

            ScaleTerm /= static_cast<double>(AlbedoSampleCount);
            BiasTerm  /= static_cast<double>(AlbedoSampleCount);

            // 📐 The fibre lobe is integrated on the supplied rule instead, over the cosine and the azimuth as a
            //    product. Charlie has no closed importance sampling and its lobe is broad rather than peaked, so
            //    a rule of a few dozen abscissae per axis resolves it where the same count would starve GGX.
            double Fibre = 0.0;

            for (std::uint32_t CosineIndex = 0u; CosineIndex < Rule.DeclaredCount(); ++CosineIndex)
            {
                double IncidentCosine = 0.0;
                double CosineWeight   = 0.0;

                if (!Rule.Project(CosineIndex, 0.0, 1.0, IncidentCosine, CosineWeight).Resolved)
                    continue;

                const double IncidentSine = std::sqrt(Bounded(1.0 - IncidentCosine * IncidentCosine, 0.0, 1.0));

                for (std::uint32_t AzimuthIndex = 0u; AzimuthIndex < Rule.DeclaredCount(); ++AzimuthIndex)
                {
                    double Azimuth       = 0.0;
                    double AzimuthWeight = 0.0;

                    if (!Rule.Project(AzimuthIndex, 0.0, 2.0 * Pi, Azimuth, AzimuthWeight).Resolved)
                        continue;

                    const SpatialSpan Incident = Spanned(IncidentSine * std::cos(Azimuth),
                                                         IncidentSine * std::sin(Azimuth),
                                                         IncidentCosine);

                    const SpatialSpan Half = Unitised(Accumulated(Incident, View));

                    const double Distribution = ProjectSheenDistributionCharlie(Roughness, Half.CoordinateZ);
                    const double Visibility   = ProjectSheenVisibility(ViewCosine, IncidentCosine);

                    Fibre += Distribution * Visibility * IncidentCosine * CosineWeight * AzimuthWeight;
                }
            }

            // 📝 `.y` is the scale and the bias together — the fraction of energy single-scatter GGX preserves at
            //    unit normal-incidence reflectance, which is exactly what `ProjectMultiScatterCompensation` reads.
            AlbedoLookup.Declare(X, Y, ScaleTerm, ScaleTerm + BiasTerm, Fibre);
        }
    }

    LookupDerived = true;

    return Deliver<bool>::Result(true);
}

void ReflectanceIntegrator::SampleDirectionalAlbedo(double  ViewCosine,
                                                    double  Roughness,
                                                    double& SplitSumScale,
                                                    double& SingleScatterAlbedo,
                                                    double& CharlieAlbedo) const
{
    double CoordinateX  = 0.0;
    double CoordinateY = 0.0;
    ProjectAlbedoCoordinate(ViewCosine, Roughness, CoordinateX, CoordinateY);

    AlbedoLookup.Sample(CoordinateX, CoordinateY, SplitSumScale, SingleScatterAlbedo, CharlieAlbedo);
}

const DirectionalAlbedoSurface& ReflectanceIntegrator::DirectionalAlbedo() const { return AlbedoLookup;  }
bool                            ReflectanceIntegrator::AlbedoDerived() const     { return LookupDerived; }

//------------------------------------------------------------------------------------------------------------------------
//                                                   CHANNEL RESOLUTION
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📝 🔴 The declared default, never zero — `42` §2. An occlusion channel defaulted to zero is a black surface and
//    a transmission channel defaulted to zero is an opaque one; only one of those is right, which is exactly why
//    the default is declared per channel rather than assumed once for all twenty.
void DeclareDefault(ResolvedChannelSet& Resolved, ChannelSubject Channel, const ChannelSpecification& Held)
{
    const std::size_t Index = ChannelIndex(Channel);

    if (MeasureCarriesColour(Held.Measured))
    {
        const ColourSpecification& Current = Held.Source == ChannelSource::Constant
                                            ? Held.ConstantColour
                                            : Held.DefaultColour;

        Resolved.Component[Index][0] = Current.RedCoordinate;
        Resolved.Component[Index][1] = Current.GreenCoordinate;
        Resolved.Component[Index][2] = Current.BlueCoordinate;

        return;
    }

    if (Held.Measured == ChannelMeasure::Direction)
    {
        // 📝 The unperturbed tangent-space direction, which is the orientation itself. A direction channel
        //    defaulted to a repeated scalar is a direction pointing along the diagonal of tangent space, and
        //    every surface that never wrote one would then be lit as though it were creased.
        Resolved.Component[Index][0] = 0.0;
        Resolved.Component[Index][1] = 0.0;
        Resolved.Component[Index][2] = 1.0;

        return;
    }

    const double Magnitude = Held.Source == ChannelSource::Constant ? Held.ConstantScalar : Held.DefaultScalar;

    Resolved.Component[Index][0] = Magnitude;
    Resolved.Component[Index][1] = Magnitude;
    Resolved.Component[Index][2] = Magnitude;
}

// 📝 Whether a channel is tangent-space and therefore withheld where the basis is absent — `18` §1.1.
constexpr bool ChannelReadsBasis(ChannelSubject Channel)
{
    return Channel == ChannelSubject::SurfaceOrientation
        || Channel == ChannelSubject::AnisotropyDirection
        || Channel == ChannelSubject::ClearCoatOrientation;
}

}   // namespace

Deliver<ResolvedChannelSet> ReflectanceIntegrator::ResolveChannels(
    const MaterialSpecification&          Declared,
    const AnalyticProjection&             Resolving,
    const SurfaceLayerSequence&           Content,
    const std::vector<ChannelPlacement>&  Placements,
    const ReconstructedSurface&           Reconstructed,
    double                                Tolerance) const
{
    if (!Reconstructed.Reconstructed)
    {
        return Deliver<ResolvedChannelSet>::Refuse(
            { RefusalReason::ContentUnsupported, "nothing was reconstructed at that pixel" });
    }

    ResolvedChannelSet Resolved;

    const ReflectanceSelection Selected = Declared.Reflectance();

    constexpr std::size_t ChannelSpan = static_cast<std::size_t>(ChannelSubject::ChannelCount);

    bool ResolutionOwed = false;

    for (std::size_t Index = 0u; Index < ChannelSpan; ++Index)
    {
        const ChannelSubject        Channel = static_cast<ChannelSubject>(Index);
        const ChannelSpecification& Held    = Declared.Channel(Channel);

        DeclareDefault(Resolved, Channel, Held);

        // 🔴 `18` §9's fifth gate: each selection declares its channels and unread channels are **not sampled**.
        //    The mask is consulted before the sample rather than after it, so an unread channel costs nothing
        //    instead of costing a sample that is then discarded.
        if (!ChannelConsumed(Selected, Channel))
            continue;

        if (ChannelReadsBasis(Channel) && !Reconstructed.BasisDeclared)
            continue;

        if (!Declared.ChannelSampled(Channel))
            continue;

        ResolutionOwed = true;
    }

    if (!ResolutionOwed)
        return Deliver<ResolvedChannelSet>::Result(Resolved);

    // 🚧 Resolved through `70`'s host path rather than read from `20`'s promoted tile, because the device
    //    residency is unbuilt. `18` §8's rule is unamended by that: the resolution happens **once** per pixel and
    //    not once per channel, so no dispatch walks the layer sequence twenty times — and `82` §5's preview takes
    //    this same routine, which is what `00` §11 gates the two against.
    const Deliver<ResolvedSample> Sampled = Resolving.ResolveAt(Content,
                                                                Placements,
                                                                Reconstructed.DomainX,
                                                                Reconstructed.DomainY,
                                                                Tolerance,
                                                                ResolvedComponentLimit);

    if (!Sampled.Resolved)
        return Deliver<ResolvedChannelSet>::Refuse(Sampled.Error);

    const ResolvedSample& Current = Sampled.Resolve();

    if (!Current.SampleResolved)
        return Deliver<ResolvedChannelSet>::Result(Resolved);

    for (const ChannelPlacement& Placing : Placements)
    {
        if (Placing.Channel == ChannelSubject::ChannelCount)
            continue;

        if (!ChannelConsumed(Selected, Placing.Channel) || !Declared.ChannelSampled(Placing.Channel))
            continue;

        if (ChannelReadsBasis(Placing.Channel) && !Reconstructed.BasisDeclared)
            continue;

        if (Placing.ComponentIndex + Placing.ComponentSpan > ResolvedComponentLimit)
            continue;

        const std::size_t Index = ChannelIndex(Placing.Channel);

        for (std::uint32_t Component = 0u; Component < Placing.ComponentSpan && Component < 3u; ++Component)
            Resolved.Component[Index][Component] = Current.Component[Placing.ComponentIndex + Component];

        // 📝 A one-component placement carrying a scalar fills all three, so a reader that takes the triple form
        //    of a scalar channel reads the scalar rather than two zeros beside it.
        if (Placing.ComponentSpan == 1u)
        {
            Resolved.Component[Index][1] = Resolved.Component[Index][0];
            Resolved.Component[Index][2] = Resolved.Component[Index][0];
        }

        Resolved.SampledMask |= 1u << static_cast<std::uint32_t>(Placing.Channel);
    }

    return Deliver<ResolvedChannelSet>::Result(Resolved);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     THE DIRECT TERM
//------------------------------------------------------------------------------------------------------------------------

namespace
{

// 📐 The perturbed orientation, or the interpolated one where the basis is absent. `18` §1.1 withholds the
//    channel there, so the perturbation is the unperturbed direction and this reduces to the identity.
SpatialSpan Perturbed(const ResolvedChannelSet& Resolved, const ReconstructedSurface& Reconstructed)
{
    const SpatialSpan Oriented = Spanned(Reconstructed.Orientation);

    if (!Reconstructed.BasisDeclared || !ChannelSampledIn(Resolved, ChannelSubject::SurfaceOrientation))
        return Oriented;

    const SpatialSpan Tangential = Spanned(Reconstructed.Basis.Tangent);
    const SpatialSpan Bitangent  = Weighted(Perpendicular(Oriented, Tangential),
                                            static_cast<double>(Reconstructed.Basis.HandednessSignum));

    const SpatialSpan Perturbing = TripleOf(Resolved, ChannelSubject::SurfaceOrientation);

    const SpatialSpan Composed = Accumulated(Accumulated(Weighted(Tangential, Perturbing.CoordinateX),
                                                         Weighted(Bitangent,  Perturbing.CoordinateY)),
                                             Weighted(Oriented, Perturbing.CoordinateZ));

    const SpatialSpan Unitary = Unitised(Composed);

    return Agreement(Unitary, Unitary) > 0.0 ? Unitary : Oriented;
}

// 📐 The normal-incidence reflectance per component. A dielectric's is derived from channel 4 by the standard
//    quadratic, and a conductor's is its own albedo; metallic interpolates between them rather than selecting,
//    because an artist texturing a rusted edge textures the interpolant and not a decision.
SpatialSpan NormalIncidence(const ResolvedChannelSet& Resolved)
{
    const SpatialSpan Albedo   = TripleOf(Resolved, ChannelSubject::AlbedoColour);
    const double      Metallic = Bounded(ScalarOf(Resolved, ChannelSubject::Metallic), 0.0, 1.0);
    const double      Declared = Bounded(ScalarOf(Resolved, ChannelSubject::NormalIncidenceReflectance), 0.0, 1.0);

    const double Dielectric = 0.16 * Declared * Declared;

    return Spanned(Dielectric * (1.0 - Metallic) + Albedo.CoordinateX * Metallic,
                   Dielectric * (1.0 - Metallic) + Albedo.CoordinateY * Metallic,
                   Dielectric * (1.0 - Metallic) + Albedo.CoordinateZ * Metallic);
}

// 📐 The distribution parameter widened by the emission shape's own solid extent — `18` §4 integrates over it and
//    `44` §3 guarantees it is never zero. A source of no width is a highlight of no width, which is a single
//    aliased pixel that flickers as the camera moves; widening the lobe by the subtended half-angle is the
//    cheapest arrangement that keeps the highlight the size the artist declared.
double WidenedParameter(double Roughness, double SolidExtent)
{
    const double Parameter = Roughness * Roughness;

    if (!(SolidExtent > 0.0))
        return Parameter;

    // 📐 A cone of half-angle θ subtends about π sin²θ, so the sine of the half-angle is the square root of the
    //    extent over π. Half of it is the widening, which keeps the total energy in the lobe rather than adding to it.
    const double SubtendedSine = std::sqrt(Bounded(SolidExtent / Pi, 0.0, 1.0));

    return Bounded(Parameter + SubtendedSine * 0.5, Parameter, 1.0);
}

}   // namespace

DirectContribution ReflectanceIntegrator::IntegrateDirect(ReflectanceSelection        Selected,
                                                          const ResolvedChannelSet&   Resolved,
                                                          const ReconstructedSurface& Reconstructed,
                                                          const IncidenceProjection&  Incidence,
                                                          const ColourSpecification&  Radiance,
                                                          double                      Visibility,
                                                          double                      ViewX,
                                                          double                      ViewY,
                                                          double                      ViewZ) const
{
    DirectContribution Contribution;
    Contribution.Visibility = Visibility;

    // 📝 The two unlit selections integrate no direct term at all — `18` §3. `EmissiveOnly` emits and is resolved
    //    in the ambient recording; `Unlit` writes its albedo and is resolved there too.
    if (Selected == ReflectanceSelection::EmissiveOnly || Selected == ReflectanceSelection::Unlit)
        return Contribution;

    if (!Reconstructed.Reconstructed || !(Incidence.Attenuation > 0.0))
        return Contribution;

    const SpatialSpan Oriented = Perturbed(Resolved, Reconstructed);
    const SpatialSpan Incident = Unitised(Spanned(Incidence.DirectionX, Incidence.DirectionY, Incidence.DirectionZ));
    const SpatialSpan View     = Unitised(Spanned(ViewX, ViewY, ViewZ));

    const double IncidentCosine = Agreement(Oriented, Incident);
    const double ViewCosine     = Agreement(Oriented, View);

    // 📝 A surface turned away from the illuminant contributes nothing, and one turned away from the camera is
    //    not being shaded at all. Both are returned as zero rather than as a small magnitude, because a lobe
    //    evaluated at a negative cosine is a lobe evaluated outside its own domain.
    if (IncidentCosine <= 0.0 || ViewCosine <= 0.0)
        return Contribution;

    const SpatialSpan Half     = Unitised(Accumulated(Incident, View));
    const double      HalfCosine = Bounded(Agreement(Oriented, Half), 0.0, 1.0);
    const double      ViewHalf   = Bounded(Agreement(View, Half), 0.0, 1.0);
    const double      IncidentView = Agreement(Incident, View);

    const double Roughness = Bounded(ScalarOf(Resolved, ChannelSubject::Roughness), 0.0, 1.0);
    const double Parameter = WidenedParameter(Roughness, Incidence.SolidExtent);
    const double Metallic  = Bounded(ScalarOf(Resolved, ChannelSubject::Metallic), 0.0, 1.0);

    const SpatialSpan Albedo   = TripleOf(Resolved, ChannelSubject::AlbedoColour);
    const SpatialSpan Incident0 = NormalIncidence(Resolved);

    double SplitSumScale       = 0.0;
    double SingleScatterAlbedo = 0.0;
    double CharlieAlbedo       = 0.0;
    SampleDirectionalAlbedo(ViewCosine, Roughness, SplitSumScale, SingleScatterAlbedo, CharlieAlbedo);

    const double Visibility_ = ProjectVisibilitySmith(Parameter, ViewCosine, IncidentCosine);

    // 📐 The distribution is the selection's own. `18` §3's eight selections compose from these terms and never
    //    reimplement one — the anisotropic form is written apart from the isotropic one in `Shared/` precisely
    //    because the isotropic denominator collapses, and reaching for the wider form here would spend two
    //    divisions per pixel for the seven selections that are isotropic.
    double Distribution = 0.0;

    if (Selected == ReflectanceSelection::Anisotropic)
    {
        const double Anisotropy = Bounded(ScalarOf(Resolved, ChannelSubject::Anisotropy), -1.0, 1.0);

        const double RoughnessX  = Bounded(Parameter * (1.0 + Anisotropy), 0.0, 1.0);
        const double RoughnessY = Bounded(Parameter * (1.0 - Anisotropy), 0.0, 1.0);

        SpatialSpan Tangential = Reconstructed.BasisDeclared
                               ? Spanned(Reconstructed.Basis.Tangent)
                               : Unitised(Perpendicular(Oriented, View));

        if (Reconstructed.BasisDeclared && ChannelSampledIn(Resolved, ChannelSubject::AnisotropyDirection))
        {
            const SpatialSpan Declared_  = TripleOf(Resolved, ChannelSubject::AnisotropyDirection);
            const SpatialSpan Bitangent  = Weighted(Perpendicular(Oriented, Tangential),
                                                    static_cast<double>(Reconstructed.Basis.HandednessSignum));

            Tangential = Unitised(Accumulated(Weighted(Tangential, Declared_.CoordinateX),
                                              Weighted(Bitangent,  Declared_.CoordinateY)));
        }

        const SpatialSpan Bitangent = Perpendicular(Oriented, Tangential);

        Distribution = ProjectDistributionAnisotropic(RoughnessX,
                                                      RoughnessY,
                                                      Agreement(Half, Tangential),
                                                      Agreement(Half, Bitangent),
                                                      HalfCosine);
    }
    else
    {
        Distribution = ProjectDistributionGGX(Parameter, HalfCosine);
    }

    const double DiffuseLobe = ProjectDiffuseSingleScatter(Roughness, ViewCosine, IncidentCosine, IncidentView);

    double DiffuseComponent[3]  = { 0.0, 0.0, 0.0 };
    double SpecularComponent[3] = { 0.0, 0.0, 0.0 };

    const double AlbedoComponent[3]   = { Albedo.CoordinateX,    Albedo.CoordinateY,    Albedo.CoordinateZ    };
    const double IncidenceComponent[3] = { Incident0.CoordinateX, Incident0.CoordinateY, Incident0.CoordinateZ };

    if (Selected == ReflectanceSelection::Cloth)
    {
        const SpatialSpan Sheen          = TripleOf(Resolved, ChannelSubject::SheenColour);
        const double      SheenRoughness = Bounded(ScalarOf(Resolved, ChannelSubject::SheenRoughness), 0.0, 1.0);

        const double SheenDistribution = ProjectSheenDistributionCharlie(SheenRoughness, HalfCosine);
        const double SheenVisibility   = ProjectSheenVisibility(ViewCosine, IncidentCosine);

        const double SheenComponent[3] = { Sheen.CoordinateX, Sheen.CoordinateY, Sheen.CoordinateZ };

        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
        {
            // 🔴 No multi-scatter compensation on the fibre lobe. `18` §9 applies it wherever GGX is, and Charlie
            //    is not GGX — compensating it would add energy the lobe never lost, and a velvet would brighten
            //    toward grazing rather than merely retro-reflecting.
            SpecularComponent[Component] = SheenDistribution * SheenVisibility * SheenComponent[Component];
            DiffuseComponent[Component]  = AlbedoComponent[Component] * DiffuseLobe;
        }
    }
    else
    {
        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
        {
            const double Fresnel      = ProjectFresnelSchlick(IncidenceComponent[Component], ViewHalf);
            const double Compensation = ProjectMultiScatterCompensation(IncidenceComponent[Component],
                                                                        SingleScatterAlbedo);

            SpecularComponent[Component] = Distribution * Visibility_ * Fresnel * Compensation;

            // 🔴 A conductor carries no diffuse lobe, which is what the metallic interpolant means at unity. The
            //    diffuse albedo is therefore scaled down as the specular reflectance is scaled up, so the two
            //    together never exceed the energy the surface received.
            const double DiffuseAlbedo = AlbedoComponent[Component] * (1.0 - Metallic);

            DiffuseComponent[Component] = DiffuseAlbedo * DiffuseLobe
                                        + ProjectDiffuseMultiScatter(DiffuseAlbedo, SingleScatterAlbedo);
        }
    }

    if (Selected == ReflectanceSelection::ClearCoated)
    {
        const double Coat          = Bounded(ScalarOf(Resolved, ChannelSubject::ClearCoat), 0.0, 1.0);
        const double CoatRoughness = Bounded(ScalarOf(Resolved, ChannelSubject::ClearCoatRoughness), 0.0, 1.0);

        SpatialSpan CoatOriented = Oriented;

        if (Reconstructed.BasisDeclared && ChannelSampledIn(Resolved, ChannelSubject::ClearCoatOrientation))
        {
            const SpatialSpan Tangential = Spanned(Reconstructed.Basis.Tangent);
            const SpatialSpan Bitangent  = Weighted(Perpendicular(Oriented, Tangential),
                                                    static_cast<double>(Reconstructed.Basis.HandednessSignum));
            const SpatialSpan Declared_  = TripleOf(Resolved, ChannelSubject::ClearCoatOrientation);

            const SpatialSpan Composed = Accumulated(Accumulated(Weighted(Tangential, Declared_.CoordinateX),
                                                                 Weighted(Bitangent,  Declared_.CoordinateY)),
                                                     Weighted(Oriented, Declared_.CoordinateZ));

            const SpatialSpan Unitary = Unitised(Composed);

            if (Agreement(Unitary, Unitary) > 0.0)
                CoatOriented = Unitary;
        }

        const double CoatIncident = Bounded(Agreement(CoatOriented, Incident), 0.0, 1.0);
        const double CoatView     = Bounded(Agreement(CoatOriented, View), 0.0, 1.0);
        const double CoatHalf     = Bounded(Agreement(CoatOriented, Half), 0.0, 1.0);

        if (CoatIncident > 0.0 && CoatView > 0.0)
        {
            // 📐 The coat is a dielectric of fixed normal-incidence reflectance — a lacquer's, which is what a
            //    clear coat is. Exposing it as a channel would be a second reflectance the artist has to keep
            //    consistent with the coat they already declared.
            const double CoatParameter    = CoatRoughness * CoatRoughness;
            const double CoatDistribution = ProjectDistributionGGX(CoatParameter, CoatHalf);
            const double CoatVisibility   = ProjectVisibilitySmith(CoatParameter, CoatView, CoatIncident);
            const double CoatFresnel      = ProjectFresnelSchlick(0.04, ViewHalf) * Coat;

            // 🔴 `18` §9: compensation is applied **wherever GGX is**, and the coat is GGX. Sampled at the coat's
            //    own roughness rather than reusing the base lobe's sample — the two are separate lobes over
            //    separate roughnesses, and a coat compensated at the roughness beneath it is compensated for
            //    energy a different surface lost. A rough coat left uncompensated darkens exactly as rough metal
            //    does, and the artist corrects it by raising a coat magnitude that was already correct.
            double CoatSplitSumScale       = 0.0;
            double CoatSingleScatterAlbedo = 0.0;
            double CoatCharlieAlbedo       = 0.0;
            SampleDirectionalAlbedo(CoatView,
                                    CoatRoughness,
                                    CoatSplitSumScale,
                                    CoatSingleScatterAlbedo,
                                    CoatCharlieAlbedo);

            const double CoatCompensation = ProjectMultiScatterCompensation(0.04, CoatSingleScatterAlbedo);

            for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            {
                // 🔴 The layer beneath is attenuated by what the coat reflected, twice — once on the way in and
                //    once on the way out. Attenuating once makes a coated surface brighter than an uncoated one,
                //    which is the opposite of what a coat does.
                const double Transmitted = (1.0 - CoatFresnel) * (1.0 - CoatFresnel);

                DiffuseComponent[Component]  *= Transmitted;
                SpecularComponent[Component] *= Transmitted;
                SpecularComponent[Component] += CoatDistribution * CoatVisibility * CoatFresnel * CoatCompensation;
            }
        }
    }

    if (Selected == ReflectanceSelection::Subsurface)
    {
        const SpatialSpan Subsurface = TripleOf(Resolved, ChannelSubject::SubsurfaceColour);
        const double      Thickness  = ScalarOf(Resolved, ChannelSubject::SubsurfaceThickness);

        // 📐 A wrapped diffuse term: light entering the far side emerges attenuated by the path it took, and the
        //    path length is channel 17. It is not a subsurface solve and does not claim to be — `00` §5.1 accounts
        //    for the absence, and what is here is the wrap the artist can actually author against.
        const double Wrapped     = Bounded(-Agreement(Oriented, Incident), 0.0, 1.0);
        const double Attenuation = Thickness > 0.0 ? std::exp(-Thickness) : 1.0;

        const double SubsurfaceComponent[3] = { Subsurface.CoordinateX, Subsurface.CoordinateY, Subsurface.CoordinateZ };

        for (std::uint32_t Component = 0u; Component < 3u; ++Component)
            DiffuseComponent[Component] += SubsurfaceComponent[Component] * Wrapped * Attenuation / Pi;
    }

    const double RadianceComponent[3] = { Radiance.RedCoordinate, Radiance.GreenCoordinate, Radiance.BlueCoordinate };

    const double Weight = IncidentCosine * Incidence.Attenuation * Bounded(Visibility, 0.0, 1.0);

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        Contribution.Component[Component] =
            (DiffuseComponent[Component] + SpecularComponent[Component]) * RadianceComponent[Component] * Weight;
    }

    return Contribution;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    THE AMBIENT TERM
//------------------------------------------------------------------------------------------------------------------------

Deliver<AmbientContribution> ReflectanceIntegrator::IntegrateAmbient(
    ReflectanceSelection        Selected,
    const ResolvedChannelSet&   Resolved,
    const ReconstructedSurface& Reconstructed,
    const AtmosphereIntegrator& Atmosphere,
    double                      ResolvedOcclusion,
    double                      ViewX,
    double                      ViewY,
    double                      ViewZ) const
{
    if (!Reconstructed.Reconstructed)
    {
        return Deliver<AmbientContribution>::Refuse(
            { RefusalReason::ContentUnsupported, "nothing was reconstructed at that pixel" });
    }

    AmbientContribution Contribution;

    const SpatialSpan Emission = TripleOf(Resolved, ChannelSubject::Emission);

    // 🔴 Emission is written **before** the attenuation is resolved and is never multiplied by it. A surface that
    //    emits does not emit less for standing in a corner, and an occlusion applied to it is the one occlusion
    //    defect the artist cannot correct by adjusting occlusion.
    Contribution.EmissiveComponent[0] = Emission.CoordinateX;
    Contribution.EmissiveComponent[1] = Emission.CoordinateY;
    Contribution.EmissiveComponent[2] = Emission.CoordinateZ;

    if (Selected == ReflectanceSelection::EmissiveOnly)
        return Deliver<AmbientContribution>::Result(Contribution);

    const SpatialSpan Albedo = TripleOf(Resolved, ChannelSubject::AlbedoColour);

    if (Selected == ReflectanceSelection::Unlit)
    {
        // 📝 An unlit selection writes its albedo unattenuated. `18` §3 gives it channels 1 and 8 and nothing
        //    else, so there is no lobe to integrate and no occlusion that could apply to one.
        Contribution.DiffuseComponent[0] = Albedo.CoordinateX;
        Contribution.DiffuseComponent[1] = Albedo.CoordinateY;
        Contribution.DiffuseComponent[2] = Albedo.CoordinateZ;

        return Deliver<AmbientContribution>::Result(Contribution);
    }

    const SpatialSpan Oriented = Perturbed(Resolved, Reconstructed);
    const SpatialSpan View     = Unitised(Spanned(ViewX, ViewY, ViewZ));

    if (Agreement(View, View) <= 0.0)
    {
        return Deliver<AmbientContribution>::Refuse(
            { RefusalReason::ContentUnsupported, "the view direction has no length to reflect about" });
    }

    const double ViewCosine = Bounded(Agreement(Oriented, View), 0.0, 1.0);
    const double Roughness  = Bounded(ScalarOf(Resolved, ChannelSubject::Roughness), 0.0, 1.0);
    const double Metallic   = Bounded(ScalarOf(Resolved, ChannelSubject::Metallic), 0.0, 1.0);

    double SplitSumScale       = 0.0;
    double SingleScatterAlbedo = 0.0;
    double CharlieAlbedo       = 0.0;
    SampleDirectionalAlbedo(ViewCosine, Roughness, SplitSumScale, SingleScatterAlbedo, CharlieAlbedo);

    // 🔴 The diffuse ambient reads `28`'s **cosine-convolved** irradiance rather than integrating the hemisphere
    //    here — `18` §5 and `28` §5. The convolution is derived once when the sky-view surface rebuilds; a
    //    hemisphere integral evaluated per shaded pixel is the whole thing the precomputation exists to avoid,
    //    and it would then be evaluated at every pixel of every rotation rather than at every rebuild.
    double IrradianceRed   = 0.0;
    double IrradianceGreen = 0.0;
    double IrradianceBlue  = 0.0;

    Atmosphere.Irradiance().Evaluate(Oriented.CoordinateX,
                                     Oriented.CoordinateY,
                                     Oriented.CoordinateZ,
                                     IrradianceRed,
                                     IrradianceGreen,
                                     IrradianceBlue);

    // 📐 The reflection direction about the perturbed orientation. The specular ambient is sampled along it and
    //    the diffuse along the orientation, which is the split the two convolutions were derived for.
    const SpatialSpan Reflected = Unitised(Accumulated(Weighted(Oriented, 2.0 * Agreement(Oriented, View)),
                                                       Weighted(View, -1.0)));

    double SkyRed   = 0.0;
    double SkyGreen = 0.0;
    double SkyBlue  = 0.0;

    Discard(Atmosphere.SampleSkyView(Reflected.CoordinateX, Reflected.CoordinateY, Reflected.CoordinateZ,
                             SkyRed, SkyGreen, SkyBlue));

    const SpatialSpan Incident0 = NormalIncidence(Resolved);

    const double AlbedoComponent[3]    = { Albedo.CoordinateX,    Albedo.CoordinateY,    Albedo.CoordinateZ    };
    const double IncidenceComponent[3] = { Incident0.CoordinateX, Incident0.CoordinateY, Incident0.CoordinateZ };
    const double IrradianceComponent[3] = { IrradianceRed, IrradianceGreen, IrradianceBlue };
    const double SkyComponent[3]        = { SkyRed, SkyGreen, SkyBlue };

    // 📐 The split-sum bias is the single-scatter albedo less the scale, which is how the lookup stores them —
    //    `.y` carries the sum so that `ProjectMultiScatterCompensation` reads it directly, and the bias is
    //    recovered here rather than stored a second time.
    const double SplitSumBias = SingleScatterAlbedo - SplitSumScale;

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        const double DiffuseAlbedo = AlbedoComponent[Component] * (1.0 - Metallic);

        Contribution.DiffuseComponent[Component] = IrradianceComponent[Component] * DiffuseAlbedo;

        if (Selected == ReflectanceSelection::Cloth)
        {
            const SpatialSpan Sheen           = TripleOf(Resolved, ChannelSubject::SheenColour);
            const double      SheenComponent[3] = { Sheen.CoordinateX, Sheen.CoordinateY, Sheen.CoordinateZ };

            Contribution.SpecularComponent[Component] =
                SkyComponent[Component] * SheenComponent[Component] * CharlieAlbedo;
        }
        else
        {
            Contribution.SpecularComponent[Component] =
                SkyComponent[Component] * (IncidenceComponent[Component] * SplitSumScale + SplitSumBias);
        }
    }

    // 🔴 The authored channel 6 and `60`'s resolved term **multiply** — `18` §5 and `60` §2, stated from both
    //    sides. They describe different scales: channel 6 is detail the topology does not carry, and `60`
    //    resolves contact the topology does carry, so one superseding the other loses whichever it replaced.
    const double Authored = Bounded(ScalarOf(Resolved, ChannelSubject::AmbientOcclusion), 0.0, 1.0);

    Contribution.Attenuation = Authored * Bounded(ResolvedOcclusion, 0.0, 1.0);

    for (std::uint32_t Component = 0u; Component < 3u; ++Component)
    {
        Contribution.DiffuseComponent[Component]  *= Contribution.Attenuation;
        Contribution.SpecularComponent[Component] *= Contribution.Attenuation;
    }

    return Deliver<AmbientContribution>::Result(Contribution);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  THE UNOCCUPIED CLASS
//------------------------------------------------------------------------------------------------------------------------

Deliver<bool> ReflectanceIntegrator::IntegrateUnoccupied(const AtmosphereIntegrator& Atmosphere,
                                                         double ViewX, double ViewY, double ViewZ,
                                                         double& Red, double& Green, double& Blue) const
{
    Red   = 0.0;
    Green = 0.0;
    Blue  = 0.0;

    const SpatialSpan View = Unitised(Spanned(ViewX, ViewY, ViewZ));

    if (Agreement(View, View) <= 0.0)
    {
        return Deliver<bool>::Refuse(
            { RefusalReason::ContentUnsupported, "the view direction has no length to sample along" });
    }

    // 🔴 `18` §5.1: reconstructs **no attribute** and reads **no material**. It samples one source and writes it,
    //    and the source is the same pair §5 already declares — the sky where the atmosphere stands and the
    //    constant floor where it does not, which `28` resolves behind one sample rather than behind a branch here.
    //
    // ⚠️ Without this class nothing in the whole schedule writes the background: every other dispatch is per
    //    material over pixels that resolved to a surface, and an unoccupied pixel resolved to none. The image
    //    would carry a hole exactly where the sky belongs, filled with whatever the cycle slot held before.
    Discard(Atmosphere.SampleSkyView(View.CoordinateX, View.CoordinateY, View.CoordinateZ, Red, Green, Blue));

    return Deliver<bool>::Result(true);
}

}   // namespace Slate
