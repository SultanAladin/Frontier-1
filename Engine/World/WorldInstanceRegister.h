/*============================================================================================================================================
                                                        WORLDINSTANCEREGISTER.H
============================================================================================================================================*/
// 🧩 Contiguous Structure-of-Arrays world instance register with O(1) slot recycling, RequireRebuild tracking & Editor hooks

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/World/InstanceToken.h"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Frontier::World
{

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

// Contiguous Structure-of-Arrays world transform storage (linear cache-line streaming)
struct WorldTransformStorage
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Back-reference to owning InstanceToken
    std::vector<float>         PositionMetersX         {}; // [m]   - World X coordinate
    std::vector<float>         PositionMetersY         {}; // [m]   - World Y coordinate
    std::vector<float>         PositionMetersZ         {}; // [m]   - World Z coordinate
    std::vector<float>         RotationDegreesX        {}; // [deg] - Euler pitch around X
    std::vector<float>         RotationDegreesY        {}; // [deg] - Euler roll around Y
    std::vector<float>         RotationDegreesZ        {}; // [deg] - Euler yaw around Z
    std::vector<float>         ScaleScalarX            {}; // [x]   - Non-uniform X scale
    std::vector<float>         ScaleScalarY            {}; // [x]   - Non-uniform Y scale
    std::vector<float>         ScaleScalarZ            {}; // [x]   - Non-uniform Z scale
    std::vector<uint8_t>       TransformRequireRebuild {}; // [-]   - 1 when transform changed since last GPU upload
    std::vector<uint32_t>      RebuildTransformSlots   {}; // [idx] - Dense list of slots needing GPU transform upload
    bool                       RequireRebuild          = false; // [-] - True when any transform awaits upload

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }

    void ReserveCapacity(uint32_t Capacity);
};

