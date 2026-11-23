// SurfaceRaster.slang - the viewport's first unit body: Proving Grounds geometry into the HDR surface.
// Compiled by the SDK build (slangc -> SPIR-V); not compiled in the portable sandbox.
// Draws the cube + ground plane from Engine/Scene/ProvingGrounds with one fixed key light.

struct FrameUniforms
{
    float4x4 ModelToWorld;
    float4x4 ViewProjection;
};

[[vk::binding(0, 0)]]
ConstantBuffer<FrameUniforms> Frame;

struct VertexInput
{
    float3 Position : POSITION;
    float3 Normal   : NORMAL;
    float3 Colour   : COLOR;
};

struct VertexOutput
{
    float4 ClipPosition : SV_Position;
    float3 Normal       : NORMAL;
    float3 Colour       : COLOR;
};

[shader("vertex")]
VertexOutput SurfaceVertex(VertexInput Input)
{
    VertexOutput Output;
    float4 World       = mul(Frame.ModelToWorld, float4(Input.Position, 1.0));
    Output.ClipPosition = mul(Frame.ViewProjection, World);
    Output.Normal       = normalize(mul(Frame.ModelToWorld, float4(Input.Normal, 0.0)).xyz);
    Output.Colour       = Input.Colour;
    return Output;
}

[shader("fragment")]
float4 SurfaceFragment(VertexOutput Input) : SV_Target
{
    float3 LightDirection = normalize(float3(0.45, 0.8, 0.35));
    float  Diffuse        = saturate(dot(normalize(Input.Normal), LightDirection));
    float3 Radiance       = Input.Colour * (0.18 + 0.95 * Diffuse);
    return float4(Radiance, 1.0);
}
