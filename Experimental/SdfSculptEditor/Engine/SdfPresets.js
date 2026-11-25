/*==========================================================================================================================================
                                                            SDFPRESETS.JS
==========================================================================================================================================*/
// Starter Hybrid SDF Document Scenes, 18 Kitbash / Cutter / Blank Library items, 10 Sculpting Brush specifications,
// and Studio Material / MatCap presets for Frontier SDF Sculpt Studio.

import { HexToLinearRgb } from './SdfMath.js';

export const SCULPT_BRUSHES = [
    {
        id: 'clay',
        key: '1',
        name: 'Clay Buildup',
        short: 'Clay',
        glyph: 'Layers',
        accent: '#e8c27a',
        defaultRadius: 0.24,
        defaultStrength: 0.68,
        defaultSmoothness: 0.34,
        description: 'Deposits continuous oblate clay strips onto the SDF volume using C1 smooth union. Hold Alt to carve.'
    },
    {
        id: 'carve',
        key: '2',
        name: 'Carve Scoop',
        short: 'Carve',
        glyph: 'Zap',
        accent: '#ff7a6b',
        defaultRadius: 0.20,
        defaultStrength: 0.72,
        defaultSmoothness: 0.28,
        description: 'Subtracts smooth volumetric channels and cavities from the SDF. Hold Alt to invert into additive clay.'
    },
    {
        id: 'inflate',
        key: '3',
        name: 'Draw / Inflate',
        short: 'Inflate',
        glyph: 'Sparkles',
        accent: '#8be0a9',
        defaultRadius: 0.28,
        defaultStrength: 0.55,
        defaultSmoothness: 0.35,
        description: 'Displaces the local SDF zero-isosurface outward along its gradient while preserving fine detail.'
    },
    {
        id: 'smooth',
        key: '4',
        name: 'Smooth Relax',
        short: 'Smooth',
        glyph: 'Drop',
        accent: '#7ec8ff',
        defaultRadius: 0.30,
        defaultStrength: 0.65,
        defaultSmoothness: 0.45,
        description: 'Applies 3D isotropic Laplacian relaxation across the signed distance field. Hold Shift in any brush.'
    },
    {
        id: 'flatten',
        key: '5',
        name: 'Flatten / Trim',
        short: 'Flatten',
        glyph: 'Scan',
        accent: '#f0cfa0',
        defaultRadius: 0.32,
        defaultStrength: 0.80,
        defaultSmoothness: 0.20,
        description: 'Trims protruding clay down to the stroke reference plane for crisp stylized bevels, rocks, and armor.'
    },
    {
        id: 'pinch',
        key: '6',
        name: 'Pinch / Crease',
        short: 'Pinch',
        glyph: 'Attract',
        accent: '#d8a6ff',
        defaultRadius: 0.18,
        defaultStrength: 0.65,
        defaultSmoothness: 0.20,
        description: 'Contracts signed distances radially toward the stroke path to sharpen ridges or cut tight panel seams.'
    },
    {
        id: 'grab',
        key: '7',
        name: 'Grab / Warp',
        short: 'Grab',
        glyph: 'Move',
        accent: '#eff4cf',
        defaultRadius: 0.44,
        defaultStrength: 0.85,
        defaultSmoothness: 0.40,
        description: 'Captures a snapshot on pointer-down and backward-advects the 3D SDF volume along cursor drag.'
    },
    {
        id: 'snake',
        key: '8',
        name: 'Snake Hook',
        short: 'Snake',
        glyph: 'Tornado',
        accent: '#ffb36b',
        defaultRadius: 0.24,
        defaultStrength: 0.86,
        defaultSmoothness: 0.30,
        description: 'Incrementally advects and deposits tapered SDF volume to pull horns, spikes, and organic tendrils.'
    },
    {
        id: 'stamp',
        key: '9',
        name: 'Stamp Primitive',
        short: 'Stamp',
        glyph: 'Boxes',
        accent: '#8fd3c4',
        defaultRadius: 0.28,
        defaultStrength: 0.80,
        defaultSmoothness: 0.22,
        description: 'Surface-aligned 3D analytical SDF stamp. Click-drag on the model to interactively scale and orient.'
    },
    {
        id: 'paint',
        key: '0',
        name: 'Polypaint RGB',
        short: 'Paint',
        glyph: 'Palette',
        accent: '#ff8fb8',
        defaultRadius: 0.26,
        defaultStrength: 0.75,
        defaultSmoothness: 0.35,
        description: 'Airbrushes smooth trilinear RGB albedo directly into the RGBA16F volume shell without altering geometry.'
    }
];

