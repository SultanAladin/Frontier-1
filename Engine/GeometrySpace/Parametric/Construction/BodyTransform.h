/*====================================================================================================================================
                                                        BODYTRANSFORM.H
====================================================================================================================================*/
// 🧩 Transformed copies of a body — move, rotate, mirror, scale and arrays (G2), exact on every carrier kind it accepts

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Surfacing/CarrierApproximation.h"
#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <string>
#include <limits>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    STRUCTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 x ↦ M·x + Shift with M given by its columns (the images of the unit axes).
struct AffineMap
{
    Vector3 ColumnX = { 1.0, 0.0, 0.0 };                                                     // [-]   - M·x̂
    Vector3 ColumnY = { 0.0, 1.0, 0.0 };                                                     // [-]   - M·ŷ
    Vector3 ColumnZ = { 0.0, 0.0, 1.0 };                                                     // [-]   - M·ẑ
    Vector3 Shift   = {};                                                                    // [len] - Translation
};

struct TransformOutcome
{
    BodyToken              Body        = {};                                                 // [-] - New body (the source is untouched)
    std::string            Refusal     = {};                                                 // [-] - "Transform: …" when refused
    std::vector<FaceToken> SourceFaces = {};                                                 // [-] - Source faces, in copy order
    std::vector<FaceToken> Faces       = {};                                                 // [-] - Their images (same order: names carry over)
    double                 Volume      = std::numeric_limits<double>::quiet_NaN();           // [len³] - Image volume: Σ of the assembly's signed shell volumes (NaN for a sheet)
};

// 📝 ApproximateBodyAsNurbs: the copy plus the certified worst deviations of the approximated carriers.
struct ApproximationOutcome
{
    TransformOutcome Transform         = {};                                                 // [-]   - The all-NURBS copy
    double           SurfaceDeviation  = 0.0;                                                // [len] - Worst face stand-in deviation
    double           CurveDeviation    = 0.0;                                                // [len] - Worst edge stand-in deviation
    int              ApproximatedFaces = 0;                                                  // [-]   - Rolling-ball / parallel faces replaced
    int              ApproximatedEdges = 0;                                                  // [-]   - Traced edges replaced
    int              PoleCount         = 0;                                                  // [-]   - Poles of the face stand-ins
    int              TolerantEdges     = 0;                                                  // [-]   - Edges given a 2 × tolerance tube
};

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

[[nodiscard]] AffineMap TranslationMap(Vector3 Displacement) noexcept;
[[nodiscard]] AffineMap RotationMap(Vector3 AxisPoint, Vector3 AxisDirection, double Angle) noexcept;
[[nodiscard]] AffineMap ReflectionMap(Vector3 PlanePoint, Vector3 PlaneNormal) noexcept;
[[nodiscard]] AffineMap ScalingMap(Vector3 Centre, Vector3 Factors) noexcept;

// 💡 Copies Source through Map into a new body (blueprint assembly). Similarities (M = s·Q, Q orthogonal) are exact on
//    every analytic carrier: frames become X′ = Q·X, Y′ = ±Q·Y (sign of det M), so Z′ = Q·Z, face senses are kept and
//    circle / ellipse parameters are unchanged (line parameters scale by s). A mirror (det M < 0) reverses every loop.
//    Other affine maps (non-uniform scale, shear) promote curved analytic faces and conic edges to their exact NURBS
//    form first and map its poles — exact as well. NURBS carriers map by their poles under any affine map.
//    Intersection-curve edges and rolling-ball faces map exactly under similarities only (refused otherwise).
//    KnownVolume: the source's volume when the caller already has it (a body moved before); a one-shell image then
//    skips the assembly's volume integral (det M · V).
[[nodiscard]] TransformOutcome TransformBody(ModelPartition& Partition, BodyToken Source, const AffineMap& Map,
                                             double KnownVolume = std::numeric_limits<double>::quiet_NaN());

// As TransformBody, reading Source from Partition and assembling the copy into Target (Map = TranslationMap({}) is a
// plain transfer): composite constructions can be built in a scratch partition and only the result kept.
[[nodiscard]] TransformOutcome TransformBodyAcross(const ModelPartition& Partition, BodyToken Source, ModelPartition& Target, const AffineMap& Map);

// Exact NURBS copy: every analytic face becomes its rational B-spline form (covering the face chart), every line and
// conic edge its NURBS curve over the same τ interval. Traced edges and rolling-ball faces are kept as they are.
[[nodiscard]] TransformOutcome PromoteBodyToNurbs(ModelPartition& Partition, BodyToken Source);

// 💡 All-NURBS copy for exchange (priority 10 N3, option A): as PromoteBodyToNurbs, and every carrier without an exact
//    NURBS form — rolling-ball and parallel faces, traced intersection / contact edges — becomes a certified cubic
//    B-spline in its own parameterisation (Surfacing/CarrierApproximation.h), within Settings.Tolerance. The exact
//    source body stays the reference; the copy is refused atomically when any carrier cannot be certified.
[[nodiscard]] ApproximationOutcome ApproximateBodyAsNurbs(ModelPartition& Partition, BodyToken Source, const ApproximationSettings& Settings = {});

// 💡 The same sheet turned over (flip normals): every face sense reversed and every loop walked the other way, carriers,
//    vertices and edges unchanged. Refused for solids (their faces point out of the material by definition).
[[nodiscard]] TransformOutcome ReverseBody(ModelPartition& Partition, BodyToken Source);

// 💡 Flip some of a sheet: the pieces (faces joined through shared edges) holding a Seeds face turn over, the others
//    stay — a piece turns as a whole, so neighbouring faces always agree across their edges. Pieces (optional) gets how
//    many pieces turned. Refused for solids and for seeds off the body.
[[nodiscard]] TransformOutcome ReverseBodyPieces(ModelPartition& Partition, BodyToken Source, const std::vector<FaceToken>& Seeds, int* Pieces = nullptr);

// Mirror copy across the plane through PlanePoint with normal PlaneNormal.
[[nodiscard]] TransformOutcome MirrorBody(ModelPartition& Partition, BodyToken Source, Vector3 PlanePoint, Vector3 PlaneNormal);

// Linear array: Count − 1 copies at Step, 2·Step, … (the source is the first instance and is not copied).
[[nodiscard]] std::vector<TransformOutcome> ArrayBody(ModelPartition& Partition, BodyToken Source, Vector3 Step, int Count);

// Circular array: Count − 1 copies rotated by k·Sweep / Count about the axis (k = 1 … Count − 1; Sweep = 2π spaces them
// evenly around a full turn). Rotations are similarities, so every carrier stays exact.
[[nodiscard]] std::vector<TransformOutcome> CircularArrayBody(ModelPartition& Partition, BodyToken Source, Vector3 AxisPoint, Vector3 AxisDirection, int Count,
                                                              double Sweep = 6.283185307179586);

} // namespace Frontier::GeometrySpace::Parametric
