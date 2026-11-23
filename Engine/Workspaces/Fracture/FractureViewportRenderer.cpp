/*============================================================================================================================================
                                                       FRACTUREVIEWPORTRENDERER.CPP
============================================================================================================================================*/
// 🧩 OpenPBR Studio + Energy/Cells/Normals fracture viewport renderer (Vulkan SPIR-V bridge + scanline CPU rasterizer in RHR +Z Up)

#include "Engine/Workspaces/Fracture/FractureViewportRenderer.h"
#include "Engine/GeometrySpace/Fracture/ConvexPartitioning.h"
#include "Engine/MaterialSpace/Scattering/OpenPbrSurfaceScattering.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <limits>
#include <utility>

namespace Frontier::Workspaces::Fracture
{

namespace
{

constexpr float PiFloat = 3.14159265358979323846f;

struct CameraBasis
{
    Mathematics::Axis3D Eye     {};
    Mathematics::Axis3D Forward {};
    Mathematics::Axis3D Right   {};
    Mathematics::Axis3D Up      {};
    float               TanHalfFovY = 0.38f;
    float               Aspect      = 1.5f;
};

CameraBasis BuildCameraBasis(const FractureViewportCamera& Camera, uint32_t WidthPx, uint32_t HeightPx) noexcept
{
    CameraBasis Basis {};
    Basis.Eye         = FractureViewportRenderer::ComputeCameraPosition(Camera);
    Basis.Forward     = Mathematics::NormalizeAxis(Camera.TargetWorld - Basis.Eye, { 0.0f, 1.0f, 0.0f });
    Basis.Right       = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Basis.Forward, { 0.0f, 0.0f, 1.0f }), { 1.0f, 0.0f, 0.0f });
    Basis.Up          = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Basis.Right, Basis.Forward), { 0.0f, 0.0f, 1.0f });
    Basis.TanHalfFovY = std::tan(0.5f * Camera.VerticalFovDeg * (PiFloat / 180.0f));
    Basis.Aspect      = static_cast<float>(std::max(1u, WidthPx)) / static_cast<float>(std::max(1u, HeightPx));
    return Basis;
}

bool ProjectWorldPoint(
    const CameraBasis&  Cam,
    Mathematics::Axis3D WorldPt,
    uint32_t            WidthPx,
    uint32_t            HeightPx,
    float&              OutScreenX,
    float&              OutScreenY,
    float&              OutViewDepth) noexcept
{
    const auto Rel = WorldPt - Cam.Eye;
    const float Z  = Mathematics::EvaluateDot(Rel, Cam.Forward);
    if (Z <= 0.05f)
    {
        return false;
    }
    const float X = Mathematics::EvaluateDot(Rel, Cam.Right);
    const float Y = Mathematics::EvaluateDot(Rel, Cam.Up);
    const float NdcX = X / (Z * Cam.TanHalfFovY * Cam.Aspect);
    const float NdcY = Y / (Z * Cam.TanHalfFovY);
    OutScreenX   = (NdcX * 0.5f + 0.5f) * static_cast<float>(WidthPx);
    OutScreenY   = (0.5f - NdcY * 0.5f) * static_cast<float>(HeightPx);
    OutViewDepth = Z;
    return true;
}

uint8_t LinearToSrgb8(float V) noexcept
{
    const float Clamped = std::clamp(V, 0.0f, 1.0f);
    const float Srgb    = (Clamped <= 0.0031308f)
        ? (12.92f * Clamped)
        : (1.055f * std::pow(Clamped, 1.0f / 2.4f) - 0.055f);
    return static_cast<uint8_t>(std::clamp(std::lround(Srgb * 255.0f), 0l, 255l));
}

Mathematics::LinearRGB AcesToneMap(Mathematics::LinearRGB Radiance) noexcept
{
    const auto MapChannel = [](float X) noexcept -> float
    {
        const float Scaled = std::max(0.0f, X * 0.88f);
        return std::clamp((Scaled * (2.51f * Scaled + 0.03f)) / (Scaled * (2.43f * Scaled + 0.59f) + 0.14f), 0.0f, 1.0f);
    };
    return { MapChannel(Radiance.R), MapChannel(Radiance.G), MapChannel(Radiance.B) };
}

Mathematics::LinearRGB EvaluateStudioEnvironmentRhrZUp(Mathematics::Axis3D WorldDir, float Roughness) noexcept
{
    const auto Dir = Mathematics::NormalizeAxis(WorldDir);
    const float UpT = std::clamp(0.5f * (Dir.Z + 1.0f), 0.0f, 1.0f);
    Mathematics::LinearRGB Env {
        0.036f + (0.115f - 0.036f) * UpT,
        0.039f + (0.145f - 0.039f) * UpT,
        0.046f + (0.195f - 0.046f) * UpT
    };

    auto AddSoftbox = [&](Mathematics::Axis3D Center, float HalfU, float HalfV, Mathematics::LinearRGB Col, float Gain)
    {
        const auto C     = Mathematics::NormalizeAxis(Center);
        const float Face = Mathematics::EvaluateDot(Dir, C);
        if (Face <= 0.05f) return;
        const auto U = Mathematics::NormalizeAxis(Mathematics::EvaluateCross({ 0.0f, 0.0f, 1.0f }, C), { 1.0f, 0.0f, 0.0f });
        const auto V = Mathematics::EvaluateCross(C, U);
        const auto Proj = Dir * (1.0f / std::max(0.15f, Face)) - C;
        const float DU   = std::abs(Mathematics::EvaluateDot(Proj, U));
        const float DV   = std::abs(Mathematics::EvaluateDot(Proj, V));
        const float Blur = 0.035f + (0.58f - 0.035f) * std::clamp(Roughness * Roughness, 0.0f, 1.0f);
        auto SmoothStep  = [](float E0, float E1, float X) noexcept -> float
        {
            const float T = std::clamp((X - E0) / std::max(1.0e-5f, E1 - E0), 0.0f, 1.0f);
            return T * T * (3.0f - 2.0f * T);
        };
        const float Mask = (1.0f - SmoothStep(HalfU - Blur * 0.35f, HalfU + Blur, DU))
                         * (1.0f - SmoothStep(HalfV - Blur * 0.35f, HalfV + Blur, DV))
                         / (1.0f + 4.5f * Roughness * Roughness);
        Env.R += Col.R * Mask * Gain;
        Env.G += Col.G * Mask * Gain;
        Env.B += Col.B * Mask * Gain;
    };

    AddSoftbox({ 0.46f, -0.52f, 0.72f }, 0.34f, 0.24f, { 1.06f, 1.00f, 0.93f }, 2.65f);
    AddSoftbox({ -0.76f, -0.58f, 0.26f }, 0.18f, 0.42f, { 0.76f, 0.90f, 1.12f }, 1.45f);
    AddSoftbox({ -0.18f, 0.69f, 0.70f }, 0.45f, 0.14f, { 0.84f, 0.93f, 1.08f }, 1.60f);
    return Env;
}