export const MATERIAL_PRESETS = {
    terracotta: {
        label: 'Terracotta Clay',
        albedoHex: '#bc795c',
        roughness: 0.62,
        metallic: 0.03,
        subsurface: 0.38,
        cavity: 0.55
    },
    zgrey: {
        label: 'Studio Grey Clay',
        albedoHex: '#94989e',
        roughness: 0.52,
        metallic: 0.05,
        subsurface: 0.22,
        cavity: 0.60
    },
    bone: {
        label: 'Warm Bone / Ivory',
        albedoHex: '#d7ccb8',
        roughness: 0.46,
        metallic: 0.02,
        subsurface: 0.45,
        cavity: 0.52
    },
    gunmetal: {
        label: 'Hard-Surface Alloy',
        albedoHex: '#7d8791',
        roughness: 0.28,
        metallic: 0.78,
        subsurface: 0.04,
        cavity: 0.65
    },
    jade: {
        label: 'Translucent Jade',
        albedoHex: '#5ca88a',
        roughness: 0.24,
        metallic: 0.04,
        subsurface: 0.72,
        cavity: 0.42
    },
    bronze: {
        label: 'Cast Foundry Bronze',
        albedoHex: '#b88b58',
        roughness: 0.32,
        metallic: 0.84,
        subsurface: 0.05,
        cavity: 0.70
    },
    obsidian: {
        label: 'Volcanic Basalt',
        albedoHex: '#52565c',
        roughness: 0.68,
        metallic: 0.12,
        subsurface: 0.10,
        cavity: 0.75
    }
};

export const SHADING_LENSES = [
    ['Lens · PBR Clay Studio', 'pbr'],
    ['Lens · MatCap Red Wax', 'matcap_wax'],
    ['Lens · MatCap Studio Grey', 'matcap_grey'],
    ['Lens · MatCap Jade Rim', 'matcap_jade'],
    ['Lens · Mean Curvature', 'curvature'],
    ['Lens · SDF Field Slice', 'slice'],
    ['Lens · World Normals |∇d|', 'normals']
];

