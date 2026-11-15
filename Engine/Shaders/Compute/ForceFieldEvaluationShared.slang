/*============================================================================================================================================
                                                       FORCEFIELDEVALUATIONSHARED.SLANG
============================================================================================================================================*/
// 🧩 Shared +Z Up Slang GPU analytical evaluation of 19 ForceModel laws, reach bounds, CurlNoise, FlowStream, and spline curves

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

static const float VfPi  = 3.14159265359;
static const float VfTau = 6.28318530718;

static const uint ForceModelLinearWind           = 0u;
static const uint ForceModelBuoyancy             = 1u;
static const uint ForceModelRadialBurst          = 2u;
static const uint ForceModelAttractor            = 3u;
static const uint ForceModelRepulsor             = 4u;
static const uint ForceModelAxisAttractor        = 5u;
static const uint ForceModelContainment          = 6u;
static const uint ForceModelRotationalColumn     = 7u;
static const uint ForceModelTorque               = 8u;
static const uint ForceModelDrag                 = 9u;
static const uint ForceModelCurlNoise            = 10u;
static const uint ForceModelAtmosphericGust      = 11u;
static const uint ForceModelVorticityConfinement = 12u;
static const uint ForceModelBrownianNoise        = 13u;
static const uint ForceModelLorentzField         = 14u;
static const uint ForceModelMagneticDipole       = 15u;
static const uint ForceModelFlowStream           = 16u;
static const uint ForceModelSplineForce          = 17u;
static const uint ForceModelBoundaryAdhesion     = 18u;

static const uint FlowStreamCurrent              = 0u;
static const uint FlowStreamThermalUpdraft       = 1u;
static const uint FlowStreamSlipstreamJet        = 2u;
static const uint FlowStreamNavigationField      = 3u;
static const uint FlowStreamVesselWake           = 4u;

static const uint AttenSmoothStep                = 0u;
static const uint AttenLinear                    = 1u;
static const uint AttenInverseSquare             = 2u;
static const uint AttenNone                      = 3u;

static const uint EnvelopeSphere                 = 0u;
static const uint EnvelopeColumn                 = 1u;
static const uint EnvelopeOrientedBox            = 2u;
static const uint EnvelopePlanarBox              = 3u;

static const uint DurationContinuous             = 0u;
static const uint DurationModulated              = 1u;
static const uint DurationImpulse                = 2u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Matches C++ ForceSourceShaderEntry (exact 144-byte / 9x float4 layout)
struct ForceSourceShaderEntry
{
    float4 CentreRadiusPacked;        // xyz = EvaluatedCentre [m], w = Radius [m]
    float4 DirectionStrength;   // xyz = Forward basis, w = EffectiveStrength [m/s]
    float4 TangentLeftFalloff;  // xyz = Left basis, w = FalloffExponent
    float4 UpCoreRadius;        // xyz = Up basis, w = CoreRadius [m]
    float4 HalfExtentsTurbScale;// xyz = HalfExtents [m], w = TurbulenceScale [m]
    float4 FreqSwirlBuoyDrag;      // x = FreqHz, y = SwirlRatio, z = BuoyancyRatio, w = DragCoeff
    uint4  IdsPacked;              // x = ModelId, y = StreamProfileId, z = AttenuationId, w = EnvelopeId
    uint4  FlagsPacked;            // x = DurationId, y = OctaveCount, z = Seed, w = FlagMask
    float2 ImpulseTimePacked;         // x = ImpulsePhase01, y = TimeSeconds
    uint2  WaypointSpan;           // x = WaypointStartIdx, y = WaypointCount
};

