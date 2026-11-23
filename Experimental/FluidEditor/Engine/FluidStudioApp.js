//============================================================================================================================================
//                                                        FLUIDSTUDIOAPP.JS
//              Fluid Studio Application State — Scene Graph (Domain · Emitters · Forces · Colliders · Visualizers),
//           Preset Loading, Transport (Play / Pause / Step / Reset), Viewport Input (Orbit / Pan / Dolly / Pick / Drag),
//                                        JSON Serialisation, and Live Metric Reporting
//============================================================================================================================================

import { GpuContext } from './GpuContext.js';
import { FluidSolver } from './FluidSolver.js';
import { VolumeRenderer } from './VolumeRenderer.js';
import { OrbitCamera, Vec3 } from './OrbitCamera.js';
import { createDomain, createEmitter, createForceGenerator, createCollider, createVisualizerSet, ForceTypeById, resolveDomainGrid, emitterTransform, gridWindowCentre, gridTileOf } from './ForceGeneratorCatalog.js';
import { ParticleSimulation } from './ParticleSimulation.js';
import { createParticleSystem, evaluateParticleSystem, ParticleTemplateById, MaximumParticleSystems, ObjectNames } from './ParticleCatalog.js';
import { ImportCustomObject, CustomObjectEntry } from './ObjectMeshCatalog.js';
import {
    convertSceneToGasStack, evaluateGasStack, applyGasEvaluation, owningLayer, createLayer, createLinkedSource, readGasStack, coerceAmount,
    LayerSpecificationByCode, SourceLayerCode, MaximumLayers, SpecificationHash, FloatKeys, evaluateParticleStack, insertLayerInPhaseOrder,
} from './LayerStack.js';
import { ScenePresets, ScenePresetById, buildSceneFromDocument, sceneToDocument } from './PresetScenes.js';
import { writeToml } from './TomlSubset.js';
import {
    findTrack, setKey, removeKey, keyIndexAt, transformKeys, sampleTrack, nodeWeight, weightTrackOf, keyedNode, isKeyableField, fieldParameter, affectedFrom,
    SteppedTypes, TimingFields, KeyTimeEpsilon, roundTime,
} from './KeyTracks.js';
import { layerAmountAt, resolveTrackPath, isKeyableParameter, WeightParameter, ParameterOwners, ParticleParameterOwners } from './LayerStack.js';
import { CheckpointRing } from './SimulationCheckpoints.js';
import {
    ensureSceneDirectory,
    syncSceneDirectory,
    recomposeSceneFromDirectory,
    appendImportedSimulation,
    activateSimulationFolder,
    assignEntityScope,
    createCollectionFolder,
    moveEntityToFolder,
    renameFolder,
    deleteFolder,
    toggleFolderVisibility,
    serializeSceneDirectory,
} from './SceneFolders.js';

/** Timeline defaults (plan §5.1): the ruler spans at least this long; Loop wraps the playhead at End. */
export const DefaultTimelineEnd = 8;

//------------------------------------------------------------------------------------------------------------------------
//                                                  FLIPBOOK LAYOUT
//------------------------------------------------------------------------------------------------------------------------

export const FlipbookChoices = Object.freeze({ Count: [4, 16, 32, 64], Size: [64, 128, 256, 512], Rate: [12, 24, 30, 60] });

/** Sheet layout for a bake: a near-square grid of `Count` tiles of `Size` pixels. */
export function specifyFlipbook(count, size, rate, warmup)
{
    if (!FlipbookChoices.Count.includes(count) || !FlipbookChoices.Size.includes(size) || !FlipbookChoices.Rate.includes(rate) || !Number.isFinite(warmup) || warmup < 0 || warmup > 3)
        throw new Error('Choose a supported frame count, tile size, frame rate and a 0–3 second warm-up.');
    const columns = Math.ceil(Math.sqrt(count));
    const rows = Math.ceil(count / columns);
    return { Count: count, Size: size, Rate: rate, Warmup: warmup, Columns: columns, Rows: rows, Width: columns * size, Height: rows * size };
}

/** GL rows run bottom-up; flip them, and for transparent frames turn premultiplied colour into straight alpha. */
export function unpackFrame(bytes, size, transparent)
{
    const pixels = new Uint8ClampedArray(bytes.length);
    for (let row = 0; row < size; row += 1)
        for (let column = 0; column < size; column += 1)
        {
            const source = ((size - 1 - row) * size + column) * 4;
            const target = (row * size + column) * 4;
            const alpha = transparent ? Math.max(bytes[source + 3], bytes[source], bytes[source + 1], bytes[source + 2]) : 255;
            for (let channel = 0; channel < 3; channel += 1)
                pixels[target + channel] = transparent ? (alpha ? Math.round(bytes[source + channel] * 255 / alpha) : 0) : bytes[source + channel];
            pixels[target + 3] = alpha;
        }
    return pixels;
}

