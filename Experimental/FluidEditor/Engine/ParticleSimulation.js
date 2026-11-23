//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE SIMULATION
//     GPU Owner for Every Particle System Node: Per-System Ping-Pong State, Emit / Cloner Update, Sprite + Streak Render
//
//  The simulation is deliberately decoupled from the fluid solver: it only borrows the solver's force-generator
//  uniforms (so the same gizmos drive gas and particles), its velocity volume (gas coupling) and its grid layout.
//  Each scene node of Kind 'Particles' owns one state record keyed by node Id; records are created lazily and
//  destroyed when their node disappears, so adding and deleting systems at runtime is free of bookkeeping elsewhere.
//------------------------------------------------------------------------------------------------------------------------

import * as ParticleShaders from './ParticleShaders.js';
import { FullscreenVertex, sparse } from './FluidShaders.js';
import { ParticleShapes, ParticleModes, ParticleColourModes, ParticleBlendModes, ParticleRenderStyles, ParticleRampSources, ParticleSprites, ParticleSizeModes, ParticleTrailModes, ParticleTrailColours, ParticleDerezModes, ParticleMocapClips, ParticleEffectorModes, ParticleEffectorAxes, ParticleCarrierModes, ParticleBackingSurfaces, ParticleStrandModes, ParticleStrandVariants, MaximumPaletteStops, particleTextureSide, effectiveTrailLength, HaltedParticleSystems } from './ParticleCatalog.js';
import { ResolveObjectMesh, SampleObjectAnchors, PackAnchorTexels, PackObjectTexels, ObjectAnchorSignature } from './ObjectMeshCatalog.js';
import { SampleFireworkAnchors } from './FireworkPatterns.js';
import { ColourRamps } from './ForceGeneratorCatalog.js';
import { nodeWeight } from './KeyTracks.js';

function hexToRgb(hex)
{
    const value = parseInt(hex.replace('#', ''), 16);
    return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255];
}

function normalise(vector)
{
    const length = Math.hypot(vector[0], vector[1], vector[2]);
    return length > 1e-6 ? [vector[0] / length, vector[1] / length, vector[2] / length] : [0, 1, 0];
}

export class ParticleSimulation
{
    get programs()
    {
        const key = this.solver.sparse ? 'sparse' : 'fixed';
        if (!this.programSets[key]) this.programSets[key] = this.buildPrograms(this.solver.sparse);
        return this.programSets[key];
    }

    buildPrograms(sparseMode)
    {
        const context = this.context;
        const t = sparseMode ? sparse : (source) => source;
        const suffix = sparseMode ? '.sparse' : '';
        return {
            update: context.createProgram('ParticleUpdate' + suffix, FullscreenVertex, t(ParticleShaders.ParticleUpdateFragment)),
            render: context.createProgram('ParticleRender' + suffix, t(ParticleShaders.ParticleRenderVertex), ParticleShaders.ParticleFragment),
            trailUpdate: context.createProgram('TrailUpdate' + suffix, FullscreenVertex, t(ParticleShaders.TrailUpdateFragment)),
            trails: context.createProgram('Trails' + suffix, t(ParticleShaders.TrailVertex), ParticleShaders.ParticleFragment),
            object: context.createProgram('ParticleObject' + suffix, t(ParticleShaders.ObjectRenderVertex), ParticleShaders.ObjectFragment),
            strand: context.createProgram('ParticleStrand' + suffix, t(ParticleShaders.StrandVertex), ParticleShaders.StrandFragment),
        };
    }

