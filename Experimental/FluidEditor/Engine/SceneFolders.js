//============================================================================================================================================
//                                                          SCENEFOLDERS.JS
//============================================================================================================================================
// 🧩 Project-Zero Outliner Folder Hierarchy & Simulation/Global Scoping:
//    Each simulation (e.g. Explosion, Campfire) owns its own Simulation Folder with local Gas Domain, Force Fields, Sources,
//    Particle Clouds, Colliders, and Overlays, while any entity promoted to Global moves into the top-level Global folder
//    and shares across simulations without polluting scene fingerprints or flat particle schemas.

import { BoundaryModes, FalloffLabels, ForceTypeById, isKeyed } from './ForceGeneratorCatalog.js';
import { ParticleTemplateById, ParticleTemplates } from './ParticleCatalog.js';
import { SourceLayerCode, evaluateGasStack, applyGasEvaluation } from './LayerStack.js';

const BoundaryShort = { ClosedBox: 'Closed Box', OpenTop: 'Open Top', OpenAll: 'Open All' };

export const GlobalFolderId = 'folder-global';

export const CollectionTypeLabels = {
    Folder: 'Folders',
    Domain: 'Gas Domains',
    Emitter: 'Sources',
    Force: 'Force Fields',
    Particles: 'Particle Clouds',
    Collider: 'Colliders',
    Visualizer: 'Overlays',
};

function slugify(text)
{
    return String(text || 'simulation').toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '') || 'simulation';
}

function cloneJson(input)
{
    return JSON.parse(JSON.stringify(input));
}

function visualizerKeyById(visualizers, id)
{
    for (const [key, entry] of Object.entries(visualizers || {}))
    {
        if (entry && entry.Id === id) return key;
    }
    return null;
}

function collectLeafList(scene)
{
    return [
        ...(scene.Emitters || []),
        ...(scene.Forces || []),
        ...(scene.Colliders || []),
        ...(scene.Particles || []),
    ];
}

//------------------------------------------------------------------------------------------------------------------------
//                                          DIRECTORY INITIALISATION & SYNC
//------------------------------------------------------------------------------------------------------------------------

export function ensureSceneDirectory(scene)
{
    if (!scene) return null;
    if (!scene.Directory)
    {
        const simId = `sim-${slugify(scene.PresetId || scene.Name)}-1`;
        const simulation = {
            Id: simId,
            Name: scene.Name || 'Simulation',
            PresetId: scene.PresetId || '',
            Notes: '',
            Tint: '#a2b87c',
            Domain: scene.Domain,
            GasStack: scene.GasStack,
            Visualizers: cloneJson(scene.Visualizers),
            LocalEmitters: [...(scene.Emitters || [])],
            LocalForces: [...(scene.Forces || [])],
            LocalColliders: [...(scene.Colliders || [])],
            LocalParticles: [...(scene.Particles || [])],
        };
        const assignments = {};
        for (const entry of collectLeafList(scene))
        {
            assignments[entry.Id] = { Scope: 'Local', SimulationId: simId, CustomFolderId: null };
        }
        const overlayScope = {};
        for (const overlay of Object.values(scene.Visualizers || {}))
        {
            overlayScope[overlay.Id] = 'Local';
        }
        scene.Directory = {
            ActiveSimulationId: simId,
            Counter: 2,
            DomainScope: 'Local',
            GlobalDomain: null,
            GlobalGasStack: null,
            GlobalEmitters: [],
            GlobalForces: [],
            GlobalColliders: [],
            GlobalParticles: [],
            GlobalVisualizers: {},
            OverlayScope: overlayScope,
            Simulations: [simulation],
            CustomFolders: [],
            Assignments: assignments,
            Hidden: {},
            MutedRestore: {},
        };
    }
    return scene.Directory;
}

/** Keeps the active simulation record and Assignments synchronized after standard app edits (addForce, removeNode, rename, etc.). */
export function syncSceneDirectory(scene)
{
    const dir = scene && scene.Directory;
    if (!dir) return;
    const activeSim = dir.Simulations.find((candidate) => candidate.Id === dir.ActiveSimulationId) || dir.Simulations[0];
    if (!activeSim) return;
    dir.ActiveSimulationId = activeSim.Id;

    // Keep single-sim name synced with scene.Name if user didn't custom-rename multiple sims
    if (dir.Simulations.length === 1 && scene.Name && activeSim.PresetId === scene.PresetId && !activeSim.CustomRenamed)
    {
        activeSim.Name = scene.Name;
    }

    if (dir.DomainScope === 'Global')
    {
        dir.GlobalDomain = scene.Domain;
        dir.GlobalGasStack = scene.GasStack;
    }
    else
    {
        activeSim.Domain = scene.Domain;
        activeSim.GasStack = scene.GasStack;
    }

    // Sync Overlay local/global snapshots from live scene.Visualizers
    for (const [visKey, liveVis] of Object.entries(scene.Visualizers || {}))
    {
        const scope = dir.OverlayScope[liveVis.Id] || 'Local';
        dir.OverlayScope[liveVis.Id] = scope;
        if (scope === 'Global') dir.GlobalVisualizers[visKey] = cloneJson(liveVis);
        else activeSim.Visualizers[visKey] = cloneJson(liveVis);
    }

    // Partition live scene arrays back into Global vs owning Simulation buckets
    const liveIds = new Set();
    const bucketPairs = [
        ['Emitters', 'LocalEmitters', 'GlobalEmitters'],
        ['Forces', 'LocalForces', 'GlobalForces'],
        ['Colliders', 'LocalColliders', 'GlobalColliders'],
        ['Particles', 'LocalParticles', 'GlobalParticles'],
    ];

    for (const [sceneKey, localKey, globalKey] of bucketPairs)
    {
        const list = scene[sceneKey] || [];
        for (const item of list)
        {
            liveIds.add(item.Id);
            if (!dir.Assignments[item.Id])
            {
                dir.Assignments[item.Id] = { Scope: 'Local', SimulationId: activeSim.Id, CustomFolderId: null };
            }
        }
        dir[globalKey] = list.filter((item) => dir.Assignments[item.Id]?.Scope === 'Global');
        if (dir.DomainScope === 'Global')
        {
            for (const sim of dir.Simulations)
            {
                sim[localKey] = list.filter((item) =>
                    dir.Assignments[item.Id]?.Scope !== 'Global' && dir.Assignments[item.Id]?.SimulationId === sim.Id);
            }
        }
        else
        {
            activeSim[localKey] = list.filter((item) =>
                dir.Assignments[item.Id]?.Scope !== 'Global' && (dir.Assignments[item.Id]?.SimulationId || activeSim.Id) === activeSim.Id);
        }
    }

    // Prune deleted IDs that belonged to the active context
    const inactiveProtectedIds = new Set();
    if (dir.DomainScope !== 'Global')
    {
        for (const sim of dir.Simulations)
        {
            if (sim.Id === activeSim.Id) continue;
            for (const key of ['LocalEmitters', 'LocalForces', 'LocalColliders', 'LocalParticles'])
            {
                for (const item of sim[key] || []) inactiveProtectedIds.add(item.Id);
            }
        }
    }
    for (const existingId of Object.keys(dir.Assignments))
    {
        if (!liveIds.has(existingId) && !inactiveProtectedIds.has(existingId))
        {
            delete dir.Assignments[existingId];
        }
    }
}

