//============================================================================================================================================
//                                                          STUDIOPANELS.JS
//============================================================================================================================================
// 🧩 Project-Zero Fluid Studio UI:
//    - Hierarchical Folder Outliner (per-Simulation Folders + Global Folder, ancestor visibility, Filter chips, F2 rename, drag-to-folder)
//    - Project-Zero FolderInspector (COLLECTION / SCENE INVENTORY) + Lettered Pill-Tab Entity Inspector + Local/Global Scope Bar
//    - Content Directory Asset Browser with procedural 2D canvas previews and "+ Folder" multi-simulation composition

import { icon } from './StudioIcons.js';
import {
    renderPresetThumbnail,
    renderForceThumbnail,
    renderParticleThumbnail,
    renderSourceThumbnail,
    renderColliderThumbnail,
    renderShadingOrb,
} from './PreviewThumbnails.js';
import { FlipbookChoices } from '../Engine/FluidStudioApp.js';
import {
    ForceGeneratorTypes, ForceTypeById, FalloffCurves, EmitterShapes, ColourRamps, SliceAxes, ScalarChannels, AdvectionSchemes, BoundaryModes,
    RampStops, MaximumForceGenerators, SmokeLooks, FireLooks, ResolutionPresets, VolumeFilters, FalloffLabels, ForceCategories, VectorModes,
    nodeWeight, isKeyed, DebrisKinds, EmitterPathKinds, GridFollowModes, MaximumGridTiles, GridModes, MaximumTilePool,
} from '../Engine/ForceGeneratorCatalog.js';
import { ScenePresets } from '../Engine/PresetScenes.js';
import {
    LayerSpecificationByCode, GasLayerSpecifications, ParticleLayerSpecifications, BlendModes, PhaseOrder, MaximumLayers, SourceLayerCode,
    layerAmountAt,
} from '../Engine/LayerStack.js';
import { keyedNode, findTrack } from '../Engine/KeyTracks.js';
import { CustomObjectEntry, CustomObjectIds } from '../Engine/ObjectMeshCatalog.js';
import { mountTimeline } from './TimelinePanel.js';
import {
    ParticleTemplates, ParticleTemplateById, ParticleCategories, PalettePresets, MaximumPaletteStops, MaximumParticleSystems,
} from '../Engine/ParticleCatalog.js';
import {
    GlobalFolderId,
    CollectionTypeLabels,
    ensureSceneDirectory,
    buildSceneRows,
    computeFolderInventory,
    entityScopeInfo,
} from '../Engine/SceneFolders.js';

const BoundaryLabels = { ClosedBox: 'Closed Box', OpenTop: 'Open Top', OpenAll: 'Open All' };
const SpawnCategoryCode = 201;
const CarrierCategoryCode = 211;
const OutlinerFilterCategories = ['Global', 'Domains', 'Sources', 'Forces', 'Particles', 'Overlays'];

function element(tag, className, html)
{
    const created = document.createElement(tag);
    if (className) created.className = className;
    if (html !== undefined) created.innerHTML = html;
    return created;
}

function formatNumber(amount, digits = 2)
{
    return Number(amount).toFixed(digits).replace(/\.?0+$/, (match) => (match.startsWith('.') ? '' : match));
}

function formatCells(count)
{
    return count >= 1e6 ? (count / 1e6).toFixed(2) + ' M' : count >= 1e3 ? (count / 1e3).toFixed(0) + ' k' : String(count);
}

function rampGradient(name)
{
    return `linear-gradient(90deg, ${(RampStops[name] || RampStops.Turbo).join(', ')})`;
}

//------------------------------------------------------------------------------------------------------------------------
//                                                  STUDIO UI
//------------------------------------------------------------------------------------------------------------------------

