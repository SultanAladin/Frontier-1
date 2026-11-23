/*============================================================================================================================================
                                                           ATELIERPANELS.JS
                       Zero-Build ESM UI Controller Rendering the Project-Zero Atelier Shell, Docks, Notch,
                     Pill Sliders, Mannequin Anthropometrics, Outliner, Solver Diagnostics, and Asset Drawer
============================================================================================================================================*/

import { GARMENT_PRESET_CATALOG } from '../Engine/GarmentPresets.js';
import { TEXTILE_CATALOG } from '../Engine/TextileCatalog.js';
import { SOLVER_CATALOG } from '../Engine/ClothSolvers.js';
import { BODY_PRESETS, BODY_SLIDER_SPECS } from '../Engine/MannequinAvatar.js';

const DYE_SWATCHES = [
    { name: 'Terracotta',  hex: '#c4513d' },
    { name: 'Champagne',   hex: '#e2cfb8' },
    { name: 'Atelier Slate', hex: '#495668' },
    { name: 'Emerald',     hex: '#2e7d62' },
    { name: 'Raw Indigo',  hex: '#273a58' },
    { name: 'Burgundy',    hex: '#6d2534' },
    { name: 'Oatmeal',     hex: '#c9bfae' },
    { name: 'Obsidian',    hex: '#24252a' },
];

