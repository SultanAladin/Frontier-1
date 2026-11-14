/*============================================================================================================================================
                                                          CLOTHSTUDIOAPP.JS
============================================================================================================================================*/
// 🧩 Frontier Cloth & Tailoring Atelier Controller (Project-Zero UI + Dual-Tier Option A / Option B + WebGPU/WebGL2).

import {
    Construct, ConstructGlyph, OpenScope, CloseScope, ReleaseScope, Broadcast,
    SectionCard, SliderRow, SwitchRow, SelectRow, SegmentedRow, ActionRow, MetricTrio, SmoothScroll, Toast,
} from '/UVEditor/StudioControls.js';

import { MannequinAvatar, AVATAR_BODY_PRESETS } from './MannequinAvatar.js?v=4';
import { TEXTILE_CATALOG, TEXTILE_COLOR_SWATCHES, cloneTextileMaterial, SOLVER_CATALOG, estimateClothResolutionPIE } from './TextileCatalog.js?v=4';
import { GARMENT_PRESET_CATALOG, buildGarmentPresetSpec } from './GarmentPresets.js?v=4';
import { compileGarmentNetwork } from './PatternDraftNetwork.js?v=4';
import { OrthotropicClothSolver } from './OrthotropicClothSolver.js?v=4';
import { WebGpuClothAccelerator } from './WebGpuClothAccelerator.js?v=4';
import { PatternCanvas2D } from './PatternCanvas2D.js?v=4';
import { GarmentViewport3D } from './GarmentViewport3D.js?v=4';
import { exportPatternSVG, exportPatternDXF, exportDrapedOBJ } from './GarmentPatternExport.js?v=4';

const SEAM_COLORS = ['#38bdf8', '#f472b6', '#34d399', '#fbbf24', '#a78bfa', '#fb7185', '#2dd4bf', '#60a5fa'];

function triggerFileDownload(filename, mimeType, textContent)
{
    const blob = new Blob([textContent], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1500);
}

class ClothStudioApp
{
    constructor()
    {
        this.mannequin = null;
        this.activePresetKey = 'coutureDress';
        this.activeBodyPresetKey = 'atelier36';
        this.activeTextileKey = 'cottonPoplin';
        this.textile = cloneTextileMaterial('cottonPoplin');
        this.fabricHex = '#c4513d';

        this.patternOverrides = {
            easeScale: 1.0,
            lengthScale: 1.0,
            flareScale: 1.0,
            grainOffsetDeg: 0.0,
        };
        this.particleSpacingMm = 16.0;

        this.network = null;
        this.solver = null;
        this.gpu = new WebGpuClothAccelerator();
        this.useWebGpu = true;

        this.running = true;
        this.stageMode = 'split';
        this.heatmapMode = 'textile';
        this.activeTab = 'solver';
        this.inspectorScope = null;
        this.telemetry = null;
    }

    async initialize()
    {
        // 1. Load Parametric Mannequin Asset + Initialize WebGPU Compute Accelerator concurrently
        const [mannequinBuf] = await Promise.all([
            fetch('./Assets/FrontierMannequin.bin').then((r) =>
            {
                if (!r.ok) throw new Error(`Failed to load FrontierMannequin.bin (${r.status})`);
                return r.arrayBuffer();
            }),
            this.gpu.initialize(),
        ]);

        this.mannequin = MannequinAvatar.fromArrayBuffer(mannequinBuf);
        this.mannequin.applyBodyPreset(this.activeBodyPresetKey);
        this.mannequin.ensureSignedDistanceVolume();

        // 2. Mount Split 2D Pattern Canvas & 3D WebGL2 PBR Atelier Viewport
        this.canvas2D = new PatternCanvas2D(document.getElementById('pattern-canvas-2d'), {
            onSelectPiece: (idx) =>
            {
                this._syncSelectedPieceHud(idx);
                this._renderOutlinerLists();
                Broadcast();
            },
        });

        this.viewport3D = new GarmentViewport3D(document.getElementById('garment-viewport-3d'), {
            onPinChange: () => Broadcast(),
        });
        this.viewport3D.bindMannequin(this.mannequin);

        SmoothScroll(document.getElementById('outliner-scroll'), document.getElementById('outliner-scroll-content'));
        SmoothScroll(document.getElementById('inspector-scroll'), document.getElementById('inspector-body'));

        // 3. Build Default Couture Dress & Run 20 Warm-Start Assembly Frames so Initial Frame Already Looks Tailored
        this.loadGarmentPreset('coutureDress', true);
        for (let warm = 0; warm < 22; ++warm)
            this.solver.stepFrame(1.0 / 60.0);
        this.viewport3D.updateClothBuffers();

        // 4. Wire Project-Zero Chrome, Tool Rail, Dock Resizers & Inspector
        this._populateRailGlyphs();
        this._bindWorkspaceChrome();
        this._bindColumnAndSplitResizers();
        this._renderGarmentSelectorCards();
        this.rebuildInspector();
        this._refreshHudAndStatus();

        // 5. Start Live 60 FPS Simulation & Render Loop
        let lastT = performance.now();
        const tick = async (now) =>
        {
            const dt = Math.min(1.0 / 45.0, Math.max(1.0 / 144.0, (now - lastT) * 0.001));
            lastT = now;

            if (this.running && this.solver)
            {
                if (this.useWebGpu && this.gpu.supported)
                    await this.gpu.stepGpuFrame(this.solver, dt);
                else
                    this.solver.stepFrame(dt);

                this.viewport3D.updateClothBuffers();
                this._refreshHudAndStatus();
            }

            if (this.stageMode !== 'mesh')
                this.canvas2D.render();
            if (this.stageMode !== 'uv')
                this.viewport3D.render();

            requestAnimationFrame(tick);
        };
        requestAnimationFrame(tick);
    }

    loadGarmentPreset(presetKey, resetTextile = true)
    {
        const meta = GARMENT_PRESET_CATALOG[presetKey] || GARMENT_PRESET_CATALOG.coutureDress;
        this.activePresetKey = meta.key;

        if (resetTextile)
        {
            this.activeTextileKey = meta.defaultTextile;
            this.textile = cloneTextileMaterial(meta.defaultTextile);
            this.fabricHex = meta.defaultSwatch || '#c4513d';
            this.patternOverrides = { easeScale: 1.0, lengthScale: 1.0, flareScale: 1.0, grainOffsetDeg: 0.0 };
        }

        this.rebuildPatternNetwork(true);
    }

