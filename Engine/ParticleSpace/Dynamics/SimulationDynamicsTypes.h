/*============================================================================================================================================
                                                          SIMULATIONDYNAMICSTYPES.H
============================================================================================================================================*/
// 🧩 Composable typed particle physics & simulation dynamics specifications (zero monolithic switches, directly reuses ForceFields)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include <cstdint>
#include <optional>

namespace Frontier::ParticleSpace
{

//------------------------------------------------------------------------------------------------------------------------
//                                                      ENUMERATIONS
//------------------------------------------------------------------------------------------------------------------------

enum class EffectorVolumeShape : uint8_t
{
    Sphere = 0u,
    Box    = 1u,
    Torus  = 2u,
    Disc   = 3u,
    Cone   = 4u
};

enum class EffectorDriverCategory : uint8_t
{
    FluidSimplexWave    = 0u,
    OrbitingDual        = 1u,
    CurlIslands         = 2u,
    ExpandingPulseRing  = 3u,
    BallForceFieldsOnly = 4u,
    Placeable3DEffectors= 5u
};

enum class ShieldBarrierForm : uint8_t
{
    SphereDome     = 0u,
    CircleDisc     = 1u,
    TriangleAegis  = 2u,
    SquarePane     = 3u,
    HexagonWall    = 4u,
    DiamondBarrier = 5u
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       STRUCTURES
//------------------------------------------------------------------------------------------------------------------------

struct BallisticDynamicsSpec
{
    float    GravityScale       = 1.0f;  // [x]     - Multiplier on global GravityMps2
    float    AerodynamicDrag    = 0.2f;  // [1/s]   - Exponential velocity drag rate
    float    BuoyancyMps2       = 0.0f;  // [m/s^2] - Upward thermal lift acceleration
    float    FlutterAmplitude   = 0.0f;  // [m/s]   - Lateral aerodynamic leaf flutter
    float    FloorRestitution   = 0.0f;  // [0-1]   - Bounce restitution against floor plane
    float    FloorElevation     = 0.0f;  // [m]     - Ground collision elevation
    float    TimedBurstInterval = 0.0f;  // [s]     - Periodic burst cadence (0 = continuous)
    uint32_t TimedBurstCount    = 120u;  // [count] - Particles spawned per timed burst
    bool     BurstFromOrigin    = false; // [-]     - True spawns burst at origin instead of random sky coordinate
};

struct ForceFieldCouplingSpec
{
    float    WindCouplingRate    = 0.25f; // [1/s]  - Relaxation rate toward ForceFields flow velocity
    uint32_t ReceiverChannelMask = 0x7Fu; // [bits] - Active receiver channels (Particles/Debris/Foliage/Cloth/...)
    bool     SampleFieldLattice  = true;  // [-]    - True samples FieldLatticeCascade when active
};

struct CurlManifoldSpec
{
    uint8_t ManifoldMode      = 1u;                      // [-]   - 1 Planar curl field, 2 Spherical curl plasma, 3 Bokeh stream / surface pour ribbons
    uint8_t PulseMode         = 0u;                      // [-]   - 0 Free swirling curl stream ribbons, 2 Object surface pour pulse
    uint8_t ObjectShape       = 0u;                      // [-]   - 0 Box, 1 Sphere, 2 Cylinder (when PulseMode > 0)
    uint8_t PulseAxis         = 1u;                      // [-]   - Pour direction axis (0 +X, 1 -Y top-to-bottom, 2 +Z)
    uint8_t HeadStyle         = 0u;                      // [-]   - 0 Mixed DoF bokeh + star cores, 1 Pinpoint, 2 Soft bokeh, 3 Aperture ring
    float   FrequencyPerM     = 0.85f;                   // [1/m] - Spatial curl frequency (HTML curlFreq)
    float   DriveSpeedMps     = 2.2f;                    // [m/s] - Tangential curl velocity drive (HTML curlAmp)
    float   EvolutionRateHz   = 0.24f;                   // [1/s] - Temporal phase rate (HTML curlRate)
    float   ShellRadiusM      = 1.75f;                   // [m]   - Target spherical or major torus radius
    float   TubeRadiusM       = 0.55f;                   // [m]   - Minor tube radius for toroidal manifold mode
    float   PulseSpeedHz      = 0.36f;                   // [Hz]  - Travelling pour wave speed (HTML pulseSpeed)
    float   PulseWidth        = 0.24f;                   // [-]   - Travelling pour wave width (HTML pulseWidth)
    float   StreakThin        = 0.34f;                   // [x]   - Ribbon thinness multiplier (HTML streakThin)
    float   BoxHalfMeters[3]  = { 1.9f, 0.24f, 1.9f };   // [m]   - Domain half-extents (HTML boxHalf)
    bool    ShowObjectCore    = true;                    // [-]   - Render translucent 3D core sphere & pour rings when PulseMode > 0
};

struct StrandConstraintSpec
{
    uint8_t  StrandVariant    = 1u;                     // [-]   - 1 Cable/Chain, 2 Vine/Rope, 3 Flag/Sail, 4 Beads/Chimes/Drape
    uint8_t  SubMode          = 0u;                     // [-]   - Variant sub-mode (Cable:1=Chain; Flag:0=Flag,1=Sail; Beads:1=Bead,2=Chime,3=Drape)
    uint32_t RowCount         = 18u;                    // [cnt] - Primary strand / row count (HTML dynRows)
    float    HeightMeters     = 3.1f;                   // [m]   - Suspension / mast elevation (HTML dynHeight)
    float    StiffnessRate    = 1.25f;                  // [1/s] - XPBD elastic recovery rate (HTML dynStiff)
    float    TearThreshold    = 0.0f;                   // [m/s] - Impact speed threshold for tearing (HTML dynTearThresh)
    float    CatenarySagM     = 0.35f;                  // [m]   - Gravity droop or belly sag (HTML dynSag)
    float    WaveSpeedHz      = 2.2f;                   // [Hz]  - Travelling flutter wave speed (HTML dynWaveSpeed)
    float    CurlFreqPerM     = 0.9f;                   // [1/m] - Spatial wind-curl frequency (HTML curlFreq)
    float    CurlAmpMps       = 0.65f;                  // [m/s] - Wind-curl gust amplitude (HTML curlAmp)
    float    BoxHalfMeters[3] = { 2.0f, 1.5f, 1.0f };   // [m]   - Span half-extents (HTML boxHalf)
    float    SpanDirection[3] = { 1.0f, 0.0f, 0.0f };   // [-]   - Primary horizontal fly direction (HTML dir)
    float    WidthScale       = 1.0f;                   // [x]   - Ribbon thinness multiplier (HTML streakThin)
    bool     ShowHardwareProp = true;                   // [-]   - Render pylons / flagpole / ship mast / doorway lintel
};

struct BotanicalSwaySpec
{
    uint8_t FloraVariant    = 4u;    // [-]   - 4 Grass/Wheat, 5 Reeds, 6 Fern, 7 CanopyTree, 8 Kelp, 9 Anemone
    float   StalkHeightM    = 2.4f;  // [m]   - Stalk or canopy elevation
    float   ArchCurvatureM  = 0.35f; // [m]   - Frond arch sag
    float   StadiumWaveHz   = 2.2f;  // [Hz]  - Travelling wind wave speed
    float   SwayFrequencyHz = 0.85f; // [Hz]  - Primary stem oscillation frequency
    float   WindSensitivity = 1.15f; // [x]   - Wind sway amplitude multiplier
    float   StemStiffness   = 3.2f;  // [1/s] - Elastic stem restoring stiffness
    float   DetachThreshold = 0.0f;  // [m/s] - Wind/impact detachment speed (dandelion/blossom)
};

struct OrbitalGravitySpec
{
    float CentralMassParam    = 12.0f; // [m^3/s^2] - Gravitational parameter G*M
    float SofteningMeters     = 0.32f; // [m]       - Plummer softening radius
    float TangentialBoost     = 2.4f;  // [m/s^2]   - Disk tangential swirl drive
    bool  EnablePairwiseNBody = false; // [-]       - Optional bounded mutual attraction
};

struct ElectromagneticSpec
{
    float ChargeToMassRatio = 1.6f;                    // [C/kg] - Specific charge q/m
    float MagneticFieldB[3] = { 0.0f, 1.2f, 0.0f };    // [T]    - Uniform background B vector
    float ElectricFieldE[3] = { 0.0f, 0.0f, 0.0f };    // [V/m]  - Uniform background E vector
    float DipoleMoment      = 2.8f;                    // [A*m^2]- Central magnetic dipole strength
};

struct HarmonicWaveSpec
{
    uint32_t ModeIndexM      = 3u;    // [-]  - Chladni harmonic mode M
    uint32_t ModeIndexN      = 5u;    // [-]  - Chladni harmonic mode N
    float    FrequencyHz     = 1.2f;  // [Hz] - Acoustic standing wave oscillation
    float    AmplitudeMeters = 1.1f;  // [m]  - Resonance drive amplitude
};

struct AttractorDynamicsSpec
{
    uint8_t AttractorKind  = 0u;    // [-] - 0 Lorenz, 1 Thomas, 2 Aizawa, 3 Halvorsen, 4 Dadras, 5 Chen, 6 Rossler, 7 Sprott, 8 Four-Wing
    float   ScaleFactor    = 0.12f; // [x] - World-space display scale
    float   TimeMultiplier = 1.25f; // [x] - RK4 integration speed multiplier
};

struct SwarmFlockingSpec
{
    float PerceptionRadiusM = 1.35f; // [m]   - Spatial-hash neighbour perception radius
    float CohesionWeight    = 1.15f; // [-]   - Flock centre attraction
    float AlignmentWeight   = 1.65f; // [-]   - Neighbour heading alignment
    float SeparationWeight  = 2.65f; // [-]   - Close-range separation weight
    float MaxSpeedMps       = 3.4f;  // [m/s] - Maximum cruising flight speed
};

struct SpeciesLifeSpec
{
    uint32_t SpeciesCount       = 4u;    // [count] - Distinct particle species [2..6]
    float    InteractionRadiusM = 1.15f; // [m]     - Interaction cut-off radius
    float    ForceMultiplier    = 2.4f;  // [x]     - Asymmetric attraction/repulsion gain
    float    FrictionDecay      = 0.92f; // [0-1]   - Per-step velocity damping factor
};

struct OrganicGrowthSpec
{
    float SensorDistanceM   = 0.45f; // [m]     - Physarum chemoattractant probe reach
    float SensorAngleRad    = 0.52f; // [rad]   - Left/right sensor spread angle
    float TurnRateRadPerSec = 3.2f;  // [rad/s] - Heading steering speed
};

struct FluidSphSpec
{
    float SmoothingRadiusM  = 0.45f; // [m]   - SPH kernel support radius h
    float RestDensity       = 2.4f;  // [-]   - Reference fluid rest density
    float PressureStiffness = 14.0f; // [-]   - Tait linear pressure stiffness
    float ViscosityCoeff    = 0.25f; // [-]   - Laplacian kinematic viscosity
    float TankHalfWidthM    = 2.6f;  // [m]   - Containment tank half-width
};

struct GranularCollisionSpec
{
    float GrainRadiusMeters = 0.08f; // [m]   - Sphere grain radius
    float Restitution       = 0.42f; // [0-1] - Contact bounce restitution
    float FrictionCoeff     = 0.45f; // [-]   - Coulomb tangential friction
};

struct ElasticStrandSpec
{
    float    RestSegmentMeters = 0.16f; // [m]     - Segment target rest length
    float    StretchStiffness  = 0.65f; // [0-1]   - PBD constraint relaxation stiffness
    uint32_t SolverIterations  = 3u;    // [count] - Distance constraint sweeps per step
};

struct ThermalFireworkSpec
{
    float ThermalRiseMps2   = 2.4f;  // [m/s^2] - Hot gas convective updraft
    float CoolingRatePerSec = 0.85f; // [1/s]   - Exponential cooling decay rate
    bool  EnableCracklePop  = true;  // [-]     - Secondary pyrotechnic crackle jitter near expiry
};

struct DielectricArcSpec
{
    uint8_t  ArcVariant         = 0u;                      // [-]   - 0 Tesla Dual-Sphere, 1 Static Shock Loop, 2 Plasma Cage Collector, 3 Lightning Storm Web
    uint32_t ArcCount           = 1u;                      // [cnt] - Simultaneous main discharge arcs
    float    JitterAmplitudeM   = 0.62f;                   // [m]   - Midpoint lightning displacement jitter (HTML arcJitter)
    float    RefreshRateHz      = 14.0f;                   // [Hz]  - Arc filament snap cadence
    float    ArcBow             = 0.14f;                   // [m]   - Upward thermal arc bow lift (HTML arcBow)
    float    ArcHoldSeconds     = 0.30f;                   // [s]   - Active discharge glow hold window (HTML arcHold)
    float    ArcIntervalSeconds = 1.40f;                   // [s]   - Discharge repetition cadence (HTML arcInterval)
    float    CathodeRadiusMeters= 0.46f;                   // [m]   - Sphere A electrode radius (HTML radius)
    float    AnodeRadiusMeters  = 0.46f;                   // [m]   - Sphere B electrode radius (HTML targetRadius)
    float    CageRadiusMeters   = 1.75f;                   // [m]   - Plasma cage conductor ring radius
    float    BoxHalfMeters[3]   = { 2.1f, 1.5f, 2.1f };    // [m]   - Storm grid 3D volume half-extents
    bool     ArcLoopEnabled     = true;                    // [-]   - Continuous looping strike cadence
    bool     ShowElectrodes     = true;                    // [-]   - Render 3D metallic electrode spheres / cage pylons / storm terminals
};

struct FieldTensorSpec
{
    float EffectorRadiusM        = 1.85f; // [m] - Orbiting spherical effector reach
    float DisplacementAmplitudeM = 0.95f; // [m] - Peak vertical displacement amplitude
};

struct SpiderwebWeaveSpec
{
    bool     MessyAtticCobweb = false;                     // [-]   - False = Orb-Weave radial/spiral, True = Attic Cobweb
    uint32_t SpokeCount       = 18u;                       // [cnt] - Orb-weave radial spokes (HTML webSpokes)
    uint32_t RingCount        = 14u;                       // [cnt] - Orb-weave spiral capture rings (HTML webRings)
    float    SpinRateScale    = 1.0f;                      // [x]   - Live silk extrusion rate (HTML webSpinRate)
    float    SilkSagMeters    = 0.16f;                     // [m]   - Catenary silk gravity droop (HTML webSag)
    float    DewHighlight     = 0.85f;                     // [x]   - Dew drop / dust specular sheen (HTML webDew)
    float    WebRadiusMeters  = 2.0f;                      // [m]   - Orb-weave outer anchor frame radius
    float    BoxHalfMeters[3] = { 2.25f, 1.55f, 1.55f };   // [m]   - Attic cobweb pocket half-extents
    float    FacingDir[3]     = { 0.18f, 0.18f, 0.97f };   // [-]   - Web plane facing normal (HTML dir)
    float    CurlFreqPerM     = 1.1f;                      // [1/m] - Spatial wind-curl frequency
    float    CurlAmpMps       = 0.34f;                     // [m/s] - Wind-curl gust amplitude
    float    WindCoupling     = 1.15f;                     // [x]   - ForceField wind sway coupling
    float    SilkWidthMeters  = 0.015f;                    // [m]   - Base gossamer silk strand diameter
    float    StreakThin       = 0.44f;                     // [x]   - Silk screen-space thinness scale
};

struct FerrofluidMeniscusSpec
{
    bool     LevitatedCrown   = false; // [-]     - False = Petri dish meniscus, True = 3D levitated crown
    uint32_t SpikeCount       = 520u;  // [count] - Rosensweig magnetic spikes
    float    MagnetOrbitRadPs = 1.35f; // [rad/s] - Orbiting magnet angular speed
    bool     FreeDraggableMag = false; // [-]     - True binds magnet to user 3D handle
};

struct FractureFissureSpec
{
    bool  SeismicBasalt    = false; // [-]   - False = Glacial ice cracking, True = Voronoi basalt fracture
    float FrontSpeedMps    = 2.6f;  // [m/s] - Crack front propagation speed
    float HeaveAmplitudeM  = 0.44f; // [m]   - Abyss tilt / heave displacement
};

struct ShallowWaterSplashSpec
{
    uint8_t SplashVariant   = 1u;    // [-]   - 1 Puddle crown, 2 Vehicle tyre splash, 3 Heavy mud splatter
    float   VehicleSpeedMps = 4.2f;  // [m/s] - Figure-8 vehicle tangential speed
    float   TrackRadiusM    = 3.15f; // [m]   - Figure-8 loop radius
};

enum class EffectorPrimitiveShape : uint8_t
{
    HalftoneCircle          = 0u, // [-] - Surface-conforming halftone circle disc
    UniformBox              = 1u, // [-] - Uniformly scaled 3D cube
    HeightExtrudedCube      = 2u, // [-] - Fixed-footprint height-extruded 3D column
    FlatSquareTile          = 3u, // [-] - Surface-conforming beveled square tile
    TrianglePlate           = 4u, // [-] - Surface-conforming equilateral triangle plate
    HexagonPlate            = 5u, // [-] - 3D beveled hexagonal prism plate (Kinetic Hex)
    AutoHalftoneUpperTiles  = 6u  // [-] - Halftone circles on lower surface + extruded raised tiles on upper surface
};

enum class EffectorHostSurfaceCategory : uint8_t
{
    FloorPlaneXZ            = 0u, // [-] - Horizontal XZ matrix (Effector Cube Matrix)
    WallPlaneXY             = 1u, // [-] - Vertical XY wall matrix
    ObsidianSphere          = 2u, // [-] - Sculpted obsidian sphere (Halftone Sphere)
    TorusRing               = 4u, // [-] - Revolving obsidian torus (Kinetic Hex Torus Matrix)
    CubicVolume             = 5u, // [-] - 3D voxel lattice
    ControlVehicle          = 7u  // [-] - Frontier Controlled Integrator (ControlVehicle Body + 4 Rims + 4 Tyres)
};

enum class EffectorBackingFinish : uint8_t
{
    ObsidianGlossy          = 0u, // [-] - Photorealistic wet jet-black obsidian PBR studio finish
    BrushedChrome           = 1u, // [-] - High-contrast brushed chrome finish
    GhostWireframe          = 2u, // [-] - Subtle glowing technical wireframe silhouette
    HiddenBacking           = 3u  // [-] - Show effector tiles only
};

struct EffectorVolumeDescriptor
{
    EffectorVolumeShape Shape        = EffectorVolumeShape::Sphere;
    float               CenterM[3]   = { 0.0f, 1.5f, 0.0f }; // [m] - Effector world centre
    float               RadiusMeters = 1.2f;                 // [m] - Effector reach (0 = inactive)
};

struct EffectorLatticeSpec
{
    EffectorDriverCategory   Driver              = EffectorDriverCategory::FluidSimplexWave;
    EffectorHostSurfaceCategory  HostSurface         = EffectorHostSurfaceCategory::FloorPlaneXZ;
    EffectorPrimitiveShape   PrimitiveShape      = EffectorPrimitiveShape::HeightExtrudedCube;
    EffectorBackingFinish    BackingFinish       = EffectorBackingFinish::ObsidianGlossy;
    EffectorVolumeDescriptor PrimaryVolume       {};
    EffectorVolumeDescriptor SecondaryVolume     { EffectorVolumeShape::Sphere, { 2.5f, 1.5f, 0.0f }, 0.0f };
    uint32_t                 GridColumns         = 24u;      // [cnt] - Columns / angular divisions
    uint32_t                 GridRows            = 24u;      // [cnt] - Rows / minor divisions
    float                    OriginExtentM[3]    = { 1.7f, 1.35f, 1.7f }; // [m] - Half-extent or radius scale
    float                    WaveFrequencyPerM   = 1.25f;    // [1/m] - Spatial effector wave frequency (HTML curlFreq)
    float                    WaveDriveSpeed      = 0.72f;    // [1/s] - Temporal effector wave speed (HTML curlAmp)
    float                    FalloffContrast     = 1.85f;    // [-]   - Power falloff exponent (HTML gridContrast)
    float                    MinScaleFraction    = 0.10f;    // [-]   - Idle scale fraction outside effector zone
    float                    MaxExtrusionMeters  = 0.95f;    // [m]   - Peak extrusion / boost height
    float                    BaseCellWidthMeters = 0.08f;    // [m]   - Nominal primitive lateral size
    float                    VehicleYawDegrees   = 0.0f;     // [deg] - Studio yaw rotation of Controlled Integrator host
    float                    IdleColorLinear[3]  = { 0.010f, 0.013f, 0.019f }; // [lin] - Dark obsidian rest colour (HTML colA)
    float                    MidColorLinear[3]   = { 1.000f, 0.016f, 0.037f }; // [lin] - Transition colour (HTML colC)
    float                    PeakColorLinear[3]  = { 1.000f, 0.246f, 0.058f }; // [lin] - Peak effector flare colour (HTML colB)
    bool                     BlendPlaceable      = false;    // [-]   - True blends manual effectors over wave driver
    bool                     IncludeVehicleWheels= true;     // [-]   - True places effector anchors on rims/tyres as well as body
};

struct FlockBoidsSpec
{
    float NeighbourRadiusM  = 1.4f;  // [m]   - Spatial-hash perception radius
    float CohesionWeight    = 1.15f; // [-]   - Flock centre attraction
    float AlignmentWeight   = 1.65f; // [-]   - Neighbour velocity alignment
    float SeparationWeight  = 2.85f; // [-]   - Close-range repulsion
    float TargetFlightSpeed = 3.4f;  // [m/s] - Cruising flight speed
};

struct MolecularKineticsSpec
{
    float TemperatureKt    = 0.60f;  // [-]   - Reduced thermal energy kT
    float WellDepthEpsilon = 0.15f;  // [-]   - Lennard-Jones epsilon
    float DiameterSigma    = 0.08f;  // [m]   - Atomic diameter sigma
    float LangevinDamping  = 2.0f;   // [1/s] - Thermostat friction
    float StepDurationSec  = 0.004f; // [s]   - Substep dt
    bool  ChemicalReaction = false;  // [-]   - True enables A + B <-> C reaction kinetics
    float ForwardRate      = 4.0f;   // [1/s] - A + B -> C rate
    float DissociationRate = 0.3f;   // [1/s] - C -> A/B rate
    float ContactRadiusM   = 0.12f;  // [m]   - Reaction contact threshold
};

struct TransitionDerezSpec
{
    uint8_t TransitionVariant = 0u;    // [-] - 0 CubeDerez, 1 Shatter, 2 CoinStack, 3 Melt, 4 Reverse, 5 VoxelBlast, 6 Butterfly, 7 Glitch
    float   GlitchRateHz      = 14.0f; // [Hz] - Hologram matrix scanline tick rate
};

struct ShieldBarrierSpec
{
    ShieldBarrierForm Form         = ShieldBarrierForm::SphereDome;
    uint32_t          MotifBitmask = 1u;    // [bits]  - 1 Hex, 2 Circle, 4 Tri, 8 Square, 16 Diamond, 32 Voronoi, 64 Star
    float             SpinRateRad  = 0.20f; // [rad/s] - Pattern rotation speed
};

struct PyroSpectacleSpec
{
    bool  CountermeasureFlares = false; // [-] - True = Military IR decoy V-salvo with smoke ribbons
    float PulseHz              = 0.0f;  // [Hz] - Fireflies / flare optical pulse rate
    float PulseDepth           = 0.0f;  // [0-1] - Optical modulation depth
};

struct InteractiveColliderSpec
{
    bool  BallActive        = false;                  // [-]   - Interactive thrown chrome sphere collider
    float BallCenterM[3]    = { 0.0f, 1.4f, 0.0f };   // [m]   - Sphere centre
    float BallVelocityMps[3]= { 0.0f, 0.0f, 0.0f };   // [m/s] - Sphere linear velocity
    float BallRadiusMeters  = 0.55f;                  // [m]   - Sphere radius
    float BallRestitution   = 0.72f;                  // [0-1] - Floor bounce coefficient
};

/// Composable dynamics ensemble attached to each ParticleRig — only active optional solvers execute.
struct SimulationDynamicsEnsemble
{
    BallisticDynamicsSpec                  Ballistic     {};
    ForceFieldCouplingSpec                 ForceCouple   {};
    std::optional<CurlManifoldSpec>        Curl          {};
    std::optional<StrandConstraintSpec>    Strand        {};
    std::optional<BotanicalSwaySpec>       Botanical     {};
    std::optional<SpiderwebWeaveSpec>      Spiderweb     {};
    std::optional<FerrofluidMeniscusSpec>  Ferrofluid    {};
    std::optional<FractureFissureSpec>     Fracture      {};
    std::optional<ShallowWaterSplashSpec>  Splash        {};
    std::optional<EffectorLatticeSpec>     Effector      {};
    std::optional<FlockBoidsSpec>          Boids         {};
    std::optional<MolecularKineticsSpec>   Molecular     {};
    std::optional<TransitionDerezSpec>     Transition    {};
    std::optional<ShieldBarrierSpec>       Shield        {};
    std::optional<PyroSpectacleSpec>       Pyro          {};
    std::optional<OrbitalGravitySpec>      Orbital       {};
    std::optional<ElectromagneticSpec>     Electro       {};
    std::optional<HarmonicWaveSpec>        Harmonic      {};
    std::optional<AttractorDynamicsSpec>   Attractor     {};
    std::optional<SwarmFlockingSpec>       Swarm         {};
    std::optional<SpeciesLifeSpec>         SpeciesLife   {};
    std::optional<OrganicGrowthSpec> BotanicalGrowth        {};
    std::optional<FluidSphSpec>            FluidSph      {};
    std::optional<GranularCollisionSpec>   Granular      {};
    std::optional<ElasticStrandSpec>       ElasticStrand {};
    std::optional<ThermalFireworkSpec>     Thermal       {};
    std::optional<DielectricArcSpec>       DielectricArc {};
    std::optional<FieldTensorSpec>         FieldMatrix   {};

