/*==========================================================================================================================================
                                                  TYREPROJECTIONRECORDS.SLANG
==========================================================================================================================================*/
// 🧩 Shared storage footprints for the standalone carcass compute and its surface consumers.
#ifndef FRONTIER_TYRE_PROJECTION_RECORDS
#define FRONTIER_TYRE_PROJECTION_RECORDS

struct TyreProjectionCommand
{
    float4 Centre;
    float4 Orientation;
    float4 GroundNormalDeflection;
    float4 PressureLateralSeconds;
    uint4 IterationCounts;
};

struct TyreProjectionParticle
{
    float4 RestInverseMass;
    float4 Local;
    float4 Previous;
    float4 Velocity;
    float4 World;
};

struct TyreProjectionResult
{
    float4 ForceStatus;
};

struct TyreInvocationExtent
{
    uint TyreCount;
};

#endif