Mathematics::LinearRGB EvaluateEnergyHeatmapLinear(float NormalizedStress) noexcept
{
    const float T = std::clamp(NormalizedStress, 0.0f, 1.0f);
    const std::array<std::array<float, 4>, 5> Stops {{
        { 0.00f, 0.188f, 0.420f, 0.949f }, // #306bf2
        { 0.30f, 0.184f, 0.647f, 0.541f }, // #2fa58a
        { 0.55f, 0.839f, 0.941f, 0.416f }, // #d6f06a
        { 0.78f, 1.000f, 0.541f, 0.239f }, // #ff8a3d
        { 1.00f, 0.941f, 0.302f, 0.239f }  // #f04d3d
    }};
    size_t I0 = 0u;
    while (I0 + 1u < Stops.size() && T > Stops[I0 + 1u][0]) ++I0;
    const size_t I1   = std::min(Stops.size() - 1u, I0 + 1u);
    const float  Span = std::max(1.0e-5f, Stops[I1][0] - Stops[I0][0]);
    const float  Frac = std::clamp((T - Stops[I0][0]) / Span, 0.0f, 1.0f);
    const float  R    = Stops[I0][1] + (Stops[I1][1] - Stops[I0][1]) * Frac;
    const float  G    = Stops[I0][2] + (Stops[I1][2] - Stops[I0][2]) * Frac;
    const float  Bl   = Stops[I0][3] + (Stops[I1][3] - Stops[I0][3]) * Frac;
    return { std::pow(R, 2.2f), std::pow(G, 2.2f), std::pow(Bl, 2.2f) };
}

Mathematics::LinearRGB EvaluateGoldenCellLinear(uint32_t CellIndex) noexcept
{
    const float Hue = std::fmod(static_cast<float>(CellIndex) * 0.61803398875f, 1.0f);
    auto Channel = [&](float Offset) noexcept -> float
    {
        const float X = std::abs(std::fmod(Hue + Offset, 1.0f) * 6.0f - 3.0f);
        const float S = std::clamp(X - 1.0f, 0.0f, 1.0f);
        return std::pow(0.24f + (S - 0.24f) * 0.74f, 2.2f);
    };
    return { Channel(1.0f), Channel(2.0f / 3.0f), Channel(1.0f / 3.0f) };
}

void DrawPixelBlend(
    std::vector<uint8_t>& Rgba,
    std::vector<float>&   Depth,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    int32_t               X,
    int32_t               Y,
    float                 ZDepth,
    uint8_t               R,
    uint8_t               G,
    uint8_t               B,
    float                 Alpha) noexcept
{
    if (X < 0 || Y < 0 || X >= static_cast<int32_t>(WidthPx) || Y >= static_cast<int32_t>(HeightPx))
    {
        return;
    }
    const size_t PIdx = static_cast<size_t>(Y) * WidthPx + static_cast<size_t>(X);
    if (ZDepth > Depth[PIdx] + 0.008f)
    {
        return;
    }
    const size_t CIdx = PIdx * 4u;
    const float  InvA = 1.0f - Alpha;
    Rgba[CIdx + 0u] = static_cast<uint8_t>(std::clamp(std::lround(Rgba[CIdx + 0u] * InvA + R * Alpha), 0l, 255l));
    Rgba[CIdx + 1u] = static_cast<uint8_t>(std::clamp(std::lround(Rgba[CIdx + 1u] * InvA + G * Alpha), 0l, 255l));
    Rgba[CIdx + 2u] = static_cast<uint8_t>(std::clamp(std::lround(Rgba[CIdx + 2u] * InvA + B * Alpha), 0l, 255l));
}

void DrawLine3D(
    const CameraBasis&    Cam,
    std::vector<uint8_t>& Rgba,
    std::vector<float>&   Depth,
    uint32_t              WidthPx,
    uint32_t              HeightPx,
    Mathematics::Axis3D   PtA,
    Mathematics::Axis3D   PtB,
    uint8_t               R,
    uint8_t               G,
    uint8_t               B,
    float                 Alpha,
    bool                  IgnoreDepth = false) noexcept
{
    float Ax = 0.0f, Ay = 0.0f, Az = 0.0f;
    float Bx = 0.0f, By = 0.0f, Bz = 0.0f;
    if (!ProjectWorldPoint(Cam, PtA, WidthPx, HeightPx, Ax, Ay, Az) ||
        !ProjectWorldPoint(Cam, PtB, WidthPx, HeightPx, Bx, By, Bz))
    {
        return;
    }

    const int32_t Steps = std::clamp(
        static_cast<int32_t>(std::ceil(std::max(std::abs(Bx - Ax), std::abs(By - Ay)))),
        1, 2048);
    for (int32_t S = 0; S <= Steps; ++S)
    {
        const float T  = static_cast<float>(S) / static_cast<float>(Steps);
        const int32_t Px = static_cast<int32_t>(std::lround(Ax + (Bx - Ax) * T));
        const int32_t Py = static_cast<int32_t>(std::lround(Ay + (By - Ay) * T));
        const float   Pz = IgnoreDepth ? 0.0f : (Az + (Bz - Az) * T);
        DrawPixelBlend(Rgba, Depth, WidthPx, HeightPx, Px, Py, Pz, R, G, B, Alpha);
    }
}

struct RenderTriangle
{
    Mathematics::Axis3D P0, P1, P2;
    Mathematics::Axis3D N0, N1, N2;
    uint32_t            CellId       = 0u;
    bool                IsInterior   = false;
    bool                Selected     = false;
    float               StressRatio  = 0.0f;
};

void ComputeFragmentWorldTransform(
    const GeometrySpace::Fracture::FracturePieceEntry&            Frag,
    Mathematics::Axis3D                                            GroupCentroid,
    const SimulationSpace::PhysicsSpace::FractureSimulationRunner* SimRunner,
    const FractureRenderOptions&                                   Options,
    Mathematics::Axis3D&                                           OutCentroidWorld,
    SimulationSpace::PhysicsSpace::RigidOrientationQuaternion&     OutQuat,
    Mathematics::Axis3D&                                           OutExplodeOffset) noexcept
{
    OutQuat          = {};
    OutExplodeOffset = (Frag.Centroid - GroupCentroid) * (Options.ExplodeFactor * 1.15f);

    if (SimRunner != nullptr && SimRunner->QueryLoaded() && Frag.Index < SimRunner->InspectBodyStates().size())
    {
        const auto& Body = SimRunner->InspectBodyStates()[Frag.Index];
        if (Body.Token.Valid())
        {
            OutCentroidWorld = Body.PositionMeters;
            OutQuat          = Body.Orientation;
            return;
        }
    }
    OutCentroidWorld = Frag.Centroid + Options.SolidWorldOffsetM;
}

Mathematics::Axis3D TransformLocalToWorld(
    Mathematics::Axis3D                                       LocalPoint,
    Mathematics::Axis3D                                       FragCentroidLocal,
    Mathematics::Axis3D                                       WorldOrigin,
    SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat,
    Mathematics::Axis3D                                       ExplodeOffset) noexcept
{
    const auto Rel     = LocalPoint - FragCentroidLocal;
    const auto Rotated = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, Rel);
    return WorldOrigin + Rotated + ExplodeOffset;
}

