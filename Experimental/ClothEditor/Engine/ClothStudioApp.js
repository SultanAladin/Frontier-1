/*============================================================================================================================================
                                                           CLOTHSTUDIOAPP.JS
============================================================================================================================================*/
// Central Coordinator for the Frontier Cloth Atelier:
// Orchestrates MannequinAvatar, GarmentPresets (6 presets), TextileCatalog (6 textiles + PIE mesh sizing),
// PatternDraftNetwork, OrthotropicClothSolver (AVBD, XPBD, MGPBD, FPB), WebGpuClothAccelerator,
// PatternCanvas2D, GarmentViewport3D, and 1:1 SVG/DXF/OBJ exporters.

import { MannequinAvatar, BODY_PRESETS, BODY_SLIDER_SPECS, DEFAULT_BODY_PARAMS } from './MannequinAvatar.js';
import { TEXTILE_CATALOG, estimateClothResolutionPIE } from './TextileCatalog.js';
import { compileGarmentNetwork } from './PatternDraftNetwork.js';
import { GARMENT_PRESET_CATALOG, buildGarmentPresetSpec } from './GarmentPresets.js';
import { SOLVER_CATALOG, OrthotropicClothSolver } from './ClothSolvers.js';
import { WebGpuClothAccelerator } from './WebGpuClothAccelerator.js';
import { PatternCanvas2D } from './PatternCanvas2D.js';
import { GarmentViewport3D } from './GarmentViewport3D.js';
import { exportPatternSVG, exportPatternDXF, exportDrapedOBJ } from './GarmentPatternExport.js';

export class ClothStudioApp
{
    constructor({ viewport3dEl, canvas2dEl, onMetrics, onTimeline, onStateChange })
    {
        this.viewport3dEl = viewport3dEl;
        this.canvas2dEl = canvas2dEl;
        this.onMetrics = onMetrics || (() => {});
        this.onTimeline = onTimeline || (() => {});
        this.onStateChange = onStateChange || (() => {});

        this.mannequin = null;
        this.bodyPresetKey = 'atelier36';
        this.viewport3d = new GarmentViewport3D(viewport3dEl, {
            onPinChange: () => this.onStateChange(this.getSnapshot()),
        });
        this.pattern2d = new PatternCanvas2D(canvas2dEl, {
            onSelectPiece: (idx) =>
            {
                this.selectedPieceIndex = idx;
                this.onStateChange(this.getSnapshot());
            },
        });

        this.gpuAccelerator = null;
        this.useWebGpu = true;

        this.garmentKey = 'coutureDress';
        this.textileKey = 'cottonPoplin';
        this.solverKey = 'avbd';
        this.fabricColor = '#c4513d';
        this.heatmapMode = 'textile';
        this.meshResolutionScale = 1.0; // multiplier on PIE mesh spacing

        this.isPlaying = true;
        this.isPlayback = false;
        this.isRecording = true;
        this.timelineLoop = true;
        this.playbackSpeed = 1.0;
        this.maxTimelineFrames = 360;
        this.timelineFrames = [];
        this.timelineCursor = 0;
        this._playbackFrac = 0.0;

        this.selectedPieceIndex = 0;
        this.selectedPanelTab = 'Outliner';
        this.activeTool = 'orbit';
        this.viewSplitMode = 'split'; // 'split' | '3d' | '2d'

        this.patternOverrides = {
            easeScale: 1.0,
            lengthScale: 1.0,
            flareScale: 1.0,
            grainOffsetDeg: 0.0,
        };
        this.garmentSpec = null;
        this.network = null;
        this.solver = null;
        this._stepInFlight = false;
        this._rafId = 0;
        this._disposed = false;
    }

    async init()
    {
        const resp = await fetch('./Assets/FrontierMannequin.bin');
        if (!resp.ok) throw new Error(`Failed to fetch mannequin binary: ${resp.status}`);
        const buf = await resp.arrayBuffer();
        this.mannequin = MannequinAvatar.fromArrayBuffer(buf);
        this.viewport3d.bindMannequin(this.mannequin);

        this.gpuAccelerator = await WebGpuClothAccelerator.create();
        this.rebuildGarment({ keepPins: false });
        this.onStateChange(this.getSnapshot());
        this._startLoop();
    }

    get textile()
    {
        return TEXTILE_CATALOG[this.textileKey] || TEXTILE_CATALOG.cottonPoplin;
    }

    get solverMeta()
    {
        return SOLVER_CATALOG[this.solverKey] || SOLVER_CATALOG.avbd;
    }

