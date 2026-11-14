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
    constructor({ viewport3dEl, canvas2dEl, onMetrics, onStateChange })
    {
        this.viewport3dEl = viewport3dEl;
        this.canvas2dEl = canvas2dEl;
        this.onMetrics = onMetrics || (() => {});
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

    setPlaying(playing)
    {
        const next = !!playing;
        if (this.isPlaying === next) return;
        this.isPlaying = next;
        if (this.isPlaying && !this._rafId)
            this._startLoop();
        this.onStateChange(this.getSnapshot());
    }

    togglePlay()
    {
        this.setPlaying(!this.isPlaying);
    }

    stepOnce()
    {
        if (!this.solver) return;
        this.isPlaying = false;
        this.solver.stepFrame(1 / 60);
        this.viewport3d.refreshClothGeometry();
        this.onMetrics(this.solver.metrics);
        this.onStateChange(this.getSnapshot());
    }

    steamRelax()
    {
        if (!this.solver) return;
        this.solver.steamRelax();
        this.viewport3d.refreshClothGeometry();
        this.onMetrics(this.solver.metrics);
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
        if (this._rafId) cancelAnimationFrame(this._rafId);
        this._rafId = 0;
        let nextStepAllowedAt = 0;

        const tick = async (nowMs) =>
        {
            this._rafId = requestAnimationFrame(tick);

            if (this.isPlaying && this.solver && !this._stepInFlight && nowMs >= nextStepAllowedAt)
            {
                this._stepInFlight = true;
                const tStart = performance.now();
                try
                {
                    if (this.useWebGpu && this.gpuAccelerator && this.gpuAccelerator.supportsSolver(this.solver.solverMode))
                    {
                        try
                        {
                            await this.gpuAccelerator.stepFrame(this.solver, 1 / 60);
                        }
                        catch (gpuErr)
                        {
                            console.warn('[ClothStudioApp] WebGPU step failed, falling back to CPU solver:', gpuErr);
                            this.useWebGpu = false;
                            this.onStateChange(this.getSnapshot());
                            if (this.isPlaying)
                                this.solver.stepFrame(1 / 60);
                        }
                    }
                    else
                    {
                        this.solver.stepFrame(1 / 60);
                    }

                    if (this.isPlaying && this.solver)
                    {
                        this.viewport3d.refreshClothGeometry();
                        this.onMetrics(this.solver.metrics);
                    }
                }
                catch (stepErr)
                {
                    console.error('[ClothStudioApp] Solver step error:', stepErr);
                    this.isPlaying = false;
                    this.onStateChange(this.getSnapshot());
                }
                finally
                {
                    this._stepInFlight = false;
                    // Leave a small browser event-loop breathing window after heavy CPU steps
                    // so clicking Pause/Play, orbiting, or scrubbing sliders always stays responsive.
                    const elapsedMs = performance.now() - tStart;
                    if (elapsedMs > 28)
                        nextStepAllowedAt = performance.now() + 24;
                }
            }

            this.viewport3d.render();
            this.pattern2d.render();
        };
        this._rafId = requestAnimationFrame(tick);
    }

    dispose()
    {
        if (this._rafId) cancelAnimationFrame(this._rafId);
        this._rafId = 0;
    }
}