// Contiguous SoA lunar body register (driven by ExecuteLunarLoop)
struct LunarInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<uint32_t>      MoonSlotOrdinal         {}; // [idx] - Target atmosphere moon slot [0..5]
    std::vector<float>         RightAscensionDegrees   {}; // [deg] - Orbital azimuth / right ascension
    std::vector<float>         DeclinationDegrees      {}; // [deg] - Orbital elevation / declination
    std::vector<float>         AngularDiameterDegrees  {}; // [deg] - Apparent disc angular diameter
    std::vector<float>         PhaseAngleDegrees       {}; // [deg] - Illuminated phase angle [0..360]
    std::vector<float>         OrbitalRateDegreesPerS  {}; // [deg/s] - Continuous orbital rate when animated
    std::vector<float>         AlbedoReflectance       {}; // [0-1] - Geometric surface reflectance
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-]   - Active render visibility toggle
    bool                       RequireRebuild          = false; // [-] - True when lunar parameters require re-evaluation

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA solar body register (driven by ExecuteSolarLoop)
struct SolarInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<float>         AzimuthDegrees          {}; // [deg] - Solar compass azimuth
    std::vector<float>         ElevationDegrees        {}; // [deg] - Solar altitude above horizon
    std::vector<float>         AngularDiameterDegrees  {}; // [deg] - Apparent solar disc diameter
    std::vector<float>         IntensityMultiplier     {}; // [x]   - Physical irradiance gain
    std::vector<float>         DayCycleSpeedDegreesS   {}; // [deg/s] - Continuous day-cycle rate when active
    std::vector<uint8_t>       CycleAnimationEnabled   {}; // [-]   - Advance elevation/azimuth each step
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-]   - Active solar contribution toggle
    bool                       RequireRebuild          = false; // [-] - True when ephemeris requires re-evaluation

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA atmosphere medium register (driven by ExecuteAtmosphereLoop)
struct AtmosphereInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-] - Owning InstanceToken
    std::vector<float>         RayleighScaleMeters     {}; // [m] - Rayleigh exponential scale height
    std::vector<float>         MieScaleMeters          {}; // [m] - Mie aerosol scale height
    std::vector<float>         OzoneDensityMultiplier  {}; // [x] - Stratospheric ozone absorption gain
    std::vector<float>         ObserverAltitudeMeters  {}; // [m] - Observer elevation above sea level
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-] - Active sky medium toggle
    bool                       RequireRebuild          = false; // [-] - True when LUTs require re-evaluation

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA cloud deck register (driven by ExecuteCloudLoop)
struct CloudInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<float>         BaseAltitudeMeters      {}; // [m]   - Cloud layer bottom altitude
    std::vector<float>         ThicknessMeters         {}; // [m]   - Vertical cloud deck thickness
    std::vector<float>         DensityFraction         {}; // [0-1] - Cloud fill fraction
    std::vector<float>         WindSpeedMetersPerS     {}; // [m/s] - Horizontal advection speed
    std::vector<float>         AdvectionOffsetMeters   {}; // [m]   - Integrated horizontal advection travel
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-]   - Active cloud deck toggle
    bool                       RequireRebuild          = false; // [-] - True when cloud deck requires re-evaluation

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA luminaire register (point, spot, area, IES)
struct LuminaireInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<uint8_t>       LuminaireFormOrdinal    {}; // [idx] - 0=Point, 1=Spot, 2=Area, 3=IES
    std::vector<float>         PeakLuminanceR          {}; // [cd]  - Linear red radiance/intensity
    std::vector<float>         PeakLuminanceG          {}; // [cd]  - Linear green radiance/intensity
    std::vector<float>         PeakLuminanceB          {}; // [cd]  - Linear blue radiance/intensity
    std::vector<float>         RangeMeters             {}; // [m]   - Effective attenuation radius
    std::vector<float>         OuterConeDegrees        {}; // [deg] - Spot cone outer half-angle
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-]   - Active luminaire toggle
    bool                       RequireRebuild          = false; // [-] - True when luminaire list requires GPU upload

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA surface geometry instance register
struct SurfaceInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<uint64_t>      GeometryDocumentHash64  {}; // [hash] - FNV-1a 64-bit hash of .GeometrySpace document
    std::vector<uint64_t>      MaterialDocumentHash64  {}; // [hash] - FNV-1a 64-bit hash of .MaterialSpace document
    std::vector<uint32_t>      PrimitiveShapeOrdinal   {}; // [idx] - Analytic primitive ordinal (0..4)
    std::vector<uint8_t>       CastShadows             {}; // [-]   - Include in solar shadow cascades
    std::vector<uint8_t>       VisibilityEnabled       {}; // [-]   - Active viewport render toggle
    bool                       RequireRebuild          = false; // [-] - True when surface register changed

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA particle cloud register
struct ParticleInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<uint64_t>      ParticleDocumentHash64  {}; // [hash] - Source .ParticleSpace hash
    std::vector<uint32_t>      MaximumParticleCount    {}; // [count] - Allocated GPU particle capacity
    std::vector<float>         ReleaseRatePerSecond    {}; // [1/s] - Continuous particle generation rate
    std::vector<float>         AccumulatedClockSeconds {}; // [s]   - Integrated cloud simulation clock
    std::vector<uint8_t>       SimulationActive        {}; // [-]   - True when cloud steps during ExecuteParticleLoop
    bool                       RequireRebuild          = false; // [-] - True when GPU cloud descriptors need rebuild

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA force field register
struct ForceFieldInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]     - Owning InstanceToken
    std::vector<uint8_t>       ForceModelOrdinal       {}; // [idx]   - Analytical ForceModel ordinal
    std::vector<float>         StrengthMetersPerS2     {}; // [m/s^2] - Acceleration / force magnitude
    std::vector<float>         InfluenceRadiusMeters   {}; // [m]     - Spherical falloff radius
    std::vector<float>         DirectionAxisX          {}; // [-]     - Normalized force axis X
    std::vector<float>         DirectionAxisY          {}; // [-]     - Normalized force axis Y
    std::vector<float>         DirectionAxisZ          {}; // [-]     - Normalized force axis Z
    std::vector<uint8_t>       FieldActive             {}; // [-]     - Active evaluation toggle
    bool                       RequireRebuild          = false; // [-]   - True when GPU force buffer needs rebuild

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA trigger zone sensor register
struct TriggerInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<float>         HalfExtentMetersX       {}; // [m]   - Axis-aligned trigger half-extent X
    std::vector<float>         HalfExtentMetersY       {}; // [m]   - Axis-aligned trigger half-extent Y
    std::vector<float>         HalfExtentMetersZ       {}; // [m]   - Axis-aligned trigger half-extent Z
    std::vector<uint32_t>      OverlapCountReading     {}; // [count] - Latest overlapping participant count
    std::vector<uint8_t>       SensorActive            {}; // [-]   - Active overlap sensing toggle
    bool                       RequireRebuild          = false; // [-] - True when sensor bounds changed

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA acoustic source register
struct AcousticInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<uint64_t>      AcousticDocumentHash64  {}; // [hash] - Source .Acoustic document hash
    std::vector<float>         GainScalar              {}; // [x]   - Linear amplitude gain
    std::vector<float>         MaxDistanceMeters       {}; // [m]   - Maximum audible attenuation range
    std::vector<uint8_t>       PlaybackActive          {}; // [-]   - Active spatial audio playback toggle
    bool                       RequireRebuild          = false; // [-] - True when acoustic routing needs rebuild

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