void DrawPolyhedronEdges3D(
    const CameraBasis&                                        Cam,
    std::vector<uint8_t>&                                     Rgba,
    std::vector<float>&                                       DepthBuf,
    uint32_t                                                  W,
    uint32_t                                                  H,
    const GeometrySpace::Fracture::ConvexPolyhedron&          Poly,
    Mathematics::Axis3D                                       FragCentroidLocal,
    Mathematics::Axis3D                                       WorldOrigin,
    SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat,
    Mathematics::Axis3D                                       ExplodeOffset,
    uint8_t                                                   R,
    uint8_t                                                   G,
    uint8_t                                                   B,
    float                                                     Alpha,
    bool                                                      IgnoreSeamFaces)
{
    std::vector<uint64_t> SeenEdges;
    SeenEdges.reserve(Poly.Faces.size() * 4u);

    for (const auto& Face : Poly.Faces)
    {
        if (IgnoreSeamFaces && Face.Seam) continue;
        if (Face.Indices.size() < 2u) continue;

        for (size_t K = 0u; K < Face.Indices.size(); ++K)
        {
            const uint32_t Ia = Face.Indices[K];
            const uint32_t Ib = Face.Indices[(K + 1u) % Face.Indices.size()];
            if (Ia >= Poly.Vertices.size() || Ib >= Poly.Vertices.size() || Ia == Ib) continue;

            const uint32_t Lo = std::min(Ia, Ib);
            const uint32_t Hi = std::max(Ia, Ib);
            const uint64_t Key = (static_cast<uint64_t>(Lo) << 32u) | static_cast<uint64_t>(Hi);
            if (std::find(SeenEdges.begin(), SeenEdges.end(), Key) != SeenEdges.end()) continue;
            SeenEdges.push_back(Key);

            const auto WaRaw = TransformLocalToWorld(Poly.Vertices[Lo], FragCentroidLocal, WorldOrigin, Quat, ExplodeOffset);
            const auto WbRaw = TransformLocalToWorld(Poly.Vertices[Hi], FragCentroidLocal, WorldOrigin, Quat, ExplodeOffset);
            const auto Wa    = WaRaw + Mathematics::NormalizeAxis(Cam.Eye - WaRaw) * 0.006f;
            const auto Wb    = WbRaw + Mathematics::NormalizeAxis(Cam.Eye - WbRaw) * 0.006f;
            DrawLine3D(Cam, Rgba, DepthBuf, W, H, Wa, Wb, R, G, B, Alpha, false);
        }
    }
}

void DrawOuterConvexHullEdges3D(
    const CameraBasis&                                        Cam,
    std::vector<uint8_t>&                                     Rgba,
    std::vector<float>&                                       DepthBuf,
    uint32_t                                                  W,
    uint32_t                                                  H,
    const std::vector<Mathematics::Axis3D>&                   RawVerts,
    Mathematics::Axis3D                                       FragCentroidLocal,
    Mathematics::Axis3D                                       WorldOrigin,
    SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat,
    Mathematics::Axis3D                                       ExplodeOffset,
    uint8_t                                                   R,
    uint8_t                                                   G,
    uint8_t                                                   B,
    float                                                     Alpha)
{
    const auto Edges = SimulationSpace::PhysicsSpace::PhysicsExtension::BuildOuterConvexHullEdges(RawVerts);
    for (const auto& [Lo, Hi] : Edges)
    {
        if (Lo >= RawVerts.size() || Hi >= RawVerts.size()) continue;
        const auto WaRaw = TransformLocalToWorld(RawVerts[Lo], FragCentroidLocal, WorldOrigin, Quat, ExplodeOffset);
        const auto WbRaw = TransformLocalToWorld(RawVerts[Hi], FragCentroidLocal, WorldOrigin, Quat, ExplodeOffset);
        const auto Wa    = WaRaw + Mathematics::NormalizeAxis(Cam.Eye - WaRaw) * 0.006f;
        const auto Wb    = WbRaw + Mathematics::NormalizeAxis(Cam.Eye - WbRaw) * 0.006f;
        DrawLine3D(Cam, Rgba, DepthBuf, W, H, Wa, Wb, R, G, B, Alpha, false);
    }
}

} // namespace

FractureViewportRenderer::~FractureViewportRenderer()
{
    TeardownVulkanShaders();
}

bool FractureViewportRenderer::InitializeVulkanShaders(
    GeometricRaster::VulkanInterchange& TargetDevice,
    const std::filesystem::path&        CompiledShaderDirectory)
{
    TeardownVulkanShaders();
    if (!TargetDevice.Ready())
    {
        return false;
    }

    const auto VertPath = CompiledShaderDirectory / "FractureSurfaceResolve.vert.spv";
    const auto FragPath = CompiledShaderDirectory / "FractureSurfaceResolve.frag.spv";
    if (!std::filesystem::exists(VertPath) || !std::filesystem::exists(FragPath))
    {
        return false;
    }

    auto ReadSpv = [](const std::filesystem::path& Path, std::vector<uint8_t>& Out) -> bool
    {
        std::ifstream Stream(Path, std::ios::binary | std::ios::ate);
        if (!Stream.is_open()) return false;
        const auto Size = Stream.tellg();
        if (Size <= 0) return false;
        Out.resize(static_cast<size_t>(Size));
        Stream.seekg(0);
        return static_cast<bool>(Stream.read(reinterpret_cast<char*>(Out.data()), Size));
    };

    if (!ReadSpv(VertPath, VertexSpirvBytes) || !ReadSpv(FragPath, FragmentSpirvBytes))
    {
        TeardownVulkanShaders();
        return false;
    }

    SpirvVerified = true;
    return true;
}

void FractureViewportRenderer::TeardownVulkanShaders() noexcept
{
    SpirvVerified = false;
    VertexSpirvBytes.clear();
    FragmentSpirvBytes.clear();
}

Mathematics::Axis3D FractureViewportRenderer::ComputeCameraPosition(const FractureViewportCamera& Camera) noexcept
{
    const float Cp = std::cos(Camera.PitchRadians);
    const float Sp = std::sin(Camera.PitchRadians);
    const float Cy = std::cos(Camera.YawRadians);
    const float Sy = std::sin(Camera.YawRadians);
    return {
        Camera.TargetWorld.X + Camera.DistanceM * Cp * Sy,
        Camera.TargetWorld.Y - Camera.DistanceM * Cp * Cy,
        Camera.TargetWorld.Z + Camera.DistanceM * Sp
    };
}

