/*====================================================================================================================================
                                                        VEHICLEASSEMBLY.JS
====================================================================================================================================*/
// 🧩 Decodes the VehicleHMI "Controlled Integrator" FCT1 facet archives and assembles body + 4 wheels as world-space triangles

//------------------------------------------------------------------------------------------------------------------------
//                                                    CONSTANTS
//------------------------------------------------------------------------------------------------------------------------

// 📝 Placement constants are the ones ControlledIntegratorLoader.js (UV Workspace) and VehicleSpecification.js use:
//    authored frame (+X forward, +Y left, +Z up) -> studio frame (+X forward, +Y up, +Z right), determinant +1.
const VehicleHeightRebase   = 0.11285;                 // [m]   - Authored Z of the body origin above the axle plane
const VehicleGroundLift     = 0.53;                    // [m]   - Puts the 0.509 m tyre contact on the studio floor y = 0
const RimToHubScale         = 0.34460 / 0.344818;      // [-]   - Rim archive radius -> hub socket radius
const BodyCreaseCosine      = Math.cos(38.0 * Math.PI / 180.0);   // [-] - Faces beyond 38° keep a hard edge
const CornerNormalsPresent  = 1;                       // [-]   - FCT1 flags bit 0

const AxleMounts = [
    { Label: 'FL', Mount: [ 1.7274,  1.0475, 0.0914], MirrorLateral: false },
    { Label: 'FR', Mount: [ 1.7274, -1.0475, 0.0914], MirrorLateral: true  },
    { Label: 'RL', Mount: [-1.6686,  1.0475, 0.0914], MirrorLateral: false },
    { Label: 'RR', Mount: [-1.6686, -1.0475, 0.0914], MirrorLateral: true  }
];

// 📝 CourseMaterial families (DriveCourse.h) -> studio material slots (StudioSceneAssembly.js MaterialSlot).
export const VehicleFamilySlot = Object.freeze({
    6:  'BodyCoat',
    7:  'VehicleGlass',
    8:  'VehiclePlastic',
    9:  'TyreRubber',
    10: 'HubAccent',
    11: 'BrakeDisc',
    12: 'VehicleTrim',
    13: 'Headlight',
    14: 'Taillight',
    15: 'VehicleTrim'
});


//------------------------------------------------------------------------------------------------------------------------
//                                                    ARCHIVE DECODE
//------------------------------------------------------------------------------------------------------------------------

export function DecodeFacetArchive(ArchiveBytes)
{
    const View  = new DataView(ArchiveBytes);
    const Magic = String.fromCharCode(View.getUint8(0), View.getUint8(1), View.getUint8(2), View.getUint8(3));
    if (Magic !== 'FCT1') throw new Error(`Facet archive magic "${Magic}" is not FCT1`);

    const VertexCount = View.getUint32(4, true);
    const FacetCount  = View.getUint32(8, true);
    const HeaderBits  = View.getUint32(12, true);
    let   Offset      = 48;

    const Positions = new Float32Array(ArchiveBytes, Offset, VertexCount * 3).slice();
    Offset += VertexCount * 12;
    const Facets    = new Uint32Array(ArchiveBytes, Offset, FacetCount * 3).slice();
    Offset += FacetCount * 12;
    const Families  = new Uint32Array(ArchiveBytes, Offset, FacetCount).slice();
    Offset += FacetCount * 4;

    let CornerNormals = null;
    if (HeaderBits & CornerNormalsPresent)
    {
        CornerNormals = new Float32Array(ArchiveBytes, Offset, FacetCount * 9).slice();
        Offset += FacetCount * 36;
    }
    if (Offset !== ArchiveBytes.byteLength) throw new Error(`Facet archive length ${ArchiveBytes.byteLength} != ${Offset}`);
    return { VertexCount, FacetCount, Positions, Facets, Families, CornerNormals };
}

// 📝 The body archive carries no corner normals. Each corner averages the area-weighted normals of the facets sharing
//    its vertex whose orientation lies within the crease cone of the facet itself, so panel gaps and edges stay crisp.
function DeriveCreasedCornerNormals(Archive)
{
    const { VertexCount, FacetCount, Positions, Facets } = Archive;
    const FacetNormals = new Float32Array(FacetCount * 3);
    const Incidence    = Array.from({ length: VertexCount }, () => []);

    for (let FacetIndex = 0; FacetIndex < FacetCount; FacetIndex++)
    {
        const A = Facets[FacetIndex * 3] * 3, B = Facets[FacetIndex * 3 + 1] * 3, C = Facets[FacetIndex * 3 + 2] * 3;
        const E1x = Positions[B] - Positions[A], E1y = Positions[B + 1] - Positions[A + 1], E1z = Positions[B + 2] - Positions[A + 2];
        const E2x = Positions[C] - Positions[A], E2y = Positions[C + 1] - Positions[A + 1], E2z = Positions[C + 2] - Positions[A + 2];
        FacetNormals[FacetIndex * 3]     = E1y * E2z - E1z * E2y;   // 📝 Unnormalized: magnitude = 2 × area weight
        FacetNormals[FacetIndex * 3 + 1] = E1z * E2x - E1x * E2z;
        FacetNormals[FacetIndex * 3 + 2] = E1x * E2y - E1y * E2x;
        for (let Corner = 0; Corner < 3; Corner++) Incidence[Facets[FacetIndex * 3 + Corner]].push(FacetIndex);
    }

    const CornerNormals = new Float32Array(FacetCount * 9);
    for (let FacetIndex = 0; FacetIndex < FacetCount; FacetIndex++)
    {
        const Fx = FacetNormals[FacetIndex * 3], Fy = FacetNormals[FacetIndex * 3 + 1], Fz = FacetNormals[FacetIndex * 3 + 2];
        const FacetLength = Math.hypot(Fx, Fy, Fz) || 1.0;
        for (let Corner = 0; Corner < 3; Corner++)
        {
            let Sx = 0.0, Sy = 0.0, Sz = 0.0;
            for (const Adjacent of Incidence[Facets[FacetIndex * 3 + Corner]])
            {
                const Ax = FacetNormals[Adjacent * 3], Ay = FacetNormals[Adjacent * 3 + 1], Az = FacetNormals[Adjacent * 3 + 2];
                const AdjacentLength = Math.hypot(Ax, Ay, Az) || 1.0;
                if ((Ax * Fx + Ay * Fy + Az * Fz) / (AdjacentLength * FacetLength) < BodyCreaseCosine) continue;
                Sx += Ax; Sy += Ay; Sz += Az;
            }
            const SumLength = Math.hypot(Sx, Sy, Sz) || 1.0;
            CornerNormals[FacetIndex * 9 + Corner * 3]     = Sx / SumLength;
            CornerNormals[FacetIndex * 9 + Corner * 3 + 1] = Sy / SumLength;
            CornerNormals[FacetIndex * 9 + Corner * 3 + 2] = Sz / SumLength;
        }
    }
    return CornerNormals;
}