/** Rebuilds live scene.{Domain,GasStack,Emitters,Forces,Colliders,Particles,Visualizers} from Directory scopes. */
export function recomposeSceneFromDirectory(scene)
{
    const dir = ensureSceneDirectory(scene);
    const activeSim = dir.Simulations.find((candidate) => candidate.Id === dir.ActiveSimulationId) || dir.Simulations[0];
    if (!activeSim) return false;

    const prevDomain = scene.Domain;
    if (dir.DomainScope === 'Global')
    {
        if (!dir.GlobalDomain)
        {
            dir.GlobalDomain = activeSim.Domain;
            dir.GlobalGasStack = activeSim.GasStack;
        }
        scene.Domain = dir.GlobalDomain;
        scene.GasStack = dir.GlobalGasStack;
        scene.Emitters = [
            ...dir.GlobalEmitters,
            ...dir.Simulations.flatMap((sim) => sim.LocalEmitters || []),
        ];
        scene.Forces = [
            ...dir.GlobalForces,
            ...dir.Simulations.flatMap((sim) => sim.LocalForces || []),
        ];
        scene.Colliders = [
            ...dir.GlobalColliders,
            ...dir.Simulations.flatMap((sim) => sim.LocalColliders || []),
        ];
        scene.Particles = [
            ...dir.GlobalParticles,
            ...dir.Simulations.flatMap((sim) => sim.LocalParticles || []),
        ];
    }
    else
    {
        scene.Domain = activeSim.Domain;
        scene.GasStack = activeSim.GasStack;
        scene.Emitters = [...dir.GlobalEmitters, ...(activeSim.LocalEmitters || [])];
        scene.Forces = [...dir.GlobalForces, ...(activeSim.LocalForces || [])];
        scene.Colliders = [...dir.GlobalColliders, ...(activeSim.LocalColliders || [])];
        scene.Particles = [...dir.GlobalParticles, ...(activeSim.LocalParticles || [])];
    }

    // Ensure GasStack Source layers match all live emitters in scene.Emitters
    if (scene.GasStack)
    {
        const emitterIds = new Set(scene.Emitters.map((emitter) => emitter.Id));
        const linkedIds = new Set(
            scene.GasStack.Layers
                .filter((layer) => layer.Category === SourceLayerCode)
                .map((layer) => layer.Parameters.Emitter)
        );
        for (const emitter of scene.Emitters)
        {
            if (!linkedIds.has(emitter.Id))
            {
                scene.GasStack.Layers.splice(1, 0, {
                    Token: `layer-src-${emitter.Id}`,
                    Category: SourceLayerCode,
                    Enabled: emitter.Enabled !== false,
                    Solo: false,
                    Weight: 1,
                    Blend: 0,
                    Parameters: { Emitter: emitter.Id },
                });
            }
        }
        scene.GasStack.Layers = scene.GasStack.Layers.filter(
            (layer) => layer.Category !== SourceLayerCode || emitterIds.has(layer.Parameters.Emitter)
        );
        applyGasEvaluation(scene, evaluateGasStack(scene.GasStack, scene.Emitters, 0));
    }

    // Merge active simulation's local Visualizers with Global Visualizers
    for (const [visKey, liveVis] of Object.entries(scene.Visualizers || {}))
    {
        const scope = dir.OverlayScope[liveVis.Id] || 'Local';
        const source = scope === 'Global'
            ? (dir.GlobalVisualizers[visKey] || activeSim.Visualizers[visKey])
            : (activeSim.Visualizers[visKey] || liveVis);
        if (source) Object.assign(liveVis, cloneJson(source));
    }

    return prevDomain !== scene.Domain;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          SIMULATION & SCOPE OPERATIONS
//------------------------------------------------------------------------------------------------------------------------

export function appendImportedSimulation(scene, importedScene)
{
    const dir = ensureSceneDirectory(scene);
    syncSceneDirectory(scene);

    const existingNames = new Set(dir.Simulations.map((sim) => sim.Name));
    let folderName = importedScene.Name || 'Simulation';
    let suffix = 2;
    while (existingNames.has(folderName)) folderName = `${importedScene.Name} ${suffix++}`;

    const simId = `sim-${slugify(folderName)}-${dir.Counter++}`;
    const idMap = new Map();
    const rekey = (prefix, list) => (list || []).map((entry, idx) =>
    {
        const freshId = `${prefix}-${dir.Counter++}${idx.toString(36)}`;
        idMap.set(entry.Id, freshId);
        const cloned = { ...entry, Id: freshId };
        dir.Assignments[freshId] = { Scope: 'Local', SimulationId: simId, CustomFolderId: null };
        return cloned;
    });

    const localEmitters = rekey('emitter', importedScene.Emitters);
    const localForces = rekey('force', importedScene.Forces);
    const localColliders = rekey('collider', importedScene.Colliders);
    const localParticles = rekey('particles', importedScene.Particles);

    if (importedScene.GasStack)
    {
        for (const layer of importedScene.GasStack.Layers)
        {
            if (layer.Category === SourceLayerCode && idMap.has(layer.Parameters.Emitter))
            {
                layer.Parameters.Emitter = idMap.get(layer.Parameters.Emitter);
            }
        }
    }

    const simRecord = {
        Id: simId,
        Name: folderName,
        PresetId: importedScene.PresetId || '',
        Notes: '',
        Tint: '#a2b87c',
        Domain: importedScene.Domain,
        GasStack: importedScene.GasStack,
        Visualizers: cloneJson(importedScene.Visualizers),
        LocalEmitters: localEmitters,
        LocalForces: localForces,
        LocalColliders: localColliders,
        LocalParticles: localParticles,
    };

    dir.Simulations.push(simRecord);
    dir.ActiveSimulationId = simId;
    recomposeSceneFromDirectory(scene);
    return simRecord;
}

export function activateSimulationFolder(scene, simulationId)
{
    const dir = ensureSceneDirectory(scene);
    if (!dir.Simulations.some((sim) => sim.Id === simulationId)) return false;
    if (dir.ActiveSimulationId === simulationId) return false;
    syncSceneDirectory(scene);
    dir.ActiveSimulationId = simulationId;
    return recomposeSceneFromDirectory(scene);
}

export function entityScopeInfo(scene, entityId)
{
    const dir = ensureSceneDirectory(scene);
    const activeSim = dir.Simulations.find((sim) => sim.Id === dir.ActiveSimulationId) || dir.Simulations[0];
    if (entityId === 'domain')
    {
        return {
            Scope: dir.DomainScope,
            SimulationId: activeSim ? activeSim.Id : null,
            SimulationName: activeSim ? activeSim.Name : scene.Name,
            CustomFolderId: null,
        };
    }
    if (String(entityId).startsWith('vis-'))
    {
        return {
            Scope: dir.OverlayScope[entityId] || 'Local',
            SimulationId: activeSim ? activeSim.Id : null,
            SimulationName: activeSim ? activeSim.Name : scene.Name,
            CustomFolderId: null,
        };
    }
    const assignment = dir.Assignments[entityId] || { Scope: 'Local', SimulationId: activeSim?.Id, CustomFolderId: null };
    const ownerSim = dir.Simulations.find((sim) => sim.Id === assignment.SimulationId) || activeSim;
    return {
        Scope: assignment.Scope || 'Local',
        SimulationId: ownerSim ? ownerSim.Id : null,
        SimulationName: ownerSim ? ownerSim.Name : scene.Name,
        CustomFolderId: assignment.CustomFolderId || null,
    };
}

export function assignEntityScope(scene, entityId, nextScope, targetSimulationId = null)
{
    const dir = ensureSceneDirectory(scene);
    syncSceneDirectory(scene);
    const activeSim = dir.Simulations.find((sim) => sim.Id === dir.ActiveSimulationId) || dir.Simulations[0];
    const destSim = dir.Simulations.find((sim) => sim.Id === (targetSimulationId || activeSim?.Id)) || activeSim;

    if (entityId === 'domain')
    {
        if (nextScope === 'Global' && dir.DomainScope !== 'Global')
        {
            dir.GlobalDomain = scene.Domain;
            dir.GlobalGasStack = scene.GasStack;
            dir.DomainScope = 'Global';
        }
        else if (nextScope === 'Local' && dir.DomainScope === 'Global')
        {
            if (destSim)
            {
                destSim.Domain = cloneJson(dir.GlobalDomain || scene.Domain);
                destSim.GasStack = cloneJson(dir.GlobalGasStack || scene.GasStack);
                dir.ActiveSimulationId = destSim.Id;
            }
            dir.DomainScope = 'Local';
        }
        return recomposeSceneFromDirectory(scene);
    }

    if (String(entityId).startsWith('vis-'))
    {
        const visKey = visualizerKeyById(scene.Visualizers, entityId);
        dir.OverlayScope[entityId] = nextScope;
        if (visKey)
        {
            if (nextScope === 'Global') dir.GlobalVisualizers[visKey] = cloneJson(scene.Visualizers[visKey]);
            else if (destSim) destSim.Visualizers[visKey] = cloneJson(scene.Visualizers[visKey]);
        }
        return recomposeSceneFromDirectory(scene);
    }

    const current = dir.Assignments[entityId] || { Scope: 'Local', SimulationId: destSim?.Id, CustomFolderId: null };
    // Move entity across simulation buckets if transferring to another Simulation Folder
    if (nextScope === 'Local' && destSim && current.SimulationId !== destSim.Id)
    {
        for (const sim of dir.Simulations)
        {
            for (const key of ['LocalEmitters', 'LocalForces', 'LocalColliders', 'LocalParticles'])
            {
                const foundIdx = (sim[key] || []).findIndex((entry) => entry.Id === entityId);
                if (foundIdx >= 0 && sim.Id !== destSim.Id)
                {
                    const [moved] = sim[key].splice(foundIdx, 1);
                    destSim[key].push(moved);
                }
            }
        }
    }
    // Also handle moving from Global back to Local simulation bucket
    if (nextScope === 'Local' && current.Scope === 'Global' && destSim)
    {
        for (const [globalKey, localKey] of [
            ['GlobalEmitters', 'LocalEmitters'],
            ['GlobalForces', 'LocalForces'],
            ['GlobalColliders', 'LocalColliders'],
            ['GlobalParticles', 'LocalParticles'],
        ])
        {
            const idx = dir[globalKey].findIndex((entry) => entry.Id === entityId);
            if (idx >= 0)
            {
                const [moved] = dir[globalKey].splice(idx, 1);
                if (!destSim[localKey].some((item) => item.Id === entityId)) destSim[localKey].push(moved);
            }
        }
    }

    dir.Assignments[entityId] = {
        Scope: nextScope,
        SimulationId: destSim ? destSim.Id : current.SimulationId,
        CustomFolderId: nextScope === 'Global' ? null : current.CustomFolderId,
    };
    return recomposeSceneFromDirectory(scene);
}

export function createCollectionFolder(scene, parentFolderId = null, baseName = 'Collection')
{
    const dir = ensureSceneDirectory(scene);
    const existing = new Set([
        ...dir.Simulations.map((sim) => sim.Name),
        ...dir.CustomFolders.map((folder) => folder.Name),
    ]);
    let name = baseName;
    let suffix = 2;
    while (existing.has(name)) name = `${baseName} ${suffix++}`;
    const parent = parentFolderId || dir.ActiveSimulationId;
    const created = {
        Id: `group-${dir.Counter++}`,
        Name: name,
        ParentId: parent,
        Notes: '',
        Tint: '#9bacb4',
    };
    dir.CustomFolders.push(created);
    return created;
}

export function moveEntityToFolder(scene, entityId, folderId)
{
    const dir = ensureSceneDirectory(scene);
    if (!folderId) return false;
    if (folderId === GlobalFolderId || folderId.startsWith('folder-global'))
    {
        return assignEntityScope(scene, entityId, 'Global');
    }
    const simMatch = dir.Simulations.find((sim) => sim.Id === folderId || folderId.endsWith(`::${sim.Id}`));
    if (simMatch)
    {
        const changed = assignEntityScope(scene, entityId, 'Local', simMatch.Id);
        if (dir.Assignments[entityId]) dir.Assignments[entityId].CustomFolderId = null;
        return changed;
    }
    const custom = dir.CustomFolders.find((folder) => folder.Id === folderId);
    if (custom && dir.Assignments[entityId])
    {
        const ownerSim = dir.Simulations.find((sim) => sim.Id === custom.ParentId) || dir.Simulations[0];
        const changed = assignEntityScope(scene, entityId, 'Local', ownerSim?.Id);
        dir.Assignments[entityId].CustomFolderId = custom.Id;
        return changed;
    }
    return false;
}

export function renameFolder(scene, folderId, nextName)
{
    const trimmed = String(nextName || '').trim();
    if (!trimmed) return false;
    const dir = ensureSceneDirectory(scene);
    const sim = dir.Simulations.find((candidate) => candidate.Id === folderId);
    if (sim)
    {
        sim.Name = trimmed;
        sim.CustomRenamed = true;
        if (dir.Simulations.length === 1) scene.Name = trimmed;
        return true;
    }
    const custom = dir.CustomFolders.find((candidate) => candidate.Id === folderId);
    if (custom)
    {
        custom.Name = trimmed;
        return true;
    }
    return false;
}

export function deleteFolder(scene, folderId)
{
    const dir = ensureSceneDirectory(scene);
    syncSceneDirectory(scene);

    // 1. Custom user collection folder: ungroup children back to their simulation/global root and remove folder
    const customIdx = dir.CustomFolders.findIndex((folder) => folder.Id === folderId);
    if (customIdx >= 0)
    {
        const removed = dir.CustomFolders[customIdx];
        dir.CustomFolders.splice(customIdx, 1);
        for (const record of Object.values(dir.Assignments))
        {
            if (record.CustomFolderId === removed.Id) record.CustomFolderId = null;
        }
        delete dir.Hidden[removed.Id];
        recomposeSceneFromDirectory(scene);
        return true;
    }

    // 2. Built-in category folders (e.g. "Particle Clouds", "Force Fields", "Sources & Colliders"): delete items inside that category
    if (typeof folderId === 'string' && folderId.startsWith('folder-'))
    {
        const dropIds = (items) =>
        {
            for (const item of items || []) delete dir.Assignments[item.Id];
        };
        if (folderId === 'folder-global::particles' && dir.GlobalParticles.length > 0)
        {
            dropIds(dir.GlobalParticles);
            dir.GlobalParticles = [];
            delete dir.Hidden[folderId];
            recomposeSceneFromDirectory(scene);
            return true;
        }
        if (folderId === 'folder-global::forces' && dir.GlobalForces.length > 0)
        {
            dropIds(dir.GlobalForces);
            dir.GlobalForces = [];
            delete dir.Hidden[folderId];
            recomposeSceneFromDirectory(scene);
            return true;
        }
        if (folderId === 'folder-global::sources' && (dir.GlobalEmitters.length + dir.GlobalColliders.length) > 0)
        {
            const emitterIds = new Set(dir.GlobalEmitters.map((emitter) => emitter.Id));
            dropIds(dir.GlobalEmitters);
            dropIds(dir.GlobalColliders);
            dir.GlobalEmitters = [];
            dir.GlobalColliders = [];
            for (const sim of dir.Simulations)
            {
                if (sim.GasStack) sim.GasStack.Layers = sim.GasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && emitterIds.has(layer.Parameters.Emitter)));
            }
            if (dir.GlobalGasStack) dir.GlobalGasStack.Layers = dir.GlobalGasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && emitterIds.has(layer.Parameters.Emitter)));
            delete dir.Hidden[folderId];
            recomposeSceneFromDirectory(scene);
            return true;
        }
        const [prefix, simId] = folderId.split('::');
        const sim = simId && dir.Simulations.find((candidate) => candidate.Id === simId);
        if (sim)
        {
            if (prefix === 'folder-particles' && (sim.LocalParticles || []).length > 0)
            {
                const remaining = (sim.LocalParticles || []).filter((item) => Boolean(dir.Assignments[item.Id]?.CustomFolderId));
                const removed = (sim.LocalParticles || []).filter((item) => !dir.Assignments[item.Id]?.CustomFolderId);
                if (removed.length > 0)
                {
                    dropIds(removed);
                    sim.LocalParticles = remaining;
                    delete dir.Hidden[folderId];
                    recomposeSceneFromDirectory(scene);
                    return true;
                }
            }
            if (prefix === 'folder-forces' && (sim.LocalForces || []).length > 0)
            {
                const remaining = (sim.LocalForces || []).filter((item) => Boolean(dir.Assignments[item.Id]?.CustomFolderId));
                const removed = (sim.LocalForces || []).filter((item) => !dir.Assignments[item.Id]?.CustomFolderId);
                if (removed.length > 0)
                {
                    dropIds(removed);
                    sim.LocalForces = remaining;
                    delete dir.Hidden[folderId];
                    recomposeSceneFromDirectory(scene);
                    return true;
                }
            }
            if (prefix === 'folder-sources' && ((sim.LocalEmitters || []).length + (sim.LocalColliders || []).length) > 0)
            {
                const remEmit = (sim.LocalEmitters || []).filter((item) => !dir.Assignments[item.Id]?.CustomFolderId);
                const remCol = (sim.LocalColliders || []).filter((item) => !dir.Assignments[item.Id]?.CustomFolderId);
                if (remEmit.length + remCol.length > 0)
                {
                    const emitterIds = new Set(remEmit.map((emitter) => emitter.Id));
                    dropIds(remEmit);
                    dropIds(remCol);
                    sim.LocalEmitters = (sim.LocalEmitters || []).filter((item) => Boolean(dir.Assignments[item.Id]?.CustomFolderId));
                    sim.LocalColliders = (sim.LocalColliders || []).filter((item) => Boolean(dir.Assignments[item.Id]?.CustomFolderId));
                    if (sim.GasStack) sim.GasStack.Layers = sim.GasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && emitterIds.has(layer.Parameters.Emitter)));
                    if (dir.GlobalGasStack) dir.GlobalGasStack.Layers = dir.GlobalGasStack.Layers.filter((layer) => !(layer.Category === SourceLayerCode && emitterIds.has(layer.Parameters.Emitter)));
                    delete dir.Hidden[folderId];
                    recomposeSceneFromDirectory(scene);
                    return true;
                }
            }
        }
    }

    // 3. Secondary simulation folder when > 1 simulation exists
    if (dir.Simulations.length > 1)
    {
        const simIdx = dir.Simulations.findIndex((sim) => sim.Id === folderId);
        if (simIdx >= 0)
        {
            const [removedSim] = dir.Simulations.splice(simIdx, 1);
            for (const [id, record] of Object.entries(dir.Assignments))
            {
                if (record.Scope !== 'Global' && record.SimulationId === removedSim.Id) delete dir.Assignments[id];
            }
            delete dir.Hidden[removedSim.Id];
            if (dir.ActiveSimulationId === removedSim.Id) dir.ActiveSimulationId = dir.Simulations[0].Id;
            recomposeSceneFromDirectory(scene);
            return true;
        }
    }
    return false;
}