void FractureViewportRenderer::BuildOpenPbrPairForFractureMaterial(
    const GeometrySpace::Fracture::FractureMaterialSpec& Spec,
    MaterialSpace::SurfaceMaterial&                      OutExterior,
    MaterialSpace::SurfaceMaterial&                      OutInterior) noexcept
{
    OutExterior = {};
    OutExterior.MaterialName             = Spec.Label;
    OutExterior.Diffuse.BaseColor        = { Spec.ExteriorLinearRgb.X, Spec.ExteriorLinearRgb.Y, Spec.ExteriorLinearRgb.Z };
    OutExterior.Diffuse.BaseWeight       = std::max(0.0f, 1.0f - Spec.Transmission);
    OutExterior.Diffuse.DiffuseRoughness = std::clamp(Spec.Roughness * 0.85f, 0.05f, 0.95f);
    OutExterior.Specular.Metalness       = Spec.Metalness;
    OutExterior.Specular.Roughness       = std::clamp(Spec.Roughness, 0.04f, 0.96f);
    OutExterior.Specular.SpecularWeight  = 1.0f;
    OutExterior.Specular.DielectricIOR   = Spec.Transmission > 0.1f ? 1.52f : 1.48f;
    OutExterior.Transmission.TransmissionWeight = Spec.Transmission;

    OutInterior = OutExterior;
    OutInterior.Diffuse.BaseColor = { Spec.InteriorLinearRgb.X, Spec.InteriorLinearRgb.Y, Spec.InteriorLinearRgb.Z };
    OutInterior.Specular.Roughness = std::clamp(Spec.Roughness * 1.05f + 0.04f, 0.05f, 0.98f);
    MaterialSpace::SanitizeSurfaceMaterial(OutExterior);
    MaterialSpace::SanitizeSurfaceMaterial(OutInterior);
}

