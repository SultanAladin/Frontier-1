//============================================================================================================================
// 🧩 Frontier — TyreCarcassLattice.cpp
//============================================================================================================================
// 📝 XPBD carcass lattice (new model — see the header for provenance). Distance constraints with compliance
//    (Macklin et al., XPBD: λ with α̃ = α/Δt²), a global enclosed-volume constraint with air-bulk compliance, and
//    positional ground contact with Coulomb friction. Wheel frame: hub at origin, X forward, Y along the axle, Z up.
//============================================================================================================================

#include "Engine/Physics/TyreCarcassLattice.h"

#include <cmath>

namespace Frontier
{

namespace
{
    constexpr double Pi = 3.141592653589793;
    constexpr double TyreRadius = 0.50855;        // [m] authored (VehicleGeometry)
    constexpr double RimRadius = 0.34460;         // [m]
    constexpr double RowOffsets[3] = { -0.13, 0.0, 0.13 };        // [m] shoulder inset within the 0.3958 m width
    constexpr double RowRadii[3] = { TyreRadius - 0.006, TyreRadius, TyreRadius - 0.006 };  // [m] profile roundness
    constexpr double TreadMassTotal = 9.0;        // [kg] tread band share of the 20 kg unsprung corner
    constexpr double AirGamma = 1.4;              // [-]  adiabatic index of air
    constexpr double AtmosphericPressure = 101325.0;  // [Pa]

    void Rotate(const double Q[4], const double V[3], double Out[3])
    {
        const double Axis[3] = { Q[0], Q[1], Q[2] };
        double Twice[3] = { 2.0 * (Axis[1] * V[2] - Axis[2] * V[1]),
                            2.0 * (Axis[2] * V[0] - Axis[0] * V[2]),
                            2.0 * (Axis[0] * V[1] - Axis[1] * V[0]) };
        Out[0] = V[0] + Q[3] * Twice[0] + (Axis[1] * Twice[2] - Axis[2] * Twice[1]);
        Out[1] = V[1] + Q[3] * Twice[1] + (Axis[2] * Twice[0] - Axis[0] * Twice[2]);
        Out[2] = V[2] + Q[3] * Twice[2] + (Axis[0] * Twice[1] - Axis[1] * Twice[0]);
    }

    double Clamp(double V, double Lo, double Hi)
    {
        return V < Lo ? Lo : (V > Hi ? Hi : V);
    }

    int ParticleIndex(int Sector, int Row)
    {
        const int S = ((Sector % TyreCarcassLattice::SectorCount) + TyreCarcassLattice::SectorCount)
                    % TyreCarcassLattice::SectorCount;
        return S * TyreCarcassLattice::RowCount + Row;
    }
}

TyreCarcassLattice::TyreCarcassLattice()
{
    BuildRestShape();
    Reset();
}

void TyreCarcassLattice::BuildRestShape()
{
    for (int Sector = 0; Sector < SectorCount; ++Sector)
    {
        const double Angle = 2.0 * Pi * Sector / SectorCount;      // [rad] 0 = +X (forward), toward +Z
        const double Cos = std::cos(Angle), Sin = std::sin(Angle);
        for (int Row = 0; Row < RowCount; ++Row)
        {
            const int Index = ParticleIndex(Sector, Row);
            m_RestPositions[Index][0] = RowRadii[Row] * Cos;
            m_RestPositions[Index][1] = RowOffsets[Row];
            m_RestPositions[Index][2] = RowRadii[Row] * Sin;
            m_RimAnchors[Index][0] = RimRadius * Cos;
            m_RimAnchors[Index][1] = RowOffsets[Row] * 0.8;
            m_RimAnchors[Index][2] = RimRadius * Sin;
            m_InvMass[Index] = ParticleCount / TreadMassTotal;     // [1/kg]
        }
    }
    // 📝 Rest volume and winding sign from the undeformed shape.
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
            m_Positions[Index][Axis] = m_RestPositions[Index][Axis];
    const double Signed = ComputeVolume(nullptr);
    m_VolumeSign = Signed < 0.0 ? -1.0 : 1.0;
    m_RestVolume = std::fabs(Signed);
}

void TyreCarcassLattice::Reset()
{
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            m_Positions[Index][Axis] = m_RestPositions[Index][Axis];
            m_Predicted[Index][Axis] = m_RestPositions[Index][Axis];
            m_Velocities[Index][Axis] = 0.0;
        }
    m_LastForce = 0.0;
    m_LastDeflection = 0.0;
    m_TangentStiffness = 223000.0;
}

