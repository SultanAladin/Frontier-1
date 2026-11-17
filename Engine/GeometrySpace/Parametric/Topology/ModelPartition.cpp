/*====================================================================================================================================
                                                       MODELPARTITION.CPP
====================================================================================================================================*/
// 🧩 B-rep topology (Body → Region → SolidShell → Face → Loop → Fin → Edge → Vertex) with attached exact geometry

#include "Engine/GeometrySpace/Parametric/Topology/ModelPartition.h"

#include <algorithm>

namespace Frontier::GeometrySpace::Parametric
{

//------------------------------------------------------------------------------------------------------------------------
//                                                    FIN GEOMETRY OF USE
//------------------------------------------------------------------------------------------------------------------------

VertexToken ModelPartition::FinStart(FinToken Fin) const noexcept
{
    const FinProfile&  Use  = Fins.Resolve(Fin);
    const EdgeProfile& Edge = Edges.Resolve(Use.Edge);
    return Use.Sense == Orientation::Forward ? Edge.Start : Edge.End;
}

VertexToken ModelPartition::FinEnd(FinToken Fin) const noexcept
{
    const FinProfile&  Use  = Fins.Resolve(Fin);
    const EdgeProfile& Edge = Edges.Resolve(Use.Edge);
    return Use.Sense == Orientation::Forward ? Edge.End : Edge.Start;
}

double ModelPartition::FinParameterStart(FinToken Fin) const noexcept
{
    const FinProfile&  Use  = Fins.Resolve(Fin);
    const EdgeProfile& Edge = Edges.Resolve(Use.Edge);
    return Use.Sense == Orientation::Forward ? Edge.IntervalLow : Edge.IntervalHigh;
}

double ModelPartition::FinParameterEnd(FinToken Fin) const noexcept
{
    const FinProfile&  Use  = Fins.Resolve(Fin);
    const EdgeProfile& Edge = Edges.Resolve(Use.Edge);
    return Use.Sense == Orientation::Forward ? Edge.IntervalHigh : Edge.IntervalLow;
}

FinToken ModelPartition::RadialMate(FinToken Fin) const noexcept
{
    return Fins.Resolve(Fin).RadialSuccessor;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TRAVERSAL
//------------------------------------------------------------------------------------------------------------------------

void ModelPartition::EnumerateLoopFins(LoopToken Loop, const std::function<void(FinToken)>& Visitor) const
{
    const FinToken First = Loops.Resolve(Loop).FirstFin;                                     // [-] - Cycle entry
    if (!First.PresentCondition()) return;
    FinToken   Cursor     = First;                                                           // [-] - Walking fin
    size_t     GuardCount = 0;                                                               // [-] - Breaks corrupt cycles
    const size_t GuardLimit = Fins.OccupiedCount() + 1;                                      // [-] - Upper bound on a cycle
    do
    {
        Visitor(Cursor);
        Cursor = Fins.Resolve(Cursor).Successor;
    } while (Cursor.PresentCondition() && !(Cursor == First) && ++GuardCount < GuardLimit);
}

FinToken ModelPartition::ResolveCorner(LoopToken Loop, VertexToken Vertex) const noexcept
{
    FinToken Corner = {};                                                                    // [-] - First fin leaving Vertex
    EnumerateLoopFins(Loop, [&](FinToken Fin)
    {
        if (!Corner.PresentCondition() && FinStart(Fin) == Vertex) Corner = Fin;
    });
    return Corner;
}

BodyInventory ModelPartition::CollectInventory(BodyToken Body) const
{
    BodyInventory Inventory;
    const auto AppendUnique = [](auto& Sequence, auto Token)
    {
        if (std::find(Sequence.begin(), Sequence.end(), Token) == Sequence.end()) Sequence.push_back(Token);
    };
    for (RegionToken Region : Bodies.Resolve(Body).Regions)
    {
        Inventory.Regions.push_back(Region);
        for (SolidShellToken Shell : Regions.Resolve(Region).Shells)
        {
            Inventory.Shells.push_back(Shell);
            for (FaceToken Face : Shells.Resolve(Shell).Faces)
            {
                Inventory.Faces.push_back(Face);
                for (LoopToken Loop : Faces.Resolve(Face).Loops)
                {
                    Inventory.Loops.push_back(Loop);
                    const LoopProfile& Cycle = Loops.Resolve(Loop);
                    if (Cycle.IsolatedVertex.PresentCondition()) AppendUnique(Inventory.Vertices, Cycle.IsolatedVertex);
                    EnumerateLoopFins(Loop, [&](FinToken Fin)
                    {
                        Inventory.Fins.push_back(Fin);
                        const EdgeToken Edge = Fins.Resolve(Fin).Edge;
                        AppendUnique(Inventory.Edges, Edge);
                        AppendUnique(Inventory.Vertices, Edges.Resolve(Edge).Start);
                        AppendUnique(Inventory.Vertices, Edges.Resolve(Edge).End);
                    });
                }
            }
        }
    }
    return Inventory;
}

BodyToken ModelPartition::EnclosingBody(FaceToken Face) const noexcept
{
    return Regions.Resolve(Shells.Resolve(Faces.Resolve(Face).Shell).Region).Body;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    GEOMETRY ATTACHMENT
//------------------------------------------------------------------------------------------------------------------------

CurveToken ModelPartition::AttachCurve(EdgeToken Edge, CurveGeometry Curve, double IntervalLow, double IntervalHigh)
{
    const CurveToken Carrier = Curves.Issue(std::move(Curve));                               // [-] - New curve record
    EdgeProfile&     Profile = Edges.Resolve(Edge);
    Profile.Curve        = Carrier;
    Profile.IntervalLow  = IntervalLow;
    Profile.IntervalHigh = IntervalHigh;
    return Carrier;
}

SurfaceToken ModelPartition::AttachSurface(FaceToken Face, SurfaceGeometry Surface, Orientation Sense)
{
    const SurfaceToken Carrier = Surfaces.Issue(std::move(Surface));                         // [-] - New surface record
    FaceProfile&       Profile = Faces.Resolve(Face);
    Profile.Surface = Carrier;
    Profile.Sense   = Sense;
    return Carrier;
}

void ModelPartition::AttachSurfaceCurve(FinToken Fin, ParameterCurve Curve)
{
    Fins.Resolve(Fin).SurfaceCurve = std::move(Curve);
}

//------------------------------------------------------------------------------------------------------------------------
//                                                    TOLERANT MODELLING
//------------------------------------------------------------------------------------------------------------------------

void ModelPartition::AssignVertexPrecision(VertexToken Vertex, double Precision)
{
    Vertices.Resolve(Vertex).Precision = Precision;
}

void ModelPartition::AssignEdgePrecision(EdgeToken Edge, double Precision)
{
    Edges.Resolve(Edge).Precision = Precision;
}

} // namespace Frontier::GeometrySpace::Parametric