// Contiguous SoA camera optics register
struct CameraInstanceRegister
{
    std::vector<InstanceToken> OwnerTokens             {}; // [-]   - Owning InstanceToken
    std::vector<float>         FocalLengthMillimeters  {}; // [mm]  - Physical lens focal length
    std::vector<float>         SensorWidthMillimeters  {}; // [mm]  - Physical sensor gate width
    std::vector<float>         NearPlaneMeters         {}; // [m]   - Near frustum clip distance
    std::vector<float>         FarPlaneMeters          {}; // [m]   - Far frustum clip distance
    std::vector<uint8_t>       PrimaryViewActive       {}; // [-]   - Active render camera toggle
    bool                       RequireRebuild          = false; // [-] - True when projection basis changed

    [[nodiscard]] uint32_t QueryActiveCount() const noexcept
    {
        return static_cast<uint32_t>(OwnerTokens.size());
    }
};

#if FRONTIER_PROFILE_EDITOR
// Lightweight Editor-only surface registration descriptor (zero ImGui / Outliner header dependency in Engine/World/)
struct EditorInstanceRegistrationDescriptor
{
    InstanceToken    Token              {}; // [-] - Issued world instance token
    InstanceToken    EnclosingToken     {}; // [-] - Optional enclosing folder/instance token
    std::string_view DisplayTitle       {}; // [-] - Outliner row title
    std::string_view CategoryLabel      {}; // [-] - Outliner category caption
    std::string_view Subtitle           {}; // [-] - Outliner secondary description
    std::string_view CategoryTag        {}; // [-] - Canonical inspector routing tag (e.g. "env:moon")
    bool             VisibilityEnabled  = true;  // [-] - Initial Outliner eye-toggle visibility
    bool             RemovalProtected   = false; // [-] - Prevent accidental deletion in Outliner
    bool             DuplicateProtected = false; // [-] - Prevent duplicate entries for singleton categories
};

// Decoupled Editor callbacks bound once by EditorLifecycle (compiled out completely in FRONTIER_PROFILE=Shipping)
struct EditorSurfaceAdapter
{
    std::function<void(const EditorInstanceRegistrationDescriptor&)> RegisterEditorEntry     {}; // [-] - Registers Outliner + Inspector row
    std::function<void(InstanceToken)>                               UnregisterEditorEntry   {}; // [-] - Removes Outliner + Inspector row
    std::function<void(InstanceToken, bool)>                         AssignEditorVisibility  {}; // [-] - Synchronizes Outliner visibility
};
#endif

