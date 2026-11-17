/*====================================================================================================================================
                                                       TOPOLOGYREBUILD.H
====================================================================================================================================*/
// 🧩 Face verbs that change topology: extrude (side ring), inset (border ring + inner face), delete (heal or sheet), heal (merge),
//    thicken (sheet → solid)

#pragma once

//------------------------------------------------------------------------------------------------------------------------
//                                                    INCLUDES
//------------------------------------------------------------------------------------------------------------------------

#include "Engine/GeometrySpace/Parametric/Construction/BodyTransform.h"
#include "Engine/GeometrySpace/Parametric/Local/FaceChange.h"
#include "Engine/GeometrySpace/Parametric/Sketch/SketchProfile.h"

#include <string>
#include <vector>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    PUBLIC FUNCTIONS
//------------------------------------------------------------------------------------------------------------------------

// 💡 Each returns an empty string on success (Body, histories, audit and method filled) or the refusal; the input body is
//    never modified.
[[nodiscard]] std::string ExtrudePlanarFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, double Distance,
                                             BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method);
[[nodiscard]] std::string InsetPlanarFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, double Distance, double Depth,
                                           BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method);
[[nodiscard]] std::string DeleteBodyFaces(ModelPartition& Partition, BodyToken Body, const std::vector<FaceToken>& Faces, BodyToken& Result,
                                          BodyCategory& Category, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method);
// OnlyEdges / OnlyVertices (Delete on edges / vertices): just those edges dissolve and those vertices join; refused unless all go.
[[nodiscard]] std::string HealBody(ModelPartition& Partition, BodyToken Body, BodyToken& Result, std::vector<FaceChangeHistory>& Histories,
                                   AuditReport& Audit, std::string& Method, const std::vector<EdgeToken>& OnlyEdges = {},
                                   const std::vector<VertexToken>& OnlyVertices = {});
// 💡 Extrude manifold outline (Blender's push-in): a planar face's loops as a sketch profile in a frame on its plane whose
//    𝑧 is the outward normal (lines, circular arcs, circles and non-rational splines; segments named E<slot>), arranged
//    outer CCW / holes CW. IslandCondition: every neighbour across its edges is a coplanar plane facing the same way —
//    an imprinted region, which no push can move (its neighbours would have to move with it).
[[nodiscard]] std::string DescribePlanarFaceProfile(const ModelPartition& Partition, BodyToken Body, FaceToken Face, SketchProfile& Profile, OrthonormalFrame& Plane,
                                                    bool& IslandCondition);
// 💡 Sliver bridges (the Windows report "extrude manifold suppose to remove or move the top part"): a cut outline on a
//    planar face whose straight side runs parallel to a straight, convex rim edge of that face (its neighbour square or
//    sharper, so the region beyond the rim is air), closer than 1 % of the outline's span, would leave a strip of the face
//    thinner than that over the cut — a pocket drawn "to the edge" a pixel short. The side moves out past the rim by the
//    cutter lead (1e-3·scale) and its neighbouring sides follow along their own lines, so the walls stay exact and the cut
//    opens through the rim. Plane: the outline's frame; CutSign: the sign of the cut along Plane 𝑧 (the rim faces face
//    against it); Region: the imprinted face being pushed (its own edges are not rims), or none. Returns the sides moved;
//    the profile is unchanged when none qualifies or the moved outline would cross itself.
[[nodiscard]] int OpenSliverBridges(const ModelPartition& Partition, BodyToken Body, const OrthonormalFrame& Plane, double CutSign, const FaceToken* Region,
                                    SketchProfile& Profile);
// 💡 Reshape (direct vertex / edge editing): the picked vertices are carried by Map and every straight edge at them is
//    re-drawn between its new ends; each face touching a moved vertex is re-fitted — ① a plane when its new corners stay
//    coplanar (within 1e-8 · scale; a box edge slid across its faces, a corner slid along a face), ② a bilinear patch
//    (degree 1 × 1 NURBS through its four corners, its sides exactly the four straight edges) when a four-sided face can no
//    longer be flat, ③ otherwise refused — a curved face or edge at a moved vertex (a fillet, a hole's circle), a face of
//    more than four sides bent off its plane, a face turned inside out or folded, an edge collapsing to a point. Faces
//    away from the moved vertices keep their carriers and SP-curves; the result is assembled, audited and clash-checked
//    like every rebuild verb, so a reshape that would make the solid cross itself is refused.
[[nodiscard]] std::string ReshapeBodyVertices(ModelPartition& Partition, BodyToken Body, const std::vector<VertexToken>& Vertices, const AffineMap& Map,
                                              BodyToken& Result, std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method);
[[nodiscard]] std::string ThickenSheet(ModelPartition& Partition, BodyToken Body, double Thickness, BodyToken& Result,
                                       std::vector<FaceChangeHistory>& Histories, AuditReport& Audit, std::string& Method);

} // namespace Frontier::GeometrySpace::Parametric