//------------------------------------------------------------------------------------------------------------------------
//                                          HIERARCHY & INVENTORY BUILDER
//------------------------------------------------------------------------------------------------------------------------

function describeEntityContext(entry)
{
    const keyCount = (entry.Tracks || []).reduce((sum, track) => sum + track.Keys.length, 0)
        + ((entry.Stack && entry.Stack.Tracks) || []).reduce((sum, track) => sum + track.Keys.length, 0);
    const keyMark = keyCount ? ` · ◆${keyCount}` : isKeyed(entry) ? ' · env' : '';
    switch (entry.Kind)
    {
        case 'Domain':
            return entry.Grid === 'Infinite'
                ? `Infinite · ${entry.TileSize} m tiles`
                : `${entry.Resolution.join('×')} · ${BoundaryShort[entry.Boundary] || entry.Boundary}`;
        case 'Force':
            return `${ Number(entry.Strength).toFixed(1).replace(/\.0$/, '') } m/s² · ${FalloffLabels[entry.Falloff] || entry.Falloff}${keyMark}`;
        case 'Emitter':
            return `${entry.Shape} · r ${Number(entry.Radius).toFixed(2)} m${keyMark}`;
        case 'Collider':
            return `Sphere · r ${Number(entry.Radius).toFixed(2)} m`;
        case 'Particles':
            return `${entry.Mode} · ${entry.Count.toLocaleString()} pts${keyMark}`;
        default:
            return entry.Ramp ? `${entry.Ramp} ramp` : 'Viewport overlay';
    }
}