FractureRenderOutput FractureViewportRenderer::RenderFrame(
    const GeometrySpace::Fracture::FracturePartitionResult*      Partition,
    const GeometrySpace::Fracture::ConvexPolyhedron*             IntactFallbackSolid,
    const GeometrySpace::Fracture::FractureMaterialSpec&         Material,
    const SimulationSpace::PhysicsSpace::FractureSimulationRunner* SimRunner,
    const FractureViewportCamera&                                Camera,
    const FractureRenderOptions&                                 Options,
    Mathematics::Axis3D                                          ImpactPointLocal,
    Mathematics::Axis3D                                          ImpactNormalLocal,
    uint32_t                                                     WidthPx,
    uint32_t                                                     HeightPx) const
{
    const auto StartClock = std::chrono::steady_clock::now();
    const uint32_t W = std::max(64u, WidthPx);
    const uint32_t H = std::max(64u, HeightPx);

    FractureRenderOutput Output {};
    Output.WidthPx          = W;
    Output.HeightPx         = H;
    Output.VulkanSpirvReady = SpirvVerified;
    Output.DisplayRgba8.resize(static_cast<size_t>(W) * static_cast<size_t>(H) * 4u, 255u);

    std::vector<float> DepthBuffer(static_cast<size_t>(W) * static_cast<size_t>(H), std::numeric_limits<float>::infinity());
    const CameraBasis Cam = BuildCameraBasis(Camera, W, H);

    // 1. Studio radial backdrop + RHR +Z Up horizon raycast onto Z=0 studio floor
    for (uint32_t Row = 0u; Row < H; ++Row)
    {
        const float VScreen = static_cast<float>(Row) / static_cast<float>(H - 1u);
        const float NdcY    = 1.0f - 2.0f * VScreen;
        for (uint32_t Col = 0u; Col < W; ++Col)
        {
            const float UScreen = static_cast<float>(Col) / static_cast<float>(W - 1u);
            const float NdcX    = 2.0f * UScreen - 1.0f;

            const float Cx = NdcX * Cam.TanHalfFovY * Cam.Aspect;
            const float Cy = NdcY * Cam.TanHalfFovY;
            const float Radial = Cx * Cx + Cy * Cy;
            const float Grad   = std::clamp(1.0f - 0.38f * Radial, 0.18f, 1.0f) * (0.042f + 0.032f * (1.0f - VScreen));
            Mathematics::LinearRGB PixelLin { Grad, Grad * 1.04f, Grad * 1.12f };

            if (Options.ShowGroundGrid)
            {
                const auto RayDir = Mathematics::NormalizeAxis(Cam.Forward + Cam.Right * Cx + Cam.Up * Cy);
                if (RayDir.Z < -1.0e-4f && Cam.Eye.Z > 0.0f)
                {
                    const float TGround = -Cam.Eye.Z / RayDir.Z;
                    if (TGround > 0.05f && TGround < 45.0f)
                    {
                        const float Gx = Cam.Eye.X + RayDir.X * TGround;
                        const float Gy = Cam.Eye.Y + RayDir.Y * TGround;
                        const float DistR = std::hypot(Gx, Gy);
                        if (DistR < 14.0f)
                        {
                            const float Fade = std::exp(-DistR * 0.16f);
                            const float Fx   = std::abs(Gx - std::round(Gx));
                            const float Fy   = std::abs(Gy - std::round(Gy));
                            const float Line = (Fx < 0.018f || Fy < 0.018f) ? 1.0f : 0.0f;
                            const float Sub  = (std::abs(Gx * 2.0f - std::round(Gx * 2.0f)) < 0.015f ||
                                                std::abs(Gy * 2.0f - std::round(Gy * 2.0f)) < 0.015f) ? 0.35f : 0.0f;
                            const float ShadowDisc = 1.0f - 0.52f * std::exp(-(DistR * DistR) / 1.45f);
                            const float FloorBase  = 0.036f * ShadowDisc;
                            const float GridAdd    = (Line * 0.065f + Sub * 0.022f) * Fade;
                            const float AxisX      = (std::abs(Gy) < 0.018f && DistR < 6.0f) ? 0.12f * Fade : 0.0f;
                            const float AxisY      = (std::abs(Gx) < 0.018f && DistR < 6.0f) ? 0.12f * Fade : 0.0f;
                            PixelLin.R = PixelLin.R * (1.0f - Fade) + (FloorBase + GridAdd + AxisX) * Fade;
                            PixelLin.G = PixelLin.G * (1.0f - Fade) + (FloorBase + GridAdd + AxisY) * Fade;
                            PixelLin.B = PixelLin.B * (1.0f - Fade) + (FloorBase + GridAdd * 1.15f) * Fade;
                        }
                    }
                }
            }

            const size_t Off = (static_cast<size_t>(Row) * W + Col) * 4u;
            Output.DisplayRgba8[Off + 0u] = LinearToSrgb8(PixelLin.R);
            Output.DisplayRgba8[Off + 1u] = LinearToSrgb8(PixelLin.G);
            Output.DisplayRgba8[Off + 2u] = LinearToSrgb8(PixelLin.B);
            Output.DisplayRgba8[Off + 3u] = 255u;
        }
    }

    // 2. Gather triangles from either active FracturePartitionResult or intact fallback polyhedron
    std::vector<RenderTriangle> Triangles;
    std::vector<Mathematics::Axis3D> FragmentWorldCentroids;

    if (Partition != nullptr && !Partition->Fragments.empty())
    {
        const auto GroupCentroid = Partition->SourceMetrics.Centroid;
        FragmentWorldCentroids.resize(Partition->Fragments.size(), GroupCentroid + Options.SolidWorldOffsetM);

        for (const auto& Frag : Partition->Fragments)
        {
            if (!Frag.VisibilityEnabled) continue;
            if (Options.FragmentVisibleFlags != nullptr &&
                Frag.Index < Options.FragmentVisibleFlags->size() &&
                !(*Options.FragmentVisibleFlags)[Frag.Index])
            {
                continue;
            }

            Mathematics::Axis3D FragWorldOrigin {};
            SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat {};
            Mathematics::Axis3D ExplodeOffset {};
            ComputeFragmentWorldTransform(Frag, GroupCentroid, SimRunner, Options, FragWorldOrigin, Quat, ExplodeOffset);
            if (Frag.Index < FragmentWorldCentroids.size())
            {
                FragmentWorldCentroids[Frag.Index] = FragWorldOrigin + ExplodeOffset;
            }

            const bool IsSelected = (Options.SelectedFragmentIndex == static_cast<int32_t>(Frag.Index));
            if (Options.UseNonPlanarFractography && Frag.HasDetailedSurf && Frag.DetailedSurface.TriangleCount > 0u)
            {
                const auto& Surf = Frag.DetailedSurface;
                for (uint32_t T = 0u; T < Surf.TriangleCount; ++T)
                {
                    const size_t I0 = static_cast<size_t>(T) * 3u;
                    const auto P0 = TransformLocalToWorld(Surf.Positions[I0 + 0u], Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                    const auto P1 = TransformLocalToWorld(Surf.Positions[I0 + 1u], Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                    const auto P2 = TransformLocalToWorld(Surf.Positions[I0 + 2u], Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                    const auto N0 = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, Surf.Normals[I0 + 0u]);
                    const auto N1 = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, Surf.Normals[I0 + 1u]);
                    const auto N2 = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, Surf.Normals[I0 + 2u]);
                    Triangles.push_back({ P0, P1, P2, N0, N1, N2, Frag.Index, Surf.InteriorMask[I0] != 0u, IsSelected, Frag.StressRatio });
                }
            }
            else
            {
                const auto& Poly = Frag.Polyhedron;
                for (const auto& Face : Poly.Faces)
                {
                    if (Face.Seam || Face.Indices.size() < 3u) continue;
                    const auto LocalN = GeometrySpace::Fracture::ConvexPartitioning::ComputeFaceNormal(Poly, Face);
                    const auto WorldN = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, LocalN);
                    const auto P0 = TransformLocalToWorld(Poly.Vertices[Face.Indices[0]], Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                    for (size_t K = 1u; K + 1u < Face.Indices.size(); ++K)
                    {
                        const auto P1 = TransformLocalToWorld(Poly.Vertices[Face.Indices[K]],      Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                        const auto P2 = TransformLocalToWorld(Poly.Vertices[Face.Indices[K + 1u]], Frag.Centroid, FragWorldOrigin, Quat, ExplodeOffset);
                        Triangles.push_back({ P0, P1, P2, WorldN, WorldN, WorldN, Frag.Index, Face.Interior, IsSelected, Frag.StressRatio });
                    }
                }
            }
        }
    }
    else if (IntactFallbackSolid != nullptr)
    {
        for (const auto& Face : IntactFallbackSolid->Faces)
        {
            if (Face.Indices.size() < 3u) continue;
            const auto N = GeometrySpace::Fracture::ConvexPartitioning::ComputeFaceNormal(*IntactFallbackSolid, Face);
            const auto P0 = IntactFallbackSolid->Vertices[Face.Indices[0]] + Options.SolidWorldOffsetM;
            for (size_t K = 1u; K + 1u < Face.Indices.size(); ++K)
            {
                const auto P1 = IntactFallbackSolid->Vertices[Face.Indices[K]]      + Options.SolidWorldOffsetM;
                const auto P2 = IntactFallbackSolid->Vertices[Face.Indices[K + 1u]] + Options.SolidWorldOffsetM;
                Triangles.push_back({ P0, P1, P2, N, N, N, 0u, false, false, 0.0f });
            }
        }
    }

    MaterialSpace::SurfaceMaterial ExtMat {}, IntMat {};
    BuildOpenPbrPairForFractureMaterial(Material, ExtMat, IntMat);
    if (Options.SurfaceMaterial != nullptr)
    {
        MaterialSpace::SurfaceMaterial IgnoredInt {};
        BuildOpenPbrPairForFractureMaterial(*Options.SurfaceMaterial, ExtMat, IgnoredInt);
    }
    const auto& ExtRef = (Options.SurfaceMaterial != nullptr) ? *Options.SurfaceMaterial : Material;
    const uint32_t ExtMask = MaterialSpace::LobeDiffuseEON | MaterialSpace::LobeSpecularMetalGGX
                           | (ExtRef.Transmission > 0.05f ? MaterialSpace::LobeDielectricTransmission : 0u);
    const auto KeyDir  = Mathematics::NormalizeAxis({ 0.46f, -0.52f, 0.72f });
    const auto FillDir = Mathematics::NormalizeAxis({ -0.76f, -0.58f, 0.26f });
    const auto RimDir  = Mathematics::NormalizeAxis({ -0.18f, 0.69f, 0.70f });

    // 3. Scanline rasterize every triangle with exact OpenPBR 10-lobe evaluation + Studio softboxes
    for (const auto& Tri : Triangles)
    {
        float Sx0 = 0.0f, Sy0 = 0.0f, Sz0 = 0.0f;
        float Sx1 = 0.0f, Sy1 = 0.0f, Sz1 = 0.0f;
        float Sx2 = 0.0f, Sy2 = 0.0f, Sz2 = 0.0f;
        if (!ProjectWorldPoint(Cam, Tri.P0, W, H, Sx0, Sy0, Sz0) ||
            !ProjectWorldPoint(Cam, Tri.P1, W, H, Sx1, Sy1, Sz1) ||
            !ProjectWorldPoint(Cam, Tri.P2, W, H, Sx2, Sy2, Sz2))
        {
            continue;
        }

        const float Area2 = (Sx1 - Sx0) * (Sy2 - Sy0) - (Sx2 - Sx0) * (Sy1 - Sy0);
        if (std::abs(Area2) < 0.25f) continue;

        const int32_t MinX = std::max(0, static_cast<int32_t>(std::floor(std::min({ Sx0, Sx1, Sx2 }))));
        const int32_t MaxX = std::min(static_cast<int32_t>(W) - 1, static_cast<int32_t>(std::ceil(std::max({ Sx0, Sx1, Sx2 }))));
        const int32_t MinY = std::max(0, static_cast<int32_t>(std::floor(std::min({ Sy0, Sy1, Sy2 }))));
        const int32_t MaxY = std::min(static_cast<int32_t>(H) - 1, static_cast<int32_t>(std::ceil(std::max({ Sy0, Sy1, Sy2 }))));
        if (MinX > MaxX || MinY > MaxY) continue;

        const float InvArea = 1.0f / Area2;
        const float InvZ0   = 1.0f / Sz0, InvZ1 = 1.0f / Sz1, InvZ2 = 1.0f / Sz2;

        ++Output.DrawnTriangles;

        for (int32_t Py = MinY; Py <= MaxY; ++Py)
        {
            const float Cy = static_cast<float>(Py) + 0.5f;
            for (int32_t Px = MinX; Px <= MaxX; ++Px)
            {
                const float Cx = static_cast<float>(Px) + 0.5f;
                const float W0 = ((Sx1 - Cx) * (Sy2 - Cy) - (Sx2 - Cx) * (Sy1 - Cy)) * InvArea;
                const float W1 = ((Sx2 - Cx) * (Sy0 - Cy) - (Sx0 - Cx) * (Sy2 - Cy)) * InvArea;
                const float W2 = 1.0f - W0 - W1;
                if (W0 < -1.0e-4f || W1 < -1.0e-4f || W2 < -1.0e-4f) continue;

                const float InvZ = W0 * InvZ0 + W1 * InvZ1 + W2 * InvZ2;
                if (InvZ <= 1.0e-6f) continue;
                const float Depth = 1.0f / InvZ;

                const size_t PixIdx = static_cast<size_t>(Py) * W + static_cast<size_t>(Px);
                if (Depth >= DepthBuffer[PixIdx]) continue;
                DepthBuffer[PixIdx] = Depth;

                const float B0 = (W0 * InvZ0) * Depth;
                const float B1 = (W1 * InvZ1) * Depth;
                const float B2 = (W2 * InvZ2) * Depth;

                const auto WorldPos = Tri.P0 * B0 + Tri.P1 * B1 + Tri.P2 * B2;
                auto N = Mathematics::NormalizeAxis(Tri.N0 * B0 + Tri.N1 * B1 + Tri.N2 * B2, { 0.0f, 0.0f, 1.0f });
                const auto V = Mathematics::NormalizeAxis(Cam.Eye - WorldPos);
                if (Mathematics::EvaluateDot(N, V) < 0.0f) N = N * -1.0f;

                Mathematics::LinearRGB OutLinear { 0.0f, 0.0f, 0.0f };
                if (Options.RenderMode == FractureRenderViewMode::Normals)
                {
                    const size_t Off = PixIdx * 4u;
                    Output.DisplayRgba8[Off + 0u] = static_cast<uint8_t>(std::clamp(std::lround((N.X * 0.5f + 0.5f) * 255.0f), 0l, 255l));
                    Output.DisplayRgba8[Off + 1u] = static_cast<uint8_t>(std::clamp(std::lround((N.Y * 0.5f + 0.5f) * 255.0f), 0l, 255l));
                    Output.DisplayRgba8[Off + 2u] = static_cast<uint8_t>(std::clamp(std::lround((N.Z * 0.5f + 0.5f) * 255.0f), 0l, 255l));
                    continue;
                }
                else if (Options.RenderMode == FractureRenderViewMode::Energy)
                {
                    const float LogStress = std::log(1.0f + std::max(0.0f, Tri.StressRatio) * 18.0f) / std::log(19.0f);
                    const auto  Heat      = EvaluateEnergyHeatmapLinear(LogStress);
                    const float Diff      = 0.35f + 0.65f * std::max(0.0f, Mathematics::EvaluateDot(N, KeyDir));
                    OutLinear = { Heat.R * Diff, Heat.G * Diff, Heat.B * Diff };
                }
                else if (Options.RenderMode == FractureRenderViewMode::Cells)
                {
                    auto        CellCol = EvaluateGoldenCellLinear(Tri.CellId);
                    const float Diff    = 0.32f + 0.68f * std::max(0.0f, Mathematics::EvaluateDot(N, KeyDir));
                    const float IntDim  = Tri.IsInterior ? 0.84f : 1.0f;
                    OutLinear = { CellCol.R * Diff * IntDim, CellCol.G * Diff * IntDim, CellCol.B * Diff * IntDim };
                }
                else
                {
                    const auto& Mat = Tri.IsInterior ? IntMat : ExtMat;
                    const auto  Ref = std::abs(N.Z) < 0.85f ? Mathematics::Axis3D { 0.0f, 0.0f, 1.0f } : Mathematics::Axis3D { 1.0f, 0.0f, 0.0f };
                    const auto  Tx  = Mathematics::NormalizeAxis(Mathematics::EvaluateCross(Ref, N));
                    const auto  Ty  = Mathematics::EvaluateCross(N, Tx);
                    const MaterialSpace::Scattering::SurfaceShadingBasis Frame { Tx, Ty, N };
                    const auto  LocalWo = Frame.WorldToLocal(V);

                    auto AccumLight = [&](Mathematics::Axis3D LWorld, Mathematics::LinearRGB LCol, float Gain)
                    {
                        const auto LLocal = Frame.WorldToLocal(LWorld);
                        if (LLocal.Z <= 1.0e-4f) return;
                        const auto F = MaterialSpace::Scattering::EvaluateOpenPbrLocalBsdf(Mat, ExtMask, LocalWo, LLocal);
                        OutLinear.R += F.R * LCol.R * Gain * LLocal.Z;
                        OutLinear.G += F.G * LCol.G * Gain * LLocal.Z;
                        OutLinear.B += F.B * LCol.B * Gain * LLocal.Z;
                    };

                    AccumLight(KeyDir,  { 1.06f, 1.00f, 0.93f }, 2.65f);
                    AccumLight(FillDir, { 0.76f, 0.90f, 1.12f }, 1.45f);
                    AccumLight(RimDir,  { 0.84f, 0.93f, 1.08f }, 1.60f);

                    const auto  ReflDir = N * (2.0f * Mathematics::EvaluateDot(N, V)) - V;
                    const auto  EnvRefl = EvaluateStudioEnvironmentRhrZUp(ReflDir, Mat.Specular.Roughness);
                    const auto  EnvDiff = EvaluateStudioEnvironmentRhrZUp(N, 0.95f);
                    const float Fresnel = 0.04f + 0.96f * std::pow(1.0f - std::max(0.0f, Mathematics::EvaluateDot(N, V)), 5.0f);
                    OutLinear.R += Mat.Diffuse.BaseColor.R * EnvDiff.R * 0.25f + EnvRefl.R * Fresnel * 0.42f;
                    OutLinear.G += Mat.Diffuse.BaseColor.G * EnvDiff.G * 0.25f + EnvRefl.G * Fresnel * 0.42f;
                    OutLinear.B += Mat.Diffuse.BaseColor.B * EnvDiff.B * 0.25f + EnvRefl.B * Fresnel * 0.42f;
                }

                if (Tri.Selected)
                {
                    OutLinear.R += 0.28f;
                    OutLinear.G += 0.18f;
                    OutLinear.B += 0.04f;
                }

                const auto Mapped = AcesToneMap(OutLinear);
                const size_t Off  = PixIdx * 4u;
                Output.DisplayRgba8[Off + 0u] = LinearToSrgb8(Mapped.R);
                Output.DisplayRgba8[Off + 1u] = LinearToSrgb8(Mapped.G);
                Output.DisplayRgba8[Off + 2u] = LinearToSrgb8(Mapped.B);
            }
        }

        if (Options.ShowWireframe)
        {
            DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, Tri.P0, Tri.P1, 210u, 224u, 240u, 0.45f);
            DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, Tri.P1, Tri.P2, 210u, 224u, 240u, 0.45f);
            DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, Tri.P2, Tri.P0, 210u, 224u, 240u, 0.45f);
        }
    }

    // 3b. Optional JoltPhysics collision-shape debug wireframe overlay (ConvexHull cyan cage vs CollisionMesh green sub-hulls)
    if (Options.ShowCollisionShapes)
    {
        const bool ExactShapeMode =
            Options.CollisionDebugMode == SimulationSpace::PhysicsSpace::FractureCollisionShapeMode::CollisionMesh;

        // Ground collision plane wireframe slab at Z = 0
        const float GroundHalf = 3.2f;
        const std::array<Mathematics::Axis3D, 4> GroundCorners {{
            { -GroundHalf, -GroundHalf, 0.003f },
            {  GroundHalf, -GroundHalf, 0.003f },
            {  GroundHalf,  GroundHalf, 0.003f },
            { -GroundHalf,  GroundHalf, 0.003f }
        }};
        for (size_t K = 0u; K < 4u; ++K)
        {
            DrawLine3D(
                Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                GroundCorners[K], GroundCorners[(K + 1u) % 4u],
                64u, 200u, 176u, 0.45f, false);
        }

        if (Partition != nullptr && !Partition->Fragments.empty())
        {
            const auto GroupCentroid = Partition->SourceMetrics.Centroid;
            for (const auto& Frag : Partition->Fragments)
            {
                if (!Frag.VisibilityEnabled) continue;
                if (Options.FragmentVisibleFlags != nullptr &&
                    Frag.Index < Options.FragmentVisibleFlags->size() &&
                    !(*Options.FragmentVisibleFlags)[Frag.Index])
                {
                    continue;
                }

                Mathematics::Axis3D FragOrigin {};
                SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat {};
                Mathematics::Axis3D ExplodeOffset {};
                ComputeFragmentWorldTransform(Frag, GroupCentroid, SimRunner, Options, FragOrigin, Quat, ExplodeOffset);

                const bool IsSel = (static_cast<int32_t>(Frag.Index) == Options.SelectedFragmentIndex);
                const uint8_t WireR = IsSel ? 255u : (ExactShapeMode ? 56u  : 64u);
                const uint8_t WireG = IsSel ? 204u : (ExactShapeMode ? 232u : 212u);
                const uint8_t WireB = IsSel ? 76u  : (ExactShapeMode ? 128u : 255u);
                const float   Alpha = IsSel ? 0.98f : 0.88f;

                if (ExactShapeMode)
                {
                    if (!Frag.SubConvexHulls.empty())
                    {
                        for (const auto& SubHull : Frag.SubConvexHulls)
                        {
                            DrawPolyhedronEdges3D(
                                Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                                SubHull, Frag.Centroid, FragOrigin, Quat, ExplodeOffset,
                                WireR, WireG, WireB, Alpha, false);
                        }
                    }
                    else
                    {
                        DrawPolyhedronEdges3D(
                            Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                            Frag.Polyhedron, Frag.Centroid, FragOrigin, Quat, ExplodeOffset,
                            WireR, WireG, WireB, Alpha, true);
                    }
                }
                else
                {
                    if (Frag.SubConvexHulls.size() >= 2u)
                    {
                        DrawOuterConvexHullEdges3D(
                            Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                            Frag.Polyhedron.Vertices, Frag.Centroid, FragOrigin, Quat, ExplodeOffset,
                            WireR, WireG, WireB, Alpha);
                    }
                    else
                    {
                        DrawPolyhedronEdges3D(
                            Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                            Frag.Polyhedron, Frag.Centroid, FragOrigin, Quat, ExplodeOffset,
                            WireR, WireG, WireB, Alpha, true);
                    }
                }
            }
        }
        else if (IntactFallbackSolid != nullptr)
        {
            const SimulationSpace::PhysicsSpace::RigidOrientationQuaternion IdentityQuat {};
            const Mathematics::Axis3D ZeroCentroid {};
            const Mathematics::Axis3D ZeroOffset {};
            DrawPolyhedronEdges3D(
                Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                *IntactFallbackSolid, ZeroCentroid, Options.SolidWorldOffsetM, IdentityQuat, ZeroOffset,
                ExactShapeMode ? 56u : 64u, ExactShapeMode ? 232u : 212u, ExactShapeMode ? 128u : 255u, 0.88f, false);
        }
    }

    // 4. Optional bond network overlay
    if (Options.ShowBonds && Partition != nullptr && !Partition->Bonds.empty())
    {
        for (size_t BIdx = 0u; BIdx < Partition->Bonds.size(); ++BIdx)
        {
            const auto& Bond = Partition->Bonds[BIdx];
            bool Intact = !Bond.BrokenOnImpact;
            if (SimRunner != nullptr && SimRunner->QueryLoaded() && BIdx < SimRunner->InspectBondStates().size())
            {
                Intact = SimRunner->InspectBondStates()[BIdx].Intact;
            }
            const auto Pa = Bond.IndexA < FragmentWorldCentroids.size() ? FragmentWorldCentroids[Bond.IndexA] : (Bond.PointA + Options.SolidWorldOffsetM);
            const auto Pb = Bond.IndexB < FragmentWorldCentroids.size() ? FragmentWorldCentroids[Bond.IndexB] : (Bond.PointB + Options.SolidWorldOffsetM);
            if (Intact)
            {
                DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, Pa, Pb, 56u, 217u, 169u, 0.85f, true);
            }
            else
            {
                DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, Pa, Pb, 255u, 96u, 82u, 0.35f, true);
            }
        }
    }

    // 5. Optional LEFM/dendritic wavefront polylines
    if (Options.ShowWavefronts && Partition != nullptr)
    {
        for (const auto& Path : Partition->WavefrontPaths)
        {
            const uint8_t Cr = Path.IsPrimaryTrunk ? 255u : 112u;
            const uint8_t Cg = Path.IsPrimaryTrunk ? 204u : 208u;
            const uint8_t Cb = Path.IsPrimaryTrunk ? 76u  : 255u;
            for (size_t K = 1u; K < Path.Points.size(); ++K)
            {
                DrawLine3D(
                    Cam, Output.DisplayRgba8, DepthBuffer, W, H,
                    Path.Points[K - 1u] + Options.SolidWorldOffsetM,
                    Path.Points[K]      + Options.SolidWorldOffsetM,
                    Cr, Cg, Cb, 0.90f, true);
            }
        }
    }

    // 6. Impact point & normal arrow marker
    if (Options.ShowImpactMarker)
    {
        const auto WorldImpact = ImpactPointLocal + Options.SolidWorldOffsetM;
        const auto ArrowTip    = WorldImpact + Mathematics::NormalizeAxis(ImpactNormalLocal, { 0.0f, -1.0f, 0.0f }) * 0.34f;
        DrawLine3D(Cam, Output.DisplayRgba8, DepthBuffer, W, H, WorldImpact, ArrowTip, 255u, 118u, 72u, 1.0f, true);

        float Sx = 0.0f, Sy = 0.0f, Sz = 0.0f;
        if (ProjectWorldPoint(Cam, WorldImpact, W, H, Sx, Sy, Sz))
        {
            const int32_t Cx = static_cast<int32_t>(std::lround(Sx));
            const int32_t Cy = static_cast<int32_t>(std::lround(Sy));
            for (int32_t Dy = -5; Dy <= 5; ++Dy)
            {
                for (int32_t Dx = -5; Dx <= 5; ++Dx)
                {
                    if (Dx * Dx + Dy * Dy <= 25 && Dx * Dx + Dy * Dy >= 9)
                    {
                        DrawPixelBlend(Output.DisplayRgba8, DepthBuffer, W, H, Cx + Dx, Cy + Dy, 0.0f, 255u, 184u, 72u, 1.0f);
                    }
                }
            }
        }
    }

    const auto EndClock = std::chrono::steady_clock::now();
    Output.RenderMillisec = std::chrono::duration<float, std::milli>(EndClock - StartClock).count();
    return Output;
}