    getSnapshot()
    {
        const pie = estimateClothResolutionPIE(this.textile);
        return {
            garmentKey: this.garmentKey,
            textileKey: this.textileKey,
            solverKey: this.solverKey,
            fabricColor: this.fabricColor,
            heatmapMode: this.heatmapMode,
            meshResolutionScale: this.meshResolutionScale,
            pieSummary: pie,
            isPlaying: this.isPlaying,
            isPlayback: this.isPlayback,
            isRecording: this.isRecording,
            timeline: this.getTimelineState(),
            activeTool: this.activeTool,
            viewSplitMode: this.viewSplitMode,
            selectedPieceIndex: this.selectedPieceIndex,
            patternOverrides: { ...this.patternOverrides },
            gpuAvailable: !!this.gpuAccelerator,
            gpuActive: !!(this.useWebGpu && this.gpuAccelerator && this.gpuAccelerator.supportsSolver(this.solverKey)),
            useWebGpu: this.useWebGpu,
            gpuName: this.gpuAccelerator?.adapterName || 'CPU Fallback (All 4 Solvers)',
            bodyPresetKey: this.bodyPresetKey,
            mannequinParams: this.mannequin ? { ...this.mannequin.params } : { ...DEFAULT_BODY_PARAMS },
            mannequinMeasures: this.mannequin ? { ...this.mannequin.measurements } : {},
            pieces: this.network ? this.network.pieces : [],
            seams: this.network ? this.network.seams : [],
            metrics: this.solver ? this.solver.metrics : null,
            solverConfig: this.solver ? {
                substeps: this.solver.substeps,
                iterations: this.solver.iterations,
                dampingScale: this.solver.dampingScale,
                windEnabled: this.solver.windEnabled,
                windSpeed: this.solver.windSpeed,
                windAngleDeg: this.solver.windAngleDeg,
                selfCollisionEnabled: this.solver.selfCollisionEnabled,
            } : null,
            viewportFlags: {
                showMannequin: this.viewport3d.showMannequin,
                showMeasureRings: this.viewport3d.showMeasureRings,
                showWireframe: this.viewport3d.showWireframe,
                showSeams3D: this.viewport3d.showSeams3D,
                showSeamAllowance: this.pattern2d.showSeamAllowance,
                showSeamLines: this.pattern2d.showSeamLines,
            },
        };
    }

    rebuildGarment({ resetPatternOverrides = false } = {})
    {
        if (resetPatternOverrides)
            this.patternOverrides = { easeScale: 1.0, lengthScale: 1.0, flareScale: 1.0, grainOffsetDeg: 0.0 };

        const tex = this.textile;
        const pie = estimateClothResolutionPIE(tex);
        const targetSpacingMm = Math.max(8.0, Math.min(25.0, pie.authoringSpacingMm / this.meshResolutionScale));

        this.garmentSpec = buildGarmentPresetSpec(this.garmentKey, this.mannequin, this.patternOverrides);
        this.network = compileGarmentNetwork(this.garmentSpec, this.mannequin, {
            particleSpacingMm: targetSpacingMm,
            textile: tex,
        });

        const prev = this.solver;
        this.solver = new OrthotropicClothSolver(this.network, this.mannequin, tex, {
            solverMode: this.solverKey,
            substeps: prev ? prev.substeps : this.solverMeta.defaultSubsteps,
            iterations: prev ? prev.iterations : this.solverMeta.defaultIterations,
            dampingScale: prev ? prev.dampingScale : 1.0,
            windEnabled: prev ? prev.windEnabled : false,
            windSpeed: prev ? prev.windSpeed : 2.6,
            windAngleDeg: prev ? prev.windAngleDeg : 28,
            selfCollisionEnabled: prev ? prev.selfCollisionEnabled : true,
        });

        if (this.selectedPieceIndex >= this.network.pieces.length)
            this.selectedPieceIndex = 0;

        this.viewport3d.bindClothSolver(this.solver);
        this.viewport3d.setTextileVisuals(tex, this.fabricColor);
        this.pattern2d.fabricHex = this.fabricColor;
        this.pattern2d.bindSimulation(this.network, this.solver);
        this.pattern2d.selectedPieceIndex = this.selectedPieceIndex;
        this.clearTimeline();
        this.onStateChange(this.getSnapshot());
    }

