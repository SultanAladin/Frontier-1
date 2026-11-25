/*============================================================================================================================================
                                                     VEHICLEREADINGCONVERTER.CPP
============================================================================================================================================*/
// 🧩 Phase D21 Native PhysicsSpace::VehicleReading -> VehicleChassisCouplingInput converter & two-way Jolt impulse feedback

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/SimulationSpace/Deformable/VehicleReadingConverter.h"

#include <algorithm>
#include <cmath>

namespace Frontier::SimulationSpace::Deformable
{

//------------------------------------------------------------------------------------------------------------------------
//                                                VEHICLE READING CONVERTER
//------------------------------------------------------------------------------------------------------------------------

VehicleChassisCouplingInput VehicleReadingConverter::ConvertFromPhysicsReading(
    const PhysicsSpace::VehicleReading& reading,
    const VehicleConversionSettings& settings) noexcept
{
    VehicleChassisCouplingInput out {};

    auto transformCoord = [&](const Mathematics::Axis3D& p) -> std::array<float, 3>
    {
        if (settings.ConvertZUpToYUp)
        {
            return { p.X, p.Z, p.Y };
        }
        return { p.X, p.Y, p.Z };
    };

    out.ChassisOriginMeters = transformCoord(reading.WorldPositionMeters);
    out.ForwardSpeedMps     = reading.ForwardSpeedMps;
    out.RoadPlaneY          = settings.RoadPlaneWorldZ;

    // Extract chassis yaw from WorldOrientation quaternion (Z-up yaw around Z)
    const auto& q = reading.WorldOrientation;
    const float siny = 2.0f * (q.W * q.Z + q.X * q.Y);
    const float cosy = 1.0f - 2.0f * (q.Y * q.Y + q.Z * q.Z);
    out.ChassisYawRad = std::atan2(siny, cosy);

    for (uint32_t w = 0u; w < 4u; ++w)
    {
        const PhysicsSpace::WheelCornerReading& corner = reading.Corners[w];
        VehicleWheelDeformInput& dstInput              = out.CornerInputs[w];
        VehicleWheelPlacement& dstPlace                = out.CornerPlacements[w];

        dstInput.InContact              = corner.InContact;
        dstInput.VerticalLoadNewtons    = std::max(0.0f, corner.VerticalLoadNewtons);
        dstInput.AngularVelocityRadS    = corner.AngularVelocityRadS;
        dstInput.SpinAngleRad           = corner.SpinAngleRad;
        dstInput.SuspensionCompressionM = corner.SuspensionCompressionM;
        dstInput.DynamicCamberRad       = corner.DynamicCamberRad;
        dstInput.SteerAngleRad          = corner.SteerAngleRad;
        dstInput.CarcassTempKelvin      = std::clamp(corner.CarcassTempKelvin, 230.0f, 480.0f);
        dstInput.ColdPressurePsi        = settings.ColdTirePressurePsi;

        // Derive local curb/bump elevation above road plane directly from Jolt contact hit height
        const float contactElevation = corner.InContact
            ? std::max(0.0f, corner.ContactPointWorldM.Z - settings.RoadPlaneWorldZ)
            : 0.0f;
        dstInput.RoadBumpHeightMeters  = std::clamp(contactElevation, 0.0f, 0.35f);
        dstInput.RoadBumpOffsetXMeters = corner.InContact
            ? std::clamp(corner.ContactPointWorldM.X - (reading.WorldPositionMeters.X + corner.WheelHubLocalMeters.X), -0.45f, 0.45f)
            : 0.0f;

        // Compute world wheel center from chassis origin + rotated local corner offset
        const Mathematics::Axis3D worldWheelCenter = reading.WorldPositionMeters +
            PhysicsSpace::RigidBodyDynamics::RotateByQuaternion(reading.WorldOrientation, corner.WheelHubLocalMeters);
        dstPlace.WheelCenterMeters = transformCoord(worldWheelCenter);
        dstPlace.SteerYawRad       = out.ChassisYawRad + corner.SteerAngleRad;
        dstPlace.CamberRollRad     = corner.DynamicCamberRad;
        dstPlace.WheelScale        = settings.CanonicalWheelScale;
        dstPlace.RightSideMirror   = (w == 1u || w == 3u);
    }

    return out;
}

uint32_t VehicleReadingConverter::AccumulateRigidContactImpulses(
    const DeformableBody& body,
    std::span<const PhysicsSpace::RigidColliderProxy> proxies,
    float contactStiffnessNsPerMeter,
    std::vector<PhysicsSpace::RigidImpulseAccumulatorEntry>& outImpulses)
{
    if (body.PointCount == 0u || proxies.empty())
    {
        return 0u;
    }

    const size_t pCount = proxies.size();
    constexpr size_t kStackProxyCap = 64u;
    std::array<PhysicsSpace::RigidImpulseAccumulatorEntry, kStackProxyCap> stackAccum {};
    std::vector<PhysicsSpace::RigidImpulseAccumulatorEntry> heapAccum {};
    PhysicsSpace::RigidImpulseAccumulatorEntry* accumData = stackAccum.data();
    if (pCount > kStackProxyCap)
    {
        heapAccum.resize(pCount);
        accumData = heapAccum.data();
    }
    for (size_t pi = 0u; pi < pCount; ++pi)
    {
        accumData[pi].BodyToken.SlotIndex = proxies[pi].BodySlotIndex;
    }

    uint32_t totalContacts = 0u;
    for (uint32_t i = 0u; i < body.PointCount; ++i)
    {
        if (body.InverseMass[i] <= 0.0f)
        {
            continue;
        }
        const size_t i3 = static_cast<size_t>(i) * 3u;
        const float px  = body.Positions[i3 + 0u];
        const float py  = body.Positions[i3 + 1u];
        const float pz  = body.Positions[i3 + 2u];
        const float rad = std::max(0.02f, body.ContactRadius[i]);
        const float ptMass = std::min(15.0f, 1.0f / body.InverseMass[i]);

        for (size_t pi = 0u; pi < pCount; ++pi)
        {
            const PhysicsSpace::RigidColliderProxy& proxy = proxies[pi];
            float testPos[3] = { px, py, pz };
            float testVel[3] = { body.Velocities[i3], body.Velocities[i3 + 1u], body.Velocities[i3 + 2u] };
            const auto contact = PhysicsSpace::BroadphaseSolver::ResolveSphereAgainstProxy(
                proxy, rad, ptMass, testPos, testVel);
            if (!contact.Collided)
            {
                continue;
            }

            const float dx = testPos[0] - px;
            const float dy = testPos[1] - py;
            const float dz = testPos[2] - pz;

            // Newton's 3rd Law: reaction impulse on rigid body = -SphereImpulseNs plus penalty pushback
            const float scale = contactStiffnessNsPerMeter * ptMass;
            const float jx = -contact.SphereImpulseNs.X - dx * scale;
            const float jy = -contact.SphereImpulseNs.Y - dy * scale;
            const float jz = -contact.SphereImpulseNs.Z - dz * scale;

            PhysicsSpace::RigidImpulseAccumulatorEntry& e = accumData[pi];
            e.LinearImpulse[0] += jx;
            e.LinearImpulse[1] += jy;
            e.LinearImpulse[2] += jz;

            // Torque impulse r x J about proxy center
            const float rx = contact.ContactPoint.X - proxy.CenterMeters[0];
            const float ry = contact.ContactPoint.Y - proxy.CenterMeters[1];
            const float rz = contact.ContactPoint.Z - proxy.CenterMeters[2];
            e.AngularImpulse[0] += (ry * jz - rz * jy);
            e.AngularImpulse[1] += (rz * jx - rx * jz);
            e.AngularImpulse[2] += (rx * jy - ry * jx);

            e.ContactPoint[0] += contact.ContactPoint.X;
            e.ContactPoint[1] += contact.ContactPoint.Y;
            e.ContactPoint[2] += contact.ContactPoint.Z;
            ++e.ContactCount;
            ++totalContacts;
        }
    }

    for (size_t pi = 0u; pi < pCount; ++pi)
    {
        auto& e = accumData[pi];
        if (e.ContactCount > 0u)
        {
            const float invC = 1.0f / static_cast<float>(e.ContactCount);
            e.ContactPoint[0] *= invC;
            e.ContactPoint[1] *= invC;
            e.ContactPoint[2] *= invC;
            outImpulses.push_back(e);
        }
    }

    return totalContacts;
}

} // namespace Frontier::SimulationSpace::Deformable