FractureRayHitResult FractureViewportRenderer::PickViewportSurface(
    const GeometrySpace::Fracture::FracturePartitionResult*      Partition,
    const GeometrySpace::Fracture::ConvexPolyhedron*             IntactFallbackSolid,
    const SimulationSpace::PhysicsSpace::FractureSimulationRunner* SimRunner,
    const FractureViewportCamera&                                Camera,
    const FractureRenderOptions&                                 Options,
    float                                                        NormalizedX,
    float                                                        NormalizedY,
    uint32_t                                                     ViewportWidthPx,
    uint32_t                                                     ViewportHeightPx) noexcept
{
    const CameraBasis Cam = BuildCameraBasis(Camera, ViewportWidthPx, ViewportHeightPx);
    const float NdcX = 2.0f * std::clamp(NormalizedX, 0.0f, 1.0f) - 1.0f;
    const float NdcY = 1.0f - 2.0f * std::clamp(NormalizedY, 0.0f, 1.0f);
    const auto RayDir = Mathematics::NormalizeAxis(
        Cam.Forward + Cam.Right * (NdcX * Cam.TanHalfFovY * Cam.Aspect) + Cam.Up * (NdcY * Cam.TanHalfFovY));

    FractureRayHitResult Best {};
    Best.DistanceM = std::numeric_limits<float>::infinity();

    auto IntersectTri = [&](Mathematics::Axis3D P0, Mathematics::Axis3D P1, Mathematics::Axis3D P2,
                            Mathematics::Axis3D FaceNormal, int32_t FragIdx)
    {
        const auto E1 = P1 - P0;
        const auto E2 = P2 - P0;
        const auto Pvec = Mathematics::EvaluateCross(RayDir, E2);
        const float Det = Mathematics::EvaluateDot(E1, Pvec);
        if (std::abs(Det) < 1.0e-7f) return;
        const float InvDet = 1.0f / Det;
        const auto  Tvec   = Cam.Eye - P0;
        const float U      = Mathematics::EvaluateDot(Tvec, Pvec) * InvDet;
        if (U < 0.0f || U > 1.0f) return;
        const auto  Qvec   = Mathematics::EvaluateCross(Tvec, E1);
        const float V      = Mathematics::EvaluateDot(RayDir, Qvec) * InvDet;
        if (V < 0.0f || U + V > 1.0f) return;
        const float T      = Mathematics::EvaluateDot(E2, Qvec) * InvDet;
        if (T > 0.05f && T < Best.DistanceM)
        {
            Best.Hit           = true;
            Best.FragmentIndex = FragIdx;
            Best.DistanceM     = T;
            Best.HitPointWorld = Cam.Eye + RayDir * T;
            Best.HitNormalWorld = Mathematics::EvaluateDot(FaceNormal, RayDir) < 0.0f ? FaceNormal : (FaceNormal * -1.0f);
        }
    };

    if (Partition != nullptr && !Partition->Fragments.empty())
    {
        const auto GroupCentroid = Partition->SourceMetrics.Centroid;
        for (const auto& Frag : Partition->Fragments)
        {
            if (!Frag.VisibilityEnabled) continue;
            Mathematics::Axis3D FragOrigin {};
            SimulationSpace::PhysicsSpace::RigidOrientationQuaternion Quat {};
            Mathematics::Axis3D ExplodeOffset {};
            ComputeFragmentWorldTransform(Frag, GroupCentroid, SimRunner, Options, FragOrigin, Quat, ExplodeOffset);

            for (const auto& Face : Frag.Polyhedron.Faces)
            {
                if (Face.Indices.size() < 3u) continue;
                const auto LocalN = GeometrySpace::Fracture::ConvexPartitioning::ComputeFaceNormal(Frag.Polyhedron, Face);
                const auto WorldN = SimulationSpace::PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(Quat, LocalN);
                const auto P0 = TransformLocalToWorld(Frag.Polyhedron.Vertices[Face.Indices[0]], Frag.Centroid, FragOrigin, Quat, ExplodeOffset);
                for (size_t K = 1u; K + 1u < Face.Indices.size(); ++K)
                {
                    const auto P1 = TransformLocalToWorld(Frag.Polyhedron.Vertices[Face.Indices[K]],      Frag.Centroid, FragOrigin, Quat, ExplodeOffset);
                    const auto P2 = TransformLocalToWorld(Frag.Polyhedron.Vertices[Face.Indices[K + 1u]], Frag.Centroid, FragOrigin, Quat, ExplodeOffset);
                    IntersectTri(P0, P1, P2, WorldN, static_cast<int32_t>(Frag.Index));
                }
            }
        }
    }
    else if (IntactFallbackSolid != nullptr)
    {
        for (const auto& Face : IntactFallbackSolid->Faces)
        {
            if (Face.Indices.size() < 3u) continue;
            const auto N = GeometrySpace::Fracture::ConvexPartitioning::ComputeFaceNormal(*IntactFallbackSolid, Face);
            const auto P0 = IntactFallbackSolid->Vertices[Face.Indices[0]] + Options.SolidWorldOffsetM;
            for (size_t K = 1u; K + 1u < Face.Indices.size(); ++K)
            {
                const auto P1 = IntactFallbackSolid->Vertices[Face.Indices[K]]      + Options.SolidWorldOffsetM;
                const auto P2 = IntactFallbackSolid->Vertices[Face.Indices[K + 1u]] + Options.SolidWorldOffsetM;
                IntersectTri(P0, P1, P2, N, -1);
            }
        }
    }

    return Best;
}