export function mountStudioUI({ app, refs })
{
    const uiMemory = {
        searchTerm: '',
        outlinerFilters: [],
        collapsed: new Set(),
        expandedDefaults: new Set(),
        renamingId: null,
        folderInspectorFilter: { query: '', type: 'all', visibility: 'all' },
        drawerOpen: false,
        drawerTab: 'presets',
        drawerFilter: 'All',
        drawerSearch: '',
        notchOpen: false,
        mode: 'sim',
        snapshot: null,
        metrics: null,
        contextMenu: null,
        tabByKind: { Domain: 0, Force: 0, Emitter: 0, Particles: 0, Collider: 0, Visualizer: 0 },
        flipbook: {
            Count: 16, Size: 256, Rate: 30, Warmup: 1, Transparent: false, Restart: true,
            baking: false, cancelled: false, progress: 0,
            label: 'Renders the viewport camera into one sprite sheet.',
            result: null, frame: 0, play: true,
        },
    };

    function isFolderCollapsed(row)
    {
        if (uiMemory.collapsed.has(row.Id)) return true;
        if (row.DefaultCollapsed && !uiMemory.expandedDefaults.has(row.Id)) return true;
        return false;
    }

    function toggleFolderCollapse(row)
    {
        if (isFolderCollapsed(row))
        {
            uiMemory.collapsed.delete(row.Id);
            if (row.DefaultCollapsed) uiMemory.expandedDefaults.add(row.Id);
        }
        else
        {
            uiMemory.collapsed.add(row.Id);
            uiMemory.expandedDefaults.delete(row.Id);
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                          1. PROJECT-ZERO FOLDER OUTLINER
    //------------------------------------------------------------------------------------------------------------------------

    function rowMatchesFilter(row, filterLabel)
    {
        switch (filterLabel)
        {
            case 'Global': return row.Scope === 'Global';
            case 'Domains': return row.Panel === 'Domain';
            case 'Sources': return row.Panel === 'Emitter' || row.Panel === 'Collider';
            case 'Forces': return row.Panel === 'Force';
            case 'Particles': return row.Panel === 'Particles';
            case 'Overlays': return row.Panel === 'Visualizer';
            default: return true;
        }
    }

    function computeDisplayRows(hierarchy)
    {
        const { childrenMap } = hierarchy;
        const query = uiMemory.searchTerm.trim();
        const filters = uiMemory.outlinerFilters;
        const filtering = Boolean(query || filters.length);

        const descendantMatches = (row) =>
        {
            const selfText = `${row.Name} ${row.Metadata || ''} ${row.Panel}`.toLowerCase();
            const selfHit = (!query || selfText.includes(query))
                && (!filters.length || (!row.IsFolder && filters.some((label) => rowMatchesFilter(row, label))));
            if (selfHit && !row.IsFolder) return true;
            if (row.IsFolder && query && !filters.length && row.Name.toLowerCase().includes(query)) return true;
            for (const child of childrenMap.get(row.Id) || [])
            {
                if (descendantMatches(child)) return true;
            }
            return false;
        };

        const result = [];
        const stack = [...(childrenMap.get(null) || [])].reverse().map((row) => ({ row, depth: 0 }));
        const visited = new Set();

        while (stack.length)
        {
            const { row, depth } = stack.pop();
            if (visited.has(row.Id)) continue;
            visited.add(row.Id);

            if (filtering && !descendantMatches(row)) continue;
            result.push({ ...row, Depth: depth });

            if (!row.HasChildren) continue;
            if (!isFolderCollapsed(row) || filtering)
            {
                const kids = childrenMap.get(row.Id) || [];
                for (let idx = kids.length - 1; idx >= 0; idx -= 1)
                {
                    stack.push({ row: kids[idx], depth: depth + 1 });
                }
            }
        }
        return result;
    }

    function openAddSceneMenu(anchorEvent)
    {
        const rect = anchorEvent.currentTarget.getBoundingClientRect();
        openContextMenu(rect.left, rect.bottom + 6, [
            { label: 'Sphere Gas Source', icon: 'flame', action: () => app.addEmitter('Sphere') },
            { label: 'Disc Nozzle Source', icon: 'flame', action: () => app.addEmitter('Disc') },
            { label: 'Box Gas Source', icon: 'smoke', action: () => app.addEmitter('Box') },
            { label: 'Sphere Collider', icon: 'collider', action: () => app.addCollider() },
            { separator: true },
            { label: 'Add Force Generator…', icon: 'wind', action: () => toggleDrawer(true, 'forces') },
            { label: 'Add Particle Cloud…', icon: 'particles', action: () => toggleDrawer(true, 'particles') },
            { separator: true },
            { label: 'New Collection Folder', icon: 'folder', action: () => app.createFolder(null, 'Collection') },
            { label: 'Import Simulation Preset as Folder…', icon: 'scene', action: () => toggleDrawer(true, 'presets') },
        ]);
    }

    function openFilterMenu(anchorEvent, snapshot)
    {
        const rect = anchorEvent.currentTarget.getBoundingClientRect();
        const entries = OutlinerFilterCategories.map((label) => ({
            label: uiMemory.outlinerFilters.includes(label) ? `✓ ${label}` : label,
            icon: label === 'Global' ? 'globe' : 'filter',
            action: () =>
            {
                if (uiMemory.outlinerFilters.includes(label))
                {
                    uiMemory.outlinerFilters = uiMemory.outlinerFilters.filter((item) => item !== label);
                }
                else
                {
                    uiMemory.outlinerFilters = [...uiMemory.outlinerFilters, label];
                }
                renderOutliner(snapshot);
            },
        }));
        if (uiMemory.outlinerFilters.length)
        {
            entries.push({ separator: true });
            entries.push({
                label: 'Clear filters',
                icon: 'reset',
                action: () => { uiMemory.outlinerFilters = []; renderOutliner(snapshot); },
            });
        }
        openContextMenu(rect.left, rect.bottom + 6, entries);
    }

    function renderOutliner(snapshot)
    {
        const { scene } = snapshot;
        const host = refs.outliner;
        host.innerHTML = '';

        const dir = ensureSceneDirectory(scene);
        const hierarchy = buildSceneRows(scene);
        const leafRows = hierarchy.rows.filter((row) => !row.IsFolder);
        const visibleCount = leafRows.filter((row) => row.Visible && !row.InheritedHidden && row.Enabled).length;
        const hiddenCount = leafRows.length - visibleCount;
        const simCount = dir.Simulations.length;
        const globalCount = leafRows.filter((row) => row.Scope === 'Global').length;

        // 1. Outliner Heading
        const subtitleText = simCount > 1 ? `${simCount} simulations · ${leafRows.length} items` : `${scene.Name} · ${leafRows.length} items`;
        const heading = element('div', 'outliner-heading', `<h1>Outliner</h1><small>${subtitleText}</small>`);
        const addButton = element('button', 'add-scene-btn', `${icon('plus', 11)} Add`);
        addButton.title = 'Add scene object, collection folder, or simulation folder';
        addButton.addEventListener('click', (event) => { event.stopPropagation(); openAddSceneMenu(event); });
        heading.appendChild(addButton);
        host.appendChild(heading);

        // 2. Project-Zero Visible / Hidden Stats Cards
        const stats = element('div', 'stats');
        const visBtn = element('button', '', `<span class="status-disc">${icon('check', 12, 2.5)}</span><small>Visible</small><strong>${visibleCount}</strong>`);
        visBtn.title = 'Show and enable all muted folders in the scene';
        visBtn.addEventListener('click', () =>
        {
            for (const folderId of Object.keys(dir.Hidden))
            {
                if (dir.Hidden[folderId]) app.toggleFolderVisibility(folderId);
            }
        });
        const hidBtn = element('button', '', `<span class="muted-disc">△</span><small>Hidden</small><strong>${hiddenCount}</strong>`);
        hidBtn.title = 'Count of muted or hidden scene items';
        stats.append(visBtn, hidBtn);
        host.appendChild(stats);

        // 3. Search + Filter Bar
        const searchBar = element('div', 'outliner-search');
        const searchPill = element('div', 'search-pill', icon('search', 13));
        const searchInput = document.createElement('input');
        searchInput.placeholder = 'Search   Ctrl+F';
        searchInput.value = uiMemory.searchTerm;
        searchInput.addEventListener('input', () =>
        {
            uiMemory.searchTerm = searchInput.value.toLowerCase();
            populateOutlinerRows(rowsHost, snapshot);
        });
        searchPill.appendChild(searchInput);
        if (uiMemory.searchTerm)
        {
            const clearBtn = element('button', 'search-clear', '×');
            clearBtn.title = 'Clear search';
            clearBtn.addEventListener('click', () =>
            {
                uiMemory.searchTerm = '';
                searchInput.value = '';
                populateOutlinerRows(rowsHost, snapshot);
            });
            searchPill.appendChild(clearBtn);
        }
        const filterBtn = element(
            'button',
            'outliner-filter-btn' + (uiMemory.outlinerFilters.length ? ' active' : ''),
            `${icon('filter', 12)} Filter ${icon('chevron', 10)}`
        );
        filterBtn.addEventListener('click', (event) => { event.stopPropagation(); openFilterMenu(event, snapshot); });
        searchBar.append(searchPill, filterBtn);
        host.appendChild(searchBar);

        if (uiMemory.outlinerFilters.length)
        {
            const chipRow = element('div', 'filter-chips');
            for (const label of uiMemory.outlinerFilters)
            {
                const chip = element('button', '', `${label} ×`);
                chip.addEventListener('click', () =>
                {
                    uiMemory.outlinerFilters = uiMemory.outlinerFilters.filter((item) => item !== label);
                    renderOutliner(snapshot);
                });
                chipRow.appendChild(chip);
            }
            host.appendChild(chipRow);
        }

        // 4. Hierarchical Folder & Entity Tree
        const rowsHost = element('div', 'outliner-rows');
        rowsHost.setAttribute('role', 'tree');
        host.appendChild(rowsHost);
        populateOutlinerRows(rowsHost, snapshot, hierarchy);

        // 5. Project-Zero 5-Column Telemetry Footer
        const fpsVal = uiMemory.metrics ? `${uiMemory.metrics.fps.toFixed(0)} <em>fps</em>` : `— <em>fps</em>`;
        const gridVal = scene.Domain.Grid === 'Infinite' ? 'Infinite' : scene.Domain.Resolution.join('×');
        host.appendChild(element(
            'footer',
            'outliner-footer',
            `<span><small>REALTIME</small><b>${fpsVal}</b></span>
             <span><small>GRID</small><b>${gridVal}</b></span>
             <span><small>SIMS</small><b>${simCount}</b></span>
             <span><small>GLOBAL</small><b>${globalCount}</b></span>
             <span><small>FORCES</small><b>${scene.Forces.length}</b></span>`
        ));
    }

    function populateOutlinerRows(rowsHost, snapshot, prebuilt = null)
    {
        rowsHost.innerHTML = '';
        const hierarchy = prebuilt || buildSceneRows(snapshot.scene);
        const visibleRows = computeDisplayRows(hierarchy);

        if (visibleRows.length === 0)
        {
            rowsHost.appendChild(element('p', 'empty-results', 'No matching scene objects'));
            return;
        }

        let draggedEntityId = null;
        for (const row of visibleRows)
        {
            rowsHost.appendChild(buildOutlinerRowElement(row, snapshot, {
                getDragged: () => draggedEntityId,
                setDragged: (val) => { draggedEntityId = val; },
            }));
        }
    }

    function canDeleteOutlinerRow(row, dir)
    {
        if (!row) return false;
        if (!row.IsFolder) return row.Panel !== 'Domain' && row.Panel !== 'Visualizer';
        if (row.Custom) return true;
        if (row.IsSimulation && dir.Simulations.length > 1) return true;
        if (typeof row.Id === 'string')
        {
            return row.Id.startsWith('folder-particles')
                || row.Id.startsWith('folder-forces')
                || row.Id.startsWith('folder-sources')
                || row.Id === 'folder-global::particles'
                || row.Id === 'folder-global::forces'
                || row.Id === 'folder-global::sources';
        }
        return false;
    }

    function openRowContextMenu(event, row, snapshot)
    {
        event.preventDefault();
        const dir = ensureSceneDirectory(snapshot.scene);
        app.select(row.Id);
        const menuItems = [];

        if (row.IsFolder)
        {
            if (row.IsSimulation && dir.Simulations.length > 1 && !row.IsActiveSimulation)
            {
                menuItems.push({ label: `Activate ${row.Name}`, icon: 'play', action: () => app.activateSimulation(row.Id) });
            }
            if (row.IsSimulation || row.Custom)
            {
                menuItems.push({
                    label: 'Rename   F2',
                    icon: 'pencil',
                    action: () => { uiMemory.renamingId = row.Id; renderOutliner(snapshot); },
                });
            }
            const parentForNew = row.IsSimulation ? row.Id : row.Scope === 'Global' ? GlobalFolderId : (row.SimulationId || dir.ActiveSimulationId);
            menuItems.push({
                label: 'New Collection Folder',
                icon: 'folder',
                action: () => app.createFolder(parentForNew, 'Collection'),
            });
            menuItems.push({
                label: row.Visible ? 'Hide Folder' : 'Show Folder',
                icon: row.Visible ? 'eyeOff' : 'eye',
                action: () => app.toggleFolderVisibility(row.Id),
            });
            if (canDeleteOutlinerRow(row, dir))
            {
                menuItems.push({ separator: true });
                menuItems.push({
                    label: row.Custom || row.IsSimulation ? 'Delete Folder' : `Delete All in ${row.Name}`,
                    icon: 'trash',
                    danger: true,
                    action: () => app.deleteFolder(row.Id),
                });
            }
        }
        else
        {
            const entityId = row.UnderlyingId || row.Id;
            const entry = row.Entry;
            if (entry && entry.Kind !== 'Domain' && entry.Kind !== 'Visualizer')
            {
                menuItems.push({
                    label: 'Rename   F2',
                    icon: 'pencil',
                    action: () => { uiMemory.renamingId = row.Id; renderOutliner(snapshot); },
                });
            }

            // Scope & Simulation Folder relocation
            if (row.Scope === 'Global')
            {
                for (const sim of dir.Simulations)
                {
                    menuItems.push({
                        label: `Move to ${sim.Name} (Local)`,
                        icon: 'folder',
                        action: () => app.setEntityScope(entityId, 'Local', sim.Id),
                    });
                }
            }
            else
            {
                menuItems.push({
                    label: 'Promote to Global Folder',
                    icon: 'globe',
                    action: () => app.setEntityScope(entityId, 'Global'),
                });
                for (const sim of dir.Simulations)
                {
                    if (sim.Id !== row.SimulationId)
                    {
                        menuItems.push({
                            label: `Move to ${sim.Name}`,
                            icon: 'folder',
                            action: () => app.setEntityScope(entityId, 'Local', sim.Id),
                        });
                    }
                }
            }

            if (entry && entry.Kind !== 'Domain' && entry.Kind !== 'Visualizer')
            {
                menuItems.push({
                    label: 'Move into new collection',
                    icon: 'folder',
                    action: () =>
                    {
                        const folder = app.createFolder(row.SimulationId || dir.ActiveSimulationId, 'Collection');
                        if (folder) app.moveToFolder(entityId, folder.Id);
                    },
                });
                menuItems.push({ separator: true });
                menuItems.push({ label: 'Duplicate', icon: 'copy', action: () => app.duplicateNode(entityId) });
                menuItems.push({
                    label: row.Enabled ? 'Disable' : 'Enable',
                    icon: 'power',
                    action: () => app.updateNode(entityId, 'Enabled', !row.Enabled),
                });
            }
            if (entry && entry.Kind !== 'Domain')
            {
                menuItems.push({
                    label: row.Visible ? 'Hide in viewport' : 'Show in viewport',
                    icon: row.Visible ? 'eyeOff' : 'eye',
                    action: () => app.updateNode(entityId, 'Visible', !row.Visible),
                });
            }
            if (entry && entry.Kind !== 'Domain' && entry.Kind !== 'Visualizer')
            {
                menuItems.push({ separator: true });
                menuItems.push({ label: 'Delete', icon: 'trash', danger: true, action: () => app.removeNode(entityId) });
            }
        }

        if (menuItems.length) openContextMenu(event.clientX, event.clientY, menuItems);
    }

    function buildOutlinerRowElement(row, snapshot, dragTracker)
    {
        const isSelected = snapshot.selection === row.Id || (!row.IsFolder && snapshot.selection === row.UnderlyingId && (!row.Id.includes('::sim-')));
        const isHidden = !row.Visible || row.InheritedHidden || (!row.IsFolder && !row.Enabled);
        const collapsed = row.HasChildren && isFolderCollapsed(row);

        const el = element(
            'div',
            'outliner-row'
                + (isSelected ? ' selected' : '')
                + (isHidden ? ' hidden-row' : '')
        );
        el.setAttribute('role', 'treeitem');
        el.setAttribute('data-outliner-row', row.Id);
        el.tabIndex = 0;
        el.style.setProperty('--depth', String(Math.min(4, row.Depth || 0)));

        // 1. Disclosure chevron
        const disclosure = element(
            'button',
            'row-disclosure' + (!row.HasChildren ? ' empty' : '') + (collapsed ? ' collapsed' : ''),
            icon('chevron', 11)
        );
        disclosure.title = collapsed ? `Expand ${row.Name}` : `Collapse ${row.Name}`;
        disclosure.addEventListener('click', (event) =>
        {
            event.stopPropagation();
            toggleFolderCollapse(row);
            renderOutliner(snapshot);
        });
        el.appendChild(disclosure);

        // 2. Project-Zero Folder SVG (24px) or Entity Glyph (15px)
        const iconSpan = element(
            'span',
            'row-icon' + (row.IsFolder ? ' folder-glyph' : ''),
            row.IsFolder ? icon(row.Icon, 24) : icon(row.Icon, 15)
        );
        if (!row.IsFolder) iconSpan.style.color = row.Tint;
        el.appendChild(iconSpan);

        // 3. Row Identity (Name + Contextual Metadata, or inline F2 Rename)
        if (uiMemory.renamingId === row.Id)
        {
            const input = document.createElement('input');
            input.className = 'inline-rename';
            input.value = row.Name;
            const commit = () =>
            {
                const next = input.value.trim();
                uiMemory.renamingId = null;
                if (next && next !== row.Name)
                {
                    if (row.IsFolder) app.renameFolder(row.Id, next);
                    else app.updateNode(row.UnderlyingId || row.Id, 'Name', next);
                }
                else
                {
                    renderOutliner(snapshot);
                }
            };
            input.addEventListener('click', (event) => event.stopPropagation());
            input.addEventListener('keydown', (event) =>
            {
                event.stopPropagation();
                if (event.key === 'Enter') commit();
                else if (event.key === 'Escape') { uiMemory.renamingId = null; renderOutliner(snapshot); }
            });
            input.addEventListener('blur', commit);
            el.appendChild(input);
            setTimeout(() => input.focus(), 0);
        }
        else
        {
            el.appendChild(element(
                'div',
                'row-identity',
                `<span class="row-name">${row.Name}</span><small title="${row.Metadata || ''}">${row.Metadata || ''}</small>`
            ));
        }

        // Show GLOBAL badge when filtering/searching flattens context
        if (!row.IsFolder && row.Scope === 'Global' && (uiMemory.searchTerm || uiMemory.outlinerFilters.length))
        {
            el.appendChild(element('span', 'row-scope-tag', 'Global'));
        }

        // 4. Status disc (non-folder rows)
        if (!row.IsFolder && row.Panel !== 'Domain')
        {
            const entityId = row.UnderlyingId || row.Id;
            if (row.Panel !== 'Visualizer')
            {
                const status = element('button', 'row-status' + (row.Enabled ? '' : ' off'), icon(row.Enabled ? 'check' : 'power', 9, 2.8));
                status.title = row.Enabled ? 'Enabled — click to disable' : 'Disabled — click to enable';
                status.addEventListener('click', (event) =>
                {
                    event.stopPropagation();
                    app.updateNode(entityId, 'Enabled', !row.Enabled);
                });
                el.appendChild(status);
            }
        }

        // 5. Visibility eye (cascades on folders, toggles Visible on entities)
        if (row.Panel !== 'Domain')
        {
            const eye = element('button', 'visibility' + (row.Visible ? '' : ' off'), icon(row.Visible ? 'eye' : 'eyeOff', 13));
            eye.title = row.IsFolder
                ? (row.Visible ? `Hide ${row.Name} folder` : `Show ${row.Name} folder`)
                : (row.Visible ? 'Visible — click to hide' : 'Hidden — click to show');
            eye.addEventListener('click', (event) =>
            {
                event.stopPropagation();
                if (row.IsFolder) app.toggleFolderVisibility(row.Id);
                else app.updateNode(row.UnderlyingId || row.Id, 'Visible', !row.Visible);
            });
            el.appendChild(eye);
        }

        // 6. Direct inline Delete button for deletable entities and folders
        const dir = ensureSceneDirectory(snapshot.scene);
        if (canDeleteOutlinerRow(row, dir))
        {
            const del = element('button', 'row-delete', icon('trash', 12));
            del.title = row.IsFolder ? `Delete ${row.Name}` : `Delete ${row.Name} (Delete)`;
            del.addEventListener('click', (event) =>
            {
                event.stopPropagation();
                if (row.IsFolder) app.deleteFolder(row.Id);
                else app.removeNode(row.UnderlyingId || row.Id);
            });
            el.appendChild(del);
        }

        // Selection, double-click collapse, keyboard navigation & drag-and-drop reparenting
        el.addEventListener('click', () => app.select(row.Id));
        el.addEventListener('dblclick', () =>
        {
            if (row.HasChildren)
            {
                toggleFolderCollapse(row);
                renderOutliner(snapshot);
            }
        });
        el.addEventListener('contextmenu', (event) => openRowContextMenu(event, row, snapshot));
        el.addEventListener('keydown', (event) =>
        {
            if (event.key === 'F2' && (row.IsSimulation || row.Custom || (!row.IsFolder && row.Panel !== 'Domain' && row.Panel !== 'Visualizer')))
            {
                event.preventDefault();
                uiMemory.renamingId = row.Id;
                renderOutliner(snapshot);
            }
            else if (event.key === 'ArrowLeft' && row.HasChildren && !collapsed)
            {
                toggleFolderCollapse(row);
                renderOutliner(snapshot);
            }
            else if (event.key === 'ArrowRight' && row.HasChildren && collapsed)
            {
                toggleFolderCollapse(row);
                renderOutliner(snapshot);
            }
        });

        if (!row.IsFolder)
        {
            el.draggable = true;
            el.addEventListener('dragstart', (event) =>
            {
                dragTracker.setDragged(row.UnderlyingId || row.Id);
                if (event.dataTransfer) event.dataTransfer.effectAllowed = 'move';
            });
            el.addEventListener('dragend', () => dragTracker.setDragged(null));
        }
        else
        {
            el.addEventListener('dragover', (event) =>
            {
                if (!dragTracker.getDragged()) return;
                event.preventDefault();
                el.classList.add('drop-folder');
            });
            el.addEventListener('dragleave', () => el.classList.remove('drop-folder'));
            el.addEventListener('drop', (event) =>
            {
                event.preventDefault();
                el.classList.remove('drop-folder');
                const entityId = dragTracker.getDragged();
                dragTracker.setDragged(null);
                if (entityId) app.moveToFolder(entityId, row.Id);
            });
        }

        return el;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                          2. PROJECT-ZERO INSPECTOR CONTROLS
    //------------------------------------------------------------------------------------------------------------------------

    function slider(entry, key, label, min, max, step, unit = '', digits = 2, write = (amount) => app.updateNode(entry.Id, key, amount))
    {
        const control = element('div', 'control');
        const head = element('div', 'control-head', `<span>${label}</span>`);
        control.appendChild(head);

        const row = element('div', 'slider-row');
        const pill = element('div', 'value-pill');
        const number = document.createElement('input');
        number.type = 'number';
        number.step = String(step);
        number.value = formatNumber(entry[key], digits);
        pill.appendChild(number);
        if (unit) pill.appendChild(element('em', '', unit));

        const range = document.createElement('input');
        range.type = 'range';
        range.min = String(min);
        range.max = String(max);
        range.step = String(step);
        range.value = String(entry[key]);
        const paint = () =>
        {
            const span = Math.max(1e-6, max - min);
            const pct = Math.max(0, Math.min(100, ((Number(range.value) - min) / span) * 100));
            range.style.setProperty('--fill', `${pct}%`);
        };
        paint();
        range.addEventListener('input', () =>
        {
            number.value = formatNumber(range.value, digits);
            paint();
            write(Number(range.value));
        });
        number.addEventListener('change', () =>
        {
            const parsed = Number(number.value);
            if (Number.isFinite(parsed))
            {
                range.value = String(Math.max(min, Math.min(max, parsed)));
                paint();
                write(parsed);
            }
        });
        row.append(pill, range);
        control.appendChild(row);
        return attachKey(control, entry.Kind ? app.keyTargetForField(entry, key) : null);
    }

    function attachKey(control, target)
    {
        if (!target || !control) return control;
        const keyState = app.keyState(target);
        if (keyState === null) return control;
        const button = element('span', `key-toggle ${keyState}`, keyState === 'key' ? '◆' : '◇');
        button.setAttribute('role', 'button');
        button.tabIndex = 0;
        button.addEventListener('keydown', (event) =>
        {
            if (event.key === 'Enter' || event.key === ' ')
            {
                event.preventDefault();
                event.stopPropagation();
                app.toggleKey(target);
            }
        });
        button.title = {
            key: 'Key at the playhead — click to remove it',
            track: 'Keyed — click to add a key at the playhead',
            timing: 'Weight follows the timing fields — click to turn it into keys',
            none: 'Not keyed — click to key the current amount at the playhead',
        }[keyState];
        button.setAttribute('aria-label', `${keyState === 'key' ? 'Remove key' : 'Add key'} for ${target.Name}`);
        button.setAttribute('data-key-target', `${target.Kind}:${target.Token || target.Id}:${target.Name}`);
        button.addEventListener('click', (event) =>
        {
            event.stopPropagation();
            event.preventDefault();
            app.toggleKey(target);
        });
        const head = control.querySelector('.control-head > span') || control.querySelector('.tile-label') || control.firstElementChild || control;
        head.prepend(button);
        control.classList.add('keyable');
        if (keyState === 'key' || keyState === 'track') control.classList.add('keyed');
        return control;
    }

    function vector(entry, key, label, step = 0.05, integer = false, write = (amount) => app.updateNode(entry.Id, key, amount))
    {
        const control = element('div', 'control');
        control.appendChild(element('div', 'control-head', `<span>${label}</span>`));
        const row = element('div', 'vector-row');
        ['x', 'y', 'z'].forEach((axis, index) =>
        {
            const wrap = element('label', axis, `<span>${axis.toUpperCase()}</span>`);
            const input = document.createElement('input');
            input.type = 'number';
            input.step = String(step);
            input.value = integer ? String(entry[key][index]) : formatNumber(entry[key][index], 2);
            input.addEventListener('change', () =>
            {
                const parsed = Number(input.value);
                if (!Number.isFinite(parsed)) return;
                const next = [...entry[key]];
                next[index] = integer ? Math.round(parsed) : parsed;
                write(next);
            });
            wrap.appendChild(input);
            row.appendChild(wrap);
        });
        control.appendChild(row);
        return attachKey(control, entry.Kind ? app.keyTargetForField(entry, key) : null);
    }

    /** Project Zero Editor Transform XYZ Matrix Card. */
    function transformTableCard(entry, liveEntry, rowsSpec, spaceLabel = 'WORLD Y-UP')
    {
        const section = element('div', 'property-card transform-card');
        section.appendChild(element('header', '', `<h3>Transform</h3><span>${spaceLabel}</span>`));

        const table = element('table', 'transform-table');
        table.appendChild(element('thead', '', `<tr><th>Component</th><th class="axis-X">X</th><th class="axis-Y">Y</th><th class="axis-Z">Z</th></tr>`));
        const tbody = element('tbody');

        for (const spec of rowsSpec)
        {
            const tr = element('tr');
            const th = element('th', '', `${spec.label}<small>${spec.unit}</small>`);
            tr.appendChild(th);
            attachKey(th, entry.Kind ? app.keyTargetForField(entry, spec.key) : null);

            const currentVec = liveEntry[spec.key] || entry[spec.key] || spec.reset;
            [0, 1, 2].forEach((axisIndex) =>
            {
                const td = element('td');
                const input = document.createElement('input');
                input.type = 'number';
                input.step = String(spec.step || 0.05);
                input.value = spec.integer ? String(currentVec[axisIndex]) : formatNumber(currentVec[axisIndex], 2);

                const applyAxis = (nextScalar) =>
                {
                    if (!Number.isFinite(nextScalar)) return;
                    const baseVec = [...(liveEntry[spec.key] || entry[spec.key] || spec.reset)];
                    baseVec[axisIndex] = spec.integer ? Math.round(nextScalar) : Number(nextScalar.toFixed(3));
                    app.updateNode(entry.Id, spec.key, baseVec);
                };

                input.addEventListener('change', () => applyAxis(Number(input.value)));

                let dragStart = null;
                input.addEventListener('pointerdown', (event) =>
                {
                    if (event.button !== 0 || document.activeElement === input) return;
                    dragStart = { x: event.clientX, startVal: Number(input.value) || 0, moved: false };
                    if (input.setPointerCapture) input.setPointerCapture(event.pointerId);
                });
                input.addEventListener('pointermove', (event) =>
                {
                    if (!dragStart) return;
                    const dx = event.clientX - dragStart.x;
                    if (Math.abs(dx) > 4) dragStart.moved = true;
                    if (dragStart.moved)
                    {
                        const nextVal = dragStart.startVal + dx * (spec.step || 0.05) * (event.shiftKey ? 0.1 : 0.5);
                        input.value = spec.integer ? String(Math.round(nextVal)) : formatNumber(nextVal, 2);
                        applyAxis(nextVal);
                    }
                });
                input.addEventListener('pointerup', () => { dragStart = null; });
                input.addEventListener('pointercancel', () => { dragStart = null; });

                td.appendChild(input);
                tr.appendChild(td);
            });
            tbody.appendChild(tr);
        }
        table.appendChild(tbody);
        section.appendChild(table);

        const resets = element('div', 'transform-resets');
        for (const spec of rowsSpec)
        {
            const btn = element('button', '', `↺ ${spec.label}`);
            btn.addEventListener('click', () => app.updateNode(entry.Id, spec.key, [...spec.reset]));
            resets.appendChild(btn);
        }
        section.appendChild(resets);
        section.appendChild(element('p', 'note', 'Type or drag horizontally on X / Y / Z cells · hold Shift for fine precision · or drag the gizmo in the viewport.'));
        return section;
    }

    function choice(entry, key, label, options, labels = {}, write = (amount) => app.updateNode(entry.Id, key, amount))
    {
        const control = element('div', 'control');
        control.appendChild(element('div', 'control-head', `<span>${label}</span>`));
        // Long catalogues (3D object meshes) read better as a single select than as 19 pill buttons.
        if (options.length > 8)
        {
            const select = document.createElement('select');
            select.className = 'layer-add';
            select.setAttribute('aria-label', label);
            for (const option of options)
            {
                const item = document.createElement('option');
                item.value = String(option);
                item.textContent = labels[option] || String(option);
                select.appendChild(item);
            }
            select.value = String(entry[key]);
            select.addEventListener('change', () => write(Number(select.value)));
            control.appendChild(select);
            return attachKey(control, entry.Kind ? app.keyTargetForField(entry, key) : null);
        }
        const row = element('div', 'choice-row');
        for (const option of options)
        {
            const button = element('button', entry[key] === option ? 'active' : '', labels[option] || option);
            button.addEventListener('click', () => write(option));
            row.appendChild(button);
        }
        control.appendChild(row);
        return attachKey(control, entry.Kind ? app.keyTargetForField(entry, key) : null);
    }

    function colour(entry, key, label, write = (amount) => app.updateNode(entry.Id, key, amount))
    {
        const row = element('div', 'colour-row', `<span>${label}</span>`);
        const input = document.createElement('input');
        input.type = 'color';
        input.value = entry[key];
        input.addEventListener('input', () => write(input.value));
        row.appendChild(input);
        return attachKey(row, entry.Kind ? app.keyTargetForField(entry, key) : null);
    }

    function card(title, hint, controls, eyebrow = '')
    {
        const wrap = element('div', 'property-card');
        if (eyebrow) wrap.appendChild(element('div', 'card-eyebrow', eyebrow));
        wrap.appendChild(element('h3', '', `${title}${hint ? `<span>${hint}</span>` : ''}`));
        for (const control of controls) if (control) wrap.appendChild(control);
        return wrap;
    }

    function statPair(leftLabel, leftVal, leftDot, rightLabel, rightVal, rightDot)
    {
        return element(
            'div',
            'pz-stat-pair',
            `<div class="pz-stat-box"><div class="stat-label"><span class="stat-dot ${leftDot || ''}"></span><span>${leftLabel}</span></div><div class="stat-num">${leftVal}</div></div>
             <div class="pz-stat-box"><div class="stat-label"><span class="stat-dot ${rightDot || 'amber'}"></span><span>${rightLabel}</span></div><div class="stat-num">${rightVal}</div></div>`
        );
    }

    function statusTile(label, stateText, iconName, on, onClick, warn = false)
    {
        const tile = element(
            'button',
            'status-tile' + (on ? ' on' : '') + (warn ? ' warn' : ''),
            `<span class="tile-icon">${icon(iconName, 14)}</span><span class="tile-copy"><span class="tile-label">${label}</span><span class="tile-state">${stateText}</span></span>`
        );
        if (onClick) tile.addEventListener('click', onClick);
        else tile.disabled = true;
        return tile;
    }

    function timingCard(entry)
    {
        const time = app.solver ? app.solver.time : 0;
        const envelope = nodeWeight(entry, time);
        const explicit = findTrack(entry.Tracks, 'Weight');
        const stateText = !isKeyed(entry) ? 'ALWAYS ON' : envelope > 0 ? `LIVE ${Math.round(envelope * 100)}%` : time < (entry.StartTime || 0) ? 'WAITING' : 'DONE';
        const track = element('div', 'key-track');
        const loop = app.scene.Domain.SceneLoop > 0 ? app.scene.Domain.SceneLoop : Math.max(6, (entry.StartTime || 0) + (entry.Duration || 2) + 1);
        const left = Math.min(100, ((entry.StartTime || 0) / loop) * 100);
        const width = entry.Duration > 0 ? Math.min(100 - left, (entry.Duration / loop) * 100) : 100 - left;
        track.innerHTML = `<span class="key-span" style="left:${left}%;width:${width}%"></span><span class="key-head" style="left:${Math.min(100, (time % loop) / loop * 100)}%"></span>`;

        const tile = statusTile('Envelope Gate', stateText, 'clock', isKeyed(entry), () => app.updateNode(entry.Id, 'Duration', isKeyed(entry) ? 0 : 1.0));
        const weight = attachKey(
            element('div', 'control', `<div class="control-head"><span>Evaluated Weight</span><b>${Math.round(envelope * 100)}%</b></div>`),
            { Kind: 'Node', Id: entry.Id, Name: 'Weight' }
        );

        if (explicit)
        {
            return card('Timeline Weight', `${explicit.Keys.length} key${explicit.Keys.length === 1 ? '' : 's'}`, [
                tile,
                weight,
                element('p', 'note', 'Weight is keyed directly in the timeline below the viewport. Editing Start or Duration below replaces explicit keys with a parametric envelope.'),
                slider(entry, 'StartTime', 'Start time', 0, 20, 0.05, 's'),
                slider(entry, 'Duration', 'Duration (0 = hold)', 0, 20, 0.05, 's'),
            ], 'TIMING / ENVELOPE');
        }
        return card('Timing Envelope', `${formatNumber(time, 1)} / ${formatNumber(loop, 1)} s`, [
            tile,
            weight,
            track,
            slider(entry, 'StartTime', 'Start time', 0, 20, 0.05, 's'),
            slider(entry, 'Duration', 'Duration (0 = hold)', 0, 20, 0.05, 's'),
            slider(entry, 'Fade', 'Fade in / out', 0.01, 2, 0.01, 's'),
            slider(entry, 'Repeat', 'Repeat period', 0, 20, 0.1, 's'),
        ], 'TIMING / ENVELOPE');
    }

    function describeEntrySubtitle(entry)
    {
        switch (entry.Kind)
        {
            case 'Domain':
                return entry.Grid === 'Infinite'
                    ? `Infinite sparse tiles · ${entry.TileSize} m tiles · ${(entry.CellSize * 100).toFixed(1)} cm voxels`
                    : `${entry.Resolution.join(' × ')} cells · ${entry.Size.map((extent) => extent.toFixed(1)).join(' × ')} m · ${BoundaryLabels[entry.Boundary]}`;
            case 'Force':
                return `${ForceTypeById[entry.Type].Name} · ${FalloffLabels[entry.Falloff]} falloff`;
            case 'Emitter':
                return `${entry.Shape} source · radius ${formatNumber(entry.Radius, 2)} m${entry.Fuel > 0 ? ' · combusting fuel' : ' · smoke & heat'}`;
            case 'Collider':
                return `No-slip solid sphere boundary · radius ${formatNumber(entry.Radius, 2)} m`;
            case 'Particles':
            {
                const template = ParticleTemplateById[entry.Template] || ParticleTemplates[0];
                return `${template.Name} · ${entry.Mode} · ${entry.Count.toLocaleString()} particles`;
            }
            default:
                return 'Viewport diagnostic & shading overlay';
        }
    }

    function openContextMenu(x, y, entries)
    {
        closeContextMenu();
        const menu = element('div', 'context-menu');
        menu.addEventListener('pointerdown', (event) => event.stopPropagation());
        for (const option of entries)
        {
            if (option.separator)
            {
                menu.appendChild(element('div', 'context-sep'));
                continue;
            }
            const row = element('button', 'context-item' + (option.danger ? ' danger' : ''), `${icon(option.icon, 13)}<span>${option.label}</span>`);
            row.addEventListener('click', (event) => { event.stopPropagation(); closeContextMenu(); option.action(); });
            menu.appendChild(row);
        }
        document.body.appendChild(menu);
        const rect = menu.getBoundingClientRect();
        const winW = window.innerWidth || 1280;
        const winH = window.innerHeight || 720;
        menu.style.left = `${Math.max(6, Math.min(x, winW - rect.width - 6))}px`;
        menu.style.top = `${Math.max(6, Math.min(y, winH - rect.height - 6))}px`;
        uiMemory.contextMenu = menu;
        const onPointerDown = (event) =>
        {
            if (uiMemory.contextMenu && uiMemory.contextMenu.contains(event.target)) return;
            closeContextMenu();
        };
        const onScroll = () => closeContextMenu();
        const onKeyDown = (event) => { if (event.key === 'Escape') closeContextMenu(); };
        uiMemory.cleanupContextMenu = () =>
        {
            document.removeEventListener('pointerdown', onPointerDown);
            window.removeEventListener('scroll', onScroll, true);
            window.removeEventListener('keydown', onKeyDown);
        };
        setTimeout(() =>
        {
            if (uiMemory.contextMenu !== menu) return;
            document.addEventListener('pointerdown', onPointerDown);
            window.addEventListener('scroll', onScroll, true);
            window.addEventListener('keydown', onKeyDown);
        }, 0);
    }

    function closeContextMenu()
    {
        if (uiMemory.cleanupContextMenu)
        {
            uiMemory.cleanupContextMenu();
            uiMemory.cleanupContextMenu = null;
        }
        if (uiMemory.contextMenu)
        {
            uiMemory.contextMenu.remove();
            uiMemory.contextMenu = null;
        }
    }

    /** Project Zero Lettered Pill Tab Strip (< [ (A) ... (B) ... ] > 1 / N) */
    function renderInspectorPages(host, kindKey, pages, snapshot)
    {
        const count = pages.length;
        let activeIndex = uiMemory.tabByKind[kindKey] || 0;
        if (activeIndex >= count || activeIndex < 0) activeIndex = 0;
        uiMemory.tabByKind[kindKey] = activeIndex;

        const bar = element('div', 'pz-tab-bar');
        const prevBtn = element('button', 'pz-tab-step', '‹');
        prevBtn.title = 'Previous inspector page';
        prevBtn.disabled = count <= 1;
        prevBtn.addEventListener('click', () =>
        {
            uiMemory.tabByKind[kindKey] = (activeIndex - 1 + count) % count;
            renderInspector(snapshot);
        });

        const track = element('div', 'pz-tab-track');
        const letters = 'ABCDEFGH';
        pages.forEach((page, index) =>
        {
            const pill = element(
                'button',
                'pz-tab-pill' + (index === activeIndex ? ' active' : ''),
                `<b>${letters[index] || index + 1}</b><span>${page.label}</span>`
            );
            pill.addEventListener('click', () =>
            {
                uiMemory.tabByKind[kindKey] = index;
                renderInspector(snapshot);
            });
            track.appendChild(pill);
        });

        const nextBtn = element('button', 'pz-tab-step', '›');
        nextBtn.title = 'Next inspector page';
        nextBtn.disabled = count <= 1;
        nextBtn.addEventListener('click', () =>
        {
            uiMemory.tabByKind[kindKey] = (activeIndex + 1) % count;
            renderInspector(snapshot);
        });

        const counter = element('span', 'pz-tab-count', `${activeIndex + 1} / ${count}`);
        bar.append(prevBtn, track, nextBtn, counter);
        host.appendChild(bar);

        pages[activeIndex].build(host);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                  PROJECT-ZERO FOLDER INSPECTOR (COLLECTION INVENTORY)
    //------------------------------------------------------------------------------------------------------------------------

    function renderFolderInspector(host, folderRow, snapshot)
    {
        const inventory = computeFolderInventory(snapshot.scene, folderRow.Id);
        if (!inventory) return;
        const dir = ensureSceneDirectory(snapshot.scene);
        const filterState = uiMemory.folderInspectorFilter;

        // 1. Heading + Trail
        const head = element('header', 'collection-heading');
        head.appendChild(element('span', 'workspace-eyebrow', 'COLLECTION / SCENE INVENTORY'));
        const titleLine = element('div', 'title-line', icon(folderRow.Icon, 28));
        if (folderRow.IsSimulation || folderRow.Custom)
        {
            const renameIn = document.createElement('input');
            renameIn.className = 'rename';
            renameIn.value = folderRow.Name;
            renameIn.title = 'Click to rename folder';
            renameIn.addEventListener('change', () => app.renameFolder(folderRow.Id, renameIn.value.trim() || folderRow.Name));
            titleLine.appendChild(renameIn);
        }
        else
        {
            titleLine.appendChild(element('h1', '', folderRow.Name));
        }
        head.appendChild(titleLine);
        head.appendChild(element('p', '', `${folderRow.Description || 'Scene collection'} · indexed from the current scene`));

        const trail = element('nav', 'collection-trail');
        for (const crumb of inventory.Trail)
        {
            const btn = element('button', crumb.Id === folderRow.Id ? 'current' : '', crumb.Name);
            btn.addEventListener('click', () => app.select(crumb.Id));
            trail.appendChild(btn);
        }
        head.appendChild(trail);
        host.appendChild(head);

        // 2. Collection Contents Total Card
        const totalItems = inventory.LeafEntries.length;
        const totalCard = element(
            'section',
            'collection-card',
            `<h2><i></i>Collection contents</h2>
             <div class="workspace-number">${totalItems}<small>${totalItems === 1 ? 'entry' : 'entries'}</small></div>
             <div class="collection-facts">
                 <div><b>${inventory.Direct}</b><span>Direct children</span></div>
                 <div><b>${inventory.Folders}</b><span>Nested folders</span></div>
                 <div><b>${inventory.Depth}</b><span>Levels below</span></div>
             </div>`
        );
        host.appendChild(totalCard);

        // 3. Visible / Hidden Filter Status Pair
        const statusRow = element('div', 'collection-status');
        const visCard = element(
            'button',
            filterState.visibility === 'visible' ? 'selected' : '',
            `<span class="collection-dot"></span><span>Visible</span><strong>${inventory.Visible}</strong>`
        );
        visCard.addEventListener('click', () =>
        {
            filterState.visibility = filterState.visibility === 'visible' ? 'all' : 'visible';
            renderInspector(snapshot);
        });
        const hidCard = element(
            'button',
            filterState.visibility === 'hidden' ? 'selected' : '',
            `<span class="collection-dot muted"></span><span>Hidden</span><strong>${totalItems - inventory.Visible}</strong>`
        );
        hidCard.addEventListener('click', () =>
        {
            filterState.visibility = filterState.visibility === 'hidden' ? 'all' : 'hidden';
            renderInspector(snapshot);
        });
        statusRow.append(visCard, hidCard);
        host.appendChild(statusRow);

        // 4. Composition Card
        const compCard = element('section', 'collection-card');
        compCard.appendChild(element('h2', '', `<i class="warm"></i>Composition <small>${inventory.Types.length} types</small>`));
        if (inventory.Types.length > 0)
        {
            const bar = element('div', 'collection-stack');
            const palette = ['#b9c7ae', '#9cabb9', '#d1b898', '#a6a198', '#879c93', '#c89b7b'];
            inventory.Types.forEach(([typeKey, count], idx) =>
            {
                const seg = document.createElement('span');
                seg.style.flex = String(count);
                seg.style.background = palette[idx % palette.length];
                seg.title = `${CollectionTypeLabels[typeKey] || typeKey}: ${count}`;
                bar.appendChild(seg);
            });
            compCard.appendChild(bar);

            const typeList = element('div', 'collection-type-list');
            for (const [typeKey, count] of inventory.Types)
            {
                const typeBtn = element(
                    'button',
                    filterState.type === typeKey ? 'active' : '',
                    `<span>${CollectionTypeLabels[typeKey] || typeKey}</span><b>${count}</b>`
                );
                typeBtn.addEventListener('click', () =>
                {
                    filterState.type = filterState.type === typeKey ? 'all' : typeKey;
                    renderInspector(snapshot);
                });
                typeList.appendChild(typeBtn);
            }
            compCard.appendChild(typeList);
        }
        else
        {
            compCard.appendChild(element('p', 'note', 'This folder currently holds no matching items.'));
        }
        host.appendChild(compCard);

        // 5. Child Inventory Browser + Quick Scope Toggles
        const browserCard = element('section', 'collection-card');
        browserCard.appendChild(element('h2', '', `<i></i>Folder items <small>click to inspect · flip Local/Global</small>`));
        const listHost = element('div', 'collection-entries');
        const filteredLeaves = inventory.LeafEntries.filter(({ Row, Visible }) =>
            (filterState.type === 'all' || Row.Panel === filterState.type)
            && (filterState.visibility === 'all' || Visible === (filterState.visibility === 'visible'))
        );

        for (const { Row } of filteredLeaves)
        {
            const itemRow = element('div', 'collection-entry');
            const glyph = element('span', 'row-icon', icon(Row.Icon, 14));
            glyph.style.color = Row.Tint;
            const meta = element('div', 'meta', `<strong>${Row.Name}</strong><small>${Row.Metadata || ''}</small>`);
            const entityId = Row.UnderlyingId || Row.Id;
            const isGlobal = Row.Scope === 'Global';
            const scopeBtn = element('button', 'scope-toggle' + (isGlobal ? ' global' : ''), isGlobal ? 'Global' : 'Local');
            scopeBtn.title = isGlobal ? 'Move into active Simulation Folder (Local)' : 'Promote to Global Folder (shared across simulations)';
            scopeBtn.addEventListener('click', (event) =>
            {
                event.stopPropagation();
                app.setEntityScope(entityId, isGlobal ? 'Local' : 'Global', dir.ActiveSimulationId);
            });
            itemRow.append(glyph, meta, scopeBtn);
            if (Row.Panel !== 'Domain' && Row.Panel !== 'Visualizer')
            {
                const itemDel = element('button', 'row-delete', icon('trash', 12));
                itemDel.title = `Delete ${Row.Name}`;
                itemDel.addEventListener('click', (event) => { event.stopPropagation(); app.removeNode(entityId); });
                itemRow.appendChild(itemDel);
            }
            itemRow.addEventListener('click', () => app.select(Row.Id));
            listHost.appendChild(itemRow);
        }
        browserCard.appendChild(listHost);

        // Folder-level actions
        const actions = element('div', 'action-row');
        if (folderRow.IsSimulation && dir.Simulations.length > 1 && !folderRow.IsActiveSimulation)
        {
            const activateBtn = element('button', '', `${icon('play', 12)} Activate simulation`);
            activateBtn.addEventListener('click', () => app.activateSimulation(folderRow.Id));
            actions.appendChild(activateBtn);
        }
        const addSub = element('button', '', `${icon('plus', 11)} Sub-folder`);
        addSub.addEventListener('click', () => app.createFolder(folderRow.IsSimulation ? folderRow.Id : dir.ActiveSimulationId, 'Collection'));
        actions.appendChild(addSub);
        if (canDeleteOutlinerRow(folderRow, dir))
        {
            const delLabel = folderRow.Custom || folderRow.IsSimulation ? 'Delete folder' : 'Delete all items';
            const delBtn = element('button', 'danger', `${icon('trash', 12)} ${delLabel}`);
            delBtn.addEventListener('click', () => app.deleteFolder(folderRow.Id));
            actions.appendChild(delBtn);
        }
        browserCard.appendChild(actions);
        host.appendChild(browserCard);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  INSPECTOR DISPATCH
    //------------------------------------------------------------------------------------------------------------------------

    function buildScopeBar(entry, snapshot)
    {
        const dir = ensureSceneDirectory(snapshot.scene);
        const entityId = entry.Kind === 'Domain' ? 'domain' : entry.Id;
        const info = entityScopeInfo(snapshot.scene, entityId);
        const isGlobal = info.Scope === 'Global';

        const bar = element('div', 'pz-scope-bar');
        const folderLabel = isGlobal ? 'Global Folder' : info.SimulationName;
        bar.appendChild(element('small', '', `${icon(isGlobal ? 'folderWorld' : 'folderScene', 15)} <span>${folderLabel}</span>`));

        if (!isGlobal && dir.Simulations.length > 1 && entry.Kind !== 'Domain' && entry.Kind !== 'Visualizer')
        {
            const simSelect = document.createElement('select');
            simSelect.className = 'pz-scope-sim';
            simSelect.title = 'Move to another Simulation Folder';
            for (const sim of dir.Simulations)
            {
                const opt = document.createElement('option');
                opt.value = sim.Id;
                opt.textContent = sim.Name;
                opt.selected = sim.Id === info.SimulationId;
                simSelect.appendChild(opt);
            }
            simSelect.addEventListener('change', () => app.setEntityScope(entityId, 'Local', simSelect.value));
            bar.appendChild(simSelect);
        }

        const seg = element('div', 'pz-scope-seg');
        const localBtn = element('button', !isGlobal ? 'active' : '', 'Local');
        localBtn.title = `Keep private inside ${info.SimulationName}'s folder`;
        localBtn.addEventListener('click', () => { if (isGlobal) app.setEntityScope(entityId, 'Local', dir.ActiveSimulationId); });

        const globalBtn = element('button', isGlobal ? 'active global' : '', 'Global');
        globalBtn.title = 'Move to Global folder (shared across all simulation folders in the scene)';
        globalBtn.addEventListener('click', () => { if (!isGlobal) app.setEntityScope(entityId, 'Global'); });

        seg.append(localBtn, globalBtn);
        bar.appendChild(seg);

        if (entry.Kind !== 'Domain' && entry.Kind !== 'Visualizer')
        {
            const dupBtn = element('button', 'pz-scope-action', icon('copy', 12));
            dupBtn.title = `Duplicate ${entry.Name}`;
            dupBtn.addEventListener('click', () => app.duplicateNode(entry.Id));
            const delBtn = element('button', 'pz-scope-action danger', icon('trash', 12));
            delBtn.title = `Delete ${entry.Name} (Delete)`;
            delBtn.addEventListener('click', () => app.removeNode(entry.Id));
            bar.append(dupBtn, delBtn);
        }
        return bar;
    }

    function renderInspector(snapshot)
    {
        const host = refs.inspector;
        host.innerHTML = '';

        // Check if current selection is a Folder in the Outliner hierarchy
        if (snapshot.selection && !snapshot.node)
        {
            const { byId } = buildSceneRows(snapshot.scene);
            const selectedFolder = byId.get(snapshot.selection);
            if (selectedFolder && selectedFolder.IsFolder)
            {
                renderFolderInspector(host, selectedFolder, snapshot);
                return;
            }
        }

        const entry = snapshot.node || snapshot.scene.Domain;

        // 1. Clean Project-Zero Entity Header + Subtitle + Folder Scope Bar (children[0])
        const header = element('div', 'pz-entity-header');
        if (entry.Kind === 'Domain' || entry.Kind === 'Visualizer')
        {
            const info = entityScopeInfo(snapshot.scene, entry.Kind === 'Domain' ? 'domain' : entry.Id);
            const ownerPrefix = info.Scope === 'Global' ? 'Global' : info.SimulationName;
            header.appendChild(element('h1', '', entry.Kind === 'Domain' ? `${ownerPrefix} · Gas Domain` : entry.Name));
        }
        else
        {
            const rename = document.createElement('input');
            rename.className = 'rename';
            rename.value = entry.Name;
            rename.title = 'Click to rename';
            rename.addEventListener('change', () => app.updateNode(entry.Id, 'Name', rename.value.trim() || entry.Name));
            header.appendChild(rename);
        }
        header.appendChild(element('div', 'subtitle', describeEntrySubtitle(entry)));
        header.appendChild(buildScopeBar(entry, snapshot));
        host.appendChild(header);

        // 2. Paged Lettered Pill Tabs (children[1] = .pz-tab-bar) + Active Page Cards
        if (entry.Kind === 'Domain') renderDomainInspector(host, entry, snapshot);
        else if (entry.Kind === 'Force') renderForceInspector(host, entry, snapshot);
        else if (entry.Kind === 'Emitter') renderEmitterInspector(host, entry, snapshot);
        else if (entry.Kind === 'Collider') renderColliderInspector(host, entry, snapshot);
        else if (entry.Kind === 'Particles') renderParticleInspector(host, entry, snapshot);
        else renderVisualizerInspector(host, entry, snapshot);
    }

    function buildDomainSchematicSvg(domain)
    {
        const [sx, sy, sz] = domain.Size || [3, 5, 3];
        const maxDim = Math.max(sx, sy, sz, 0.1);
        const bw = Math.round((sx / maxDim) * 76);
        const bh = Math.round((sy / maxDim) * 68);
        const dx = Math.round((sz / maxDim) * 26);
        const dy = Math.round((sz / maxDim) * 14);
        const x0 = 130 - Math.round((bw + dx) * 0.5);
        const y0 = 26 + dy;
        const openTop = domain.Boundary !== 'ClosedBox';
        const openSides = domain.Boundary === 'OpenAll';
        const topStroke = openTop ? '#38bdf8' : '#8a8f9c';
        const topDash = openTop ? 'stroke-dasharray="4 3"' : '';
        const sideDash = openSides ? 'stroke-dasharray="4 3"' : '';

        return `<svg viewBox="0 0 260 122" xmlns="http://www.w3.org/2000/svg">
            <rect width="260" height="122" rx="10" fill="#101115"/>
            <line x1="22" y1="104" x2="238" y2="104" stroke="#ffffff12" stroke-width="1"/>
            <rect x="${x0 + dx}" y="${y0 - dy}" width="${bw}" height="${bh}" fill="#ffffff04" stroke="#ffffff22" ${sideDash}/>
            <polygon points="${x0},${y0} ${x0 + dx},${y0 - dy} ${x0 + dx + bw},${y0 - dy} ${x0 + bw},${y0}" fill="${openTop ? '#38bdf814' : '#ffffff08'}" stroke="${topStroke}" ${topDash}/>
            <rect x="${x0}" y="${y0}" width="${bw}" height="${bh}" fill="#ffffff05" stroke="#b8bec9" ${sideDash}/>
            <line x1="${x0}" y1="${y0 + bh}" x2="${x0 + dx}" y2="${y0 + bh - dy}" stroke="#ffffff30"/>
            <line x1="${x0 + bw}" y1="${y0 + bh}" x2="${x0 + dx + bw}" y2="${y0 + bh - dy}" stroke="#ffffff30"/>
            <text x="14" y="20" fill="#8c919e" font-size="9" font-family="DM Sans, sans-serif">${domain.Resolution.join(' × ')} voxels</text>
            <text x="246" y="20" text-anchor="end" fill="${openTop ? '#38bdf8' : '#9ca3af'}" font-size="9" font-family="DM Sans, sans-serif">${BoundaryLabels[domain.Boundary]}</text>
            <text x="130" y="115" text-anchor="middle" fill="#777b86" font-size="9" font-family="DM Sans, sans-serif">${sx.toFixed(1)} × ${sy.toFixed(1)} × ${sz.toFixed(1)} m</text>
        </svg>`;
    }

    function renderDomainInspector(host, domain, snapshot)
    {
        const metrics = uiMemory.metrics || {};
        const stack = snapshot.scene.GasStack;
        const activeLayers = stack ? stack.Layers.filter((layer) => layer.Enabled).length : 0;
        const trackCount = stack && stack.Tracks ? stack.Tracks.length : 0;
        const visualizer = snapshot.scene.Visualizers.Volume;

        renderInspectorPages(host, 'Domain', [
            {
                label: 'Stack',
                build: (target) =>
                {
                    target.appendChild(statPair('Active layers', activeLayers, '', 'Keyed tracks', trackCount, 'amber'));
                    renderGasStack(target, snapshot);
                    const actions = element('div', 'action-row');
                    const reset = element('button', '', `${icon('reset', 12)} Reset fields`);
                    reset.addEventListener('click', () => app.reset());
                    const frame = element('button', '', `${icon('frame', 12)} Frame domain`);
                    frame.addEventListener('click', () => app.frameDomain());
                    actions.append(reset, frame);
                    target.appendChild(actions);
                },
            },
            {
                label: 'Grid',
                build: (target) =>
                {
                    const voxelCm = (domain.CellSize * 100).toFixed(1);
                    const schematic = element('div', 'pz-schematic-box');
                    schematic.innerHTML = buildDomainSchematicSvg(domain);
                    schematic.appendChild(element(
                        'div',
                        'pz-schematic-caption',
                        `<span>Domain schematic</span><span>${formatCells(domain.Resolution[0] * domain.Resolution[1] * domain.Resolution[2])} cells</span>`
                    ));

                    const resolutionChips = element('div', 'control');
                    resolutionChips.appendChild(element('div', 'control-head', '<span>Resolution preset</span>'));
                    const chipRow = element('div', 'choice-row');
                    for (const [name, cells] of Object.entries(ResolutionPresets))
                    {
                        const button = element('button', domain.MaxCells === cells ? 'active' : '', `${name} <span style="opacity:.55;margin-left:3px">${cells}</span>`);
                        button.addEventListener('click', () => app.updateNode(domain.Id, 'MaxCells', cells));
                        chipRow.appendChild(button);
                    }
                    resolutionChips.appendChild(chipRow);

                    target.appendChild(card('Voxel Domain', `${domain.Resolution.join(' × ')}`, [
                        element('div', 'pz-figure-head', `<strong>${voxelCm}</strong><span>cm voxel</span>`),
                        schematic,
                        slider(domain, 'MaxCells', 'Longest-axis cells', 16, 160, 8, 'cells', 0),
                        resolutionChips,
                        choice(domain, 'Boundary', 'Boundary walls', BoundaryModes, BoundaryLabels),
                    ], 'GAS VOLUME / DOMAIN'));

                    target.appendChild(transformTableCard(domain, domain, [
                        { key: 'Size', label: 'Bounds', unit: 'm', step: 0.25, reset: [3, 5, 3] },
                    ], 'DOMAIN EXTENTS'));

                    const infinite = domain.Grid === 'Infinite';
                    const gridMode = choice(domain, 'Grid', 'Allocation mode', GridModes, { Fixed: 'Fixed / Window', Infinite: 'Infinite sparse tiles' });
                    if (infinite)
                    {
                        const resident = metrics.residentTiles || 0;
                        const pool = metrics.poolSize || domain.TilePool;
                        target.appendChild(card('Infinite Sparse Grid', `${resident} / ${pool} tiles`, [
                            gridMode,
                            slider(domain, 'TileSize', 'Tile edge', 0.5, 8, 0.25, 'm'),
                            slider(domain, 'TileResolution', 'Cells per tile', 8, 48, 4, 'cells', 0),
                            slider(domain, 'TilePool', 'Tile pool budget', 4, MaximumTilePool, 4, 'tiles', 0),
                        ], 'SPARSE LATTICE'));
                    }
                    else
                    {
                        const tiles = domain.Tiles || [1, 1, 1];
                        target.appendChild(card('World Tile Window', `${tiles.join(' × ')} tiles`, [
                            gridMode,
                            vector(domain, 'Tiles', `Tiles per axis (max ${MaximumGridTiles})`, 1, true),
                            choice(domain, 'Follow', 'Follow target', GridFollowModes, { None: 'Static', Emitter: 'Source', Particles: 'Particles', Camera: 'Camera' }),
                        ], 'WORLD WINDOW'));
                    }
                },
            },
            {
                label: 'Shading',
                build: (target) =>
                {
                    const orbBox = element('div', 'pz-schematic-box');
                    orbBox.appendChild(renderShadingOrb({
                        smokeHex: visualizer.Albedo,
                        flameCentre: visualizer.FireCore,
                        fireFlame: visualizer.FireFlame,
                        fireEmber: visualizer.FireEmber,
                        fireIntensity: visualizer.FireIntensity,
                        mode: 'combined',
                    }, 120));
                    orbBox.appendChild(element('div', 'pz-schematic-caption', `<span>Volumetric plume study</span><span>${visualizer.Steps} ray steps</span>`));
                    target.appendChild(card('Plume Shader Preview', 'Live look', [orbBox], 'SHADING / VOLUME'));

                    const fireRail = element('div', 'pz-catalogue-rail');
                    for (const [name, look] of Object.entries(FireLooks))
                    {
                        const active = visualizer.FireEmber === look.FireEmber;
                        const item = element('button', 'pz-catalogue-item' + (active ? ' active' : ''));
                        item.appendChild(renderShadingOrb({ flameCentre: look.FireCore, fireFlame: look.FireFlame, fireEmber: look.FireEmber, mode: 'fire' }, 72));
                        item.appendChild(element('b', '', name));
                        item.addEventListener('click', () => app.applyLook('vis-volume', look));
                        fireRail.appendChild(item);
                    }
                    target.appendChild(card('Fire Catalogue & Glow', 'Incandescence', [
                        fireRail,
                        colour(visualizer, 'FireEmber', 'Ember edge'),
                        colour(visualizer, 'FireFlame', 'Flame body'),
                        colour(visualizer, 'FireCore', 'Hot core'),
                        slider(visualizer, 'FireIntensity', 'Fire intensity', 0, 12, 0.1, '×'),
                        slider(visualizer, 'FireThreshold', 'Ignition temp', 0.3, 3, 0.05, 'K'),
                        slider(visualizer, 'FireSoftness', 'Temp span', 0.5, 8, 0.1),
                    ], 'SHADING / FIRE'));

                    const smokeRail = element('div', 'pz-catalogue-rail');
                    for (const [name, look] of Object.entries(SmokeLooks))
                    {
                        const active = visualizer.Albedo === look.Albedo;
                        const item = element('button', 'pz-catalogue-item' + (active ? ' active' : ''));
                        item.appendChild(renderShadingOrb({ smokeHex: look.Albedo, mode: 'smoke' }, 72));
                        item.appendChild(element('b', '', name));
                        item.addEventListener('click', () => app.applyLook('vis-volume', look));
                        smokeRail.appendChild(item);
                    }
                    target.appendChild(card('Smoke & Illumination', 'Scattering', [
                        smokeRail,
                        colour(visualizer, 'Albedo', 'Smoke albedo'),
                        slider(visualizer, 'Density', 'Density scale', 0, 4, 0.05, '×'),
                        slider(visualizer, 'Absorption', 'Extinction', 0.2, 12, 0.1, '/m'),
                        slider(visualizer, 'LightIntensity', 'Sun intensity', 0, 4, 0.05, '×'),
                        slider(visualizer, 'Exposure', 'Exposure', 0.2, 4, 0.05, 'EV'),
                    ], 'SHADING / SMOKE'));
                },
            },
            {
                label: 'Solver',
                build: (target) =>
                {
                    target.appendChild(statPair(
                        'Solver step', `${(metrics.stepMilliseconds || 0).toFixed(1)}<small>ms</small>`, '',
                        'GPU kernels', String(metrics.passes || 0), 'blue'
                    ));
                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile(snapshot.playing ? 'Simulating' : 'Paused', snapshot.playing ? 'LIVE' : 'HELD', snapshot.playing ? 'pause' : 'play', snapshot.playing, () => app.togglePlay()));
                    status.appendChild(statusTile('Advection', domain.Advection === 'MacCormack' ? 'MACCORMACK' : 'SEMI-LAGRANGIAN', 'vectors', domain.Advection === 'MacCormack', () => app.updateNode(domain.Id, 'Advection', domain.Advection === 'MacCormack' ? AdvectionSchemes[0] : AdvectionSchemes[1])));
                    target.appendChild(status);

                    target.appendChild(card('Integration & Loop', 'Solver clock', [
                        choice(domain, 'Advection', 'Advection scheme', AdvectionSchemes),
                        slider(domain, 'SceneLoop', 'Auto-restart loop (0 = off)', 0, 30, 0.5, 's', 1),
                        choice(visualizer, 'Filter', 'Volume sampling', VolumeFilters, { Smooth: 'Trilinear smooth', Voxel: 'Crisp voxels' }),
                        slider(visualizer, 'Steps', 'Raymarch steps', 24, 256, 4, 'steps', 0),
                        slider(visualizer, 'ShadowSteps', 'Self-shadow steps', 0, 16, 1, 'steps', 0),
                    ], 'SOLVER / QUALITY'));
                },
            },
        ], snapshot);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  LAYER STACK
    //------------------------------------------------------------------------------------------------------------------------

    function layerSummary(layer, specification, scene)
    {
        if (!specification) return `Category ${layer.Category} · kept, not evaluated`;
        if (specification.Code === SourceLayerCode)
        {
            const emitter = scene.Emitters.find((candidate) => candidate.Id === layer.Parameters.Emitter);
            return emitter ? `${emitter.Name} · ${emitter.Shape.toLowerCase()} ${formatNumber(emitter.Radius, 2)} m` : 'source missing';
        }
        const lead = specification.Parameter[0].Type === 'Choice' ? specification.Parameter[0] : null;
        const option = lead && lead.Options.find((candidate) => candidate.Code === layer.Parameters[lead.Name]);
        const numbers = specification.Parameter
            .filter((parameter) => parameter.Type === 'Float32' || parameter.Type === 'Int32')
            .slice(0, lead ? 1 : 2)
            .map((parameter) => `${parameter.Label.replace(/ \(.*\)$/, '').toLowerCase()} ${formatNumber(layer.Parameters[parameter.Name], parameter.Digits ?? 2)}`);
        return [option ? option.Label.toLowerCase() : null, ...numbers].filter(Boolean).join(' · ');
    }

    function renderGasStack(host, snapshot)
    {
        const stack = snapshot.scene.GasStack;
        if (!stack) return;
        renderLayerStack(host, snapshot, {
            stack,
            evaluation: snapshot.gas || { Problems: [], Contributing: new Set() },
            specifications: GasLayerSpecifications,
            stopped: (reason) => `⚠️ Gas stopped: ${reason}. Particles keep running.`,
            note: 'Drag ≡ to reorder · click ◉ to mute · select any row to edit its parameters below.',
        });
    }

    function renderLayerStack(host, snapshot, { stack, evaluation, specifications, stopped, note, owner = null })
    {
        const problemsFor = (token) => evaluation.Problems.filter((problem) => problem.Token === token);
        const stackCard = element('div', 'property-card layer-stack');
        stackCard.appendChild(element('div', 'card-eyebrow', `LAYER STACK · ${stack.Layers.length} / ${MaximumLayers}`));
        const headH3 = element('h3', '', `<span>Layers</span>`);
        stackCard.appendChild(headH3);
        stackCard.setAttribute('data-layer-stack', stack.Token);
        if (evaluation.Stopped) stackCard.appendChild(element('p', 'layer-stopped', stopped(evaluation.Stopped)));

        const add = document.createElement('select');
        add.className = 'layer-add';
        add.setAttribute('aria-label', 'Add layer');
        add.innerHTML = '<option value="">+ Add layer</option>';
        for (const phase of PhaseOrder)
        {
            const group = document.createElement('optgroup');
            group.label = phase;
            for (const specification of specifications.filter((candidate) => candidate.Phase === phase && !candidate.Required))
            {
                const used = stack.Layers.filter((layer) => layer.Category === specification.Code).length;
                const option = document.createElement('option');
                option.value = String(specification.Code);
                option.textContent = `${specification.Name}${specification.Capacity > 1 ? `  (${used}/${specification.Capacity})` : ''}`;
                option.disabled = used >= specification.Capacity || stack.Layers.length >= MaximumLayers;
                group.appendChild(option);
            }
            if (group.children.length) add.appendChild(group);
        }
        add.addEventListener('change', () => { if (add.value) app.addLayer(Number(add.value)); });
        headH3.appendChild(add);

        const list = element('ol', 'layer-list');
        let dragged = null;
        stack.Layers.forEach((layer, index) =>
        {
            const specification = LayerSpecificationByCode[layer.Category];
            const required = Boolean(specification && specification.Required);
            const issues = problemsFor(layer.Token);
            const live = evaluation.Contributing.has(layer.Token);
            const row = element(
                'li',
                'layer-row'
                    + (snapshot.selectedLayer === layer.Token ? ' selected' : '')
                    + (live ? '' : ' muted')
                    + (issues.some((issue) => issue.Severity !== 'Info') ? ' problem' : '')
                    + (specification ? '' : ' inert')
            );
            row.setAttribute('data-layer', layer.Token);
            row.draggable = !required;

            const handle = element('span', 'layer-handle', required ? '' : '≡');
            handle.title = required ? 'Required first layer' : 'Drag to reorder';
            const toggle = element('button', 'layer-toggle' + (layer.Enabled ? ' on' : ''), layer.Enabled ? '◉' : '○');
            toggle.title = required ? 'Required — always on' : layer.Enabled ? 'Disable layer' : 'Enable layer';
            toggle.setAttribute('aria-label', `${layer.Enabled ? 'Disable' : 'Enable'} ${specification ? specification.Name : 'layer'}`);
            toggle.disabled = required || !specification;
            toggle.addEventListener('click', (event) => { event.stopPropagation(); app.updateLayer(layer.Token, 'Enabled', !layer.Enabled); });

            const text = element('span', 'layer-text', `<b>${specification ? specification.Name : 'Unknown layer'}</b><small>${layerSummary(layer, specification, snapshot.scene)}</small>`);
            const badges = element('span', 'layer-badges');
            if (required) badges.appendChild(element('em', 'layer-badge', 'base'));
            if (layer.Solo) badges.appendChild(element('em', 'layer-badge solo', 'solo'));
            if (specification && !specification.Linked && layer.Weight < 1) badges.appendChild(element('em', 'layer-badge', `${Math.round(layer.Weight * 100)}%`));
            for (const issue of issues)
            {
                const badge = element('em', 'layer-badge ' + (issue.Severity === 'Info' ? 'info' : 'warn'), issue.Severity === 'Info' ? 'inert' : '!');
                badge.title = issue.Text;
                badges.appendChild(badge);
            }
            row.append(handle, toggle, text, badges);

            if (!required)
            {
                const move = (delta) => (event) => { event.stopPropagation(); app.moveLayer(layer.Token, index + delta); };
                const up = element('button', 'layer-move', '↑');
                up.title = 'Move up';
                up.disabled = index <= 1;
                up.addEventListener('click', move(-1));
                const down = element('button', 'layer-move', '↓');
                down.title = 'Move down';
                down.disabled = index >= stack.Layers.length - 1;
                down.addEventListener('click', move(1));
                const remove = element('button', 'layer-remove', '×');
                remove.title = specification && specification.Linked ? 'Delete layer and its source' : 'Delete layer';
                remove.addEventListener('click', (event) => { event.stopPropagation(); app.removeLayer(layer.Token); });
                row.append(up, down, remove);
            }
            row.addEventListener('click', () => app.selectLayer(layer.Token));
            row.addEventListener('dragstart', (event) => { dragged = layer.Token; event.dataTransfer.effectAllowed = 'move'; row.classList.add('dragging'); });
            row.addEventListener('dragend', () => row.classList.remove('dragging'));
            row.addEventListener('dragover', (event) => { if (dragged && dragged !== layer.Token) { event.preventDefault(); row.classList.add('drop-target'); } });
            row.addEventListener('dragleave', () => row.classList.remove('drop-target'));
            row.addEventListener('drop', (event) =>
            {
                event.preventDefault();
                row.classList.remove('drop-target');
                if (dragged) app.moveLayer(dragged, index);
                dragged = null;
            });
            list.appendChild(row);
        });
        stackCard.appendChild(list);
        stackCard.appendChild(element('p', 'note', note));
        host.appendChild(stackCard);

        const selected = stack.Layers.find((layer) => layer.Token === snapshot.selectedLayer) || stack.Layers[0];
        if (!selected) return;
        const specification = LayerSpecificationByCode[selected.Category];
        if (!specification)
        {
            host.appendChild(card('Unknown layer', `category ${selected.Category}`, [
                element('p', 'note', 'Written by a newer editor. Kept verbatim and saved back unchanged.'),
            ], 'SELECTED LAYER'));
            return;
        }

        const controls = [element('p', 'note', specification.Summary)];
        const writeSetting = (key) => (amount) => app.updateLayer(selected.Token, key, amount);
        const writeParameter = (key) => (amount) => app.updateLayer(selected.Token, key, amount, true);
        const time = app.solver ? app.solver.time : 0;
        const view = {
            ...selected,
            Weight: layerAmountAt(stack, selected, 'Weight', time),
            Parameters: Object.fromEntries(Object.keys(selected.Parameters).map((name) => [name, layerAmountAt(stack, selected, name, time)])),
        };
        const keyed = (control, name) => attachKey(control, { Kind: 'Layer', Token: selected.Token, Name: name });

        if (!specification.Required && !specification.Linked)
        {
            controls.push(keyed(slider(view, 'Weight', 'Layer weight', 0, 1, 0.01, '', 2, writeSetting('Weight')), 'Weight'));
            const blendLabels = Object.fromEntries(BlendModes.map((mode) => [mode.Code, mode.Label]));
            controls.push(choice(selected, 'Blend', 'Blend mode', BlendModes.map((mode) => mode.Code), blendLabels, writeSetting('Blend')));
            controls.push(statusTile('Solo layer', selected.Solo ? 'SOLO' : 'OFF', 'eye', selected.Solo, () => app.updateLayer(selected.Token, 'Solo', !selected.Solo)));
        }

        for (const parameter of specification.Parameter)
        {
            const write = writeParameter(parameter.Name);
            const amounts = view.Parameters;
            if (owner && !particleParameterShown(parameter.Name, amounts, owner)) continue;
            const range = owner ? particleParameterRange(parameter, owner) : parameter;
            if (parameter.Type === 'Float32' || parameter.Type === 'Int32' || parameter.Type === 'UInt32')
            {
                controls.push(keyed(
                    slider(amounts, parameter.Name, parameter.Label, range.Minimum, range.Maximum, range.Step ?? 0.01, range.Unit || '', parameter.Type === 'Float32' ? (range.Digits ?? 2) : 0, write),
                    parameter.Name
                ));
            }
            else if (parameter.Type === 'Vector3')
            {
                controls.push(keyed(vector(amounts, parameter.Name, parameter.Unit ? `${parameter.Label} (${parameter.Unit})` : parameter.Label, parameter.Step ?? 0.05, false, write), parameter.Name));
            }
            else if (parameter.Type === 'Ramp8') controls.push(paletteControl(selected.Parameters, write));
            else if (parameter.Type === 'Choice')
            {
                controls.push(keyed(
                    choice(amounts, parameter.Name, parameter.Label, parameter.Options.map((option) => option.Code), Object.fromEntries(parameter.Options.map((option) => [option.Code, option.Label])), write),
                    parameter.Name
                ));
            }
            else if (parameter.Type === 'ColourLinear') controls.push(keyed(colour(amounts, parameter.Name, parameter.Label, write), parameter.Name));
            else if (parameter.Type === 'Bool')
            {
                controls.push(keyed(statusTile(parameter.Label, amounts[parameter.Name] ? 'ON' : 'OFF', 'power', amounts[parameter.Name], () => write(!amounts[parameter.Name])), parameter.Name));
            }
            else if (parameter.Type === 'Token' && specification.Linked === 'Emitter')
            {
                const emitter = snapshot.scene.Emitters.find((candidate) => candidate.Id === selected.Parameters.Emitter);
                const open = element('button', '', `${icon('flame', 12)} Inspect ${emitter ? emitter.Name : 'source'}`);
                open.disabled = !emitter;
                open.addEventListener('click', () => emitter && app.select(emitter.Id));
                const actions = element('div', 'action-row');
                actions.appendChild(open);
                controls.push(actions);
            }
            else if (parameter.Type === 'Token')
            {
                const row = element('div', 'control-row');
                row.appendChild(element('label', '', parameter.Label));
                const input = document.createElement('input');
                input.type = 'text';
                input.className = 'text-input';
                input.maxLength = 24;
                input.value = String(amounts[parameter.Name] ?? '');
                input.addEventListener('change', () => write(input.value.trim() || '2026'));
                row.appendChild(input);
                controls.push(row);
            }
        }

        if (selected.Category === SpawnCategoryCode || selected.Category === CarrierCategoryCode)
            controls.push(objectImportControl(stack, selected.Category === CarrierCategoryCode ? 'carrier' : 'spawn'));

        const panel = card(specification.Name, specification.Phase, controls, `SELECTED LAYER · ${specification.Phase.toUpperCase()}`);
        panel.setAttribute('data-layer-inspector', selected.Token);
        host.appendChild(panel);
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                     CUSTOM 3D OBJECT IMPORT (Spawn + Sprite & Object)
    //------------------------------------------------------------------------------------------------------------------------

    /** Importer supporting independent Spawn vs Sprite & Object custom meshes (plus drag-and-drop onto the viewport). */
    function importCustomObjectFile(file, target = 'both')
    {
        if (!file) return;
        const name = file.name || 'Custom Object';
        const lower = name.toLowerCase();
        const reader = new FileReader();
        reader.addEventListener('load', () =>
        {
            const result = app.importCustomObject(name, reader.result, null, target);
            if (!result) alert(`Could not read "${name}". Supported: .obj, binary/ascii .stl, Frontier .facet (FCT1), .json { vertices, indices }.`);
        });
        reader.addEventListener('error', () => alert(`Could not read "${name}".`));
        if (lower.endsWith('.stl') || lower.endsWith('.facet')) reader.readAsArrayBuffer(file);
        else reader.readAsText(file);
    }

    function pickCustomObjectFile(target = 'both')
    {
        const input = document.createElement('input');
        input.type = 'file';
        input.accept = '.obj,.stl,.facet,.json';
        input.addEventListener('change', () => { const file = input.files && input.files[0]; if (file) importCustomObjectFile(file, target); });
        input.click();
    }

    function objectImportControl(stack, target = 'spawn')
    {
        const spawn = stack.Layers.find((layer) => layer.Category === SpawnCategoryCode);
        const carrier = stack.Layers.find((layer) => layer.Category === CarrierCategoryCode);
        const currentId = target === 'carrier'
            ? ((carrier && carrier.Parameters.CarrierCustomObjectId) || (spawn && spawn.Parameters.CustomObjectId) || '')
            : ((spawn && spawn.Parameters.CustomObjectId) || '');
        const entry = CustomObjectEntry(currentId);
        const holder = element('div', 'control');
        const heading = target === 'carrier' ? 'Custom 3D carrier object' : 'Custom 3D spawn object';
        holder.appendChild(element('div', 'control-head', `<span>${heading}</span>`));
        holder.appendChild(element('p', 'note', entry
            ? `Using "${entry.Name}" · ${entry.TriangleCount} triangles · supports .obj / .stl / .facet / .json (or drop onto viewport).`
            : 'No mesh imported yet. Import .obj, .stl, .facet or .json — or drop onto the viewport. Without one the Utah Teapot stands in.'));
        const row = element('div', 'action-row');
        const importButton = element('button', '', `${icon('upload', 12)} Import 3D object`);
        importButton.addEventListener('click', () => pickCustomObjectFile(target));
        row.appendChild(importButton);
        const ids = CustomObjectIds();
        if (ids.length)
        {
            const select = document.createElement('select');
            select.className = 'layer-add';
            select.setAttribute('aria-label', heading);
            const none = document.createElement('option');
            none.value = '';
            none.textContent = 'Teapot stand-in';
            select.appendChild(none);
            for (const id of ids)
            {
                const option = document.createElement('option');
                option.value = id;
                option.textContent = `${CustomObjectEntry(id).Name} (${CustomObjectEntry(id).TriangleCount} tris)`;
                select.appendChild(option);
            }
            select.value = currentId || '';
            select.addEventListener('change', () => app.setCustomObject(select.value, null, target));
            row.appendChild(select);
        }
        holder.appendChild(row);
        holder.setAttribute('data-custom-object', currentId || 'none');
        holder.setAttribute('data-custom-target', target);
        return holder;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                          FORCE / SOURCE / PARTICLE / COLLIDER
    //------------------------------------------------------------------------------------------------------------------------

    function renderForceInspector(host, force, snapshot)
    {
        const type = ForceTypeById[force.Type];
        const live = keyedNode(force, app.solver ? app.solver.time : 0);

        renderInspectorPages(host, 'Force', [
            {
                label: 'Field',
                build: (target) =>
                {
                    const schematic = element('div', 'pz-schematic-box');
                    schematic.appendChild(renderForceThumbnail(type, 290, 128));
                    schematic.appendChild(element(
                        'div',
                        'pz-schematic-caption',
                        `<span>Analytical streamlines</span><span>${FalloffLabels[force.Falloff]} reach</span>`
                    ));

                    const typeControl = element('div', 'control');
                    typeControl.appendChild(element('div', 'control-head', '<span>Generator type</span>'));
                    const typeSelect = document.createElement('select');
                    for (const category of ForceCategories)
                    {
                        const group = document.createElement('optgroup');
                        group.label = category;
                        for (const candidate of ForceGeneratorTypes.filter((entry) => entry.Category === category))
                        {
                            const option = document.createElement('option');
                            option.value = candidate.Id;
                            option.textContent = candidate.Name;
                            option.selected = candidate.Id === force.Type;
                            group.appendChild(option);
                        }
                        typeSelect.appendChild(group);
                    }
                    typeSelect.addEventListener('change', () =>
                    {
                        const next = ForceTypeById[typeSelect.value];
                        app.updateNode(force.Id, 'Type', next.Id);
                        if (force.Name === type.Name) app.updateNode(force.Id, 'Name', next.Name);
                    });
                    typeControl.appendChild(typeSelect);

                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile('Enabled', force.Enabled ? 'ON' : 'OFF', 'power', force.Enabled, () => app.updateNode(force.Id, 'Enabled', !force.Enabled)));
                    status.appendChild(statusTile('Gizmo', force.Visible ? 'SHOWN' : 'HIDDEN', 'gizmo', force.Visible, () => app.updateNode(force.Id, 'Visible', !force.Visible)));
                    target.appendChild(status);

                    const extras = [];
                    if (type.Labels.Frequency) extras.push(slider(live, 'Frequency', type.Labels.Frequency, 0, type.Category === 'Burst' ? 6 : (type.Id === 'BrownianJitter' ? 20 : (type.Id === 'FlowStream' ? 10 : 4)), 0.05));
                    if (type.Labels.Secondary) extras.push(slider(live, 'Secondary', type.Labels.Secondary, 0, type.Id === 'VortexColumn' ? 8 : (type.Id === 'BrownianJitter' ? 30 : 2), 0.05));
                    if (type.Labels.Tertiary) extras.push(slider(live, 'Tertiary', type.Labels.Tertiary, 0, 10, 0.1, 's'));

                    target.appendChild(card('Force Field', type.Category, [
                        element('div', 'pz-figure-head', `<strong>${formatNumber(live.Strength, 1)}</strong><span>m/s²</span>`),
                        schematic,
                        typeControl,
                        slider(live, 'Strength', 'Strength', -60, 60, 0.5, 'm/s²', 1),
                        slider(live, 'Radius', 'Reach radius', 0.1, 4, 0.05, 'm'),
                        choice(live, 'Falloff', 'Falloff envelope', FalloffCurves, FalloffLabels),
                        ...extras,
                    ], `FORCE / ${type.Category.toUpperCase()}`));

                    target.appendChild(entryActions(force));
                },
            },
            {
                label: 'Transform',
                build: (target) =>
                {
                    target.appendChild(transformTableCard(force, live, [
                        { key: 'Position', label: 'Position', unit: 'm', step: 0.05, reset: [0, 0, 0] },
                        { key: 'Direction', label: 'Axis', unit: 'dir', step: 0.05, reset: [0, 1, 0] },
                    ], 'WORLD Y-UP'));
                    target.appendChild(entryActions(force));
                },
            },
            {
                label: 'Timing',
                build: (target) =>
                {
                    target.appendChild(timingCard(force));
                    target.appendChild(entryActions(force));
                },
            },
        ], snapshot);
    }

    function renderEmitterInspector(host, emitter, snapshot)
    {
        const live = keyedNode(emitter, app.solver ? app.solver.time : 0);

        renderInspectorPages(host, 'Emitter', [
            {
                label: 'Source',
                build: (target) =>
                {
                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile('Enabled', emitter.Enabled ? 'ON' : 'OFF', 'power', emitter.Enabled, () => app.updateNode(emitter.Id, 'Enabled', !emitter.Enabled)));
                    status.appendChild(statusTile('Pulse', emitter.Pulse > 0 ? `${formatNumber(emitter.Pulse, 1)} s` : 'CONTINUOUS', 'burst', emitter.Pulse > 0, () => app.updateNode(emitter.Id, 'Pulse', emitter.Pulse > 0 ? 0 : 1.5)));
                    target.appendChild(status);

                    const shapeRail = element('div', 'pz-catalogue-rail');
                    for (const shapeName of EmitterShapes)
                    {
                        const item = element('button', 'pz-catalogue-item' + (emitter.Shape === shapeName ? ' active' : ''));
                        item.appendChild(renderSourceThumbnail(shapeName, 84, 84));
                        item.appendChild(element('b', '', shapeName));
                        item.addEventListener('click', () => app.updateNode(emitter.Id, 'Shape', shapeName));
                        shapeRail.appendChild(item);
                    }

                    target.appendChild(card('Gas & Heat Injection', `${emitter.Shape} nozzle`, [
                        element('div', 'pz-figure-head', `<strong>${formatNumber(live.Temperature, 1)}</strong><span>K · ρ ${formatNumber(live.Density, 1)}</span>`),
                        shapeRail,
                        slider(live, 'Radius', 'Nozzle radius', 0.05, 2, 0.01, 'm'),
                        slider(live, 'Density', 'Smoke density', 0, 3, 0.05, 'ρ'),
                        slider(live, 'Temperature', 'Temperature', 0, 8, 0.1, 'K'),
                        slider(live, 'Fuel', 'Combustion fuel', 0, 2, 0.05),
                        slider(live, 'Jitter', 'Turbulent jitter', 0, 1, 0.02),
                        slider(live, 'Pulse', 'Pulse period', 0, 5, 0.1, 's'),
                    ], 'SOURCE / INJECTION'));

                    target.appendChild(card('Ballistic Debris', 'Ejecta', [
                        choice(live, 'DebrisKind', 'Debris material', DebrisKinds),
                        slider(live, 'Debris', 'Debris rate', 0, 1, 0.02),
                        slider(live, 'DebrisSpeed', 'Ejection speed', 0, 20, 0.5, 'm/s', 1),
                    ], 'SOURCE / DEBRIS'));
                },
            },
            {
                label: 'Transform',
                build: (target) =>
                {
                    const rows = [
                        { key: 'Position', label: 'Position', unit: 'm', step: 0.05, reset: [0, -1.6, 0] },
                        { key: 'Velocity', label: 'Velocity', unit: 'm/s', step: 0.1, reset: [0, 1.5, 0] },
                    ];
                    if (emitter.Shape === 'Disc') rows.push({ key: 'Orientation', label: 'Normal', unit: 'dir', step: 0.05, reset: [0, 1, 0] });
                    target.appendChild(transformTableCard(emitter, live, rows, 'WORLD Y-UP'));
                    target.appendChild(entryActions(emitter));
                },
            },
            {
                label: 'Motion',
                build: (target) =>
                {
                    target.appendChild(card('Animated Trajectory', 'Moving nozzle', [
                        choice(live, 'PathKind', 'Trajectory', EmitterPathKinds, { None: 'Static', Figure8: 'Figure 8' }),
                        emitter.PathKind !== 'None' && emitter.PathKind !== 'Straight'
                            ? slider(live, 'PathRadius', emitter.PathKind === 'Slalom' ? 'Weave width' : 'Orbit radius', 0.1, 4, 0.05, 'm')
                            : null,
                        emitter.PathKind === 'Straight' || emitter.PathKind === 'Slalom'
                            ? slider(live, 'PathSpeed', 'Drive speed', 0, 12, 0.1, 'm/s')
                            : emitter.PathKind !== 'None'
                                ? slider(live, 'PathSpeed', 'Angular speed', 0, 4, 0.05, 'rad/s')
                                : null,
                        element('p', 'note', 'Exit velocity transforms with the moving nozzle heading so negative X trails smoke behind a drifting vehicle or projectile.'),
                    ], 'SOURCE / TRAJECTORY'));
                    target.appendChild(entryActions(emitter));
                },
            },
            {
                label: 'Timing',
                build: (target) =>
                {
                    target.appendChild(timingCard(emitter));
                    target.appendChild(entryActions(emitter));
                },
            },
        ], snapshot);
    }

    function renderParticleInspector(host, cloud, snapshot)
    {
        const template = ParticleTemplateById[cloud.Template] || ParticleTemplates[0];
        const evaluation = app.particleEvaluation(cloud);
        const live = keyedNode(cloud, app.solver ? app.solver.time : 0);
        const activeLayers = cloud.Stack ? cloud.Stack.Layers.filter((layer) => layer.Enabled).length : 0;

        renderInspectorPages(host, 'Particles', [
            {
                label: 'Stack',
                build: (target) =>
                {
                    target.appendChild(statPair('Particles', formatCells(cloud.Count), 'blue', 'Active layers', activeLayers, ''));
                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile('Enabled', cloud.Enabled ? 'ON' : 'OFF', 'power', cloud.Enabled, () => app.updateNode(cloud.Id, 'Enabled', !cloud.Enabled)));
                    status.appendChild(statusTile('Mode', cloud.Mode === 'Cloner' ? 'CLONER' : 'EMIT', cloud.Mode === 'Cloner' ? 'gridcloner' : 'sparks', cloud.Mode === 'Cloner', () => app.updateNode(cloud.Id, 'Mode', cloud.Mode === 'Cloner' ? 'Emit' : 'Cloner')));
                    target.appendChild(status);

                    renderLayerStack(target, snapshot, {
                        stack: cloud.Stack,
                        evaluation,
                        specifications: ParticleLayerSpecifications,
                        stopped: (reason) => `⚠️ ${cloud.Name} held: ${reason}.`,
                        note: 'Particle layers evaluate Spawn → Advance → Shade → Overlay.',
                        owner: cloud,
                    });
                    target.appendChild(entryActions(cloud));
                },
            },
            {
                label: 'Catalogue',
                build: (target) =>
                {
                    const previewBox = element('div', 'pz-schematic-box');
                    previewBox.appendChild(renderParticleThumbnail(template, 290, 128));
                    previewBox.appendChild(element('div', 'pz-schematic-caption', `<span>${template.Name}</span><span>${template.Category}</span>`));
                    target.appendChild(card('Active Template', template.Category, [
                        previewBox,
                        element('p', 'note', template.Summary),
                    ], 'PARTICLE CLOUD / TEMPLATE'));

                    for (const category of ParticleCategories)
                    {
                        const rail = element('div', 'pz-catalogue-rail');
                        for (const candidate of ParticleTemplates.filter((entry) => entry.Category === category))
                        {
                            const item = element('button', 'pz-catalogue-item' + (candidate.Id === cloud.Template ? ' active' : ''));
                            item.appendChild(renderParticleThumbnail(candidate, 80, 80));
                            item.appendChild(element('b', '', candidate.Name));
                            item.title = candidate.Summary;
                            item.addEventListener('click', () =>
                            {
                                app.replaceParticleTemplate(cloud.Id, candidate.Id);
                            });
                            rail.appendChild(item);
                        }
                        target.appendChild(card(category, `${rail.children.length} templates`, [rail], 'CATALOGUE'));
                    }
                },
            },
            {
                label: 'Transform',
                build: (target) =>
                {
                    target.appendChild(transformTableCard(cloud, live, [
                        { key: 'Position', label: 'Origin', unit: 'm', step: 0.05, reset: [0, 0, 0] },
                        { key: 'Direction', label: 'Axis', unit: 'dir', step: 0.05, reset: [0, 1, 0] },
                    ], 'SPAWN TRANSFORM'));
                    target.appendChild(entryActions(cloud));
                },
            },
            {
                label: 'Timing',
                build: (target) =>
                {
                    target.appendChild(timingCard(cloud));
                    target.appendChild(entryActions(cloud));
                },
            },
        ], snapshot);
    }

    function particleParameterShown(name, parameters, cloud)
    {
        const emit = parameters.Mode === 0;
        const scalar = [1, 2, 3].includes(parameters.ColourMode);
        const trail = parameters.Trail !== 0;
        void cloud;
        switch (name)
        {
            case 'Fill': return (parameters.Mode === 1 && parameters.Shape === 1) || [11, 12, 13, 14, 15].includes(parameters.Shape);
            case 'ShapeDetail': return [12, 13, 14, 15].includes(parameters.Shape);
            case 'FireworkPattern': return parameters.Shape === 15;
            case 'FireworkText': return parameters.Shape === 15 && (parameters.FireworkPattern === 1 || parameters.FireworkPattern === 9);
            case 'SpawnObject': case 'SpawnSampleMode': case 'ObjectYaw': return parameters.Shape === 11;
            case 'BackingSurface': return parameters.Shape === 11 || parameters.BackingSurface > 0;
            case 'Sprite': case 'Style': return parameters.CarrierMode !== 1;
            case 'CarrierObject': case 'ObjectSpin': case 'ObjectTilt': case 'ObjectRoughness': case 'ObjectMetallic': case 'ObjectGlow':
                return parameters.CarrierMode === 1;
            case 'CustomObjectId': case 'CarrierCustomObjectId': return false;
            case 'StrandVariant': return [3, 4, 9].includes(parameters.StrandMode);
            case 'StrandCount': case 'StrandDetail': case 'StrandSag': case 'StrandThickness': case 'StrandSway': case 'StrandSpan': case 'StrandGlow':
                return parameters.StrandMode > 0;
            case 'Rate': case 'Lifetime': case 'Linger': case 'Speed': case 'Spread': case 'Radial': return emit;
            case 'RampSource': return scalar;
            case 'Ramp': return scalar && parameters.RampSource === 1;
            case 'Palette': return (!scalar || parameters.RampSource === 0) && parameters.ColourMode !== 4;
            case 'MaxSpeed': return parameters.ColourMode === 1;
            case 'DisplacementRange': return parameters.ColourMode === 2;
            case 'CycleSpeed': return parameters.ColourMode === 6;
            case 'Aperture': return parameters.FocusDistance > 0;
            case 'Spring': case 'Damping': case 'Dissolve': return true;
            default: return !(name.startsWith('Trail') && name !== 'Trail') || trail;
        }
    }

    function particleParameterRange(parameter, cloud)
    {
        if (!['SizeStart', 'SizeEnd', 'TrailWidth'].includes(parameter.Name)) return parameter;
        const world = cloud.SizeMode !== 'Pixels';
        return world
            ? { ...parameter, Minimum: 0.001, Maximum: parameter.Name === 'TrailWidth' ? 0.1 : 0.15, Step: 0.001, Unit: 'm', Digits: 3 }
            : { ...parameter, Minimum: 0.5, Maximum: 12, Step: 0.5, Unit: 'px', Digits: 1 };
    }

    function paletteControl(holder, write)
    {
        const palette = holder.Palette;
        const control = element('div', 'control palette');
        const head = element('div', 'control-head', `<span>Palette <span class="unit">${palette.length} stops</span></span>`);
        const tools = element('div', 'palette-tools');
        const add = element('button', 'mini', '+');
        add.title = 'Add stop';
        add.disabled = palette.length >= MaximumPaletteStops;
        add.addEventListener('click', () => write([...palette, palette[palette.length - 1]]));
        const shuffle = element('button', 'mini', icon('reset', 10));
        shuffle.title = 'Shuffle order';
        shuffle.addEventListener('click', () => write([...palette].sort(() => Math.random() - 0.5)));
        const reverse = element('button', 'mini', '⇄');
        reverse.title = 'Reverse';
        reverse.addEventListener('click', () => write([...palette].reverse()));
        tools.append(add, shuffle, reverse);
        head.appendChild(tools);
        control.appendChild(head);

        const strip = element('div', 'palette-strip');
        strip.style.background = `linear-gradient(90deg, ${palette.length > 1 ? palette.join(', ') : `${palette[0]}, ${palette[0]}`})`;
        control.appendChild(strip);

        const stops = element('div', 'palette-stops');
        palette.forEach((hex, index) =>
        {
            const stop = element('div', 'palette-stop');
            const input = document.createElement('input');
            input.type = 'color';
            input.value = hex;
            input.title = `Stop ${index + 1}`;
            input.addEventListener('input', () => { const next = [...palette]; next[index] = input.value; write(next); });
            stop.appendChild(input);
            if (palette.length > 1)
            {
                const remove = element('button', 'mini', '×');
                remove.title = 'Remove stop';
                remove.addEventListener('click', () => write(palette.filter((_, i) => i !== index)));
                stop.appendChild(remove);
            }
            stops.appendChild(stop);
        });
        control.appendChild(stops);

        const presets = element('div', 'choice-row');
        for (const [name, swatches] of Object.entries(PalettePresets))
        {
            const button = element('button', '', `<span class="swatch-dots">${swatches.map((hex) => `<i style="background:${hex}"></i>`).join('')}</span>${name}`);
            button.addEventListener('click', () => write([...swatches]));
            presets.appendChild(button);
        }
        control.appendChild(presets);
        return control;
    }

    function renderColliderInspector(host, collider, snapshot)
    {
        renderInspectorPages(host, 'Collider', [
            {
                label: 'Boundary',
                build: (target) =>
                {
                    const schematic = element('div', 'pz-schematic-box');
                    schematic.appendChild(renderColliderThumbnail(290, 124));
                    schematic.appendChild(element('div', 'pz-schematic-caption', `<span>No-slip Neumann obstacle</span><span>r = ${formatNumber(collider.Radius, 2)} m</span>`));

                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile('Enabled', collider.Enabled ? 'ON' : 'OFF', 'power', collider.Enabled, () => app.updateNode(collider.Id, 'Enabled', !collider.Enabled)));
                    status.appendChild(statusTile('Visible', collider.Visible ? 'SHOWN' : 'HIDDEN', 'eye', collider.Visible, () => app.updateNode(collider.Id, 'Visible', !collider.Visible)));
                    target.appendChild(status);

                    target.appendChild(card('Sphere Obstacle', 'Solid boundary', [
                        element('div', 'pz-figure-head', `<strong>${formatNumber(collider.Radius, 2)}</strong><span>m radius</span>`),
                        schematic,
                        slider(collider, 'Radius', 'Sphere radius', 0.05, 2, 0.01, 'm'),
                    ], 'COLLIDER / SHAPE'));
                    target.appendChild(entryActions(collider));
                },
            },
            {
                label: 'Transform',
                build: (target) =>
                {
                    target.appendChild(transformTableCard(collider, collider, [
                        { key: 'Position', label: 'Centre', unit: 'm', step: 0.05, reset: [0, 0, 0] },
                    ], 'WORLD Y-UP'));
                    target.appendChild(entryActions(collider));
                },
            },
        ], snapshot);
    }

    function renderVisualizerInspector(host, visualizer, snapshot)
    {
        renderInspectorPages(host, 'Visualizer', [
            {
                label: 'Overlay',
                build: (target) =>
                {
                    const status = element('div', 'status-grid');
                    status.appendChild(statusTile('Visible', visualizer.Visible ? 'SHOWN' : 'HIDDEN', 'eye', visualizer.Visible, () => app.updateNode(visualizer.Id, 'Visible', !visualizer.Visible)));
                    if (visualizer.Ramp)
                    {
                        const legend = element('div', 'status-tile', `<span class="tile-icon" style="width:28px;height:28px;background:${rampGradient(visualizer.Ramp)}"></span><span class="tile-copy"><span class="tile-label">${visualizer.Ramp}</span><span class="tile-state">Colour ramp</span></span>`);
                        status.appendChild(legend);
                    }
                    else
                    {
                        status.appendChild(statusTile('Gas Stack', 'DOMAIN', 'domain', true, () => app.select('domain')));
                    }
                    target.appendChild(status);

                    if (visualizer.Id === 'vis-volume')
                    {
                        const owned = element('p', 'note layer-owned', 'Fire and smoke parameters write directly to the Gas Domain shading layers. ');
                        const openStack = element('button', 'link-button', 'Inspect Gas Domain →');
                        openStack.addEventListener('click', () => { uiMemory.tabByKind.Domain = 2; app.select('domain'); });
                        owned.appendChild(openStack);
                        target.appendChild(card('Volume Shading', 'Raymarch', [
                            owned,
                            slider(visualizer, 'FireIntensity', 'Fire emission', 0, 12, 0.1, '×'),
                            slider(visualizer, 'Density', 'Smoke density', 0, 4, 0.05, '×'),
                            slider(visualizer, 'Exposure', 'Exposure', 0.2, 4, 0.05, 'EV'),
                            slider(visualizer, 'Steps', 'March steps', 24, 256, 4, 'steps', 0),
                        ], 'OVERLAY / VOLUME'));
                    }
                    else if (visualizer.Id === 'vis-vectors')
                    {
                        target.appendChild(card('Velocity Slice', 'Vectors', [
                            choice(visualizer, 'Axis', 'Slice axis', SliceAxes),
                            slider(visualizer, 'Offset', 'Slice offset', 0, 1, 0.01),
                            slider(visualizer, 'Stride', 'Cell stride', 1, 8, 1, '', 0),
                            slider(visualizer, 'Scale', 'Arrow scale', 0.02, 1.5, 0.01, 's'),
                            slider(visualizer, 'MaxSpeed', 'Max speed', 0.5, 20, 0.1, 'm/s'),
                            choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
                        ], 'OVERLAY / VECTORS'));
                    }
                    else if (visualizer.Id === 'vis-forces')
                    {
                        target.appendChild(card('Force Field Vectors', 'Σ forces', [
                            choice(visualizer, 'Mode', 'Sampling', VectorModes, { Slice: 'Slice plane', Volume: 'Sparse 3D' }),
                            visualizer.Mode === 'Slice' ? choice(visualizer, 'Axis', 'Slice axis', SliceAxes) : null,
                            visualizer.Mode === 'Slice' ? slider(visualizer, 'Offset', 'Slice offset', 0, 1, 0.01) : null,
                            slider(visualizer, 'Stride', 'Cell stride', 1, 12, 1, '', 0),
                            slider(visualizer, 'Scale', 'Arrow scale', 0.005, 0.3, 0.005, 's²'),
                            slider(visualizer, 'MaxForce', 'Max |F|', 1, 100, 1, 'm/s²'),
                            choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
                        ], 'OVERLAY / FORCES'));
                    }
                    else if (visualizer.Id === 'vis-slice')
                    {
                        target.appendChild(card('Scalar Field Slice', 'Heat-map', [
                            choice(visualizer, 'Channel', 'Channel', ScalarChannels, { ForceMagnitude: 'Force |F|' }),
                            choice(visualizer, 'Axis', 'Slice axis', SliceAxes),
                            slider(visualizer, 'Offset', 'Slice offset', 0, 1, 0.01),
                            slider(visualizer, 'Range', 'Ramp max', 0.1, 40, 0.1),
                            slider(visualizer, 'Opacity', 'Opacity', 0, 1, 0.02),
                            choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
                        ], 'OVERLAY / SLICE'));
                    }
                    else if (visualizer.Id === 'vis-debris')
                    {
                        target.appendChild(card('Ballistic Debris', 'GPU particles', [
                            slider(visualizer, 'Count', 'Particle budget', 256, 4096, 256, '', 0),
                            slider(visualizer, 'Lifetime', 'Lifetime', 1, 15, 0.5, 's'),
                            slider(visualizer, 'Size', 'Point size', 1, 8, 0.5, 'px'),
                            slider(visualizer, 'Gravity', 'Gravity', 0, 20, 0.1, 'm/s²'),
                            slider(visualizer, 'Drag', 'Gas coupling', 0, 6, 0.1),
                            slider(visualizer, 'Bounce', 'Restitution', 0, 0.9, 0.05),
                        ], 'OVERLAY / DEBRIS'));
                    }
                    else if (visualizer.Id === 'vis-tracers')
                    {
                        target.appendChild(card('Flow Tracers', 'Stream motes', [
                            slider(visualizer, 'Count', 'Tracer count', 256, 8192, 256, '', 0),
                            slider(visualizer, 'Lifetime', 'Lifetime', 0.5, 12, 0.1, 's'),
                            slider(visualizer, 'Size', 'Point size', 1, 6, 0.5, 'px'),
                            slider(visualizer, 'MaxSpeed', 'Max speed', 0.5, 20, 0.1, 'm/s'),
                            choice(visualizer, 'Ramp', 'Colour ramp', ColourRamps),
                        ], 'OVERLAY / TRACERS'));
                    }
                    else
                    {
                        target.appendChild(card('Analytical Gizmos', 'Handles', [
                            slider(visualizer, 'Opacity', 'Wire opacity', 0, 1, 0.02),
                            element('p', 'note', 'Click a gizmo handle in the viewport to select its source or force; drag to reposition (hold Shift to lock height).'),
                        ], 'OVERLAY / GIZMOS'));
                    }
                    target.appendChild(element('p', 'note', 'Use Local / Global above to keep this overlay private to the active simulation folder or share it globally.'));
                },
            },
        ], snapshot);
    }

    function entryActions(entry)
    {
        const actions = element('div', 'action-row');
        const duplicate = element('button', '', `${icon('copy', 12)} Duplicate`);
        duplicate.addEventListener('click', () => app.duplicateNode(entry.Id));
        const remove = element('button', 'danger', `${icon('trash', 12)} Delete`);
        remove.addEventListener('click', () => app.removeNode(entry.Id));
        actions.append(duplicate, remove);
        return actions;
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  VIEWPORT TOOLBAR + TRANSPORT
    //------------------------------------------------------------------------------------------------------------------------

    function renderToolbar(snapshot)
    {
        const host = refs.toolbar;
        host.innerHTML = '';
        const scene = snapshot.scene;
        const dir = ensureSceneDirectory(scene);
        const activeSim = dir.Simulations.find((sim) => sim.Id === dir.ActiveSimulationId) || dir.Simulations[0];
        const titleLabel = dir.Simulations.length > 1 && activeSim ? `${activeSim.Name} (${dir.Simulations.length} sims)` : scene.Name;

        const rowA = element('div', 'toolbar-row');
        rowA.innerHTML = `<div class="scene-title"><span class="scene-icon">${icon('folderScene', 20)}</span><span class="meta"><strong>${titleLabel}</strong><span>${scene.Forces.length} forces · ${scene.Emitters.length} sources · ${scene.Domain.Resolution.join('×')}</span></span></div><span class="chip">PREVIEW</span>`;

        const segmented = element('div', 'segmented');
        for (const [key, label, glyph] of [['edit', 'Edit', 'pencil'], ['sim', 'Sim', 'vectors'], ['play', 'Play', 'play']])
        {
            const button = element('button', uiMemory.mode === key ? 'active' : '', `${icon(glyph, 11)} ${label}`);
            button.addEventListener('click', () => setMode(key));
            segmented.appendChild(button);
        }
        const scenes = element('button', 'scenes-button', `${icon('scene', 12)} Scenes <span class="badge">${ScenePresets.length}</span>`);
        scenes.title = 'Open the visual scene preset directory';
        scenes.addEventListener('click', () => toggleDrawer(!uiMemory.drawerOpen || uiMemory.drawerTab !== 'presets', 'presets'));
        rowA.appendChild(scenes);

        const flipbookButton = element('button', 'scenes-button', `${icon('download', 12)} Flipbook`);
        flipbookButton.title = 'Bake the simulation into a sprite sheet';
        flipbookButton.addEventListener('click', () => toggleDrawer(!uiMemory.drawerOpen || uiMemory.drawerTab !== 'flipbook', 'flipbook'));
        rowA.appendChild(flipbookButton);
        rowA.appendChild(segmented);
        host.appendChild(rowA);

        const rowB = element('div', 'toolbar-row');
        const addForce = element('button', 'pill primary', `${icon('plus', 12)} Add Force`);
        addForce.addEventListener('click', () => toggleDrawer(!uiMemory.drawerOpen || uiMemory.drawerTab !== 'forces', 'forces'));
        rowB.appendChild(addForce);

        const frame = element('button', 'view-toggle', icon('frame', 14));
        frame.title = 'Frame domain (F)';
        frame.addEventListener('click', () => app.frameDomain());
        rowB.appendChild(frame);

        const cameraSelect = element('div', 'pill-select', `<span class="leading-icon">${icon('perspective', 13)}</span>`);
        const select = document.createElement('select');
        for (const [value, label] of [['threeQuarter', 'Perspective'], ['front', 'Front'], ['side', 'Side'], ['top', 'Top']])
        {
            const option = document.createElement('option');
            option.value = value;
            option.textContent = label;
            select.appendChild(option);
        }
        select.addEventListener('change', () => app.setCameraPreset(select.value));
        cameraSelect.appendChild(select);
        rowB.appendChild(cameraSelect);
        rowB.appendChild(element('div', 'toolbar-spacer'));

        for (const [id, glyph, title] of [
            ['vis-volume', 'volume', 'Volume render'],
            ['vis-vectors', 'vectors', 'Velocity vectors'],
            ['vis-slice', 'slice', 'Scalar slice'],
            ['vis-tracers', 'tracers', 'Flow tracers'],
            ['vis-forces', 'dburst', 'Force field vectors'],
            ['vis-debris', 'debris', 'Debris particles'],
            ['vis-gizmos', 'gizmo', 'Force gizmos'],
        ])
        {
            const visualizer = Object.values(scene.Visualizers).find((candidate) => candidate.Id === id);
            const button = element('button', 'view-toggle' + (visualizer.Visible ? ' active' : ''), icon(glyph, 14));
            button.title = title;
            button.addEventListener('click', () => app.updateNode(id, 'Visible', !visualizer.Visible));
            rowB.appendChild(button);
        }
        host.appendChild(rowB);
    }

    function setMode(mode)
    {
        uiMemory.mode = mode;
        const gizmos = app.scene.Visualizers.ForceGizmos;
        if (mode === 'play')
        {
            gizmos.Visible = false;
            app.scene.Visualizers.VelocityVectors.Visible = false;
            app.play();
        }
        else if (mode === 'edit')
        {
            gizmos.Visible = true;
            app.pause();
        }
        else
        {
            gizmos.Visible = false;
            app.play();
        }
    }

    function renderTransport(snapshot)
    {
        const host = refs.transport;
        host.innerHTML = '';
        const play = element('button', 'primary', icon(snapshot.playing ? 'pause' : 'play', 14));
        play.title = snapshot.playing ? 'Pause (Space)' : 'Play (Space)';
        play.addEventListener('click', () => app.togglePlay());
        const step = element('button', '', icon('step', 14));
        step.title = 'Step one frame (.)';
        step.addEventListener('click', () => app.stepOnce());
        const reset = element('button', '', icon('reset', 14));
        reset.title = 'Reset fields (R)';
        reset.addEventListener('click', () => app.reset());
        const readout = element('div', 'readout', `<strong id="transport-time">0.00 s</strong><span id="transport-frame">frame 0</span>`);
        host.append(play, step, reset, readout);
    }

    function renderLegend(snapshot)
    {
        const host = refs.legend;
        host.innerHTML = '';
        const visualizers = snapshot.scene.Visualizers;
        const entries = [];
        if (visualizers.VelocityVectors.Visible) entries.push({ title: 'Velocity', ramp: visualizers.VelocityVectors.Ramp, min: '0', max: `${formatNumber(visualizers.VelocityVectors.MaxSpeed, 1)} m/s` });
        if (visualizers.Tracers.Visible && !(visualizers.VelocityVectors.Visible && visualizers.Tracers.Ramp === visualizers.VelocityVectors.Ramp)) entries.push({ title: 'Tracer speed', ramp: visualizers.Tracers.Ramp, min: '0', max: `${formatNumber(visualizers.Tracers.MaxSpeed, 1)} m/s` });
        if (visualizers.ForceVectors && visualizers.ForceVectors.Visible) entries.push({ title: 'Force |F|', ramp: visualizers.ForceVectors.Ramp, min: '0', max: `${formatNumber(visualizers.ForceVectors.MaxForce, 0)} m/s²` });
        if (visualizers.ScalarSlice.Visible)
        {
            const signed = visualizers.ScalarSlice.Channel === 'Pressure' || visualizers.ScalarSlice.Channel === 'Divergence';
            entries.push({ title: visualizers.ScalarSlice.Channel, ramp: visualizers.ScalarSlice.Ramp, min: signed ? `−${formatNumber(visualizers.ScalarSlice.Range, 1)}` : '0', max: formatNumber(visualizers.ScalarSlice.Range, 1) });
        }
        for (const entry of entries)
        {
            host.appendChild(element('div', 'legend-card', `<strong>${entry.title}</strong><div class="ramp" style="background:${rampGradient(entry.ramp)}"></div><div class="range"><span>${entry.min}</span><span>${entry.max}</span></div>`));
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                          3. NOTCH + VISUAL CONTENT DIRECTORY (DRAWER)
    //------------------------------------------------------------------------------------------------------------------------

    function renderNotch()
    {
        refs.notch.innerHTML = `<span class="notch-dot"></span><span>Project-Zero</span><span style="color:#555">·</span><span>Fluid Studio</span>`;
        refs.notch.addEventListener('click', () => { uiMemory.notchOpen = !uiMemory.notchOpen; refs.notchMenu.classList.toggle('open', uiMemory.notchOpen); });
        const menu = refs.notchMenu;
        menu.innerHTML = '';
        menu.appendChild(element('h4', '', 'Workspace'));
        const workspaces = element('div', 'notch-grid');
        for (const [label, sub, active] of [['World', 'Scene editor', false], ['Fluid', 'Gas + fire solver', true], ['ForceField', 'Vector field design', false]])
        {
            workspaces.appendChild(element('button', active ? 'active' : '', `<strong>${label}</strong><span>${sub}</span>`));
        }
        menu.appendChild(workspaces);
        menu.appendChild(element('h4', '', 'Scene document'));
        const documentRow = element('div', 'notch-grid');
        const exportButton = element('button', '', `<strong>${icon('download', 12)} Export</strong><span>.FluidScene JSON</span>`);
        exportButton.addEventListener('click', () =>
        {
            const blob = new Blob([app.exportJson()], { type: 'application/json' });
            downloadBlob(blob, `${app.scene.Name.replace(/\s+/g, '')}.FluidScene.json`);
        });
        const importButton = element('button', '', `<strong>${icon('upload', 12)} Import</strong><span>Load JSON</span>`);
        importButton.addEventListener('click', () =>
        {
            const input = document.createElement('input');
            input.type = 'file';
            input.accept = 'application/json';
            input.addEventListener('change', () =>
            {
                const file = input.files && input.files[0];
                if (!file) return;
                file.text().then((text) => app.importJson(text)).catch((error) => alert(error.message));
            });
            input.click();
        });
        const presetButton = element('button', '', `<strong>${icon('download', 12)} Save as preset</strong><span>.toml document</span>`);
        presetButton.addEventListener('click', () =>
        {
            const slug = app.scene.Name.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'custom';
            downloadBlob(new Blob([app.exportPresetToml({ Id: `preset-${slug}`, Family: 'Custom' })], { type: 'application/toml' }), `${slug}.toml`);
        });
        const resetButton = element('button', '', `<strong>${icon('reset', 12)} Reload</strong><span>Reset preset</span>`);
        resetButton.addEventListener('click', () => app.loadPreset(app.scene.PresetId || ScenePresets[0].Id));
        documentRow.append(exportButton, importButton, presetButton, resetButton);
        menu.appendChild(documentRow);
        menu.appendChild(element('p', 'note', 'Shortcuts — Space play/pause · . step · R reset · F frame · F2 rename · Delete remove selection · Drag outliner rows onto folders to rescope.'));
        document.addEventListener('pointerdown', (event) =>
        {
            if (uiMemory.notchOpen && !menu.contains(event.target) && !refs.notch.contains(event.target))
            {
                uiMemory.notchOpen = false;
                menu.classList.remove('open');
            }
        });
    }

    function toggleDrawer(open, tab)
    {
        uiMemory.drawerOpen = open;
        if (tab && tab !== uiMemory.drawerTab)
        {
            uiMemory.drawerTab = tab;
            uiMemory.drawerFilter = 'All';
            uiMemory.drawerSearch = '';
        }
        refs.drawer.classList.toggle('open', open);
        renderDrawer();
    }

    function buildVisualCard({ previewCanvas, pillText, title, summary, badge, loaded = false, onClick, onAddFolder = null })
    {
        const cardBtn = element('div', 'asset-card' + (loaded ? ' loaded' : ''));
        const thumb = element('div', 'thumb');
        thumb.appendChild(previewCanvas);
        if (pillText) thumb.appendChild(element('span', 'thumb-pill', pillText));
        cardBtn.appendChild(thumb);
        cardBtn.appendChild(element('strong', '', title));
        cardBtn.appendChild(element('span', '', summary));

        const footer = element('div', 'asset-card-footer');
        footer.appendChild(element('div', 'badge', badge));
        if (onAddFolder)
        {
            const folderBtn = element('button', 'add-folder-btn', `${icon('plus', 9)} Folder`);
            folderBtn.title = `Add "${title}" as its own Simulation Folder alongside existing folders`;
            folderBtn.addEventListener('click', (event) =>
            {
                event.stopPropagation();
                onAddFolder();
            });
            footer.appendChild(folderBtn);
        }
        cardBtn.appendChild(footer);
        cardBtn.addEventListener('click', onClick);
        return cardBtn;
    }

    function renderDrawer()
    {
        const drawer = refs.drawer;
        drawer.innerHTML = '';

        const head = element('div', 'drawer-head');
        head.appendChild(element('div', 'drawer-brand', `<small>CONTENT DIRECTORY</small><strong>Asset Browser</strong>`));

        const seg = element('div', 'seg');
        const tabDefs = [
            ['presets', 'Scene Presets', ScenePresets.length],
            ['forces', 'Force Generators', ForceGeneratorTypes.length],
            ['particles', 'Particle Clouds', ParticleTemplates.length],
            ['emitters', 'Sources & Colliders', 4],
            ['flipbook', 'Flipbook', ''],
        ];
        for (const [key, label, count] of tabDefs)
        {
            const button = element('button', uiMemory.drawerTab === key ? 'active' : '', `${label}${count !== '' ? ` <em>${count}</em>` : ''}`);
            button.addEventListener('click', () =>
            {
                uiMemory.drawerTab = key;
                uiMemory.drawerFilter = 'All';
                renderDrawer();
            });
            seg.appendChild(button);
        }
        head.appendChild(seg);

        if (uiMemory.drawerTab !== 'flipbook')
        {
            const searchWrap = element('div', 'drawer-search', icon('search', 12));
            const searchIn = document.createElement('input');
            searchIn.placeholder = 'Filter assets…';
            searchIn.value = uiMemory.drawerSearch;
            searchIn.addEventListener('input', () =>
            {
                uiMemory.drawerSearch = searchIn.value.toLowerCase();
                populateDrawerBody(body);
            });
            searchWrap.appendChild(searchIn);
            head.appendChild(searchWrap);
        }
        else
        {
            head.appendChild(element('div', 'toolbar-spacer'));
        }

        const close = element('button', 'icon-button', '×');
        close.title = 'Close Content Directory';
        close.addEventListener('click', () => toggleDrawer(false));
        head.appendChild(close);

        const subBar = element('div', 'drawer-subfilters');
        const subCategories = uiMemory.drawerTab === 'presets'
            ? ['All', 'VFX', 'Studio', 'Pyro']
            : uiMemory.drawerTab === 'forces'
                ? ['All', ...ForceCategories]
                : uiMemory.drawerTab === 'particles'
                    ? ['All', ...ParticleCategories]
                    : [];
        for (const sub of subCategories)
        {
            const chip = element('button', uiMemory.drawerFilter === sub ? 'active' : '', sub === 'VFX' ? 'VFX Suite (4)' : sub === 'Studio' ? 'Fluid Studio (18)' : sub === 'Pyro' ? 'Pyro Suite (18)' : sub);
            chip.addEventListener('click', () =>
            {
                uiMemory.drawerFilter = sub;
                renderDrawer();
            });
            subBar.appendChild(chip);
        }
        const hintText = uiMemory.drawerTab === 'forces'
            ? `${app.scene.Forces.length} / ${MaximumForceGenerators} force slots · click to add into active Simulation Folder`
            : uiMemory.drawerTab === 'particles'
                ? `${(app.scene.Particles || []).length} particle ${(app.scene.Particles || []).length === 1 ? 'cloud' : 'clouds'} in active simulation · click to add`
                : uiMemory.drawerTab === 'presets'
                    ? `Click card to replace scene · or click "+ Folder" to add alongside existing simulations`
                    : uiMemory.drawerTab === 'flipbook'
                        ? 'Bake viewport simulation into a game-ready sprite sheet + JSON timing'
                        : 'Click a source nozzle or collider obstacle to add into active Simulation Folder';
        subBar.appendChild(element('span', 'hint', hintText));
        head.appendChild(subBar);

        drawer.appendChild(head);
        const body = element('div', 'drawer-body');
        populateDrawerBody(body);
        drawer.appendChild(body);
    }

    function matchesDrawerQuery(name, summary)
    {
        if (!uiMemory.drawerSearch) return true;
        return name.toLowerCase().includes(uiMemory.drawerSearch) || (summary || '').toLowerCase().includes(uiMemory.drawerSearch);
    }

    function populateDrawerBody(body)
    {
        body.innerHTML = '';
        body.className = 'drawer-body';

        if (uiMemory.drawerTab === 'flipbook')
        {
            renderFlipbook(body);
            return;
        }

        if (uiMemory.drawerTab === 'presets')
        {
            const families = [['VFX', 'Derez, Mocap & Lensing VFX'], ['Studio', 'Fluid Studio Scenes'], ['Pyro', 'Pyro & Ordnance Scenes']];
            for (const [family, title] of families)
            {
                if (uiMemory.drawerFilter !== 'All' && uiMemory.drawerFilter !== family) continue;
                const members = ScenePresets.filter((preset) => preset.Family === family && matchesDrawerQuery(preset.Name, preset.Summary));
                if (members.length === 0) continue;
                const group = element('div', 'asset-group');
                group.appendChild(element('h4', '', `${title} <span>${members.length}</span>`));
                const row = element('div', 'asset-row');
                for (const preset of members)
                {
                    const isLoaded = app.scene && app.scene.PresetId === preset.Id;
                    const gridBadge = preset.Grid.Grid === 'Infinite' ? 'Infinite sparse' : `${preset.Grid.Resolution.join('×')} cells`;
                    row.appendChild(buildVisualCard({
                        previewCanvas: renderPresetThumbnail(preset),
                        pillText: isLoaded ? 'Loaded' : preset.Family,
                        title: preset.Name,
                        summary: preset.Summary,
                        badge: gridBadge,
                        loaded: isLoaded,
                        onClick: () => app.loadPreset(preset.Id),
                        onAddFolder: preset.Id !== 'preset-blank' ? () => app.addPresetAsFolder(preset.Id) : null,
                    }));
                }
                group.appendChild(row);
                body.appendChild(group);
            }
            return;
        }

        if (uiMemory.drawerTab === 'forces')
        {
            for (const category of ForceCategories)
            {
                if (uiMemory.drawerFilter !== 'All' && uiMemory.drawerFilter !== category) continue;
                const members = ForceGeneratorTypes.filter((entry) => entry.Category === category && matchesDrawerQuery(entry.Name, entry.Summary));
                if (members.length === 0) continue;
                const group = element('div', 'asset-group');
                group.appendChild(element('h4', '', `${category} <span>${members.length}</span>`));
                const row = element('div', 'asset-row');
                for (const forceType of members)
                {
                    row.appendChild(buildVisualCard({
                        previewCanvas: renderForceThumbnail(forceType),
                        pillText: `${forceType.Defaults.Strength} m/s²`,
                        title: forceType.Name,
                        summary: forceType.Summary,
                        badge: `${FalloffLabels[forceType.Defaults.Falloff]} falloff`,
                        onClick: () => app.addForce(forceType.Id),
                    }));
                }
                group.appendChild(row);
                body.appendChild(group);
            }
            return;
        }

        if (uiMemory.drawerTab === 'particles')
        {
            for (const category of ParticleCategories)
            {
                if (uiMemory.drawerFilter !== 'All' && uiMemory.drawerFilter !== category) continue;
                const members = ParticleTemplates.filter((entry) => entry.Category === category && matchesDrawerQuery(entry.Name, entry.Summary));
                if (members.length === 0) continue;
                const group = element('div', 'asset-group');
                group.appendChild(element('h4', '', `${category} <span>${members.length}</span>`));
                const row = element('div', 'asset-row');
                for (const template of members)
                {
                    row.appendChild(buildVisualCard({
                        previewCanvas: renderParticleThumbnail(template),
                        pillText: `${template.Defaults.Count} pts`,
                        title: template.Name,
                        summary: template.Summary,
                        badge: `${template.Defaults.Mode} · ${template.Defaults.Shape}`,
                        onClick: () => app.addParticles(template.Id),
                    }));
                }
                group.appendChild(row);
                body.appendChild(group);
            }
            return;
        }

        body.classList.add('EmitterGrid');
        for (const [shape, summary] of [
            ['Sphere', 'Isotropic volumetric plume source for smoke, buoyancy heat and combusting fuel.'],
            ['Disc', 'Directional circular jet nozzle — orient its normal vector to aim the plume.'],
            ['Box', 'Rectangular volume source with soft boundary falloff for wide fire beds.'],
        ])
        {
            if (!matchesDrawerQuery(`${shape} Source`, summary)) continue;
            body.appendChild(buildVisualCard({
                previewCanvas: renderSourceThumbnail(shape),
                pillText: 'Source',
                title: `${shape} Source`,
                summary,
                badge: 'Gas + Heat Emitter',
                onClick: () => app.addEmitter(shape),
            }));
        }
        if (matchesDrawerQuery('Sphere Collider', 'Solid no-slip obstacle'))
        {
            body.appendChild(buildVisualCard({
                previewCanvas: renderColliderThumbnail(),
                pillText: 'Collider',
                title: 'Sphere Collider',
                summary: 'Solid obstacle enforcing no-slip velocity and Neumann pressure deflection.',
                badge: 'Solid Boundary',
                onClick: () => app.addCollider(),
            }));
        }
    }

    //------------------------------------------------------------------------------------------------------------------------
    //                                                  METRICS + FLIPBOOK
    //------------------------------------------------------------------------------------------------------------------------

    function updateMetrics(metrics)
    {
        uiMemory.metrics = metrics;
        if (timeline) timeline.update();
        const time = document.getElementById('transport-time');
        const frame = document.getElementById('transport-frame');
        if (time) time.textContent = `${metrics.simulationTime.toFixed(2)} s`;
        if (frame) frame.textContent = `frame ${metrics.simulationFrame}`;
        refs.statusFps.textContent = `${metrics.fps.toFixed(0)} fps`;
        refs.statusStep.textContent = `${metrics.stepMilliseconds.toFixed(1)} ms · ${metrics.passes} passes`;
        refs.statusGrid.textContent = `${metrics.resolution.join('×')} · ${formatCells(metrics.cells)}`;
        refs.statusForces.textContent = `${metrics.forces} active · ${metrics.emitters} sources${metrics.particleSystems ? ` · ${formatCells(metrics.particles)} particles` : ''}`;
        refs.statusText.innerHTML = `HTML preview · <b>WebGL2 GPU solver</b> · ${metrics.playing ? 'simulating' : 'paused'}`;
    }

    function renderFlipbook(body)
    {
        const settings = uiMemory.flipbook;
        body.classList.add('flipbook-body');
        const locked = settings.baking ? ' disabled' : '';

        const setup = element('div', 'flipbook-card');
        setup.appendChild(element('h4', '', 'Sheet'));
        const flipChoice = (label, key, values, suffix) =>
        {
            const row = element('label', 'flipbook-row', `<span>${label}</span>`);
            const select = document.createElement('select');
            select.disabled = settings.baking;
            for (const val of values)
            {
                const option = document.createElement('option');
                option.value = val;
                option.textContent = `${val}${suffix}`;
                option.selected = val === settings[key];
                select.appendChild(option);
            }
            select.addEventListener('change', () => { settings[key] = Number(select.value); renderDrawer(); });
            row.appendChild(select);
            return row;
        };
        setup.appendChild(flipChoice('Frames', 'Count', FlipbookChoices.Count, ''));
        setup.appendChild(flipChoice('Tile size', 'Size', FlipbookChoices.Size, ' px'));
        setup.appendChild(flipChoice('Frame rate', 'Rate', FlipbookChoices.Rate, ' fps'));
        const warm = element('label', 'flipbook-row', `<span>Warm-up</span><input type="number" min="0" max="3" step="0.5" value="${settings.Warmup}"${locked}><em>s</em>`);
        warm.querySelector('input').addEventListener('change', (event) => { settings.Warmup = Math.max(0, Math.min(3, Number(event.target.value) || 0)); renderDrawer(); });
        setup.appendChild(warm);
        const columns = Math.ceil(Math.sqrt(settings.Count));
        setup.appendChild(element('p', 'flipbook-note', `${columns} × ${Math.ceil(settings.Count / columns)} grid · ${columns * settings.Size} × ${Math.ceil(settings.Count / columns) * settings.Size} px · ${(settings.Count / settings.Rate).toFixed(2)} s`));
        body.appendChild(setup);

        const options = element('div', 'flipbook-card');
        options.appendChild(element('h4', '', 'Bake'));
        const toggle = (label, key, hint) =>
        {
            const row = element('label', 'flipbook-toggle', `<input type="checkbox"${settings[key] ? ' checked' : ''}${locked}><span><strong>${label}</strong><em>${hint}</em></span>`);
            row.querySelector('input').addEventListener('change', (event) => { settings[key] = event.target.checked; });
            return row;
        };
        options.appendChild(toggle('Start from an empty grid', 'Restart', 'Otherwise continues from the current frame'));
        options.appendChild(toggle('Transparent background', 'Transparent', 'Straight alpha from smoke and fire'));
        const progress = element('div', 'flipbook-progress', `<div class="bar"><span style="width:${Math.round(settings.progress * 100)}%"></span></div><p id="flipbook-label">${settings.label}</p>`);
        options.appendChild(progress);
        const bake = element('button', 'pill primary', settings.baking ? `${icon('pause', 12)} Cancel` : `${icon('play', 12)} Bake flipbook`);
        bake.addEventListener('click', () => { if (settings.baking) settings.cancelled = true; else startFlipbookBake(); });
        options.appendChild(bake);
        body.appendChild(options);

        const preview = element('div', 'flipbook-card flipbook-preview');
        preview.appendChild(element('h4', '', 'Result'));
        preview.insertAdjacentHTML('beforeend', '<canvas id="flipbook-preview" width="168" height="168"></canvas>');
        const side = element('div', 'flipbook-side');
        if (settings.result)
        {
            const metadata = settings.result.metadata;
            side.appendChild(element('p', 'flipbook-note', `${settings.result.name}<br>${metadata.width} × ${metadata.height} · ${metadata.frameCount} frames · ${metadata.framesPerSecond} fps`));
            const scrub = element('label', 'flipbook-row', `<span>Frame</span><input type="range" min="0" max="${metadata.frameCount - 1}" value="${settings.frame}"><em id="flipbook-frame">${settings.frame + 1}</em>`);
            scrub.querySelector('input').addEventListener('input', (event) => { settings.play = false; settings.frame = Number(event.target.value); drawFlipbookPreview(); });
            side.appendChild(scrub);
            const play = element('label', 'flipbook-toggle', `<input type="checkbox"${settings.play ? ' checked' : ''}><span><strong>Loop playback</strong></span>`);
            play.querySelector('input').addEventListener('change', (event) => { settings.play = event.target.checked; });
            side.appendChild(play);
            const png = element('button', 'pill', `${icon('download', 12)} Sheet .png`);
            png.addEventListener('click', () => settings.result.sheet.toBlob((blob) => downloadBlob(blob, `${settings.result.name}.flipbook.png`), 'image/png'));
            const json = element('button', 'pill', `${icon('download', 12)} Timing + UV .json`);
            json.addEventListener('click', () => downloadBlob(new Blob([JSON.stringify(settings.result.metadata, null, 2)], { type: 'application/json' }), `${settings.result.name}.flipbook.json`));
            side.append(png, json);
        }
        else side.appendChild(element('p', 'flipbook-note', 'No bake yet. Frame the shot in the viewport first.'));
        preview.appendChild(side);
        body.appendChild(preview);
        drawFlipbookPreview();
    }

    function drawFlipbookPreview()
    {
        const settings = uiMemory.flipbook;
        const canvas = document.getElementById('flipbook-preview');
        if (!canvas || typeof canvas.getContext !== 'function') return;
        const context = canvas.getContext('2d');
        if (!context) return;
        context.fillStyle = '#101010';
        context.fillRect(0, 0, canvas.width, canvas.height);
        if (!settings.result) return;
        const frame = settings.result.metadata.frames[settings.frame];
        if (settings.result.metadata.alphaMode !== 'opaque')
        {
            for (let y = 0; y < canvas.height; y += 12)
            {
                for (let x = 0; x < canvas.width; x += 12)
                {
                    context.fillStyle = ((x + y) / 12) % 2 ? '#2a2a2a' : '#1a1a1a';
                    context.fillRect(x, y, 12, 12);
                }
            }
        }
        context.drawImage(settings.result.sheet, frame.x, frame.y, frame.width, frame.height, 0, 0, canvas.width, canvas.height);
        const label = document.getElementById('flipbook-frame');
        if (label) label.textContent = `${settings.frame + 1}`;
    }

    function downloadBlob(blob, name)
    {
        if (!blob) return;
        const link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = name;
        link.click();
        setTimeout(() => URL.revokeObjectURL(link.href), 1500);
    }

    async function startFlipbookBake()
    {
        const settings = uiMemory.flipbook;
        settings.baking = true;
        settings.cancelled = false;
        settings.progress = 0;
        settings.label = 'Starting the bake…';
        renderDrawer();
        const report = (fraction, text) =>
        {
            settings.progress = fraction;
            settings.label = text;
            const bar = refs.drawer.querySelector('.flipbook-progress .bar span');
            if (bar) bar.style.width = `${Math.round(fraction * 100)}%`;
            const label = document.getElementById('flipbook-label');
            if (label) label.textContent = text;
        };
        try
        {
            settings.result = await app.bakeFlipbook(settings, report, () => settings.cancelled);
            settings.frame = 0;
            settings.label = 'Bake complete — download the sheet and its timing / UV metadata.';
            settings.progress = 1;
        }
        catch (error)
        {
            settings.label = error.message;
        }
        settings.baking = false;
        renderDrawer();
    }

    let flipbookClock = 0;
    function flipbookPlayback(time)
    {
        requestAnimationFrame(flipbookPlayback);
        const settings = uiMemory.flipbook;
        if (!settings.result || !settings.play || settings.baking || !uiMemory.drawerOpen || uiMemory.drawerTab !== 'flipbook') return;
        if (time - flipbookClock < 1000 / settings.result.metadata.framesPerSecond) return;
        flipbookClock = time;
        settings.frame = (settings.frame + 1) % settings.result.metadata.frameCount;
        drawFlipbookPreview();
        const scrub = refs.drawer.querySelector('.flipbook-preview input[type=range]');
        if (scrub) scrub.value = settings.frame;
    }
    requestAnimationFrame(flipbookPlayback);

    function renderAll(snapshot)
    {
        uiMemory.snapshot = snapshot;
        renderOutliner(snapshot);
        renderInspector(snapshot);
        renderToolbar(snapshot);
        renderTransport(snapshot);
        renderLegend(snapshot);
        if (timeline) timeline.render();
        if (uiMemory.drawerOpen) renderDrawer();
        if (uiMemory.metrics) updateMetrics(uiMemory.metrics);
    }

    const timeline = refs.timeline ? mountTimeline({ app, host: refs.timeline }) : null;

    renderNotch();
    refs.drawerTab.innerHTML = `${icon('folder', 12)} Asset Browser`;
    refs.drawerTab.addEventListener('click', () => toggleDrawer(!uiMemory.drawerOpen));
    document.addEventListener('keydown', (event) =>
    {
        if (event.key === 'Escape') closeContextMenu();
        if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === 'f')
        {
            event.preventDefault();
            const input = refs.outliner.querySelector('.search-pill input');
            if (input) input.focus();
        }
    });

    //------------------------------------------------------------------------------------------------------------------------
    //                                 VIEWPORT DRAG & DROP — drop a mesh file straight onto the stage
    //------------------------------------------------------------------------------------------------------------------------

    const dropTarget = app.canvas;
    if (dropTarget && dropTarget.addEventListener)
    {
        dropTarget.addEventListener('dragover', (event) =>
        {
            if (!event.dataTransfer) return;
            event.preventDefault();
            event.dataTransfer.dropEffect = 'copy';
            dropTarget.classList.add('mesh-drop');
        });
        dropTarget.addEventListener('dragleave', () => dropTarget.classList.remove('mesh-drop'));
        dropTarget.addEventListener('drop', (event) =>
        {
            const file = event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0];
            if (!file) return;
            event.preventDefault();
            dropTarget.classList.remove('mesh-drop');
            importCustomObjectFile(file);
        });
    }

    return { renderAll, updateMetrics, toggleDrawer, timeline, state: uiMemory, importCustomObjectFile };
}