    rebuildPatternNetwork(preserveSolverMode = true)
    {
        const prevMode = (preserveSolverMode && this.solver) ? this.solver.solverMode : 'avbd';
        const prevWind = (preserveSolverMode && this.solver) ? this.solver.windEnabled : false;
        const prevSelf = (preserveSolverMode && this.solver) ? this.solver.enableSelfCollision : true;

        const spec = buildGarmentPresetSpec(this.activePresetKey, this.mannequin, this.patternOverrides);
        this.network = compileGarmentNetwork(spec, this.mannequin, { particleSpacingMm: this.particleSpacingMm });

        if (!this.solver)
            this.solver = new OrthotropicClothSolver(this.network, this.textile, this.mannequin);
        else
            this.solver.bindNetwork(this.network, this.textile);

        this.solver.solverMode = prevMode;
        this.solver.windEnabled = prevWind;
        this.solver.enableSelfCollision = prevSelf;

        if (this.gpu.supported)
            this.gpu.uploadNetworkBuffers(this.solver);

        this.canvas2D.fabricHex = this.fabricHex;
        this.canvas2D.bindSimulation(this.network, this.solver);

        this.viewport3D.fabricHex = this.fabricHex;
        this.viewport3D.bindClothSolver(this.solver);

        this._renderGarmentSelectorCards();
        this._renderOutlinerLists();
        this._syncSelectedPieceHud(this.canvas2D.selectedPieceIndex);
        this._refreshHudAndStatus();
        Broadcast();
    }

    runFitVerificationBake()
    {
        if (!this.solver) return null;
        // Ensure initial seam zipping has completed before running equilibrium bake
        while (this.solver.simTime < this.solver.zipDuration + 0.15)
            this.solver.stepFrame(1.0 / 60.0);

        const cert = this.solver.runCubicBarrierFitVerification(16);
        if (this.gpu.supported)
            this.gpu.syncStateToGpu(this.solver);

        this.running = false;
        this._syncPlayPauseUi();
        this.viewport3D.updateClothBuffers();
        this.canvas2D.render();
        this.viewport3D.render();
        this._refreshHudAndStatus();
        Broadcast();

        Toast(cert.verified
            ? `Fit Verified for 1:1 Cutting (${cert.maxExcessStretchPct.toFixed(2)}% excess stretch · ${cert.meanSeamGapMm.toFixed(2)} mm seam)`
            : `Fit Check Complete (${cert.maxExcessStretchPct.toFixed(2)}% excess yarn stretch — inspect Strain Lens)`);
        return cert;
    }

    _populateRailGlyphs()
    {
        const map = {
            '[data-cloth-tool="orbit"]': 'CameraView',
            '[data-cloth-tool="grab"]':  'Move',
            '[data-cloth-tool="pin"]':   'Pin',
            '#rail-clear-pins':          'PinOff',
            '#rail-fit-sheet':           'Focus',
            '#rail-export-svg':          'Download',
        };
        for (const [sel, glyph] of Object.entries(map))
        {
            const el = document.querySelector(sel);
            if (el) el.append(ConstructGlyph(glyph));
        }
    }

