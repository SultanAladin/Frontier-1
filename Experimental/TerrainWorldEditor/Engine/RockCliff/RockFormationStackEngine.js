/*==========================================================================================================================================
                                                 ROCKFORMATIONSTACKENGINE.JS
==========================================================================================================================================*/
// RockFormation 2.0 — Composable Layer-Stack Pure-SDF Plain Rock Shape Engine.
//
// Design Principles:
//   1. 100% Layer-Stack Driven: Every rock formation is an ordered stack of Pure-SDF nodes combined via
//      Union, Smooth Blend (smin), 45° Chamfer Seam, Boolean Subtract (Cutter), or Intersect.
//   2. Foundation Stage — Pure Plain Rock Shapes Only: ZERO fractures, ZERO thermal crazing, ZERO onion-skin
//      peeling, ZERO chemical tafoni/karren noise processing. Clean sculptural rock silhouettes ready to serve
//      as foundational shape generators in the stack.
//   3. Reuses Core Algorithms from Rock Formation 1.0: Strike/Dip half-space polyhedral clipping, C-infinity
//      Log-Sum-Exp & 45° chamfer rounding, hexagonal basalt lattice prisms, 4^3 Sparse-Brick evaluation, and
//      3x3 Tikhonov QEF Dual Contouring (.GLB / .OBJ / .STL) — with strictly 0 Mesh->SDF conversions!

import {
    DEG2RAD,
    clamp,
    mix,
    smoothstep,
    vdot,
    vnorm,
    buildStrikeDipFrame,
    smin,
    smax,
    chamferMax,
    createRng
} from './Core/SpatialNoise3D.js';
import {
    createBoxPoly,
    clipPolyByHalfSpace,
    measurePoly,
    sculptGeologicalParentPoly
} from './PureSdfFractureEngine.js';
import { evaluateSparseSdf } from './Core/SparseSdfBrickGrid.js';
import {
    extractDualContourMesh,
    removeFloatingSpeckles,
    taubinSmoothCreasePreserving,
    buildGameReadyAttributes
} from './Core/QefDualContour.js';

/*==========================================================================================================================================
                                   1. LAYER COMBINE MODES, STONE TONES & NODE CATALOG
==========================================================================================================================================*/

export const ROCK2_COMBINE_MODES = [
    ['Union (Add Solid Rock)', 'union'],
    ['Smooth Blend (Fuse Mass)', 'smooth_union'],
    ['Chamfer Seam (45° Contact)', 'chamfer_union'],
    ['Subtract (Carve / Cavity)', 'subtract'],
    ['Intersect (Clip Volume)', 'intersect']
];

export const ROCK2_COMBINE_OP_CODE = {
    union: 0,
    smooth_union: 1,
    chamfer_union: 2,
    subtract: 3,
    intersect: 4
};

export const ROCK2_STONE_TONES = {
    clay:       { label: 'Studio Stone Grey',  hex: '#94918c', rgb: [0.58, 0.57, 0.55] },
    basalt:     { label: 'Dark Basalt Slate',  hex: '#454a52', rgb: [0.27, 0.29, 0.32] },
    granite:    { label: 'Pale Sierra Granite',hex: '#aea69c', rgb: [0.68, 0.65, 0.61] },
    sandstone:  { label: 'Warm Desert Buff',   hex: '#b88f6b', rgb: [0.72, 0.56, 0.42] },
    cobble:     { label: 'River Cobble Blue',  hex: '#757d85', rgb: [0.46, 0.49, 0.52] },
    limestone:  { label: 'Karst Cream Stone',  hex: '#bab5a6', rgb: [0.73, 0.71, 0.65] },
    redSlate:   { label: 'Red Canyon Slate',   hex: '#9e5e4d', rgb: [0.62, 0.37, 0.30] }
};

export const ROCK2_STONE_TONE_OPTIONS = Object.entries(ROCK2_STONE_TONES).map(([k, v]) => [v.label, k]);

/**
 * 9 Foundational Plain Rock Generator Node Types:
 * Capable of generating any plain rock or compound rock shape (Basalt, Small Rocks, Big Boulders,
 * Smooth Rocks, Cliffs, Spires/Hoodoos, Flat Slabs/Ledges, Natural Arches, and Custom Parametric Shapes).
 */
