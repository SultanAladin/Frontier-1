/*============================================================================================================================================
                                                       FORCEMODELEVALUATION.SLANG
============================================================================================================================================*/
// 🧩 Shared pure .slang evaluation of all 20 analytic ForceModels (0..19), 7 falloff curves (0..6), and gas velocity saturation
// Reference: Experimental/FluidEditor/Engine/FluidShaders.js L294-L493 (ForceFieldChunk) & L700-L722 (GasForce)

module ForceModelEvaluation;

import ForceFieldNoise;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

public static const uint kForceChannelGas      = 1u;
public static const uint kForceChannelParticle = 2u;
public static const uint kForceChannelRigid    = 4u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// Matches C++ ForceModelGpuRecord byte-for-byte (64 bytes, 16-byte aligned).
public struct ForceModelGpuRecord
{
    public float4 PositionRadius;    // xyz = centre (m), w = radius (m)
    public float4 DirectionStrength; // xyz = direction, w = strength * timingWeight
    public float4 TypeFalloffParams; // x = type (0..19), y = falloff (0..6), z = frequency, w = secondary
    public float4 TimingChannelMask; // x = tertiary (delay s), y = reserved, z = targetChannelMaskBits, w = enabled
};

//------------------------------------------------------------------------------------------------------------------------
//                                                FALLOFF & ENVELOPE HELPERS
//------------------------------------------------------------------------------------------------------------------------

/// 0 SmoothStep · 1 Linear · 2 InverseSquare · 3 Constant · 4 Gaussian · 5 Plateau · 6 Cosine
public float EvaluateForceFalloff(int falloffMode, float normalizedDistance)
{
    if (falloffMode == 3)
    {
        return 1.0;
    }
    if (normalizedDistance >= 1.0)
    {
        return 0.0;
    }
    if (falloffMode == 0)
    {
        return 1.0 - smoothstep(0.0, 1.0, normalizedDistance);
    }
    if (falloffMode == 1)
    {
        return 1.0 - normalizedDistance;
    }
    if (falloffMode == 4)
    {
        return exp(-normalizedDistance * normalizedDistance * 5.0) * (1.0 - smoothstep(0.9, 1.0, normalizedDistance));
    }
    if (falloffMode == 5)
    {
        return 1.0 - smoothstep(0.55, 1.0, normalizedDistance);
    }
    if (falloffMode == 6)
    {
        return 0.5 + 0.5 * cos(normalizedDistance * 3.14159265);
    }
    return (1.0 / (1.0 + 16.0 * normalizedDistance * normalizedDistance)) * (1.0 - smoothstep(0.85, 1.0, normalizedDistance));
}

public float EvaluatePulseEnvelope(float simulationTime, float period, float width, float delay)
{
    float localTime = simulationTime - delay;
    if (localTime < 0.0)
    {
        return 0.0;
    }
    float phase = frac(localTime / max(period, 0.05));
    return exp(-phase / max(width, 0.01)) * (1.0 - smoothstep(0.8, 1.0, phase));
}