function entityGlyph(entry)
{
    if (entry.Kind === 'Particles') return (ParticleTemplateById[entry.Template] || ParticleTemplates[0]).Icon;
    if (entry.Kind === 'Force') return ForceTypeById[entry.Type].Icon;
    if (entry.Kind === 'Emitter') return entry.Fuel > 0 ? 'flame' : 'smoke';
    if (entry.Kind === 'Collider') return 'collider';
    if (entry.Kind === 'Domain') return 'domain';
    return { 'vis-volume': 'volume', 'vis-vectors': 'vectors', 'vis-slice': 'slice', 'vis-tracers': 'tracers', 'vis-forces': 'dburst', 'vis-debris': 'debris' }[entry.Id] || 'gizmo';
}

function entityAccent(entry)
{
    if (entry.Kind === 'Force') return ForceTypeById[entry.Type].Tint;
    if (entry.Kind === 'Emitter') return entry.Fuel > 0 ? '#f97316' : '#cbd5e1';
    if (entry.Kind === 'Collider') return '#94a3b8';
    if (entry.Kind === 'Particles') return (ParticleTemplateById[entry.Template] || ParticleTemplates[0]).Tint;
    if (entry.Kind === 'Domain') return '#e2e8f0';
    return '#38bdf8';
}

/** Builds the complete un-collapsed Outliner row list with Parent links (Project Zero Editor Row model). */
export function buildSceneRows(scene)
{
    const dir = ensureSceneDirectory(scene);
    syncSceneDirectory(scene);
    const rows = [];

    const pushFolder = (id, name, iconName, parentId, description, extra = {}) =>
    {
        const row = {
            Id: id,
            Name: name,
            Panel: 'Folder',
            Icon: iconName,
            Tint: '#d4d4d8',
            Parent: parentId,
            Description: description,
            IsFolder: true,
            Enabled: !dir.Hidden[id],
            Visible: !dir.Hidden[id],
            ...extra,
        };
        rows.push(row);
        return row;
    };

    const pushEntity = (entry, parentId, scope, simId, rowIdOverride = null) =>
    {
        rows.push({
            Id: rowIdOverride || entry.Id,
            UnderlyingId: entry.Id,
            Name: entry.Name,
            Panel: entry.Kind || 'Visualizer',
            Icon: entityGlyph(entry),
            Tint: entityAccent(entry),
            Parent: parentId,
            Description: describeEntityContext(entry),
            Metadata: describeEntityContext(entry),
            IsFolder: false,
            Scope: scope,
            SimulationId: simId,
            Enabled: entry.Enabled !== false,
            Visible: entry.Visible !== false,
            Entry: entry,
        });
    };

    // 1. Global Folder (only materialized when at least one Global entity or custom folder under Global exists)
    const diagnosticOverlayIds = new Set(['vis-vectors', 'vis-slice', 'vis-tracers', 'vis-forces', 'vis-gizmos']);
    const globalOverlays = Object.values(scene.Visualizers || {}).filter(
        (vis) => vis && diagnosticOverlayIds.has(vis.Id) && vis.Visible && dir.OverlayScope[vis.Id] === 'Global'
    );
    const customGlobal = dir.CustomFolders.filter((folder) => folder.ParentId === GlobalFolderId);
    const hasGlobal = dir.DomainScope === 'Global'
        || dir.GlobalEmitters.length > 0
        || dir.GlobalForces.length > 0
        || dir.GlobalColliders.length > 0
        || dir.GlobalParticles.length > 0
        || globalOverlays.length > 0
        || customGlobal.length > 0;

    if (hasGlobal)
    {
        pushFolder(GlobalFolderId, 'Global', 'folderWorld', null, 'Shared across all simulation folders in the scene', { Scope: 'Global' });
        if (dir.DomainScope === 'Global')
        {
            pushEntity(scene.Domain, GlobalFolderId, 'Global', null, 'domain');
            for (const emitter of dir.GlobalEmitters) pushEntity(emitter, 'domain', 'Global', null);
            for (const collider of dir.GlobalColliders) pushEntity(collider, 'domain', 'Global', null);
        }
        else if (dir.GlobalEmitters.length + dir.GlobalColliders.length > 0)
        {
            const srcFolder = 'folder-global::sources';
            pushFolder(srcFolder, 'Sources & Colliders', 'folderGeneric', GlobalFolderId, 'Global gas sources and solid obstacles', { Scope: 'Global' });
            for (const emitter of dir.GlobalEmitters) pushEntity(emitter, srcFolder, 'Global', null);
            for (const collider of dir.GlobalColliders) pushEntity(collider, srcFolder, 'Global', null);
        }
        if (dir.GlobalForces.length > 0)
        {
            const forceFolder = 'folder-global::forces';
            pushFolder(forceFolder, 'Force Fields', 'folderGeneric', GlobalFolderId, 'Global force fields affecting every simulation', { Scope: 'Global' });
            for (const force of dir.GlobalForces) pushEntity(force, forceFolder, 'Global', null);
        }
        if (dir.GlobalParticles.length > 0)
        {
            const partFolder = 'folder-global::particles';
            pushFolder(partFolder, 'Particle Clouds', 'folderGeneric', GlobalFolderId, 'Global particle clouds', { Scope: 'Global' });
            for (const cloud of dir.GlobalParticles) pushEntity(cloud, partFolder, 'Global', null);
        }
        if (globalOverlays.length > 0)
        {
            const ovFolder = 'folder-global::overlays';
            pushFolder(ovFolder, 'Overlays', 'folderGeneric', GlobalFolderId, 'Global viewport overlays shared across simulations', { Scope: 'Global', DefaultCollapsed: true });
            for (const vis of globalOverlays) pushEntity(vis, ovFolder, 'Global', null);
        }
        for (const custom of customGlobal)
        {
            pushFolder(custom.Id, custom.Name, 'folderGeneric', GlobalFolderId, 'Custom collection', { Scope: 'Global', Custom: true });
        }
    }

    // 2. Per-Simulation Folders (e.g. Explosion, Campfire)
    const multiSim = dir.Simulations.length > 1;
    for (const sim of dir.Simulations)
    {
        const isActiveSim = sim.Id === dir.ActiveSimulationId;
        const simDesc = multiSim
            ? (isActiveSim ? 'Active simulation folder' : 'Simulation folder · click to activate')
            : 'Simulation folder';
        pushFolder(sim.Id, sim.Name, 'folderScene', null, simDesc, {
            Scope: 'Local',
            SimulationId: sim.Id,
            IsSimulation: true,
            IsActiveSimulation: isActiveSim,
        });

        // Custom user collections inside this Simulation Folder
        const simCustomFolders = dir.CustomFolders.filter((folder) => folder.ParentId === sim.Id);
        const customFolderIds = new Set(simCustomFolders.map((folder) => folder.Id));
        for (const custom of simCustomFolders)
        {
            pushFolder(custom.Id, custom.Name, 'folderGeneric', sim.Id, 'Custom simulation collection', {
                Scope: 'Local',
                SimulationId: sim.Id,
                Custom: true,
            });
        }

        const customParentOf = (entry) =>
        {
            const assigned = dir.Assignments[entry.Id]?.CustomFolderId;
            return assigned && customFolderIds.has(assigned) ? assigned : null;
        };

        const emitters = sim.LocalEmitters || [];
        const colliders = sim.LocalColliders || [];
        const forces = sim.LocalForces || [];
        const particles = sim.LocalParticles || [];

        // Local Gas Domain (omitted inside this folder if Gas Domain was promoted to Global!)
        if (dir.DomainScope !== 'Global')
        {
            const domainRowId = isActiveSim ? 'domain' : `domain::${sim.Id}`;
            const domainObj = isActiveSim ? scene.Domain : sim.Domain;
            pushEntity(domainObj, sim.Id, 'Local', sim.Id, domainRowId);
            for (const emitter of emitters) pushEntity(emitter, customParentOf(emitter) || domainRowId, 'Local', sim.Id);
            for (const collider of colliders) pushEntity(collider, customParentOf(collider) || domainRowId, 'Local', sim.Id);
        }
        else if (emitters.length + colliders.length > 0)
        {
            const srcFolder = `folder-sources::${sim.Id}`;
            const uncustomCount = [...emitters, ...colliders].filter((entry) => !customParentOf(entry)).length;
            if (uncustomCount > 0)
            {
                pushFolder(srcFolder, 'Sources & Colliders', 'folderGeneric', sim.Id, `${sim.Name} local emitters and obstacles`, {
                    Scope: 'Local',
                    SimulationId: sim.Id,
                });
            }
            for (const emitter of emitters) pushEntity(emitter, customParentOf(emitter) || srcFolder, 'Local', sim.Id);
            for (const collider of colliders) pushEntity(collider, customParentOf(collider) || srcFolder, 'Local', sim.Id);
        }

        // Local Force Fields folder (omitted when 0 local forces!)
        const directForces = forces.filter((force) => !customParentOf(force));
        const customForces = forces.filter((force) => customParentOf(force));
        for (const force of customForces) pushEntity(force, customParentOf(force), 'Local', sim.Id);
        if (directForces.length > 0)
        {
            const forceFolder = `folder-forces::${sim.Id}`;
            pushFolder(forceFolder, 'Force Fields', 'folderGeneric', sim.Id, `${sim.Name} local force generators`, {
                Scope: 'Local',
                SimulationId: sim.Id,
            });
            for (const force of directForces) pushEntity(force, forceFolder, 'Local', sim.Id);
        }

        // Local Particle Clouds folder (omitted when 0 local particle clouds!)
        const directParticles = particles.filter((cloud) => !customParentOf(cloud));
        const customParticles = particles.filter((cloud) => customParentOf(cloud));
        for (const cloud of customParticles) pushEntity(cloud, customParentOf(cloud), 'Local', sim.Id);
        if (directParticles.length > 0)
        {
            const partFolder = `folder-particles::${sim.Id}`;
            pushFolder(partFolder, 'Particle Clouds', 'folderGeneric', sim.Id, `${sim.Name} local particle clouds`, {
                Scope: 'Local',
                SimulationId: sim.Id,
            });
            for (const cloud of directParticles) pushEntity(cloud, partFolder, 'Local', sim.Id);
        }

        // Local Overlays folder (omitted by default until the user explicitly enables a diagnostic overlay!)
        const visSource = isActiveSim ? scene.Visualizers : sim.Visualizers;
        const localOverlays = Object.values(visSource || {}).filter(
            (vis) => vis && diagnosticOverlayIds.has(vis.Id) && vis.Visible && dir.OverlayScope[vis.Id] !== 'Global'
        );
        if (localOverlays.length > 0)
        {
            const ovFolder = `folder-overlays::${sim.Id}`;
            pushFolder(ovFolder, 'Overlays', 'folderGeneric', sim.Id, `${sim.Name} local diagnostic & render overlays`, {
                Scope: 'Local',
                SimulationId: sim.Id,
                DefaultCollapsed: true,
            });
            for (const vis of localOverlays)
            {
                const ovRowId = isActiveSim ? vis.Id : `${vis.Id}::${sim.Id}`;
                pushEntity(vis, ovFolder, 'Local', sim.Id, ovRowId);
            }
        }
    }

    // Populate child counts & metadata on folders + ancestor visibility inheritance
    const childrenMap = new Map();
    for (const row of rows)
    {
        if (!childrenMap.has(row.Parent)) childrenMap.set(row.Parent, []);
        childrenMap.get(row.Parent).push(row);
    }

    const leafCountUnder = (folderId) =>
    {
        let total = 0;
        const stack = [...(childrenMap.get(folderId) || [])];
        while (stack.length)
        {
            const cur = stack.pop();
            if (!cur.IsFolder) total += 1;
            const kids = childrenMap.get(cur.Id) || [];
            for (const kid of kids) stack.push(kid);
        }
        return total;
    };

    const byId = new Map(rows.map((row) => [row.Id, row]));
    for (const row of rows)
    {
        row.HasChildren = (childrenMap.get(row.Id) || []).length > 0;
        if (row.IsFolder)
        {
            const count = leafCountUnder(row.Id);
            const activeBadge = row.IsSimulation && multiSim && row.IsActiveSimulation ? 'Active · ' : '';
            row.Metadata = `${activeBadge}${count} ${count === 1 ? 'item' : 'items'}`;
        }
        let cursor = byId.get(row.Parent);
        let ancestorHidden = false;
        while (cursor)
        {
            if (!cursor.Visible || dir.Hidden[cursor.Id]) ancestorHidden = true;
            cursor = byId.get(cursor.Parent);
        }
        row.InheritedHidden = ancestorHidden;
    }

    return { rows, childrenMap, byId };
}

