// Steering grip/spokes, wheel-mounted hexagonal cluster and centre tablet from
// unassignedinbox/Slate@3c987f33c71c2477bd73ca29c69d5bd0ee484d1a, CabinProjection.js.
// Geometry is unchanged; the host maps material IDs and local poses to its deferred facets.
import { CabinLayout } from './SlateCabinLayout.js';
import { ClusterOutlines } from './Cabin/ClusterSequence.js';
import { NormalizeVector, CrossVector, SubtractVector } from './TransformProjection.js';
export function ConstructSlateFittings()
{
    const Vertices=[];
    const Triangle=(Points,Material,UV=null,Normals=null)=>{
        const N=NormalizeVector(CrossVector(SubtractVector(Points[1],Points[0]),SubtractVector(Points[2],Points[0])));
        Points.forEach((P,I)=>Vertices.push({P,N:Normals?.[I]||N,UV:UV?.[I]||[0,0],Material}));
    };
    const Quad=(A,B,C,D,Material)=>{
        Triangle([A,B,C],Material,[[0,1],[1,1],[1,0]]);
        Triangle([A,C,D],Material,[[0,1],[1,0],[0,0]]);
    };
    // Toroidal grip, not a flat billboard. Spokes and display turn together on the steering column.
    const R=CabinLayout.WheelRadius, Tube=0.018, Sectors=64, Sides=10;
    const Point=(I,J)=>{const A=I/Sectors*2*Math.PI,B=J/Sides*2*Math.PI;return [(R+Tube*Math.cos(B))*Math.cos(A),(R+Tube*Math.cos(B))*Math.sin(A),Tube*Math.sin(B)];};
    for(let I=0;I<Sectors;I++) for(let J=0;J<Sides;J++) Quad(Point(I,J),Point(I+1,J),Point(I+1,J+1),Point(I,J+1),1);
    Quad([-.17,-.018,0],[.17,-.018,0],[.17,.018,0],[-.17,.018,0],1);
    Quad([-.018,-.18,0],[.018,-.18,0],[.028,0,0],[-.028,0,0],1);
    const Outline=ClusterOutlines.hexagon.Points();
    const W=.27,H=.151; // same 0.34:0.19 aspect as the original cluster, scaled to fit inside the wheel
    for(let I=0;I<Outline.length;I++)
    {
        const A=Outline[I],B=Outline[(I+1)%Outline.length];
        Triangle([[0,0,.018],[A[0]*W*.54,A[1]*H*.54,.018],[B[0]*W*.54,B[1]*H*.54,.018]],1);
        Triangle([[0,0,.023],[A[0]*W*.5,A[1]*H*.5,.023],[B[0]*W*.5,B[1]*H*.5,.023]],2,
            [[.5,.5],[A[0]*.5+.5,.5-A[1]*.5],[B[0]*.5+.5,.5-B[1]*.5]]);
    }
    const X=CabinLayout.TabletWidth/2,Y=CabinLayout.TabletHeight/2;
    Quad([-X-.012,-Y-.012,.008],[X+.012,-Y-.012,.008],[X+.012,Y+.012,.008],[-X-.012,Y+.012,.008],3);
    Quad([-X,-Y,.014],[X,-Y,.014],[X,Y,.014],[-X,Y,.014],4);
    return Vertices;
}