    constructor(context, solver)
    {
        this.context = context;
        this.gl = context.gl;
        this.solver = solver;
        this.records = new Map();
        this.programSets = {};
        this.emptyVao = this.gl.createVertexArray();
        this.statistics = { systems: 0, particles: 0 };
        this.paletteScratch = new Float32Array(MaximumPaletteStops * 3);
        // 3D object carriers: one packed RGBA32F texel mesh per distinct object, plus a 1x1 placeholder for the spawn anchor slot.
        this.meshTextures = new Map();
        this.emptyAnchors = this.context.createDataTexture(1, 1, new Float32Array([0, 0, 0, 0]));
        this.backingColour = [0.62, 0.86, 1.0];
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  STATE RECORDS
    //------------------------------------------------------------------------------------------------------------------------

    record(node)
    {
        const side = particleTextureSide(node.Count);
        let record = this.records.get(node.Id);
        if (record && record.side !== side) { this.destroyRecord(record); record = null; }
        if (!record)
        {
            const context = this.context;
            record = {
                side,
                positions: context.createPingPong(side, side, { rgba32f: true, nearest: true }),
                velocities: context.createPingPong(side, side, { rgba32f: true, nearest: true }),
                previous: context.createTarget(side, side, { rgba32f: true, nearest: true }),
                needsReset: true,
                signature: '',
            };
            this.records.set(node.Id, record);
        }
        // Formation-defining parameters rebuild the cloner homes; emitters simply keep flowing.
        const signature = `${node.Mode}|${node.Shape}|${node.Count}|${node.DerezMode === 'ReverseDerez' ? 'rev' : 'std'}`;
        if (record.signature !== signature) { record.needsReset = true; record.signature = signature; }
        this.refreshAnchors(record, node);
        // Trail history is allocated on demand and rebuilt when its length changes.
        const wantsTrail = node.Trail && node.Trail !== 'Off';
        const trailLength = effectiveTrailLength(node.Count, node.TrailLength);
        if (wantsTrail && (!record.trails || record.trailLength !== trailLength))
        {
            if (record.trails) this.context.destroyPingPong(record.trails);
            record.trails = this.context.createPingPong(side, side * trailLength, { rgba32f: true, nearest: true });
            record.trailLength = trailLength;
            record.trailHead = 0;
            record.trailClock = 0;
            record.trailNeedsReset = true;
        }
        else if (!wantsTrail && record.trails) { this.context.destroyPingPong(record.trails); record.trails = null; }
        return record;
    }

    destroyRecord(record)
    {
        this.context.destroyPingPong(record.positions);
        this.context.destroyPingPong(record.velocities);
        this.context.destroyTarget(record.previous);
        if (record.trails) this.context.destroyPingPong(record.trails);
        if (record.anchors) this.context.destroyDataTexture(record.anchors);
    }

    /**
     * Bakes the 3D Object spawn anchors on the CPU when the object, sampling mode, yaw or count changes. Emitters on any other shape
     * keep a 1x1 placeholder bound to the anchor sampler, so the uniform is always valid.
     */
    refreshAnchors(record, node)
    {
        const isFireworkPattern = node.Shape === 'FireworkBurst' && node.FireworkPattern && node.FireworkPattern !== 'ClassicBouquet';
        const shellIndex = node.FireworkPattern === 'GrandFinale' ? Math.floor((this.solver.time || 0) / 3.5) : 0;
        const wanted = node.Shape === 'ObjectSurface'
            ? ObjectAnchorSignature(node)
            : (isFireworkPattern ? `fw|${node.FireworkPattern}|${node.FireworkText || '2026'}|${node.Count}|${shellIndex}` : '');
        if (record.anchorSignature === wanted) return;
        record.anchorSignature = wanted;
        if (record.anchors) { this.context.destroyDataTexture(record.anchors); record.anchors = null; }
        record.anchorCount = 0;
        record.anchorWidth = 1;
        if (!wanted) return;
        const sampled = isFireworkPattern
            ? SampleFireworkAnchors(node.FireworkPattern, node.FireworkText, node.Count, shellIndex)
            : SampleObjectAnchors(ResolveObjectMesh(node.SpawnObject, node.CustomObjectId), node.Count, node.SpawnSampleMode, node.ObjectYaw);
        const packed = PackAnchorTexels(sampled);
        record.anchors = this.context.createDataTexture(packed.Width, packed.Height, packed.Data);
        record.anchorWidth = packed.Width;
        record.anchorCount = packed.Count;
        if (node.Mode === 'Cloner') record.needsReset = true;
    }

    /** Packed texel mesh for a 3D carrier or backing surface (cached by object name + custom mesh id). */
    objectTexture(name, customId = '')
    {
        const mesh = ResolveObjectMesh(name, customId);
        const key = `${name}|${customId}|${mesh.Triangles}`;
        let entry = this.meshTextures.get(key);
        if (!entry)
        {
            const packed = PackObjectTexels(mesh);
            const dataTexture = this.context.createDataTexture(packed.Width, packed.Height, packed.Data);
            entry = {
                dataTexture,
                texture: dataTexture.texture,
                width: packed.Width,
                triangleCount: packed.TriangleCount,
                vertexCount: packed.VertexCount,
            };
            this.meshTextures.set(key, entry);
        }
        return entry;
    }

    updateTrails(node, record, deltaTime)
    {
        const gl = this.gl, context = this.context;
        record.trailClock += deltaTime;
        const advance = record.trailClock >= node.TrailInterval || record.trailNeedsReset;
        if (advance)
        {
            record.trailClock = 0;
            record.trailHead = (record.trailHead + 1) % record.trailLength;
        }
        const uniforms = context.use(this.programs.trailUpdate);
        context.bindTexture('uTrails', record.trails.read.texture);
        context.bindTexture('uPositions', record.positions.read.texture);
        context.bindTexture('uPrevious', record.previous.texture);
        gl.uniform1i(uniforms.uTrailLength, record.trailLength);
        gl.uniform1i(uniforms.uHead, record.trailHead);
        gl.uniform1i(uniforms.uAdvance, advance ? 1 : 0);
        gl.uniform1i(uniforms.uReset, record.trailNeedsReset ? 1 : 0);
        gl.uniform1f(uniforms.uLifetime, node.Mode === 'Cloner' ? 0 : Math.max(0.05, node.Lifetime));
        context.dispatch(record.trails.write);
        record.trails.swap();
        record.trailNeedsReset = false;
    }

    prune(scene)
    {
        const live = new Set(scene.Particles.map((node) => node.Id));
        for (const [id, record] of [...this.records]) if (!live.has(id)) { this.destroyRecord(record); this.records.delete(id); }
    }

    resetAll()
    {
        // Trail history restarts too, so a run from t = 0 matches a fresh load bit for bit (timeline scrubbing relies on it).
        for (const record of this.records.values()) { record.needsReset = true; record.trailHead = 0; record.trailClock = 0; }
    }

    reset(nodeId)
    {
        const record = this.records.get(nodeId);
        if (record) record.needsReset = true;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  SHAPE UNIFORMS (shared by update + render)
    //------------------------------------------------------------------------------------------------------------------------

    applyShapeUniforms(uniforms, node, record)
    {
        const gl = this.gl;
        const axis = normalise(node.Direction);
        gl.uniform1i(uniforms.uShape, Math.max(0, ParticleShapes.indexOf(node.Shape)));
        gl.uniform3f(uniforms.uOrigin, node.Position[0], node.Position[1], node.Position[2]);
        gl.uniform3f(uniforms.uAxis, axis[0], axis[1], axis[2]);
        gl.uniform1f(uniforms.uRadius, Math.max(0.01, node.Radius));
        gl.uniform1f(uniforms.uFill, node.Fill);
        gl.uniform1f(uniforms.uShapeDetail, node.ShapeDetail ?? 0);
        gl.uniform1i(uniforms.uAnchorWidth, record.anchorWidth || 1);
        gl.uniform1i(uniforms.uAnchorCount, record.anchorCount || 0);
        gl.uniform1i(uniforms.uCount, Math.min(node.Count, record.side * record.side));
        gl.uniform2i(uniforms.uParticleSize, record.side, record.side);
        gl.uniform1i(uniforms.uMode, Math.max(0, ParticleModes.indexOf(node.Mode)));
        gl.uniform1i(uniforms.uMocapClip, Math.max(0, ParticleMocapClips.indexOf(node.MocapClip)));
        gl.uniform1f(uniforms.uMocapAmplitude, node.MocapAmplitude ?? 0);
        gl.uniform1f(uniforms.uMocapSpeed, node.MocapSpeed ?? 1);
        gl.uniform1f(uniforms.uMocapScale, node.MocapScale ?? 1);
        gl.uniform1f(uniforms.uMocapTrailKick, node.MocapTrailKick ?? 0);
        gl.uniform1i(uniforms.uEffectorMode, Math.max(0, ParticleEffectorModes.indexOf(node.EffectorMode)));
        gl.uniform1f(uniforms.uEffectorStrength, node.EffectorStrength ?? 0);
        gl.uniform1f(uniforms.uEffectorFrequency, node.EffectorFrequency ?? 1.8);
        gl.uniform1f(uniforms.uEffectorSpeed, node.EffectorSpeed ?? 1.6);
        gl.uniform1f(uniforms.uEffectorPush, node.EffectorPush ?? 0);
        gl.uniform1f(uniforms.uEffectorScaleBoost, node.EffectorScaleBoost ?? 0);
        gl.uniform1f(uniforms.uEffectorColourShift, node.EffectorColourShift ?? 0);
        gl.uniform1i(uniforms.uEffectorAxis, Math.max(0, ParticleEffectorAxes.indexOf(node.EffectorAxis)));
        gl.uniform1i(uniforms.uDerezMode, Math.max(0, ParticleDerezModes.indexOf(node.DerezMode)));
        gl.uniform1f(uniforms.uDerezMix, node.DerezMix ?? 0);
        gl.uniform1f(uniforms.uDerezProgress, node.DerezProgress ?? 0);
        gl.uniform1f(uniforms.uDerezAutoSpeed, node.DerezAutoSpeed ?? 0);
        gl.uniform1f(uniforms.uDerezEdgeWidth, node.DerezEdgeWidth ?? 0.14);
        gl.uniform1f(uniforms.uDerezEdgeGlow, node.DerezEdgeGlow ?? 0);
        const edgeRgb = hexToRgb(node.DerezEdgeColour || '#00f0ff');
        gl.uniform3f(uniforms.uDerezEdgeColour, edgeRgb[0], edgeRgb[1], edgeRgb[2]);
        gl.uniform1f(uniforms.uDerezShatter, node.DerezShatter ?? 0);
        gl.uniform1f(uniforms.uSwarmWingRate, node.SwarmWingRate ?? 10);
        gl.uniform1f(uniforms.uGlitchAmount, node.GlitchAmount ?? 0);
        gl.uniform1f(uniforms.uGlitchRate, node.GlitchRate ?? 14);
        gl.uniform1f(uniforms.uGlitchStepJitter, node.GlitchStepJitter ?? 0);
        gl.uniform1f(uniforms.uGlitchScanlines, node.GlitchScanlines ?? 0);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  UPDATE
    //------------------------------------------------------------------------------------------------------------------------

    update(scene, deltaTime)
    {
        const solver = this.solver;
        if (!solver.fields) return;
        this.prune(scene);
        const gl = this.gl, context = this.context;
        gl.disable(gl.BLEND);
        this.statistics.systems = 0;
        this.statistics.particles = 0;
        for (const node of scene.Particles)
        {
            // A stack that cannot evaluate (no Spawn layer, over capacity) holds this system; the rest of the scene keeps running.
            if (!node.Enabled || HaltedParticleSystems.has(node)) continue;
            const record = this.record(node);
            if (deltaTime <= 0 && !record.needsReset) continue;
            this.statistics.systems += 1;
            this.statistics.particles += node.Count;
            const copyPrevious = () =>
            {
                gl.bindFramebuffer(gl.READ_FRAMEBUFFER, record.positions.read.framebuffer);
                gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, record.previous.framebuffer);
                gl.blitFramebuffer(0, 0, record.side, record.side, 0, 0, record.side, record.side, gl.COLOR_BUFFER_BIT, gl.NEAREST);
                gl.bindFramebuffer(gl.READ_FRAMEBUFFER, null);
                gl.bindFramebuffer(gl.DRAW_FRAMEBUFFER, null);
            };
            copyPrevious();
            const uniforms = context.use(this.programs.update);
            solver.applyVolumeUniforms(uniforms);
            solver.applyForceUniforms(uniforms);
            this.applyShapeUniforms(uniforms, node, record);
            gl.uniform1f(uniforms.uDeltaTime, deltaTime);
            gl.uniform1f(uniforms.uEnvelope, nodeWeight(node, solver.time));
            gl.uniform1f(uniforms.uRate, node.Rate);
            gl.uniform1f(uniforms.uLifetime, Math.max(0.05, node.Lifetime));
            gl.uniform1f(uniforms.uSpeed, node.Speed);
            gl.uniform1f(uniforms.uSpread, node.Spread);
            gl.uniform1f(uniforms.uRadial, node.Radial);
            gl.uniform1f(uniforms.uGravity, node.Gravity);
            gl.uniform1f(uniforms.uDrag, node.Drag);
            gl.uniform1f(uniforms.uNoise, node.Noise);
            gl.uniform1f(uniforms.uFieldInfluence, node.FieldInfluence);
            gl.uniform1f(uniforms.uGasInfluence, node.GasInfluence);
            gl.uniform1f(uniforms.uSpring, node.Spring);
            gl.uniform1f(uniforms.uDamping, node.Damping);
            gl.uniform1f(uniforms.uBounce, node.Bounce);
            gl.uniform1f(uniforms.uNoiseScale, node.NoiseScale);
            gl.uniform1f(uniforms.uNoiseSpeed, node.NoiseSpeed);
            gl.uniform1f(uniforms.uDissolve, node.Dissolve);
            gl.uniform1f(uniforms.uLinger, node.Linger);
            gl.uniform1f(uniforms.uSwarmStrength, node.SwarmStrength ?? 0);
            gl.uniform1f(uniforms.uSwarmCohesion, node.SwarmCohesion ?? 0);
            gl.uniform1f(uniforms.uSwarmOrbit, node.SwarmOrbit ?? 0);
            for (const output of [0, 1])
            {
                context.textureUnit = 0;
                context.bindTexture('uPositions', record.positions.read.texture);
                context.bindTexture('uVelocities', record.velocities.read.texture);
                context.bindTexture('uAnchors', (record.anchors || this.emptyAnchors).texture);
                context.bindTexture('uVelocity', solver.fields.velocity.read.texture);
                gl.uniform1i(uniforms.uOutput, output);
                gl.uniform1i(uniforms.uReset, record.needsReset ? 1 : 0);
                context.dispatch(output === 0 ? record.positions.write : record.velocities.write);
            }
            record.positions.swap();
            record.velocities.swap();
            if (record.needsReset) { record.needsReset = false; record.trailNeedsReset = true; copyPrevious(); }
            if (record.trails) this.updateTrails(node, record, deltaTime);
        }
    }

    applyShadeUniforms(uniforms, node)
    {
        const gl = this.gl;
        const palette = node.Palette && node.Palette.length ? node.Palette.slice(0, MaximumPaletteStops) : ['#ffffff'];
        this.paletteScratch.fill(0);
        palette.forEach((hex, index) => this.paletteScratch.set(hexToRgb(hex), index * 3));
        gl.uniform3fv(uniforms.uPalette, this.paletteScratch);
        gl.uniform1i(uniforms.uPaletteCount, palette.length);
        gl.uniform1i(uniforms.uColourMode, Math.max(0, ParticleColourModes.indexOf(node.ColourMode)));
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(node.Ramp)));
        gl.uniform1i(uniforms.uRampSource, Math.max(0, ParticleRampSources.indexOf(node.RampSource)));
        gl.uniform1f(uniforms.uHueJitter, node.HueJitter);
        gl.uniform1f(uniforms.uBrightnessJitter, node.BrightnessJitter);
        gl.uniform1f(uniforms.uCycleSpeed, node.CycleSpeed);
        gl.uniform1f(uniforms.uBrightness, node.Brightness);
        gl.uniform1f(uniforms.uLifetime, Math.max(0.05, node.Lifetime));
        gl.uniform1f(uniforms.uLinger, node.Linger);
        gl.uniform1f(uniforms.uMaxSpeed, node.MaxSpeed);
        gl.uniform1f(uniforms.uDisplacementRange, node.DisplacementRange);
        gl.uniform1f(uniforms.uFlicker, node.Flicker);
        gl.uniform1f(uniforms.uTime, this.solver.time);
        gl.uniform1f(uniforms.uDomainHeight, this.solver.domainHalfExtent[1] * 2);
    }

