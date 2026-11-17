/*====================================================================================================================================
                                                        JOINTSOLVER.H
====================================================================================================================================*/
// 🧩 Assembly joints (priority 11, A1): joint connectors read exactly off body geometry and the placement each joint kind imposes

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Feature/TopologicalNaming.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <optional>
#include <string>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    ENUMS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Fusion 360's joint types. Degrees of freedom: Rigid 0 · Revolute 1 (turn about 𝑧) · Slider 1 (along 𝑧) ·
//    Cylindrical 2 (turn about and slide along 𝑧) · PinSlot 2 (turn about 𝑧, slide along 𝑥) · Planar 3 (slide in 𝑥𝑦, turn
//    about 𝑧) · Ball 3 (yaw about 𝑧, pitch about 𝑦, roll about 𝑥). Axes are those of the fixed connector's frame.
enum class JointKind : uint8_t
{
    Rigid       = 0,
    Revolute    = 1,
    Slider      = 2,
    Cylindrical = 3,
    PinSlot     = 4,
    Planar      = 5,
    Ball        = 6,
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 A joint origin on a body (Fusion "joint origin"): a right-handed frame read exactly off one face, edge or vertex.
//      circle / ellipse edge → centre, 𝑧 = the plane normal (pointing out of an adjacent planar face when there is one)
//      line edge             → midpoint, 𝑧 = the line direction
//      plane face            → area centroid, 𝑧 = the outward normal
//      cylinder / cone face  → the axis point level with the face's area centroid, 𝑧 = the carrier axis
//      sphere / torus face   → the centre, 𝑧 = the carrier axis
//      vertex                → the point, world axes
struct JointConnector
{
    OrthonormalFrame Frame       = {};                                                       // [-] - Joint origin (world)
    std::string      Description = {};                                                       // [-] - "circle centre", "plane centroid", …
    std::string      Refusal     = {};                                                       // [-] - Empty on success
};

// 📝 Joint values; each kind reads only its own (see JointKind). Offset always shifts along the fixed 𝑧.
struct JointMotion
{
    double Angle       = 0.0;                                                                // [rad] - Turn about 𝑧 (ball: yaw)
    double Slide       = 0.0;                                                                // [len] - Slider / cylindrical: along 𝑧 · pin-slot / planar: along 𝑥
    double SecondSlide = 0.0;                                                                // [len] - Planar: along 𝑦
    double Pitch       = 0.0;                                                                // [rad] - Ball: about 𝑦
    double Roll        = 0.0;                                                                // [rad] - Ball: about 𝑥
    double Offset      = 0.0;                                                                // [len] - Along 𝑧, every kind
};

// 📝 Optional motion limits (Fusion joint limits): a value outside [Low, High] refuses the joint instead of clamping.
struct JointRange
{
    double Low  = 0.0;                                                                       // [rad|len] - Smallest allowed value
    double High = 0.0;                                                                       // [rad|len] - Largest allowed value
};

struct JointPlacement
{
    AffineMap        Map      = {};                                                          // [-] - Rigid map applied to the moving body
    OrthonormalFrame Target   = {};                                                          // [-] - Where the moving connector lands (world)
    std::string      Refusal  = {};                                                          // [-] - Empty on success
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// Joint origin of the face, edge or vertex in record Slot (see JointConnector for the rules). Refused for carriers
// without a canonical frame (NURBS, procedural, traced) and for faces whose centroid cannot be measured.
[[nodiscard]] JointConnector ResolveJointConnector(const ModelPartition& Partition, ReferenceKind Kind, uint32_t Slot);

// 💡 The rigid map taking the moving connector Moving onto Fixed under the joint: P = F · Motion · Flip · M⁻¹, with F and M
//    the connectors' local → world maps, Motion = T(translation) · R(rotation) built from Values in the fixed frame, and
//    Flip = a half turn about 𝑥 (the two origins face each other, as when a face rests on a face) unless Flipped.
//    Refused when a value lies outside its range.
[[nodiscard]] JointPlacement SolveJointPlacement(const OrthonormalFrame& Moving, const OrthonormalFrame& Fixed, JointKind Kind, const JointMotion& Values,
                                                 bool FlippedCondition = false, const std::optional<JointRange>& AngleRange = {},
                                                 const std::optional<JointRange>& SlideRange = {});

// The motion part alone (in the fixed connector's frame): translation and rotation per kind.
[[nodiscard]] AffineMap ComposeJointMotion(JointKind Kind, const JointMotion& Values) noexcept;

[[nodiscard]] int         JointFreedom(JointKind Kind) noexcept;
[[nodiscard]] const char* JointKindLabel(JointKind Kind) noexcept;

// ---- rigid-map algebra (AffineMap is x ↦ M·x + Shift)
[[nodiscard]] AffineMap ComposeAffineMaps(const AffineMap& Outer, const AffineMap& Inner) noexcept;    // Outer ∘ Inner
[[nodiscard]] AffineMap InvertRigidMap(const AffineMap& Map) noexcept;                                 // M orthogonal
[[nodiscard]] AffineMap FrameToWorldMap(const OrthonormalFrame& Frame) noexcept;                       // local → world
[[nodiscard]] Vector3   MapPosition(const AffineMap& Map, Vector3 Position) noexcept;
[[nodiscard]] Vector3   MapDirection(const AffineMap& Map, Vector3 Direction) noexcept;
[[nodiscard]] OrthonormalFrame MapFrame(const AffineMap& Map, const OrthonormalFrame& Frame) noexcept;

} // namespace Frontier::GeometrySpace::Parametric