bool FractureViewportRenderer::ProjectWorldToViewport(
    const FractureViewportCamera& Camera,
    Mathematics::Axis3D           WorldPoint,
    uint32_t                      ViewportWidthPx,
    uint32_t                      ViewportHeightPx,
    float&                        OutScreenX,
    float&                        OutScreenY,
    float&                        OutViewDepth) noexcept
{
    const CameraBasis Cam = BuildCameraBasis(Camera, ViewportWidthPx, ViewportHeightPx);
    return ProjectWorldPoint(Cam, WorldPoint, ViewportWidthPx, ViewportHeightPx, OutScreenX, OutScreenY, OutViewDepth);
}

void FractureViewportRenderer::SampleEnergySwatchSrgb(
    float    NormalizedStress,
    uint8_t& OutR,
    uint8_t& OutG,
    uint8_t& OutB) noexcept
{
    const auto Lin = EvaluateEnergyHeatmapLinear(NormalizedStress);
    OutR = LinearToSrgb8(Lin.R);
    OutG = LinearToSrgb8(Lin.G);
    OutB = LinearToSrgb8(Lin.B);
}

void FractureViewportRenderer::SampleCellSwatchSrgb(
    uint32_t CellIndex,
    uint8_t& OutR,
    uint8_t& OutG,
    uint8_t& OutB) noexcept
{
    const auto Lin = EvaluateGoldenCellLinear(CellIndex);
    OutR = LinearToSrgb8(Lin.R);
    OutG = LinearToSrgb8(Lin.G);
    OutB = LinearToSrgb8(Lin.B);
}

} // namespace Frontier::Workspaces::Fracture
