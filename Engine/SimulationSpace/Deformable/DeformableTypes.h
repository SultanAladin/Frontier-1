/*============================================================================================================================================
                                                           DEFORMABLETYPES.H
============================================================================================================================================*/
// 🧩 Phase D01 Unified XPBD + 3D Volumetric Stable Neo-Hookean FEM core types, SOA storage & 16-byte aligned GPU uniform blocks
// Reference: Documentation/Plans/DeformableFemVatArchitecturePlan.md §3

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class DeformablePresetKind : uint8_t
{
    SportTireFem       = 0u, // [-] - 504-point, 1,080-tet 3D Volumetric Stable Neo-Hookean FEM tire
    SportTirePneumatic = 1u, // [-] - 432-point, 48-sector pneumatic XPBD tire carcass
    BotanicalFerns     = 2u, // [-] - Posture-elastic fern fronds, pinnae & rachis tubes
    OrbSpiderweb       = 3u, // [-] - Tearable radial silk spokes, capture spirals & dewdrop beads
    CablesChains       = 4u, // [-] - Catenary suspension cables & interlocking metallic chain links
    HangingVines       = 5u, // [-] - Weeping ivy tendrils & opposing foliage blades
    MarshReeds         = 6u, // [-] - Posture-elastic marsh reeds, cattail heads & grass blades
    UnderwaterKelp     = 7u, // [-] - Buoyant underwater kelp stipes, gas bladders & kelp blades
    WovenCloth         = 8u  // [-] - Orthotropic warp/weft/shear/bending tearable fabric sheet
};

enum class TireDeformMode : uint8_t
{
    LiveGpuCage     = 0u, // [-] - Shared 12.5k-tri high-poly tire deformed live on GPU from cage
    BakedMotionAtlas = 1u, // [-] - 0.00 ms CPU physics playback from baked RGBA32F/RGBA16F VAT
    CoarseCage      = 2u  // [-] - Direct coarse physics cage surface rendering
};

//------------------------------------------------------------------------------------------------------------------------
//                                                 GPU ABI UNIFORM BLOCKS
//------------------------------------------------------------------------------------------------------------------------

/// Exact 128-byte uniform block matching Engine/Shaders/Deformable/DeformableLayouts.slang::DeformableStepUniforms.
struct alignas(16) DeformableStepUniforms
{
    std::array<float, 4>    TimeStepMeters   { 1.0f / 360.0f, 360.0f, 129600.0f, 0.0f }; // x=h, y=invH, z=invH2, w=simTime
    std::array<float, 4>    GravityWind      { -9.8f, 0.985f, 0.0f, 0.0f };              // x=gravityY, y=damping, z=windSpeed, w=turbulence
    std::array<float, 4>    ElasticModuli    { 2.4f, 0.44f, 1600.0f, 0.55f };            // x=youngMpa, y=poissonNu, z=stretchStiff, w=bendStiff
    std::array<float, 4>    TireControls     { 30.0f, 1.35f, 3.0f, 0.0f };               // x=psi, y=axleLoad, z=spinRadSec, w=tearRatio
    std::array<float, 4>    BoundsWindPulse  { -1.78f, 14.0f, 0.0f, 0.0f };              // x=groundY, y=maxSpeed, z=windPulse, w=underwaterFlag
    std::array<float, 4>    ColliderSphere   { 1.55f, -1.62f, 0.0f, 0.42f };             // xyz=center, w=radius
    std::array<uint32_t, 4> CountsBatch      { 0u, 0u, 0u, 0u };                         // x=pointCount, y=batchStart, z=batchEnd, w=colliderEnabled
    std::array<uint32_t, 4> SolidCounts      { 0u, 0u, 0u, 0u };                         // x=tetCount, y=distCount, z=bendCount, w=postureCount
};
static_assert(sizeof(DeformableStepUniforms) == 128u, "DeformableStepUniforms must match 128-byte Slang UBO.");

//------------------------------------------------------------------------------------------------------------------------
//                                               SIMULATION CONFIGURATION
//------------------------------------------------------------------------------------------------------------------------