//------------------------------------------------------------------------------------------------------------------------
//                                               WORLD INSTANCE REGISTER
//------------------------------------------------------------------------------------------------------------------------

class WorldInstanceRegister
{
public:
    WorldInstanceRegister() = default;
    ~WorldInstanceRegister() = default;

    WorldInstanceRegister(const WorldInstanceRegister&)            = delete;
    WorldInstanceRegister& operator=(const WorldInstanceRegister&) = delete;

    void ReserveInstanceCapacity(uint32_t Capacity);
    void ClearAll() noexcept;

    [[nodiscard]] InstanceToken IssueInstanceSlot(
        InstanceCategory Category,
        uint32_t         DirectiveMask,
        uint64_t         DocumentHash64 = 0ull);

    bool ReclaimInstanceSlot(InstanceToken Token) noexcept;

    [[nodiscard]] bool AuthenticateToken(InstanceToken Token) const noexcept;
    [[nodiscard]] const InstanceRecordEntry* FindRecordEntry(InstanceToken Token) const noexcept;
    [[nodiscard]] InstanceRecordEntry* AccessRecordEntry(InstanceToken Token) noexcept;

    // Non-kinship containment linking (EnclosingIndex / FirstNestedIndex / NextLateralIndex)
    bool AttachNestedSlot(InstanceToken EnclosingToken, InstanceToken NestedToken) noexcept;
    bool DetachNestedSlot(InstanceToken NestedToken) noexcept;

    // Contiguous transform mutation with automatic RequireRebuild tracking
    bool AssignWorldTransform(
        InstanceToken Token,
        float         PositionX,
        float         PositionY,
        float         PositionZ,
        float         RotationX = 0.0f,
        float         RotationY = 0.0f,
        float         RotationZ = 0.0f,
        float         ScaleX    = 1.0f,
        float         ScaleY    = 1.0f,
        float         ScaleZ    = 1.0f) noexcept;

    bool AssignVisibility(InstanceToken Token, bool VisibilityEnabled) noexcept;

    [[nodiscard]] uint32_t QueryActiveInstanceCount() const noexcept { return ActiveInstanceCount; }
    [[nodiscard]] uint32_t QueryAllocatedSlotCount() const noexcept
    {
        return static_cast<uint32_t>(RecordEntries.size());
    }

    [[nodiscard]] WorldTransformStorage&      AccessTransforms() noexcept      { return Transforms; }
    [[nodiscard]] const WorldTransformStorage& InspectTransforms() const noexcept { return Transforms; }

    [[nodiscard]] SolarInstanceRegister&      AccessSolarRegister() noexcept      { return SolarRegister; }
    [[nodiscard]] LunarInstanceRegister&      AccessLunarRegister() noexcept      { return LunarRegister; }
    [[nodiscard]] AtmosphereInstanceRegister& AccessAtmosphereRegister() noexcept { return AtmosphereRegister; }
    [[nodiscard]] CloudInstanceRegister&      AccessCloudRegister() noexcept      { return CloudRegister; }
    [[nodiscard]] LuminaireInstanceRegister&  AccessLuminaireRegister() noexcept  { return LuminaireRegister; }
    [[nodiscard]] SurfaceInstanceRegister&    AccessSurfaceRegister() noexcept    { return SurfaceRegister; }
    [[nodiscard]] ParticleInstanceRegister&   AccessParticleRegister() noexcept   { return ParticleRegister; }
    [[nodiscard]] ForceFieldInstanceRegister& AccessForceFieldRegister() noexcept { return ForceFieldRegister; }
    [[nodiscard]] TriggerInstanceRegister&    AccessTriggerRegister() noexcept    { return TriggerRegister; }
    [[nodiscard]] AcousticInstanceRegister&   AccessAcousticRegister() noexcept   { return AcousticRegister; }
    [[nodiscard]] CameraInstanceRegister&     AccessCameraRegister() noexcept     { return CameraRegister; }

