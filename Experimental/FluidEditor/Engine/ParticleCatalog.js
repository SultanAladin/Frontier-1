//------------------------------------------------------------------------------------------------------------------------
//                                                  PARTICLE CATALOG
//                     Mograph-style Particle / VFX System Data Model: Shapes, Modes, Colour Rules, Templates
//
//  This file is the portable specification of the particle module. It contains no WebGL — only enumerations, numeric
//  codes (which the GLSL and a future C++ port share) and factory helpers. ParticleShaders.js implements the behaviour
//  on the GPU and ParticleSimulation.js owns the per-system state. Adding a shape, colour rule or template means adding
//  one entry here plus its branch in the shader; nothing else in the studio needs to change.
//------------------------------------------------------------------------------------------------------------------------

import { allocateNodeId } from './ForceGeneratorCatalog.js';
import { ParticleTemplateDocuments, ParticleTemplateLibraryHash } from './ParticleTemplateLibrary.js';
import { readTracks } from './KeyTracks.js';
import { readParticleStack, evaluateParticleStack, applyParticleEvaluation, convertParticleSystemToStack, instantiateParticleStack } from './LayerStack.js';
import { ObjectNames, ObjectLabels, ObjectSampleModes, BuiltInObjectNames, ResolveObjectMesh } from './ObjectMeshCatalog.js';

export const MaximumParticleSystems = Infinity;
export const MaximumParticlesPerSystem = 16384;

/**
 * Spawn / cloner shapes. Code = index. Direction acts as the shape axis for Disc, Ring, Line, Grid, Torus, SurfaceShell, MocapFigure
 * and Dome. `ObjectSurface` samples a 3D mesh (built-in or imported); Spiderweb, ClothGrid, FloraBed and FireworkBurst are the
 * structured origins ported from Particle Studio.
 */
export const ParticleShapes = Object.freeze(['Point', 'Sphere', 'Box', 'Disc', 'Ring', 'Line', 'Grid', 'Torus', 'SurfaceShell', 'MocapFigure', 'Dome', 'ObjectSurface', 'Spiderweb', 'ClothGrid', 'FloraBed', 'FireworkBurst']);

/** Emit = particles are born, age and die. Cloner = a fixed formation displaced by forces and pulled home by a spring. */
export const ParticleModes = Object.freeze(['Emit', 'Cloner']);

/** Colour rules. Code = index. */
export const ParticleColourModes = Object.freeze(['Life', 'Speed', 'Displacement', 'Height', 'Seed', 'Swatch', 'Cycle']);
export const ParticleColourLabels = Object.freeze({ Life: 'Gradient over life', Speed: 'Speed', Displacement: 'Distance from home', Height: 'Height', Seed: 'Random hue', Swatch: 'Random palette swatch', Cycle: 'Palette cycling in time' });
/** Where Speed / Displacement / Height take their gradient from. */
export const ParticleRampSources = Object.freeze(['Palette', 'Ramp']);
export const MaximumPaletteStops = 8;
export const PalettePresets = Object.freeze({
    Fire: ['#fff3c4', '#ffb454', '#ff5a1f', '#b3120a'],
    Ice: ['#ffffff', '#bfe9ff', '#38bdf8', '#1d4ed8'],
    Candy: ['#ff5f7e', '#ffb454', '#fef08a', '#34c759', '#38bdf8', '#a78bfa'],
    Neon: ['#f472b6', '#a78bfa', '#38bdf8', '#34c759'],
    Mono: ['#ffffff', '#71717a'],
    Earth: ['#d6c7ad', '#b89c74', '#8a7a5c', '#4b4038'],
});