    _bindWorkspaceChrome()
    {
        // Notch drawer pull-down
        const notchDrawer = document.getElementById('pz-notch-drawer');
        document.getElementById('pz-notch-toggle').addEventListener('click', () =>
        {
            notchDrawer.hidden = !notchDrawer.hidden;
        });

        // Stage mode capsule (Split | 2D Pattern | 3D Atelier)
        const stageSplit = document.getElementById('stage-split');
        document.querySelectorAll('#stage-layout-nav button').forEach((btn) =>
        {
            btn.addEventListener('click', () =>
            {
                this.stageMode = btn.dataset.stageMode;
                document.querySelectorAll('#stage-layout-nav button').forEach((b) => b.classList.toggle('active', b === btn));
                stageSplit.className = `stage-split mode-${this.stageMode}`;
                if (this.stageMode !== 'mesh')
                    setTimeout(() => this.canvas2D.fitToView(), 20);
            });
        });

        // Play / Pause
        document.getElementById('btn-play-pause').addEventListener('click', () =>
        {
            this.running = !this.running;
            this._syncPlayPauseUi();
        });

        // Verify Fit (Cubic-Barrier + Strain-Limit Bake)
        document.getElementById('btn-verify-bake').addEventListener('click', () => this.runFitVerificationBake());

        // Reset Drape
        const doReset = () =>
        {
            this.solver.resetDrape();
            if (this.gpu.supported) this.gpu.syncStateToGpu(this.solver);
            this.viewport3D.syncPinMarkers();
            this.viewport3D.updateClothBuffers();
            this.running = true;
            this._syncPlayPauseUi();
            this._refreshHudAndStatus();
            Broadcast();
            Toast('Panels re-arranged and re-zipping onto mannequin');
        };
        document.getElementById('btn-reset-drape').addEventListener('click', doReset);
        document.getElementById('btn-inspector-reset').addEventListener('click', doReset);

        // Candidate Solver Architecture Switch (Options A–F, all kept side by side for direct comparison)
        const solverSel = document.getElementById('solver-mode-select');
        const applySolverMode = (mode) =>
        {
            this.solver.solverMode = mode;
            this.solver.fitVerifiedCertificate = null;
            // Purge architecture-specific transient state so a mid-run switch can never inherit a stale
            // contact manifold / Anderson history and appear to freeze for a few frames.
            this.solver.resetSolverMemory();
            this.solver.wakeUp();
            // The CPU-side positions may have moved while a CPU-only architecture (C–F) was active; push them
            // to the WebGPU buffers so switching back to A/B never snaps to a stale pose.
            if (this.gpu.supported)
                this.gpu.syncStateToGpu(this.solver);
            // A previous "Verify Fit (Bake)" pauses the loop — switching architecture always resumes live draping.
            this.running = true;
            this._syncPlayPauseUi();
            const meta = SOLVER_CATALOG[mode];
            Toast(`${meta.title} — ${meta.role}`);
            this._refreshHudAndStatus();
            Broadcast();
        };
        solverSel.addEventListener('change', () => applySolverMode(solverSel.value));

        // Zhang et al. (2025) PIE: one-click physics-optimal mesh resolution for the active textile
        document.getElementById('btn-pie-auto-res').addEventListener('click', () =>
        {
            const pie = estimateClothResolutionPIE(this.textile);
            this.pieTelemetry = pie;
            this.particleSpacingMm = pie.authoringSpacingMm;
            this.rebuildPatternNetwork(true);
            Toast(`PIE auto-res: ${pie.authoringSpacingMm.toFixed(1)} mm (r_opt ${pie.rPieBaseMm.toFixed(1)} mm, λ ${pie.wavelengthMm.toFixed(1)} mm, wrinklon ${pie.wrinklonTransitionMm.toFixed(1)} mm)`);
        });

        // Synchronized 2D & 3D Heatmap Lens
        const setLens = (mode) =>
        {
            this.heatmapMode = mode;
            document.getElementById('heatmap-mode-select').value = mode;
            this.canvas2D.heatmapMode = mode;
            this.viewport3D.heatmapMode = mode;
            this.viewport3D.syncTextileAppearance();
            this.viewport3D.updateClothBuffers();

            const legend = document.getElementById('heatmap-legend-pill');
            legend.hidden = mode === 'textile';
            if (mode === 'strain')
            {
                document.getElementById('heatmap-legend-min').textContent = 'Compressed';
                document.getElementById('heatmap-legend-max').textContent = 'Cubic Lock Wall';
            }
            else if (mode === 'shear')
            {
                document.getElementById('heatmap-legend-min').textContent = '0° Ortho';
                document.getElementById('heatmap-legend-max').textContent = '18° Trellis Jam';
            }
            else if (mode === 'pressure')
            {
                document.getElementById('heatmap-legend-min').textContent = '0 kPa Free';
                document.getElementById('heatmap-legend-max').textContent = '5+ kPa Snug';
            }
            else if (mode === 'batches')
            {
                document.getElementById('heatmap-legend-min').textContent = 'Batch 0';
                document.getElementById('heatmap-legend-max').textContent = `Batch ${this.solver.network.pointBatches.colorCount - 1}`;
            }
            Broadcast();
        };
        document.getElementById('heatmap-mode-select').addEventListener('change', (e) => setLens(e.target.value));
        document.getElementById('kpi-yarn-btn').addEventListener('click', () => setLens(this.heatmapMode === 'strain' ? 'textile' : 'strain'));
        document.getElementById('kpi-pen-btn').addEventListener('click', () => setLens(this.heatmapMode === 'pressure' ? 'textile' : 'pressure'));

        // Camera Presets
        document.getElementById('camera-preset-select').addEventListener('change', (e) =>
        {
            this.viewport3D.setCameraPreset(e.target.value);
        });

        // Quick Toggles (Wire, Seams, Wind)
        const wireBtn = document.getElementById('btn-toggle-wire');
        wireBtn.addEventListener('click', () =>
        {
            const next = !this.viewport3D.showWireframe;
            wireBtn.classList.toggle('active', next);
            this.viewport3D.setVisibility({ showWireframe: next });
            Broadcast();
        });

        const seamBtn = document.getElementById('btn-toggle-seams');
        seamBtn.addEventListener('click', () =>
        {
            const next = !this.viewport3D.showSeams3D;
            seamBtn.classList.toggle('active', next);
            this.canvas2D.showSeamLines = next;
            this.viewport3D.setVisibility({ showSeams3D: next });
            Broadcast();
        });

        const windBtn = document.getElementById('btn-toggle-wind');
        windBtn.addEventListener('click', () =>
        {
            this.solver.windEnabled = !this.solver.windEnabled;
            windBtn.classList.toggle('active', this.solver.windEnabled);
            this.running = true;
            this._syncPlayPauseUi();
            Broadcast();
        });

        // Context Tool Rail
        document.querySelectorAll('[data-cloth-tool]').forEach((btn) =>
        {
            btn.addEventListener('click', () =>
            {
                const tool = btn.dataset.clothTool;
                this.viewport3D.toolMode = tool;
                document.querySelectorAll('[data-cloth-tool]').forEach((b) => b.classList.toggle('active', b === btn));
                Toast(tool === 'grab' ? '3D Cloth Grab Tool: Drag fabric directly in 3D' :
                      tool === 'pin'  ? '3D Cloth Pin Tool: Click garment vertices to pin/unpin' :
                                        '3D Orbit Camera Mode (Alt+Drag still grabs cloth)');
            });
        });

        document.getElementById('rail-clear-pins').addEventListener('click', () =>
        {
            this.solver.clearAllPins();
            if (this.gpu.supported) this.gpu.syncStateToGpu(this.solver);
            this.viewport3D.syncPinMarkers();
            Toast('Cleared all cloth pins');
        });

        const fit2D = () => this.canvas2D.fitToView();
        document.getElementById('rail-fit-sheet').addEventListener('click', fit2D);
        document.getElementById('btn-fit-2d-top').addEventListener('click', fit2D);

        // Exporters (1:1 SVG, ASTM DXF, Draped OBJ)
        const doExportSVG = () =>
        {
            const svg = exportPatternSVG(this.network, this.textile, this.solver.fitVerifiedCertificate);
            triggerFileDownload(`${this.activePresetKey}_pattern_1to1_mm.svg`, 'image/svg+xml', svg);
            Toast('Exported 1:1 Millimetre Cutting Pattern (.SVG)');
        };
        const doExportDXF = () =>
        {
            const dxf = exportPatternDXF(this.network, this.textile);
            triggerFileDownload(`${this.activePresetKey}_pattern_astm.dxf`, 'application/dxf', dxf);
            Toast('Exported Industrial Apparel Pattern (.DXF)');
        };
        const doExportOBJ = () =>
        {
            const obj = exportDrapedOBJ(this.solver);
            triggerFileDownload(`${this.activePresetKey}_draped_3d.obj`, 'text/plain', obj);
            Toast('Exported Draped 3D Garment (.OBJ)');
        };

        document.getElementById('btn-export-svg').addEventListener('click', doExportSVG);
        document.getElementById('btn-quick-svg').addEventListener('click', doExportSVG);
        document.getElementById('btn-inspector-svg').addEventListener('click', doExportSVG);
        document.getElementById('rail-export-svg').addEventListener('click', doExportSVG);
        document.getElementById('btn-export-dxf').addEventListener('click', doExportDXF);
        document.getElementById('btn-export-obj').addEventListener('click', doExportOBJ);

        // Bottom garment cycler
        document.getElementById('btn-cycle-garment').addEventListener('click', () =>
        {
            const keys = Object.keys(GARMENT_PRESET_CATALOG);
            const nextKey = keys[(keys.indexOf(this.activePresetKey) + 1) % keys.length];
            this.loadGarmentPreset(nextKey, true);
            this.running = true;
            this._syncPlayPauseUi();
        });

        // Inspector tabs
        document.querySelectorAll('#inspector-tabs button').forEach((btn) =>
        {
            btn.addEventListener('click', () =>
            {
                this.activeTab = btn.dataset.tab;
                document.querySelectorAll('#inspector-tabs button').forEach((b) => b.classList.toggle('active', b === btn));
                this.rebuildInspector();
            });
        });

        // Keyboard shortcuts
        window.addEventListener('keydown', (e) =>
        {
            if (e.target && (e.target.tagName === 'INPUT' || e.target.tagName === 'SELECT')) return;
            if (e.code === 'Space') { e.preventDefault(); document.getElementById('btn-play-pause').click(); }
            else if (e.key === 'b' || e.key === 'B') { this.runFitVerificationBake(); }
            else if (e.key === 'r' || e.key === 'R') { doReset(); }
            else if (e.key === 'f' || e.key === 'F') { fit2D(); }
            else if (e.key === 'g' || e.key === 'G') { document.querySelector('[data-cloth-tool="grab"]').click(); }
            else if (e.key === 'p' || e.key === 'P') { document.querySelector('[data-cloth-tool="pin"]').click(); }
            else if (e.key === 'q' || e.key === 'Q') { document.querySelector('[data-cloth-tool="orbit"]').click(); }
        });
    }