// 📐 Enclosed volume by the divergence theorem over the closed tread/rim surface: tread band quads between rows, side
//    walls from the shoulder rows to their rim anchors, and the rim band between anchor rows (kinematic but included so
//    the surface is closed). Gradients ∂V/∂p accumulate ⅓ factor-weighted triangle terms for tread particles.
double TyreCarcassLattice::ComputeVolume(double* OutGradients) const
{
    if (OutGradients)
        for (int Index = 0; Index < ParticleCount * 3; ++Index)
            OutGradients[Index] = 0.0;
    double Volume6 = 0.0;   // 6·V accumulator (a·(b×c) sums)
    const auto Triangle = [&](const double A[3], const double B[3], const double C[3],
                              int IndexA, int IndexB, int IndexC)
    {
        const double Cross[3] = { B[1] * C[2] - B[2] * C[1], B[2] * C[0] - B[0] * C[2], B[0] * C[1] - B[1] * C[0] };
        Volume6 += A[0] * Cross[0] + A[1] * Cross[1] + A[2] * Cross[2];
        if (!OutGradients)
            return;
        // ∂(a·(b×c))/∂a = b×c, ∂/∂b = c×a, ∂/∂c = a×b.
        const double GradB[3] = { C[1] * A[2] - C[2] * A[1], C[2] * A[0] - C[0] * A[2], C[0] * A[1] - C[1] * A[0] };
        const double GradC[3] = { A[1] * B[2] - A[2] * B[1], A[2] * B[0] - A[0] * B[2], A[0] * B[1] - A[1] * B[0] };
        const int Indices[3] = { IndexA, IndexB, IndexC };
        const double* Grads[3] = { Cross, GradB, GradC };
        for (int Corner = 0; Corner < 3; ++Corner)
        {
            if (Indices[Corner] < 0)
                continue;
            for (int Axis = 0; Axis < 3; ++Axis)
                OutGradients[Indices[Corner] * 3 + Axis] += Grads[Corner][Axis] / 6.0;
        }
    };
    const auto Quad = [&](const double A[3], const double B[3], const double C[3], const double D[3],
                          int Ia, int Ib, int Ic, int Id)
    {
        Triangle(A, B, C, Ia, Ib, Ic);
        Triangle(A, C, D, Ia, Ic, Id);
    };
    for (int Sector = 0; Sector < SectorCount; ++Sector)
    {
        const int Next = (Sector + 1) % SectorCount;
        for (int Row = 0; Row + 1 < RowCount; ++Row)
        {
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            const int C = ParticleIndex(Next, Row + 1), D = ParticleIndex(Sector, Row + 1);
            Quad(m_Positions[A], m_Positions[B], m_Positions[C], m_Positions[D], A, B, C, D);
        }
        for (int Side = 0; Side < 2; ++Side)
        {
            const int Row = Side == 0 ? 0 : RowCount - 1;
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            Quad(m_Positions[A], m_Positions[B], m_RimAnchors[B], m_RimAnchors[A], A, B, -1, -1);
        }
        for (int Row = 0; Row + 1 < RowCount; ++Row)
        {
            const int A = ParticleIndex(Sector, Row), B = ParticleIndex(Next, Row);
            const int C = ParticleIndex(Next, Row + 1), D = ParticleIndex(Sector, Row + 1);
            Quad(m_RimAnchors[A], m_RimAnchors[B], m_RimAnchors[C], m_RimAnchors[D], -1, -1, -1, -1);
        }
    }
    return Volume6 / 6.0;
}

