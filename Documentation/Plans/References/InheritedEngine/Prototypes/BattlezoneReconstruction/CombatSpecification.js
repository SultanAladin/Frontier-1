/*====================================================================================================================================
                                                   COMBATSPECIFICATION.JS
====================================================================================================================================*/
// 🧩 Chassis-class profiles, armament profiles, and field constants driving the armoured-combat reconstruction

//------------------------------------------------------------------------------------------------------------------------
//                                                     FIELD CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

export const FieldSpecification =
{
    GroundHalfExtent:      420.0,      // [m]   - Half-width of the playable plain; chassis are constrained inside
    TerrainCellEdge:         6.0,      // [m]   - Ground tessellation spacing; must stay well under a crater rim's
                                       //         width or the rims alias into the surrounding plain (140x140 cells)
    TerrainReliefAmplitude:  2.1,      // [m]   - Unit scale for the relief octaves; craters derive depth from radius
    GravityAcceleration:    24.0,      // [m/s²]- Exaggerated fall rate; keeps arcing shells readable at play speed
    ObstacleCount:            46,      // [-]   - Pyramids and blocks scattered as cover
    HorizonColour:  [0.02, 0.05, 0.07],// [0-1] - Deep night sky the vector field sits against
    SunDirection:  [0.42, 0.78, 0.46]  // [-]   - Normalized in the shader; low key light for long chassis shadows
};

// 📝 Distinct silhouettes matter more than raw statistics here — the class must be identifiable at radar range,
//    so proportion, height and barrel length are part of the profile rather than cosmetic afterthoughts.
export const ChassisClassRegistry =
{
    Light:
    {
        ClassLabel:          "LIGHT SCOUT",
        ArmourCapacity:            55.0,   // [-]     - Absorbed damage before destruction
        ForwardSpeed:              27.0,   // [m/s]   - Top forward rate
        ReverseSpeed:              15.0,   // [m/s]   - Top reverse rate
        ChassisTurnRate:            2.15,  // [rad/s] - Hull yaw authority
        TurretTurnRate:             2.60,  // [rad/s] - Turret slew relative to hull
        AccelerationRate:          22.0,   // [m/s²]  - Throttle response
        HullExtent:        [1.55, 0.78, 2.55], // [m] - Half-extents of the hull box
        TurretRadius:               0.86,  // [m]     - Turret drum radius
        TurretHeight:               0.56,  // [m]     - Turret drum height
        BarrelLength:               2.30,  // [m]     - Cannon protrusion
        BarrelRadius:               0.105, // [m]     - Cannon calibre
        TrackHeight:                0.40,  // [m]     - Track skirt height
        SightElevation:             1.92,  // [m]     - First-person eye height above the chassis origin
        ChassisTone:        [0.35, 0.95, 0.62],// [0-1]- Vector-glow hull tone
        ThreatRating:                0.55  // [-]     - Aggression weighting used by opponent pursuit
    },
    Medium:
    {
        ClassLabel:          "MEDIUM LINE",
        ArmourCapacity:           105.0,
        ForwardSpeed:              19.5,
        ReverseSpeed:              10.5,
        ChassisTurnRate:            1.45,
        TurretTurnRate:             1.85,
        AccelerationRate:          14.0,
        HullExtent:        [1.95, 0.94, 3.10],
        TurretRadius:               1.10,
        TurretHeight:               0.70,
        BarrelLength:               3.05,
        BarrelRadius:               0.140,
        TrackHeight:                0.50,
        SightElevation:             2.28,
        ChassisTone:        [0.42, 0.86, 1.00],
        ThreatRating:               0.75
    },
    Heavy:
    {
        ClassLabel:          "HEAVY SIEGE",
        ArmourCapacity:           190.0,
        ForwardSpeed:              13.0,
        ReverseSpeed:               7.0,
        ChassisTurnRate:            0.92,
        TurretTurnRate:             1.20,
        AccelerationRate:           9.0,
        HullExtent:        [2.45, 1.16, 3.85],
        TurretRadius:               1.42,
        TurretHeight:               0.90,
        BarrelLength:               4.05,
        BarrelRadius:               0.190,
        TrackHeight:                0.62,
        SightElevation:             2.74,
        ChassisTone:        [1.00, 0.64, 0.30],
        ThreatRating:               0.95
    }
};

export const ChassisClassOrder = ["Light", "Medium", "Heavy"];

//------------------------------------------------------------------------------------------------------------------------
//                                                   ARMAMENT PROFILES
//------------------------------------------------------------------------------------------------------------------------

// 💡 Three armaments deliberately occupy three different aiming skills: flat-trajectory precision, slow heavy
//    impact with splash, and an indirect arc that reaches over the block obstacles.
export const ArmamentRegistry =
{
    Cannon:
    {
        ArmamentLabel:      "PLASMA CANNON",
        ProjectileDamage:         26.0,   // [-]     - Armour removed per impact
        MuzzleSpeed:             165.0,   // [m/s]   - Launch speed along the barrel
        ReloadInterval:            0.42,  // [s]     - Minimum spacing between shots
        GravityScale:              0.0,   // [-]     - Flat trajectory
        SplashRadius:              0.0,   // [m]     - Single-target
        ProjectileRadius:          0.22,  // [m]     - Rendered bolt radius
        ProjectileTone:    [0.55, 1.00, 0.75],// [0-1]
        MagazineCapacity:          Infinity, // [-]  - The default armament never depletes
        LifetimeCeiling:           3.0    // [s]     - Despawn horizon
    },
    Rocket:
    {
        ArmamentLabel:     "ROCKET BATTERY",
        ProjectileDamage:         58.0,
        MuzzleSpeed:              98.0,
        ReloadInterval:            1.15,
        GravityScale:              0.18,
        SplashRadius:              7.5,
        ProjectileRadius:          0.34,
        ProjectileTone:    [1.00, 0.52, 0.28],
        MagazineCapacity:           14,
        LifetimeCeiling:           5.0
    },
    ArcMortar:
    {
        ArmamentLabel:       "ARC MORTAR",
        ProjectileDamage:         74.0,
        MuzzleSpeed:              72.0,
        ReloadInterval:            1.70,
        GravityScale:              1.00,
        SplashRadius:             11.0,
        ProjectileRadius:          0.40,
        ProjectileTone:    [0.86, 0.44, 1.00],
        MagazineCapacity:            9,
        LifetimeCeiling:           7.5
    }
};

export const ArmamentOrder = ["Cannon", "Rocket", "ArcMortar"];

// Collectable armament crates seeded on the field; the Cannon is innate so only the two scarce armaments drop.
export const ArmamentDropOrder = ["Rocket", "ArcMortar"];

export const PickupSpecification =
{
    CollectionRadius:        4.6,   // [m] - Ground distance at which a chassis absorbs the crate
    ReplenishInterval:      16.0,   // [s] - Delay before a collected crate returns
    CrateHalfExtent:         1.05,  // [m] - Rendered crate size
    HoverAmplitude:          0.42,  // [m] - Vertical bob amplitude
    HoverRate:               1.8    // [rad/s] - Bob angular rate
};
