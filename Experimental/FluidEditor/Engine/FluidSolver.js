//============================================================================================================================================
//                                                          FLUIDSOLVER.JS
//             GPU Eulerian Gas Solver Orchestration — Tiled 3D Half-Float Fields, Pass Sequencing (Inject → Advect →
//             Thermodynamics → Forces → Vorticity Confinement → Pressure Projection), Force / Emitter / Collider
//                                  Uniform Packing, and Flow Tracer Particle Integration
//============================================================================================================================================

import { computeVolumeLayout } from './GpuContext.js';
import * as Shaders from './FluidShaders.js';
import { ForceTypeById, FalloffCurves, EmitterShapes, BoundaryModes, MaximumForceGenerators, MaximumEmitters, MaximumColliders, DebrisKinds, emitterTransform } from './ForceGeneratorCatalog.js';
import { nodeWeight, keyedNode } from './KeyTracks.js';

export class FluidSolver
{
    constructor(context)
    {
        this.context = context;
        this.gl = context.gl;
        this.time = 0;
        this.frame = 0;
        this.layout = null;
        this.fields = null;
        this.tracerSize = [128, 64];
        this.gridCentre = [0, 0, 0];
        this.cellOrigin = [0, 0, 0];
        this.relocations = 0;
        this.tracers = null;
        this.statistics = { stepMilliseconds: 0, passes: 0 };
        this.forceArrays = {
            a: new Float32Array(MaximumForceGenerators * 4),
            b: new Float32Array(MaximumForceGenerators * 4),
            c: new Float32Array(MaximumForceGenerators * 4),
            d: new Float32Array(MaximumForceGenerators * 4),
            count: 0,
        };
        this.emitterArrays = {
            a: new Float32Array(MaximumEmitters * 4),
            b: new Float32Array(MaximumEmitters * 4),
            c: new Float32Array(MaximumEmitters * 4),
            d: new Float32Array(MaximumEmitters * 4),
            e: new Float32Array(MaximumEmitters * 4),
            count: 0,
        };
        this.colliderArray = { data: new Float32Array(MaximumColliders * 4), count: 0 };
        this.compilePrograms();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PROGRAMS + FIELDS
    //------------------------------------------------------------------------------------------------------------------------

    compilePrograms()
    {
        this.programSets = {};
        this.sparse = false;
    }

    /** Kernels are compiled per addressing mode on first use: fixed window (VolumeChunk) or sparse tiles (SparseVolumeChunk). */
    get programs()
    {
        const key = this.sparse ? 'sparse' : 'fixed';
        if (!this.programSets[key]) this.programSets[key] = this.buildPrograms(this.sparse);
        return this.programSets[key];
    }

    buildPrograms(sparseMode)
    {
        const transform = sparseMode ? Shaders.sparse : (source) => source;
        const create = (label, fragment) => this.context.createProgram(label + (sparseMode ? '.sparse' : ''), Shaders.FullscreenVertex, transform(fragment));
        const programs = {
            advect: create('Advect', Shaders.AdvectFragment),
            macCormack: create('MacCormack', Shaders.MacCormackFragment),
            inject: create('Inject', Shaders.InjectFragment),
            forces: create('Forces', Shaders.ForcesFragment),
            curl: create('Curl', Shaders.CurlFragment),
            vorticity: create('Vorticity', Shaders.VorticityFragment),
            divergence: create('Divergence', Shaders.DivergenceFragment),
            pressure: create('Pressure', Shaders.PressureFragment),
            gradient: create('Gradient', Shaders.GradientFragment),
            thermodynamics: create('Thermodynamics', Shaders.ThermodynamicsFragment),
            tracerUpdate: create('TracerUpdate', Shaders.TracerUpdateFragment),
            debrisUpdate: create('DebrisUpdate', Shaders.DebrisUpdateFragment),
            shift: create('Shift', Shaders.ShiftFragment),
        };
        if (sparseMode) programs.occupancy = this.context.createProgram('Occupancy', Shaders.FullscreenVertex, Shaders.OccupancyFragment);
        return programs;
    }

    configure(resolution, cellSize, centre = [0, 0, 0])
    {
        const [width, height, depth] = resolution.map((value) => Math.max(8, Math.min(160, Math.round(value))));
        const layout = computeVolumeLayout(width, height, depth);
        const unchanged = this.layout && this.layout.width === width && this.layout.height === height && this.layout.depth === depth;
        this.cellSize = cellSize;
        this.gridCentre = [centre[0], centre[1], centre[2]];
        this.cellOrigin = [-Math.floor(width / 2), -Math.floor(height / 2), -Math.floor(depth / 2)];
        if (this.sparse) { this.destroyFields(); this.sparse = false; this.tilePool = null; }
        if (unchanged && this.fields) return;
        this.destroyFields();
        this.layout = layout;
        const context = this.context;
        const w = layout.textureWidth, h = layout.textureHeight;
        this.fields = {
            velocity: context.createPingPong(w, h),
            state: context.createPingPong(w, h),
            pressure: context.createPingPong(w, h),
            divergence: context.createTarget(w, h),
            curl: context.createTarget(w, h),
            scratchA: context.createTarget(w, h),
            scratchB: context.createTarget(w, h),
        };
        this.tracers = context.createPingPong(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.tracerPrevious = context.createTarget(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.debrisSize = [64, 64];
        this.debrisPositions = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisVelocities = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisPrevious = context.createTarget(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.reset();
    }

    destroyFields()
    {
        if (!this.fields) return;
        const context = this.context;
        context.destroyPingPong(this.fields.velocity);
        context.destroyPingPong(this.fields.state);
        context.destroyPingPong(this.fields.pressure);
        context.destroyTarget(this.fields.divergence);
        context.destroyTarget(this.fields.curl);
        context.destroyTarget(this.fields.scratchA);
        context.destroyTarget(this.fields.scratchB);
        context.destroyPingPong(this.tracers);
        context.destroyTarget(this.tracerPrevious);
        context.destroyPingPong(this.debrisPositions);
        context.destroyPingPong(this.debrisVelocities);
        context.destroyTarget(this.debrisPrevious);
        if (this.tilePool)
        {
            this.gl.deleteTexture(this.tilePool.lookup.texture);
            this.gl.deleteTexture(this.tilePool.slotTable.texture);
            context.destroyTarget(this.tilePool.occupancy);
        }
        this.fields = null;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INFINITE (SPARSE TILE) GRID
    //------------------------------------------------------------------------------------------------------------------------

    /**
     * Unbounded world: T³ tiles are allocated on demand out of a pool of `maxTiles` atlas slots. Tiles containing an
     * emitter are always resident; the scheduler reads a per-slot occupancy summary back every few frames, allocates the
     * neighbour across any face gas is pressing on, and frees tiles that have gone empty — so the gas is never clipped
     * and the only limit is the pool size (not a fixed box).
     */
    configureInfinite(tileCells, cellSize, maxTiles)
    {
        const T = Math.max(8, Math.min(48, Math.round(tileCells)));
        const slots = Math.max(4, Math.min(256, Math.round(maxTiles)));
        const sliceCols = Math.ceil(Math.sqrt(T)), sliceRows = Math.ceil(T / sliceCols);
        const slotsX = Math.ceil(Math.sqrt(slots)), slotsY = Math.ceil(slots / slotsX);
        const blockW = T * sliceCols, blockH = T * sliceRows;
        const unchanged = this.sparse && this.tilePool && this.tilePool.cells === T && this.tilePool.slots === slots;
        this.cellSize = cellSize;
        if (unchanged) { this.tilePool.size = T * cellSize; this.updateBounds(); return; }
        this.destroyFields();
        this.sparse = true;
        const context = this.context;
        const w = slotsX * blockW, h = slotsY * blockH;
        this.tilePool = {
            cells: T, size: T * cellSize, slots, slotsX, slotsY, sliceCols, sliceRows, blockW, blockH,
            entries: new Array(slots).fill(null),
            map: new Map(),
            lookupOrigin: [-32, 0, -32],
            lookupData: new Int16Array(64 * 1024).fill(-1),
            slotData: new Int32Array(slots * 4),
            lookup: context.createIntegerTexture(64, 1024, 1),
            slotTable: context.createIntegerTexture(slots, 1, 4),
            occupancy: context.createTarget(3, slots, { rgba32f: true, nearest: true }),
            readback: new Float32Array(3 * slots * 4),
            dirty: true, interval: 3, band: Math.max(3, Math.round(T * 0.25)), stride: T >= 32 ? 2 : 1,
            allocations: 0, frees: 0,
        };
        this.layout = { width: T, height: T, depth: T, tilesX: sliceCols, tilesY: sliceRows, textureWidth: w, textureHeight: h, cellCount: 0 };
        this.fields = {
            velocity: context.createPingPong(w, h),
            state: context.createPingPong(w, h),
            pressure: context.createPingPong(w, h),
            divergence: context.createTarget(w, h),
            curl: context.createTarget(w, h),
            scratchA: context.createTarget(w, h),
            scratchB: context.createTarget(w, h),
        };
        this.tracers = context.createPingPong(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.tracerPrevious = context.createTarget(this.tracerSize[0], this.tracerSize[1], { rgba32f: true, nearest: true });
        this.debrisSize = [64, 64];
        this.debrisPositions = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisVelocities = context.createPingPong(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.debrisPrevious = context.createTarget(this.debrisSize[0], this.debrisSize[1], { rgba32f: true, nearest: true });
        this.reset();
    }

    get residentTiles()
    {
        return this.tilePool ? this.tilePool.entries.filter(Boolean) : [];
    }

    tileKey(tile) { return `${tile[0]},${tile[1]},${tile[2]}`; }

    tileOfWorld(position)
    {
        const size = this.tilePool.size;
        return [Math.floor(position[0] / size), Math.floor(position[1] / size), Math.floor(position[2] / size)];
    }

    tileInLookup(tile)
    {
        const o = this.tilePool.lookupOrigin;
        return tile[0] >= o[0] && tile[0] < o[0] + 64 && tile[1] >= o[1] && tile[1] < o[1] + 16 && tile[2] >= o[2] && tile[2] < o[2] + 64;
    }

    allocateTile(tile, pinned, mean = 0)
    {
        const pool = this.tilePool;
        if (tile[1] < 0 || !this.tileInLookup(tile)) return -1;
        const key = this.tileKey(tile);
        const existing = pool.map.get(key);
        if (existing !== undefined) { pool.entries[existing].pinned = pool.entries[existing].pinned || pinned; return existing; }
        let slot = pool.entries.findIndex((entry) => entry === null);
        if (slot < 0)
        {
            // Pool exhausted: evict the thinnest unpinned tile if it carries less gas than the one asking.
            let weakest = -1, weakestMean = Math.max(mean, 0.01);
            pool.entries.forEach((entry, index) => { if (entry && !entry.pinned && entry.mean < weakestMean) { weakest = index; weakestMean = entry.mean; } });
            if (weakest < 0) return -1;
            this.freeTile(weakest);
            slot = weakest;
        }
        pool.entries[slot] = { tile: [tile[0], tile[1], tile[2]], pinned, since: this.frame, mean: 0, peak: 0 };
        pool.map.set(key, slot);
        const x = (slot % pool.slotsX) * pool.blockW, y = Math.floor(slot / pool.slotsX) * pool.blockH;
        for (const pair of [this.fields.velocity, this.fields.state, this.fields.pressure])
        {
            this.context.clearRegion(pair.read, x, y, pool.blockW, pool.blockH);
            this.context.clearRegion(pair.write, x, y, pool.blockW, pool.blockH);
        }
        pool.allocations += 1;
        pool.dirty = true;
        return slot;
    }

    freeTile(slot)
    {
        const pool = this.tilePool;
        const entry = pool.entries[slot];
        if (!entry) return;
        pool.map.delete(this.tileKey(entry.tile));
        pool.entries[slot] = null;
        pool.frees += 1;
        pool.dirty = true;
    }

    /** Emitter tiles are pinned every frame; occupancy drives growth / shrink every `interval` frames. */
    scheduleTiles(scene)
    {
        const pool = this.tilePool;
        const gl = this.gl, context = this.context;
        for (const entry of pool.entries) if (entry) entry.pinned = false;
        const required = [];
        for (const sourceEmitter of scene.Emitters)
        {
            // Keyed fields (phase 4) replace the static ones at the solver clock; Weight comes from the node's Weight track.
            const emitter = keyedNode(sourceEmitter, this.time);
            if (!emitter.Enabled || nodeWeight(sourceEmitter, this.time) <= 0) continue;
            const { Position: position } = emitterTransform(emitter, this.time);
            const reach = emitter.Radius + pool.size * 0.15;
            const lo = this.tileOfWorld([position[0] - reach, position[1] - reach, position[2] - reach]);
            const hi = this.tileOfWorld([position[0] + reach, position[1] + reach, position[2] + reach]);
            for (let x = lo[0]; x <= hi[0]; x += 1) for (let y = Math.max(0, lo[1]); y <= hi[1]; y += 1) for (let z = lo[2]; z <= hi[2]; z += 1) required.push([x, y, z]);
        }
        // Keep the lookup window centred on the emitters (or the resident gas) so the lattice is effectively endless.
        const focusSource = required.length ? required : this.residentTiles.map((entry) => entry.tile);
        if (focusSource.length)
        {
            const focus = [0, 1, 2].map((axis) => Math.round(focusSource.reduce((sum, tile) => sum + tile[axis], 0) / focusSource.length));
            const o = pool.lookupOrigin;
            if (Math.abs(focus[0] - (o[0] + 32)) > 16 || Math.abs(focus[2] - (o[2] + 32)) > 16 || focus[1] - o[1] > 12)
            {
                pool.lookupOrigin = [focus[0] - 32, Math.max(0, focus[1] - 4), focus[2] - 32];
                pool.entries.forEach((entry, slot) => { if (entry && !this.tileInLookup(entry.tile)) this.freeTile(slot); });
                pool.dirty = true;
            }
        }
        for (const tile of required) this.allocateTile(tile, true);

        if (this.frame % pool.interval === 0 && this.residentTiles.length > 0)
        {
            // Occupancy summary → readback → grow across pressing faces, free what has emptied.
            const uniforms = context.use(this.programs.occupancy);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uState', this.fields.state.read.texture);
            context.bindTexture('uVelocity', this.fields.velocity.read.texture);
            gl.uniform1i(uniforms.uStride, pool.stride);
            gl.uniform1i(uniforms.uBand, pool.band);
            context.dispatch(pool.occupancy);
            this.statistics.passes += 1;
            const data = context.readTarget(pool.occupancy, pool.readback);
            const perAxis = Math.ceil(pool.cells / pool.stride);
            const samples = perAxis * perAxis * perAxis;
            const bandSamples = Math.max(1, Math.ceil(pool.band / pool.stride)) * perAxis * perAxis;
            const grow = [];
            pool.entries.forEach((entry, slot) => {
                if (!entry) return;
                const base = slot * 12;
                entry.mean = data[base + 0] / samples;
                entry.peak = data[base + 1];
                const faces = [data[base + 4], data[base + 5], data[base + 6], data[base + 7], data[base + 8], data[base + 9]].map((sum) => sum / bandSamples);
                const directions = [[-1, 0, 0], [1, 0, 0], [0, -1, 0], [0, 1, 0], [0, 0, -1], [0, 0, 1]];
                faces.forEach((mean, face) => { if (mean > 0.0025) grow.push({ tile: [entry.tile[0] + directions[face][0], entry.tile[1] + directions[face][1], entry.tile[2] + directions[face][2]], mean }); });
                if (!entry.pinned && entry.mean < 0.0015 && entry.peak < 0.02 && this.frame - entry.since > 90) this.freeTile(slot);
            });
            grow.sort((a, b) => b.mean - a.mean);
            for (const request of grow) if (!pool.map.has(this.tileKey(request.tile))) this.allocateTile(request.tile, false, request.mean);
        }
        if (pool.dirty) this.uploadTileTables();
    }

    uploadTileTables()
    {
        const pool = this.tilePool;
        pool.lookupData.fill(-1);
        pool.slotData.fill(0);
        const o = pool.lookupOrigin;
        pool.entries.forEach((entry, slot) => {
            if (!entry) return;
            const r = [entry.tile[0] - o[0], entry.tile[1] - o[1], entry.tile[2] - o[2]];
            pool.lookupData[r[0] + (r[2] + 64 * r[1]) * 64] = slot;
            pool.slotData[slot * 4] = entry.tile[0]; pool.slotData[slot * 4 + 1] = entry.tile[1]; pool.slotData[slot * 4 + 2] = entry.tile[2]; pool.slotData[slot * 4 + 3] = 1;
        });
        this.context.uploadIntegerTexture(pool.lookup, pool.lookupData);
        this.context.uploadIntegerTexture(pool.slotTable, pool.slotData);
        pool.dirty = false;
        this.updateBounds();
    }

    /** Bounding box of resident tiles → uGrid / uCellOrigin / uGridCentre (an empty pool frames tile 0). */
    updateBounds()
    {
        const pool = this.tilePool;
        const resident = this.residentTiles;
        const T = pool.cells;
        let lo = [0, 0, 0], hi = [0, 0, 0];
        if (resident.length)
        {
            lo = [Infinity, Infinity, Infinity]; hi = [-Infinity, -Infinity, -Infinity];
            for (const { tile } of resident) for (let axis = 0; axis < 3; axis += 1) { lo[axis] = Math.min(lo[axis], tile[axis]); hi[axis] = Math.max(hi[axis], tile[axis]); }
        }
        this.cellOrigin = lo.map((value) => value * T);
        const extent = hi.map((value, axis) => (value - lo[axis] + 1) * T);
        this.layout = { ...this.layout, width: extent[0], height: extent[1], depth: extent[2], cellCount: resident.length * T * T * T, residentTiles: resident.length, poolSize: pool.slots };
        this.gridCentre = extent.map((cells, axis) => (this.cellOrigin[axis] + cells * 0.5) * this.cellSize);
    }

    /**
     * World Grid relocation: the simulated window has stepped `shiftCells` cells through world space. Every field is
     * copied across by that integer offset (an exact texel move — no resampling blur) so the gas keeps its world position
     * while the window re-centres; the new centre feeds every world↔cell mapping through uGridCentre.
     */
    relocate(shiftCells, centre)
    {
        if (this.sparse) return;
        this.gridCentre = [centre[0], centre[1], centre[2]];
        if (!this.fields || !shiftCells.some((value) => value !== 0)) return;
        const gl = this.gl, context = this.context, layout = this.layout;
        const whole = Math.abs(shiftCells[0]) >= layout.width || Math.abs(shiftCells[1]) >= layout.height || Math.abs(shiftCells[2]) >= layout.depth;
        const uniforms = context.use(this.programs.shift);
        this.applyVolumeUniforms(uniforms);
        gl.uniform3i(uniforms.uShift, shiftCells[0], shiftCells[1], shiftCells[2]);
        for (const pair of [this.fields.velocity, this.fields.state, this.fields.pressure])
        {
            if (whole) { context.clearTarget(pair.read); continue; }
            context.bindTexture('uSource', pair.read.texture);
            context.dispatch(pair.write);
            pair.swap();
            this.statistics.passes += 1;
        }
        this.relocations = (this.relocations || 0) + 1;
    }

    reset()
    {
        const context = this.context;
        for (const pair of [this.fields.velocity, this.fields.state, this.fields.pressure])
        {
            context.clearTarget(pair.read);
            context.clearTarget(pair.write);
        }
        context.clearTarget(this.fields.divergence);
        context.clearTarget(this.fields.curl);
        if (this.sparse)
        {
            const pool = this.tilePool;
            pool.entries.fill(null);
            pool.map.clear();
            pool.lookupOrigin = [-32, 0, -32];        // as configured: a reset run schedules tiles exactly like a fresh one
            pool.dirty = true;
            this.uploadTileTables();
        }
        this.time = 0;
        this.frame = 0;
        this.tracersNeedReset = true;
        this.debrisNeedReset = true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  UNIFORM PACKING
    //------------------------------------------------------------------------------------------------------------------------

    packForces(forces)
    {
        const arrays = this.forceArrays;
        let count = 0;
        for (const sourceForce of forces)
        {
            const force = keyedNode(sourceForce, this.time);
            if (!force.Enabled || count >= MaximumForceGenerators) continue;
            const type = ForceTypeById[force.Type];
            const envelope = nodeWeight(sourceForce, this.time);
            const offset = count * 4;
            arrays.a.set([force.Position[0], force.Position[1], force.Position[2], force.Radius], offset);
            arrays.b.set([force.Direction[0], force.Direction[1], force.Direction[2], force.Strength * envelope], offset);
            arrays.c.set([type.Code, Math.max(0, FalloffCurves.indexOf(force.Falloff)), force.Frequency, force.Secondary], offset);
            arrays.d.set([force.Tertiary || 0, 0, 0, 0], offset);
            count += 1;
        }
        arrays.count = count;
    }

    packEmitters(emitters)
    {
        const arrays = this.emitterArrays;
        let count = 0;
        for (const sourceEmitter of emitters)
        {
            const emitter = keyedNode(sourceEmitter, this.time);
            if (!emitter.Enabled || count >= MaximumEmitters) continue;
            const envelope = nodeWeight(sourceEmitter, this.time);
            if (envelope <= 0 && !(emitter.Debris > 0)) continue;
            const offset = count * 4;
            const transform = emitterTransform(emitter, this.time);
            arrays.a.set([transform.Position[0], transform.Position[1], transform.Position[2], emitter.Radius], offset);
            arrays.b.set([transform.Velocity[0], transform.Velocity[1], transform.Velocity[2], emitter.Density * envelope], offset);
            arrays.c.set([emitter.Temperature * envelope, emitter.Fuel * envelope, emitter.Jitter, Math.max(0, EmitterShapes.indexOf(emitter.Shape))], offset);
            arrays.d.set([emitter.Orientation[0], emitter.Orientation[1], emitter.Orientation[2], emitter.Pulse], offset);
            arrays.e.set([emitter.Debris || 0, emitter.DebrisSpeed || 0, envelope, Math.max(0, DebrisKinds.indexOf(emitter.DebrisKind))], offset);
            count += 1;
        }
        arrays.count = count;
    }

    packColliders(colliders)
    {
        let count = 0;
        for (const collider of colliders)
        {
            if (!collider.Enabled || count >= MaximumColliders) continue;
            this.colliderArray.data.set([collider.Position[0], collider.Position[1], collider.Position[2], collider.Radius], count * 4);
            count += 1;
        }
        this.colliderArray.count = count;
    }

    applyVolumeUniforms(uniforms)
    {
        const gl = this.gl;
        const layout = this.layout;
        if (uniforms.uGrid) gl.uniform3i(uniforms.uGrid, layout.width, layout.height, layout.depth);
        if (uniforms.uTiles) gl.uniform2i(uniforms.uTiles, layout.tilesX, layout.tilesY);
        if (uniforms.uTexSize) gl.uniform2f(uniforms.uTexSize, layout.textureWidth, layout.textureHeight);
        if (uniforms.uCellSize) gl.uniform1f(uniforms.uCellSize, this.cellSize);
        if (uniforms.uGridCentre) gl.uniform3f(uniforms.uGridCentre, this.gridCentre[0], this.gridCentre[1], this.gridCentre[2]);
        if (uniforms.uCellOrigin) gl.uniform3i(uniforms.uCellOrigin, this.cellOrigin[0], this.cellOrigin[1], this.cellOrigin[2]);
        if (this.sparse && uniforms.uTileCells !== undefined)
        {
            const pool = this.tilePool;
            gl.uniform1i(uniforms.uTileCells, pool.cells);
            gl.uniform1i(uniforms.uSlotsX, pool.slotsX);
            gl.uniform1i(uniforms.uSliceCols, pool.sliceCols);
            gl.uniform1i(uniforms.uSliceRows, pool.sliceRows);
            gl.uniform3i(uniforms.uLookupOrigin, pool.lookupOrigin[0], pool.lookupOrigin[1], pool.lookupOrigin[2]);
            this.context.bindTexture('uTileLookup', pool.lookup.texture);
            this.context.bindTexture('uSlotTable', pool.slotTable.texture);
        }
    }

    applyForceUniforms(uniforms)
    {
        const gl = this.gl;
        if (uniforms.uForceCount === undefined) return;
        gl.uniform1i(uniforms.uForceCount, this.forceArrays.count);
        gl.uniform4fv(uniforms.uForceA, this.forceArrays.a);
        gl.uniform4fv(uniforms.uForceB, this.forceArrays.b);
        gl.uniform4fv(uniforms.uForceC, this.forceArrays.c);
        gl.uniform4fv(uniforms.uForceD, this.forceArrays.d);
        gl.uniform1f(uniforms.uTime, this.time);
    }

    applyColliderUniforms(uniforms)
    {
        const gl = this.gl;
        if (uniforms.uColliderCount === undefined) return;
        gl.uniform1i(uniforms.uColliderCount, this.colliderArray.count);
        gl.uniform4fv(uniforms.uColliders, this.colliderArray.data);
    }

    applyBoundaryUniform(uniforms, domain)
    {
        if (uniforms.uBoundaryMode !== undefined) this.gl.uniform1i(uniforms.uBoundaryMode, this.sparse ? 3 : Math.max(0, BoundaryModes.indexOf(domain.Boundary)));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PASSES
    //------------------------------------------------------------------------------------------------------------------------

    advectField(pair, velocityTexture, deltaTime, dissipation, scheme)
    {
        const gl = this.gl, context = this.context, programs = this.programs, fields = this.fields;
        if (scheme === 'MacCormack')
        {
            // Forward trace into scratchA.
            let uniforms = context.use(programs.advect);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', pair.read.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uDissipation, dissipation);
            gl.uniform1f(uniforms.uDirection, 1.0);
            context.dispatch(fields.scratchA);
            // Reverse trace of the forward result into scratchB.
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', fields.scratchA.texture);
            gl.uniform1f(uniforms.uDirection, -1.0);
            gl.uniform1f(uniforms.uDissipation, 1.0);
            context.dispatch(fields.scratchB);
            // Error-compensated combine with monotone clamp.
            uniforms = context.use(programs.macCormack);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', velocityTexture);
            context.bindTexture('uSource', pair.read.texture);
            context.bindTexture('uForward', fields.scratchA.texture);
            context.bindTexture('uBackward', fields.scratchB.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uDissipation, dissipation);
            context.dispatch(pair.write);
            pair.swap();
            this.statistics.passes += 3;
            return;
        }
        const uniforms = context.use(programs.advect);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', velocityTexture);
        context.bindTexture('uSource', pair.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uDissipation, dissipation);
        gl.uniform1f(uniforms.uDirection, 1.0);
        context.dispatch(pair.write);
        pair.swap();
        this.statistics.passes += 1;
    }

    inject(mode, pair, deltaTime)
    {
        const gl = this.gl, context = this.context;
        const uniforms = context.use(this.programs.inject);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uTarget', pair.read.texture);
        gl.uniform1i(uniforms.uMode, mode);
        gl.uniform1i(uniforms.uEmitterCount, this.emitterArrays.count);
        gl.uniform4fv(uniforms.uEmitterA, this.emitterArrays.a);
        gl.uniform4fv(uniforms.uEmitterB, this.emitterArrays.b);
        gl.uniform4fv(uniforms.uEmitterC, this.emitterArrays.c);
        gl.uniform4fv(uniforms.uEmitterD, this.emitterArrays.d);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uTime, this.time);
        context.dispatch(pair.write);
        pair.swap();
        this.statistics.passes += 1;
    }

    step(scene, deltaTime)
    {
        if (!this.fields) return;
        const started = performance.now();
        const gl = this.gl, context = this.context, programs = this.programs, fields = this.fields;
        const domain = scene.Domain;
        this.statistics.passes = 0;
        if (this.sparse) this.scheduleTiles(scene);
        this.packForces(scene.Forces);
        this.packEmitters(scene.Emitters);
        this.packColliders(scene.Colliders);
        gl.disable(gl.BLEND);
        gl.disable(gl.DEPTH_TEST);

        // 1. Emission into velocity + state.
        if (this.emitterArrays.count > 0)
        {
            this.inject(0, fields.velocity, deltaTime);
            this.inject(1, fields.state, deltaTime);
        }

        // 2. Self-advection of velocity and transport of the scalar state.
        this.advectField(fields.velocity, fields.velocity.read.texture, deltaTime, domain.VelocityDissipation, domain.Advection);
        this.advectField(fields.state, fields.velocity.read.texture, deltaTime, domain.Dissipation, domain.Advection);

        // 3. Combustion + cooling.
        let uniforms = context.use(programs.thermodynamics);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uState', fields.state.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uCooling, domain.Cooling);
        gl.uniform1f(uniforms.uCombustion, domain.Combustion);
        gl.uniform1f(uniforms.uAmbientTemperature, domain.AmbientTemperature);
        context.dispatch(fields.state.write);
        fields.state.swap();

        // 4. Buoyancy, gravity, force generators, collider masking.
        uniforms = context.use(programs.forces);
        this.applyVolumeUniforms(uniforms);
        this.applyForceUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.bindTexture('uState', fields.state.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uBuoyancy, domain.Buoyancy);
        gl.uniform1f(uniforms.uGravity, domain.Gravity);
        gl.uniform1f(uniforms.uAmbientTemperature, domain.AmbientTemperature);
        gl.uniform1f(uniforms.uForceResponse, domain.ForceResponse ?? 1.0);
        context.dispatch(fields.velocity.write);
        fields.velocity.swap();

        // 5. Vorticity confinement (curl is retained for visualisation).
        uniforms = context.use(programs.curl);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.dispatch(fields.curl);
        if (domain.Vorticity > 0)
        {
            uniforms = context.use(programs.vorticity);
            this.applyVolumeUniforms(uniforms);
            context.bindTexture('uVelocity', fields.velocity.read.texture);
            context.bindTexture('uCurl', fields.curl.texture);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uConfinement, domain.Vorticity);
            context.dispatch(fields.velocity.write);
            fields.velocity.swap();
        }

        // 6. Pressure projection.
        uniforms = context.use(programs.divergence);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.bindTexture('uState', fields.state.read.texture);
        const look = (scene.Visualizers && scene.Visualizers.Volume) || {};
        gl.uniform1f(uniforms.uExpansion, domain.Expansion ?? 0);
        gl.uniform1f(uniforms.uFireThreshold, look.FireThreshold ?? 1.0);
        gl.uniform1f(uniforms.uFireSoftness, look.FireSoftness ?? 3.5);
        context.dispatch(fields.divergence);

        // Warm start: the previous step's pressure is the first guess (it is close, so the Jacobi iterations go further),
        // slightly forgotten each step so closed boxes cannot drift. Its .y channel carries the fire-light field.
        uniforms = context.use(programs.pressure);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        const pressureUnit = context.textureUnit;
        for (let iteration = 0; iteration < domain.PressureIterations; iteration += 1)
        {
            context.textureUnit = pressureUnit;
            gl.uniform1f(uniforms.uRetain, iteration === 0 ? 0.96 : 1.0);
            context.bindTexture('uPressure', fields.pressure.read.texture);
            context.bindTexture('uDivergence', fields.divergence.texture);
            context.dispatch(fields.pressure.write);
            fields.pressure.swap();
        }

        uniforms = context.use(programs.gradient);
        this.applyVolumeUniforms(uniforms);
        this.applyColliderUniforms(uniforms);
        this.applyBoundaryUniform(uniforms, domain);
        context.bindTexture('uPressure', fields.pressure.read.texture);
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.dispatch(fields.velocity.write);
        fields.velocity.swap();

        this.statistics.passes += 7 + domain.PressureIterations;
        this.time += deltaTime;
        this.frame += 1;
        this.statistics.stepMilliseconds = performance.now() - started;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TRACERS
    //------------------------------------------------------------------------------------------------------------------------

    updateTracers(deltaTime, lifetime)
    {
        if (!this.fields) return;
        const gl = this.gl, context = this.context;
        gl.disable(gl.BLEND);
        // Keep the previous frame for trail segments.
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.tracers.read.framebuffer);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.tracerPrevious.framebuffer);
        gl.blitFramebuffer(0, 0, this.tracerSize[0], this.tracerSize[1], 0, 0, this.tracerSize[0], this.tracerSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
        gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
        gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        const uniforms = context.use(this.programs.tracerUpdate);
        this.applyVolumeUniforms(uniforms);
        context.bindTexture('uTracers', this.tracers.read.texture);
        context.bindTexture('uVelocity', this.fields.velocity.read.texture);
        gl.uniform1f(uniforms.uDeltaTime, deltaTime);
        gl.uniform1f(uniforms.uTime, this.time);
        gl.uniform1f(uniforms.uLifetime, lifetime);
        gl.uniform1i(uniforms.uReset, this.tracersNeedReset ? 1 : 0);
        context.dispatch(this.tracers.write);
        this.tracers.swap();
        if (this.tracersNeedReset)
        {
            this.tracersNeedReset = false;
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.tracers.read.framebuffer);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.tracerPrevious.framebuffer);
            gl.blitFramebuffer(0, 0, this.tracerSize[0], this.tracerSize[1], 0, 0, this.tracerSize[0], this.tracerSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  DEBRIS
    //------------------------------------------------------------------------------------------------------------------------

    updateDebris(deltaTime, settings)
    {
        if (!this.fields) return;
        const gl = this.gl, context = this.context;
        gl.disable(gl.BLEND);
        const copyPrevious = () =>
        {
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, this.debrisPositions.read.framebuffer);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, this.debrisPrevious.framebuffer);
            gl.blitFramebuffer(0, 0, this.debrisSize[0], this.debrisSize[1], 0, 0, this.debrisSize[0], this.debrisSize[1], gl.COLOR_BUFFER_BIT, gl.NEAREST);
            gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
            gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
        };
        copyPrevious();
        const uniforms = context.use(this.programs.debrisUpdate);
        this.applyVolumeUniforms(uniforms);
        for (const output of [0, 1])
        {
            context.textureUnit = 0;
            context.bindTexture('uPositions', this.debrisPositions.read.texture);
            context.bindTexture('uVelocities', this.debrisVelocities.read.texture);
            context.bindTexture('uVelocity', this.fields.velocity.read.texture);
            gl.uniform1i(uniforms.uOutput, output);
            gl.uniform1i(uniforms.uReset, this.debrisNeedReset ? 1 : 0);
            gl.uniform1i(uniforms.uEmitterCount, this.emitterArrays.count);
            gl.uniform4fv(uniforms.uEmitterA, this.emitterArrays.a);
            gl.uniform4fv(uniforms.uEmitterB, this.emitterArrays.b);
            gl.uniform4fv(uniforms.uEmitterE, this.emitterArrays.e);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uTime, this.time);
            gl.uniform1f(uniforms.uLifetime, settings.Lifetime);
            gl.uniform1f(uniforms.uGravity, settings.Gravity);
            gl.uniform1f(uniforms.uDrag, settings.Drag);
            gl.uniform1f(uniforms.uBounce, settings.Bounce);
            context.dispatch(output === 0 ? this.debrisPositions.write : this.debrisVelocities.write);
        }
        this.debrisPositions.swap();
        this.debrisVelocities.swap();
        if (this.debrisNeedReset)
        {
            this.debrisNeedReset = false;
            copyPrevious();
        }
    }

    get debrisCount()
    {
        return this.debrisSize[0] * this.debrisSize[1];
    }

    get tracerCount()
    {
        return this.tracerSize[0] * this.tracerSize[1];
    }

    get domainHalfExtent()
    {
        const layout = this.layout;
        return [layout.width * this.cellSize * 0.5, layout.height * this.cellSize * 0.5, layout.depth * this.cellSize * 0.5];
    }

    /** World-space centre of the simulated window (moves as the World Grid follows its target). */
    get domainCentre()
    {
        return this.gridCentre || [0, 0, 0];
    }
}
