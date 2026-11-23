//============================================================================================================================
// 🧩 Frontier — TyreCarcassLattice.h
//============================================================================================================================
// 📝 ⚠️ Provenance, stated plainly: this is NOT a port. The HTML demonstration contains no XPBD tyre — its own text says
//    "there is no XPBD carcass here" and attributes the soft-body lattice to Frontier's C++ code, which is not in this
//    repository. This is a new implementation of that described model — an XPBD particle lattice for the carcass of the
//    same authored tyre (0.50855 m radius, 0.3958 m width, 0.3446 m rim) — calibrated so its radial load/deflection
//    matches the demo's pressure-fed carcass spring K = 25 kN/m + 0.9 N/m/Pa·p (≈223 kN/m at 220 kPa). Structure: tread
//    particles in 28 sectors × 3 rows (two shoulders and the crown) tied to a kinematic rim ring by compliant spokes
//    (a ring on an elastic foundation), belt/lateral/shear distance constraints between tread particles, and one global
//    volume constraint with the enclosed air's bulk compliance V/(γ·p_abs) as shape-keeper, so flattening the patch
//    bulges the sidewalls instead of losing air. Inflation enters as a force, the way it does in a real tyre: each
//    tread particle carries η·p·Aᵢ radially outward (η the calibrated patch-pressure factor), tensioning the spokes
//    and belt — pneumatic stiffness is that tension working over the growing patch. Ground contact is a positional
//    constraint with Coulomb-capped tangential friction; the tyre's normal force is the spokes' summed XPBD reaction
//    on the rim ring (λ·∇C/Δt²), which is what lets the lattice stand in for the spring inside VehicleSolver's
//    strut/tyre series stack. The lattice is solved in the steered wheel frame: the rim ring is
//    axisymmetric, so spin does not change the carcass shape — rolling enters through the contact slip velocity the
//    solver supplies for friction. Rolling hysteresis from material cycling is represented by the lattice's velocity
//    damping, not modelled per-particle.
//============================================================================================================================

#pragma once

namespace Frontier
{

// 📝 One lattice step's boundary conditions, supplied by VehicleSolver each substep.
struct TyreLatticeInput
{
    double HubPosition[3]    = { 0.0, 0.0, 0.0 };   // [m]       world
    double HubOrientation[4] = { 0.0, 0.0, 0.0, 1.0 };  // [x,y,z,w] steered wheel frame (no spin; see header)
    double HubVelocity[3]    = { 0.0, 0.0, 0.0 };   // [m/s]     world, at the hub
    double Spin              = 0.0;                 // [rad/s]   wheel spin, for contact slip velocity
    double GroundHeight      = 0.0;                 // [m]       ground plane height under the hub
    double GroundNormal[3]   = { 0.0, 0.0, 1.0 };   // [-]       unit
    double GaugePressure     = 220000.0;            // [Pa]      from the thermal gas law
    double Seconds           = 1.0 / 480.0;         // [s]
};

struct TyreLatticeOutput
{
    double NormalForce      = 0.0;   // [N]    road normal force on the tyre (summed contact impulse / step)
    double Deflection       = 0.0;   // [m]    radial deflection at the contact (R − hub height over the road)
    double TangentStiffness = 0.0;   // [N/m]  dF/dδ, smoothed — feeds VehicleSolver's stack bisection
    double Volume           = 0.0;   // [m³]   enclosed volume now
    double RestVolume       = 0.0;   // [m³]   enclosed volume of the undeformed authored tyre
};

class TyreCarcassLattice
{
public:
    static const int SectorCount = 28;                       // [-] around the circumference
    static const int RowCount = 3;                           // [-] shoulder, crown, shoulder
    static const int ParticleCount = SectorCount * RowCount; // [-] 84 tread particles

    TyreCarcassLattice();

    // 📝 Undeformed authored tyre at rest in the wheel frame.
    void Reset();

    // 📐 One XPBD step: predict under gravity, iterate spokes/belt/lateral/shear/volume/contact, derive velocities.
    TyreLatticeOutput Step(const TyreLatticeInput& Input);

    // 📝 Tread particle position [m] in the wheel frame (hub at origin), for rendering and proofs.
    void QueryParticle(int Index, double OutPosition[3]) const;

    // 📝 Calibration constants (defaults measured against the demo's carcass spring in the VehiclePhysics exhibit).
    //    Pressure acts the way a real tyre's does: the enclosed air's target volume grows with gauge pressure
    //    (V_target = V₀·(1 + β·p)), so the volume constraint inflates against the spokes and pre-tensions the
    //    carcass — pneumatic stiffness is that tension working over the growing contact patch, not ΔV compression
    //    (a loaded tyre's volume barely changes; the bulge gives the displaced air somewhere to go).
    double PressureVolumeGain = 0.0;    // [1/Pa] target-volume growth per pascal above the reference pressure (0: pure shape-keeper)
    double ReferencePressure = 220000.0; // [Pa]  gauge pressure the rest shape is authored at
    // 📝 Inflation enters as a force: each tread particle carries η·p·Aᵢ radially outward (Aᵢ its tread area share),
    //    applied gradually across the XPBD iterations so spokes/belt tension against it within the same step. η is
    //    the calibrated patch-pressure factor: contact pressure is not uniformly p over the geometric patch, and η
    //    absorbs that distribution (measured against the demo's spring curve in the VehiclePhysics exhibit).
    double PressureForceFactor = 0.16;  // [-]    η, calibrated patch-pressure factor
    double SpokeCompliance  = 4.0e-5;   // [m/N]  radial spokes rim→tread (the elastic foundation under the ring)
    double BeltCompliance   = 2.0e-6;   // [m/N]  circumferential belt
    double LateralCompliance = 2.0e-6;  // [m/N]  across the tread
    double ShearCompliance  = 3.0e-6;   // [m/N]  diagonals
    double DampingPerSecond = 24.0;     // [1/s]  carcass hysteresis velocity damping in the wheel frame
    double FrictionCoefficient = 1.0;   // [-]    contact friction cap (tangential/normal correction)
    int    Iterations       = 12;       // [-]    XPBD iterations per step (deep-deflection convergence)

private:
    void BuildRestShape();
    double ComputeVolume(double* OutGradients) const;  // OutGradients: ParticleCount×3, may be null

    double m_Positions[ParticleCount][3];    // [m] wheel frame
    double m_Predicted[ParticleCount][3];    // [m]
    double m_Velocities[ParticleCount][3];   // [m/s] wheel frame
    double m_RestPositions[ParticleCount][3];// [m]
    double m_RimAnchors[ParticleCount][3];   // [m] kinematic
    double m_InvMass[ParticleCount];         // [1/kg]
    double m_RestVolume = 0.0;               // [m³]
    double m_VolumeSign = 1.0;               // [-]
    double m_LastForce = 0.0;                // [N]
    double m_LastDeflection = 0.0;           // [m]
    double m_TangentStiffness = 223000.0;    // [N/m]
};

} // namespace Frontier
