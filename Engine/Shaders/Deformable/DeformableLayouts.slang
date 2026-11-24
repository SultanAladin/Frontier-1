//============================================================================================================================================
//                                                        DEFORMABLELAYOUTS.SLANG
//============================================================================================================================================
// Phase D06 Shared GPU storage & uniform layouts for Frontier XPBD + 3D Stable Neo-Hookean FEM + GPU Cage + VAT.
// Exact byte-for-byte parity with:
//   - Engine/SimulationSpace/Deformable/DeformableTypes.h (DeformableStepUniforms: 128 B)
//   - Engine/GeometrySpace/Deformation/CageWeightTypes.h  (CageBoundVertex: 84 B)
//   - Engine/GeometrySpace/Deformation/VertexMotionAtlasTypes.h (VertexMotionAtlasUniforms: 64 B)

#pragma once

struct DeformableStepUniforms
{
    float4 TimeGravity;       // x = subDt, y = simTime, z = gravityY, w = damping
    float4 WindConfig;        // x = windStrength, y = windDirRad, z = fluidDrag, w = groundY
    float4 MaterialCompliance;// x = stretchCompliance, y = bendCompliance, z = femLambda, w = femMu
    float4 TireSpinPressure;  // x = spinRadPerSec, y = pressureBar, z = rimRadius, w = axleY
    float4 TireAxleTarget;    // x = axleX, y = targetAxleY, z = axleZ, w = tireMode (0=none, 1=pneumatic, 2=fem)
    float4 ColliderSphere;    // xyz = centerMeters, w = radiusMeters (<= 0 disables)
    uint4  Counts;            // x = pointCount, y = activeBatchStart, z = activeBatchCount, w = proxyCount
    float4 Flags;             // x = tearingEnabled, y = tearRatio, z = strainDecay, w = femComplianceScale
};

struct DistanceConstraintGpu
{
    uint  PointA;
    uint  PointB;
    float RestLength;
    float Compliance;
    float TearThreshold;
    uint  Flags;
};

struct BendConstraintGpu
{
    uint  PointA;
    uint  PointB;
    uint  PointC;
    float RestDistance;
    float Compliance;
    uint  Pad;
};

struct TetElementGpu
{
    uint4  Indices;
    float4 DmInvRow0; // xyz = row0 of D_m^-1, w = restVolume V0
    float4 DmInvRow1; // xyz = row1 of D_m^-1, w = mu
    float4 DmInvRow2; // xyz = row2 of D_m^-1, w = lambda
};

struct RigidColliderProxyGpu
{
    float4 CenterRadius;   // xyz = world center, w = bounding sphere radius
    float4 HalfExtents;    // xyz = half extents (box/capsule), w = shapeType
    float4 Orientation;    // xyzw = quaternion
    float4 LinearVelocity; // xyz = linear velocity, w = restitution
    float4 FrictionPad;    // x = friction, yzw = pad
};

struct CageBoundVertexGpu
{
    float3 RestPosition;
    float3 RestNormal;
    float2 TexCoord;
    float  ZoneId;
    uint4  CageIndices;
    float4 CageWeights;
    float3 LocalBasisOffset;
    uint   HostTetIndex;
};

struct VertexMotionAtlasUniforms
{
    uint4  PackingSlices; // x = texWidth(256), y = rowsPerSlice, z = slice0, w = slice1
    float4 BlendFlags;    // x = sliceFrac, y = deformMode (0=VAT, 1=TetCage, 2=QuadCage), z = showHeatmap, w = pad
    float4 BoundsMinPad;
    float4 BoundsMaxPad;
};

float3 RotateAboutZ(float3 v, float cosA, float sinA)
{
    return float3(v.x * cosA - v.y * sinA, v.x * sinA + v.y * cosA, v.z);
}

float3 ComputeWindForce(float3 pos, float simTime, float strength, float dirRad, float fluidDrag)
{
    if (strength <= 1.0e-5)
    {
        return float3(0.0, 0.0, 0.0);
    }
    float wx = cos(dirRad);
    float wz = sin(dirRad);
    float gust = 0.65 + 0.35 * sin(simTime * 2.1 + pos.x * 0.7 + pos.z * 0.6)
                      + 0.25 * sin(simTime * 4.7 - pos.y * 1.1);
    float turb = 0.28 * cos(simTime * 3.3 + pos.x * 1.5 - pos.z * 1.3);
    float scale = strength * (1.0 + fluidDrag * 0.4);
    return float3((wx * gust - wz * turb) * scale,
                  0.12 * sin(simTime * 2.8 + pos.x) * strength,
                  (wz * gust + wx * turb) * scale);
}