    [[nodiscard]] const SolarInstanceRegister&      InspectSolarRegister() const noexcept      { return SolarRegister; }
    [[nodiscard]] const LunarInstanceRegister&      InspectLunarRegister() const noexcept      { return LunarRegister; }
    [[nodiscard]] const AtmosphereInstanceRegister& InspectAtmosphereRegister() const noexcept { return AtmosphereRegister; }
    [[nodiscard]] const CloudInstanceRegister&      InspectCloudRegister() const noexcept      { return CloudRegister; }
    [[nodiscard]] const LuminaireInstanceRegister&  InspectLuminaireRegister() const noexcept  { return LuminaireRegister; }
    [[nodiscard]] const SurfaceInstanceRegister&    InspectSurfaceRegister() const noexcept    { return SurfaceRegister; }
    [[nodiscard]] const ParticleInstanceRegister&   InspectParticleRegister() const noexcept   { return ParticleRegister; }
    [[nodiscard]] const ForceFieldInstanceRegister& InspectForceFieldRegister() const noexcept { return ForceFieldRegister; }
    [[nodiscard]] const TriggerInstanceRegister&    InspectTriggerRegister() const noexcept    { return TriggerRegister; }
    [[nodiscard]] const AcousticInstanceRegister&   InspectAcousticRegister() const noexcept   { return AcousticRegister; }
    [[nodiscard]] const CameraInstanceRegister&     InspectCameraRegister() const noexcept     { return CameraRegister; }

    // Internal O(1) swap-remove helpers invoked during ReclaimInstanceSlot
    void BindCategorySlot(InstanceToken Token, uint32_t CategorySlot) noexcept;

#if FRONTIER_PROFILE_EDITOR
    void BindEditorSurfaceAdapter(EditorSurfaceAdapter Adapter) noexcept;
    void ClearEditorSurfaceAdapter() noexcept;
    void NotifyEditorInstanceRegistered(const EditorInstanceRegistrationDescriptor& Descriptor) const;
    void NotifyEditorInstanceUnregistered(InstanceToken Token) const noexcept;
#endif

private:
    void ReclaimTransformSlot(uint32_t TransformSlot) noexcept;
    void ReclaimCategorySlot(InstanceCategory Category, uint32_t CategorySlot) noexcept;

    std::vector<InstanceRecordEntry> RecordEntries       {}; // [-]     - Dense slot array indexed by SlotIndex
    std::vector<uint16_t>            SlotGenerations     {}; // [ver]   - Current valid generation per slot
    std::vector<uint32_t>            RecycledSlotIndices {}; // [idx]   - O(1) LIFO free-slot stack
    uint32_t                         ActiveInstanceCount = 0u; // [count] - Live active world instance count

    WorldTransformStorage            Transforms          {}; // [-] - Contiguous SoA world transforms
    SolarInstanceRegister            SolarRegister       {}; // [-] - Contiguous solar register
    LunarInstanceRegister            LunarRegister       {}; // [-] - Contiguous lunar register
    AtmosphereInstanceRegister       AtmosphereRegister  {}; // [-] - Contiguous atmosphere register
    CloudInstanceRegister            CloudRegister       {}; // [-] - Contiguous cloud register
    LuminaireInstanceRegister        LuminaireRegister   {}; // [-] - Contiguous luminaire register
    SurfaceInstanceRegister          SurfaceRegister     {}; // [-] - Contiguous surface geometry register
    ParticleInstanceRegister         ParticleRegister    {}; // [-] - Contiguous particle cloud register
    ForceFieldInstanceRegister       ForceFieldRegister  {}; // [-] - Contiguous force field register
    TriggerInstanceRegister          TriggerRegister     {}; // [-] - Contiguous trigger zone register
    AcousticInstanceRegister         AcousticRegister    {}; // [-] - Contiguous acoustic source register
    CameraInstanceRegister           CameraRegister      {}; // [-] - Contiguous camera optics register

#if FRONTIER_PROFILE_EDITOR
    EditorSurfaceAdapter             EditorAdapter       {}; // [-] - Optional Editor Outliner/Inspector hook adapter
#endif
};

} // namespace Frontier::World