struct DeformableSimulationConfiguration
{
    uint32_t Substeps          = 6u;     // [-]   - XPBD/FEM substeps per 60 Hz tick
    float    GravityY          = -9.8f;  // [m/s2]- Vertical gravitational acceleration (positive = buoyant)
    float    WindStrength      = 0.0f;   // [m/s] - Ambient wind speed
    float    Turbulence        = 0.0f;   // [m/s] - Curl turbulence amplitude
    float    StretchStiffness  = 1600.0f;// [N/m] - Global distance compliance scalar (baseline 1200)
    float    BendStiffness     = 0.55f;  // [-]   - Flexural bending & botanical posture multiplier
    float    Damping           = 0.985f; // [-]   - Per-step velocity retention factor
    float    TearRatio         = 0.0f;   // [-]   - Stretch fracture ratio (0 = unbreakable)
    float    TirePressurePsi   = 30.0f;  // [psi] - Pneumatic air-cavity pressure
    float    TireAxleLoad      = 1.35f;  // [x]   - Normalized vertical axle downforce multiplier
    float    TireSpinRadPerSec = 3.0f;   // [r/s] - Wheel angular velocity
    float    FemYoungMpa       = 2.4f;   // [MPa] - 3D Neo-Hookean Young's elastic modulus
    float    FemPoissonNu      = 0.44f;  // [-]   - 3D Neo-Hookean Poisson ratio (0.15..0.485)
    bool     UnderwaterEnabled = false;  // [-]   - Elevated aquatic drag & buoyancy regime
    bool     ColliderEnabled   = true;   // [-]   - Enables interactive sphere / curb collider
};

//------------------------------------------------------------------------------------------------------------------------
//                                         CONFLICT-COLORED BATCH PARTITION
//------------------------------------------------------------------------------------------------------------------------

struct ConflictColorBatches
{
    uint32_t              ColorCount      = 0u; // [-] - Number of disjoint parallel color groups
    std::vector<uint32_t> BatchOffsets    {};   // [-] - Size = ColorCount + 1 (prefix sums into OrderedOrdinals)
    std::vector<uint32_t> OrderedOrdinals {};   // [-] - Constraint/tet indices grouped with zero shared mass points
};

//------------------------------------------------------------------------------------------------------------------------
//                                            CONSTRAINT & SOLID RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct DistanceConstraintEntry
{
    uint32_t PointA       = 0u;
    uint32_t PointB       = 0u;
    float    RestLength   = 0.0f;   // [m]
    float    Stiffness    = 1200.0f;// [N/m]
    float    BreakRatio   = 0.0f;   // [-] (0 = unbreakable)
    float    LiveStrain   = 0.0f;   // [-] |L / L0 - 1|
    bool     Structural   = true;   // [-] Primary strand/warp/weft vs diagonal shear
    bool     Severed      = false;  // [-] Torn/cut state
};

struct BendConstraintEntry
{
    uint32_t PointA     = 0u;
    uint32_t PointB     = 0u;
    float    RestLength = 0.0f; // [m]
    float    Stiffness  = 20.0f;// [N/m]
};

struct PostureConstraintEntry
{
    uint32_t PointIndex = 0u;
    float    TargetX    = 0.0f; // [m]
    float    TargetY    = 0.0f; // [m]
    float    TargetZ    = 0.0f; // [m]
    float    Stiffness  = 18.0f;// [1/s2]
};

struct SphereColliderPrimitive
{
    std::array<float, 3> CenterMeters { 1.55f, -1.62f, 0.0f }; // [m]
    float                RadiusMeters = 0.42f;                 // [m]
    bool                 Active       = true;
};

//------------------------------------------------------------------------------------------------------------------------
//                                       TIRE CARCASS & 3D FEM SOLID RECORDS
//------------------------------------------------------------------------------------------------------------------------

struct PneumaticTireCarcass
{
    uint32_t                            StartPointIndex = 0u;
    uint32_t                            SectorCount     = 48u;
    uint32_t                            ProfileCount    = 9u;
    std::vector<std::array<float, 2>>   ProfileStations {}; // [r, z] per profile ring
    float                               AxleX           = 0.0f;
    float                               AxleY           = -0.62f;
    float                               AxleZ           = 0.0f;
    float                               AxleVelocityY   = 0.0f;
    float                               SpinAngleRad    = 0.0f;
    float                               RimRadius       = 0.66f;
    float                               OuterRadius     = 1.14f;
    float                               BeadHalfWidthZ  = 0.3192f;
    float                               RestCavityVol   = 1.0f;
};

struct FemTireSolid
{
    uint32_t                            StartPointIndex = 0u;
    uint32_t                            StartTetIndex   = 0u;
    uint32_t                            TetCount        = 0u;
    uint32_t                            SectorCount     = 36u;
    uint32_t                            ProfileCount    = 7u;
    std::vector<std::array<float, 2>>   InnerStations   {}; // [r, z] inner cavity liner
    std::vector<std::array<float, 2>>   OuterStations   {}; // [r, z] outer rubber skin
    float                               AxleX           = 0.0f;
    float                               AxleY           = -0.62f;
    float                               AxleZ           = 0.0f;
    float                               AxleVelocityY   = 0.0f;
    float                               SpinAngleRad    = 0.0f;
    float                               RimRadius       = 0.66f;
    float                               OuterRadius     = 1.15f;
    float                               BeadHalfWidthZ  = 0.3354f;
    float                               RestCavityVol   = 1.0f;
};

