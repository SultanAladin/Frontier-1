//============================================================================================================================================
//                                                      WORKSPACESCENEFRAGMENT.SLANG
//============================================================================================================================================
// 🧩 Constant/fill material preview for imported scene meshes: lit, matcap, wire, points and diagnostic channels
//    route through the same material packet instead of hard-coded row colours.

#include "Shared/ToolchainInterchange.slang.h"

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

[[vk::binding(1, 0)]] StructuredBuffer<SceneMaterialRecord> SceneMaterials;

Real32 EdgeFactor(Real32x3 Bary)
{
    const Real32 Edge = min(Bary.x, min(Bary.y, Bary.z));
    return smoothstep(0.0, 0.018, Edge);
}

Real32x3 MatcapShade(Real32x3 Normal)
{
    const Real32x3 N = normalize(Normal);
    const Real32 Rim = pow(1.0 - saturate(N.z * 0.5 + 0.5), 2.0);
    const Real32 Key = saturate(N.y * 0.45 + N.z * 0.35 + 0.55);
    return Real32x3(0.58, 0.60, 0.64) * Key + Real32x3(0.18, 0.20, 0.24) * Rim;
}

[shader("fragment")]
Real32x4 main(WorkspaceSceneVertexOutput Input) : SV_Target
{
    const SceneMaterialRecord Material = SceneMaterials[Input.MaterialSlot];
    const PhysicalSurfacePacket Surface = Material.Surface;
    const Real32x3 Albedo = Surface.Albedo.rgb * Input.RowColour.rgb;
    const Real32 Opacity = saturate(Surface.Scalars.z * Input.RowColour.a);
    const Real32 Metallic = saturate(Surface.Scalars.x);
    const Real32 Roughness = saturate(Surface.Scalars.y);
    const Real32x3 Normal = normalize(Input.Normal);

    if (Input.ViewMode == SceneViewPoints)
        return Real32x4(Albedo, Opacity);

    if (Input.ViewMode == SceneViewSourceWire || Input.ViewMode == SceneViewTriangulatedWire)
    {
        const Real32 Interior = EdgeFactor(Input.Bary);
        const Real32x3 Wire = Input.ViewMode == SceneViewSourceWire ? Real32x3(0.05, 0.08, 0.12)
                                                                    : Real32x3(0.92, 0.94, 0.98);
        return Real32x4(lerp(Wire, Albedo * 0.42, Interior), Opacity);
    }

    if (Input.ViewMode == SceneViewNormal)
        return Real32x4(Normal * 0.5 + 0.5, Opacity);

    if (Input.ViewMode == SceneViewMetallic)
        return Real32x4(Metallic, Metallic, Metallic, Opacity);

    if (Input.ViewMode == SceneViewIllumination)
        return Real32x4(Surface.NormalIncidenceReflectance.rgb, Opacity);

    if (Input.ViewMode == SceneViewMatcap)
        return Real32x4(Albedo * MatcapShade(Normal), Opacity);

    const Real32x3 LightDirection = normalize(Real32x3(0.35, 0.82, 0.44));
    const Real32 Diffuse = saturate(dot(Normal, LightDirection));
    const Real32 Ambient = 0.24;
    const Real32 Specular = pow(saturate(dot(reflect(-LightDirection, Normal), normalize(Real32x3(0.0, 0.0, 1.0)))),
                                lerp(96.0, 8.0, Roughness));
    const Real32x3 SpecularColour = lerp(Surface.NormalIncidenceReflectance.rgb, Albedo, Metallic);
    return Real32x4(Albedo * (Ambient + Diffuse * 0.76) + SpecularColour * Specular * 0.2, Opacity);
}

} // namespace Slate