public float3 SafeNormalizeVector(float3 v)
{
    float len = length(v);
    return (len > 1.0e-5) ? (v / len) : float3(0.0, 1.0, 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                 SINGLE MODEL EVALUATION
//------------------------------------------------------------------------------------------------------------------------

/// Exact 1:1 port of EvaluateForceGenerator(index, position, velocity) from FluidShaders.js L330-L479.
public float3 EvaluateSingleForceModel(ForceModelGpuRecord record, float3 worldPosition, float3 velocity, float simulationTime)
{
    int typeCode    = int(record.TypeFalloffParams.x + 0.5);
    int falloffMode = int(record.TypeFalloffParams.y + 0.5);
    float frequency = record.TypeFalloffParams.z;
    float secondary = record.TypeFalloffParams.w;
    float tertiary  = record.TimingChannelMask.x;

    float3 centre   = record.PositionRadius.xyz;
    float radius    = max(record.PositionRadius.w, 1.0e-3);
    float3 axis     = SafeNormalizeVector(record.DirectionStrength.xyz);
    float strength  = record.DirectionStrength.w;

    float3 r              = worldPosition - centre;
    float distance        = length(r);
    float3 radial         = r - axis * dot(r, axis);
    float radialDistance  = length(radial);

    if (typeCode == 0)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float gust = ValueNoise(worldPosition * 0.6 + float3(simulationTime * frequency, 0.0, simulationTime * frequency * 0.7)) * 2.0 - 1.0;
        return axis * strength * (1.0 + secondary * gust) * w;
    }
    if (typeCode == 1 || typeCode == 2)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float core = distance / (distance + max(secondary, 0.02) * radius + 1.0e-4);
        float3 direction = (distance > 1.0e-4) ? (r / distance) : float3(0.0, 0.0, 0.0);
        return direction * strength * w * core * ((typeCode == 1) ? -1.0 : 1.0);
    }
    if (typeCode == 3)
    {
        float w = EvaluateForceFalloff(falloffMode, radialDistance / radius);
        float3 radialDirection = (radialDistance > 1.0e-4) ? (radial / radialDistance) : float3(0.0, 0.0, 0.0);
        float3 tangent = cross(axis, radialDirection);
        float core = radialDistance / (radialDistance + 0.12 * radius);
        return (tangent * strength * core - radialDirection * strength * 0.3 * frequency + axis * secondary) * w;
    }
    if (typeCode == 4)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        return CurlNoise(worldPosition * frequency + float3(0.0, simulationTime * secondary, simulationTime * secondary * 0.31)) * strength * w * 0.35;
    }
    if (typeCode == 5)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        return -velocity * strength * w;
    }
    if (typeCode == 6 || typeCode == 13)
    {
        float pulse = EvaluatePulseEnvelope(simulationTime, frequency, secondary, tertiary);
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float3 direction = (distance > 1.0e-4) ? (r / distance) : float3(0.0, 0.0, 0.0);
        return direction * strength * pulse * w * ((typeCode == 13) ? -1.0 : 1.0);
    }
    if (typeCode == 14)
    {
        float pulse = EvaluatePulseEnvelope(simulationTime, frequency, secondary, tertiary);
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        return axis * strength * pulse * w;
    }
    if (typeCode == 12)
    {
        float localTime = simulationTime - tertiary;
        if (localTime < 0.0)
        {
            return float3(0.0, 0.0, 0.0);
        }
        float phase = frac(localTime / max(frequency, 0.05));
        float shellRadius = phase * radius;
        float thickness = max(secondary, 0.02) * radius;
        float delta = (distance - shellRadius) / thickness;
        float shell = exp(-(delta * delta));
        float3 direction = (distance > 1.0e-4) ? (r / distance) : float3(0.0, 0.0, 0.0);
        return direction * strength * shell * (1.0 - phase * 0.7);
    }
    if (typeCode == 10)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float3 radialDirection = (radialDistance > 1.0e-4) ? (radial / radialDistance) : float3(0.0, 0.0, 0.0);
        float3 tangent = cross(axis, radialDirection);
        float3 toCentre = (distance > 1.0e-4) ? (-r / distance) : float3(0.0, 0.0, 0.0);
        float core = radialDistance / (radialDistance + 0.1 * radius);
        return (tangent * core + toCentre * secondary) * strength * w;
    }
    if (typeCode == 11)
    {
        float overshoot = distance - radius * (1.0 - max(secondary, 0.02));
        if (overshoot <= 0.0)
        {
            return float3(0.0, 0.0, 0.0);
        }
        float wall = clamp(overshoot / (max(secondary, 0.02) * radius), 0.0, 1.0);
        float3 direction = (distance > 1.0e-4) ? (-r / distance) : float3(0.0, 0.0, 0.0);
        return direction * strength * wall * wall;
    }
    if (typeCode == 15)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float3 kick = Hash33(floor(worldPosition * frequency) + floor(simulationTime * max(secondary, 0.1)) * 17.0);
        return kick * strength * w;
    }
    if (typeCode == 16)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        return (axis * frequency - velocity) * strength * w;
    }
    if (typeCode == 17)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float height = clamp(dot(r, axis) / radius, -1.0, 1.0);
        float3 refUp = (abs(axis.y) < 0.9) ? float3(0.0, 1.0, 0.0) : float3(1.0, 0.0, 0.0);
        float3 u = SafeNormalizeVector(cross(axis, refUp));
        float3 v = cross(axis, u);
        float angle = height * secondary * 3.14159265;
        return (u * cos(angle) + v * sin(angle)) * strength * w;
    }
    if (typeCode == 18)
    {
        float w = EvaluateForceFalloff(falloffMode, radialDistance / radius);
        return (cross(axis, radial) / radius) * strength * w;
    }
    if (typeCode == 19)
    {
        float w = EvaluateForceFalloff(falloffMode, distance / radius);
        float core = max(secondary, 0.05) * radius;
        float d = max(distance, core);
        float3 n = r / max(distance, 1.0e-4);
        float3 field = 3.0 * n * dot(axis, n) - axis;
        float scale = (core * core * core) / (d * d * d);
        return SafeNormalizeVector(field) * strength * w * lerp(scale, 1.0, 0.35);
    }
    if (typeCode == 7)
    {
        float w = EvaluateForceFalloff(falloffMode, radialDistance / radius);
        return axis * strength * w;
    }
    if (typeCode == 8)
    {
        float w = EvaluateForceFalloff(falloffMode, radialDistance / radius);
        float3 radialDirection = (radialDistance > 1.0e-4) ? (radial / radialDistance) : float3(0.0, 0.0, 0.0);
        return -radialDirection * strength * w;
    }
    if (typeCode == 9)
    {
        float tube = max(frequency, 0.05);
        float3 radialDirection = (radialDistance > 1.0e-4) ? (radial / radialDistance) : float3(1.0, 0.0, 0.0);
        float3 ringPoint = centre + radialDirection * radius;
        float3 d = worldPosition - ringPoint;
        float coreDistance = length(d);
        float w = EvaluateForceFalloff(falloffMode, coreDistance / tube);
        float3 ringTangent = cross(axis, radialDirection);
        float3 circulation = cross(ringTangent, d / max(coreDistance, 1.0e-4));
        return circulation * strength * w;
    }
    return float3(0.0, 0.0, 0.0);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                ACCUMULATED FIELD QUERIES
