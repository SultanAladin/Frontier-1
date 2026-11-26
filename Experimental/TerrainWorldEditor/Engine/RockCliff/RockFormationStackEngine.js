/*==========================================================================================================================================
                                                ROCKFORMATIONSTACKENGINE.JS
==========================================================================================================================================*/
// Pure-SDF Composable Layer-Stack Engine for RockFormation 2.0:
//   - 8 Atomic Plain Base Rock Shapes (`big_boulder`, `smooth_rock`, `small_rock`, `cliff_block`, `basalt_column`,
//     `spire_pinnacle`, `rock_slab`, `custom_sculpt_rock`) — ZERO arches, ZERO hardcoded multi-rock scatters,
//     ZERO fractures, ZERO thermal crazing, ZERO exfoliation, ZERO chemical/noise surface processing.
//   - Composable Per-Layer Distribution Generators (`none`, `spline`, `array`, `splatter`, `scatter`) with:
//       * Interactive 3D Viewport Multi-Knot Catmull-Rom Splines (ideal for curved cliffs & canyon faces)
//       * Interactive 3D Viewport Array Lines/Frames where every generated rock has distinct mutated geometry,
//         random 3D angles, and controllable spacing randomness (never mechanical identical clones)
//   - Ordered CSG Layer Stack (`union`, `smooth_union`, `chamfer_union`, `subtract`, `intersect`), Solo/Mute,
//     GPU RGBA32F Buffer Packing (`< 2 ms` WebGL2 3D volume bake), and 4^3 Sparse-Brick QEF Dual-Contour Meshing.

import {
    clamp,
    mix as lerp,
    smoothstep,
    DEG2RAD,
    vnorm,
    smin as sminPoly,
    smax as smaxPoly,
    createRng,
    buildStrikeDipFrame
} from './Core/SpatialNoise3D.js';

import { evaluateSparseSdf } from './Core/SparseSdfBrickGrid.js';
import {
    extractDualContourMesh,
    removeFloatingSpeckles,
    taubinSmoothCreasePreserving,
    buildGameReadyAttributes
} from './Core/QefDualContour.js';

import {
    createBoxPoly,
    clipPolyByHalfSpace,
    measurePoly,
    sculptGeologicalParentPoly
} from './PureSdfFractureEngine.js';

/*==========================================================================================================================================
                                      1. CSG COMBINE MODES & STUDIO STONE PALETTE
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
    clay:       { label: 'Studio Stone Grey',   hex: '#94918c', rgb: [0.58, 0.57, 0.55] },
    basalt:     { label: 'Dark Basalt Slate',   hex: '#454a52', rgb: [0.27, 0.29, 0.32] },
    granite:    { label: 'Pale Sierra Granite', hex: '#aea69c', rgb: [0.68, 0.65, 0.61] },
    sandstone:  { label: 'Warm Desert Buff',    hex: '#b88f6b', rgb: [0.72, 0.56, 0.42] },
    cobble:     { label: 'River Cobble Blue',   hex: '#757d85', rgb: [0.46, 0.49, 0.52] },
    limestone:  { label: 'Karst Cream Stone',   hex: '#bab5a6', rgb: [0.73, 0.71, 0.65] },
    redSlate:   { label: 'Red Canyon Slate',    hex: '#9e5e4d', rgb: [0.62, 0.37, 0.30] }
};

export const ROCK2_STONE_TONE_OPTIONS = Object.entries(ROCK2_STONE_TONES).map(([k, v]) => [v.label, k]);

/*==========================================================================================================================================
                     2. 8 ATOMIC PLAIN BASE ROCK GENERATORS (SINGLE ROCK PER BASE NODE · ZERO ARCHES)
==========================================================================================================================================*/

