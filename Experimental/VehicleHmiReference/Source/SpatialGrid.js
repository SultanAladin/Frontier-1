// Shared world-cell convention for the gas lattice and particle surface deposits.
// Match PlumeRecords.wgsl: floor world coordinates; positive modulo for negative cells.
export const WorldGridCell=(Position,Edge)=>Position.map(V=>Math.floor(V/Edge));
export const WrapGridCell=(Cell,Extent)=>Cell.map((V,I)=>(V%Extent[I]+Extent[I])%Extent[I]);
export const FlattenGridCell=(Cell,Extent)=>(Cell[2]*Extent[1]+Cell[1])*Extent[0]+Cell[0];
export function RecentreGridWindow(Focus,Edge,Extent,Origin,Force=false){
 const Cell=WorldGridCell(Focus.slice(0,2),Edge),Half=Extent.slice(0,2).map(V=>V>>1),Centre=Origin.map((V,I)=>V+Half[I]);
 return Force||Cell.some((V,I)=>Math.abs(V-Centre[I])>Half[I]/2)?Cell.map((V,I)=>V-Half[I]):Origin;
}
export const SurfaceChunkKey=(Surface,X,Y)=>JSON.stringify([Surface,X,Y]);