//------------------------------------------------------------------------------------------------------------------------
//                                                    ASSEMBLY
//------------------------------------------------------------------------------------------------------------------------

function TransformToStudio(X, YLeft, ZUp)
{
    return [X, ZUp - VehicleHeightRebase + VehicleGroundLift, -YLeft];
}

// 📝 Appends one archive instance as world-space triangles. Mirroring the lateral axis flips handedness, so corners B and
//    C (positions and normals) swap to keep counter-clockwise winding seen from outside.
function AppendArchiveInstance(Target, Archive, CornerNormals, Mount, MirrorLateral, Scale, ResolveSlot)
{
    const { FacetCount, Positions, Facets, Families } = Archive;
    const CornerOrder = MirrorLateral ? [0, 2, 1] : [0, 1, 2];
    const LateralSign = MirrorLateral ? -1.0 : 1.0;

    for (let FacetIndex = 0; FacetIndex < FacetCount; FacetIndex++)
    {
        for (const Corner of CornerOrder)
        {
            const VertexOffset = Facets[FacetIndex * 3 + Corner] * 3;
            const Studio = TransformToStudio(
                Positions[VertexOffset] * Scale + Mount[0],
                Positions[VertexOffset + 1] * Scale * LateralSign + Mount[1],
                Positions[VertexOffset + 2] * Scale + Mount[2]);
            Target.Positions.push(Studio[0], Studio[1], Studio[2]);

            const NormalOffset = FacetIndex * 9 + Corner * 3;
            Target.Normals.push(CornerNormals[NormalOffset], CornerNormals[NormalOffset + 2], -CornerNormals[NormalOffset + 1] * LateralSign);
        }
        Target.Slots.push(ResolveSlot(Families[FacetIndex]));
    }
}

// 📝 Rim faces carry family 9 (authored MatTyre) for the spoke body and 10 for the hub accent. The studio separates the
//    spoke body into its own satin-metal slot so the wheel reads as alloy against the rubber.
export function AssembleVehicle(BodyBytes, RimBytes, TyreBytes)
{
    const Body = DecodeFacetArchive(BodyBytes);
    const Rim  = DecodeFacetArchive(RimBytes);
    const Tyre = DecodeFacetArchive(TyreBytes);

    const Assembly = { Positions: [], Normals: [], Slots: [] };
    // 📝 On the body archive family 9 is the cabin canopy (roof and pillars), not a tyre: same authored dark albedo,
    //    given a clear-coated canopy slot instead of rubber roughness.
    const BodySlot = Family => (Family === 9 ? 'CanopyGloss' : (VehicleFamilySlot[Family] || 'VehicleTrim'));
    const RimSlot  = Family => (Family === 10 ? 'HubAccent' : 'RimAlloy');
    const TyreSlot = () => 'TyreRubber';

    AppendArchiveInstance(Assembly, Body, DeriveCreasedCornerNormals(Body), [0, 0, 0], false, 1.0, BodySlot);
    for (const AxleMount of AxleMounts)
    {
        AppendArchiveInstance(Assembly, Tyre, Tyre.CornerNormals, AxleMount.Mount, AxleMount.MirrorLateral, 1.0, TyreSlot);
        AppendArchiveInstance(Assembly, Rim, Rim.CornerNormals, AxleMount.Mount, AxleMount.MirrorLateral, RimToHubScale, RimSlot);
    }

    return {
        TriangleCount: Assembly.Slots.length,
        Positions:     new Float32Array(Assembly.Positions),
        Normals:       new Float32Array(Assembly.Normals),
        Slots:         Assembly.Slots,
        ArchiveFacets: { Body: Body.FacetCount, Rim: Rim.FacetCount, Tyre: Tyre.FacetCount }
    };
}

export async function RetrieveVehicleArchives(AssetRoot)
{
    const Names = ['Body', 'Rim', 'Tyre'];
    const Buffers = await Promise.all(Names.map(async Name =>
    {
        const Response = await fetch(new URL(`ControlVehicle${Name}.facet`, AssetRoot));
        if (!Response.ok) throw new Error(`ControlVehicle${Name}.facet: HTTP ${Response.status}`);
        return Response.arrayBuffer();
    }));
    return AssembleVehicle(Buffers[0], Buffers[1], Buffers[2]);
}