    _bindColumnAndSplitResizers()
    {
        const workspace = document.getElementById('workspace');
        const leftResizer = document.getElementById('pz-resizer-left');
        const rightResizer = document.getElementById('pz-resizer-right');
        const splitDivider = document.getElementById('split-divider');
        const stageSplit = document.getElementById('stage-split');

        const bindDrag = (handle, onMove) =>
        {
            if (!handle) return;
            let active = false;
            handle.addEventListener('pointerdown', (e) =>
            {
                active = true;
                handle.setPointerCapture(e.pointerId);
                e.preventDefault();
            });
            handle.addEventListener('pointermove', (e) => { if (active) onMove(e); });
            const stop = () => { active = false; };
            handle.addEventListener('pointerup', stop);
            handle.addEventListener('pointercancel', stop);
        };

        bindDrag(leftResizer, (e) =>
        {
            const w = Math.max(220, Math.min(400, e.clientX));
            workspace.style.setProperty('--left-w', `${w}px`);
        });
        bindDrag(rightResizer, (e) =>
        {
            const w = Math.max(260, Math.min(440, window.innerWidth - e.clientX));
            workspace.style.setProperty('--right-w', `${w}px`);
        });
        bindDrag(splitDivider, (e) =>
        {
            const rect = stageSplit.getBoundingClientRect();
            const frac = Math.max(0.24, Math.min(0.76, (e.clientX - rect.left) / Math.max(1, rect.width)));
            stageSplit.style.gridTemplateColumns = `${(frac * 100).toFixed(2)}% 6px 1fr`;
        });
    }

    _renderGarmentSelectorCards()
    {
        const grid = document.getElementById('garment-preset-grid');
        const notchTabs = document.getElementById('notch-garment-tabs');
        grid.innerHTML = '';
        notchTabs.innerHTML = '';

        for (const meta of Object.values(GARMENT_PRESET_CATALOG))
        {
            const active = meta.key === this.activePresetKey;
            const texInfo = TEXTILE_CATALOG[meta.defaultTextile];
            const card = Construct('button', 'cloth-preset-card' + (active ? ' active' : ''), [
                Construct('strong', null, meta.shortLabel),
                Construct('small', null, texInfo ? texInfo.shortLabel : meta.defaultTextile),
            ]);
            card.type = 'button';
            card.addEventListener('click', () =>
            {
                this.loadGarmentPreset(meta.key, true);
                this.running = true;
                this._syncPlayPauseUi();
                this.rebuildInspector();
            });
            grid.append(card);

            const nTab = Construct('button', 'doc-tab' + (active ? ' active' : ''), meta.shortLabel);
            nTab.type = 'button';
            nTab.addEventListener('click', () => card.click());
            notchTabs.append(nTab);
        }
    }

    _renderOutlinerLists()
    {
        if (!this.network) return;
        const pieceList = document.getElementById('pattern-piece-list');
        const seamList = document.getElementById('seam-stitch-list');
        pieceList.innerHTML = '';
        seamList.innerHTML = '';

        document.getElementById('outliner-summary').textContent =
            `${this.network.pieces.length} panels · ${this.network.seams.length} seams`;

        this.network.pieces.forEach((piece, idx) =>
        {
            const active = idx === this.canvas2D.selectedPieceIndex;
            const row = Construct('div', 'cloth-piece-row' + (active ? ' active' : ''), [
                Construct('span', null, piece.name),
                Construct('span', 'cloth-piece-meta', `${(piece.grainlineAngleDeg || 0).toFixed(0)}° · L${piece.layerOrder}`),
            ]);
            row.addEventListener('click', () =>
            {
                this.canvas2D.selectedPieceIndex = idx;
                this._syncSelectedPieceHud(idx);
                this._renderOutlinerLists();
                Broadcast();
            });
            pieceList.append(row);
        });

        this.network.seams.forEach((seam, idx) =>
        {
            const dot = Construct('span', 'cloth-seam-dot');
            dot.style.background = SEAM_COLORS[idx % SEAM_COLORS.length];
            const ratioTxt = Math.abs(seam.gatherRatio - 1.0) > 0.08
                ? `${seam.gatherRatio.toFixed(2)}:1`
                : `${seam.pairCount} st`;
            const row = Construct('div', 'cloth-seam-row', [
                Construct('span', null, [dot, seam.name]),
                Construct('span', 'cloth-piece-meta', ratioTxt),
            ]);
            seamList.append(row);
        });
    }

