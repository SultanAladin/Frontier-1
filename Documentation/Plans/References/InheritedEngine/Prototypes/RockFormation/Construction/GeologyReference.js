//========================================================================================================================
//                                              GeologyReference.js                                                🧩
//========================================================================================================================
//
// 📝 Measured field values, kept apart from the specifications that consume them so a default can be
//    traced to its source without reading shader-adjacent code.
//
//    🔴 Provenance discipline. Every entry below is either a CITED measurement or is explicitly marked
//       SELF-TUNED. Nothing in between. A plausible-looking constant with no source is the easiest way
//       to make a procedural model look authoritative while being invented, so the two are never mixed.
//
//    Bed thicknesses are quoted at their measured metre scale. The prototype scene is authored inside a
//    ~14 m bounding radius, so ReferenceToSceneScale maps one to the other in a single place rather
//    than baking a scale factor into each default.

export const ReferenceToSceneScale = 0.0075;                        // [-] - 167 m Claron -> ~1.25 scene units

export const GeologyReference =
{
    //-------------------------------------------------------- bed thicknesses [m] · NPS GRE reports
    ClaronPinkThickness      : 167.0,   // [m] - midpoint of the measured 122-213 m range
    ClaronWhiteThickness     :  45.0,   // [m] - midpoint of the measured 0-91 m range
    SlickRockThickness       :  84.0,   // [m] - midpoint of the measured 61-107 m range
    DeweyBridgeThickness     :  41.0,   // [m] - midpoint of the measured 21-61 m range
    MoabThickness            :  27.0,   // [m] - midpoint of the measured 18-36 m range

    //-------------------------------------------------------- fracture geometry
    // Ji 2022, median spacing-to-thickness ratio over 16 sandstone localities (0.99).
    // Bai & Pollard 2000 give 0.976 as the critical value below which a joint set saturates.
    JointSpacingRatio        :   1.00,  // [-]

    //-------------------------------------------------------- granular repose [deg] · Peytavie 2009
    // 🔴 Controlled by ANGULARITY, not grain size (Carson 1977). An earlier draft of this prototype had
    //    it keyed to grain diameter, which is a real and commonly repeated error.
    ReposeAngleSand          :  32.5,   // [deg] - midpoint of 30-35
    ReposeAngleRocks         :  42.5,   // [deg] - midpoint of 40-45

    //-------------------------------------------------------- arch proportion
    // Landscape Arch: 93 m span carried on a 1.8-6 m ligament.
    ArchApertureToLigament   :   7.0    // [-]
};

// ⚠️ SELF-TUNED. No published measurement was found for either of these after a dedicated search, so
//    they are my own tuning and are labelled as such wherever they surface in the interface. They are
//    kept in a separate table precisely so they cannot be mistaken for the cited values above.
export const SelfTunedReference =
{
    HoodooWaist              :   0.34,  // [-] - waist radius as a fraction of column height
    HoodooCapFlare           :   0.28   // [-] - additional cap radius over waist
};

// 📝 Which defaults are sourced, for the interface to surface honestly on hover. A dial absent from this
//    table is treated as self-tuned rather than assumed cited.
export const DefaultProvenance =
{
    "StratumBand.thickness"  : "NPS GRE bed thickness, Claron pink member 122-213 m",
    "JointNetwork.spacing"   : "Ji 2022 median S/T = 0.99 over 16 sandstone localities",
    "JointNetwork.bearing"   : "Arches NP field geometry, joint sets meeting near 35 deg",
    "ApertureCarve.span"     : "Landscape Arch 93 m span over a 1.8-6 m ligament",
    "HoodooMass.height"      : "Bryce Canyon measured hoodoo heights, under 12 m to over 61 m",
    "HoodooMass.waist"       : "⚠️ self-tuned — no published measurement found",
    "HoodooMass.capFlare"    : "⚠️ self-tuned — no published measurement found",
    "MesaMass.batter"        : "Ward & Anderson: strong caprock holds the scarp near-vertical",
    "BoulderMass.faceCount"  : "Joshua Tree: three orthogonal joint sets cut rectangular blocks"
};