/** Iterative FolderInventory matching Experimental/ProjectZeroEditor/FolderInventory.mjs. */
export function computeFolderInventory(scene, folderId)
{
    const { rows, childrenMap, byId } = buildSceneRows(scene);
    const subject = byId.get(folderId);
    if (!subject) return null;

    const trail = [];
    let cursor = subject;
    const seenAncestors = new Set();
    while (cursor && !seenAncestors.has(cursor.Id))
    {
        seenAncestors.add(cursor.Id);
        trail.unshift(cursor);
        cursor = byId.get(cursor.Parent);
    }

    const entries = [];
    const stack = [...(childrenMap.get(folderId) || [])]
        .reverse()
        .map((row) => ({ Row: row, Depth: 1, Inherited: !subject.Visible }));

    while (stack.length)
    {
        const current = stack.pop();
        const visible = Boolean(current.Row.Visible && !current.Inherited);
        entries.push({ Row: current.Row, Depth: current.Depth, Visible: visible });
        const kids = childrenMap.get(current.Row.Id) || [];
        for (let i = kids.length - 1; i >= 0; i -= 1)
        {
            stack.push({ Row: kids[i], Depth: current.Depth + 1, Inherited: !visible });
        }
    }

    const leafEntries = entries.filter((item) => !item.Row.IsFolder);
    const typeCounts = new Map();
    for (const { Row } of leafEntries)
    {
        typeCounts.set(Row.Panel, (typeCounts.get(Row.Panel) || 0) + 1);
    }

    return {
        Subject: subject,
        Trail: trail,
        Entries: entries,
        LeafEntries: leafEntries,
        Types: [...typeCounts].sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0])),
        Direct: entries.filter((item) => item.Depth === 1).length,
        Folders: entries.filter((item) => item.Row.IsFolder).length,
        Visible: leafEntries.filter((item) => item.Visible).length,
        Depth: entries.reduce((max, item) => Math.max(max, item.Depth), 0),
    };
}

