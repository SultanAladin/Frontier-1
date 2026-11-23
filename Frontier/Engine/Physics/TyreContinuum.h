/*==========================================================================================================================================
                                                    TYRE CONTINUUM
                                     ------------------------------------------------------------------
                      VehicleHMI demo's 160-particle projection and Hex8 deformation solvers.
==========================================================================================================================================*/

// 🧩 Source-grounded tyre deformation. These solvers reproduce the VehicleHMI HTML demo's authored
//    contact targets and return prescribed spring forces, not integrated tyre/road reaction forces.

#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace Frontier
{

constexpr int TyreCircumferentialCount = 32;
constexpr int TyreMeridionalCount = 5;
constexpr int TyreContinuumParticleCount = TyreCircumferentialCount * TyreMeridionalCount;
constexpr int TyreContinuumCellCount = TyreCircumferentialCount * (TyreMeridionalCount - 1);
using TyreContinuumPositions = std::array<float, TyreContinuumParticleCount * 3>;

struct TyreContinuumInput
{
    double Centre[3]       = {0.0, 0.0, 0.0};
    double Orientation[4]  = {0.0, 0.0, 0.0, 1.0};
    double GroundNormal[3] = {0.0, 0.0, 1.0};
    double Deflection = 0.0;
    double Pressure = 220000.0;
    double LateralForce = 0.0;
};

class TyreCarcassProjection
{
public:
    TyreCarcassProjection();
    void Reset(const TyreContinuumInput& Input = TyreContinuumInput());
    double Advance(const TyreContinuumInput& Input, double Seconds, int Substeps = 2);
    const TyreContinuumPositions& QueryRestPositions() const { return m_Rest; }
    const TyreContinuumPositions& QueryLocalPositions() const { return m_Local; }
    const TyreContinuumPositions& QueryWorldPositions() const { return m_World; }
    double QueryVerticalForce() const { return m_VerticalForce; }

private:
    void SyncPositions(const TyreContinuumInput& Input);
    TyreContinuumPositions m_Rest{}, m_Local{}, m_Previous{}, m_Velocity{}, m_World{};
    std::array<float, TyreContinuumParticleCount> m_InverseMass{};
    std::vector<std::uint16_t> m_Pairs;
    std::vector<float> m_Lengths, m_Compliance;
    double m_VerticalForce = 0.0;
};

class TyreCarcassSolid
{
public:
    TyreCarcassSolid();
    void Reset(const TyreContinuumInput& Input = TyreContinuumInput());
    double Advance(const TyreContinuumInput& Input, double Seconds, int Substeps = 2);
    const TyreContinuumPositions& QueryRestPositions() const { return m_Rest; }
    const TyreContinuumPositions& QueryLocalPositions() const { return m_Local; }
    const TyreContinuumPositions& QueryWorldPositions() const { return m_World; }
    double QueryVerticalForce() const { return m_VerticalForce; }
    double QueryMeanJacobian() const { return m_MeanJacobian; }
    double QueryMeanStress() const { return m_MeanStress; }
    int QueryContactCount() const { return m_ContactCount; }
    // 📝 Immutable reference operators for device uploads; these do not depend on the current deformation.
    const std::array<std::uint16_t, TyreContinuumCellCount * 4>& QueryCellCorners() const { return m_Corners; }
    const std::array<float, TyreContinuumCellCount * 12>& QueryCellGradients() const { return m_Gradient; }
    const std::array<float, TyreContinuumCellCount>& QueryCellVolumes() const { return m_Volume; }

private:
    void SyncPositions(const TyreContinuumInput& Input);
    TyreContinuumPositions m_Rest{}, m_InnerRest{}, m_Local{}, m_Velocity{}, m_Forces{}, m_World{};
    std::array<float, TyreContinuumParticleCount> m_InverseMass{};
    std::array<std::uint16_t, TyreContinuumCellCount * 4> m_Corners{};
    std::array<float, TyreContinuumCellCount * 12> m_Gradient{};
    std::array<float, TyreContinuumCellCount> m_Volume{};
    double m_MeanJacobian = 1.0, m_MeanStress = 0.0, m_VerticalForce = 0.0;
    int m_ContactCount = 0;
};

} // namespace Frontier
