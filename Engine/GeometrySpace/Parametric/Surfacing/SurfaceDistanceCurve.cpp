#include "Engine/GeometrySpace/Parametric/Surfacing/SurfaceDistanceCurve.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>

namespace Frontier::GeometrySpace::Parametric {
namespace {
constexpr double Infinity = std::numeric_limits<double>::infinity();
Vector3 ClosestTriangle(Vector3 P, Vector3 A, Vector3 B, Vector3 C) {
    const auto AB=B-A, AC=C-A, AP=P-A;
    const double D1=Dot(AB,AP), D2=Dot(AC,AP);
    if(D1<=0 && D2<=0) return A;
    const auto BP=P-B; const double D3=Dot(AB,BP),D4=Dot(AC,BP);
    if(D3>=0 && D4<=D3) return B;
    const double VC=D1*D4-D3*D2;
    if(VC<=0 && D1>=0 && D3<=0) return A+AB*(D1/(D1-D3));
    const auto CP=P-C; const double D5=Dot(AB,CP),D6=Dot(AC,CP);
    if(D6>=0 && D5<=D6) return C;
    const double VB=D5*D2-D1*D6;
    if(VB<=0 && D2>=0 && D6<=0) return A+AC*(D2/(D2-D6));
    const double VA=D3*D6-D5*D4;
    if(VA<=0 && D4-D3>=0 && D5-D6>=0) return B+(C-B)*((D4-D3)/((D4-D3)+(D5-D6)));
    const double Sum=VA+VB+VC;
    if(std::abs(Sum)<1e-30) return A;
    return A+AB*(VB/Sum)+AC*(VC/Sum);
}
// Upwind Eikonal update through a triangle, including edge-only fallbacks. This
// crosses triangle interiors; using only a shortest path on mesh edges is NOT a geodesic solver.
double Arrival(Vector3 A,Vector3 B,Vector3 C,double DA,double DB) {
    double Best=std::min(DA+Length(C-A),DB+Length(C-B));
    const double L=Length(B-A); if(L<1e-15) return Best;
    const double Q=(DB-DA)/L; if(std::abs(Q)>=1) return Best;
    const auto Axis=(B-A)*(1/L);const double X=Dot(C-A,Axis), H=Length(C-A-Axis*X);
    const double Root=std::sqrt(1-Q*Q), S=X-H*Q/Root;
    const double Candidate=DA+Q*X+H*Root;
    if(S>=0 && S<=L && Candidate>=std::max(DA,DB)) Best=std::min(Best,Candidate);
    return Best;
}
NurbsCurve Polyline(const std::vector<Vector3>& P) {
    NurbsCurveDefinition D;D.Degree=1;D.Poles=P;D.Weights.assign(P.size(),1);
    D.Knots={0,0};
    for(size_t I=1;I+1<P.size();++I) D.Knots.push_back(double(I)/(P.size()-1));
    D.Knots.insert(D.Knots.end(),{1,1});
    return NurbsCurve{std::make_shared<NurbsCurveDefinition>(std::move(D))};
}
double PointPath(Vector3 P,const std::vector<Vector3>& Path) {
    double Best=Infinity;
    for(size_t I=1;I<Path.size();++I) {
        auto V=Path[I]-Path[I-1];double L=Dot(V,V);
        Best=std::min(Best,Length(P-(Path[I-1]+V*(L>0?std::clamp(Dot(P-Path[I-1],V)/L,0.0,1.0):0))));
    }
    return Best;
}
}
SurfaceDistanceCurveOutcome TraceSurfaceDistanceCurve(const PolygonComplex& M,const NurbsCurve& Guide,
    const NurbsCurve& Target,double Offset,double Tolerance) {
    SurfaceDistanceCurveOutcome Out;
    auto Fail=[&](const char* Why){Out.Refusal=Why;return Out;};
    if(!Guide.Definition || !Target.Definition || M.Triangles.empty() || !(Offset>0) || !(Tolerance>0) || !std::isfinite(Offset+Tolerance))
        return Fail("Surface offset needs two valid curves, a reference surface and finite positive distance/tolerance");
    const size_t N=M.Positions.size();
    std::vector<std::vector<int>> Incident(N);
    for(int I=0;I<int(M.Triangles.size());++I) for(int V:M.Triangles[I]) {
        if(V<0 || size_t(V)>=N) return Fail("Invalid reference triangulation");
        Incident[V].push_back(I);
    }
    std::vector<double> D(N,Infinity);std::vector<bool> Accepted(N,false);
    using Entry=std::pair<double,int>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> Heap;
    double LengthEstimate=0;auto Previous=EvaluateNurbsCurve(*Guide.Definition,0).Position;
    for(int I=1;I<=64;++I){auto P=EvaluateNurbsCurve(*Guide.Definition,I/64.0).Position;LengthEstimate+=Length(P-Previous);Previous=P;}
    const int Samples=std::clamp(int(std::ceil(LengthEstimate/std::max(Tolerance,Offset/16))),64,4096);
    for(int I=0;I<=Samples;++I) {
        auto P=EvaluateNurbsCurve(*Guide.Definition,double(I)/Samples).Position;double Best=Infinity;int At=-1;Vector3 Foot{};
        for(int J=0;J<int(M.Triangles.size());++J) {
            auto T=M.Triangles[J];auto Q=ClosestTriangle(P,M.Positions[T[0]],M.Positions[T[1]],M.Positions[T[2]]);
            double Gap=Length(Q-P);if(Gap<Best){Best=Gap;At=J;Foot=Q;}
        }
        Out.SourceGap=std::max(Out.SourceGap,Best);
        if(Best>Tolerance) return Fail("Guide leaves the selected reference: project/repair the guide or increase the explicit source tolerance");
        for(int V:M.Triangles[At]) {double Seed=Length(M.Positions[V]-Foot);if(Seed<D[V]){D[V]=Seed;Heap.emplace(Seed,V);}}
    }
    while(!Heap.empty()) {
        auto [Distance,U]=Heap.top();Heap.pop();if(Accepted[U] || Distance!=D[U])continue;Accepted[U]=true;
        for(int At:Incident[U]) {
            auto T=M.Triangles[At];
            for(int K=0;K<3;++K) {
                int V=T[K];if(Accepted[V])continue;
                int A=T[(K+1)%3],B=T[(K+2)%3];double Candidate=Infinity;
                if(Accepted[A]) Candidate=std::min(Candidate,D[A]+Length(M.Positions[V]-M.Positions[A]));
                if(Accepted[B]) Candidate=std::min(Candidate,D[B]+Length(M.Positions[V]-M.Positions[B]));
                if(Accepted[A] && Accepted[B]) Candidate=std::min(Candidate,Arrival(M.Positions[A],M.Positions[B],M.Positions[V],D[A],D[B]));
                if(Candidate<D[V]){D[V]=Candidate;Heap.emplace(Candidate,V);}
            }
        }
    }
    // Shared edge keys, not positional welding: disconnected/coincident sheets stay disconnected.
    std::map<std::pair<int,int>,int> Nodes;std::vector<Vector3> Points;std::vector<std::vector<int>> Links;
    const double Iso=Offset*(1+1e-10);
    for(const auto& T:M.Triangles) {
        std::vector<int> Crossings;
        for(int K=0;K<3;++K) {
            int A=T[K],B=T[(K+1)%3];if(!std::isfinite(D[A]+D[B]) || (D[A]<Iso)==(D[B]<Iso))continue;
            auto Key=std::minmax(A,B);auto [It,Made]=Nodes.emplace(Key,int(Points.size()));
            if(Made){Points.push_back(M.Positions[A]+(M.Positions[B]-M.Positions[A])*((Iso-D[A])/(D[B]-D[A])));Links.emplace_back();}
            Crossings.push_back(It->second);
        }
        if(Crossings.size()==2 && Crossings[0]!=Crossings[1]) {
            int A=Crossings[0],B=Crossings[1];Links[A].push_back(B);Links[B].push_back(A);
        }
    }
    for(auto& L:Links){std::sort(L.begin(),L.end());L.erase(std::unique(L.begin(),L.end()),L.end());if(L.size()>2)return Fail("Non-manifold offset contour: choose another distance or repair the reference");}
    std::vector<std::vector<Vector3>> Paths;std::set<std::pair<int,int>> Used;
    auto Walk=[&](int Start,int Next) {
        std::vector<Vector3> Path{Points[Start]};int A=Start,B=Next;
        for(size_t Limit=0;Limit<=Points.size();++Limit) {
            auto Key=std::minmax(A,B);if(Used.contains(Key))break;Used.insert(Key);Path.push_back(Points[B]);
            int C=-1;for(int V:Links[B])if(V!=A && !Used.contains(std::minmax(B,V))){C=V;break;}
            if(C<0)break;
            A=B;B=C;
        }
        if(Path.size()>1)Paths.push_back(std::move(Path));
    };
    for(int I=0;I<int(Points.size());++I) if(Links[I].size()==1)Walk(I,Links[I][0]);
    for(int I=0;I<int(Points.size());++I) for(int J:Links[I])if(!Used.contains(std::minmax(I,J)))Walk(I,J);
    if(Paths.empty())return Fail("No connected surface contour at this distance (boundary, gap or distance too large)");
    std::vector<Vector3> Seeds;for(int I=0;I<=32;++I) Seeds.push_back(EvaluateNurbsCurve(*Target.Definition,I/32.0).Position);
    double Best=Infinity;size_t Chosen=0;
    for(size_t I=0;I<Paths.size();++I){double Score=0;for(auto P:Seeds)Score+=PointPath(P,Paths[I]);if(Score<Best){Best=Score;Chosen=I;}}
    auto Path=Paths[Chosen];
    if(Length(Seeds.front()-Seeds.back())>Tolerance) {
        // Trim the chosen contour to the target's endpoint stations. Preserve the
        // side/extent instead of replacing a short target with the whole closed tube.
        auto Station=[&](Vector3 P){double BestGap=Infinity;double S=0;
            for(size_t I=1;I<Path.size();++I){auto V=Path[I]-Path[I-1];double Q=Dot(V,V);double F=Q>0?std::clamp(Dot(P-Path[I-1],V)/Q,0.0,1.0):0;double Gap=Length(P-(Path[I-1]+V*F));if(Gap<BestGap){BestGap=Gap;S=I-1+F;}}return S;};
        auto At=[&](double S){size_t I=std::min(size_t(S),Path.size()-2);return Path[I]+(Path[I+1]-Path[I])*(S-I);};
        double A=Station(Seeds.front()),B=Station(Seeds.back());bool Reversed=A>B;if(Reversed)std::swap(A,B);
        std::vector<Vector3> Part{At(A)};for(int I=int(std::floor(A))+1;I<B;++I)Part.push_back(Path[I]);Part.push_back(At(B));
        if(Length(Path.front()-Path.back())<=Tolerance) {
            std::vector<Vector3> Other{At(B)};for(int I=int(std::floor(B))+1;I<int(Path.size());++I)Other.push_back(Path[I]);
            for(int I=1;I<A;++I)Other.push_back(Path[I]);
            Other.push_back(At(A));
            double First=0,Second=0;for(auto P:Seeds){First+=PointPath(P,Part);Second+=PointPath(P,Other);}if(Second<First){Part=std::move(Other);Reversed=!Reversed;}
        }
        if(Reversed)std::reverse(Part.begin(),Part.end());
        Path=std::move(Part);
    }
    std::vector<Vector3> Clean;for(auto P:Path)if(Clean.empty() || Length(P-Clean.back())>1e-10)Clean.push_back(P);
    if(Clean.size()<2)return Fail("Offset target extent collapses on this contour");
    Out.Curve=Polyline(Clean);return Out;
}
SurfaceDistanceCurveOutcome OffsetCurveOnBody(const ModelPartition& P,BodyToken Body,const NurbsCurve& Guide,
    const NurbsCurve& Target,double Distance,double Tolerance) {
    SurfaceDistanceCurveOutcome Last;NurbsCurve Previous;
    if(!(Tolerance>0) || !(Distance>0) || !std::isfinite(Tolerance+Distance)) {Last.Refusal="Distance and tolerance must be finite and positive";return Last;}
    for(int Level=0;Level<5;++Level) {
        TessellationSettings Settings;Settings.Sag=Tolerance*.125;Settings.EdgeLengthRatio=.02/std::pow(1.5,Level);Settings.TriangleLimit=1000000;
        auto Mesh=TessellateBody(P,Body,Settings);
        if(!Mesh.Refusal.empty()){Last.Refusal="Surface offset tessellation: "+Mesh.Refusal;return Last;}
        Last=TraceSurfaceDistanceCurve(Mesh.Complex,Guide,Target,Distance,std::max(Tolerance*4,Mesh.Sag*4));
        if(!Last.Refusal.empty())return Last;
        if(Previous.Definition) {
            double Change=0;
            // Both results are degree-one contours: measure exact segment distances,
            // not Newton projections from one seed (which can settle at the wrong knot).
            for(int Side=0;Side<2;++Side) {
                const auto& A=(Side?Previous:Last.Curve).Definition->Poles;
                const auto& B=(Side?Last.Curve:Previous).Definition->Poles;
                for(size_t I=0;I<A.size();++I) {
                    Change=std::max(Change,PointPath(A[I],B));
                    if(I) Change=std::max(Change,PointPath((A[I-1]+A[I])*.5,B));
                }
            }
            if(Change<=Tolerance)return Last;
        }
        Previous=Last.Curve;
    }
    Last.Curve={};Last.Refusal="Surface-distance contour did not converge at this tolerance; no XYZ approximation was substituted";return Last;
}
}
