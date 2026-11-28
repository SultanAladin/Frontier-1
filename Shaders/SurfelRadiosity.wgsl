struct SurfelSample { originArea: vec4<f32>, normal: vec4<f32>, albedo: vec4<f32>, directRadiance: vec4<f32> };
struct BounceUniforms { surfelCount: u32, bounceIndex: u32, bounceGain: f32, deltaTime: f32 };
@group(0) @binding(0) var<storage, read> surfels: array<SurfelSample>;
@group(0) @binding(1) var<storage, read> radianceIn: array<vec4<f32>>;
@group(0) @binding(2) var<storage, read_write> radianceOut: array<vec4<f32>>;
@group(0) @binding(3) var<uniform> settings: BounceUniforms;
@compute @workgroup_size(64) fn main(@builtin(global_invocation_id) gid: vec3<u32>) {
    let index = gid.x;
    if (index >= settings.surfelCount) { return; }
    let receiver = surfels[index];
    var incoming = receiver.directRadiance.xyz + radianceIn[index].xyz;
    let count = min(settings.surfelCount, 384u);
    for (var sampleIndex = 0u; sampleIndex < count; sampleIndex = sampleIndex + 1u) {
        if (sampleIndex == index) { continue; }
        let sourceIndex = (sampleIndex * 2654435761u + index * 2246822519u) % settings.surfelCount;
        if (sourceIndex == index) { continue; }
        let source = surfels[sourceIndex];
        let delta = source.originArea.xyz - receiver.originArea.xyz;
        let distanceSquared = max(dot(delta, delta), 0.0025);
        let direction = delta * inverseSqrt(distanceSquared);
        let receiverCosine = max(dot(receiver.normal.xyz, direction), 0.0);
        let sourceCosine = max(dot(source.normal.xyz, -direction), 0.0);
        let formFactor = clamp(source.originArea.w * receiverCosine * sourceCosine / (3.14159265 * distanceSquared + source.originArea.w), 0.0, 1.0);
        incoming += (radianceIn[sourceIndex].xyz + source.directRadiance.xyz) * source.albedo.xyz * (formFactor / 3.14159265);
    }
    let diffuse = receiver.albedo.xyz * incoming / 3.14159265;
    let temporal = clamp(settings.deltaTime * 5.0, 0.04, 1.0);
    radianceOut[index] = vec4<f32>(mix(radianceIn[index].xyz, diffuse, temporal) * settings.bounceGain, 1.0);
}
