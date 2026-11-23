//============================================================================================================================================
//                                                       WORKSPACESCENEVERTEX.SLANG
//============================================================================================================================================
// 🧩 Scene mesh vertex expansion for imported polygon objects. Triangles read document-resolved material slots;
//    points mode expands each source vertex into a small viewport-space marker.

#include "Shared/ToolchainInterchange.slang.h"
#include "Shared/OverlayTransform.slang.h"

namespace Slate
{

static const Unsigned32 SceneViewLit              = 0u;
static const Unsigned32 SceneViewMatcap           = 1u;
static const Unsigned32 SceneViewSourceWire       = 2u;
static const Unsigned32 SceneViewTriangulatedWire = 3u;
static const Unsigned32 SceneViewPoints           = 4u;
static const Unsigned32 SceneViewNormal           = 5u;
static const Unsigned32 SceneViewMetallic         = 6u;
static const Unsigned32 SceneViewIllumination     = 7u;

struct SceneTriangleRecord
{
    Real32 Position[9];
    Real32x4 Colour;
    Unsigned32 MaterialSlot;
    Real32 Padding[2];
};

struct PhysicalSurfacePacket
{
    Real32x4 Albedo;
    Real32x4 NormalIncidenceReflectance;
    Real32x4 Scalars;
    Real32 RefractionRatio;
    Unsigned32 ActiveChannelMask;
    Unsigned32 Closure;
    Unsigned32 Features;
    Unsigned32 Coverage;
    Unsigned32 Wall;
    Unsigned32 Interface;
    Unsigned32 TwoSided;
    Unsigned32 Reserved;
    Real32 RegisterPadding[3];
};

struct SceneMaterialRecord
{
    PhysicalSurfacePacket Surface;
    Unsigned32 DirtyLow;
    Unsigned32 DirtyHigh;
    Unsigned32 Reserved[2];
};

struct ScenePushConstant
{
    Unsigned32 ViewMode;
    Unsigned32 MaterialCount;
    Real32 DisplayWidth;
    Real32 DisplayHeight;
    Real32x4 ViewProjection0;
    Real32x4 ViewProjection1;
    Real32x4 ViewProjection2;
    Real32x4 ViewProjection3;
};

[[vk::push_constant]]
ConstantBuffer<ScenePushConstant> Push;

[[vk::binding(0, 0)]] StructuredBuffer<SceneTriangleRecord> SceneTriangles;
[[vk::binding(1, 0)]] StructuredBuffer<SceneMaterialRecord> SceneMaterials;

struct WorkspaceSceneVertexOutput
{
    Real32x4 Position : SV_Position;
    Real32x3 World    : TEXCOORD0;
    Real32x3 Normal   : TEXCOORD1;
    Real32x3 Bary     : TEXCOORD2;
    Real32x4 RowColour: COLOR0;
    Unsigned32 MaterialSlot : TEXCOORD3;
    Unsigned32 ViewMode : TEXCOORD4;
};

Real32x3 TriangleCorner(SceneTriangleRecord Triangle, Unsigned32 Corner)
{
    const Unsigned32 Base = Corner * 3u;
    return Real32x3(Triangle.Position[Base + 0u], Triangle.Position[Base + 1u], Triangle.Position[Base + 2u]);
}

Real32x4 ProjectWorld(Real32x3 World)
{
    const Real32x4 Input = Real32x4(World, 1.0);
    return Real32x4(dot(Push.ViewProjection0, Input), dot(Push.ViewProjection1, Input),
                    dot(Push.ViewProjection2, Input), dot(Push.ViewProjection3, Input));
}

Unsigned32 QuadCorner(Unsigned32 VertexIndex)
{
    const Unsigned32 Within = VertexIndex % 6u;
    if (Within == 0u) return 0u;
    if (Within == 1u) return 1u;
    if (Within == 2u) return 2u;
    if (Within == 3u) return 2u;
    if (Within == 4u) return 1u;
    return 3u;
}

[shader("vertex")]
WorkspaceSceneVertexOutput main(Unsigned32 VertexIndex : SV_VertexID)
{
    WorkspaceSceneVertexOutput Output;
    Output.Position = Real32x4(-2.0, -2.0, 0.0, 1.0);
    Output.World = Real32x3(0.0, 0.0, 0.0);
    Output.Normal = Real32x3(0.0, 1.0, 0.0);
    Output.Bary = Real32x3(1.0, 0.0, 0.0);
    Output.RowColour = Real32x4(1.0, 1.0, 1.0, 1.0);
    Output.MaterialSlot = 0u;
    Output.ViewMode = Push.ViewMode;

    const bool Points = Push.ViewMode == SceneViewPoints;
    const Unsigned32 TriangleIndex = Points ? (VertexIndex / 18u) : (VertexIndex / 3u);
    const SceneTriangleRecord Triangle = SceneTriangles[TriangleIndex];
    const Real32x3 A = TriangleCorner(Triangle, 0u);
    const Real32x3 B = TriangleCorner(Triangle, 1u);
    const Real32x3 C = TriangleCorner(Triangle, 2u);
    const Real32x3 FaceNormal = normalize(cross(B - A, C - A));

    Output.Normal = FaceNormal;
    Output.RowColour = Triangle.Colour;
    Output.MaterialSlot = Triangle.MaterialSlot;

    if (Points)
    {
        const Unsigned32 PointInTriangle = (VertexIndex / 6u) % 3u;
        const Unsigned32 Corner = QuadCorner(VertexIndex);
        const Real32 LocalX = ((Corner == 1u) || (Corner == 3u)) ? 1.0 : -1.0;
        const Real32 LocalY = Corner >= 2u ? 1.0 : -1.0;
        Output.World = PointInTriangle == 0u ? A : PointInTriangle == 1u ? B : C;
        const Real32x4 Clip = ProjectWorld(Output.World);
        const Real32x2 Ndc = Clip.xy / max(0.0001, Clip.w);
        const Real32x2 Pixel = Real32x2(LocalX * 3.0 / max(1.0, Push.DisplayWidth),
                                        LocalY * 3.0 / max(1.0, Push.DisplayHeight));
        Output.Position = Real32x4(Ndc + Pixel, Clip.z / max(0.0001, Clip.w), 1.0);
        return Output;
    }

    const Unsigned32 Corner = VertexIndex % 3u;
    Output.World = Corner == 0u ? A : Corner == 1u ? B : C;
    Output.Bary = Corner == 0u ? Real32x3(1.0, 0.0, 0.0)
                : Corner == 1u ? Real32x3(0.0, 1.0, 0.0)
                                : Real32x3(0.0, 0.0, 1.0);
    Output.Position = ProjectWorld(Output.World);
    return Output;
}

} // namespace Slate
