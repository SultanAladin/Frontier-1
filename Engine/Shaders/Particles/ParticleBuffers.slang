/*============================================================================================================================================
                                                           PARTICLEBUFFERS.SLANG
============================================================================================================================================*/
// 🧩 Shared Vulkan 1.3 GPU particle buffers bit-exact with Engine/ParticleSpace/ParticleBuffers.h (80B Slot, 208B Global, 256B Rig)

module ParticleBuffers;

//------------------------------------------------------------------------------------------------------------------------
//                                                       CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

public static const uint ParticleTraitAliveBit     = 1u << 0u;
public static const uint ParticleTraitAnchoredBit  = 1u << 1u;
public static const uint ParticleTraitRibbonBit    = 1u << 2u;

public static const uint ParticleDynCurlBit        = 1u << 2u;

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

/// 80-byte per-particle GPU slot matching C++ ParticleSlot byte-for-byte.
public struct ParticleSlotGpu
{
    public float3 PositionMeters;
    public float  AgeSeconds;

    public float3 VelocityMps;
    public float  LifespanSeconds;

    public float  SizeMeters;
    public float  SeedScalar;
    public float  SpinRadians;
    public float  AuxScalar;

    public float3 OriginAnchor;
    public uint   PackedTraits;

    public float4 ColourLinear;
};

/// 16-byte ribbon/strand vertex matching C++ StrandVertex byte-for-byte.
public struct StrandVertexGpu
{
    public float3 PositionMeters;
    public float  NormalisedSpanU;
};

/// 32-byte surface/firework anchor sample matching C++ ParticleAnchorSample byte-for-byte.
public struct ParticleAnchorSampleGpu
{
    public float3 PositionMeters;
    public float  CurvatureWeight;

    public float3 NormalUnit;
    public uint   GroupIndex;
};

/// 208-byte per-frame uniform buffer matching C++ ParticleGlobalUniform.
public struct ParticleGlobalUniformGpu
{
    public float4x4 ViewProjectionMatrix;
    public float4   CameraPositionMeters;
    public float4   CameraRightMeters;
    public float4   CameraUpMeters;

    public float4   AmbientWindVector;

    public float    DeltaSeconds;
    public float    ElapsedSeconds;
    public uint     FrameIndex;
    public uint     ActiveForceCount;

    public float3   ColliderBallCenterM;
    public float    ColliderBallRadiusM;

    public float3   ColliderBallVelocity;
    public uint     ColliderFlags;

    public float4   ViewportDimensions;
};

/// 256-byte per-rig uniform buffer matching C++ ParticleRigUniform.
public struct ParticleRigUniformGpu
{
    public uint   SlotOffset;
    public uint   SlotCapacity;
    public uint   ReleaseOriginCode;
    public uint   VisualCarrierCode;

    public uint   SpriteCutoutCode;
    public uint   BokehApertureCode;
    public uint   ScalarChannelCode;
    public uint   OpticalBlendCode;

    public float  ReleaseRatePerSec;
    public float  BurstCount;
    public uint   StrandSegments;
    public uint   SubdivisionMode;

    public float3 OriginCenter;
    public float  OriginRadius;

    public float3 OriginHalfExtents;
    public float  OriginPitchRadians;

    public float3 LaunchDirection;
    public float  SpreadRadians;

    public float  SpeedMinMps;
    public float  SpeedMaxMps;
    public float  LifeMinSeconds;
    public float  LifeMaxSeconds;

    public float  SizeBirthMeters;
    public float  SizeDeathMeters;
    public float  SizeMultiplier;
    public float  StreakThinness;

    public float  GravityScale;
    public float  AerodynamicDrag;
    public float  WindCoupling;
    public float  BuoyancyMps2;

    public float  FloorBounce;
    public float  FlutterAmplitude;
    public float  CurlFrequency;
    public float  CurlAmplitude;

    public float  CurlEvolutionRate;
    public float  ManifoldRadius;
    public float  ColourRangeMin;
    public float  ColourRangeMax;

    public float  ColourRepeat;
    public float  ColourPhase;
    public uint   ColourWrapCode;
    public uint   ColourCurveCode;

    public float4 PrimaryTintLinear;
    public float4 MidTintLinear;
    public float4 EndTintLinear;

    public uint   DynamicsBitmask;
    public uint   ReceiverChannelMask;
    public uint   AnchorBufferOffset;
    public uint   AnchorBufferCount;
};