    _syncSelectedPieceHud(idx)
    {
        if (!this.network || !this.network.pieces[idx]) return;
        const p = this.network.pieces[idx];
        const cache = this.canvas2D._pieceRenderCache && this.canvas2D._pieceRenderCache[idx];
        document.getElementById('hud-piece-name').textContent = p.name;
        document.getElementById('hud-piece-dims').textContent = cache
            ? `${cache.widthMm.toFixed(0)} × ${cache.heightMm.toFixed(0)} mm`
            : '1:1 mm';
        document.getElementById('hud-piece-grain').textContent = `Grain ${(p.grainlineAngleDeg || 0).toFixed(0)}°`;
    }

    _syncPlayPauseUi()
    {
        document.getElementById('play-glyph').textContent = this.running ? '❚❚' : '▶';
        document.getElementById('play-label').textContent = this.running ? 'Simulating' : 'Paused';
    }

    _refreshHudAndStatus()
    {
        if (!this.solver || !this.network) return;
        const tel = this.solver.evaluateTelemetry();
        this.telemetry = tel;

        const yarnPct = (tel.meanYarnStretch - 1.0) * 100.0;
        document.getElementById('kpi-yarn-val').textContent = `${yarnPct >= 0 ? '+' : ''}${yarnPct.toFixed(2)}%`;
        document.getElementById('kpi-yarn-sub').textContent = `Crimp ${(Math.max(this.textile.warpCrimpPct, this.textile.weftCrimpPct)).toFixed(1)}%`;
        document.getElementById('kpi-seam-val').textContent = `${tel.meanSeamGapMm.toFixed(2)} mm`;
        document.getElementById('kpi-seam-sub').textContent = `${tel.seamPairCount} M:N pairs`;
        document.getElementById('kpi-pen-val').textContent = `${tel.maxPenetrationMm.toFixed(2)} mm`;
        document.getElementById('kpi-pen-sub').textContent = `${tel.peakPressureKpa.toFixed(1)} kPa peak`;

        const solverMeta = SOLVER_CATALOG[this.solver.solverMode] || SOLVER_CATALOG.avbd;
        document.getElementById('solver-mode-select').value = this.solver.solverMode;
        document.getElementById('solver-tier-tag').textContent = solverMeta.shortCode;
        document.getElementById('active-garment-title').textContent = this.network.name;
        document.getElementById('active-garment-sub').textContent =
            `${solverMeta.shortCode} · ${this.textile.name.toUpperCase()}`;

        document.getElementById('inspector-garment-name').value = GARMENT_PRESET_CATALOG[this.activePresetKey].shortLabel;
        document.getElementById('inspector-garment-sub').textContent =
            `${this.textile.shortLabel} · ${AVATAR_BODY_PRESETS[this.activeBodyPresetKey].name}`;

        const gpuActive = this.useWebGpu && this.gpu.supported;
        const backendTxt = gpuActive ? 'WebGPU Compute (WGSL)' : 'CPU Typed-Array (Deterministic)';
        document.getElementById('compute-backend-text').textContent = backendTxt;
        document.getElementById('status-backend-label').textContent = backendTxt;
        const speedTxt = `RMS ${this.solver.rmsSpeedMmS.toFixed(1)} mm/s`;
        document.getElementById('status-solver-time').textContent = this.solver.isSettled
            ? `At equilibrium · ${speedTxt}`
            : `Solve: ${tel.stepMs.toFixed(1)} ms · ${speedTxt}`;
        const pie = this.pieTelemetry || estimateClothResolutionPIE(this.textile);
        document.getElementById('status-mesh-counts').dataset.pie = `PIE r* ${pie.rPieBaseMm.toFixed(1)} mm · λ ${pie.wavelengthMm.toFixed(1)} mm`;
        document.getElementById('status-shear-info').textContent = `Bias Shear: ${tel.meanShearDeg.toFixed(1)}°`;
        document.getElementById('status-mesh-counts').innerHTML =
            `<b>${tel.pointCount}</b> pts · <b>${tel.faceCount}</b> tris · <b>${tel.seamPairCount}</b> stitches · <b>${tel.colorBatches}</b> color batches`;

        const certPill = document.getElementById('fit-cert-pill');
        const cert = this.solver.fitVerifiedCertificate;
        if (cert)
        {
            certPill.textContent = cert.verified
                ? `VERIFIED (${cert.maxExcessStretchPct.toFixed(2)}%)`
                : `EXCESS ${cert.maxExcessStretchPct.toFixed(1)}%`;
            certPill.classList.toggle('verified', !!cert.verified);
        }
        else
        {
            certPill.textContent = !this.running
                ? 'Paused'
                : (this.solver.isSettled
                    ? 'At rest (physical equilibrium)'
                    : `Relaxing · ${this.solver.rmsSpeedMmS.toFixed(1)} mm/s`);
            certPill.classList.toggle('verified', !!this.solver.isSettled);
        }
    }

    /*----------------------------------------------------------------------------------------------------------------------------------------
      RIGHT DOCK PROJECT-ZERO INSPECTOR TABS (Solver | Textile | Pattern | Avatar)
    ----------------------------------------------------------------------------------------------------------------------------------------*/
    rebuildInspector()
    {
        const body = document.getElementById('inspector-body');
        ReleaseScope(this.inspectorScope);
        body.innerHTML = '';
        this.inspectorScope = OpenScope();

        const crumbMap = {
            solver:  'Inspector / Top-2 Cloth Solvers & WebGPU',
            textile: 'Inspector / Orthotropic Textile Constitutive Law',
            pattern: 'Inspector / 2D Millimetre Pattern & Export',
            avatar:  'Inspector / Parametric Tailor Mannequin',
        };
        document.getElementById('inspector-crumb').textContent = crumbMap[this.activeTab] || 'Inspector';

        if (this.activeTab === 'solver') this._buildSolverTab(body);
        else if (this.activeTab === 'textile') this._buildTextileTab(body);
        else if (this.activeTab === 'pattern') this._buildPatternTab(body);
        else if (this.activeTab === 'avatar') this._buildAvatarTab(body);

        CloseScope(this.inspectorScope);
        Broadcast();
    }

