//============================================================================================================================================
//                                                        SIMULATIONCHECKPOINTS.JS
//============================================================================================================================================
// 🧩 Timeline scrubbing for a stateful simulation (Fluid layer stack plan §5.4, phase 4).
//
//   Scrub to t ──► t ≥ live time? ── yes ──► step forward to t
//                         └──── no ──► restore the newest checkpoint ≤ t (or the empty grid at 0) ──► step forward to t
//
// A checkpoint is an exact GPU copy (blit, nearest) of everything a step reads: velocity, density / temperature / fuel, pressure (warm
// start), tracers, debris, every particle system's position / velocity / previous / trail textures, plus the CPU state that steers the
// next step: solver clock and step index, the sparse tile table, the World Grid origin and each particle record's flags. Noise is seeded
// from the solver clock, so restore + re-step reproduces the live run bit for bit (FluidValidation §9 and the scrub capture check it).
// Checkpoints live in a ring inside a GPU memory budget; the oldest is dropped first. Any edit invalidates the checkpoints it could affect.

export const DefaultCheckpointInterval = 0.5;                  // seconds of simulation time between checkpoints
export const DefaultCheckpointBudget = 192 * 1024 * 1024;      // bytes of GPU memory for the ring
export const MaximumCheckpoints = 64;

const bytesOf = (target) => target.width * target.height * (target.rgba32f ? 16 : 8);
const cloneEntries = (entries) => entries.map((entry) => (entry ? { ...entry, tile: [...entry.tile] } : null));

