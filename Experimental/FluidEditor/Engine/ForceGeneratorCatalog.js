//============================================================================================================================================
//                                                     FORCEGENERATORCATALOG.JS
//                  Force Generator Type Registry — GLSL Type Codes, Parameter Schemas, Falloff Envelopes,
//                                 Emitter Shapes, Colour Ramps, and Scene Presets
//============================================================================================================================================

import { readTracks } from './KeyTracks.js';

export const MaximumForceGenerators = 16;
export const MaximumEmitters = 6;
export const MaximumColliders = 4;

export const FalloffCurves = ['SmoothStep', 'Linear', 'InverseSquare', 'Constant', 'Gaussian', 'Plateau', 'Cosine'];
export const FalloffLabels = { SmoothStep: 'Smooth', Linear: 'Linear', InverseSquare: 'Inverse²', Constant: 'Infinite', Gaussian: 'Gaussian', Plateau: 'Plateau', Cosine: 'Interpolated' };
export const ForceCategories = ['Directional', 'Attraction', 'Rotational', 'Turbulence', 'Burst', 'Damping', 'Thermal'];
export const EmitterShapes = ['Sphere', 'Disc', 'Box'];
export const ColourRamps = ['Turbo', 'Engineering', 'Plasma', 'Diverging'];
export const SliceAxes = ['X', 'Y', 'Z'];
export const ScalarChannels = ['Speed', 'Pressure', 'Vorticity', 'Temperature', 'Density', 'Divergence', 'ForceMagnitude'];
export const AdvectionSchemes = ['SemiLagrangian', 'MacCormack'];
export const BoundaryModes = ['ClosedBox', 'OpenTop', 'OpenAll'];
export const VolumeFilters = ['Smooth', 'Voxel'];
export const VectorModes = ['Slice', 'Volume'];

//------------------------------------------------------------------------------------------------------------------------
//                                                  FORCE GENERATOR TYPES
//------------------------------------------------------------------------------------------------------------------------