    [[nodiscard]] uint32_t ComputeActiveBitmask() const noexcept
    {
        uint32_t Bits = 0u;
        if (Strand.has_value())        Bits |= (1u << 0u);
        if (Botanical.has_value())     Bits |= (1u << 1u);
        if (Curl.has_value())          Bits |= (1u << 2u); // Matches ParticleDynCurlBit in ParticleBuffers.slang
        if (Spiderweb.has_value())     Bits |= (1u << 3u);
        if (Ferrofluid.has_value())    Bits |= (1u << 4u);
        if (Fracture.has_value())      Bits |= (1u << 5u);
        if (Splash.has_value())        Bits |= (1u << 6u);
        if (Effector.has_value())      Bits |= (1u << 7u);
        if (Boids.has_value())         Bits |= (1u << 8u);
        if (Molecular.has_value())     Bits |= (1u << 9u);
        if (Transition.has_value())    Bits |= (1u << 10u);
        if (Shield.has_value())        Bits |= (1u << 11u);
        if (Pyro.has_value())          Bits |= (1u << 12u);
        if (Orbital.has_value())       Bits |= (1u << 13u);
        if (Electro.has_value())       Bits |= (1u << 14u);
        if (Harmonic.has_value())      Bits |= (1u << 15u);
        if (Attractor.has_value())     Bits |= (1u << 16u);
        if (Swarm.has_value())         Bits |= (1u << 17u);
        if (SpeciesLife.has_value())   Bits |= (1u << 18u);
        if (BotanicalGrowth.has_value())        Bits |= (1u << 19u);
        if (FluidSph.has_value())      Bits |= (1u << 20u);
        if (Granular.has_value())      Bits |= (1u << 21u);
        if (ElasticStrand.has_value()) Bits |= (1u << 22u);
        if (Thermal.has_value())       Bits |= (1u << 23u);
        if (DielectricArc.has_value()) Bits |= (1u << 24u);
        if (FieldMatrix.has_value())   Bits |= (1u << 25u);
        return Bits;
    }
};

} // namespace Frontier::ParticleSpace