    getTimelineState()
    {
        const len = this.timelineFrames.length;
        const cur = Math.max(0, Math.min(len - 1, this.timelineCursor));
        const curFrame = len > 0 ? this.timelineFrames[cur] : null;
        const lastFrame = len > 0 ? this.timelineFrames[len - 1] : null;
        return {
            totalCached: len,
            cursor: cur,
            maxIndex: Math.max(0, len - 1),
            simFrame: curFrame ? curFrame.frameCount : (this.solver ? this.solver.frameCount : 0),
            maxSimFrame: lastFrame ? lastFrame.frameCount : 0,
            timeSec: curFrame ? curFrame.timeSec : (this.solver ? this.solver.simTimeSec : 0),
            durationSec: lastFrame ? lastFrame.timeSec : 0,
            isPlaying: this.isPlaying,
            isPlayback: this.isPlayback,
            isRecording: this.isRecording,
            loop: this.timelineLoop,
            speed: this.playbackSpeed,
        };
    }

    recordTimelineFrame(force = false)
    {
        if (!this.solver) return;
        if (!force && !this.isRecording) return;
        if (this.timelineCursor < this.timelineFrames.length - 1)
            this.timelineFrames.length = this.timelineCursor + 1;

        this.timelineFrames.push({
            frameCount: this.solver.frameCount,
            substepCountTotal: this.solver.substepCountTotal,
            timeSec: this.solver.simTimeSec,
            positions: new Float32Array(this.solver.positions),
            velocities: new Float32Array(this.solver.velocities),
            metrics: { ...this.solver.metrics },
        });
        if (this.timelineFrames.length > this.maxTimelineFrames)
            this.timelineFrames.shift();
        this.timelineCursor = this.timelineFrames.length - 1;
    }

    clearTimeline()
    {
        this.isPlayback = false;
        this._playbackFrac = 0.0;
        this.timelineFrames = [];
        this.timelineCursor = 0;
        this.recordTimelineFrame(true);
        this.onTimeline(this.getTimelineState());
    }

    _applyTimelineFrame(idx)
    {
        if (!this.solver || this.timelineFrames.length === 0) return;
        const clamped = Math.max(0, Math.min(this.timelineFrames.length - 1, Math.round(idx)));
        const kf = this.timelineFrames[clamped];
        this.timelineCursor = clamped;
        this.solver.positions.set(kf.positions);
        this.solver.prevPositions.set(kf.positions);
        this.solver.velocities.set(kf.velocities);
        this.solver.frameCount = kf.frameCount;
        this.solver.substepCountTotal = kf.substepCountTotal;
        this.solver.simTimeSec = kf.timeSec;
        this.solver.computeNormalsAndMetrics();
        this.viewport3d.refreshClothGeometry();
        this.onMetrics(this.solver.metrics);
    }

    seekTimeline(idx)
    {
        const wasRunning = this.isPlaying || this.isPlayback;
        this.isPlaying = false;
        this.isPlayback = false;
        this._applyTimelineFrame(idx);
        this.onTimeline(this.getTimelineState());
        if (wasRunning) this.onStateChange(this.getSnapshot());
    }

    toggleTimelinePlay()
    {
        if (this.isPlaying || this.isPlayback)
        {
            this.isPlaying = false;
            this.isPlayback = false;
        }
        else if (this.timelineCursor < this.timelineFrames.length - 1)
        {
            this._playbackFrac = 0.0;
            this.isPlayback = true;
        }
        else if (!this.isRecording && this.timelineFrames.length > 1)
        {
            this._playbackFrac = 0.0;
            this._applyTimelineFrame(0);
            this.isPlayback = true;
        }
        else
        {
            this.isPlaying = true;
        }
        this.onStateChange(this.getSnapshot());
    }

    toggleLivePlay()
    {
        if (this.isPlaying || this.isPlayback)
        {
            this.isPlaying = false;
            this.isPlayback = false;
        }
        else
        {
            this.isPlayback = false;
            this.isPlaying = true;
        }
        this.onStateChange(this.getSnapshot());
    }

    playLive()
    {
        this.isPlayback = false;
        this.isPlaying = true;
        this.onStateChange(this.getSnapshot());
    }

    stepTimeline(delta)
    {
        this.isPlaying = false;
        this.isPlayback = false;
        if (delta < 0)
        {
            this._applyTimelineFrame(Math.max(0, this.timelineCursor - 1));
            this.onStateChange(this.getSnapshot());
            return;
        }
        if (this.timelineCursor < this.timelineFrames.length - 1)
        {
            this._applyTimelineFrame(this.timelineCursor + 1);
            this.onStateChange(this.getSnapshot());
            return;
        }
        this.stepOnce();
        this.onStateChange(this.getSnapshot());
    }

