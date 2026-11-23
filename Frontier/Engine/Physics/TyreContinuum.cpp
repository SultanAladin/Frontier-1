/*==========================================================================================================================================
                                                    TYRE CONTINUUM
                                     ------------------------------------------------------------------
                       VehicleHMI projection and Hex8 solvers, retained JavaScript as oracle.
==========================================================================================================================================*/

#include "Engine/Physics/TyreContinuum.h"

#include <algorithm>
#include <cmath>

namespace Frontier
{
namespace
{
constexpr double Pi = 3.14159265358979323846;
constexpr double RimRadius = 0.3448;
constexpr double OuterRadius = 0.50855;
constexpr double ReferencePressure = 220000.0;
struct Meridian { double Radius, Y, Thickness; bool Mobile; };
constexpr Meridian Profile[5] = {
    {0.34480, -0.115, 0.018, false}, {0.48650, -0.118, 0.016, true},
    {0.50855,  0.000, 0.024, true},  {0.48650,  0.118, 0.016, true},
    {0.34480,  0.115, 0.018, false}};

void Rotate(const double Q[4], const double V[3], double Out[3])
{
    const double Tx = 2.0 * (Q[1] * V[2] - Q[2] * V[1]);
    const double Ty = 2.0 * (Q[2] * V[0] - Q[0] * V[2]);
    const double Tz = 2.0 * (Q[0] * V[1] - Q[1] * V[0]);
    Out[0] = V[0] + Q[3] * Tx + (Q[1] * Tz - Q[2] * Ty);
    Out[1] = V[1] + Q[3] * Ty + (Q[2] * Tx - Q[0] * Tz);
    Out[2] = V[2] + Q[3] * Tz + (Q[0] * Ty - Q[1] * Tx);
}
void Sync(const TyreContinuumInput& Input, const TyreContinuumPositions& Local, TyreContinuumPositions& World)
{
    for (int Particle = 0; Particle < TyreContinuumParticleCount; ++Particle)
    {
        const int B = Particle * 3;
        const double Point[3] = {Local[B], Local[B + 1], Local[B + 2]};
        double Rotated[3]; Rotate(Input.Orientation, Point, Rotated);
        // The demo's RotateVecInto writes Float32Array before adding the centre.
        for (int Axis = 0; Axis < 3; ++Axis)
        {
            World[B + Axis] = static_cast<float>(Rotated[Axis]);
            World[B + Axis] = static_cast<float>(static_cast<double>(World[B + Axis]) + Input.Centre[Axis]);
        }
    }
}
double ContactAngle(const TyreContinuumInput& Input)
{
    const double Q[4] = {-Input.Orientation[0], -Input.Orientation[1], -Input.Orientation[2], Input.Orientation[3]};
    const double Down[3] = {-Input.GroundNormal[0], -Input.GroundNormal[1], -Input.GroundNormal[2]};
    double Local[3]; Rotate(Q, Down, Local);
    return std::atan2(Local[2], Local[0]);
}
double WrappedDistance(double Theta, double Contact)
{
    const double Difference = Theta - Contact;
    return std::abs(Difference - 2.0 * Pi * std::floor(Difference / (2.0 * Pi) + 0.5));
}
// Shared authored target: the solid's amplitudes differ slightly from the projection's.
void Target(double Theta, int M, double Distance, double Span, double Deflection, double PressureRatio,
            double LateralShift, bool Solid, double RestY, double Out[3])
{
    const double Radius = Profile[M].Radius;
    const double Fraction = (Radius - RimRadius) / (OuterRadius - RimRadius);
    double DR = 0.0, DY = 0.0;
    if (Deflection > 1e-5)
    {
        if (Distance < Span)
        {
            const double Envelope = 0.5 * (1.0 + std::cos(Distance / Span * Pi));
            DR = -Deflection * Envelope * Fraction;
            if (M != 2)
                DY += (M == 1 ? -1.0 : 1.0) * Deflection * (Solid ? 0.52 : 0.48) /
                      (0.4 + 0.6 * std::sqrt(PressureRatio)) * Envelope;
        }
        else if (Distance < Span * 1.5)
            DR = std::sin((Distance - Span) / (0.5 * Span) * Pi) *
                 ((Solid ? 0.12 : 0.11) * Deflection * Fraction);
        if (Distance < Span * 1.2)
            DY += LateralShift * 0.5 * (1.0 + std::cos(Distance / (Span * 1.2) * Pi)) * Fraction;
    }
    const double GoalR = std::max(RimRadius + 0.008, Radius + DR);
    Out[0] = GoalR * std::cos(Theta);
    Out[1] = RestY + DY;
    Out[2] = GoalR * std::sin(Theta);
}

// Cofactor matrix divided by determinant; writes float as the reference's Float32Array does.
double InvertTranspose(const double J[9], float Out[9])
{
    const double C[9] = {
        J[4]*J[8]-J[5]*J[7], J[5]*J[6]-J[3]*J[8], J[3]*J[7]-J[4]*J[6],
        J[2]*J[7]-J[1]*J[8], J[0]*J[8]-J[2]*J[6], J[1]*J[6]-J[0]*J[7],
        J[1]*J[5]-J[2]*J[4], J[2]*J[3]-J[0]*J[5], J[0]*J[4]-J[1]*J[3]};
    const double Determinant = J[0]*C[0] + J[1]*C[1] + J[2]*C[2];
    const double Inverse = std::abs(Determinant) > 1e-12 ? 1.0 / Determinant : 0.0;
    for (int I = 0; I < 9; ++I) Out[I] = static_cast<float>(C[I] * Inverse);
    return std::abs(Determinant);
}
} // namespace

//------------------------------------------------------------------------------------------------------------------------
//                                              PROJECTION CARCASS
//------------------------------------------------------------------------------------------------------------------------

TyreCarcassProjection::TyreCarcassProjection()
{
    for (int I = 0; I < TyreCircumferentialCount; ++I)
    {
        const double Theta = 2.0 * Pi * I / TyreCircumferentialCount;
        for (int M = 0; M < TyreMeridionalCount; ++M)
        {
            const int N = I * TyreMeridionalCount + M, B = N * 3;
            m_Rest[B] = static_cast<float>(Profile[M].Radius * std::cos(Theta));
            m_Rest[B + 1] = static_cast<float>(Profile[M].Y);
            m_Rest[B + 2] = static_cast<float>(Profile[M].Radius * std::sin(Theta));
            m_InverseMass[N] = Profile[M].Mobile ? 10.0f : 0.0f;
        }
    }
    auto AddConstraint = [&](int A, int B, double Compliance)
    {
        const int X = A * 3, Y = B * 3;
        const double Dx = static_cast<double>(m_Rest[Y]) - m_Rest[X];
        const double Dy = static_cast<double>(m_Rest[Y + 1]) - m_Rest[X + 1];
        const double Dz = static_cast<double>(m_Rest[Y + 2]) - m_Rest[X + 2];
        m_Pairs.push_back(static_cast<std::uint16_t>(A)); m_Pairs.push_back(static_cast<std::uint16_t>(B));
        m_Lengths.push_back(static_cast<float>(std::sqrt(Dx*Dx + Dy*Dy + Dz*Dz)));
        m_Compliance.push_back(static_cast<float>(Compliance));
    };
    for (int I = 0; I < TyreCircumferentialCount; ++I)
    {
        const int Next = (I + 1) % TyreCircumferentialCount;
        for (int M = 1; M <= 3; ++M) AddConstraint(I*5+M, Next*5+M, M == 2 ? 1.5e-5 : 3.5e-5);
        for (int M = 0; M < 4; ++M) AddConstraint(I*5+M, I*5+M+1, 2.5e-5);
        for (int M = 0; M < 4; ++M) AddConstraint(I*5+M, Next*5+M+1, 6.0e-5);
    }
    Reset();
}
void TyreCarcassProjection::SyncPositions(const TyreContinuumInput& Input) { Sync(Input, m_Local, m_World); }
void TyreCarcassProjection::Reset(const TyreContinuumInput& Input)
{
    m_Local = m_Previous = m_Rest; m_Velocity.fill(0.0f); m_VerticalForce = 0.0; SyncPositions(Input);
}
double TyreCarcassProjection::Advance(const TyreContinuumInput& Input, double Seconds, int Substeps)
{
    if (!(Seconds > 0.0) || Substeps <= 0) return m_VerticalForce;
    const double Deflection = std::max(0.0, Input.Deflection), Pressure = std::max(0.0, Input.Pressure);
    const double PressureRatio = std::max(0.08, Pressure / ReferencePressure);
    const double Step = std::min(Seconds / Substeps, 1.0 / 240.0), InverseStep2 = 1.0 / (Step * Step);
    const double Damping = std::exp(-32.0 * Step), Contact = ContactAngle(Input);
    const double Shift = std::clamp(Input.LateralForce / 145000.0, -0.042, 0.042);
    const double Span = std::clamp(0.44 / std::sqrt(std::max(0.15, PressureRatio)), 0.22, 1.10);
    for (int Iteration = 0; Iteration < Substeps; ++Iteration)
    {
        for (int N = 0; N < TyreContinuumParticleCount; ++N)
        {
            if (m_InverseMass[N] <= 0.0f) continue;
            const int B = N * 3;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                const int K = B + Axis;
                m_Previous[K] = m_Local[K];
                const double Velocity = (m_Velocity[K] + Step * (420.0 * (0.35 + 0.65 * PressureRatio)) *
                                         (static_cast<double>(m_Rest[K]) - m_Local[K])) * Damping;
                m_Local[K] = static_cast<float>(m_Local[K] + Step * Velocity);
            }
        }
        for (size_t C = 0; C < m_Lengths.size(); ++C)
        {
            const int A = m_Pairs[2*C], B = m_Pairs[2*C+1];
            const double WA = m_InverseMass[A], WB = m_InverseMass[B];
            if (WA + WB <= 0.0) continue;
            const int A3 = A*3, B3 = B*3;
            const double D[3] = {static_cast<double>(m_Local[B3])-m_Local[A3],
                                 static_cast<double>(m_Local[B3+1])-m_Local[A3+1],
                                 static_cast<double>(m_Local[B3+2])-m_Local[A3+2]};
            const double Distance = std::sqrt(D[0]*D[0]+D[1]*D[1]+D[2]*D[2]);
            if (Distance < 1e-7) continue;
            const double Scale = -(Distance - m_Lengths[C]) / (WA + WB + m_Compliance[C] * InverseStep2) / Distance;
            for (int Axis = 0; Axis < 3; ++Axis)
            {
                if (WA > 0.0) m_Local[A3+Axis] = static_cast<float>(m_Local[A3+Axis] - WA * Scale * D[Axis]);
                if (WB > 0.0) m_Local[B3+Axis] = static_cast<float>(m_Local[B3+Axis] + WB * Scale * D[Axis]);
            }
        }
        for (int I = 0; I < TyreCircumferentialCount; ++I)
        {
            const double Theta = 2.0 * Pi * I / TyreCircumferentialCount;
            const double Distance = WrappedDistance(Theta, Contact);
            for (int M = 1; M <= 3; ++M)
            {
                const int B = (I*5+M)*3;
                double Goal[3]; Target(Theta, M, Distance, Span, Deflection, PressureRatio, Shift, false, m_Rest[B+1], Goal);
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const int K = B + Axis;
                    m_Local[K] = static_cast<float>(m_Local[K] + (Goal[Axis] - m_Local[K]) * 0.75);
                    m_Velocity[K] = static_cast<float>((static_cast<double>(m_Local[K]) - m_Previous[K]) / Step);
                }
            }
        }
    }
    m_VerticalForce = Deflection * (25000.0 + 0.88 * Pressure); SyncPositions(Input); return m_VerticalForce;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                HEX8 SOLID CARCASS
