/*==========================================================================================================================================
                                                   REFERENCE RENDERER DEFINITION
==========================================================================================================================================*/

#include "ReferenceRenderer.h"
#include <stdexcept>

#include "DiscrepancySequence.h"
#include "LunarBounceReservoir.h"
#include "LunarDiscProjection.h"
#include "ReservoirSampling.h"
#include "SolarDiscFilter.h"
#include "StaticIndirectReservoir.h"
#include "VarianceFiltering.h"
#include "Engine/Foundation/DeterministicRandom.h"
#include "Engine/Foundation/Math.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Frontier
{

namespace
{

constexpr double Pi = 3.14159265358979323846;

SpatialVector Add(SpatialVector A, SpatialVector B) noexcept { return {A.X + B.X, A.Y + B.Y, A.Z + B.Z}; }
SpatialVector Sub(SpatialVector A, SpatialVector B) noexcept { return {A.X - B.X, A.Y - B.Y, A.Z - B.Z}; }
SpatialVector Mul(SpatialVector A, double S) noexcept { return {A.X * S, A.Y * S, A.Z * S}; }
double        Dot(SpatialVector A, SpatialVector B) noexcept { return A.X * B.X + A.Y * B.Y + A.Z * B.Z; }
double        Length(SpatialVector A) noexcept { return std::sqrt(Dot(A, A)); }
SpatialVector Cross(SpatialVector A, SpatialVector B) noexcept
{
    return {A.Y * B.Z - A.Z * B.Y, A.Z * B.X - A.X * B.Z, A.X * B.Y - A.Y * B.X};
}
SpatialVector Normalised(SpatialVector A) noexcept
{
    const double M = Length(A);
    return M > 0.0 ? Mul(A, 1.0 / M) : SpatialVector{};
}
double Luminance(const SpectrumRgb& C) noexcept { return 0.2126 * C.R + 0.7152 * C.G + 0.0722 * C.B; }
SpectrumRgb MulRgb(const SpectrumRgb& A, double S) noexcept { return {A.R * S, A.G * S, A.B * S}; }
SpectrumRgb AddRgb(const SpectrumRgb& A, const SpectrumRgb& B) noexcept { return {A.R + B.R, A.G + B.G, A.B + B.B}; }

bool SameAtmosphere(const AtmosphereParameters& A, const AtmosphereParameters& B) noexcept
{
    return A.GroundRadiusKm == B.GroundRadiusKm && A.TopRadiusKm == B.TopRadiusKm &&
        A.RayleighScattering.R == B.RayleighScattering.R && A.RayleighScattering.G == B.RayleighScattering.G &&
        A.RayleighScattering.B == B.RayleighScattering.B && A.RayleighScaleHeightKm == B.RayleighScaleHeightKm &&
        A.MieScattering == B.MieScattering && A.MieAbsorption == B.MieAbsorption &&
        A.MieScaleHeightKm == B.MieScaleHeightKm && A.MiePhaseG == B.MiePhaseG &&
        A.OzoneAbsorption.R == B.OzoneAbsorption.R && A.OzoneAbsorption.G == B.OzoneAbsorption.G &&
        A.OzoneAbsorption.B == B.OzoneAbsorption.B && A.OzonePeakKm == B.OzonePeakKm &&
        A.OzoneWidthKm == B.OzoneWidthKm && A.GroundAlbedo == B.GroundAlbedo;
}

double PlanetHorizonCos(const AtmosphereParameters& P, double AltitudeKm) noexcept
{
    const double Radius = P.GroundRadiusKm + std::max(0.0, AltitudeKm);
    if (Radius <= 0.0) return 0.0;
    const double Ratio = P.GroundRadiusKm / Radius;
    return -std::sqrt(std::max(0.0, 1.0 - Ratio * Ratio));
}

// The transmittance integrator stops at the ground, so explicitly reject a planet-blocked beam.
SpectrumRgb CelestialBeam(const ReferenceRenderSettings& Settings, SpatialVector Direction,
                          double AltitudeKm, SpectrumRgb TopIrradiance, bool Attenuate) noexcept
{
    Direction = Normalised(Direction);
    if (Length(Direction) == 0.0 || Direction.Z <= PlanetHorizonCos(Settings.Atmosphere, AltitudeKm)) return {};
    if (!Settings.AtmosphereEnabled || !Attenuate) return TopIrradiance;
    const auto T = TransmittanceToTop(Settings.Atmosphere,
        Settings.Atmosphere.GroundRadiusKm + std::max(0.0, AltitudeKm), Direction.Z, 96);
    return {TopIrradiance.R*T.R, TopIrradiance.G*T.G, TopIrradiance.B*T.B};
}

// B-V to approximate RGB chromaticity (Ballesteros temperature and a blackbody RGB fit).
SpectrumRgb StarColour(double Bv) noexcept
{
    Bv = std::clamp(Bv, -0.39, 2.0);
    const double T = 46.0 * (1.0 / (0.92*Bv + 1.7) + 1.0 / (0.92*Bv + 0.62));
    const double R = T <= 66.0 ? 255.0 : 329.698727446 * std::pow(T - 60.0, -0.1332047592);
    const double G = T <= 66.0 ? 99.4708025861 * std::log(T) - 161.1195681661
                               : 288.1221695283 * std::pow(T - 60.0, -0.0755148492);
    const double B = T >= 66.0 ? 255.0 : (T <= 19.0 ? 0.0 : 138.5177312231 * std::log(T - 10.0) - 305.0447927307);
    const SpectrumRgb C{std::clamp(R, 0.0, 255.0)/255.0, std::clamp(G, 0.0, 255.0)/255.0,
                        std::clamp(B, 0.0, 255.0)/255.0};
    const double L = Luminance(C);
    return L > 0.0 ? MulRgb(C, 1.0/L) : SpectrumRgb{1, 1, 1};
}

struct PreparedTri
{
    SpatialVector V[3];
    SpatialVector N[3];
    uint32_t      Mesh = 0;
};

// Unshadowed direct contribution of one light at a surface point (Lambert).
SpectrumRgb LightContribution(const ReferenceLight& Light, SpatialVector Point, SpatialVector Normal,
                              SpatialVector View, const ReferenceMaterial& Material,
                              SpatialVector& OutDirection, double& OutDistance) noexcept
{
    const auto        Arrival     = ReferenceLightArrival(Light, Point, OutDirection, OutDistance);
    const double NdotL = std::max(Dot(Normal, OutDirection), 0.0);
    const SpectrumRgb Brdf = EvaluateReferenceBrdf(Material, Normal, View, OutDirection);
    const double      Attenuation = Light.Kind == ReferenceLight::Kind::Directional
                                        ? 1.0
                                        : 1.0 / std::max(OutDistance * OutDistance, 1e-6);
    if (Light.Active && Light.Reach == 0 && Light.Kind != ReferenceLight::Kind::Spot && !Light.Photometry)
    {
        const double Scale = NdotL * Attenuation;
        return {Brdf.R * Light.Radiance.R * Scale, Brdf.G * Light.Radiance.G * Scale,
                Brdf.B * Light.Radiance.B * Scale};
    }
    return {Brdf.R * Arrival.R * NdotL, Brdf.G * Arrival.G * NdotL, Brdf.B * Arrival.B * NdotL};
}

} // namespace

SpectrumRgb ReferenceLightArrival(const ReferenceLight& Light, SpatialVector Point,
                                  SpatialVector& OutDirection, double& OutDistance) noexcept
{
    if (Light.Kind == ReferenceLight::Kind::Directional)
    {
        OutDirection = Light.Vector;
        OutDistance  = 1e30;
        return Light.Active ? Light.Radiance : SpectrumRgb{};
    }
    const SpatialVector Delta{Light.Vector.X - Point.X, Light.Vector.Y - Point.Y, Light.Vector.Z - Point.Z};
    OutDistance  = Length(Delta);
    OutDirection = OutDistance > 0 ? Mul(Delta, 1 / OutDistance) : SpatialVector{0, 0, 1};
    if (!Light.Active || (Light.Reach > 0 && OutDistance >= Light.Reach))
        return {};
    double Angular = 1;
    if (Light.Kind == ReferenceLight::Kind::Spot || Light.Photometry)
    {
        const auto   Forward = Normalised(Light.Direction);
        const auto   Toward  = Mul(OutDirection, -1);
        const double Cosine  = std::clamp(Dot(Forward, Toward), -1.0, 1.0);
        if (Light.Kind == ReferenceLight::Kind::Spot)
        {
            double T =
                std::clamp((Cosine - Light.OuterCosine) / (Light.InnerCosine - Light.OuterCosine), 0.0, 1.0);
            Angular = T * T * (3 - 2 * T);
        }
        if (Light.Photometry)
        {
            const auto   Right = Normalised(Cross(Light.Up, Forward));
            const auto   Up    = Cross(Forward, Right);
            const double θ     = std::acos(Cosine) * 180 / Pi;
            const double φ     = std::atan2(Dot(Toward, Right), Dot(Toward, Up)) * 180 / Pi;
            Angular *= Light.Photometry->Relative(θ, φ);
        }
    }
    double Window = 1;
    if (Light.Reach > 0)
    {
        double Q = OutDistance / Light.Reach;
        Window   = std::pow(1 - Q * Q * Q * Q, 2);
    }
    const double Scale = Angular * Window / std::max(OutDistance * OutDistance, 1e-6);
    return {Light.Radiance.R * Scale, Light.Radiance.G * Scale, Light.Radiance.B * Scale};
}

SpectrumRgb EvaluateReferenceBrdf(const ReferenceMaterial& Material, SpatialVector Normal, SpatialVector View,
                                  SpatialVector Light) noexcept
{
    Normal = Normalised(Normal);
    View   = Normalised(View);
    Light  = Normalised(Light);
    const double NdotV = std::max(Dot(Normal, View), 0.0);
    const double NdotL = std::max(Dot(Normal, Light), 0.0);
    if (NdotV <= 0.0 || NdotL <= 0.0)
    {
        return {};
    }

    const SpatialVector Half = Normalised(Add(View, Light));
    const double NdotH = std::clamp(Dot(Normal, Half), 0.0, 1.0);
    const double VdotH = std::clamp(Dot(View, Half), 0.0, 1.0);
    const double Roughness = std::clamp(Material.Roughness, 0.04, 1.0);
    const double Alpha = Roughness * Roughness;
    const double AlphaSquared = Alpha * Alpha;
    const double Denominator = NdotH * NdotH * (AlphaSquared - 1.0) + 1.0;
    const double Distribution = AlphaSquared / (Pi * Denominator * Denominator);
    const double VisibilityDenominator =
        NdotL * std::sqrt(NdotV * NdotV * (1.0 - AlphaSquared) + AlphaSquared) +
        NdotV * std::sqrt(NdotL * NdotL * (1.0 - AlphaSquared) + AlphaSquared);
    const double Visibility = 0.5 / std::max(VisibilityDenominator, 1e-12);

    const double Ior = std::max(Material.Ior, 1.0);
    const double IorRatio = (Ior - 1.0) / (Ior + 1.0);
    const double DielectricF0 = IorRatio * IorRatio * std::clamp(Material.SpecularLevel, 0.0, 1.0) * 2.0;
    const double Metallic = std::clamp(Material.Metallic, 0.0, 1.0);
    const SpectrumRgb F0{DielectricF0 * (1.0 - Metallic) + Material.Albedo.R * Metallic,
                         DielectricF0 * (1.0 - Metallic) + Material.Albedo.G * Metallic,
                         DielectricF0 * (1.0 - Metallic) + Material.Albedo.B * Metallic};
    const double Schlick = std::pow(1.0 - VdotH, 5.0);
    const SpectrumRgb Fresnel{F0.R + (1.0 - F0.R) * Schlick,
                              F0.G + (1.0 - F0.G) * Schlick,
                              F0.B + (1.0 - F0.B) * Schlick};
    const double DiffuseWeight = (1.0 - Metallic) / Pi;
    return {Distribution * Visibility * Fresnel.R + (1.0 - Fresnel.R) * Material.Albedo.R * DiffuseWeight,
            Distribution * Visibility * Fresnel.G + (1.0 - Fresnel.G) * Material.Albedo.G * DiffuseWeight,
            Distribution * Visibility * Fresnel.B + (1.0 - Fresnel.B) * Material.Albedo.B * DiffuseWeight};
}

namespace
{

// MIS environment integral mirroring IntegrateMaterialEnvironment for a single isotropic stratum:
// one cosine-hemisphere proposal + one GGX-half-vector proposal, eight samples each by default.
SpectrumRgb IntegrateReferenceEnvironment(const ReferenceMaterial& Material, SpatialVector Normal,
                                          SpatialVector View, const ReferenceRenderSettings& Settings,
                                          const TransmittanceLut& Lut, SpatialVector PlanetObserver,
                                          DeterministicRandom& Random, const ReferenceScene& Scene,
                                          SpatialVector SurfacePosition)
{
    if ((!Settings.AtmosphereEnabled && !Scene.TriangleDistance) || Settings.EnvironmentSamples == 0)
    {
        return {};
    }
    Normal = Normalised(Normal);
    View = Normalised(View);
    const SpatialVector Helper = std::abs(Normal.Z) < 0.999 ? SpatialVector{0,0,1} : SpatialVector{0,1,0};
    const SpatialVector Tangent = Normalised(Cross(Helper, Normal));
    const SpatialVector Bitangent = Cross(Normal, Tangent);
    const double Components = 2.0;
    const uint32_t Samples = Settings.EnvironmentSamples;
    const double Roughness = std::clamp(Material.Roughness, 0.04, 1.0);
    const double Alpha = Roughness * Roughness;
    SpectrumRgb Accumulated{};

    for (uint32_t Component = 0; Component < 2; ++Component)
    {
        for (uint32_t Sample = 0; Sample < Samples; ++Sample)
        {
            const double First = (static_cast<double>(Sample) + 0.5) / Samples;
            double Second = std::fmod(Sample * 0.61803398875 + Component * 0.371 + Random.NextUnit(), 1.0);
            if (Second < 0.0) Second += 1.0;
            const double Phi = 2.0 * Pi * Second;
            SpatialVector Light{};
            SpatialVector Half{};

            if (Component == 0)
            {
                const SpatialVector Local{std::sqrt(First) * std::cos(Phi),
                                          std::sqrt(First) * std::sin(Phi),
                                          std::sqrt(1.0 - First)};
                Light = Normalised(Add(Add(Mul(Tangent, Local.X), Mul(Bitangent, Local.Y)), Mul(Normal, Local.Z)));
            }
            else
            {
                const double Radius = std::sqrt(First / std::max(1.0 - First, 1e-12));
                const SpatialVector LocalHalf = Normalised({Alpha * std::cos(Phi) * Radius,
                                                            Alpha * std::sin(Phi) * Radius, 1.0});
                Half = Normalised(Add(Add(Mul(Tangent, LocalHalf.X), Mul(Bitangent, LocalHalf.Y)),
                                      Mul(Normal, LocalHalf.Z)));
                if (Dot(View, Half) <= 0.0)
                {
                    continue;
                }
                Light = Normalised(Sub(Mul(Half, 2.0 * Dot(View, Half)), View));
            }

            const double NdotL = std::max(Dot(Normal, Light), 0.0);
            const SpatialVector H = Normalised(Add(View, Light));
            const double NdotH = std::max(Dot(Normal, H), 0.0);
            const double VdotH = std::max(Dot(View, H), 1e-6);
            const double Dden = NdotH * NdotH * (Alpha * Alpha - 1.0) + 1.0;
            const double D = (Alpha * Alpha) / (Pi * Dden * Dden);
            double Density = NdotL / Pi + D * NdotH / (4.0 * VdotH);
            Density /= Components;
            if (Density <= 1e-12)
            {
                continue;
            }

            SpectrumRgb Environment = Settings.AtmosphereEnabled ? SingleScattering(Settings.Atmosphere, PlanetObserver, Light,
                                                        Settings.SunDirection, Settings.SunIrradiance, &Lut, 12) : SpectrumRgb{};
            if (Scene.TriangleDistance)
            {
                const auto Hit = Scene.TriangleDistance->Trace(Add(SurfacePosition, Mul(Normal, .002)), Light, 0.0, 200.0);
                if (Hit.Hit) Environment = Scene.TriangleRadiance[Hit.Facet];
            }
            const SpectrumRgb Brdf = EvaluateReferenceBrdf(Material, Normal, View, Light);
            const double Weight = NdotL / Density / (Components * Samples);
            Accumulated = AddRgb(Accumulated, {Brdf.R * Environment.R * Weight,
                                               Brdf.G * Environment.G * Weight,
                                               Brdf.B * Environment.B * Weight});
        }
    }
    return Accumulated;
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                       SCENE PREP
//------------------------------------------------------------------------------------------------------------------------

void ReferenceRenderer::SetScene(ReferenceScene Scene)
{
    if (Scene.TriangleDistance && Scene.TriangleRadiance.size() != Scene.TriangleDistance->FacetCount())
        throw std::runtime_error("Triangle distance and surface radiance count mismatch");
    for (const auto &Radiance : Scene.TriangleRadiance)
        if (!std::isfinite(Radiance.R) || !std::isfinite(Radiance.G) || !std::isfinite(Radiance.B) ||
            Radiance.R<0 || Radiance.G<0 || Radiance.B<0)
            throw std::runtime_error("Invalid triangle surface radiance");
    for (const auto& Light : Scene.Lights)
    {
        auto Finite = [](SpatialVector V) {
            return std::isfinite(V.X) && std::isfinite(V.Y) && std::isfinite(V.Z);
        };
        if ((Light.Kind != ReferenceLight::Kind::Directional && Light.Kind != ReferenceLight::Kind::Point &&
             Light.Kind != ReferenceLight::Kind::Spot) ||
            !Finite(Light.Vector) || !Finite(Light.Direction) || !Finite(Light.Up) ||
            !std::isfinite(Light.Reach) || Light.Reach < 0 || !std::isfinite(Light.Radiance.R) ||
            !std::isfinite(Light.Radiance.G) || !std::isfinite(Light.Radiance.B) || Light.Radiance.R < 0 ||
            Light.Radiance.G < 0 || Light.Radiance.B < 0)
            throw std::runtime_error("Invalid analytic light");
        if (Light.Kind == ReferenceLight::Kind::Spot || Light.Photometry)
        {
            if (!std::isfinite(Length(Light.Direction)) || Length(Light.Direction) < 1e-12 ||
                (Light.Photometry &&
                 (!std::isfinite(Length(Cross(Light.Up, Light.Direction))) ||
                  Length(Cross(Light.Up, Light.Direction)) < 1e-12 || !Light.Photometry->Loaded())))
                throw std::runtime_error("Invalid light orientation or empty IES distribution");
        }
        if (Light.Kind == ReferenceLight::Kind::Spot &&
            (!std::isfinite(Light.InnerCosine) || !std::isfinite(Light.OuterCosine) ||
             Light.InnerCosine > 1 || Light.OuterCosine < -1 || Light.InnerCosine <= Light.OuterCosine))
            throw std::runtime_error("Spot cone ordering rejected");
        if (Light.Kind == ReferenceLight::Kind::Directional &&
            (Length(Light.Vector) < 1e-12 || Light.Photometry))
            throw std::runtime_error("Invalid directional light");
    }
    Scene_ = std::move(Scene);
    Prepared_.clear();
    std::vector<std::array<SpatialVector, 3>> VisibilityFacets;
    for (const ReferenceMesh& Mesh : Scene_.Meshes)
    {
        PreparedMesh Prepared;
        Prepared.Material = Mesh.Material;
        Prepared.Emissive = Mesh.Emissive;
        Prepared.WorldGeometry.Indices = Mesh.Geometry.Indices;
        Prepared.WorldGeometry.Vertices.reserve(Mesh.Geometry.Vertices.size());
        for (const SpatialVector& V : Mesh.Geometry.Vertices)
        {
            Prepared.WorldGeometry.Vertices.push_back(Add(Mul(V, Mesh.Scale), Mesh.Translation));
        }
        // Area-weighted smooth normals.
        Prepared.Normals.assign(Prepared.WorldGeometry.Vertices.size(), SpatialVector{});
        const std::vector<uint32_t>& Indices = Prepared.WorldGeometry.Indices;
        for (size_t T = 0; T + 2 < Indices.size(); T += 3)
        {
            const SpatialVector A = Prepared.WorldGeometry.Vertices[Indices[T]];
            const SpatialVector B = Prepared.WorldGeometry.Vertices[Indices[T + 1]];
            const SpatialVector C = Prepared.WorldGeometry.Vertices[Indices[T + 2]];
            VisibilityFacets.push_back({A, B, C});
            const SpatialVector Face = Cross(Sub(B, A), Sub(C, A));
            for (uint32_t K = 0; K < 3; ++K)
            {
                Prepared.Normals[Indices[T + K]] = Add(Prepared.Normals[Indices[T + K]], Face);
            }
        }
        for (SpatialVector& N : Prepared.Normals)
        {
            N = Normalised(N);
        }
        (void)Prepared.Nesting.Build(Prepared.WorldGeometry, 64);
        Prepared_.push_back(std::move(Prepared));
    }
    Visibility_.Build(std::move(VisibilityFacets));
}

//------------------------------------------------------------------------------------------------------------------------
//                                                          RENDER
//------------------------------------------------------------------------------------------------------------------------

ReferenceImage ReferenceRenderer::Render(const CullCamera& Camera, const ReferenceRenderSettings& Settings)
{
    ReferenceImage Image;
    Image.Width  = Settings.Width;
    Image.Height = Settings.Height;
    const uint32_t W = Settings.Width, H = Settings.Height;
    const size_t   PixelCount = static_cast<size_t>(W) * H;
    Image.Pixels.assign(PixelCount, SpectrumRgb{});
    if (W == 0 || H == 0)
    {
        return Image;
    }

    if (Settings.StoreSurfaceSamples)
    {
        if (!Settings.ExhaustiveLights || Settings.AccumulationFrames != 1 || Settings.Denoise ||
            Settings.FogEnabled)
            throw std::runtime_error(
                "Surface samples require one exact unfiltered, unfogged lighting evaluation");
        Image.SurfaceSamples.resize(PixelCount);
    }
    const double AltitudeKm = std::max(Camera.Position.Z, 0.0) / 1000.0;
    const SpatialVector PlanetObserver{0.0, 0.0, Settings.Atmosphere.GroundRadiusKm + AltitudeKm};
    if (Settings.AtmosphereEnabled &&
        (!AtmosphereLutBaked_ || !SameAtmosphere(AtmosphereLutParameters_, Settings.Atmosphere)))
    {
        AtmosphereLutBaked_ = AtmosphereLut_.Bake(Settings.Atmosphere, 128, 32, 48);
        AtmosphereLutParameters_ = Settings.Atmosphere;
    }
    std::unique_ptr<LunarDiscProjection> LunarProjection;
    if (Settings.LunarDiscEnabled || Settings.LunarSurfaceLighting || Settings.LunarIndirectLighting)
        LunarProjection = std::make_unique<LunarDiscProjection>(Settings.Lunar, Settings.SunDirection,
                                                                Settings.SunIrradiance);
    const bool LunarValid = LunarProjection && LunarProjection->Valid();
    const SpatialVector MoonDirection = LunarValid ? LunarProjection->Direction() : SpatialVector{};
    const SpectrumRgb MoonTopIrradiance = LunarValid ? LunarProjection->Irradiance() : SpectrumRgb{};
    const SpectrumRgb MoonAtCamera = LunarValid
        ? CelestialBeam(Settings, MoonDirection, AltitudeKm, MoonTopIrradiance, true) : SpectrumRgb{};
    const double MoonLuminance = Luminance(MoonAtCamera);

    const double TanY = std::tan(Camera.FovYRadians * 0.5);
    const double TanX = TanY * Camera.Aspect;
    const double Projection = ClusterNesting::ProjectionFactor(static_cast<double>(H), Camera.FovYRadians);

    CameraFrustum Frustum;
    (void)Frustum.Build(Camera);

    // --- Geometry stage: cut + cull per mesh, gather visible triangles --------------------------------
    std::vector<PreparedTri> Visible;
    for (uint32_t MeshIndex = 0; MeshIndex < Prepared_.size(); ++MeshIndex)
    {
        PreparedMesh& Mesh = Prepared_[MeshIndex];
        Image.Stats.TrianglesTotal += Mesh.Nesting.TriangleTotal();
        std::vector<uint32_t> Cut;
        Mesh.Nesting.SelectCut(Camera.Position, Projection, Settings.CutErrorPx, Cut);
        std::vector<uint32_t> Culled;
        CullCut(Mesh.Nesting, Cut, Frustum, Camera, nullptr, Culled);
        Image.Stats.CutNodes += static_cast<uint32_t>(Cut.size());
        for (uint32_t NodeIndex : Culled)
        {
            // Collect the node's triangles (contiguous range by construction of the builder).
            const ClusterNode& Node = Mesh.Nesting.Nodes()[NodeIndex];
            Image.Stats.TrianglesAfterCut += Node.TriangleCount;
            std::vector<uint32_t> Stack{NodeIndex};
            while (!Stack.empty())
            {
                const uint32_t Current = Stack.back();
                Stack.pop_back();
                const ClusterNode& N = Mesh.Nesting.Nodes()[Current];
                if (N.ChildCount > 0)
                {
                    for (uint32_t S = 0; S < N.ChildCount; ++S)
                    {
                        Stack.push_back(Mesh.Nesting.ChildIndices()[N.FirstChild + S]);
                    }
                    continue;
                }
                for (uint32_t Slot = 0; Slot < N.TriangleCount; ++Slot)
                {
                    const uint32_t TriIndex = Mesh.Nesting.TriangleOrder()[N.FirstTriangle + Slot];
                    PreparedTri Tri;
                    Tri.Mesh = MeshIndex;
                    for (uint32_t K = 0; K < 3; ++K)
                    {
                        const uint32_t Vertex = Mesh.WorldGeometry.Indices[TriIndex * 3 + K];
                        Tri.V[K] = Mesh.WorldGeometry.Vertices[Vertex];
                        Tri.N[K] = Mesh.Normals[Vertex];
                    }
                    Visible.push_back(Tri);
                }
            }
        }
    }
    Image.Stats.TrianglesVisible = static_cast<uint32_t>(Visible.size());

    // --- Raster stage: G-buffer ------------------------------------------------------------------------
    std::vector<double>        ViewDepth(PixelCount, std::numeric_limits<double>::infinity());
    std::vector<SpatialVector> WorldPos(PixelCount), WorldNormal(PixelCount);
    std::vector<ReferenceMaterial> MaterialBuffer(PixelCount);
    std::vector<SpectrumRgb>   EmissiveBuffer(PixelCount);
    std::vector<uint8_t>       HitMask(PixelCount, 0);
    std::vector<uint32_t> SurfaceIdentity(Settings.StoreSurfaceSamples ? PixelCount : 0);

    const auto ToView = [&](SpatialVector World) {
        const SpatialVector Rel = Sub(World, Camera.Position);
        return SpatialVector{Dot(Rel, Camera.Right), Dot(Rel, Camera.Up), Dot(Rel, Camera.Forward)};
    };

    for (const PreparedTri& Tri : Visible)
    {
        // View-space polygon with attributes; clip against the near plane only
        // (screen bounds are handled by the rasteriser's bounding box clamp).
        struct ClipVertex { SpatialVector View, World, Normal; };
        std::vector<ClipVertex> Polygon;
        for (uint32_t K = 0; K < 3; ++K)
        {
            Polygon.push_back({ToView(Tri.V[K]), Tri.V[K], Tri.N[K]});
        }
        std::vector<ClipVertex> Clipped;
        for (size_t I = 0; I < Polygon.size(); ++I)
        {
            const ClipVertex& Current = Polygon[I];
            const ClipVertex& Previous = Polygon[(I + Polygon.size() - 1) % Polygon.size()];
            const double DC = Current.View.Z - Camera.NearPlane;
            const double DP = Previous.View.Z - Camera.NearPlane;
            if (DC >= 0.0)
            {
                if (DP < 0.0)
                {
                    const double F = DP / (DP - DC);
                    Clipped.push_back({Add(Previous.View, Mul(Sub(Current.View, Previous.View), F)),
                                       Add(Previous.World, Mul(Sub(Current.World, Previous.World), F)),
                                       Add(Previous.Normal, Mul(Sub(Current.Normal, Previous.Normal), F))});
                }
                Clipped.push_back(Current);
            }
            else if (DP >= 0.0)
            {
                const double F = DP / (DP - DC);
                Clipped.push_back({Add(Previous.View, Mul(Sub(Current.View, Previous.View), F)),
                                   Add(Previous.World, Mul(Sub(Current.World, Previous.World), F)),
                                   Add(Previous.Normal, Mul(Sub(Current.Normal, Previous.Normal), F))});
            }
        }
        if (Clipped.size() < 3)
        {
            continue;
        }
        // Project + fan-triangulate.
        struct ScreenVertex { double X, Y, InvZ, ViewZ; SpatialVector World, Normal; };
        std::vector<ScreenVertex> Screen;
        for (const ClipVertex& V : Clipped)
        {
            const double NdcX = V.View.X / (V.View.Z * TanX);
            const double NdcY = V.View.Y / (V.View.Z * TanY);
            Screen.push_back({(NdcX * 0.5 + 0.5) * W, (1.0 - (NdcY * 0.5 + 0.5)) * H, 1.0 / V.View.Z, V.View.Z,
                              V.World, V.Normal});
        }
        const ReferenceMaterial& MeshMaterial = Prepared_[Tri.Mesh].Material;
        const SpectrumRgb& MeshEmissive = Prepared_[Tri.Mesh].Emissive;
        for (size_t Fan = 1; Fan + 1 < Screen.size(); ++Fan)
        {
            const ScreenVertex* V[3] = {&Screen[0], &Screen[Fan], &Screen[Fan + 1]};
            const double Area = (V[1]->X - V[0]->X) * (V[2]->Y - V[0]->Y) - (V[1]->Y - V[0]->Y) * (V[2]->X - V[0]->X);
            if (std::abs(Area) < 1e-12)
            {
                continue;
            }
            const int32_t MinX = static_cast<int32_t>(std::max(0.0, std::floor(std::min({V[0]->X, V[1]->X, V[2]->X}))));
            const int32_t MaxX = static_cast<int32_t>(std::min(static_cast<double>(W - 1), std::ceil(std::max({V[0]->X, V[1]->X, V[2]->X}))));
            const int32_t MinY = static_cast<int32_t>(std::max(0.0, std::floor(std::min({V[0]->Y, V[1]->Y, V[2]->Y}))));
            const int32_t MaxY = static_cast<int32_t>(std::min(static_cast<double>(H - 1), std::ceil(std::max({V[0]->Y, V[1]->Y, V[2]->Y}))));
            for (int32_t Y = MinY; Y <= MaxY; ++Y)
            {
                for (int32_t X = MinX; X <= MaxX; ++X)
                {
                    const double PX = X + 0.5, PY = Y + 0.5;
                    const double W0 = ((V[1]->X - PX) * (V[2]->Y - PY) - (V[1]->Y - PY) * (V[2]->X - PX)) / Area;
                    const double W1 = ((V[2]->X - PX) * (V[0]->Y - PY) - (V[2]->Y - PY) * (V[0]->X - PX)) / Area;
                    const double W2 = 1.0 - W0 - W1;
                    if (W0 < 0.0 || W1 < 0.0 || W2 < 0.0)
                    {
                        continue;
                    }
                    const double InvZ = W0 * V[0]->InvZ + W1 * V[1]->InvZ + W2 * V[2]->InvZ;
                    if (InvZ <= 0.0)
                    {
                        continue;
                    }
                    const double ViewZ = 1.0 / InvZ;
                    const size_t Index = static_cast<size_t>(Y) * W + static_cast<size_t>(X);
                    if (ViewZ >= ViewDepth[Index])
                    {
                        continue;
                    }
                    const auto Interp = [&](const SpatialVector& A0, const SpatialVector& A1, const SpatialVector& A2) {
                        return Mul(Add(Add(Mul(A0, W0 * V[0]->InvZ), Mul(A1, W1 * V[1]->InvZ)),
                                       Mul(A2, W2 * V[2]->InvZ)), ViewZ);
                    };
                    ViewDepth[Index]     = ViewZ;
                    WorldPos[Index]      = Interp(V[0]->World, V[1]->World, V[2]->World);
                    WorldNormal[Index]   = Normalised(Interp(V[0]->Normal, V[1]->Normal, V[2]->Normal));
                    MaterialBuffer[Index] = MeshMaterial;
                    EmissiveBuffer[Index] = MeshEmissive;
                    HitMask[Index]       = 1;
                    if (Settings.StoreSurfaceSamples)
                        SurfaceIdentity[Index] = Tri.Mesh + 1;
                }
            }
        }
    }
    for (size_t I = 0; I < PixelCount; ++I)
    {
        Image.Stats.SurfacePixels += HitMask[I] ? 1u : 0u;
        // Double-sided shading: face normals toward the viewer.
        if (HitMask[I] && Dot(WorldNormal[I], Sub(WorldPos[I], Camera.Position)) > 0.0)
        {
            WorldNormal[I] = Mul(WorldNormal[I], -1.0);
        }
    }

    // Environment MIS is evaluated once per surface pixel and reused across direct-light frames.
    std::vector<SpectrumRgb> EnvironmentBuffer(PixelCount, SpectrumRgb{});
    if ((Settings.AtmosphereEnabled || Scene_.TriangleDistance) && Settings.EnvironmentSamples > 0)
    {
        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P]) continue;
            DeterministicRandom Random(Settings.Seed ^ (0xD1B54A32D192ED03ull * (P + 1)), 0xE17B1B3ull);
            const SpatialVector View = Normalised(Sub(Camera.Position, WorldPos[P]));
            EnvironmentBuffer[P] = IntegrateReferenceEnvironment(MaterialBuffer[P], WorldNormal[P], View,
                Settings, AtmosphereLut_, PlanetObserver, Random, Scene_, WorldPos[P]);
            Image.Stats.MeanEnvironmentIbl += Luminance(EnvironmentBuffer[P]);
        }
        Image.Stats.MeanEnvironmentIbl /= std::max<uint32_t>(Image.Stats.SurfacePixels, 1u);
    }

    // Celestial beams are separate from the sampled analytic-light list and remain in the spatial result.
    const bool CelestialDirectEnabled = Settings.SolarSurfaceLighting || (Settings.LunarSurfaceLighting && LunarValid);
    std::vector<SpectrumRgb> CelestialDirect(CelestialDirectEnabled ? PixelCount : 0);
    if (CelestialDirectEnabled)
    {
        const SpatialVector SolarDirection = Normalised(Settings.SunDirection);
        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P]) continue;
            const SpatialVector View = Normalised(Sub(Camera.Position, WorldPos[P]));
            auto AddBeam = [&](SpatialVector Direction, SpectrumRgb Irradiance) {
                if (Luminance(Irradiance) <= 0.0) return;
                ReferenceLight Light;
                Light.Kind = ReferenceLight::Kind::Directional;
                Light.Vector = Direction;
                Light.Radiance = Irradiance;
                SpatialVector Dir; double Dist;
                const auto F = LightContribution(Light, WorldPos[P], WorldNormal[P], View, MaterialBuffer[P], Dir, Dist);
                if (Luminance(F) > 0.0 &&
                    !Visibility_.Occluded(Add(WorldPos[P], Mul(WorldNormal[P], 0.002)), Dir, 0.0, Dist - 0.004))
                    CelestialDirect[P] = AddRgb(CelestialDirect[P], F);
            };
            const double SurfaceAltitudeKm = std::max(WorldPos[P].Z, 0.0) / 1000.0;
            if (Settings.SolarSurfaceLighting)
                AddBeam(SolarDirection, CelestialBeam(Settings, SolarDirection, SurfaceAltitudeKm,
                                                       Settings.SunIrradiance, Settings.AtmosphericSunlight));
            if (Settings.LunarSurfaceLighting && LunarValid)
                AddBeam(MoonDirection, CelestialBeam(Settings, MoonDirection, SurfaceAltitudeKm,
                                                    MoonTopIrradiance, true));
        }
    }

    LightInfluenceSpace   LightGrid;
    std::vector<uint32_t> AllLights(Scene_.Lights.size());
    std::iota(AllLights.begin(), AllLights.end(), 0u);
    if (Settings.ClusteredLights)
    {
        std::vector<LightInfluence> Influences;
        for (const auto& Light : Scene_.Lights)
            Influences.push_back(
                {Light.Vector, Light.Kind == ReferenceLight::Kind::Directional ? 0 : Light.Reach});
        LightGrid.Build(Influences, Settings.LightGridMinimum, Settings.LightGridMaximum,
                        Settings.LightGridResolution, Settings.LightCellCapacity);
    }
    // Light proposal pdf (power proxy; correctness holds for any positive proposal).
    std::vector<double> LightPdf(Scene_.Lights.size(), 0.0);
    {
        double Total = 0.0;
        for (size_t I = 0; I < Scene_.Lights.size(); ++I)
        {
            LightPdf[I] = std::max(Luminance(Scene_.Lights[I].Radiance), 1e-6);
            Total += LightPdf[I];
        }
        if (Total > 0.0)
        {
            if (!std::isfinite(Total))
            {
                const double Peak = *std::max_element(LightPdf.begin(), LightPdf.end());
                Total             = 0;
                for (auto& Power : LightPdf)
                {
                    Power /= Peak;
                    Total += Power;
                }
            }
            for (double& P : LightPdf)
                P /= Total;
        }
    }
    const auto PickLight = [&](double U) {
        double Acc = 0.0;
        for (uint32_t I = 0; I < LightPdf.size(); ++I)
        {
            Acc += LightPdf[I];
            if (U < Acc) return I;
        }
        return static_cast<uint32_t>(LightPdf.size()) - 1;
    };

    // --- Lighting accumulation frames (direct RIS + reuse, DF/card indirect) ----------------------------
    std::vector<SpectrumRgb> MeanColour(PixelCount, SpectrumRgb{});
    std::vector<double>      MeanLuminanceBuffer(PixelCount, 0.0);
    VarianceHistory          History;
    History.Resize(W, H);
    std::vector<Reservoir>   PreviousReservoirs(PixelCount);
    const bool IndirectReservoirEnabled = Settings.IndirectTemporalReuse && Settings.IndirectSamples > 0 &&
                                          !Scene_.Distance.Primitives().empty();
    std::vector<StaticIndirectReservoir> PreviousIndirect(IndirectReservoirEnabled ? PixelCount : 0);
    const bool LunarBounceEnabled = Settings.LunarIndirectLighting && LunarValid && MoonLuminance > 0.0 &&
                                     Settings.IndirectSamples > 0 && !Scene_.Distance.Primitives().empty();
    const bool LunarTemporalEnabled = LunarBounceEnabled && Settings.IndirectTemporalReuse;
    std::vector<LunarBounceReservoir> PreviousLunar(LunarTemporalEnabled ? PixelCount : 0);

    const uint32_t Frames = std::max(Settings.AccumulationFrames, 1u);
    std::vector<double> FrameLuminance(PixelCount, 0.0);
    for (uint32_t Frame = 0; Frame < Frames; ++Frame)
    {
        std::vector<Reservoir> Reservoirs(PixelCount);
        std::vector<StaticIndirectReservoir> IndirectReservoirs(IndirectReservoirEnabled ? PixelCount : 0);
        std::vector<LunarBounceReservoir> LunarReservoirs(LunarTemporalEnabled ? PixelCount : 0);
        std::vector<SpectrumRgb> FrameColour(PixelCount, SpectrumRgb{});
        std::vector<SpectrumRgb> DirectColour(PixelCount, SpectrumRgb{});
        std::vector<SpectrumRgb> ResidualColour(PixelCount, SpectrumRgb{});   // indirect + emissive

        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P])
            {
                continue;
            }
            DeterministicRandom Random(Settings.Seed ^ (0x9E3779B9ull * (P + 1)), 0xF00Dull + Frame);
            const SpatialVector& Point  = WorldPos[P];
            const SpatialVector& Normal = WorldNormal[P];
            const ReferenceMaterial& Material = MaterialBuffer[P];
            const SpatialVector View = Normalised(Sub(Camera.Position, Point));

            bool        LightFallback = false;
            const auto& LocalLights =
                Settings.ClusteredLights ? LightGrid.Candidates(Point, &LightFallback) : AllLights;
            Image.Stats.LightListVisits += LocalLights.size();
            Image.Stats.LightFallbackQueries += LightFallback ? 1 : 0;
            double LocalMass = 0;
            if (Settings.ClusteredLights && !Settings.ExhaustiveLights)
                for (auto Index : LocalLights)
                    LocalMass += LightPdf[Index];
            auto PickLocal = [&](double U) {
                double Sum = 0;
                for (auto Index : LocalLights)
                {
                    Sum += LightPdf[Index];
                    if (U * LocalMass < Sum)
                        return Index;
                }
                return LocalLights.empty() ? 0u : LocalLights.back();
            };
            // Initial candidates (RIS), temporal combine, finalise.
            Reservoir& R = Reservoirs[P];
            for (uint32_t C = 0;
                 C < Settings.RisCandidates && !Scene_.Lights.empty() && !Settings.ExhaustiveLights; ++C)
            {
                const double   Draw  = Random.NextUnit();
                const uint32_t Light = Settings.ClusteredLights ? PickLocal(Draw) : PickLight(Draw);
                SpatialVector Dir; double Dist;
                const SpectrumRgb Unshadowed = LightContribution(Scene_.Lights[Light], Point, Normal, View, Material, Dir, Dist);
                const double Target = Luminance(Unshadowed);
                const double Proposal = Settings.ClusteredLights
                                            ? (LocalMass > 0 ? LightPdf[Light] / LocalMass : 0)
                                            : LightPdf[Light];
                R.Update({Light, 0.0, 0.0}, Proposal > 0.0 ? Target / Proposal : 0.0, Random.NextUnit());
            }
            if (Frame > 0 && !Settings.ExhaustiveLights && !Scene_.Lights.empty())
            {
                Reservoir Previous = PreviousReservoirs[P];
                Previous.ClampSampleCount(20u * std::max(Settings.RisCandidates, 1u));
                SpatialVector Dir; double Dist;
                const double TargetHere = Luminance(LightContribution(
                    Scene_.Lights[Previous.Sample().Candidate], Point, Normal, View, Material, Dir, Dist));
                R.Combine(Previous, TargetHere, 1.0, Random.NextUnit());
            }
            if (!Scene_.Lights.empty())
            {
                SpatialVector Dir; double Dist;
                R.Finalize(Luminance(LightContribution(Scene_.Lights[R.Sample().Candidate], Point, Normal, View, Material, Dir, Dist)));
            }

            SpectrumRgb Direct{};
            if (!Scene_.Lights.empty() && R.HasSample())
            {
                const uint32_t Light = R.Sample().Candidate;
                SpatialVector Dir; double Dist;
                SpectrumRgb F = LightContribution(Scene_.Lights[Light], Point, Normal, View, Material, Dir, Dist);
                const SpatialVector Origin = Add(Point, Mul(Normal, 2e-3));
                if (Visibility_.Occluded(Origin, Dir, 0.0, Dist - 4e-3))
                {
                    F = {};
                }
                Direct = {F.R * R.Weight(), F.G * R.Weight(), F.B * R.Weight()};
            }

            if (Settings.ExhaustiveLights)
            {
                Direct = {};
                for (auto Index : LocalLights)
                {
                    SpatialVector Dir;
                    double        Dist;
                    auto          F =
                        LightContribution(Scene_.Lights[Index], Point, Normal, View, Material, Dir, Dist);
                    if ((F.R != 0 || F.G != 0 || F.B != 0) &&
                        !Visibility_.Occluded(Add(Point, Mul(Normal, .002)), Dir, 0, Dist - .004))
                        Direct = AddRgb(Direct, F);
                }
            }
            if (CelestialDirectEnabled) Direct = AddRgb(Direct, CelestialDirect[P]);
            // Indirect: distance-field gather + optional card texel.
            SpectrumRgb Indirect = EnvironmentBuffer[P];
            if (Settings.IndirectSamples > 0 && !Scene_.Distance.Primitives().empty())
            {
                double E = 0.0;
                if (IndirectReservoirEnabled)
                {
                    StaticIndirectReservoir& IndirectReservoir = IndirectReservoirs[P];
                    IndirectReservoir.Stream(Scene_.Distance, Point, Normal, Settings.IndirectSamples, Random);
                    if (Frame > 0)
                    {
                        const uint32_t MaximumHistory = Settings.IndirectSamples > UINT32_MAX / 20u
                            ? UINT32_MAX : 20u * Settings.IndirectSamples;
                        IndirectReservoir.Reuse(PreviousIndirect[P], Scene_.Distance, MaximumHistory, Random);
                    }
                    IndirectReservoir.Finalize(Scene_.Distance);
                    E = IndirectReservoir.Irradiance(Scene_.Distance);
                }
                else
                    E = Scene_.Distance.IndirectIrradiance(Point, Normal, Settings.IndirectSamples, Random);
                Indirect = AddRgb(Indirect, {Material.Albedo.R * (1.0 - Material.Metallic) * E / Pi,
                                              Material.Albedo.G * (1.0 - Material.Metallic) * E / Pi,
                                              Material.Albedo.B * (1.0 - Material.Metallic) * E / Pi});
            }
            if (LunarBounceEnabled)
            {
                // Separate random stream keeps the pre-existing emitter estimator unchanged.
                DeterministicRandom LunarRandom(Settings.Seed ^ (0xA24BAED4963EE407ull * (P + 1)), 0xC3A5ull + Frame);
                LunarBounceReservoir Fresh;
                LunarBounceReservoir& Bounce = LunarTemporalEnabled ? LunarReservoirs[P] : Fresh;
                Bounce.Stream(Scene_.Distance, Point, Normal, MoonDirection, MoonLuminance,
                              Settings.IndirectSamples, LunarRandom);
                if (LunarTemporalEnabled && Frame > 0)
                {
                    const uint32_t MaximumHistory = Settings.IndirectSamples > UINT32_MAX / 20u
                        ? UINT32_MAX : 20u * Settings.IndirectSamples;
                    Bounce.Reuse(PreviousLunar[P], Scene_.Distance, MaximumHistory, LunarRandom);
                }
                Bounce.Finalize();
                const double E = Bounce.Irradiance();
                const double Scale = (1.0 - Material.Metallic) * E / (Pi * MoonLuminance);
                Indirect = AddRgb(Indirect, {Material.Albedo.R * MoonAtCamera.R * Scale,
                                             Material.Albedo.G * MoonAtCamera.G * Scale,
                                             Material.Albedo.B * MoonAtCamera.B * Scale});
            }
            if (Scene_.Cards != nullptr && Scene_.Cards->TexelCount() > 0)
            {
                double BestD2 = 2.25;   // within 1.5 m
                double BestB = 0.0;
                for (uint32_t T = 0; T < Scene_.Cards->TexelCount(); ++T)
                {
                    const SpatialVector Delta = Sub(Scene_.Cards->TexelCentre(T), Point);
                    const double D2 = Dot(Delta, Delta);
                    if (D2 < BestD2) { BestD2 = D2; BestB = Scene_.Cards->Radiosity(T); }
                }
                Indirect = AddRgb(Indirect, {Material.Albedo.R * (1.0 - Material.Metallic) * BestB / Pi,
                                     Material.Albedo.G * (1.0 - Material.Metallic) * BestB / Pi,
                                     Material.Albedo.B * (1.0 - Material.Metallic) * BestB / Pi});
            }

            if (Settings.StoreSurfaceSamples)
                Image.SurfaceSamples[P] = {Point, Normal, View, Material, Direct, true, SurfaceIdentity[P]};
            DirectColour[P]   = Direct;
            ResidualColour[P] = AddRgb(Indirect, EmissiveBuffer[P]);
            FrameColour[P]    = AddRgb(Direct, ResidualColour[P]);
        }

        // History must contain only same-pixel temporal reservoirs. Feeding the spatial result back
        // would hide the original source domains needed by the selected-sample MIS normalization.
        PreviousReservoirs = Reservoirs;

        // Spatial direct-light reuse over the two adjacent source domains. Include zero-weight
        // reservoirs in the domain tally; their random draw does not determine proposal support.
        if (Settings.SpatialReuse && !Settings.ExhaustiveLights && !Scene_.Lights.empty())
        {
            const std::vector<Reservoir>& Source = PreviousReservoirs;
            for (uint32_t Y = 0; Y < H; ++Y)
            {
                for (uint32_t X = 0; X < W; ++X)
                {
                    const size_t P = static_cast<size_t>(Y) * W + X;
                    if (!HitMask[P])
                    {
                        continue;
                    }
                    DeterministicRandom Random(Settings.Seed ^ (0x85EBCA77ull * (P + 1)), 0x57A7ull + Frame);
                    Reservoir& R = Reservoirs[P];
                    const size_t Neighbours[2] = {P + (X + 1 < W ? 1 : 0), P + (Y + 1 < H ? W : 0)};
                    for (size_t NP : Neighbours)
                    {
                        if (NP == P || !HitMask[NP])
                        {
                            continue;
                        }
                        double TargetHere = 0.0;
                        if (Source[NP].HasSample())
                        {
                            SpatialVector Dir; double Dist;
                            TargetHere = Luminance(LightContribution(
                                Scene_.Lights[Source[NP].Sample().Candidate], WorldPos[P], WorldNormal[P],
                                Normalised(Sub(Camera.Position, WorldPos[P])), MaterialBuffer[P], Dir, Dist));
                        }
                        R.Combine(Source[NP], TargetHere, 1.0, Random.NextUnit());
                    }
                    if (R.HasSample())
                    {
                        const uint32_t Selected = R.Sample().Candidate;
                        // For a candidate y, Z(y) sums M over domains whose proposal can produce y
                        // and whose selection target at y is positive. This is deterministic from
                        // the domains, including those whose actual stored sample had zero weight.
                        const auto Supports = [&](size_t Q) {
                            if (Source[Q].SampleCount() == 0) return false;
                            if (Settings.ClusteredLights)
                            {
                                const auto& Candidates = LightGrid.Candidates(WorldPos[Q]);
                                if (std::find(Candidates.begin(), Candidates.end(), Selected) == Candidates.end())
                                    return false;
                            }
                            SpatialVector Dir; double Dist;
                            return Luminance(LightContribution(
                                Scene_.Lights[Selected], WorldPos[Q], WorldNormal[Q],
                                Normalised(Sub(Camera.Position, WorldPos[Q])), MaterialBuffer[Q], Dir, Dist)) > 0.0;
                        };
                        uint64_t Z = Supports(P) ? Source[P].SampleCount() : 0;
                        for (size_t NP : Neighbours)
                            if (NP != P && HitMask[NP] && Supports(NP)) Z += Source[NP].SampleCount();

                        SpatialVector Dir; double Dist;
                        SpectrumRgb F = LightContribution(Scene_.Lights[Selected], WorldPos[P], WorldNormal[P],
                            Normalised(Sub(Camera.Position, WorldPos[P])), MaterialBuffer[P], Dir, Dist);
                        R.FinalizeWithNormalization(Luminance(F), Z);
                        const SpatialVector Origin = Add(WorldPos[P], Mul(WorldNormal[P], 2e-3));
                        if (Visibility_.Occluded(Origin, Dir, 0.0, Dist - 4e-3))
                        {
                            F = {};
                        }
                        // Replace this pixel's direct term; indirect + emissive stay as computed.
                        DirectColour[P] = {F.R * R.Weight(), F.G * R.Weight(), F.B * R.Weight()};
                        if (CelestialDirectEnabled) DirectColour[P] = AddRgb(DirectColour[P], CelestialDirect[P]);
                        FrameColour[P]  = AddRgb(DirectColour[P], ResidualColour[P]);
                    }
                }
            }
        }

        if (IndirectReservoirEnabled) PreviousIndirect = std::move(IndirectReservoirs);
        if (LunarTemporalEnabled) PreviousLunar = std::move(LunarReservoirs);

        for (size_t P = 0; P < PixelCount; ++P)
        {
            MeanColour[P] = AddRgb(MeanColour[P], MulRgb(FrameColour[P], 1.0 / Frames));
            FrameLuminance[P] = Luminance(FrameColour[P]);
        }
        if (Settings.Denoise)
        {
            (void)History.Integrate(FrameLuminance.data(), W, H, Frame == 0 ? 1.0 : 0.35);
        }
    }

    // --- Sky (computed once) ------------------------------------------------------------------------------
    std::vector<SpectrumRgb> Sky(PixelCount, SpectrumRgb{});
    std::vector<double> CloudTransmission(Settings.SiderealStarsEnabled && Settings.CloudsEnabled ? PixelCount : 0, 1.0);
    const SpatialVector SunDirection = Normalised(Settings.SunDirection);
    const double SunDepth = Dot(SunDirection, Camera.Forward);
    double SunU = 0.0, SunV = 0.0, SolarScaleX = 0.0, SolarScaleY = 0.0, SolarShear = 0.0;
    double SolarPixelWidth = 0.0, SolarPixelHeight = 0.0, SunVisibility = 0.0;
    const double PlanetRadius = Settings.Atmosphere.GroundRadiusKm;
    const double ViewingRadius = PlanetRadius + AltitudeKm;
    const double PlanetEdgeCos = ViewingRadius > 0.0
        ? -std::sqrt(std::max(0.0, 1.0 - (PlanetRadius/ViewingRadius)*(PlanetRadius/ViewingRadius))) : 0.0;
    // TransmittanceToTop integrates only as far as the planet, so it must not make a fully hidden Sun visible.
    // Partial clipping/refraction at the planet edge remains outside this approximation.
    const bool PlanetBlocksSun = std::asin(std::clamp(SunDirection.Z, -1.0, 1.0)) +
                                 std::max(0.0, Settings.SunDisc.AngularRadiusRad) <= std::asin(PlanetEdgeCos);
    SpectrumRgb SunAtCamera{};
    if (Settings.AtmosphereEnabled && !PlanetBlocksSun && SunDepth > 1e-6 && TanY > 0.0 && TanX > 0.0)
    {
        SunU = Dot(SunDirection, Camera.Right) / SunDepth;
        SunV = Dot(SunDirection, Camera.Up) / SunDepth;
        // Local tangent metric; the small pixel shear is ignored by the rectangular PSF integral.
        const SpatialVector Right = Normalised(Sub(Camera.Right, Mul(SunDirection, Dot(Camera.Right, SunDirection))));
        const SpatialVector Up = Normalised(Sub(Sub(Camera.Up, Mul(SunDirection, Dot(Camera.Up, SunDirection))),
                                                  Mul(Right, Dot(Camera.Up, Right))));
        SolarScaleX = SunDepth * Dot(Camera.Right, Right);
        SolarScaleY = SunDepth * Dot(Camera.Up, Up);
        SolarShear = SunDepth * Dot(Camera.Up, Right);
        SolarPixelWidth = 2.0 * TanX * SolarScaleX / W;
        SolarPixelHeight = 2.0 * TanY * SolarScaleY / H;
        const SpectrumRgb T = AtmosphereLut_.Sample(Settings.Atmosphere, Settings.Atmosphere.GroundRadiusKm + AltitudeKm,
                                                     std::clamp(SunDirection.Z, -1.0, 1.0));
        SunAtCamera = {Settings.SunIrradiance.R*T.R, Settings.SunIrradiance.G*T.G, Settings.SunIrradiance.B*T.B};
        if (Settings.LensFlaresEnabled && SolarPixelWidth > 0.0 && SolarPixelHeight > 0.0)
        {
            // Visibility is a coarse screen-space disc sample; this is not a subpixel geometry or optical-occlusion solve.
            int Samples = 0, Visible = 0;
            for (int Y = -2; Y <= 2; ++Y) for (int X = -2; X <= 2; ++X)
            {
                if (X*X+Y*Y > 4) continue;
                const double U = SunU + 0.5 * X * Settings.SunDisc.AngularRadiusRad / SolarScaleX;
                const double V = SunV + 0.5 * Y * Settings.SunDisc.AngularRadiusRad / SolarScaleY;
                const int PX = static_cast<int>(std::floor((U/TanX + 1.0)*0.5*W));
                const int PY = static_cast<int>(std::floor((1.0 - V/TanY)*0.5*H));
                ++Samples;
                if (PX >= 0 && PY >= 0 && PX < static_cast<int>(W) && PY < static_cast<int>(H) &&
                    !HitMask[static_cast<size_t>(PY)*W+PX]) ++Visible;
            }
            SunVisibility = Samples ? static_cast<double>(Visible) / Samples : 0.0;
            if (SunVisibility > 0.0 && Settings.CloudsEnabled)
                SunVisibility *= MarchClouds(Settings.Clouds, Camera.Position, SunDirection, SunDirection,
                    MulRgb(Settings.SunIrradiance, Settings.CloudSunScale), 30000.0, 40, 4).Transmittance;
        }
    }
    SunDiscProfile PixelProfile = Settings.SunDisc;
    if (SolarPixelWidth > 0.0 && SolarPixelHeight > 0.0)
        PixelProfile.EdgeBlurRad = std::max(PixelProfile.EdgeBlurRad, 0.6 * std::sqrt(SolarPixelWidth*SolarPixelHeight));
    const SolarDiscPixelFilter SolarFilter(PixelProfile);

    const double MoonDepth = Dot(MoonDirection, Camera.Forward);
    const SpectrumRgb MoonTransmission = LunarValid
        ? CelestialBeam(Settings, MoonDirection, AltitudeKm, {1, 1, 1}, true) : SpectrumRgb{};
    double MoonU = 0, MoonV = 0, MoonScaleX = 0, MoonScaleY = 0, MoonShear = 0;
    double MoonPixelWidth = 0, MoonPixelHeight = 0;
    SpatialVector MoonRight{}, MoonUp{};
    const bool MoonInSky = Settings.LunarDiscEnabled && LunarValid && MoonDepth > 1e-6 &&
                           Luminance(MoonTransmission) > 0.0 && TanY > 0.0 && TanX > 0.0;
    if (MoonInSky)
    {
        MoonU = Dot(MoonDirection, Camera.Right) / MoonDepth;
        MoonV = Dot(MoonDirection, Camera.Up) / MoonDepth;
        MoonRight = Normalised(Sub(Camera.Right, Mul(MoonDirection, Dot(Camera.Right, MoonDirection))));
        MoonUp = Normalised(Sub(Sub(Camera.Up, Mul(MoonDirection, Dot(Camera.Up, MoonDirection))),
                                Mul(MoonRight, Dot(Camera.Up, MoonRight))));
        MoonScaleX = MoonDepth * Dot(Camera.Right, MoonRight);
        MoonScaleY = MoonDepth * Dot(Camera.Up, MoonUp);
        MoonShear = MoonDepth * Dot(Camera.Up, MoonRight);
        MoonPixelWidth = 2.0 * TanX * MoonScaleX / W;
        MoonPixelHeight = 2.0 * TanY * MoonScaleY / H;
    }
    if (Settings.AtmosphereEnabled || MoonInSky ||
        (Settings.CloudsEnabled && Settings.SiderealStarsEnabled && Settings.SiderealStars))
    {
        for (uint32_t Y = 0; Y < H; ++Y)
        {
            for (uint32_t X = 0; X < W; ++X)
            {
                const size_t P = static_cast<size_t>(Y) * W + X;
                if (HitMask[P])
                {
                    continue;
                }
                const double NdcX = 2.0 * (X + 0.5) / W - 1.0;
                const double NdcY = 1.0 - 2.0 * (Y + 0.5) / H;
                const SpatialVector Dir = Normalised(Add(Add(Camera.Forward, Mul(Camera.Right, NdcX * TanX)),
                                                         Mul(Camera.Up, NdcY * TanY)));
                SpectrumRgb Value = Settings.AtmosphereEnabled
                    ? SingleScattering(Settings.Atmosphere, PlanetObserver, Dir, Settings.SunDirection,
                                       Settings.SunIrradiance, &AtmosphereLut_, 24) : SpectrumRgb{};
                SpectrumRgb Solar{};
                if (Settings.SunDiscEnabled && SunDepth > 1e-6 && SolarPixelWidth > 0.0 && SolarPixelHeight > 0.0)
                {
                    const double Du = NdcX*TanX - SunU, Dv = NdcY*TanY - SunV;
                    const double Dx = Du*SolarScaleX + Dv*SolarShear, Dy = Dv*SolarScaleY;
                    Solar = SolarFilter.Evaluate(Dx, Dy, SolarPixelWidth, SolarPixelHeight, SunAtCamera);
                }
                LunarPixel Lunar;
                if (MoonInSky && MoonPixelWidth > 0.0 && MoonPixelHeight > 0.0)
                {
                    const double Du = NdcX*TanX - MoonU, Dv = NdcY*TanY - MoonV;
                    const double Dx = Du*MoonScaleX + Dv*MoonShear, Dy = Dv*MoonScaleY;
                    const double Reach = std::sin(Settings.Lunar.AngularRadiusRad) +
                                         4.0 * std::max(MoonPixelWidth, MoonPixelHeight);
                    if (std::abs(Dx) <= Reach && std::abs(Dy) <= Reach)
                        Lunar = LunarProjection->Evaluate(Dx, Dy, MoonPixelWidth, MoonPixelHeight, MoonRight, MoonUp);
                }
                // The atmosphere is in front; only the celestial Sun and stars are masked by the Moon.
                Value = AddRgb(Value, MulRgb(Solar, 1.0 - Lunar.Coverage));
                Value = AddRgb(Value, {Lunar.Radiance.R*MoonTransmission.R, Lunar.Radiance.G*MoonTransmission.G,
                                       Lunar.Radiance.B*MoonTransmission.B});
                if (Settings.CloudsEnabled && Dir.Z > -0.05)
                {
                    const CloudMarchResult CM = MarchClouds(Settings.Clouds, Camera.Position, Dir,
                                                            Settings.SunDirection,
                                                            MulRgb(Settings.SunIrradiance, Settings.CloudSunScale),
                                                            30000.0, 40, 4);
                    Value = {Value.R * CM.Transmittance + CM.Radiance.R, Value.G * CM.Transmittance + CM.Radiance.G,
                             Value.B * CM.Transmittance + CM.Radiance.B};
                    if (!CloudTransmission.empty()) CloudTransmission[P] = CM.Transmittance;
                }
                Sky[P] = Value;
            }
        }
    }

    // Celestial stars remain in the linear HDR background in both day and night. Their magnitude
    // sets integrated flux relative to the Sun; daylight scattering can overwhelm that contrast.
    if (Settings.SiderealStarsEnabled && Settings.SiderealStars && TanX > 0.0 && TanY > 0.0)
    {
        constexpr double SunVisualMagnitude = -26.74;
        constexpr double SigmaPixels = 0.65;
        const double StarGain = std::isfinite(Settings.StarBrightnessScale)
            ? std::max(0.0, Settings.StarBrightnessScale) : 0.0;
        const double SolarLum = std::max(0.0, Luminance(Settings.SunIrradiance)) * StarGain;
        const double Root2Sigma = std::sqrt(2.0) * SigmaPixels;
        const double NdcWidth = 2.0 * TanX / W, NdcHeight = 2.0 * TanY / H;
        for (const StarRecord& Star : *Settings.SiderealStars)
        {
            if (!std::isfinite(Star.Direction.X) || !std::isfinite(Star.Direction.Y) ||
                !std::isfinite(Star.Direction.Z) || !std::isfinite(Star.Magnitude) ||
                !std::isfinite(Star.ColourIndex) || Star.Magnitude < -35.0 || Star.Magnitude > 40.0) continue;
            const SpatialVector Direction = Normalised(Star.Direction);
            const double Depth = Dot(Direction, Camera.Forward);
            if (Depth <= 1e-6 || Direction.Z <= PlanetHorizonCos(Settings.Atmosphere, AltitudeKm)) continue;
            // Occlude the point source geometrically before applying the camera PSF, even for a subpixel Moon.
            if (MoonInSky && Dot(Direction, MoonDirection) >= std::cos(Settings.Lunar.AngularRadiusRad)) continue;
            if (Visibility_.Occluded(Camera.Position, Direction, 0.0, 1e30)) continue;
            const double U = Dot(Direction, Camera.Right) / Depth, V = Dot(Direction, Camera.Up) / Depth;
            const double CentreX = (U/TanX + 1.0) * 0.5 * W - 0.5;
            const double CentreY = (1.0 - V/TanY) * 0.5 * H - 0.5;
            if (CentreX < -5.0 || CentreY < -5.0 || CentreX > W + 5.0 || CentreY > H + 5.0) continue;
            const auto T = CelestialBeam(Settings, Direction, AltitudeKm, {1, 1, 1}, true);
            const auto Colour = StarColour(Star.ColourIndex);
            const double Flux = SolarLum * std::pow(10.0, -0.4 * (Star.Magnitude - SunVisualMagnitude));
            const int X0 = std::max(0, static_cast<int>(std::floor(CentreX)) - 4);
            const int X1 = std::min(static_cast<int>(W) - 1, static_cast<int>(std::floor(CentreX)) + 4);
            const int Y0 = std::max(0, static_cast<int>(std::floor(CentreY)) - 4);
            const int Y1 = std::min(static_cast<int>(H) - 1, static_cast<int>(std::floor(CentreY)) + 4);
            for (int Y = Y0; Y <= Y1; ++Y)
                for (int X = X0; X <= X1; ++X)
                {
                    const size_t P = static_cast<size_t>(Y) * W + static_cast<uint32_t>(X);
                    if (HitMask[P]) continue;
                    const double Wx = 0.5 * (std::erf((X + 0.5 - CentreX)/Root2Sigma) -
                                             std::erf((X - 0.5 - CentreX)/Root2Sigma));
                    const double Wy = 0.5 * (std::erf((Y + 0.5 - CentreY)/Root2Sigma) -
                                             std::erf((Y - 0.5 - CentreY)/Root2Sigma));
                    const double Px = (2.0*(X + 0.5)/W - 1.0)*TanX;
                    const double Py = (1.0 - 2.0*(Y + 0.5)/H)*TanY;
                    const double Omega = NdcWidth * NdcHeight / std::pow(1.0 + Px*Px + Py*Py, 1.5);
                    const double Visible = CloudTransmission.empty() ? 1.0 : CloudTransmission[P];
                    const double Scale = Omega > 0.0 ? Flux * Wx * Wy * Visible / Omega : 0.0;
                    Sky[P] = AddRgb(Sky[P], {Colour.R*T.R*Scale, Colour.G*T.G*Scale, Colour.B*T.B*Scale});
                }
        }
    }

    // --- Denoise (SVGF luminance) + composite --------------------------------------------------------------
    std::vector<SpectrumRgb> FinalColour = MeanColour;
    if (Settings.Denoise && Frames > 0)
    {
        // Guides: view depth (miss = far), normals as float Vec3.
        std::vector<double> GuideDepth(PixelCount);
        std::vector<Vec3>   GuideNormals(PixelCount);
        for (size_t P = 0; P < PixelCount; ++P)
        {
            GuideDepth[P] = HitMask[P] ? ViewDepth[P] : Camera.FarPlane;
            const SpatialVector& N = WorldNormal[P];
            GuideNormals[P] = HitMask[P] ? Vec3{static_cast<float>(N.X), static_cast<float>(N.Y), static_cast<float>(N.Z)}
                                         : Vec3{0.0f, 0.0f, 1.0f};
            MeanLuminanceBuffer[P] = Luminance(MeanColour[P]);
        }
        std::vector<double> Ping = History.Mean(), Pong(PixelCount), VarPing = History.Variance(), VarPong(PixelCount);
        const VarianceFilterParameters Params{};
        for (uint32_t Level = 0; Level < 5; ++Level)
        {
            const bool Last = Level == 4;
            (void)VarianceWaveletStep(Ping.data(), VarPing.data(), GuideDepth.data(), GuideNormals.data(), W, H,
                                      1u << Level, Params, Pong.data(), Last ? nullptr : VarPong.data());
            Ping.swap(Pong);
            VarPing.swap(VarPong);
        }
        for (size_t P = 0; P < PixelCount; ++P)
        {
            if (!HitMask[P])
            {
                continue;
            }
            const double Ratio = std::clamp(Ping[P] / std::max(MeanLuminanceBuffer[P], 1e-6), 0.2, 5.0);
            FinalColour[P] = MulRgb(MeanColour[P], Ratio);
        }
    }

    double LuminanceSum = 0.0;
    for (size_t P = 0; P < PixelCount; ++P)
    {
        SpectrumRgb Value = HitMask[P] ? FinalColour[P] : Sky[P];
        if (HitMask[P] && Settings.FogEnabled)
        {
            const SpatialVector Delta = Sub(WorldPos[P], Camera.Position);
            const double Distance = Length(Delta);
            const double T = FogTransmittance(Settings.Fog, Camera.Position,
                                              Distance > 0 ? Mul(Delta, 1.0 / Distance) : Camera.Forward, Distance);
            Value = ApplyFog(Settings.Fog, Value, T);
        }
        if (Settings.LensFlaresEnabled && SunVisibility > 0.0)
        {
            const uint32_t X = static_cast<uint32_t>(P % W), Y = static_cast<uint32_t>(P / W);
            const double ScreenX = (static_cast<double>(X) + 0.5 - 0.5*W) / (0.5*H);
            const double ScreenY = (0.5*H - static_cast<double>(Y) - 0.5) / (0.5*H);
            const auto Flare = EvaluateLensFlareRadiance(ScreenX, ScreenY, SunU/TanY, SunV/TanY,
                                                        SunAtCamera, SunVisibility, Settings.LensFlares);
            Value = AddRgb(Value, Flare);
        }
        Image.Pixels[P] = Value;
        LuminanceSum += Luminance(Value);
    }
    Image.Stats.MeanLuminance = PixelCount ? LuminanceSum / static_cast<double>(PixelCount) : 0.0;
    return Image;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                       UTILITIES
