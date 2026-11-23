//============================================================================================================================================
//                                                        VOLUMERENDERER.JS
//               Viewport Composition — Analytic Ground / Collider / Volume Ray-March Pass, Velocity Vector Slice,
//                Scalar Heat-Map Slice, Flow Tracer Points + Trails, and Force / Emitter / Domain Gizmo Line Batches
//============================================================================================================================================

import * as Shaders from './FluidShaders.js';
import { Vec3 } from './OrbitCamera.js';
import { ForceTypeById, ColourRamps, ScalarChannels, SliceAxes, VolumeFilters, VectorModes, emitterTransform } from './ForceGeneratorCatalog.js';
import { ParticleDistortionModes, HaltedParticleSystems } from './ParticleCatalog.js';
import { nodeWeight, keyedNode } from './KeyTracks.js';

function hexToRgb(hex)
{
    const value = parseInt(hex.replace('#', ''), 16);
    return [((value >> 16) & 255) / 255, ((value >> 8) & 255) / 255, (value & 255) / 255];
}

export class VolumeRenderer
{
    /** Overlay programs follow the solver's addressing mode (fixed window vs sparse tiles), compiled on first use. */
    get programs()
    {
        const key = this.solver.sparse ? 'sparse' : 'fixed';
        if (!this.programSets[key]) this.programSets[key] = this.buildPrograms(this.solver.sparse);
        return this.programSets[key];
    }

    buildPrograms(sparseMode)
    {
        const context = this.context;
        const t = sparseMode ? Shaders.sparse : (source) => source;
        const suffix = sparseMode ? '.sparse' : '';
        return {
            scene: context.createProgram('Scene' + suffix, Shaders.FullscreenVertex, t(Shaders.SceneFragment)),
            vectors: context.createProgram('Vectors' + suffix, t(Shaders.VectorVertex), Shaders.ColourFragment),
            forceVectors: context.createProgram('ForceVectors' + suffix, t(Shaders.ForceVectorVertex), Shaders.ColourFragment),
            slice: context.createProgram('Slice' + suffix, t(Shaders.SliceVertex), t(Shaders.SliceFragment)),
            tracers: context.createProgram('Tracers' + suffix, t(Shaders.TracerRenderVertex), Shaders.ColourFragment),
            debris: context.createProgram('Debris' + suffix, t(Shaders.DebrisRenderVertex), Shaders.ColourFragment),
            lines: context.createProgram('Lines' + suffix, Shaders.LineVertex, Shaders.ColourFragment),
            postOverlay: context.createProgram('PostOverlay' + suffix, Shaders.FullscreenVertex, Shaders.PostOverlayFragment),
        };
    }