//------------------------------------------------------------------------------------------------------------------------

public float3 EvaluateTotalForceField(
    StructuredBuffer<ForceModelGpuRecord> records,
    uint recordCount,
    float3 worldPosition,
    float3 velocity,
    float simulationTime,
    uint channelMaskBits)
{
    float3 total = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < recordCount; ++i)
    {
        ForceModelGpuRecord rec = records[i];
        if (rec.TimingChannelMask.w < 0.5)
        {
            continue;
        }
        uint recMask = uint(rec.TimingChannelMask.z + 0.5);
        if ((recMask & channelMaskBits) == 0u)
        {
            continue;
        }
        total += EvaluateSingleForceModel(rec, worldPosition, velocity, simulationTime);
    }
    return total;
}

/// Exact 1:1 port of GasForce(world, velocity) velocity saturation from FluidShaders.js L700-L722.
public float3 EvaluateSaturatedGasForceField(
    StructuredBuffer<ForceModelGpuRecord> records,
    uint recordCount,
    float3 worldPosition,
    float3 velocity,
    float simulationTime,
    float forceResponseSeconds)
{
    float3 total = float3(0.0, 0.0, 0.0);
    for (uint i = 0u; i < recordCount; ++i)
    {
        ForceModelGpuRecord rec = records[i];
        if (rec.TimingChannelMask.w < 0.5)
        {
            continue;
        }
        uint recMask = uint(rec.TimingChannelMask.z + 0.5);
        if ((recMask & kForceChannelGas) == 0u)
        {
            continue;
        }
        float3 force = EvaluateSingleForceModel(rec, worldPosition, velocity, simulationTime);
        int typeCode = int(rec.TypeFalloffParams.x + 0.5);
        bool limited = !(typeCode == 5 || typeCode == 6 || typeCode == 11 || typeCode == 12 || typeCode == 13 || typeCode == 14 || typeCode == 16);
        float size = length(force);
        if (limited && forceResponseSeconds > 0.0 && size > 1.0e-5)
        {
            float along = dot(velocity, force / size);
            force *= clamp(1.0 - along / (size * forceResponseSeconds), 0.0, 1.0);
        }
        total += force;
    }
    return total;
}