    cyclePlaybackSpeed()
    {
        const speeds = [0.25, 0.5, 1.0, 2.0];
        const idx = speeds.indexOf(this.playbackSpeed);
        this.playbackSpeed = speeds[(idx + 1) % speeds.length];
        this.onTimeline(this.getTimelineState());
        this.onStateChange(this.getSnapshot());
    }

    toggleTimelineLoop()
    {
        this.timelineLoop = !this.timelineLoop;
        this.onStateChange(this.getSnapshot());
    }

    toggleRecording()
    {
        this.isRecording = !this.isRecording;
        this.onStateChange(this.getSnapshot());
    }

    selectGarment(key)
    {
        if (!GARMENT_PRESET_CATALOG[key]) return;
        this.garmentKey = key;
        const defaultTex = GARMENT_PRESET_CATALOG[key].defaultTextile || this.textileKey;
        this.textileKey = defaultTex;
        this.fabricColor = TEXTILE_CATALOG[defaultTex].colorHex || this.fabricColor;
        this.rebuildGarment({ resetPatternOverrides: true });
    }

    selectTextile(key)
    {
        if (!TEXTILE_CATALOG[key]) return;
        this.textileKey = key;
        this.fabricColor = TEXTILE_CATALOG[key].colorHex;
        if (this.solver)
        {
            this.solver.setTextile(this.textile);
            this.viewport3d.setTextileVisuals(this.textile, this.fabricColor);
            this.pattern2d.fabricHex = this.fabricColor;
        }
        this.onStateChange(this.getSnapshot());
    }

    setFabricColor(hex)
    {
        this.fabricColor = hex;
        this.viewport3d.setTextileVisuals(this.textile, hex);
        this.pattern2d.fabricHex = hex;
        this.onStateChange(this.getSnapshot());
    }

    selectSolver(key)
    {
        if (!SOLVER_CATALOG[key]) return;
        this.solverKey = key;
        const meta = SOLVER_CATALOG[key];
        if (this.solver)
        {
            this.solver.switchSolverHot(key, meta.defaultSubsteps, meta.defaultIterations);
        }
        this.onStateChange(this.getSnapshot());
    }

    setMannequinMorph(morphKey, val)
    {
        if (!this.mannequin) return;
        this.mannequin.setParam(morphKey, val);
        this.viewport3d.refreshMannequinGeometry();
        if (this.solver)
        {
            this.solver._resolveMannequinCollisions(1 / 60);
            this.solver.computeNormalsAndMetrics();
        }
        this.onStateChange(this.getSnapshot());
    }

    selectBodyPreset(presetKey)
    {
        if (!this.mannequin || !BODY_PRESETS[presetKey]) return;
        this.bodyPresetKey = presetKey;
        this.mannequin.applyBodyPreset(presetKey);
        this.viewport3d.refreshMannequinGeometry();
        this.rebuildGarment({ resetPatternOverrides: false });
    }

    resetMannequinMorphs()
    {
        this.selectBodyPreset('atelier36');
    }

    updatePatternOverride(patch)
    {
        this.patternOverrides = { ...this.patternOverrides, ...patch };
        this.rebuildGarment({ resetPatternOverrides: false });
    }

    toggleComputeBackend()
    {
        if (!this.gpuAccelerator) return;
        this.useWebGpu = !this.useWebGpu;
        this.onStateChange(this.getSnapshot());
    }

    setHeatmapMode(mode)
    {
        this.heatmapMode = mode;
        this.viewport3d.heatmapMode = mode;
        this.pattern2d.heatmapMode = mode;
        this.onStateChange(this.getSnapshot());
    }

    setToolMode(tool)
    {
        this.activeTool = tool;
        this.viewport3d.toolMode = tool;
        this.onStateChange(this.getSnapshot());
    }

    setViewSplitMode(mode)
    {
        this.viewSplitMode = mode;
        this.onStateChange(this.getSnapshot());
        requestAnimationFrame(() => this.pattern2d.fitToView());
    }

    stepOnce()
    {
        if (!this.solver) return;
        this.isPlaying = false;
        this.isPlayback = false;
        if (this.timelineCursor < this.timelineFrames.length - 1)
            this.timelineFrames.length = this.timelineCursor + 1;
        this.solver.stepFrame(1 / 60);
        this.recordTimelineFrame(true);
        this.viewport3d.refreshClothGeometry();
        this.viewport3d.render();
        this.pattern2d.render();
        this.onMetrics(this.solver.metrics);
        this.onTimeline(this.getTimelineState());
    }

