struct GatherUniforms { width: u32, height: u32, surfelCount: u32, radius: f32 };
struct ScreenSurfel { originArea: vec4<f32>, normal: vec4<f32>, irradiance: vec4<f32> };
@group(0) @binding(0) var baseTexture: texture_2d<f32>;
@group(0) @binding(1) var normalTexture: texture_2d<f32>;
@group(0) @binding(2) var positionTexture: texture_2d<f32>;
@group(0) @binding(3) var linearSampler: sampler;
@group(0) @binding(4) var<storage, read> surfels: array<ScreenSurfel>;
@group(0) @binding(5) var<uniform> settings: GatherUniforms;
@group(0) @binding(6) var outputTexture: texture_storage_2d<rgba16float, write>;
@compute @workgroup_size(8, 8) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    if (gid.x >= settings.width || gid.y >= settings.height) { return; }
    let pixel = vec2<i32>(i32(gid.x), i32(gid.y));
    let dims = vec2<f32>(f32(settings.width), f32(settings.height));
    let uv = (vec2<f32>(gid.xy) + vec2<f32>(0.5)) / dims;
    let base = textureLoad(baseTexture, pixel, 0);
    let normal = normalize(textureLoad(normalTexture, pixel, 0).xyz);
    let position = textureLoad(positionTexture, pixel, 0).xyz;
    var indirect = vec3<f32>(0.0);
    var weightSum = 0.0;
    let count = min(settings.surfelCount, 1024u);
    for (var i = 0u; i < count; i = i + 1u) {
        let sample = surfels[i];
        let delta = sample.originArea.xyz - position;
        let distance = length(delta);
        if (distance > settings.radius) { continue; }
        let planeWeight = exp(-distance * 2.8) * pow(max(dot(normal, sample.normal.xyz), 0.0), 8.0);
        indirect += sample.irradiance.xyz * planeWeight;
        weightSum += planeWeight;
    }
    if (weightSum > 1.0e-5) { indirect /= weightSum; }
    textureStore(outputTexture, pixel, vec4<f32>(base.rgb * (0.08 + indirect), base.a));
}