//------------------------------------------------------------------------------------------------------------------------

TyreCarcassSolid::TyreCarcassSolid()
{
    for (int I = 0; I < TyreCircumferentialCount; ++I)
    {
        const double Theta = 2.0 * Pi * I / TyreCircumferentialCount;
        for (int M = 0; M < 5; ++M)
        {
            const int N = I*5+M, B = N*3;
            m_Rest[B] = static_cast<float>(Profile[M].Radius * std::cos(Theta));
            m_Rest[B+1] = static_cast<float>(Profile[M].Y);
            m_Rest[B+2] = static_cast<float>(Profile[M].Radius * std::sin(Theta));
            const double InnerRadius = Profile[M].Radius - Profile[M].Thickness;
            m_InnerRest[B] = static_cast<float>(InnerRadius * std::cos(Theta));
            m_InnerRest[B+1] = static_cast<float>(Profile[M].Y * 0.94);
            m_InnerRest[B+2] = static_cast<float>(InnerRadius * std::sin(Theta));
            m_InverseMass[N] = Profile[M].Mobile ? (M == 2 ? 0.85f : 1.10f) : 0.0f;
        }
    }
    constexpr int Signs[8][3] = {{-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
                                  {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}};
    int Cell = 0;
    for (int I = 0; I < TyreCircumferentialCount; ++I)
    {
        const int Next = (I+1) % TyreCircumferentialCount;
        for (int M = 0; M < 4; ++M, ++Cell)
        {
            const int Corners[4] = {I*5+M, Next*5+M, Next*5+M+1, I*5+M+1};
            float Coordinates[24];
            for (int C = 0; C < 4; ++C)
            {
                m_Corners[Cell*4+C] = static_cast<std::uint16_t>(Corners[C]);
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    Coordinates[C*3+Axis] = m_InnerRest[Corners[C]*3+Axis];
                    Coordinates[(C+4)*3+Axis] = m_Rest[Corners[C]*3+Axis];
                }
            }
            double J[9]{};
            for (int C = 0; C < 8; ++C)
                for (int Row = 0; Row < 3; ++Row)
                    for (int Col = 0; Col < 3; ++Col)
                        J[Row*3+Col] += Coordinates[C*3+Row] * (0.125 * Signs[C][Col]);
            float Inverse[9];
            const double Det = InvertTranspose(J, Inverse);
            m_Volume[Cell] = static_cast<float>(std::max(1e-6, 8.0 * Det));
            for (int C = 0; C < 4; ++C)
                for (int Row = 0; Row < 3; ++Row)
                    m_Gradient[Cell*12+C*3+Row] = static_cast<float>(
                        Inverse[Row*3] * (0.125*Signs[C+4][0]) + Inverse[Row*3+1] * (0.125*Signs[C+4][1]) +
                        Inverse[Row*3+2] * (0.125*Signs[C+4][2]));
        }
    }
    Reset();
}
void TyreCarcassSolid::SyncPositions(const TyreContinuumInput& Input) { Sync(Input, m_Local, m_World); }
void TyreCarcassSolid::Reset(const TyreContinuumInput& Input)
{
    m_Local = m_Rest; m_Velocity.fill(0.0f); m_MeanJacobian = 1.0; m_MeanStress = 0.0;
    m_ContactCount = 0; m_VerticalForce = 0.0; SyncPositions(Input);
}

