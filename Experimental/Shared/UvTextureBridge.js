/*==========================================================================================================================================
                                                          UVTEXTUREBRIDGE.JS
==========================================================================================================================================*/
// Same-origin cross-studio transfer bridge connecting Frontier UV Studio and Frontier Texture Studio via IndexedDB
// ("FrontierStudioBridge") with an in-memory fallback for headless Node validation. Preserves exact UV seams, normals,
// and per-part surfacePart segmentation across arbitrarily large meshes (e.g. 25,068-tri Controlled Integrator).

const DB_NAME = 'FrontierStudioBridge';
const DB_VERSION = 1;
const STORE_NAME = 'transfers';
const META_KEY = 'frontier-uv-bridge-meta';
const MEMORY_FALLBACK = new Map();

function OpenBridgeDatabase()
{
    if (typeof indexedDB === 'undefined') return Promise.resolve(null);
    return new Promise((Resolve, Reject) =>
    {
        try
        {
            const Req = indexedDB.open(DB_NAME, DB_VERSION);
            Req.onupgradeneeded = () =>
            {
                const Db = Req.result;
                if (!Db.objectStoreNames.contains(STORE_NAME)) Db.createObjectStore(STORE_NAME);
            };
            Req.onsuccess = () => Resolve(Req.result);
            Req.onerror = () => Reject(Req.error || new Error('IndexedDB open failed'));
        }
        catch (Err)
        {
            Reject(Err);
        }
    });
}

export function SerializeTopologySurfaceForBridge(Surface, { udimTiles = null, importanceGroups = null } = {})
{
    if (!Surface?.faces?.length || !Surface?.positions?.length)
    {
        throw new Error('Cannot serialize empty TopologySurface');
    }

    const PartIndexByName = new Map();
    const PartNames = [];
    const ResolvePartIndex = (RawName) =>
    {
        const Clean = (RawName && String(RawName).trim()) || 'Surface';
        let Idx = PartIndexByName.get(Clean);
        if (Idx === undefined)
        {
            Idx = PartNames.length;
            PartNames.push(Clean);
            PartIndexByName.set(Clean, Idx);
        }
        return Idx;
    };

    // Count triangulated faces first for exact typed array allocation
    let TriCount = 0;
    for (const Face of Surface.faces)
    {
        if (Face.corners.length >= 3) TriCount += Face.corners.length - 2;
    }

    const Positions = new Float32Array(TriCount * 9);
    const Normals = new Float32Array(TriCount * 9);
    const Uvs = new Float32Array(TriCount * 6);
    const SurfacePart = new Float32Array(TriCount * 3);

    // Build smooth vertex-crease normals (52-deg threshold) matching UVViewport3D
    const CosCrease = Math.cos((52 * Math.PI) / 180);
    if (!Surface.faceNormals?.length && typeof Surface.RecomputeNormals === 'function')
    {
        Surface.RecomputeNormals();
    }
    const VertFaces = new Map();
    for (let Fi = 0; Fi < Surface.faces.length; Fi++)
    {
        for (const Corner of Surface.faces[Fi].corners)
        {
            let List = VertFaces.get(Corner.v);
            if (!List) { List = []; VertFaces.set(Corner.v, List); }
            List.push(Fi);
        }
    }

    let MinX = Infinity, MinY = Infinity, MinZ = Infinity;
    let MaxX = -Infinity, MaxY = -Infinity, MaxZ = -Infinity;
    for (const P of Surface.positions)
    {
        if (P[0] < MinX) MinX = P[0]; if (P[0] > MaxX) MaxX = P[0];
        if (P[1] < MinY) MinY = P[1]; if (P[1] > MaxY) MaxY = P[1];
        if (P[2] < MinZ) MinZ = P[2]; if (P[2] > MaxZ) MaxZ = P[2];
    }

    let Po = 0, No = 0, Uo = 0, So = 0;
    for (let Fi = 0; Fi < Surface.faces.length; Fi++)
    {
        const Face = Surface.faces[Fi];
        const Corners = Face.corners;
        if (Corners.length < 3) continue;

        const Fn = Surface.faceNormals?.[Fi] || [0, 1, 0];
        const PartName = Face.partId || Face.materialName || Face.name || 'Surface';
        const PartIdx = ResolvePartIndex(PartName);

        // Compute smooth corner normals respecting sharp creases
        const CornerN = Corners.map(C =>
        {
            const Adj = VertFaces.get(C.v) || [Fi];
            let Sx = 0, Sy = 0, Sz = 0;
            for (const Afi of Adj)
            {
                const An = Surface.faceNormals?.[Afi] || Fn;
                if (Fn[0] * An[0] + Fn[1] * An[1] + Fn[2] * An[2] >= CosCrease)
                {
                    Sx += An[0]; Sy += An[1]; Sz += An[2];
                }
            }
            const L = Math.hypot(Sx, Sy, Sz) || 1;
            return [Sx / L, Sy / L, Sz / L];
        });

        for (let k = 1; k < Corners.length - 1; k++)
        {
            const TriIdx = [0, k, k + 1];
            for (const Ci of TriIdx)
            {
                const Corner = Corners[Ci];
                const P = Surface.positions[Corner.v] || [0, 0, 0];
                const Uv = Surface.uvCoords?.[Corner.uv] || [0, 0];
                const N = CornerN[Ci];

                Positions[Po++] = P[0]; Positions[Po++] = P[1]; Positions[Po++] = P[2];
                Normals[No++]   = N[0]; Normals[No++]   = N[1]; Normals[No++]   = N[2];
                Uvs[Uo++]       = Uv[0]; Uvs[Uo++]      = Uv[1];
                SurfacePart[So++] = PartIdx;
            }
        }
    }

    const Center = [0.5 * (MinX + MaxX), 0.5 * (MinY + MaxY), 0.5 * (MinZ + MaxZ)];
    const Radius = Math.max(0.25, 0.5 * Math.hypot(MaxX - MinX, MaxY - MinY, MaxZ - MinZ));

    return {
        version: 1,
        source: 'uv-studio',
        name: Surface.name || 'UV Mesh',
        triCount: TriCount,
        partNames: PartNames,
        positions: Positions,
        normals: Normals,
        uvs: Uvs,
        surfacePart: SurfacePart,
        bounds: { min: [MinX, MinY, MinZ], max: [MaxX, MaxY, MaxZ], center: Center, radius: Radius },
        udimTiles: Array.isArray(udimTiles) ? udimTiles.map(T => ({ ...T })) : [{ id: 1001, label: 'Tile 1001', resolution: 2048 }],
        importanceGroups: Array.isArray(importanceGroups) ? importanceGroups.map(G => ({ ...G })) : [],
        timestamp: Date.now()
    };
}