    applyCameraUniforms(uniforms, node, camera, pixelRatio)
    {
        const gl = this.gl;
        const viewportHeight = camera ? camera.viewport[1] * pixelRatio : 720;
        const fovY = camera ? camera.fovY : 0.733;
        gl.uniform1f(uniforms.uFocalPixels, viewportHeight / (2 * Math.tan(fovY / 2)));
        const cameraPosition = camera ? camera.position : [0, 0, 10];
        const cameraForward = camera ? camera.forward : [0, 0, -1];
        gl.uniform3f(uniforms.uCameraPosition, cameraPosition[0], cameraPosition[1], cameraPosition[2]);
        gl.uniform3f(uniforms.uCameraForward, cameraForward[0], cameraForward[1], cameraForward[2]);
        gl.uniform1i(uniforms.uSizeMode, Math.max(0, ParticleSizeModes.indexOf(node.SizeMode)));
    }

    /**
     * 3D object pass: every particle draws the whole triangle mesh, rebuilt from gl_VertexID and the packed RGBA32F texels.
     * The backing surface reuses the same program as a single centred instance; the ghost wireframe draws triangle edges.
     */
    renderObjects(node, record, matrices, camera, pixelRatio, options = {})
    {
        const backing = Boolean(options.backing);
        const wire = Boolean(options.wire);
        const mode = options.mode || 0;
        const gl = this.gl, context = this.context;
        const customId = backing ? node.CustomObjectId : (node.CarrierCustomObjectId || node.CustomObjectId);
        const entry = this.objectTexture(backing ? node.SpawnObject : node.CarrierObject, customId);
        const uniforms = context.use(this.programs.object);
        context.textureUnit = 0;
        context.bindTexture('uPositions', record.positions.read.texture);
        context.bindTexture('uVelocities', record.velocities.read.texture);
        context.bindTexture('uMesh', entry.texture);
        context.bindTexture('uAnchors', (record.anchors || this.emptyAnchors).texture);
        this.applyShapeUniforms(uniforms, node, record);
        this.applyShadeUniforms(uniforms, node);
        this.applyCameraUniforms(uniforms, node, camera, pixelRatio);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform2i(uniforms.uParticleSize, record.side, record.side);
        gl.uniform1i(uniforms.uMeshWidth, entry.width);
        gl.uniform1i(uniforms.uMeshVertexCount, entry.vertexCount);
        gl.uniform1i(uniforms.uMeshTriangleCount, entry.triangleCount);
        gl.uniform1i(uniforms.uWire, wire ? 1 : 0);
        gl.uniform1i(uniforms.uBacking, backing ? 1 : 0);
        gl.uniform1f(uniforms.uBackingScale, Math.max(0.05, node.Radius));
        gl.uniform1f(uniforms.uObjectYaw, ((node.ObjectYaw || 0) * Math.PI) / 180);
        gl.uniform3f(uniforms.uOrigin, node.Position[0], node.Position[1], node.Position[2]);
        gl.uniform1f(uniforms.uSizeStart, node.SizeStart);
        gl.uniform1f(uniforms.uSizeEnd, node.SizeEnd);
        gl.uniform1f(uniforms.uSizeRandom, node.SizeRandom);
        gl.uniform1f(uniforms.uPixelRatio, pixelRatio);
        gl.uniform1f(uniforms.uObjectSpin, node.ObjectSpin ?? 0);
        gl.uniform1f(uniforms.uObjectTilt, node.ObjectTilt ?? 0);
        gl.uniform1f(uniforms.uObjectRoughness, node.ObjectRoughness ?? 0.35);
        gl.uniform1f(uniforms.uObjectMetallic, node.ObjectMetallic ?? 0);
        gl.uniform1f(uniforms.uObjectGlow, node.ObjectGlow ?? 0);
        gl.uniform1i(uniforms.uBackingMode, mode);
        gl.uniform3f(uniforms.uBackingColour, this.backingColour[0], this.backingColour[1], this.backingColour[2]);
        const stride = wire ? entry.triangleCount * 6 : entry.vertexCount;
        const rawCount = backing ? 1 : Math.min(node.Count, record.side * record.side);
        const maxCarrierVertices = 720000;
        const carrierStride = (!backing && stride > 0 && rawCount * stride > maxCarrierVertices)
            ? Math.max(1, Math.ceil((rawCount * stride) / maxCarrierVertices))
            : 1;
        const drawnCount = backing ? 1 : Math.ceil(rawCount / carrierStride);
        gl.uniform1i(uniforms.uCarrierStride, carrierStride);
        const vertexCount = drawnCount * stride;
        if (vertexCount <= 0) return;

        // Solid backing surfaces (obsidian / chrome) and opaque 3D carriers must z-buffer cleanly.
        const solidDepth = (!wire) && (backing ? (mode === 1 || mode === 2) : (ParticleBlendModes.indexOf(node.Blend) === 0));
        if (solidDepth)
        {
            gl.enable(gl.DEPTH_TEST);
            gl.depthMask(true);
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        }
        else if (backing && wire)
        {
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
        }
        gl.bindVertexArray(this.emptyVao);
        gl.drawArrays(wire ? gl.LINES : gl.TRIANGLES, 0, vertexCount);
        gl.bindVertexArray(null);
        if (solidDepth)
        {
            gl.depthMask(false);
            gl.disable(gl.DEPTH_TEST);
        }
    }

