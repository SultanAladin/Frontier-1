//============================================================================================================================================
//                                                          LIGHTCLUSTERSPACE.JS
//============================================================================================================================================
// 🧩 Light records binned into a world-space grid: each cell's 32-bit mask names the lights whose reach sphere touches the cell.

import { MaxSceneLights, LightRecordFloats } from "./LightingSystem.js";
import { CourseConstants } from "./CourseStructure.js";

// 📝 The grid covers the asphalt pad the lights stand on (CourseStructure's pad). Points outside it take every light
//    (LightClusterSpace.wgsl), so a light is never skipped; the cost is only the lost culling there.
const PadMinimum = CourseConstants.PadMinimum;
const PadMaximum = CourseConstants.PadMaximum;
export const LightClusterBounds = Object.freeze({
    Minimum:    Object.freeze([PadMinimum[0], PadMinimum[1], -2.0]),                                 // [m]
    Extent:     Object.freeze([PadMaximum[0] - PadMinimum[0], PadMaximum[1] - PadMinimum[1], 30.0]), // [m]
    Resolution: Object.freeze([16, 16, 4])                                                           // cells along x, y, z
});

const CellCount    = LightClusterBounds.Resolution[0] * LightClusterBounds.Resolution[1] * LightClusterBounds.Resolution[2];
const HeaderFloats = 8;                                // Minimum and Extent, each a vec4f
const LightFloats  = MaxSceneLights * LightRecordFloats;
export const LightClusterBytes = (HeaderFloats + LightFloats + CellCount) * 4;

// 📝 Layout of LightClusterRecords (LightClusterSpace.wgsl): Minimum, Extent, Lights[32], CellMasks[1024], as one storage buffer.
export class LightClusterSpace
{
    constructor(Exchange)
    {
        this.Exchange = Exchange;
        this.Bytes  = new ArrayBuffer(LightClusterBytes);
        this.Floats = new Float32Array(this.Bytes);
        this.Masks  = new Uint32Array(this.Bytes, (HeaderFloats + LightFloats) * 4, CellCount);
        const [X0, Y0, Z0] = LightClusterBounds.Minimum;
        const [SizeX, SizeY, SizeZ] = LightClusterBounds.Extent;
        this.Floats.set([X0, Y0, Z0, 0.0, SizeX, SizeY, SizeZ, 0.0], 0);
        this.Records = Exchange.ConstructRecords(LightClusterBytes, GPUBufferUsage.STORAGE | GPUBufferUsage.COPY_DST, "Light grid records");
    }

    // 📝 LightData: the packed light records of LightingSystem.BufferData. Each active light lands in every cell whose box lies
    //    within its reach; the test is conservative (the sphere-to-box distance), so no contributing light is dropped.
    Update(Device, LightData)
    {
        this.Floats.set(LightData.subarray(0, LightFloats), HeaderFloats);
        const [Minimum, Extent, Resolution] = [LightClusterBounds.Minimum, LightClusterBounds.Extent, LightClusterBounds.Resolution];
        const CellSize = Extent.map((Size, Axis) => Size / Resolution[Axis]);
        const Lights = [];
        for (let Index = 0; Index < MaxSceneLights; ++Index)
        {
            const Offset = Index * LightRecordFloats;
            if (LightData[Offset + 15] < 0.5)   // Params.w: active
                continue;
            Lights.push({ Index, Centre: [LightData[Offset], LightData[Offset + 1], LightData[Offset + 2]], Reach: LightData[Offset + 7] });
        }
        for (let Z = 0; Z < Resolution[2]; ++Z)
        for (let Y = 0; Y < Resolution[1]; ++Y)
        for (let X = 0; X < Resolution[0]; ++X)
        {
            const Cell = [X, Y, Z];
            const Low  = Cell.map((Index, Axis) => Minimum[Axis] + Index * CellSize[Axis]);
            const High = Low.map((Corner, Axis) => Corner + CellSize[Axis]);
            let Mask = 0;
            for (const Light of Lights)
            {
                let Distance2 = 0.0;
                for (let Axis = 0; Axis < 3; ++Axis)
                {
                    const Nearest = Math.min(Math.max(Light.Centre[Axis], Low[Axis]), High[Axis]);
                    Distance2 += (Light.Centre[Axis] - Nearest) ** 2;
                }
                if (Distance2 < Light.Reach * Light.Reach)
                    Mask |= 1 << Light.Index;
            }
            this.Masks[X + Resolution[0] * (Y + Resolution[1] * Z)] = Mask >>> 0;
        }
        Device.queue.writeBuffer(this.Records, 0, this.Bytes);
    }
}