/** Toggles visibility/mute on a Folder, cascading cleanly to descendant entities in the solver & viewport. */
export function toggleFolderVisibility(scene, folderId)
{
    const dir = ensureSceneDirectory(scene);
    const inv = computeFolderInventory(scene, folderId);
    if (!inv) return false;

    const nextHidden = !dir.Hidden[folderId];
    dir.Hidden[folderId] = nextHidden;

    for (const { Row } of inv.LeafEntries)
    {
        const entry = Row.Entry;
        if (!entry || entry.Kind === 'Domain') continue;
        const key = `${folderId}::${entry.Id}`;
        if (nextHidden)
        {
            dir.MutedRestore[key] = { Enabled: entry.Enabled, Visible: entry.Visible };
            if ('Enabled' in entry) entry.Enabled = false;
            if ('Visible' in entry) entry.Visible = false;
        }
        else
        {
            const saved = dir.MutedRestore[key];
            if (saved)
            {
                if ('Enabled' in entry && saved.Enabled !== undefined) entry.Enabled = saved.Enabled;
                if ('Visible' in entry && saved.Visible !== undefined) entry.Visible = saved.Visible;
                delete dir.MutedRestore[key];
            }
            else
            {
                if ('Enabled' in entry) entry.Enabled = true;
                if ('Visible' in entry) entry.Visible = true;
            }
        }
    }
    syncSceneDirectory(scene);
    return recomposeSceneFromDirectory(scene);
}