export function BuildThreeGeometryFromBridgeTransfer(Transfer, THREE)
{
    if (!Transfer?.positions || !Transfer?.uvs) throw new Error('Invalid UV bridge transfer payload');
    const Geo = new THREE.BufferGeometry();
    Geo.setAttribute('position', new THREE.BufferAttribute(new Float32Array(Transfer.positions), 3));
    if (Transfer.normals?.length === Transfer.positions.length)
    {
        Geo.setAttribute('normal', new THREE.BufferAttribute(new Float32Array(Transfer.normals), 3));
    }
    else
    {
        Geo.computeVertexNormals();
    }
    Geo.setAttribute('uv', new THREE.BufferAttribute(new Float32Array(Transfer.uvs), 2));
    if (Transfer.surfacePart?.length)
    {
        Geo.setAttribute('surfacePart', new THREE.BufferAttribute(new Float32Array(Transfer.surfacePart), 1));
    }
    Geo.userData.partNames = Array.isArray(Transfer.partNames) && Transfer.partNames.length
        ? [...Transfer.partNames]
        : ['Imported Mesh'];
    Geo.userData.uvBridgeName = Transfer.name || 'UV Studio Mesh';
    Geo.computeBoundingBox();
    Geo.computeBoundingSphere();
    return Geo;
}

export async function SaveUvBridgeTransfer(Transfer, Key = 'latest')
{
    MEMORY_FALLBACK.set(Key, Transfer);
    try
    {
        if (typeof localStorage !== 'undefined')
        {
            localStorage.setItem(META_KEY, JSON.stringify({
                key: Key,
                name: Transfer.name,
                triCount: Transfer.triCount,
                partCount: Transfer.partNames?.length || 1,
                timestamp: Transfer.timestamp
            }));
        }
    }
    catch {}

    const Db = await OpenBridgeDatabase();
    if (!Db) return true;
    return new Promise((Resolve, Reject) =>
    {
        const Tx = Db.transaction(STORE_NAME, 'readwrite');
        Tx.objectStore(STORE_NAME).put(Transfer, Key);
        Tx.oncomplete = () => { Db.close(); Resolve(true); };
        Tx.onerror = () => { Db.close(); Reject(Tx.error || new Error('IndexedDB write failed')); };
    });
}

export async function LoadUvBridgeTransfer(Key = 'latest')
{
    const Db = await OpenBridgeDatabase().catch(() => null);
    if (Db)
    {
        const Found = await new Promise((Resolve) =>
        {
            try
            {
                const Tx = Db.transaction(STORE_NAME, 'readonly');
                const Req = Tx.objectStore(STORE_NAME).get(Key);
                Req.onsuccess = () => { Db.close(); Resolve(Req.result || null); };
                Req.onerror = () => { Db.close(); Resolve(null); };
            }
            catch
            {
                Db.close();
                Resolve(null);
            }
        });
        if (Found) return Found;
    }
    return MEMORY_FALLBACK.get(Key) || null;
}

export function ReadUvBridgeMeta()
{
    try
    {
        if (typeof localStorage === 'undefined') return null;
        const Raw = localStorage.getItem(META_KEY);
        return Raw ? JSON.parse(Raw) : null;
    }
    catch
    {
        return null;
    }
}