//------------------------------------------------------------------------------------------------------------------------
//                                  PROCEDURAL STRAND / PLANT / CLOTH VISUAL ATTACHMENTS
//------------------------------------------------------------------------------------------------------------------------

struct StrandTubeAttachment
{
    std::vector<uint32_t> PointIndices {};
    std::vector<int32_t>  EdgeOrdinals {};
    float                 RadiusStart  = 0.02f;
    float                 RadiusEnd    = 0.01f;
    uint32_t              RingSides    = 6u;
    std::array<float, 3>  ColourStart  { 0.5f, 0.5f, 0.5f };
    std::array<float, 3>  ColourEnd    { 0.5f, 0.5f, 0.5f };
    std::array<float, 3>  MaterialPbr  { 0.45f, 0.0f, 0.0f }; // roughness, sss/emissive, metallic
};

struct ChainStrandAttachment
{
    std::vector<uint32_t> PointIndices {};
    std::vector<int32_t>  EdgeOrdinals {};
    float                 LinkRadius   = 0.07f;
    std::array<float, 3>  Colour       { 0.65f, 0.68f, 0.74f };
    std::array<float, 3>  MaterialPbr  { 0.22f, 0.0f, 0.92f };
};

struct FoliageBladeAttachment
{
    uint32_t             PointIndex  = 0u;
    uint32_t             NextIndex   = 0u;
    int32_t              LateralSide = 1;
    float                AngleOffset = 0.0f;
    float                PitchRad    = 0.0f;
    float                Length      = 0.25f;
    float                Width       = 0.09f;
    float                ArchFactor  = 0.35f;
    std::array<float, 3> ColourBase  { 0.10f, 0.40f, 0.14f };
    std::array<float, 3> ColourTip   { 0.30f, 0.72f, 0.22f };
    std::array<float, 3> MaterialPbr { 0.34f, 0.72f, 0.0f };
};

struct PointBeadAttachment
{
    uint32_t             PointIndex  = 0u;
    std::array<float, 3> RadiiMeters { 0.03f, 0.03f, 0.03f };
    std::array<float, 3> OffsetMeters{ 0.0f, 0.0f, 0.0f };
    std::array<float, 3> Colour      { 0.8f, 0.9f, 1.0f };
    std::array<float, 3> MaterialPbr { 0.1f, 0.4f, 0.1f };
};

struct ClothSheetAttachment
{
    uint32_t             Columns         = 19u;
    uint32_t             Rows            = 15u;
    uint32_t             StartPointIndex = 0u;
    std::vector<int32_t> HorizontalEdges {};
    std::vector<int32_t> VerticalEdges   {};
    std::array<float, 3> ColourA         { 0.76f, 0.16f, 0.22f };
    std::array<float, 3> ColourB         { 0.95f, 0.58f, 0.20f };
    std::array<float, 3> MaterialPbr     { 0.68f, 0.48f, 0.0f };
};

struct StaticHardwareProp
{
    std::array<float, 3> PositionMeters { 0.0f, 0.0f, 0.0f };
    std::array<float, 3> ScaleMeters    { 0.1f, 0.1f, 0.1f };
    std::array<float, 3> Colour         { 0.3f, 0.3f, 0.3f };
    std::array<float, 3> MaterialPbr    { 0.7f, 0.0f, 0.1f };
};

//------------------------------------------------------------------------------------------------------------------------
//                                           UNIFIED DEFORMABLE BODY SOA
//------------------------------------------------------------------------------------------------------------------------

struct DeformableBody
{
    std::string                        PresetId           { "fem_tyre" };
    std::string                        DisplayTitle       { "3D FEM Soft-Body Tire" };
    DeformablePresetKind               PresetKind         = DeformablePresetKind::SportTireFem;
    float                              GroundY            = -1.78f;
    bool                               UnderwaterEnabled  = false;
    SphereColliderPrimitive            Collider           {};

    // Mass point SOA (size = PointCount * 3 for XYZ vectors, PointCount for scalars)
    uint32_t                           PointCount         = 0u;
    std::vector<float>                 Positions          {}; // [m] live XYZ
    std::vector<float>                 PrevPositions      {}; // [m] previous substep XYZ
    std::vector<float>                 RestPositions      {}; // [m] initial rest XYZ
    std::vector<float>                 Velocities         {}; // [m/s] live XYZ
    std::vector<float>                 InverseMass        {}; // [1/kg] 0 = pinned/bead
    std::vector<float>                 ContactRadius      {}; // [m]
    std::vector<float>                 AeroDragScale      {}; // [-]
    std::vector<float>                 PointStrain        {}; // [-] scattered 3D FEM / XPBD strain