/** Produces a clean JSON-serialisable snapshot of scene.Directory for .FluidScene.json files. */
export function serializeSceneDirectory(scene)
{
    const dir = ensureSceneDirectory(scene);
    if (!dir) return null;
    syncSceneDirectory(scene);
    return {
        ActiveSimulationId: dir.ActiveSimulationId,
        Counter: dir.Counter,
        DomainScope: dir.DomainScope,
        OverlayScope: cloneJson(dir.OverlayScope),
        CustomFolders: cloneJson(dir.CustomFolders),
        Assignments: cloneJson(dir.Assignments),
        Hidden: cloneJson(dir.Hidden),
        Simulations: dir.Simulations.map((sim) => ({
            Id: sim.Id,
            Name: sim.Name,
            PresetId: sim.PresetId || '',
            Notes: sim.Notes || '',
            Tint: sim.Tint || '#a2b87c',
            Domain: cloneJson(sim.Domain),
            GasStack: cloneJson(sim.GasStack),
            Visualizers: cloneJson(sim.Visualizers),
            LocalEmitters: cloneJson(sim.LocalEmitters),
            LocalForces: cloneJson(sim.LocalForces),
            LocalColliders: cloneJson(sim.LocalColliders),
            LocalParticles: cloneJson(sim.LocalParticles),
        })),
    };
}