    steamRelax()
    {
        if (!this.solver) return;
        this.solver.steamRelax();
        this.recordTimelineFrame(false);
        this.viewport3d.refreshClothGeometry();
        this.onMetrics(this.solver.metrics);
        this.onTimeline(this.getTimelineState());
    }

    clearPins()
    {
        if (!this.solver) return;
        this.solver.clearUserPins();
        this.viewport3d.refreshPins();
        this.onStateChange(this.getSnapshot());
    }

    exportFile(kind)
    {
        if (!this.network || !this.solver) return;
        const nameBase = `${this.garmentKey}_${this.textileKey}`;
        let content = '';
        let filename = `${nameBase}.${kind}`;
        let mime = 'text/plain;charset=utf-8';

        if (kind === 'svg')
        {
            content = exportPatternSVG(this.network, this.textile, null);
            mime = 'image/svg+xml;charset=utf-8';
        }
        else if (kind === 'dxf')
        {
            content = exportPatternDXF(this.network, this.textile);
            mime = 'application/dxf';
        }
        else if (kind === 'obj')
        {
            content = exportDrapedOBJ(this.solver);
        }
        else return;

        const blob = new Blob([content], { type: mime });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = filename;
        document.body.appendChild(a);
        a.click();
        a.remove();
        setTimeout(() => URL.revokeObjectURL(url), 1000);
    }

    _startLoop()
    {
        const tick = async () =>
        {
            try
            {
                if (this.isPlayback && this.timelineFrames.length > 0)
                {
                    this._playbackFrac += this.playbackSpeed;
                    if (this._playbackFrac >= 1.0)
                    {
                        const adv = Math.floor(this._playbackFrac);
                        this._playbackFrac -= adv;
                        let nextIdx = this.timelineCursor + adv;
                        if (nextIdx >= this.timelineFrames.length)
                        {
                            if (this.timelineLoop)
                            {
                                nextIdx = 0;
                            }
                            else
                            {
                                nextIdx = this.timelineFrames.length - 1;
                                this.isPlayback = false;
                                this._applyTimelineFrame(nextIdx);
                                this.onStateChange(this.getSnapshot());
                            }
                        }
                        if (this.isPlayback)
                        {
                            this._applyTimelineFrame(nextIdx);
                            this.onTimeline(this.getTimelineState());
                        }
                    }
                }
                else if (this.isPlaying && this.solver && !this._stepInFlight)
                {
                    this._stepInFlight = true;
                    try
                    {
                        const canUseGpu = this.useWebGpu && this.gpuAccelerator && this.gpuAccelerator.supportsSolver(this.solver.solverMode);
                        if (canUseGpu)
                        {
                            let timerId = 0;
                            try
                            {
                                if (!this.gpuAccelerator.firstStepValidated)
                                {
                                    await Promise.race([
                                        this.gpuAccelerator.stepFrame(this.solver, 1 / 60),
                                        new Promise((_, rej) => {
                                            timerId = setTimeout(() => rej(new Error('WebGPU frame timeout')), 2500);
                                        }),
                                    ]);
                                    clearTimeout(timerId);
                                }
                                else
                                {
                                    await this.gpuAccelerator.stepFrame(this.solver, 1 / 60);
                                }
                            }
                            catch (gpuErr)
                            {
                                clearTimeout(timerId);
                                console.warn('WebGPU simulation step failed; switching permanently to CPU solver:', gpuErr);
                                this.useWebGpu = false;
                                this.gpuAccelerator = null;
                                this.solver.stepFrame(1 / 60);
                                this.onStateChange(this.getSnapshot());
                            }
                        }
                        else
                        {
                            this.solver.stepFrame(1 / 60);
                        }
                        if (this.isRecording)
                            this.recordTimelineFrame();
                        this.viewport3d.refreshClothGeometry();
                        this.onMetrics(this.solver.metrics);
                        this.onTimeline(this.getTimelineState());
                    }
                    catch (stepErr)
                    {
                        console.error('Cloth simulation frame error:', stepErr);
                    }
                    finally
                    {
                        this._stepInFlight = false;
                    }
                }
                this.viewport3d.render();
                this.pattern2d.render();
            }
            catch (loopErr)
            {
                console.error('Atelier render loop error:', loopErr);
            }
            if (!this._disposed)
                this._rafId = requestAnimationFrame(tick);
        };
        this._rafId = requestAnimationFrame(tick);
    }

    dispose()
    {
        this._disposed = true;
        if (this._rafId) cancelAnimationFrame(this._rafId);
    }
}
