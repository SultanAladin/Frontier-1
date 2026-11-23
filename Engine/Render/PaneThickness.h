/*==========================================================================================================================================
                                                          PANETHICKNESS.H
==========================================================================================================================================*/
// 🧩 Offset connected triangular panes along area-weighted normals and close boundary cycles with outward side faces.
#pragma once
#include "SurfaceGeometry.h"
#include <algorithm>
#include <map>
#include <numeric>
#include <stdexcept>

namespace Frontier
{
namespace PaneThickness
{
using Vector = std::array<double,3>;
using Triangle = std::array<uint32_t,3>;
inline Vector Subtract(Vector A,Vector B) { return {A[0]-B[0],A[1]-B[1],A[2]-B[2]}; }
inline Vector Cross(Vector A,Vector B)
{
    return {A[1]*B[2]-A[2]*B[1],A[2]*B[0]-A[0]*B[2],A[0]*B[1]-A[1]*B[0]};
}
inline double Dot(Vector A,Vector B) { return A[0]*B[0]+A[1]*B[1]+A[2]*B[2]; }
inline Vector Position(const SurfaceGeometry &Surface,uint32_t Index)
{
    const auto &P = Surface.Corners.at(Index).Position;
    return {P[0],P[1],P[2]};
}
inline Vector Normal(const SurfaceGeometry &Surface,Triangle T)
{
    return Cross(Subtract(Position(Surface,T[1]),Position(Surface,T[0])),
                 Subtract(Position(Surface,T[2]),Position(Surface,T[0])));
}
// 📝 Triangle SAT includes in-plane edge normals for coplanar intersection/contact.
inline bool Intersects(const SurfaceGeometry &Surface,Triangle A,Triangle B)
{
    std::array<Vector,3> P,Q,E,F;
    for (uint32_t I = 0; I < 3; ++I) { P[I] = Position(Surface,A[I]); Q[I] = Position(Surface,B[I]); }
    for (uint32_t I = 0; I < 3; ++I)
    {
        E[I] = Subtract(P[(I+1)%3],P[I]); F[I] = Subtract(Q[(I+1)%3],Q[I]);
    }
    auto Separated = [&](Vector Axis)
    {
        const double Length = std::sqrt(Dot(Axis,Axis));
        if (Length < 1e-18) return false;
        for (auto &C : Axis) C /= Length;
        double MinP = Dot(P[0],Axis), MaxP = MinP, MinQ = Dot(Q[0],Axis), MaxQ = MinQ;
        for (uint32_t I = 1; I < 3; ++I)
        {
            MinP = std::min(MinP,Dot(P[I],Axis)); MaxP = std::max(MaxP,Dot(P[I],Axis));
            MinQ = std::min(MinQ,Dot(Q[I],Axis)); MaxQ = std::max(MaxQ,Dot(Q[I],Axis));
        }
        return MaxP < MinQ-1e-9 || MaxQ < MinP-1e-9;
    };
    const auto N = Cross(E[0],E[1]), M = Cross(F[0],F[1]);
    if (Separated(N) || Separated(M)) return false;
    for (const auto &U : E)
    {
        if (Separated(Cross(N,U))) return false;
        for (const auto &V : F) if (Separated(Cross(U,V))) return false;
    }
    for (const auto &V : F) if (Separated(Cross(M,V))) return false;
    return true;
}
} // namespace PaneThickness

//------------------------------------------------------------------------------------------------------------------------
//                                                     FINITE THICKNESS CONSTRUCTION
//------------------------------------------------------------------------------------------------------------------------
struct ThickenedPane
{
    SurfaceGeometry Surface;
    uint32_t PaneCount = 0, BoundaryEdges = 0;
    double SignedVolume = 0;
};

inline ThickenedPane ThickenPane(const SurfaceGeometry &Input,double Thickness)
{
    using namespace PaneThickness;
    std::string Diagnostic;
    if (!ValidateSurfaceGeometry(Input,Diagnostic) || !std::isfinite(Thickness) || Thickness < .0001 ||
        Thickness > .05 || Input.TriangleCorners.size() > 1536)
        throw std::runtime_error("Pane input/thickness outside finite bounded construction.");
    ThickenedPane Result;
    SurfaceGeometry Welded;
    std::map<std::array<float,3>,uint32_t> Positions;
    std::vector<Triangle> Faces;
    std::vector<Vector> Normals;
    for (size_t I = 0; I < Input.TriangleCorners.size(); I += 3)
    {
        Triangle T;
        for (uint32_t C = 0; C < 3; ++C)
        {
            const auto &Corner = Input.Corners[Input.TriangleCorners[I+C]];
            auto [At,Inserted] = Positions.emplace(Corner.Position,uint32_t(Welded.Corners.size()));
            if (Inserted) Welded.Corners.push_back(Corner);
            T[C] = At->second;
        }
        const auto N = Normal(Welded,T);
        if (Dot(N,N) < 1e-16) throw std::runtime_error("Degenerate pane triangle.");
        for (uint32_t C = 0; C < 3; ++C)
        {
            const auto E = Subtract(Position(Welded,T[C]),Position(Welded,T[(C+1)%3]));
            if (std::sqrt(Dot(E,E)) < Thickness*2) throw std::runtime_error("Pane thickness exceeds local edge clearance.");
        }
        Faces.push_back(T); Normals.push_back(N);
    }
    std::map<std::pair<uint32_t,uint32_t>,std::vector<std::pair<uint32_t,int>>> Edges;
    std::vector<Vector> Averaged(Welded.Corners.size());
    std::vector<uint32_t> Component(Faces.size());
    std::iota(Component.begin(),Component.end(),0u);
    auto Root = [&](uint32_t I) { while (Component[I] != I) I = Component[I]; return I; };
    for (uint32_t I = 0; I < Faces.size(); ++I)
        for (uint32_t C = 0; C < 3; ++C)
        {
            const auto A = Faces[I][C], B = Faces[I][(C+1)%3];
            Edges[std::minmax(A,B)].push_back({I,A < B ? 1 : -1});
            for (uint32_t K = 0; K < 3; ++K) Averaged[A][K] += Normals[I][K];
        }
    std::vector<uint32_t> BoundaryDegree(Welded.Corners.size());
    for (const auto &[Edge,Incidence] : Edges)
    {
        if (Incidence.size() > 2 || (Incidence.size() == 2 && Incidence[0].second == Incidence[1].second))
            throw std::runtime_error("Nonmanifold or inconsistent pane winding.");
        if (Incidence.size() == 2)
        {
            const auto A = Incidence[0].first, B = Incidence[1].first;
            if (Dot(Normals[A],Normals[B]) <= 0) throw std::runtime_error("Pane folds beyond supported normal offset.");
            Component[Root(A)] = Root(B);
        }
        else { ++BoundaryDegree[Edge.first]; ++BoundaryDegree[Edge.second]; ++Result.BoundaryEdges; }
    }
    for (const auto Degree : BoundaryDegree)
        if (Degree != 0 && Degree != 2) throw std::runtime_error("Pane boundary is not a simple cycle.");
    std::map<uint32_t,uint32_t> OpenEdges;
    for (const auto &[Edge,Incidence] : Edges)
    {
        (void)Edge;
        if (Incidence.size() == 1) ++OpenEdges[Root(Incidence[0].first)];
    }
    for (uint32_t I = 0; I < Faces.size(); ++I)
        if (Root(I) == I)
        {
            if (!OpenEdges[I]) throw std::runtime_error("Already closed surfaces are not pane inputs.");
            ++Result.PaneCount;
        }
    std::vector<uint32_t> VertexPane(Welded.Corners.size(),uint32_t(-1));
    for (uint32_t I = 0; I < Faces.size(); ++I)
        for (uint32_t V : Faces[I])
        {
            if (VertexPane[V] != uint32_t(-1) && VertexPane[V] != Root(I))
                throw std::runtime_error("Separate pane surfaces meet at a vertex.");
            VertexPane[V] = Root(I);
        }
    for (uint32_t I = 0; I < Welded.Corners.size(); ++I)
    {
        const double Length = std::sqrt(Dot(Averaged[I],Averaged[I]));
        if (Length < 1e-12) throw std::runtime_error("Pane vertex has no offset normal.");
        for (auto &C : Averaged[I]) C /= Length;
        for (float Side : {1.f,-1.f})
        {
            auto Corner = Welded.Corners[I];
            for (uint32_t C = 0; C < 3; ++C)
            {
                Corner.Position[C] += float(Side*Thickness*.5*Averaged[I][C]);
                Corner.Normal[C] = float(Side*Averaged[I][C]);
            }
            Result.Surface.Corners.push_back(Corner);
        }
    }
    auto Append = [&](Triangle T)
    {
        const auto N = Normal(Result.Surface,T);
        if (Dot(N,N) < 1e-18) throw std::runtime_error("Offset collapsed a pane face.");
        Result.Surface.TriangleCorners.insert(Result.Surface.TriangleCorners.end(),T.begin(),T.end());
    };
    for (uint32_t I = 0; I < Faces.size(); ++I)
    {
        const auto T = Faces[I];
        Triangle Front{T[0]*2,T[1]*2,T[2]*2}, Back{T[2]*2+1,T[1]*2+1,T[0]*2+1};
        if (Dot(Normal(Result.Surface,Front),Normals[I]) <= 0 || Dot(Normal(Result.Surface,Back),Normals[I]) >= 0)
            throw std::runtime_error("Pane offset inverted a face.");
        Append(Front); Append(Back);
    }
    for (const auto &[Edge,Incidence] : Edges)
        if (Incidence.size() == 1)
        {
            auto [A,B] = Edge;
            if (Incidence[0].second < 0) std::swap(A,B);
            Append({A*2,A*2+1,B*2+1}); Append({A*2,B*2+1,B*2});
        }
    std::map<std::pair<uint32_t,uint32_t>,std::pair<uint32_t,int>> ClosedEdges;
    const auto &Indices = Result.Surface.TriangleCorners;
    std::vector<Triangle> Closed;
    std::map<uint32_t,double> Volumes;
    std::map<uint32_t,Vector> Origins;
    for (size_t I = 0; I < Indices.size(); I += 3)
    {
        Triangle T{Indices[I],Indices[I+1],Indices[I+2]};
        Closed.push_back(T);
        for (uint32_t C = 0; C < 3; ++C)
        {
            uint32_t A = T[C], B = T[(C+1)%3];
            auto &Edge = ClosedEdges[std::minmax(A,B)];
            ++Edge.first; Edge.second += A < B ? 1 : -1;
        }
        const auto Pane = VertexPane[T[0]/2];
        if (!Origins.contains(Pane)) Origins[Pane] = Position(Welded,T[0]/2);
        const auto Origin = Origins[Pane];
        const double Volume = Dot(Subtract(Position(Result.Surface,T[0]),Origin),
            Cross(Subtract(Position(Result.Surface,T[1]),Origin),Subtract(Position(Result.Surface,T[2]),Origin)))/6;
        Result.SignedVolume += Volume; Volumes[Pane] += Volume;
    }
    for (const auto &[Edge,Incidence] : ClosedEdges)
    {
        (void)Edge;
        if (Incidence.first != 2 || Incidence.second != 0) throw std::runtime_error("Output pane is not closed/outward coherent.");
    }
    for (const auto &[Pane,Volume] : Volumes)
    {
        (void)Pane;
        if (!std::isfinite(Volume) || Volume <= 1e-10) throw std::runtime_error("Pane component has nonpositive volume.");
    }
    if (!ValidateSurfaceGeometry(Result.Surface,Diagnostic) || !std::isfinite(Result.SignedVolume) || Result.SignedVolume <= 1e-10)
        throw std::runtime_error("Output pane has invalid geometry or volume.");
    for (uint32_t A = 0; A < Closed.size(); ++A)
        for (uint32_t B = A+1; B < Closed.size(); ++B)
        {
            bool Adjacent = false;
            for (uint32_t I : Closed[A]) for (uint32_t J : Closed[B]) Adjacent |= I == J;
            if (!Adjacent && Intersects(Result.Surface,Closed[A],Closed[B]))
                throw std::runtime_error("Offset pane surfaces intersect or touch.");
        }
    return Result;
}
} // namespace Frontier