    // 1D / 2D XPBD Constraints & Dual Multipliers
    std::vector<DistanceConstraintEntry> DistanceConstraints {};
    std::vector<float>                   DistanceDuals       {};
    ConflictColorBatches                 DistanceBatches     {};

    std::vector<BendConstraintEntry>     BendConstraints     {};
    std::vector<float>                   BendDuals           {};
    ConflictColorBatches                 BendBatches         {};

    std::vector<PostureConstraintEntry>  PostureConstraints  {};

    // 3D Volumetric Stable Neo-Hookean FEM Tetrahedra
    uint32_t                           TetCount           = 0u;
    std::vector<uint32_t>              TetIndices         {}; // 4 * TetCount
    std::vector<float>                 TetInverseRestDm   {}; // 9 * TetCount (row-major Q = Dm^-1)
    std::vector<float>                 TetCofactorNormals {}; // 9 * TetCount (3 col vectors of Nanson cof(F))
    std::vector<float>                 TetRestVolume      {}; // TetCount (positive V0 [m3])
    std::vector<float>                 TetStrain          {}; // TetCount (live deviatoric + volumetric strain)
    ConflictColorBatches               TetBatches         {};

    // Tire & Visual Attachment Descriptors
    std::vector<PneumaticTireCarcass>  PneumaticTires     {};
    std::vector<FemTireSolid>          FemTires           {};
    std::vector<StrandTubeAttachment>  StrandTubes        {};
    std::vector<ChainStrandAttachment> ChainStrands       {};
    std::vector<FoliageBladeAttachment> FoliageBlades     {};
    std::vector<PointBeadAttachment>   PointBeads         {};
    std::vector<ClothSheetAttachment>  ClothSheets        {};
    std::vector<StaticHardwareProp>    StaticProps        {};

    // Diagnostics & Interactive Grab
    int32_t                            GrabbedPointIndex  = -1;
    std::array<float, 3>               GrabTargetMeters   { 0.0f, 0.0f, 0.0f };
    uint32_t                           SeveredCount       = 0u;

    void ResetAll()
    {
        PointCount = 0u;
        TetCount = 0u;
        SeveredCount = 0u;
        GrabbedPointIndex = -1;
        Positions.clear();
        PrevPositions.clear();
        RestPositions.clear();
        Velocities.clear();
        InverseMass.clear();
        ContactRadius.clear();
        AeroDragScale.clear();
        PointStrain.clear();
        DistanceConstraints.clear();
        DistanceDuals.clear();
        DistanceBatches = {};
        BendConstraints.clear();
        BendDuals.clear();
        BendBatches = {};
        PostureConstraints.clear();
        TetIndices.clear();
        TetInverseRestDm.clear();
        TetCofactorNormals.clear();
        TetRestVolume.clear();
        TetStrain.clear();
        TetBatches = {};
        PneumaticTires.clear();
        FemTires.clear();
        StrandTubes.clear();
        ChainStrands.clear();
        FoliageBlades.clear();
        PointBeads.clear();
        ClothSheets.clear();
        StaticProps.clear();
    }

    uint32_t AppendMassPoint(float x, float y, float z, float invMass = 1.0f, float radius = 0.025f, float aero = 1.0f)
    {
        const uint32_t ordinal = PointCount++;
        Positions.push_back(x);     Positions.push_back(y);     Positions.push_back(z);
        PrevPositions.push_back(x); PrevPositions.push_back(y); PrevPositions.push_back(z);
        RestPositions.push_back(x); RestPositions.push_back(y); RestPositions.push_back(z);
        Velocities.push_back(0.0f); Velocities.push_back(0.0f); Velocities.push_back(0.0f);
        InverseMass.push_back(invMass);
        ContactRadius.push_back(radius);
        AeroDragScale.push_back(aero);
        PointStrain.push_back(0.0f);
        return ordinal;
    }

    int32_t AppendDistanceConstraint(
        uint32_t a,
        uint32_t b,
        float stiffness = 1200.0f,
        float breakRatio = 0.0f,
        bool structural = true,
        float lengthScale = 1.0f);

    void AppendBendConstraint(uint32_t a, uint32_t b, float stiffness = 20.0f);
    void AppendPostureConstraint(uint32_t pointIndex, float stiffness = 18.0f);
};

} // namespace Frontier::SimulationSpace::Deformable
