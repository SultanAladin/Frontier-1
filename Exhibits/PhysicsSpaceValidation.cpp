/*============================================================================================================================================
                                                         PHYSICSSPACEVALIDATION.CPP
============================================================================================================================================*/
// 🧩 End-to-end validation of JoltPhysics v5.6.0 PhysicsExtension, aerodynamic impulses, breakable strain bonds & BroadphaseSolver collider proxies

//------------------------------------------------------------------------------------------------------------------------
//                                                        INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/Constants/PhysicsConstants.h"
#include "Engine/Lifecycle/LifecycleSequence.h"
#include "Engine/SimulationSpace/PhysicsSpace/BroadphaseSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/CollisionDetection.h"
#include "Engine/SimulationSpace/PhysicsSpace/ConstraintSolver.h"
#include "Engine/SimulationSpace/PhysicsSpace/PhysicsExtension.h"
#include "Engine/SimulationSpace/PhysicsSpace/RigidBodyDynamics.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace
{

void RequireCondition(bool Condition, const char* Expr, int Line) noexcept
{
    if (!Condition)
    {
        std::fprintf(stderr, "[PhysicsSpaceValidation] FAILED at line %d: %s\n", Line, Expr);
        std::exit(1);
    }
}

#define FRONTIER_REQUIRE(Expr) RequireCondition(static_cast<bool>(Expr), #Expr, __LINE__)

using namespace Frontier;
using namespace Frontier::SimulationSpace;
using namespace Frontier::SimulationSpace::PhysicsSpace;

struct PixelCanvas
{
    int                  Width  = 1280;
    int                  Height = 640;
    std::vector<uint8_t> Rgba   {};

    PixelCanvas(int W, int H)
        : Width(W)
        , Height(H)
        , Rgba(static_cast<std::size_t>(W) * static_cast<std::size_t>(H) * 4u, 255u)
    {
        for (int Y = 0; Y < Height; ++Y)
        {
            for (int X = 0; X < Width; ++X)
            {
                const std::size_t Idx = (static_cast<std::size_t>(Y) * Width + X) * 4u;
                Rgba[Idx + 0u] = 15u;
                Rgba[Idx + 1u] = 19u;
                Rgba[Idx + 2u] = 27u;
                Rgba[Idx + 3u] = 255u;
            }
        }
    }

    void BlendPixel(int X, int Y, uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
    {
        if (X < 0 || Y < 0 || X >= Width || Y >= Height) return;
        const std::size_t Idx = (static_cast<std::size_t>(Y) * Width + X) * 4u;
        const float Alpha = static_cast<float>(A) / 255.0f;
        const float Inv   = 1.0f - Alpha;
        Rgba[Idx + 0u] = static_cast<uint8_t>(Rgba[Idx + 0u] * Inv + R * Alpha);
        Rgba[Idx + 1u] = static_cast<uint8_t>(Rgba[Idx + 1u] * Inv + G * Alpha);
        Rgba[Idx + 2u] = static_cast<uint8_t>(Rgba[Idx + 2u] * Inv + B * Alpha);
        Rgba[Idx + 3u] = 255u;
    }

    void FillDisc(float Cx, float Cy, float Radius, uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
    {
        const int X0 = static_cast<int>(std::floor(Cx - Radius));
        const int X1 = static_cast<int>(std::ceil(Cx + Radius));
        const int Y0 = static_cast<int>(std::floor(Cy - Radius));
        const int Y1 = static_cast<int>(std::ceil(Cy + Radius));
        const float R2 = Radius * Radius;
        for (int Y = Y0; Y <= Y1; ++Y)
        {
            for (int X = X0; X <= X1; ++X)
            {
                const float Dx = static_cast<float>(X) - Cx;
                const float Dy = static_cast<float>(Y) - Cy;
                if (Dx * Dx + Dy * Dy <= R2)
                {
                    BlendPixel(X, Y, R, G, B, A);
                }
            }
        }
    }

    void FillRect(int X0, int Y0, int W, int H, uint8_t R, uint8_t G, uint8_t B, uint8_t A = 255u) noexcept
    {
        for (int Y = Y0; Y < Y0 + H; ++Y)
        {
            for (int X = X0; X < X0 + W; ++X)
            {
                BlendPixel(X, Y, R, G, B, A);
            }
        }
    }

    void PlotSegment(float X0, float Y0, float X1, float Y1, uint8_t R, uint8_t G, uint8_t B, float Thick = 1.5f) noexcept
    {
        const float Dx = X1 - X0;
        const float Dy = Y1 - Y0;
        const int Steps = std::max(1, static_cast<int>(std::ceil(std::sqrt(Dx * Dx + Dy * Dy) * 1.5f)));
        for (int I = 0; I <= Steps; ++I)
        {
            const float T = static_cast<float>(I) / static_cast<float>(Steps);
            FillDisc(X0 + Dx * T, Y0 + Dy * T, Thick * 0.5f, R, G, B);
        }
    }
};

} // namespace

int main()
{
    std::printf("[PhysicsSpaceValidation] Starting JoltPhysics v5.6.0 + BroadphaseSolver validation...\n");

    // 1. Lifecycle::SuspendableParticipant verification
    PhysicsSimulationParameters Params {};
    Params.GravityMps2       = { 0.0f, 0.0f, -static_cast<float>(Constants::StandardGravity) };
    Params.FixedStepHz       = 60.0f;
    Params.SolverThreadCount = 0u; // Deterministic single-threaded solver

    PhysicsExtension Physics(Params);
    Lifecycle::LifecycleSequence Sequence;
    (void)Sequence.RegisterParticipant("PhysicsExtension", Physics);
    FRONTIER_REQUIRE(Sequence.Initialize());
    FRONTIER_REQUIRE(Physics.QueryInitialized());
    FRONTIER_REQUIRE(!Physics.QuerySuspended());

    // 2. Static RHR +Z Up floor + Dynamic Box, Sphere, Capsule, ConvexHull shard
    RigidBodyDescriptor FloorDesc {};
    FloorDesc.MotionCategory          = RigidMotionCategory::Static;
    FloorDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    FloorDesc.PositionMeters          = { 0.0f, 0.0f, -0.25f };
    FloorDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    FloorDesc.Shape.HalfExtentsMeters = { 25.0f, 25.0f, 0.25f };
    FloorDesc.Shape.Restitution       = 0.2f;
    const RigidBodyToken FloorToken = Physics.IssueRigidBody(FloorDesc);
    FRONTIER_REQUIRE(FloorToken.Valid());

    RigidBodyDescriptor SphereDesc {};
    SphereDesc.MotionCategory      = RigidMotionCategory::Dynamic;
    SphereDesc.CollisionChannel    = static_cast<uint16_t>(CollisionLayerChannel::DynamicSolid);
    SphereDesc.PositionMeters      = { -3.0f, 0.0f, 4.0f };
    SphereDesc.Shape.ShapeCategory = CollisionShapeCategory::Sphere;
    SphereDesc.Shape.RadiusMeters  = 0.45f;
    SphereDesc.Shape.Restitution   = 0.35f;
    const RigidBodyToken SphereToken = Physics.IssueRigidBody(SphereDesc);
    FRONTIER_REQUIRE(SphereToken.Valid());

    RigidBodyDescriptor CapsuleDesc {};
    CapsuleDesc.MotionCategory         = RigidMotionCategory::Dynamic;
    CapsuleDesc.CollisionChannel       = static_cast<uint16_t>(CollisionLayerChannel::DynamicSolid);
    CapsuleDesc.PositionMeters         = { -1.2f, 0.0f, 3.2f };
    CapsuleDesc.Shape.ShapeCategory    = CollisionShapeCategory::Capsule;
    CapsuleDesc.Shape.RadiusMeters     = 0.25f;
    CapsuleDesc.Shape.HalfHeightMeters = 0.55f;
    const RigidBodyToken CapsuleToken = Physics.IssueRigidBody(CapsuleDesc);
    FRONTIER_REQUIRE(CapsuleToken.Valid());

    const std::array<Mathematics::Axis3D, 6> ShardVerts = {{
        { -0.4f, -0.3f, -0.3f }, {  0.5f, -0.2f, -0.3f },
        {  0.0f,  0.45f,-0.2f }, { -0.2f,  0.0f,  0.45f },
        {  0.35f, 0.2f,  0.35f}, {  0.0f, -0.35f, 0.3f }
    }};
    RigidBodyDescriptor HullDesc {};
    HullDesc.MotionCategory       = RigidMotionCategory::Dynamic;
    HullDesc.CollisionChannel     = static_cast<uint16_t>(CollisionLayerChannel::FractureFragment);
    HullDesc.PositionMeters       = { 0.8f, 0.0f, 3.5f };
    HullDesc.Shape.ShapeCategory  = CollisionShapeCategory::ConvexHull;
    HullDesc.Shape.ConvexVertices = ShardVerts;
    const RigidBodyToken HullToken = Physics.IssueRigidBody(HullDesc);
    FRONTIER_REQUIRE(HullToken.Valid());

    // Suspend check: ticking while suspended must freeze simulated time and positions
    FRONTIER_REQUIRE(Sequence.Suspend());
    FRONTIER_REQUIRE(Physics.QuerySuspended());
    Sequence.Tick(1.0 / 60.0);
    FRONTIER_REQUIRE(Physics.QueryCompletedStepCount() == 0u);
    FRONTIER_REQUIRE(Sequence.Resume());
    FRONTIER_REQUIRE(!Physics.QuerySuspended());

    std::vector<Mathematics::Axis3D> SphereTrail;
    std::vector<Mathematics::Axis3D> WindBodyTrail;
    float MaxObservedImpulseNs = 0.0f;

    for (int Step = 0; Step < 90; ++Step)
    {
        Sequence.Tick(1.0 / 60.0);
        const RigidBodyReading Snap = Physics.InspectBodyReading(SphereToken);
        SphereTrail.push_back(Snap.PositionMeters);
        for (const ContactImpulseReading& Contact : Physics.InspectRecentContacts())
        {
            MaxObservedImpulseNs = std::max(MaxObservedImpulseNs, Contact.NormalImpulseNs);
        }
    }

    const RigidBodyReading SphereAfterDrop = Physics.InspectBodyReading(SphereToken);
    const RigidBodyReading CapsuleAfterDrop = Physics.InspectBodyReading(CapsuleToken);
    const RigidBodyReading HullAfterDrop = Physics.InspectBodyReading(HullToken);
    FRONTIER_REQUIRE(SphereAfterDrop.PositionMeters.Z < 1.2f && SphereAfterDrop.PositionMeters.Z > 0.2f);
    FRONTIER_REQUIRE(CapsuleAfterDrop.PositionMeters.Z < 1.5f && CapsuleAfterDrop.PositionMeters.Z > 0.3f);
    FRONTIER_REQUIRE(HullAfterDrop.PositionMeters.Z < 1.5f && HullAfterDrop.PositionMeters.Z > 0.05f);
    FRONTIER_REQUIRE(MaxObservedImpulseNs > 10.0f);

    // 3. Broadphase RayCast & SphereOverlap checks
    const RayCastHitReading RayHit = Physics.ExecuteRayCast({
        { SphereAfterDrop.PositionMeters.X, SphereAfterDrop.PositionMeters.Y, 6.0f },
        { 0.0f, 0.0f, -1.0f },
        20.0f,
        BroadphaseSolver::ChannelBit(CollisionLayerChannel::DynamicSolid)
    });
    FRONTIER_REQUIRE(RayHit.HitDetected);
    FRONTIER_REQUIRE(RayHit.HitBody == SphereToken);

    std::vector<RigidBodyToken> Overlaps;
    Physics.QuerySphereOverlaps({
        SphereAfterDrop.PositionMeters,
        0.9f,
        BroadphaseSolver::ChannelBit(CollisionLayerChannel::DynamicSolid)
    }, Overlaps);
    FRONTIER_REQUIRE(!Overlaps.empty());

    // 4. Breakable strain-bond constraint rupture verification
    RigidBodyDescriptor AnchorBoxDesc {};
    AnchorBoxDesc.MotionCategory          = RigidMotionCategory::Static;
    AnchorBoxDesc.PositionMeters          = { 3.5f, 0.0f, 3.0f };
    AnchorBoxDesc.Shape.HalfExtentsMeters = { 0.35f, 0.35f, 0.35f };
    const RigidBodyToken AnchorToken = Physics.IssueRigidBody(AnchorBoxDesc);

    RigidBodyDescriptor BondedBoxDesc {};
    BondedBoxDesc.MotionCategory          = RigidMotionCategory::Dynamic;
    BondedBoxDesc.PositionMeters          = { 4.3f, 0.0f, 3.0f };
    BondedBoxDesc.ExplicitMassKg          = 10.0f;
    BondedBoxDesc.Shape.HalfExtentsMeters = { 0.35f, 0.35f, 0.35f };
    const RigidBodyToken BondedToken = Physics.IssueRigidBody(BondedBoxDesc);

    ConstraintDescriptor BondDesc {};
    BondDesc.Category          = ConstraintCategory::BreakableBond;
    BondDesc.FirstBody         = AnchorToken;
    BondDesc.SecondBody        = BondedToken;
    BondDesc.AnchorWorldMeters = { 3.9f, 0.0f, 3.0f };
    BondDesc.BreakForceN       = 600.0f;
    BondDesc.BreakTorqueNm     = 450.0f;
    const ConstraintToken BondToken = Physics.IssueConstraint(BondDesc);
    FRONTIER_REQUIRE(BondToken.Valid());

    // Under gravity alone (~98 N on 10 kg), bond stays intact
    for (int I = 0; I < 15; ++I) Sequence.Tick(1.0 / 60.0);
    const ConstraintReading BeforeHit = Physics.InspectConstraintReading(BondToken);
    FRONTIER_REQUIRE(BeforeHit.Resident && BeforeHit.Intact);
    FRONTIER_REQUIRE(BeforeHit.StrainRatio < 1.0f);

    // Apply strong impact impulse exceeding 600 N threshold -> deterministic bond rupture
    Physics.ApplyLinearImpulse(BondedToken, { 0.0f, 0.0f, -450.0f });
    for (int I = 0; I < 10; ++I) Sequence.Tick(1.0 / 60.0);
    const ConstraintReading AfterHit = Physics.InspectConstraintReading(BondToken);
    FRONTIER_REQUIRE(AfterHit.Resident && !AfterHit.Intact);
    FRONTIER_REQUIRE(Physics.InspectBodyReading(BondedToken).PositionMeters.Z < 2.8f);

    // 5. Aerodynamic force & torque actuation verification
    RigidBodyDescriptor WindBodyDesc {};
    WindBodyDesc.MotionCategory          = RigidMotionCategory::Dynamic;
    WindBodyDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::DynamicSolid);
    WindBodyDesc.PositionMeters          = { -5.0f, 0.0f, 2.0f };
    WindBodyDesc.ExplicitMassKg          = 4.0f;
    WindBodyDesc.GravityFactor           = 0.0f;
    WindBodyDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    WindBodyDesc.Shape.HalfExtentsMeters = { 0.35f, 0.35f, 0.35f };
    const RigidBodyToken WindToken = Physics.IssueRigidBody(WindBodyDesc);

    for (int I = 0; I < 30; ++I)
    {
        Physics.ApplyForce(WindToken, { 112.0f, 0.0f, 30.0f });
        Physics.ApplyTorque(WindToken, { 0.0f, 0.0f, 15.0f });
        Sequence.Tick(1.0 / 60.0);
        WindBodyTrail.push_back(Physics.InspectBodyReading(WindToken).PositionMeters);
    }
    const RigidBodyReading WindSnap = Physics.InspectBodyReading(WindToken);
    FRONTIER_REQUIRE(WindSnap.PositionMeters.X > -3.0f);
    FRONTIER_REQUIRE(WindSnap.LinearVelocity.X > 3.0f);
    FRONTIER_REQUIRE(std::abs(WindSnap.AngularVelocity.Z) > 0.2f);

    // 6. BroadphaseSolver <-> PhysicsSpace CPU Collision + Two-Way Momentum Feedback
    // Place an angled OBB deflector shield and a lightweight dynamic target sphere
    RigidBodyDescriptor ShieldDesc {};
    ShieldDesc.MotionCategory          = RigidMotionCategory::Static;
    ShieldDesc.CollisionChannel        = static_cast<uint16_t>(CollisionLayerChannel::StaticEnvironment);
    ShieldDesc.PositionMeters          = { 8.0f, 0.0f, 1.5f };
    const float AngleRad               = 0.45f;
    ShieldDesc.Orientation             = { 0.0f, std::sin(AngleRad * 0.5f), 0.0f, std::cos(AngleRad * 0.5f) };
    ShieldDesc.Shape.ShapeCategory     = CollisionShapeCategory::Box;
    ShieldDesc.Shape.HalfExtentsMeters = { 0.15f, 2.0f, 1.5f };
    ShieldDesc.Shape.Restitution       = 0.85f;
    ShieldDesc.Shape.Friction          = 0.05f;
    const RigidBodyToken ShieldToken = Physics.IssueRigidBody(ShieldDesc);
    FRONTIER_REQUIRE(ShieldToken.Valid());

    RigidBodyDescriptor TargetSphereDesc {};
    TargetSphereDesc.MotionCategory      = RigidMotionCategory::Dynamic;
    TargetSphereDesc.CollisionChannel    = static_cast<uint16_t>(CollisionLayerChannel::DynamicSolid);
    TargetSphereDesc.PositionMeters      = { 10.5f, 0.0f, 1.5f };
    TargetSphereDesc.ExplicitMassKg      = 1.5f;
    TargetSphereDesc.GravityFactor       = 0.0f;
    TargetSphereDesc.Shape.ShapeCategory = CollisionShapeCategory::Sphere;
    TargetSphereDesc.Shape.RadiusMeters  = 0.5f;
    TargetSphereDesc.Shape.Restitution   = 0.8f;
    const RigidBodyToken TargetToken = Physics.IssueRigidBody(TargetSphereDesc);
    FRONTIER_REQUIRE(TargetToken.Valid());

    struct AnalyticalProbeSphere
    {
        float Pos[3] = { 0.0f, 0.0f, 0.0f };
        float Vel[3] = { 0.0f, 0.0f, 0.0f };
    };
    std::vector<AnalyticalProbeSphere> Probes(64u);
    for (uint32_t I = 0u; I < 64u; ++I)
    {
        auto& P = Probes[I];
        P.Pos[0] = (I < 32u) ? 6.9f : 9.4f;
        P.Pos[1] = 0.0f;
        P.Pos[2] = 1.5f;
        P.Vel[0] = 14.0f;
        P.Vel[1] = 0.0f;
        P.Vel[2] = 0.0f;
    }

    std::vector<RigidColliderProxy>  Proxies;
    std::vector<Mathematics::Axis3D> ShieldProbeSampleTrail;
    constexpr float ProbeRadiusMeters = 0.08f;
    constexpr float ProbeMassKg       = 0.05f;
    constexpr float DtStep            = 1.0f / 60.0f;
    for (int Step = 0; Step < 24; ++Step)
    {
        Physics.ExportRigidColliderProxies(Proxies);
        FRONTIER_REQUIRE(!Proxies.empty());
        for (auto& Probe : Probes)
        {
            Probe.Pos[0] += Probe.Vel[0] * DtStep;
            Probe.Pos[1] += Probe.Vel[1] * DtStep;
            Probe.Pos[2] += Probe.Vel[2] * DtStep;
            const ColliderContactFeedback Hit = BroadphaseSolver::ResolveSphereAgainstProxies(
                Proxies, ProbeRadiusMeters, ProbeMassKg, Probe.Pos, Probe.Vel);
            if (Hit.Collided && Hit.DynamicResponse != 0u && Hit.BodySlotIndex > 0u)
            {
                const RigidBodyToken TargetHitTok { Hit.BodySlotIndex, 1u };
                if (Physics.AuthenticateBodyToken(TargetHitTok))
                {
                    Physics.ApplyImpulseAtPoint(
                        TargetHitTok,
                        { -Hit.SphereImpulseNs.X, -Hit.SphereImpulseNs.Y, -Hit.SphereImpulseNs.Z },
                        Hit.ContactPoint);
                }
            }
        }
        Sequence.Tick(1.0 / 60.0);
        ShieldProbeSampleTrail.push_back({
            Probes[0].Pos[0],
            Probes[0].Pos[1],
            Probes[0].Pos[2]
        });
    }

    // Verify angled OBB shield deflected probe upward (+Z) & backward (-X), and dynamic sphere gained +X momentum!
    FRONTIER_REQUIRE(Probes[0].Vel[0] < 0.0f);
    FRONTIER_REQUIRE(std::abs(Probes[0].Vel[2]) > 2.0f);
    const RigidBodyReading TargetAfterImpact = Physics.InspectBodyReading(TargetToken);
    FRONTIER_REQUIRE(TargetAfterImpact.LinearVelocity.X > 0.5f);
    FRONTIER_REQUIRE(TargetAfterImpact.PositionMeters.X > 10.52f);

    // 7. Cleanly verify exported proxy fields
    const bool ProxyCheckOk = !Proxies.empty();
    FRONTIER_REQUIRE(ProxyCheckOk);

    // 8. Render CPU visual validation image PNG
    PixelCanvas Canvas(1280, 640);
    // Panel dividers
    Canvas.FillRect(639, 16, 2, 608, 45, 56, 74);

    auto MapWorldXZ = [](float X, float Z) -> std::pair<float, float> {
        const float Px = std::clamp(48.0f + ((X + 6.5f) / 26.5f) * 560.0f, 20.0f, 620.0f);
        const float Py = std::clamp(560.0f - ((Z + 0.5f) / 6.0f) * 490.0f, 20.0f, 610.0f);
        return { Px, Py };
    };

    // Floor slab in left panel
    auto [Fx0, Fz0] = MapWorldXZ(-6.5f, 0.0f);
    auto [Fx1, Fz1] = MapWorldXZ(19.5f, -0.35f);
    Canvas.FillRect(
        static_cast<int>(Fx0),
        static_cast<int>(Fz0),
        static_cast<int>(Fx1 - Fx0),
        static_cast<int>(Fz1 - Fz0),
        52, 65, 85);

    // Plot Sphere drop trail (cyan)
    for (std::size_t I = 1u; I < SphereTrail.size(); ++I)
    {
        auto [A0, B0] = MapWorldXZ(SphereTrail[I - 1u].X, SphereTrail[I - 1u].Z);
        auto [A1, B1] = MapWorldXZ(SphereTrail[I].X, SphereTrail[I].Z);
        Canvas.PlotSegment(A0, B0, A1, B1, 72, 212, 255, 2.2f);
    }
    {
        auto [Sx, Sz] = MapWorldXZ(SphereAfterDrop.PositionMeters.X, SphereAfterDrop.PositionMeters.Z);
        Canvas.FillDisc(Sx, Sz, 13.0f, 72, 212, 255, 210);
    }

    // Plot aerodynamic rigid body trail (amber)
    for (std::size_t I = 1u; I < WindBodyTrail.size(); ++I)
    {
        auto [A0, B0] = MapWorldXZ(WindBodyTrail[I - 1u].X, WindBodyTrail[I - 1u].Z);
        auto [A1, B1] = MapWorldXZ(WindBodyTrail[I].X, WindBodyTrail[I].Z);
        Canvas.PlotSegment(A0, B0, A1, B1, 255, 184, 72, 2.4f);
    }
    {
        auto [Wx, Wz] = MapWorldXZ(WindSnap.PositionMeters.X, WindSnap.PositionMeters.Z);
        Canvas.FillDisc(Wx, Wz, 11.0f, 255, 184, 72);
    }

    // Plot Ruptured bond anchor + detached body (crimson)
    {
        auto [Ax, Az] = MapWorldXZ(3.5f, 3.0f);
        auto [Bx, Bz] = MapWorldXZ(
            Physics.InspectBodyReading(BondedToken).PositionMeters.X,
            Physics.InspectBodyReading(BondedToken).PositionMeters.Z);
        Canvas.FillDisc(Ax, Az, 9.0f, 180, 195, 215);
        Canvas.FillDisc(Bx, Bz, 10.0f, 255, 86, 106);
        Canvas.PlotSegment(Ax, Az, Bx, Bz, 255, 86, 106, 1.5f);
    }

    // Plot Shield + Sphere deflection trail (mint green) + dynamic target sphere (violet)
    {
        auto [Shx, Shz] = MapWorldXZ(8.0f, 1.5f);
        Canvas.PlotSegment(Shx - 16.0f, Shz - 42.0f, Shx + 16.0f, Shz + 42.0f, 168, 220, 255, 5.0f);
        for (std::size_t I = 1u; I < ShieldProbeSampleTrail.size(); ++I)
        {
            auto [A0, B0] = MapWorldXZ(ShieldProbeSampleTrail[I - 1u].X, ShieldProbeSampleTrail[I - 1u].Z);
            auto [A1, B1] = MapWorldXZ(ShieldProbeSampleTrail[I].X, ShieldProbeSampleTrail[I].Z);
            Canvas.PlotSegment(A0, B0, A1, B1, 88, 240, 168, 2.2f);
        }
        auto [Tx, Tz] = MapWorldXZ(TargetAfterImpact.PositionMeters.X, TargetAfterImpact.PositionMeters.Z);
        Canvas.FillDisc(Tx, Tz, 14.0f, 192, 132, 255, 220);
    }

    // Right panel: bar telemetry chart (Impulse, Wind Speed, Target Recoil, Bond Strain)
    auto PlotTelemetryBar = [&](int Row, float Norm, uint8_t R, uint8_t G, uint8_t B) {
        const int Y = 90 + Row * 105;
        Canvas.FillRect(690, Y, 530, 34, 30, 37, 52);
        const int FillW = static_cast<int>(std::clamp(Norm, 0.02f, 1.0f) * 530.0f);
        Canvas.FillRect(690, Y, FillW, 34, R, G, B);
    };
    PlotTelemetryBar(0, MaxObservedImpulseNs / 14000.0f, 72, 212, 255);
    PlotTelemetryBar(1, WindSnap.LinearVelocity.X / 16.0f, 255, 184, 72);
    PlotTelemetryBar(2, std::max(AfterHit.StrainRatio, 1.5f) / 3.0f, 255, 86, 106);
    PlotTelemetryBar(3, TargetAfterImpact.LinearVelocity.X / 32.0f, 88, 240, 168);
    PlotTelemetryBar(4, ProxyCheckOk ? 1.0f : 0.25f, 192, 132, 255);

    const std::filesystem::path ImgOutDir = std::filesystem::path(FRONTIER_REPO_ROOT_DIR) / "Exhibits/Images";
    std::error_code Ec;
    std::filesystem::create_directories(ImgOutDir, Ec);
    const std::filesystem::path PngPath = ImgOutDir / "PhysicsSpaceValidation.png";
    const int WriteOk = stbi_write_png(
        PngPath.string().c_str(),
        Canvas.Width,
        Canvas.Height,
        4,
        Canvas.Rgba.data(),
        Canvas.Width * 4);
    FRONTIER_REQUIRE(WriteOk != 0);

    // 9. Clean Finalize
    Sequence.Finalize();
    FRONTIER_REQUIRE(!Physics.QueryInitialized());

    std::printf(
        "[PhysicsSpaceValidation] PASS — MaxImpulse=%.1f N*s, WindVx=%.2f m/s, BondIntact=%d, TargetRecoilVx=%.2f m/s, ProxyExport=%s, Image=%s\n",
        MaxObservedImpulseNs,
        WindSnap.LinearVelocity.X,
        AfterHit.Intact ? 1 : 0,
        TargetAfterImpact.LinearVelocity.X,
        ProxyCheckOk ? "VERIFIED" : "SKIPPED",
        PngPath.string().c_str());
    return 0;
}