    constructor(context, solver)
    {
        this.context = context;
        this.solver = solver;
        this.gl = context.gl;
        const gl = this.gl;
        this.programSets = {};
        this.emptyVao = gl.createVertexArray();
        this.lineVao = gl.createVertexArray();
        this.lineBuffer = gl.createBuffer();
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
        gl.enableVertexAttribArray(0);
        gl.vertexAttribPointer(0, 3, gl.FLOAT, false, 28, 0);
        gl.enableVertexAttribArray(1);
        gl.vertexAttribPointer(1, 4, gl.FLOAT, false, 28, 12);
        gl.bindVertexArray(null);
        this.lineScratch = new Float32Array(7 * 4096);
        this.lineVertexCount = 0;
        this.lightDirection = Vec3.normalize([0.45, 0.8, 0.35]);
        this.lightColour = [1.35, 1.22, 1.05];
        this.postTarget = null;
        this.lensScratchA = new Float32Array(64);
        this.lensScratchB = new Float32Array(64);
        this.glitchScratchA = new Float32Array(64);
        this.glitchScratchB = new Float32Array(64);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FRAME
    //------------------------------------------------------------------------------------------------------------------------

    hasActivePostOverlay(scene)
    {
        const volume = scene.Visualizers && scene.Visualizers.Volume;
        if (volume && (volume.ShimmerStrength || 0) > 0.0005) return true;
        for (const node of scene.Particles || [])
        {
            if (!node.Visible || !node.Enabled || HaltedParticleSystems.has(node)) continue;
            if (node.DistortionMode && node.DistortionMode !== 'Off' && (node.DistortionStrength || 0) > 0.0005) return true;
            if ((node.GlitchAmount || 0) > 0.001 && ((node.GlitchScanlines || 0) > 0.001 || (node.GlitchChromatic || 0) > 0.001)) return true;
        }
        return false;
    }

    ensurePostTarget(width, height)
    {
        const w = Math.max(1, width | 0), h = Math.max(1, height | 0);
        if (this.postTarget && this.postTarget.width === w && this.postTarget.height === h) return this.postTarget;
        if (this.postTarget) this.destroyBakeTarget(this.postTarget);
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, w, h);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.LINEAR);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
        gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
        const framebuffer = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        this.postTarget = { texture, framebuffer, width: w, height: h };
        return this.postTarget;
    }

    projectWorldSphere(worldPos, worldRadius, camera, viewProjection)
    {
        const m = viewProjection;
        const x = worldPos[0], y = worldPos[1], z = worldPos[2];
        const cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        const cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        const cw = m[3] * x + m[7] * y + m[11] * z + m[15];
        if (cw <= 0.05) return null;
        const uvX = (cx / cw) * 0.5 + 0.5;
        const uvY = (cy / cw) * 0.5 + 0.5;
        const fovY = camera ? camera.fovY : 0.733;
        const dist = camera ? Math.max(0.25, Vec3.length(Vec3.sub(worldPos, camera.position))) : Math.max(0.25, cw);
        const screenRadius = Math.min(1.2, Math.max(0.03, worldRadius / (2.0 * dist * Math.tan(fovY * 0.5))));
        return { uvX, uvY, screenRadius };
    }

    renderPostOverlay(scene, camera, matrices, sourceTarget, finalTarget)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const volume = scene.Visualizers.Volume;
        const uniforms = context.use(this.programs.postOverlay);
        context.bindTexture('uSceneColour', sourceTarget.texture);
        gl.uniform2f(uniforms.uViewport, this.viewportSize[0], this.viewportSize[1]);
        gl.uniform1f(uniforms.uTime, solver.time);

        // 1. Gas Volume Heat Shimmer (Layer 109)
        const shimmerStrength = volume.ShimmerStrength || 0;
        if (shimmerStrength > 0.0005)
        {
            const proj = this.projectWorldSphere(solver.domainCentre, Math.max(...solver.domainHalfExtent) * 1.15, camera, matrices.viewProjection);
            if (proj) gl.uniform4fv(uniforms.uShimmerArea, [proj.uvX, proj.uvY, proj.screenRadius, shimmerStrength]);
            else gl.uniform4fv(uniforms.uShimmerArea, [0.5, 0.5, 0.45, shimmerStrength]);
            gl.uniform3f(uniforms.uShimmerParams, volume.ShimmerFrequency ?? 14, volume.ShimmerSpeed ?? 2.2, volume.ShimmerChromatic ?? 0);
        }
        else
        {
            gl.uniform4fv(uniforms.uShimmerArea, [0.5, 0.5, 0.35, 0]);
            gl.uniform3f(uniforms.uShimmerParams, 14, 2.2, 0);
        }

        // 2. Particle Distortion & Lensing (Layer 218) + Screen-Space Glitch (Layer 217)
        let lensCount = 0, glitchCount = 0;
        this.lensScratchA.fill(0); this.lensScratchB.fill(0);
        this.glitchScratchA.fill(0); this.glitchScratchB.fill(0);
        for (const node of scene.Particles || [])
        {
            if (!node.Visible || !node.Enabled || HaltedParticleSystems.has(node)) continue;
            const proj = this.projectWorldSphere(node.Position, Math.max(0.25, node.Radius * 1.35), camera, matrices.viewProjection);
            if (!proj) continue;
            const dMode = Math.max(0, ParticleDistortionModes.indexOf(node.DistortionMode));
            if (dMode > 0 && (node.DistortionStrength || 0) > 0.0005 && lensCount < 16)
            {
                const lensRad = Math.max(0.03, (node.DistortionRadius ?? 0.28) * Math.min(1.6, proj.screenRadius * 3.2));
                const horizon = (node.DistortionHorizon ?? 0) * Math.min(1.6, proj.screenRadius * 3.2);
                this.lensScratchA.set([proj.uvX, proj.uvY, node.DistortionStrength, lensRad], lensCount * 4);
                this.lensScratchB.set([dMode, horizon, node.DistortionChromatic ?? 0, 0], lensCount * 4);
                lensCount += 1;
            }
            if ((node.GlitchAmount || 0) > 0.001 && ((node.GlitchScanlines || 0) > 0.001 || (node.GlitchChromatic || 0) > 0.001) && glitchCount < 16)
            {
                this.glitchScratchA.set([proj.uvX, proj.uvY, Math.max(0.08, proj.screenRadius * 1.25), node.GlitchAmount], glitchCount * 4);
                this.glitchScratchB.set([node.GlitchRate ?? 14, node.GlitchScanlines ?? 0, node.GlitchChromatic ?? 0, 0], glitchCount * 4);
                glitchCount += 1;
            }
        }
        gl.uniform1i(uniforms.uLensCount, lensCount);
        gl.uniform4fv(uniforms.uLensA, this.lensScratchA);
        gl.uniform4fv(uniforms.uLensB, this.lensScratchB);
        gl.uniform1i(uniforms.uGlitchCount, glitchCount);
        gl.uniform4fv(uniforms.uGlitchA, this.glitchScratchA);
        gl.uniform4fv(uniforms.uGlitchB, this.glitchScratchB);
        gl.disable(gl.BLEND);
        context.dispatch(finalTarget);
    }

    /**
     * Draws one frame. `target` (optional) redirects it into an offscreen framebuffer — the flipbook bake uses this with
     * { framebuffer, width, height, transparent } and gets no gizmos, so frames hold only what the scene shows.
     */
    render(scene, camera, selection, deltaTime, target = null)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        if (!solver.fields) return;
        const matrices = camera.matrices();
        const canvas = context.canvas;
        this.viewportSize = target ? [target.width, target.height] : [canvas.width, canvas.height];
        this.transparent = Boolean(target && target.transparent);
        const usePost = this.hasActivePostOverlay(scene);
        const sceneTarget = usePost ? this.ensurePostTarget(this.viewportSize[0], this.viewportSize[1]) : target;
        this.bakeTarget = sceneTarget;
        gl.bindFramebuffer(gl.FRAMEBUFFER, sceneTarget ? sceneTarget.framebuffer : null);
        gl.viewport(0, 0, this.viewportSize[0], this.viewportSize[1]);
        if (sceneTarget) { gl.clearColor(0, 0, 0, 0); gl.clear(gl.COLOR_BUFFER_BIT); }
        gl.disable(gl.DEPTH_TEST);
        gl.disable(gl.BLEND);

        this.renderScene(scene, camera, matrices, selection);

        gl.enable(gl.BLEND);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        const visualizers = scene.Visualizers;
        if (visualizers.Debris && visualizers.Debris.Visible) this.renderDebris(visualizers.Debris, matrices);
        if (this.particles && scene.Particles) this.particles.render(scene, matrices, typeof window !== 'undefined' ? Math.min(window.devicePixelRatio || 1, 1.5) : 1, camera);

        if (usePost)
        {
            this.renderPostOverlay(scene, camera, matrices, sceneTarget, target);
            gl.bindFramebuffer(gl.FRAMEBUFFER, target ? target.framebuffer : null);
            gl.viewport(0, 0, this.viewportSize[0], this.viewportSize[1]);
            gl.enable(gl.BLEND);
        }

        // Diagnostic overlays & gizmos stay crisp above post-refraction and are strictly off unless explicitly enabled.
        const diagnostics = !target;
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        if (diagnostics && visualizers.ScalarSlice.Visible) this.renderSlice(visualizers.ScalarSlice, matrices);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE);
        if (diagnostics && visualizers.VelocityVectors.Visible) this.renderVectors(visualizers.VelocityVectors, matrices);
        if (diagnostics && visualizers.ForceVectors && visualizers.ForceVectors.Visible) this.renderForceVectors(visualizers.ForceVectors, matrices);
        if (diagnostics && visualizers.Tracers.Visible) this.renderTracers(visualizers.Tracers, matrices, deltaTime);
        gl.blendFunc(gl.SRC_ALPHA, gl.ONE_MINUS_SRC_ALPHA);
        if (!target) this.renderGizmos(scene, matrices, selection);
        gl.disable(gl.BLEND);
        if (target) gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        return matrices;
    }

    /** 8-bit colour target for flipbook frames (readPixels needs RGBA8, not the solver's float formats). */
    createBakeTarget(size)
    {
        const gl = this.gl;
        const texture = gl.createTexture();
        gl.bindTexture(gl.TEXTURE_2D, texture);
        gl.texStorage2D(gl.TEXTURE_2D, 1, gl.RGBA8, size, size);
        const framebuffer = gl.createFramebuffer();
        gl.bindFramebuffer(gl.FRAMEBUFFER, framebuffer);
        gl.framebufferTexture2D(gl.FRAMEBUFFER, gl.COLOR_ATTACHMENT0, gl.TEXTURE_2D, texture, 0);
        const status = gl.checkFramebufferStatus(gl.FRAMEBUFFER);
        gl.bindFramebuffer(gl.FRAMEBUFFER, null);
        if (status !== gl.FRAMEBUFFER_COMPLETE) { gl.deleteFramebuffer(framebuffer); gl.deleteTexture(texture); throw new Error('Flipbook frame target incomplete: 0x' + status.toString(16)); }
        return { texture, framebuffer, width: size, height: size };
    }

    destroyBakeTarget(target)
    {
        if (!target) return;
        this.gl.deleteFramebuffer(target.framebuffer);
        this.gl.deleteTexture(target.texture);
    }

    renderScene(scene, camera, matrices, selection)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const volume = scene.Visualizers.Volume;
        const uniforms = context.use(this.programs.scene);
        solver.applyVolumeUniforms(uniforms);
        solver.applyColliderUniforms(uniforms);
        context.bindTexture('uState', solver.fields.state.read.texture);
        context.bindTexture('uPressure', solver.fields.pressure.read.texture);
        gl.uniform1f(uniforms.uFireLight, volume.FireLight ?? 1.0);
        const position = camera.position;
        gl.uniform3f(uniforms.uCameraPosition, position[0], position[1], position[2]);
        gl.uniformMatrix4fv(uniforms.uInverseViewProjection, false, matrices.inverseViewProjection);
        gl.uniform2f(uniforms.uViewport, this.viewportSize[0], this.viewportSize[1]);
        gl.uniform3fv(uniforms.uLightDirection, this.lightDirection);
        gl.uniform3fv(uniforms.uLightColour, this.lightColour);
        gl.uniform3fv(uniforms.uAlbedo, hexToRgb(volume.Albedo));
        gl.uniform1f(uniforms.uDensityScale, volume.Density);
        gl.uniform1f(uniforms.uAbsorption, volume.Absorption);
        gl.uniform1f(uniforms.uFireIntensity, volume.FireIntensity);
        gl.uniform3fv(uniforms.uFireEmber, hexToRgb(volume.FireEmber));
        gl.uniform3fv(uniforms.uFireFlame, hexToRgb(volume.FireFlame));
        gl.uniform3fv(uniforms.uFireCore, hexToRgb(volume.FireCore));
        gl.uniform1f(uniforms.uFireThreshold, volume.FireThreshold);
        gl.uniform1f(uniforms.uFireSoftness, volume.FireSoftness);
        gl.uniform1f(uniforms.uFireOpacity, volume.FireOpacity);
        gl.uniform1f(uniforms.uExposure, volume.Exposure);
        gl.uniform1f(uniforms.uAnisotropy, Math.min(0.95, volume.Anisotropy));
        gl.uniform1f(uniforms.uLightIntensity, volume.LightIntensity);
        gl.uniform3fv(uniforms.uAmbientColour, hexToRgb(volume.Ambient));
        gl.uniform1i(uniforms.uSteps, Math.round(volume.Steps));
        gl.uniform1i(uniforms.uShadowSteps, Math.round(volume.ShadowSteps));
        gl.uniform1f(uniforms.uGroundHeight, solver.sparse ? -0.02 : solver.domainCentre[1] - solver.domainHalfExtent[1] - 0.02);
        gl.uniform1i(uniforms.uShowVolume, volume.Visible ? 1 : 0);
        gl.uniform1i(uniforms.uTransparent, this.transparent ? 1 : 0);
        gl.uniform1i(uniforms.uVoxelPreview, VolumeFilters.indexOf(volume.Filter) === 1 ? 1 : 0);
        gl.uniform1i(uniforms.uFrame, solver.frame & 1023);
        const selectedCollider = selection ? scene.Colliders.filter((c) => c.Enabled).findIndex((c) => c.Id === selection) : -1;
        gl.uniform1i(uniforms.uSelectedCollider, selectedCollider);
        context.dispatch(this.bakeTarget);
    }

    renderVectors(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver, layout = solver.layout;
        const axis = Math.max(0, SliceAxes.indexOf(settings.Axis));
        const axisCells = [layout.width, layout.height, layout.depth][axis];
        const slice = Math.min(axisCells - 1, Math.max(0, Math.floor(settings.Offset * axisCells)));
        const stride = Math.max(1, Math.round(settings.Stride));
        const plane = axis === 0 ? [layout.height, layout.depth] : axis === 1 ? [layout.width, layout.depth] : [layout.width, layout.height];
        const columns = Math.ceil(plane[0] / stride), rows = Math.ceil(plane[1] / stride);
        const arrows = columns * rows;
        const uniforms = context.use(this.programs.vectors);
        solver.applyVolumeUniforms(uniforms);
        context.bindTexture('uVelocity', solver.fields.velocity.read.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform1i(uniforms.uAxis, axis);
        gl.uniform1i(uniforms.uSlice, slice);
        gl.uniform1i(uniforms.uStride, stride);
        gl.uniform1f(uniforms.uScale, settings.Scale);
        gl.uniform1f(uniforms.uMaxSpeed, settings.MaxSpeed);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uHeads, 0);
        gl.drawArrays(gl.LINES, 0, arrows * 2);
        gl.uniform1i(uniforms.uHeads, 1);
        gl.drawArrays(gl.POINTS, 0, arrows);
        gl.bindVertexArray(null);
    }

    renderForceVectors(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver, layout = solver.layout;
        const stride = Math.max(1, Math.round(settings.Stride));
        const mode = Math.max(0, VectorModes.indexOf(settings.Mode));
        const axis = Math.max(0, SliceAxes.indexOf(settings.Axis));
        const axisCells = [layout.width, layout.height, layout.depth][axis];
        const slice = Math.min(axisCells - 1, Math.max(0, Math.floor(settings.Offset * axisCells)));
        let arrows;
        if (mode === 0)
        {
            const plane = axis === 0 ? [layout.height, layout.depth] : axis === 1 ? [layout.width, layout.depth] : [layout.width, layout.height];
            arrows = Math.ceil(plane[0] / stride) * Math.ceil(plane[1] / stride);
        }
        else arrows = Math.ceil(layout.width / stride) * Math.ceil(layout.height / stride) * Math.ceil(layout.depth / stride);
        const uniforms = context.use(this.programs.forceVectors);
        solver.applyVolumeUniforms(uniforms);
        solver.applyForceUniforms(uniforms);
        context.bindTexture('uVelocity', solver.fields.velocity.read.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform1i(uniforms.uMode, mode);
        gl.uniform1i(uniforms.uAxis, axis);
        gl.uniform1i(uniforms.uSlice, slice);
        gl.uniform1i(uniforms.uStride, stride);
        gl.uniform1f(uniforms.uScale, settings.Scale);
        gl.uniform1f(uniforms.uMaxForce, settings.MaxForce);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uHeads, 0);
        gl.drawArrays(gl.LINES, 0, arrows * 2);
        gl.uniform1i(uniforms.uHeads, 1);
        gl.drawArrays(gl.POINTS, 0, arrows);
        gl.bindVertexArray(null);
    }

    renderSlice(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const half = solver.domainHalfExtent, centre = solver.domainCentre;
        const axis = Math.max(0, SliceAxes.indexOf(settings.Axis));
        const offset = centre[axis] + (settings.Offset * 2 - 1) * half[axis];
        let corner, edgeU, edgeV;
        if (axis === 0) { corner = [offset, centre[1] - half[1], centre[2] - half[2]]; edgeU = [0, 2 * half[1], 0]; edgeV = [0, 0, 2 * half[2]]; }
        else if (axis === 1) { corner = [centre[0] - half[0], offset, centre[2] - half[2]]; edgeU = [2 * half[0], 0, 0]; edgeV = [0, 0, 2 * half[2]]; }
        else { corner = [centre[0] - half[0], centre[1] - half[1], offset]; edgeU = [2 * half[0], 0, 0]; edgeV = [0, 2 * half[1], 0]; }
        const uniforms = context.use(this.programs.slice);
        solver.applyVolumeUniforms(uniforms);
        solver.applyForceUniforms(uniforms);
        const fields = solver.fields;
        context.bindTexture('uVelocity', fields.velocity.read.texture);
        context.bindTexture('uState', fields.state.read.texture);
        context.bindTexture('uPressure', fields.pressure.read.texture);
        context.bindTexture('uCurl', fields.curl.texture);
        context.bindTexture('uDivergence', fields.divergence.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform3fv(uniforms.uCorner, corner);
        gl.uniform3fv(uniforms.uEdgeU, edgeU);
        gl.uniform3fv(uniforms.uEdgeV, edgeV);
        gl.uniform1i(uniforms.uChannel, Math.max(0, ScalarChannels.indexOf(settings.Channel)));
        gl.uniform1f(uniforms.uRange, Math.max(1e-3, settings.Range));
        gl.uniform1f(uniforms.uOpacity, settings.Opacity);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.drawArrays(gl.TRIANGLES, 0, 6);
        gl.bindVertexArray(null);
    }

    renderTracers(settings, matrices, deltaTime)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const count = Math.min(solver.tracerCount, Math.max(0, Math.round(settings.Count)));
        if (count === 0) return;
        const uniforms = context.use(this.programs.tracers);
        context.bindTexture('uTracers', solver.tracers.read.texture);
        context.bindTexture('uPrevious', solver.tracerPrevious.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform2i(uniforms.uTracerSize, solver.tracerSize[0], solver.tracerSize[1]);
        gl.uniform1f(uniforms.uMaxSpeed, settings.MaxSpeed);
        gl.uniform1f(uniforms.uPointSize, settings.Size * (typeof window !== "undefined" ? Math.min(window.devicePixelRatio || 1, 1.5) : 1));
        gl.uniform1f(uniforms.uDeltaTime, Math.max(deltaTime, 1e-4));
        gl.uniform1f(uniforms.uRespawnDistance, solver.cellSize * 10);
        gl.uniform1f(uniforms.uLifetime, settings.Lifetime);
        gl.uniform1i(uniforms.uRampMode, Math.max(0, ColourRamps.indexOf(settings.Ramp)));
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uTrail, 1);
        gl.drawArrays(gl.LINES, 0, count * 2);
        gl.uniform1i(uniforms.uTrail, 0);
        gl.drawArrays(gl.POINTS, 0, count);
        gl.bindVertexArray(null);
    }

    renderDebris(settings, matrices)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const count = Math.min(solver.debrisCount, Math.max(0, Math.round(settings.Count)));
        if (count === 0) return;
        const uniforms = context.use(this.programs.debris);
        context.bindTexture('uPositions', solver.debrisPositions.read.texture);
        context.bindTexture('uVelocities', solver.debrisVelocities.read.texture);
        context.bindTexture('uPrevious', solver.debrisPrevious.texture);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.uniform2i(uniforms.uParticleSize, solver.debrisSize[0], solver.debrisSize[1]);
        gl.uniform1f(uniforms.uPointSize, settings.Size * (typeof window !== "undefined" ? Math.min(window.devicePixelRatio || 1, 1.5) : 1));
        gl.uniform1f(uniforms.uLifetime, settings.Lifetime);
        gl.uniform1f(uniforms.uGlow, settings.Glow);
        gl.bindVertexArray(this.emptyVao);
        gl.uniform1i(uniforms.uTrail, 1);
        gl.drawArrays(gl.LINES, 0, count * 2);
        gl.uniform1i(uniforms.uTrail, 0);
        gl.drawArrays(gl.POINTS, 0, count);
        gl.bindVertexArray(null);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  GIZMO LINE BATCH
    //------------------------------------------------------------------------------------------------------------------------

    beginLines()
    {
        this.lineVertexCount = 0;
    }

    pushVertex(p, colour)
    {
        if ((this.lineVertexCount + 1) * 7 > this.lineScratch.length)
        {
            const grown = new Float32Array(this.lineScratch.length * 2);
            grown.set(this.lineScratch);
            this.lineScratch = grown;
        }
        const o = this.lineVertexCount * 7;
        this.lineScratch[o] = p[0]; this.lineScratch[o + 1] = p[1]; this.lineScratch[o + 2] = p[2];
        this.lineScratch[o + 3] = colour[0]; this.lineScratch[o + 4] = colour[1]; this.lineScratch[o + 5] = colour[2]; this.lineScratch[o + 6] = colour[3];
        this.lineVertexCount += 1;
    }

    line(a, b, colour) { this.pushVertex(a, colour); this.pushVertex(b, colour); }

    circle(centre, axis, radius, colour, segments = 40)
    {
        const u = Vec3.anyPerpendicular(axis);
        const v = Vec3.normalize(Vec3.cross(Vec3.normalize(axis), u));
        let previous = null;
        for (let i = 0; i <= segments; i += 1)
        {
            const angle = (i / segments) * Math.PI * 2;
            const point = Vec3.add(centre, Vec3.add(Vec3.scale(u, Math.cos(angle) * radius), Vec3.scale(v, Math.sin(angle) * radius)));
            if (previous) this.line(previous, point, colour);
            previous = point;
        }
    }

    arrow(origin, direction, length, colour)
    {
        const d = Vec3.normalize(direction);
        const tip = Vec3.add(origin, Vec3.scale(d, length));
        this.line(origin, tip, colour);
        const side = Vec3.anyPerpendicular(d);
        const side2 = Vec3.cross(d, side);
        const back = Vec3.sub(tip, Vec3.scale(d, length * 0.18));
        const wing = length * 0.07;
        for (const s of [side, Vec3.scale(side, -1), side2, Vec3.scale(side2, -1)]) this.line(tip, Vec3.add(back, Vec3.scale(s, wing)), colour);
    }

    box(centre, half, colour)
    {
        const corners = [];
        for (let i = 0; i < 8; i += 1) corners.push([centre[0] + (i & 1 ? half[0] : -half[0]), centre[1] + (i & 2 ? half[1] : -half[1]), centre[2] + (i & 4 ? half[2] : -half[2])]);
        const edges = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
        for (const [a, b] of edges) this.line(corners[a], corners[b], colour);
    }

    sphereWire(centre, radius, colour)
    {
        this.circle(centre, [1, 0, 0], radius, colour, 32);
        this.circle(centre, [0, 1, 0], radius, colour, 32);
        this.circle(centre, [0, 0, 1], radius, colour, 32);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  GIZMO PRIMITIVES
    //------------------------------------------------------------------------------------------------------------------------

    dashedCircle(centre, axis, radius, colour, dashes = 24)
    {
        const u = Vec3.anyPerpendicular(axis);
        const v = Vec3.normalize(Vec3.cross(Vec3.normalize(axis), u));
        for (let i = 0; i < dashes; i += 1)
        {
            const a0 = (i / dashes) * Math.PI * 2, a1 = ((i + 0.55) / dashes) * Math.PI * 2;
            const p0 = Vec3.add(centre, Vec3.add(Vec3.scale(u, Math.cos(a0) * radius), Vec3.scale(v, Math.sin(a0) * radius)));
            const p1 = Vec3.add(centre, Vec3.add(Vec3.scale(u, Math.cos(a1) * radius), Vec3.scale(v, Math.sin(a1) * radius)));
            this.line(p0, p1, colour);
        }
    }

    chevron(point, direction, size, colour)
    {
        const d = Vec3.normalize(direction);
        const side = Vec3.anyPerpendicular(d);
        const side2 = Vec3.cross(d, side);
        const back = Vec3.sub(point, Vec3.scale(d, size));
        for (const s of [side, Vec3.scale(side, -1), side2, Vec3.scale(side2, -1)]) this.line(point, Vec3.add(back, Vec3.scale(s, size * 0.55)), colour);
    }

    polyline(points, colour)
    {
        for (let i = 1; i < points.length; i += 1) this.line(points[i - 1], points[i], colour);
    }

    // Arrowheads on a circle showing the sense of rotation about `axis`.
    rotationMarks(centre, axis, radius, colour, count = 4, sign = 1)
    {
        const u = Vec3.anyPerpendicular(axis);
        const v = Vec3.normalize(Vec3.cross(Vec3.normalize(axis), u));
        for (let i = 0; i < count; i += 1)
        {
            const angle = (i / count) * Math.PI * 2;
            const point = Vec3.add(centre, Vec3.add(Vec3.scale(u, Math.cos(angle) * radius), Vec3.scale(v, Math.sin(angle) * radius)));
            const tangent = Vec3.scale(Vec3.add(Vec3.scale(u, -Math.sin(angle)), Vec3.scale(v, Math.cos(angle))), sign);
            this.chevron(point, tangent, radius * 0.16, colour);
        }
    }

    helix(centre, axis, radius, height, turns, colour, phase = 0, segments = 64)
    {
        const a = Vec3.normalize(axis);
        const u = Vec3.anyPerpendicular(a), v = Vec3.cross(a, u);
        const points = [];
        for (let i = 0; i <= segments; i += 1)
        {
            const t = i / segments;
            const angle = t * Math.PI * 2 * turns + phase;
            points.push(Vec3.add(centre, Vec3.add(Vec3.scale(a, (t - 0.5) * height), Vec3.add(Vec3.scale(u, Math.cos(angle) * radius), Vec3.scale(v, Math.sin(angle) * radius)))));
        }
        this.polyline(points, colour);
        return points;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  FORCE GIZMOS
    //------------------------------------------------------------------------------------------------------------------------

    forceGizmo(force, selected, opacity, time)
    {
        const type = ForceTypeById[force.Type];
        const tint = hexToRgb(type.Tint);
        const alpha = (force.Enabled ? 1 : 0.35) * opacity;
        const colour = selected ? [1, 1, 1, alpha] : [tint[0], tint[1], tint[2], alpha * 0.9];
        const faint = [colour[0], colour[1], colour[2], alpha * 0.32];
        const c = force.Position, r = force.Radius, axis = Vec3.normalize(force.Direction);
        const u = Vec3.anyPerpendicular(axis), v = Vec3.cross(axis, u);
        const infinite = force.Falloff === 'Constant';
        const sign = force.Strength >= 0 ? 1 : -1;
        const half = this.solver.domainHalfExtent;
        switch (type.Id)
        {
            case 'DirectionalWind':
            {
                // 3×3 bundle of streamlines with chevrons sliding along them: a wind field, not a single arrow.
                const length = infinite ? Math.max(half[0], half[1], half[2]) * 1.2 : r * 1.6;
                const spacing = infinite ? 0.45 : r * 0.35;
                const slide = ((time * 0.9) % 1 + 1) % 1;
                for (let i = -1; i <= 1; i += 1) for (let j = -1; j <= 1; j += 1)
                {
                    const offset = Vec3.add(Vec3.scale(u, i * spacing), Vec3.scale(v, j * spacing));
                    const start = Vec3.add(Vec3.sub(c, Vec3.scale(axis, length * 0.5)), offset);
                    const end = Vec3.add(start, Vec3.scale(axis, length * sign));
                    const streak = i === 0 && j === 0 ? colour : faint;
                    this.line(start, end, streak);
                    for (let k = 0; k < 3; k += 1)
                    {
                        const t = ((k + slide) / 3);
                        this.chevron(Vec3.add(start, Vec3.scale(axis, length * sign * t)), Vec3.scale(axis, sign), Math.min(0.14, length * 0.06), streak);
                    }
                }
                if (!infinite) this.dashedCircle(c, axis, r, faint, 32);
                else this.circle(c, axis, spacing * 1.6, faint, 24);
                break;
            }
            case 'PointAttractor':
            case 'PointRepulsor':
            {
                const inward = (type.Id === 'PointAttractor') === (sign > 0);
                this.dashedCircle(c, [0, 1, 0], r, faint, 36);
                this.dashedCircle(c, [1, 0, 0], r, faint, 36);
                this.dashedCircle(c, [0, 0, 1], r, faint, 36);
                this.sphereWire(c, r * 0.1, colour);
                // Pulse rings travel in (attractor) or out (repulsor).
                const phase = ((time * 0.6) % 1 + 1) % 1;
                for (let k = 0; k < 3; k += 1)
                {
                    const t = (k / 3 + (inward ? 1 - phase : phase)) % 1;
                    const ringRadius = r * (0.15 + 0.85 * t);
                    const fade = [colour[0], colour[1], colour[2], alpha * (inward ? 0.15 + 0.5 * (1 - t) : 0.15 + 0.5 * (1 - t))];
                    this.circle(c, [0, 1, 0], ringRadius, fade, 36);
                }
                // 14 radial spokes with heads showing direction of travel.
                const directions = [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]];
                for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1]) directions.push(Vec3.normalize([sx, sy, sz]));
                for (const d of directions)
                {
                    const outer = Vec3.add(c, Vec3.scale(d, r * 0.92)), inner = Vec3.add(c, Vec3.scale(d, r * 0.22));
                    this.line(inner, outer, faint);
                    this.chevron(inward ? inner : outer, Vec3.scale(d, inward ? -1 : 1), r * 0.1, colour);
                }
                break;
            }
            case 'VortexColumn':
            {
                const height = r * 2.0;
                const top = Vec3.add(c, Vec3.scale(axis, height * 0.5)), bottom = Vec3.sub(c, Vec3.scale(axis, height * 0.5));
                this.circle(top, axis, r, faint, 40);
                this.circle(bottom, axis, r, faint, 40);
                this.dashedCircle(c, axis, r, faint, 32);
                this.line(bottom, top, faint);
                // Two counter-phased funnel helices (tighter at the base when lift is present).
                const spin = time * 1.2 * sign;
                for (const phase of [0, Math.PI])
                {
                    const points = [];
                    for (let i = 0; i <= 72; i += 1)
                    {
                        const t = i / 72;
                        const radius = r * (0.35 + 0.6 * t);
                        const angle = t * Math.PI * 5 * sign + phase + spin;
                        points.push(Vec3.add(bottom, Vec3.add(Vec3.scale(axis, t * height), Vec3.add(Vec3.scale(u, Math.cos(angle) * radius), Vec3.scale(v, Math.sin(angle) * radius)))));
                    }
                    this.polyline(points, colour);
                    this.chevron(points[points.length - 1], Vec3.sub(points[points.length - 1], points[points.length - 2]), r * 0.12, colour);
                }
                this.rotationMarks(top, axis, r, colour, 4, sign);
                if (force.Secondary > 0.01) this.arrow(Vec3.add(c, Vec3.scale(axis, -height * 0.15)), axis, height * 0.4, colour);
                break;
            }
            case 'CurlNoiseTurbulence':
            {
                this.dashedCircle(c, [0, 1, 0], r, faint, 40);
                this.dashedCircle(c, [1, 0, 0], r, faint, 40);
                // Several wandering curls inside the reach: random-looking but deterministic swirls.
                for (let k = 0; k < 6; k += 1)
                {
                    const seed = k * 2.39996;
                    const centre = Vec3.add(c, [Math.sin(seed) * r * 0.5, Math.cos(seed * 1.7) * r * 0.45, Math.sin(seed * 0.9 + 1.0) * r * 0.5]);
                    const swirlAxis = Vec3.normalize([Math.cos(seed * 1.3), Math.sin(seed * 0.7) + 0.4, Math.cos(seed * 2.1)]);
                    const points = this.helix(centre, swirlAxis, r * 0.16, r * 0.18, 1.6, colour, time * 1.5 + seed, 28);
                    this.chevron(points[points.length - 1], Vec3.sub(points[points.length - 1], points[points.length - 2]), r * 0.06, colour);
                }
                break;
            }
            case 'DragBaffle':
            {
                this.dashedCircle(c, [0, 1, 0], r, faint, 36);
                this.dashedCircle(c, [1, 0, 0], r, faint, 36);
                this.dashedCircle(c, [0, 0, 1], r, faint, 36);
                // Louvre slats: flow enters fast on the left and leaves slow on the right.
                for (let i = -2; i <= 2; i += 1)
                {
                    const y = i * r * 0.3;
                    for (const z of [-r * 0.35, 0, r * 0.35])
                    {
                        const a = Vec3.add(c, [-r * 0.45, y, z]), b = Vec3.add(c, [r * 0.45, y, z]);
                        this.line(a, b, faint);
                    }
                }
                for (let i = -1; i <= 1; i += 1)
                {
                    const y = i * r * 0.45;
                    const start = Vec3.add(c, [-r * 1.0, y, 0]);
                    this.arrow(start, [1, 0, 0], r * 0.5, colour);
                    this.arrow(Vec3.add(c, [r * 0.55, y, 0]), [1, 0, 0], r * 0.22, colour);
                    this.chevron(Vec3.add(c, [r * 0.2, y, 0]), [1, 0, 0], r * 0.07, faint);
                    this.chevron(Vec3.add(c, [r * 0.34, y, 0]), [1, 0, 0], r * 0.06, faint);
                }
                break;
            }
            case 'RadialBurst':
            {
                // Expanding shock shells synced to the pulse period, plus a detonation star.
                const period = Math.max(force.Frequency, 0.05);
                const phase = ((time / period) % 1 + 1) % 1;
                this.dashedCircle(c, [0, 1, 0], r, faint, 40);
                this.dashedCircle(c, [1, 0, 0], r, faint, 40);
                for (let k = 0; k < 2; k += 1)
                {
                    const t = (phase + k * 0.5) % 1;
                    const shell = r * (0.08 + 0.92 * t);
                    const fade = [colour[0], colour[1], colour[2], alpha * (1 - t) * 0.9];
                    this.circle(c, [0, 1, 0], shell, fade, 48);
                    this.circle(c, [1, 0, 0], shell, fade, 48);
                    this.circle(c, [0, 0, 1], shell, fade, 48);
                }
                for (let i = 0; i < 12; i += 1)
                {
                    const theta = (i / 12) * Math.PI * 2;
                    const d = Vec3.normalize([Math.cos(theta), Math.sin(theta * 2.0) * 0.6, Math.sin(theta)]);
                    const spike = r * (0.25 + 0.2 * ((i % 3) / 2));
                    this.line(Vec3.add(c, Vec3.scale(d, r * 0.05)), Vec3.add(c, Vec3.scale(d, spike)), colour);
                    this.chevron(Vec3.add(c, Vec3.scale(d, spike)), d, r * 0.06, colour);
                }
                break;
            }
            case 'ThermalUpdraft':
            {
                const height = r * 2.6;
                const bottom = Vec3.sub(c, Vec3.scale(axis, height * 0.5));
                for (const k of [0, 0.5, 1]) this.circle(Vec3.add(bottom, Vec3.scale(axis, k * height)), axis, r, k === 0.5 ? faint : colour, 36);
                for (let i = 0; i < 4; i += 1)
                {
                    const theta = (i / 4) * Math.PI * 2;
                    const foot = Vec3.add(bottom, Vec3.add(Vec3.scale(u, Math.cos(theta) * r), Vec3.scale(v, Math.sin(theta) * r)));
                    this.line(foot, Vec3.add(foot, Vec3.scale(axis, height)), faint);
                }
                // Wavy heat-shimmer streamers rising with the flow.
                const rise = ((time * 0.7) % 1 + 1) % 1;
                for (let i = 0; i < 5; i += 1)
                {
                    const theta = (i / 5) * Math.PI * 2 + 0.4;
                    const base = Vec3.add(bottom, Vec3.add(Vec3.scale(u, Math.cos(theta) * r * 0.55), Vec3.scale(v, Math.sin(theta) * r * 0.55)));
                    const points = [];
                    for (let k = 0; k <= 24; k += 1)
                    {
                        const t = k / 24;
                        const wobble = Math.sin(t * Math.PI * 3 + rise * Math.PI * 2 + i) * r * 0.12;
                        points.push(Vec3.add(base, Vec3.add(Vec3.scale(axis, t * height * sign + (sign > 0 ? 0 : height)), Vec3.scale(u, wobble))));
                    }
                    this.polyline(points, colour);
                    this.chevron(points[points.length - 1], Vec3.scale(axis, sign), r * 0.12, colour);
                }
                break;
            }
            case 'AxisAttractor':
            {
                const length = r * 3.0;
                const bottom = Vec3.sub(c, Vec3.scale(axis, length * 0.5));
                this.line(bottom, Vec3.add(bottom, Vec3.scale(axis, length)), colour);
                this.chevron(Vec3.add(bottom, Vec3.scale(axis, length)), axis, r * 0.1, faint);
                this.chevron(bottom, Vec3.scale(axis, -1), r * 0.1, faint);
                // Rings at three heights with spokes collapsing toward the line.
                const pull = ((time * 0.6) % 1 + 1) % 1;
                for (const k of [0.15, 0.5, 0.85])
                {
                    const ringCentre = Vec3.add(bottom, Vec3.scale(axis, k * length));
                    this.dashedCircle(ringCentre, axis, r, faint, 32);
                    this.circle(ringCentre, axis, r * (sign > 0 ? 1 - pull * 0.85 : 0.15 + pull * 0.85), [colour[0], colour[1], colour[2], alpha * 0.4], 32);
                    for (let i = 0; i < 6; i += 1)
                    {
                        const theta = (i / 6) * Math.PI * 2;
                        const d = Vec3.add(Vec3.scale(u, Math.cos(theta)), Vec3.scale(v, Math.sin(theta)));
                        const outer = Vec3.add(ringCentre, Vec3.scale(d, r * 0.95)), inner = Vec3.add(ringCentre, Vec3.scale(d, r * 0.1));
                        this.line(inner, outer, faint);
                        this.chevron(sign > 0 ? inner : outer, Vec3.scale(d, sign > 0 ? -1 : 1), r * 0.09, colour);
                    }
                }
                break;
            }
            case 'VortexRing':
            {
                const tube = Math.max(0.05, force.Frequency);
                this.circle(c, axis, r, colour, 64);
                this.circle(c, axis, r + tube, faint, 64);
                this.circle(c, axis, Math.max(0.01, r - tube), faint, 64);
                // Tube cross-sections carrying circulation arrows, plus the ring's travel direction.
                for (let i = 0; i < 8; i += 1)
                {
                    const theta = (i / 8) * Math.PI * 2;
                    const radialDirection = Vec3.add(Vec3.scale(u, Math.cos(theta)), Vec3.scale(v, Math.sin(theta)));
                    const ringPoint = Vec3.add(c, Vec3.scale(radialDirection, r));
                    const ringTangent = Vec3.cross(axis, radialDirection);
                    this.circle(ringPoint, ringTangent, tube, faint, 20);
                    this.rotationMarks(ringPoint, ringTangent, tube, colour, 2, sign);
                }
                this.arrow(c, Vec3.scale(axis, sign), r * 1.1, colour);
                break;
            }
            case 'Orbit':
            {
                this.circle(c, axis, r, colour, 56);
                this.circle(c, axis, r * 0.6, faint, 40);
                this.rotationMarks(c, axis, r, colour, 6, sign);
                this.rotationMarks(c, axis, r * 0.6, faint, 3, sign);
                this.sphereWire(c, r * 0.08, colour);
                const orbitPhase = time * 1.4 * sign;
                const satellite = Vec3.add(c, Vec3.add(Vec3.scale(u, Math.cos(orbitPhase) * r * 0.8), Vec3.scale(v, Math.sin(orbitPhase) * r * 0.8)));
                this.sphereWire(satellite, r * 0.05, colour);
                if (force.Secondary > 0.01) this.arrow(satellite, Vec3.sub(c, satellite), r * 0.3, faint);
                break;
            }
            case 'Containment':
            {
                this.sphereWire(c, r, colour);
                this.dashedCircle(c, [0, 1, 0], r * (1 - force.Secondary), faint, 36);
                const breath = r * (1.08 + 0.06 * Math.sin(time * 2.5));
                for (const sx of [-1, 1]) for (const sy of [-1, 1]) for (const sz of [-1, 1])
                {
                    const d = Vec3.normalize([sx, sy, sz]);
                    this.line(Vec3.add(c, Vec3.scale(d, breath + r * 0.25)), Vec3.add(c, Vec3.scale(d, breath)), faint);
                    this.chevron(Vec3.add(c, Vec3.scale(d, breath)), Vec3.scale(d, -1), r * 0.09, colour);
                }
                break;
            }
            case 'ShockwaveShell':
            {
                const period = Math.max(force.Frequency, 0.05);
                const phase = (((time - force.Tertiary) / period) % 1 + 1) % 1;
                this.dashedCircle(c, [0, 1, 0], r, faint, 48);
                this.dashedCircle(c, [1, 0, 0], r, faint, 48);
                const shell = r * Math.max(0.03, phase);
                const thick = Math.max(0.02, force.Secondary) * r;
                for (const k of [-1, 0, 1])
                {
                    const radius = Math.max(0.01, shell + k * thick * 0.5);
                    const fade = [colour[0], colour[1], colour[2], alpha * (k === 0 ? 0.95 : 0.35) * (1 - phase * 0.6)];
                    this.circle(c, [0, 1, 0], radius, fade, 56);
                    this.circle(c, [1, 0, 0], radius, fade, 56);
                    this.circle(c, [0, 0, 1], radius, fade, 56);
                }
                this.sphereWire(c, r * 0.05, colour);
                break;
            }
            case 'Implosion':
            {
                const period = Math.max(force.Frequency, 0.05);
                const phase = (((time - force.Tertiary) / period) % 1 + 1) % 1;
                this.dashedCircle(c, [0, 1, 0], r, faint, 40);
                this.dashedCircle(c, [1, 0, 0], r, faint, 40);
                for (let k = 0; k < 3; k += 1)
                {
                    const t = (phase + k / 3) % 1;
                    const radius = r * (1 - t) + r * 0.05;
                    const fade = [colour[0], colour[1], colour[2], alpha * (0.2 + 0.7 * t)];
                    this.circle(c, [0, 1, 0], radius, fade, 48);
                    this.circle(c, [0, 0, 1], radius, fade, 48);
                }
                for (const d of [[1, 0, 0], [-1, 0, 0], [0, 1, 0], [0, -1, 0], [0, 0, 1], [0, 0, -1]])
                {
                    this.line(Vec3.add(c, Vec3.scale(d, r)), Vec3.add(c, Vec3.scale(d, r * 0.15)), faint);
                    this.chevron(Vec3.add(c, Vec3.scale(d, r * 0.15)), Vec3.scale(d, -1), r * 0.09, colour);
                }
                break;
            }
            case 'DirectionalBurst':
            {
                const period = Math.max(force.Frequency, 0.05);
                const phase = (((time - force.Tertiary) / period) % 1 + 1) % 1;
                const pulse = Math.exp(-phase / Math.max(force.Secondary, 0.01));
                this.dashedCircle(c, axis, r, faint, 36);
                this.circle(c, axis, r * 0.35, faint, 24);
                this.line(Vec3.sub(c, Vec3.scale(axis, r * 0.6)), Vec3.add(c, Vec3.scale(axis, r * 0.6)), faint);
                const reach = r * (0.5 + 1.1 * (1 - Math.exp(-phase * 4)));
                for (let i = 0; i < 5; i += 1)
                {
                    const theta = (i / 5) * Math.PI * 2;
                    const offset = i === 0 ? [0, 0, 0] : Vec3.add(Vec3.scale(u, Math.cos(theta) * r * 0.3), Vec3.scale(v, Math.sin(theta) * r * 0.3));
                    const start = Vec3.add(c, offset);
                    this.arrow(start, Vec3.scale(axis, sign), reach, [colour[0], colour[1], colour[2], alpha * (0.25 + 0.75 * pulse)]);
                }
                break;
            }
            case 'BrownianJitter':
            {
                this.dashedCircle(c, [0, 1, 0], r, faint, 36);
                this.dashedCircle(c, [1, 0, 0], r, faint, 36);
                const roll = Math.floor(time * Math.max(force.Secondary, 0.1));
                for (let k = 0; k < 10; k += 1)
                {
                    let point = Vec3.add(c, [Math.sin(k * 2.1) * r * 0.5, Math.cos(k * 1.3) * r * 0.5, Math.sin(k * 0.7 + 2.0) * r * 0.5]);
                    const points = [point];
                    for (let step = 0; step < 5; step += 1)
                    {
                        const seed = k * 31 + step * 7 + roll * 13;
                        const kick = [Math.sin(seed * 12.9898) , Math.sin(seed * 78.233), Math.sin(seed * 37.719)];
                        point = Vec3.add(point, Vec3.scale(kick, r * 0.08));
                        points.push(point);
                    }
                    this.polyline(points, colour);
                }
                break;
            }
            case 'FlowStream':
            {
                const length = r * 1.8;
                this.circle(Vec3.sub(c, Vec3.scale(axis, length * 0.5)), axis, r, faint, 36);
                this.circle(Vec3.add(c, Vec3.scale(axis, length * 0.5)), axis, r, colour, 36);
                const slide = ((time * Math.max(force.Frequency, 0.3) * 0.3) % 1 + 1) % 1;
                for (let i = 0; i < 7; i += 1)
                {
                    const theta = (i / 7) * Math.PI * 2;
                    const radial = i === 6 ? 0 : r * 0.65;
                    const start = Vec3.add(Vec3.sub(c, Vec3.scale(axis, length * 0.5)), Vec3.add(Vec3.scale(u, Math.cos(theta) * radial), Vec3.scale(v, Math.sin(theta) * radial)));
                    this.line(start, Vec3.add(start, Vec3.scale(axis, length)), i === 6 ? colour : faint);
                    for (let k = 0; k < 4; k += 1) this.chevron(Vec3.add(start, Vec3.scale(axis, length * ((k + slide) / 4))), axis, r * 0.09, i === 6 ? colour : faint);
                }
                break;
            }
            case 'WindShear':
            {
                const height = r * 2.0;
                const bottom = Vec3.sub(c, Vec3.scale(axis, height * 0.5));
                this.line(bottom, Vec3.add(bottom, Vec3.scale(axis, height)), faint);
                const layers = 7;
                for (let i = 0; i < layers; i += 1)
                {
                    const h = i / (layers - 1) * 2 - 1;
                    const angle = h * force.Secondary * Math.PI;
                    const heading = Vec3.add(Vec3.scale(u, Math.cos(angle)), Vec3.scale(v, Math.sin(angle)));
                    const centre = Vec3.add(bottom, Vec3.scale(axis, (h * 0.5 + 0.5) * height));
                    this.arrow(Vec3.sub(centre, Vec3.scale(heading, r * 0.6)), Vec3.scale(heading, sign), r * 1.2, i === Math.floor(layers / 2) ? colour : faint);
                    this.dashedCircle(centre, axis, r * 0.25, faint, 12);
                }
                if (!infinite) this.dashedCircle(c, axis, r, faint, 40);
                break;
            }
            case 'Spin':
            {
                const spinPhase = time * 1.0 * sign;
                for (const k of [0.3, 0.65, 1.0])
                {
                    this.circle(c, axis, r * k, k === 1 ? colour : faint, 48);
                    this.rotationMarks(c, axis, r * k, colour, Math.round(2 + 4 * k), sign);
                }
                for (let i = 0; i < 4; i += 1)
                {
                    const theta = (i / 4) * Math.PI * 2 + spinPhase;
                    const d = Vec3.add(Vec3.scale(u, Math.cos(theta)), Vec3.scale(v, Math.sin(theta)));
                    this.line(c, Vec3.add(c, Vec3.scale(d, r)), faint);
                }
                this.line(Vec3.sub(c, Vec3.scale(axis, r * 0.5)), Vec3.add(c, Vec3.scale(axis, r * 0.5)), colour);
                break;
            }
            case 'MagneticDipole':
            {
                this.arrow(Vec3.sub(c, Vec3.scale(axis, r * 0.35)), axis, r * 0.7, colour);
                this.dashedCircle(c, [0, 1, 0], r, faint, 40);
                // Dipole field lines: r = L sin²θ in the planes containing the axis.
                for (let plane = 0; plane < 3; plane += 1)
                {
                    const theta0 = (plane / 3) * Math.PI;
                    const w = Vec3.add(Vec3.scale(u, Math.cos(theta0)), Vec3.scale(v, Math.sin(theta0)));
                    for (const L of [r * 0.45, r * 0.8, r * 1.15])
                    {
                        for (const flip of [1, -1])
                        {
                            const points = [];
                            for (let k = 0; k <= 36; k += 1)
                            {
                                const th = 0.12 + (k / 36) * (Math.PI - 0.24);
                                const rad = L * Math.sin(th) * Math.sin(th);
                                if (rad > r) continue;
                                points.push(Vec3.add(c, Vec3.add(Vec3.scale(axis, rad * Math.cos(th)), Vec3.scale(w, rad * Math.sin(th) * flip))));
                            }
                            this.polyline(points, L === r * 0.8 ? colour : faint);
                            if (points.length > 2) this.chevron(points[Math.floor(points.length / 2)], Vec3.scale(Vec3.sub(points[Math.floor(points.length / 2) - 1], points[Math.floor(points.length / 2)]), sign), r * 0.07, colour);
                        }
                    }
                }
                break;
            }
            default:
                this.sphereWire(c, r, colour);
        }
    }

    particleGizmo(node, selected, opacity, time)
    {
        const alpha = (node.Enabled ? 1 : 0.35) * opacity;
        const colour = selected ? [1, 1, 1, alpha] : [0.66, 0.55, 0.98, alpha * 0.9];
        const faint = [colour[0], colour[1], colour[2], alpha * 0.4];
        const c = node.Position, r = Math.max(0.05, node.Radius);
        const axis = Vec3.normalize(node.Direction);
        switch (node.Shape)
        {
            case 'Sphere':
            case 'Dome': this.sphereWire(c, r, colour); break;
            case 'Box':
            case 'SurfaceShell': this.box(c, [r, r, r], colour); break;
            case 'Grid':
                this.box(c, [r, r, r], colour);
                this.box(c, [r * 0.5, r * 0.5, r * 0.5], faint);
                break;
            case 'Torus':
                this.circle(c, axis, r * 1.25, colour, 48);
                this.circle(c, axis, r * 0.72, faint, 36);
                break;
            case 'MocapFigure':
                this.box(c, [r * 0.45, r * 0.92, r * 0.3], colour);
                this.circle(Vec3.add(c, [0, r * 0.78, 0]), [0, 1, 0], r * 0.14, faint, 24);
                break;
            case 'Disc': this.circle(c, axis, r, colour, 40); this.circle(c, axis, r * 0.5, faint, 24); break;
            case 'Ring': this.circle(c, axis, r, colour, 48); this.dashedCircle(c, axis, r * 1.08, faint, 32); break;
            case 'Line':
                this.polyline([Vec3.add(c, Vec3.scale(axis, -r)), Vec3.add(c, Vec3.scale(axis, r))], colour);
                this.chevron(Vec3.add(c, Vec3.scale(axis, r)), axis, 0.12, colour);
                this.chevron(Vec3.add(c, Vec3.scale(axis, -r)), Vec3.scale(axis, -1), 0.12, colour);
                break;
            default:
                this.polyline([Vec3.add(c, [-0.15, 0, 0]), Vec3.add(c, [0.15, 0, 0])], colour);
                this.polyline([Vec3.add(c, [0, -0.15, 0]), Vec3.add(c, [0, 0.15, 0])], colour);
                this.polyline([Vec3.add(c, [0, 0, -0.15]), Vec3.add(c, [0, 0, 0.15])], colour);
        }
        if (node.Mode === 'Emit' && node.Speed > 1e-3) this.arrow(c, axis, Math.min(1.2, 0.3 + node.Speed * 0.08), colour);
        if (node.Mode === 'Cloner')
        {
            // Breathing marker: cloners pulse so they read as "held formation" rather than a source.
            const pulse = 1 + 0.06 * Math.sin(time * 3);
            this.dashedCircle(c, [0, 1, 0], r * pulse * 1.15, faint, 28);
        }
    }

    emitterGizmo(emitter, selected, opacity, time = 0)
    {
        const alpha = (emitter.Enabled ? 1 : 0.35) * opacity;
        const colour = selected ? [1, 1, 1, alpha] : [1.0, 0.62, 0.25, alpha * 0.9];
        const faint = [colour[0], colour[1], colour[2], alpha * 0.35];
        const transform = emitterTransform(emitter, time);
        if (emitter.PathKind && emitter.PathKind !== 'None')
        {
            if (emitter.PathKind === 'Circle') this.dashedCircle(emitter.Position, [0, 1, 0], emitter.PathRadius, faint, 36);
            else if (emitter.PathKind === 'Line') this.polyline([Vec3.add(emitter.Position, [-emitter.PathRadius, 0, 0]), Vec3.add(emitter.Position, [emitter.PathRadius, 0, 0])], faint);
            else
            {
                const points = [];
                for (let i = 0; i <= 64; i += 1) { const a = (i / 64) * Math.PI * 2; points.push(Vec3.add(emitter.Position, [Math.sin(a) * emitter.PathRadius, 0, Math.sin(a * 2) * emitter.PathRadius * 0.5])); }
                this.polyline(points, faint);
            }
            if (transform.Heading) this.chevron(transform.Position, transform.Heading, 0.14, colour);
        }
        emitter = { ...emitter, Position: transform.Position, Velocity: transform.Velocity };
        const c = emitter.Position, r = emitter.Radius;
        if (emitter.Shape === 'Sphere') this.sphereWire(c, r, colour);
        else if (emitter.Shape === 'Disc')
        {
            this.circle(c, emitter.Orientation, r, colour, 40);
            this.circle(c, emitter.Orientation, r * 0.5, faint, 24);
        }
        else this.box(c, [r, r, r], colour);
        const speed = Vec3.length(emitter.Velocity);
        if (speed > 1e-3) this.arrow(c, emitter.Velocity, Math.min(1.2, 0.25 + speed * 0.2), colour);
    }

    /** World Grid lattice: seams between the tiles of the sliding window, plus the ring of neighbour tiles it can step into. */
    worldGridGizmo(domain, half, centre, selected)
    {
        const tiles = domain.Tiles || [1, 1, 1];
        if (tiles[0] * tiles[1] * tiles[2] <= 1 && domain.Follow === 'None') return;
        const pitch = [0, 1, 2].map((axis) => domain.TileCells[axis] * domain.CellSize);
        const floor = centre[1] - half[1] + 0.005;
        const seam = [0.62, 0.66, 0.74, selected ? 0.5 : 0.22];
        // Seams inside the window, drawn on the floor and up the walls so the tile structure reads in 3D.
        for (let i = 1; i < tiles[0]; i += 1)
        {
            const x = centre[0] - half[0] + i * pitch[0];
            this.line([x, floor, centre[2] - half[2]], [x, floor, centre[2] + half[2]], seam);
            this.line([x, floor, centre[2] - half[2]], [x, centre[1] + half[1], centre[2] - half[2]], seam);
            this.line([x, floor, centre[2] + half[2]], [x, centre[1] + half[1], centre[2] + half[2]], seam);
        }
        for (let k = 1; k < tiles[2]; k += 1)
        {
            const z = centre[2] - half[2] + k * pitch[2];
            this.line([centre[0] - half[0], floor, z], [centre[0] + half[0], floor, z], seam);
            this.line([centre[0] - half[0], floor, z], [centre[0] - half[0], centre[1] + half[1], z], seam);
            this.line([centre[0] + half[0], floor, z], [centre[0] + half[0], centre[1] + half[1], z], seam);
        }
        for (let j = 1; j < tiles[1]; j += 1)
        {
            const y = centre[1] - half[1] + j * pitch[1];
            this.box([centre[0], y, centre[2]], [half[0], 0, half[2]], seam);
        }
        // Neighbour tiles on the floor: the lattice the window slides across while following its target.
        if (domain.Follow === 'None') return;
        const ghost = [0.62, 0.66, 0.74, selected ? 0.16 : 0.07];
        for (let i = -1; i <= 1; i += 1) for (let k = -1; k <= 1; k += 1)
        {
            if (i === 0 && k === 0) continue;
            const cx = centre[0] + i * pitch[0] * tiles[0], cz = centre[2] + k * pitch[2] * tiles[2];
            const corners = [[cx - half[0], floor, cz - half[2]], [cx + half[0], floor, cz - half[2]], [cx + half[0], floor, cz + half[2]], [cx - half[0], floor, cz + half[2]]];
            for (let e = 0; e < 4; e += 1) this.line(corners[e], corners[(e + 1) % 4], ghost);
        }
    }

    /** Infinite grid: one wire cube per resident tile (pinned emitter tiles brighter) plus the ground lattice around them. */
    infiniteGridGizmo(domain, selected)
    {
        const pool = this.solver.tilePool;
        if (!pool) return;
        const size = pool.size, h = size * 0.5;
        const resident = this.solver.residentTiles;
        for (const entry of resident)
        {
            const centre = [entry.tile[0] * size + h, entry.tile[1] * size + h, entry.tile[2] * size + h];
            const alpha = entry.pinned ? (selected ? 0.9 : 0.6) : (selected ? 0.45 : 0.26);
            const tint = entry.pinned ? [0.96, 0.65, 0.14] : [0.62, 0.66, 0.74];
            this.box(centre, [h, h, h], [tint[0], tint[1], tint[2], alpha]);
        }
        // Ground lattice two tiles beyond the resident footprint: the endless grid the gas can expand into.
        if (!resident.length) return;
        let lo = [Infinity, Infinity], hi = [-Infinity, -Infinity];
        for (const { tile } of resident) { lo = [Math.min(lo[0], tile[0]), Math.min(lo[1], tile[2])]; hi = [Math.max(hi[0], tile[0]), Math.max(hi[1], tile[2])]; }
        const ghost = [0.62, 0.66, 0.74, selected ? 0.14 : 0.06];
        const y = 0.004;
        for (let x = lo[0] - 2; x <= hi[0] + 3; x += 1) this.line([x * size, y, (lo[1] - 2) * size], [x * size, y, (hi[1] + 3) * size], ghost);
        for (let z = lo[1] - 2; z <= hi[1] + 3; z += 1) this.line([(lo[0] - 2) * size, y, z * size], [(hi[0] + 3) * size, y, z * size], ghost);
    }

    renderGizmos(scene, matrices, selection)
    {
        const gl = this.gl, context = this.context, solver = this.solver;
        const gizmos = scene.Visualizers.ForceGizmos;
        this.lineVertexCount = 0;
        if (!gizmos || !gizmos.Visible) return;
        this.beginLines();
        const half = solver.domainHalfExtent, centre = solver.domainCentre;
        if (solver.sparse) this.infiniteGridGizmo(scene.Domain, selection === 'domain');
        const tiles = solver.sparse ? [1, 1, 1] : (scene.Domain.Tiles || [1, 1, 1]);
        const multi = tiles[0] * tiles[1] * tiles[2] > 1;
        // Multi-tile windows: the bright box is the single *tile* the domain size describes; the whole simulated window
        // is the dimmer outline around it, so gas visibly carries on past the tile into its neighbours.
        if (multi) this.box(centre, half.map((extent, axis) => extent / tiles[axis]), [0.62, 0.66, 0.74, selection === 'domain' ? 0.95 : 0.45]);
        if (!solver.sparse) this.box(centre, half, multi ? [0.62, 0.66, 0.74, selection === 'domain' ? 0.45 : 0.2] : [0.62, 0.66, 0.74, selection === 'domain' ? 0.95 : 0.45]);
        if (!solver.sparse) this.worldGridGizmo(scene.Domain, half, centre, selection === 'domain');
        const show = (node) => node.Visible;
        for (const force of scene.Forces) if (show(force)) this.forceGizmo(keyedNode(force, solver.time), force.Id === selection, gizmos.Opacity * (0.3 + 0.7 * nodeWeight(force, solver.time)), solver.time);
        for (const emitter of scene.Emitters) if (show(emitter)) this.emitterGizmo(keyedNode(emitter, solver.time), emitter.Id === selection, gizmos.Opacity * (0.3 + 0.7 * nodeWeight(emitter, solver.time)), solver.time);
        for (const node of scene.Particles || []) if (show(node)) this.particleGizmo(node, node.Id === selection, gizmos.Opacity * (0.3 + 0.7 * nodeWeight(node, solver.time)), solver.time);
        for (const collider of scene.Colliders) if (collider.Visible && collider.Id === selection) this.sphereWire(collider.Position, collider.Radius * 1.04, [1, 1, 1, 0.8]);
        if (this.lineVertexCount === 0) return;
        const uniforms = context.use(this.programs.lines);
        gl.uniformMatrix4fv(uniforms.uViewProjection, false, matrices.viewProjection);
        gl.bindVertexArray(this.lineVao);
        gl.bindBuffer(gl.ARRAY_BUFFER, this.lineBuffer);
        gl.bufferData(gl.ARRAY_BUFFER, this.lineScratch.subarray(0, this.lineVertexCount * 7), gl.DYNAMIC_DRAW);
        gl.drawArrays(gl.LINES, 0, this.lineVertexCount);
        gl.bindVertexArray(null);
    }
}