export class FluidStudioApp
{
    constructor({ canvas, onMetrics, onStateChange, onError })
    {
        this.canvas = canvas;
        this.onMetrics = onMetrics || (() => {});
        this.onStateChange = onStateChange || (() => {});
        this.onError = onError || ((error) => console.error(error));
        this.camera = new OrbitCamera();
        this.scene = null;
        this.selection = null;
        this.playing = true;
        this.stepRequested = false;
        this.fixedDeltaTime = 1 / 60;
        this.cameraFollowsGrid = true;
        this.lastFrameTime = 0;
        this.frameTimes = [];
        this.pointer = { mode: null, x: 0, y: 0, dragNode: null, dragPlanePoint: null, dragOffset: null, moved: false };
        this.hover = null;
        this.supported = true;
        this.autoKey = false;                 // Auto-key: an edit writes a key at the playhead even on an un-keyed parameter
        this.seekTarget = null;               // pending scrub target (seconds), stepped towards a few steps per frame
        this.resimulate = false;              // a key edit changed the past: restore before the edit and re-step to the playhead
        this.resumeAfterSeek = false;         // playback continues once a re-simulation reaches the playhead
        this.seekBudgetMilliseconds = 24;
        this.authoredText = '';
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  LIFECYCLE
    //------------------------------------------------------------------------------------------------------------------------

    init()
    {
        try
        {
            this.context = new GpuContext(this.canvas);
            this.solver = new FluidSolver(this.context);
            this.renderer = new VolumeRenderer(this.context, this.solver);
            this.particles = new ParticleSimulation(this.context, this.solver);
            this.renderer.particles = this.particles;
            this.checkpoints = new CheckpointRing(this.context);
        }
        catch (error)
        {
            this.supported = false;
            this.onError(error);
            return;
        }
        this.loadPreset(ScenePresets[0].Id, false);
        this.attachInput();
        this.resize();
        window.addEventListener('resize', () => this.resize());
        requestAnimationFrame((time) => this.frame(time));
    }

    resize()
    {
        const ratio = Math.min(window.devicePixelRatio || 1, 1.5);
        const rect = this.canvas.getBoundingClientRect();
        const width = Math.max(1, Math.floor(rect.width * ratio));
        const height = Math.max(1, Math.floor(rect.height * ratio));
        if (this.canvas.width !== width || this.canvas.height !== height)
        {
            this.canvas.width = width;
            this.canvas.height = height;
        }
        this.camera.setViewport(rect.width, rect.height);
    }

    frame(time)
    {
        requestAnimationFrame((next) => this.frame(next));
        if (!this.supported || !this.scene || this.baking) return;
        const elapsed = this.lastFrameTime ? Math.min(0.1, (time - this.lastFrameTime) / 1000) : this.fixedDeltaTime;
        this.lastFrameTime = time;
        this.frameTimes.push(elapsed);
        if (this.frameTimes.length > 45) this.frameTimes.shift();
        this.resize();
        const domain = this.scene.Domain;
        const deltaTime = this.fixedDeltaTime * domain.TimeScale;
        try
        {
            if (this.seekTarget !== null) this.continueSeek(this.seekBudgetMilliseconds);
            else if (this.playing || this.stepRequested)
            {
                const timeline = this.timeline();
                if (this.playing && timeline.Loop && this.solver.time >= timeline.End - deltaTime * 0.5) this.restartTimeline();
                else this.advanceSimulation(deltaTime);
                this.stepRequested = false;
            }
            else
            {
                if (this.solver.tracersNeedReset) this.solver.updateTracers(0, this.scene.Visualizers.Tracers.Lifetime);
                if (this.solver.debrisNeedReset) this.solver.updateDebris(0, this.scene.Visualizers.Debris);
                this.particles.update(this.scene, 0);
            }
            this.lastMatrices = this.renderer.render(this.scene, this.camera, this.selection, deltaTime);
        }
        catch (error)
        {
            this.supported = false;
            this.onError(error);
            return;
        }
        this.reportMetrics(elapsed);
    }

    /** One simulation step of everything that moves: grid, tracers, debris and particle systems. */
    advanceSimulation(deltaTime)
    {
        // A gas stack that cannot evaluate (missing Domain layer, over capacity) holds the gas still; particles keep moving.
        if (this.gasEvaluation && this.gasEvaluation.Stopped) { this.particles.update(this.scene, deltaTime); return; }
        const domain = this.scene.Domain;
        if (domain.SceneLoop > 0 && this.solver.time >= domain.SceneLoop) { this.solver.reset(); this.particles.resetAll(); this.recentreGrid(); }
        // Keyed stacks are sampled at the solver clock before the step, like the timing envelope always was.
        if (this.hasStackTracks()) this.applyKeys(this.solver.time);
        this.followGrid();
        this.solver.step(this.scene, deltaTime);
        this.solver.updateTracers(deltaTime, this.scene.Visualizers.Tracers.Lifetime);
        this.solver.updateDebris(deltaTime, this.scene.Visualizers.Debris);
        this.particles.update(this.scene, deltaTime);
        if (this.checkpoints && this.checkpoints.due(this.solver.time, deltaTime)) this.checkpoints.capture(this.solver, this.particles, this.scene, deltaTime);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TIMELINE AND SCRUBBING (plan §5.4)
    //------------------------------------------------------------------------------------------------------------------------

    /** Seconds of simulation per step. */
    stepDeltaTime() { return this.fixedDeltaTime * (this.scene ? this.scene.Domain.TimeScale : 1); }

    /** The scene's timeline settings: End (ruler length / loop point) and Loop. A Scene Loop on the domain sets End. */
    timeline()
    {
        if (!this.scene.Timeline) this.scene.Timeline = { End: this.defaultTimelineEnd(), Loop: false };
        return this.scene.Timeline;
    }

    defaultTimelineEnd()
    {
        const domain = this.scene.Domain;
        if (domain.SceneLoop > 0) return domain.SceneLoop;
        let last = 0;
        for (const { Track } of this.allTracks()) last = Math.max(last, Track.Keys[Track.Keys.length - 1].Time);
        return Math.max(DefaultTimelineEnd, Math.ceil(last + 1));
    }

    setTimeline(key, value)
    {
        const timeline = this.timeline();
        if (key === 'End') timeline.End = Math.max(0.5, Math.min(600, Number(value) || DefaultTimelineEnd));
        else if (key === 'Loop') timeline.Loop = Boolean(value);
        this.notify();
    }

    /** Back to t = 0: the empty grid every run starts from (the implicit first checkpoint). */
    restartTimeline()
    {
        const domain = this.scene.Domain;
        if (domain.GridOrigin && domain.GridOrigin.some((value) => value !== 0))
        {
            domain.GridOrigin = [0, 0, 0];
            this.solver.relocate([0, 0, 0], gridWindowCentre(domain));
        }
        this.solver.reset();
        this.particles.resetAll();
        this.applyKeys(0);
    }

    /** Scrub to `time` (seconds). Forward steps the simulation; backward restores the newest checkpoint ≤ time and re-steps. */
    seek(time)
    {
        if (this.baking || !this.scene) return;
        this.seekTarget = Math.max(0, roundTime(Number(time) || 0));
        this.playing = false;
        this.continueSeek(this.seekBudgetMilliseconds);
        this.notify();
    }

    /** Synchronous scrub (validation, flipbook, tests): steps until the playhead reaches `time`. Returns the number of steps taken. */
    seekNow(time)
    {
        this.seekTarget = Math.max(0, roundTime(Number(time) || 0));
        this.playing = false;
        const before = this.statistics ? this.statistics.seekSteps : 0;
        this.continueSeek(Infinity);
        return (this.statistics ? this.statistics.seekSteps : 0) - before;
    }

    continueSeek(budgetMilliseconds)
    {
        if (this.seekTarget === null) return;
        this.statistics = this.statistics || { seekSteps: 0, seekRestores: 0 };
        const deltaTime = this.stepDeltaTime();
        const target = this.seekTarget;
        if (target < this.solver.time - deltaTime * 0.5 || this.resimulate)
        {
            this.resimulate = false;
            const entry = this.checkpoints && this.checkpoints.newestAtOrBefore(target, this.solver, this.particles);
            if (entry) { this.applyKeys(entry.Time); this.checkpoints.restore(entry, this.solver, this.particles, this.scene); }
            else this.restartTimeline();
            this.statistics.seekRestores += 1;
        }
        const started = performance.now();
        while (this.solver.time < target - deltaTime * 0.5)
        {
            this.advanceSimulation(deltaTime);
            this.statistics.seekSteps += 1;
            if (performance.now() - started > budgetMilliseconds) return;
        }
        this.seekTarget = null;
        this.applyKeys(this.solver.time);
        if (this.resumeAfterSeek) { this.resumeAfterSeek = false; this.playing = true; }
        this.notify();
    }

    stepBack() { this.seek(Math.max(0, this.solver.time - this.stepDeltaTime())); }
    goToStart() { this.seek(0); }
    goToEnd() { this.seek(this.timeline().End); }

    /** Previous / next key time across every track (dope-sheet navigation). */
    adjacentKeyTime(direction)
    {
        const now = this.solver.time;
        let best = null;
        for (const { Track } of this.allTracks())
            for (const key of Track.Keys)
                if (direction > 0 ? key.Time > now + KeyTimeEpsilon && (best === null || key.Time < best) : key.Time < now - KeyTimeEpsilon && (best === null || key.Time > best)) best = key.Time;
        return best;
    }

    checkpointSummary() { return this.checkpoints ? this.checkpoints.summary(this.solver, this.particles) : { Count: 0, Capacity: 0, Bytes: 0, Times: [] }; }

    /** Edits that change authored data invalidate checkpoints. Runtime fields (evaluated amounts, grid origin) are not authored. */
    authoredSignature()
    {
        const scene = this.scene;
        if (!scene) return '';
        const evaluated = new Set([...Object.keys(ParameterOwners.Domain), ...Object.keys(ParameterOwners.Volume), 'GridOrigin']);
        const domain = Object.fromEntries(Object.entries(scene.Domain).filter(([key]) => !evaluated.has(key)));
        const volume = Object.fromEntries(Object.entries(scene.Visualizers.Volume).filter(([key]) => !evaluated.has(key)));
        const particles = scene.Particles.map((node) => [node.Id, node.Enabled, node.Stack, node.Tracks, ...TimingFields.map((key) => node[key])]);
        const emitters = scene.Emitters.map((emitter) => ({ ...emitter, Enabled: undefined }));
        return JSON.stringify([domain, volume, scene.GasStack, emitters, scene.Forces, scene.Colliders, particles, scene.Visualizers.Debris, scene.Visualizers.Tracers]);
    }

    /** Drops checkpoints an edit could affect: all of them for a static edit, those after `fromTime` for a key edit. */
    invalidateCheckpoints(fromTime = 0)
    {
        if (this.checkpoints) this.checkpoints.invalidateAfter(fromTime);
        this.authoredText = this.authoredSignature();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  KEYS (plan §5.2)
    //------------------------------------------------------------------------------------------------------------------------

    hasStackTracks()
    {
        if (this.scene.GasStack && this.scene.GasStack.Tracks && this.scene.GasStack.Tracks.length) return true;
        return this.scene.Particles.some((node) => node.Stack && node.Stack.Tracks && node.Stack.Tracks.length);
    }

    /** Folds every keyed stack at `time` into the flat fields the GPU reads (forces and emitters are sampled inside the solver). */
    applyKeys(time)
    {
        const stack = this.scene.GasStack;
        if (stack && stack.Tracks && stack.Tracks.length)
        {
            this.gasEvaluation = evaluateGasStack(stack, this.scene.Emitters, time);
            applyGasEvaluation(this.scene, this.gasEvaluation);
        }
        for (const node of this.scene.Particles) if (node.Stack && node.Stack.Tracks && node.Stack.Tracks.length) this.evaluateParticles(node, time);
    }

    /** Every track in the scene with its owner: { Track, Target, Stack, Node, Derived }. Derived = Weight generated by timing fields. */
    allTracks(includeDerived = false)
    {
        const result = [];
        if (!this.scene) return result;
        const stack = this.scene.GasStack;
        for (const track of (stack && stack.Tracks) || []) { const [token, name] = track.Path.split('/'); result.push({ Track: track, Target: { Kind: 'Layer', Token: token, Name: name }, Stack: stack, Node: null }); }
        for (const node of [...this.scene.Emitters, ...this.scene.Forces, ...this.scene.Particles])
        {
            for (const track of node.Tracks || []) result.push({ Track: track, Target: { Kind: 'Node', Id: node.Id, Name: track.Path }, Stack: null, Node: node });
            if (includeDerived && !findTrack(node.Tracks, 'Weight') && weightTrackOf(node)) result.push({ Track: weightTrackOf(node), Target: { Kind: 'Node', Id: node.Id, Name: 'Weight' }, Stack: null, Node: node, Derived: true });
            if (node.Kind === 'Particles' && node.Stack) for (const track of node.Stack.Tracks || []) { const [token, name] = track.Path.split('/'); result.push({ Track: track, Target: { Kind: 'Layer', Token: token, Name: name }, Stack: node.Stack, Node: node }); }
        }
        return result;
    }

    /** A flat inspector field → its key target: layer-owned fields (Domain, Volume Render, particle GPU fields) key their owning layer. */
    keyTargetForField(node, key)
    {
        if (!node) return null;
        if (node.Kind === 'Particles' && key !== 'Weight')
        {
            const found = node.Stack && owningLayer(node.Stack, 'Particles', key);
            return found && found.Layer && isKeyableParameter(found.Owner.Parameter) ? { Kind: 'Layer', Token: found.Layer.Token, Name: key } : null;
        }
        if (node.Kind === 'Domain' || node.Id === 'vis-volume')
        {
            const found = this.scene.GasStack && owningLayer(this.scene.GasStack, node.Kind === 'Domain' ? 'Domain' : 'Volume', key);
            return found && found.Layer && isKeyableParameter(found.Owner.Parameter) ? { Kind: 'Layer', Token: found.Layer.Token, Name: key } : null;
        }
        if ((node.Kind === 'Force' || node.Kind === 'Emitter') && isKeyableField(node, key)) return { Kind: 'Node', Id: node.Id, Name: key };
        if (node.Kind === 'Particles' && key === 'Weight') return { Kind: 'Node', Id: node.Id, Name: 'Weight' };
        return null;
    }

    /** Where a key target's track lives and what it animates. */
    resolveKeyTarget(target)
    {
        if (!target || !this.scene) return null;
        const time = this.solver.time;
        if (target.Kind === 'Layer')
        {
            const { stack, node } = this.stackOfLayer(target.Token);
            if (!stack) return null;
            const path = `${target.Token}/${target.Name}`;
            const resolved = resolveTrackPath(stack, path);
            if (!resolved || (resolved.Parameter && !isKeyableParameter(resolved.Parameter))) return null;
            if (!stack.Tracks) stack.Tracks = [];
            const parameter = resolved.Parameter || WeightParameter;
            return { Tracks: stack.Tracks, Path: path, Parameter: parameter, Stack: stack, Node: node, Layer: resolved.Layer, Label: `${resolved.Specification.Name} › ${parameter.Label || target.Name}`,
                Current: () => layerAmountAt(stack, resolved.Layer, target.Name, this.solver.time), Static: () => (target.Name === 'Weight' ? resolved.Layer.Weight : resolved.Layer.Parameters[target.Name]) };
        }
        const node = this.findNode(target.Id);
        if (!node || !isKeyableField(node, target.Name)) return null;
        if (!node.Tracks) node.Tracks = [];
        const parameter = target.Name === 'Weight' ? WeightParameter : fieldParameter(node, target.Name);
        return { Tracks: node.Tracks, Path: target.Name, Parameter: parameter, Stack: null, Node: node, Label: target.Name,
            Current: () => (target.Name === 'Weight' ? nodeWeight(node, this.solver.time) : keyedNode(node, this.solver.time)[target.Name]), Static: () => (target.Name === 'Weight' ? 1 : node[target.Name]) };
    }

    /** ◆ state beside a control: 'key' (a key at the playhead), 'track' (keyed, between keys), 'timing' (Weight from timing fields), 'none'. */
    keyState(target)
    {
        const resolved = this.resolveKeyTarget(target);
        if (!resolved) return null;
        const track = findTrack(resolved.Tracks, resolved.Path);
        if (!track) return target.Kind === 'Node' && target.Name === 'Weight' && weightTrackOf(resolved.Node) ? 'timing' : 'none';
        return keyIndexAt(track, this.solver.time) >= 0 ? 'key' : 'track';
    }

    /** Amount shown by a control: the keyed amount at the playhead when keyed, else the static one. */
    keyedAmount(target)
    {
        const resolved = this.resolveKeyTarget(target);
        return resolved ? resolved.Current() : undefined;
    }

    /** Timing fields generate the Weight track; keying Weight by hand turns that track into explicit keys and clears the generator. */
    materialiseWeight(node)
    {
        if (findTrack(node.Tracks, 'Weight')) return findTrack(node.Tracks, 'Weight');
        const generated = weightTrackOf(node);
        if (!node.Tracks) node.Tracks = [];
        if (!generated) return null;
        const track = JSON.parse(JSON.stringify(generated));
        delete track.Envelope;
        node.Tracks.push(track);
        node.StartTime = 0; node.Duration = 0; node.Repeat = 0;
        return track;
    }

    /** ◆ button: adds a key holding the current amount, or removes the key at the playhead. */
    toggleKey(target)
    {
        const resolved = this.resolveKeyTarget(target);
        if (!resolved) return false;
        const time = this.solver.time;
        if (target.Kind === 'Node' && target.Name === 'Weight') this.materialiseWeight(resolved.Node);
        const track = findTrack(resolved.Tracks, resolved.Path);
        const from = affectedFrom(track, time);
        if (track && keyIndexAt(track, time) >= 0) removeKey(resolved.Tracks, resolved.Path, time);
        else setKey(resolved.Tracks, resolved.Path, time, resolved.Current(), 2, SteppedTypes.includes(resolved.Parameter.Type));
        this.afterKeyEdit(resolved, from);
        return true;
    }

    /**
     * An inspector edit of a keyable amount. A keyed parameter (or any parameter with Auto-key on) gets a key at the playhead and the
     * static amount stays as it was; returns false when the edit is static and the caller should write it.
     */
    writeKeyed(target, value)
    {
        const resolved = this.resolveKeyTarget(target);
        if (!resolved) return false;
        const keyed = Boolean(findTrack(resolved.Tracks, resolved.Path)) || (target.Kind === 'Node' && target.Name === 'Weight' && weightTrackOf(resolved.Node));
        if (!keyed && !this.autoKey) return false;
        if (target.Kind === 'Node' && target.Name === 'Weight') this.materialiseWeight(resolved.Node);
        const time = this.solver.time;
        const from = affectedFrom(findTrack(resolved.Tracks, resolved.Path), time);
        const amount = resolved.Parameter.Type === 'Float32' && Number.isFinite(resolved.Parameter.Minimum) ? Math.min(resolved.Parameter.Maximum, Math.max(resolved.Parameter.Minimum, value)) : value;
        setKey(resolved.Tracks, resolved.Path, time, amount, 2, SteppedTypes.includes(resolved.Parameter.Type));
        this.afterKeyEdit(resolved, from);
        return true;
    }

    /**
     * The live state at the playhead was simulated with the old keys when a key edit reaches back before it: re-simulate from the newest
     * surviving checkpoint (a few steps per frame), so the viewport, the checkpoints captured later and a scrub back all agree.
     */
    resimulateFrom(fromTime)
    {
        if (!this.solver || this.baking || !(fromTime < this.solver.time - this.stepDeltaTime() * 0.5)) return;
        this.resumeAfterSeek = this.resumeAfterSeek || this.playing;
        this.resimulate = true;
        this.seekTarget = roundTime(this.solver.time);
        this.playing = false;
        this.continueSeek(this.seekBudgetMilliseconds);
    }

    afterKeyEdit(resolved, fromTime)
    {
        if (resolved && resolved.Node && resolved.Node.Kind === 'Particles' && resolved.Stack) this.evaluateParticles(resolved.Node);
        else if (resolved && resolved.Stack) this.evaluateStack();
        this.invalidateCheckpoints(fromTime);
        this.resimulateFrom(fromTime);
        this.notify();
    }

    toggleAutoKey() { this.autoKey = !this.autoKey; this.notify(); }

    /** Dope-sheet edits on a selection of { Track, Key } (key objects survive re-sorting). */
    editKeys(selection, change)
    {
        const tracks = new Map();
        for (const { Track, Key } of selection) { if (!tracks.has(Track)) tracks.set(Track, []); tracks.get(Track).push(Key); }
        let from = Infinity;
        for (const [track, keys] of tracks)
        {
            const before = Math.min(...keys.map((key) => key.Time));
            from = Math.min(from, affectedFrom(track, before));
            if (change.Move)
            {
                transformKeys(track, keys.map((key) => track.Keys.indexOf(key)).filter((index) => index >= 0), change.Move);
                from = Math.min(from, affectedFrom(track, Math.min(...keys.map((key) => key.Time))));
            }
            if (change.Ease !== undefined) for (const key of keys) key.Ease = change.Ease;
            if (change.Tangent) for (const key of keys) Object.assign(key, change.Tangent);
            if (change.Amount !== undefined) for (const key of keys) key.Amount = change.Amount;
            if (change.Delete) track.Keys = track.Keys.filter((key) => !keys.includes(key));
        }
        // Drop tracks that lost every key.
        for (const owner of this.trackOwners()) for (let index = owner.length - 1; index >= 0; index -= 1) if (!owner[index].Keys.length) owner.splice(index, 1);
        this.refreshAfterKeys(from === Infinity ? 0 : from);
    }

    setTrackExtrapolation(track, code)
    {
        track.Extrapolation = [0, 1, 2].includes(code) ? code : 0;
        this.refreshAfterKeys(affectedFrom(track, track.Keys[track.Keys.length - 1].Time));
    }

    trackOwners()
    {
        const owners = [];
        if (this.scene.GasStack && this.scene.GasStack.Tracks) owners.push(this.scene.GasStack.Tracks);
        for (const node of [...this.scene.Emitters, ...this.scene.Forces, ...this.scene.Particles]) { if (node.Tracks) owners.push(node.Tracks); if (node.Stack && node.Stack.Tracks) owners.push(node.Stack.Tracks); }
        return owners;
    }

    refreshAfterKeys(fromTime)
    {
        this.evaluateStack();
        for (const node of this.scene.Particles) if (node.Stack) this.evaluateParticles(node);
        this.invalidateCheckpoints(fromTime);
        this.resimulateFrom(fromTime);
        this.notify();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FLIPBOOK BAKE
    //------------------------------------------------------------------------------------------------------------------------

    /**
     * Renders `Count` square frames of the simulation into one sprite sheet (a canvas), left-to-right, top-to-bottom, plus
     * the timing and UV metadata a game engine needs to play it back. Ported from the other fluid editor's FlipbookSequence:
     * same frame counts, tile sizes, rates, warm-up and straight-alpha transparent mode.
     *
     * Unlike that editor, which bakes in a second, isolated WebGL context, this bakes on the live solver: it is paused for
     * the bake, and the viewport carries on from the last baked frame. `Restart` begins the bake from an empty grid.
     */
    async bakeFlipbook(settings, onProgress = () => {}, isCancelled = () => false)
    {
        const layout = specifyFlipbook(settings.Count, settings.Size, settings.Rate, settings.Warmup);
        if (this.baking) throw new Error('A flipbook bake is already running.');
        this.baking = true;
        const wasPlaying = this.playing;
        const camera = this.camera;
        const savedViewport = [...camera.viewport];
        const savedAspect = camera.aspect;
        const ratio = Math.min(window.devicePixelRatio || 1, 1.5);
        const target = this.renderer.createBakeTarget(layout.Size);
        target.transparent = Boolean(settings.Transparent);
        const sheet = document.createElement('canvas');
        sheet.width = layout.Width;
        sheet.height = layout.Height;
        const context2d = sheet.getContext('2d');
        const readback = new Uint8Array(layout.Size * layout.Size * 4);
        const gl = this.context.gl;
        const deltaTime = this.fixedDeltaTime * this.scene.Domain.TimeScale;
        const yieldToBrowser = () => new Promise((resolve) => setTimeout(resolve, 0));
        const advanceTo = async (time) =>
        {
            let steps = 0;
            while (this.solver.time < time - deltaTime * 0.5)
            {
                if (isCancelled()) throw new Error('Bake cancelled.');
                this.advanceSimulation(deltaTime);
                if (++steps % 4 === 0) await yieldToBrowser();
            }
        };
        try
        {
            if (settings.Restart) { this.solver.reset(); this.particles.resetAll(); this.recentreGrid(); }
            // Square frames: the camera keeps its vertical field of view and frames the same subject as the viewport.
            camera.setViewport(layout.Size / ratio, layout.Size / ratio);
            const start = this.solver.time;
            if (layout.Warmup > 0) { onProgress(0, 'Warming up the simulation…'); await advanceTo(start + layout.Warmup); }
            const frames = [];
            const first = this.solver.time;
            for (let index = 0; index < layout.Count; index += 1)
            {
                await advanceTo(first + (index + 1) / layout.Rate);
                this.renderer.render(this.scene, camera, null, deltaTime, target);
                gl.bindFramebuffer(gl.FRAMEBUFFER, target.framebuffer);
                gl.readPixels(0, 0, layout.Size, layout.Size, gl.RGBA, gl.UNSIGNED_BYTE, readback);
                gl.bindFramebuffer(gl.FRAMEBUFFER, null);
                if (gl.isContextLost()) throw new Error('GPU bake interrupted. Lower the voxel resolution or tile size and retry.');
                const x = (index % layout.Columns) * layout.Size;
                const y = Math.floor(index / layout.Columns) * layout.Size;
                context2d.putImageData(new ImageData(unpackFrame(readback, layout.Size, target.transparent), layout.Size, layout.Size), x, y);
                frames.push({ index, x, y, width: layout.Size, height: layout.Size, simulationTime: Number(this.solver.time.toFixed(4)), uv: [x / layout.Width, y / layout.Height, layout.Size / layout.Width, layout.Size / layout.Height] });
                onProgress((index + 1) / layout.Count, `Baking frame ${index + 1} / ${layout.Count}`);
                await yieldToBrowser();
            }
            const name = this.scene.Name.replace(/[^a-z0-9 _-]/gi, '').trim().replace(/\s+/g, '') || 'Scene';
            const metadata = {
                format: 'frontier-fluid-flipbook', version: 1, name, image: `${name}.flipbook.png`,
                width: layout.Width, height: layout.Height, columns: layout.Columns, rows: layout.Rows,
                frameCount: layout.Count, framesPerSecond: layout.Rate, playbackDuration: layout.Count / layout.Rate,
                warmup: layout.Warmup, restarted: Boolean(settings.Restart),
                origin: 'top-left', uvEncoding: 'normalized x, y, width, height', simulationTimeUnits: 'seconds',
                frameOrder: 'left-to-right, top-to-bottom', colourSpace: 'srgb-tonemapped',
                alphaMode: target.transparent ? 'straight-opacity-and-emission-coverage' : 'opaque',
                camera: { position: camera.position, forward: camera.forward, up: camera.up, aspect: 1, fovY: camera.fovY },
                scene: JSON.parse(this.exportJson()),
                frames,
            };
            return { sheet, metadata, name };
        }
        finally
        {
            this.renderer.destroyBakeTarget(target);
            camera.viewport = savedViewport;
            camera.aspect = savedAspect;
            this.baking = false;
            this.playing = wasPlaying;
            this.lastFrameTime = 0;
            this.notify();
        }
    }

    reportMetrics(elapsed)
    {
        const average = this.frameTimes.reduce((sum, value) => sum + value, 0) / Math.max(1, this.frameTimes.length);
        const layout = this.solver.layout;
        this.onMetrics({
            fps: 1 / Math.max(average, 1e-4),
            frameMilliseconds: elapsed * 1000,
            stepMilliseconds: this.solver.statistics.stepMilliseconds,
            passes: this.solver.statistics.passes,
            simulationTime: this.solver.time,
            simulationFrame: this.solver.frame,
            cells: layout.cellCount,
            residentTiles: layout.residentTiles || 0,
            poolSize: layout.poolSize || 0,
            resolution: [layout.width, layout.height, layout.depth],
            textureSize: [layout.textureWidth, layout.textureHeight],
            forces: this.scene.Forces.filter((force) => force.Enabled).length,
            emitters: this.scene.Emitters.filter((emitter) => emitter.Enabled).length,
            particleSystems: this.particles.statistics.systems,
            particles: this.particles.statistics.particles,
            playing: this.playing,
        });
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  SCENE CONSTRUCTION
    //------------------------------------------------------------------------------------------------------------------------

    loadPreset(presetId, announce = true)
    {
        if (this.baking) return;
        const preset = ScenePresetById[presetId] || ScenePresets[0];
        // Presets are documents (Presets/*.toml): grid, emitters, force fields, colliders, particles and the gas layer stack.
        this.scene = buildSceneFromDocument(preset.Document);
        this.particles.resetAll();
        this.selectedLayer = null;
        this.seekTarget = null;
        if (this.checkpoints) this.checkpoints.destroy();
        this.evaluateStack();
        ensureSceneDirectory(this.scene);
        this.applyDomainResolution();
        this.applyKeys(0);
        this.authoredText = this.authoredSignature();
        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        this.selection = this.scene.Forces[0] ? this.scene.Forces[0].Id : 'domain';
        this.playing = true;
        if (announce) this.notify();
    }

    addPresetAsFolder(presetId)
    {
        if (this.baking || !this.scene) return null;
        const preset = ScenePresetById[presetId] || ScenePresets[0];
        const imported = buildSceneFromDocument(preset.Document);
        const simulation = appendImportedSimulation(this.scene, imported);
        this.particles.resetAll();
        this.selectedLayer = null;
        this.seekTarget = null;
        if (this.checkpoints) this.checkpoints.destroy();
        this.evaluateStack();
        this.applyDomainResolution();
        this.applyKeys(this.solver ? this.solver.time : 0);
        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        this.selection = simulation.Id;
        this.notify();
        return simulation;
    }

    activateSimulation(simulationId)
    {
        if (!this.scene) return;
        const domainChanged = activateSimulationFolder(this.scene, simulationId);
        if (domainChanged)
        {
            this.particles.resetAll();
            this.seekTarget = null;
            if (this.checkpoints) this.checkpoints.destroy();
            this.evaluateStack();
            this.applyDomainResolution();
            this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        }
        this.notify();
    }

    setEntityScope(entityId, scope, targetSimulationId = null)
    {
        if (!this.scene) return;
        const domainChanged = assignEntityScope(this.scene, entityId, scope, targetSimulationId);
        if (domainChanged)
        {
            this.evaluateStack();
            this.applyDomainResolution();
        }
        this.notify();
    }

    createFolder(parentFolderId = null, baseName = 'Collection')
    {
        if (!this.scene) return null;
        const folder = createCollectionFolder(this.scene, parentFolderId, baseName);
        this.selection = folder.Id;
        this.notify();
        return folder;
    }

    moveToFolder(entityId, folderId)
    {
        if (!this.scene) return;
        const domainChanged = moveEntityToFolder(this.scene, entityId, folderId);
        if (domainChanged)
        {
            this.evaluateStack();
            this.applyDomainResolution();
        }
        this.notify();
    }

    renameFolder(folderId, nextName)
    {
        if (this.scene && renameFolder(this.scene, folderId, nextName)) this.notify();
    }

    deleteFolder(folderId)
    {
        if (!this.scene) return false;
        const prevDomain = this.scene.Domain;
        if (deleteFolder(this.scene, folderId))
        {
            if (this.particles) this.particles.prune(this.scene);
            this.evaluateStack();
            if (this.scene.Domain !== prevDomain)
            {
                this.applyDomainResolution();
            }
            if (this.selection === folderId || !this.findNode(this.selection))
            {
                this.selection = this.scene.Particles[0]?.Id || this.scene.Forces[0]?.Id || 'domain';
            }
            this.notify();
            return true;
        }
        return false;
    }

    toggleFolderVisibility(folderId)
    {
        if (!this.scene) return;
        toggleFolderVisibility(this.scene, folderId);
        this.notify();
    }

    applyDomainResolution()
    {
        const domain = resolveDomainGrid(this.scene.Domain);
        if (domain.Grid === 'Infinite') this.solver.configureInfinite(domain.TileResolution, domain.CellSize, domain.TilePool);
        else this.solver.configure(domain.Resolution, domain.CellSize, gridWindowCentre(domain));
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  WORLD GRID
    //------------------------------------------------------------------------------------------------------------------------

    /** World position the sliding window keeps centred, or null when the grid is static. */
    gridFollowTarget()
    {
        const domain = this.scene.Domain;
        const time = this.solver.time;
        switch (domain.Follow)
        {
            case 'Emitter':
            {
                const emitter = this.scene.Emitters.find((node) => node.Id === domain.FollowId) || this.scene.Emitters.find((node) => node.Enabled) || this.scene.Emitters[0];
                return emitter ? emitterTransform(emitter, time).Position : null;
            }
            case 'Particles':
            {
                const node = (this.scene.Particles || []).find((system) => system.Id === domain.FollowId) || (this.scene.Particles || [])[0];
                return node ? node.Position : null;
            }
            case 'Camera': return this.camera.target;
            default: return null;
        }
    }

    /**
     * Sliding World Grid: when the follow target leaves the centre tile the window steps by whole tiles so the target's
     * tile becomes the centre again. Fields are shifted by an exact cell offset, so smoke already laid down keeps its
     * world position and keeps simulating — the gas is never clipped at the old box. Height only follows when enabled.
     */
    followGrid()
    {
        const domain = this.scene.Domain;
        if (domain.Grid === 'Infinite') return;
        const target = this.gridFollowTarget();
        if (!target) return;
        const tile = gridTileOf(domain, target);
        if (!domain.FollowHeight) tile[1] = domain.GridOrigin[1];
        const delta = tile.map((value, axis) => value - domain.GridOrigin[axis]);
        if (!delta.some((value) => value !== 0)) return;
        domain.GridOrigin = tile;
        const shift = delta.map((value, axis) => value * domain.TileCells[axis]);
        this.solver.relocate(shift, gridWindowCentre(domain));
        if (domain.Follow !== 'Camera' && this.cameraFollowsGrid) this.camera.target = Vec3.add(this.camera.target, shift.map((cells) => cells * domain.CellSize));
        this.notify();
    }

    recentreGrid()
    {
        const domain = this.scene.Domain;
        if (!domain.GridOrigin.some((value) => value !== 0)) return;
        domain.GridOrigin = [0, 0, 0];
        this.solver.relocate([0, 0, 0], gridWindowCentre(domain));
        this.solver.reset();
        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        this.notify();
    }

    notify()
    {
        if (this.scene) syncSceneDirectory(this.scene);
        // Any authored change since the last check makes every checkpoint stale (key edits invalidate precisely before they get here).
        if (this.scene && this.checkpoints)
        {
            const text = this.authoredSignature();
            if (text !== this.authoredText) { this.checkpoints.invalidateAfter(0); this.authoredText = text; }
        }
        this.onStateChange(this.snapshot());
    }

    snapshot()
    {
        return { scene: this.scene, selection: this.selection, playing: this.playing, node: this.findNode(this.selection), gas: this.gasEvaluation, selectedLayer: this.selectedLayer, autoKey: this.autoKey, time: this.solver ? this.solver.time : 0 };
    }

    findNode(id)
    {
        if (!this.scene || !id) return null;
        if (id === 'domain') return this.scene.Domain;
        for (const list of [this.scene.Emitters, this.scene.Forces, this.scene.Colliders, this.scene.Particles])
        {
            const node = list.find((candidate) => candidate.Id === id);
            if (node) return node;
        }
        for (const visualizer of Object.values(this.scene.Visualizers)) if (visualizer.Id === id) return visualizer;
        return null;
    }

    select(id)
    {
        if (this.scene && this.scene.Directory && typeof id === 'string')
        {
            const dir = this.scene.Directory;
            if (id.includes('::sim-'))
            {
                const [canonical, simPart] = id.split('::');
                if (dir.Simulations.some((sim) => sim.Id === simPart))
                {
                    if (activateSimulationFolder(this.scene, simPart))
                    {
                        this.particles.resetAll();
                        this.seekTarget = null;
                        if (this.checkpoints) this.checkpoints.destroy();
                        this.evaluateStack();
                        this.applyDomainResolution();
                        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
                    }
                    if (canonical === 'domain' || canonical.startsWith('vis-'))
                    {
                        this.selection = canonical;
                        this.notify();
                        return;
                    }
                }
            }
            else if (dir.Simulations.some((sim) => sim.Id === id) && dir.ActiveSimulationId !== id)
            {
                if (activateSimulationFolder(this.scene, id))
                {
                    this.particles.resetAll();
                    this.seekTarget = null;
                    if (this.checkpoints) this.checkpoints.destroy();
                    this.evaluateStack();
                    this.applyDomainResolution();
                    this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
                }
            }
            else
            {
                const assignment = dir.Assignments[id];
                if (assignment && assignment.Scope !== 'Global' && assignment.SimulationId && assignment.SimulationId !== dir.ActiveSimulationId)
                {
                    if (activateSimulationFolder(this.scene, assignment.SimulationId))
                    {
                        this.particles.resetAll();
                        this.seekTarget = null;
                        if (this.checkpoints) this.checkpoints.destroy();
                        this.evaluateStack();
                        this.applyDomainResolution();
                        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
                    }
                }
            }
        }
        this.selection = id;
        this.notify();
    }

    addForce(typeId)
    {
        const type = ForceTypeById[typeId];
        const existing = this.scene.Forces.filter((force) => force.Type === typeId).length;
        const half = this.solver.domainHalfExtent, centre = this.solver.domainCentre;
        const force = createForceGenerator(typeId, {
            Name: existing > 0 ? `${type.Name} ${existing + 1}` : type.Name,
            Position: [centre[0] + ((this.scene.Forces.length % 3) - 1) * half[0] * 0.35, centre[1] + (Math.random() - 0.5) * half[1] * 0.4, centre[2]],
        });
        this.scene.Forces.push(force);
        this.select(force.Id);
        return force;
    }

    addEmitter(shape = 'Sphere')
    {
        const half = this.solver.domainHalfExtent, centre = this.solver.domainCentre;
        const emitter = createEmitter({ Name: `${shape} Emitter`, Shape: shape, Position: [centre[0], centre[1] - half[1] * 0.8, centre[2]] });
        this.scene.Emitters.push(emitter);
        this.linkSource(emitter);
        this.select(emitter.Id);
        return emitter;
    }

    addParticles(templateId)
    {
        const template = ParticleTemplateById[templateId];
        if (!template) return null;
        const existing = this.scene.Particles.filter((node) => node.Template === templateId).length;
        const half = this.solver.domainHalfExtent, centre = this.solver.domainCentre;
        const centredShapes = new Set(['MocapFigure', 'SurfaceShell', 'Torus', 'Dome', 'Sphere', 'Grid']);
        const placeAtCentre = template.Defaults.Mode === 'Cloner' || centredShapes.has(template.Defaults.Shape);
        const node = createParticleSystem(templateId, {
            Name: existing > 0 ? `${template.Name} ${existing + 1}` : template.Name,
            Position: placeAtCentre ? [...centre] : [centre[0], centre[1] + (template.Defaults.Direction && template.Defaults.Direction[1] < 0 ? half[1] * 0.9 : -half[1] * 0.85), centre[2]],
        });
        // Never auto-enable diagnostic overlays or force gizmos when spawning particles.
        if (this.scene.Visualizers && this.scene.Visualizers.ForceGizmos) this.scene.Visualizers.ForceGizmos.Visible = false;
        this.scene.Particles.push(node);
        this.select(node.Id);
        return node;
    }

    replaceParticleTemplate(id, templateId)
    {
        const template = ParticleTemplateById[templateId];
        if (!template || !this.scene) return null;
        const index = this.scene.Particles.findIndex((node) => node.Id === id);
        if (index < 0) return this.addParticles(templateId);
        const current = this.scene.Particles[index];
        const existing = this.scene.Particles.filter((node) => node.Id !== id && node.Template === templateId).length;
        const replacement = createParticleSystem(templateId, {
            Name: existing > 0 ? `${template.Name} ${existing + 1}` : template.Name,
            Position: [...current.Position],
            Enabled: current.Enabled,
            Visible: current.Visible,
        });
        replacement.Id = current.Id;
        this.scene.Particles[index] = replacement;
        if (this.particles) this.particles.reset(replacement.Id);
        this.selectedLayer = null;
        this.select(replacement.Id);
        return replacement;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                        CUSTOM 3D OBJECT IMPORT (mesh → node → GPU)
    //------------------------------------------------------------------------------------------------------------------------

    /**
     * Registers an imported .obj / .stl / .facet / .json mesh and points the selected particle system at it.
     * target = 'spawn' updates only Spawn (201), 'carrier' updates only Sprite & Object (211), and 'both' updates both layers.
     */
    importCustomObject(fileName, bufferOrText, particleId = null, target = 'both')
    {
        const entry = ImportCustomObject(fileName, bufferOrText);
        if (!entry) return null;
        const node = this.scene && (this.scene.Particles.find((candidate) => candidate.Id === (particleId || this.selection)) || this.scene.Particles[0]);
        if (!node) return entry;
        this.setCustomObject(entry.Id, node.Id, target);
        return entry;
    }

    /** Points a particle system's Spawn, Sprite & Object carrier, or both at a registered custom mesh. */
    setCustomObject(id, particleId = null, target = 'both')
    {
        const node = this.scene && (this.scene.Particles.find((candidate) => candidate.Id === (particleId || this.selection)) || this.scene.Particles[0]);
        if (!node) return null;
        const customCode = Math.max(0, ObjectNames.indexOf('CustomObject'));
        const resolvedId = typeof id === 'string' ? id : '';
        const spawn = node.Stack.Layers.find((layer) => layer.Category === 201);
        const carrier = node.Stack.Layers.find((layer) => layer.Category === 211);
        if (spawn && (target === 'spawn' || target === 'both'))
        {
            spawn.Parameters.CustomObjectId = resolvedId;
            spawn.Parameters.SpawnObject = customCode;
            if (target === 'spawn') spawn.Parameters.Shape = 11;
        }
        if (carrier && (target === 'carrier' || target === 'both'))
        {
            carrier.Parameters.CarrierCustomObjectId = resolvedId;
            carrier.Parameters.CarrierObject = customCode;
            if (target === 'carrier') carrier.Parameters.CarrierMode = 1;
        }
        this.evaluateOwner(node);
        this.invalidateCheckpoints();
        this.notify();
        return CustomObjectEntry(resolvedId);
    }

    addCollider()
    {
        const collider = createCollider({ Name: `Sphere Collider ${this.scene.Colliders.length + 1}`, Position: [0, 0, 0] });
        this.scene.Colliders.push(collider);
        this.select(collider.Id);
        return collider;
    }

    removeNode(id)
    {
        if (!this.scene || !id || id === 'domain') return false;
        for (const key of ['Emitters', 'Forces', 'Colliders', 'Particles'])
        {
            const index = this.scene[key].findIndex((node) => node.Id === id);
            if (index >= 0)
            {
                const [removed] = this.scene[key].splice(index, 1);
                if (this.scene.Directory && this.scene.Directory.Assignments) delete this.scene.Directory.Assignments[id];
                if (key === 'Emitters')
                {
                    this.scene.GasStack.Layers = this.scene.GasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && layer.Parameters.Emitter === id));
                    this.evaluateStack();
                }
                if (key === 'Particles')
                {
                    if (this.particles) this.particles.prune(this.scene);
                    if (removed && removed.Stack && removed.Stack.Layers.some((layer) => layer.Token === this.selectedLayer)) this.selectedLayer = null;
                }
                if (this.selection === id)
                {
                    this.selection = this.scene.Particles[0]?.Id || this.scene.Forces[0]?.Id || this.scene.Emitters[0]?.Id || 'domain';
                }
                this.notify();
                return true;
            }
        }
        // Also support deleting an entity stored inside an inactive simulation folder in scene.Directory
        if (this.scene.Directory)
        {
            const dir = this.scene.Directory;
            let removedFromInactive = false;
            for (const sim of dir.Simulations || [])
            {
                for (const localKey of ['LocalEmitters', 'LocalForces', 'LocalColliders', 'LocalParticles'])
                {
                    const list = sim[localKey] || [];
                    const idx = list.findIndex((node) => node.Id === id);
                    if (idx >= 0)
                    {
                        list.splice(idx, 1);
                        if (localKey === 'LocalEmitters' && sim.GasStack)
                        {
                            sim.GasStack.Layers = sim.GasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && layer.Parameters.Emitter === id));
                        }
                        removedFromInactive = true;
                    }
                }
            }
            if (removedFromInactive)
            {
                delete dir.Assignments[id];
                if (this.selection === id) this.selection = 'domain';
                this.notify();
                return true;
            }
        }
        return false;
    }

    removeSelection()
    {
        if (!this.selection || this.selection === 'domain') return false;
        if (this.removeNode(this.selection)) return true;
        return this.deleteFolder(this.selection);
    }

    duplicateNode(id)
    {
        const node = this.findNode(id);
        if (!node || node.Kind === 'Domain' || node.Kind === 'Visualizer') return null;
        let copy;
        if (node.Kind === 'Force') { copy = createForceGenerator(node.Type, { ...node, Name: node.Name + ' Copy' }); this.scene.Forces.push(copy); }
        else if (node.Kind === 'Emitter') { copy = createEmitter({ ...node, Name: node.Name + ' Copy' }); this.scene.Emitters.push(copy); this.linkSource(copy); }
        else if (node.Kind === 'Particles') { copy = createParticleSystem(node.Template, { ...node, Name: node.Name + ' Copy' }); this.scene.Particles.push(copy); }
        else { copy = createCollider({ ...node, Name: node.Name + ' Copy' }); this.scene.Colliders.push(copy); }
        this.placeNode(copy, [node.Position[0] + 0.4, node.Position[1], node.Position[2]]);
        this.select(copy.Id);
        return copy;
    }

    updateNode(id, key, value)
    {
        const node = this.findNode(id);
        if (!node) return;
        if (this.routeToLayer(node, key, value)) { this.afterNodeUpdate(node, key); this.notify(); return; }
        // Keyed force / emitter fields take a key at the playhead instead (phase 4); timing edits hand Weight back to the timing fields.
        if ((node.Kind === 'Force' || node.Kind === 'Emitter') && isKeyableField(node, key) && this.writeKeyed({ Kind: 'Node', Id: node.Id, Name: key }, value)) return;
        if (TimingFields.includes(key) && findTrack(node.Tracks, 'Weight')) node.Tracks = node.Tracks.filter((track) => track.Path !== 'Weight');
        node[key] = value;
        this.afterNodeUpdate(node, key);
        this.notify();
    }

    applyLook(id, look)
    {
        const node = this.findNode(id);
        if (!node) return;
        for (const [key, value] of Object.entries(look)) if (!this.routeToLayer(node, key, value)) node[key] = value;
        this.notify();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  GAS LAYER STACK
    //------------------------------------------------------------------------------------------------------------------------

    /** Re-folds the gas stack into scene.Domain / Visualizers.Volume. Called after every stack edit (phase 4 adds per-step keys). */
    evaluateStack()
    {
        const previousAdvection = this.scene.Domain.Advection;
        this.gasEvaluation = evaluateGasStack(this.scene.GasStack, this.scene.Emitters, this.solver ? this.solver.time : 0);
        applyGasEvaluation(this.scene, this.gasEvaluation);
        return previousAdvection !== this.scene.Domain.Advection;
    }

    /**
     * Inspector edits of a flat field the stack owns (Domain dynamics, Volume Render shading, emitter Enabled) are written into the
     * owning layer, so every existing control keeps working and the stack stays the single source. Returns true when routed.
     */
    routeToLayer(node, key, value)
    {
        if (node.Kind === 'Particles') return this.routeToParticleLayer(node, key, value);
        const stack = this.scene && this.scene.GasStack;
        if (!stack) return false;
        if (node.Kind === 'Emitter' && key === 'Enabled')
        {
            const layer = stack.Layers.find((candidate) => candidate.Category === SourceLayerCode && candidate.Parameters.Emitter === node.Id);
            if (!layer) return false;
            layer.Enabled = Boolean(value);
            this.evaluateStack();
            return true;
        }
        const target = node.Kind === 'Domain' ? 'Domain' : node.Id === 'vis-volume' ? 'Volume' : null;
        const found = target && owningLayer(stack, target, key);
        if (!found) return false;
        let layer = found.Layer;
        if (!layer)
        {
            // No layer of this category yet: editing its field adds one, placed by phase order.
            layer = this.insertLayer(found.Owner.Layer.Code);
            if (!layer) return false;
        }
        if (!layer.Enabled) layer.Enabled = true;
        if (isKeyableParameter(found.Owner.Parameter) && this.writeKeyed({ Kind: 'Layer', Token: layer.Token, Name: key }, coerceAmount(found.Owner.Parameter, value))) return true;
        layer.Parameters[key] = coerceAmount(found.Owner.Parameter, value);
        this.evaluateStack();
        return true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  PARTICLE LAYER STACKS
    //------------------------------------------------------------------------------------------------------------------------

    /** Folds a particle node's stack into its flat fields; rebuilds the GPU state when the formation (mode, shape, count) changed. */
    evaluateParticles(node, time = this.solver ? this.solver.time : 0)
    {
        const before = `${node.Mode}|${node.Shape}|${node.Count}`;
        const evaluation = evaluateParticleSystem(node, time);
        if (`${node.Mode}|${node.Shape}|${node.Count}` !== before) this.particles.reset(node.Id);
        return evaluation;
    }

    /** Evaluation of a particle node's stack for the Inspector (problems, contributing layers). Pure; nothing is applied. */
    particleEvaluation(node) { return evaluateParticleStack(node.Stack, this.solver ? this.solver.time : undefined); }

    /** Flat particle edits (gizmo drag, status tiles) write into the owning layer; a missing layer is added in phase order. */
    routeToParticleLayer(node, key, value)
    {
        if (!node.Stack) return false;
        const found = owningLayer(node.Stack, 'Particles', key);
        if (!found) return false;
        let layer = found.Layer;
        if (!layer)
        {
            // The new layer starts at the node's current (neutral) amounts, so only the edited field moves.
            const current = Object.fromEntries(found.Owner.Layer.Parameter.map((parameter) => [parameter.Name, parameter.Type === 'Choice' ? (parameter.Options.find((option) => option.Token === node[parameter.Name]) || parameter.Options[0]).Code : node[parameter.Name]]));
            layer = insertLayerInPhaseOrder(node.Stack, found.Owner.Layer.Code, { Parameters: current });
            if (!layer) return false;
        }
        if (!layer.Enabled) layer.Enabled = true;
        if (isKeyableParameter(found.Owner.Parameter) && this.writeKeyed({ Kind: 'Layer', Token: layer.Token, Name: key }, coerceAmount(found.Owner.Parameter, value))) return true;
        layer.Parameters[key] = coerceAmount(found.Owner.Parameter, value);
        this.evaluateParticles(node);
        return true;
    }

    /** Moves a node; a particle system's position lives in its Spawn layer. */
    placeNode(node, position)
    {
        if (node.Kind === 'Particles' && this.routeToParticleLayer(node, 'Position', position)) return;
        if ((node.Kind === 'Force' || node.Kind === 'Emitter') && this.writeKeyed({ Kind: 'Node', Id: node.Id, Name: 'Position' }, position)) return;
        node.Position = position;
    }

    /** The stack holding a layer token, and the particle node that owns it (null for the gas stack). */
    stackOfLayer(token)
    {
        if (this.scene.GasStack && this.scene.GasStack.Layers.some((layer) => layer.Token === token)) return { stack: this.scene.GasStack, node: null };
        for (const node of this.scene.Particles || []) if (node.Stack && node.Stack.Layers.some((layer) => layer.Token === token)) return { stack: node.Stack, node };
        return { stack: null, node: null };
    }

    /** Re-evaluates whichever stack a layer edit touched. Returns true when the gas advection scheme changed. */
    evaluateOwner(node)
    {
        if (node) { this.evaluateParticles(node); return false; }
        return this.evaluateStack();
    }

    linkSource(emitter)
    {
        const stack = this.scene.GasStack;
        if (!stack) return;
        const layer = createLinkedSource(emitter);
        const lastSource = stack.Layers.map((candidate) => candidate.Category).lastIndexOf(SourceLayerCode);
        stack.Layers.splice(lastSource >= 0 ? lastSource + 1 : Math.min(1, stack.Layers.length), 0, layer);
        this.evaluateStack();
    }

    /** Inserts a new layer after the last layer of the same or an earlier phase; returns null at capacity. */
    insertLayer(categoryCode, stack = this.scene.GasStack)
    {
        return insertLayerInPhaseOrder(stack, categoryCode);
    }

    addLayer(categoryCode)
    {
        if (categoryCode === SourceLayerCode) { this.addEmitter('Sphere'); return null; }
        const specification = LayerSpecificationByCode[categoryCode];
        // Particle layers go into the selected particle system's stack.
        const node = specification && specification.Stack === 'ParticleCloud' ? this.findNode(this.selection) : null;
        if (specification && specification.Stack === 'ParticleCloud' && (!node || node.Kind !== 'Particles')) return null;
        const layer = this.insertLayer(categoryCode, node ? node.Stack : this.scene.GasStack);
        if (!layer) return null;
        this.selectedLayer = layer.Token;
        this.evaluateOwner(node);
        this.notify();
        return layer;
    }

    removeLayer(token)
    {
        const { stack, node } = this.stackOfLayer(token);
        if (!stack) return false;
        const layer = stack.Layers.find((candidate) => candidate.Token === token);
        const specification = LayerSpecificationByCode[layer.Category];
        if (specification && specification.Required) return false;
        if (specification && specification.Code === SourceLayerCode) return this.removeNode(layer.Parameters.Emitter);
        stack.Layers.splice(stack.Layers.indexOf(layer), 1);
        if (stack.Tracks) stack.Tracks = stack.Tracks.filter((track) => !track.Path.startsWith(`${token}/`));
        if (this.selectedLayer === token) this.selectedLayer = null;
        this.evaluateOwner(node);
        this.notify();
        return true;
    }

    /** Moves a layer to `index` (drag) — the required Domain layer stays first. */
    moveLayer(token, index)
    {
        const { stack, node } = this.stackOfLayer(token);
        if (!stack) return;
        const layers = stack.Layers;
        const from = layers.findIndex((candidate) => candidate.Token === token);
        const specification = LayerSpecificationByCode[layers[from].Category];
        if (specification && specification.Required) return;
        const [layer] = layers.splice(from, 1);
        const firstFree = layers.findIndex((candidate) => !(LayerSpecificationByCode[candidate.Category] || {}).Required);
        layers.splice(Math.max(firstFree < 0 ? layers.length : firstFree, Math.min(layers.length, index)), 0, layer);
        this.evaluateOwner(node);
        this.notify();
    }

    selectLayer(token)
    {
        this.selectedLayer = token;
        const { stack } = this.stackOfLayer(token);
        const layer = stack && stack.Layers.find((candidate) => candidate.Token === token);
        if (layer && layer.Category === SourceLayerCode && this.findNode(layer.Parameters.Emitter)) { this.select(layer.Parameters.Emitter); return; }
        this.notify();
    }

    /** Edits a layer's own settings (Enabled, Solo, Weight, Blend) or, with `parameter`, one of its parameters. */
    updateLayer(token, key, value, parameter = false)
    {
        const { stack, node } = this.stackOfLayer(token);
        const layer = stack && stack.Layers.find((candidate) => candidate.Token === token);
        if (!layer) return;
        const specification = LayerSpecificationByCode[layer.Category];
        if (parameter)
        {
            const entry = specification && specification.Parameter.find((candidate) => candidate.Name === key);
            if (!entry) return;
            if (isKeyableParameter(entry) && this.writeKeyed({ Kind: 'Layer', Token: token, Name: key }, coerceAmount(entry, value))) return;
            layer.Parameters[key] = coerceAmount(entry, value);
        }
        else if (key === 'Weight' && !(specification && (specification.Required || specification.Linked)) && this.writeKeyed({ Kind: 'Layer', Token: token, Name: 'Weight' }, Math.min(1, Math.max(0, Number(value) || 0)))) return;
        else if (key === 'Weight') layer.Weight = Math.min(1, Math.max(0, Number(value) || 0));
        else if (key === 'Enabled' || key === 'Solo') layer[key] = Boolean(value);
        else if (key === 'Blend') layer.Blend = [0, 1, 2, 3].includes(value) ? value : 0;
        else return;
        if (specification && specification.Required && key === 'Enabled') layer.Enabled = true;
        const advectionChanged = this.evaluateOwner(node);
        if (advectionChanged) this.afterNodeUpdate(this.scene.Domain, 'Advection');
        this.notify();
    }

    afterNodeUpdate(node, key)
    {
        if (node.Kind === 'Domain' && key === 'Follow') this.followGrid();
        if (node.Kind === 'Domain' && key === 'Grid')
        {
            // The fixed box floor sits at −Size.y/2; the infinite grid's ground is y = 0. Lift / lower every node so the scene
            // keeps standing on the floor when the mode flips.
            const lift = node.Size[1] * 0.5 * (node.Grid === 'Infinite' ? 1 : -1);
            for (const list of [this.scene.Emitters, this.scene.Forces, this.scene.Colliders, this.scene.Particles || []]) for (const item of list) this.placeNode(item, [item.Position[0], item.Position[1] + lift, item.Position[2]]);
            this.applyDomainResolution();
            this.solver.reset();
            this.particles.resetAll();
            this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        }
        if (node.Kind === 'Domain' && (key === 'TileSize' || key === 'TileResolution' || key === 'TilePool'))
        {
            this.applyDomainResolution();
        }
        if (node.Kind === 'Domain' && (key === 'Size' || key === 'MaxCells' || key === 'Tiles'))
        {
            node.Size = node.Size.map((extent) => Math.max(0.5, Math.min(20, extent)));
            this.applyDomainResolution();
            this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
        }
        if (node.Kind === 'Visualizer' && key === 'Lifetime' && node.Id === 'vis-tracers') this.solver.tracersNeedReset = true;
        if (node.Kind === 'Visualizer' && key === 'Lifetime' && node.Id === 'vis-debris') this.solver.debrisNeedReset = true;
        if (node.Kind === 'Particles' && (key === 'Mode' || key === 'Shape' || key === 'Count')) this.particles.reset(node.Id);
        if ((node.Kind === 'Force' || node.Kind === 'Emitter' || node.Kind === 'Particles') && (key === 'StartTime' || key === 'Duration' || key === 'Repeat') && this.solver.time > (node.StartTime || 0) + (node.Duration || 0) + 0.5) this.solver.reset();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  TRANSPORT
    //------------------------------------------------------------------------------------------------------------------------

    play() { this.playing = true; this.notify(); }
    pause() { this.playing = false; this.notify(); }
    togglePlay() { this.playing = !this.playing; this.notify(); }
    stepOnce() { this.playing = false; this.stepRequested = true; this.notify(); }
    reset() { this.seekTarget = null; this.resimulate = false; this.resumeAfterSeek = false; this.restartTimeline(); this.notify(); }
    frameDomain() { this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre); }

    setCameraPreset(name)
    {
        const presets = { threeQuarter: [-0.65, 0.32], front: [0, 0.08], side: [Math.PI / 2, 0.08], top: [-0.65, 1.35] };
        const [yaw, pitch] = presets[name] || presets.threeQuarter;
        this.camera.yaw = yaw;
        this.camera.pitch = pitch;
        this.camera.frame(this.solver.domainHalfExtent, this.solver.domainCentre);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  SERIALISATION
    //------------------------------------------------------------------------------------------------------------------------

    exportJson()
    {
        if (this.scene) syncSceneDirectory(this.scene);
        const { Domain, Emitters, Forces, Colliders, Particles, Visualizers, Name, GasStack, Directory } = this.scene;
        // Version 3 adds the gas layer stack, Version 4 a layer stack per particle system (Particles[n].Stack), Version 5 keyed tracks
        // (GasStack.Tracks, Stack.Tracks, node Tracks) and the Timeline. Flat fields still hold the evaluated amounts so older readers keep working.
        return JSON.stringify({
            Format: 'FrontierFluidScene',
            Version: 5,
            SpecificationHash,
            Name,
            Timeline: this.timeline(),
            Domain,
            Emitters,
            Forces,
            Colliders,
            Particles,
            Visualizers,
            GasStack,
            ...(Directory ? { Directory: serializeSceneDirectory(this.scene) } : {}),
        }, null, 2);
    }

    /** Current scene as a `frontier-fluid-preset` TOML document (Save as preset). Add it to Presets/ and run GeneratePresetLibrary.mjs. */
    exportPresetToml(summary = {})
    {
        const document = sceneToDocument(this.scene, summary);
        return `# ${document.Scene.Name} — Fluid Studio scene preset (saved from the editor).\n\n` + writeToml(document, FloatKeys);
    }

    importJson(text)
    {
        const data = JSON.parse(text);
        if (data.Format !== 'FrontierFluidScene') throw new Error('Not a FrontierFluidScene document.');
        const domain = createDomain(data.Domain);
        const remappedIds = new Map();
        const mapList = (rawList, factory) => (rawList || []).map((raw) =>
        {
            const created = factory(raw);
            if (raw && raw.Id) remappedIds.set(raw.Id, created.Id);
            return created;
        });
        this.scene = {
            Name: data.Name || 'Imported Scene',
            PresetId: null,
            Domain: domain,
            Emitters: mapList(data.Emitters, (emitter) => createEmitter(emitter)),
            Forces: mapList(data.Forces, (force) => createForceGenerator(force.Type, force)),
            Colliders: mapList(data.Colliders, (collider) => createCollider(collider)),
            Particles: mapList(data.Particles, (node) => createParticleSystem(node.Template, node)),
            Visualizers: Object.fromEntries(Object.entries(createVisualizerSet()).map(([key, defaults]) => [key, { ...defaults, ...((data.Visualizers || {})[key] || {}) }])),
        };
        // Version 2 files have no stack: convert their flat fields exactly. Stack links follow emitter ids only when they still exist.
        this.scene.GasStack = data.GasStack ? readGasStack(data.GasStack) : convertSceneToGasStack(this.scene);
        if (data.GasStack)
        {
            for (const layer of this.scene.GasStack.Layers) if (layer.Category === SourceLayerCode && remappedIds.get(layer.Parameters.Emitter)) layer.Parameters.Emitter = remappedIds.get(layer.Parameters.Emitter);
            const linked = new Set(this.scene.GasStack.Layers.filter((layer) => layer.Category === SourceLayerCode).map((layer) => layer.Parameters.Emitter));
            for (const emitter of this.scene.Emitters) if (!linked.has(emitter.Id)) this.scene.GasStack.Layers.splice(1, 0, createLinkedSource(emitter));
        }
        if (data.Timeline && data.Timeline.End > 0) this.scene.Timeline = { End: data.Timeline.End, Loop: Boolean(data.Timeline.Loop) };
        this.selectedLayer = null;
        this.seekTarget = null;
        if (this.checkpoints) this.checkpoints.destroy();
        this.evaluateStack();
        const dir = ensureSceneDirectory(this.scene);
        if (data.Directory)
        {
            const savedDir = data.Directory;
            if (Array.isArray(savedDir.Simulations) && savedDir.Simulations.length > 0)
            {
                const activeSavedId = savedDir.ActiveSimulationId || savedDir.Simulations[0].Id;
                dir.ActiveSimulationId = activeSavedId;
                dir.Counter = savedDir.Counter || savedDir.Simulations.length + 1;
                dir.DomainScope = savedDir.DomainScope === 'Global' ? 'Global' : 'Local';
                dir.OverlayScope = { ...dir.OverlayScope, ...(savedDir.OverlayScope || {}) };
                dir.CustomFolders = Array.isArray(savedDir.CustomFolders) ? savedDir.CustomFolders : [];
                dir.Hidden = savedDir.Hidden || {};

                // Remap assignments from saved IDs to newly created runtime IDs
                const nextAssignments = {};
                const rawAssignments = savedDir.Assignments || {};
                const activeByNewId = new Map([
                    ...this.scene.Emitters, ...this.scene.Forces, ...this.scene.Colliders, ...this.scene.Particles,
                ].map((item) => [item.Id, item]));

                dir.GlobalEmitters = [];
                dir.GlobalForces = [];
                dir.GlobalColliders = [];
                dir.GlobalParticles = [];
                if (dir.DomainScope === 'Global')
                {
                    dir.GlobalDomain = this.scene.Domain;
                    dir.GlobalGasStack = this.scene.GasStack;
                }

                dir.Simulations = savedDir.Simulations.map((rawSim) =>
                {
                    if (rawSim.Id === activeSavedId)
                    {
                        const localEmitters = [], localForces = [], localColliders = [], localParticles = [];
                        for (const [oldId, newId] of remappedIds.entries())
                        {
                            const item = activeByNewId.get(newId);
                            if (!item) continue;
                            const spec = rawAssignments[oldId] || { Scope: 'Local', SimulationId: activeSavedId, CustomFolderId: null };
                            nextAssignments[newId] = { ...spec };
                            const targetBucket = spec.Scope === 'Global'
                                ? { Emitter: dir.GlobalEmitters, Force: dir.GlobalForces, Collider: dir.GlobalColliders, Particles: dir.GlobalParticles }[item.Kind]
                                : { Emitter: localEmitters, Force: localForces, Collider: localColliders, Particles: localParticles }[item.Kind];
                            if (targetBucket && !targetBucket.includes(item)) targetBucket.push(item);
                        }
                        return {
                            Id: rawSim.Id,
                            Name: rawSim.Name,
                            PresetId: rawSim.PresetId || '',
                            Notes: rawSim.Notes || '',
                            Tint: rawSim.Tint || '#a2b87c',
                            Domain: rawSim.Domain ? createDomain(rawSim.Domain) : this.scene.Domain,
                            GasStack: rawSim.GasStack ? readGasStack(rawSim.GasStack) : this.scene.GasStack,
                            Visualizers: Object.fromEntries(Object.entries(createVisualizerSet()).map(([k, d]) => [k, { ...d, ...((rawSim.Visualizers || {})[k] || {}) }])),
                            LocalEmitters: localEmitters,
                            LocalForces: localForces,
                            LocalColliders: localColliders,
                            LocalParticles: localParticles,
                        };
                    }
                    const simRemap = new Map();
                    const simEmitters = (rawSim.LocalEmitters || []).map((e) => { const c = createEmitter(e); if (e.Id) simRemap.set(e.Id, c.Id); nextAssignments[c.Id] = { ...(rawAssignments[e.Id] || { Scope: 'Local', SimulationId: rawSim.Id, CustomFolderId: null }) }; return c; });
                    const simForces = (rawSim.LocalForces || []).map((f) => { const c = createForceGenerator(f.Type, f); if (f.Id) simRemap.set(f.Id, c.Id); nextAssignments[c.Id] = { ...(rawAssignments[f.Id] || { Scope: 'Local', SimulationId: rawSim.Id, CustomFolderId: null }) }; return c; });
                    const simColliders = (rawSim.LocalColliders || []).map((col) => { const c = createCollider(col); if (col.Id) simRemap.set(col.Id, c.Id); nextAssignments[c.Id] = { ...(rawAssignments[col.Id] || { Scope: 'Local', SimulationId: rawSim.Id, CustomFolderId: null }) }; return c; });
                    const simParticles = (rawSim.LocalParticles || []).map((p) => { const c = createParticleSystem(p.Template, p); if (p.Id) simRemap.set(p.Id, c.Id); nextAssignments[c.Id] = { ...(rawAssignments[p.Id] || { Scope: 'Local', SimulationId: rawSim.Id, CustomFolderId: null }) }; return c; });
                    const simDomain = createDomain(rawSim.Domain || data.Domain);
                    const simStack = rawSim.GasStack ? readGasStack(rawSim.GasStack) : convertSceneToGasStack({ Domain: simDomain, Emitters: simEmitters, Visualizers: rawSim.Visualizers || createVisualizerSet() });
                    for (const layer of simStack.Layers) if (layer.Category === SourceLayerCode && simRemap.get(layer.Parameters.Emitter)) layer.Parameters.Emitter = simRemap.get(layer.Parameters.Emitter);
                    return {
                        Id: rawSim.Id,
                        Name: rawSim.Name,
                        PresetId: rawSim.PresetId || '',
                        Notes: rawSim.Notes || '',
                        Tint: rawSim.Tint || '#77b4bf',
                        Domain: simDomain,
                        GasStack: simStack,
                        Visualizers: Object.fromEntries(Object.entries(createVisualizerSet()).map(([k, d]) => [k, { ...d, ...((rawSim.Visualizers || {})[k] || {}) }])),
                        LocalEmitters: simEmitters,
                        LocalForces: simForces,
                        LocalColliders: simColliders,
                        LocalParticles: simParticles,
                    };
                });
                dir.Assignments = nextAssignments;
                for (const key of Object.keys(this.scene.Visualizers))
                {
                    if (dir.OverlayScope[this.scene.Visualizers[key].Id] === 'Global')
                    {
                        dir.GlobalVisualizers[key] = JSON.parse(JSON.stringify(this.scene.Visualizers[key]));
                    }
                }
                recomposeSceneFromDirectory(this.scene);
            }
        }
        this.applyDomainResolution();
        this.applyKeys(0);
        this.selection = 'domain';
        this.notify();
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  VIEWPORT INPUT
    //------------------------------------------------------------------------------------------------------------------------

    attachInput()
    {
        const canvas = this.canvas;
        canvas.addEventListener('contextmenu', (event) => event.preventDefault());
        canvas.addEventListener('pointerdown', (event) => this.onPointerDown(event));
        window.addEventListener('pointermove', (event) => this.onPointerMove(event));
        window.addEventListener('pointerup', (event) => this.onPointerUp(event));
        canvas.addEventListener('wheel', (event) => { event.preventDefault(); this.camera.dolly(event.deltaY); }, { passive: false });
        window.addEventListener('keydown', (event) =>
        {
            if (event.target && (event.target.tagName === 'INPUT' || event.target.tagName === 'SELECT' || event.target.tagName === 'TEXTAREA' || event.target.isContentEditable)) return;
            if (event.code === 'Space') { event.preventDefault(); this.togglePlay(); }
            else if (event.key === 'f' || event.key === 'F') this.frameDomain();
            else if (event.key === 'r' || event.key === 'R') this.reset();
            else if (event.key === 'Delete' || event.key === 'Backspace') { if (this.removeSelection()) event.preventDefault(); }
            else if (event.key === '.') this.stepOnce();
        });
    }

    localPointer(event)
    {
        const rect = this.canvas.getBoundingClientRect();
        return [event.clientX - rect.left, event.clientY - rect.top];
    }

    pickableNodes()
    {
        if (!this.scene.Visualizers.ForceGizmos.Visible) return [];
        const nodes = [];
        const show = (node) => node.Visible;
        for (const force of this.scene.Forces) if (show(force)) nodes.push(force);
        for (const emitter of this.scene.Emitters) if (show(emitter)) nodes.push(emitter);
        for (const node of this.scene.Particles) if (show(node)) nodes.push(node);
        for (const collider of this.scene.Colliders) if (collider.Visible) nodes.push(collider);
        return nodes;
    }

    pick(x, y)
    {
        if (!this.lastMatrices) return null;
        let best = null, bestDistance = 26;
        for (const node of this.pickableNodes())
        {
            const keyed = keyedNode(node, this.solver.time);
            const screen = this.camera.project(this.lastMatrices.viewProjection, node.Kind === 'Emitter' ? emitterTransform(keyed, this.solver.time).Position : keyed.Position);
            if (!screen) continue;
            const distance = Math.hypot(screen[0] - x, screen[1] - y);
            if (distance < bestDistance) { best = node; bestDistance = distance; }
        }
        return best;
    }

    onPointerDown(event)
    {
        const [x, y] = this.localPointer(event);
        this.canvas.setPointerCapture(event.pointerId);
        this.pointer.x = x; this.pointer.y = y; this.pointer.moved = false;
        if (event.button === 0)
        {
            const hit = this.pick(x, y);
            if (hit)
            {
                this.pointer.mode = 'drag';
                this.pointer.dragNode = hit;
                this.pointer.dragStart = [...((hit.Kind === 'Force' || hit.Kind === 'Emitter') ? keyedNode(hit, this.solver.time).Position : hit.Position)];
                this.select(hit.Id);
                return;
            }
            this.pointer.mode = 'orbit';
        }
        else this.pointer.mode = 'pan';
    }

    onPointerMove(event)
    {
        const [x, y] = this.localPointer(event);
        const dx = x - this.pointer.x, dy = y - this.pointer.y;
        if (!this.pointer.mode)
        {
            const hit = this.pick(x, y);
            this.canvas.style.cursor = hit ? 'grab' : 'default';
            return;
        }
        if (Math.abs(dx) + Math.abs(dy) > 0) this.pointer.moved = true;
        if (this.pointer.mode === 'orbit') this.camera.orbit(dx, dy);
        else if (this.pointer.mode === 'pan') this.camera.pan(dx, dy);
        else if (this.pointer.mode === 'drag' && this.lastMatrices)
        {
            // Move the node on the camera-facing plane through its original position.
            const node = this.pointer.dragNode;
            const normal = Vec3.scale(this.camera.forward, -1);
            const ray = this.camera.rayFromScreen(this.lastMatrices.inverseViewProjection, x, y);
            const startRay = this.camera.rayFromScreen(this.lastMatrices.inverseViewProjection, this.pointer.x, this.pointer.y);
            const planePoint = this.pointer.dragStart;
            const intersect = (r) =>
            {
                const denominator = Vec3.dot(r.direction, normal);
                if (Math.abs(denominator) < 1e-6) return null;
                const t = Vec3.dot(Vec3.sub(planePoint, r.origin), normal) / denominator;
                return Vec3.add(r.origin, Vec3.scale(r.direction, t));
            };
            const current = intersect(ray), start = intersect(startRay);
            if (current && start)
            {
                const offset = Vec3.sub(current, start);
                const half = this.solver.domainHalfExtent, centre = this.solver.domainCentre;
                const position = Vec3.add(planePoint, event.shiftKey ? [offset[0], 0, offset[2]] : offset);
                this.placeNode(node, position.map((value, axis) => Math.max(centre[axis] - half[axis] * 1.5, Math.min(centre[axis] + half[axis] * 1.5, Math.round(value * 100) / 100))));
                this.notify();
            }
            return;
        }
        this.pointer.x = x; this.pointer.y = y;
    }

    onPointerUp(event)
    {
        if (!this.pointer.mode) return;
        if (this.pointer.mode === 'orbit' && !this.pointer.moved)
        {
            this.select('domain');
        }
        this.pointer.mode = null;
        this.pointer.dragNode = null;
        try { this.canvas.releasePointerCapture(event.pointerId); } catch (error) { /* pointer already released */ }
    }
}