TyreLatticeOutput TyreCarcassLattice::Step(const TyreLatticeInput& Input)
{
    const double Dt = Input.Seconds;
    TyreLatticeOutput Output;
    Output.RestVolume = m_RestVolume;
    if (!(Dt > 0.0))
        return Output;

    // 📝 Gravity in the wheel frame; hub motion reaches the particles through the spoke constraints to the rim ring.
    double InverseQ[4] = { -Input.HubOrientation[0], -Input.HubOrientation[1], -Input.HubOrientation[2], Input.HubOrientation[3] };
    const double GravityWorld[3] = { 0.0, 0.0, -9.81 };
    double Gravity[3];
    Rotate(InverseQ, GravityWorld, Gravity);

    // ① predict
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            m_Velocities[Index][Axis] += Gravity[Axis] * Dt;
            m_Predicted[Index][Axis] = m_Positions[Index][Axis] + m_Velocities[Index][Axis] * Dt;
        }

    // 📝 XPBD distance constraint solve on m_Predicted (Anchor weight zero when IndexB < 0, RestOverride via arrays).
    const auto SolveDistance = [&](int IndexA, const double PointB[3], double InvMassB, double Rest, double Compliance,
                                   double& Lambda)
    {
        double Delta[3] = { m_Predicted[IndexA][0] - PointB[0], m_Predicted[IndexA][1] - PointB[1],
                            m_Predicted[IndexA][2] - PointB[2] };
        const double Length = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
        if (Length < 1.0e-12)
            return;
        const double C = Length - Rest;
        const double AlphaTilde = Compliance / (Dt * Dt);
        const double WA = m_InvMass[IndexA];
        const double DeltaLambda = (-C - AlphaTilde * Lambda) / (WA + InvMassB + AlphaTilde);
        Lambda += DeltaLambda;
        const double Scale = DeltaLambda * WA / Length;
        for (int Axis = 0; Axis < 3; ++Axis)
            m_Predicted[IndexA][Axis] += Scale * Delta[Axis];
    };

    // 📝 Constraint list built once per step with per-constraint λ (XPBD accumulates λ across iterations of one step).
    struct DistanceConstraint { int A; int B; double Rest; double Compliance; double Lambda; };
    static thread_local DistanceConstraint Constraints[1024];
    int ConstraintCount = 0;
    const auto RestLength = [&](const double A[3], const double B[3])
    {
        const double Dx = A[0] - B[0], Dy = A[1] - B[1], Dz = A[2] - B[2];
        return std::sqrt(Dx * Dx + Dy * Dy + Dz * Dz);
    };
    const auto AddConstraint = [&](int A, int B, double Compliance)
    {
        DistanceConstraint& C = Constraints[ConstraintCount++];
        C.A = A; C.B = B; C.Compliance = Compliance; C.Lambda = 0.0;
        C.Rest = B < 0 ? RestLength(m_RestPositions[A], m_RimAnchors[A])
                       : RestLength(m_RestPositions[A], m_RestPositions[B]);
    };
    for (int Sector = 0; Sector < SectorCount; ++Sector)
        for (int Row = 0; Row < RowCount; ++Row)
        {
            const int Index = ParticleIndex(Sector, Row);
            AddConstraint(Index, -1, SpokeCompliance);                                   // spoke to rim anchor
            AddConstraint(Index, ParticleIndex(Sector + 1, Row), BeltCompliance);        // belt
            if (Row + 1 < RowCount)
            {
                AddConstraint(Index, ParticleIndex(Sector, Row + 1), LateralCompliance); // across
                AddConstraint(Index, ParticleIndex(Sector + 1, Row + 1), ShearCompliance); // diagonals
                AddConstraint(ParticleIndex(Sector + 1, Row), ParticleIndex(Sector, Row + 1), ShearCompliance);
            }
        }

    double VolumeLambda = 0.0;
    const double PressureAbs = AtmosphericPressure + std::fmax(0.0, Input.GaugePressure);       // [Pa]
    const double VolumeCompliance = m_RestVolume / (AirGamma * PressureAbs);                    // [m³/Pa] air bulk compliance
    double ContactImpulse = 0.0;                                                                // [N·s]

    // ② iterate
    for (int Iteration = 0; Iteration < Iterations; ++Iteration)
    {
        // 📐 Inflation force η·p·Aᵢ radially outward, spread across the iterations (a = F/m, Δx = a·Δt²/N each pass).
        if (Input.GaugePressure > 0.0 && PressureForceFactor > 0.0)
        {
            for (int Index = 0; Index < ParticleCount; ++Index)
            {
                const int Row = Index % RowCount;
                const double Area = (2.0 * Pi * RowRadii[Row] / SectorCount) * 0.131;                    // [m²]
                const double Push = PressureForceFactor * Input.GaugePressure * Area * m_InvMass[Index]
                                  * Dt * Dt / Iterations;                                                // [m]
                const double Radial = std::sqrt(m_Predicted[Index][0] * m_Predicted[Index][0]
                                              + m_Predicted[Index][2] * m_Predicted[Index][2]);
                if (Radial > 1.0e-9)
                {
                    m_Predicted[Index][0] += Push * m_Predicted[Index][0] / Radial;
                    m_Predicted[Index][2] += Push * m_Predicted[Index][2] / Radial;
                }
            }
        }
        for (int C = 0; C < ConstraintCount; ++C)
        {
            DistanceConstraint& Constraint = Constraints[C];
            if (Constraint.B < 0)
                SolveDistance(Constraint.A, m_RimAnchors[Constraint.A], 0.0, Constraint.Rest, Constraint.Compliance,
                              Constraint.Lambda);
            else
            {
                // 📝 Symmetric two-particle solve: apply half the standard XPBD update from each side.
                double LambdaB = Constraint.Lambda;
                SolveDistance(Constraint.A, m_Predicted[Constraint.B], m_InvMass[Constraint.B], Constraint.Rest,
                              Constraint.Compliance, Constraint.Lambda);
                // Move B by the same Δλ along the (updated) direction.
                double Delta[3] = { m_Predicted[Constraint.B][0] - m_Predicted[Constraint.A][0],
                                    m_Predicted[Constraint.B][1] - m_Predicted[Constraint.A][1],
                                    m_Predicted[Constraint.B][2] - m_Predicted[Constraint.A][2] };
                const double Length = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
                if (Length > 1.0e-12)
                {
                    const double DeltaLambda = Constraint.Lambda - LambdaB;
                    const double Scale = DeltaLambda * m_InvMass[Constraint.B] / Length;
                    for (int Axis = 0; Axis < 3; ++Axis)
                        m_Predicted[Constraint.B][Axis] += Scale * Delta[Axis];
                }
            }
        }
        // 📐 Volume constraint C = V − V₀ with air compliance: Δλ = (−C − α̃λ)/(Σ w|∇C|² + α̃).
        {
            // Volume/gradients are computed on m_Positions layout; swap predicted in temporarily.
            double Saved[ParticleCount][3];
            for (int Index = 0; Index < ParticleCount; ++Index)
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Saved[Index][Axis] = m_Positions[Index][Axis];
                    m_Positions[Index][Axis] = m_Predicted[Index][Axis];
                }
            double Gradients[ParticleCount * 3];
            const double Volume = m_VolumeSign * ComputeVolume(Gradients);
            for (int Index = 0; Index < ParticleCount; ++Index)
                for (int Axis = 0; Axis < 3; ++Axis)
                    m_Positions[Index][Axis] = Saved[Index][Axis];
            // 📝 The rest shape IS the tyre at the reference pressure: the target grows above it and shrinks below
            //    it, so a deflated carcass is pulled slack (soft, sagging) and an over-inflated one tensions up.
            const double TargetVolume = m_RestVolume * std::fmax(0.55, 1.0 + PressureVolumeGain
                                                                                 * (Input.GaugePressure - ReferencePressure));
            const double C = Volume - TargetVolume;
            const double AlphaTilde = VolumeCompliance / (Dt * Dt);
            double Denominator = AlphaTilde;
            for (int Index = 0; Index < ParticleCount; ++Index)
            {
                double Gradient[3] = { m_VolumeSign * Gradients[Index * 3 + 0], m_VolumeSign * Gradients[Index * 3 + 1],
                                       m_VolumeSign * Gradients[Index * 3 + 2] };
                Denominator += m_InvMass[Index] * (Gradient[0] * Gradient[0] + Gradient[1] * Gradient[1] + Gradient[2] * Gradient[2]);
            }
            const double DeltaLambda = (-C - AlphaTilde * VolumeLambda) / Denominator;
            VolumeLambda += DeltaLambda;
            for (int Index = 0; Index < ParticleCount; ++Index)
                for (int Axis = 0; Axis < 3; ++Axis)
                    m_Predicted[Index][Axis] += DeltaLambda * m_InvMass[Index] * m_VolumeSign * Gradients[Index * 3 + Axis];
        }
        // 📐 Ground contact: positional projection along the normal with Coulomb-capped tangential friction. The
        //    contact force is the PBD estimate Σ m·Δx/Δt² over the corrections of ALL iterations: at rest under load
        //    the spokes push the tread into the road every iteration and the contact pushes back, so the cumulative
        //    correction is the impulse the road applies — the final iteration's residue alone would read only sag.
        for (int Index = 0; Index < ParticleCount; ++Index)
        {
            double World[3];
            Rotate(Input.HubOrientation, m_Predicted[Index], World);
            World[0] += Input.HubPosition[0]; World[1] += Input.HubPosition[1]; World[2] += Input.HubPosition[2];
            const double Penetration = Input.GroundHeight - World[2];
            if (Penetration <= 0.0)
                continue;
            double CorrectionWorld[3] = { Input.GroundNormal[0] * Penetration, Input.GroundNormal[1] * Penetration,
                                          Input.GroundNormal[2] * Penetration };
            // Friction: oppose the particle's tangential slip motion this step (hub travel + spin surface motion),
            // capped at μ × the normal correction (Coulomb cone, positional form).
            double SlipTravel[3] = { Input.HubVelocity[0] * Dt, Input.HubVelocity[1] * Dt, Input.HubVelocity[2] * Dt };
            const double NormalTravel = SlipTravel[0] * Input.GroundNormal[0] + SlipTravel[1] * Input.GroundNormal[1]
                                      + SlipTravel[2] * Input.GroundNormal[2];
            double Tangential[3] = { SlipTravel[0] - Input.GroundNormal[0] * NormalTravel,
                                     SlipTravel[1] - Input.GroundNormal[1] * NormalTravel,
                                     SlipTravel[2] - Input.GroundNormal[2] * NormalTravel };
            const double TangentialLength = std::sqrt(Tangential[0] * Tangential[0] + Tangential[1] * Tangential[1]
                                                    + Tangential[2] * Tangential[2]);
            if (TangentialLength > 1.0e-12)
            {
                const double Cap = FrictionCoefficient * Penetration;
                const double Scale = -std::fmin(TangentialLength, Cap) / TangentialLength;
                CorrectionWorld[0] += Tangential[0] * Scale;
                CorrectionWorld[1] += Tangential[1] * Scale;
                CorrectionWorld[2] += Tangential[2] * Scale;
            }
            double CorrectionLocal[3];
            Rotate(InverseQ, CorrectionWorld, CorrectionLocal);
            for (int Axis = 0; Axis < 3; ++Axis)
                m_Predicted[Index][Axis] += CorrectionLocal[Axis];
            ContactImpulse += (1.0 / m_InvMass[Index]) * Penetration / Dt;   // [N·s] ≈ m·Δx/Δt, all iterations
        }
    }

    // ③ velocities from positions, carcass hysteresis damping in the wheel frame
    const double DampingFactor = std::exp(-DampingPerSecond * Dt);
    for (int Index = 0; Index < ParticleCount; ++Index)
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            m_Velocities[Index][Axis] = (m_Predicted[Index][Axis] - m_Positions[Index][Axis]) / Dt * DampingFactor;
            m_Positions[Index][Axis] = m_Predicted[Index][Axis];
        }

    (void)ContactImpulse;
    // 📐 The force the vehicle feels is the spokes' reaction on the rim ring: per spoke the XPBD constraint force on
    //    the particle is λ·∇C/Δt², so the rim feels its opposite. Summed and rotated to world, the vertical part is
    //    the corner load the tyre carries — bottom spokes in compression push the rim up, top spokes in tension pull
    //    it down, and pressure pre-tension enters through both. (The road-side contact impulse balances this at
    //    equilibrium but makes a noisier measurement under Gauss–Seidel iteration.)
    double RimReaction[3] = { 0.0, 0.0, 0.0 };                                                    // [N] wheel frame
    for (int C = 0; C < ConstraintCount; ++C)
    {
        const DistanceConstraint& Constraint = Constraints[C];
        if (Constraint.B >= 0)
            continue;
        const double* Anchor = m_RimAnchors[Constraint.A];
        double Delta[3] = { m_Predicted[Constraint.A][0] - Anchor[0], m_Predicted[Constraint.A][1] - Anchor[1],
                            m_Predicted[Constraint.A][2] - Anchor[2] };
        const double Length = std::sqrt(Delta[0] * Delta[0] + Delta[1] * Delta[1] + Delta[2] * Delta[2]);
        if (Length < 1.0e-12)
            continue;
        const double Scale = -Constraint.Lambda / (Dt * Dt) / Length;
        RimReaction[0] += Scale * Delta[0];
        RimReaction[1] += Scale * Delta[1];
        RimReaction[2] += Scale * Delta[2];
    }
    double RimReactionWorld[3];
    Rotate(Input.HubOrientation, RimReaction, RimReactionWorld);
    Output.NormalForce = RimReactionWorld[2];                                                     // [N]
    Output.Volume = m_VolumeSign * ComputeVolume(nullptr);
    // 📝 Deflection: how far the road plane has pushed into the tyre along the normal at the hub ground track.
    const double HubHeight = Input.HubPosition[2] - Input.GroundHeight;
    Output.Deflection = Clamp(TyreRadius - HubHeight, 0.0, TyreRadius - RimRadius);
    // 📝 Tangent stiffness for the vehicle stack solve: smoothed finite difference, clamped to a sane band.
    const double DeltaDeflection = Output.Deflection - m_LastDeflection;
    if (std::fabs(DeltaDeflection) > 2.0e-4)
    {
        const double Measured = (Output.NormalForce - m_LastForce) / DeltaDeflection;
        if (Measured > 0.0)
            m_TangentStiffness = Clamp(0.85 * m_TangentStiffness + 0.15 * Measured, 15000.0, 600000.0);
        m_LastDeflection = Output.Deflection;
        m_LastForce = Output.NormalForce;
    }
    Output.TangentStiffness = m_TangentStiffness;
    return Output;
}

void TyreCarcassLattice::QueryParticle(int Index, double OutPosition[3]) const
{
    OutPosition[0] = m_Positions[Index][0];
    OutPosition[1] = m_Positions[Index][1];
    OutPosition[2] = m_Positions[Index][2];
}

} // namespace Frontier