export const ROCK2_NODE_CATALOG = [
    {
        type: 'big_boulder',
        name: 'Big Boulder',
        shortBadge: 'BOULDER',
        glyph: 'Mountain',
        accent: '#e8c27a',
        desc: 'Massive plain angular-to-subrounded boulder, erratic monolith, overhanging prow or pedestal block',
        defaults: {
            stoneTone: 'granite',
            seed: 2104,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.8, height: 3.9, length: 4.1,
            boulderStyle: 'angular',
            facetCount: 12,
            roundness: 0.24,
            angularity: 0.22,
            taper: 0.08,
            overhangProw: 0.0,
            asymmetry: 0.0,
            flatBottom: 1.0,
            flatTop: 0.0
        },
        controlGroups: [
            {
                title: 'Boulder Silhouette & Faceting',
                badge: 'SHAPE',
                glyph: 'Mountain',
                hint: 'Control plain boulder style, facet count, corner rounding, angularity & overhanging prow.',
                controls: [
                    {
                        type: 'select', key: 'boulderStyle', label: 'Boulder Archetype',
                        options: [
                            ['Angular Erratic Megalith', 'angular'],
                            ['Sub-Rounded Granite Boulder', 'subrounded'],
                            ['Overhanging Climbing Prow', 'prow'],
                            ['Blocky Cuboidal Monolith', 'blocky'],
                            ['Balanced Narrow-Base Boulder', 'pedestal']
                        ]
                    },
                    { key: 'facetCount', label: 'Stone Facet Planes', min: 6, max: 24, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Edge & Corner Rounding', min: 0.02, max: 0.92, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'angularity', label: 'Facet Angle Variation', min: 0.02, max: 0.45, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Crown Taper / Flare', min: -0.65, max: 0.80, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'overhangProw', label: 'Forward Prow Lean', min: -0.60, max: 0.60, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'asymmetry', label: 'Crest Asymmetry', min: -0.65, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatBottom', label: 'Planted Flat Base', min: 0.0, max: 1.0, step: 1.0, decimals: 0, unit: '' },
                    { key: 'flatTop', label: 'Tabletop Cap Cut', min: 0.0, max: 0.85, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'smooth_rock',
        name: 'Smooth Rock',
        shortBadge: 'SMOOTH',
        glyph: 'Planet',
        accent: '#9bd1e5',
        desc: 'Plain water-worn river cobble, smooth pebble, batholith dome, zen pillow stone or egg boulder',
        defaults: {
            stoneTone: 'cobble',
            seed: 3318,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.4, height: 2.9, length: 3.6,
            smoothStyle: 'cobble',
            roundness: 0.86,
            squarenessHoriz: 0.95,
            squarenessVert: 0.92,
            softFacets: 0.18,
            taper: 0.10,
            bend: 0.0,
            asymmetry: 0.08,
            flatBottom: 0.75
        },
        controlGroups: [
            {
                title: 'Smooth Cobble & Dome Contour',
                badge: 'SMOOTH SDF',
                glyph: 'Planet',
                hint: 'Morph smoothly between pure spheroidal river cobbles, pillowed zen stones, eggs & domes.',
                controls: [
                    {
                        type: 'select', key: 'smoothStyle', label: 'Smooth Form Type',
                        options: [
                            ['River Cobble / Pebble', 'cobble'],
                            ['Rounded Batholith Dome', 'dome'],
                            ['Pillowed Cushion Stone', 'pillow'],
                            ['Ovoid Egg / Glacial Stone', 'egg'],
                            ['Multi-Lobe Smooth Mass', 'lobes']
                        ]
                    },
                    { key: 'roundness', label: 'Overall Smoothness', min: 0.25, max: 1.00, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'squarenessHoriz', label: 'Plan Roundness (Box↔Circle)', min: 0.35, max: 1.50, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'squarenessVert', label: 'Profile Curve (Slab↔Sphere)', min: 0.35, max: 1.50, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'softFacets', label: 'Subtle Plane Faceting', min: 0.0, max: 0.85, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Egg / Dome Taper', min: -0.55, max: 0.75, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bend', label: 'Kidney / Bean Curvature', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'asymmetry', label: 'Summit Skew', min: -0.60, max: 0.60, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatBottom', label: 'Grounded Base Cut', min: 0.0, max: 1.0, step: 0.05, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'small_rocks',
        name: 'Small Rocks',
        shortBadge: 'SMALL ROCKS',
        glyph: 'Sparkles',
        accent: '#c8d99b',
        desc: 'Scatter or cluster of plain small rocks, pebbles, angular scree chunks or stepping stones (1–24 rocks)',
        defaults: {
            stoneTone: 'clay',
            seed: 5190,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 5.6, height: 1.1, length: 5.2,
            layoutPattern: 'cluster',
            rockCount: 11,
            sizeMin: 0.32,
            sizeMax: 1.15,
            heroCenterBoost: 0.45,
            roundness: 0.35,
            flatness: 0.38,
            tiltRandomness: 0.35,
            embedDepth: 0.18
        },
        controlGroups: [
            {
                title: 'Small Rock Scatter & Shapes',
                badge: 'SCATTER',
                glyph: 'Sparkles',
                hint: 'Generate 1 to 24 plain small stones — from smooth river pebbles to sharp angular talus chunks.',
                controls: [
                    {
                        type: 'select', key: 'layoutPattern', label: 'Scatter Pattern',
                        options: [
                            ['Natural Boulder + Satellites Cluster', 'cluster'],
                            ['Perimeter Ring / Talus Apron', 'ring'],
                            ['Stepping-Stone Trail / Ridge Line', 'line'],
                            ['Even Cobble Field', 'grid']
                        ]
                    },
                    { key: 'rockCount', label: 'Number of Rocks', min: 1, max: 24, step: 1, decimals: 0, unit: 'rk' },
                    { key: 'sizeMin', label: 'Smallest Rock Size', min: 0.15, max: 1.80, step: 0.02, decimals: 2, unit: 'm' },
                    { key: 'sizeMax', label: 'Largest Rock Size', min: 0.25, max: 2.60, step: 0.02, decimals: 2, unit: 'm' },
                    { key: 'heroCenterBoost', label: 'Center Hero Dominance', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Angular Scree ↔ Smooth Pebble', min: 0.04, max: 0.96, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatness', label: 'Chunky ↔ Flat Slate Ratio', min: 0.0, max: 0.80, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'tiltRandomness', label: 'Random Tumble Tilt', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'embedDepth', label: 'Ground Embed Depth', min: 0.0, max: 0.65, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'cliff_face',
        name: 'Cliff Escarpment',
        shortBadge: 'CLIFF',
        glyph: 'Layers',
        accent: '#e09f6d',
        desc: 'Plain vertical cliff wall, stepped canyon benches, overhanging headland brow or curved amphitheater',
        defaults: {
            stoneTone: 'sandstone',
            seed: 7042,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 7.6, height: 5.4, length: 3.4,
            cliffProfile: 'stepped',
            buttressCount: 4,
            buttressBlend: 0.22,
            stepBenches: 3,
            overhangLean: 0.08,
            wallCurvature: 0.0,
            roundness: 0.14,
            crestSlope: 0.08
        },
        controlGroups: [
            {
                title: 'Cliff Wall Architecture',
                badge: 'ESCARPMENT',
                glyph: 'Layers',
                hint: 'Sculpt plain cliff walls from interlocking structural buttresses, terrace benches, overhangs & curves.',
                controls: [
                    {
                        type: 'select', key: 'cliffProfile', label: 'Cliff Profile',
                        options: [
                            ['Stepped Canyon Escarpment', 'stepped'],
                            ['Sheer Vertical Headwall', 'vertical'],
                            ['Overhanging Sea-Cliff Brow', 'overhang'],
                            ['Slanted Cuesta Rampart', 'rampart'],
                            ['Mesa Caprock Promontory', 'mesa']
                        ]
                    },
                    { key: 'buttressCount', label: 'Structural Buttresses', min: 1, max: 7, step: 1, decimals: 0, unit: 'bt' },
                    { key: 'buttressBlend', label: 'Buttress Seam Fusion', min: 0.0, max: 0.90, step: 0.02, decimals: 2, unit: 'm' },
                    { key: 'stepBenches', label: 'Terrace Bench Tiers', min: 0, max: 5, step: 1, decimals: 0, unit: 'tr' },
                    { key: 'overhangLean', label: 'Slope Back ↔ Overhang Brow', min: -0.55, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'wallCurvature', label: 'Concave Cove ↔ Promontory', min: -0.80, max: 0.80, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Cliff Arris Rounding', min: 0.02, max: 0.70, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'crestSlope', label: 'Rim Crest Tilt', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'columnar_basalt',
        name: 'Columnar Basalt',
        shortBadge: 'BASALT',
        glyph: 'Boxes',
        accent: '#9aa8bd',
        desc: 'Plain polygonal 4/5/6/7-sided basalt columns — stepped colonnade, volcanic plug, causeway or splayed fan',
        defaults: {
            stoneTone: 'basalt',
            seed: 5512,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 5.8, height: 5.0, length: 4.8,
            formationShape: 'cliff',
            polygonSides: 6,
            columnDiameter: 0.95,
            columnGap: 0.04,
            heightSteps: 6,
            edgeBevel: 0.08,
            columnSplay: 0.06,
            capTilt: 0.10
        },
        controlGroups: [
            {
                title: 'Polygonal Basalt Colonnade',
                badge: 'HEX PRISMS',
                glyph: 'Boxes',
                hint: 'Pure plain polygonal basalt prisms with controllable side count, column diameter, terrace steps & splay.',
                controls: [
                    {
                        type: 'select', key: 'formationShape', label: 'Colonnade Envelope',
                        options: [
                            ['Stepped Cliff Colonnade', 'cliff'],
                            ['Volcanic Plug / Tower Mound', 'mound'],
                            ['Giant’s Causeway Pavement', 'pavement'],
                            ['Curved Cove Amphitheater', 'amphitheater'],
                            ['Single Hero Basalt Column', 'single']
                        ]
                    },
                    {
                        type: 'select', key: 'polygonSides', label: 'Column Polygon Sides',
                        options: [
                            ['6-Sided Classic Hexagonal', 6],
                            ['Mixed 5 / 6 / 7 Natural Prisms', 0],
                            ['5-Sided Pentagonal Prisms', 5],
                            ['4-Sided Tetragonal Prisms', 4],
                            ['8-Sided Octagonal Columns', 8]
                        ]
                    },
                    { key: 'columnDiameter', label: 'Column Diameter', min: 0.42, max: 2.20, step: 0.02, decimals: 2, unit: 'm' },
                    { key: 'columnGap', label: 'Inter-Column Spacing', min: 0.00, max: 0.35, step: 0.01, decimals: 2, unit: 'm' },
                    { key: 'heightSteps', label: 'Terrace Step Quantization', min: 1, max: 12, step: 1, decimals: 0, unit: 'st' },
                    { key: 'edgeBevel', label: 'Prism Edge Bevel', min: 0.01, max: 0.45, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'columnSplay', label: 'Radial Fan / Splay Lean', min: -0.45, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'capTilt', label: 'Top Facet Slant', min: 0.0, max: 0.45, step: 0.01, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'spire_pinnacle',
        name: 'Spire & Pinnacle',
        shortBadge: 'SPIRE',
        glyph: 'Activity',
        accent: '#d9b4f0',
        desc: 'Plain tapered rock needle, alpine horn, twisted spire, desert obelisk or waist-pinched hoodoo pillar',
        defaults: {
            stoneTone: 'limestone',
            seed: 8814,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 2.8, height: 6.2, length: 2.6,
            spireStyle: 'horn',
            facetSides: 7,
            roundness: 0.16,
            taper: 0.58,
            waistPinch: 0.0,
            twistDeg: 14,
            bendX: 0.12,
            flatCap: 0.08
        },
        controlGroups: [
            {
                title: 'Spire, Horn & Hoodoo Geometry',
                badge: 'PINNACLE',
                glyph: 'Activity',
                hint: 'Sculpt plain needles, cantilevered alpine horns, obelisks & waist-pinched hoodoo pillars.',
                controls: [
                    {
                        type: 'select', key: 'spireStyle', label: 'Pinnacle Type',
                        options: [
                            ['Curved Alpine Horn', 'horn'],
                            ['Sharp Tapered Needle', 'needle'],
                            ['Waist-Pinched Hoodoo Pillar', 'hoodoo'],
                            ['Flat-Topped Butte Tower', 'butte']
                        ]
                    },
                    { key: 'facetSides', label: 'Perimeter Facet Sides', min: 4, max: 14, step: 1, decimals: 0, unit: 'sd' },
                    { key: 'roundness', label: 'Ridge Rounding', min: 0.02, max: 0.85, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Summit Taper', min: -0.35, max: 0.90, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'waistPinch', label: 'Stem Waist Pinch', min: -0.35, max: 0.68, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'twistDeg', label: 'Helical Torsion Twist', min: -90, max: 90, step: 1, decimals: 0, unit: '°' },
                    { key: 'bendX', label: 'Horn Cantilever Bend', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatCap', label: 'Summit Cap Bevel', min: 0.0, max: 0.70, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'rock_slab',
        name: 'Slab & Flatiron',
        shortBadge: 'SLAB',
        glyph: 'Scan',
        accent: '#f0b899',
        desc: 'Plain tabular rock slab, tilted flatiron hogback, cantilevered cliff shelf ledge or flagstone plate',
        defaults: {
            stoneTone: 'redSlate',
            seed: 4419,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 5.8, height: 1.15, length: 4.4,
            planShape: 'plate',
            dipAngleDeg: 18,
            roundness: 0.12,
            edgeTaper: 0.22,
            bend: 0.0
        },
        controlGroups: [
            {
                title: 'Tabular Slab & Flatiron Controls',
                badge: 'TABULAR',
                glyph: 'Scan',
                hint: 'Plain tabular stone plates, tilted flatiron fins, triangular wedges & cantilevered ledges.',
                controls: [
                    {
                        type: 'select', key: 'planShape', label: 'Slab Plan Silhouette',
                        options: [
                            ['Polygonal Stone Plate', 'plate'],
                            ['Tilted Triangular Flatiron', 'wedge'],
                            ['Elongated Shelf Ledge', 'ledge'],
                            ['Smooth Rounded Flagstone', 'flagstone']
                        ]
                    },
                    { key: 'dipAngleDeg', label: 'Bedding Dip Tilt', min: -65, max: 65, step: 1, decimals: 0, unit: '°' },
                    { key: 'roundness', label: 'Rim Rounding', min: 0.02, max: 0.85, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'edgeTaper', label: 'Chisel Rim Bevel', min: -0.45, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bend', label: 'Slab Saddle Warp', min: -0.40, max: 0.40, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'arch_formation',
        name: 'Natural Rock Arch',
        shortBadge: 'ARCH',
        glyph: 'Arch',
        accent: '#eff4cf',
        desc: 'Plain sculptural rock arch, natural bridge or sea-cliff window with clean Pure-SDF span geometry',
        defaults: {
            stoneTone: 'sandstone',
            seed: 6180,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 7.4, height: 4.8, length: 2.6,
            archProfile: 'vault',
            openingWidth: 0.54,
            openingHeight: 0.58,
            roundness: 0.28,
            buttressFlare: 0.25,
            archSkew: 0.0
        },
        controlGroups: [
            {
                title: 'Natural Arch & Span Geometry',
                badge: 'PURE ARCH',
                glyph: 'Arch',
                hint: 'Pure-SDF rock bridge sculpted from flared stone buttresses and a clean tunable arch vault.',
                controls: [
                    {
                        type: 'select', key: 'archProfile', label: 'Arch Opening Style',
                        options: [
                            ['Rounded Desert Vault', 'vault'],
                            ['Pointed Cathedral Arch', 'pointed'],
                            ['Flat-Lintelled Stone Bridge', 'lintel'],
                            ['Keyhole / Sea-Cave Window', 'keyhole']
                        ]
                    },
                    { key: 'openingWidth', label: 'Arch Span Width', min: 0.22, max: 0.78, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'openingHeight', label: 'Arch Clear Height', min: 0.22, max: 0.76, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Arch & Pier Rounding', min: 0.05, max: 0.80, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'buttressFlare', label: 'Base Pier Flare', min: 0.0, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'archSkew', label: 'Span Asymmetry', min: -0.45, max: 0.45, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'custom_sculpt_rock',
        name: 'Custom Parametric Rock',
        shortBadge: 'CUSTOM',
        glyph: 'Sliders',
        accent: '#8ee5b8',
        desc: 'Universal Pure-SDF rock shape generator — morph continuously across spheres, crystals, boxes, cylinders, tusks & custom rocks',
        defaults: {
            stoneTone: 'clay',
            seed: 9012,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.5, height: 4.0, length: 4.0,
            analyticBlend: 0.50,
            expHoriz: 0.85,
            expVert: 0.85,
            facetCount: 14,
            roundness: 0.32,
            angularity: 0.18,
            taper: 0.12,
            waistPinch: 0.0,
            twistDeg: 0,
            bendX: 0.0,
            bendZ: 0.0,
            skewX: 0.0,
            flatBottom: 0.85
        },
        controlGroups: [
            {
                title: 'Universal Hybrid Shape Kernel',
                badge: 'PARAMETRIC',
                glyph: 'Sliders',
                hint: 'Blend Faceted Half-Space Polyhedra with 3D Superquadrics + Taper, Pinch, Twist, Bend & Skew to make ANY shape.',
                controls: [
                    { key: 'analyticBlend', label: 'Faceted Poly ↔ Analytic Superquadric', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'expHoriz', label: 'Plan Exponent (Box↔Circle↔Diamond)', min: 0.25, max: 1.85, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'expVert', label: 'Vertical Exponent (Cyl↔Sphere↔Cone)', min: 0.25, max: 1.85, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'facetCount', label: 'Polyhedron Facet Count', min: 6, max: 26, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Facet Rounding', min: 0.02, max: 0.95, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'angularity', label: 'Facet Dispersion', min: 0.0, max: 0.45, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Vertical Taper / Flare', min: -0.70, max: 0.88, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'waistPinch', label: 'Mid-Waist Pinch / Bulge', min: -0.45, max: 0.68, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'twistDeg', label: 'Helical Twist', min: -120, max: 120, step: 2, decimals: 0, unit: '°' },
                    { key: 'bendX', label: 'Bend X', min: -0.65, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bendZ', label: 'Bend Z', min: -0.65, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'skewX', label: 'Shear / Skew X', min: -0.65, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatBottom', label: 'Flat Base Contact', min: 0.0, max: 1.0, step: 0.05, decimals: 2, unit: 'w' }
                ]
            }
        ]
    }
];

export function getRock2NodeSpec(type)
{
    return ROCK2_NODE_CATALOG.find(n => n.type === type) || ROCK2_NODE_CATALOG[0];
}

let _nextRock2LayerSeq = 1;

export function createRock2Layer(type = 'big_boulder', overrides = {})
{
    const spec = getRock2NodeSpec(type);
    const seq = _nextRock2LayerSeq++;
    const params = { ...spec.defaults, ...(overrides.params || {}) };
    // Allow flat overrides on top-level call for convenience: createRock2Layer('cliff_face', { width: 8, combineMode: 'union' })
    for (const k of Object.keys(spec.defaults))
    {
        if (overrides[k] !== undefined) params[k] = overrides[k];
    }
    return {
        id: overrides.id || `r2_layer_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
        name: overrides.name || `${spec.name} ${String(seq).padStart(2, '0')}`,
        type: spec.type,
        enabled: overrides.enabled !== undefined ? !!overrides.enabled : true,
        solo: !!overrides.solo,
        combineMode: overrides.combineMode || 'union',
        blendRadius: overrides.blendRadius ?? 0.35,
        params
    };
}

export function cloneRock2Layer(layer)
{
    const seq = _nextRock2LayerSeq++;
    return {
        id: `r2_layer_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
        name: `${layer.name} Copy`,
        type: layer.type,
        enabled: layer.enabled,
        solo: false,
        combineMode: layer.combineMode,
        blendRadius: layer.blendRadius,
        params: JSON.parse(JSON.stringify(layer.params))
    };
}

/*==========================================================================================================================================
                                     2. STARTER LAYER-STACK PRESETS (PLAIN ROCKS)
==========================================================================================================================================*/

export const ROCK2_STARTER_STACKS = [
    {
        id: 'stack-cliff-outcrop',
        name: '01 · Cliff + Boulders + Small Rocks Stack',
        badge: '3-LAYER STACK',
        buildLayers: () => [
            createRock2Layer('cliff_face', {
                name: 'Base Stepped Cliff Wall',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 7042,
                posZ: -1.1,
                width: 8.2, height: 5.6, length: 3.4,
                cliffProfile: 'stepped', buttressCount: 4, stepBenches: 3, roundness: 0.14
            }),
            createRock2Layer('big_boulder', {
                name: 'Fused Front Buttress Boulder',
                combineMode: 'smooth_union',
                blendRadius: 0.45,
                stoneTone: 'sandstone',
                seed: 2188,
                posX: -1.6, posY: 0.0, posZ: 1.15,
                yawDeg: 22,
                width: 3.6, height: 2.8, length: 3.1,
                boulderStyle: 'angular', roundness: 0.22
            }),
            createRock2Layer('small_rocks', {
                name: 'Basal Plain Small Rocks',
                combineMode: 'union',
                stoneTone: 'clay',
                seed: 5190,
                posX: 1.2, posY: 0.0, posZ: 1.85,
                width: 5.2, height: 0.95, length: 3.6,
                layoutPattern: 'cluster', rockCount: 10, sizeMin: 0.28, sizeMax: 0.95, roundness: 0.28
            })
        ]
    },
    {
        id: 'stack-basalt',
        name: '02 · Plain Columnar Basalt',
        badge: 'BASALT',
        buildLayers: () => [
            createRock2Layer('columnar_basalt', {
                name: 'Hexagonal Basalt Colonnade',
                combineMode: 'union',
                stoneTone: 'basalt',
                seed: 5512,
                width: 6.2, height: 5.2, length: 5.0,
                formationShape: 'cliff', polygonSides: 6, columnDiameter: 0.95, columnGap: 0.04, heightSteps: 6, edgeBevel: 0.08
            })
        ]
    },
    {
        id: 'stack-big-boulder',
        name: '03 · Plain Big Boulder',
        badge: 'BIG BOULDER',
        buildLayers: () => [
            createRock2Layer('big_boulder', {
                name: 'Granite Megalith Boulder',
                combineMode: 'union',
                stoneTone: 'granite',
                seed: 2104,
                width: 5.2, height: 4.1, length: 4.4,
                boulderStyle: 'angular', facetCount: 12, roundness: 0.22, angularity: 0.22
            })
        ]
    },
    {
        id: 'stack-smooth-rock',
        name: '04 · Plain Smooth Cobble',
        badge: 'SMOOTH ROCK',
        buildLayers: () => [
            createRock2Layer('smooth_rock', {
                name: 'River Cobble Stone',
                combineMode: 'union',
                stoneTone: 'cobble',
                seed: 3318,
                width: 4.8, height: 3.0, length: 3.8,
                smoothStyle: 'cobble', roundness: 0.88, softFacets: 0.16, flatBottom: 0.70
            })
        ]
    },
    {
        id: 'stack-small-rocks',
        name: '05 · Plain Small Rocks Cluster',
        badge: 'SMALL ROCKS',
        buildLayers: () => [
            createRock2Layer('small_rocks', {
                name: 'Small Rocks Scatter',
                combineMode: 'union',
                stoneTone: 'clay',
                seed: 5190,
                width: 6.0, height: 1.2, length: 5.6,
                layoutPattern: 'cluster', rockCount: 14, sizeMin: 0.30, sizeMax: 1.20, roundness: 0.34
            })
        ]
    },
    {
        id: 'stack-cliff-wall',
        name: '06 · Plain Cliff Escarpment',
        badge: 'CLIFF',
        buildLayers: () => [
            createRock2Layer('cliff_face', {
                name: 'Overhanging Headland Cliff',
                combineMode: 'union',
                stoneTone: 'limestone',
                seed: 7711,
                width: 8.4, height: 5.8, length: 3.6,
                cliffProfile: 'overhang', buttressCount: 5, stepBenches: 2, overhangLean: 0.28, wallCurvature: -0.25, roundness: 0.15
            })
        ]
    },
    {
        id: 'stack-carved-arch',
        name: '07 · Multi-Layer Carved Sea Arch',
        badge: 'CSG SUBTRACT',
        buildLayers: () => [
            createRock2Layer('cliff_face', {
                name: 'Promontory Cliff Fin',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 4108,
                width: 8.2, height: 5.2, length: 2.8,
                cliffProfile: 'vertical', buttressCount: 4, stepBenches: 2, roundness: 0.20
            }),
            createRock2Layer('smooth_rock', {
                name: 'Boolean Arch Tunnel Cutter',
                combineMode: 'subtract',
                blendRadius: 0.38,
                stoneTone: 'sandstone',
                seed: 1902,
                posX: 0.1, posY: 0.0, posZ: 0.0,
                width: 3.6, height: 3.1, length: 4.8,
                smoothStyle: 'dome', roundness: 0.92, softFacets: 0.08, flatBottom: 1.0
            }),
            createRock2Layer('rock_slab', {
                name: 'BasalShore Ledge Slab',
                combineMode: 'chamfer_union',
                blendRadius: 0.24,
                stoneTone: 'redSlate',
                seed: 4419,
                posX: -2.4, posY: 0.0, posZ: 1.4,
                yawDeg: -16,
                width: 4.2, height: 0.85, length: 3.2,
                dipAngleDeg: 8, roundness: 0.15
            })
        ]
    }
];

export function createDefaultRock2Document(starterId = 'stack-cliff-outcrop')
{
    const starter = ROCK2_STARTER_STACKS.find(s => s.id === starterId) || ROCK2_STARTER_STACKS[0];
    const layers = starter.buildLayers();
    return {
        format: 'frontier-rockformation-2.0',
        version: 1,
        starterId: starter.id,
        name: starter.name,
        meshResolution: 84,
        sunAzimuthDeg: 46,
        sunElevationDeg: 42,
        layers
    };
}

export function serializeRock2Document(doc)
{
    return {
        format: 'frontier-rockformation-2.0',
        version: 1,
        starterId: doc.starterId || 'stack-cliff-outcrop',
        name: doc.name || 'RockFormation 2.0 Stack',
        meshResolution: doc.meshResolution ?? 84,
        sunAzimuthDeg: doc.sunAzimuthDeg ?? 46,
        sunElevationDeg: doc.sunElevationDeg ?? 42,
        layers: (doc.layers || []).map(l => ({
            id: l.id,
            name: l.name,
            type: l.type,
            enabled: !!l.enabled,
            solo: !!l.solo,
            combineMode: l.combineMode || 'union',
            blendRadius: l.blendRadius ?? 0.35,
            params: JSON.parse(JSON.stringify(l.params || {}))
        }))
    };
}

export function deserializeRock2Document(raw)
{
    if (!raw || !Array.isArray(raw.layers) || raw.layers.length === 0)
    {
        return createDefaultRock2Document();
    }
    const layers = raw.layers.map(l => createRock2Layer(l.type || 'big_boulder', {
        id: l.id,
        name: l.name,
        enabled: l.enabled !== false,
        solo: !!l.solo,
        combineMode: l.combineMode || 'union',
        blendRadius: Number.isFinite(l.blendRadius) ? l.blendRadius : 0.35,
        params: l.params || {}
    }));
    return {
        format: 'frontier-rockformation-2.0',
        version: 1,
        starterId: raw.starterId || 'stack-cliff-outcrop',
        name: raw.name || 'Imported Rock Stack',
        meshResolution: clamp(raw.meshResolution ?? 84, 36, 140),
        sunAzimuthDeg: raw.sunAzimuthDeg ?? 46,
        sunElevationDeg: raw.sunElevationDeg ?? 42,
        layers
    };
}

/*==========================================================================================================================================
                                3. RIGID EULER MATRICES & PRIMITIVE COMPILER (CPU + GPU)
==========================================================================================================================================*/

function buildEulerMatrix3(yawDeg = 0, pitchDeg = 0, rollDeg = 0)
{
    const y = yawDeg * DEG2RAD;
    const p = pitchDeg * DEG2RAD;
    const r = rollDeg * DEG2RAD;
    const cy = Math.cos(y), sy = Math.sin(y);
    const cp = Math.cos(p), sp = Math.sin(p);
    const cr = Math.cos(r), sr = Math.sin(r);

    // R = Ry(yaw) * Rx(pitch) * Rz(roll)
    return [
        cy * cr + sy * sp * sr,  -cy * sr + sy * sp * cr,  sy * cp,
        cp * sr,                  cp * cr,                -sp,
       -sy * cr + cy * sp * sr,   sy * sr + cy * sp * cr,  cy * cp
    ];
}

function invertRigidTransform(R, pivot)
{
    // invR = transpose(R)
    const invR = [
        R[0], R[3], R[6],
        R[1], R[4], R[7],
        R[2], R[5], R[8]
    ];
    const [px, py, pz] = pivot;
    const invT = [
        -(invR[0] * px + invR[1] * py + invR[2] * pz),
        -(invR[3] * px + invR[4] * py + invR[5] * pz),
        -(invR[6] * px + invR[7] * py + invR[8] * pz)
    ];
    return { invR, invT };
}

function forwardPoint(R, pivot, lx, ly, lz)
{
    return [
        R[0] * lx + R[1] * ly + R[2] * lz + pivot[0],
        R[3] * lx + R[4] * ly + R[5] * lz + pivot[1],
        R[6] * lx + R[7] * ly + R[8] * lz + pivot[2]
    ];
}

/**
 * Compiles a single Pure-SDF Plain Rock Primitive (Half-Space Polyhedron, Analytic Superellipsoid, or Hybrid).
 * Evaluated in exact local space `q = invR * w + invT` with optional continuous `taper`, `pinch`, `twist`, `bend` & `skew`!
 */
function compilePlainPrimitive(opts)
{
    const poly = opts.poly || null;
    const R = opts.R || [1, 0, 0, 0, 1, 0, 0, 0, 1];
    const pivot = opts.pivot || [0, 0, 0];
    const { invR, invT } = invertRigidTransform(R, pivot);

    let planes = new Float32Array(0);
    let numPlanes = 0;
    let localMinX = -1, localMaxX = 1, localMinY = 0, localMaxY = 2, localMinZ = -1, localMaxZ = 1;

    if (poly && poly.faces && poly.faces.length >= 4)
    {
        const m = measurePoly(poly);
        if (!m || m.volume < 1e-5) return null;
        numPlanes = poly.faces.length;
        planes = new Float32Array(numPlanes * 4);
        for (let i = 0; i < numPlanes; i++)
        {
            const f = poly.faces[i];
            planes[i * 4 + 0] = f.normal[0];
            planes[i * 4 + 1] = f.normal[1];
            planes[i * 4 + 2] = f.normal[2];
            planes[i * 4 + 3] = f.offset;
        }
        localMinX = m.minX; localMaxX = m.maxX;
        localMinY = m.minY; localMaxY = m.maxY;
        localMinZ = m.minZ; localMaxZ = m.maxZ;
    }

    const sqWeight = clamp(opts.sqWeight ?? 0.0, 0.0, 1.0);
    const sqRx = Math.max(0.04, opts.sqRx ?? 1.0);
    const sqRy = Math.max(0.04, opts.sqRy ?? 1.0);
    const sqRz = Math.max(0.04, opts.sqRz ?? 1.0);
    const sqCy = opts.sqCy ?? sqRy;
    const sqExpH = clamp(opts.sqExpH ?? 1.0, 0.22, 2.0);
    const sqExpV = clamp(opts.sqExpV ?? 1.0, 0.22, 2.0);

    if (numPlanes === 0 || sqWeight > 0.01)
    {
        if (numPlanes === 0)
        {
            localMinX = -sqRx; localMaxX = sqRx;
            localMinY = (opts.flatBaseCut ?? 0) > 0.01 ? 0.0 : (sqCy - sqRy);
            localMaxY = sqCy + sqRy;
            localMinZ = -sqRz; localMaxZ = sqRz;
        }
        else
        {
            localMinX = Math.min(localMinX, -sqRx); localMaxX = Math.max(localMaxX, sqRx);
            localMinY = Math.min(localMinY, sqCy - sqRy); localMaxY = Math.max(localMaxY, sqCy + sqRy);
            localMinZ = Math.min(localMinZ, -sqRz); localMaxZ = Math.max(localMaxZ, sqRz);
        }
    }

    const baseY = opts.baseY ?? localMinY;
    const refH = Math.max(0.15, opts.refH ?? (localMaxY - localMinY));
    const taper = clamp(opts.taper ?? 0.0, -0.75, 0.92);
    const pinch = clamp(opts.pinch ?? 0.0, -0.50, 0.72);
    const twistRad = (opts.twistDeg ?? 0.0) * DEG2RAD;
    const bendX = clamp(opts.bendX ?? 0.0, -0.75, 0.75);
    const bendZ = clamp(opts.bendZ ?? 0.0, -0.75, 0.75);
    const skewX = clamp(opts.skewX ?? 0.0, -0.75, 0.75);

    // Conservative expansion of local AABB for non-linear flare/bend/skew so culling is 100% watertight!
    const flareScale = (taper < 0 ? (1.0 + Math.abs(taper) * 0.85) : 1.08) * (pinch < 0 ? (1.0 + Math.abs(pinch) * 0.65) : 1.02);
    const shiftPadX = (Math.abs(bendX) + Math.abs(skewX) * 0.6) * refH;
    const shiftPadZ = Math.abs(bendZ) * refH;
    const padR = Math.hypot(Math.max(Math.abs(localMinX), Math.abs(localMaxX)), Math.max(Math.abs(localMinZ), Math.abs(localMaxZ)));
    const rotPad = Math.abs(twistRad) > 0.02 ? padR * 0.25 : 0.0;

    const bMinX = localMinX * flareScale - shiftPadX - rotPad;
    const bMaxX = localMaxX * flareScale + shiftPadX + rotPad;
    const bMinY = localMinY - 0.04;
    const bMaxY = localMaxY + 0.04;
    const bMinZ = localMinZ * flareScale - shiftPadZ - rotPad;
    const bMaxZ = localMaxZ * flareScale + shiftPadZ + rotPad;

    // Transform all 8 conservative local AABB corners into world space to get exact world AABB + bounding sphere!
    let wMinX = Infinity, wMinY = Infinity, wMinZ = Infinity;
    let wMaxX = -Infinity, wMaxY = -Infinity, wMaxZ = -Infinity;
    const xs = [bMinX, bMaxX], ys = [bMinY, bMaxY], zs = [bMinZ, bMaxZ];
    for (let ix = 0; ix < 2; ix++)
    {
        for (let iy = 0; iy < 2; iy++)
        {
            for (let iz = 0; iz < 2; iz++)
            {
                const wp = forwardPoint(R, pivot, xs[ix], ys[iy], zs[iz]);
                if (wp[0] < wMinX) wMinX = wp[0]; if (wp[0] > wMaxX) wMaxX = wp[0];
                if (wp[1] < wMinY) wMinY = wp[1]; if (wp[1] > wMaxY) wMaxY = wp[1];
                if (wp[2] < wMinZ) wMinZ = wp[2]; if (wp[2] > wMaxZ) wMaxZ = wp[2];
            }
        }
    }

    const wCx = 0.5 * (wMinX + wMaxX);
    const wCy = 0.5 * (wMinY + wMaxY);
    const wCz = 0.5 * (wMinZ + wMaxZ);
    const boundRadius = 0.5 * Math.hypot(wMaxX - wMinX, wMaxY - wMinY, wMaxZ - wMinZ) + 0.08;

    const avgSpan = Math.max(0.15, ((localMaxX - localMinX) + (localMaxY - localMinY) + (localMaxZ - localMinZ)) / 3.0);
    const roundness = clamp(opts.roundness ?? 0.20, 0.005, 1.0);
    const kLse = Math.max(0.0035, avgSpan * (0.004 + 0.24 * roundness * roundness));
    const baseBevel = Math.max(0.003, avgSpan * (0.004 + 0.045 * roundness));

    // Conservative Lipschitz factor <= 1.0 for deformed local coordinates
    const hasDeform = (Math.abs(taper) + Math.abs(pinch) + Math.abs(twistRad) + Math.abs(bendX) + Math.abs(bendZ) + Math.abs(skewX)) > 1e-4;
    const deformPenalty = 1.0 / (1.0 + 0.36 * (Math.abs(taper) + Math.abs(pinch)) + 0.42 * (Math.abs(bendX) + Math.abs(bendZ) + Math.abs(skewX)) + 0.16 * Math.abs(twistRad));

    return {
        cx: wCx, cy: wCy, cz: wCz, radius: boundRadius,
        minX: wMinX, minY: wMinY, minZ: wMinZ,
        maxX: wMaxX, maxY: wMaxY, maxZ: wMaxZ,
        invR, invT,
        planes,
        numPlanes,
        kLse,
        baseBevel,
        sharpBase: opts.sharpBase !== false ? 1.0 : 0.0,
        flatBaseCut: opts.flatBaseCut ?? 0.0,
        sqWeight,
        sqRx, sqRy, sqRz, sqCy,
        sqExpH, sqExpV,
        baseY, refH,
        taper, pinch, twistRad, bendX, bendZ, skewX,
        hasDeform,
        deformPenalty,
        primOp: opts.primOp ?? 0 // 0 = intra-layer union/smin, 1 = intra-layer subtract!
    };
}

/*==========================================================================================================================================
                             4. NINE ROBUST PLAIN ROCK SHAPE GENERATORS (ZERO NOISE PROCESSING)
==========================================================================================================================================*/

function buildBigBoulderPrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const style = p.boulderStyle || 'angular';

    let roundness = p.roundness ?? 0.24;
    let disp = p.angularity ?? 0.22;
    let extraCuts = Math.max(0, Math.round((p.facetCount ?? 12) - 8));
    let taper = p.taper ?? 0.08;
    let prow = p.overhangProw ?? 0.0;
    let pinch = 0.0;
    let strikeDip = 0.28;

    if (style === 'subrounded')
    {
        roundness = Math.max(roundness, 0.52);
        extraCuts += 6;
        disp *= 0.75;
    }
    else if (style === 'prow')
    {
        taper -= 0.26;
        prow += 0.34;
    }
    else if (style === 'blocky')
    {
        disp *= 0.45;
        extraCuts = Math.min(extraCuts, 2);
        strikeDip = 0.08;
    }
    else if (style === 'pedestal')
    {
        taper -= 0.22;
        pinch = 0.34;
    }

    const flatBottom = (p.flatBottom ?? 1.0) > 0.5;
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, strikeDip + rng.signed() * 0.12);
    const cy = flatBottom ? ry * 0.94 : ry;
    let poly = sculptGeologicalParentPoly(
        [0, cy, 0],
        [rx, ry * (flatBottom ? 1.06 : 1.0), rz],
        bf,
        rng,
        {
            jointDispersion: disp,
            cornerBlunt: 0.72 + 0.22 * roundness,
            flatBaseY: flatBottom ? 0.0 : null,
            extraBevelCuts: extraCuts
        }
    );

    if (poly && (p.flatTop ?? 0) > 0.02)
    {
        const m = measurePoly(poly);
        if (m)
        {
            const capH = m.minY + m.spanY * (1.0 - 0.38 * p.flatTop);
            const capN = vnorm([-(p.asymmetry ?? 0) * 0.30, 1.0, rng.signed() * 0.05]);
            const next = clipPolyByHalfSpace(poly, capN, capN[1] * capH, 0);
            if (next) poly = next;
        }
    }

    const prim = compilePlainPrimitive({
        poly,
        R: layerR,
        pivot: layerPivot,
        roundness,
        sharpBase: flatBottom,
        sqWeight: style === 'subrounded' ? 0.28 * roundness : 0.0,
        sqRx: rx * 0.92, sqRy: ry * 0.92, sqRz: rz * 0.92, sqCy: cy,
        sqExpH: 0.92, sqExpV: 0.92,
        baseY: flatBottom ? 0.0 : (cy - ry),
        refH: p.height,
        taper,
        pinch,
        bendZ: prow,
        skewX: p.asymmetry ?? 0.0
    });
    return { primitives: prim ? [prim] : [], intraBlend: 0.0 };
}

function buildSmoothRockPrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const style = p.smoothStyle || 'cobble';
    const flatCut = clamp(p.flatBottom ?? 0.75, 0.0, 1.0);
    const cy = flatCut > 0.02 ? ry * (1.0 - 0.22 * flatCut) : ry;

    let expH = p.squarenessHoriz ?? 0.95;
    let expV = p.squarenessVert ?? 0.92;
    let taper = p.taper ?? 0.10;
    let softFacets = p.softFacets ?? 0.18;

    if (style === 'dome')
    {
        expH = 0.95; expV = 0.88;
        taper += 0.16;
    }
    else if (style === 'pillow')
    {
        expH = Math.min(expH, 0.58);
        expV = Math.min(expV, 0.56);
    }
    else if (style === 'egg')
    {
        expH = 1.05; expV = 1.05;
        taper += 0.28;
    }

    if (style === 'lobes')
    {
        // Multi-lobe smooth compound river rock (3 fused smooth superquadric stones!)
        const prims = [];
        const specs = [
            [0.0, 0.0, 1.0, 1.0, 1.0, 0],
            [-rx * 0.48, rz * 0.22, 0.68, 0.76, 0.74, 18],
            [rx * 0.46, -rz * 0.20, 0.64, 0.68, 0.70, -24]
        ];
        for (let i = 0; i < specs.length; i++)
        {
            const [ox, oz, sx, sy, sz, yawOff] = specs[i];
            const subPivot = forwardPoint(layerR, layerPivot, ox, 0, oz);
            const subR = buildEulerMatrix3((p.yawDeg ?? 0) + yawOff, p.pitchDeg ?? 0, p.rollDeg ?? 0);
            const pr = compilePlainPrimitive({
                poly: null,
                R: subR,
                pivot: subPivot,
                roundness: p.roundness ?? 0.88,
                sharpBase: flatCut > 0.05,
                flatBaseCut: flatCut,
                sqWeight: 1.0,
                sqRx: rx * sx, sqRy: ry * sy, sqRz: rz * sz,
                sqCy: cy * sy,
                sqExpH: expH, sqExpV: expV,
                baseY: 0.0, refH: p.height * sy,
                taper,
                bendX: p.bend ?? 0.0,
                skewX: p.asymmetry ?? 0.0
            });
            if (pr) prims.push(pr);
        }
        return { primitives: prims, intraBlend: Math.min(rx, ry, rz) * 0.48 };
    }

    let poly = null;
    if (softFacets > 0.02)
    {
        const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, 0.14);
        poly = sculptGeologicalParentPoly(
            [0, cy, 0],
            [rx * 1.02, ry * 1.02, rz * 1.02],
            bf,
            rng,
            {
                jointDispersion: 0.10 + 0.14 * softFacets,
                cornerBlunt: 0.94,
                flatBaseY: flatCut > 0.05 ? 0.0 : null,
                extraBevelCuts: 12
            }
        );
    }

    const prim = compilePlainPrimitive({
        poly,
        R: layerR,
        pivot: layerPivot,
        roundness: p.roundness ?? 0.86,
        sharpBase: flatCut > 0.05,
        flatBaseCut: flatCut,
        sqWeight: poly ? clamp(1.0 - softFacets * 0.85, 0.20, 1.0) : 1.0,
        sqRx: rx, sqRy: ry, sqRz: rz,
        sqCy: cy,
        sqExpH: expH, sqExpV: expV,
        baseY: 0.0,
        refH: p.height,
        taper,
        bendX: p.bend ?? 0.0,
        skewX: p.asymmetry ?? 0.0
    });
    return { primitives: prim ? [prim] : [], intraBlend: 0.0 };
}

function buildSmallRocksPrimitives(p, layerR, layerPivot, rng)
{
    const count = clamp(Math.round(p.rockCount ?? 11), 1, 24);
    const halfW = p.width * 0.5;
    const halfL = p.length * 0.5;
    const sMin = Math.min(p.sizeMin ?? 0.32, p.sizeMax ?? 1.15);
    const sMax = Math.max(p.sizeMin ?? 0.32, p.sizeMax ?? 1.15);
    const heroBoost = p.heroCenterBoost ?? 0.45;
    const roundness = p.roundness ?? 0.35;
    const flatness = p.flatness ?? 0.38;
    const tiltAmt = p.tiltRandomness ?? 0.35;
    const embed = p.embedDepth ?? 0.18;
    const pattern = p.layoutPattern || 'cluster';

    const prims = [];
    const placed = [];

    for (let i = 0; i < count; i++)
    {
        let lx = 0, lz = 0;
        let sizeT = rng.float();

        if (count === 1)
        {
            lx = 0; lz = 0; sizeT = 1.0;
        }
        else if (pattern === 'ring')
        {
            const ang = (i / count) * Math.PI * 2 + rng.signed() * 0.22;
            const rad = rng.range(0.62, 0.96);
            lx = Math.cos(ang) * halfW * rad;
            lz = Math.sin(ang) * halfL * rad;
        }
        else if (pattern === 'line')
        {
            const u = (i / (count - 1)) * 2.0 - 1.0;
            lx = u * halfW * 0.92 + rng.signed() * halfW * 0.08;
            lz = Math.sin(u * 2.2) * halfL * 0.24 + rng.signed() * halfL * 0.16;
            sizeT = mix(sizeT, 1.0 - Math.abs(u) * 0.65, heroBoost);
        }
        else if (pattern === 'grid')
        {
            const cols = Math.max(1, Math.ceil(Math.sqrt(count * (halfW / Math.max(0.2, halfL)))));
            const rows = Math.max(1, Math.ceil(count / cols));
            const c = i % cols, r = Math.floor(i / cols);
            lx = (cols === 1 ? 0 : ((c / (cols - 1)) * 2 - 1) * halfW * 0.82) + rng.signed() * halfW * 0.12;
            lz = (rows === 1 ? 0 : ((r / (rows - 1)) * 2 - 1) * halfL * 0.82) + rng.signed() * halfL * 0.12;
        }
        else
        {
            // Natural golden-angle cluster with non-overlapping relaxation!
            if (i === 0 && heroBoost > 0.15)
            {
                lx = rng.signed() * halfW * 0.08;
                lz = rng.signed() * halfL * 0.08;
                sizeT = mix(sizeT, 1.0, heroBoost);
            }
            else
            {
                const ang = i * 2.399963 + rng.signed() * 0.28;
                const rad = Math.sqrt(i / count) * rng.range(0.55, 0.98);
                lx = Math.cos(ang) * halfW * rad;
                lz = Math.sin(ang) * halfL * rad;
                sizeT = mix(sizeT, 1.0 - rad * 0.72, heroBoost);
            }
        }

        const diam = mix(sMin, sMax, clamp(sizeT, 0.0, 1.0));
        const rx = diam * 0.5 * rng.range(0.82, 1.24);
        const rz = diam * 0.5 * rng.range(0.80, 1.20);
        const ry = diam * 0.5 * (1.0 - 0.62 * flatness) * clamp(p.height / 1.1, 0.35, 2.5) * rng.range(0.78, 1.12);

        // Simple repulsion nudge so scattered small rocks don't awkwardly stack right on top of each other
        for (let iter = 0; iter < 3; iter++)
        {
            for (let j = 0; j < placed.length; j++)
            {
                const q = placed[j];
                const dx = lx - q.x, dz = lz - q.z;
                const dist = Math.hypot(dx, dz);
                const minSep = (Math.max(rx, rz) + q.r) * 0.88;
                if (dist < minSep)
                {
                    const push = (minSep - dist) * 0.65;
                    const nx = dist > 1e-4 ? dx / dist : Math.cos(i);
                    const nz = dist > 1e-4 ? dz / dist : Math.sin(i);
                    lx += nx * push;
                    lz += nz * push;
                }
            }
        }
        placed.push({ x: lx, z: lz, r: Math.max(rx, rz) });

        const rockYaw = (p.yawDeg ?? 0) + rng.float() * 360.0;
        const rockPitch = (p.pitchDeg ?? 0) + rng.signed() * 26.0 * tiltAmt;
        const rockRoll = (p.rollDeg ?? 0) + rng.signed() * 26.0 * tiltAmt;
        const rockR = buildEulerMatrix3(rockYaw, rockPitch, rockRoll);
        const ly = ry * (0.92 - 0.65 * embed);
        const worldPos = forwardPoint(layerR, layerPivot, lx, ly, lz);

        let poly = null;
        if (roundness < 0.82)
        {
            const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, rng.range(0.10, 0.55));
            poly = sculptGeologicalParentPoly(
                [0, 0, 0],
                [rx, ry, rz],
                bf,
                rng,
                {
                    jointDispersion: 0.26 * (1.0 - 0.5 * roundness),
                    cornerBlunt: 0.68 + 0.28 * roundness,
                    flatBaseY: null,
                    extraBevelCuts: roundness > 0.45 ? 6 : 3
                }
            );
        }

        const pr = compilePlainPrimitive({
            poly,
            R: rockR,
            pivot: worldPos,
            roundness,
            sharpBase: false,
            sqWeight: roundness >= 0.82 ? 1.0 : smoothstep(0.35, 0.82, roundness),
            sqRx: rx, sqRy: ry, sqRz: rz, sqCy: 0.0,
            sqExpH: 0.85 + 0.25 * roundness,
            sqExpV: 0.78 + 0.28 * roundness,
            baseY: -ry,
            refH: ry * 2.0,
            taper: rng.signed() * 0.16
        });
        if (pr) prims.push(pr);
    }

    return { primitives: prims, intraBlend: 0.0 };
}

function buildCliffFacePrimitives(p, layerR, layerPivot, rng)
{
    const count = clamp(Math.round(p.buttressCount ?? 4), 1, 7);
    const totalW = p.width;
    const totalH = p.height;
    const totalL = p.length;
    const profile = p.cliffProfile || 'stepped';
    const benches = clamp(Math.round(p.stepBenches ?? 3), 0, 5);
    const roundness = p.roundness ?? 0.14;
    const curv = p.wallCurvature ?? 0.0;
    const crestSlope = p.crestSlope ?? 0.08;

    let overhang = p.overhangLean ?? 0.08;
    let taper = 0.10;
    if (profile === 'overhang') { overhang += 0.32; taper = -0.22; }
    else if (profile === 'rampart') { overhang -= 0.30; taper = 0.34; }
    else if (profile === 'vertical') { taper = 0.03; }
    else if (profile === 'mesa') { taper = 0.06; }

    const prims = [];
    const segW = totalW / count;

    for (let i = 0; i < count; i++)
    {
        const u = count === 1 ? 0.0 : (i / (count - 1)) * 2.0 - 1.0;
        const bx = u * (totalW * 0.5 - segW * 0.42);
        const bz = -curv * (1.0 - u * u) * totalL * 0.48 + rng.signed() * totalL * 0.06;

        const hMod = clamp(1.0 + crestSlope * u - 0.10 * u * u + rng.signed() * 0.07, 0.52, 1.25);
        const bH = totalH * hMod;
        const rx = segW * rng.range(0.58, 0.76);
        const rz = totalL * 0.5 * rng.range(0.84, 1.08);

        const bf = buildStrikeDipFrame(0.08 + rng.signed() * 0.12, 0.12 + rng.signed() * 0.08);
        let poly = sculptGeologicalParentPoly(
            [0, bH * 0.5, 0],
            [rx, bH * 0.52, rz],
            bf,
            rng,
            {
                jointDispersion: 0.12,
                cornerBlunt: 0.62 + 0.28 * roundness,
                flatBaseY: 0.0,
                extraBevelCuts: 4
            }
        );

        if (poly && profile === 'mesa')
        {
            const capN = vnorm([-crestSlope * 0.25, 1.0, 0.0]);
            const next = clipPolyByHalfSpace(poly, capN, bH * 0.92, 0);
            if (next) poly = next;
        }

        const subYaw = (p.yawDeg ?? 0) + (-curv * u * 28.0);
        const subR = buildEulerMatrix3(subYaw, p.pitchDeg ?? 0, p.rollDeg ?? 0);
        const subPivot = forwardPoint(layerR, layerPivot, bx, 0.0, bz);

        const mainB = compilePlainPrimitive({
            poly,
            R: subR,
            pivot: subPivot,
            roundness,
            sharpBase: true,
            baseY: 0.0,
            refH: bH,
            taper,
            bendZ: overhang
        });
        if (mainB) prims.push(mainB);

        // Stepped forward canyon terrace benches when `stepBenches > 0`!
        if (benches > 0 && (profile === 'stepped' || profile === 'mesa' || profile === 'rampart'))
        {
            const tierCount = (i % 2 === 0) ? Math.min(2, benches) : 1;
            for (let t = 0; t < tierCount; t++)
            {
                const fracH = (t + 1) / (benches + 1);
                const benchH = bH * fracH * rng.range(0.88, 1.04);
                const benchOutZ = rz * (0.45 + 0.42 * (1.0 - fracH));
                const benchPoly = sculptGeologicalParentPoly(
                    [0, benchH * 0.5, 0],
                    [rx * 0.92, benchH * 0.52, rz * 0.62],
                    bf,
                    rng,
                    { jointDispersion: 0.09, cornerBlunt: 0.58 + 0.25 * roundness, flatBaseY: 0.0, extraBevelCuts: 2 }
                );
                const flatBench = benchPoly
                    ? (clipPolyByHalfSpace(benchPoly, vnorm([0, 1, 0.04]), benchH * 0.94, 0) || benchPoly)
                    : null;
                const benchPivot = forwardPoint(layerR, layerPivot, bx + rng.signed() * rx * 0.1, 0.0, bz + benchOutZ);
                const benchPrim = compilePlainPrimitive({
                    poly: flatBench,
                    R: subR,
                    pivot: benchPivot,
                    roundness,
                    sharpBase: true,
                    baseY: 0.0,
                    refH: benchH,
                    taper: 0.06
                });
                if (benchPrim) prims.push(benchPrim);
            }
        }
    }

    return { primitives: prims, intraBlend: p.buttressBlend ?? 0.22 };
}

function buildColumnarBasaltPrimitives(p, layerR, layerPivot, rng)
{
    const shape = p.formationShape || 'cliff';
    const fixedSides = Number(p.polygonSides ?? 6);
    const colDiam = Math.max(0.35, p.columnDiameter ?? 0.95);
    const colRadius = colDiam * 0.5;
    const gap = clamp(p.columnGap ?? 0.04, 0.0, colDiam * 0.45);
    const steps = clamp(Math.round(p.heightSteps ?? 6), 1, 16);
    const bevelWeight = clamp(p.edgeBevel ?? 0.08, 0.01, 0.55);
    const splay = p.columnSplay ?? 0.06;
    const capTilt = p.capTilt ?? 0.10;

    if (shape === 'single')
    {
        const sides = fixedSides > 0 ? fixedSides : 6;
        const r = Math.max(0.15, Math.min(p.width, p.length) * 0.48);
        const h = p.height;
        let poly = createBoxPoly(-r * 1.35, 0, -r * 1.35, r * 1.35, h + r, r * 1.35, 0);
        for (let s = 0; s < sides; s++)
        {
            const ang = 0.26 + (s * Math.PI * 2) / sides + rng.signed() * 0.03;
            const n = vnorm([Math.cos(ang), 0.0, Math.sin(ang)]);
            poly = clipPolyByHalfSpace(poly, n, r * rng.range(0.96, 1.03), 0);
        }
        const capN = vnorm([rng.signed() * capTilt, 1.0, rng.signed() * capTilt]);
        poly = clipPolyByHalfSpace(poly, capN, capN[1] * h, 0);
        const pr = compilePlainPrimitive({
            poly, R: layerR, pivot: layerPivot,
            roundness: bevelWeight * 0.55,
            sharpBase: true,
            baseY: 0.0, refH: h
        });
        return { primitives: pr ? [pr] : [], intraBlend: 0.0 };
    }

    const spacingX = colRadius * 1.80;
    const spacingZ = colRadius * 1.56;
    const halfW = p.width * 0.5;
    const halfL = p.length * 0.5;
    const maxColsX = Math.max(1, Math.min(6, Math.round(halfW / spacingX)));
    const maxRowsZ = Math.max(1, Math.min(5, Math.round(halfL / spacingZ)));

    const candidates = [];
    for (let rIdx = -maxRowsZ; rIdx <= maxRowsZ; rIdx++)
    {
        const shift = (Math.abs(rIdx) % 2 === 1) ? 0.5 : 0.0;
        for (let qIdx = Math.floor(-maxColsX - shift); qIdx <= Math.ceil(maxColsX - shift); qIdx++)
        {
            const gx = qIdx + shift;
            const gz = rIdx;
            const lx = gx * spacingX;
            const lz = gz * spacingZ;
            const nx = lx / Math.max(0.25, halfW * 1.12);
            const nz = lz / Math.max(0.25, halfL * 1.12);

            let hFrac = 0.0;
            if (shape === 'mound')
            {
                const rad = Math.hypot(nx, nz);
                if (rad > 1.06) continue;
                hFrac = 0.16 + 0.84 * Math.pow(Math.max(0, 1.0 - rad * rad), 0.85);
            }
            else if (shape === 'pavement')
            {
                const rad = Math.hypot(nx * 0.94, nz * 0.94);
                if (rad > 1.08) continue;
                hFrac = 0.22 + 0.36 * (0.5 - 0.5 * nz) + rng.signed() * 0.08;
            }
            else if (shape === 'amphitheater')
            {
                const rad = Math.hypot(nx * 0.92, (nz - 0.34) * 0.95);
                if (Math.abs(nx) > 1.10 || Math.abs(nz) > 1.08) continue;
                if (rad < 0.42 && nz > -0.10) continue;
                hFrac = clamp((rad - 0.28) * 1.05 + clamp((0.35 - nz) * 0.28, 0, 0.25), 0.20, 1.0);
            }
            else
            {
                const rad = Math.hypot(nx * 0.92, nz * 0.95);
                if (rad > 1.12) continue;
                const tFront = clamp((nz + 0.95) * 0.52, 0.0, 1.0);
                hFrac = clamp(0.98 * (1.0 - 0.65 * tFront) - 0.12 * nx * nx, 0.18, 1.0);
            }

            if (steps > 1)
            {
                hFrac = Math.max(0.16, Math.round(hFrac * steps) / steps);
            }
            hFrac *= rng.range(0.95, 1.04);
            candidates.push({ lx, lz, nx, nz, hFrac });
        }
    }

    if (candidates.length > 48)
    {
        candidates.sort((a, b) => b.hFrac - a.hFrac);
        candidates.length = 48;
    }

    const prims = [];
    for (let i = 0; i < candidates.length; i++)
    {
        const c = candidates[i];
        const colH = Math.max(0.25, p.height * c.hFrac);
        const r = Math.max(0.10, colRadius - gap * 0.5);
        const sides = fixedSides > 0 ? fixedSides : (i % 5 === 0 ? 5 : (i % 7 === 0 ? 7 : 6));
        const rot0 = 0.26 + rng.signed() * 0.05;

        let poly = createBoxPoly(-r * 1.35, 0, -r * 1.35, r * 1.35, colH + r, r * 1.35, 0);
        for (let s = 0; s < sides; s++)
        {
            const ang = rot0 + (s * Math.PI * 2) / sides + rng.signed() * 0.035;
            const n = vnorm([Math.cos(ang), 0.0, Math.sin(ang)]);
            poly = clipPolyByHalfSpace(poly, n, r * rng.range(0.96, 1.03), 0);
        }
        const capN = vnorm([rng.signed() * capTilt, 1.0, rng.signed() * capTilt]);
        poly = clipPolyByHalfSpace(poly, capN, capN[1] * colH, 0);
        if (!poly) continue;

        const pitchSplay = (p.pitchDeg ?? 0) + c.nz * splay * 22.0;
        const rollSplay = (p.rollDeg ?? 0) - c.nx * splay * 22.0;
        const colR = buildEulerMatrix3(p.yawDeg ?? 0, pitchSplay, rollSplay);
        const colPivot = forwardPoint(layerR, layerPivot, c.lx, 0.0, c.lz);

        const pr = compilePlainPrimitive({
            poly,
            R: colR,
            pivot: colPivot,
            roundness: bevelWeight * 0.52,
            sharpBase: true,
            baseY: 0.0,
            refH: colH
        });
        if (pr) prims.push(pr);
    }

    return { primitives: prims, intraBlend: gap <= 0.005 ? 0.03 : 0.0 };
}

function buildSpirePinnaclePrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const h = p.height;
    const rz = p.length * 0.5;
    const style = p.spireStyle || 'horn';
    const sides = clamp(Math.round(p.facetSides ?? 7), 4, 14);
    const roundness = p.roundness ?? 0.16;

    let taper = p.taper ?? 0.58;
    let pinch = p.waistPinch ?? 0.0;
    let flatCap = p.flatCap ?? 0.08;
    let bendX = p.bendX ?? 0.12;

    if (style === 'needle') { taper = Math.max(taper, 0.72); flatCap = Math.min(flatCap, 0.04); }
    else if (style === 'hoodoo') { pinch = Math.max(pinch, 0.42); taper = Math.min(taper, 0.05); flatCap = Math.max(flatCap, 0.22); }
    else if (style === 'butte') { taper = clamp(taper * 0.4, 0.05, 0.32); flatCap = Math.max(flatCap, 0.48); bendX *= 0.25; }

    const bound = Math.max(rx, rz, h) * 1.5;
    let poly = createBoxPoly(-bound, 0.0, -bound, bound, h * 1.15, bound, 0);
    const rot0 = rng.float() * Math.PI * 2;
    for (let s = 0; s < sides; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / sides + rng.signed() * 0.10;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const rad = Math.hypot(rx * dx, rz * dz) * rng.range(0.88, 1.08);
        const n = vnorm([dx, rng.signed() * 0.06, dz]);
        poly = clipPolyByHalfSpace(poly, n, rad, 0);
    }

    // Crown termination planes
    if (flatCap > 0.12)
    {
        const capN = vnorm([rng.signed() * 0.12, 1.0, rng.signed() * 0.12]);
        poly = clipPolyByHalfSpace(poly, capN, h * (1.0 - 0.18 * flatCap), 0);
    }
    else
    {
        for (let k = 0; k < 4; k++)
        {
            const ang = rot0 + 0.4 + k * (Math.PI * 0.5);
            const n = vnorm([Math.cos(ang) * 0.55, 0.84, Math.sin(ang) * 0.55]);
            poly = clipPolyByHalfSpace(poly, n, h * 0.86, 0);
        }
    }

    const prim = compilePlainPrimitive({
        poly,
        R: layerR,
        pivot: layerPivot,
        roundness,
        sharpBase: true,
        sqWeight: style === 'hoodoo' ? 0.35 * roundness : 0.0,
        sqRx: rx, sqRy: h * 0.5, sqRz: rz, sqCy: h * 0.5,
        sqExpH: 0.90, sqExpV: 0.75,
        baseY: 0.0,
        refH: h,
        taper,
        pinch,
        twistDeg: p.twistDeg ?? 14,
        bendX
    });
    return { primitives: prim ? [prim] : [], intraBlend: 0.0 };
}

function buildRockSlabPrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const shape = p.planShape || 'plate';
    const roundness = p.roundness ?? 0.12;
    const dipDeg = p.dipAngleDeg ?? 18;

    let poly = null;
    if (shape === 'wedge')
    {
        // Clean triangular flatiron hogback wedge!
        const bound = Math.max(rx, ry, rz) * 2.2;
        poly = createBoxPoly(-bound, -bound, -bound, bound, bound, bound, 0);
        poly = clipPolyByHalfSpace(poly, [0, 1, 0], ry, 0);
        poly = clipPolyByHalfSpace(poly, [0, -1, 0], ry, 0);
        const triAngles = [0.0, 2.15 + rng.signed() * 0.12, 4.18 + rng.signed() * 0.12];
        for (let k = 0; k < 3; k++)
        {
            const a = triAngles[k];
            const n = vnorm([Math.cos(a), (p.edgeTaper ?? 0.22) * 0.35, Math.sin(a)]);
            const d = Math.hypot(rx * Math.cos(a), rz * Math.sin(a)) * 0.86;
            poly = clipPolyByHalfSpace(poly, n, d, 0);
        }
    }
    else
    {
        const bf = buildStrikeDipFrame(0.15, 0.04);
        poly = sculptGeologicalParentPoly(
            [0, 0, 0],
            [rx, ry, shape === 'ledge' ? rz * 0.65 : rz],
            bf,
            rng,
            {
                jointDispersion: shape === 'flagstone' ? 0.10 : 0.16,
                cornerBlunt: shape === 'flagstone' ? 0.90 : 0.72,
                flatBaseY: null,
                extraBevelCuts: shape === 'flagstone' ? 10 : 4
            }
        );
    }

    const slabR = buildEulerMatrix3(p.yawDeg ?? 0, (p.pitchDeg ?? 0) + dipDeg, p.rollDeg ?? 0);
    const liftY = ry + Math.abs(Math.sin(dipDeg * DEG2RAD)) * rz * 0.45;
    const slabPivot = forwardPoint(layerR, layerPivot, 0, liftY, 0);

    const prim = compilePlainPrimitive({
        poly,
        R: slabR,
        pivot: slabPivot,
        roundness: shape === 'flagstone' ? Math.max(roundness, 0.45) : roundness,
        sharpBase: false,
        sqWeight: shape === 'flagstone' ? 0.55 : 0.0,
        sqRx: rx, sqRy: ry, sqRz: rz, sqCy: 0.0,
        sqExpH: 0.85, sqExpV: 0.55,
        baseY: -ry,
        refH: ry * 2.0,
        taper: p.edgeTaper ?? 0.22,
        bendX: p.bend ?? 0.0
    });
    return { primitives: prim ? [prim] : [], intraBlend: 0.0 };
}

function buildRockArchPrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const h = p.height;
    const rz = p.length * 0.5;
    const profile = p.archProfile || 'vault';
    const openW = clamp(p.openingWidth ?? 0.54, 0.18, 0.82);
    const openH = clamp(p.openingHeight ?? 0.58, 0.18, 0.80);
    const roundness = p.roundness ?? 0.28;
    const flare = p.buttressFlare ?? 0.25;
    const skew = p.archSkew ?? 0.0;

    // 1. Solid outer bridge / fin body
    const bf = buildStrikeDipFrame(0.12, 0.08);
    const bodyPoly = sculptGeologicalParentPoly(
        [0, h * 0.5, 0],
        [rx, h * 0.52, rz],
        bf,
        rng,
        { jointDispersion: 0.12, cornerBlunt: 0.78 + 0.18 * roundness, flatBaseY: 0.0, extraBevelCuts: 6 }
    );
    const outerPrim = compilePlainPrimitive({
        poly: bodyPoly,
        R: layerR,
        pivot: layerPivot,
        roundness,
        sharpBase: true,
        baseY: 0.0,
        refH: h,
        taper: flare * 0.55,
        skewX: skew * 0.35,
        primOp: 0
    });

    // 2. Clean Pure-SDF arch tunnel boolean cutter (`primOp: 1` = intra-layer subtract!)
    const holeRx = rx * openW;
    const holeH = h * openH;
    const holeRz = rz * 1.85; // Cleanly pierces front-to-back through the entire bridge thickness!
    const holePivot = forwardPoint(layerR, layerPivot, skew * rx * 0.22, 0.0, 0.0);

    let expH = 0.95, expV = 0.95;
    let holeCy = holeH * 0.35;
    let holeRy = holeH * 0.68;
    let holeTaper = 0.0;

    if (profile === 'pointed') { expV = 1.45; holeTaper = 0.38; }
    else if (profile === 'lintel') { expH = 0.42; expV = 0.38; holeCy = holeH * 0.42; holeRy = holeH * 0.58; }
    else if (profile === 'keyhole') { holeCy = holeH * 0.62; holeRy = holeH * 0.52; }

    const cutterPrim = compilePlainPrimitive({
        poly: null,
        R: layerR,
        pivot: holePivot,
        roundness: 0.65,
        sharpBase: false,
        sqWeight: 1.0,
        sqRx: holeRx, sqRy: holeRy, sqRz: holeRz, sqCy: holeCy,
        sqExpH: expH, sqExpV: expV,
        baseY: 0.0, refH: holeH,
        taper: holeTaper,
        primOp: 1
    });

    const prims = [];
    if (outerPrim) prims.push(outerPrim);
    if (cutterPrim) prims.push(cutterPrim);
    return { primitives: prims, intraBlend: Math.min(rx, h, rz) * (0.08 + 0.22 * roundness) };
}

function buildCustomSculptRockPrimitives(p, layerR, layerPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const sqWeight = clamp(p.analyticBlend ?? 0.50, 0.0, 1.0);
    const flatCut = clamp(p.flatBottom ?? 0.85, 0.0, 1.0);
    const cy = flatCut > 0.05 ? ry * (1.0 - 0.16 * flatCut) : ry;

    let poly = null;
    if (sqWeight < 0.99)
    {
        const extraCuts = Math.max(0, Math.round((p.facetCount ?? 14) - 8));
        const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, 0.22);
        poly = sculptGeologicalParentPoly(
            [0, cy, 0],
            [rx, ry * (flatCut > 0.05 ? 1.04 : 1.0), rz],
            bf,
            rng,
            {
                jointDispersion: p.angularity ?? 0.18,
                cornerBlunt: 0.68 + 0.26 * (p.roundness ?? 0.32),
                flatBaseY: flatCut > 0.05 ? 0.0 : null,
                extraBevelCuts: extraCuts
            }
        );
    }

    const prim = compilePlainPrimitive({
        poly,
        R: layerR,
        pivot: layerPivot,
        roundness: p.roundness ?? 0.32,
        sharpBase: flatCut > 0.05,
        flatBaseCut: flatCut,
        sqWeight,
        sqRx: rx, sqRy: ry, sqRz: rz, sqCy: cy,
        sqExpH: p.expHoriz ?? 0.85,
        sqExpV: p.expVert ?? 0.85,
        baseY: flatCut > 0.05 ? 0.0 : (cy - ry),
        refH: p.height,
        taper: p.taper ?? 0.12,
        pinch: p.waistPinch ?? 0.0,
        twistDeg: p.twistDeg ?? 0.0,
        bendX: p.bendX ?? 0.0,
        bendZ: p.bendZ ?? 0.0,
        skewX: p.skewX ?? 0.0
    });
    return { primitives: prim ? [prim] : [], intraBlend: 0.0 };
}

/*==========================================================================================================================================
                               5. LAYER-STACK COMPILER & EXACT CPU PURE-SDF EVALUATOR
==========================================================================================================================================*/

export function compileRock2Stack(doc)
{
    const layers = doc?.layers || [];
    const hasSolo = layers.some(l => l.enabled && l.solo);

    const compiledLayers = [];
    const allPrimitives = [];
    let totalPlanes = 0;

    let minX = Infinity, minY = 0.0, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;

    for (let li = 0; li < layers.length; li++)
    {
        const lyr = layers[li];
        if (!lyr.enabled) continue;
        if (hasSolo && !lyr.solo) continue;

        const spec = getRock2NodeSpec(lyr.type);
        const p = { ...spec.defaults, ...(lyr.params || {}) };
        const rng = createRng(p.seed ?? 2104);
        const layerR = buildEulerMatrix3(p.yawDeg ?? 0, p.pitchDeg ?? 0, p.rollDeg ?? 0);
        const layerPivot = [p.posX ?? 0.0, p.posY ?? 0.0, p.posZ ?? 0.0];

        let built;
        switch (lyr.type)
        {
            case 'smooth_rock':        built = buildSmoothRockPrimitives(p, layerR, layerPivot, rng); break;
            case 'small_rocks':        built = buildSmallRocksPrimitives(p, layerR, layerPivot, rng); break;
            case 'cliff_face':         built = buildCliffFacePrimitives(p, layerR, layerPivot, rng); break;
            case 'columnar_basalt':    built = buildColumnarBasaltPrimitives(p, layerR, layerPivot, rng); break;
            case 'spire_pinnacle':     built = buildSpirePinnaclePrimitives(p, layerR, layerPivot, rng); break;
            case 'rock_slab':          built = buildRockSlabPrimitives(p, layerR, layerPivot, rng); break;
            case 'arch_formation':     built = buildRockArchPrimitives(p, layerR, layerPivot, rng); break;
            case 'custom_sculpt_rock': built = buildCustomSculptRockPrimitives(p, layerR, layerPivot, rng); break;
            case 'big_boulder':
            default:                   built = buildBigBoulderPrimitives(p, layerR, layerPivot, rng); break;
        }

        if (!built || built.primitives.length === 0) continue;

        // Enforce GPU primitive budget (<= 128 total primitives across the entire stack)
        const room = Math.max(0, 128 - allPrimitives.length);
        if (room <= 0) break;
        const primSlice = built.primitives.slice(0, room);

        const blockStart = allPrimitives.length;
        for (let k = 0; k < primSlice.length; k++)
        {
            const pr = primSlice[k];
            allPrimitives.push(pr);
            totalPlanes += pr.numPlanes;

            // Subtract / Intersect layers do not expand outer positive solid bounds beyond additive rock layers
            if (lyr.combineMode !== 'subtract' || compiledLayers.length === 0)
            {
                if (pr.primOp === 0)
                {
                    if (pr.minX < minX) minX = pr.minX;
                    if (pr.maxX > maxX) maxX = pr.maxX;
                    if (pr.minY < minY) minY = pr.minY;
                    if (pr.maxY > maxY) maxY = pr.maxY;
                    if (pr.minZ < minZ) minZ = pr.minZ;
                    if (pr.maxZ > maxZ) maxZ = pr.maxZ;
                }
            }
        }

        const tone = ROCK2_STONE_TONES[p.stoneTone] || ROCK2_STONE_TONES.clay;
        compiledLayers.push({
            id: lyr.id,
            name: lyr.name,
            type: lyr.type,
            combineOp: ROCK2_COMBINE_OP_CODE[lyr.combineMode] ?? 0,
            combineMode: lyr.combineMode || 'union',
            blendRadius: Math.max(0.0, lyr.blendRadius ?? 0.35),
            intraBlend: Math.max(0.0, built.intraBlend ?? 0.0),
            color: tone.rgb,
            blockStart,
            blockCount: primSlice.length
        });
    }

    if (!Number.isFinite(minX))
    {
        minX = -2.5; maxX = 2.5; minY = 0.0; maxY = 3.5; minZ = -2.5; maxZ = 2.5;
    }

    const span = Math.max(2.0, maxX - minX, maxY - minY, maxZ - minZ);
    const margin = span * 0.08 + 0.25;
    const bounds = {
        minX: minX - margin,
        minY: Math.min(0.0, minY - margin * 0.5),
        minZ: minZ - margin,
        maxX: maxX + margin,
        maxY: maxY + margin,
        maxZ: maxZ + margin
    };

    const sunAz = (doc?.sunAzimuthDeg ?? 46) * DEG2RAD;
    const sunEl = (doc?.sunElevationDeg ?? 42) * DEG2RAD;
    const sunDir = [
        Math.cos(sunEl) * Math.sin(sunAz),
        Math.sin(sunEl),
        Math.cos(sunEl) * Math.cos(sunAz)
    ];

    return {
        doc,
        compiledLayers,
        primitives: allPrimitives,
        totalPlanes,
        bounds,
        sunDir,
        meshResolution: clamp(doc?.meshResolution ?? 84, 36, 140)
    };
}

function chamferMin(a, b, r)
{
    const m = Math.min(a, b);
    if (r <= 1e-6) return m;
    return Math.min(m, (a + b - r) * 0.70710678);
}

/**
 * Evaluates a single compiled plain rock primitive at world `(wx, wy, wz)`.
 * Exact 1:1 algebraic match with `evalPlainPrimitive` in `RockFormation2Viewport.js`!
 */
export function evalRock2PrimitiveSdf(prim, wx, wy, wz)
{
    const invR = prim.invR, invT = prim.invT;
    let qx = invR[0] * wx + invR[1] * wy + invR[2] * wz + invT[0];
    const qy = invR[3] * wx + invR[4] * wy + invR[5] * wz + invT[1];
    let qz = invR[6] * wx + invR[7] * wy + invR[8] * wz + invT[2];

    let scaleFactor = 1.0;
    if (prim.hasDeform)
    {
        const h = clamp((qy - prim.baseY) / prim.refH, -0.25, 1.35);
        const u = h - 0.5;
        const h2 = Math.max(0.0, h);
        let x1 = qx - prim.bendX * h2 * h2 * prim.refH - prim.skewX * u * prim.refH;
        let z1 = qz - prim.bendZ * h2 * h2 * prim.refH;

        if (Math.abs(prim.twistRad) > 1e-5)
        {
            const th = prim.twistRad * u;
            const ct = Math.cos(th), st = Math.sin(th);
            const rx = x1 * ct + z1 * st;
            const rz = -x1 * st + z1 * ct;
            x1 = rx;
            z1 = rz;
        }

        const sTap = Math.max(0.16, 1.0 - prim.taper * (h - 0.35));
        const pRel = (h - 0.34) / 0.25;
        const sPinch = 1.0 - prim.pinch * Math.exp(-pRel * pRel);
        const s = clamp(sTap * sPinch, 0.16, 2.40);
        qx = x1 / s;
        qz = z1 / s;
        scaleFactor = Math.min(1.0, s) * prim.deformPenalty;
    }

    let dPoly = 1e9;
    if (prim.numPlanes > 0)
    {
        const planes = prim.planes;
        const kLse = prim.kLse;
        const invKLse = 1.0 / kLse;
        let dFloor = -1e9;
        let bestPd = -1e9;
        let sumExp = 0.0;

        for (let i = 0, p = 0; i < prim.numPlanes; i++, p += 4)
        {
            const pny = planes[p + 1];
            const pd = planes[p] * qx + pny * qy + planes[p + 2] * qz - planes[p + 3];

            if (prim.sharpBase > 0.5 && pny <= -0.92)
            {
                if (pd > dFloor) dFloor = pd;
            }
            else if (pd > bestPd)
            {
                sumExp = bestPd > -1e8 ? sumExp * Math.exp(Math.max(-24.0, (bestPd - pd) * invKLse)) + 1.0 : 1.0;
                bestPd = pd;
            }
            else
            {
                sumExp += Math.exp(Math.max(-24.0, (pd - bestPd) * invKLse));
            }
        }

        dPoly = bestPd > -1e8 ? (bestPd + kLse * Math.log(Math.max(1.0, sumExp))) : dFloor;
        if (dFloor > -1e8 && bestPd > -1e8)
        {
            dPoly = smax(dPoly, dFloor, prim.baseBevel);
        }
    }

    let dSq = 1e9;
    if (prim.sqWeight > 0.001 || prim.numPlanes === 0)
    {
        const p1 = 2.0 / prim.sqExpH;
        const p2 = 2.0 / prim.sqExpV;
        const m12 = p2 / p1;

        const ux = Math.abs(qx) / prim.sqRx + 1e-5;
        const uy = Math.abs(qy - prim.sqCy) / prim.sqRy + 1e-5;
        const uz = Math.abs(qz) / prim.sqRz + 1e-5;

        const uxP = Math.pow(ux, p1);
        const uzP = Math.pow(uz, p1);
        const A = uxP + uzP;
        const B = Math.pow(A, m12) + Math.pow(uy, p2);

        if (B < 1e-5)
        {
            dSq = -Math.min(prim.sqRx, prim.sqRy, prim.sqRz) * 0.85;
        }
        else
        {
            const rootB = Math.pow(B, 1.0 / p2);
            const F = rootB - 1.0;
            const bTerm = Math.pow(B, 1.0 / p2 - 1.0);
            const aTerm = Math.pow(A, m12 - 1.0);
            const gx = (bTerm * aTerm * Math.pow(ux, p1 - 1.0)) / prim.sqRx;
            const gy = (bTerm * Math.pow(uy, p2 - 1.0)) / prim.sqRy;
            const gz = (bTerm * aTerm * Math.pow(uz, p1 - 1.0)) / prim.sqRz;
            const gradLen = Math.max(1e-4, Math.hypot(gx, gy, gz));
            dSq = (F / gradLen) * 0.92;
        }

        if (prim.flatBaseCut > 0.02)
        {
            dSq = smax(dSq, -qy, prim.baseBevel);
        }
    }

    let dLocal;
    if (prim.numPlanes === 0) dLocal = dSq;
    else if (prim.sqWeight <= 0.001) dLocal = dPoly;
    else dLocal = mix(dPoly, dSq, prim.sqWeight);

    return dLocal * scaleFactor;
}

/**
 * Evaluates the complete RockFormation 2.0 Layer Stack at `(wx, wy, wz)`.
 * Optionally writes blended stone layer color `(r, g, b)` to `outColor`.
 */
export function evalRock2StackSdf(compiledStack, wx, wy, wz, outColor = null)
{
    const { compiledLayers, primitives } = compiledStack;
    if (compiledLayers.length === 0)
    {
        if (outColor) { outColor.r = 0.58; outColor.g = 0.57; outColor.b = 0.55; }
        return 1e6;
    }

    let dAcc = 1e9;
    let colR = compiledLayers[0].color[0];
    let colG = compiledLayers[0].color[1];
    let colB = compiledLayers[0].color[2];
    let hasBase = false;

    for (let li = 0; li < compiledLayers.length; li++)
    {
        const lyr = compiledLayers[li];
        let dLyr = 1e9;
        const end = lyr.blockStart + lyr.blockCount;

        for (let bi = lyr.blockStart; bi < end; bi++)
        {
            const pr = primitives[bi];
            const lb = Math.hypot(wx - pr.cx, wy - pr.cy, wz - pr.cz) - pr.radius;

            if (pr.primOp === 0)
            {
                if (lb > dLyr + lyr.intraBlend) continue;
                const dP = evalRock2PrimitiveSdf(pr, wx, wy, wz);
                dLyr = dLyr > 1e8 ? dP : (lyr.intraBlend > 1e-4 ? smin(dLyr, dP, lyr.intraBlend) : Math.min(dLyr, dP));
            }
            else
            {
                // Intra-layer subtraction (e.g. Rock Arch vault cutter!)
                if (lb > Math.abs(dLyr) + lyr.intraBlend + 0.2) continue;
                const dP = evalRock2PrimitiveSdf(pr, wx, wy, wz);
                if (dLyr < 1e8)
                {
                    dLyr = smax(dLyr, -dP, lyr.intraBlend);
                }
            }
        }

        if (!hasBase)
        {
            if (lyr.combineOp === 3) continue; // Skip leading subtract if nothing exists below it
            dAcc = dLyr;
            colR = lyr.color[0]; colG = lyr.color[1]; colB = lyr.color[2];
            hasBase = true;
            continue;
        }

        const k = lyr.blendRadius;
        const op = lyr.combineOp;
        if (op === 0) // Union
        {
            if (dLyr < dAcc)
            {
                dAcc = dLyr;
                colR = lyr.color[0]; colG = lyr.color[1]; colB = lyr.color[2];
            }
        }
        else if (op === 1) // Smooth Blend (smin)
        {
            const w = k > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / k, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = smin(dAcc, dLyr, k);
            colR = mix(colR, lyr.color[0], w);
            colG = mix(colG, lyr.color[1], w);
            colB = mix(colB, lyr.color[2], w);
        }
        else if (op === 2) // 45° Chamfer Seam
        {
            const w = k > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / k, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = chamferMin(dAcc, dLyr, k);
            colR = mix(colR, lyr.color[0], w);
            colG = mix(colG, lyr.color[1], w);
            colB = mix(colB, lyr.color[2], w);
        }
        else if (op === 3) // Subtract (Cutter)
        {
            const prev = dAcc;
            dAcc = smax(dAcc, -dLyr, k);
            const cutW = smoothstep(0.0, Math.max(0.02, k + 0.04), dAcc - prev);
            colR = mix(colR, lyr.color[0], cutW * 0.65);
            colG = mix(colG, lyr.color[1], cutW * 0.65);
            colB = mix(colB, lyr.color[2], cutW * 0.65);
        }
        else if (op === 4) // Intersect
        {
            const w = dLyr > dAcc ? 1.0 : 0.0;
            dAcc = smax(dAcc, dLyr, k);
            colR = mix(colR, lyr.color[0], w * 0.5);
            colG = mix(colG, lyr.color[1], w * 0.5);
            colB = mix(colB, lyr.color[2], w * 0.5);
        }
    }

    if (outColor)
    {
        outColor.r = colR;
        outColor.g = colG;
        outColor.b = colB;
    }
    return dAcc;
}

/*==========================================================================================================================================
                               6. GPU TEXTURE PACKER & QEF DUAL-CONTOUR MESH BAKER
==========================================================================================================================================*/

/**
 * Packs `compiledStack` into three compact RGBA32F 2D textures for instant (< 1.5 ms) 60 FPS WebGL2 GPU Volume Baking:
 *   - `layerTexData`: width = 2, height = numLayers
 *   - `blockTexData`: width = 9, height = numPrimitives
 *   - `planeTexData`: width = 1, height = totalPlanes
 */
export function packRock2StackGpuBuffers(compiledStack)
{
    const { compiledLayers, primitives } = compiledStack;
    const numLayers = Math.max(1, compiledLayers.length);
    const numBlocks = Math.max(1, primitives.length);
    const totalPlanes = Math.max(1, compiledStack.totalPlanes);

    const layerTexData = new Float32Array(numLayers * 2 * 4);
    const blockTexData = new Float32Array(numBlocks * 9 * 4);
    const planeTexData = new Float32Array(totalPlanes * 4);

    for (let li = 0; li < compiledLayers.length; li++)
    {
        const lyr = compiledLayers[li];
        const off = li * 8;
        // Col 0: (blockStart, blockCount, combineOp, blendRadius)
        layerTexData[off + 0] = lyr.blockStart;
        layerTexData[off + 1] = lyr.blockCount;
        layerTexData[off + 2] = lyr.combineOp;
        layerTexData[off + 3] = lyr.blendRadius;
        // Col 1: (colR, colG, colB, intraBlend)
        layerTexData[off + 4] = lyr.color[0];
        layerTexData[off + 5] = lyr.color[1];
        layerTexData[off + 6] = lyr.color[2];
        layerTexData[off + 7] = lyr.intraBlend;
    }

    let planeCursor = 0;
    for (let i = 0; i < primitives.length; i++)
    {
        const b = primitives[i];
        const off = i * 36;

        // Col 0: (cx, cy, cz, radius)
        blockTexData[off + 0] = b.cx;
        blockTexData[off + 1] = b.cy;
        blockTexData[off + 2] = b.cz;
        blockTexData[off + 3] = b.radius;

        // Col 1: (planeStart, numPlanes, kLse, baseBevel)
        blockTexData[off + 4] = planeCursor;
        blockTexData[off + 5] = b.numPlanes;
        blockTexData[off + 6] = b.kLse;
        blockTexData[off + 7] = b.baseBevel;

        // Col 2..4: invR rows + invT
        blockTexData[off + 8]  = b.invR[0]; blockTexData[off + 9]  = b.invR[1]; blockTexData[off + 10] = b.invR[2]; blockTexData[off + 11] = b.invT[0];
        blockTexData[off + 12] = b.invR[3]; blockTexData[off + 13] = b.invR[4]; blockTexData[off + 14] = b.invR[5]; blockTexData[off + 15] = b.invT[1];
        blockTexData[off + 16] = b.invR[6]; blockTexData[off + 17] = b.invR[7]; blockTexData[off + 18] = b.invR[8]; blockTexData[off + 19] = b.invT[2];

        // Col 5: (sqRx, sqRy, sqRz, sqCy)
        blockTexData[off + 20] = b.sqRx;
        blockTexData[off + 21] = b.sqRy;
        blockTexData[off + 22] = b.sqRz;
        blockTexData[off + 23] = b.sqCy;

        // Col 6: (sqWeight, sqExpH, sqExpV, sharpBase + flatBaseCut*2 + primOp*8)
        blockTexData[off + 24] = b.sqWeight;
        blockTexData[off + 25] = b.sqExpH;
        blockTexData[off + 26] = b.sqExpV;
        blockTexData[off + 27] = b.primOp;

        // Col 7: (baseY, refH, taper, pinch)
        blockTexData[off + 28] = b.baseY;
        blockTexData[off + 29] = b.refH;
        blockTexData[off + 30] = b.taper;
        blockTexData[off + 31] = b.pinch;

        // Col 8: (twistRad, bendX, bendZ, skewX) + Col 9 packed via deformPenalty/flags? Keep width = 10 for total clarity!
        blockTexData[off + 32] = b.twistRad;
        blockTexData[off + 33] = b.bendX;
        blockTexData[off + 34] = b.bendZ;
        blockTexData[off + 35] = b.skewX;

        for (let p = 0; p < b.numPlanes; p++)
        {
            const pOff = (planeCursor + p) * 4;
            planeTexData[pOff + 0] = b.planes[p * 4 + 0];
            planeTexData[pOff + 1] = b.planes[p * 4 + 1];
            planeTexData[pOff + 2] = b.planes[p * 4 + 2];
            planeTexData[pOff + 3] = b.planes[p * 4 + 3];
        }
        planeCursor += b.numPlanes;
    }

    // Also pack auxiliary flags (sharpBase, flatBaseCut, hasDeform, deformPenalty) in a 3rd column of block meta -> let's use width = 10!
    const blockTexData10 = new Float32Array(numBlocks * 10 * 4);
    for (let i = 0; i < primitives.length; i++)
    {
        const b = primitives[i];
        blockTexData10.set(blockTexData.subarray(i * 36, i * 36 + 36), i * 40);
        const off10 = i * 40 + 36;
        blockTexData10[off10 + 0] = b.sharpBase;
        blockTexData10[off10 + 1] = b.flatBaseCut;
        blockTexData10[off10 + 2] = b.hasDeform ? 1.0 : 0.0;
        blockTexData10[off10 + 3] = b.deformPenalty;
    }

    return {
        numLayers: compiledLayers.length,
        numBlocks: primitives.length,
        totalPlanes: compiledStack.totalPlanes,
        layerTexWidth: 2,
        layerTexHeight: numLayers,
        layerTexData,
        blockTexWidth: 10,
        blockTexHeight: numBlocks,
        blockTexData: blockTexData10,
        planeTexWidth: 1,
        planeTexHeight: totalPlanes,
        planeTexData
    };
}

/**
 * Extracts a sharp-creased manifold triangle mesh of the entire RockFormation 2.0 Layer Stack
 * via 4^3 Sparse Narrow-Band Bricks + 3x3 Tikhonov QEF Dual Contouring (`0` Mesh->SDF!).
 */
export function bakeRock2StackQefMesh(compiledStack, options = {})
{
    const t0 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    const { bounds, primitives } = compiledStack;
    const spanMax = Math.max(1.0, bounds.maxX - bounds.minX, bounds.maxY - bounds.minY, bounds.maxZ - bounds.minZ);
    const targetRes = clamp(options.resolution ?? compiledStack.meshResolution ?? 84, 36, 140);
    const voxelSize = spanMax / targetRes;
    const dispBound = voxelSize * 2.2;

    const sdf = evaluateSparseSdf(bounds, voxelSize, {
        positiveDisplacementBound: dispBound,
        negativeDisplacementBound: dispBound,
        fineDisplacementBound: dispBound,
        buildBrickContext(cx, cy, cz, cullRadius)
        {
            const margin = cullRadius + 0.45;
            for (let i = 0; i < primitives.length; i++)
            {
                const b = primitives[i];
                if (Math.hypot(cx - b.cx, cy - b.cy, cz - b.cz) - b.radius <= margin)
                {
                    return true;
                }
            }
            return null;
        },
        evalMacro(wx, wy, wz, active)
        {
            if (!active) return 1e6;
            return evalRock2StackSdf(compiledStack, wx, wy, wz, null);
        },
        evalFine(wx, wy, wz, _dMacro, active)
        {
            if (!active) return 1e6;
            return evalRock2StackSdf(compiledStack, wx, wy, wz, null);
        }
    });

    const rawDc = extractDualContourMesh(sdf, { qefRegularization: 0.042 });
    const cleaned = removeFloatingSpeckles(rawDc.positions, rawDc.indices, rawDc.crease, 10);
    const smoothedPos = taubinSmoothCreasePreserving(cleaned.positions, cleaned.indices, cleaned.crease, options.taubinPasses ?? 1);

    const tmpCol = { r: 0.58, g: 0.57, b: 0.55 };
    const mesh = buildGameReadyAttributes(smoothedPos, cleaned.indices, sdf, {
        creaseAngleDeg: 42,
        uvScale: 0.35,
        evalMaterial: (px, py, pz, _nx, _ny, _nz, ao, cav, edg, outMat) =>
        {
            evalRock2StackSdf(compiledStack, px, py, pz, tmpCol);
            // Clean plain stone shading: gentle edge bevel highlight + subtle cavity AO, ZERO noisy stain!
            const shade = (0.86 + 0.14 * ao) * (1.0 - 0.12 * cav) + 0.05 * edg;
            outMat.r = clamp(tmpCol.r * shade, 0.04, 1.0);
            outMat.g = clamp(tmpCol.g * shade, 0.04, 1.0);
            outMat.b = clamp(tmpCol.b * shade, 0.04, 1.0);
            outMat.roughness = 0.78;
        }
    });

    const t1 = typeof performance !== 'undefined' ? performance.now() : Date.now();
    return {
        mesh,
        sdfStats: {
            voxelSize: Number(voxelSize.toFixed(4)),
            gridDims: [sdf.nx, sdf.ny, sdf.nz],
            activeBricks: sdf.activeBrickCount,
            totalBricks: sdf.totalBricks,
            brickOccupancyPct: Number(((sdf.activeBrickCount / Math.max(1, sdf.totalBricks)) * 100).toFixed(1)),
            evaluatedSamples: sdf.evaluatedSamples,
            vertices: mesh.positions.length / 3,
            triangles: mesh.indices.length / 3,
            layerCount: compiledStack.compiledLayers.length,
            primitiveCount: primitives.length,
            meshToSdfCount: 0,
            bakeMs: Math.round(t1 - t0)
        }
    };
}