//------------------------------------------------------------------------------------------------------------------------

uint64_t ReferenceImageHash(const ReferenceImage& Image) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    const auto Mix = [&Hash](uint64_t Value) {
        for (int Byte = 0; Byte < 8; ++Byte)
        {
            Hash ^= (Value >> (Byte * 8)) & 0xFFull;
            Hash *= 1099511628211ull;
        }
    };
    Mix(Image.Width);
    Mix(Image.Height);
    for (const SpectrumRgb& P : Image.Pixels)
    {
        Mix(static_cast<uint64_t>(std::llround(P.R * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(P.G * 1e6)));
        Mix(static_cast<uint64_t>(std::llround(P.B * 1e6)));
    }
    return Hash;
}

void EncodeDisplayRgb8(const ReferenceImage& Image, double Exposure, std::vector<uint8_t>& OutRgb8)
{
    OutRgb8.assign(Image.Pixels.size() * 3, 0);
    const auto Encode = [Exposure](double V) {
        const double Tonemapped = 1.0 - std::exp(-std::max(V, 0.0) * Exposure);
        return static_cast<uint8_t>(std::clamp(std::pow(Tonemapped, 1.0 / 2.2), 0.0, 1.0) * 255.0 + 0.5);
    };
    for (size_t P = 0; P < Image.Pixels.size(); ++P)
    {
        OutRgb8[P * 3 + 0] = Encode(Image.Pixels[P].R);
        OutRgb8[P * 3 + 1] = Encode(Image.Pixels[P].G);
        OutRgb8[P * 3 + 2] = Encode(Image.Pixels[P].B);
    }
}

} // namespace Frontier