struct ForceSampleOutput
{
    float3 AdvectionVelocity;
    float3 LinearAcceleration;
    float3 AngularTorque;
    float  DragDecayRate;
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 CORE MATH & ENVELOPE HELPERS
//------------------------------------------------------------------------------------------------------------------------

float3 VfNormalizeSafe(float3 Vector, float3 Fallback)
{
    float LenSq = dot(Vector, Vector);
    if (LenSq <= 1.0e-12)
    {
        return Fallback;
    }
    return Vector * rsqrt(LenSq);
}

float VfHashFloat01(uint Key)
{
    uint X = Key;
    X ^= X >> 16u;
    X *= 0x7FEB352Du;
    X ^= X >> 15u;
    X *= 0x846CA68Bu;
    X ^= X >> 16u;
    return float(X & 0x00FFFFFFu) / 16777216.0;
}

float VfEvaluateReachNormalized(ForceSourceShaderEntry Src, float3 WorldPoint)
{
    float3 Anchor = Src.CentreRadiusPacked.xyz;
    float3 Delta  = WorldPoint - Anchor;
    float  SafeR  = max(0.25, Src.CentreRadiusPacked.w);
    uint   Env    = Src.IdsPacked.w;

    if (Env == EnvelopeColumn)
    {
        float3 Up    = Src.UpCoreRadius.xyz;
        float  Axial = abs(dot(Delta, Up));
        float3 Rad   = Delta - Up * Axial;
        return max(length(Rad) / SafeR, Axial / max(0.25, Src.HalfExtentsTurbScale.z));
    }
    if (Env == EnvelopeOrientedBox || Env == EnvelopePlanarBox)
    {
        float Lx = abs(dot(Delta, Src.DirectionStrength.xyz)) / max(0.25, Src.HalfExtentsTurbScale.x);
        float Ly = abs(dot(Delta, Src.TangentLeftFalloff.xyz)) / max(0.25, Src.HalfExtentsTurbScale.y);
        float Lz = abs(dot(Delta, Src.UpCoreRadius.xyz))        / max(0.25, Src.HalfExtentsTurbScale.z);
        return max(Lx, max(Ly, Lz));
    }
    return length(Delta) / SafeR;
}

float VfEvaluateSpatialAttenuation(ForceSourceShaderEntry Src, float3 WorldPoint)
{
    float U = VfEvaluateReachNormalized(Src, WorldPoint);
    if (U >= 1.0)
    {
        return 0.0;
    }

    float Taper = saturate(1.0 - U);
    float Power = max(0.1, Src.TangentLeftFalloff.w);
    uint  Atten = Src.IdsPacked.z;

    if (Atten == AttenNone)
    {
        return 1.0;
    }
    if (Atten == AttenLinear)
    {
        return pow(Taper, Power);
    }
    if (Atten == AttenInverseSquare)
    {
        float Dist     = U * max(0.25, Src.CentreRadiusPacked.w);
        float Core     = max(0.1, Src.UpCoreRadius.w);
        float RatioSq  = (Dist / Core) * (Dist / Core);
        float EdgeFade = saturate(1.0 - U * U * U * U);
        return EdgeFade / (1.0 + RatioSq);
    }

    float Hermite = Taper * Taper * (3.0 - 2.0 * Taper);
    return pow(Hermite, 0.25 + 0.75 * Power);
}

void VfEvaluatePotentialCurl(
    float3 P,
    float  ScaleMeters,
    uint   OctaveCount,
    uint   Seed,
    float  TimeSeconds,
    out float3 OutPotential,
    out float3 OutCurl)
{
    static const float3 ModeWave[6] = {
        float3( 1.000,  0.618, -0.382),
        float3(-0.465,  1.000,  0.754),
        float3( 0.577, -0.707,  1.000),
        float3(-0.819,  0.414,  0.523),
        float3( 0.331,  0.887, -0.612),
        float3(-0.642, -0.519,  0.911)
    };
    static const float ModePhase[6] = { 0.41, 1.73, 2.89, 4.12, 5.04, 0.95 };
    static const uint  ModeComp[6]  = { 0u, 1u, 2u, 0u, 1u, 2u };

    uint  ClampedOctaves = min(max(OctaveCount, 1u), 5u);
    float BaseFreq       = VfTau / max(0.5, ScaleMeters);
    float SeedPhase      = VfHashFloat01(Seed) * VfTau;
    float TimeDrift      = TimeSeconds * 0.85;

    float3 Psi = float3(0.0, 0.0, 0.0);
    float dPsiX_dy = 0.0, dPsiX_dz = 0.0;
    float dPsiY_dx = 0.0, dPsiY_dz = 0.0;
    float dPsiZ_dx = 0.0, dPsiZ_dy = 0.0;

    float Amplitude = 1.0;
    float FreqMul   = 1.0;

    for (uint Oct = 0u; Oct < ClampedOctaves; ++Oct)
    {
        for (uint MIdx = 0u; MIdx < 6u; ++MIdx)
        {
            float3 K    = ModeWave[MIdx] * (BaseFreq * FreqMul);
            float  Ph   = ModePhase[MIdx] + SeedPhase + float(Oct) * 1.618 + TimeDrift * (0.65 + 0.25 * float(MIdx % 3u));
            float  Arg  = dot(K, P) + Ph;
            float  S    = Amplitude * sin(Arg);
            float  C    = Amplitude * cos(Arg);
            uint   Comp = (ModeComp[MIdx] + Oct) % 3u;

            if (Comp == 0u)
            {
                Psi.x    += S;
                dPsiX_dy += C * K.y;
                dPsiX_dz += C * K.z;
            }
            else if (Comp == 1u)
            {
                Psi.y    += S;
                dPsiY_dx += C * K.x;
                dPsiY_dz += C * K.z;
            }
            else
            {
                Psi.z    += S;
                dPsiZ_dx += C * K.x;
                dPsiZ_dy += C * K.y;
            }
        }
        Amplitude *= 0.5;
        FreqMul   *= 2.03;
    }

    OutPotential = Psi;
    OutCurl      = float3(dPsiZ_dy - dPsiY_dz, dPsiX_dz - dPsiZ_dx, dPsiY_dx - dPsiX_dy);
}

//------------------------------------------------------------------------------------------------------------------------
//                                            UNIFIED 19-LAW SHADER EVALUATOR
//------------------------------------------------------------------------------------------------------------------------

ForceSampleOutput VfEvaluateSingleSource(
    ForceSourceShaderEntry       Src,
    StructuredBuffer<float4>     WaypointBuffer,
    float3                       WorldPoint,
    float3                       BodyVelocity)
{
    ForceSampleOutput Output;
    Output.AdvectionVelocity  = float3(0.0, 0.0, 0.0);
    Output.LinearAcceleration = float3(0.0, 0.0, 0.0);
    Output.AngularTorque      = float3(0.0, 0.0, 0.0);
    Output.DragDecayRate      = 0.0;

    if ((Src.FlagsPacked.w & 1u) == 0u)
    {
        return Output;
    }

    float3 Anchor   = Src.CentreRadiusPacked.xyz;
    float3 Delta    = WorldPoint - Anchor;
    float  SafeR    = max(0.25, Src.CentreRadiusPacked.w);
    float  CoreR    = max(0.1, Src.UpCoreRadius.w);
    float3 Forward  = Src.DirectionStrength.xyz;
    float3 Left     = Src.TangentLeftFalloff.xyz;
    float3 Up       = Src.UpCoreRadius.xyz;
    float  Strength = Src.DirectionStrength.w;
    float  TimeSec  = Src.ImpulseTimePacked.y;
    uint   ModelId  = Src.IdsPacked.x;

    if (ModelId == ForceModelCurlNoise)
    {
        float U2 = dot(Delta, Delta) / (SafeR * SafeR);
        if (U2 >= 1.0)
        {
            return Output;
        }
        float  W         = (Src.IdsPacked.z == AttenNone) ? 1.0 : pow(1.0 - U2, 3.0);
        float  dWdU2     = (Src.IdsPacked.z == AttenNone) ? 0.0 : -3.0 * (1.0 - U2) * (1.0 - U2);
        float3 GradW     = Delta * ((2.0 * dWdU2) / (SafeR * SafeR));
        float3 Pot       = float3(0.0, 0.0, 0.0);
        float3 RawCurl   = float3(0.0, 0.0, 0.0);
        VfEvaluatePotentialCurl(Delta, Src.HalfExtentsTurbScale.w, Src.FlagsPacked.y, Src.FlagsPacked.z, TimeSec, Pot, RawCurl);
        float3 DivFree   = RawCurl * W + cross(GradW, Pot);
        float  NormScale = Strength * max(0.5, Src.HalfExtentsTurbScale.w * 0.14);
        Output.AdvectionVelocity = DivFree * NormScale;
        return Output;
    }

    float Atten = VfEvaluateSpatialAttenuation(Src, WorldPoint);
    if (Atten <= 1.0e-6)
    {
        return Output;
    }

    float Scale = Strength * Atten;

    if (ModelId == ForceModelLinearWind)
    {
        Output.AdvectionVelocity = Forward * Scale;
    }
    else if (ModelId == ForceModelBuoyancy)
    {
        float Uplift              = Scale * max(0.1, Src.FreqSwirlBuoyDrag.z);
        Output.AdvectionVelocity  = float3(0.0, 0.0, Uplift * 0.45);
        Output.LinearAcceleration = float3(0.0, 0.0, Uplift);
    }
    else if (ModelId == ForceModelRadialBurst)
    {
        float  Dist     = length(Delta);
        float3 Radial   = (Dist > 1.0e-4) ? (Delta / Dist) : Up;
        float  ShellMod = 1.0;
        if (Src.FlagsPacked.x == DurationImpulse && Src.ImpulseTimePacked.x > 0.0)
        {
            float FrontDist = Src.ImpulseTimePacked.x * SafeR;
            float Sigma     = max(1.5, SafeR * 0.22);
            float Diff      = Dist - FrontDist;
            ShellMod        = exp(-(Diff * Diff) / (2.0 * Sigma * Sigma));
        }
        float SmoothCore          = Dist / sqrt(Dist * Dist + CoreR * CoreR);
        Output.LinearAcceleration = Radial * (Scale * SmoothCore * ShellMod);
        Output.AdvectionVelocity  = Radial * (Scale * SmoothCore * ShellMod * 0.35);
    }
    else if (ModelId == ForceModelAttractor)
    {
        float  Dist               = length(Delta);
        float3 Inward             = (Dist > 1.0e-4) ? (-Delta / Dist) : float3(0.0, 0.0, 0.0);
        float  Soft               = Dist / sqrt(Dist * Dist + CoreR * CoreR);
        Output.LinearAcceleration = Inward * (Scale * Soft);
        Output.AdvectionVelocity  = Inward * (Scale * Soft * 0.4);
    }
    else if (ModelId == ForceModelRepulsor)
    {
        float  Dist               = length(Delta);
        float3 Outward            = (Dist > 1.0e-4) ? (Delta / Dist) : Up;
        float  Soft               = Dist / sqrt(Dist * Dist + CoreR * CoreR);
        Output.LinearAcceleration = Outward * (Scale * Soft);
        Output.AdvectionVelocity  = Outward * (Scale * Soft * 0.4);
    }
    else if (ModelId == ForceModelAxisAttractor)
    {
        float  Axial              = dot(Delta, Up);
        float3 RadVec             = Delta - Up * Axial;
        float  RadDist            = length(RadVec);
        float3 Inward             = (RadDist > 1.0e-4) ? (-RadVec / RadDist) : float3(0.0, 0.0, 0.0);
        float  Soft               = RadDist / sqrt(RadDist * RadDist + CoreR * CoreR);
        Output.LinearAcceleration = Inward * (Scale * Soft);
        Output.AdvectionVelocity  = (Inward * Soft + Up * (Src.FreqSwirlBuoyDrag.y * 0.25)) * Scale * 0.5;
    }
    else if (ModelId == ForceModelContainment)
    {
        float Dist = length(Delta);
        float Norm = Dist / SafeR;
        if (Norm > 0.35)
        {
            float  EdgeRatio          = saturate((Norm - 0.35) / 0.65);
            float3 Inward             = (Dist > 1.0e-4) ? (-Delta / Dist) : float3(0.0, 0.0, 0.0);
            Output.LinearAcceleration = Inward * (Strength * EdgeRatio * EdgeRatio);
            Output.AdvectionVelocity  = Inward * (Strength * EdgeRatio * 0.5);
        }
    }
    else if (ModelId == ForceModelRotationalColumn)
    {
        float  Axial      = dot(Delta, Up);
        float3 RadVec     = Delta - Up * Axial;
        float  RadDist    = length(RadVec);
        float3 RadialDir  = (RadDist > 1.0e-4) ? (RadVec / RadDist) : Forward;
        float3 Tangent    = VfNormalizeSafe(cross(Up, RadialDir), Left);
        float  Ratio      = RadDist / CoreR;
        float  SoftProf   = Ratio / (1.0 + Ratio * Ratio);
        float3 SwirlVel   = Tangent * (Scale * (0.65 + 1.35 * SoftProf));
        float3 InPull     = RadialDir * (-Scale * abs(Src.FreqSwirlBuoyDrag.y) * 0.32 * SoftProf);
        float3 Lift       = Up * (Scale * Src.FreqSwirlBuoyDrag.y * 0.35);
        Output.AdvectionVelocity = SwirlVel + InPull + Lift;
        Output.AngularTorque     = Up * (Scale / SafeR);
    }
    else if (ModelId == ForceModelTorque)
    {
        float  Axial              = dot(Delta, Up);
        float3 RadVec             = Delta - Up * Axial;
        Output.AngularTorque      = Up * Scale;
        Output.LinearAcceleration = cross(Up, RadVec / SafeR) * (Scale * 0.5);
    }
    else if (ModelId == ForceModelDrag)
    {
        float DragRate            = max(0.0, Src.FreqSwirlBuoyDrag.w) * Atten;
        Output.DragDecayRate      = DragRate;
        Output.LinearAcceleration = BodyVelocity * (-DragRate);
    }
    else if (ModelId == ForceModelAtmosphericGust)
    {
        float Wave1  = sin(dot(Delta, Forward) * 0.14 - TimeSec * Src.FreqSwirlBuoyDrag.x * VfTau);
        float Wave2  = cos(dot(Delta, Left)    * 0.21 + TimeSec * Src.FreqSwirlBuoyDrag.x * 3.7);
        float Gust   = 0.45 + 0.55 * max(0.0, 0.6 * Wave1 + 0.4 * Wave2);
        float3 Pot   = float3(0.0, 0.0, 0.0);
        float3 Swirl = float3(0.0, 0.0, 0.0);
        VfEvaluatePotentialCurl(Delta, Src.HalfExtentsTurbScale.w, min(Src.FlagsPacked.y, 3u), Src.FlagsPacked.z, TimeSec, Pot, Swirl);
        Output.AdvectionVelocity = Forward * (Scale * Gust) + Swirl * (Scale * 0.25);
    }
    else if (ModelId == ForceModelVorticityConfinement)
    {
        float3 Pot  = float3(0.0, 0.0, 0.0);
        float3 Curl = float3(0.0, 0.0, 0.0);
        VfEvaluatePotentialCurl(Delta, Src.HalfExtentsTurbScale.w, Src.FlagsPacked.y, Src.FlagsPacked.z, TimeSec, Pot, Curl);
        float3 Axis = VfNormalizeSafe(Curl, Up);
        Output.AdvectionVelocity  = Curl * (Scale * 0.35);
        Output.LinearAcceleration = cross(Axis, VfNormalizeSafe(Delta, Forward)) * (Scale * 0.6);
        Output.AngularTorque      = Axis * (Scale * 0.2);
    }
    else if (ModelId == ForceModelBrownianNoise)
    {
        float TimeBin = floor(TimeSec * max(1.0, Src.FreqSwirlBuoyDrag.x * 12.0));
        uint  Key     = Src.FlagsPacked.z ^ uint(TimeBin * 2654435761.0) ^
                        uint(int(round(Delta.x * 1.7)) * 73856093) ^
                        uint(int(round(Delta.y * 1.7)) * 19349663) ^
                        uint(int(round(Delta.z * 1.7)) * 83492791);
        float3 Jitter = float3(VfHashFloat01(Key + 11u), VfHashFloat01(Key + 29u), VfHashFloat01(Key + 47u)) * 2.0 - 1.0;
        Output.AdvectionVelocity  = Jitter * Scale;
        Output.LinearAcceleration = Jitter * (Scale * 0.4);
    }
    else if (ModelId == ForceModelLorentzField)
    {
        float3 VelCrossB = cross(BodyVelocity, Up);
        float3 Dir       = (dot(VelCrossB, VelCrossB) > 1.0e-6) ? VelCrossB : cross(Forward, Up);
        Output.LinearAcceleration = Dir * Scale;
        Output.AdvectionVelocity  = cross(Forward, Up) * (Scale * 0.35);
    }
    else if (ModelId == ForceModelMagneticDipole)
    {
        float  RawDist            = length(Delta);
        float  Dist               = max(CoreR, RawDist);
        float3 RHat               = (RawDist > 1.0e-4) ? (Delta / RawDist) : Up;
        float3 DipoleB            = RHat * (3.0 * dot(Up, RHat)) - Up;
        float  Falloff            = (CoreR * CoreR) / (Dist * Dist);
        Output.AdvectionVelocity  = DipoleB * (Scale * Falloff);
        Output.LinearAcceleration = DipoleB * (Scale * Falloff * 0.65);
    }
    else if (ModelId == ForceModelFlowStream)
    {
        uint StreamProf = Src.IdsPacked.y;
        if (StreamProf == FlowStreamThermalUpdraft)
        {
            float  Axial  = dot(Delta, Up);
            float3 Rad    = Delta - Up * Axial;
            float  NormR  = saturate(length(Rad) / SafeR);
            float  Bell   = 1.0 - NormR * NormR;
            float3 Tang   = VfNormalizeSafe(cross(Up, Rad), Left);
            float3 InPull = (length(Rad) > 1.0e-4) ? (-0.25 * Src.FreqSwirlBuoyDrag.y / SafeR) * Rad : float3(0.0, 0.0, 0.0);
            Output.AdvectionVelocity  = Up * (Scale * max(0.15, Bell)) + Tang * (Scale * Src.FreqSwirlBuoyDrag.y * 0.45) + InPull * Scale;
            Output.LinearAcceleration = Up * (Scale * 0.35 * max(0.15, Bell));
        }
        else if (StreamProf == FlowStreamSlipstreamJet)
        {
            float  FwdDist = max(0.0, dot(Delta, Forward));
            float3 Lat     = Delta - Forward * FwdDist;
            float  Cone    = CoreR + FwdDist * 0.32;
            float  Prof    = exp(-dot(Lat, Lat) / max(0.25, Cone * Cone));
            Output.AdvectionVelocity  = Forward * (Scale * (1.0 + 0.5 * Prof));
            Output.LinearAcceleration = Forward * (Scale * Prof * 0.4);
        }
        else if (StreamProf == FlowStreamNavigationField)
        {
            float  Axial    = dot(Delta, Up);
            float3 Flat     = Delta - Up * Axial;
            float  LatY     = dot(Flat, Left);
            float  Steer    = clamp(-LatY / SafeR, -1.0, 1.0) * 0.55;
            Output.AdvectionVelocity = VfNormalizeSafe(Forward + Left * Steer, Forward) * Scale;
        }
        else if (StreamProf == FlowStreamVesselWake)
        {
            float  FwdDist = dot(Delta, Forward);
            float  LatDist = dot(Delta, Left);
            float  KSign   = (LatDist >= 0.0) ? 1.0 : -1.0;
            float  Ripple  = 0.5 + 0.5 * cos((FwdDist - abs(LatDist) * 1.45) * 0.25 - TimeSec * 2.0);
            float3 WakeDir = VfNormalizeSafe(Forward * -0.7 + Left * (KSign * 0.72), Left);
            Output.AdvectionVelocity = WakeDir * (Scale * (0.45 + 0.55 * Ripple));
        }
        else
        {
            float  Meander = 0.28 * sin(dot(Delta, Forward) / max(1.0, Src.HalfExtentsTurbScale.w) + TimeSec * Src.FreqSwirlBuoyDrag.x);
            Output.AdvectionVelocity = VfNormalizeSafe(Forward + Left * Meander, Forward) * Scale;
        }
    }
    else if (ModelId == ForceModelSplineForce)
    {
        if (Src.WaypointSpan.y < 2u)
        {
            Output.AdvectionVelocity = Forward * Scale;
        }
        else
        {
            uint   Count      = Src.WaypointSpan.y;
            uint   BaseIdx    = Src.WaypointSpan.x;
            float  BestDistSq = 1.0e30;
            float3 BestPos    = Anchor;
            float3 BestTan    = Forward;
            for (uint Step = 0u; Step < Count - 1u; ++Step)
            {
                float3 A   = WaypointBuffer[BaseIdx + Step].xyz;
                float3 B   = WaypointBuffer[BaseIdx + Step + 1u].xyz;
                float3 AB  = B - A;
                float  L2  = max(1.0e-5, dot(AB, AB));
                float  T   = saturate(dot(WorldPoint - A, AB) / L2);
                float3 Pt  = lerp(A, B, T);
                float3 Dif = Pt - WorldPoint;
                float  D2  = dot(Dif, Dif);
                if (D2 < BestDistSq)
                {
                    BestDistSq = D2;
                    BestPos    = Pt;
                    BestTan    = VfNormalizeSafe(AB, Forward);
                }
            }
            float3 CenterPull         = (BestPos - WorldPoint) / max(0.5, CoreR);
            Output.AdvectionVelocity  = BestTan * Scale + CenterPull * (Scale * 0.45);
            Output.LinearAcceleration = CenterPull * (Scale * 0.35);
        }
    }
    else if (ModelId == ForceModelBoundaryAdhesion)
    {
        float SignedDist          = dot(Delta, Up);
        Output.LinearAcceleration = Up * ((-SignedDist / max(0.5, CoreR)) * Scale);
        Output.DragDecayRate      = max(0.0, Src.FreqSwirlBuoyDrag.w) * Atten;
    }

    return Output;
}

float3 VfEvaluateCombinedVelocity(
    StructuredBuffer<ForceSourceShaderEntry> SourceBuffer,
    StructuredBuffer<float4>                 WaypointBuffer,
    uint                                     SourceCount,
    float3                                   WorldPoint,
    float3                                   BodyVelocity)
{
    float3 Total = float3(0.0, 0.0, 0.0);
    for (uint Idx = 0u; Idx < SourceCount; ++Idx)
    {
        ForceSampleOutput S = VfEvaluateSingleSource(SourceBuffer[Idx], WaypointBuffer, WorldPoint, BodyVelocity);
        Total += S.AdvectionVelocity + S.LinearAcceleration;
    }
    return Total;
}

float3 VfRampSpeedColor(float SpeedRatio)
{
    float T = saturate(SpeedRatio);
    float3 C0 = float3(0.08, 0.25, 0.42);
    float3 C1 = float3(0.18, 0.72, 0.88);
    float3 C2 = float3(0.95, 0.72, 0.24);
    float3 C3 = float3(0.96, 0.28, 0.22);
    if (T < 0.33)
    {
        return lerp(C0, C1, T / 0.33);
    }
    if (T < 0.66)
    {
        return lerp(C1, C2, (T - 0.33) / 0.33);
    }
    return lerp(C2, C3, (T - 0.66) / 0.34);
}