export function mountAtelierUI({ app, refs })
{
    let leftTab = 'Outliner';       // 'Outliner' | 'Drafting' | 'Avatar'
    let rightTab = 'Solvers';       // 'Solvers' | 'Textile' | 'Export'
    let drawerOpen = false;
    let drawerCategory = 'garments'; // 'garments' | 'textiles'

    function sliderHtml(id, label, min, max, step, value, unit = '', decimals = 2)
    {
        const pct = Math.max(0, Math.min(100, ((value - min) / Math.max(1e-9, max - min)) * 100));
        const valStr = Number(value).toFixed(decimals);
        return `
            <div class="pz-slider-row">
                <div class="pz-slider-head">
                    <span class="pz-slider-label">${label}</span>
                    <span class="split-value" data-val-for="${id}">
                        <span>${valStr}</span>
                        ${unit ? `<span class="split-unit">${unit}</span>` : ''}
                    </span>
                </div>
                <input type="range" class="pz-slider" id="${id}" min="${min}" max="${max}" step="${step}" value="${value}" style="--fill:${pct.toFixed(1)}%">
            </div>
        `;
    }

    function bindSlider(container, id, unit, decimals, onInput)
    {
        const el = container.querySelector(`#${id}`);
        if (!el) return;
        const badge = container.querySelector(`[data-val-for="${id}"]`);
        el.addEventListener('input', () =>
        {
            const v = parseFloat(el.value);
            const pct = Math.max(0, Math.min(100, ((v - parseFloat(el.min)) / Math.max(1e-9, parseFloat(el.max) - parseFloat(el.min))) * 100));
            el.style.setProperty('--fill', `${pct.toFixed(1)}%`);
            if (badge)
            {
                badge.innerHTML = `<span>${v.toFixed(decimals)}</span>${unit ? `<span class="split-unit">${unit}</span>` : ''}`;
            }
            onInput(v);
        });
    }

    function renderNotch(snap)
    {
        const presetOpts = Object.values(GARMENT_PRESET_CATALOG).map((g) =>
            `<option value="${g.key}" ${g.key === snap.garmentKey ? 'selected' : ''}>${g.shortLabel || g.name}</option>`
        ).join('');

        const solverOpts = Object.values(SOLVER_CATALOG).map((s) =>
            `<option value="${s.key}" ${s.key === snap.solverKey ? 'selected' : ''}>${s.shortLabel}</option>`
        ).join('');

        refs.notch.innerHTML = `
            <div class="notch-brand" title="${snap.gpuStatusReason || ''}">
                <span class="notch-dot ${snap.gpuActive ? '' : 'cpu'}"></span>
                <span class="notch-title">Project-Zero</span>
                <span class="notch-sub">Cloth Atelier · ${snap.gpuActive ? 'WebGPU' : 'CPU'}</span>
            </div>
            <div class="notch-group">
                <select class="notch-select" id="notch-garment">${presetOpts}</select>
                <select class="notch-select" id="notch-solver">${solverOpts}</select>
            </div>
            <div class="notch-group">
                <button class="notch-btn ${(snap.isPlaying || snap.isPlayback) ? 'playing' : 'primary'}" id="notch-play">${(snap.isPlaying || snap.isPlayback) ? 'Pause' : 'Play'}</button>
                <button class="notch-btn" id="notch-step">Step</button>
                <button class="notch-btn" id="notch-steam">Steam</button>
                <button class="notch-btn" id="notch-reset">Reset</button>
            </div>
        `;

        refs.notch.querySelector('#notch-garment').onchange = (e) => app.selectGarment(e.target.value);
        refs.notch.querySelector('#notch-solver').onchange = (e) => app.selectSolver(e.target.value);
        refs.notch.querySelector('#notch-play').onclick = () => app.toggleLivePlay();
        refs.notch.querySelector('#notch-step').onclick = () =>
        {
            app.stepOnce();
            renderAll(app.getSnapshot());
        };
        refs.notch.querySelector('#notch-steam').onclick = () => app.steamRelax();
        refs.notch.querySelector('#notch-reset').onclick = () => app.rebuildGarment();
    }

    function renderLeftDock(snap)
    {
        const pieces = snap.pieces || [];
        const selPiece = pieces[snap.selectedPieceIndex] || pieces[0];
        const m = snap.mannequinMeasures || {};
        const mParams = snap.mannequinParams || {};
        const gMeta = GARMENT_PRESET_CATALOG[snap.garmentKey];

        let bodyHtml = '';
        if (leftTab === 'Outliner')
        {
            const pieceRows = pieces.map((p, idx) => `
                <div class="outliner-item ${idx === snap.selectedPieceIndex ? 'selected' : ''}" data-piece-idx="${idx}">
                    <div>
                        <div class="outliner-item-title">${p.name}</div>
                        <div class="outliner-item-meta">Layer ${p.layerOrder} · Grain ${Math.round(p.grainlineAngleDeg)}° · ${p.vertexCount} pts</div>
                    </div>
                    <span class="pz-pill-badge">SA ${p.seamAllowanceMm}mm</span>
                </div>
            `).join('');

            const seamRows = (snap.seams || []).map((s) => `
                <div class="outliner-item">
                    <div>
                        <div class="outliner-item-title">${s.name}</div>
                        <div class="outliner-item-meta">${s.pairCount} stitch pairs · ${Math.round(s.lengthAMm)}/${Math.round(s.lengthBMm)}mm</div>
                    </div>
                    <span class="pz-pill-badge green">Active</span>
                </div>
            `).join('');

            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">${gMeta.shortLabel || gMeta.name}</span>
                        <span class="pz-pill-badge amber">${pieces.length} Panels</span>
                    </div>
                    <div class="pz-sub-caption">${gMeta.description}</div>
                    <div class="outliner-list">${pieceRows}</div>
                </div>
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">M:N Sewing Topology</span>
                        <span class="pz-pill-badge">${(snap.seams || []).length} Seams</span>
                    </div>
                    <div class="outliner-list">${seamRows}</div>
                </div>
            `;
        }
        else if (leftTab === 'Drafting')
        {
            const ov = snap.patternOverrides || { easeScale: 1.0, lengthScale: 1.0, flareScale: 1.0, grainOffsetDeg: 0.0 };

            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">${gMeta.shortLabel || gMeta.name}</span>
                        <span class="pz-pill-badge amber">${selPiece ? selPiece.name : `${pieces.length} Panels`}</span>
                    </div>
                    <div class="pz-sub-caption">Live parametric block drafting updates 2D UV cutting sheets, arc-length M:N seams, and 3D drape mesh.</div>
                    ${sliderHtml('p-grain', 'Grainline Bias Offset', -45, 45, 1, ov.grainOffsetDeg, '°', 0)}
                    ${sliderHtml('p-ease', 'Pattern Ease Scale', 0.85, 1.35, 0.01, ov.easeScale, '×', 2)}
                    ${sliderHtml('p-len', 'Hem / Drop Length', 0.75, 1.25, 0.01, ov.lengthScale, '×', 2)}
                    ${sliderHtml('p-flare', 'Hem Flare / Gather', 0.80, 1.35, 0.01, ov.flareScale, '×', 2)}
                </div>
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">PIE Mesh Density (Zhang 2025)</span>
                        <span class="pz-pill-badge">${snap.pieSummary.authoringSpacingMm.toFixed(1)} mm</span>
                    </div>
                    ${sliderHtml('p-res', 'Resolution Multiplier', 0.75, 1.50, 0.05, snap.meshResolutionScale, '×', 2)}
                    <div class="pz-btn-row">
                        <button class="pz-action-btn" id="btn-fit-2d">Fit 2D Pattern</button>
                        <button class="pz-action-btn" id="btn-reset-pattern">Reset Pattern</button>
                    </div>
                </div>
            `;
        }
        else
        {
            const bodyPresetButtons = Object.entries(BODY_PRESETS).map(([k, bp]) => `
                <button class="solver-pill-btn ${snap.bodyPresetKey === k ? 'active' : ''}" data-bpkey="${k}">
                    <span class="solver-pill-name">${bp.name}</span>
                    <span class="solver-pill-tag">${bp.subtitle}</span>
                </button>
            `).join('');

            const sliderCards = ['Silhouette', 'Upper Torso', 'Waist & Hip', 'Limbs'].map((grp) =>
            {
                const specs = BODY_SLIDER_SPECS.filter((s) => s.group === grp);
                return `
                    <div class="pz-card">
                        <div class="pz-card-header">
                            <span class="pz-card-title">${grp}</span>
                            ${grp === 'Silhouette' ? '<button class="pane-chip-btn" id="btn-reset-morphs">Reset</button>' : ''}
                        </div>
                        ${specs.map((s) => sliderHtml(`m-${s.key}`, s.label, s.min, s.max, s.step, mParams[s.key] ?? s.def, '', 2)).join('')}
                    </div>
                `;
            }).join('');

            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Atelier Tailor Form</span>
                        <span class="pz-pill-badge">Faceless · ${(m.height || 167.5).toFixed(1)} cm</span>
                    </div>
                    <div class="kpi-grid">
                        <div class="kpi-cell"><span class="kpi-label">Bust / Chest</span><span class="kpi-val">${(m.bust || 0).toFixed(1)} cm</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Waist</span><span class="kpi-val">${(m.waist || 0).toFixed(1)} cm</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Hip</span><span class="kpi-val">${(m.hips || 0).toFixed(1)} cm</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Shoulder Span</span><span class="kpi-val">${(m.shoulderWidth || 0).toFixed(1)} cm</span></div>
                    </div>
                    <div class="solver-grid">${bodyPresetButtons}</div>
                    <div class="pz-btn-row">
                        <button class="pz-action-btn accent" id="btn-redrape-morph">Redrape Pattern to Form</button>
                    </div>
                </div>
                ${sliderCards}
            `;
        }

        refs.leftDock.innerHTML = `
            <div class="dock-tab-strip">
                ${['Outliner', 'Drafting', 'Avatar'].map((t) =>
                    `<button class="trapezoid-tab ${leftTab === t ? 'active' : ''}" data-ltab="${t}">${t}</button>`
                ).join('')}
            </div>
            <div class="dock-scroll">${bodyHtml}</div>
        `;

        refs.leftDock.querySelectorAll('[data-ltab]').forEach((btn) =>
        {
            btn.onclick = () => { leftTab = btn.dataset.ltab; renderLeftDock(app.getSnapshot()); };
        });

        refs.leftDock.querySelectorAll('[data-piece-idx]').forEach((row) =>
        {
            row.onclick = () =>
            {
                const idx = parseInt(row.dataset.pieceIdx, 10);
                app.selectedPieceIndex = idx;
                app.pattern2d.selectedPieceIndex = idx;
                app.pattern2d.markDirty();
                renderLeftDock(app.getSnapshot());
            };
        });

        bindSlider(refs.leftDock, 'p-grain', '°', 0, (v) => app.updatePatternOverride({ grainOffsetDeg: v }));
        bindSlider(refs.leftDock, 'p-ease', '×', 2, (v) => app.updatePatternOverride({ easeScale: v }));
        bindSlider(refs.leftDock, 'p-len', '×', 2, (v) => app.updatePatternOverride({ lengthScale: v }));
        bindSlider(refs.leftDock, 'p-flare', '×', 2, (v) => app.updatePatternOverride({ flareScale: v }));
        bindSlider(refs.leftDock, 'p-res', '×', 2, (v) => { app.meshResolutionScale = v; app.rebuildGarment(); });
        const fitBtn = refs.leftDock.querySelector('#btn-fit-2d');
        if (fitBtn) fitBtn.onclick = () => app.pattern2d.fitToView();
        const rstPatBtn = refs.leftDock.querySelector('#btn-reset-pattern');
        if (rstPatBtn) rstPatBtn.onclick = () => app.rebuildGarment({ resetPatternOverrides: true });

        refs.leftDock.querySelectorAll('[data-bpkey]').forEach((btn) =>
        {
            btn.onclick = () => app.selectBodyPreset(btn.dataset.bpkey);
        });
        for (const s of BODY_SLIDER_SPECS)
            bindSlider(refs.leftDock, `m-${s.key}`, '', 2, (v) => app.setMannequinMorph(s.key, v));
        const rstM = refs.leftDock.querySelector('#btn-reset-morphs');
        if (rstM) rstM.onclick = () => app.resetMannequinMorphs();
        const redrapeM = refs.leftDock.querySelector('#btn-redrape-morph');
        if (redrapeM) redrapeM.onclick = () => app.rebuildGarment();
    }

    function renderRightDock(snap)
    {
        const sMeta = SOLVER_CATALOG[snap.solverKey];
        const cfg = snap.solverConfig || { substeps: 4, iterations: 12, dampingScale: 1, windEnabled: false, windSpeed: 2.6, windAngleDeg: 28, selfCollisionEnabled: true };
        const met = snap.metrics || {};
        const tex = TEXTILE_CATALOG[snap.textileKey];
        const flags = snap.viewportFlags;

        let bodyHtml = '';
        if (rightTab === 'Solvers')
        {
            const solverButtons = Object.values(SOLVER_CATALOG).map((s) => `
                <button class="solver-pill-btn ${s.key === snap.solverKey ? 'active' : ''}" data-skey="${s.key}">
                    <span class="solver-pill-name">${s.shortLabel}</span>
                    <span class="solver-pill-tag">${s.badge}</span>
                </button>
            `).join('');

            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Cloth Research Solver</span>
                        <span class="pz-pill-badge ${snap.gpuActive ? 'green' : 'amber'}">${snap.gpuActive ? 'WebGPU Compute' : 'CPU Precise'}</span>
                    </div>
                    <div class="solver-grid">${solverButtons}</div>
                    <div class="pz-sub-caption"><strong>${sMeta.label}:</strong> ${sMeta.summary}</div>
                    <div class="toggle-row">
                        <span class="toggle-label">WebGPU Hardware Path (AVBD / XPBD)</span>
                        <button class="pz-switch ${snap.useWebGpu ? 'on' : ''}" id="sw-webgpu" ${snap.gpuAvailable ? '' : 'disabled'}></button>
                    </div>
                    <div class="pz-sub-caption" style="margin-top:4px">${snap.gpuStatusReason || ''}</div>
                </div>

                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Live Telemetry</span>
                        <span class="pz-pill-badge" id="metric-badge-fps">${(met.fps || 60).toFixed(0)} FPS</span>
                    </div>
                    <div class="kpi-grid">
                        <div class="kpi-cell"><span class="kpi-label">Frame Solve</span><span class="kpi-val" id="m-step">${(met.stepTimeMs || 0).toFixed(2)} ms</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Mesh Size</span><span class="kpi-val" id="m-mesh">${met.faceCount || 0} Δ</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Max Strain</span><span class="kpi-val" id="m-strain">${(met.maxStrainPct || 0).toFixed(1)}%</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Body Penetration</span><span class="kpi-val good" id="m-pen">${(met.mannequinPenetrationPct || 0).toFixed(2)}%</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Seam Residual</span><span class="kpi-val" id="m-seam">${(met.seamResidualMm || 0).toFixed(2)} mm</span></div>
                        <div class="kpi-cell"><span class="kpi-label">Mean Fold Angle</span><span class="kpi-val" id="m-fold">${(met.meanFoldAngleDeg || 0).toFixed(1)}°</span></div>
                    </div>
                </div>

                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Integration & Environment</span>
                    </div>
                    ${sliderHtml('s-substeps', 'Substeps / Frame', 1, 12, 1, cfg.substeps, '', 0)}
                    ${sliderHtml('s-iters', 'Solver Iterations', 4, 28, 1, cfg.iterations, '', 0)}
                    ${sliderHtml('s-damp', 'Aerodynamic Damping', 0.25, 2.50, 0.05, cfg.dampingScale, '×', 2)}
                    <div class="toggle-row">
                        <span class="toggle-label">Self-Collision & Layer Order</span>
                        <button class="pz-switch ${cfg.selfCollisionEnabled ? 'on' : ''}" id="sw-selfcol"></button>
                    </div>
                    <div class="toggle-row">
                        <span class="toggle-label">Atelier Wind Field</span>
                        <button class="pz-switch ${cfg.windEnabled ? 'on' : ''}" id="sw-wind"></button>
                    </div>
                    ${sliderHtml('s-windspd', 'Wind Speed', 0.0, 8.0, 0.2, cfg.windSpeed, 'm/s', 1)}
                    ${sliderHtml('s-windang', 'Wind Azimuth', 0, 360, 5, cfg.windAngleDeg, '°', 0)}
                </div>
            `;
        }
        else if (rightTab === 'Textile')
        {
            const dyeDots = DYE_SWATCHES.map((d) =>
                `<button class="dye-dot ${snap.fabricColor.toLowerCase() === d.hex.toLowerCase() ? 'active' : ''}" data-hex="${d.hex}" title="${d.name}" style="background:${d.hex}"></button>`
            ).join('');

            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">${tex.name}</span>
                        <span class="pz-pill-badge amber">${tex.gsm} GSM · ${tex.thicknessMm} mm</span>
                    </div>
                    <div class="pz-sub-caption">${tex.category} (${tex.code}) · Shear Lock ${tex.lockingAngleDeg}° · Crimp ${tex.warpCrimpPct}%/${tex.weftCrimpPct}%</div>
                    <div class="dye-palette">${dyeDots}</div>
                </div>
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Anisotropic Orthotropic Elastic Tensor</span>
                    </div>
                    ${sliderHtml('t-warp', 'Warp Modulus', 200, 12000, 50, tex.warpModulus, 'N/m', 0)}
                    ${sliderHtml('t-weft', 'Weft Modulus', 200, 12000, 50, tex.weftModulus, 'N/m', 0)}
                    ${sliderHtml('t-shear', 'In-Plane Shear', 40, 2600, 10, tex.shearModulus, 'N/m', 0)}
                    ${sliderHtml('t-bend', 'Bending Rigidity', 0.04, 2.20, 0.02, tex.bendRigidity, 'cN·cm', 2)}
                    ${sliderHtml('t-fric', 'Kinetic Friction μ', 0.12, 0.72, 0.02, tex.frictionKinetic, '', 2)}
                </div>
            `;
        }
        else
        {
            bodyHtml = `
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Viewport Overlays</span>
                    </div>
                    <div class="toggle-row"><span class="toggle-label">Mannequin Tailor Form</span><button class="pz-switch ${flags.showMannequin ? 'on' : ''}" id="v-mann"></button></div>
                    <div class="toggle-row"><span class="toggle-label">Body Circumference Tapes</span><button class="pz-switch ${flags.showMeasureRings ? 'on' : ''}" id="v-rings"></button></div>
                    <div class="toggle-row"><span class="toggle-label">3D Triangle Wireframe</span><button class="pz-switch ${flags.showWireframe ? 'on' : ''}" id="v-wire"></button></div>
                    <div class="toggle-row"><span class="toggle-label">3D M:N Sewing Threads</span><button class="pz-switch ${flags.showSeams3D ? 'on' : ''}" id="v-seam3"></button></div>
                    <div class="toggle-row"><span class="toggle-label">2D Seam Allowance Cut Line</span><button class="pz-switch ${flags.showSeamAllowance ? 'on' : ''}" id="v-sa2"></button></div>
                </div>
                <div class="pz-card">
                    <div class="pz-card-header">
                        <span class="pz-card-title">Production & CAD Export</span>
                        <span class="pz-pill-badge">1:1 mm</span>
                    </div>
                    <div class="pz-sub-caption">Export laser-cutter pattern sheets with seam allowances & grainlines, or draped 3D OBJ mesh with pattern UVs.</div>
                    <div class="pz-btn-row">
                        <button class="pz-action-btn accent" id="exp-svg">Pattern SVG</button>
                        <button class="pz-action-btn" id="exp-dxf">ASTM DXF</button>
                        <button class="pz-action-btn" id="exp-obj">3D OBJ</button>
                    </div>
                    <div class="pz-btn-row">
                        <button class="pz-action-btn" id="btn-clear-pins">Clear All Pins (${met.pinnedCount || 0})</button>
                    </div>
                </div>
            `;
        }

        refs.rightDock.innerHTML = `
            <div class="dock-tab-strip">
                ${['Solvers', 'Textile', 'Export'].map((t) =>
                    `<button class="trapezoid-tab ${rightTab === t ? 'active' : ''}" data-rtab="${t}">${t}</button>`
                ).join('')}
            </div>
            <div class="dock-scroll">${bodyHtml}</div>
        `;

        refs.rightDock.querySelectorAll('[data-rtab]').forEach((btn) =>
        {
            btn.onclick = () => { rightTab = btn.dataset.rtab; renderRightDock(app.getSnapshot()); };
        });
        refs.rightDock.querySelectorAll('[data-skey]').forEach((btn) =>
        {
            btn.onclick = () => app.selectSolver(btn.dataset.skey);
        });
        const swGpu = refs.rightDock.querySelector('#sw-webgpu');
        if (swGpu) swGpu.onclick = () => { app.useWebGpu = !app.useWebGpu; renderAll(app.getSnapshot()); };

        bindSlider(refs.rightDock, 's-substeps', '', 0, (v) => { if (app.solver) app.solver.substeps = v; });
        bindSlider(refs.rightDock, 's-iters', '', 0, (v) => { if (app.solver) app.solver.iterations = v; });
        bindSlider(refs.rightDock, 's-damp', '×', 2, (v) => { if (app.solver) app.solver.dampingScale = v; });
        bindSlider(refs.rightDock, 's-windspd', 'm/s', 1, (v) => { if (app.solver) app.solver.windSpeed = v; });
        bindSlider(refs.rightDock, 's-windang', '°', 0, (v) => { if (app.solver) app.solver.windAngleDeg = v; });

        const swSc = refs.rightDock.querySelector('#sw-selfcol');
        if (swSc) swSc.onclick = () => { if (app.solver) { app.solver.selfCollisionEnabled = !app.solver.selfCollisionEnabled; renderRightDock(app.getSnapshot()); } };
        const swWind = refs.rightDock.querySelector('#sw-wind');
        if (swWind) swWind.onclick = () => { if (app.solver) { app.solver.windEnabled = !app.solver.windEnabled; renderRightDock(app.getSnapshot()); } };

        refs.rightDock.querySelectorAll('.dye-dot').forEach((d) =>
        {
            d.onclick = () => app.setFabricColor(d.dataset.hex);
        });
        bindSlider(refs.rightDock, 't-warp', 'N/m', 0, (v) => { if (app.solver) app.solver.textile = { ...app.solver.textile, warpModulus: v }; });
        bindSlider(refs.rightDock, 't-weft', 'N/m', 0, (v) => { if (app.solver) app.solver.textile = { ...app.solver.textile, weftModulus: v }; });
        bindSlider(refs.rightDock, 't-shear', 'N/m', 0, (v) => { if (app.solver) app.solver.textile = { ...app.solver.textile, shearModulus: v }; });
        bindSlider(refs.rightDock, 't-bend', 'cN·cm', 2, (v) => { if (app.solver) app.solver.textile = { ...app.solver.textile, bendRigidity: v }; });
        bindSlider(refs.rightDock, 't-fric', '', 2, (v) => { if (app.solver) app.solver.textile = { ...app.solver.textile, frictionKinetic: v }; });

        const bindFlag = (id, fn) =>
        {
            const el = refs.rightDock.querySelector(`#${id}`);
            if (el) el.onclick = () => { fn(); renderRightDock(app.getSnapshot()); };
        };
        bindFlag('v-mann', () => { app.viewport3d.showMannequin = !app.viewport3d.showMannequin; app.viewport3d.refreshClothGeometry(); });
        bindFlag('v-rings', () => { app.viewport3d.showMeasureRings = !app.viewport3d.showMeasureRings; app.viewport3d.refreshClothGeometry(); });
        bindFlag('v-wire', () => { app.viewport3d.showWireframe = !app.viewport3d.showWireframe; app.viewport3d.refreshClothGeometry(); });
        bindFlag('v-seam3', () => { app.viewport3d.showSeams3D = !app.viewport3d.showSeams3D; app.viewport3d.refreshClothGeometry(); });
        bindFlag('v-sa2', () => { app.pattern2d.showSeamAllowance = !app.pattern2d.showSeamAllowance; app.pattern2d.markDirty(); });

        const expSvg = refs.rightDock.querySelector('#exp-svg');
        if (expSvg) expSvg.onclick = () => app.exportFile('svg');
        const expDxf = refs.rightDock.querySelector('#exp-dxf');
        if (expDxf) expDxf.onclick = () => app.exportFile('dxf');
        const expObj = refs.rightDock.querySelector('#exp-obj');
        if (expObj) expObj.onclick = () => app.exportFile('obj');
        const clrPins = refs.rightDock.querySelector('#btn-clear-pins');
        if (clrPins) clrPins.onclick = () => app.clearPins();
    }

    function renderToolbar(snap)
    {
        refs.toolbar.className = `viewport-toolbar ${drawerOpen ? 'drawer-open' : ''}`;
        refs.toolbar.innerHTML = `
            <span class="construct-pill">Construct</span>
            <button class="tool-seg-btn ${snap.activeTool === 'orbit' ? 'active' : ''}" data-tool="orbit">Orbit</button>
            <button class="tool-seg-btn ${snap.activeTool === 'grab' ? 'active' : ''}" data-tool="grab">Grab</button>
            <button class="tool-seg-btn ${snap.activeTool === 'pin' ? 'active' : ''}" data-tool="pin">Pin</button>
            <span class="toolbar-sep"></span>
            <select class="notch-select" id="tb-heatmap">
                <option value="textile" ${snap.heatmapMode === 'textile' ? 'selected' : ''}>Textile PBR</option>
                <option value="strain" ${snap.heatmapMode === 'strain' ? 'selected' : ''}>Warp/Weft Strain</option>
                <option value="shear" ${snap.heatmapMode === 'shear' ? 'selected' : ''}>Bias Shear °</option>
                <option value="pressure" ${snap.heatmapMode === 'pressure' ? 'selected' : ''}>Fit Pressure kPa</option>
                <option value="batches" ${snap.heatmapMode === 'batches' ? 'selected' : ''}>Color Batches</option>
            </select>
            <span class="toolbar-sep"></span>
            <button class="tool-seg-btn ${snap.viewSplitMode === '3d' ? 'active' : ''}" data-split="3d">3D</button>
            <button class="tool-seg-btn ${snap.viewSplitMode === 'split' ? 'active' : ''}" data-split="split">Split</button>
            <button class="tool-seg-btn ${snap.viewSplitMode === '2d' ? 'active' : ''}" data-split="2d">2D</button>
        `;

        refs.toolbar.querySelectorAll('[data-tool]').forEach((b) =>
        {
            b.onclick = () => app.setToolMode(b.dataset.tool);
        });
        refs.toolbar.querySelector('#tb-heatmap').onchange = (e) => app.setHeatmapMode(e.target.value);
        refs.toolbar.querySelectorAll('[data-split]').forEach((b) =>
        {
            b.onclick = () => app.setViewSplitMode(b.dataset.split);
        });

        refs.pane3d.classList.toggle('hidden', snap.viewSplitMode === '2d');
        refs.pane2d.classList.toggle('hidden', snap.viewSplitMode === '3d');
    }

    function renderDrawer(snap)
    {
        refs.drawer.className = `drawer-shell ${drawerOpen ? 'open' : ''}`;
        const cardsHtml = drawerCategory === 'garments'
            ? Object.values(GARMENT_PRESET_CATALOG).map((g) => `
                <button class="asset-card ${g.key === snap.garmentKey ? 'active' : ''}" data-gkey="${g.key}">
                    <div>
                        <div class="asset-card-top">
                            <span class="asset-title">${g.shortLabel || g.name}</span>
                            <span class="pz-pill-badge">${g.category}</span>
                        </div>
                        <div class="asset-desc" style="margin-top:5px;">${g.description}</div>
                    </div>
                    <div class="asset-meta">Default: ${TEXTILE_CATALOG[g.defaultTextile]?.name || g.defaultTextile}</div>
                </button>
            `).join('')
            : Object.values(TEXTILE_CATALOG).map((t) => `
                <button class="asset-card ${t.key === snap.textileKey ? 'active' : ''}" data-tkey="${t.key}">
                    <div>
                        <div class="asset-card-top">
                            <span class="asset-title">${t.name}</span>
                            <span class="asset-swatch" style="background:${t.colorHex}"></span>
                        </div>
                        <div class="asset-desc" style="margin-top:5px;">${t.category} (${t.code})</div>
                    </div>
                    <div class="asset-meta">${t.gsm} GSM · Warp ${t.warpModulus} · Bend ${t.bendRigidity}</div>
                </button>
            `).join('');

        refs.drawer.innerHTML = `
            <button class="drawer-pull-tab" id="drawer-toggle">
                <span>Asset Browser</span>
                <span style="color:var(--text-muted)">${drawerOpen ? '▾' : '▴'}</span>
            </button>
            <div class="drawer-body">
                <div class="drawer-head">
                    <div class="drawer-tabs">
                        <button class="pane-chip-btn ${drawerCategory === 'garments' ? 'active' : ''}" data-dcat="garments">6 Garment Presets</button>
                        <button class="pane-chip-btn ${drawerCategory === 'textiles' ? 'active' : ''}" data-dcat="textiles">6 Physical Textiles</button>
                    </div>
                    <span class="pz-sub-caption">Click any card to hot-swap patterns or textile weave parameters</span>
                </div>
                <div class="drawer-cards">${cardsHtml}</div>
            </div>
        `;

        refs.drawer.querySelector('#drawer-toggle').onclick = () =>
        {
            drawerOpen = !drawerOpen;
            const cur = app.getSnapshot();
            renderDrawer(cur);
            renderToolbar(cur);
            renderTimeline(cur);
        };
        refs.drawer.querySelectorAll('[data-dcat]').forEach((b) =>
        {
            b.onclick = () => { drawerCategory = b.dataset.dcat; renderDrawer(app.getSnapshot()); };
        });
        refs.drawer.querySelectorAll('[data-gkey]').forEach((b) =>
        {
            b.onclick = () => app.selectGarment(b.dataset.gkey);
        });
        refs.drawer.querySelectorAll('[data-tkey]').forEach((b) =>
        {
            b.onclick = () => app.selectTextile(b.dataset.tkey);
        });
    }

    function formatTimelineReadout(tl)
    {
        const curStr = String(tl.simFrame || 0).padStart(3, '0');
        const maxStr = String(tl.maxSimFrame || 0).padStart(3, '0');
        return `<strong>F${curStr}</strong> / ${maxStr} · ${(tl.timeSec || 0).toFixed(2)}s`;
    }

    function renderTimeline(snap)
    {
        if (!refs.timeline) return;
        const tl = snap.timeline || app.getTimelineState();
        refs.timeline.className = `timeline-bar ${drawerOpen ? 'drawer-open' : ''}`;
        const pct = tl.maxIndex > 0 ? Math.max(0, Math.min(100, (tl.cursor / tl.maxIndex) * 100)) : 0;
        const activeRunning = tl.isPlaying || tl.isPlayback;
        const recCls = tl.isPlayback ? 'playback' : (tl.isRecording ? 'recording' : '');
        const recLabel = tl.isPlayback ? 'PLAY' : (tl.isRecording ? 'REC' : 'HOLD');

        refs.timeline.innerHTML = `
            <div class="tl-transport">
                <button class="tl-btn" id="tl-first" title="Jump to frame 0">|&#9664;</button>
                <button class="tl-btn" id="tl-prev" title="Step back 1 frame">&#9664;|</button>
                <button class="tl-btn primary" id="tl-play" title="Play / Pause simulation or cache">${activeRunning ? '&#10074;&#10074;' : '&#9654;'}</button>
                <button class="tl-btn" id="tl-next" title="Step forward 1 frame">|&#9654;</button>
                <button class="tl-btn" id="tl-last" title="Jump to latest frame">&#9654;|</button>
            </div>
            <button class="tl-rec-pill ${recCls}" id="tl-rec" title="Toggle timeline frame recording">
                <span class="tl-rec-dot"></span>
                <span>${recLabel}</span>
            </button>
            <div class="tl-scrub-wrap">
                <input type="range" class="tl-scrubber" id="tl-scrubber" min="0" max="${tl.maxIndex}" step="1" value="${tl.cursor}" style="--fill:${pct.toFixed(1)}%">
            </div>
            <span class="tl-timecode" id="tl-timecode">${formatTimelineReadout(tl)}</span>
            <div class="tl-transport">
                <button class="tl-btn" id="tl-speed" title="Cycle playback speed">${tl.speed}x</button>
                <button class="tl-btn ${tl.loop ? 'active' : ''}" id="tl-loop" title="Toggle loop playback">Loop</button>
                <button class="tl-btn" id="tl-live" title="Resume live draping from current frame">Live</button>
                <button class="tl-btn" id="tl-trim" title="Trim timeline buffer to current frame">Trim</button>
            </div>
        `;

        refs.timeline.querySelector('#tl-first').onclick = () => app.seekTimeline(0);
        refs.timeline.querySelector('#tl-prev').onclick = () => app.stepTimeline(-1);
        refs.timeline.querySelector('#tl-play').onclick = () => app.toggleTimelinePlay();
        refs.timeline.querySelector('#tl-next').onclick = () => app.stepTimeline(+1);
        refs.timeline.querySelector('#tl-last').onclick = () => app.seekTimeline(app.timelineFrames.length - 1);
        refs.timeline.querySelector('#tl-rec').onclick = () => app.toggleRecording();
        refs.timeline.querySelector('#tl-speed').onclick = () => app.cyclePlaybackSpeed();
        refs.timeline.querySelector('#tl-loop').onclick = () => app.toggleTimelineLoop();
        refs.timeline.querySelector('#tl-live').onclick = () => app.playLive();
        refs.timeline.querySelector('#tl-trim').onclick = () => app.clearTimeline();

        const scrub = refs.timeline.querySelector('#tl-scrubber');
        scrub.addEventListener('input', () =>
        {
            const idx = parseInt(scrub.value, 10) || 0;
            app.seekTimeline(idx);
        });
    }

    function updateTimelineOnly(tl)
    {
        if (!refs.timeline || !tl) return;
        const scrub = refs.timeline.querySelector('#tl-scrubber');
        const tc = refs.timeline.querySelector('#tl-timecode');
        if (scrub)
        {
            scrub.max = String(tl.maxIndex);
            scrub.value = String(tl.cursor);
            const pct = tl.maxIndex > 0 ? Math.max(0, Math.min(100, (tl.cursor / tl.maxIndex) * 100)) : 0;
            scrub.style.setProperty('--fill', `${pct.toFixed(1)}%`);
        }
        if (tc)
            tc.innerHTML = formatTimelineReadout(tl);
    }

    function renderAll(snap)
    {
        renderNotch(snap);
        renderLeftDock(snap);
        renderRightDock(snap);
        renderToolbar(snap);
        renderTimeline(snap);
        renderDrawer(snap);
    }

    function updateMetricsOnly(met)
    {
        if (!met) return;
        const setTxt = (id, str) => { const el = refs.rightDock.querySelector(`#${id}`); if (el) el.textContent = str; };
        setTxt('metric-badge-fps', `${(met.fps || 60).toFixed(0)} FPS`);
        setTxt('m-step', `${(met.stepTimeMs || 0).toFixed(2)} ms`);
        setTxt('m-mesh', `${met.faceCount || 0} Δ`);
        setTxt('m-strain', `${(met.maxStrainPct || 0).toFixed(1)}%`);
        setTxt('m-pen', `${(met.mannequinPenetrationPct || 0).toFixed(2)}%`);
        setTxt('m-seam', `${(met.seamResidualMm || 0).toFixed(2)} mm`);
        setTxt('m-fold', `${(met.meanFoldAngleDeg || 0).toFixed(1)}°`);
    }

    return { renderAll, updateMetricsOnly, updateTimelineOnly };
}