export const ForceGeneratorTypes = Object.freeze([
    // ---------------------------------------------------------------- Directional
    {
        Id: 'DirectionalWind', Code: 0, Category: 'Directional', Name: 'Directional Wind', Icon: 'wind', Tint: '#38bdf8',
        Summary: 'Uniform directional push across the whole domain or a bounded reach, with optional gusting.',
        Defaults: { Strength: 6, Radius: 2.0, Direction: [1, 0, 0], Falloff: 'Constant', Frequency: 0.6, Secondary: 0.35, Tertiary: 0 },
        Labels: { Frequency: 'Gust Rate', Secondary: 'Gust Depth' },
    },
    {
        Id: 'WindShear', Code: 17, Category: 'Directional', Name: 'Wind Shear', Icon: 'shear', Tint: '#60a5fa',
        Summary: 'Crosswind whose heading rotates with height along the axis — layered, twisting flow.',
        Defaults: { Strength: 5, Radius: 2.0, Direction: [0, 1, 0], Falloff: 'Constant', Frequency: 0, Secondary: 1.0, Tertiary: 0 },
        Labels: { Secondary: 'Twist (half-turns)' },
    },
    {
        Id: 'FlowStream', Code: 16, Category: 'Directional', Name: 'Flow Stream', Icon: 'stream', Tint: '#7dd3fc',
        Summary: 'Steers gas toward a target velocity along the axis — a soft conveyor rather than a push.',
        Defaults: { Strength: 3, Radius: 1.2, Direction: [1, 0, 0], Falloff: 'SmoothStep', Frequency: 2.5, Secondary: 0, Tertiary: 0 },
        Labels: { Frequency: 'Target Speed' },
    },
    {
        Id: 'DirectionalBurst', Code: 14, Category: 'Burst', Name: 'Directional Burst', Icon: 'dburst', Tint: '#fb923c',
        Summary: 'Periodic impulse along one axis — cannon shots, piston puffs, muzzle blasts.',
        Defaults: { Strength: 45, Radius: 0.9, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 1.5, Secondary: 0.1, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    // ---------------------------------------------------------------- Attraction
    {
        Id: 'PointAttractor', Code: 1, Category: 'Attraction', Name: 'Point Attractor', Icon: 'attractor', Tint: '#a78bfa',
        Summary: 'Pulls fluid toward a centre; negative strength repels.',
        Defaults: { Strength: 14, Radius: 1.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.1, Tertiary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'PointRepulsor', Code: 2, Category: 'Attraction', Name: 'Point Repulsor', Icon: 'repulsor', Tint: '#f472b6',
        Summary: 'Pushes fluid away from a centre in all directions.',
        Defaults: { Strength: 14, Radius: 1.2, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.1, Tertiary: 0 },
        Labels: { Secondary: 'Core Softness' },
    },
    {
        Id: 'AxisAttractor', Code: 8, Category: 'Attraction', Name: 'Axis Attractor', Icon: 'axis', Tint: '#22d3ee',
        Summary: 'Pulls fluid toward an infinite line through its centre.',
        Defaults: { Strength: 10, Radius: 1.8, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    {
        Id: 'Orbit', Code: 10, Category: 'Attraction', Name: 'Orbit', Icon: 'orbit', Tint: '#c4b5fd',
        Summary: 'Tangential circulation about a point with centripetal pull — gas orbits the centre.',
        Defaults: { Strength: 10, Radius: 1.6, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0.5, Tertiary: 0 },
        Labels: { Secondary: 'Centripetal Pull' },
    },
    {
        Id: 'Containment', Code: 11, Category: 'Attraction', Name: 'Containment', Icon: 'containment', Tint: '#2dd4bf',
        Summary: 'Spring that pushes gas back inside its radius — an invisible soft bottle.',
        Defaults: { Strength: 20, Radius: 1.2, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0, Secondary: 0.4, Tertiary: 0 },
        Labels: { Secondary: 'Wall Softness' },
    },
    {
        Id: 'MagneticDipole', Code: 19, Category: 'Attraction', Name: 'Magnetic Dipole', Icon: 'dipole', Tint: '#e879f9',
        Summary: 'Dipole field lines looping pole to pole — gas follows the magnetic flux.',
        Defaults: { Strength: 10, Radius: 1.8, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0.25, Tertiary: 0 },
        Labels: { Secondary: 'Core Radius' },
    },
    // ---------------------------------------------------------------- Rotational
    {
        Id: 'VortexColumn', Code: 3, Category: 'Rotational', Name: 'Vortex Column', Icon: 'vortex', Tint: '#34d399',
        Summary: 'Swirls fluid around an axis with optional inward pull and axial lift.',
        Defaults: { Strength: 12, Radius: 1.6, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 0.4, Secondary: 2.5, Tertiary: 0 },
        Labels: { Frequency: 'Inward Pull', Secondary: 'Axial Lift' },
    },
    {
        Id: 'VortexRing', Code: 9, Category: 'Rotational', Name: 'Vortex Ring', Icon: 'ring', Tint: '#c084fc',
        Summary: 'Toroidal circulation around a ring — launches smoke rings.',
        Defaults: { Strength: 16, Radius: 0.9, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0.35, Secondary: 0, Tertiary: 0 },
        Labels: { Frequency: 'Tube Radius' },
    },
    {
        Id: 'Spin', Code: 18, Category: 'Rotational', Name: 'Rigid Spin', Icon: 'spin', Tint: '#4ade80',
        Summary: 'Solid-body rotation: tangential force grows with distance from the axis.',
        Defaults: { Strength: 6, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Plateau', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    // ---------------------------------------------------------------- Turbulence
    {
        Id: 'CurlNoiseTurbulence', Code: 4, Category: 'Turbulence', Name: 'Curl Noise Turbulence', Icon: 'turbulence', Tint: '#fbbf24',
        Summary: 'Divergence-free animated curl noise that breaks up laminar flow.',
        Defaults: { Strength: 9, Radius: 2.4, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 1.8, Secondary: 0.7, Tertiary: 0 },
        Labels: { Frequency: 'Noise Scale', Secondary: 'Evolution Speed' },
    },
    {
        Id: 'BrownianJitter', Code: 15, Category: 'Turbulence', Name: 'Brownian Jitter', Icon: 'brownian', Tint: '#facc15',
        Summary: 'Per-cell random kicks re-rolled at a fixed rate — fine-grained, incoherent agitation.',
        Defaults: { Strength: 6, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 6, Secondary: 12, Tertiary: 0 },
        Labels: { Frequency: 'Cell Scale', Secondary: 'Re-roll Rate (Hz)' },
    },
    // ---------------------------------------------------------------- Burst
    {
        Id: 'RadialBurst', Code: 6, Category: 'Burst', Name: 'Radial Burst', Icon: 'burst', Tint: '#fb7185',
        Summary: 'Pulsing outward blast filling the reach — detonation kick.',
        Defaults: { Strength: 40, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 1.2, Secondary: 0.12, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    {
        Id: 'ShockwaveShell', Code: 12, Category: 'Burst', Name: 'Shockwave Shell', Icon: 'shockwave', Tint: '#f87171',
        Summary: 'A thin spherical pressure front expanding from the centre to the reach radius each period.',
        Defaults: { Strength: 60, Radius: 2.0, Direction: [0, 1, 0], Falloff: 'Constant', Frequency: 1.6, Secondary: 0.12, Tertiary: 0 },
        Labels: { Frequency: 'Period', Secondary: 'Shell Thickness', Tertiary: 'Start Delay' },
    },
    {
        Id: 'Implosion', Code: 13, Category: 'Burst', Name: 'Implosion', Icon: 'implosion', Tint: '#fda4af',
        Summary: 'Periodic inward collapse toward the centre — the pre-blast suck of a fireball.',
        Defaults: { Strength: 35, Radius: 1.5, Direction: [0, 1, 0], Falloff: 'Linear', Frequency: 1.5, Secondary: 0.15, Tertiary: 0 },
        Labels: { Frequency: 'Pulse Period', Secondary: 'Pulse Width', Tertiary: 'Start Delay' },
    },
    // ---------------------------------------------------------------- Damping / Thermal
    {
        Id: 'DragBaffle', Code: 5, Category: 'Damping', Name: 'Drag Baffle', Icon: 'drag', Tint: '#94a3b8',
        Summary: 'Damps velocity inside its reach — a soft brake on the flow.',
        Defaults: { Strength: 4, Radius: 1.0, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
    {
        Id: 'ThermalUpdraft', Code: 7, Category: 'Thermal', Name: 'Thermal Updraft', Icon: 'updraft', Tint: '#f97316',
        Summary: 'Cylindrical column of lift along its axis.',
        Defaults: { Strength: 8, Radius: 0.8, Direction: [0, 1, 0], Falloff: 'SmoothStep', Frequency: 0, Secondary: 0, Tertiary: 0 },
        Labels: {},
    },
]);

export const ForceTypeById = Object.fromEntries(ForceGeneratorTypes.map((type) => [type.Id, type]));

//------------------------------------------------------------------------------------------------------------------------
//                                                  COLOUR RAMPS (Overlay Legend Mirror)
//------------------------------------------------------------------------------------------------------------------------

export const RampStops = Object.freeze({
    Turbo: ['#30123b', '#4662d7', '#36aaf9', '#1ae4b6', '#72fe5e', '#c8ef34', '#faba39', '#f66b19', '#ca2a04', '#7a0403'],
    Engineering: ['#0b1f3a', '#12518a', '#1aa0c8', '#5fd0a0', '#e5e36a', '#f28b2d', '#d9281c'],
    Plasma: ['#0d0887', '#5b02a3', '#9a179b', '#cb4679', '#ed7953', '#fdb32f', '#f0f921'],
    Diverging: ['#2166ac', '#67a9cf', '#d1e5f0', '#f7f7f7', '#fddbc7', '#ef8a62', '#b2182b'],
});

//------------------------------------------------------------------------------------------------------------------------
//                                                  SCENE PRESETS
//------------------------------------------------------------------------------------------------------------------------

export const SmokeLooks = Object.freeze({
    Soot: { Albedo: '#4b4b4f', Absorption: 4.0, Anisotropy: 0.4 },
    Grey: { Albedo: '#8e8f94', Absorption: 3.2, Anisotropy: 0.45 },
    Steam: { Albedo: '#d9dde3', Absorption: 2.2, Anisotropy: 0.6 },
    Dust: { Albedo: '#b89c74', Absorption: 2.6, Anisotropy: 0.3 },
});

export const FireLooks = Object.freeze({
    Wood: { FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf' },
    Gas: { FireEmber: '#1f4bff', FireFlame: '#58a6ff', FireCore: '#e6f2ff' },
    Fuel: { FireEmber: '#b3120a', FireFlame: '#ff6a00', FireCore: '#ffd27a' },
    Magic: { FireEmber: '#5b0bb5', FireFlame: '#c84bff', FireCore: '#f6e3ff' },
});

// Scene presets are data: Presets/*.toml → Engine/PresetLibrary.js, loaded through Engine/PresetScenes.js (ScenePresets).

//------------------------------------------------------------------------------------------------------------------------
//                                                  TIMING KEYS
//------------------------------------------------------------------------------------------------------------------------

// Phase 4 (Fluid layer stack plan §5.3): StartTime / Duration / Fade / Repeat generate a linear Weight track (KeyTracks.envelopeToTrack);
// the solver, particles and gizmos sample it through nodeWeight(). An explicit Weight track keyed on the timeline replaces it.
export { nodeWeight, hasWeightTrack as isKeyed } from './KeyTracks.js';

//------------------------------------------------------------------------------------------------------------------------
//                                                  FACTORY HELPERS
//------------------------------------------------------------------------------------------------------------------------

let NextNodeIndex = 1;

export function allocateNodeId(prefix)
{
    NextNodeIndex += 1;
    return `${prefix}-${NextNodeIndex.toString(36)}`;
}

export function createForceGenerator(typeId, overrides = {})
{
    const type = ForceTypeById[typeId];
    if (!type) throw new Error(`Unknown force generator type "${typeId}".`);
    return {
        Id: allocateNodeId('force'),
        Kind: 'Force',
        Type: type.Id,
        Name: overrides.Name || type.Name,
        Enabled: true,
        Visible: true,
        Position: [...(overrides.Position || [0, 0, 0])],
        Direction: [...(overrides.Direction || type.Defaults.Direction)],
        Strength: overrides.Strength ?? type.Defaults.Strength,
        Radius: overrides.Radius ?? type.Defaults.Radius,
        Falloff: overrides.Falloff || type.Defaults.Falloff,
        Frequency: overrides.Frequency ?? type.Defaults.Frequency,
        Secondary: overrides.Secondary ?? type.Defaults.Secondary,
        Tertiary: overrides.Tertiary ?? type.Defaults.Tertiary ?? 0,
        StartTime: overrides.StartTime ?? 0,
        Duration: overrides.Duration ?? 0,
        Fade: overrides.Fade ?? 0.15,
        Repeat: overrides.Repeat ?? 0,
        Tracks: readTracks(overrides.Tracks || overrides.Track),
    };
}

export const DebrisKinds = Object.freeze(['Rubble', 'Ember', 'Paper']);
export const EmitterPathKinds = Object.freeze(['None', 'Circle', 'Line', 'Figure8', 'Straight', 'Slalom']);
export const GridFollowModes = Object.freeze(['None', 'Emitter', 'Particles', 'Camera']);
export const MaximumGridTiles = 5;
export const GridModes = Object.freeze(['Fixed', 'Infinite']);
export const MaximumTilePool = 160;
export const MaximumInfiniteCells = 1600000;

/** Animated emitter transform: returns { Position, Velocity } for an emitter following its motion path at `time`. */
export function emitterTransform(emitter, time)
{
    const kind = emitter.PathKind || 'None';
    if (kind === 'None') return { Position: emitter.Position, Velocity: emitter.Velocity };
    const radius = emitter.PathRadius || 0, speed = emitter.PathSpeed || 0;
    const angle = time * speed;
    let offset, heading;
    if (kind === 'Circle')
    {
        offset = [Math.cos(angle) * radius, 0, Math.sin(angle) * radius];
        heading = [-Math.sin(angle), 0, Math.cos(angle)];
    }
    else if (kind === 'Line')
    {
        const s = Math.sin(angle);
        offset = [s * radius, 0, 0];
        heading = [Math.cos(angle) >= 0 ? 1 : -1, 0, 0];
    }
    else if (kind === 'Straight')
    {
        // Unbounded drive: PathSpeed is metres per second along +X; PathRadius is ignored. Pair with a following World Grid.
        offset = [time * speed, 0, 0];
        heading = [1, 0, 0];
    }
    else if (kind === 'Slalom')
    {
        // Unbounded drive weaving left/right: PathRadius is the weave half-width, PathSpeed the forward speed.
        const period = Math.max(radius * 2.5, 0.5);
        const weave = time * speed / period;
        offset = [time * speed, 0, Math.sin(weave) * radius];
        const dz = Math.cos(weave) * radius * speed / period;
        const length = Math.hypot(speed, dz) || 1;
        heading = [speed / length, 0, dz / length];
    }
    else
    {
        offset = [Math.sin(angle) * radius, 0, Math.sin(angle * 2) * radius * 0.5];
        const length = Math.hypot(Math.cos(angle), Math.cos(angle * 2)) || 1;
        heading = [Math.cos(angle) / length, 0, Math.cos(angle * 2) / length];
    }
    // Rotate the local exit velocity so "backwards" stays behind the moving source: local x = heading, local z = right.
    const right = [heading[2], 0, -heading[0]];
    const local = emitter.Velocity;
    const velocity = [
        heading[0] * local[0] + right[0] * local[2],
        local[1],
        heading[2] * local[0] + right[2] * local[2],
    ];
    return { Position: [emitter.Position[0] + offset[0], emitter.Position[1] + offset[1], emitter.Position[2] + offset[2]], Velocity: velocity, Heading: heading };
}


export function createEmitter(overrides = {})
{
    return {
        Id: allocateNodeId('emitter'),
        Kind: 'Emitter',
        Name: overrides.Name || 'Emitter',
        Enabled: true,
        Visible: true,
        Shape: overrides.Shape || 'Sphere',
        Position: [...(overrides.Position || [0, -1.5, 0])],
        Orientation: [...(overrides.Orientation || [0, 1, 0])],
        Radius: overrides.Radius ?? 0.4,
        Density: overrides.Density ?? 1.0,
        Temperature: overrides.Temperature ?? 2.0,
        Fuel: overrides.Fuel ?? 0.0,
        Velocity: [...(overrides.Velocity || [0, 1, 0])],
        Jitter: overrides.Jitter ?? 0.4,
        Pulse: overrides.Pulse ?? 0.0,
        StartTime: overrides.StartTime ?? 0,
        Duration: overrides.Duration ?? 0,
        Fade: overrides.Fade ?? 0.15,
        Repeat: overrides.Repeat ?? 0,
        Debris: overrides.Debris ?? 0,
        DebrisSpeed: overrides.DebrisSpeed ?? 6,
        DebrisKind: overrides.DebrisKind || 'Rubble',
        PathKind: overrides.PathKind || 'None',
        PathRadius: overrides.PathRadius ?? 1.5,
        PathSpeed: overrides.PathSpeed ?? 1.0,
        Tracks: readTracks(overrides.Tracks || overrides.Track),
    };
}

export function createCollider(overrides = {})
{
    return {
        Id: allocateNodeId('collider'),
        Kind: 'Collider',
        Name: overrides.Name || 'Sphere Collider',
        Enabled: true,
        Visible: true,
        Position: [...(overrides.Position || [0, 0, 0])],
        Radius: overrides.Radius ?? 0.5,
    };
}

export const ResolutionPresets = Object.freeze({ Draft: 32, Low: 48, Medium: 64, High: 96, Ultra: 128, Extreme: 160 });

// Resolution follows the Unreal convention: the domain keeps its world size, `MaxCells` sets the cell count along the
// longest axis, and `CellSize` + per-axis `Resolution` are derived so shrinking the voxel count makes the gas coarser.
export function resolveDomainGrid(domain)
{
    if (domain.Grid === 'Infinite') return resolveInfiniteGrid(domain);
    domain.Grid = 'Fixed';
    // World Grid: the domain `Size` is one tile; the simulated window spans `Tiles` adjacent tiles per axis as a single
    // seamless field. Cells per tile are integers so the window can slide by whole tiles with an exact texel copy.
    const tiles = (domain.Tiles || [1, 1, 1]).map((count) => Math.max(1, Math.min(MaximumGridTiles, Math.round(count || 1))));
    const longest = Math.max(domain.Size[0], domain.Size[1], domain.Size[2]);
    const maxCells = Math.max(8, Math.min(160, Math.round(domain.MaxCells)));
    let cellSize = longest / maxCells;
    // Keep the whole window inside the 160-cell solver limit per axis: coarsen the voxel rather than block the grid.
    for (let axis = 0; axis < 3; axis += 1) cellSize = Math.max(cellSize, domain.Size[axis] * tiles[axis] / 160);
    const multi = tiles[0] * tiles[1] * tiles[2] > 1;
    const tileCells = domain.Size.map((extent) => Math.max(multi ? 4 : 8, Math.round(extent / cellSize)));
    domain.Tiles = tiles;
    domain.TileCells = tileCells;
    domain.CellSize = cellSize;
    domain.Resolution = tileCells.map((cells, axis) => Math.max(8, Math.min(160, cells * tiles[axis])));
    if (!domain.GridOrigin) domain.GridOrigin = [0, 0, 0];
    if (!GridFollowModes.includes(domain.Follow)) domain.Follow = 'None';
    return domain;
}

/**
 * Infinite grid: an unbounded lattice of cubic tiles (`TileSize` m, `TileResolution` cells per edge) allocated on demand
 * from a pool of `TilePool` atlas slots. There is no box at all — the pool size is the only budget, and it is capped so
 * the atlas stays inside a sane GPU memory footprint.
 */
export function resolveInfiniteGrid(domain)
{
    domain.Grid = 'Infinite';
    domain.TileSize = Math.max(0.5, Math.min(8, domain.TileSize || 2));
    domain.TileResolution = Math.max(8, Math.min(48, Math.round(domain.TileResolution || 24)));
    const perTile = domain.TileResolution ** 3;
    domain.TilePool = Math.max(4, Math.min(MaximumTilePool, Math.floor(MaximumInfiniteCells / perTile), Math.round(domain.TilePool || 48)));
    domain.CellSize = domain.TileSize / domain.TileResolution;
    domain.Resolution = [domain.TileResolution, domain.TileResolution, domain.TileResolution];
    domain.Tiles = [1, 1, 1];
    domain.TileCells = [...domain.Resolution];
    if (!domain.GridOrigin) domain.GridOrigin = [0, 0, 0];
    if (!GridFollowModes.includes(domain.Follow)) domain.Follow = 'None';
    return domain;
}

/** World-space centre of the simulated window: tile index × tile pitch (tile 0 is centred on the origin). */
export function gridWindowCentre(domain)
{
    const origin = domain.GridOrigin || [0, 0, 0];
    return [0, 1, 2].map((axis) => origin[axis] * domain.TileCells[axis] * domain.CellSize);
}

/** Tile index containing a world position (tiles are centred on multiples of the tile pitch). */
export function gridTileOf(domain, position)
{
    return [0, 1, 2].map((axis) => Math.floor(position[axis] / (domain.TileCells[axis] * domain.CellSize) + 0.5));
}

export function createDomain(overrides = {})
{
    const size = [...(overrides.Size || [3, 6, 3])];
    const maxCells = overrides.MaxCells || (overrides.Resolution ? Math.max(...overrides.Resolution) : 96);
    return resolveDomainGrid({
        Id: 'domain',
        Kind: 'Domain',
        Name: 'Gas Domain',
        Size: size,
        MaxCells: maxCells,
        Buoyancy: overrides.Buoyancy ?? 1.2,
        AmbientTemperature: overrides.AmbientTemperature ?? 0,
        Cooling: overrides.Cooling ?? 0.6,
        Vorticity: overrides.Vorticity ?? 0.35,
        Dissipation: overrides.Dissipation ?? 0.996,
        VelocityDissipation: overrides.VelocityDissipation ?? 0.999,
        Gravity: overrides.Gravity ?? 0.25,
        PressureIterations: overrides.PressureIterations ?? 36,
        Advection: overrides.Advection || 'MacCormack',
        Boundary: overrides.Boundary || 'OpenTop',
        TimeScale: overrides.TimeScale ?? 1.0,
        Combustion: overrides.Combustion ?? 1.0,
        Expansion: overrides.Expansion ?? 0.6,
        ForceResponse: overrides.ForceResponse ?? 1.0,
        SceneLoop: overrides.SceneLoop ?? 0,
        Tiles: overrides.Tiles ? [...overrides.Tiles] : [1, 1, 1],
        Follow: overrides.Follow || 'None',
        FollowHeight: overrides.FollowHeight ?? false,
        GridOrigin: overrides.GridOrigin ? [...overrides.GridOrigin] : [0, 0, 0],
        Grid: overrides.Grid || 'Fixed',
        TileSize: overrides.TileSize ?? 2,
        TileResolution: overrides.TileResolution ?? 24,
        TilePool: overrides.TilePool ?? 48,
    });
}

export function createVisualizerSet()
{
    return {
        Volume: {
            Id: 'vis-volume', Kind: 'Visualizer', Name: 'Volume Render', Visible: true,
            Density: 1.0, Albedo: '#8e8f94', Absorption: 3.2, Anisotropy: 0.45, Ambient: '#3a414c', LightIntensity: 1.4, Exposure: 1.15,
            FireIntensity: 4.0, FireThreshold: 1.0, FireSoftness: 3.5, FireOpacity: 1.4, FireEmber: '#ff2a05', FireFlame: '#ff8a16', FireCore: '#fff3cf',
            FireLight: 1.0, ShimmerStrength: 0.0, ShimmerFrequency: 14.0, ShimmerSpeed: 2.2, ShimmerChromatic: 0.0,
            Steps: 80, ShadowSteps: 8, Filter: 'Smooth',
        },
        VelocityVectors: { Id: 'vis-vectors', Kind: 'Visualizer', Name: 'Velocity Vectors', Visible: false, Axis: 'Z', Offset: 0.5, Stride: 2, Scale: 0.35, Ramp: 'Turbo', MaxSpeed: 6 },
        ScalarSlice: { Id: 'vis-slice', Kind: 'Visualizer', Name: 'Scalar Slice', Visible: false, Axis: 'Y', Offset: 0.35, Channel: 'Speed', Ramp: 'Turbo', Range: 6, Opacity: 0.85 },
        Tracers: { Id: 'vis-tracers', Kind: 'Visualizer', Name: 'Flow Tracers', Visible: false, Count: 8192, Lifetime: 4.0, Ramp: 'Turbo', MaxSpeed: 6, Size: 2.0 },
        Debris: { Id: 'vis-debris', Kind: 'Visualizer', Name: 'Debris', Visible: false, Count: 2048, Lifetime: 5.0, Size: 3.0, Gravity: 9.8, Drag: 1.2, Bounce: 0.35, Glow: 1.0 },
        ForceVectors: { Id: 'vis-forces', Kind: 'Visualizer', Name: 'Force Vectors', Visible: false, Mode: 'Slice', Axis: 'Z', Offset: 0.5, Stride: 3, Scale: 0.04, Ramp: 'Plasma', MaxForce: 20 },
        ForceGizmos: { Id: 'vis-gizmos', Kind: 'Visualizer', Name: 'Force Gizmos', Visible: false, Opacity: 0.9 },
    };
}