export const ParticleBlendModes = Object.freeze(['Alpha', 'Additive']);
export const ParticleRenderStyles = Object.freeze(['Point', 'Streak']);
/** Sprite footprint. Code = index. 9–17 are the analytical cutouts ported from ParticleSpriteCutouts.slang. */
export const ParticleSprites = Object.freeze(['Glow', 'Disc', 'Bokeh', 'Star', 'Square', 'Cube3D', 'Butterfly', 'HexTile', 'HoloGlyph', 'SnowCrystal', 'RainDrop', 'LeafBlade', 'BlossomPetal', 'CrystalShard2D', 'ShockRing', 'RadarArc', 'AuroraCurtain', 'CoinDisc']);
/** The visual carrier of a particle: a billboard sprite or a shaded 3D object mesh. Code = index. */
export const ParticleCarrierModes = Object.freeze(['Sprite', 'Object3D']);
/** Backing surface drawn behind a 3D object spawn or carrier. Code = index. */
export const ParticleBackingSurfaces = Object.freeze(['Hidden', 'ObsidianGlossy', 'BrushedChrome', 'GhostWireframe']);
export { ObjectNames, ObjectLabels, ObjectSampleModes, BuiltInObjectNames, ResolveObjectMesh };
export const ParticleSizeModes = Object.freeze(['World', 'Pixels']);
/** Phase 5 effect layer choice enumerations. Code = index. */
export const ParticleDerezModes = Object.freeze(['Off', 'Derez', 'ReverseDerez', 'MeltDissolve', 'ButterflyMorph', 'CellGlitch']);
export const ParticleMocapClips = Object.freeze(['Off', 'Sprint', 'Dance', 'Martial', 'Levitate']);
export const ParticleEffectorModes = Object.freeze(['Off', 'Ripple', 'Orbiters', 'PlaneWave', 'NoiseField']);
export const ParticleDistortionModes = Object.freeze(['Off', 'GravitationalLens', 'ShockwaveRipple', 'HeatBubble']);
/** Trail rendering. Off = sprites only. Ribbon = spline ribbons behind each particle. Only = ribbons without head sprites. */
export const ParticleTrailModes = Object.freeze(['Off', 'Ribbon', 'Only']);
export const ParticleTrailColours = Object.freeze(['Particle', 'Along']);
export const MaximumTrailLength = 96;
export const MaximumTrailTexels = 8192;
/** History samples actually allocated: capped so the trail texture height stays within GPU limits. */
export function effectiveTrailLength(count, requested)
{
    return Math.max(2, Math.min(requested, Math.floor(MaximumTrailTexels / particleTextureSide(count))));
}

export const ParticleCategories = Object.freeze(['Transitions', 'Mocap & MoGraph', 'FX & Shields', 'Cinematic', 'Trails', 'Emitters', 'Cloners', 'Environment']);

/**
 * Asset-browser templates (Fluid layer stack plan, phase 3). Each template is a `frontier-particle-template` TOML document in
 * ParticleTemplates/ holding a particle layer stack; GeneratePresetLibrary.mjs bundles them into Engine/ParticleTemplateLibrary.js.
 * `Defaults` is what the stack evaluates to (every GPU field), so code that reads template.Defaults keeps working unchanged.
 * The 26 original templates were converted by script; FluidValidation.mjs checks each still builds the identical node.
 */
export const ParticleTemplates = Object.freeze(ParticleTemplateDocuments.map((document) =>
{
    const stack = readParticleStack({ Layer: document.Layer, Track: (document.Track || []).filter((track) => String(track.Path).includes('/')) });
    const evaluation = evaluateParticleStack(stack);
    return Object.freeze({
        ...document.Template,
        Defaults: Object.freeze({ ...evaluation.Particles, ...(document.Timing || {}) }),
        Stack: stack,
        Problems: evaluation.Problems,
        Stopped: evaluation.Stopped,
    });
}));
export { ParticleTemplateLibraryHash };

export const ParticleTemplateById = Object.freeze(Object.fromEntries(ParticleTemplates.map((template) => [template.Id, template])));

/** Particle systems whose stack cannot evaluate (no Spawn layer, over capacity): kept in the scene, not simulated or drawn. */
export const HaltedParticleSystems = new WeakSet();

/**
 * New particle system node. The layer stack is the source: `overrides.Stack` (a saved or duplicated stack) wins over flat fields;
 * otherwise the template's stack is copied and the flat overrides are written into their owning layers (exactly). Either way the node's
 * flat fields hold the evaluated amounts the GPU reads, and `node.Stack` holds the layers the Inspector edits.
 */
export function createParticleSystem(templateId, overrides = {})
{
    const { Stack: savedStack, ...flat } = overrides;
    const node = createFlatParticleSystem(templateId, flat);
    if (savedStack && (savedStack.Layers || savedStack.Layer))
    {
        node.Stack = readParticleStack(savedStack, true);
        evaluateParticleSystem(node);
    }
    else
    {
        // Template instance: keep the template's layer set (Empty Cloud stays Spawn + Sprite); overrides go into their layers.
        const template = ParticleTemplateById[node.Template];
        node.Stack = (template && instantiateParticleStack(template.Stack, node)) || convertParticleSystemToStack(node);
    }
    return node;
}