export const KITBASH_LIBRARY = [
    {
        id: 'prim-sphere',
        category: 'Primitives',
        kind: 'csg',
        name: 'Sphere Blob',
        badge: 'UNION',
        accent: '#e8c27a',
        glyph: 'Planet',
        blurb: 'Isotropic analytical sphere with organic smooth-union blending.',
        node: { name: 'Sphere Blob', shape: 0, op: 0, size: [0.38, 0.38, 0.38], blend: 0.09, round: 0, colorHex: '#bc795c' }
    },
    {
        id: 'prim-rbox',
        category: 'Primitives',
        kind: 'csg',
        name: 'Rounded Box',
        badge: 'UNION',
        accent: '#e8c27a',
        glyph: 'Boxes',
        blurb: 'Bevelled hard-surface block with exact Euclidean corner rounding.',
        node: { name: 'Rounded Box', shape: 1, op: 0, size: [0.36, 0.26, 0.36], blend: 0.04, round: 0.05, colorHex: '#b88366' }
    },
    {
        id: 'prim-capsule',
        category: 'Primitives',
        kind: 'csg',
        name: 'Capsule Limb',
        badge: 'UNION',
        accent: '#e8c27a',
        glyph: 'Activity',
        blurb: 'Anatomical line-swept capsule segment for limbs, tubes, and struts.',
        node: { name: 'Capsule Limb', shape: 2, op: 0, size: [0.16, 0.38, 0.16], blend: 0.08, round: 0, colorHex: '#bc795c' }
    },
    {
        id: 'prim-cylinder',
        category: 'Primitives',
        kind: 'csg',
        name: 'Cylinder Boss',
        badge: 'UNION',
        accent: '#e8c27a',
        glyph: 'Current',
        blurb: 'Capped mechanical cylinder boss with optional fillet blend.',
        node: { name: 'Cylinder Boss', shape: 3, op: 0, size: [0.28, 0.22, 0.28], blend: 0.04, round: 0.02, colorHex: '#9da5ae' }
    },
    {
        id: 'prim-torus',
        category: 'Primitives',
        kind: 'csg',
        name: 'Torus Collar',
        badge: 'UNION',
        accent: '#e8c27a',
        glyph: 'Orbit',
        blurb: 'Revolved ring collar for gaskets, halos, and nozzle shrouds.',
        node: { name: 'Torus Collar', shape: 4, op: 0, size: [0.44, 0.10, 0.10], blend: 0.05, round: 0, colorHex: '#d4ab6a' }
    },
    {
        id: 'prim-horns',
        category: 'Primitives',
        kind: 'csg',
        name: 'Mirrored Horns',
        badge: 'MIRROR X',
        accent: '#8be0a9',
        glyph: 'Zap',
        blurb: 'X-mirrored capped cones with Y-twist for creature horns or aero spikes.',
        node: { name: 'Mirrored Horns', shape: 5, op: 0, position: [0.42, 0.45, 0.05], rotation: [0, 0, -36], size: [0.16, 0.34, 0.03], blend: 0.08, twist: 1.2, mirrorX: true, colorHex: '#d7ccb8' }
    },
    {
        id: 'prim-octa',
        category: 'Primitives',
        kind: 'csg',
        name: 'Octahedron Shard',
        badge: 'CHAMFER',
        accent: '#8fd3c4',
        glyph: 'Sparkles',
        blurb: 'Exact 8-faced crystal polyhedron joined with 45° architectural chamfer.',
        node: { name: 'Octahedron Shard', shape: 6, op: 3, size: [0.36, 0.36, 0.36], blend: 0.05, round: 0.01, colorHex: '#6bc4a6' }
    },
    {
        id: 'prim-hex',
        category: 'Kitbash',
        kind: 'csg',
        name: 'Hex Bolt Head',
        badge: 'KITBASH',
        accent: '#f0cfa0',
        glyph: 'Lattice',
        blurb: '6-sided fastener head with subtle edge rounding.',
        node: { name: 'Hex Bolt Head', shape: 7, op: 0, size: [0.22, 0.12, 0.22], blend: 0.025, round: 0.015, colorHex: '#a5aeb8' }
    },
    {
        id: 'prim-fins',
        category: 'Kitbash',
        kind: 'csg',
        name: 'Aero Blade Fins',
        badge: 'MIRROR X',
        accent: '#8be0a9',
        glyph: 'Leaf',
        blurb: 'Symmetrical extruded vesica airfoils for helmets, pods, and vehicles.',
        node: { name: 'Aero Blade Fins', shape: 10, op: 0, position: [0.58, 0.05, -0.1], rotation: [12, 0, -18], size: [0.38, 0.28, 0.11], blend: 0.04, round: 0.01, mirrorX: true, colorHex: '#89949f' }
    },
    {
        id: 'prim-link',
        category: 'Kitbash',
        kind: 'csg',
        name: 'Lift Tow Lug',
        badge: 'KITBASH',
        accent: '#f0cfa0',
        glyph: 'Magnet',
        blurb: 'Elongated chain-link eyelet lug for industrial hard-surface mounts.',
        node: { name: 'Lift Tow Lug', shape: 9, op: 0, position: [0, 0.62, 0], size: [0.20, 0.14, 0.06], blend: 0.03, colorHex: '#c89d62' }
    },
    {
        id: 'cut-scoop',
        category: 'Cutters',
        kind: 'csg',
        name: 'Spherical Scoop',
        badge: 'SUBTRACT',
        accent: '#ff6b6b',
        glyph: 'Target',
        blurb: 'Non-destructive spherical boolean cutter with adjustable bevel k.',
        node: { name: 'Spherical Cutter', shape: 0, op: 1, position: [0, 0.25, 0.55], size: [0.28, 0.28, 0.28], blend: 0.05, colorHex: '#3c424a' }
    },
    {
        id: 'cut-panel',
        category: 'Cutters',
        kind: 'csg',
        name: 'Panel Trench Cutter',
        badge: 'SUBTRACT',
        accent: '#ff6b6b',
        glyph: 'Scan',
        blurb: 'Sharp rectangular recess cutter for visors, vents, and armor seams.',
        node: { name: 'Panel Trench', shape: 1, op: 1, position: [0, 0.18, 0.56], size: [0.46, 0.09, 0.22], blend: 0.025, round: 0.02, colorHex: '#2c3138' }
    },
    {
        id: 'cut-bore',
        category: 'Cutters',
        kind: 'csg',
        name: 'Through-Bore Cylinder',
        badge: 'SUBTRACT',
        accent: '#ff6b6b',
        glyph: 'Crosshair',
        blurb: 'Long cylindrical drill cutter for sockets, barrels, and axle tunnels.',
        node: { name: 'Through Bore', shape: 3, op: 1, position: [0, 0.0, 0], rotation: [90, 0, 0], size: [0.20, 1.15, 0.20], blend: 0.03, colorHex: '#353940' }
    },
    {
        id: 'cut-radial',
        category: 'Cutters',
        kind: 'csg',
        name: 'Radial Louver Array',
        badge: '6× RADIAL',
        accent: '#ff6b6b',
        glyph: 'Aperture',
        blurb: '6-fold polar-array boolean slot cutter around the Y axis.',
        node: { name: 'Radial Louvers', shape: 1, op: 1, position: [0.52, 0.0, 0.0], size: [0.22, 0.06, 0.07], blend: 0.02, round: 0.015, radialCount: 6, colorHex: '#2f343b' }
    },
    {
        id: 'kit-shell',
        category: 'Kitbash',
        kind: 'csg',
        name: 'Hollow Dome Shell',
        badge: 'SHELL',
        accent: '#7ec8ff',
        glyph: 'Compass',
        blurb: 'Onion-shelled spherical containment dome (5.5 cm wall thickness).',
        node: { name: 'Hollow Dome', shape: 0, op: 0, size: [0.62, 0.62, 0.62], blend: 0.04, shell: 0.055, colorHex: '#8ab8c9' }
    },
    {
        id: 'kit-gyroid',
        category: 'Kitbash',
        kind: 'csg',
        name: 'Gyroid TPMS Core',
        badge: 'LATTICE',
        accent: '#8fd3c4',
        glyph: 'Atom',
        blurb: 'Triply periodic minimal surface (Gyroid) lightweight structural infill.',
        node: { name: 'Gyroid Core', shape: 11, op: 0, size: [0.55, 0.55, 0.55], blend: 0.04, colorHex: '#67b59b' }
    },
    {
        id: 'blank-sphere',
        category: 'Blanks',
        kind: 'blank',
        name: 'Fresh Clay Sphere',
        badge: 'RE-SEED',
        accent: '#eff4cf',
        glyph: 'Planet',
        blurb: 'Replaces the Sculpt Volume with a clean 0.72 m terracotta clay sphere.',
        blank: { shape: 0, size: [0.72, 0.72, 0.72], round: 0, colorHex: '#bc795c' }
    },
    {
        id: 'blank-block',
        category: 'Blanks',
        kind: 'blank',
        name: 'Rounded Clay Billet',
        badge: 'RE-SEED',
        accent: '#eff4cf',
        glyph: 'Boxes',
        blurb: 'Replaces the Sculpt Volume with a bevelled studio block ready for trimming.',
        blank: { shape: 1, size: [0.58, 0.68, 0.48], round: 0.12, colorHex: '#94989e' }
    }
];