export class CheckpointRing
{
    constructor(context, { interval = DefaultCheckpointInterval, budget = DefaultCheckpointBudget } = {})
    {
        this.context = context;
        this.interval = interval;
        this.budget = budget;
        this.entries = [];                                       // sorted by Time
        this.spare = [];                                         // dropped checkpoints whose targets can be reused
        this.statistics = { captures: 0, restores: 0, dropped: 0 };
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  WHAT A CHECKPOINT HOLDS
    //------------------------------------------------------------------------------------------------------------------------

    /** Every GPU target a step reads, by stable name. Write sides of ping-pongs are fully overwritten before use, so only read sides count. */
    sources(solver, particles)
    {
        const list = [];
        const fields = solver.fields;
        if (!fields) return list;
        list.push(['velocity', fields.velocity.read], ['state', fields.state.read], ['pressure', fields.pressure.read], ['curl', fields.curl]);
        if (solver.tracers) list.push(['tracers', solver.tracers.read], ['tracerPrevious', solver.tracerPrevious]);
        if (solver.debrisPositions) list.push(['debrisPositions', solver.debrisPositions.read], ['debrisVelocities', solver.debrisVelocities.read], ['debrisPrevious', solver.debrisPrevious]);
        for (const [id, record] of particles.records)
        {
            list.push([`${id}:positions`, record.positions.read], [`${id}:velocities`, record.velocities.read], [`${id}:previous`, record.previous]);
            if (record.trails) list.push([`${id}:trails`, record.trails.read]);
        }
        return list;
    }

    /** Layout signature: a checkpoint only restores into the same grid and particle records. */
    signature(solver, particles)
    {
        return this.sources(solver, particles).map(([name, target]) => `${name}:${target.width}x${target.height}${target.rgba32f ? 'f' : 'h'}`).join('|') + `|${solver.sparse ? 'sparse' : 'fixed'}`;
    }

    bytesPerCheckpoint(solver, particles) { return this.sources(solver, particles).reduce((sum, [, target]) => sum + bytesOf(target), 0); }

    /** How many checkpoints fit the budget for the current scene. */
    capacity(solver, particles)
    {
        const bytes = this.bytesPerCheckpoint(solver, particles);
        return bytes > 0 ? Math.max(1, Math.min(MaximumCheckpoints, Math.floor(this.budget / bytes))) : 0;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  CAPTURE / RESTORE
    //------------------------------------------------------------------------------------------------------------------------

    /** True when a checkpoint is due after a step that ended at `time` (one per interval; none twice for the same slot). */
    due(time, deltaTime)
    {
        if (!(this.interval > 0)) return false;
        const slot = Math.round(time / this.interval);
        if (Math.abs(time - slot * this.interval) > deltaTime * 0.5 || slot <= 0) return false;
        return !this.entries.some((entry) => entry.Slot === slot);
    }

    capture(solver, particles, scene, deltaTime)
    {
        const signature = this.signature(solver, particles);
        if (this.entries.length && this.entries[0].Signature !== signature) this.clear();
        const capacity = this.capacity(solver, particles);
        while (this.entries.length >= capacity) this.drop(this.entries.shift());
        for (const stale of this.spare.filter((spare) => spare.Signature !== signature)) for (const target of stale.Targets.values()) this.context.destroyTarget(target);
        this.spare = this.spare.filter((spare) => spare.Signature === signature);
        const reuse = this.spare.findIndex((spare) => spare.Signature === signature);
        const entry = reuse >= 0 ? this.spare.splice(reuse, 1)[0] : { Signature: signature, Targets: new Map() };
        for (const [name, source] of this.sources(solver, particles))
        {
            let target = entry.Targets.get(name);
            if (!target) { target = this.context.createTarget(source.width, source.height, { rgba32f: source.rgba32f, nearest: true }); entry.Targets.set(name, target); }
            this.context.copyTarget(source, target);
        }
        entry.Time = solver.time;
        entry.Slot = Math.round(solver.time / this.interval);
        entry.DeltaTime = deltaTime;
        entry.Solver = {
            time: solver.time, frame: solver.frame, relocations: solver.relocations,
            gridCentre: [...solver.gridCentre], cellOrigin: [...solver.cellOrigin],
            tracersNeedReset: Boolean(solver.tracersNeedReset), debrisNeedReset: Boolean(solver.debrisNeedReset),
            pool: solver.sparse && solver.tilePool ? { entries: cloneEntries(solver.tilePool.entries), lookupOrigin: [...solver.tilePool.lookupOrigin], allocations: solver.tilePool.allocations, frees: solver.tilePool.frees } : null,
        };
        entry.GridOrigin = scene.Domain.GridOrigin ? [...scene.Domain.GridOrigin] : null;
        entry.Records = new Map([...particles.records].map(([id, record]) => [id, { needsReset: record.needsReset, signature: record.signature, trailHead: record.trailHead, trailClock: record.trailClock, trailNeedsReset: record.trailNeedsReset, trailLength: record.trailLength }]));
        this.entries.push(entry);
        this.entries.sort((left, right) => left.Time - right.Time);
        this.statistics.captures += 1;
        return entry;
    }

    /** Newest checkpoint at or before `time` that still matches the scene layout, or null. */
    newestAtOrBefore(time, solver, particles)
    {
        const signature = this.signature(solver, particles);
        for (let index = this.entries.length - 1; index >= 0; index -= 1)
            if (this.entries[index].Time <= time + 1e-9 && this.entries[index].Signature === signature) return this.entries[index];
        return null;
    }

    /** Writes a checkpoint back into the live solver and particle records. */
    restore(entry, solver, particles, scene)
    {
        for (const [name, source] of this.sources(solver, particles))
        {
            const saved = entry.Targets.get(name);
            if (saved) this.context.copyTarget(saved, source);
        }
        const state = entry.Solver;
        solver.time = state.time;
        solver.frame = state.frame;
        solver.relocations = state.relocations;
        solver.gridCentre = [...state.gridCentre];
        solver.cellOrigin = [...state.cellOrigin];
        solver.tracersNeedReset = state.tracersNeedReset;
        solver.debrisNeedReset = state.debrisNeedReset;
        if (state.pool && solver.tilePool)
        {
            const pool = solver.tilePool;
            pool.entries = cloneEntries(state.pool.entries);
            pool.map = new Map();
            pool.entries.forEach((tileEntry, slot) => { if (tileEntry) pool.map.set(solver.tileKey(tileEntry.tile), slot); });
            pool.lookupOrigin = [...state.pool.lookupOrigin];
            pool.allocations = state.pool.allocations;
            pool.frees = state.pool.frees;
            pool.dirty = true;
            solver.uploadTileTables();
        }
        if (entry.GridOrigin && scene.Domain.GridOrigin) scene.Domain.GridOrigin = [...entry.GridOrigin];
        for (const [id, record] of particles.records)
        {
            const saved = entry.Records.get(id);
            if (saved) Object.assign(record, saved);
        }
        this.statistics.restores += 1;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INVALIDATION
    //------------------------------------------------------------------------------------------------------------------------

    /** Drops every checkpoint later than `time` (an edit at `time` cannot change the past). `time` 0 drops them all. */
    invalidateAfter(time = 0)
    {
        const keep = [];
        for (const entry of this.entries) if (entry.Time <= time + 1e-9 && time > 0) keep.push(entry); else this.drop(entry);
        this.entries = keep;
    }

    clear() { for (const entry of this.entries) this.drop(entry); this.entries = []; }

    drop(entry)
    {
        this.statistics.dropped += 1;
        // Keep a couple of spare target sets so a ring that keeps cycling does not reallocate; free the rest.
        if (this.spare.length < 2) this.spare.push(entry);
        else for (const target of entry.Targets.values()) this.context.destroyTarget(target);
    }

    /** Releases every GPU target (scene change, resolution change). */
    destroy()
    {
        this.clear();
        for (const spare of this.spare) for (const target of spare.Targets.values()) this.context.destroyTarget(target);
        this.spare = [];
    }

    summary(solver, particles)
    {
        const bytes = this.bytesPerCheckpoint(solver, particles);
        return { Count: this.entries.length, Capacity: this.capacity(solver, particles), Bytes: bytes * this.entries.length, BytesEach: bytes, Interval: this.interval, Times: this.entries.map((entry) => entry.Time) };
    }
}