    /** Fluid-coupled GPU strands, catenary cables, hanging vines, flexing flora, spiderwebs and electrical arcs (Layer 219). */
    renderStrands(node, matrices, camera, pixelRatio)
    {
        const mode = ParticleStrandModes.indexOf(node.StrandMode);
        if (mode <= 0) return;
        const gl = this.gl, context = this.context, solver = this.solver;
        const uniforms = context.use(this.programs.strand);
        context.textureUnit = 0;
        context.bindTexture('uVelocity', solver.fields.velocity.read.texture);
        solver.applyVolumeUniforms(uniforms);
        solver.applyForceUniforms(uniforms);

        const palette = node.Palette && node.Palette.length ? node.Palette.slice(0, MaximumPaletteStops) : ['#ffffff'];
        this.paletteScratch.fill(0);
        palette.forEach((hex, index) => this.paletteScratch.set(hexToRgb(hex), index * 3));
        gl.uniform3fv(uniforms.uPalette, this.paletteScratch);
        gl.uniform1i(uniforms.uPaletteCount, palette.length);

        const axis = normalise(node.Direction);
        const viewport = camera ? [camera.viewport[0] * pixelRatio, camera.viewport[1] * pixelRatio] : [1280, 720];
        const fovY = camera ? camera.fovY : 0.733;
        const focalPixels = viewport[1] / (2 * Math.tan(fovY / 2));
        const cameraPosition = camera ? camera.position : [0, 0, 10];
        const cameraForward = camera ? camera.forward : [0, 0, -1];
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform2f(uniforms.uViewport, Math.max(1, viewport[0]), Math.max(1, viewport[1]));
        gl.uniform3f(uniforms.uCameraPosition, cameraPosition[0], cameraPosition[1], cameraPosition[2]);
        gl.uniform3f(uniforms.uCameraForward, cameraForward[0], cameraForward[1], cameraForward[2]);
        gl.uniform1f(uniforms.uFocalPixels, focalPixels);
        gl.uniform3f(uniforms.uOrigin, node.Position[0], node.Position[1], node.Position[2]);
        gl.uniform3f(uniforms.uAxis, axis[0], axis[1], axis[2]);
        gl.uniform1f(uniforms.uRadius, Math.max(0.1, node.Radius));
        gl.uniform1f(uniforms.uTime, solver.time);

        const baseCount = Math.max(6, Math.min(192, Math.round(node.StrandCount ?? 36)));
        const detail = Math.max(4, Math.min(28, Math.round(node.StrandDetail ?? 14)));
        let totalStrands = baseCount;
        if (mode === 1) totalStrands = Math.min(640, 12 + Math.min(baseCount, 36) + Math.min(baseCount, 36) * Math.min(detail, 20));
        else if (mode === 2) totalStrands = Math.min(480, baseCount * 2);
        else if (mode === 5 || mode === 8) totalStrands = Math.min(320, baseCount * 2);
        const segments = mode >= 9 ? Math.max(14, Math.min(24, detail)) : 14;

        gl.uniform1i(uniforms.uStrandMode, mode);
        gl.uniform1i(uniforms.uStrandVariant, Math.max(0, ParticleStrandVariants.indexOf(node.StrandVariant)));
        gl.uniform1i(uniforms.uStrandCount, baseCount);
        gl.uniform1i(uniforms.uStrandTotal, totalStrands);
        gl.uniform1i(uniforms.uStrandSegments, segments);
        gl.uniform1i(uniforms.uStrandDetail, detail);
        gl.uniform1f(uniforms.uStrandSag, node.StrandSag ?? 0.32);
        gl.uniform1f(uniforms.uStrandThickness, Math.max(0.004, node.StrandThickness ?? 0.032));
        gl.uniform1f(uniforms.uStrandSway, node.StrandSway ?? 0.85);
        gl.uniform1f(uniforms.uStrandSpan, Math.max(0.3, node.StrandSpan ?? 1.8));
        gl.uniform1f(uniforms.uStrandGlow, node.StrandGlow ?? 0.65);
        gl.uniform1f(uniforms.uBrightness, node.Brightness ?? 1.0);
        gl.uniform1f(uniforms.uEnvelope, nodeWeight(node, solver.time));

        const additiveStrand = mode >= 9 || ParticleBlendModes.indexOf(node.Blend) === 1;
        gl.blendFunc(gl.SRC_ALPHA, additiveStrand ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
        gl.bindVertexArray(this.emptyVao);
        gl.drawArrays(gl.TRIANGLES, 0, totalStrands * segments * 6);
        gl.bindVertexArray(null);
    }

    /** Backing surface behind a 3D origin or carrier: obsidian gloss, brushed chrome or the ghost wireframe. */
    renderBacking(node, record, matrices, camera, pixelRatio)
    {
        const mode = ParticleBackingSurfaces.indexOf(node.BackingSurface);
        if (mode <= 0) return;
        this.renderObjects(node, record, matrices, camera, pixelRatio, { backing: true, wire: mode === 3, mode });
    }

    renderTrails(node, record, matrices, camera, pixelRatio)
    {
        const gl = this.gl, context = this.context;
        const uniforms = context.use(this.programs.trails);
        context.bindTexture('uPositions', record.positions.read.texture);
        context.bindTexture('uVelocities', record.velocities.read.texture);
        context.bindTexture('uAnchors', (record.anchors || this.emptyAnchors).texture);
        context.bindTexture('uTrails', record.trails.read.texture);
        this.applyShapeUniforms(uniforms, node, record);
        this.applyShadeUniforms(uniforms, node);
        this.applyCameraUniforms(uniforms, node, camera, pixelRatio);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform1i(uniforms.uTrailLength, record.trailLength);
        gl.uniform1i(uniforms.uHead, record.trailHead);
        gl.uniform1i(uniforms.uSubdivisions, node.TrailSubdivisions);
        const stride = Math.max(1, Math.round(1 / Math.max(0.01, node.TrailRatio)));
        gl.uniform1i(uniforms.uStride, stride);
        const viewport = camera ? [camera.viewport[0] * pixelRatio, camera.viewport[1] * pixelRatio] : [1280, 720];
        gl.uniform2f(uniforms.uViewport, viewport[0], viewport[1]);
        gl.uniform1f(uniforms.uWidth, node.TrailWidth * (ParticleSizeModes.indexOf(node.SizeMode) === 1 ? pixelRatio : 1));
        gl.uniform1f(uniforms.uTaper, node.TrailTaper);
        gl.uniform1f(uniforms.uTrailFade, node.TrailFade);
        gl.uniform1i(uniforms.uTrailColour, Math.max(0, ParticleTrailColours.indexOf(node.TrailColour)));
        gl.uniform1i(uniforms.uSprite, 4);
        gl.uniform1i(uniforms.uStyle, 0);
        const count = Math.min(node.Count, record.side * record.side);
        const trailed = Math.ceil(count / stride);
        const vertices = trailed * record.trailLength * node.TrailSubdivisions * 6;
        gl.bindVertexArray(this.emptyVao);
        if (node.Glow > 0)
        {
            gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
            gl.uniform1f(uniforms.uHaloScale, 3.0);
            gl.uniform1f(uniforms.uHaloAlpha, 0.12 * node.Glow);
            gl.drawArrays(gl.TRIANGLES, 0, vertices);
            gl.blendFunc(gl.SRC_ALPHA, ParticleBlendModes.indexOf(node.Blend) === 1 ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
        }
        gl.uniform1f(uniforms.uHaloScale, 1);
        gl.uniform1f(uniforms.uHaloAlpha, 1);
        gl.drawArrays(gl.TRIANGLES, 0, vertices);
        gl.bindVertexArray(null);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  RENDER
    //------------------------------------------------------------------------------------------------------------------------

    render(scene, matrices, pixelRatio = 1, camera = null)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        if (!solver.fields) return;
        const usesDepth = scene.Particles.some((node) => node.Visible && node.Enabled && !HaltedParticleSystems.has(node)
            && (ParticleCarrierModes.indexOf(node.CarrierMode) === 1 || ParticleBackingSurfaces.indexOf(node.BackingSurface) === 1 || ParticleBackingSurfaces.indexOf(node.BackingSurface) === 2));
        if (usesDepth)
        {
            gl.depthMask(true);
            gl.clear(gl.DEPTH_BUFFER_BIT);
            gl.depthMask(false);
        }
        gl.enable(gl.BLEND);
        for (const node of scene.Particles)
        {
            if (!node.Visible || !node.Enabled || HaltedParticleSystems.has(node)) continue;
            const record = this.records.get(node.Id);
            if (!record) continue;
            const additive = ParticleBlendModes.indexOf(node.Blend) === 1;
            gl.blendFunc(gl.SRC_ALPHA, additive ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
            this.renderBacking(node, record, matrices, camera, pixelRatio);
            const strandMode = ParticleStrandModes.indexOf(node.StrandMode);
            if (strandMode > 0)
            {
                this.renderStrands(node, matrices, camera, pixelRatio);
                // Pure strand/web/rigging/flora systems (modes 1..8) draw continuous silk/botanical/cable geometry
                // without scattering loose billboard particle dots over the strands; electrical arcs (9..11) keep corona sparks.
                if (strandMode <= 8 && ParticleCarrierModes.indexOf(node.CarrierMode) !== 1) continue;
            }
            const trailMode = ParticleTrailModes.indexOf(node.Trail);
            if (record.trails && trailMode > 0) this.renderTrails(node, record, matrices, camera, pixelRatio);
            // The visual carrier is a billboard sprite or a shaded 3D mesh — the Sprite layer decides.
            if (ParticleCarrierModes.indexOf(node.CarrierMode) === 1)
            {
                gl.blendFunc(gl.SRC_ALPHA, additive ? gl.ONE : gl.ONE_MINUS_SRC_ALPHA);
                this.renderObjects(node, record, matrices, camera, pixelRatio);
                continue;
            }
            if (trailMode === 2) continue;
            const uniforms = context.use(this.programs.render);
            context.bindTexture('uPositions', record.positions.read.texture);
            context.bindTexture('uVelocities', record.velocities.read.texture);
            context.bindTexture('uPrevious', record.previous.texture);
            context.bindTexture('uAnchors', (record.anchors || this.emptyAnchors).texture);
            this.applyShapeUniforms(uniforms, node, record);
            this.applyShadeUniforms(uniforms, node);
            this.applyCameraUniforms(uniforms, node, camera, pixelRatio);
            gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
            gl.uniform1f(uniforms.uSizeStart, node.SizeStart);
            gl.uniform1f(uniforms.uSizeEnd, node.SizeEnd);
            gl.uniform1f(uniforms.uPixelRatio, pixelRatio);
            gl.uniform1f(uniforms.uSizeRandom, node.SizeRandom);
            gl.uniform1f(uniforms.uFocusDistance, node.FocusDistance);
            gl.uniform1f(uniforms.uAperture, node.Aperture);
            gl.uniform1i(uniforms.uSprite, Math.max(0, ParticleSprites.indexOf(node.Sprite)));
            gl.uniform1i(uniforms.uStyle, Math.max(0, ParticleRenderStyles.indexOf(node.Style)));
            const count = Math.min(node.Count, record.side * record.side);
            gl.bindVertexArray(this.emptyVao);
            gl.uniform1f(uniforms.uHaloScale, 1);
            gl.uniform1f(uniforms.uHaloAlpha, 1);
            if (ParticleRenderStyles.indexOf(node.Style) === 1)
            {
                gl.uniform1i(uniforms.uTrail, 1);
                gl.drawArrays(gl.LINES, 0, count * 2);
            }
            gl.uniform1i(uniforms.uTrail, 0);
            gl.drawArrays(gl.POINTS, 0, count);
            if (node.Glow > 0)
            {
                // Cheap bloom: a second, larger, fainter additive halo pass with the soft glow sprite.
                gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
                gl.uniform1i(uniforms.uSprite, 0);
                gl.uniform1f(uniforms.uHaloScale, 3.2);
                gl.uniform1f(uniforms.uHaloAlpha, 0.16 * node.Glow);
                gl.drawArrays(gl.POINTS, 0, count);
            }
            gl.bindVertexArray(null);
        }
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
    }

    destroy()
    {
        for (const record of this.records.values()) this.destroyRecord(record);
        this.records.clear();
        for (const entry of this.meshTextures.values()) this.context.destroyDataTexture(entry.dataTexture);
        this.meshTextures.clear();
        this.context.destroyDataTexture(this.emptyAnchors);
    }
}
