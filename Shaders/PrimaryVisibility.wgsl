struct CameraUniforms {
    viewProjection: mat4x4<f32>,
    eyeExposure: vec4<f32>,
    sunDirection: vec4<f32>,
    sunRadiance: vec4<f32>,
    viewportTime: vec4<f32>,
};
struct VertexInput {
    @location(0) position: vec3<f32>,
    @location(1) normal: vec3<f32>,
    @location(2) albedo: vec3<f32>,
    @location(3) glow: vec3<f32>,
    @location(4) finish: vec2<f32>,
};
struct VertexOutput {
    @builtin(position) clipPosition: vec4<f32>,
    @location(0) worldPosition: vec3<f32>,
    @location(1) worldNormal: vec3<f32>,
    @location(2) albedo: vec3<f32>,
    @location(3) glow: vec3<f32>,
    @location(4) finish: vec2<f32>,
    @location(5) barycentric: vec3<f32>,
};
struct FragmentOutput {
    @location(0) baseColor: vec4<f32>,
    @location(1) surfaceNormal: vec4<f32>,
    @location(2) positionDepth: vec4<f32>,
    @location(3) radiance: vec4<f32>,
};
@group(0) @binding(0) var<uniform> camera: CameraUniforms;
@vertex fn vertexMain(input: VertexInput, @builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    output.clipPosition = camera.viewProjection * vec4<f32>(input.position, 1.0);
    output.worldPosition = input.position;
    output.worldNormal = normalize(input.normal);
    output.albedo = input.albedo;
    output.glow = input.glow;
    output.finish = input.finish;
    output.barycentric = select(select(vec3<f32>(0.0,0.0,1.0),vec3<f32>(0.0,1.0,0.0),vertexIndex%3u==1u),vec3<f32>(1.0,0.0,0.0),vertexIndex%3u==0u);
    return output;
}
@fragment fn fragmentMain(input: VertexOutput) -> FragmentOutput {
    var output: FragmentOutput;
    output.baseColor = vec4<f32>(input.albedo, input.finish.x);
    output.surfaceNormal = vec4<f32>(normalize(input.worldNormal), input.finish.y);
    output.positionDepth = vec4<f32>(input.worldPosition, input.barycentric.x);
    output.radiance = vec4<f32>(input.glow, input.barycentric.y);
    return output;
}
