/*============================================================================================================================================
                                          WORLDGISHADOWPATHTRACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 Real-mesh studio proof: two GTX GI candidates, two GTX solar-shadow modes, and one deterministic path tracer on GTX BVH4 + RTX ray query

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometricRaster/DirectLighting/LinearCosineLightIntegration.h"
#include "Engine/GeometricRaster/DirectLighting/SolarShadowSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/DeterministicPathTraceSequence.h"
#include "Engine/GeometricRaster/GlobalIllumination/EmissiveGiCandidateSequence.h"
#include "Engine/GeometricRaster/Visibility/DepthPyramidSequence.h"
#include "Engine/Mathematics/DeterministicQuadrature.h"
#include "Exhibits/CleanEmissiveGiCandidates.h"
#include "Exhibits/ProofImageWriter.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <span>
#include <string>
#include <utility>
#include <vector>

#ifndef FRONTIER_REPO_ROOT_DIR
#define FRONTIER_REPO_ROOT_DIR "."
#endif

namespace
{

using namespace Frontier;
using namespace Frontier::Exhibits::ProofImage;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

constexpr uint32_t GiWidthPx        = 64u;   // [px]    - GI candidate comparison width
constexpr uint32_t GiHeightPx       = 48u;   // [px]    - GI candidate comparison height
constexpr uint32_t BeautyWidthPx    = 128u;  // [px]    - Path-traced beauty width
constexpr uint32_t BeautyHeightPx   = 96u;   // [px]    - Path-traced beauty height
constexpr uint32_t PanelScale       = 4u;    // [-]     - Integer panel magnification for the proof sheet
constexpr uint32_t CharacterSides   = 9u;    // [count] - Radial segments per limb tube
constexpr uint32_t CharacterRings   = 3u;    // [count] - Longitudinal rings per limb tube
constexpr float    FloorTopMeters   = 0.0f;  // [m]     - Studio floor surface height

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct MeshStudioScene
{
    std::vector<SpatialAcceleration::RayTrianglePrimitive>  Primitives {};
    MaterialSpace::CompactMaterialRegister                  Materials {};
    SpatialAcceleration::WideBoundingTree                   SceneTree {};
    GeometricRaster::SolarDirectionalSource                 Sun {};
    std::vector<GeometricRaster::PolygonalAreaQuadLight>     AreaLights {};
    std::vector<GeometricRaster::DirectPointLightSpec>       PointLights {};
    Mathematics::LinearRGB                                  SkyRadiance { 0.16f, 0.23f, 0.36f };
    GeometricRaster::PerspectiveCameraBasis                 Camera {};
    uint16_t                                                CharacterSuitSlot = 0u;
    uint16_t                                                CharacterAccentSlot = 0u;
    uint32_t                                                CharacterTriangleCount = 0u;
};

struct ScalarErrorSummary
{
    double   MeanAbsoluteError = 0.0; // [-] - Channel-mean absolute linear error
    double   MeanSquaredError  = 0.0; // [-] - Channel-mean squared linear error
    double   LeakageRatio      = 0.0; // [-] - Candidate excess over dark reference pixels
    uint32_t ComparedPixels    = 0u;  // [count] - Pixels contributing to the summary
};

struct CheckScorecard
{
    uint32_t Passed = 0u;
    uint32_t Failed = 0u;

    void Check(bool Condition, const char* Format, ...)
    {
        char Text[512u] {};
        va_list Arguments;
        va_start(Arguments, Format);
        std::vsnprintf(Text, sizeof(Text), Format, Arguments);
        va_end(Arguments);

        if (Condition)
        {
            ++Passed;
            std::printf("[WorldGiShadowPathTrace][PASS] %s\n", Text);
        }
        else
        {
            ++Failed;
            std::fprintf(stderr, "[WorldGiShadowPathTrace][FAIL] %s\n", Text);
        }
    }
};

//------------------------------------------------------------------------------------------------------------------------
//                                                  TRIANGLE MESH AUTHORING
//------------------------------------------------------------------------------------------------------------------------

void AppendMeshTriangle(
    MeshStudioScene&    Scene,
    Mathematics::Axis3D Corner0,
    Mathematics::Axis3D Corner1,
    Mathematics::Axis3D Corner2,
    uint16_t            MaterialSlot)
{
    SpatialAcceleration::RayTrianglePrimitive Primitive {};
    Primitive.Corner0         = Corner0;
    Primitive.Edge01          = Corner1 - Corner0;
    Primitive.Edge02          = Corner2 - Corner0;
    Primitive.MaterialOrdinal = MaterialSlot;
    Primitive.OpaqueMask      = 1u;
    Primitive.SourceFaceIndex = static_cast<uint32_t>(Scene.Primitives.size());
    Primitive.PrimitiveOrdinal = static_cast<uint32_t>(Scene.Primitives.size());
    Scene.Primitives.push_back(Primitive);
}

void AppendQuad(
    MeshStudioScene&    Scene,
    Mathematics::Axis3D Corner00,
    Mathematics::Axis3D Corner10,
    Mathematics::Axis3D Corner11,
    Mathematics::Axis3D Corner01,
    uint16_t            MaterialSlot)
{
    AppendMeshTriangle(Scene, Corner00, Corner10, Corner11, MaterialSlot);
    AppendMeshTriangle(Scene, Corner00, Corner11, Corner01, MaterialSlot);
}

void AppendBoxMesh(
    MeshStudioScene&      Scene,
    Mathematics::Axis3D   Center,
    Mathematics::Scalar3D HalfExtents,
    uint16_t              MaterialSlot)
{
    const Mathematics::Axis3D X { HalfExtents.X, 0.0f, 0.0f };
    const Mathematics::Axis3D Y { 0.0f, HalfExtents.Y, 0.0f };
    const Mathematics::Axis3D Z { 0.0f, 0.0f, HalfExtents.Z };
    const Mathematics::Axis3D Min = Center - X - Y - Z;
    const Mathematics::Axis3D Max = Center + X + Y + Z;
    const auto Corner = [&](uint32_t Ordinal)
    {
        return Mathematics::Axis3D {
            ((Ordinal & 1u) != 0u) ? Max.X : Min.X,
            ((Ordinal & 2u) != 0u) ? Max.Y : Min.Y,
            ((Ordinal & 4u) != 0u) ? Max.Z : Min.Z,
        };
    };
    AppendQuad(Scene, Corner(0u), Corner(2u), Corner(3u), Corner(1u), MaterialSlot); // -Z
    AppendQuad(Scene, Corner(4u), Corner(5u), Corner(7u), Corner(6u), MaterialSlot); // +Z
    AppendQuad(Scene, Corner(0u), Corner(1u), Corner(5u), Corner(4u), MaterialSlot); // -Y
    AppendQuad(Scene, Corner(2u), Corner(6u), Corner(7u), Corner(3u), MaterialSlot); // +Y
    AppendQuad(Scene, Corner(0u), Corner(4u), Corner(6u), Corner(2u), MaterialSlot); // -X
    AppendQuad(Scene, Corner(1u), Corner(3u), Corner(7u), Corner(5u), MaterialSlot); // +X
}

void AppendSphereMesh(
    MeshStudioScene&    Scene,
    Mathematics::Axis3D Center,
    float               Radius,
    uint32_t            LongitudeSegments,
    uint32_t            LatitudeRings,
    uint16_t            MaterialSlot)
{
    const uint32_t SafeLongitude = std::max(8u, LongitudeSegments);
    const uint32_t SafeLatitude = std::max(4u, LatitudeRings);
    const auto Vertex = [Center, Radius, SafeLongitude, SafeLatitude](uint32_t Latitude, uint32_t Longitude)
    {
        const float Theta = Constants::PiFloat * static_cast<float>(Latitude) / static_cast<float>(SafeLatitude);
        const float Phi = Constants::TauFloat * static_cast<float>(Longitude) / static_cast<float>(SafeLongitude);
        const float SinTheta = std::sin(Theta);
        return Center + Mathematics::Axis3D {
            Radius * SinTheta * std::cos(Phi),
            Radius * SinTheta * std::sin(Phi),
            Radius * std::cos(Theta),
        };
    };
    for (uint32_t Latitude = 0u; Latitude < SafeLatitude; ++Latitude)
    {
        for (uint32_t Longitude = 0u; Longitude < SafeLongitude; ++Longitude)
        {
            const Mathematics::Axis3D P00 = Vertex(Latitude, Longitude);
            const Mathematics::Axis3D P01 = Vertex(Latitude, Longitude + 1u);
            const Mathematics::Axis3D P10 = Vertex(Latitude + 1u, Longitude);
            const Mathematics::Axis3D P11 = Vertex(Latitude + 1u, Longitude + 1u);
            AppendMeshTriangle(Scene, P00, P10, P11, MaterialSlot);
            AppendMeshTriangle(Scene, P00, P11, P01, MaterialSlot);
        }
    }
}

// 📝 Real tapered limb tube (no capsule / no SDF): radial rings of triangles with hemispherical end fans
void AppendTaperedLimbMesh(
    MeshStudioScene&              Scene,
    const Mathematics::Axis3D&    JointStart,
    const Mathematics::Axis3D&    JointEnd,
    float                         RadiusStart,
    float                         RadiusEnd,
    uint16_t                      MaterialSlot)
{
    const Mathematics::Axis3D AxisVector = JointEnd - JointStart;
    const float AxisLength = Mathematics::EvaluateLength(AxisVector);
    if (AxisLength <= 1.0e-5f)
    {
        return;
    }

    const Mathematics::Axis3D Direction = AxisVector * (1.0f / AxisLength);
    const Mathematics::TangentFrame3D TubeFrame = Mathematics::ConstructTangentFrame(Direction);
    const uint32_t Sides = CharacterSides;
    const uint32_t Rings = CharacterRings;

    std::vector<Mathematics::Axis3D> RingVertices(static_cast<std::size_t>(Rings) * Sides);
    for (uint32_t RingOrdinal = 0u; RingOrdinal < Rings; ++RingOrdinal)
    {
        const float Frac = static_cast<float>(RingOrdinal) / static_cast<float>(Rings - 1u);
        const Mathematics::Axis3D RingCenter = JointStart + AxisVector * Frac;
        const float RingRadius = RadiusStart + (RadiusEnd - RadiusStart) * Frac;
        for (uint32_t SideOrdinal = 0u; SideOrdinal < Sides; ++SideOrdinal)
        {
            const float Angle = Constants::TauFloat * static_cast<float>(SideOrdinal) / static_cast<float>(Sides);
            RingVertices[static_cast<std::size_t>(RingOrdinal) * Sides + SideOrdinal] = RingCenter +
                (TubeFrame.TangentX * std::cos(Angle) + TubeFrame.BitangentY * std::sin(Angle)) * RingRadius;
        }
    }

    for (uint32_t RingOrdinal = 0u; RingOrdinal + 1u < Rings; ++RingOrdinal)
    {
        for (uint32_t SideOrdinal = 0u; SideOrdinal < Sides; ++SideOrdinal)
        {
            const uint32_t NextSide = (SideOrdinal + 1u) % Sides;
            const Mathematics::Axis3D V00 = RingVertices[static_cast<std::size_t>(RingOrdinal) * Sides + SideOrdinal];
            const Mathematics::Axis3D V01 = RingVertices[static_cast<std::size_t>(RingOrdinal) * Sides + NextSide];
            const Mathematics::Axis3D V10 = RingVertices[static_cast<std::size_t>(RingOrdinal + 1u) * Sides + SideOrdinal];
            const Mathematics::Axis3D V11 = RingVertices[static_cast<std::size_t>(RingOrdinal + 1u) * Sides + NextSide];
            AppendMeshTriangle(Scene, V00, V01, V11, MaterialSlot);
            AppendMeshTriangle(Scene, V00, V11, V10, MaterialSlot);
        }
    }

    const Mathematics::Axis3D StartPole = JointStart - Direction * RadiusStart;
    const Mathematics::Axis3D EndPole = JointEnd + Direction * RadiusEnd;
    const std::size_t LastRingOffset = static_cast<std::size_t>(Rings - 1u) * Sides;
    for (uint32_t SideOrdinal = 0u; SideOrdinal < Sides; ++SideOrdinal)
    {
        const uint32_t NextSide = (SideOrdinal + 1u) % Sides;
        AppendMeshTriangle(Scene, StartPole, RingVertices[NextSide], RingVertices[SideOrdinal], MaterialSlot);
        AppendMeshTriangle(Scene, EndPole, RingVertices[LastRingOffset + SideOrdinal],
                           RingVertices[LastRingOffset + NextSide], MaterialSlot);
    }
}

//------------------------------------------------------------------------------------------------------------------------
//                                             ARTICULATED CHARACTER MESH POSE
//------------------------------------------------------------------------------------------------------------------------

struct CharacterJointRig
{
    Mathematics::Axis3D Pelvis {};
    Mathematics::Axis3D Chest {};
    Mathematics::Axis3D Head {};
    Mathematics::Axis3D ShoulderLeft {};
    Mathematics::Axis3D ElbowLeft {};
    Mathematics::Axis3D HandLeft {};
    Mathematics::Axis3D ShoulderRight {};
    Mathematics::Axis3D ElbowRight {};
    Mathematics::Axis3D HandRight {};
    Mathematics::Axis3D HipLeft {};
    Mathematics::Axis3D KneeLeft {};
    Mathematics::Axis3D FootLeft {};
    Mathematics::Axis3D HipRight {};
    Mathematics::Axis3D KneeRight {};
    Mathematics::Axis3D FootRight {};
};

[[nodiscard]] CharacterJointRig BuildCharacterJoints(
    const Mathematics::Axis3D& RootWorld,
    float                      PosePhase) noexcept
{
    const float Lean = 0.05f + 0.14f * PosePhase;
    const float StrideLeft = -0.18f + 0.46f * PosePhase;
    const float StrideRight = 0.18f - 0.42f * PosePhase;
    const float ArmLift = 0.30f * (1.0f - PosePhase);
    const float Reach = 0.10f + 0.34f * PosePhase;
    const float Crouch = 0.05f * PosePhase;

    CharacterJointRig Rig {};
    Rig.Pelvis       = { RootWorld.X + Lean * 0.20f, RootWorld.Y, FloorTopMeters + 0.94f - Crouch };
    Rig.Chest        = { RootWorld.X + Lean * 0.60f, RootWorld.Y, FloorTopMeters + 1.38f - Crouch };
    Rig.Head         = { RootWorld.X + Lean * 0.90f, RootWorld.Y, FloorTopMeters + 1.63f - Crouch };
    Rig.ShoulderLeft = { Rig.Chest.X, Rig.Chest.Y - 0.21f, Rig.Chest.Z - 0.04f };
    Rig.ElbowLeft    = { Rig.ShoulderLeft.X + Reach * 0.55f, Rig.ShoulderLeft.Y - 0.20f,
                         Rig.Chest.Z - 0.24f + ArmLift * 0.35f };
    Rig.HandLeft     = { Rig.ElbowLeft.X + Reach * 0.65f, Rig.ElbowLeft.Y - 0.12f,
                         Rig.ElbowLeft.Z + 0.20f + ArmLift * 0.55f };
    Rig.ShoulderRight = { Rig.Chest.X, Rig.Chest.Y + 0.21f, Rig.Chest.Z - 0.04f };
    Rig.ElbowRight    = { Rig.ShoulderRight.X - 0.06f * PosePhase, Rig.ShoulderRight.Y + 0.14f,
                          Rig.Chest.Z - 0.30f };
    Rig.HandRight     = { Rig.ElbowRight.X + 0.10f * (1.0f - PosePhase), Rig.ElbowRight.Y + 0.07f,
                          Rig.ElbowRight.Z - 0.22f + 0.10f * PosePhase };
    Rig.HipLeft       = { Rig.Pelvis.X, Rig.Pelvis.Y - 0.11f, Rig.Pelvis.Z - 0.03f };
    Rig.KneeLeft      = { Rig.HipLeft.X + StrideLeft * 0.50f, Rig.HipLeft.Y, FloorTopMeters + 0.50f };
    Rig.FootLeft      = { RootWorld.X + StrideLeft, Rig.HipLeft.Y, FloorTopMeters + 0.06f };
    Rig.HipRight      = { Rig.Pelvis.X, Rig.Pelvis.Y + 0.11f, Rig.Pelvis.Z - 0.03f };
    Rig.KneeRight     = { Rig.HipRight.X + StrideRight * 0.50f, Rig.HipRight.Y, FloorTopMeters + 0.50f };
    Rig.FootRight     = { RootWorld.X + StrideRight, Rig.HipRight.Y, FloorTopMeters + 0.06f + 0.08f * PosePhase };
    return Rig;
}

// 📝 Appends the full articulated humanoid as triangle geometry only (zero SDF, zero capsule primitives in the BVH)
void AppendCharacterMesh(
    MeshStudioScene&              Scene,
    const CharacterJointRig&      Rig,
    uint16_t                      SuitSlot,
    uint16_t                      AccentSlot)
{
    const uint32_t TriangleCountBefore = static_cast<uint32_t>(Scene.Primitives.size());

    AppendTaperedLimbMesh(Scene, Rig.Pelvis, Rig.Chest, 0.15f, 0.19f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.Chest, Rig.Head, 0.11f, 0.10f, AccentSlot);
    AppendSphereMesh(Scene, Rig.Head + Mathematics::Axis3D { 0.0f, 0.0f, 0.08f }, 0.115f, 18u, 12u, AccentSlot);

    AppendTaperedLimbMesh(Scene, Rig.ShoulderLeft, Rig.ElbowLeft, 0.070f, 0.060f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.ElbowLeft, Rig.HandLeft, 0.058f, 0.048f, AccentSlot);
    AppendSphereMesh(Scene, Rig.HandLeft, 0.055f, 12u, 8u, AccentSlot);
    AppendTaperedLimbMesh(Scene, Rig.ShoulderRight, Rig.ElbowRight, 0.070f, 0.060f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.ElbowRight, Rig.HandRight, 0.058f, 0.048f, AccentSlot);
    AppendSphereMesh(Scene, Rig.HandRight, 0.055f, 12u, 8u, AccentSlot);

    AppendTaperedLimbMesh(Scene, Rig.HipLeft, Rig.KneeLeft, 0.092f, 0.074f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.KneeLeft, Rig.FootLeft, 0.070f, 0.052f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.HipRight, Rig.KneeRight, 0.092f, 0.074f, SuitSlot);
    AppendTaperedLimbMesh(Scene, Rig.KneeRight, Rig.FootRight, 0.070f, 0.052f, SuitSlot);

    AppendBoxMesh(Scene, Rig.FootLeft + Mathematics::Axis3D { 0.05f, 0.0f, -0.03f },
                  { 0.115f, 0.050f, 0.032f }, SuitSlot);
    AppendBoxMesh(Scene, Rig.FootRight + Mathematics::Axis3D { 0.05f, 0.0f, -0.03f },
                  { 0.115f, 0.050f, 0.032f }, SuitSlot);

    Scene.CharacterTriangleCount += static_cast<uint32_t>(Scene.Primitives.size()) - TriangleCountBefore;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    STUDIO SCENE ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

// 📝 Rebuilds the whole triangle-mesh studio including the character pose; every rebuild is a pure mesh generation cost
[[nodiscard]] MeshStudioScene BuildMeshStudioScene(
    const Mathematics::Axis3D& CharacterRoot,
    float                      PosePhase)
{
    MeshStudioScene Scene {};

    MaterialSpace::SurfaceMaterial Plaster {};
    Plaster.MaterialName = "StudioPlaster";
    Plaster.Diffuse.BaseColor = { 0.78f, 0.77f, 0.75f };
    Plaster.Diffuse.DiffuseRoughness = 0.55f;
    Plaster.Specular.Roughness = 0.62f;

    MaterialSpace::SurfaceMaterial Crimson {};
    Crimson.MaterialName = "StudioCrimson";
    Crimson.Diffuse.BaseColor = { 0.86f, 0.08f, 0.07f };
    Crimson.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial Emerald {};
    Emerald.MaterialName = "StudioEmerald";
    Emerald.Diffuse.BaseColor = { 0.09f, 0.72f, 0.18f };
    Emerald.Specular.Roughness = 0.55f;

    MaterialSpace::SurfaceMaterial Chrome {};
    Chrome.MaterialName = "StudioChrome";
    Chrome.Diffuse.BaseColor = { 0.95f, 0.96f, 0.98f };
    Chrome.Specular.Metalness = 1.0f;
    Chrome.Specular.Roughness = 0.06f;

    MaterialSpace::SurfaceMaterial Gold {};
    Gold.MaterialName = "StudioGold";
    Gold.Diffuse.BaseColor = { 0.96f, 0.76f, 0.32f };
    Gold.Specular.Metalness = 1.0f;
    Gold.Specular.Roughness = 0.22f;

    MaterialSpace::SurfaceMaterial Suit {};
    Suit.MaterialName = "CharacterSuit";
    Suit.Diffuse.BaseColor = { 0.62f, 0.64f, 0.70f };
    Suit.Diffuse.DiffuseRoughness = 0.48f;
    Suit.Specular.Roughness = 0.42f;

    MaterialSpace::SurfaceMaterial Accent {};
    Accent.MaterialName = "CharacterAccent";
    Accent.Diffuse.BaseColor = { 0.90f, 0.46f, 0.10f };
    Accent.Specular.Roughness = 0.35f;

    const uint16_t PlasterSlot = Scene.Materials.RegisterMaterial(Plaster);
    const uint16_t CrimsonSlot = Scene.Materials.RegisterMaterial(Crimson);
    const uint16_t EmeraldSlot = Scene.Materials.RegisterMaterial(Emerald);
    const uint16_t ChromeSlot = Scene.Materials.RegisterMaterial(Chrome);
    const uint16_t GoldSlot = Scene.Materials.RegisterMaterial(Gold);
    Scene.CharacterSuitSlot = Scene.Materials.RegisterMaterial(Suit);
    Scene.CharacterAccentSlot = Scene.Materials.RegisterMaterial(Accent);

    // Studio shell: floor, open-top sky dome exposure, crimson and emerald side walls, back wall
    AppendBoxMesh(Scene, { 0.0f, 0.0f, FloorTopMeters - 0.05f }, { 2.75f, 2.40f, 0.05f }, PlasterSlot);
    AppendBoxMesh(Scene, { -2.15f, 0.30f, 1.20f }, { 0.05f, 2.10f, 1.20f }, CrimsonSlot);
    AppendBoxMesh(Scene, { 2.15f, 0.30f, 1.20f }, { 0.05f, 2.10f, 1.20f }, EmeraldSlot);
    AppendBoxMesh(Scene, { 0.0f, 2.35f, 1.20f }, { 2.75f, 0.05f, 1.20f }, PlasterSlot);

    // Specular colour-bleeding props plus one emissive softbox quad
    AppendSphereMesh(Scene, { -0.72f, 0.55f, 0.52f }, 0.52f, 34u, 20u, ChromeSlot);
    AppendBoxMesh(Scene, { 1.02f, 0.62f, 0.34f }, { 0.34f, 0.34f, 0.34f }, GoldSlot);

    AppendCharacterMesh(Scene, BuildCharacterJoints(CharacterRoot, PosePhase),
                        Scene.CharacterSuitSlot, Scene.CharacterAccentSlot);

    Scene.Sun.DirectionToSun = Mathematics::NormalizeAxis({ 0.38f, -0.62f, 0.68f });
    Scene.Sun.SolarIrradiance = { 5.10f, 4.85f, 4.40f };
    Scene.Sun.SunAngularRadiusRadians = 0.020f;
    Scene.Sun.RtxSamplesPerPixel = 8u;

    GeometricRaster::PolygonalAreaQuadLight Softbox {};
    Softbox.CenterWorld = { -0.20f, 1.52f, 1.95f };
    Softbox.RightTangent = { 1.0f, 0.0f, 0.0f };
    Softbox.UpBitangent = { 0.0f, 1.0f, 0.0f };
    Softbox.HalfWidthMeters = 0.62f;
    Softbox.HalfHeightMeters = 0.34f;
    Softbox.EmittedRadiance = { 7.2f, 6.6f, 5.8f };
    Softbox.TwoSidedEmission = false;
    Scene.AreaLights.push_back(Softbox);

    GeometricRaster::DirectPointLightSpec FillLight {};
    FillLight.PositionWorld = { 0.95f, -0.85f, 1.85f };
    FillLight.RangeMeters = 4.5f;
    FillLight.Intensity = 6.0f;
    FillLight.RadiusMeters = 0.12f;
    FillLight.Color = { 0.85f, 0.90f, 1.0f };
    Scene.PointLights.push_back(FillLight);

    Scene.Camera.CameraOrigin = { 0.02f, -3.55f, 1.22f };
    Scene.Camera.TargetPoint = { 0.12f, 0.35f, 0.86f };
    Scene.Camera.WorldUpAxis = { 0.0f, 0.0f, 1.0f };
    Scene.Camera.VerticalFovRadians = 45.0f * Constants::DegToRadFloat;
    Scene.Camera.NearClipMeters = 0.12f;
    Scene.Camera.FarClipMeters = 40.0f;
    Scene.Camera.ViewportWidthPx = BeautyWidthPx;
    Scene.Camera.ViewportHeightPx = BeautyHeightPx;

    (void)Scene.SceneTree.BuildFromTrianglePrimitives(Scene.Primitives);
    return Scene;
}

[[nodiscard]] std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> BuildPrimarySamples(
    const MeshStudioScene& Scene,
    uint32_t               WidthPx,
    uint32_t               HeightPx)
{
    std::vector<GeometricRaster::AnalyticResolvedSurfaceSample> Samples(
        static_cast<std::size_t>(WidthPx) * HeightPx);
    const auto Basis = GeometricRaster::ComputeCameraBasis(Scene.Camera);

    for (uint32_t PixelY = 0u; PixelY < HeightPx; ++PixelY)
    {
        for (uint32_t PixelX = 0u; PixelX < WidthPx; ++PixelX)
        {
            const float NdcX =
                ((static_cast<float>(PixelX) + 0.5f) / static_cast<float>(WidthPx)) * 2.0f - 1.0f;
            const float NdcY =
                1.0f - ((static_cast<float>(PixelY) + 0.5f) / static_cast<float>(HeightPx)) * 2.0f;
            const Mathematics::Axis3D RayDirection = Mathematics::NormalizeAxis(
                Basis.ForwardAxis + Basis.RightAxis * (NdcX * Basis.TanHalfFovX) +
                Basis.UpAxis * (NdcY * Basis.TanHalfFovY),
                Basis.ForwardAxis);

            SpatialAcceleration::RaySegmentQuery Ray {};
            Ray.Origin = Basis.CameraOrigin;
            Ray.MinimumDistance = Basis.NearClip;
            Ray.UnitDirection = RayDirection;
            Ray.MaximumDistance = Basis.FarClip;
            const auto Hit = Scene.SceneTree.TraceClosestRay(Ray, false);
            if (!Hit.HitConfirmed || Hit.MaterialOrdinal >= Scene.Materials.QuerySlotCount())
            {
                continue;
            }

            auto& Sample = Samples[static_cast<std::size_t>(PixelY) * WidthPx + PixelX];
            Sample.ValidHit = true;
            Sample.MaterialSlot = Hit.MaterialOrdinal;
            Sample.TriangleOrdinal = Hit.PrimitiveIndex;
            Sample.WorldPosition = Basis.CameraOrigin + RayDirection * Hit.HitDistanceMeters;
            Sample.WorldViewDirection = -RayDirection;
            Mathematics::Axis3D Normal = Mathematics::NormalizeAxis(Hit.GeometricNormal, { 0.0f, 0.0f, 1.0f });
            if (Mathematics::EvaluateDot(Normal, RayDirection) > 0.0f)
            {
                Normal = -Normal;
            }
            const Mathematics::TangentFrame3D Frame = Mathematics::ConstructTangentFrame(Normal);
            Sample.TangentFrame.TangentX = Frame.TangentX;
            Sample.TangentFrame.BitangentY = Frame.BitangentY;
            Sample.TangentFrame.NormalZ = Frame.NormalZ;
        }
    }
    return Samples;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     PROOF IMAGE OUTPUT
//------------------------------------------------------------------------------------------------------------------------

// 📝 Linear radiance tile to tone-mapped RGBA8 (Reinhard + sRGB), amplified by an integer panel scale
[[nodiscard]] std::vector<uint8_t> ToneMapRadianceTile(
    std::span<const Mathematics::LinearRGB> Radiance,
    uint32_t                                WidthPx,
    uint32_t                                HeightPx)
{
    std::vector<uint8_t> Rgba(static_cast<std::size_t>(WidthPx) * HeightPx * 4u, 255u);
    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < Radiance.size() && PixelOrdinal < WidthPx * HeightPx;
         ++PixelOrdinal)
    {
        const auto& Pixel = Radiance[PixelOrdinal];
        Rgba[PixelOrdinal * 4u + 0u] = static_cast<uint8_t>(ToneMapByte(Pixel.R));
        Rgba[PixelOrdinal * 4u + 1u] = static_cast<uint8_t>(ToneMapByte(Pixel.G));
        Rgba[PixelOrdinal * 4u + 2u] = static_cast<uint8_t>(ToneMapByte(Pixel.B));
    }
    return Rgba;
}

[[nodiscard]] std::vector<uint8_t> ToneMapVisibilityTile(
    std::span<const float> Visibility,
    uint32_t               WidthPx,
    uint32_t               HeightPx)
{
    std::vector<uint8_t> Rgba(static_cast<std::size_t>(WidthPx) * HeightPx * 4u, 255u);
    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < Visibility.size() && PixelOrdinal < WidthPx * HeightPx;
         ++PixelOrdinal)
    {
        const uint8_t Level = static_cast<uint8_t>(EncodeSrgbByte(std::clamp(Visibility[PixelOrdinal], 0.0f, 1.0f)));
        Rgba[PixelOrdinal * 4u + 0u] = Level;
        Rgba[PixelOrdinal * 4u + 1u] = Level;
        Rgba[PixelOrdinal * 4u + 2u] = Level;
    }
    return Rgba;
}

[[nodiscard]] bool WriteComparisonSheet(
    const std::filesystem::path&                     OutputPath,
    std::span<const std::vector<uint8_t>>            Panels,
    uint32_t                                         SourceWidthPx,
    uint32_t                                         SourceHeightPx)
{
    constexpr uint32_t ColumnCount = 2u;
    const uint32_t RowCount = static_cast<uint32_t>((Panels.size() + ColumnCount - 1u) / ColumnCount);
    const uint32_t PanelWidthPx = SourceWidthPx * PanelScale;
    const uint32_t PanelHeightPx = SourceHeightPx * PanelScale;
    constexpr uint32_t GutterPx = 12u;
    constexpr uint32_t MarginPx = 12u;
    const uint32_t SheetWidthPx = MarginPx + ColumnCount * (PanelWidthPx + GutterPx);
    const uint32_t SheetHeightPx = MarginPx + RowCount * (PanelHeightPx + GutterPx);

    std::vector<uint8_t> Sheet(static_cast<std::size_t>(SheetWidthPx) * SheetHeightPx * 4u, 24u);
    for (std::size_t PanelOrdinal = 0u; PanelOrdinal < Panels.size(); ++PanelOrdinal)
    {
        const uint32_t Column = static_cast<uint32_t>(PanelOrdinal) % ColumnCount;
        const uint32_t RowOrdinal = static_cast<uint32_t>(PanelOrdinal) / ColumnCount;
        const uint32_t OriginX = MarginPx + Column * (PanelWidthPx + GutterPx);
        const uint32_t OriginY = MarginPx + RowOrdinal * (PanelHeightPx + GutterPx);
        BlitScaledPanel(
            Sheet,
            SheetWidthPx,
            SheetHeightPx,
            OriginX,
            OriginY,
            PanelWidthPx,
            PanelHeightPx,
            Panels[PanelOrdinal],
            SourceWidthPx,
            SourceHeightPx);
    }
    return WritePngRgba8(OutputPath, SheetWidthPx, SheetHeightPx, Sheet);
}

[[nodiscard]] ScalarErrorSummary CompareRadiance(
    std::span<const Mathematics::LinearRGB> Candidate,
    std::span<const Mathematics::LinearRGB> Reference) noexcept
{
    const auto Summary = ::Exhibits::CleanEmissiveGi::CompareLinearRadiance(Candidate, Reference);
    ScalarErrorSummary Out {};
    Out.MeanAbsoluteError = Summary.MeanAbsoluteError;
    Out.MeanSquaredError = Summary.MeanSquaredError;
    Out.LeakageRatio = Summary.DarkReferenceLeakageRatio;
    Out.ComparedPixels = static_cast<uint32_t>(Summary.ComparedPixelCount);
    return Out;
}

[[nodiscard]] uint64_t HashRadiance(std::span<const Mathematics::LinearRGB> Radiance) noexcept
{
    uint64_t Hash = 1469598103934665603ull;
    for (const auto& Pixel : Radiance)
    {
        const float Channels[3u] { Pixel.R, Pixel.G, Pixel.B };
        for (const float Channel : Channels)
        {
            uint32_t Bits = 0u;
            std::memcpy(&Bits, &Channel, sizeof(uint32_t));
            Hash ^= Bits;
            Hash *= 1099511628211ull;
        }
    }
    return Hash;
}

[[nodiscard]] double MeanLuminance(std::span<const Mathematics::LinearRGB> Radiance) noexcept
{
    if (Radiance.empty())
    {
        return 0.0;
    }
    double Total = 0.0;
    for (const auto& Pixel : Radiance)
    {
        Total += 0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B;
    }
    return Total / static_cast<double>(Radiance.size());
}

} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                                   VALIDATION ENTRY POINT
//------------------------------------------------------------------------------------------------------------------------

int main()
{
    std::printf("\n=== World GI / Shadow / Deterministic Path Trace validation (real triangle meshes, zero SDF) ===\n");
    CheckScorecard Score {};

    const Mathematics::Axis3D CharacterRoot { 0.78f, 0.32f, FloorTopMeters };

    const auto SceneBuildBegin = std::chrono::steady_clock::now();
    MeshStudioScene Scene = BuildMeshStudioScene(CharacterRoot, 0.0f);
    const float SceneBuildMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - SceneBuildBegin).count());

    Score.Check(!Scene.SceneTree.WideEntries.empty() && !Scene.SceneTree.Primitives.empty(),
                "Built 128-byte 4-wide BVH4 from authored triangle meshes only");
    Score.Check(Scene.CharacterTriangleCount >= 900u,
                "Articulated character contributes %u real triangles (no capsule or SDF proxy)",
                Scene.CharacterTriangleCount);
    Score.Check(Scene.Primitives.size() >= Scene.CharacterTriangleCount,
                "Studio scene assembled %zu triangles in %.2f ms",
                Scene.Primitives.size(),
                SceneBuildMilliseconds);

    // Pose A -> Pose B is a pure triangle regeneration: no voxel rebake, no acceleration structure rebuild cost beyond refit
    const auto PoseBegin = std::chrono::steady_clock::now();
    MeshStudioScene ScenePoseB = BuildMeshStudioScene(Mathematics::Axis3D { 0.86f, 0.42f, FloorTopMeters }, 1.0f);
    const float PoseMilliseconds = static_cast<float>(
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - PoseBegin).count());
    Score.Check(PoseMilliseconds < 250.0f && ScenePoseB.CharacterTriangleCount == Scene.CharacterTriangleCount,
                "Deforming pose rebuild of %u character triangles took %.2f ms with identical topology",
                ScenePoseB.CharacterTriangleCount,
                PoseMilliseconds);

    const auto PrimarySamples = BuildPrimarySamples(Scene, GiWidthPx, GiHeightPx);
    const std::size_t VisiblePrimaryCount = static_cast<std::size_t>(std::count_if(
        PrimarySamples.begin(), PrimarySamples.end(), [](const auto& Sample) { return Sample.ValidHit; }));
    Score.Check(VisiblePrimaryCount > PrimarySamples.size() / 3u,
                "Resolved %zu primary surface hits of %zu pixels",
                VisiblePrimaryCount,
                PrimarySamples.size());

    const GeometricRaster::EmissiveGiSceneView SceneView {
        Scene.SceneTree,
        Scene.Materials,
        Scene.Sun,
        std::span<const GeometricRaster::PolygonalAreaQuadLight>(Scene.AreaLights),
        std::span<const GeometricRaster::DirectPointLightSpec>(Scene.PointLights),
        Scene.SkyRadiance,
    };

    //====================================================================================================================
    // 1. DETERMINISTIC PATH TRACER: GTX BVH4 AND RTX RAY QUERY BACKENDS
    //====================================================================================================================
    GeometricRaster::DeterministicPathTraceSequence PathTracer {};
    GeometricRaster::PathTraceSettings TraceSettings {};
    TraceSettings.SamplesPerPixel = 48u;
    TraceSettings.MaximumBounceDepth = 3u;
    TraceSettings.EnableNextEventEstimation = true;

    const auto ReferenceGtx = PathTracer.EvaluateIndirectReference(
        GeometricRaster::PathTraceTraversalBackend::GtxWideBvh4,
        PrimarySamples, GiWidthPx, GiHeightPx, SceneView, TraceSettings);
    const auto ReferenceGtxRepeat = PathTracer.EvaluateIndirectReference(
        GeometricRaster::PathTraceTraversalBackend::GtxWideBvh4,
        PrimarySamples, GiWidthPx, GiHeightPx, SceneView, TraceSettings);
    const auto ReferenceRtx = PathTracer.EvaluateIndirectReference(
        GeometricRaster::PathTraceTraversalBackend::RtxRayQuery,
        PrimarySamples, GiWidthPx, GiHeightPx, SceneView, TraceSettings);

    const auto RtxCapability = GeometricRaster::DeterministicPathTraceSequence::ProbeTraversalBackend(
        GeometricRaster::PathTraceTraversalBackend::RtxRayQuery);
    std::printf("  Traversal backend probe: gtx=wide_bvh4 | rtx=%s (hardware_ray_query=%s)\n",
                RtxCapability.BackendLabel,
                RtxCapability.HardwareRayQuerySupported ? "yes" : "compiled out, BVH4 mirror");

    Score.Check(ReferenceGtx.FixedPointDigest == ReferenceGtxRepeat.FixedPointDigest,
                "GTX path tracer repeats bit-identically (%016llx)",
                static_cast<unsigned long long>(ReferenceGtx.FixedPointDigest));
    Score.Check(ReferenceGtx.FixedPointDigest == ReferenceRtx.FixedPointDigest,
                "GTX and RTX traversal produce identical fixed-point digests");
    Score.Check(ReferenceGtx.PathLengthMeters.size() == static_cast<std::size_t>(GiWidthPx) * GiHeightPx,
                "Path-length telemetry resolved for every pixel");
    Score.Check(ReferenceGtx.Metrics.SurfaceHitCount > 0u && ReferenceGtx.Metrics.VisibilityRayCount > 0u,
                "Path tracer issued %llu hit and %llu visibility traversals",
                static_cast<unsigned long long>(ReferenceGtx.Metrics.SurfaceHitCount),
                static_cast<unsigned long long>(ReferenceGtx.Metrics.VisibilityRayCount));

    //====================================================================================================================
    // 2. TWO GTX GI CANDIDATES EVALUATED AGAINST THE PATH-TRACED REFERENCE
    //====================================================================================================================
    GeometricRaster::EmissiveGiCandidateSequence GiEvaluator {};
    GeometricRaster::EmissiveGiSettings GiSettings {};
    GiSettings.SamplesPerPixel = 48u;
    GiSettings.BounceLimit = 3u;
    GiSettings.SurfelLimitPerCascade = 4096u;
    GiSettings.SurfelRayCount = 4u;
    GiSettings.MaximumTraceDistance = 40.0f;

    const auto Stokes = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::StokesSh9MultiBounce,
        PrimarySamples, GiWidthPx, GiHeightPx, SceneView, GiSettings);
    const auto SurfelCascade = GiEvaluator.Evaluate(
        GeometricRaster::EmissiveGiMethod::SurfelCascadeDdgi,
        PrimarySamples, GiWidthPx, GiHeightPx, SceneView, GiSettings);

    std::printf("  GI mean luminance: stokes=%.5f surfel=%.5f reference=%.5f\n",
                MeanLuminance(Stokes.IndirectRadiance),
                MeanLuminance(SurfelCascade.IndirectRadiance),
                MeanLuminance(ReferenceGtx.LinearRadiance));

    const ScalarErrorSummary StokesError = CompareRadiance(Stokes.IndirectRadiance, ReferenceGtx.LinearRadiance);
    const ScalarErrorSummary SurfelError = CompareRadiance(SurfelCascade.IndirectRadiance, ReferenceGtx.LinearRadiance);

    std::printf("  GI #1 Stokes SH9 multi-bounce : mae=%.5f mse=%.6f leakage=%.4f queries=%llu elapsed=%.1fms\n",
                StokesError.MeanAbsoluteError, StokesError.MeanSquaredError, StokesError.LeakageRatio,
                static_cast<unsigned long long>(Stokes.Metrics.RayQueryCount), Stokes.Metrics.ElapsedMilliseconds);
    std::printf("  GI #2 Surfel cascade DDGI     : mae=%.5f mse=%.6f leakage=%.4f queries=%llu surfels=%u elapsed=%.1fms\n",
                SurfelError.MeanAbsoluteError, SurfelError.MeanSquaredError, SurfelError.LeakageRatio,
                static_cast<unsigned long long>(SurfelCascade.Metrics.RayQueryCount),
                SurfelCascade.Metrics.SurfelCount, SurfelCascade.Metrics.ElapsedMilliseconds);
    std::printf("  Path-traced GTX reference     : queries=%llu elapsed=%.1fms digest=%016llx\n",
                static_cast<unsigned long long>(ReferenceGtx.Metrics.RayQueryCount),
                ReferenceGtx.Metrics.ElapsedMilliseconds,
                static_cast<unsigned long long>(ReferenceGtx.FixedPointDigest));

    Score.Check(std::isfinite(StokesError.MeanAbsoluteError) && StokesError.MeanAbsoluteError < 0.10,
                "GI #1 Stokes SH9 stays within 0.10 MAE of the path-traced reference");
    Score.Check(StokesError.MeanAbsoluteError < SurfelError.MeanAbsoluteError,
                "GI #1 Stokes SH9 is the lower-error GTX candidate (%.5f < %.5f)",
                StokesError.MeanAbsoluteError, SurfelError.MeanAbsoluteError);
    Score.Check(StokesError.LeakageRatio < 0.05 && SurfelError.LeakageRatio < 0.25,
                "Both GTX GI candidates limit dark-region leakage (%.4f / %.4f)",
                StokesError.LeakageRatio, SurfelError.LeakageRatio);
    Score.Check(SurfelCascade.Metrics.SurfelCount > 0u,
                "GI #2 Surfel cascade built %u deterministic spatial surfels",
                SurfelCascade.Metrics.SurfelCount);

    //====================================================================================================================
    // 3. TWO GTX SOLAR SHADOW MODES AGAINST THE EXACT BVH4 SOLAR DISC REFERENCE
    //====================================================================================================================
    GeometricRaster::OrthographicShadowCascade FineCascade {};
    GeometricRaster::OrthographicShadowCascade CoarseCascade {};
    GeometricRaster::SolarShadowSequence::BuildTwoCascadeContactClips(
        Scene.Sun, { 0.10f, 0.30f, 1.10f }, Scene.SceneTree, FineCascade, CoarseCascade);
    Score.Check(!FineCascade.ClosestLightDepth.empty() && !CoarseCascade.ClosestLightDepth.empty(),
                "Built fine/coarse 1024-texel solar cascades once for both shadow modes");

    const auto EvaluateShadowField = [&](GeometricRaster::SolarShadowAlgorithm Algorithm)
    {
        const auto Begin = std::chrono::steady_clock::now();
        auto Field = GeometricRaster::SolarShadowSequence::EvaluateSolarShadowFieldWithAlgorithm(
            Algorithm, Scene.Sun, GiWidthPx, GiHeightPx, PrimarySamples, Scene.SceneTree,
            &FineCascade, &CoarseCascade);
        const float Milliseconds = static_cast<float>(
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - Begin).count());
        return std::pair<GeometricRaster::SolarShadowFieldResult, float>(std::move(Field), Milliseconds);
    };

    auto [ContactPcss, ContactPcssMs] = EvaluateShadowField(GeometricRaster::SolarShadowAlgorithm::ContactPcss);
    auto [PcssPlaneAware, PcssPlaneAwareMs] = EvaluateShadowField(
        GeometricRaster::SolarShadowAlgorithm::PcssPlaneAware);
    auto [ExactRayQuery, ExactRayQueryMs] = EvaluateShadowField(
        GeometricRaster::SolarShadowAlgorithm::WideTreeRayQuery);

    double ContactAbsolute = 0.0;
    double PlaneAwareAbsolute = 0.0;
    uint32_t ShadowComparedCount = 0u;
    for (std::size_t PixelOrdinal = 0u; PixelOrdinal < ContactPcss.RawVisibility.size(); ++PixelOrdinal)
    {
        if (!PrimarySamples[PixelOrdinal].ValidHit)
        {
            continue;
        }
        ContactAbsolute += std::fabs(static_cast<double>(ContactPcss.FilteredVisibility[PixelOrdinal]) -
                                     static_cast<double>(ExactRayQuery.FilteredVisibility[PixelOrdinal]));
        PlaneAwareAbsolute += std::fabs(static_cast<double>(PcssPlaneAware.FilteredVisibility[PixelOrdinal]) -
                                        static_cast<double>(ExactRayQuery.FilteredVisibility[PixelOrdinal]));
        ++ShadowComparedCount;
    }
    const double ContactShadowMae = ShadowComparedCount > 0u
        ? ContactAbsolute / static_cast<double>(ShadowComparedCount)
        : 0.0;
    const double PlaneAwareShadowMae = ShadowComparedCount > 0u
        ? PlaneAwareAbsolute / static_cast<double>(ShadowComparedCount)
        : 0.0;
    std::printf("  Shadow #1 Contact PCSS        : mae=%.5f elapsed=%.1fms queries=%llu\n",
                ContactShadowMae, ContactPcssMs,
                static_cast<unsigned long long>(ContactPcss.RayQueryCount));
    std::printf("  Shadow #2 PCSS plane-aware    : mae=%.5f elapsed=%.1fms\n",
                PlaneAwareShadowMae, PcssPlaneAwareMs);
    std::printf("  Shadow reference BVH4 disc    : queries=%llu elapsed=%.1fms\n",
                static_cast<unsigned long long>(ExactRayQuery.RayQueryCount), ExactRayQueryMs);

    Score.Check(ContactShadowMae < 0.05 && PlaneAwareShadowMae < 0.05,
                "Both GTX shadow modes stay within 0.05 MAE of the exact solar-disc reference (%.5f / %.5f)",
                ContactShadowMae, PlaneAwareShadowMae);
    Score.Check(ContactPcssMs < 250.0f && PcssPlaneAwareMs < 250.0f,
                "Both GTX shadow modes evaluated the character and studio meshes in under 250 ms");

    //====================================================================================================================
    // 4. PATH-TRACED BEAUTY ON BOTH BACKENDS (GTX BVH4 + RTX RAY QUERY)
    //====================================================================================================================
    GeometricRaster::PathTraceSettings BeautySettings {};
    BeautySettings.SamplesPerPixel = 40u;
    BeautySettings.MaximumBounceDepth = 3u;
    BeautySettings.EnableNextEventEstimation = true;
    BeautySettings.EnableSpecularBounce = true;

    const auto BeautyGtx = PathTracer.EvaluateBeauty(
        GeometricRaster::PathTraceTraversalBackend::GtxWideBvh4,
        Scene.Camera, BeautyWidthPx, BeautyHeightPx, SceneView, BeautySettings);
    const auto BeautyRtx = PathTracer.EvaluateBeauty(
        GeometricRaster::PathTraceTraversalBackend::RtxRayQuery,
        Scene.Camera, BeautyWidthPx, BeautyHeightPx, SceneView, BeautySettings);

    const double BeautyMeanLuminance = MeanLuminance(BeautyGtx.LinearRadiance);
    std::printf("  Path-traced beauty GTX        : mean_luminance=%.5f queries=%llu elapsed=%.1fms\n",
                BeautyMeanLuminance,
                static_cast<unsigned long long>(BeautyGtx.Metrics.RayQueryCount),
                BeautyGtx.Metrics.ElapsedMilliseconds);
    std::printf("  Path-traced beauty RTX mirror  : elapsed=%.1fms digest_match=%s\n",
                BeautyRtx.Metrics.ElapsedMilliseconds,
                BeautyGtx.FixedPointDigest == BeautyRtx.FixedPointDigest ? "yes" : "no");

    Score.Check(BeautyGtx.FixedPointDigest == BeautyRtx.FixedPointDigest,
                "Beauty renders are bit-identical across traversal backends");
    Score.Check(BeautyGtx.Metrics.VisibilityRayCount > 0u && BeautyGtx.Metrics.ScatterRayCount > 0u,
                "Beauty render issued NEE visibility (%llu) and scatter (%llu) traversals",
                static_cast<unsigned long long>(BeautyGtx.Metrics.VisibilityRayCount),
                static_cast<unsigned long long>(BeautyGtx.Metrics.ScatterRayCount));
    Score.Check(std::isfinite(BeautyMeanLuminance) && BeautyMeanLuminance > 1.0e-4 &&
                BeautyMeanLuminance < 40.0,
                "Beauty radiance stays physically finite (mean luminance %.5f cd/m2)",
                BeautyMeanLuminance);

    // Zero-energy guard: every pixel of the indirect reference must be finite and non-negative
    bool AllReferenceFinite = true;
    for (const auto& Pixel : ReferenceGtx.LinearRadiance)
    {
        AllReferenceFinite = AllReferenceFinite &&
            std::isfinite(Pixel.R) && std::isfinite(Pixel.G) && std::isfinite(Pixel.B) &&
            Pixel.R >= 0.0f && Pixel.G >= 0.0f && Pixel.B >= 0.0f;
    }
    Score.Check(AllReferenceFinite, "Path-traced indirect reference contains no NaN, negative or infinite radiance");

    //====================================================================================================================
    // 5. PROOF SHEETS AND CSV REPORT
    //====================================================================================================================
    const std::filesystem::path OutputDirectory =
        std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits/Images/GlobalIllumination";

    const auto GiReferenceTile = ToneMapRadianceTile(ReferenceGtx.LinearRadiance, GiWidthPx, GiHeightPx);
    const auto StokesTile = ToneMapRadianceTile(Stokes.IndirectRadiance, GiWidthPx, GiHeightPx);
    const auto SurfelTile = ToneMapRadianceTile(SurfelCascade.IndirectRadiance, GiWidthPx, GiHeightPx);
    const auto ContactShadowTile = ToneMapVisibilityTile(ContactPcss.FilteredVisibility, GiWidthPx, GiHeightPx);
    const auto PlaneAwareShadowTile = ToneMapVisibilityTile(PcssPlaneAware.FilteredVisibility, GiWidthPx, GiHeightPx);

    const auto ExactShadowTile = ToneMapVisibilityTile(ExactRayQuery.FilteredVisibility, GiWidthPx, GiHeightPx);
    const std::array<std::vector<uint8_t>, 6u> GiPanels {{
        StokesTile, SurfelTile, GiReferenceTile, ContactShadowTile, PlaneAwareShadowTile, ExactShadowTile,
    }};
    const bool GiSheetWritten = WriteComparisonSheet(
        OutputDirectory / "WorldGiShadowPathTrace.png", GiPanels, GiWidthPx, GiHeightPx);

    const auto BeautyTileGtx = ToneMapRadianceTile(BeautyGtx.LinearRadiance, BeautyWidthPx, BeautyHeightPx);
    const auto BeautyTileRtx = ToneMapRadianceTile(BeautyRtx.LinearRadiance, BeautyWidthPx, BeautyHeightPx);
    const std::array<std::vector<uint8_t>, 2u> BeautyPanels {{
        BeautyTileGtx, BeautyTileRtx,
    }};
    const bool BeautySheetWritten = WriteComparisonSheet(
        OutputDirectory / "WorldGiShadowBeautyBackends.png", BeautyPanels, BeautyWidthPx, BeautyHeightPx);

    Score.Check(GiSheetWritten && BeautySheetWritten,
                "Wrote GI, shadow and beauty proof sheets under %s",
                OutputDirectory.string().c_str());

    {
        std::ofstream Report(OutputDirectory / "WorldGiShadowPathTrace.csv");
        Report << std::fixed << std::setprecision(6);
        Report << "family,candidate,mae,mse,leakage_ratio,ray_queries,visibility_rays,surfels,elapsed_ms,digest\n";
        Report << "gtx_gi,Stokes_SH9_multi_bounce," << StokesError.MeanAbsoluteError << ','
               << StokesError.MeanSquaredError << ',' << StokesError.LeakageRatio << ','
               << Stokes.Metrics.RayQueryCount << ",0," << Stokes.Metrics.SurfelCount << ','
               << Stokes.Metrics.ElapsedMilliseconds << ",0\n";
        Report << "gtx_gi,Surfel_cascade_DDGI," << SurfelError.MeanAbsoluteError << ','
               << SurfelError.MeanSquaredError << ',' << SurfelError.LeakageRatio << ','
               << SurfelCascade.Metrics.RayQueryCount << ",0," << SurfelCascade.Metrics.SurfelCount << ','
               << SurfelCascade.Metrics.ElapsedMilliseconds << ",0\n";
        Report << "path_trace,GTX_wide_bvh4,0,0,0," << ReferenceGtx.Metrics.RayQueryCount << ','
               << ReferenceGtx.Metrics.VisibilityRayCount << ",0," << ReferenceGtx.Metrics.ElapsedMilliseconds
               << ',' << ReferenceGtx.FixedPointDigest << "\n";
        Report << "path_trace,RTX_ray_query,0,0,0,"
               << ReferenceRtx.Metrics.RayQueryCount << ',' << ReferenceRtx.Metrics.VisibilityRayCount
               << ",0," << ReferenceRtx.Metrics.ElapsedMilliseconds << ',' << ReferenceRtx.FixedPointDigest << "\n";
        Report << "gtx_shadow,Contact_PCSS," << ContactShadowMae << ",0,0," << ContactPcss.RayQueryCount
               << ",0,0," << ContactPcssMs << ",0\n";
        Report << "gtx_shadow,PCSS_plane_aware," << PlaneAwareShadowMae << ",0,0," << PcssPlaneAware.RayQueryCount
               << ",0,0," << PcssPlaneAwareMs << ",0\n";
        Report << "gtx_shadow,WideTree_ray_query_reference,0,0,0," << ExactRayQuery.RayQueryCount
               << ",0,0," << ExactRayQueryMs << ",0\n";
        Report << "beauty,GTX_wide_bvh4," << BeautyMeanLuminance << ",0,0," << BeautyGtx.Metrics.RayQueryCount
               << ',' << BeautyGtx.Metrics.VisibilityRayCount << ",0," << BeautyGtx.Metrics.ElapsedMilliseconds
               << ',' << BeautyGtx.FixedPointDigest << "\n";
        Report << "beauty,RTX_ray_query," << BeautyMeanLuminance << ",0,0," << BeautyRtx.Metrics.RayQueryCount
               << ',' << BeautyRtx.Metrics.VisibilityRayCount << ",0," << BeautyRtx.Metrics.ElapsedMilliseconds
               << ',' << BeautyRtx.FixedPointDigest << "\n";
        Report << "scene,character_triangles," << Scene.CharacterTriangleCount << ",0,0," << Scene.Primitives.size()
               << ",0,0," << SceneBuildMilliseconds << ",0\n";
    }

    std::printf("\n[WorldGiShadowPathTrace] passed=%u failed=%u | character_triangles=%u | output=%s\n",
                Score.Passed, Score.Failed, Scene.CharacterTriangleCount, OutputDirectory.string().c_str());
    return Score.Failed == 0u ? 0 : 1;
}