double TyreCarcassSolid::Advance(const TyreContinuumInput& Input, double Seconds, int Substeps)
{
    if (!(Seconds > 0.0) || Substeps <= 0) return m_VerticalForce;
    const double Deflection = std::max(0.0, Input.Deflection), Pressure = std::max(0.0, Input.Pressure);
    const double PressureRatio = std::max(0.08, Pressure / ReferencePressure);
    const double Young = 1.2e4 * (0.32 + 0.68 * PressureRatio), Nu = 0.44;
    const double Mu = Young / (2.0 * (1.0 + Nu)), Lambda = Young * Nu / ((1.0 + Nu) * (1.0 - 2.0*Nu));
    const double Step = std::min(Seconds / Substeps, 1.0 / 240.0), Damping = std::exp(-38.0 * Step);
    const double Contact = ContactAngle(Input), Shift = std::clamp(Input.LateralForce / 145000.0, -0.042, 0.042);
    const double Span = std::clamp(0.44 / std::sqrt(std::max(0.15, PressureRatio)), 0.22, 1.10);
    double JSum = 0.0, StressSum = 0.0;
    for (int Iteration = 0; Iteration < Substeps; ++Iteration)
    {
        m_Forces.fill(0.0f); JSum = 0.0; StressSum = 0.0;
        for (int E = 0; E < TyreContinuumCellCount; ++E)
        {
            double F[9] = {1,0,0,0,1,0,0,0,1};
            for (int C = 0; C < 4; ++C)
            {
                const int B = m_Corners[E*4+C]*3, G = E*12+C*3;
                const double U[3] = {static_cast<double>(m_Local[B])-m_Rest[B],
                                     static_cast<double>(m_Local[B+1])-m_Rest[B+1],
                                     static_cast<double>(m_Local[B+2])-m_Rest[B+2]};
                for (int R = 0; R < 3; ++R)
                    for (int Col = 0; Col < 3; ++Col) F[R*3+Col] += U[R] * m_Gradient[G+Col];
            }
            const double C[9] = {F[4]*F[8]-F[5]*F[7], F[5]*F[6]-F[3]*F[8], F[3]*F[7]-F[4]*F[6],
                                 F[2]*F[7]-F[1]*F[8], F[0]*F[8]-F[2]*F[6], F[1]*F[6]-F[0]*F[7],
                                 F[1]*F[5]-F[2]*F[4], F[2]*F[3]-F[0]*F[5], F[0]*F[4]-F[1]*F[3]};
            const double J = std::clamp(F[0]*C[0]+F[1]*C[1]+F[2]*C[2], 0.30, 2.20);
            JSum += J;
            const double Coefficient = (Lambda * J * (J - 1.0) - Mu) / J;
            double P[9]; for (int K = 0; K < 9; ++K) P[K] = Mu*F[K] + Coefficient*C[K];
            StressSum += std::sqrt(P[0]*P[0]+P[4]*P[4]+P[8]*P[8]);
            for (int Corner = 0; Corner < 4; ++Corner)
            {
                const int N = m_Corners[E*4+Corner], B = N*3, G = E*12+Corner*3;
                if (m_InverseMass[N] <= 0.0f) continue;
                for (int R = 0; R < 3; ++R)
                {
                    const double Force = m_Volume[E] * (P[R*3]*m_Gradient[G] + P[R*3+1]*m_Gradient[G+1] +
                                                        P[R*3+2]*m_Gradient[G+2]);
                    m_Forces[B+R] = static_cast<float>(m_Forces[B+R] - Force);
                }
            }
        }
        int Count = 0;
        for (int I = 0; I < TyreCircumferentialCount; ++I)
        {
            const double Theta = 2.0 * Pi * I / TyreCircumferentialCount, Distance = WrappedDistance(Theta, Contact);
            for (int M = 1; M <= 3; ++M)
            {
                const int N = I*5+M, B = N*3;
                double Next[3];
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const int K = B+Axis;
                    const double Displacement = static_cast<double>(m_Local[K]) - m_Rest[K];
                    const double Velocity = (m_Velocity[K] + Step * m_InverseMass[N] *
                        (m_Forces[K] - 450.0 * (0.4 + 0.6 * PressureRatio) * Displacement)) * Damping;
                    Next[Axis] = m_Local[K] + Step * Velocity;
                }
                if (Deflection > 1e-5)
                {
                    if (Distance < Span) ++Count;
                    double Goal[3]; Target(Theta, M, Distance, Span, Deflection, PressureRatio, Shift, true, m_Rest[B+1], Goal);
                    const double Blend = Distance < Span * 1.5 ? 0.78 : 0.45;
                    for (int Axis = 0; Axis < 3; ++Axis) Next[Axis] += (Goal[Axis]-Next[Axis]) * Blend;
                }
                for (int Axis = 0; Axis < 3; ++Axis)
                {
                    const int K = B+Axis;
                    m_Velocity[K] = static_cast<float>((Next[Axis] - m_Local[K]) / Step);
                    m_Local[K] = static_cast<float>(Next[Axis]);
                }
            }
        }
        m_ContactCount = Count;
    }
    m_MeanJacobian = JSum / TyreContinuumCellCount; m_MeanStress = StressSum / TyreContinuumCellCount;
    m_VerticalForce = Deflection * (25000.0 + 0.90 * Pressure); SyncPositions(Input); return m_VerticalForce;
}

} // namespace Frontier