let _NodeCounter = 1;
export function MakeCsgNode(Partial = {})
{
    const Id = Partial.id || `csg-${_NodeCounter++}`;
    const Col = Partial.color ? Partial.color.slice() : HexToLinearRgb(Partial.colorHex || '#bc795c');
    return {
        id: Id,
        name: Partial.name || 'SDF Primitive',
        shape: Partial.shape ?? 0,
        op: Partial.op ?? 0,
        enabled: Partial.enabled !== false,
        locked: !!Partial.locked,
        position: (Partial.position || [0, 0.15, 0]).slice(),
        rotation: (Partial.rotation || [0, 0, 0]).slice(),
        size: (Partial.size || [0.36, 0.36, 0.36]).slice(),
        scale: Partial.scale ?? 1.0,
        blend: Partial.blend ?? 0.06,
        round: Partial.round ?? 0.0,
        shell: Partial.shell ?? 0.0,
        twist: Partial.twist ?? 0.0,
        mirrorX: !!Partial.mirrorX,
        mirrorZ: !!Partial.mirrorZ,
        radialCount: Partial.radialCount || 1,
        color: Col
    };
}

/**
 * Populates one of the 4 starter studio scenes into { volume, sceneState }.
 */
export function BuildStarterScene(PresetKey, Volume)
{
    const Terracotta = HexToLinearRgb('#bc795c');
    const DarkClay = HexToLinearRgb('#8f543e');
    const Bone = HexToLinearRgb('#d7ccb8');
    const Alloy = HexToLinearRgb('#828c96');
    const DarkCut = HexToLinearRgb('#2a2f36');
    const Gold = HexToLinearRgb('#d6a85a');
    const Jade = HexToLinearRgb('#58ab8c');
    const Basalt = HexToLinearRgb('#5c6168');

    if (PresetKey === 'mech-pod')
    {
        Volume.clearVolume(Alloy);
        // Bake core aero fuselages into clay so user can immediately sculpt dents/welds OR tweak live CSG!
        Volume.bakeCsgNodes([
            MakeCsgNode({ shape: 1, op: 0, position: [0, 0.06, 0], size: [0.68, 0.36, 0.86], round: 0.18, blend: 0.04, color: Alloy }),
            MakeCsgNode({ shape: 8, op: 0, position: [0, 0.24, -0.06], size: [0.54, 0.34, 0.68], blend: 0.12, color: Alloy })
        ], false);

        return {
            key: 'mech-pod',
            name: 'Hard-Surface Pod',
            materialPreset: 'gunmetal',
            lens: 'pbr',
            symmetry: { x: true, y: false, z: false, radial: 1 },
            csgNodes: [
                MakeCsgNode({ id: 'visor-cut', name: 'Optic Visor Trench', shape: 1, op: 1, position: [0, 0.14, 0.78], rotation: [-8, 0, 0], size: [0.52, 0.11, 0.26], round: 0.03, blend: 0.03, color: DarkCut }),
                MakeCsgNode({ id: 'optic-eye', name: 'Scanner Core Lens', shape: 0, op: 0, position: [0, 0.14, 0.66], size: [0.16, 0.16, 0.16], blend: 0.02, color: Gold }),
                MakeCsgNode({ id: 'side-thrusters', name: 'Twin Thruster Pods', shape: 3, op: 0, position: [0.74, -0.04, -0.18], rotation: [90, 0, 0], size: [0.22, 0.42, 0.22], round: 0.03, blend: 0.06, mirrorX: true, color: Alloy }),
                MakeCsgNode({ id: 'thruster-bores', name: 'Nozzle Bores', shape: 3, op: 1, position: [0.74, -0.04, -0.58], rotation: [90, 0, 0], size: [0.14, 0.22, 0.14], blend: 0.025, mirrorX: true, color: DarkCut }),
                MakeCsgNode({ id: 'aero-wings', name: 'Swept Stabilizer Fins', shape: 10, op: 0, position: [0.82, 0.16, -0.22], rotation: [8, 12, -28], size: [0.44, 0.28, 0.09], round: 0.01, blend: 0.045, mirrorX: true, color: Gold }),
                MakeCsgNode({ id: 'top-collar', name: 'Top Docking Ring', shape: 4, op: 0, position: [0, 0.50, -0.08], size: [0.28, 0.055, 0.055], blend: 0.03, color: Alloy })
            ]
        };
    }

    if (PresetKey === 'gyroid-relic')
    {
        Volume.clearVolume(Jade);
        Volume.bakeCsgNodes([
            MakeCsgNode({ shape: 3, op: 0, position: [0, -0.78, 0], size: [0.72, 0.12, 0.72], round: 0.04, blend: 0.04, color: Basalt })
        ], false);

        return {
            key: 'gyroid-relic',
            name: 'Gyroid Lattice',
            materialPreset: 'jade',
            lens: 'pbr',
            symmetry: { x: true, y: false, z: true, radial: 1 },
            csgNodes: [
                MakeCsgNode({ id: 'gyroid-sphere', name: 'Gyroid TPMS Matrix', shape: 11, op: 0, position: [0, 0.10, 0], size: [0.74, 0.74, 0.74], round: 0.0, blend: 0.04, color: Jade }),
                MakeCsgNode({ id: 'bound-sphere', name: 'Spherical Trim Envelope', shape: 0, op: 2, position: [0, 0.10, 0], size: [0.78, 0.78, 0.78], blend: 0.02, color: Jade }),
                MakeCsgNode({ id: 'equator-ring', name: 'Equatorial Gimbal Ring', shape: 4, op: 0, position: [0, 0.10, 0], rotation: [18, 0, 12], size: [0.86, 0.055, 0.055], blend: 0.03, color: Gold }),
                MakeCsgNode({ id: 'core-cavity', name: 'Central Chamber Cut', shape: 0, op: 1, position: [0, 0.10, 0], size: [0.34, 0.34, 0.34], blend: 0.04, color: DarkCut }),
                MakeCsgNode({ id: 'inner-crystal', name: 'Suspended Octa Core', shape: 6, op: 0, position: [0, 0.10, 0], rotation: [0, 35, 0], size: [0.22, 0.22, 0.22], blend: 0.02, color: Gold })
            ]
        };
    }

    if (PresetKey === 'rock-monolith')
    {
        Volume.clearVolume(Basalt);
        Volume.bakeCsgNodes([
            MakeCsgNode({ shape: 1, op: 0, position: [0, -0.05, 0], rotation: [4, 18, -6], size: [0.62, 0.92, 0.52], round: 0.08, blend: 0.05, color: Basalt }),
            MakeCsgNode({ shape: 1, op: 0, position: [-0.42, -0.38, 0.18], rotation: [-8, -24, 12], size: [0.44, 0.56, 0.42], round: 0.06, blend: 0.12, color: Basalt }),
            MakeCsgNode({ shape: 1, op: 0, position: [0.46, -0.46, -0.14], rotation: [10, 32, -10], size: [0.40, 0.46, 0.38], round: 0.06, blend: 0.12, color: Basalt })
        ], false);

        // Apply authentic Flatten & Carve strokes to give immediate hand-sculpted rock facets
        const RockSettings = {
            radius: 0.48, strength: 0.88, smoothness: 0.2, invert: false,
            brushColor: Basalt, paintWithSculpt: false,
            symmetry: { x: false, y: false, z: false, radial: 1 }
        };
        Volume.applyBrushDab('flatten', [0.35, 0.55, 0.38], [0.35, 0.55, 0.38], [0.58, 0.45, 0.68], [0, 0, 0], RockSettings);
        Volume.applyBrushDab('flatten', [-0.38, 0.42, 0.36], [-0.38, 0.42, 0.36], [-0.62, 0.35, 0.70], [0, 0, 0], RockSettings);
        Volume.applyBrushDab('carve', [-0.15, 0.25, 0.52], [0.22, -0.18, 0.48], [0, 0, 1], [0.3, -0.4, 0], {
            ...RockSettings, radius: 0.13, strength: 0.65, smoothness: 0.25
        });

        return {
            key: 'rock-monolith',
            name: 'Rock Monolith',
            materialPreset: 'obsidian',
            lens: 'pbr',
            symmetry: { x: false, y: false, z: false, radial: 1 },
            csgNodes: [
                MakeCsgNode({ id: 'crystal-cluster-1', name: 'Amber Quartz Spire', shape: 6, op: 0, position: [0.28, 0.32, 0.30], rotation: [22, 15, -24], size: [0.34, 0.48, 0.34], blend: 0.03, color: Gold }),
                MakeCsgNode({ id: 'crystal-cluster-2', name: 'Secondary Crystal', shape: 6, op: 0, position: [-0.36, -0.12, 0.38], rotation: [-18, 28, 30], size: [0.24, 0.34, 0.24], blend: 0.03, color: Jade })
            ]
        };
    }

    // Default scene: 'sculpt-bust' ("Sculpt Bust · Clay")
    Volume.clearVolume(Terracotta);
    Volume.bakeCsgNodes([
        // Cranium + Jaw + Neck + Plinth baked directly into 3D Sculpt Clay so user can sculpt every feature immediately!
        MakeCsgNode({ shape: 8, op: 0, position: [0, 0.32, 0.0], size: [0.52, 0.60, 0.56], blend: 0.06, color: Terracotta }),
        MakeCsgNode({ shape: 1, op: 0, position: [0, -0.02, 0.14], rotation: [12, 0, 0], size: [0.32, 0.28, 0.30], round: 0.14, blend: 0.18, color: Terracotta }),
        MakeCsgNode({ shape: 2, op: 0, position: [0, -0.42, -0.06], rotation: [10, 0, 0], size: [0.24, 0.32, 0.24], blend: 0.16, color: Terracotta }),
        MakeCsgNode({ shape: 8, op: 0, position: [0, -0.78, -0.02], size: [0.78, 0.28, 0.48], blend: 0.18, color: Terracotta }),
        MakeCsgNode({ shape: 3, op: 0, position: [0, -1.04, -0.02], size: [0.64, 0.10, 0.64], round: 0.03, blend: 0.05, color: DarkClay })
    ], false);

    // Sculpt anatomical brow, nose bridge, cheekbones, and eye hollows right onto the volume!
    const SymX = { x: true, y: false, z: false, radial: 1 };
    Volume.applyBrushDab('clay', [0.0, 0.36, 0.50], [0.26, 0.40, 0.44], [0, 0.2, 0.98], [0.26, 0.04, -0.06], {
        radius: 0.16, strength: 0.72, smoothness: 0.38, brushColor: Terracotta, paintWithSculpt: true, symmetry: SymX
    });
    Volume.applyBrushDab('carve', [0.19, 0.25, 0.49], [0.19, 0.25, 0.49], [0.2, 0, 0.98], [0, 0, 0], {
        radius: 0.15, strength: 0.68, smoothness: 0.35, brushColor: DarkClay, paintWithSculpt: true, symmetry: SymX
    });
    Volume.applyBrushDab('clay', [0.0, 0.32, 0.54], [0.0, 0.10, 0.60], [0, 0.1, 0.99], [0, -0.22, 0.06], {
        radius: 0.13, strength: 0.76, smoothness: 0.35, brushColor: Terracotta, paintWithSculpt: true, symmetry: { x: false, y: false, z: false, radial: 1 }
    });
    Volume.applyBrushDab('clay', [0.30, 0.12, 0.38], [0.18, 0.04, 0.46], [0.5, 0, 0.86], [-0.12, -0.08, 0.08], {
        radius: 0.17, strength: 0.62, smoothness: 0.42, brushColor: Terracotta, paintWithSculpt: true, symmetry: SymX
    });

    return {
        key: 'sculpt-bust',
        name: 'Sculpt Bust · Clay',
        materialPreset: 'terracotta',
        lens: 'pbr',
        symmetry: { x: true, y: false, z: false, radial: 1 },
        csgNodes: [
            MakeCsgNode({
                id: 'crown-horns',
                name: 'Swept Crown Horns',
                shape: 5,
                op: 0,
                position: [0.46, 0.68, -0.05],
                rotation: [-14, 0, -38],
                size: [0.17, 0.36, 0.035],
                blend: 0.11,
                twist: 1.1,
                mirrorX: true,
                color: Bone
            }),
            MakeCsgNode({
                id: 'collar-torus',
                name: 'Collar Gorget Ring',
                shape: 4,
                op: 0,
                position: [0, -0.60, 0.02],
                rotation: [14, 0, 0],
                size: [0.44, 0.075, 0.075],
                blend: 0.07,
                color: Gold
            }),
            MakeCsgNode({
                id: 'temple-cut',
                name: 'Temple Relief Groove',
                shape: 1,
                op: 1,
                position: [0.49, 0.30, 0.08],
                rotation: [0, 12, 10],
                size: [0.08, 0.05, 0.28],
                round: 0.02,
                blend: 0.035,
                mirrorX: true,
                color: DarkClay
            })
        ]
    };
}