    _buildSolverTab(parent)
    {
        const meta = SOLVER_CATALOG[this.solver.solverMode] || SOLVER_CATALOG.avbd;
        const { Inner: archInner } = SectionCard(parent, {
            Id: 'solver-arch',
            Title: 'Candidate Solver Architectures (A–F)',
            Glyph: 'Sparkles',
            Badge: meta.option,
            Open: true,
        });

        const banner = Construct('div', 'solver-compare-banner');
        banner.innerHTML = `<strong>${meta.title} — ${meta.role}</strong><br>${meta.summary}<br><em>${meta.citation}</em>`;
        archInner.append(banner);

        SegmentedRow(archInner, {
            Label: 'Algorithm',
            Options: [
                ['A · AVBD', 'avbd'],
                ['B · XPBD', 'xpbd'],
                ['C · Cosserat', 'scr'],
                ['D · GN Knot', 'gn_knot'],
                ['E · JGS2', 'jgs2'],
                ['F · KES-AA', 'kes_aa'],
            ],
            Read: () => this.solver.solverMode,
            Write: (val) =>
            {
                this.solver.solverMode = val;
                this.solver.fitVerifiedCertificate = null;
                this.solver.resetSolverMemory();
                this.solver.wakeUp();
                if (this.gpu.supported)
                    this.gpu.syncStateToGpu(this.solver);
                this.running = true;
                this._syncPlayPauseUi();
                this._refreshHudAndStatus();
                this.rebuildInspector();
            },
        });

        // Zhang et al. (2025) PIE live wrinkle/resolution telemetry + 1-click auto-resolution
        const pie = this.pieTelemetry || estimateClothResolutionPIE(this.textile);
        const pieCard = Construct('div', 'solver-compare-banner');
        pieCard.innerHTML = `<strong>PIE (Zhang 2025) — optimal mesh resolution</strong><br>`
            + `r* = ${pie.rPieBaseMm.toFixed(2)} mm (warp ${pie.rWarpMm.toFixed(2)} / weft ${pie.rWeftMm.toFixed(2)} mm, anisotropy ${pie.anisotropyRatio.toFixed(2)})<br>`
            + `wrinkle λ = ${pie.wavelengthMm.toFixed(2)} mm · wrinklon transition = ${pie.wrinklonTransitionMm.toFixed(2)} mm<br>`
            + `recommended authoring spacing ${pie.authoringSpacingMm.toFixed(1)} mm · game spacing ${pie.gameSpacingMm.toFixed(1)} mm · current ${this.particleSpacingMm.toFixed(1)} mm`;
        archInner.append(pieCard);
        ActionRow(archInner, [{
            Label: 'PIE Auto-Res (Zhang 2025)',
            Glyph: 'Sparkles',
            Tone: 'primary',
            Action: () =>
            {
                this.pieTelemetry = estimateClothResolutionPIE(this.textile);
                this.particleSpacingMm = this.pieTelemetry.authoringSpacingMm;
                this.rebuildPatternNetwork(true);
                this.rebuildInspector();
                Toast(`PIE auto-res applied: ${this.particleSpacingMm.toFixed(1)} mm`);
            },
        }]);

        SwitchRow(archInner, {
            Label: 'WebGPU Compute (WGSL)',
            Detail: this.gpu.supported ? `Active: ${this.gpu.adapterName}` : 'Auto-fallback: CPU Typed-Array active',
            Read: () => this.useWebGpu && this.gpu.supported,
            Write: (v) =>
            {
                this.useWebGpu = v;
                if (v && this.gpu.supported) this.gpu.syncStateToGpu(this.solver);
                this._refreshHudAndStatus();
            },
            Availability: () => ({
                Enabled: this.gpu.supported,
                Reason: 'WebGPU adapter unavailable in this browser; deterministic CPU Typed-Array solver active.',
            }),
        });

        SliderRow(archInner, {
            Label: 'Substeps / Frame',
            Minimum: 6, Maximum: 16, Step: 1, Unit: 'st',
            Read: () => this.solver.substeps,
            Write: (v) => { this.solver.substeps = v; },
        });

        SliderRow(archInner, {
            Label: 'Mesh Spacing',
            Minimum: 12.0, Maximum: 22.0, Step: 0.5, Unit: 'mm',
            Read: () => this.particleSpacingMm,
            Write: (v) => { this.particleSpacingMm = v; this.rebuildPatternNetwork(true); },
        });

        SwitchRow(archInner, {
            Label: 'Barycentric Self-Collision',
            Detail: 'Deterministic micrometre Int32 accumulators',
            Read: () => this.solver.enableSelfCollision,
            Write: (v) => { this.solver.enableSelfCollision = v; },
        });

        const { Inner: bakeInner } = SectionCard(parent, {
            Id: 'fit-bake',
            Title: 'Pre-Cut Fit Verification (Bake)',
            Glyph: 'Check',
            Hint: 'Runs SIGGRAPH Asia 2024 Cubic-Barrier + Strict Crimp-Wall Equilibrium Polish to verify <0.5% woven yarn drift prior to physical cutting.',
            Open: true,
        });

        MetricTrio(bakeInner, [
            {
                Label: 'Yarn Drift',
                Highlight: true,
                Read: () =>
                {
                    const c = this.solver.fitVerifiedCertificate;
                    if (c) return { value: `${c.maxExcessStretchPct.toFixed(2)}%`, sub: c.verified ? 'Verified' : 'Above Crimp' };
                    const tel = this.telemetry || this.solver.evaluateTelemetry();
                    return { value: `${((tel.meanYarnStretch - 1) * 100).toFixed(2)}%`, sub: 'Live Mean' };
                },
            },
            {
                Label: 'Bias Shear',
                Read: () =>
                {
                    const tel = this.telemetry || this.solver.evaluateTelemetry();
                    return { value: `${tel.meanShearDeg.toFixed(1)}°`, sub: `Max ${tel.maxShearDeg.toFixed(1)}°` };
                },
            },
            {
                Label: 'Seam / Pen',
                Read: () =>
                {
                    const tel = this.telemetry || this.solver.evaluateTelemetry();
                    return { value: `${tel.meanSeamGapMm.toFixed(2)}mm`, sub: `Pen ${tel.maxPenetrationMm.toFixed(2)}mm` };
                },
            },
        ]);

        ActionRow(bakeInner, [
            { Label: 'Verify Fit (Bake)', Glyph: 'Check', Tone: 'primary', Act: () => this.runFitVerificationBake() },
            { Label: 'Re-Drape', Glyph: 'Reset', Act: () => document.getElementById('btn-reset-drape').click() },
        ]);

        const { Inner: windInner } = SectionCard(parent, {
            Id: 'wind-stage',
            Title: 'Runway Wind & Diagnostics',
            Glyph: 'Gauge',
            Open: true,
        });

        SwitchRow(windInner, {
            Label: 'Studio Runway Breeze',
            Read: () => this.solver.windEnabled,
            Write: (v) =>
            {
                this.solver.windEnabled = v;
                document.getElementById('btn-toggle-wind').classList.toggle('active', v);
            },
        });

        SliderRow(windInner, {
            Label: 'Wind Speed',
            Minimum: 0.5, Maximum: 7.0, Step: 0.1, Unit: 'm/s',
            Read: () => this.solver.windSpeed,
            Write: (v) => { this.solver.windSpeed = v; },
        });

        SelectRow(windInner, {
            Label: 'Inspection Lens',
            Options: [
                ['Physical Textile', 'textile'],
                ['Warp/Weft Strain %', 'strain'],
                ['Bias Shear Angle (°)', 'shear'],
                ['Contact Pressure (kPa)', 'pressure'],
                ['GPU Color Batches', 'batches'],
            ],
            Read: () => this.heatmapMode,
            Write: (v) =>
            {
                const sel = document.getElementById('heatmap-mode-select');
                sel.value = v;
                sel.dispatchEvent(new Event('change'));
            },
        });
    }