export const ROCK2_NODE_CATALOG = [
    {
        type: 'big_boulder',
        name: 'Big Boulder',
        shortBadge: 'BOULDER',
        glyph: 'Mountain',
        accent: '#e8c27a',
        desc: 'Single plain angular erratic, sub-rounded granite boulder, overhanging prow, cuboid or pedestal block',
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
                badge: 'BASE ROCK',
                glyph: 'Mountain',
                hint: 'Single plain boulder shape — attach Spline / Array / Splatter / Scatter above to place varied rocks.',
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
        desc: 'Single plain water-worn river cobble, smooth pebble, batholith dome, pillow stone or skipping lens',
        defaults: {
            stoneTone: 'cobble',
            seed: 3318,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.2, height: 2.8, length: 3.5,
            smoothStyle: 'cobble',
            roundness: 0.86,
            squarenessHoriz: 0.95,
            squarenessVert: 0.92,
            softFacets: 0.16,
            taper: 0.10,
            bend: 0.0,
            asymmetry: 0.06,
            flatBottom: 0.72
        },
        controlGroups: [
            {
                title: 'Smooth Cobble & Dome Contour',
                badge: 'BASE ROCK',
                glyph: 'Planet',
                hint: 'Single plain smooth rock — morph between spheroidal cobbles, domes, pillows, eggs & flat river lenses.',
                controls: [
                    {
                        type: 'select', key: 'smoothStyle', label: 'Smooth Form Type',
                        options: [
                            ['River Cobble / Pebble', 'cobble'],
                            ['Rounded Batholith Dome', 'dome'],
                            ['Pillowed Cushion Stone', 'pillow'],
                            ['Ovoid Egg / Glacial Stone', 'egg'],
                            ['Flat Skipping Disc / Lens', 'lens']
                        ]
                    },
                    { key: 'roundness', label: 'Surface Polish / Rounding', min: 0.25, max: 1.0, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'squarenessHoriz', label: 'Plan Roundness (Box↔Circle)', min: 0.35, max: 1.65, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'squarenessVert', label: 'Profile Roundness (Slab↔Dome)', min: 0.35, max: 1.65, step: 0.02, decimals: 2, unit: 'e' },
                    { key: 'softFacets', label: 'Subtle Worn Faceting', min: 0.0, max: 0.75, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Crown Taper', min: -0.45, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bend', label: 'Bean / Kidney Curvature', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'asymmetry', label: 'Streamline Skew', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatBottom', label: 'Grounded Base Cut', min: 0.0, max: 1.0, step: 0.05, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'small_rock',
        name: 'Small Rock',
        shortBadge: 'SMALL ROCK',
        glyph: 'Sparkles',
        accent: '#c8d99b',
        desc: 'Single plain small rock clast, angular scree shard, tumbled pebble or flat slate chip',
        defaults: {
            stoneTone: 'clay',
            seed: 5190,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 1.05, height: 0.68, length: 0.88,
            clastStyle: 'angular',
            facetCount: 9,
            roundness: 0.26,
            flatness: 0.22,
            elongation: 0.10,
            asymmetry: 0.12,
            flatBottom: 0.65
        },
        controlGroups: [
            {
                title: 'Small Rock Clast Shape',
                badge: 'BASE ROCK',
                glyph: 'Sparkles',
                hint: 'Single plain small rock — attach Scatter, Splatter, Array, or Spline on this layer to distribute varied stones.',
                controls: [
                    {
                        type: 'select', key: 'clastStyle', label: 'Small Rock Type',
                        options: [
                            ['Angular Talus / Scree Clast', 'angular'],
                            ['Tumbled Stream Pebble', 'pebble'],
                            ['Sharp Wedge Shard', 'shard'],
                            ['Flat Shale / Slate Chip', 'chip']
                        ]
                    },
                    { key: 'facetCount', label: 'Cleavage Facet Count', min: 5, max: 18, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Sharp Scree ↔ Worn Pebble', min: 0.03, max: 0.95, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatness', label: 'Chunky ↔ Flat Slate', min: 0.0, max: 0.78, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'elongation', label: 'Compact ↔ Elongated', min: -0.40, max: 0.70, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'asymmetry', label: 'Wedge Asymmetry', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatBottom', label: 'Resting Base Contact', min: 0.0, max: 1.0, step: 0.05, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'cliff_block',
        name: 'Cliff Block',
        shortBadge: 'CLIFF',
        glyph: 'Layers',
        accent: '#e09f6d',
        desc: 'Single plain cliff buttress — attach interactive 3D Spline to sculpt full curved cliffs & canyon faces',
        defaults: {
            stoneTone: 'sandstone',
            seed: 7042,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 3.6, height: 5.4, length: 2.9,
            cliffProfile: 'vertical',
            facetCount: 10,
            roundness: 0.14,
            overhangLean: 0.08,
            batterTaper: 0.14,
            crestSlope: 0.08,
            prowWedge: 0.15
        },
        controlGroups: [
            {
                title: 'Cliff Buttress Monolith',
                badge: 'BASE ROCK',
                glyph: 'Layers',
                hint: 'Single plain cliff buttress — attach Spline Generator above and drag 3D knots in the viewport to shape cliffs & canyon walls.',
                controls: [
                    {
                        type: 'select', key: 'cliffProfile', label: 'Cliff Block Profile',
                        options: [
                            ['Sheer Vertical Headwall Block', 'vertical'],
                            ['Overhanging Sea-Cliff Brow', 'overhang'],
                            ['Battered Cuesta Rampart', 'rampart'],
                            ['Flat-Capped Mesa Buttress', 'mesa'],
                            ['Angular Promontory Prow', 'prow']
                        ]
                    },
                    { key: 'facetCount', label: 'Face Joint Planes', min: 6, max: 20, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Cliff Arris Rounding', min: 0.02, max: 0.65, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'overhangLean', label: 'Slope Back ↔ Overhang Brow', min: -0.55, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'batterTaper', label: 'Base-to-Crest Taper', min: -0.45, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'crestSlope', label: 'Rim Crest Tilt', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'prowWedge', label: 'Front Face Angularity', min: 0.0, max: 0.60, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'basalt_column',
        name: 'Basalt Column',
        shortBadge: 'BASALT',
        glyph: 'Boxes',
        accent: '#9aa8bd',
        desc: 'Single plain 4/5/6/7/8-sided polygonal basalt prism — pair with Hexagonal Array or Spline for colonnades',
        defaults: {
            stoneTone: 'basalt',
            seed: 5512,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 1.15, height: 4.6, length: 1.15,
            polygonSides: 6,
            irregularity: 0.08,
            edgeBevel: 0.09,
            capTilt: 0.10,
            taper: 0.02,
            bendX: 0.0,
            twistDeg: 0
        },
        controlGroups: [
            {
                title: 'Polygonal Basalt Prism',
                badge: 'BASE ROCK',
                glyph: 'Boxes',
                hint: 'Single plain polygonal basalt column — attach Array (Hexagonal Pack) to build stepped organ-pipe colonnades.',
                controls: [
                    {
                        type: 'select', key: 'polygonSides', label: 'Prism Polygon Sides',
                        options: [
                            ['6-Sided Classic Hexagonal', 6],
                            ['Seed-Varied 5 / 6 / 7 Natural', 0],
                            ['5-Sided Pentagonal Prism', 5],
                            ['4-Sided Tetragonal Prism', 4],
                            ['7-Sided Heptagonal Prism', 7],
                            ['8-Sided Octagonal Prism', 8]
                        ]
                    },
                    { key: 'irregularity', label: 'Side Face Irregularity', min: 0.0, max: 0.35, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'edgeBevel', label: 'Prism Edge Bevel', min: 0.01, max: 0.55, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'capTilt', label: 'Top Termination Tilt', min: 0.0, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Shaft Taper', min: -0.30, max: 0.45, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bendX', label: 'Column Curvature', min: -0.45, max: 0.45, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'twistDeg', label: 'Prism Twist', min: -60, max: 60, step: 1, decimals: 0, unit: '°' }
                ]
            }
        ]
    },
    {
        type: 'spire_pinnacle',
        name: 'Spire & Pinnacle',
        shortBadge: 'SPIRE',
        glyph: 'Activity',
        accent: '#d9b8ff',
        desc: 'Single plain rock spire, obelisk needle, desert hoodoo shaft, leaning fang or flat-topped pinnacle',
        defaults: {
            stoneTone: 'limestone',
            seed: 8821,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 2.3, height: 6.2, length: 2.1,
            spireStyle: 'horn',
            facetSides: 7,
            roundness: 0.16,
            taper: 0.58,
            waistPinch: 0.0,
            twistDeg: 14,
            bendX: 0.10,
            flatCap: 0.08
        },
        controlGroups: [
            {
                title: 'Spire, Needle & Hoodoo Tower',
                badge: 'BASE ROCK',
                glyph: 'Activity',
                hint: 'Single plain vertical rock spire — control taper, hoodoo waist pinch, helical twist & lean.',
                controls: [
                    {
                        type: 'select', key: 'spireStyle', label: 'Spire Archetype',
                        options: [
                            ['Alpine Horn / Obelisk', 'horn'],
                            ['Slender Sea-Stack Needle', 'needle'],
                            ['Pinched Desert Hoodoo', 'hoodoo'],
                            ['Flat-Topped Butte Tower', 'butte']
                        ]
                    },
                    { key: 'facetSides', label: 'Perimeter Facet Sides', min: 4, max: 14, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Ridge Rounding', min: 0.02, max: 0.80, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Vertical Spire Taper', min: -0.35, max: 0.88, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'waistPinch', label: 'Pedestal / Hoodoo Pinch', min: -0.40, max: 0.68, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'twistDeg', label: 'Helical Spire Twist', min: -90, max: 90, step: 2, decimals: 0, unit: '°' },
                    { key: 'bendX', label: 'Cantilever Spire Lean', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatCap', label: 'Summit Plateau Cut', min: 0.0, max: 0.80, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'rock_slab',
        name: 'Rock Slab',
        shortBadge: 'SLAB',
        glyph: 'Scan',
        accent: '#f0b67f',
        desc: 'Single plain tabular rock slab, tilted flatiron hogback, cantilever ledge or flagstone plate',
        defaults: {
            stoneTone: 'redSlate',
            seed: 4410,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 5.4, height: 1.15, length: 4.0,
            planShape: 'plate',
            dipAngleDeg: 16,
            roundness: 0.14,
            edgeTaper: 0.22,
            bend: 0.0
        },
        controlGroups: [
            {
                title: 'Tabular Slab & Flatiron',
                badge: 'BASE ROCK',
                glyph: 'Scan',
                hint: 'Single plain stone slab — pair with Array or Spline to build tilted hogback ridges or strata benches.',
                controls: [
                    {
                        type: 'select', key: 'planShape', label: 'Slab Plan Silhouette',
                        options: [
                            ['Polygonal Bedrock Plate', 'plate'],
                            ['Triangular Flatiron Wedge', 'wedge'],
                            ['Elongated Cantilever Ledge', 'ledge'],
                            ['Rounded Flagstone', 'flagstone']
                        ]
                    },
                    { key: 'dipAngleDeg', label: 'Geological Dip Tilt', min: -65, max: 65, step: 1, decimals: 0, unit: '°' },
                    { key: 'roundness', label: 'Rim Rounding', min: 0.02, max: 0.85, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'edgeTaper', label: 'Chisel Rim Bevel', min: -0.45, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bend', label: 'Slab Saddle Warp', min: -0.40, max: 0.40, step: 0.02, decimals: 2, unit: 'w' }
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
        desc: 'Universal single-rock SDF generator — morph continuously across crystals, spheres, boxes, cylinders, tusks & custom rocks',
        defaults: {
            stoneTone: 'clay',
            seed: 9012,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.4, height: 3.9, length: 3.9,
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
                hint: 'Blend Faceted Half-Space Polyhedra with 3D Superquadrics + Taper, Pinch, Twist, Bend & Skew to make ANY single rock shape.',
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

/*==========================================================================================================================================
               3. INTERACTIVE 3D SPLINE KNOT PRESETS & COMPOSABLE PER-LAYER DISTRIBUTION GENERATORS
==========================================================================================================================================*/

export const ROCK2_ROCK_POOL_OPTIONS = [
    ['Varied Shapes of Base Rock', 'varied_base'],
    ['Mixed Boulders, Cobbles, Slabs & Clasts', 'mixed_boulders'],
    ['Mixed Canyon Outcrop (Cliff + Boulder + Slab + Spire)', 'mixed_outcrop'],
    ['Exact Base Rock Only (No Shape Mutation)', 'uniform_base']
];

const ROCK_POOLS = {
    mixed_boulders: ['big_boulder', 'smooth_rock', 'small_rock', 'rock_slab'],
    mixed_outcrop:  ['cliff_block', 'big_boulder', 'rock_slab', 'spire_pinnacle']
};

/**
 * Builds editable 3D Catmull-Rom knots `[{ x, y, z, scale }]` for quick cliff & canyon spline presets.
 */
export function buildRock2SplinePresetKnots(preset = 'arc', span = 9.2, depth = 2.0)
{
    const half = span * 0.5;
    if (preset === 'scurve')
    {
        return [
            { x: -half,        y: 0.0, z:  depth * 0.55, scale: 1.0 },
            { x: -half * 0.50, y: 0.0, z: -depth * 0.75, scale: 1.05 },
            { x:  0.0,         y: 0.0, z:  0.0,          scale: 1.0 },
            { x:  half * 0.50, y: 0.0, z:  depth * 0.75, scale: 1.05 },
            { x:  half,        y: 0.0, z: -depth * 0.55, scale: 1.0 }
        ];
    }
    if (preset === 'box_canyon')
    {
        return [
            { x: -half * 0.88, y: 0.0, z:  depth * 1.35, scale: 0.95 },
            { x: -half * 0.82, y: 0.0, z: -depth * 0.75, scale: 1.05 },
            { x:  0.0,         y: 0.0, z: -depth * 1.25, scale: 1.12 },
            { x:  half * 0.82, y: 0.0, z: -depth * 0.75, scale: 1.05 },
            { x:  half * 0.88, y: 0.0, z:  depth * 1.35, scale: 0.95 }
        ];
    }
    if (preset === 'line')
    {
        return [
            { x: -half,         y: 0.0, z: 0.0, scale: 1.0 },
            { x: -half * 0.333, y: 0.0, z: 0.0, scale: 1.0 },
            { x:  half * 0.333, y: 0.0, z: 0.0, scale: 1.0 },
            { x:  half,         y: 0.0, z: 0.0, scale: 1.0 }
        ];
    }
    if (preset === 'ring')
    {
        const R = Math.max(1.2, span * 0.42);
        const pts = [];
        for (let i = 0; i < 6; i++)
        {
            const a = (i / 6) * Math.PI * 2.0;
            pts.push({ x: Math.cos(a) * R, y: 0.0, z: Math.sin(a) * R, scale: 1.0 });
        }
        return pts;
    }
    // Default 'arc': natural curved cliff / canyon amphitheater wall
    return [
        { x: -half,        y: 0.0, z:  depth * 0.55, scale: 0.96 },
        { x: -half * 0.36, y: 0.0, z: -depth * 0.52, scale: 1.04 },
        { x:  half * 0.36, y: 0.0, z: -depth * 0.52, scale: 1.04 },
        { x:  half,        y: 0.0, z:  depth * 0.55, scale: 0.96 }
    ];
}

export const ROCK2_GENERATOR_DEFAULTS = {
    none: {
        type: 'none'
    },
    spline: {
        type: 'spline',
        seed: 701,
        splinePreset: 'arc',
        closedLoop: 0,
        knots: buildRock2SplinePresetKnots('arc', 9.2, 2.0),
        count: 7,
        alignToTangent: 1,
        yawOffsetDeg: 0,
        spacingJitter: 0.14,
        lateralJitter: 0.08,
        rockPoolMode: 'varied_base',
        shapeVariety: 0.68,
        aspectVariation: 0.22,
        scaleVariation: 0.14,
        heightVariation: 0.18,
        yawVariationDeg: 9,
        tiltVariationDeg: 3.5,
        intraBlend: 0.24,
        uniqueSeeds: 1
    },
    array: {
        type: 'array',
        seed: 809,
        arrayMode: 'line',      // 'line' | 'grid2d' | 'grid3d' | 'hex' | 'radial'
        // Interactive 3D Viewport Line Endpoints (A -> B)
        startX: -4.8, startY: 0.0, startZ: 0.0,
        endX:    4.8, endY:   0.0, endZ:   0.0,
        countX: 6,
        countY: 2,
        countZ: 3,
        stepX: 2.4,
        stepY: 1.6,
        stepZ: 2.4,
        rowStagger: 0.35,
        // Per-rock variation & spacing randomness (defaults produce genuinely DIFFERENT, randomly angled rocks!)
        rockPoolMode: 'varied_base',
        shapeVariety: 0.82,
        aspectVariation: 0.32,
        scaleMin: 0.72,
        scaleMax: 1.28,
        spacingJitter: 0.35,
        lateralJitter: 0.24,
        heightVariation: 0.22,
        yawJitterDeg: 180,
        tiltJitterDeg: 12,
        // Hex / Radial specific
        hexRings: 2,
        cellSpacing: 1.02,
        terraceSteps: 5,
        terraceDrop: 0.55,
        radialLeanDeg: 3,
        radialCount: 8,
        ringRadius: 3.8,
        arcAngleDeg: 360,
        faceCenter: 0,
        posJitter: 0.18,
        scaleVariation: 0.22,
        intraBlend: 0.0,
        uniqueSeeds: 1
    },
    splatter: {
        type: 'splatter',
        seed: 919,
        splatterMode: 'burst',  // 'burst' | 'talus_fan' | 'clusters' | 'rings'
        count: 14,
        outerRadius: 3.8,
        innerRadiusRatio: 0.0,
        fanSpreadDeg: 110,
        fanAzimuthDeg: 0,
        pileHeight: 0.35,
        radialScaleFalloff: 0.45,
        rockPoolMode: 'varied_base',
        shapeVariety: 0.78,
        aspectVariation: 0.28,
        scaleMin: 0.45,
        scaleMax: 1.25,
        yawRandom: 1.0,
        tiltRandomDeg: 16,
        embedDepth: 0.12,
        intraBlend: 0.0,
        uniqueSeeds: 1
    },
    scatter: {
        type: 'scatter',
        seed: 1031,
        count: 16,
        scatterWidth: 6.8,
        scatterLength: 6.2,
        minSpacing: 0.72,
        moundHeight: 0.0,
        rockPoolMode: 'varied_base',
        shapeVariety: 0.80,
        aspectVariation: 0.30,
        scaleMin: 0.45,
        scaleMax: 1.30,
        sizeBias: 1.55,
        yawRandom: 1.0,
        tiltRandomDeg: 18,
        embedDepth: 0.14,
        intraBlend: 0.0,
        uniqueSeeds: 1
    }
};

export function normalizeSplineKnots(rawKnots, fallbackPreset = 'arc', pathLength = 9.2, curveBend = 2.0)
{
    if (Array.isArray(rawKnots) && rawKnots.length >= 2)
    {
        return rawKnots.map(k => ({
            x: Number.isFinite(k.x) ? k.x : 0.0,
            y: Number.isFinite(k.y) ? k.y : 0.0,
            z: Number.isFinite(k.z) ? k.z : 0.0,
            scale: Number.isFinite(k.scale) ? clamp(k.scale, 0.2, 3.0) : 1.0
        }));
    }
    return buildRock2SplinePresetKnots(fallbackPreset, pathLength, curveBend);
}

export function createRock2Generator(type = 'none', overrides = {})
{
    const cleanType = ROCK2_GENERATOR_DEFAULTS[type] ? type : 'none';
    const base = JSON.parse(JSON.stringify(ROCK2_GENERATOR_DEFAULTS[cleanType]));
    const merged = { ...base, ...overrides, type: cleanType };

    if (cleanType === 'spline')
    {
        const preset = overrides.splinePreset || overrides.splineShape || merged.splinePreset || 'arc';
        merged.splinePreset = preset;
        if (preset === 'ring' && overrides.closedLoop === undefined && !Array.isArray(overrides.knots))
        {
            merged.closedLoop = 1;
        }
        if (Array.isArray(overrides.knots) && overrides.knots.length >= 2)
        {
            merged.knots = normalizeSplineKnots(overrides.knots);
        }
        else if (overrides.splineShape || overrides.splinePreset || overrides.pathLength !== undefined || overrides.curveBend !== undefined || overrides.ringRadius !== undefined)
        {
            const span = overrides.ringRadius !== undefined ? overrides.ringRadius * 2.38 : (overrides.pathLength ?? 9.2);
            const bend = overrides.curveBend ?? 2.0;
            if (preset === 'custom' && overrides.startX !== undefined)
            {
                merged.knots = [
                    { x: overrides.startX ?? -4.5, y: overrides.startY ?? 0.0, z: overrides.startZ ?? 0.8, scale: overrides.scaleStart ?? 1.0 },
                    { x: overrides.midX ?? 0.0,    y: overrides.midY ?? 0.4,   z: overrides.midZ ?? -1.8,  scale: lerp(overrides.scaleStart ?? 1.0, overrides.scaleEnd ?? 1.0, 0.5) },
                    { x: overrides.endX ?? 4.5,    y: overrides.endY ?? 0.0,   z: overrides.endZ ?? 0.8,   scale: overrides.scaleEnd ?? 1.0 }
                ];
            }
            else
            {
                merged.knots = buildRock2SplinePresetKnots(preset, span, bend);
            }
        }
        else
        {
            merged.knots = normalizeSplineKnots(merged.knots);
        }
    }
    else if (cleanType === 'array')
    {
        // Upgrade legacy 'linear' stepX/stepY/stepZ into explicit 3D viewport endpoints (startX/Y/Z -> endX/Y/Z)
        if (merged.arrayMode === 'linear')
        {
            merged.arrayMode = 'line';
            if (overrides.startX === undefined && overrides.endX === undefined &&
                (overrides.stepX !== undefined || overrides.stepY !== undefined || overrides.stepZ !== undefined))
            {
                const n = Math.max(2, Math.round(merged.countX ?? 5));
                const spanX = (overrides.stepX ?? 2.0) * (n - 1);
                const spanY = (overrides.stepY ?? 0.0) * (n - 1);
                const spanZ = (overrides.stepZ ?? 0.0) * (n - 1);
                merged.startX = -spanX * 0.5;
                merged.startY = 0.0;
                merged.startZ = -spanZ * 0.5;
                merged.endX = spanX * 0.5;
                merged.endY = spanY;
                merged.endZ = spanZ * 0.5;
            }
        }
    }

    return merged;
}

let _nextRock2LayerSeq = 1;

export function createRock2Layer(type = 'big_boulder', overrides = {})
{
    let cleanType = type;
    if (cleanType === 'small_rocks') cleanType = 'small_rock';
    else if (cleanType === 'cliff_face') cleanType = 'cliff_block';
    else if (cleanType === 'columnar_basalt') cleanType = 'basalt_column';
    else if (cleanType === 'arch_formation') cleanType = 'big_boulder';

    const spec = getRock2NodeSpec(cleanType);
    const seq = _nextRock2LayerSeq++;
    const params = { ...spec.defaults, ...(overrides.params || {}) };
    for (const k of Object.keys(spec.defaults))
    {
        if (overrides[k] !== undefined) params[k] = overrides[k];
    }

    const genRaw = overrides.generator || { type: 'none' };
    const generator = createRock2Generator(genRaw.type || 'none', genRaw);

    return {
        id: overrides.id || `r2_layer_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
        name: overrides.name || `${spec.name} ${String(seq).padStart(2, '0')}`,
        type: spec.type,
        enabled: overrides.enabled !== undefined ? !!overrides.enabled : true,
        solo: !!overrides.solo,
        combineMode: overrides.combineMode || 'union',
        blendRadius: overrides.blendRadius ?? 0.35,
        params,
        generator
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
        params: JSON.parse(JSON.stringify(layer.params || {})),
        generator: createRock2Generator(layer.generator?.type || 'none', JSON.parse(JSON.stringify(layer.generator || {})))
    };
}

/*==========================================================================================================================================
                     4. STARTER LAYER-STACK PRESETS (3D VIEWPORT SPLINES + VARIED ARRAYS · ZERO ARCHES)
==========================================================================================================================================*/

export const ROCK2_STARTER_STACKS = [
    {
        id: 'stack-cliff-outcrop',
        name: '01 · Canyon Cliff Spline + Boulder + Talus',
        badge: '3D SPLINE CLIFF',
        buildLayers: () => [
            createRock2Layer('cliff_block', {
                name: 'Curved Canyon Wall (3D Spline)',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 7042,
                posZ: -1.0,
                width: 3.1, height: 5.5, length: 2.8,
                cliffProfile: 'vertical', roundness: 0.14, batterTaper: 0.14,
                generator: createRock2Generator('spline', {
                    splinePreset: 'arc',
                    knots: [
                        { x: -4.8, y: 0.0, z:  1.15, scale: 0.94 },
                        { x: -1.6, y: 0.2, z: -0.95, scale: 1.06 },
                        { x:  1.6, y: 0.1, z: -0.95, scale: 1.04 },
                        { x:  4.8, y: 0.0, z:  1.15, scale: 0.92 }
                    ],
                    count: 6,
                    alignToTangent: 1,
                    spacingJitter: 0.10,
                    shapeVariety: 0.72,
                    heightVariation: 0.18,
                    scaleVariation: 0.12,
                    intraBlend: 0.26,
                    uniqueSeeds: 1
                })
            }),
            createRock2Layer('big_boulder', {
                name: 'Front Buttress Boulder',
                combineMode: 'smooth_union',
                blendRadius: 0.42,
                stoneTone: 'sandstone',
                seed: 2188,
                posX: -1.45, posY: 0.0, posZ: 1.05,
                yawDeg: 22,
                width: 3.4, height: 2.6, length: 2.9,
                boulderStyle: 'angular', roundness: 0.22
            }),
            createRock2Layer('small_rock', {
                name: 'Basal Talus Rocks (Splatter)',
                combineMode: 'union',
                stoneTone: 'clay',
                seed: 5190,
                posX: 0.9, posY: 0.0, posZ: 1.55,
                width: 0.95, height: 0.62, length: 0.82,
                clastStyle: 'angular', roundness: 0.24,
                generator: createRock2Generator('splatter', {
                    splatterMode: 'talus_fan',
                    count: 12,
                    outerRadius: 3.2,
                    fanSpreadDeg: 115,
                    fanAzimuthDeg: 0,
                    pileHeight: 0.32,
                    radialScaleFalloff: 0.45,
                    scaleMin: 0.45,
                    scaleMax: 1.20,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-varied-array',
        name: '02 · Varied Boulders Along Array Line',
        badge: 'RANDOM ARRAY',
        buildLayers: () => [
            createRock2Layer('big_boulder', {
                name: 'Randomised Boulder Ridge (Array)',
                combineMode: 'union',
                stoneTone: 'granite',
                seed: 2410,
                width: 2.4, height: 2.1, length: 2.2,
                boulderStyle: 'angular', roundness: 0.26,
                generator: createRock2Generator('array', {
                    arrayMode: 'line',
                    startX: -5.4, startY: 0.0, startZ:  0.8,
                    endX:    5.4, endY:   0.0, endZ: -0.8,
                    countX: 6,
                    rockPoolMode: 'mixed_boulders',
                    shapeVariety: 0.88,
                    aspectVariation: 0.35,
                    scaleMin: 0.68,
                    scaleMax: 1.35,
                    spacingJitter: 0.38,
                    lateralJitter: 0.32,
                    yawJitterDeg: 180,
                    tiltJitterDeg: 14,
                    intraBlend: 0.0,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-canyon-swall',
        name: '03 · Winding Canyon Face (S-Spline)',
        badge: 'CANYON SPLINE',
        buildLayers: () => [
            createRock2Layer('cliff_block', {
                name: 'S-Curve Canyon Escarpment',
                combineMode: 'union',
                stoneTone: 'redSlate',
                seed: 7315,
                width: 2.9, height: 5.6, length: 2.6,
                cliffProfile: 'overhang', roundness: 0.12, overhangLean: 0.22,
                generator: createRock2Generator('spline', {
                    splinePreset: 'scurve',
                    knots: buildRock2SplinePresetKnots('scurve', 11.0, 2.3),
                    count: 8,
                    alignToTangent: 1,
                    spacingJitter: 0.08,
                    shapeVariety: 0.75,
                    heightVariation: 0.20,
                    intraBlend: 0.28,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-basalt',
        name: '04 · Basalt Column + Hex Array',
        badge: 'HEX ARRAY',
        buildLayers: () => [
            createRock2Layer('basalt_column', {
                name: 'Hexagonal Basalt Colonnade',
                combineMode: 'union',
                stoneTone: 'basalt',
                seed: 5512,
                width: 1.02, height: 5.0, length: 1.02,
                polygonSides: 6, edgeBevel: 0.08, capTilt: 0.10,
                generator: createRock2Generator('array', {
                    arrayMode: 'hex',
                    hexRings: 2,
                    cellSpacing: 0.94,
                    terraceSteps: 6,
                    terraceDrop: 0.62,
                    radialLeanDeg: 2.5,
                    shapeVariety: 0.35,
                    heightVariation: 0.10,
                    scaleVariation: 0.04,
                    posJitter: 0.0,
                    yawJitterDeg: 4,
                    tiltJitterDeg: 1,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-big-boulder',
        name: '05 · Plain Big Boulder (Single)',
        badge: 'SINGLE ROCK',
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
        name: '06 · Plain Smooth Cobble (Single)',
        badge: 'SINGLE ROCK',
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
        name: '07 · Small Rock + Scatter Field',
        badge: 'SCATTER',
        buildLayers: () => [
            createRock2Layer('small_rock', {
                name: 'Scattered Small Rocks',
                combineMode: 'union',
                stoneTone: 'clay',
                seed: 5190,
                width: 1.0, height: 0.65, length: 0.85,
                clastStyle: 'angular', roundness: 0.30,
                generator: createRock2Generator('scatter', {
                    count: 16,
                    scatterWidth: 6.4,
                    scatterLength: 5.8,
                    minSpacing: 0.75,
                    shapeVariety: 0.85,
                    scaleMin: 0.45,
                    scaleMax: 1.35,
                    sizeBias: 1.5,
                    uniqueSeeds: 1
                })
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
        version: 3,
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
        version: 3,
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
            params: JSON.parse(JSON.stringify(l.params || {})),
            generator: createRock2Generator(l.generator?.type || 'none', JSON.parse(JSON.stringify(l.generator || {})))
        }))
    };
}

export function deserializeRock2Document(raw)
{
    if (!raw || !Array.isArray(raw.layers) || raw.layers.length === 0)
    {
        return createDefaultRock2Document();
    }
    const layers = raw.layers
        .filter(l => l && l.type !== 'arch_formation')
        .map(l =>
        {
            let gen = l.generator;
            if (!gen)
            {
                if (l.type === 'small_rocks') gen = createRock2Generator('scatter', { count: l.params?.rockCount ?? 12 });
                else if (l.type === 'columnar_basalt') gen = createRock2Generator('array', { arrayMode: 'hex', hexRings: 2 });
                else gen = createRock2Generator('none');
            }
            return createRock2Layer(l.type || 'big_boulder', {
                id: l.id,
                name: l.name,
                enabled: l.enabled !== false,
                solo: !!l.solo,
                combineMode: l.combineMode || 'union',
                blendRadius: Number.isFinite(l.blendRadius) ? l.blendRadius : 0.35,
                params: l.params || {},
                generator: gen
            });
        });

    if (layers.length === 0) return createDefaultRock2Document();
    return {
        format: 'frontier-rockformation-2.0',
        version: 3,
        starterId: raw.starterId || 'stack-cliff-outcrop',
        name: raw.name || 'Imported Rock Stack',
        meshResolution: clamp(raw.meshResolution ?? 84, 36, 140),
        sunAzimuthDeg: raw.sunAzimuthDeg ?? 46,
        sunElevationDeg: raw.sunElevationDeg ?? 42,
        layers
    };
}

/*==========================================================================================================================================
               5. RIGID MATRICES, ARC-LENGTH 3D CATMULL-ROM SPLINE & RANDOMIZED GENERATOR EVALUATOR
==========================================================================================================================================*/

export function buildRock2EulerMatrix3(yawDeg = 0, pitchDeg = 0, rollDeg = 0)
{
    const y = yawDeg * DEG2RAD;
    const p = pitchDeg * DEG2RAD;
    const r = rollDeg * DEG2RAD;
    const cy = Math.cos(y), sy = Math.sin(y);
    const cp = Math.cos(p), sp = Math.sin(p);
    const cr = Math.cos(r), sr = Math.sin(r);

    return [
        cy * cr + sy * sp * sr,  -cy * sr + sy * sp * cr,  sy * cp,
        cp * sr,                  cp * cr,                -sp,
       -sy * cr + cy * sp * sr,   sy * sr + cy * sp * cr,  cy * cp
    ];
}

function mulMat3(A, B)
{
    return [
        A[0] * B[0] + A[1] * B[3] + A[2] * B[6],
        A[0] * B[1] + A[1] * B[4] + A[2] * B[7],
        A[0] * B[2] + A[1] * B[5] + A[2] * B[8],

        A[3] * B[0] + A[4] * B[3] + A[5] * B[6],
        A[3] * B[1] + A[4] * B[4] + A[5] * B[7],
        A[3] * B[2] + A[4] * B[5] + A[5] * B[8],

        A[6] * B[0] + A[7] * B[3] + A[8] * B[6],
        A[6] * B[1] + A[7] * B[4] + A[8] * B[7],
        A[6] * B[2] + A[7] * B[5] + A[8] * B[8]
    ];
}

function invertRigidTransform(R, pivot)
{
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

export function forwardRock2Point(R, pivot, lx, ly, lz)
{
    return [
        R[0] * lx + R[1] * ly + R[2] * lz + pivot[0],
        R[3] * lx + R[4] * ly + R[5] * lz + pivot[1],
        R[6] * lx + R[7] * ly + R[8] * lz + pivot[2]
    ];
}

export function inverseRock2Vector(R, wx, wy, wz)
{
    return [
        R[0] * wx + R[3] * wy + R[6] * wz,
        R[1] * wx + R[4] * wy + R[7] * wz,
        R[2] * wx + R[5] * wy + R[8] * wz
    ];
}

function catmullRomScalar(p0, p1, p2, p3, t)
{
    const t2 = t * t;
    const t3 = t2 * t;
    return 0.5 * (
        (2.0 * p1) +
        (-p0 + p2) * t +
        (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t2 +
        (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t3
    );
}

/**
 * Evaluates a dense arc-length table for an interactive multi-knot 3D Catmull-Rom Spline (`g.knots`).
 * Shared 1:1 by the Pure-SDF Compiler AND the 3D Viewport Spline Gizmo Overlay!
 */
export function buildRock2SplineArcTable(gOrKnots, samplesPerSeg = 20)
{
    const isArrayInput = Array.isArray(gOrKnots);
    const knots = normalizeSplineKnots(isArrayInput ? gOrKnots : gOrKnots?.knots);
    const n = knots.length;
    const closed = isArrayInput
        ? (typeof samplesPerSeg === 'boolean' ? (samplesPerSeg && n >= 3) : false)
        : ((gOrKnots?.closedLoop ?? 0) > 0.5 && n >= 3);
    const segSamples = typeof samplesPerSeg === 'number' ? Math.max(4, Math.round(samplesPerSeg)) : 20;
    const segCount = closed ? n : Math.max(1, n - 1);

    const getK = idx =>
    {
        if (closed) return knots[((idx % n) + n) % n];
        if (idx < 0)
        {
            return {
                x: 2 * knots[0].x - knots[1].x,
                y: 2 * knots[0].y - knots[1].y,
                z: 2 * knots[0].z - knots[1].z,
                scale: knots[0].scale
            };
        }
        if (idx >= n)
        {
            return {
                x: 2 * knots[n - 1].x - knots[n - 2].x,
                y: 2 * knots[n - 1].y - knots[n - 2].y,
                z: 2 * knots[n - 1].z - knots[n - 2].z,
                scale: knots[n - 1].scale
            };
        }
        return knots[idx];
    };

    const samples = [];
    let totalLen = 0.0;

    const totalSteps = segCount * segSamples;
    for (let s = 0; s <= totalSteps; s++)
    {
        const uSeg = Math.min(segCount - 1e-6, (s / totalSteps) * segCount);
        const seg = Math.min(segCount - 1, Math.floor(uSeg));
        const t = uSeg - seg;

        const k0 = getK(seg - 1);
        const k1 = getK(seg);
        const k2 = getK(seg + 1);
        const k3 = getK(seg + 2);

        const x = catmullRomScalar(k0.x, k1.x, k2.x, k3.x, t);
        const y = catmullRomScalar(k0.y, k1.y, k2.y, k3.y, t);
        const z = catmullRomScalar(k0.z, k1.z, k2.z, k3.z, t);
        const sc = clamp(catmullRomScalar(k0.scale ?? 1, k1.scale ?? 1, k2.scale ?? 1, k3.scale ?? 1, t), 0.2, 3.0);

        if (samples.length > 0)
        {
            const prev = samples[samples.length - 1];
            totalLen += Math.hypot(x - prev.x, y - prev.y, z - prev.z);
        }
        samples.push({ x, y, z, scale: sc, arc: totalLen, segIndex: seg, segT: t });
    }

    // Compute smooth horizontal unit tangents (tx, tz) for cliff-face alignment
    for (let i = 0; i < samples.length; i++)
    {
        const a = samples[Math.max(0, i - 1)];
        const b = samples[Math.min(samples.length - 1, i + 1)];
        let dx = b.x - a.x;
        let dz = b.z - a.z;
        const hl = Math.hypot(dx, dz);
        if (hl < 1e-5) { dx = 1.0; dz = 0.0; }
        else { dx /= hl; dz /= hl; }
        samples[i].tx = dx;
        samples[i].tz = dz;
    }

    return { knots, closed, totalLen: Math.max(1e-4, totalLen), samples };
}

export function sampleRock2SplineAtNormalizedArc(arcTable, uNorm)
{
    const { samples, totalLen, closed } = arcTable;
    let u = uNorm;
    if (closed) u = ((u % 1.0) + 1.0) % 1.0;
    else u = clamp(u, 0.0, 1.0);

    const targetArc = u * totalLen;
    let lo = 0, hi = samples.length - 1;
    while (lo + 1 < hi)
    {
        const mid = (lo + hi) >> 1;
        if (samples[mid].arc <= targetArc) lo = mid;
        else hi = mid;
    }
    const s0 = samples[lo], s1 = samples[hi];
    const span = Math.max(1e-6, s1.arc - s0.arc);
    const f = clamp((targetArc - s0.arc) / span, 0.0, 1.0);

    let tx = lerp(s0.tx, s1.tx, f);
    let tz = lerp(s0.tz, s1.tz, f);
    const tl = Math.hypot(tx, tz) || 1;
    tx /= tl; tz /= tl;

    return {
        x: lerp(s0.x, s1.x, f),
        y: lerp(s0.y, s1.y, f),
        z: lerp(s0.z, s1.z, f),
        scale: lerp(s0.scale, s1.scale, f),
        tx,
        ty: 0.0,
        tz
    };
}

function chooseInstanceRockType(baseType, poolMode, instRng, index)
{
    if (!poolMode || poolMode === 'varied_base' || poolMode === 'uniform_base')
    {
        return baseType;
    }
    const pool = ROCK_POOLS[poolMode];
    if (!pool || pool.length === 0) return baseType;
    // Stratified shuffle so adjacent rocks in an array are guaranteed diverse!
    const pickIdx = (index + instRng.int(0, pool.length)) % pool.length;
    return pool[pickIdx];
}

/**
 * Evaluates a layer's placement generator (`none`, `spline`, `array`, `splatter`, `scatter`)
 * into an ordered array of local rock instance descriptors + `intraBlend`.
 * Every multi-rock generator produces genuinely distinct per-instance shapes, proportions & orientations!
 */
export function evaluateRock2GeneratorInstances(generator, baseRockParams, baseType = 'big_boulder')
{
    const g = generator || { type: 'none' };
    const type = g.type || 'none';

    if (type === 'none')
    {
        return {
            intraBlend: 0.0,
            instances: [{
                pos: [0, 0, 0],
                yawDeg: 0, pitchDeg: 0, rollDeg: 0,
                scaleX: 1, scaleY: 1, scaleZ: 1,
                seedOffset: 0,
                shapeVariety: 0.0,
                rockType: baseType
            }]
        };
    }

    const rng = createRng(((g.seed ?? 701) * 2654435761) ^ ((baseRockParams?.seed ?? 101) * 40503));
    const unique = (g.uniqueSeeds ?? 1) > 0.5;
    const poolMode = g.rockPoolMode || 'varied_base';
    const shapeVariety = poolMode === 'uniform_base' ? 0.0 : clamp(g.shapeVariety ?? 0.75, 0.0, 1.0);
    const aspectVar = poolMode === 'uniform_base' ? 0.0 : clamp(g.aspectVariation ?? 0.28, 0.0, 0.75);
    const instances = [];

    if (type === 'spline')
    {
        const count = clamp(Math.round(g.count ?? 7), 2, 56);
        const arcTable = buildRock2SplineArcTable(g, 20);
        const closed = arcTable.closed;
        const spJitter = clamp(g.spacingJitter ?? 0.14, 0.0, 0.85);
        const latJit = g.lateralJitter ?? 0.08;
        const sVar = clamp(g.scaleVariation ?? 0.14, 0.0, 0.85);
        const hVar = clamp(g.heightVariation ?? 0.18, 0.0, 0.85);
        const yawVar = g.yawVariationDeg ?? 9;
        const tiltVar = g.tiltVariationDeg ?? 3.5;
        const align = (g.alignToTangent ?? 1) > 0.5;
        const yawOff = g.yawOffsetDeg ?? 0;

        const stepU = closed ? (1.0 / count) : (count > 1 ? 1.0 / (count - 1) : 0.0);

        for (let i = 0; i < count; i++)
        {
            const baseU = closed ? (i * stepU) : (count === 1 ? 0.5 : i * stepU);
            // Keep first & last anchors crisp at low jitter so cliff ends match the viewport endpoints cleanly
            const endMask = (!closed && (i === 0 || i === count - 1)) ? 0.25 : 1.0;
            const uJit = baseU + rng.signed() * stepU * 0.44 * spJitter * endMask;
            const sp = sampleRock2SplineAtNormalizedArc(arcTable, uJit);

            const nx = -sp.tz, nz = sp.tx;
            const lat = rng.signed() * latJit;

            const knotScale = sp.scale ?? 1.0;
            const sBase = Math.max(0.18, knotScale * (1.0 + rng.signed() * sVar));
            const ax = 1.0 + rng.signed() * aspectVar * 0.65;
            const az = 1.0 + rng.signed() * aspectVar * 0.65;
            const sH = Math.max(0.18, knotScale * (1.0 + rng.signed() * hVar));

            const tangYawDeg = align ? (-Math.atan2(sp.tz, sp.tx) / DEG2RAD) : 0.0;
            const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

            instances.push({
                pos: [sp.x + nx * lat, sp.y, sp.z + nz * lat],
                yawDeg: tangYawDeg + yawOff + rng.signed() * yawVar,
                pitchDeg: rng.signed() * tiltVar,
                rollDeg: rng.signed() * tiltVar,
                scaleX: Math.max(0.16, sBase * ax),
                scaleY: sH,
                scaleZ: Math.max(0.16, sBase * az),
                seedOffset: unique ? (i + 1) * 7919 : 0,
                shapeVariety,
                rockType: rType
            });
        }
        return { intraBlend: clamp(g.intraBlend ?? 0.24, 0.0, 2.5), instances };
    }

    if (type === 'array')
    {
        const mode = (g.arrayMode === 'linear' || !g.arrayMode) ? 'line' : g.arrayMode;
        const hVar = clamp(g.heightVariation ?? 0.20, 0.0, 0.85);
        const yawJit = g.yawJitterDeg ?? 180;
        const tiltJit = g.tiltJitterDeg ?? 12;

        if (mode === 'line')
        {
            const n = clamp(Math.round(g.countX ?? 6), 2, 48);
            const ax = g.startX ?? -4.8, ay = g.startY ?? 0.0, az = g.startZ ?? 0.0;
            const bx = g.endX ?? 4.8,    by = g.endY ?? 0.0,   bz = g.endZ ?? 0.0;
            const dx = bx - ax, dy = by - ay, dz = bz - az;
            const horizLen = Math.hypot(dx, dz);
            const tx = horizLen > 1e-4 ? dx / horizLen : 1.0;
            const tz = horizLen > 1e-4 ? dz / horizLen : 0.0;
            const nx = -tz, nz = tx;

            const spJitter = clamp(g.spacingJitter ?? 0.35, 0.0, 0.95);
            const latJit = g.lateralJitter ?? 0.24;
            const sMin = Math.min(g.scaleMin ?? 0.72, g.scaleMax ?? 1.28);
            const sMax = Math.max(g.scaleMin ?? 0.72, g.scaleMax ?? 1.28);
            const stepT = n > 1 ? 1.0 / (n - 1) : 0.0;
            const lineYawDeg = -Math.atan2(tz, tx) / DEG2RAD;

            for (let i = 0; i < n; i++)
            {
                const baseT = n > 1 ? i * stepT : 0.5;
                //Jitter along the A->B line while keeping endpoints bounded near A and B
                const endWeight = (i === 0 || i === n - 1) ? 0.35 : 1.0;
                const t = clamp(baseT + rng.signed() * stepT * 0.46 * spJitter * endWeight, 0.0, 1.0);
                const lat = rng.signed() * latJit;

                const uniS = lerp(sMin, sMax, rng.float());
                const wAspect = 1.0 + rng.signed() * aspectVar;
                const lAspect = 1.0 + rng.signed() * aspectVar;
                const hAspect = 1.0 + rng.signed() * hVar;
                const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

                instances.push({
                    pos: [
                        ax + dx * t + nx * lat,
                        ay + dy * t,
                        az + dz * t + nz * lat
                    ],
                    yawDeg: lineYawDeg + rng.signed() * yawJit,
                    pitchDeg: rng.signed() * tiltJit,
                    rollDeg: rng.signed() * tiltJit,
                    scaleX: Math.max(0.16, uniS * wAspect),
                    scaleY: Math.max(0.16, uniS * hAspect),
                    scaleZ: Math.max(0.16, uniS * lAspect),
                    seedOffset: unique ? (i + 1) * 7919 : 0,
                    shapeVariety,
                    rockType: rType
                });
            }
        }
        else if (mode === 'grid2d' || mode === 'grid3d')
        {
            const nx = clamp(Math.round(g.countX ?? 3), 1, 10);
            const ny = mode === 'grid3d' ? clamp(Math.round(g.countY ?? 2), 1, 5) : 1;
            const nz = clamp(Math.round(g.countZ ?? 3), 1, 10);
            const dx = g.stepX ?? 2.4;
            const dy = g.stepY ?? 1.6;
            const dz = g.stepZ ?? 2.4;
            const stag = g.rowStagger ?? 0.35;
            const posJit = g.posJitter ?? 0.18;
            const sVar = clamp(g.scaleVariation ?? 0.22, 0.0, 0.85);
            let idx = 0;

            for (let iy = 0; iy < ny; iy++)
            {
                for (let iz = 0; iz < nz; iz++)
                {
                    const rowOff = ((iz + iy) % 2) * stag * dx;
                    for (let ix = 0; ix < nx; ix++)
                    {
                        if (instances.length >= 64) break;
                        const s = Math.max(0.18, 1.0 + rng.signed() * sVar);
                        const rType = chooseInstanceRockType(baseType, poolMode, rng, idx);
                        instances.push({
                            pos: [
                                (ix - (nx - 1) * 0.5) * dx + rowOff + rng.signed() * posJit,
                                iy * dy,
                                (iz - (nz - 1) * 0.5) * dz + rng.signed() * posJit
                            ],
                            yawDeg: rng.signed() * yawJit,
                            pitchDeg: rng.signed() * tiltJit,
                            rollDeg: rng.signed() * tiltJit,
                            scaleX: Math.max(0.16, s * (1.0 + rng.signed() * aspectVar)),
                            scaleY: Math.max(0.16, s * (1.0 + rng.signed() * hVar)),
                            scaleZ: Math.max(0.16, s * (1.0 + rng.signed() * aspectVar)),
                            seedOffset: unique ? (++idx) * 7919 : 0,
                            shapeVariety,
                            rockType: rType
                        });
                    }
                }
            }
        }
        else if (mode === 'hex')
        {
            const rings = clamp(Math.round(g.hexRings ?? 2), 1, 4);
            const spacing = Math.max(0.25, g.cellSpacing ?? 1.02);
            const steps = clamp(Math.round(g.terraceSteps ?? 5), 1, 14);
            const drop = clamp(g.terraceDrop ?? 0.55, 0.0, 0.90);
            const lean = g.radialLeanDeg ?? 2.5;
            const posJit = g.posJitter ?? 0.0;
            const sVar = clamp(g.scaleVariation ?? 0.05, 0.0, 0.65);
            const maxRad = Math.max(1.0, rings * spacing);
            let idx = 0;

            for (let q = -rings; q <= rings; q++)
            {
                const r1 = Math.max(-rings, -q - rings);
                const r2 = Math.min(rings, -q + rings);
                for (let r = r1; r <= r2; r++)
                {
                    if (instances.length >= 64) break;
                    const lx = spacing * (q + r * 0.5);
                    const lz = spacing * (r * 0.8660254);
                    const distNorm = clamp(Math.hypot(lx, lz) / maxRad, 0.0, 1.0);
                    const frontBias = clamp((lz / maxRad) * 0.28, -0.25, 0.30);
                    let hFrac = clamp(1.0 - drop * (distNorm * 0.72 + Math.max(0, frontBias)), 0.16, 1.0);
                    if (steps > 1) hFrac = Math.max(0.16, Math.round(hFrac * steps) / steps);
                    hFrac = Math.max(0.15, hFrac * (1.0 + rng.signed() * hVar));
                    const s = Math.max(0.20, 1.0 + rng.signed() * sVar);

                    const nx = maxRad > 1e-4 ? lx / maxRad : 0;
                    const nz = maxRad > 1e-4 ? lz / maxRad : 0;
                    const rType = chooseInstanceRockType(baseType, poolMode, rng, idx);

                    instances.push({
                        pos: [lx + rng.signed() * posJit, 0.0, lz + rng.signed() * posJit],
                        yawDeg: rng.signed() * yawJit,
                        pitchDeg: nz * lean + rng.signed() * tiltJit,
                        rollDeg: -nx * lean + rng.signed() * tiltJit,
                        scaleX: s,
                        scaleY: hFrac,
                        scaleZ: s,
                        seedOffset: unique ? (++idx) * 7919 : 0,
                        shapeVariety: shapeVariety * 0.45,
                        rockType: rType
                    });
                }
            }
        }
        else if (mode === 'radial')
        {
            const n = clamp(Math.round(g.radialCount ?? 8), 3, 40);
            const rad = Math.max(0.3, g.ringRadius ?? 3.8);
            const arc = clamp(g.arcAngleDeg ?? 360, 30, 360);
            const fullCircle = arc >= 359.5;
            const face = (g.faceCenter ?? 0) > 0.5;
            const posJit = g.posJitter ?? 0.18;
            const sVar = clamp(g.scaleVariation ?? 0.22, 0.0, 0.85);
            const spJit = clamp(g.spacingJitter ?? 0.25, 0.0, 0.90);
            const stepU = fullCircle ? (1.0 / n) : (n > 1 ? 1.0 / (n - 1) : 0.0);

            for (let i = 0; i < n; i++)
            {
                const t = clamp((i * stepU) + rng.signed() * stepU * 0.42 * spJit, 0.0, 1.0);
                const deg = -arc * 0.5 + t * arc;
                const a = deg * DEG2RAD;
                const lx = Math.sin(a) * rad + rng.signed() * posJit;
                const lz = Math.cos(a) * rad + rng.signed() * posJit;
                const s = Math.max(0.18, 1.0 + rng.signed() * sVar);
                const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

                instances.push({
                    pos: [lx, 0.0, lz],
                    yawDeg: (face ? deg : 0) + rng.signed() * yawJit,
                    pitchDeg: rng.signed() * tiltJit,
                    rollDeg: rng.signed() * tiltJit,
                    scaleX: Math.max(0.16, s * (1.0 + rng.signed() * aspectVar)),
                    scaleY: Math.max(0.18, s * (1.0 + rng.signed() * hVar)),
                    scaleZ: Math.max(0.16, s * (1.0 + rng.signed() * aspectVar)),
                    seedOffset: unique ? (i + 1) * 7919 : 0,
                    shapeVariety,
                    rockType: rType
                });
            }
        }

        return { intraBlend: clamp(g.intraBlend ?? 0.0, 0.0, 2.5), instances };
    }

    if (type === 'splatter')
    {
        const mode = g.splatterMode || 'burst';
        const count = clamp(Math.round(g.count ?? 14), 2, 64);
        const outerR = Math.max(0.3, g.outerRadius ?? 3.8);
        const innerR = outerR * clamp(g.innerRadiusRatio ?? 0.0, 0.0, 0.90);
        const sMin = Math.min(g.scaleMin ?? 0.45, g.scaleMax ?? 1.25);
        const sMax = Math.max(g.scaleMin ?? 0.45, g.scaleMax ?? 1.25);
        const falloff = clamp(g.radialScaleFalloff ?? 0.45, -0.85, 0.90);
        const pileH = g.pileHeight ?? 0.35;
        const yawRand = clamp(g.yawRandom ?? 1.0, 0.0, 1.0);
        const tiltRand = g.tiltRandomDeg ?? 16;
        const embed = clamp(g.embedDepth ?? 0.12, 0.0, 0.75);
        const baseH = baseRockParams?.height ?? 1.0;

        const clusterCenters = [];
        if (mode === 'clusters')
        {
            const numC = 3;
            for (let c = 0; c < numC; c++)
            {
                const ca = (c / numC) * Math.PI * 2 + rng.signed() * 0.45;
                const cr = outerR * rng.range(0.32, 0.62);
                clusterCenters.push([Math.cos(ca) * cr, Math.sin(ca) * cr]);
            }
        }

        for (let i = 0; i < count; i++)
        {
            let lx = 0, lz = 0, radNorm = 0;

            if (mode === 'talus_fan')
            {
                const az0 = (g.fanAzimuthDeg ?? 0) * DEG2RAD;
                const halfSpread = ((g.fanSpreadDeg ?? 110) * 0.5) * DEG2RAD;
                const u = Math.pow(rng.float(), 0.72);
                const r = lerp(innerR, outerR, u);
                const ang = az0 + rng.signed() * halfSpread;
                lx = Math.sin(ang) * r;
                lz = Math.cos(ang) * r;
                radNorm = outerR > 1e-4 ? r / outerR : 0;
            }
            else if (mode === 'clusters')
            {
                const cc = clusterCenters[i % clusterCenters.length];
                const subR = outerR * 0.38 * Math.sqrt(rng.float());
                const subA = rng.float() * Math.PI * 2;
                lx = cc[0] + Math.cos(subA) * subR;
                lz = cc[1] + Math.sin(subA) * subR;
                radNorm = clamp(Math.hypot(lx, lz) / outerR, 0, 1);
            }
            else if (mode === 'rings')
            {
                const ringCount = 2 + (count >= 16 ? 1 : 0);
                const ringIdx = i % ringCount;
                const r = lerp(Math.max(innerR, outerR * 0.28), outerR, ringCount > 1 ? ringIdx / (ringCount - 1) : 1.0)
                        + rng.signed() * outerR * 0.06;
                const ang = (i * 2.399963) + rng.signed() * 0.18;
                lx = Math.cos(ang) * r;
                lz = Math.sin(ang) * r;
                radNorm = clamp(r / outerR, 0, 1);
            }
            else
            {
                const u = i === 0 && innerR < 0.05 ? 0.0 : Math.pow((i + rng.float() * 0.65) / count, 0.62);
                const r = lerp(innerR, outerR, clamp(u, 0, 1));
                const ang = i * 2.3999632 + rng.signed() * 0.32;
                lx = Math.cos(ang) * r;
                lz = Math.sin(ang) * r;
                radNorm = outerR > 1e-4 ? r / outerR : 0;
            }

            const envScale = clamp(1.0 - falloff * radNorm, 0.22, 1.85);
            const randScale = lerp(sMin, sMax, Math.pow(rng.float(), 1.25));
            const s = Math.max(0.15, randScale * envScale);
            const sY = Math.max(0.15, s * (1.0 + rng.signed() * aspectVar * 0.8));
            const ly = pileH * Math.max(0.0, 1.0 - radNorm * radNorm) - embed * baseH * sY;
            const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

            instances.push({
                pos: [lx, ly, lz],
                yawDeg: rng.signed() * 180.0 * yawRand,
                pitchDeg: rng.signed() * tiltRand,
                rollDeg: rng.signed() * tiltRand,
                scaleX: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar)),
                scaleY: sY,
                scaleZ: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar)),
                seedOffset: unique ? (i + 1) * 7919 : 0,
                shapeVariety,
                rockType: rType
            });
        }

        return { intraBlend: clamp(g.intraBlend ?? 0.0, 0.0, 2.5), instances };
    }

    if (type === 'scatter')
    {
        const count = clamp(Math.round(g.count ?? 16), 2, 64);
        const halfW = Math.max(0.25, (g.scatterWidth ?? 6.8) * 0.5);
        const halfL = Math.max(0.25, (g.scatterLength ?? 6.2) * 0.5);
        const relax = clamp(g.minSpacing ?? 0.72, 0.0, 1.0);
        const moundH = g.moundHeight ?? 0.0;
        const sMin = Math.min(g.scaleMin ?? 0.45, g.scaleMax ?? 1.30);
        const sMax = Math.max(g.scaleMin ?? 0.45, g.scaleMax ?? 1.30);
        const bias = clamp(g.sizeBias ?? 1.55, 0.25, 3.5);
        const yawRand = clamp(g.yawRandom ?? 1.0, 0.0, 1.0);
        const tiltRand = g.tiltRandomDeg ?? 18;
        const embed = clamp(g.embedDepth ?? 0.14, 0.0, 0.75);
        const baseFoot = Math.max(0.25, Math.hypot(baseRockParams?.width ?? 1.0, baseRockParams?.length ?? 1.0) * 0.45);
        const baseH = baseRockParams?.height ?? 1.0;

        const placed = [];
        for (let i = 0; i < count; i++)
        {
            const s = lerp(sMin, sMax, Math.pow(rng.float(), bias));
            const myFoot = baseFoot * s * relax;

            let bestX = 0, bestZ = 0, bestClear = -1e9;
            const candidates = relax > 0.05 ? 14 : 1;
            for (let c = 0; c < candidates; c++)
            {
                const cx = rng.signed() * halfW;
                const cz = rng.signed() * halfL;
                let minClear = 1e9;
                for (let pIdx = 0; pIdx < placed.length; pIdx++)
                {
                    const pl = placed[pIdx];
                    const clear = Math.hypot(cx - pl.x, cz - pl.z) - (myFoot + pl.r);
                    if (clear < minClear) minClear = clear;
                }
                if (placed.length === 0 || minClear > bestClear)
                {
                    bestClear = minClear;
                    bestX = cx;
                    bestZ = cz;
                    if (minClear >= 0.05) break;
                }
            }
            placed.push({ x: bestX, z: bestZ, r: myFoot });

            const rNorm2 = clamp((bestX * bestX) / (halfW * halfW) + (bestZ * bestZ) / (halfL * halfL), 0, 1);
            const sY = Math.max(0.15, s * (1.0 + rng.signed() * aspectVar * 0.8));
            const ly = moundH * (1.0 - rNorm2) - embed * baseH * sY;
            const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

            instances.push({
                pos: [bestX, ly, bestZ],
                yawDeg: rng.signed() * 180.0 * yawRand,
                pitchDeg: rng.signed() * tiltRand,
                rollDeg: rng.signed() * tiltRand,
                scaleX: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar)),
                scaleY: sY,
                scaleZ: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar)),
                seedOffset: unique ? (i + 1) * 7919 : 0,
                shapeVariety,
                rockType: rType
            });
        }

        return { intraBlend: clamp(g.intraBlend ?? 0.0, 0.0, 2.5), instances };
    }

    return {
        intraBlend: 0.0,
        instances: [{
            pos: [0, 0, 0], yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            scaleX: 1, scaleY: 1, scaleZ: 1, seedOffset: 0, shapeVariety: 0, rockType: baseType
        }]
    };
}

/**
 * Applies deterministic per-rock shape mutation so arrayed/splined/scattered rocks are genuinely different rocks,
 * never identical clones repeated X times!
 */
function buildMutatedInstanceParams(rockType, layerParams, inst)
{
    const targetSpec = getRock2NodeSpec(rockType);
    // Start from target rock defaults overlaid with matching layer params
    const out = { ...targetSpec.defaults };
    if (rockType === layerParams._sourceType)
    {
        Object.assign(out, layerParams);
    }
    else
    {
        out.stoneTone = layerParams.stoneTone;
    }

    out.width  = Math.max(0.08, (layerParams.width ?? 4.0) * inst.scaleX);
    out.height = Math.max(0.08, (layerParams.height ?? 3.5) * inst.scaleY);
    out.length = Math.max(0.08, (layerParams.length ?? 4.0) * inst.scaleZ);
    out.seed   = ((layerParams.seed ?? 2104) + (inst.seedOffset || 0)) >>> 0;

    // When mixing a slab or small rock into a boulder array, adapt vertical ratio naturally
    if (rockType !== layerParams._sourceType)
    {
        if (rockType === 'rock_slab') out.height *= 0.42;
        else if (rockType === 'small_rock') { out.width *= 0.62; out.height *= 0.55; out.length *= 0.62; }
        else if (rockType === 'spire_pinnacle') { out.width *= 0.65; out.height *= 1.35; out.length *= 0.65; }
    }

    const v = clamp(inst.shapeVariety ?? 0.0, 0.0, 1.0);
    if (v <= 0.01 || inst.seedOffset === 0) return out;

    const mrng = createRng(out.seed ^ 0x9e3779b9);

    if (rockType === 'big_boulder')
    {
        if (v > 0.45 && mrng.float() < v * 0.75)
        {
            const styles = ['angular', 'subrounded', 'prow', 'blocky'];
            out.boulderStyle = styles[mrng.int(0, styles.length)];
        }
        out.facetCount   = clamp(Math.round((out.facetCount ?? 12) + mrng.signed() * 6 * v), 9, 22);
        out.roundness    = clamp((out.roundness ?? 0.24) + mrng.signed() * 0.24 * v, 0.08, 0.85);
        out.angularity   = clamp((out.angularity ?? 0.22) + mrng.signed() * 0.14 * v, 0.05, 0.42);
        out.taper        = clamp((out.taper ?? 0.08) + mrng.signed() * 0.30 * v, -0.35, 0.55);
        out.overhangProw = clamp((out.overhangProw ?? 0.0) + mrng.signed() * 0.26 * v, -0.45, 0.45);
        out.asymmetry    = clamp((out.asymmetry ?? 0.0) + mrng.signed() * 0.34 * v, -0.50, 0.50);
        out.flatTop      = clamp((out.flatTop ?? 0.0) + Math.max(0, mrng.signed() * 0.35 * v), 0.0, 0.65);
    }
    else if (rockType === 'cliff_block')
    {
        if (v > 0.50 && mrng.float() < v * 0.65)
        {
            const profiles = ['vertical', 'overhang', 'rampart', 'mesa', 'prow'];
            out.cliffProfile = profiles[mrng.int(0, profiles.length)];
        }
        out.facetCount   = clamp(Math.round((out.facetCount ?? 11) + mrng.signed() * 5 * v), 9, 18);
        out.roundness    = clamp((out.roundness ?? 0.14) + mrng.signed() * 0.09 * v, 0.03, 0.42);
        out.overhangLean = clamp((out.overhangLean ?? 0.08) + mrng.signed() * 0.22 * v, -0.35, 0.45);
        out.batterTaper  = clamp((out.batterTaper ?? 0.14) + mrng.signed() * 0.20 * v, -0.25, 0.48);
        out.crestSlope   = clamp((out.crestSlope ?? 0.08) + mrng.signed() * 0.26 * v, -0.40, 0.40);
        out.prowWedge    = clamp((out.prowWedge ?? 0.15) + mrng.signed() * 0.22 * v, 0.02, 0.52);
    }
    else if (rockType === 'smooth_rock')
    {
        if (v > 0.45 && mrng.float() < v * 0.75)
        {
            const styles = ['cobble', 'dome', 'pillow', 'egg', 'lens'];
            out.smoothStyle = styles[mrng.int(0, styles.length)];
        }
        out.squarenessHoriz = clamp((out.squarenessHoriz ?? 0.95) + mrng.signed() * 0.32 * v, 0.45, 1.45);
        out.squarenessVert  = clamp((out.squarenessVert ?? 0.92) + mrng.signed() * 0.28 * v, 0.45, 1.40);
        out.softFacets      = clamp((out.softFacets ?? 0.16) + mrng.signed() * 0.22 * v, 0.0, 0.55);
        out.taper           = clamp((out.taper ?? 0.10) + mrng.signed() * 0.24 * v, -0.30, 0.48);
        out.bend            = clamp((out.bend ?? 0.0) + mrng.signed() * 0.25 * v, -0.40, 0.40);
        out.asymmetry       = clamp((out.asymmetry ?? 0.06) + mrng.signed() * 0.25 * v, -0.40, 0.40);
    }
    else if (rockType === 'small_rock')
    {
        if (v > 0.40 && mrng.float() < v * 0.80)
        {
            const styles = ['angular', 'pebble', 'shard', 'chip'];
            out.clastStyle = styles[mrng.int(0, styles.length)];
        }
        out.facetCount = clamp(Math.round((out.facetCount ?? 9) + mrng.signed() * 4 * v), 8, 16);
        out.roundness  = clamp((out.roundness ?? 0.26) + mrng.signed() * 0.30 * v, 0.08, 0.88);
        out.flatness   = clamp((out.flatness ?? 0.22) + mrng.signed() * 0.28 * v, 0.0, 0.68);
        out.elongation = clamp((out.elongation ?? 0.10) + mrng.signed() * 0.30 * v, -0.30, 0.55);
        out.asymmetry  = clamp((out.asymmetry ?? 0.12) + mrng.signed() * 0.28 * v, -0.45, 0.45);
    }
    else if (rockType === 'rock_slab')
    {
        if (v > 0.45 && mrng.float() < v * 0.70)
        {
            const shapes = ['plate', 'ledge', 'flagstone'];
            out.planShape = shapes[mrng.int(0, shapes.length)];
        }
        out.dipAngleDeg = clamp((out.dipAngleDeg ?? 16) + mrng.signed() * 14 * v, -55, 55);
        out.edgeTaper   = clamp((out.edgeTaper ?? 0.22) + mrng.signed() * 0.24 * v, -0.30, 0.55);
        out.bend        = clamp((out.bend ?? 0.0) + mrng.signed() * 0.18 * v, -0.30, 0.30);
    }
    else if (rockType === 'spire_pinnacle')
    {
        out.facetSides = clamp(Math.round((out.facetSides ?? 7) + mrng.signed() * 3 * v), 4, 12);
        out.taper      = clamp((out.taper ?? 0.58) + mrng.signed() * 0.22 * v, 0.15, 0.85);
        out.waistPinch = clamp((out.waistPinch ?? 0.0) + mrng.signed() * 0.25 * v, -0.20, 0.50);
        out.twistDeg   = clamp((out.twistDeg ?? 14) + mrng.signed() * 28 * v, -70, 70);
        out.bendX      = clamp((out.bendX ?? 0.10) + mrng.signed() * 0.20 * v, -0.35, 0.35);
    }
    else if (rockType === 'basalt_column')
    {
        if (v > 0.30) out.polygonSides = [5, 6, 6, 7][mrng.int(0, 4)];
        out.irregularity = clamp((out.irregularity ?? 0.08) + mrng.signed() * 0.08 * v, 0.02, 0.26);
        out.capTilt      = clamp((out.capTilt ?? 0.10) + mrng.signed() * 0.12 * v, 0.0, 0.38);
    }
    else if (rockType === 'custom_sculpt_rock')
    {
        out.analyticBlend = clamp((out.analyticBlend ?? 0.50) + mrng.signed() * 0.32 * v, 0.05, 0.95);
        out.taper         = clamp((out.taper ?? 0.12) + mrng.signed() * 0.30 * v, -0.45, 0.65);
        out.twistDeg      = clamp((out.twistDeg ?? 0.0) + mrng.signed() * 35 * v, -90, 90);
        out.skewX         = clamp((out.skewX ?? 0.0) + mrng.signed() * 0.28 * v, -0.45, 0.45);
    }

    return out;
}

/*==========================================================================================================================================
                               6. ATOMIC PLAIN PRIMITIVE COMPILER & 8 SINGLE-ROCK BUILDERS
==========================================================================================================================================*/

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

    let wMinX = Infinity, wMinY = Infinity, wMinZ = Infinity;
    let wMaxX = -Infinity, wMaxY = -Infinity, wMaxZ = -Infinity;
    const xs = [bMinX, bMaxX], ys = [bMinY, bMaxY], zs = [bMinZ, bMaxZ];
    for (let ix = 0; ix < 2; ix++)
    {
        for (let iy = 0; iy < 2; iy++)
        {
            for (let iz = 0; iz < 2; iz++)
            {
                const wp = forwardRock2Point(R, pivot, xs[ix], ys[iy], zs[iz]);
                if (wp[0] < wMinX) wMinX = wp[0]; if (wp[0] > wMaxX) wMaxX = wp[0];
                if (wp[1] < wMinY) wMinY = wp[1]; if (wp[1] > wMaxY) wMaxY = wp[1];
                if (wp[2] < wMinZ) wMinZ = wp[2]; if (wp[2] > wMaxZ) wMaxZ = wp[2];
            }
        }
    }

    const cx = (wMinX + wMaxX) * 0.5;
    const cy = (wMinY + wMaxY) * 0.5;
    const cz = (wMinZ + wMaxZ) * 0.5;
    const radius = 0.5 * Math.hypot(wMaxX - wMinX, wMaxY - wMinY, wMaxZ - wMinZ) + 0.08;

    const minSpan = Math.max(0.08, Math.min(localMaxX - localMinX, localMaxY - localMinY, localMaxZ - localMinZ));
    const roundness = clamp(opts.roundness ?? 0.20, 0.01, 1.0);
    const kLse = clamp(minSpan * (0.012 + 0.24 * roundness * roundness), 0.004, minSpan * 0.34);
    const baseBevel = clamp(minSpan * (0.015 + 0.14 * roundness), 0.005, 0.35);

    const hasDeform = Math.abs(taper) > 1e-4 || Math.abs(pinch) > 1e-4 || Math.abs(twistRad) > 1e-4
                   || Math.abs(bendX) > 1e-4 || Math.abs(bendZ) > 1e-4 || Math.abs(skewX) > 1e-4;
    const deformStrength = Math.abs(taper) * 0.35 + Math.abs(pinch) * 0.45 + Math.abs(twistRad) * 0.20
                         + (Math.abs(bendX) + Math.abs(bendZ) + Math.abs(skewX)) * 0.40;
    const deformPenalty = hasDeform ? clamp(1.0 / (1.0 + deformStrength), 0.48, 1.0) : 1.0;

    return {
        cx, cy, cz, radius,
        minX: wMinX, minY: wMinY, minZ: wMinZ,
        maxX: wMaxX, maxY: wMaxY, maxZ: wMaxZ,
        invR, invT,
        planes, numPlanes,
        kLse, baseBevel,
        sqRx, sqRy, sqRz, sqCy,
        sqWeight, sqExpH, sqExpV,
        primOp: 0,
        sharpBase: opts.sharpBase ? 1.0 : 0.0,
        flatBaseCut: clamp(opts.flatBaseCut ?? 0.0, 0.0, 1.0),
        baseY, refH,
        taper, pinch, twistRad, bendX, bendZ, skewX,
        hasDeform, deformPenalty
    };
}

function buildSingleBigBoulder(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const style = p.boulderStyle || 'angular';
    const facets = clamp(Math.round(p.facetCount ?? 12), 6, 24);
    const extraCuts = Math.max(0, facets - 6);

    let roundness = p.roundness ?? 0.24;
    let disp = p.angularity ?? 0.22;
    let taper = p.taper ?? 0.08;
    let pinch = 0.0;
    let prow = p.overhangProw ?? 0.0;
    let strikeDip = 0.22;

    if (style === 'subrounded') { roundness = Math.max(roundness, 0.58); disp *= 0.65; }
    else if (style === 'prow') { prow += 0.34; taper -= 0.14; strikeDip = 0.48; }
    else if (style === 'blocky') { disp *= 0.35; strikeDip = 0.08; }
    else if (style === 'pedestal') { taper -= 0.22; pinch = 0.34; }

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

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
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
}

function buildSingleSmoothRock(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    let ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const style = p.smoothStyle || 'cobble';
    const flatCut = clamp(p.flatBottom ?? 0.72, 0.0, 1.0);

    let expH = p.squarenessHoriz ?? 0.95;
    let expV = p.squarenessVert ?? 0.92;
    let taper = p.taper ?? 0.10;
    const softFacets = p.softFacets ?? 0.16;

    if (style === 'dome') { expH = 0.95; expV = 0.88; taper += 0.16; }
    else if (style === 'pillow') { expH = Math.min(expH, 0.58); expV = Math.min(expV, 0.56); }
    else if (style === 'egg') { expH = 1.05; expV = 1.05; taper += 0.28; }
    else if (style === 'lens') { ry *= 0.52; expH = 1.02; expV = 0.96; }

    const cy = flatCut > 0.02 ? ry * (1.0 - 0.22 * flatCut) : ry;
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

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness: p.roundness ?? 0.86,
        sharpBase: flatCut > 0.05,
        flatBaseCut: flatCut,
        sqWeight: poly ? clamp(1.0 - softFacets * 0.85, 0.20, 1.0) : 1.0,
        sqRx: rx, sqRy: ry, sqRz: rz,
        sqCy: cy,
        sqExpH: expH, sqExpV: expV,
        baseY: 0.0,
        refH: ry * 2.0,
        taper,
        bendX: p.bend ?? 0.0,
        skewX: p.asymmetry ?? 0.0
    });
}

function buildSingleSmallRock(p, worldR, worldPivot, rng)
{
    const style = p.clastStyle || 'angular';
    const flatRatio = clamp(p.flatness ?? 0.22, 0.0, 0.80);
    const elong = clamp(p.elongation ?? 0.10, -0.45, 0.75);
    const rx = p.width * 0.5 * (1.0 + elong * 0.35);
    const ry = p.height * 0.5 * (1.0 - flatRatio * 0.52) * (style === 'chip' ? 0.52 : 1.0);
    const rz = p.length * 0.5 * (1.0 - elong * 0.20);

    let roundness = p.roundness ?? 0.26;
    let facets = clamp(Math.round(p.facetCount ?? 9), 5, 18);
    let disp = 0.24;
    let taper = 0.10;

    if (style === 'pebble') { roundness = Math.max(roundness, 0.76); disp = 0.12; }
    else if (style === 'shard') { roundness = Math.min(roundness, 0.12); facets = Math.min(facets, 8); disp = 0.34; taper = 0.32; }
    else if (style === 'chip') { disp = 0.18; }

    const flatCut = clamp(p.flatBottom ?? 0.65, 0.0, 1.0);
    const cy = flatCut > 0.05 ? ry * (1.0 - 0.18 * flatCut) : ry;
    const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, 0.24 + rng.signed() * 0.15);
    const poly = sculptGeologicalParentPoly(
        [0, cy, 0],
        [rx, ry, rz],
        bf,
        rng,
        {
            jointDispersion: disp,
            cornerBlunt: 0.68 + 0.28 * roundness,
            flatBaseY: flatCut > 0.05 ? 0.0 : null,
            extraBevelCuts: Math.max(0, facets - 6)
        }
    );

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness,
        sharpBase: flatCut > 0.05,
        flatBaseCut: flatCut,
        sqWeight: style === 'pebble' ? clamp(roundness * 0.85, 0.35, 0.92) : (roundness > 0.55 ? (roundness - 0.55) * 1.6 : 0.0),
        sqRx: rx, sqRy: ry, sqRz: rz,
        sqCy: cy,
        sqExpH: 0.94, sqExpV: 0.90,
        baseY: 0.0,
        refH: Math.max(0.15, ry * 2.0),
        taper,
        skewX: p.asymmetry ?? 0.12
    });
}

function buildSingleCliffBlock(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    const h = p.height;
    const rz = p.length * 0.5;
    const profile = p.cliffProfile || 'vertical';
    const facets = clamp(Math.round(p.facetCount ?? 10), 6, 20);
    const roundness = p.roundness ?? 0.14;
    const prow = clamp(p.prowWedge ?? 0.15, 0.0, 0.65);

    let overhang = p.overhangLean ?? 0.08;
    let taper = p.batterTaper ?? 0.14;

    if (profile === 'overhang') { overhang = Math.max(overhang, 0.32); taper = Math.min(taper, -0.14); }
    else if (profile === 'rampart') { overhang = Math.min(overhang, -0.24); taper = Math.max(taper, 0.30); }
    else if (profile === 'prow') { overhang += 0.18; }

    const bf = buildStrikeDipFrame(rng.signed() * 0.25, 0.10 + prow * 0.32);
    let poly = sculptGeologicalParentPoly(
        [0, h * 0.5, 0],
        [rx, h * 0.53, rz],
        bf,
        rng,
        {
            jointDispersion: 0.11 + prow * 0.20,
            cornerBlunt: 0.62 + 0.26 * roundness,
            flatBaseY: 0.0,
            extraBevelCuts: Math.max(0, facets - 6)
        }
    );

    if (poly && (profile === 'mesa' || Math.abs(p.crestSlope ?? 0) > 0.02))
    {
        const capN = vnorm([-(p.crestSlope ?? 0.08) * 0.35, 1.0, rng.signed() * 0.04]);
        const next = clipPolyByHalfSpace(poly, capN, capN[1] * h * (profile === 'mesa' ? 0.90 : 0.96), 0);
        if (next) poly = next;
    }

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness,
        sharpBase: true,
        baseY: 0.0,
        refH: h,
        taper,
        bendZ: overhang
    });
}

function buildSingleBasaltColumn(p, worldR, worldPivot, rng)
{
    const fixedSides = Number(p.polygonSides ?? 6);
    const sides = fixedSides > 0 ? fixedSides : (5 + rng.int(0, 3));
    const rx = Math.max(0.10, p.width * 0.5);
    const rz = Math.max(0.10, p.length * 0.5);
    const h = p.height;
    const irreg = clamp(p.irregularity ?? 0.08, 0.0, 0.38);
    const bevelWeight = clamp(p.edgeBevel ?? 0.09, 0.01, 0.55);
    const capTilt = p.capTilt ?? 0.10;

    const bound = Math.max(rx, rz) * 1.5;
    let poly = createBoxPoly(-bound, 0.0, -bound, bound, h + bound, bound, 0);
    const rot0 = 0.26 + rng.signed() * 0.08;

    for (let s = 0; s < sides; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / sides + rng.signed() * irreg * 0.35;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const rad = Math.hypot(rx * dx, rz * dz) * (1.0 + rng.signed() * irreg * 0.45);
        const n = vnorm([dx, 0.0, dz]);
        poly = clipPolyByHalfSpace(poly, n, rad, 0);
    }

    const capN = vnorm([rng.signed() * capTilt, 1.0, rng.signed() * capTilt]);
    poly = clipPolyByHalfSpace(poly, capN, capN[1] * h, 0);

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness: bevelWeight * 0.55,
        sharpBase: true,
        baseY: 0.0,
        refH: h,
        taper: p.taper ?? 0.02,
        bendX: p.bendX ?? 0.0,
        twistDeg: p.twistDeg ?? 0.0
    });
}

function buildSingleSpirePinnacle(p, worldR, worldPivot, rng)
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
    let bendX = p.bendX ?? 0.10;

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

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
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
}

function buildSingleRockSlab(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const shape = p.planShape || 'plate';
    const roundness = p.roundness ?? 0.14;
    const dipDeg = p.dipAngleDeg ?? 16;

    let poly = null;
    if (shape === 'wedge')
    {
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

    const dipR = mulMat3(worldR, buildRock2EulerMatrix3(0, dipDeg, 0));
    const liftY = ry + Math.abs(Math.sin(dipDeg * DEG2RAD)) * rz * 0.45;
    const slabPivot = forwardRock2Point(worldR, worldPivot, 0, liftY, 0);

    return compilePlainPrimitive({
        poly,
        R: dipR,
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
}

function buildSingleCustomSculptRock(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    const ry = p.height * 0.5;
    const rz = p.length * 0.5;
    const sqWeight = clamp(p.analyticBlend ?? 0.50, 0.0, 1.0);
    const flatCut = clamp(p.flatBottom ?? 0.85, 0.0, 1.0);
    const cy = flatCut > 0.02 ? ry * (1.0 - 0.18 * flatCut) : ry;

    let poly = null;
    if (sqWeight < 0.99)
    {
        const facets = clamp(Math.round(p.facetCount ?? 14), 6, 26);
        const extraCuts = Math.max(0, facets - 6);
        const bf = buildStrikeDipFrame(rng.float() * Math.PI * 2, 0.22);
        poly = sculptGeologicalParentPoly(
            [0, cy, 0],
            [rx, ry, rz],
            bf,
            rng,
            {
                jointDispersion: p.angularity ?? 0.18,
                cornerBlunt: 0.65 + 0.30 * (p.roundness ?? 0.32),
                flatBaseY: flatCut > 0.05 ? 0.0 : null,
                extraBevelCuts: extraCuts
            }
        );
    }

    return compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
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
}

function buildAtomicBaseRockPrimitive(type, p, worldR, worldPivot)
{
    const rng = createRng(p.seed ?? 2104);
    switch (type)
    {
        case 'smooth_rock':        return buildSingleSmoothRock(p, worldR, worldPivot, rng);
        case 'small_rock':         return buildSingleSmallRock(p, worldR, worldPivot, rng);
        case 'cliff_block':        return buildSingleCliffBlock(p, worldR, worldPivot, rng);
        case 'basalt_column':      return buildSingleBasaltColumn(p, worldR, worldPivot, rng);
        case 'spire_pinnacle':     return buildSingleSpirePinnacle(p, worldR, worldPivot, rng);
        case 'rock_slab':          return buildSingleRockSlab(p, worldR, worldPivot, rng);
        case 'custom_sculpt_rock': return buildSingleCustomSculptRock(p, worldR, worldPivot, rng);
        case 'big_boulder':
        default:                   return buildSingleBigBoulder(p, worldR, worldPivot, rng);
    }
}

/*==========================================================================================================================================
                               7. LAYER-STACK COMPILER & EXACT CPU PURE-SDF EVALUATOR
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

    const MAX_STACK_PRIMITIVES = 160;

    for (let li = 0; li < layers.length; li++)
    {
        const lyr = layers[li];
        if (!lyr.enabled) continue;
        if (hasSolo && !lyr.solo) continue;

        const spec = getRock2NodeSpec(lyr.type);
        const p = { ...spec.defaults, ...(lyr.params || {}), _sourceType: spec.type };
        const layerR = buildRock2EulerMatrix3(p.yawDeg ?? 0, p.pitchDeg ?? 0, p.rollDeg ?? 0);
        const layerPivot = [p.posX ?? 0.0, p.posY ?? 0.0, p.posZ ?? 0.0];

        const { intraBlend, instances } = evaluateRock2GeneratorInstances(lyr.generator, p, spec.type);
        const room = Math.max(0, MAX_STACK_PRIMITIVES - allPrimitives.length);
        if (room <= 0) break;

        const layerPrims = [];
        for (let k = 0; k < instances.length && layerPrims.length < room; k++)
        {
            const inst = instances[k];
            const localInstR = buildRock2EulerMatrix3(inst.yawDeg, inst.pitchDeg, inst.rollDeg);
            const worldR = mulMat3(layerR, localInstR);
            const worldPivot = forwardRock2Point(layerR, layerPivot, inst.pos[0], inst.pos[1], inst.pos[2]);

            const instRockType = inst.rockType || spec.type;
            const instParams = buildMutatedInstanceParams(instRockType, p, inst);

            const pr = buildAtomicBaseRockPrimitive(instRockType, instParams, worldR, worldPivot);
            if (pr) layerPrims.push(pr);
        }

        if (layerPrims.length === 0) continue;

        const blockStart = allPrimitives.length;
        for (let k = 0; k < layerPrims.length; k++)
        {
            const pr = layerPrims[k];
            allPrimitives.push(pr);
            totalPlanes += pr.numPlanes;

            if (lyr.combineMode !== 'subtract' || compiledLayers.length === 0)
            {
                if (pr.minX < minX) minX = pr.minX;
                if (pr.maxX > maxX) maxX = pr.maxX;
                if (pr.minY < minY) minY = pr.minY;
                if (pr.maxY > maxY) maxY = pr.maxY;
                if (pr.minZ < minZ) minZ = pr.minZ;
                if (pr.maxZ > maxZ) maxZ = pr.maxZ;
            }
        }

        const tone = ROCK2_STONE_TONES[p.stoneTone] || ROCK2_STONE_TONES.clay;
        compiledLayers.push({
            id: lyr.id,
            name: lyr.name,
            type: spec.type,
            generatorType: lyr.generator?.type || 'none',
            instanceCount: layerPrims.length,
            combineOp: ROCK2_COMBINE_OP_CODE[lyr.combineMode] ?? 0,
            blendRadius: clamp(lyr.blendRadius ?? 0.35, 0.0, 4.0),
            intraBlend: clamp(intraBlend ?? 0.0, 0.0, 3.0),
            color: tone.rgb,
            blockStart,
            blockCount: layerPrims.length
        });
    }

    if (!Number.isFinite(minX))
    {
        minX = -3.0; maxX = 3.0;
        minY = 0.0;  maxY = 4.0;
        minZ = -3.0; maxZ = 3.0;
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
        const invK = 1.0 / Math.max(1e-4, prim.kLse);
        let dFloor = -1e9;
        let bestPd = -1e9;
        let sumExp = 0.0;

        for (let j = 0; j < prim.numPlanes; j++)
        {
            const off = j * 4;
            const nx = planes[off], ny = planes[off + 1], nz = planes[off + 2], d = planes[off + 3];
            const pd = nx * qx + ny * qy + nz * qz - d;

            if (prim.sharpBase > 0.5 && ny <= -0.92)
            {
                if (pd > dFloor) dFloor = pd;
            }
            else if (pd > bestPd)
            {
                sumExp = bestPd > -1e8 ? sumExp * Math.exp(Math.max(-24.0, (bestPd - pd) * invK)) + 1.0 : 1.0;
                bestPd = pd;
            }
            else
            {
                sumExp += Math.exp(Math.max(-24.0, (pd - bestPd) * invK));
            }
        }

        dPoly = bestPd > -1e8 ? (bestPd + prim.kLse * Math.log(Math.max(1.0, sumExp))) : dFloor;
        if (dFloor > -1e8 && bestPd > -1e8)
        {
            dPoly = smaxPoly(dPoly, dFloor, prim.baseBevel);
        }
    }

    let dSq = 1e9;
    if (prim.sqWeight > 0.001 || prim.numPlanes === 0)
    {
        const p1 = 2.0 / Math.max(0.22, prim.sqExpH);
        const p2 = 2.0 / Math.max(0.22, prim.sqExpV);
        const m12 = p2 / p1;

        const ux = Math.max(1e-5, Math.abs(qx) / Math.max(0.04, prim.sqRx) + 1e-5);
        const uy = Math.max(1e-5, Math.abs(qy - prim.sqCy) / Math.max(0.04, prim.sqRy) + 1e-5);
        const uz = Math.max(1e-5, Math.abs(qz) / Math.max(0.04, prim.sqRz) + 1.0e-5);

        const A = Math.max(1e-6, Math.pow(ux, p1) + Math.pow(uz, p1));
        const B = Math.max(1e-6, Math.pow(A, m12) + Math.pow(uy, p2));

        if (B < 1e-5)
        {
            dSq = -Math.min(prim.sqRx, prim.sqRy, prim.sqRz) * 0.85;
        }
        else
        {
            const F = Math.pow(B, 1.0 / p2) - 1.0;
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
            dSq = smaxPoly(dSq, -qy, prim.baseBevel);
        }
    }

    const dLocal = prim.numPlanes === 0 ? dSq : (prim.sqWeight <= 0.001 ? dPoly : lerp(dPoly, dSq, prim.sqWeight));
    return dLocal * scaleFactor;
}

export function evalRock2StackSdf(compiledStack, wx, wy, wz, outColor = null)
{
    const { compiledLayers, primitives } = compiledStack;
    if (compiledLayers.length === 0)
    {
        if (outColor) { outColor.r = 0.58; outColor.g = 0.57; outColor.b = 0.55; }
        return 10.0;
    }

    let dAcc = 1e9;
    let cr = 0.58, cg = 0.57, cb = 0.55;
    let hasBase = false;

    for (let li = 0; li < compiledLayers.length; li++)
    {
        const lyr = compiledLayers[li];
        let dLyr = 1e9;
        const ib = lyr.intraBlend;

        for (let k = 0; k < lyr.blockCount; k++)
        {
            const prim = primitives[lyr.blockStart + k];
            const lb = Math.hypot(wx - prim.cx, wy - prim.cy, wz - prim.cz) - prim.radius;
            if (lb > dLyr + ib) continue;
            const dP = evalRock2PrimitiveSdf(prim, wx, wy, wz);
            dLyr = dLyr > 1e8 ? dP : (ib > 1e-4 ? sminPoly(dLyr, dP, ib) : Math.min(dLyr, dP));
        }

        const lr = lyr.color[0], lg = lyr.color[1], lbCol = lyr.color[2];
        const op = lyr.combineOp;
        const br = lyr.blendRadius;

        if (!hasBase)
        {
            if (op === 3) continue;
            dAcc = dLyr;
            cr = lr; cg = lg; cb = lbCol;
            hasBase = true;
            continue;
        }

        if (op === 0)
        {
            if (dLyr < dAcc) { dAcc = dLyr; cr = lr; cg = lg; cb = lbCol; }
        }
        else if (op === 1)
        {
            const w = br > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / br, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = sminPoly(dAcc, dLyr, br);
            cr = lerp(cr, lr, w); cg = lerp(cg, lg, w); cb = lerp(cb, lbCol, w);
        }
        else if (op === 2)
        {
            const w = br > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / br, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
            dAcc = chamferMin(dAcc, dLyr, br);
            cr = lerp(cr, lr, w); cg = lerp(cg, lg, w); cb = lerp(cb, lbCol, w);
        }
        else if (op === 3)
        {
            const prev = dAcc;
            dAcc = smaxPoly(dAcc, -dLyr, br);
            const cutW = smoothstep(0.0, Math.max(0.02, br + 0.04), dAcc - prev);
            cr = lerp(cr, lr, cutW * 0.65); cg = lerp(cg, lg, cutW * 0.65); cb = lerp(cb, lbCol, cutW * 0.65);
        }
        else if (op === 4)
        {
            const w = dLyr > dAcc ? 1.0 : 0.0;
            dAcc = smaxPoly(dAcc, dLyr, br);
            cr = lerp(cr, lr, w * 0.5); cg = lerp(cg, lg, w * 0.5); cb = lerp(cb, lbCol, w * 0.5);
        }
    }

    if (outColor) { outColor.r = cr; outColor.g = cg; outColor.b = cb; }
    return dAcc;
}

/*==========================================================================================================================================
                               8. GPU TEXTURE PACKER & QEF DUAL-CONTOUR MESH BAKER
==========================================================================================================================================*/

export function packRock2StackGpuBuffers(compiledStack)
{
    const { compiledLayers, primitives } = compiledStack;
    const numLayers = Math.max(1, compiledLayers.length);
    const numBlocks = Math.max(1, primitives.length);
    const totalPlanes = Math.max(1, compiledStack.totalPlanes);

    const layerTexData = new Float32Array(numLayers * 2 * 4);
    const blockTexData = new Float32Array(numBlocks * 10 * 4);
    const planeTexData = new Float32Array(totalPlanes * 4);

    for (let li = 0; li < compiledLayers.length; li++)
    {
        const lyr = compiledLayers[li];
        const off = li * 8;
        layerTexData[off + 0] = lyr.blockStart;
        layerTexData[off + 1] = lyr.blockCount;
        layerTexData[off + 2] = lyr.combineOp;
        layerTexData[off + 3] = lyr.blendRadius;
        layerTexData[off + 4] = lyr.color[0];
        layerTexData[off + 5] = lyr.color[1];
        layerTexData[off + 6] = lyr.color[2];
        layerTexData[off + 7] = lyr.intraBlend;
    }

    let planeCursor = 0;
    for (let i = 0; i < primitives.length; i++)
    {
        const b = primitives[i];
        const off = i * 40;

        blockTexData[off + 0] = b.cx;
        blockTexData[off + 1] = b.cy;
        blockTexData[off + 2] = b.cz;
        blockTexData[off + 3] = b.radius;

        blockTexData[off + 4] = planeCursor;
        blockTexData[off + 5] = b.numPlanes;
        blockTexData[off + 6] = b.kLse;
        blockTexData[off + 7] = b.baseBevel;

        blockTexData[off + 8]  = b.invR[0]; blockTexData[off + 9]  = b.invR[1]; blockTexData[off + 10] = b.invR[2]; blockTexData[off + 11] = b.invT[0];
        blockTexData[off + 12] = b.invR[3]; blockTexData[off + 13] = b.invR[4]; blockTexData[off + 14] = b.invR[5]; blockTexData[off + 15] = b.invT[1];
        blockTexData[off + 16] = b.invR[6]; blockTexData[off + 17] = b.invR[7]; blockTexData[off + 18] = b.invR[8]; blockTexData[off + 19] = b.invT[2];

        blockTexData[off + 20] = b.sqRx;
        blockTexData[off + 21] = b.sqRy;
        blockTexData[off + 22] = b.sqRz;
        blockTexData[off + 23] = b.sqCy;

        blockTexData[off + 24] = b.sqWeight;
        blockTexData[off + 25] = b.sqExpH;
        blockTexData[off + 26] = b.sqExpV;
        blockTexData[off + 27] = b.primOp;

        blockTexData[off + 28] = b.baseY;
        blockTexData[off + 29] = b.refH;
        blockTexData[off + 30] = b.taper;
        blockTexData[off + 31] = b.pinch;

        blockTexData[off + 32] = b.twistRad;
        blockTexData[off + 33] = b.bendX;
        blockTexData[off + 34] = b.bendZ;
        blockTexData[off + 35] = b.skewX;

        blockTexData[off + 36] = b.sharpBase;
        blockTexData[off + 37] = b.flatBaseCut;
        blockTexData[off + 38] = b.hasDeform ? 1.0 : 0.0;
        blockTexData[off + 39] = b.deformPenalty;

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

    return {
        numLayers: compiledLayers.length,
        numBlocks: primitives.length,
        totalPlanes: compiledStack.totalPlanes,
        layerTexWidth: 2,
        layerTexHeight: numLayers,
        layerTexData,
        blockTexWidth: 10,
        blockTexHeight: numBlocks,
        blockTexData,
        planeTexWidth: 1,
        planeTexHeight: totalPlanes,
        planeTexData
    };
}

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
