//============================================================================================================================================
//                                                          CLUSTEREDLIGHTS.WGSL
//============================================================================================================================================
// 🧩 World-space clustered lighting with analytic beam profiles: roadway batwing, asymmetric low beam, collimated high beam, fog, floodlight.
//    The profiles are hand-tuned curves shaped like their real counterparts, not measured photometric data.

struct LightRecord {
    PositionType    : vec4f, // xyz = world position, w = light type (0: omni, 1: street lamp, 2: low beam, 3: high beam, 4: fog, 5: flood)
    DirectionReach  : vec4f, // xyz = forward direction, w = reach radius [m]
    ColourIntensity : vec4f, // rgb = linear color, w = intensity
    Params          : vec4f, // x = inner angle cos, y = outer angle cos, z = profile variant, w = active
}

// 📝 Cluster assignment (written every frame by LightingSystem.AssignClusters, bound by each consumer as ClusterCells
//    / ClusterIndices): element 0 carries the grid resolution and the per-cell cap, 1–2 the bitcast minimum and
//    inverse extent, 3+ one (offset, count) pair per cell. Positions outside the grid clamp to its edge; the reach
//    window in EvaluateLightArrival still attenuates distant lights correctly there.
// 📐 The light-index range of the cell holding a world position.
fn ResolveClusterRange(Cells: ptr<storage, array<vec4u>, read>, Position: vec3f) -> vec2u
{
    let Resolution = (*Cells)[0].xyz;
    let Minimum = vec3f(bitcast<f32>((*Cells)[1].x), bitcast<f32>((*Cells)[1].y), bitcast<f32>((*Cells)[1].z));
    let InverseExtent = vec3f(bitcast<f32>((*Cells)[2].x), bitcast<f32>((*Cells)[2].y), bitcast<f32>((*Cells)[2].z));
    let Scaled = clamp((Position - Minimum) * InverseExtent, vec3f(0.0), vec3f(1.0));
    let Cell = min(vec3u(Scaled * vec3f(Resolution)), Resolution - vec3u(1u));
    let Index = (Cell.z * Resolution.y + Cell.y) * Resolution.x + Cell.x;
    return (*Cells)[3u + Index].xy;
}

// 📐 Analytic beam-profile evaluation
fn EvaluateBeamProfile(Light: LightRecord, TowardPoint: vec3f, Distance: f32) -> f32 {
    let Type = u32(Light.PositionType.w + 0.5);
    if (Type == 0u) {
        return 1.0; // Omnidirectional point
    }

    let LDir = TowardPoint / max(Distance, 1.0e-4);
    let Forward = normalize(Light.DirectionReach.xyz);
    let CosTheta = dot(LDir, Forward);

    // Type 1: Street Lamp / Roadway Batwing distribution
    if (Type == 1u) {
        let CosDown = -LDir.z;
        if (CosDown <= 0.05) { return 0.0; }
        let Theta = acos(clamp(CosDown, 0.0, 1.0));
        // Smooth batwing distribution without sharp ring discontinuities
        let Batwing = sin(Theta * 1.8) * CosDown * 2.2;
        return max(0.0, Batwing);
    }

    // Type 2: Automotive Low Beam (Asymmetric sharp cutoff, right-side kick, wide horizontal spread)
    if (Type == 2u) {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);

        // Asymmetric beam cutoff (ECE / DOT standard: lower on oncoming lane, kicks up towards shoulder)
        let CutoffZ = -0.02 - 0.04 * max(0.0, LocalY);
        let TopFade = clamp((CutoffZ + 0.035 - LocalZ) / 0.035, 0.0, 1.0);
        let HorizAngle = atan2(LocalY, CosTheta);
        let HorizFade = max(0.0, cos(clamp(HorizAngle * 1.6, -1.57, 1.57)));
        return TopFade * HorizFade * (CosTheta * CosTheta);
    }

    // Type 3: Automotive High Beam (Collimated pencil spotlight with soft halo)
    if (Type == 3u) {
        if (CosTheta <= 0.15) { return 0.0; }
        return pow(max(0.0, CosTheta), 14.0);
    }

    // Type 4: Automotive Fog Light (Wide fan, razor-sharp top cutoff to prevent glare in fog/smoke)
    if (Type == 4u) {
        if (CosTheta <= 0.08) { return 0.0; }
        let Right = normalize(cross(Forward, vec3f(0.0, 0.0, 1.0)));
        let Up = cross(Right, Forward);
        let LocalY = dot(LDir, Right);
        let LocalZ = dot(LDir, Up);
        if (LocalZ > -0.005) { return 0.0; }
        let HorizAngle = atan2(LocalY, CosTheta);
        return max(0.0, cos(clamp(HorizAngle * 1.3, -1.57, 1.57))) * pow(CosTheta, 4.0);
    }

    // Type 5: Roof Floodlight (Multi-lens LED bar or dual rally pods)
    if (Type == 5u) {
        let InnerCos = Light.Params.x;
        let OuterCos = Light.Params.y;
        return smoothstep(OuterCos, InnerCos, CosTheta);
    }

    return 1.0;
}

// 📐 Smooth, physical inverse-square illuminance with windowed reach
fn EvaluateLightArrival(Light: LightRecord, Position: vec3f, Normal: vec3f) -> vec3f {
    if (Light.Params.w < 0.5) {
        return vec3f(0.0);
    }

    let LightPos = Light.PositionType.xyz;
    let Reach = Light.DirectionReach.w;
    let Toward = LightPos - Position;
    let D2 = dot(Toward, Toward);

    if (D2 >= Reach * Reach || Reach <= 0.0) {
        return vec3f(0.0);
    }

    let Distance = sqrt(D2);
    let LightDir = Toward / max(Distance, 1.0e-4);
    let NdotL = max(dot(Normal, LightDir), 0.0);

    if (NdotL <= 0.0) {
        return vec3f(0.0);
    }

    // Window function: C2 continuous polynomial smoothly dropping to 0 at Reach
    let Ratio = D2 / (Reach * Reach);
    let Window = (1.0 - Ratio * Ratio) * (1.0 - Ratio * Ratio);

    // Beam-profile evaluation along the beam direction
    let Profile = EvaluateBeamProfile(Light, -Toward, Distance);
    if (Profile <= 0.0) {
        return vec3f(0.0);
    }

    let Intensity = Light.ColourIntensity.w;
    let Colour = Light.ColourIntensity.rgb;

    // Standard physical inverse-square falloff with radius margin to avoid singularity
    let Falloff = Window / max(D2, 0.36);
    return Colour * (Intensity * Falloff * Profile * NdotL);
}