/** Folds node.Stack into the node's flat fields (keyed amounts at `time` when given); returns the evaluation (problems, contributing layers). */
export function evaluateParticleSystem(node, time = undefined)
{
    const evaluation = evaluateParticleStack(node.Stack, time);
    if (evaluation.Stopped) HaltedParticleSystems.add(node);
    else
    {
        HaltedParticleSystems.delete(node);
        applyParticleEvaluation(node, evaluation);
    }
    return evaluation;
}

function createFlatParticleSystem(templateId, overrides = {})
{
    const template = ParticleTemplateById[templateId] || ParticleTemplates[0];
    const base = { ...template.Defaults, ...overrides };
    return {
        Id: allocateNodeId('particles'),
        Kind: 'Particles',
        Template: template.Id,
        Name: overrides.Name || template.Name,
        Enabled: overrides.Enabled ?? true,
        Visible: overrides.Visible ?? true,
        Mode: base.Mode || 'Emit',
        Shape: base.Shape || 'Point',
        ShapeDetail: base.ShapeDetail ?? 8,          // web spokes / cloth folds / flora blades / burst lobes
        SpawnObject: base.SpawnObject || 'UtahTeapot',   // built-in 3D object the ObjectSurface shape samples
        SpawnSampleMode: base.SpawnSampleMode || 'EvenSurface',
        ObjectYaw: base.ObjectYaw ?? 0,             // degrees, baked into the sampled anchors
        BackingSurface: base.BackingSurface || 'Hidden',
        CustomObjectId: base.CustomObjectId || '',
        Position: [...(base.Position || [0, 0, 0])],
        Direction: [...(base.Direction || [0, 1, 0])],
        Radius: base.Radius ?? 0.5,
        Count: Math.min(MaximumParticlesPerSystem, Math.max(16, Math.round(base.Count ?? 2048))),
        Rate: base.Rate ?? 8,                      // respawn probability per second for dead particles
        Lifetime: base.Lifetime ?? 3,
        Fill: base.Fill ?? 0,                      // sphere / disc formations: 0 = shell, 1 = solid volume
        Dissolve: base.Dissolve ?? 0,              // cloner homes drift on curl noise (Form-style dissolve)
        NoiseScale: base.NoiseScale ?? 1.3,        // spatial frequency of the turbulence field
        NoiseSpeed: base.NoiseSpeed ?? 0.5,        // evolution speed of the turbulence field
        Speed: base.Speed ?? 2,
        Spread: base.Spread ?? 0.5,                // 0 = all along Direction, 1 = fully random
        Radial: base.Radial ?? 0,                  // blend launch direction toward 'outward from the shape origin'
        Gravity: base.Gravity ?? 0,
        Drag: base.Drag ?? 0.5,
        Noise: base.Noise ?? 0,                    // curl-noise wander strength
        FieldInfluence: base.FieldInfluence ?? 1,  // how much scene force generators accelerate the particles
        GasInfluence: base.GasInfluence ?? 0,      // coupling to the fluid velocity field
        Spring: base.Spring ?? 6,                  // cloner return stiffness
        Damping: base.Damping ?? 3,
        Bounce: base.Bounce ?? 0.2,
        SizeStart: base.SizeStart ?? 3,
        SizeEnd: base.SizeEnd ?? 1,
        SizeRandom: base.SizeRandom ?? 0.3,        // 0 = uniform, 1 = log-spread from micro dust to big bokeh
        SizeMode: base.SizeMode || 'Pixels',
        Sprite: base.Sprite || 'Disc',
        CarrierMode: base.CarrierMode || 'Sprite',   // Sprite = billboard cutout, Object3D = shaded mesh per particle
        CarrierObject: base.CarrierObject || 'Cube',
        CarrierCustomObjectId: base.CarrierCustomObjectId || '',
        ObjectSpin: base.ObjectSpin ?? 1.2,
        ObjectTilt: base.ObjectTilt ?? 0.15,
        ObjectRoughness: base.ObjectRoughness ?? 0.35,
        ObjectMetallic: base.ObjectMetallic ?? 0,
        ObjectGlow: base.ObjectGlow ?? 0,
        Glow: base.Glow ?? 0,                      // additive halo pass strength
        FocusDistance: base.FocusDistance ?? 0,    // 0 = depth of field off
        Aperture: base.Aperture ?? 0.5,
        ColourMode: base.ColourMode || 'Life',
        Palette: [...(base.Palette && base.Palette.length ? base.Palette : [base.ColourStart || '#ffffff', base.ColourEnd || '#808080'])].slice(0, MaximumPaletteStops),
        RampSource: base.RampSource || 'Palette',
        HueJitter: base.HueJitter ?? 0,
        BrightnessJitter: base.BrightnessJitter ?? 0,
        CycleSpeed: base.CycleSpeed ?? 0.5,
        Brightness: base.Brightness ?? 1,
        Ramp: base.Ramp || 'Turbo',
        DisplacementRange: base.DisplacementRange ?? 1,
        MaxSpeed: base.MaxSpeed ?? 6,
        Flicker: base.Flicker ?? 0,
        Blend: base.Blend || 'Alpha',
        Style: base.Style || 'Point',
        Trail: base.Trail || 'Off',
        TrailLength: Math.min(MaximumTrailLength, Math.max(2, Math.round(base.TrailLength ?? 24))),
        TrailInterval: base.TrailInterval ?? 0.08,   // seconds between history samples (longer = longer trails)
        TrailWidth: base.TrailWidth ?? 0.01,         // same units as SizeMode
        TrailTaper: base.TrailTaper ?? 0.6,
        TrailFade: base.TrailFade ?? 0.7,
        TrailSubdivisions: Math.min(4, Math.max(1, Math.round(base.TrailSubdivisions ?? 2))),
        TrailColour: base.TrailColour || 'Particle',
        TrailRatio: base.TrailRatio ?? 1,            // fraction of particles that draw a trail
        Linger: base.Linger ?? (base.Trail && base.Trail !== 'Off' ? 3 : 0.6),  // seconds a dead particle's trail keeps fading before respawn
        DerezMode: base.DerezMode || 'Off',
        DerezMix: base.DerezMix ?? 0,
        DerezProgress: base.DerezProgress ?? 0,
        DerezAutoSpeed: base.DerezAutoSpeed ?? 0,
        DerezEdgeWidth: base.DerezEdgeWidth ?? 0.14,
        DerezEdgeGlow: base.DerezEdgeGlow ?? 0,
        DerezEdgeColour: base.DerezEdgeColour || '#00f0ff',
        DerezShatter: base.DerezShatter ?? 0,
        MocapClip: base.MocapClip || 'Off',
        MocapAmplitude: base.MocapAmplitude ?? 0,
        MocapSpeed: base.MocapSpeed ?? 1,
        MocapScale: base.MocapScale ?? 1,
        MocapTrailKick: base.MocapTrailKick ?? 0,
        EffectorMode: base.EffectorMode || 'Off',
        EffectorStrength: base.EffectorStrength ?? 0,
        EffectorFrequency: base.EffectorFrequency ?? 1.8,
        EffectorSpeed: base.EffectorSpeed ?? 1.6,
        EffectorPush: base.EffectorPush ?? 0,
        EffectorScaleBoost: base.EffectorScaleBoost ?? 0,
        EffectorColourShift: base.EffectorColourShift ?? 0,
        SwarmStrength: base.SwarmStrength ?? 0,
        SwarmCohesion: base.SwarmCohesion ?? 0,
        SwarmOrbit: base.SwarmOrbit ?? 0,
        SwarmWingRate: base.SwarmWingRate ?? 10,
        GlitchAmount: base.GlitchAmount ?? 0,
        GlitchRate: base.GlitchRate ?? 14,
        GlitchStepJitter: base.GlitchStepJitter ?? 0,
        GlitchScanlines: base.GlitchScanlines ?? 0,
        GlitchChromatic: base.GlitchChromatic ?? 0,
        DistortionMode: base.DistortionMode || 'Off',
        DistortionStrength: base.DistortionStrength ?? 0,
        DistortionRadius: base.DistortionRadius ?? 0.28,
        DistortionHorizon: base.DistortionHorizon ?? 0,
        DistortionChromatic: base.DistortionChromatic ?? 0,
        StartTime: base.StartTime ?? 0,
        Duration: base.Duration ?? 0,
        Fade: base.Fade ?? 0.15,
        Repeat: base.Repeat ?? 0,
        Tracks: readTracks(overrides.Tracks || overrides.Track),   // node-level tracks: Weight (timing); layer tracks live in Stack.Tracks
    };
}

/** Square texture side that holds `count` particles. */
export function particleTextureSide(count)
{
    const side = Math.ceil(Math.sqrt(Math.max(16, Math.min(MaximumParticlesPerSystem, count))));
    return Math.min(128, Math.max(4, Math.ceil(side / 4) * 4));
}