    _buildTextileTab(parent)
    {
        const { Inner: catInner } = SectionCard(parent, {
            Id: 'textile-preset',
            Title: 'Physical Textile Preset',
            Glyph: 'Layers',
            Badge: `${this.textile.gsm} GSM`,
            Open: true,
        });

        SelectRow(catInner, {
            Label: 'Fabric Roll',
            Options: Object.values(TEXTILE_CATALOG).map((t) => [t.name, t.key]),
            Read: () => this.activeTextileKey,
            Write: (key) =>
            {
                this.activeTextileKey = key;
                this.textile = cloneTextileMaterial(key);
                this.solver.setTextile(this.textile);
                this.viewport3D.syncTextileAppearance();
                this.running = true;
                this._syncPlayPauseUi();
                this._refreshHudAndStatus();
                this.rebuildInspector();
            },
        });

        const swatchWrap = Construct('div', 'swatch-grid');
        TEXTILE_COLOR_SWATCHES.forEach((sw) =>
        {
            const b = Construct('button', 'swatch-btn' + (sw.hex.toLowerCase() === this.fabricHex.toLowerCase() ? ' active' : ''));
            b.type = 'button';
            b.style.background = sw.hex;
            b.title = sw.name;
            b.addEventListener('click', () =>
            {
                this.fabricHex = sw.hex;
                this.canvas2D.fabricHex = sw.hex;
                this.viewport3D.fabricHex = sw.hex;
                this.viewport3D.updateClothBuffers();
                this.rebuildInspector();
            });
            swatchWrap.append(b);
        });
        catInner.append(swatchWrap);

        const { Inner: orthoInner } = SectionCard(parent, {
            Id: 'ortho-constitutive',
            Title: 'Orthotropic Continuum & Crimp Law',
            Glyph: 'Sliders',
            Hint: 'SI Kawabata / CUSICK physical parameters driving 2D (u,v) Warp, Weft, Bias Shear, and Isometric Plate Bending.',
            Open: true,
        });

        SliderRow(orthoInner, {
            Label: 'Areal Weight',
            Minimum: 50, Maximum: 520, Step: 5, Unit: 'GSM',
            Read: () => this.textile.gsm,
            Write: (v) => { this.textile.gsm = v; this.solver.updateTextileMasses(); this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Warp Modulus',
            Minimum: 120, Maximum: 7000, Step: 50, Unit: 'N/m',
            Read: () => this.textile.warpModulus,
            Write: (v) => { this.textile.warpModulus = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Weft Modulus',
            Minimum: 120, Maximum: 6000, Step: 50, Unit: 'N/m',
            Read: () => this.textile.weftModulus,
            Write: (v) => { this.textile.weftModulus = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Shear Modulus',
            Minimum: 15, Maximum: 900, Step: 5, Unit: 'N/m',
            Read: () => this.textile.shearModulus,
            Write: (v) => { this.textile.shearModulus = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Warp Crimp Slack',
            Minimum: 0.5, Maximum: 25.0, Step: 0.2, Unit: '%',
            Read: () => this.textile.warpCrimpPct,
            Write: (v) => { this.textile.warpCrimpPct = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Weft Crimp Slack',
            Minimum: 0.5, Maximum: 28.0, Step: 0.2, Unit: '%',
            Read: () => this.textile.weftCrimpPct,
            Write: (v) => { this.textile.weftCrimpPct = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Bending Rigidity',
            Minimum: 0.02, Maximum: 5.50, Step: 0.02, Unit: 'mN·m',
            Read: () => this.textile.bendRigidity,
            Write: (v) => { this.textile.bendRigidity = v; this.solver.wakeUp(); },
        });
        SliderRow(orthoInner, {
            Label: 'Coulomb Friction',
            Minimum: 0.08, Maximum: 0.80, Step: 0.02, Unit: 'μ',
            Read: () => this.textile.frictionKinetic,
            Write: (v) => { this.textile.frictionKinetic = v; this.textile.frictionStatic = v + 0.10; this.solver.wakeUp(); },
        });
    }

    _buildPatternTab(parent)
    {
        const { Inner: paramInner } = SectionCard(parent, {
            Id: 'pattern-parametric',
            Title: '2D Parametric Pattern Grading',
            Glyph: 'Scissors',
            Hint: 'Adjusts real 2D millimetre sewing contours and re-stitches M:N seams live onto the mannequin.',
            Open: true,
        });

        SliderRow(paramInner, {
            Label: 'Circumference Ease',
            Minimum: 0.88, Maximum: 1.24, Step: 0.01, Unit: '×',
            Read: () => this.patternOverrides.easeScale,
            Write: (v) => { this.patternOverrides.easeScale = v; this.rebuildPatternNetwork(true); },
        });
        SliderRow(paramInner, {
            Label: 'Hem Length Scale',
            Minimum: 0.72, Maximum: 1.30, Step: 0.01, Unit: '×',
            Read: () => this.patternOverrides.lengthScale,
            Write: (v) => { this.patternOverrides.lengthScale = v; this.rebuildPatternNetwork(true); },
        });
        SliderRow(paramInner, {
            Label: 'Skirt / Gather Flare',
            Minimum: 0.80, Maximum: 1.45, Step: 0.02, Unit: '×',
            Read: () => this.patternOverrides.flareScale,
            Write: (v) => { this.patternOverrides.flareScale = v; this.rebuildPatternNetwork(true); },
        });
        SliderRow(paramInner, {
            Label: 'Grainline Rotation',
            Minimum: -45, Maximum: 90, Step: 5, Unit: 'deg',
            Read: () => this.patternOverrides.grainOffsetDeg,
            Write: (v) => { this.patternOverrides.grainOffsetDeg = v; this.rebuildPatternNetwork(true); },
        });

        const { Inner: viewInner } = SectionCard(parent, {
            Id: 'pattern-2d-display',
            Title: '2D Drafting Sheet & 1:1 CAD Export',
            Glyph: 'Download',
            Open: true,
        });

        SwitchRow(viewInner, {
            Label: 'Show Seam Allowance (mm)',
            Read: () => this.canvas2D.showSeamAllowance,
            Write: (v) => { this.canvas2D.showSeamAllowance = v; },
        });
        SwitchRow(viewInner, {
            Label: 'Show Isotropic Honeycomb Mesh',
            Read: () => this.canvas2D.showMeshWire,
            Write: (v) => { this.canvas2D.showMeshWire = v; },
        });
        SwitchRow(viewInner, {
            Label: 'Show M:N Sewing Stitches',
            Read: () => this.canvas2D.showSeamLines,
            Write: (v) => { this.canvas2D.showSeamLines = v; },
        });

        ActionRow(viewInner, [
            { Label: '1:1 Cutting SVG', Glyph: 'Download', Tone: 'primary', Act: () => document.getElementById('btn-export-svg').click() },
            { Label: 'ASTM DXF', Glyph: 'Download', Act: () => document.getElementById('btn-export-dxf').click() },
        ]);
        ActionRow(viewInner, [
            { Label: 'Draped 3D OBJ', Glyph: 'Cube', Act: () => document.getElementById('btn-export-obj').click() },
        ]);
    }

    _buildAvatarTab(parent)
    {
        const { Inner: presetInner } = SectionCard(parent, {
            Id: 'avatar-preset',
            Title: 'Parametric Tailor Form',
            Glyph: 'Sphere',
            Open: true,
        });

        SelectRow(presetInner, {
            Label: 'Body Standard',
            Options: Object.entries(AVATAR_BODY_PRESETS).map(([k, v]) => [v.name, k]),
            Read: () => this.activeBodyPresetKey,
            Write: (key) =>
            {
                this.activeBodyPresetKey = key;
                this.mannequin.applyBodyPreset(key);
                this.mannequin.ensureSignedDistanceVolume();
                this.viewport3D.refreshMannequinGeometry();
                this.rebuildPatternNetwork(true);
                this.rebuildInspector();
            },
        });

        SwitchRow(presetInner, {
            Label: 'Show Mannequin Form',
            Read: () => this.viewport3D.showMannequin,
            Write: (v) => { this.viewport3D.setVisibility({ showMannequin: v }); },
        });
        SwitchRow(presetInner, {
            Label: 'Show Tailor Tape Rings',
            Read: () => this.viewport3D.showMeasureRings,
            Write: (v) => { this.viewport3D.setVisibility({ showMeasureRings: v }); },
        });

        const { Inner: morphInner } = SectionCard(parent, {
            Id: 'avatar-morphs',
            Title: 'Live Anthropometric Morphs',
            Glyph: 'Sliders',
            Open: true,
        });

        const bindMorph = (label, key, min = -1.0, max = 1.0) =>
        {
            SliderRow(morphInner, {
                Label: label,
                Minimum: min, Maximum: max, Step: 0.05, Unit: 'm',
                Read: () => this.mannequin.params[key] || 0.0,
                Write: (val) =>
                {
                    this.mannequin.setParam(key, val);
                    this.mannequin.ensureSignedDistanceVolume();
                    this.viewport3D.refreshMannequinGeometry();
                    this.solver.wakeUp();
                    if (this.gpu.supported) this.gpu.syncStateToGpu(this.solver);
                    this._renderMeasureTable();
                },
            });
        };

        bindMorph('Bust Fullness', 'bust', -0.6, 1.0);
        bindMorph('Waist Taper', 'waist', -0.8, 1.0);
        bindMorph('Hip Curve', 'hips', -0.6, 1.0);
        bindMorph('Shoulder Width', 'shoulders', -0.6, 1.0);
        bindMorph('Body Weight', 'weight', -0.6, 1.0);
        bindMorph('Stature Height', 'height', -0.8, 0.8);

        const { Inner: tapeInner } = SectionCard(parent, {
            Id: 'avatar-measurements',
            Title: 'Live Tailor Tape Readout (cm)',
            Glyph: 'Align',
            Open: true,
        });
        this._measureTableEl = Construct('div', 'measure-table');
        tapeInner.append(this._measureTableEl);
        this._renderMeasureTable();
    }

    _renderMeasureTable()
    {
        if (!this._measureTableEl || !this.mannequin) return;
        const m = this.mannequin.measurements;
        const rows = [
            ['Stature Height', `${m.height.toFixed(1)} cm`],
            ['Neck Base',      `${m.neck.toFixed(1)} cm`],
            ['Shoulder Span',  `${m.shoulderWidth.toFixed(1)} cm`],
            ['Bust Girth',     `${m.bust.toFixed(1)} cm`],
            ['Underbust',      `${m.underbust.toFixed(1)} cm`],
            ['Natural Waist',  `${m.waist.toFixed(1)} cm`],
            ['High/Full Hip',  `${m.hips.toFixed(1)} cm`],
        ];
        this._measureTableEl.innerHTML = rows.map(([k, v]) => `<span>${k}</span><strong>${v}</strong>`).join('');
    }
}

const studio = new ClothStudioApp();
window.__FrontierClothStudio = studio;
studio.initialize().catch((err) =>
{
    console.error('Frontier Cloth Atelier initialization error:', err);
});
