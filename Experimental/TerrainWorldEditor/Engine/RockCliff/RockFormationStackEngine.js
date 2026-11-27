/*==========================================================================================================================================
                                                ROCKFORMATIONSTACKENGINE.JS
==========================================================================================================================================*/
// Pure-SDF Composable Layer-Stack Engine for RockFormation 2.0:
//   - 11 Atomic Plain Base Rock Shapes (`big_boulder`, `smooth_rock`, `small_rock`, `cliff_block`, `basalt_column`,
//     `spire_pinnacle`, `hoodoo_column`, `pedestal_rock`, `floating_rock`, `rock_slab`, `custom_sculpt_rock`) —
//     ZERO hardcoded multi-rock scatters, ZERO fractures, ZERO thermal crazing, ZERO chemical/noise surface processing.
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
    ['Boolean Subtract (Carve A − B)', 'subtract'],
    ['Boolean Chamfer Subtract (45° Bevel Cut)', 'chamfer_subtract'],
    ['Boolean Intersect (Clip A ∩ B)', 'intersect']
];

export const ROCK2_BOOLEAN_MODES = [
    ['Subtract (Carve Cavity A − B)', 'subtract'],
    ['Chamfer Subtract (45° Bevel Cut)', 'chamfer_subtract'],
    ['Intersect (Clip Volume A ∩ B)', 'intersect']
];

export const ROCK2_COMBINE_OP_CODE = {
    union: 0,
    smooth_union: 1,
    chamfer_union: 2,
    subtract: 3,
    intersect: 4,
    chamfer_subtract: 5
};

export const ROCK2_STONE_TONES = {
    clay:       { label: 'Studio Stone Grey',   hex: '#94918c', rgb: [0.58, 0.57, 0.55] },
    basalt:     { label: 'Dark Basalt Slate',   hex: '#454a52', rgb: [0.27, 0.29, 0.32] },
    granite:    { label: 'Pale Sierra Granite', hex: '#aea69c', rgb: [0.68, 0.65, 0.61] },
    sandstone:  { label: 'Warm Desert Buff',    hex: '#b88f6b', rgb: [0.72, 0.56, 0.42] },
    bryceCoral:   { label: 'Bryce Claron Coral',     hex: '#bd7257', rgb: [0.74, 0.45, 0.34] },
    cobble:       { label: 'River Cobble Blue',      hex: '#757d85', rgb: [0.46, 0.49, 0.52] },
    limestone:    { label: 'Karst Cream Stone',      hex: '#bab5a6', rgb: [0.73, 0.71, 0.65] },
    redSlate:     { label: 'Red Canyon Slate',       hex: '#9e5e4d', rgb: [0.62, 0.37, 0.30] },
    amethyst:     { label: 'Amethyst Violet Crystal',hex: '#8b5cf6', rgb: [0.55, 0.36, 0.88] },
    quartzIce:    { label: 'Glacial Clear Quartz',   hex: '#c7e2f0', rgb: [0.78, 0.88, 0.94] },
    emeraldBeryl: { label: 'Emerald Beryl Crystal',  hex: '#3ca878', rgb: [0.24, 0.66, 0.47] },
    citrine:      { label: 'Citrine Amber Crystal',  hex: '#d99b38', rgb: [0.85, 0.61, 0.22] },
    obsidian:     { label: 'Volcanic Obsidian Glass',hex: '#2f333b', rgb: [0.19, 0.20, 0.23] }
};

export const ROCK2_STONE_TONE_OPTIONS = Object.entries(ROCK2_STONE_TONES).map(([k, v]) => [v.label, k]);

/*==========================================================================================================================================
                     2. 11 ATOMIC PLAIN BASE ROCK GENERATORS (SINGLE ROCK PER BASE NODE · 100% PURE SDF)
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
        desc: 'Single plain cliff buttress, yardang prow or half-dome — attach 3D Spline to sculpt curved cliffs & canyons',
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
            basalUndercut: 0.0,
            batterTaper: 0.14,
            crestSlope: 0.08,
            prowWedge: 0.15
        },
        controlGroups: [
            {
                title: 'Cliff Buttress, Yardang & Half-Dome',
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
                            ['Angular Promontory Prow', 'prow'],
                            ['Saharan Wind-Carved Yardang Fin', 'yardang_fin'],
                            ['Granite Half-Dome & Sheer Face', 'exfoliation_dome']
                        ]
                    },
                    { key: 'facetCount', label: 'Face Joint Planes', min: 6, max: 20, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Cliff Arris Rounding', min: 0.02, max: 0.65, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'overhangLean', label: 'Slope Back ↔ Overhang Brow', min: -0.55, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'basalUndercut', label: 'Basal Wave / Wind Undercut', min: 0.0, max: 0.60, step: 0.02, decimals: 2, unit: 'w' },
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
        desc: 'Polygonal 4/5/6/7/8-sided basalt prism with ball-and-socket caps, cooling bands, stepped cross-joints & colonnade curvature',
        defaults: {
            stoneTone: 'basalt',
            seed: 5512,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 1.15, height: 4.6, length: 1.15,
            basaltStyle: 'classic_hex',
            polygonSides: 6,
            irregularity: 0.08,
            edgeBevel: 0.09,
            capTilt: 0.10,
            socketDish: 0.0,
            pinchBands: 0.0,
            taper: 0.02,
            bendX: 0.0,
            twistDeg: 0
        },
        controlGroups: [
            {
                title: 'Polygonal Basalt Prism & Jointing',
                badge: 'BASE ROCK',
                glyph: 'Boxes',
                hint: 'Pair with Array → Hexagonal Pack or Linear Colonnade Wall (A→B) to build Giant’s Causeway & Fingal’s Cave cliffs.',
                controls: [
                    {
                        type: 'select', key: 'basaltStyle', label: 'Basalt Column Style',
                        options: [
                            ['Classic Causeway Hex Prism', 'classic_hex'],
                            ['Ball-and-Socket Cupped Column', 'ball_socket'],
                            ['Curved Entablature Fan Prism', 'entablature_splay'],
                            ['Cross-Jointed Stepped Column', 'stepped_fracture']
                        ]
                    },
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
                    { key: 'socketDish', label: 'Cup Socket (−) / Dome (+)', min: -0.45, max: 0.45, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'pinchBands', label: 'Cooling Chisel Bands', min: 0.0, max: 0.32, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Shaft Taper', min: -0.30, max: 0.45, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'bendX', label: 'Column Curvature', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
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
        desc: 'Single plain alpine horn, sea-stack needle, Tsingy karst razor blade or cathedral organ-pipe spire',
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
            buttressRibs: 0,
            fluteDepth: 0.0,
            waistPinch: 0.0,
            twistDeg: 14,
            bendX: 0.10,
            flatCap: 0.08
        },
        controlGroups: [
            {
                title: 'Spire, Karst Blade & Cathedral Organ',
                badge: 'BASE ROCK',
                glyph: 'Activity',
                hint: 'Single plain vertical rock spire — control gothic taper, bundled organ-pipe ribs, karst fluting & lean.',
                controls: [
                    {
                        type: 'select', key: 'spireStyle', label: 'Spire Archetype',
                        options: [
                            ['Alpine Horn / Obelisk', 'horn'],
                            ['Slender Sea-Stack Needle', 'needle'],
                            ['Tsingy Limestone Karst Razor', 'karst_razor'],
                            ['Cathedral Organ-Pipe Spire', 'cathedral_organ'],
                            ['Pinched Desert Spire', 'hoodoo'],
                            ['Flat-Topped Butte Tower', 'butte']
                        ]
                    },
                    { key: 'facetSides', label: 'Perimeter Facet Sides', min: 4, max: 14, step: 1, decimals: 0, unit: 'pl' },
                    { key: 'roundness', label: 'Ridge Rounding', min: 0.02, max: 0.80, step: 0.01, decimals: 2, unit: 'w' },
                    { key: 'taper', label: 'Vertical Spire Taper', min: -0.35, max: 0.88, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'buttressRibs', label: 'Organ-Pipe Buttress Ribs', min: 0, max: 5, step: 1, decimals: 0, unit: 'rb' },
                    { key: 'fluteDepth', label: 'Vertical Karst Fluting', min: 0.0, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'waistPinch', label: 'Mid-Shaft Pinch', min: -0.40, max: 0.68, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'twistDeg', label: 'Helical Spire Twist', min: -90, max: 90, step: 2, decimals: 0, unit: '°' },
                    { key: 'bendX', label: 'Cantilever Spire Lean', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'flatCap', label: 'Summit Plateau Cut', min: 0.0, max: 0.80, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'hoodoo_column',
        name: 'Hoodoo Column',
        shortBadge: 'HOODOO',
        glyph: 'Hoodoo',
        accent: '#f08a6c',
        desc: 'Single plain Bryce badlands hoodoo pillar with continuous pinch-and-swell rings & sculpted crown cap',
        defaults: {
            stoneTone: 'bryceCoral',
            seed: 6408,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 2.2, height: 6.4, length: 2.1,
            hoodooStyle: 'bulb_cap',
            ringCount: 5,
            ringSwell: 0.22,
            neckPinch: 0.36,
            capSize: 1.12,
            taper: 0.24,
            basalApron: 0.34,
            leanX: 0.06,
            roundness: 0.30
        },
        controlGroups: [
            {
                title: 'Badlands Hoodoo Rings & Crown',
                badge: 'BASE ROCK',
                glyph: 'Hoodoo',
                hint: 'Continuous fluted badlands shaft with analytical pinch-and-swell rings and pitched hoodoo crown — attach Spline or Array for Bryce amphitheaters.',
                controls: [
                    {
                        type: 'select', key: 'hoodooStyle', label: 'Hoodoo Archetype',
                        options: [
                            ['Bulb-Cap Bryce Hoodoo', 'bulb_cap'],
                            ['Castellated Turret Hoodoo', 'turret'],
                            ['Stacked Totem Pillar', 'totem'],
                            ['Narrow Badlands Fin Hoodoo', 'fin_spire']
                        ]
                    },
                    { key: 'ringCount', label: 'Pinch-and-Swell Rings', min: 2, max: 9, step: 1, decimals: 0, unit: 'rg' },
                    { key: 'ringSwell', label: 'Ring Bulge & Constriction', min: 0.0, max: 0.44, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'neckPinch', label: 'Sub-Crown Throat Pinch', min: 0.0, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'capSize', label: 'Crown Cap Overhang', min: 0.55, max: 1.60, step: 0.02, decimals: 2, unit: '×' },
                    { key: 'taper', label: 'Column Base→Crown Taper', min: -0.25, max: 0.75, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'basalApron', label: 'Grounded Pedestal Apron', min: 0.0, max: 0.80, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'leanX', label: 'Column Lean', min: -0.40, max: 0.40, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Facet↔Ring Rounding', min: 0.04, max: 0.80, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'pedestal_rock',
        name: 'Pedestal / Balanced Rock',
        shortBadge: 'PEDESTAL',
        glyph: 'Pedestal',
        accent: '#e6b472',
        desc: 'Single plain perched balanced boulder, desert mushroom rock, caprock toadstool or cantilevered anvil',
        defaults: {
            stoneTone: 'sandstone',
            seed: 4729,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 3.4, height: 5.2, length: 3.2,
            pedestalStyle: 'balanced_boulder',
            stemHeightRatio: 0.48,
            neckWidth: 0.36,
            capOverhang: 1.22,
            capTiltDeg: 14,
            capOffset: 0.08,
            stemRibs: 0.14,
            plinthFlare: 0.38,
            roundness: 0.26
        },
        controlGroups: [
            {
                title: 'Pedestal Stem & Perched Caprock',
                badge: 'BASE ROCK',
                glyph: 'Pedestal',
                hint: 'Stepped plinth + concave-tapered pedestal throat + tilted overhanging cap boulder in 100% Pure SDF.',
                controls: [
                    {
                        type: 'select', key: 'pedestalStyle', label: 'Pedestal Archetype',
                        options: [
                            ['Perched Balanced Boulder', 'balanced_boulder'],
                            ['Desert Mushroom / Toadstool', 'desert_mushroom'],
                            ['Tabular Caprock Table', 'caprock_table'],
                            ['Cantilevered Anvil Prow', 'anvil_prow']
                        ]
                    },
                    { key: 'stemHeightRatio', label: 'Pedestal Stem Height Ratio', min: 0.25, max: 0.74, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'neckWidth', label: 'Throat Neck Width', min: 0.18, max: 0.80, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'capOverhang', label: 'Caprock Overhang Scale', min: 0.70, max: 1.75, step: 0.02, decimals: 2, unit: '×' },
                    { key: 'capTiltDeg', label: 'Perched Cap Tilt', min: -36, max: 36, step: 1, decimals: 0, unit: '°' },
                    { key: 'capOffset', label: 'Eccentric Cap Balance Shift', min: -0.45, max: 0.45, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'stemRibs', label: 'Mudstone Stem Ribs', min: 0.0, max: 0.38, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'plinthFlare', label: 'Basal Plinth Flare', min: 0.0, max: 0.85, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Cap & Throat Rounding', min: 0.04, max: 0.85, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'floating_rock',
        name: 'Floating Rock / Sky Island',
        shortBadge: 'FLOATING',
        glyph: 'FloatingIsland',
        accent: '#7cd4f8',
        desc: 'Single plain mid-air floating rock mass with inverted stalactite root keel, spurs & plateau/crag top',
        defaults: {
            stoneTone: 'granite',
            seed: 9137,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 4.8, height: 4.6, length: 4.4,
            floatingStyle: 'sky_island',
            hoverHeight: 2.4,
            keelRatio: 0.62,
            keelSharpness: 0.74,
            topFlatness: 0.82,
            rimOverhang: 0.32,
            keelSpurs: 3,
            keelSkew: 0.10,
            roundness: 0.18
        },
        controlGroups: [
            {
                title: 'Floating Sky Island & Hanging Root Keel',
                badge: 'BASE ROCK',
                glyph: 'FloatingIsland',
                hint: 'True elevated Pure-SDF floating rock (zero ground clipping at Y=0) — pair with 3D Array or Scatter for floating archipelagos.',
                controls: [
                    {
                        type: 'select', key: 'floatingStyle', label: 'Floating Archetype',
                        options: [
                            ['Flat-Topped Sky Island', 'sky_island'],
                            ['Torn Floating Monolith Crag', 'torn_monolith'],
                            ['Stalactite Dagger Spire', 'stalactite_dagger'],
                            ['Cantilevered Hover Ledge', 'hover_ledge']
                        ]
                    },
                    { key: 'hoverHeight', label: 'Hover Clearance (Air Gap)', min: 0.0, max: 12.0, step: 0.1, decimals: 1, unit: 'm' },
                    { key: 'keelRatio', label: 'Hanging Root Keel Depth', min: 0.25, max: 0.82, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'keelSharpness', label: 'Blunt Root ↔ Razor Keel Tip', min: 0.10, max: 0.95, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'topFlatness', label: 'Crag Peak ↔ Plateau Deck', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'rimOverhang', label: 'Equatorial Rim Shelf Overhang', min: 0.0, max: 0.75, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'keelSpurs', label: 'Secondary Hanging Root Spurs', min: 0, max: 4, step: 1, decimals: 0, unit: '#' },
                    { key: 'keelSkew', label: 'Wind-Sheared Root Slant', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Bedrock Facet Rounding', min: 0.04, max: 0.72, step: 0.02, decimals: 2, unit: 'w' }
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
    },
    {
        type: 'crystal_prism',
        name: 'Crystal & Gem Prism',
        shortBadge: 'CRYSTAL',
        glyph: 'Crystal',
        accent: '#c084fc',
        desc: 'Razor-sharp Pure-SDF mineral crystal — Hexagonal Quartz, Double-Terminated Herkimer, Selenite Sword, Fluorite Octahedron & Tourmaline',
        defaults: {
            stoneTone: 'amethyst',
            seed: 6410,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 1.35, height: 5.2, length: 1.35,
            crystalHabit: 'quartz_hex',
            prismSides: 6,
            terminationStyle: 'quartz_rz',
            apexHeightRatio: 0.34,
            bottomPoint: 0.0,
            faceAsymmetry: 0.18,
            edgeBevel: 0.02,
            shaftTaper: 0.06,
            leanX: 0.0,
            satelliteTwin: 0.0
        },
        controlGroups: [
            {
                title: 'Crystallographic Habit & Miller Terminations',
                badge: 'CRYSTAL SDF',
                glyph: 'Crystal',
                hint: 'Exact half-space crystal faces — pair with Array → 3D Radiating Crystal Spray for geode clusters & quartz bursts.',
                controls: [
                    {
                        type: 'select', key: 'crystalHabit', label: 'Crystal Mineral Habit',
                        options: [
                            ['Hexagonal Quartz Point (α-Quartz)', 'quartz_hex'],
                            ['Double-Terminated Herkimer Crystal', 'double_terminated'],
                            ['Monoclinic Selenite / Gypsum Sword', 'chisel_selenite'],
                            ['Octahedral Fluorite / Diamond Gem', 'fluorite_octa'],
                            ['Trigonal Tourmaline / Beryl Prism', 'tourmaline_trigonal'],
                            ['Skewed Obelisk Razor Shard', 'obelisk_shard']
                        ]
                    },
                    { key: 'prismSides', label: 'Prism Miller Faces', min: 3, max: 12, step: 1, decimals: 0, unit: 'pl' },
                    {
                        type: 'select', key: 'terminationStyle', label: 'Apex Termination Geometry',
                        options: [
                            ['Quartz Alternating r/z Rhombohedra', 'quartz_rz'],
                            ['Symmetric Equant Pyramid Point', 'pyramid'],
                            ['2-Face Chisel / Selenite Blade', 'chisel_wedge'],
                            ['Single-Slant Oblique Cleavage', 'oblique_single'],
                            ['Flat Pedion Table + Bevel Crown', 'pedion_bevel']
                        ]
                    },
                    { key: 'apexHeightRatio', label: 'Apex Pyramid Ratio', min: 0.08, max: 0.80, step: 0.02, decimals: 2, unit: '×H' },
                    { key: 'bottomPoint', label: 'Bottom Point (Double-Term)', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'faceAsymmetry', label: 'Crystal Face Asymmetry', min: 0.0, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'edgeBevel', label: 'Crystal Edge Sharpness', min: 0.005, max: 0.30, step: 0.005, decimals: 3, unit: 'w' },
                    { key: 'shaftTaper', label: 'Prism Shaft Taper', min: -0.25, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'leanX', label: 'Crystal Cant / Lean', min: -0.50, max: 0.50, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'satelliteTwin', label: 'Penetration Twin Spur', min: 0.0, max: 1.0, step: 0.02, decimals: 2, unit: 'w' }
                ]
            }
        ]
    },
    {
        type: 'linear_dike',
        name: 'Linear Dike, Hogback & Fin',
        shortBadge: 'LINEAR',
        glyph: 'LinearDike',
        accent: '#f59e0b',
        desc: 'Elongated tabular geological rock wall — Volcanic Igneous Dike, Tilted Hogback/Flatiron, Moab Sandstone Fin & Knife-Edge Arête',
        defaults: {
            stoneTone: 'redSlate',
            seed: 7820,
            posX: 0.0, posY: 0.0, posZ: 0.0,
            yawDeg: 0, pitchDeg: 0, rollDeg: 0,
            width: 8.6, height: 4.4, length: 1.55,
            linearStyle: 'volcanic_dike',
            dipTiltDeg: 0,
            crestSharpness: 0.62,
            spineSegments: 3,
            crestUndulation: 0.28,
            wallTaper: 0.26,
            strikeBow: 0.08,
            basalUndercut: 0.0,
            roundness: 0.14
        },
        controlGroups: [
            {
                title: 'Linear Dike, Hogback & Parallel Fin Profile',
                badge: 'LINEAR SDF',
                glyph: 'LinearDike',
                hint: 'Elongated strike-aligned formation — pair with Array → Parallel Linear Fins / Strike Ribs for Moab fin fields & hogbacks.',
                controls: [
                    {
                        type: 'select', key: 'linearStyle', label: 'Linear Formation Type',
                        options: [
                            ['Volcanic Igneous Dike Wall (Shiprock)', 'volcanic_dike'],
                            ['Tilted Hogback / Flatiron Cuesta', 'hogback_flatiron'],
                            ['Moab Parallel-Joint Sandstone Fin', 'sandstone_fin'],
                            ['Razor Knife-Edge Arête Spine', 'knife_arete'],
                            ['Crenellated Jointed Battlement Wall', 'crenellated_wall'],
                            ['Low Protruding Bedrock Strike Rib', 'strike_rib']
                        ]
                    },
                    { key: 'dipTiltDeg', label: 'Strata Dip / Flatiron Lean', min: -55, max: 55, step: 1, decimals: 0, unit: '°' },
                    { key: 'crestSharpness', label: 'Crest Ridge Sharpness', min: 0.05, max: 0.95, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'spineSegments', label: 'Fused Strike Wall Blocks', min: 1, max: 5, step: 1, decimals: 0, unit: 'seg' },
                    { key: 'crestUndulation', label: 'Crest Spine Serration', min: 0.0, max: 0.65, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'wallTaper', label: 'Wall Thickness Taper', min: -0.35, max: 0.78, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'strikeBow', label: 'Strike Curvature / Bow', min: -0.55, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'basalUndercut', label: 'Basal Alcove Undercut', min: 0.0, max: 0.55, step: 0.02, decimals: 2, unit: 'w' },
                    { key: 'roundness', label: 'Edge Weathering / Polish', min: 0.02, max: 0.75, step: 0.01, decimals: 2, unit: 'w' }
                ]
            }
        ]
    }
];

export const DEFAULT_ROCK2_SPLIT_FAULT_PARAMS = {
    targetScope: 'stack_below', // 'stack_below' | 'single_layer'
    targetLayerId: '',
    splitMode: 'single_plane',  // 'single_plane' | 'parallel_family' | 'conjugate_pair' | 'radial_wedge'
    stoneTone: 'inherit',       // 'inherit' or key in ROCK2_STONE_TONES
    // Orientation & Origin
    originX: 0.0,
    originY: 1.8,
    originZ: 0.0,
    strikeYawDeg: 24.0,
    dipPitchDeg: 68.0,
    conjugateAngleDeg: 62.0,
    // Family Distribution (Parallel / Conjugate / Radial Wedge)
    planeCount: 3,
    spacing: 1.35,
    spacingJitter: 0.18,
    dipJitterDeg: 2.5,
    // Fracture Surface Roughness (Jigsaw Profile)
    macroWavinessAmp: 0.18,
    macroWavelength: 2.40,
    microJaggednessAmp: 0.045,
    microFrequency: 3.20,
    octaves: 3,
    anisotropyStretch: 1.45,
    // Aperture & Edge Profile
    crackGapWidth: 0.06,
    lipChamfer: 0.045,
    vTaperDepth: 0.0,
    tipSharpness: 1.25,
    // Kinematic Displacement (T^-1(p) Rigid Slip)
    normalHeave: 0.14,
    strikeSlip: 0.32,
    dipSlip: 0.22,
    hingeTiltDeg: 5.0,
    gravitySettle: 0.08,
    seed: 1409
};

export const ROCK2_SCULPT_BRUSH_MODES = [
    ['Clay Buildup / Add (+)', 'add'],
    ['Carve / Chisel Gouge (−)', 'carve'],
    ['Flatten / Planar Facet', 'flatten'],
    ['Smooth / Polish', 'smooth'],
    ['Inflate / Bulge', 'inflate'],
    ['Crease / Fissure Groove', 'crease'],
    ['Erase SDF / Delete Floating', 'erase']
];

export const ROCK2_SCULPT_OP_CODE = {
    add: 0,
    carve: 1,
    flatten: 2,
    smooth: 3,
    inflate: 4,
    crease: 5,
    erase: 6,
    deflate: 7,
    ridge: 8
};

export const DEFAULT_ROCK2_SCULPT_LAYER_PARAMS = {
    targetScope: 'stack_below', // 'stack_below' | 'single_layer'
    targetLayerId: '',
    brushOp: 'add',             // 'add' | 'carve' | 'erase' | 'flatten' | 'smooth' | 'inflate' | 'crease'
    brushPolarity: 'add',       // 'add' (+) | 'sub' (-)
    brushRadius: 0.78,
    brushStrength: 0.58,
    brushHardness: 0.50,
    strokeSpacing: 0.28,
    symmetryMode: 'none',       // 'none' | 'x' | 'z' | 'xz'
    stoneTone: 'inherit',       // 'inherit' or key in ROCK2_STONE_TONES
    dabs: []                    // [{ x, y, z, nx, ny, nz, tx, tz, r, s, h, op, tone, strokeId }]
};

/**
 * Resolves the effective Pure-SDF sculpt operation given the selected `brushOp`,
 * explicit `+ / -` polarity (`'add' | 'sub'`), and live keyboard modifiers (`Shift` / `Ctrl`).
 */
export function resolveRock2EffectiveSculptOp(brushOp = 'add', brushPolarity = 'add', shiftDown = false, invertDown = false)
{
    if (shiftDown) return 'smooth';
    let isSub = brushPolarity === 'sub';
    if (invertDown) isSub = !isSub;

    if (brushOp === 'erase') return invertDown ? 'add' : 'erase';
    if (brushOp === 'carve') return invertDown ? 'add' : 'carve';
    if (brushOp === 'smooth') return 'smooth';
    if (brushOp === 'add') return isSub ? 'carve' : 'add';
    if (brushOp === 'inflate') return isSub ? 'deflate' : 'inflate';
    if (brushOp === 'crease') return invertDown ? 'ridge' : 'crease';
    return brushOp;
}

export const ROCK2_MODIFIER_CATALOG = [
    {
        type: 'sculpt_layer',
        kind: 'modifier',
        name: '3D Sculpt Layer',
        shortBadge: 'SCULPT',
        glyph: 'Brush',
        desc: 'Non-destructive 3D Pure-SDF surface sculpting layer (Clay Add, Carve, Planar Facet, Smooth, Inflate & Crease with symmetry).',
        defaults: { ...DEFAULT_ROCK2_SCULPT_LAYER_PARAMS, dabs: [] },
        controlGroups: []
    },
    {
        type: 'split_fault',
        kind: 'modifier',
        name: 'Split & Fault Plane',
        shortBadge: 'FAULT',
        glyph: 'SplitFault',
        desc: 'Non-destructive Pure-SDF kinematic fracture & fault plane modifier with T⁻¹(p) rigid slip, jigsaw teeth, blind V-taper & 3D Fault Gizmo.',
        defaults: { ...DEFAULT_ROCK2_SPLIT_FAULT_PARAMS },
        controlGroups: []
    }
];

export function isRock2BooleanCombineMode(mode)
{
    return mode === 'subtract' || mode === 'chamfer_subtract' || mode === 'intersect';
}

export function isRock2BooleanLayer(layer)
{
    if (!layer || isRock2ModifierType(layer.type)) return false;
    return !!layer.isBoolean || isRock2BooleanCombineMode(layer.combineMode);
}

export function setRock2LayerBooleanMode(layer, enableBool, boolOp = 'subtract')
{
    if (!layer || isRock2ModifierType(layer.type)) return layer;
    const validBoolOp = (boolOp === 'intersect' || boolOp === 'chamfer_subtract') ? boolOp : 'subtract';
    if (enableBool)
    {
        if (!isRock2BooleanCombineMode(layer.combineMode))
        {
            layer.nonBoolCombineMode = layer.combineMode || 'smooth_union';
        }
        layer.isBoolean = true;
        layer.booleanOp = validBoolOp;
        layer.combineMode = validBoolOp;
    }
    else
    {
        layer.isBoolean = false;
        if (isRock2BooleanCombineMode(layer.combineMode))
        {
            layer.combineMode = layer.nonBoolCombineMode || 'smooth_union';
        }
    }
    return layer;
}

export function isRock2ModifierType(type)
{
    return ROCK2_MODIFIER_CATALOG.some(m => m.type === type);
}

export function getRock2ModifierSpec(type)
{
    return ROCK2_MODIFIER_CATALOG.find(m => m.type === type) || null;
}

export function getRock2NodeSpec(type)
{
    return ROCK2_MODIFIER_CATALOG.find(m => m.type === type)
        || ROCK2_NODE_CATALOG.find(n => n.type === type)
        || ROCK2_NODE_CATALOG[0];
}

/*==========================================================================================================================================
               3. INTERACTIVE 3D SPLINE KNOT PRESETS & COMPOSABLE PER-LAYER DISTRIBUTION GENERATORS
==========================================================================================================================================*/

export const ROCK2_ROCK_POOL_OPTIONS = [
    ['Varied Shapes of Base Rock', 'varied_base'],
    ['Mixed Boulders, Cobbles, Slabs & Clasts', 'mixed_boulders'],
    ['Mixed Canyon Outcrop (Cliff + Boulder + Slab + Spire)', 'mixed_outcrop'],
    ['Mixed Badlands (Hoodoo + Pedestal + Spire + Cliff)', 'mixed_badlands'],
    ['Mixed Crystals & Basalt Prisms', 'mixed_crystals'],
    ['Mixed Linear Dikes, Fins & Slabs', 'mixed_linear'],
    ['Exact Base Rock Only (No Shape Mutation)', 'uniform_base']
];

const ROCK_POOLS = {
    mixed_boulders: ['big_boulder', 'smooth_rock', 'small_rock', 'rock_slab'],
    mixed_outcrop:  ['cliff_block', 'big_boulder', 'rock_slab', 'spire_pinnacle'],
    mixed_badlands: ['hoodoo_column', 'pedestal_rock', 'spire_pinnacle', 'cliff_block'],
    mixed_crystals: ['crystal_prism', 'crystal_prism', 'basalt_column', 'spire_pinnacle'],
    mixed_linear:   ['linear_dike', 'linear_dike', 'rock_slab', 'cliff_block']
};

/**
 * Builds editable 3D Catmull-Rom knots `[{ x, y, z, scale }]` for quick cliff, canyon & 3D rock arch spline presets.
 */
export function buildRock2SplinePresetKnots(preset = 'arc', span = 9.2, depth = 2.0)
{
    const half = span * 0.5;
    if (preset === 'desert_arch')
    {
        const archSpan = Math.max(6.0, span * 1.16);
        const h = archSpan * 0.5;
        const apexY = archSpan * 0.56;
        return [
            { x: -h,        y: 0.0,          z:  depth * 0.16, scale: 1.24 },
            { x: -h * 0.68, y: apexY * 0.68, z: -depth * 0.10, scale: 1.02 },
            { x:  0.0,      y: apexY,        z: -depth * 0.20, scale: 0.82 },
            { x:  h * 0.68, y: apexY * 0.66, z:  depth * 0.08, scale: 1.02 },
            { x:  h,        y: 0.0,          z:  depth * 0.20, scale: 1.26 }
        ];
    }
    if (preset === 'sea_arch')
    {
        const archSpan = Math.max(6.4, span * 1.20);
        const h = archSpan * 0.5;
        const apexY = archSpan * 0.52;
        return [
            { x: -h,        y: 0.0,          z: -depth * 0.25, scale: 1.42 },
            { x: -h * 0.58, y: apexY * 0.86, z: -depth * 0.12, scale: 1.18 },
            { x:  h * 0.06, y: apexY,        z:  depth * 0.06, scale: 0.88 },
            { x:  h * 0.66, y: apexY * 0.64, z:  depth * 0.22, scale: 0.98 },
            { x:  h * 0.96, y: 0.0,          z:  depth * 0.30, scale: 1.18 }
        ];
    }
    if (preset === 'cathedral_arch')
    {
        const archSpan = Math.max(6.0, span * 1.14);
        const h = archSpan * 0.5;
        const apexY = archSpan * 0.72;
        return [
            { x: -h,        y: 0.0,          z:  depth * 0.14, scale: 1.30 },
            { x: -h * 0.64, y: apexY * 0.64, z: -depth * 0.08, scale: 1.04 },
            { x:  0.0,      y: apexY,        z: -depth * 0.16, scale: 0.78 },
            { x:  h * 0.64, y: apexY * 0.64, z: -depth * 0.08, scale: 1.04 },
            { x:  h,        y: 0.0,          z:  depth * 0.14, scale: 1.30 }
        ];
    }
    if (preset === 'natural_bridge')
    {
        const archSpan = Math.max(6.8, span * 1.24);
        const h = archSpan * 0.5;
        const deckY = archSpan * 0.46;
        return [
            { x: -h,        y: 0.0,          z:  depth * 0.12, scale: 1.32 },
            { x: -h * 0.56, y: deckY * 0.94, z: -depth * 0.08, scale: 0.96 },
            { x:  0.0,      y: deckY,        z: -depth * 0.12, scale: 0.84 },
            { x:  h * 0.56, y: deckY * 0.94, z: -depth * 0.08, scale: 0.96 },
            { x:  h,        y: 0.0,          z:  depth * 0.12, scale: 1.32 }
        ];
    }
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

export const ROCK2_ARCH_SPLINE_PRESETS = new Set(['desert_arch', 'sea_arch', 'cathedral_arch', 'natural_bridge']);

export const ROCK2_GENERATOR_DEFAULTS = {
    none: {
        type: 'none'
    },
    spline: {
        type: 'spline',
        seed: 701,
        splineMode: 'wall',     // 'wall' (Canyon / Cliff Wall) | 'arch_span' (3D Rock Arch Span)
        splinePreset: 'arc',
        closedLoop: 0,
        knots: buildRock2SplinePresetKnots('arc', 9.2, 2.0),
        count: 7,
        alignToTangent: 1,
        yawOffsetDeg: 0,
        spacingJitter: 0.14,
        lateralJitter: 0.08,
        // 3D Arch Span geological controls (active when splineMode === 'arch_span')
        archPierFlare: 0.42,
        archCrownThin: 0.22,
        archBeddingBlend: 0.45,
        archButtress: 0.45,
        archExtrados: 0.35,
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
        // Hex / Radial / Colonnade / Parallel Fins / Crystal Spray specific
        hexRings: 2,
        cellSpacing: 1.02,
        terraceSteps: 5,
        terraceDrop: 0.55,
        radialLeanDeg: 3,
        colonnadeRanks: 3,
        sprayCount: 15,
        splayAngleDeg: 54,
        sprayCoreRadius: 0.65,
        finCorridorWidth: 2.35,
        finLengthVar: 0.28,
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
    },
    cloud: {
        type: 'cloud',
        seed: 1187,
        cloudMode: 'orbit_halo', // 'orbit_halo' | 'cluster_nebula' | 'rising_vortex' | 'shatter_wake'
        count: 10,
        centerX: 0.0,
        centerY: 3.8,
        centerZ: 0.0,
        radiusXZ: 4.8,
        spreadY: 2.1,
        innerClearance: 0.42,
        orbitTiltDeg: 14,
        rockPoolMode: 'varied_base',
        shapeVariety: 0.86,
        aspectVariation: 0.32,
        scaleMin: 0.36,
        scaleMax: 1.08,
        radialSizeFalloff: 0.44,
        yawJitterDeg: 180,
        tiltJitterDeg: 22,
        intraBlend: 0.0,
        uniqueSeeds: 1
    }
};

export const DEFAULT_ROCK2_INSTANCE_OVERRIDE = {
    offsetX: 0.0,
    offsetY: 0.0,
    offsetZ: 0.0,
    yawDelta: 0.0,
    pitchDelta: 0.0,
    rollDelta: 0.0,
    scaleX: 1.0,
    scaleY: 1.0,
    scaleZ: 1.0,
    seedDelta: 0,
    rockType: '',
    hidden: 0
};

export function hasActiveRock2InstanceOverride(ov)
{
    if (!ov || typeof ov !== 'object') return false;
    return !!(
        ov.hidden ||
        (ov.rockType && ov.rockType !== '') ||
        Math.abs(ov.offsetX || 0) > 1e-4 ||
        Math.abs(ov.offsetY || 0) > 1e-4 ||
        Math.abs(ov.offsetZ || 0) > 1e-4 ||
        Math.abs(ov.yawDelta || 0) > 1e-3 ||
        Math.abs(ov.pitchDelta || 0) > 1e-3 ||
        Math.abs(ov.rollDelta || 0) > 1e-3 ||
        Math.abs((ov.scaleX ?? 1.0) - 1.0) > 1e-4 ||
        Math.abs((ov.scaleY ?? 1.0) - 1.0) > 1e-4 ||
        Math.abs((ov.scaleZ ?? 1.0) - 1.0) > 1e-4 ||
        (ov.seedDelta | 0) !== 0
    );
}

export function normalizeRock2InstanceOverrides(rawMap)
{
    const out = {};
    if (!rawMap || typeof rawMap !== 'object') return out;
    for (const [k, v] of Object.entries(rawMap))
    {
        const idx = Number(k);
        if (!Number.isInteger(idx) || idx < 0 || idx > 128 || !v || typeof v !== 'object') continue;
        const clean = {
            offsetX: Number.isFinite(v.offsetX) ? clamp(v.offsetX, -32.0, 32.0) : 0.0,
            offsetY: Number.isFinite(v.offsetY) ? clamp(v.offsetY, -24.0, 32.0) : 0.0,
            offsetZ: Number.isFinite(v.offsetZ) ? clamp(v.offsetZ, -32.0, 32.0) : 0.0,
            yawDelta: Number.isFinite(v.yawDelta) ? v.yawDelta : 0.0,
            pitchDelta: Number.isFinite(v.pitchDelta) ? clamp(v.pitchDelta, -89.0, 89.0) : 0.0,
            rollDelta: Number.isFinite(v.rollDelta) ? clamp(v.rollDelta, -89.0, 89.0) : 0.0,
            scaleX: Number.isFinite(v.scaleX) ? clamp(v.scaleX, 0.12, 8.0) : 1.0,
            scaleY: Number.isFinite(v.scaleY) ? clamp(v.scaleY, 0.12, 8.0) : 1.0,
            scaleZ: Number.isFinite(v.scaleZ) ? clamp(v.scaleZ, 0.12, 8.0) : 1.0,
            seedDelta: Number.isFinite(v.seedDelta) ? (v.seedDelta | 0) : 0,
            rockType: (typeof v.rockType === 'string' && ROCK2_NODE_CATALOG.some(n => n.type === v.rockType)) ? v.rockType : '',
            hidden: v.hidden ? 1 : 0
        };
        if (hasActiveRock2InstanceOverride(clean))
        {
            out[idx] = clean;
        }
    }
    return out;
}

export function getOrCreateRock2InstanceOverride(generator, index)
{
    if (!generator) return { ...DEFAULT_ROCK2_INSTANCE_OVERRIDE };
    if (!generator.instanceOverrides || typeof generator.instanceOverrides !== 'object')
    {
        generator.instanceOverrides = {};
    }
    const idx = Math.max(0, index | 0);
    if (!generator.instanceOverrides[idx])
    {
        generator.instanceOverrides[idx] = { ...DEFAULT_ROCK2_INSTANCE_OVERRIDE };
    }
    return generator.instanceOverrides[idx];
}

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
        let preset = overrides.splinePreset || overrides.splineShape || merged.splinePreset || 'arc';
        if (overrides.splineMode === 'arch_span' && !overrides.splinePreset && !Array.isArray(overrides.knots) && !ROCK2_ARCH_SPLINE_PRESETS.has(preset))
        {
            preset = 'desert_arch';
        }
        if (ROCK2_ARCH_SPLINE_PRESETS.has(preset) && overrides.splineMode === undefined)
        {
            merged.splineMode = 'arch_span';
        }
        merged.splinePreset = preset;
        if (merged.splineMode === 'arch_span')
        {
            merged.closedLoop = 0;
            if (overrides.count === undefined) merged.count = 9;
            if (overrides.intraBlend === undefined) merged.intraBlend = 0.28;
        }
        else if (preset === 'ring' && overrides.closedLoop === undefined && !Array.isArray(overrides.knots))
        {
            merged.closedLoop = 1;
        }
        if (Array.isArray(overrides.knots) && overrides.knots.length >= 2)
        {
            merged.knots = normalizeSplineKnots(overrides.knots);
        }
        else if (overrides.splineMode === 'arch_span' || ROCK2_ARCH_SPLINE_PRESETS.has(preset) || overrides.splineShape || overrides.splinePreset || overrides.pathLength !== undefined || overrides.curveBend !== undefined || overrides.ringRadius !== undefined)
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

    merged.instanceOverrides = normalizeRock2InstanceOverrides(overrides.instanceOverrides || merged.instanceOverrides);
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

    const modSpec = getRock2ModifierSpec(cleanType);
    if (modSpec)
    {
        const seq = _nextRock2LayerSeq++;
        const rawDefaults = JSON.parse(JSON.stringify(modSpec.defaults));
        const params = { ...rawDefaults, ...(overrides.params ? JSON.parse(JSON.stringify(overrides.params)) : {}) };
        for (const k of Object.keys(modSpec.defaults))
        {
            if (overrides[k] !== undefined) params[k] = JSON.parse(JSON.stringify(overrides[k]));
        }
        if (modSpec.type === 'sculpt_layer' && !Array.isArray(params.dabs))
        {
            params.dabs = [];
        }
        return {
            id: overrides.id || `r2_mod_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
            name: overrides.name || `${modSpec.name} ${String(seq).padStart(2, '0')}`,
            type: modSpec.type,
            kind: 'modifier',
            enabled: overrides.enabled !== undefined ? !!overrides.enabled : true,
            solo: !!overrides.solo,
            combineMode: overrides.combineMode || 'union',
            blendRadius: overrides.blendRadius ?? 0.0,
            params,
            generator: createRock2Generator('none')
        };
    }

    const spec = getRock2NodeSpec(cleanType);
    const seq = _nextRock2LayerSeq++;
    const params = { scaleX: 1.0, scaleY: 1.0, scaleZ: 1.0, ...spec.defaults, ...(overrides.params || {}) };
    for (const k of [...Object.keys(spec.defaults), 'scaleX', 'scaleY', 'scaleZ'])
    {
        if (overrides[k] !== undefined) params[k] = overrides[k];
    }

    const genRaw = overrides.generator || { type: 'none' };
    const generator = createRock2Generator(genRaw.type || 'none', genRaw);

    let combineMode = overrides.combineMode || 'union';
    const isBool = overrides.isBoolean !== undefined ? !!overrides.isBoolean : isRock2BooleanCombineMode(combineMode);
    const booleanOp = isRock2BooleanCombineMode(overrides.booleanOp)
        ? overrides.booleanOp
        : (isRock2BooleanCombineMode(combineMode) ? combineMode : 'subtract');
    if (isBool && !isRock2BooleanCombineMode(combineMode))
    {
        combineMode = booleanOp;
    }

    return {
        id: overrides.id || `r2_layer_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
        name: overrides.name || `${spec.name} ${String(seq).padStart(2, '0')}`,
        type: spec.type,
        kind: 'rock',
        enabled: overrides.enabled !== undefined ? !!overrides.enabled : true,
        solo: !!overrides.solo,
        isBoolean: isBool,
        booleanOp,
        nonBoolCombineMode: overrides.nonBoolCombineMode || (isRock2BooleanCombineMode(combineMode) ? 'smooth_union' : combineMode),
        combineMode,
        blendRadius: overrides.blendRadius ?? (isBool ? 0.14 : 0.35),
        params,
        generator
    };
}

export function cloneRock2Layer(layer)
{
    const seq = _nextRock2LayerSeq++;
    const isMod = isRock2ModifierType(layer.type);
    const isBool = !isMod && isRock2BooleanLayer(layer);
    return {
        id: `r2_${isMod ? 'mod' : 'layer'}_${seq}_${Math.floor(1000 + Math.random() * 8999)}`,
        name: `${layer.name} Copy`,
        type: layer.type,
        kind: isMod ? 'modifier' : 'rock',
        enabled: layer.enabled,
        solo: false,
        isBoolean: isBool,
        booleanOp: layer.booleanOp || (isRock2BooleanCombineMode(layer.combineMode) ? layer.combineMode : 'subtract'),
        nonBoolCombineMode: layer.nonBoolCombineMode || 'smooth_union',
        combineMode: layer.combineMode,
        blendRadius: layer.blendRadius,
        params: JSON.parse(JSON.stringify(layer.params || {})),
        generator: isMod
            ? createRock2Generator('none')
            : createRock2Generator(layer.generator?.type || 'none', JSON.parse(JSON.stringify(layer.generator || {})))
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
    },
    {
        id: 'stack-bryce-hoodoos',
        name: '08 · Bryce Hoodoo Amphitheater (Spline)',
        badge: 'HOODOO SPLINE',
        buildLayers: () => [
            createRock2Layer('hoodoo_column', {
                name: 'Bryce Hoodoo Colonnade',
                combineMode: 'union',
                stoneTone: 'bryceCoral',
                seed: 6408,
                width: 2.1, height: 6.5, length: 2.0,
                hoodooStyle: 'bulb_cap',
                ringCount: 5, ringSwell: 0.24, neckPinch: 0.38, capSize: 1.14, basalApron: 0.42,
                generator: createRock2Generator('spline', {
                    splinePreset: 'arc',
                    knots: buildRock2SplinePresetKnots('arc', 10.4, 2.2),
                    count: 7,
                    alignToTangent: 1,
                    spacingJitter: 0.16,
                    shapeVariety: 0.85,
                    heightVariation: 0.24,
                    scaleVariation: 0.14,
                    intraBlend: 0.22,
                    uniqueSeeds: 1
                })
            }),
            createRock2Layer('spire_pinnacle', {
                name: 'Cathedral Organ Sentinel',
                combineMode: 'smooth_union',
                blendRadius: 0.30,
                stoneTone: 'sandstone',
                seed: 8821,
                posX: 0.0, posY: 0.0, posZ: -1.9,
                width: 2.5, height: 7.4, length: 2.3,
                spireStyle: 'cathedral_organ',
                buttressRibs: 3, fluteDepth: 0.16, taper: 0.62, roundness: 0.14
            })
        ]
    },
    {
        id: 'stack-pedestal-outcrop',
        name: '09 · Balanced Pedestal & Yardang Fin',
        badge: 'PEDESTAL',
        buildLayers: () => [
            createRock2Layer('pedestal_rock', {
                name: 'Hero Balanced Caprock',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 4729,
                posX: -1.3, posY: 0.0, posZ: 0.3,
                width: 3.6, height: 5.5, length: 3.3,
                pedestalStyle: 'balanced_boulder',
                stemHeightRatio: 0.48, neckWidth: 0.34, capOverhang: 1.28, capTiltDeg: 16, plinthFlare: 0.44
            }),
            createRock2Layer('cliff_block', {
                name: 'Wind-Carved Yardang Prow',
                combineMode: 'union',
                stoneTone: 'redSlate',
                seed: 7049,
                posX: 2.6, posY: 0.0, posZ: -0.9,
                yawDeg: -22,
                width: 2.2, height: 3.6, length: 4.8,
                cliffProfile: 'yardang_fin',
                basalUndercut: 0.34, overhangLean: 0.24, roundness: 0.16
            })
        ]
    },
    {
        id: 'stack-floating-islands',
        name: '10 · Floating Sky Islands (Array)',
        badge: 'FLOATING',
        buildLayers: () => [
            createRock2Layer('floating_rock', {
                name: 'Primary Sky Island Sanctuary',
                combineMode: 'union',
                stoneTone: 'granite',
                seed: 9137,
                posX: 0.0, posY: 0.0, posZ: 0.0,
                width: 5.4, height: 5.0, length: 4.8,
                floatingStyle: 'sky_island',
                hoverHeight: 2.6, keelRatio: 0.64, keelSharpness: 0.78, topFlatness: 0.86, rimOverhang: 0.36, keelSpurs: 3
            }),
            createRock2Layer('floating_rock', {
                name: 'Drifting Satellite Shards',
                combineMode: 'union',
                stoneTone: 'limestone',
                seed: 9411,
                width: 2.1, height: 2.7, length: 1.9,
                floatingStyle: 'stalactite_dagger',
                hoverHeight: 3.4, keelRatio: 0.70, keelSharpness: 0.86, topFlatness: 0.52, keelSpurs: 2,
                generator: createRock2Generator('array', {
                    arrayMode: 'radial',
                    radialCount: 4,
                    ringRadius: 4.8,
                    arcAngleDeg: 300,
                    spacingJitter: 0.32,
                    posJitter: 0.45,
                    scaleVariation: 0.28,
                    heightVariation: 0.30,
                    yawJitterDeg: 180,
                    tiltJitterDeg: 10,
                    shapeVariety: 0.85,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-sandstone-arch',
        name: '11 · Moab Sandstone Arch (3D Arch Spline)',
        badge: '3D ARCH SPAN',
        buildLayers: () => [
            createRock2Layer('cliff_block', {
                name: 'Entrada Sandstone Arch Span (3D Spline)',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 9612,
                width: 2.35, height: 1.88, length: 2.30,
                cliffProfile: 'stepped', roundness: 0.18, ledgeStep: 0.34, batterTaper: 0.14,
                generator: createRock2Generator('spline', {
                    seed: 9620,
                    splineMode: 'arch_span',
                    splinePreset: 'desert_arch',
                    count: 10,
                    archPierFlare: 0.46,
                    archCrownThin: 0.24,
                    archBeddingBlend: 0.46,
                    archButtress: 0.55,
                    archExtrados: 0.40,
                    shapeVariety: 0.72,
                    intraBlend: 0.30,
                    uniqueSeeds: 1
                })
            }),
            createRock2Layer('big_boulder', {
                name: 'Perimeter Pier Scree Ring',
                combineMode: 'union',
                stoneTone: 'redSlate',
                seed: 9645,
                width: 1.65, height: 1.15, length: 1.50,
                roundness: 0.24, flatBottom: 0.78,
                generator: createRock2Generator('array', {
                    arrayMode: 'radial',
                    radialCount: 6,
                    ringRadius: 5.9,
                    arcAngleDeg: 320,
                    spacingJitter: 0.35,
                    posJitter: 0.45,
                    rockPoolMode: 'mixed_boulders',
                    scaleVariation: 0.34,
                    yawJitterDeg: 180,
                    shapeVariety: 0.82
                })
            })
        ]
    },
    {
        id: 'stack-sky-orbit-cloud',
        name: '12 · Sky Island + 3D Orbital Debris Cloud',
        badge: '3D CLOUD ORBIT',
        buildLayers: () => [
            createRock2Layer('floating_rock', {
                name: 'Central Levitating Citadel Island',
                combineMode: 'union',
                stoneTone: 'granite',
                seed: 9810,
                width: 4.8, height: 5.0, length: 4.4,
                floatingStyle: 'sky_island',
                hoverHeight: 2.8, keelRatio: 0.64, keelSharpness: 0.80, topFlatness: 0.88, rimOverhang: 0.38, keelSpurs: 3
            }),
            createRock2Layer('big_boulder', {
                name: 'Orbital Asteroid & Shard Halo (3D Cloud)',
                combineMode: 'union',
                stoneTone: 'basalt',
                seed: 9844,
                width: 1.35, height: 1.10, length: 1.20,
                facetCount: 11, roundness: 0.14, angularity: 0.34, flatBottom: 0.0,
                generator: createRock2Generator('cloud', {
                    seed: 9860,
                    cloudMode: 'orbit_halo',
                    count: 12,
                    centerX: 0.0,
                    centerY: 5.2,
                    centerZ: 0.0,
                    radiusXZ: 5.4,
                    spreadY: 2.2,
                    innerClearance: 0.56,
                    orbitTiltDeg: 16,
                    rockPoolMode: 'mixed_boulders',
                    scaleMin: 0.42,
                    scaleMax: 1.12,
                    shapeVariety: 0.88,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-cleaved-jigsaw-monolith',
        name: '13 · Cleaved Jigsaw Monolith (3D Fault Gizmo)',
        badge: 'D1 SPLIT FAULT',
        buildLayers: () => [
            createRock2Layer('big_boulder', {
                name: 'Sierra Granite Monolith',
                combineMode: 'union',
                stoneTone: 'granite',
                seed: 7112,
                width: 4.8, height: 4.6, length: 4.2,
                boulderStyle: 'angular', facetCount: 14, roundness: 0.20, angularity: 0.26, taper: 0.14, flatBottom: 0.88
            }),
            createRock2Layer('split_fault', {
                name: 'Inclined Jigsaw Normal Fault (3D Gizmo)',
                splitMode: 'single_plane',
                originX: 0.15, originY: 1.85, originZ: 0.0,
                strikeYawDeg: 22.0,
                dipPitchDeg: 68.0,
                macroWavinessAmp: 0.22,
                macroWavelength: 2.30,
                microJaggednessAmp: 0.055,
                microFrequency: 3.40,
                octaves: 3,
                anisotropyStretch: 1.55,
                crackGapWidth: 0.08,
                lipChamfer: 0.05,
                vTaperDepth: 0.0,
                normalHeave: 0.26,
                strikeSlip: 0.42,
                dipSlip: 0.34,
                hingeTiltDeg: 9.5,
                gravitySettle: 0.14,
                seed: 1409
            })
        ]
    },
    {
        id: 'stack-conjugate-fault-cliff',
        name: '14 · Conjugate X-Shear & Stepped Fault Cliff',
        badge: 'CONJUGATE SHEAR',
        buildLayers: () => [
            createRock2Layer('cliff_block', {
                name: 'Massive Sandstone Escarpment',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 8310,
                width: 6.4, height: 5.2, length: 4.0,
                cliffProfile: 'rampart', roundness: 0.15, batterTaper: 0.18, flatBottom: 0.94
            }),
            createRock2Layer('split_fault', {
                name: 'Mohr-Coulomb Conjugate X-Shear Pair',
                splitMode: 'conjugate_pair',
                originX: 0.0, originY: 2.3, originZ: 0.0,
                strikeYawDeg: 8.0,
                dipPitchDeg: 78.0,
                conjugateAngleDeg: 58.0,
                planeCount: 3,
                spacing: 1.75,
                spacingJitter: 0.16,
                dipJitterDeg: 2.0,
                macroWavinessAmp: 0.16,
                macroWavelength: 2.60,
                microJaggednessAmp: 0.04,
                microFrequency: 3.10,
                octaves: 3,
                anisotropyStretch: 1.40,
                crackGapWidth: 0.055,
                lipChamfer: 0.04,
                vTaperDepth: 0.0,
                normalHeave: 0.12,
                strikeSlip: 0.24,
                dipSlip: 0.26,
                hingeTiltDeg: 3.5,
                gravitySettle: 0.08,
                seed: 2207
            })
        ]
    },
    {
        id: 'stack-live-boolean-sculpt',
        name: '15 · Live Boolean Cutters + 3D Sculpted Monolith',
        badge: 'BOOLEAN + SCULPT',
        buildLayers: () => [
            createRock2Layer('cliff_block', {
                name: 'Primary Sandstone Monolith',
                combineMode: 'union',
                stoneTone: 'sandstone',
                seed: 4410,
                width: 5.6, height: 4.8, length: 4.2,
                cliffProfile: 'rampart', roundness: 0.16, batterTaper: 0.16, flatBottom: 0.94
            }),
            createRock2Layer('smooth_rock', {
                name: 'Boolean Cavern Portal Cutter',
                isBoolean: true,
                booleanOp: 'subtract',
                combineMode: 'subtract',
                blendRadius: 0.18,
                stoneTone: 'bryceCoral',
                seed: 7712,
                posX: -0.2, posY: 0.85, posZ: 1.05,
                width: 2.55, height: 2.50, length: 3.10, roundness: 0.42
            }),
            createRock2Layer('rock_slab', {
                name: 'Boolean Chamfer Notch Cutter',
                isBoolean: true,
                booleanOp: 'chamfer_subtract',
                combineMode: 'chamfer_subtract',
                blendRadius: 0.22,
                stoneTone: 'redSlate',
                seed: 9103,
                posX: 1.85, posY: 2.75, posZ: 0.25,
                yawDeg: -22, pitchDeg: 18,
                width: 2.4, height: 1.4, length: 2.8, roundness: 0.12
            }),
            createRock2Layer('sculpt_layer', {
                name: 'Hand-Sculpted Facets & Ledges',
                brushOp: 'add',
                brushRadius: 0.82,
                brushStrength: 0.62,
                dabs: [
                    { x: -1.85, y: 2.35, z: 1.15, nx: -0.52, ny: 0.32, nz: 0.79, tx: 0.85, tz: 0.52, r: 0.95, s: 0.68, h: 0.45, op: 'add', tone: 'inherit', strokeId: 1 },
                    { x: -1.35, y: 3.45, z: 0.65, nx: -0.35, ny: 0.74, nz: 0.57, tx: 0.92, tz: 0.39, r: 0.85, s: 0.65, h: 0.65, op: 'flatten', tone: 'inherit', strokeId: 2 },
                    { x: 1.35,  y: 1.55, z: 1.45, nx: 0.44,  ny: 0.22, nz: 0.87, tx: 0.89, tz: -0.45, r: 0.72, s: 0.60, h: 0.52, op: 'carve', tone: 'inherit', strokeId: 3 }
                ]
            })
        ]
    },
    {
        id: 'stack-crystal-cluster',
        name: '16 · Radiating Amethyst & Quartz Crystal Cluster',
        badge: 'CRYSTAL SPRAY',
        buildLayers: () => [
            createRock2Layer('big_boulder', {
                name: 'Dark Obsidian & Basalt Geode Matrix Base',
                combineMode: 'union',
                stoneTone: 'obsidian',
                seed: 6102,
                width: 4.8, height: 1.35, length: 4.5,
                boulderStyle: 'angular', facetCount: 14, roundness: 0.14, angularity: 0.30, flatBottom: 1.0, flatTop: 0.35
            }),
            createRock2Layer('crystal_prism', {
                name: 'Radiating Hexagonal Amethyst Quartz Cluster',
                combineMode: 'union',
                stoneTone: 'amethyst',
                seed: 6410,
                posY: 0.55,
                width: 1.15, height: 4.8, length: 1.15,
                crystalHabit: 'quartz_hex',
                prismSides: 6,
                terminationStyle: 'quartz_rz',
                apexHeightRatio: 0.36,
                faceAsymmetry: 0.22,
                edgeBevel: 0.015,
                shaftTaper: 0.06,
                generator: createRock2Generator('array', {
                    seed: 6422,
                    arrayMode: 'crystal_spray',
                    sprayCount: 16,
                    splayAngleDeg: 58,
                    sprayCoreRadius: 0.85,
                    scaleMin: 0.42,
                    scaleMax: 1.18,
                    heightVariation: 0.32,
                    yawJitterDeg: 180,
                    tiltJitterDeg: 8,
                    shapeVariety: 0.65,
                    aspectVariation: 0.18,
                    intraBlend: 0.04,
                    uniqueSeeds: 1
                })
            }),
            createRock2Layer('crystal_prism', {
                name: 'Secondary Clear Quartz Selenite Needles',
                combineMode: 'union',
                stoneTone: 'quartzIce',
                seed: 6599,
                posY: 0.45,
                width: 0.62, height: 3.4, length: 0.62,
                crystalHabit: 'chisel_selenite',
                prismSides: 6,
                terminationStyle: 'chisel_wedge',
                apexHeightRatio: 0.44,
                edgeBevel: 0.01,
                generator: createRock2Generator('array', {
                    seed: 6611,
                    arrayMode: 'crystal_spray',
                    sprayCount: 9,
                    splayAngleDeg: 72,
                    sprayCoreRadius: 1.35,
                    scaleMin: 0.50,
                    scaleMax: 1.05,
                    shapeVariety: 0.55,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-basalt-colonnade',
        name: '17 · Giant’s Causeway Stepped Basalt Colonnade',
        badge: 'BASALT WALL',
        buildLayers: () => [
            createRock2Layer('basalt_column', {
                name: 'Linear Organ-Pipe Basalt Cliff Wall (A → B)',
                combineMode: 'union',
                stoneTone: 'basalt',
                seed: 5518,
                width: 1.18, height: 6.2, length: 1.18,
                basaltStyle: 'ball_socket',
                polygonSides: 0,
                irregularity: 0.07,
                edgeBevel: 0.06,
                capTilt: 0.06,
                socketDish: -0.22,
                pinchBands: 0.05,
                generator: createRock2Generator('array', {
                    seed: 5540,
                    arrayMode: 'colonnade_wall',
                    startX: -5.4, startY: 0.0, startZ: -1.4,
                    endX:    5.4, endY:   0.0, endZ:  -0.6,
                    countX: 11,
                    colonnadeRanks: 3,
                    cellSpacing: 1.04,
                    terraceSteps: 6,
                    terraceDrop: 0.58,
                    radialLeanDeg: 3.5,
                    heightVariation: 0.16,
                    scaleVariation: 0.06,
                    yawJitterDeg: 18,
                    tiltJitterDeg: 1.5,
                    shapeVariety: 0.60,
                    uniqueSeeds: 1
                })
            }),
            createRock2Layer('basalt_column', {
                name: 'Foreground Hexagonal Stepping-Stone Terrace',
                combineMode: 'union',
                stoneTone: 'obsidian',
                seed: 5620,
                posZ: 1.65,
                width: 1.16, height: 2.4, length: 1.16,
                basaltStyle: 'ball_socket',
                polygonSides: 6,
                irregularity: 0.06,
                edgeBevel: 0.07,
                socketDish: -0.28,
                generator: createRock2Generator('array', {
                    seed: 5688,
                    arrayMode: 'hex',
                    hexRings: 2,
                    cellSpacing: 1.04,
                    terraceSteps: 5,
                    terraceDrop: 0.64,
                    radialLeanDeg: 1.5,
                    heightVariation: 0.14,
                    scaleVariation: 0.04,
                    yawJitterDeg: 12,
                    tiltJitterDeg: 1.0,
                    uniqueSeeds: 1
                })
            })
        ]
    },
    {
        id: 'stack-linear-dike-fins',
        name: '18 · Linear Volcanic Dike, Hogbacks & Parallel Fins',
        badge: 'LINEAR DIKE',
        buildLayers: () => [
            createRock2Layer('linear_dike', {
                name: 'Central Shiprock Volcanic Igneous Dike Spine',
                combineMode: 'union',
                stoneTone: 'redSlate',
                seed: 7820,
                width: 11.2, height: 5.8, length: 1.50,
                linearStyle: 'volcanic_dike',
                dipTiltDeg: 6,
                crestSharpness: 0.72,
                spineSegments: 4,
                crestUndulation: 0.36,
                wallTaper: 0.34,
                strikeBow: 0.10,
                roundness: 0.11
            }),
            createRock2Layer('linear_dike', {
                name: 'Parallel Tilted Hogback & Sandstone Fin Corridor',
                combineMode: 'smooth_union',
                blendRadius: 0.22,
                stoneTone: 'sandstone',
                seed: 7904,
                width: 7.4, height: 3.4, length: 1.25,
                linearStyle: 'hogback_flatiron',
                dipTiltDeg: 28,
                crestSharpness: 0.66,
                spineSegments: 3,
                crestUndulation: 0.24,
                wallTaper: 0.28,
                roundness: 0.15,
                generator: createRock2Generator('array', {
                    seed: 7931,
                    arrayMode: 'parallel_fins',
                    countZ: 4,
                    countX: 1,
                    finCorridorWidth: 2.85,
                    finLengthVar: 0.32,
                    rowStagger: 0.30,
                    heightVariation: 0.26,
                    scaleVariation: 0.14,
                    yawJitterDeg: 4,
                    tiltJitterDeg: 2,
                    shapeVariety: 0.72,
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
            isBoolean: isRock2BooleanLayer(l),
            booleanOp: l.booleanOp || (isRock2BooleanCombineMode(l.combineMode) ? l.combineMode : 'subtract'),
            nonBoolCombineMode: l.nonBoolCombineMode || 'smooth_union',
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
                isBoolean: l.isBoolean,
                booleanOp: l.booleanOp,
                nonBoolCombineMode: l.nonBoolCombineMode,
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

export function mulRock2Mat3(A, B)
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
const mulMat3 = mulRock2Mat3;

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

    // Compute smooth horizontal unit tangents (tx, tz) AND full 3D unit tangents (t3x, t3y, t3z)
    for (let i = 0; i < samples.length; i++)
    {
        const a = samples[Math.max(0, i - 1)];
        const b = samples[Math.min(samples.length - 1, i + 1)];
        const rawDx = b.x - a.x;
        const rawDy = b.y - a.y;
        const rawDz = b.z - a.z;

        let dx = rawDx;
        let dz = rawDz;
        const hl = Math.hypot(dx, dz);
        if (hl < 1e-5) { dx = 1.0; dz = 0.0; }
        else { dx /= hl; dz /= hl; }
        samples[i].tx = dx;
        samples[i].tz = dz;

        const l3 = Math.hypot(rawDx, rawDy, rawDz);
        if (l3 < 1e-5)
        {
            samples[i].t3x = 1.0;
            samples[i].t3y = 0.0;
            samples[i].t3z = 0.0;
        }
        else
        {
            samples[i].t3x = rawDx / l3;
            samples[i].t3y = rawDy / l3;
            samples[i].t3z = rawDz / l3;
        }
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

    let t3x = lerp(s0.t3x ?? s0.tx, s1.t3x ?? s1.tx, f);
    let t3y = lerp(s0.t3y ?? 0.0,   s1.t3y ?? 0.0,   f);
    let t3z = lerp(s0.t3z ?? s0.tz, s1.t3z ?? s1.tz, f);
    const t3l = Math.hypot(t3x, t3y, t3z) || 1;
    t3x /= t3l; t3y /= t3l; t3z /= t3l;

    return {
        x: lerp(s0.x, s1.x, f),
        y: lerp(s0.y, s1.y, f),
        z: lerp(s0.z, s1.z, f),
        scale: lerp(s0.scale, s1.scale, f),
        tx,
        ty: 0.0,
        tz,
        t3x,
        t3y,
        t3z
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
function evaluateRock2RawGeneratorInstances(generator, baseRockParams, baseType = 'big_boulder')
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
        if (g.splineMode === 'arch_span')
        {
            const count = clamp(Math.round(g.count ?? 9), 4, 40);
            const arcTable = buildRock2SplineArcTable({ ...g, closedLoop: 0 }, 28);
            const spJitter = clamp(g.spacingJitter ?? 0.12, 0.0, 0.65);
            const latJit = g.lateralJitter ?? 0.06;
            const sVar = clamp(g.scaleVariation ?? 0.14, 0.0, 0.70);
            const hVar = clamp(g.heightVariation ?? 0.16, 0.0, 0.70);
            const yawVar = g.yawVariationDeg ?? 7;
            const tiltVar = g.tiltVariationDeg ?? 3.5;
            const align = (g.alignToTangent ?? 1) > 0.5;
            const yawOff = g.yawOffsetDeg ?? 0;

            const pierFlare = clamp(g.archPierFlare ?? 0.42, 0.0, 1.25);
            const crownThin = clamp(g.archCrownThin ?? 0.22, 0.0, 0.65);
            const bedBlend = clamp(g.archBeddingBlend ?? 0.45, 0.0, 1.0);
            const buttressAmt = clamp(g.archButtress ?? 0.45, 0.0, 1.0);
            const extradosAmt = clamp(g.archExtrados ?? 0.35, 0.0, 1.0);

            const baseW = Math.max(0.25, baseRockParams?.width ?? 2.35);
            const baseH = Math.max(0.25, baseRockParams?.height ?? 1.85);
            const avgStepArc = arcTable.totalLen / Math.max(1, count - 1);
            const bridgeScaleX = Math.max(0.95, (avgStepArc * 1.28) / baseW);
            const stepU = 1.0 / (count - 1);

            for (let i = 0; i < count; i++)
            {
                const baseU = i * stepU;
                const endMask = (i === 0 || i === count - 1) ? 0.0 : 1.0;
                const uJit = clamp(baseU + rng.signed() * stepU * 0.28 * spJitter * endMask, 0.0, 1.0);
                const sp = sampleRock2SplineAtNormalizedArc(arcTable, uJit);

                const sinU = Math.sin(Math.PI * uJit);
                const pierEnv = Math.pow(Math.max(0.0, 1.0 - sinU), 1.35);
                const envThick = Math.max(0.32, (1.0 + pierFlare * pierEnv) * (1.0 - crownThin * sinU));

                const knotScale = sp.scale ?? 1.0;
                const sBase = Math.max(0.22, knotScale * envThick * (1.0 + rng.signed() * sVar * 0.75));
                const ax = 1.0 + rng.signed() * aspectVar * 0.45;
                const az = 1.0 + rng.signed() * aspectVar * 0.55;

                const minCourseScaleY = bedBlend > 0.25
                    ? Math.max(1.0, (Math.abs(sp.t3y) * avgStepArc * 1.20) / baseH)
                    : 1.0;
                const sY = Math.max(0.24, Math.max(sBase, minCourseScaleY * 0.92) * (1.0 + rng.signed() * hVar * 0.65));
                const sX = Math.max(0.24, Math.max(sBase * ax, bridgeScaleX * (0.94 + 0.14 * pierEnv)));
                const sZ = Math.max(0.24, sBase * az * (1.0 + 0.18 * pierEnv));

                const tangYawDeg = align ? (-Math.atan2(sp.tz, sp.tx) / DEG2RAD) : 0.0;
                const horizMag = Math.max(1e-4, Math.hypot(sp.t3x, sp.t3z));
                const climbDeg = align ? (Math.atan2(sp.t3y, horizMag) / DEG2RAD) : 0.0;

                const blockH = baseH * sY;
                const spanMask = clamp(sp.y / Math.max(0.32, blockH * 0.68), 0.0, 1.0);
                const effRollDeg = climbDeg * (1.0 - 0.72 * bedBlend) * (0.28 + 0.72 * spanMask);
                const effRollRad = effRollDeg * DEG2RAD;

                // Extrados normal = Column 1 of buildRock2EulerMatrix3(tangYawDeg, 0, effRollDeg)
                const upX = -sp.tx * Math.sin(effRollRad);
                const upY =  Math.cos(effRollRad);
                const upZ = -sp.tz * Math.sin(effRollRad);

                // Center elevated arch voussoirs on the 3D spline curve while keeping y=0 piers grounded
                const centerShift = -0.46 * blockH * spanMask;
                const nx = -sp.tz, nz = sp.tx;
                const lat = rng.signed() * latJit * (0.4 + 0.6 * pierEnv);
                const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

                instances.push({
                    pos: [
                        sp.x + upX * centerShift + nx * lat,
                        Math.max(0.0, sp.y + upY * centerShift),
                        sp.z + upZ * centerShift + nz * lat
                    ],
                    yawDeg: tangYawDeg + yawOff + rng.signed() * yawVar * (0.45 + 0.55 * pierEnv),
                    pitchDeg: rng.signed() * tiltVar,
                    rollDeg: effRollDeg + rng.signed() * tiltVar * 0.65,
                    scaleX: sX,
                    scaleY: sY,
                    scaleZ: sZ,
                    seedOffset: unique ? (i + 1) * 7919 : 0,
                    shapeVariety,
                    rockType: rType
                });
            }

            // Optional Pier Buttresses (flanking outer abutments — strictly outside the portal span!)
            if (buttressAmt > 0.05)
            {
                const pierEnds = [
                    { u: 0.0, outSign: -1.0, seedIdx: count + 1 },
                    { u: 1.0, outSign:  1.0, seedIdx: count + 2 }
                ];
                for (let pi = 0; pi < pierEnds.length; pi++)
                {
                    const pe = pierEnds[pi];
                    const sp = sampleRock2SplineAtNormalizedArc(arcTable, pe.u);
                    const tangYawDeg = align ? (-Math.atan2(sp.tz, sp.tx) / DEG2RAD) : 0.0;
                    const knotScale = sp.scale ?? 1.0;
                    const bScale = Math.max(0.28, knotScale * (0.72 + 0.55 * buttressAmt));
                    const outShift = pe.outSign * baseW * (0.48 + 0.34 * buttressAmt);
                    const nx = -sp.tz, nz = sp.tx;
                    const flankShift = rng.signed() * baseW * 0.22;

                    instances.push({
                        pos: [
                            sp.x + sp.tx * outShift + nx * flankShift,
                            Math.max(0.0, sp.y * 0.25),
                            sp.z + sp.tz * outShift + nz * flankShift
                        ],
                        yawDeg: tangYawDeg + yawOff + rng.signed() * 18.0,
                        pitchDeg: rng.signed() * 5.0,
                        rollDeg: -pe.outSign * (6.0 + 8.0 * buttressAmt),
                        scaleX: bScale * 1.12,
                        scaleY: bScale * (0.88 + 0.42 * buttressAmt),
                        scaleZ: bScale * 1.18,
                        seedOffset: unique ? pe.seedIdx * 7919 : 0,
                        shapeVariety,
                        rockType: chooseInstanceRockType(baseType, poolMode, rng, pe.seedIdx)
                    });
                }
            }

            // Optional Extrados Shoulder / Cap Courses (strictly ABOVE the spline curve — never intrudes into portal!)
            if (extradosAmt > 0.05)
            {
                const capUs = extradosAmt > 0.52 ? [0.24, 0.50, 0.76] : [0.28, 0.72];
                for (let ci = 0; ci < capUs.length; ci++)
                {
                    const uCap = capUs[ci] + rng.signed() * 0.03;
                    const sp = sampleRock2SplineAtNormalizedArc(arcTable, uCap);
                    const tangYawDeg = align ? (-Math.atan2(sp.tz, sp.tx) / DEG2RAD) : 0.0;
                    const horizMag = Math.max(1e-4, Math.hypot(sp.t3x, sp.t3z));
                    const climbDeg = align ? (Math.atan2(sp.t3y, horizMag) / DEG2RAD) : 0.0;
                    const effRollDeg = climbDeg * (1.0 - 0.80 * bedBlend) * 0.65;
                    const effRollRad = effRollDeg * DEG2RAD;
                    const upX = -sp.tx * Math.sin(effRollRad);
                    const upY =  Math.cos(effRollRad);
                    const upZ = -sp.tz * Math.sin(effRollRad);

                    const sinU = Math.sin(Math.PI * uCap);
                    const cScale = Math.max(0.24, (sp.scale ?? 1.0) * (0.58 + 0.42 * extradosAmt) * (1.0 - crownThin * 0.55 * sinU));
                    const lift = baseH * (0.14 + 0.24 * extradosAmt);
                    const seedIdx = count + 5 + ci;

                    instances.push({
                        pos: [
                            sp.x + upX * lift,
                            sp.y + upY * lift,
                            sp.z + upZ * lift
                        ],
                        yawDeg: tangYawDeg + yawOff + rng.signed() * 9.0,
                        pitchDeg: rng.signed() * 3.5,
                        rollDeg: effRollDeg,
                        scaleX: cScale * 1.26,
                        scaleY: cScale * 0.56,
                        scaleZ: cScale * 0.88,
                        seedOffset: unique ? seedIdx * 7919 : 0,
                        shapeVariety,
                        rockType: chooseInstanceRockType(baseType, poolMode, rng, seedIdx)
                    });
                }
            }

            return { intraBlend: clamp(g.intraBlend ?? 0.28, 0.0, 2.5), instances };
        }

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
        else if (mode === 'colonnade_wall')
        {
            // Linear Organ-Pipe Basalt / Palisade Wall along A -> B with hex-interlocked depth ranks & stepped cliff terraces
            const ax = g.startX ?? -5.2, ay = g.startY ?? 0.0, az = g.startZ ?? 0.0;
            const bx = g.endX   ??  5.2, by = g.endY   ?? 0.0, bz = g.endZ   ?? 0.0;
            const dx = bx - ax, dy = by - ay, dz = bz - az;
            const lineLen = Math.hypot(dx, dz) || 1.0;
            const tx = dx / lineLen, tz = dz / lineLen;
            const nx = -tz, nz = tx;
            const lineYawDeg = Math.atan2(dx, dz) / DEG2RAD;

            const cols = clamp(Math.round(g.countX ?? 10), 2, 24);
            const ranks = clamp(Math.round(g.colonnadeRanks ?? 3), 1, 5);
            const cellSp = Math.max(0.28, (g.cellSpacing ?? 1.04) * Math.max(0.35, baseRockParams?.width ?? 1.15) * 0.88);
            const steps = clamp(Math.round(g.terraceSteps ?? 6), 1, 14);
            const drop = clamp(g.terraceDrop ?? 0.58, 0.0, 0.90);
            const lean = g.radialLeanDeg ?? 3.0;
            const posJit = g.posJitter ?? 0.02;
            const sVar = clamp(g.scaleVariation ?? 0.06, 0.0, 0.55);

            let idx = 0;
            for (let rk = 0; rk < ranks; rk++)
            {
                const rankFrac = ranks > 1 ? (rk / (ranks - 1)) : 0.0;
                const rowShiftU = (rk % 2) * (0.5 / Math.max(1, cols - 1));
                const depthOff = (rk - (ranks - 1) * 0.5) * cellSp * 0.866;

                for (let ci = 0; ci < cols; ci++)
                {
                    if (instances.length >= 64) break;
                    const u = clamp((cols > 1 ? ci / (cols - 1) : 0.5) + rowShiftU, 0.0, 1.0);
                    const ridgeWave = 1.0 - 0.14 * Math.abs(u * 2.0 - 1.0);
                    let hFrac = ridgeWave * (1.0 - drop * rankFrac);
                    if (steps > 1) hFrac = Math.max(0.16, Math.round(hFrac * steps) / steps);
                    hFrac = Math.max(0.15, hFrac * (1.0 + rng.signed() * hVar));

                    const s = Math.max(0.20, 1.0 + rng.signed() * sVar);
                    const rType = chooseInstanceRockType(baseType, poolMode, rng, idx);

                    instances.push({
                        pos: [
                            ax + dx * u + nx * depthOff + rng.signed() * posJit,
                            ay + dy * u,
                            az + dz * u + nz * depthOff + rng.signed() * posJit
                        ],
                        yawDeg: lineYawDeg + rng.signed() * yawJit,
                        pitchDeg: rankFrac * lean + rng.signed() * tiltJit,
                        rollDeg: (u - 0.5) * lean * 0.65 + rng.signed() * tiltJit,
                        scaleX: s,
                        scaleY: hFrac,
                        scaleZ: s,
                        seedOffset: unique ? (++idx) * 7919 : 0,
                        shapeVariety: shapeVariety * 0.55,
                        rockType: rType
                    });
                }
            }
        }
        else if (mode === 'parallel_fins')
        {
            // Parallel Joint Set / Moab Sandstone Fin Field / Parallel Hogback Strike Ribs
            const rowsZ = clamp(Math.round(g.countZ ?? 4), 2, 10);
            const segsX = clamp(Math.round(g.countX ?? 1), 1, 6);
            const corridorZ = Math.max(0.5, g.finCorridorWidth ?? 2.65);
            const segStepX = Math.max(0.6, (baseRockParams?.width ?? 6.5) * 0.88);
            const lenVar = clamp(g.finLengthVar ?? 0.30, 0.0, 0.75);
            const stag = clamp(g.rowStagger ?? 0.30, 0.0, 0.85);
            const posJit = g.posJitter ?? 0.14;
            const sVar = clamp(g.scaleVariation ?? 0.16, 0.0, 0.65);

            let idx = 0;
            for (let rzIdx = 0; rzIdx < rowsZ; rzIdx++)
            {
                const zBase = (rzIdx - (rowsZ - 1) * 0.5) * corridorZ;
                const rowShiftX = ((rzIdx % 2) - 0.5) * stag * segStepX * 0.65;
                const flankEnv = rowsZ > 2 ? (1.0 - 0.28 * Math.abs((rzIdx / (rowsZ - 1)) * 2.0 - 1.0)) : 1.0;

                for (let sxIdx = 0; sxIdx < segsX; sxIdx++)
                {
                    if (instances.length >= 64) break;
                    const xBase = (sxIdx - (segsX - 1) * 0.5) * segStepX + rowShiftX;
                    const lenScale = Math.max(0.28, flankEnv * (1.0 + rng.signed() * lenVar));
                    const thickScale = Math.max(0.24, (1.0 + rng.signed() * sVar));
                    const hScale = Math.max(0.22, flankEnv * (1.0 + rng.signed() * hVar));
                    const rType = chooseInstanceRockType(baseType, poolMode, rng, idx);

                    instances.push({
                        pos: [
                            xBase + rng.signed() * posJit,
                            0.0,
                            zBase + rng.signed() * posJit * 0.45
                        ],
                        yawDeg: rng.signed() * Math.min(yawJit, 14),
                        pitchDeg: rng.signed() * tiltJit,
                        rollDeg: rng.signed() * tiltJit,
                        scaleX: lenScale,
                        scaleY: hScale,
                        scaleZ: thickScale,
                        seedOffset: unique ? (++idx) * 7919 : 0,
                        shapeVariety,
                        rockType: rType
                    });
                }
            }
        }
        else if (mode === 'crystal_spray')
        {
            // 3D Radiating Crystal Cluster / Mineral Geode Burst / Basalt Rosette Fan (Golden-angle phyllotaxis!)
            const n = clamp(Math.round(g.sprayCount ?? 15), 3, 42);
            const maxSplay = clamp(g.splayAngleDeg ?? 58, 0, 88);
            const coreRad = Math.max(0.0, g.sprayCoreRadius ?? 0.75);
            const sMin = Math.max(0.18, g.scaleMin ?? 0.42);
            const sMax = Math.max(sMin, g.scaleMax ?? 1.18);
            const goldenAngle = 2.399963229728653;

            for (let i = 0; i < n; i++)
            {
                const u = n > 1 ? (i / (n - 1)) : 0.0;
                const az = i * goldenAngle + rng.signed() * 0.16;
                const polarDeg = Math.pow(u, 0.78) * maxSplay + (i === 0 ? rng.range(0, 4) : rng.signed() * tiltJit);
                const polarRad = clamp(polarDeg, 0, 89) * DEG2RAD;

                const dirX = Math.sin(polarRad) * Math.cos(az);
                const dirZ = Math.sin(polarRad) * Math.sin(az);
                const radDist = coreRad * Math.pow(u, 0.65);

                const kingBias = lerp(sMax, sMin, Math.pow(u, 0.72));
                const wS = Math.max(0.18, kingBias * (1.0 + rng.signed() * aspectVar * 0.65));
                const hS = Math.max(0.22, kingBias * (1.08 - 0.24 * u) * (1.0 + rng.signed() * hVar));
                const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

                const outAzDeg = Math.atan2(dirX, dirZ) / DEG2RAD;
                const spinJitter = rng.signed() * yawJit;

                instances.push({
                    pos: [
                        dirX * radDist,
                        Math.max(0.0, (1.0 - u) * coreRad * 0.18 - u * 0.08),
                        dirZ * radDist
                    ],
                    yawDeg: outAzDeg + spinJitter * 0.25,
                    pitchDeg: Math.cos(az) * polarDeg,
                    rollDeg: -Math.sin(az) * polarDeg,
                    scaleX: wS,
                    scaleY: hS,
                    scaleZ: wS * (1.0 + rng.signed() * aspectVar * 0.35),
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

    if (type === 'cloud')
    {
        const mode = g.cloudMode || 'orbit_halo';
        const count = clamp(Math.round(g.count ?? 10), 2, 64);
        const cx = g.centerX ?? 0.0;
        const cy = g.centerY ?? 3.8;
        const cz = g.centerZ ?? 0.0;
        const radXZ = Math.max(0.35, g.radiusXZ ?? 4.8);
        const sprY = Math.max(0.10, g.spreadY ?? 2.1);
        const innerClear = clamp(g.innerClearance ?? 0.42, 0.0, 0.92);
        const tiltRad = (g.orbitTiltDeg ?? 14) * DEG2RAD;
        const cosT = Math.cos(tiltRad), sinT = Math.sin(tiltRad);

        const sMin = Math.min(g.scaleMin ?? 0.36, g.scaleMax ?? 1.08);
        const sMax = Math.max(g.scaleMin ?? 0.36, g.scaleMax ?? 1.08);
        const falloff = clamp(g.radialSizeFalloff ?? 0.44, -1.0, 1.0);
        const yawJit = g.yawJitterDeg ?? 180;
        const tiltJit = g.tiltJitterDeg ?? 22;
        const goldenAngle = 2.399963229728653;

        for (let i = 0; i < count; i++)
        {
            let ox = 0.0, oy = 0.0, oz = 0.0, distNorm = 0.5;

            if (mode === 'cluster_nebula')
            {
                const u = (i + 0.5) / count;
                const ySphere = 1.0 - 2.0 * u + rng.signed() * 0.22;
                const rHoriz = Math.sqrt(Math.max(0.0, 1.0 - clamp(ySphere, -1.0, 1.0) ** 2));
                const phi = i * goldenAngle + rng.signed() * 0.35;
                const rShell = lerp(innerClear * 0.65, 1.0, Math.cbrt(rng.float()));
                const xRaw = Math.cos(phi) * rHoriz * radXZ * rShell;
                const yRaw = clamp(ySphere, -1.0, 1.0) * sprY * rShell;
                const zRaw = Math.sin(phi) * rHoriz * radXZ * rShell;
                ox = xRaw * cosT - yRaw * sinT;
                oy = xRaw * sinT + yRaw * cosT;
                oz = zRaw;
                distNorm = clamp(rShell, 0.0, 1.0);
            }
            else if (mode === 'rising_vortex')
            {
                const hFrac = count > 1 ? (i / (count - 1)) : 0.5;
                const hSigned = hFrac * 2.0 - 1.0 + rng.signed() * 0.12;
                const funnel = lerp(Math.max(0.18, innerClear * 0.55), 1.0, 0.28 + 0.72 * hFrac);
                const r = radXZ * funnel * (0.82 + 0.32 * rng.float());
                const ang = hFrac * Math.PI * 3.6 + rng.signed() * 0.38;
                const xRaw = Math.cos(ang) * r;
                const yRaw = hSigned * sprY;
                const zRaw = Math.sin(ang) * r;
                ox = xRaw * cosT - yRaw * sinT;
                oy = xRaw * sinT + yRaw * cosT;
                oz = zRaw;
                distNorm = clamp(hFrac, 0.0, 1.0);
            }
            else if (mode === 'shatter_wake')
            {
                const tWake = count > 1 ? (i / (count - 1)) : 0.0;
                const tJit = clamp(tWake + rng.signed() * (0.45 / Math.max(2, count)), 0.0, 1.0);
                const cone = 0.16 + 0.84 * Math.pow(tJit, 0.78);
                const xStream = lerp(-radXZ * (1.0 - innerClear * 0.45), radXZ, tJit);
                const yStream = rng.signed() * sprY * cone;
                const zStream = rng.signed() * radXZ * 0.58 * cone;
                ox = xStream * cosT - yStream * sinT;
                oy = xStream * sinT + yStream * cosT;
                oz = zStream;
                distNorm = tJit;
            }
            else
            {
                // Default 'orbit_halo': 3D tilted orbital asteroid / shard ring with inner clearance void
                const ang = (i / count) * Math.PI * 2.0 + rng.signed() * (Math.PI / count) * 0.86;
                const rNorm = lerp(innerClear, 1.0, Math.sqrt(rng.float()));
                const r = radXZ * rNorm;
                const xOrb = Math.cos(ang) * r;
                const yOrb = rng.signed() * sprY * 0.50 * (1.0 - 0.25 * (rNorm - innerClear));
                const zOrb = Math.sin(ang) * r;
                ox = xOrb * cosT - yOrb * sinT;
                oy = xOrb * sinT + yOrb * cosT;
                oz = zOrb;
                distNorm = clamp((rNorm - innerClear) / Math.max(0.08, 1.0 - innerClear), 0.0, 1.0);
            }

            const radialBias = falloff >= 0.0
                ? lerp(1.0, 1.0 - falloff * 0.72, distNorm)
                : lerp(1.0 + falloff * 0.72, 1.0, distNorm);
            const sRand = lerp(sMin, sMax, Math.pow(rng.float(), 1.25));
            const s = Math.max(0.14, sRand * radialBias);
            const rType = chooseInstanceRockType(baseType, poolMode, rng, i);

            instances.push({
                pos: [cx + ox, cy + oy, cz + oz],
                yawDeg: rng.signed() * yawJit,
                pitchDeg: rng.signed() * tiltJit,
                rollDeg: rng.signed() * tiltJit,
                scaleX: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar)),
                scaleY: Math.max(0.14, s * (1.0 + rng.signed() * aspectVar * 0.85)),
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
 * Evaluates a layer's placement generator (`none`, `spline`, `array`, `cloud`, `splatter`, `scatter`)
 * and applies non-destructive per-instance overrides (`g.instanceOverrides[k]`)!
 */
export function evaluateRock2GeneratorInstances(generator, baseRockParams, baseType = 'big_boulder', options = {})
{
    const rawRes = evaluateRock2RawGeneratorInstances(generator, baseRockParams, baseType);
    const rawList = rawRes.instances || [];
    const g = generator || { type: 'none' };
    const ovMap = (!options.ignoreOverrides && g.type !== 'none' && g.instanceOverrides && typeof g.instanceOverrides === 'object')
        ? g.instanceOverrides
        : null;

    const activeInstances = [];
    const rawInstances = [];

    for (let i = 0; i < rawList.length; i++)
    {
        const r = rawList[i];
        const rawCopy = {
            ...r,
            pos: [r.pos[0], r.pos[1], r.pos[2]],
            sourceIndex: i
        };
        rawInstances.push(rawCopy);

        const ov = ovMap ? ovMap[i] : null;
        if (!ov)
        {
            activeInstances.push({
                ...rawCopy,
                rawPos: [r.pos[0], r.pos[1], r.pos[2]],
                rawEuler: [r.yawDeg, r.pitchDeg, r.rollDeg],
                hasOverride: false
            });
            continue;
        }

        if (ov.hidden) continue;

        const chosenType = (ov.rockType && ROCK2_NODE_CATALOG.some(n => n.type === ov.rockType))
            ? ov.rockType
            : r.rockType;

        activeInstances.push({
            pos: [
                r.pos[0] + (ov.offsetX ?? 0.0),
                r.pos[1] + (ov.offsetY ?? 0.0),
                r.pos[2] + (ov.offsetZ ?? 0.0)
            ],
            rawPos: [r.pos[0], r.pos[1], r.pos[2]],
            rawEuler: [r.yawDeg, r.pitchDeg, r.rollDeg],
            yawDeg: r.yawDeg + (ov.yawDelta ?? 0.0),
            pitchDeg: clamp(r.pitchDeg + (ov.pitchDelta ?? 0.0), -89.0, 89.0),
            rollDeg: clamp(r.rollDeg + (ov.rollDelta ?? 0.0), -89.0, 89.0),
            scaleX: Math.max(0.08, r.scaleX * (ov.scaleX ?? 1.0)),
            scaleY: Math.max(0.08, r.scaleY * (ov.scaleY ?? 1.0)),
            scaleZ: Math.max(0.08, r.scaleZ * (ov.scaleZ ?? 1.0)),
            seedOffset: (r.seedOffset || 0) + (ov.seedDelta || 0),
            shapeVariety: r.shapeVariety,
            rockType: chosenType,
            sourceIndex: i,
            hasOverride: hasActiveRock2InstanceOverride(ov)
        });
    }

    return {
        intraBlend: rawRes.intraBlend,
        instances: activeInstances,
        rawInstances,
        rawCount: rawInstances.length
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

    // When mixing a slab, hoodoo, pedestal or small rock into a mixed pool, adapt vertical ratio naturally
    if (rockType !== layerParams._sourceType)
    {
        const defaults = getRock2NodeSpec(rockType).defaults;
        for (const [dk, dv] of Object.entries(defaults))
        {
            if (out[dk] === undefined) out[dk] = dv;
        }
        if (rockType === 'rock_slab') out.height *= 0.42;
        else if (rockType === 'small_rock') { out.width *= 0.62; out.height *= 0.55; out.length *= 0.62; }
        else if (rockType === 'spire_pinnacle' || rockType === 'hoodoo_column') { out.width *= 0.65; out.height *= 1.35; out.length *= 0.65; }
        else if (rockType === 'pedestal_rock') { out.width *= 0.85; out.height *= 1.12; out.length *= 0.85; }
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
        if (v > 0.50 && mrng.float() < v * 0.60 && out.cliffProfile !== 'exfoliation_dome' && out.cliffProfile !== 'yardang_fin')
        {
            const profiles = ['vertical', 'overhang', 'rampart', 'mesa', 'prow'];
            out.cliffProfile = profiles[mrng.int(0, profiles.length)];
        }
        out.facetCount    = clamp(Math.round((out.facetCount ?? 11) + mrng.signed() * 5 * v), 9, 18);
        out.roundness     = clamp((out.roundness ?? 0.14) + mrng.signed() * 0.09 * v, 0.03, 0.58);
        out.overhangLean  = clamp((out.overhangLean ?? 0.08) + mrng.signed() * 0.22 * v, -0.35, 0.48);
        out.basalUndercut = clamp((out.basalUndercut ?? 0.0) + mrng.signed() * 0.14 * v, 0.0, 0.55);
        out.batterTaper   = clamp((out.batterTaper ?? 0.14) + mrng.signed() * 0.20 * v, -0.25, 0.48);
        out.crestSlope    = clamp((out.crestSlope ?? 0.08) + mrng.signed() * 0.26 * v, -0.40, 0.40);
        out.prowWedge     = clamp((out.prowWedge ?? 0.15) + mrng.signed() * 0.22 * v, 0.02, 0.52);
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
        out.facetSides   = clamp(Math.round((out.facetSides ?? 7) + mrng.signed() * 3 * v), 4, 12);
        out.taper        = clamp((out.taper ?? 0.58) + mrng.signed() * 0.22 * v, 0.15, 0.85);
        out.buttressRibs = clamp(Math.round((out.buttressRibs ?? 0) + (mrng.float() < 0.45 * v ? mrng.int(-1, 2) : 0)), 0, 5);
        out.fluteDepth   = clamp((out.fluteDepth ?? 0.0) + mrng.signed() * 0.14 * v, 0.0, 0.50);
        out.waistPinch   = clamp((out.waistPinch ?? 0.0) + mrng.signed() * 0.25 * v, -0.20, 0.50);
        out.twistDeg     = clamp((out.twistDeg ?? 14) + mrng.signed() * 28 * v, -70, 70);
        out.bendX        = clamp((out.bendX ?? 0.10) + mrng.signed() * 0.20 * v, -0.35, 0.35);
    }
    else if (rockType === 'hoodoo_column')
    {
        if (v > 0.45 && mrng.float() < v * 0.70)
        {
            const styles = ['bulb_cap', 'turret', 'totem', 'fin_spire'];
            out.hoodooStyle = styles[mrng.int(0, styles.length)];
        }
        out.ringCount  = clamp(Math.round((out.ringCount ?? 5) + mrng.signed() * 2.5 * v), 2, 8);
        out.ringSwell  = clamp((out.ringSwell ?? 0.22) + mrng.signed() * 0.12 * v, 0.06, 0.42);
        out.neckPinch  = clamp((out.neckPinch ?? 0.36) + mrng.signed() * 0.16 * v, 0.12, 0.60);
        out.capSize    = clamp((out.capSize ?? 1.12) + mrng.signed() * 0.26 * v, 0.70, 1.50);
        out.taper      = clamp((out.taper ?? 0.24) + mrng.signed() * 0.22 * v, -0.10, 0.62);
        out.leanX      = clamp((out.leanX ?? 0.06) + mrng.signed() * 0.16 * v, -0.30, 0.30);
        out.roundness  = clamp((out.roundness ?? 0.30) + mrng.signed() * 0.18 * v, 0.08, 0.68);
    }
    else if (rockType === 'pedestal_rock')
    {
        if (v > 0.50 && mrng.float() < v * 0.65)
        {
            const styles = ['balanced_boulder', 'desert_mushroom', 'caprock_table', 'anvil_prow'];
            out.pedestalStyle = styles[mrng.int(0, styles.length)];
        }
        out.stemHeightRatio = clamp((out.stemHeightRatio ?? 0.48) + mrng.signed() * 0.12 * v, 0.30, 0.68);
        out.neckWidth       = clamp((out.neckWidth ?? 0.36) + mrng.signed() * 0.14 * v, 0.20, 0.65);
        out.capOverhang     = clamp((out.capOverhang ?? 1.22) + mrng.signed() * 0.26 * v, 0.85, 1.65);
        out.capTiltDeg      = clamp((out.capTiltDeg ?? 14) + mrng.signed() * 18 * v, -32, 32);
        out.capOffset       = clamp((out.capOffset ?? 0.08) + mrng.signed() * 0.20 * v, -0.35, 0.35);
    }
    else if (rockType === 'floating_rock')
    {
        if (v > 0.50 && mrng.float() < v * 0.60)
        {
            const styles = ['sky_island', 'torn_monolith', 'stalactite_dagger', 'hover_ledge'];
            out.floatingStyle = styles[mrng.int(0, styles.length)];
        }
        out.hoverHeight   = Math.max(0.15, (out.hoverHeight ?? 2.4) + mrng.signed() * 1.15 * v);
        out.keelRatio     = clamp((out.keelRatio ?? 0.62) + mrng.signed() * 0.14 * v, 0.35, 0.80);
        out.keelSharpness = clamp((out.keelSharpness ?? 0.74) + mrng.signed() * 0.18 * v, 0.25, 0.94);
        out.rimOverhang   = clamp((out.rimOverhang ?? 0.32) + mrng.signed() * 0.18 * v, 0.05, 0.65);
        out.keelSkew      = clamp((out.keelSkew ?? 0.10) + mrng.signed() * 0.25 * v, -0.42, 0.42);
    }
    else if (rockType === 'basalt_column')
    {
        if (v > 0.30 && Number(out.polygonSides ?? 6) === 0) out.polygonSides = [5, 6, 6, 7][mrng.int(0, 4)];
        else if (v > 0.55) out.polygonSides = [5, 6, 6, 7][mrng.int(0, 4)];
        out.irregularity = clamp((out.irregularity ?? 0.08) + mrng.signed() * 0.08 * v, 0.02, 0.26);
        out.capTilt      = clamp((out.capTilt ?? 0.10) + mrng.signed() * 0.12 * v, 0.0, 0.38);
        if (Math.abs(out.socketDish ?? 0.0) > 0.02)
        {
            out.socketDish = clamp((out.socketDish ?? 0.0) + mrng.signed() * 0.14 * v, -0.45, 0.45);
        }
    }
    else if (rockType === 'crystal_prism')
    {
        out.apexHeightRatio = clamp((out.apexHeightRatio ?? 0.34) + mrng.signed() * 0.12 * v, 0.12, 0.68);
        out.faceAsymmetry   = clamp((out.faceAsymmetry ?? 0.18) + mrng.signed() * 0.18 * v, 0.02, 0.48);
        out.shaftTaper      = clamp((out.shaftTaper ?? 0.06) + mrng.signed() * 0.08 * v, -0.10, 0.35);
    }
    else if (rockType === 'linear_dike')
    {
        out.crestUndulation = clamp((out.crestUndulation ?? 0.28) + mrng.signed() * 0.18 * v, 0.04, 0.62);
        out.strikeBow       = clamp((out.strikeBow ?? 0.08) + mrng.signed() * 0.16 * v, -0.40, 0.40);
        out.wallTaper       = clamp((out.wallTaper ?? 0.26) + mrng.signed() * 0.14 * v, -0.15, 0.65);
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
    const pinchHeightNorm = clamp(opts.pinchHeightNorm ?? 0.34, 0.04, 0.94);
    const ringFreq = clamp(opts.ringFreq ?? 0.0, 0.0, 12.0);
    const ringAmp = clamp(opts.ringAmp ?? 0.0, 0.0, 0.48);
    const ringPhase = opts.ringPhase ?? 0.0;
    const twistRad = (opts.twistDeg ?? 0.0) * DEG2RAD;
    const bendX = clamp(opts.bendX ?? 0.0, -0.75, 0.75);
    const bendZ = clamp(opts.bendZ ?? 0.0, -0.75, 0.75);
    const skewX = clamp(opts.skewX ?? 0.0, -0.75, 0.75);

    const flareScale = (taper < 0 ? (1.0 + Math.abs(taper) * 0.85) : 1.08)
                     * (pinch < 0 ? (1.0 + Math.abs(pinch) * 0.65) : 1.02)
                     * (ringAmp > 0 ? (1.0 + ringAmp * 1.18) : 1.0);
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

    const hasDeform = Math.abs(taper) > 1e-4 || Math.abs(pinch) > 1e-4 || ringAmp > 1e-4
                   || Math.abs(twistRad) > 1e-4 || Math.abs(bendX) > 1e-4 || Math.abs(bendZ) > 1e-4 || Math.abs(skewX) > 1e-4;
    const deformStrength = Math.abs(taper) * 0.35 + Math.abs(pinch) * 0.45
                         + ringAmp * Math.max(0.85, ringFreq * 0.26)
                         + Math.abs(twistRad) * 0.20
                         + (Math.abs(bendX) + Math.abs(bendZ) + Math.abs(skewX)) * 0.40;
    const deformPenalty = hasDeform ? clamp(1.0 / (1.0 + deformStrength), 0.42, 1.0) : 1.0;

    return {
        cx, cy, cz, radius,
        minX: wMinX, minY: wMinY, minZ: wMinZ,
        maxX: wMaxX, maxY: wMaxY, maxZ: wMaxZ,
        invR, invT,
        planes, numPlanes,
        kLse, baseBevel,
        sqRx, sqRy, sqRz, sqCy,
        sqWeight, sqExpH, sqExpV,
        primOp: opts.primOp ?? -1.0,
        sharpBase: opts.sharpBase ? 1.0 : 0.0,
        flatBaseCut: clamp(opts.flatBaseCut ?? 0.0, 0.0, 1.0),
        baseY, refH,
        taper, pinch, pinchHeightNorm,
        ringFreq, ringAmp, ringPhase,
        twistRad, bendX, bendZ, skewX,
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
    const basalUndercut = clamp(p.basalUndercut ?? 0.0, 0.0, 0.62);
    const crestSlope = p.crestSlope ?? 0.08;

    let overhang = p.overhangLean ?? 0.08;
    let taper = p.batterTaper ?? 0.14;
    let pinch = basalUndercut;

    if (profile === 'overhang')
    {
        overhang = Math.max(overhang, 0.32);
        taper = Math.min(taper, -0.14);
    }
    else if (profile === 'rampart')
    {
        overhang = Math.min(overhang, -0.24);
        taper = Math.max(taper, 0.30);
    }
    else if (profile === 'prow')
    {
        overhang += 0.18;
    }

    // Saharan Wind-Carved Yardang Fin: aerodynamic prow nose at +Z, tapered leeward dorsal spine at -Z, aeolian foot undercut
    if (profile === 'yardang_fin')
    {
        const finRx = rx * 0.72;
        const finRz = rz * 1.18;
        const bound = Math.max(finRx, finRz, h) * 1.8;
        let poly = createBoxPoly(-bound, 0.0, -bound, bound, h * 1.08, bound, 0);

        //Sharp converging windward prow (+Z) + streamlined side flanks + tapered leeward tail (-Z)
        const prowSharp = 0.52 + prow * 0.38;
        poly = clipPolyByHalfSpace(poly, vnorm([ prowSharp, -0.06, 0.72]), finRx * 0.92 + finRz * 0.36, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([-prowSharp, -0.06, 0.72]), finRx * 0.92 + finRz * 0.36, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([ 0.96, 0.14, -0.26]), finRx * 0.84, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([-0.96, 0.14, -0.26]), finRx * 0.84, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([ 0.0,  0.34, -0.94]), finRz * 0.95, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([ 0.0,  0.12,  0.99]), finRz * 1.04, 0);

        // Pitched dorsal ridge spine
        const ridgeTilt = crestSlope * 0.35;
        poly = clipPolyByHalfSpace(poly, vnorm([ 0.42 - ridgeTilt, 0.90, -0.12]), h * 0.88, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([-0.42 - ridgeTilt, 0.90, -0.12]), h * 0.88, 0);

        const extra = Math.max(0, facets - 8);
        for (let i = 0; i < extra; i++)
        {
            const a = rng.float() * Math.PI * 2;
            const n = vnorm([Math.cos(a) * 0.88, 0.18 + rng.float() * 0.30, Math.sin(a) * 0.65]);
            const d = Math.hypot(finRx * n[0], h * 0.55 * n[1], finRz * n[2]) * rng.range(1.02, 1.18);
            const next = clipPolyByHalfSpace(poly, n, d, 0);
            if (next) poly = next;
        }

        return compilePlainPrimitive({
            poly,
            R: worldR,
            pivot: worldPivot,
            roundness: clamp(roundness * 1.15, 0.03, 0.65),
            sharpBase: true,
            baseY: 0.0,
            refH: h,
            taper: taper * 0.65 + 0.12,
            pinch: clamp(0.28 + basalUndercut * 0.65, 0.18, 0.68),
            pinchHeightNorm: 0.16,
            bendZ: overhang + 0.16
        });
    }

    // Granite Exfoliation Half-Dome: smooth convex granite batholith back/crown + sheer Yosemite-style split headwall
    if (profile === 'exfoliation_dome')
    {
        const domeRound = clamp(0.52 + roundness * 0.65, 0.42, 0.94);
        const bf = buildStrikeDipFrame(rng.signed() * 0.16, 0.06);
        let domePoly = sculptGeologicalParentPoly(
            [0, h * 0.48, -rz * 0.08],
            [rx * 1.04, h * 0.56, rz * 1.08],
            bf,
            rng,
            {
                jointDispersion: 0.07 + prow * 0.10,
                cornerBlunt: 0.94,
                flatBaseY: 0.0,
                extraBevelCuts: Math.max(5, facets - 1)
            }
        );
        if (domePoly && Math.abs(crestSlope) > 0.01)
        {
            const capN = vnorm([-crestSlope * 0.36, 1.0, -0.05]);
            const next = clipPolyByHalfSpace(domePoly, capN, capN[1] * h * 0.97, 0);
            if (next) domePoly = next;
        }

        const domeRoot = compilePlainPrimitive({
            poly: domePoly,
            R: worldR,
            pivot: worldPivot,
            roundness: domeRound,
            sharpBase: true,
            flatBaseCut: 1.0,
            sqWeight: 0.68,
            sqRx: rx * 1.02, sqRy: h * 0.54, sqRz: rz * 1.06, sqCy: h * 0.46,
            sqExpH: 0.98, sqExpV: 0.96,
            primOp: -1.0,
            baseY: 0.0,
            refH: h,
            taper: clamp(taper * 0.55 + 0.20, -0.15, 0.58),
            pinch,
            pinchHeightNorm: 0.14,
            bendZ: overhang * 0.30
        });

        // Sheer glacial split headwall cutter across the +Z front (angled slightly so both the sheer face & arched dome shoulder read from front-3/4 view)
        const faceZ = rz * (0.28 - prow * 0.22);
        const cBound = Math.max(rx, rz, h) * 1.7;
        let cutPoly = createBoxPoly(-cBound, -0.25, faceZ - rz * 0.15, cBound, h * 1.25, faceZ + cBound, 0);
        const headwallN = vnorm([-0.14 - prow * 0.12, overhang * 0.24 - 0.06, -1.0]);
        cutPoly = clipPolyByHalfSpace(cutPoly, headwallN, -(faceZ) * headwallN[2] + headwallN[1] * h * 0.48, 0);

        const faceCutter = compilePlainPrimitive({
            poly: cutPoly,
            R: worldR,
            pivot: worldPivot,
            roundness: clamp(roundness * 0.30, 0.015, 0.14),
            sharpBase: false,
            primOp: -2.0 - clamp(0.025 + roundness * 0.07, 0.015, 0.11),
            baseY: 0.0,
            refH: h
        });

        return faceCutter ? [domeRoot, faceCutter] : domeRoot;
    }

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

    if (poly && (profile === 'mesa' || Math.abs(crestSlope) > 0.01))
    {
        const capN = vnorm([-crestSlope * 0.35, 1.0, rng.signed() * 0.04]);
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
        pinch,
        pinchHeightNorm: 0.15,
        bendZ: overhang
    });
}

function buildSingleBasaltColumn(p, worldR, worldPivot, rng)
{
    const style = p.basaltStyle || 'classic_hex';
    const fixedSides = Number(p.polygonSides ?? 6);
    const sides = clamp(fixedSides > 0 ? Math.round(fixedSides) : [5, 6, 6, 7][rng.int(0, 4)], 3, 12);
    const rx = Math.max(0.10, p.width * 0.5);
    const rz = Math.max(0.10, p.length * 0.5);
    const h = Math.max(0.25, p.height);
    const irreg = clamp(p.irregularity ?? 0.08, 0.0, 0.38);
    const bevelWeight = clamp(p.edgeBevel ?? 0.09, 0.01, 0.55);
    const capTilt = p.capTilt ?? 0.10;
    const socketDish = p.socketDish ?? (style === 'ball_socket' ? -0.22 : 0.0);
    const pinchBands = clamp(p.pinchBands ?? 0.0, 0.0, 0.35);
    const bendVal = (p.bendX ?? 0.0) + (style === 'entablature_splay' ? 0.24 : 0.0);

    const bound = Math.max(rx, rz, h) * 1.5;
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

    const parts = [];
    const mainPrim = compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness: bevelWeight * 0.55,
        sharpBase: true,
        baseY: 0.0,
        refH: h,
        taper: p.taper ?? 0.02,
        ringFreq: pinchBands > 0.005 ? 4.2 : 0.0,
        ringAmp: pinchBands,
        ringPhase: rng.float() * 6.28,
        bendX: bendVal,
        twistDeg: p.twistDeg ?? 0.0
    });
    if (!mainPrim) return null;
    parts.push(mainPrim);

    // Cross-jointed lower step buttress for `stepped_fracture` basalt columns
    if (style === 'stepped_fracture')
    {
        const stepH = h * (0.48 + rng.float() * 0.16);
        let stepPoly = createBoxPoly(-bound, 0.0, -bound, bound, stepH + bound * 0.5, bound, 0);
        const stepOffX = rx * 0.52, stepOffZ = rz * 0.30;
        for (let s = 0; s < sides; s++)
        {
            const ang = rot0 + (s * Math.PI * 2) / sides;
            const dx = Math.cos(ang), dz = Math.sin(ang);
            const rad = Math.hypot(rx * dx, rz * dz) * 0.88;
            const n = vnorm([dx, 0.0, dz]);
            stepPoly = clipPolyByHalfSpace(stepPoly, n, nxDot(n, stepOffX, stepOffZ) + rad, 0);
        }
        stepPoly = clipPolyByHalfSpace(stepPoly, [0, 1, 0], stepH, 0);
        const stepPrim = compilePlainPrimitive({
            poly: stepPoly,
            R: worldR,
            pivot: worldPivot,
            roundness: bevelWeight * 0.50,
            sharpBase: true,
            primOp: 0.03,
            baseY: 0.0,
            refH: h,
            bendX: bendVal
        });
        if (stepPrim) parts.push(stepPrim);
    }

    // Ball-and-Socket jointing at top of column: `< 0` carves a concave crater cup; `> 0` adds a convex ball dome
    if (Math.abs(socketDish) > 0.02)
    {
        const dishR = Math.min(rx, rz) * 0.78;
        const dishDepth = dishR * Math.abs(socketDish) * 0.95;
        const dishCenterY = socketDish < 0.0 ? (h + dishR * 0.42) : (h - dishR * 0.38);
        const dBound = dishR * 1.45;
        let dishPoly = createBoxPoly(-dBound, dishCenterY - dBound, -dBound, dBound, dishCenterY + dBound, dBound, 0);
        for (let s = 0; s < 8; s++)
        {
            const a = (s * Math.PI * 2) / 8;
            dishPoly = clipPolyByHalfSpace(dishPoly, [Math.cos(a), 0, Math.sin(a)], dishR, 0);
        }
        const dishPrim = compilePlainPrimitive({
            poly: dishPoly,
            R: worldR,
            pivot: worldPivot,
            roundness: 0.92,
            sqWeight: 0.95,
            sqRx: dishR,
            sqRy: dishDepth + dishR * 0.42,
            sqRz: dishR,
            sqCy: dishCenterY,
            sqExpH: 1.0,
            sqExpV: 1.0,
            primOp: socketDish < 0.0 ? (-2.0 - dishR * 0.14) : (dishR * 0.12),
            baseY: 0.0,
            refH: h,
            taper: p.taper ?? 0.02,
            bendX: bendVal,
            twistDeg: p.twistDeg ?? 0.0
        });
        if (dishPrim) parts.push(dishPrim);
    }

    return parts.length === 1 ? parts[0] : parts;
}

function nxDot(n, ox, oz)
{
    return n[0] * ox + n[2] * oz;
}

function buildSingleCrystalPrism(p, worldR, worldPivot, rng)
{
    const habit = p.crystalHabit || 'quartz_hex';
    let rx = Math.max(0.08, (p.width ?? 1.35) * 0.5);
    let rz = Math.max(0.08, (p.length ?? 1.35) * 0.5);
    const h = Math.max(0.25, p.height ?? 5.2);

    let sides = clamp(Math.round(p.prismSides ?? 6), 3, 12);
    let termStyle = p.terminationStyle || 'quartz_rz';
    let apexRatio = clamp(p.apexHeightRatio ?? 0.34, 0.08, 0.82);
    let botPoint = clamp(p.bottomPoint ?? 0.0, 0.0, 1.0);
    const asym = clamp(p.faceAsymmetry ?? 0.18, 0.0, 0.55);
    const edgeBevel = clamp(p.edgeBevel ?? 0.02, 0.005, 0.32);
    const shaftTaper = clamp(p.shaftTaper ?? 0.06, -0.25, 0.55);
    const leanX = clamp(p.leanX ?? 0.0, -0.50, 0.50);
    const satTwin = clamp(p.satelliteTwin ?? 0.0, 0.0, 1.0);

    if (habit === 'double_terminated')
    {
        botPoint = Math.max(botPoint, 0.85);
        apexRatio = Math.max(apexRatio, 0.30);
    }
    else if (habit === 'chisel_selenite')
    {
        rx *= 1.28;
        rz *= 0.54;
        if (!p.terminationStyle || p.terminationStyle === 'quartz_rz') termStyle = 'chisel_wedge';
    }
    else if (habit === 'fluorite_octa')
    {
        sides = 4;
        botPoint = Math.max(botPoint, 0.92);
        apexRatio = clamp(Math.max(apexRatio, 0.46), 0.32, 0.52);
        termStyle = 'pyramid';
    }
    else if (habit === 'tourmaline_trigonal')
    {
        sides = Math.max(3, p.prismSides === 6 ? 9 : sides);
        if (!p.terminationStyle || p.terminationStyle === 'quartz_rz') termStyle = 'pedion_bevel';
    }
    else if (habit === 'obelisk_shard')
    {
        if (!p.terminationStyle || p.terminationStyle === 'quartz_rz') termStyle = 'oblique_single';
    }

    const bound = Math.max(rx, rz, h) * 1.8;
    let poly = createBoxPoly(-bound, 0.0, -bound, bound, h + bound, bound, 0);
    const rot0 = habit === 'fluorite_octa' ? (Math.PI * 0.25) : (rng.float() * Math.PI * 2);

    // 1. Vertical Crystallographic Prism Miller Faces {10-10}
    const faceAngles = [];
    const faceRadii = [];
    for (let s = 0; s < sides; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / sides;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        // Tourmaline rounded-trigonal cross-section or natural crystal unequal face growth
        const trigMod = habit === 'tourmaline_trigonal' ? (1.0 + 0.14 * Math.cos(3.0 * (ang - rot0))) : 1.0;
        const altMod = (s % 2 === 0 ? 1.0 : (1.0 - asym * 0.22)) * (1.0 + rng.signed() * asym * 0.18);
        const rad = Math.hypot(rx * dx, rz * dz) * trigMod * altMod;
        faceAngles.push(ang);
        faceRadii.push(rad);
        if (habit !== 'fluorite_octa')
        {
            poly = clipPolyByHalfSpace(poly, [dx, 0.0, dz], rad, 0);
        }
    }

    // 2. Top Crystallographic Termination Faces {10-11} / {01-11}
    const shoulderY = h * (1.0 - apexRatio);
    const apexSpanY = Math.max(0.06, h - shoulderY);

    if (termStyle === 'chisel_wedge')
    {
        // Monoclinic 2-face razor chisel blade meeting along a sharp crest line at y = h
        for (const sgn of [-1, 1])
        {
            const a = rot0 + (sgn > 0 ? Math.PI * 0.5 : -Math.PI * 0.5);
            const dx = Math.cos(a), dz = Math.sin(a);
            const rSide = Math.hypot(rx * dx, rz * dz);
            const n = vnorm([apexSpanY * dx, rSide, apexSpanY * dz]);
            const dPlane = n[1] * h * (1.0 - (sgn > 0 ? 0.0 : asym * 0.08));
            poly = clipPolyByHalfSpace(poly, n, dPlane, 0);
        }
        // Subtle gable end bevels so the chisel edge has crisp mineral corners
        for (const sgnEnd of [-1, 1])
        {
            const aEnd = rot0 + (sgnEnd > 0 ? 0.0 : Math.PI);
            const dxE = Math.cos(aEnd), dzE = Math.sin(aEnd);
            const rEnd = Math.hypot(rx * dxE, rz * dzE);
            const nEnd = vnorm([apexSpanY * 0.45 * dxE, rEnd, apexSpanY * 0.45 * dzE]);
            poly = clipPolyByHalfSpace(poly, nEnd, nEnd[1] * h * 1.06, 0);
        }
    }
    else if (termStyle === 'oblique_single')
    {
        // Dramatic skewed single cleavage plane + secondary back bevel
        const aMain = rot0 + 0.35;
        const rMain = Math.max(rx, rz);
        const nMain = vnorm([Math.cos(aMain) * apexSpanY, rMain * 1.05, Math.sin(aMain) * apexSpanY]);
        poly = clipPolyByHalfSpace(poly, nMain, nMain[1] * h * 0.88, 0);
        const aBack = aMain + Math.PI;
        const nBack = vnorm([Math.cos(aBack) * apexSpanY * 0.55, rMain * 1.25, Math.sin(aBack) * apexSpanY * 0.55]);
        poly = clipPolyByHalfSpace(poly, nBack, nBack[1] * h * 0.98, 0);
    }
    else
    {
        // Multi-face pyramidal crown: `pyramid`, `quartz_rz` (alternating major r / minor z faces), or `pedion_bevel`
        for (let s = 0; s < sides; s++)
        {
            const ang = faceAngles[s];
            const dx = Math.cos(ang), dz = Math.sin(ang);
            const rFace = faceRadii[s];
            // In real α-quartz, alternating r and z rhombohedral faces sit slightly closer/farther, enlarging 3 dominant faces!
            const rzShift = (termStyle === 'quartz_rz')
                ? ((s % 2 === 0 ? -0.14 : 0.16) * asym * rFace)
                : (rng.signed() * 0.08 * asym * rFace);
            // Plane passes through prism shoulder (rFace * (dx,0,dz) + shoulderY * Y) and apex (0, h, 0)
            const n = vnorm([apexSpanY * dx, rFace, apexSpanY * dz]);
            const dPlane = n[1] * h + rzShift;
            poly = clipPolyByHalfSpace(poly, n, dPlane, 0);
        }
        if (termStyle === 'pedion_bevel')
        {
            // Flat hexagonal/trigonal pedion {0001} table slicing the pyramid tip cleanly
            poly = clipPolyByHalfSpace(poly, [0.0, 1.0, 0.0], h * (1.0 - apexRatio * 0.42), 0);
        }
    }

    // 3. Bottom Termination: `0.0` = planted base (`y >= 0`), `> 0` = double-terminated lower pyramid!
    if (botPoint > 0.04)
    {
        const botShoulderY = h * clamp(apexRatio * botPoint * 0.92, 0.05, 0.48);
        for (let s = 0; s < sides; s++)
        {
            const ang = faceAngles[s];
            const dx = Math.cos(ang), dz = Math.sin(ang);
            const rFace = faceRadii[s];
            const n = vnorm([botShoulderY * dx, -rFace, botShoulderY * dz]);
            // At y = 0, radius shrinks to (1 - botPoint) * rFace (0 = sharp point at y=0!)
            const tipR = (1.0 - botPoint) * rFace;
            const dPlane = botShoulderY * tipR / Math.hypot(botShoulderY, rFace);
            poly = clipPolyByHalfSpace(poly, n, dPlane, 0);
        }
    }

    const parts = [];
    const mainCrystal = compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness: edgeBevel,
        sharpBase: botPoint <= 0.04,
        baseY: 0.0,
        refH: h,
        taper: shaftTaper,
        bendX: leanX
    });
    if (!mainCrystal) return null;
    parts.push(mainCrystal);

    // 4. Optional Penetrating Satellite Crystal Twin Spur sprouting out of the lower shaft!
    if (satTwin > 0.04)
    {
        const twinScale = 0.38 + 0.34 * satTwin;
        const twinH = h * (0.42 + 0.26 * satTwin);
        const twinRx = rx * twinScale, twinRz = rz * twinScale;
        const twinTiltRad = (28 + 26 * satTwin) * DEG2RAD;
        const twinAz = rot0 + Math.PI * 0.33;
        const tR = mulMat3(
            worldR,
            buildRock2EulerMatrix3(twinAz / DEG2RAD, Math.cos(twinAz) * (twinTiltRad / DEG2RAD), -Math.sin(twinAz) * (twinTiltRad / DEG2RAD))
        );
        const tBaseLocal = [Math.cos(twinAz) * rx * 0.32, h * 0.08, Math.sin(twinAz) * rz * 0.32];
        const tPivot = forwardRock2Point(worldR, worldPivot, tBaseLocal[0], tBaseLocal[1], tBaseLocal[2]);

        let tPoly = createBoxPoly(-bound, 0.0, -bound, bound, twinH + bound * 0.5, bound, 0);
        for (let s = 0; s < sides; s++)
        {
            const a = (s * Math.PI * 2) / sides;
            const dx = Math.cos(a), dz = Math.sin(a);
            const rT = Math.hypot(twinRx * dx, twinRz * dz);
            tPoly = clipPolyByHalfSpace(tPoly, [dx, 0, dz], rT, 0);
            const tSpan = twinH * apexRatio;
            const nTop = vnorm([tSpan * dx, rT, tSpan * dz]);
            tPoly = clipPolyByHalfSpace(tPoly, nTop, nTop[1] * twinH, 0);
        }
        const twinPrim = compilePlainPrimitive({
            poly: tPoly,
            R: tR,
            pivot: tPivot,
            roundness: edgeBevel,
            sharpBase: true,
            primOp: edgeBevel * 0.5,
            baseY: 0.0,
            refH: twinH,
            taper: shaftTaper
        });
        if (twinPrim) parts.push(twinPrim);
    }

    return parts.length === 1 ? parts[0] : parts;
}

function buildSingleLinearDike(p, worldR, worldPivot, rng)
{
    const style = p.linearStyle || 'volcanic_dike';
    const fullW = Math.max(0.5, p.width ?? 8.6);   // Strike length along local X
    let h = Math.max(0.35, p.height ?? 4.4);        // Ridge height along local Y
    let halfThick = Math.max(0.12, (p.length ?? 1.55) * 0.5); // Wall thickness along local Z

    let dipTiltDeg = clamp(p.dipTiltDeg ?? 0.0, -55.0, 55.0);
    let crestSharp = clamp(p.crestSharpness ?? 0.62, 0.05, 0.95);
    const segCount = clamp(Math.round(p.spineSegments ?? 3), 1, 5);
    const crestUndul = clamp(p.crestUndulation ?? 0.28, 0.0, 0.65);
    let wallTaper = clamp(p.wallTaper ?? 0.26, -0.35, 0.78);
    const strikeBow = clamp(p.strikeBow ?? 0.08, -0.55, 0.55);
    const basalUndercut = clamp(p.basalUndercut ?? 0.0, 0.0, 0.55);
    let roundness = clamp(p.roundness ?? 0.14, 0.02, 0.75);

    if (style === 'hogback_flatiron')
    {
        if (Math.abs(dipTiltDeg) < 8.0) dipTiltDeg = 28.0;
        halfThick *= 1.25;
        crestSharp = Math.max(crestSharp, 0.58);
    }
    else if (style === 'sandstone_fin')
    {
        halfThick *= 0.78;
        roundness = Math.max(roundness, 0.26);
        crestSharp = Math.min(crestSharp, 0.48);
    }
    else if (style === 'knife_arete')
    {
        crestSharp = Math.max(crestSharp, 0.86);
        wallTaper = Math.max(wallTaper, 0.48);
        roundness = Math.min(roundness, 0.12);
    }
    else if (style === 'strike_rib')
    {
        h *= 0.46;
        halfThick *= 1.18;
    }

    const dipRad = dipTiltDeg * DEG2RAD;
    const dipShearZ = Math.tan(dipRad * 0.78);
    const parts = [];
    const halfSpanX = fullW * 0.5;
    const segSpan = fullW / segCount;

    for (let si = 0; si < segCount; si++)
    {
        const u0 = si / segCount;
        const u1 = (si + 1) / segCount;
        const uMid = (u0 + u1) * 0.5;
        const uSigned = uMid * 2.0 - 1.0; // -1 .. +1 along linear strike

        // Natural ridge envelope: tapers slightly toward strike ends unless single block, with serrated crest undulation
        const endEnv = segCount > 1 ? (1.0 - 0.22 * uSigned * uSigned) : 1.0;
        const serrate = style === 'crenellated_wall'
            ? ((si % 2 === 0 ? 1.0 : (1.0 - 0.34 * (0.35 + crestUndul))))
            : (1.0 + rng.signed() * crestUndul * 0.42);
        const segH = Math.max(0.28, h * endEnv * serrate);

        // Overlap adjacent strike segments by 18% so the fused dike forms one continuous geological wall with stepped joints!
        const overlapX = segCount > 1 ? segSpan * 0.18 : 0.0;
        const xMin = -halfSpanX + u0 * fullW - (si > 0 ? overlapX : 0.0);
        const xMax = -halfSpanX + u1 * fullW + (si < segCount - 1 ? overlapX : 0.0);
        const segHalfX = (xMax - xMin) * 0.5;
        const segCx = (xMin + xMax) * 0.5;

        // Plan-view strike bow offset in Z: parabolic arc along strike
        const zBow = strikeBow * halfSpanX * 0.35 * (1.0 - uSigned * uSigned);
        const zJitter = segCount > 1 ? rng.signed() * halfThick * 0.16 : 0.0;
        const segCz = zBow + zJitter;
        const segThick = halfThick * (1.0 + rng.signed() * 0.12);

        const bound = Math.max(segHalfX, segH, segThick) * 1.85;
        let poly = createBoxPoly(-bound, 0.0, -bound, bound, segH * 1.12, bound, 0);

        // Strike end faces (+/-X): sloped prow & tail terminations at the outer ends of the linear dike
        const leftSlopeY = si === 0 ? (0.28 + 0.22 * crestSharp) : (0.06 + Math.abs(rng.signed()) * 0.10);
        const rightSlopeY = si === segCount - 1 ? (0.28 + 0.22 * crestSharp) : (0.06 + Math.abs(rng.signed()) * 0.10);
        poly = clipPolyByHalfSpace(poly, vnorm([-1.0, leftSlopeY, rng.signed() * 0.12]), segHalfX, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([ 1.0, rightSlopeY, rng.signed() * 0.12]), segHalfX, 0);

        // Front & Back Strike Walls (+/-Z): exact height-coupled slope so the wall rises cleanly to `segH`!
        const topRidgeHalfZ = segThick * (1.0 - 0.88 * crestSharp);
        const symSlopeY = Math.max(0.02, (segThick - topRidgeHalfZ) / segH);
        const posZSlopeY = style === 'hogback_flatiron'
            ? Math.max(0.08, (segThick * (1.35 + 0.55 * crestSharp)) / segH)
            : symSlopeY;
        const negZSlopeY = style === 'hogback_flatiron'
            ? -0.04 * (segThick / Math.max(1.0, segH)) // Crisp steep scarp face on opposite side of the dip slope!
            : symSlopeY;

        poly = clipPolyByHalfSpace(poly, vnorm([rng.signed() * 0.04, posZSlopeY,  1.0]), segThick, 0);
        poly = clipPolyByHalfSpace(poly, vnorm([rng.signed() * 0.04, negZSlopeY, -1.0]), segThick, 0);

        // Crest ridge cap cut with deterministic alternating tooth serration controlled by `crestUndulation`
        const serrateTilt = ((si % 2 === 0 ? 1 : -1) * 0.22 + rng.signed() * 0.14) * crestUndul;
        const capN = vnorm([serrateTilt, 1.0, dipRad * 0.18]);
        const capY = segH * (1.0 - 0.10 * (1.0 - crestSharp));
        poly = clipPolyByHalfSpace(poly, capN, capN[1] * capY, 0);

        // Subtle geological shoulder bevel facets so every dike segment catches directional sunlight crisply
        for (const sgnX of [-1, 1])
        {
            for (const sgnZ of [-1, 1])
            {
                const cn = vnorm([sgnX * 0.52, 0.24, sgnZ * 0.82]);
                const cDist = Math.abs(cn[0]) * segHalfX + Math.abs(cn[2]) * segThick + cn[1] * segH * 0.45;
                poly = clipPolyByHalfSpace(poly, cn, cDist * rng.range(0.88, 0.98), 0);
            }
        }

        const segPivot = forwardRock2Point(worldR, worldPivot, segCx, 0.0, segCz);
        const bowYawDeg = -strikeBow * uSigned * 22.0;
        const segR = Math.abs(bowYawDeg) > 0.05
            ? mulMat3(worldR, buildRock2EulerMatrix3(bowYawDeg, 0, 0))
            : worldR;

        const segPrim = compilePlainPrimitive({
            poly,
            R: segR,
            pivot: segPivot,
            roundness,
            sharpBase: true,
            sqWeight: style === 'sandstone_fin' ? 0.34 : 0.08,
            sqRx: segHalfX, sqRy: segH * 0.52, sqRz: segThick, sqCy: segH * 0.48,
            sqExpH: 0.75, sqExpV: 0.68,
            primOp: si === 0 ? -1.0 : (0.06 + roundness * 0.18),
            baseY: 0.0,
            refH: segH,
            taper: wallTaper,
            pinch: basalUndercut * 0.72,
            pinchHeightNorm: 0.16,
            bendZ: dipShearZ * 0.48
        });
        if (segPrim) parts.push(segPrim);
    }

    if (parts.length === 0) return null;
    return parts.length === 1 ? parts[0] : parts;
}

function buildSingleSpirePinnacle(p, worldR, worldPivot, rng)
{
    const rx = p.width * 0.5;
    const h = p.height;
    const rz = p.length * 0.5;
    const style = p.spireStyle || 'horn';
    const sides = clamp(Math.round(p.facetSides ?? 7), 4, 14);
    let roundness = p.roundness ?? 0.16;

    let taper = p.taper ?? 0.58;
    let pinch = p.waistPinch ?? 0.0;
    let flatCap = p.flatCap ?? 0.08;
    let bendX = p.bendX ?? 0.10;
    const twistDeg = p.twistDeg ?? 14;
    const userRibs = clamp(Math.round(p.buttressRibs ?? 0), 0, 5);
    const userFlute = clamp(p.fluteDepth ?? 0.0, 0.0, 0.55);

    if (style === 'needle') { taper = Math.max(taper, 0.72); flatCap = Math.min(flatCap, 0.04); }
    else if (style === 'karst_razor') { taper = Math.max(taper, 0.68); roundness = Math.min(roundness, 0.12); flatCap *= 0.35; }
    else if (style === 'cathedral_organ') { taper = Math.max(taper, 0.52); roundness = Math.min(roundness, 0.22); }
    else if (style === 'hoodoo') { pinch = Math.max(pinch, 0.42); taper = Math.min(taper, 0.05); flatCap = Math.max(flatCap, 0.22); }
    else if (style === 'butte') { taper = clamp(taper * 0.4, 0.05, 0.32); flatCap = Math.max(flatCap, 0.48); bendX *= 0.25; }

    const ribCount = style === 'cathedral_organ' ? (2 + userRibs) : userRibs;
    const fluteAmt = style === 'karst_razor' ? clamp(0.24 + userFlute * 0.72, 0.0, 0.68) : userFlute;

    const bound = Math.max(rx, rz, h) * 1.5;
    let poly = createBoxPoly(-bound, 0.0, -bound, bound, h * 1.15, bound, 0);
    const rot0 = rng.float() * Math.PI * 2;
    const coreScale = ribCount > 0 ? 0.80 : 1.0;

    for (let s = 0; s < sides; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / sides + rng.signed() * 0.10;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const aspect = style === 'karst_razor' ? (0.72 + 0.48 * Math.abs(dx)) : 1.0;
        const rad = Math.hypot(rx * dx, rz * dz) * coreScale * aspect * rng.range(0.88, 1.08);
        const n = vnorm([dx, rng.signed() * 0.06, dz]);
        poly = clipPolyByHalfSpace(poly, n, rad, 0);
    }

    if (flatCap > 0.14)
    {
        const capN = vnorm([rng.signed() * 0.10, 1.0, rng.signed() * 0.10]);
        poly = clipPolyByHalfSpace(poly, capN, h * (1.0 - 0.22 * flatCap), 0);
    }
    else
    {
        const tipScale = 1.0 - 0.28 * flatCap;
        const slopeH = style === 'karst_razor' ? 0.68 : 0.55;
        for (let k = 0; k < 4; k++)
        {
            const ang = rot0 + 0.4 + k * (Math.PI * 0.5);
            const n = vnorm([Math.cos(ang) * slopeH, 0.84, Math.sin(ang) * slopeH]);
            poly = clipPolyByHalfSpace(poly, n, h * 0.86 * tipScale, 0);
        }
    }

    const corePrim = compilePlainPrimitive({
        poly,
        R: worldR,
        pivot: worldPivot,
        roundness,
        sharpBase: true,
        sqWeight: style === 'hoodoo' ? 0.35 * roundness : 0.0,
        sqRx: rx, sqRy: h * 0.5, sqRz: rz, sqCy: h * 0.5,
        sqExpH: 0.90, sqExpV: 0.75,
        primOp: -1.0,
        baseY: 0.0,
        refH: h,
        taper,
        pinch,
        twistDeg,
        bendX
    });
    if (!corePrim) return null;

    const parts = [corePrim];

    // Clustered vertical cathedral organ-pipe buttress ribs
    if (ribCount > 0)
    {
        const ribBlend = Math.min(rx, rz) * (0.06 + 0.14 * roundness);
        for (let r = 0; r < ribCount; r++)
        {
            const rAng = rot0 + (r * Math.PI * 2) / ribCount + rng.signed() * 0.14;
            const dx = Math.cos(rAng), dz = Math.sin(rAng);
            const dist = 0.56 + rng.float() * 0.14;
            const ox = dx * rx * dist;
            const oz = dz * rz * dist;
            const ribH = h * (0.48 + ((r * 37) % 5) * 0.075 + rng.float() * 0.06);
            const ribRx = rx * rng.range(0.32, 0.42);
            const ribRz = rz * rng.range(0.32, 0.42);

            const rBound = Math.max(ribRx, ribRz) * 1.8;
            let rPoly = createBoxPoly(ox - rBound, 0.0, oz - rBound, ox + rBound, ribH * 1.1, oz + rBound, 0);
            const rSides = 6;
            for (let s = 0; s < rSides; s++)
            {
                const a = rAng + (s * Math.PI * 2) / rSides;
                const nx = Math.cos(a), nz = Math.sin(a);
                const rr = Math.hypot(ribRx * nx, ribRz * nz);
                rPoly = clipPolyByHalfSpace(rPoly, [nx, 0, nz], nx * ox + nz * oz + rr, 0);
            }
            // Steep gothic gable cap on each organ pipe
            const capN = vnorm([dx * 0.48, 0.88, dz * 0.48]);
            rPoly = clipPolyByHalfSpace(rPoly, capN, capN[0] * ox + capN[1] * ribH * 0.94 + capN[2] * oz, 0);

            const ribPrim = compilePlainPrimitive({
                poly: rPoly,
                R: worldR,
                pivot: worldPivot,
                roundness: roundness * 0.75,
                sharpBase: true,
                primOp: ribBlend,
                baseY: 0.0,
                refH: h,
                taper: taper * 0.72,
                pinch: pinch * 0.5,
                twistDeg,
                bendX
            });
            if (ribPrim) parts.push(ribPrim);
        }
    }

    // Vertical concave Tsingy karst dissolution flute channels (built in spire-local space so taper/twist/bend track the shaft 1:1!)
    if (fluteAmt > 0.008)
    {
        const fluteCount = clamp(Math.round(sides * 0.65), 3, 5);
        const minR = Math.min(rx, rz);
        const fluteBevel = minR * (0.05 + 0.09 * roundness);
        for (let f = 0; f < fluteCount; f++)
        {
            const fAng = rot0 + (Math.PI / fluteCount) + (f * Math.PI * 2) / fluteCount;
            const dx = Math.cos(fAng), dz = Math.sin(fAng);
            const surfR = Math.hypot(rx * dx, rz * dz) * coreScale;
            const fRad = minR * (0.18 + 0.12 * fluteAmt);
            const cx = dx * (surfR + fRad * (0.74 - 0.56 * fluteAmt));
            const cz = dz * (surfR + fRad * (0.74 - 0.56 * fluteAmt));

            const fb = fRad * 1.6;
            let fPoly = createBoxPoly(cx - fb, h * 0.04, cz - fb, cx + fb, h * 0.84, cz + fb, 0);
            for (let s = 0; s < 6; s++)
            {
                const a = fAng + (s * Math.PI * 2) / 6;
                const nx = Math.cos(a), nz = Math.sin(a);
                fPoly = clipPolyByHalfSpace(fPoly, [nx, 0, nz], nx * cx + nz * cz + fRad, 0);
            }
            // Tapered top & bottom terminations so flutes emerge and fade smoothly along the rock face
            fPoly = clipPolyByHalfSpace(fPoly, vnorm([-dx * 0.55, 0.84, -dz * 0.55]), (-dx * 0.55) * cx + 0.84 * (h * 0.82) + (-dz * 0.55) * cz, 0);

            const flutePrim = compilePlainPrimitive({
                poly: fPoly,
                R: worldR,
                pivot: worldPivot,
                roundness: 0.72,
                sharpBase: false,
                primOp: -2.0 - fluteBevel,
                baseY: 0.0,
                refH: h,
                taper,
                pinch,
                twistDeg,
                bendX
            });
            if (flutePrim) parts.push(flutePrim);
        }
    }

    return parts.length === 1 ? parts[0] : parts;
}

function buildSingleHoodooColumn(p, worldR, worldPivot, rng)
{
    const style = p.hoodooStyle || 'bulb_cap';
    const h = Math.max(0.4, p.height ?? 6.4);
    const rx = Math.max(0.12, (p.width ?? 2.2) * 0.5) * (style === 'fin_spire' ? 0.72 : 1.0);
    const rz = Math.max(0.12, (p.length ?? 2.1) * 0.5) * (style === 'fin_spire' ? 1.24 : 1.0);
    const ringCount = clamp(Math.round(p.ringCount ?? 5), 2, 9);
    const ringSwell = clamp(p.ringSwell ?? 0.22, 0.0, 0.44);
    const neckPinch = clamp(p.neckPinch ?? 0.36, 0.0, 0.65);
    const capSize = clamp(p.capSize ?? 1.12, 0.55, 1.60);
    const taper = clamp(p.taper ?? 0.24, -0.25, 0.75);
    const basalApron = clamp(p.basalApron ?? 0.34, 0.0, 0.80);
    const leanX = clamp(p.leanX ?? 0.06, -0.40, 0.40);
    const roundness = clamp(p.roundness ?? 0.30, 0.04, 0.80);

    const parts = [];
    const rot0 = rng.float() * Math.PI * 2;

    // 1. Continuous fluted badlands shaft from y = 0 up into the crown (guarantees zero detached floating caps!)
    const shaftTop = h * 0.90;
    const sBound = Math.max(rx, rz, h) * 1.5;
    let shaftPoly = createBoxPoly(-sBound, 0.0, -sBound, sBound, shaftTop, sBound, 0);
    const shaftSides = style === 'turret' ? 8 : 9;
    for (let s = 0; s < shaftSides; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / shaftSides + rng.signed() * 0.08;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const rad = Math.hypot(rx * dx, rz * dz) * rng.range(0.86, 1.04);
        shaftPoly = clipPolyByHalfSpace(shaftPoly, vnorm([dx, rng.signed() * 0.04, dz]), rad, 0);
    }

    const ringFreq = (style === 'totem' ? ringCount * 0.72 : ringCount * 0.54);
    const ringAmp = style === 'totem' ? Math.min(0.44, ringSwell * 1.22 + 0.03) : ringSwell;
    const shaftPrim = compilePlainPrimitive({
        poly: shaftPoly,
        R: worldR,
        pivot: worldPivot,
        roundness,
        sharpBase: true,
        sqWeight: clamp(0.18 + roundness * 0.42, 0.08, 0.62),
        sqRx: rx * 0.92, sqRy: shaftTop * 0.5, sqRz: rz * 0.92, sqCy: shaftTop * 0.5,
        sqExpH: 0.94, sqExpV: 0.45,
        primOp: -1.0,
        baseY: 0.0,
        refH: h,
        taper,
        pinch: neckPinch,
        pinchHeightNorm: 0.79,
        ringFreq,
        ringAmp,
        ringPhase: 0.55 + rng.float() * 1.4,
        bendX: leanX
    });
    if (!shaftPrim) return null;
    parts.push(shaftPrim);

    // 2. Flared grounded badlands mudstone apron at the base (y = 0 .. apronH)
    if (basalApron > 0.005)
    {
        const apronH = h * (0.14 + 0.16 * basalApron);
        const apRx = rx * (1.06 + 0.58 * basalApron);
        const apRz = rz * (1.06 + 0.58 * basalApron);
        const bfAp = buildStrikeDipFrame(rot0, 0.12);
        const apPoly = sculptGeologicalParentPoly(
            [0, apronH * 0.46, 0],
            [apRx, apronH * 0.54, apRz],
            bfAp,
            rng,
            {
                jointDispersion: 0.16,
                cornerBlunt: 0.74 + 0.20 * roundness,
                flatBaseY: 0.0,
                extraBevelCuts: 5
            }
        );
        const apPrim = compilePlainPrimitive({
            poly: apPoly,
            R: worldR,
            pivot: worldPivot,
            roundness: clamp(roundness * 1.1, 0.06, 0.78),
            sharpBase: true,
            primOp: h * 0.045,
            baseY: 0.0,
            refH: apronH,
            taper: 0.48
        });
        if (apPrim) parts.push(apPrim);
    }

    // 3. Sculpted multi-faceted weathered hoodoo crown (tracks shaft leanX at crown height!)
    const crownBaseFrac = style === 'fin_spire' ? 0.74 : 0.77;
    const crownCy = h * (0.5 * (crownBaseFrac + 1.0));
    const crownRy = h * (0.5 * (1.0 - crownBaseFrac) + 0.03);
    const topWidthScale = Math.max(0.38, 1.0 - taper * 0.45) * capSize;
    const crownRx = rx * topWidthScale * (style === 'fin_spire' ? 0.82 : 1.04);
    const crownRz = rz * topWidthScale * (style === 'fin_spire' ? 1.12 : 1.02);

    const hNormCrown = crownCy / h;
    const crownShiftX = leanX * hNormCrown * hNormCrown * h;
    const crownPivot = forwardRock2Point(worldR, worldPivot, crownShiftX, crownCy, 0.0);
    const crownTiltR = mulMat3(worldR, buildRock2EulerMatrix3(0, rng.signed() * 5, -leanX * 16));

    const bfCrown = buildStrikeDipFrame(rot0 + 0.4, style === 'turret' ? 0.08 : 0.24);
    let crownPoly = sculptGeologicalParentPoly(
        [0, 0, 0],
        [crownRx, crownRy, crownRz],
        bfCrown,
        rng,
        {
            jointDispersion: style === 'turret' ? 0.12 : 0.22,
            cornerBlunt: style === 'bulb_cap' ? 0.88 : 0.70,
            flatBaseY: null,
            extraBevelCuts: 7
        }
    );

    // Pitched weathered roof facets so hoodoo crowns never look like flat square mortarboards
    if (crownPoly)
    {
        const roofSlope = style === 'turret' ? 0.24 : (style === 'fin_spire' ? 0.62 : 0.40);
        for (let k = 0; k < 3; k++)
        {
            const a = rot0 + k * 2.094 + rng.signed() * 0.2;
            const n = vnorm([Math.cos(a) * roofSlope, 0.92, Math.sin(a) * roofSlope]);
            const next = clipPolyByHalfSpace(crownPoly, n, crownRy * (style === 'fin_spire' ? 0.76 : 0.86), 0);
            if (next) crownPoly = next;
        }
    }

    const crownBlend = style === 'turret'
        ? h * 0.016
        : h * (0.028 + 0.045 * roundness);

    const crownPrim = compilePlainPrimitive({
        poly: crownPoly,
        R: crownTiltR,
        pivot: crownPivot,
        roundness: style === 'turret' ? roundness * 0.65 : roundness,
        sharpBase: false,
        sqWeight: (style === 'bulb_cap' || style === 'totem') ? clamp(0.32 + roundness * 0.35, 0.15, 0.68) : 0.0,
        sqRx: crownRx * 0.95, sqRy: crownRy * 0.92, sqRz: crownRz * 0.95, sqCy: 0.0,
        sqExpH: 0.94, sqExpV: 0.88,
        primOp: crownBlend,
        baseY: -crownRy,
        refH: crownRy * 2.0,
        taper: style === 'fin_spire' ? 0.42 : (style === 'turret' ? -0.10 : 0.12)
    });
    if (crownPrim) parts.push(crownPrim);

    return parts;
}

function buildSinglePedestalRock(p, worldR, worldPivot, rng)
{
    const style = p.pedestalStyle || 'balanced_boulder';
    const h = Math.max(0.4, p.height ?? 5.2);
    const rx = Math.max(0.14, (p.width ?? 3.4) * 0.5);
    const rz = Math.max(0.14, (p.length ?? 3.2) * 0.5);

    const stemRatio = clamp(p.stemHeightRatio ?? 0.48, 0.25, 0.74);
    const neckWidth = clamp(p.neckWidth ?? 0.36, 0.18, 0.80);
    const capOverhang = clamp(p.capOverhang ?? 1.22, 0.70, 1.75);
    const capTiltDeg = clamp(p.capTiltDeg ?? 14, -36, 36);
    const capOffset = clamp(p.capOffset ?? 0.08, -0.45, 0.45);
    const stemRibs = clamp(p.stemRibs ?? 0.14, 0.0, 0.38);
    const plinthFlare = clamp(p.plinthFlare ?? 0.38, 0.0, 0.85);
    const roundness = clamp(p.roundness ?? 0.26, 0.04, 0.85);

    const parts = [];
    const rot0 = rng.float() * Math.PI * 2;

    // 1. Concave-tapered mudstone pedestal stem (grounded at y = 0, penetrating slightly into the caprock)
    const stemTop = h * stemRatio;
    const stemH = stemTop + h * 0.07;
    const stemRx = rx * (0.58 + 0.28 * neckWidth);
    const stemRz = rz * (0.58 + 0.28 * neckWidth);
    const sBound = Math.max(stemRx, stemRz, stemH) * 1.5;
    let stemPoly = createBoxPoly(-sBound, 0.0, -sBound, sBound, stemH, sBound, 0);
    for (let s = 0; s < 8; s++)
    {
        const ang = rot0 + (s * Math.PI * 2) / 8 + rng.signed() * 0.08;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const rad = Math.hypot(stemRx * dx, stemRz * dz) * rng.range(0.90, 1.06);
        stemPoly = clipPolyByHalfSpace(stemPoly, [dx, 0, dz], rad, 0);
    }

    const stemPrim = compilePlainPrimitive({
        poly: stemPoly,
        R: worldR,
        pivot: worldPivot,
        roundness: clamp(0.22 + roundness * 0.55, 0.08, 0.80),
        sharpBase: true,
        sqWeight: style === 'desert_mushroom' ? 0.55 : 0.25,
        sqRx: stemRx * 0.94, sqRy: stemH * 0.5, sqRz: stemRz * 0.94, sqCy: stemH * 0.5,
        sqExpH: 0.95, sqExpV: 0.42,
        primOp: -1.0,
        baseY: 0.0,
        refH: stemH,
        taper: clamp(0.45 * (1.0 - neckWidth), 0.04, 0.45),
        pinch: clamp((1.0 - neckWidth) * 0.68, 0.06, 0.62),
        pinchHeightNorm: 0.84,
        ringFreq: 3.6,
        ringAmp: stemRibs,
        ringPhase: 0.8,
        skewX: capOffset * 0.28
    });
    if (!stemPrim) return null;
    parts.push(stemPrim);

    // 2. Stepped basal plinth flare
    if (plinthFlare > 0.005)
    {
        const plH = h * (0.12 + 0.12 * plinthFlare);
        const plRx = stemRx * (1.15 + 0.72 * plinthFlare);
        const plRz = stemRz * (1.15 + 0.72 * plinthFlare);
        const bfPl = buildStrikeDipFrame(rot0 + 0.3, 0.10);
        const plPoly = sculptGeologicalParentPoly(
            [0, plH * 0.46, 0],
            [plRx, plH * 0.55, plRz],
            bfPl,
            rng,
            {
                jointDispersion: 0.16,
                cornerBlunt: 0.72 + 0.20 * roundness,
                flatBaseY: 0.0,
                extraBevelCuts: 5
            }
        );
        const plPrim = compilePlainPrimitive({
            poly: plPoly,
            R: worldR,
            pivot: worldPivot,
            roundness: clamp(roundness * 0.95, 0.06, 0.75),
            sharpBase: true,
            primOp: h * 0.04,
            baseY: 0.0,
            refH: plH,
            taper: 0.42
        });
        if (plPrim) parts.push(plPrim);
    }

    // 3. Perched Balanced Boulder / Mushroom Dome / Caprock Table / Cantilevered Anvil Prow
    const capSpanY = Math.max(0.25, h - stemTop);
    let capRy = capSpanY * 0.54;
    let capRx = rx * capOverhang * 0.86;
    let capRz = rz * capOverhang * 0.86;
    let sqW = 0.0;
    let capBlend = h * 0.016;
    let capSkewX = 0.0;

    if (style === 'desert_mushroom')
    {
        capRy *= 0.86;
        capRx *= 1.06;
        capRz *= 1.06;
        sqW = clamp(0.55 + roundness * 0.38, 0.42, 0.92);
        capBlend = h * (0.055 + 0.04 * roundness);
    }
    else if (style === 'caprock_table')
    {
        capRy *= 0.62;
        capRx *= 1.18;
        capRz *= 1.14;
        capBlend = h * 0.020;
    }
    else if (style === 'anvil_prow')
    {
        capRx *= 1.24;
        capRz *= 0.82;
        capSkewX = 0.34;
        capBlend = h * 0.018;
    }

    const shiftX = capOffset * rx * 1.15 + (style === 'anvil_prow' ? rx * 0.22 : 0.0);
    const capCy = stemTop + capRy * 0.86;
    const capPivot = forwardRock2Point(worldR, worldPivot, shiftX, capCy, 0.0);
    const capR = mulMat3(worldR, buildRock2EulerMatrix3(0, rng.signed() * 4, -capTiltDeg));

    const bfCap = buildStrikeDipFrame(rot0 + 0.7, style === 'caprock_table' ? 0.05 : 0.24);
    let capPoly = sculptGeologicalParentPoly(
        [0, 0, 0],
        [capRx, capRy, capRz],
        bfCap,
        rng,
        {
            jointDispersion: style === 'caprock_table' ? 0.10 : 0.22,
            cornerBlunt: style === 'desert_mushroom' ? 0.92 : (0.68 + 0.24 * roundness),
            flatBaseY: null,
            extraBevelCuts: 8
        }
    );

    if (capPoly && style === 'caprock_table')
    {
        const nextTop = clipPolyByHalfSpace(capPoly, [0, 1, 0], capRy * 0.72, 0);
        if (nextTop) capPoly = nextTop;
        const nextBot = clipPolyByHalfSpace(capPoly, [0, -1, 0], capRy * 0.78, 0);
        if (nextBot) capPoly = nextBot;
    }

    const capPrim = compilePlainPrimitive({
        poly: capPoly,
        R: capR,
        pivot: capPivot,
        roundness: style === 'desert_mushroom' ? Math.max(roundness, 0.55) : roundness,
        sharpBase: false,
        sqWeight: sqW,
        sqRx: capRx * 0.96, sqRy: capRy * 0.94, sqRz: capRz * 0.96, sqCy: 0.0,
        sqExpH: 0.96, sqExpV: style === 'desert_mushroom' ? 0.88 : 0.72,
        primOp: capBlend,
        baseY: -capRy,
        refH: capRy * 2.0,
        taper: style === 'desert_mushroom' ? 0.28 : (style === 'caprock_table' ? -0.18 : 0.06),
        skewX: capSkewX
    });
    if (capPrim) parts.push(capPrim);

    return parts;
}

function buildSingleFloatingRock(p, worldR, worldPivot, rng)
{
    const style = p.floatingStyle || 'sky_island';
    const h = Math.max(0.45, p.height ?? 4.6);
    let rx = Math.max(0.15, (p.width ?? 4.8) * 0.5);
    let rz = Math.max(0.15, (p.length ?? 4.4) * 0.5);

    const hoverHeight = Math.max(0.0, p.hoverHeight ?? 2.4);
    let keelRatio = clamp(p.keelRatio ?? 0.62, 0.25, 0.82);
    let keelSharp = clamp(p.keelSharpness ?? 0.74, 0.10, 0.95);
    let topFlatness = clamp(p.topFlatness ?? 0.82, 0.0, 1.0);
    const rimOverhang = clamp(p.rimOverhang ?? 0.32, 0.0, 0.75);
    const keelSpurs = clamp(Math.round(p.keelSpurs ?? 3), 0, 4);
    const keelSkew = clamp(p.keelSkew ?? 0.10, -0.50, 0.50);
    const roundness = clamp(p.roundness ?? 0.18, 0.04, 0.72);

    if (style === 'torn_monolith')
    {
        topFlatness *= 0.32;
        keelSharp = clamp(keelSharp * 0.85, 0.15, 0.92);
    }
    else if (style === 'stalactite_dagger')
    {
        rx *= 0.76;
        rz *= 0.76;
        keelRatio = clamp(keelRatio + 0.12, 0.40, 0.86);
        keelSharp = Math.max(keelSharp, 0.84);
    }
    else if (style === 'hover_ledge')
    {
        rx *= 1.18;
        keelRatio = clamp(keelRatio * 0.78, 0.25, 0.68);
        topFlatness = Math.max(topFlatness, 0.78);
    }

    const keelH = Math.max(0.18, h * keelRatio);
    const deckH = Math.max(0.16, h - keelH);

    // Anchor local y = 0 at the equatorial rim so tilting rotates naturally around the sky island's mid-air center!
    const floatPivot = [worldPivot[0], worldPivot[1] + hoverHeight + keelH, worldPivot[2]];
    const rot0 = rng.float() * Math.PI * 2;
    const parts = [];

    // 1. Upper Plateau / Crag Rim Deck (spans local y in [-deckOverlap .. +deckH])
    const deckRx = rx * (1.0 + rimOverhang * 0.38);
    const deckRz = rz * (1.0 + rimOverhang * 0.38);
    const deckOverlap = Math.min(keelH * 0.32, deckH * 0.45);
    const deckCy = (deckH - deckOverlap) * 0.5;
    const deckRy = (deckH + deckOverlap) * 0.55;

    const bfDeck = buildStrikeDipFrame(rot0, 0.14);
    let deckPoly = sculptGeologicalParentPoly(
        [style === 'hover_ledge' ? rx * 0.16 : 0, deckCy, 0],
        [deckRx, deckRy, deckRz],
        bfDeck,
        rng,
        {
            jointDispersion: 0.18,
            cornerBlunt: 0.68 + 0.24 * roundness,
            flatBaseY: null,
            extraBevelCuts: 8
        }
    );

    if (deckPoly)
    {
        // Plateau truncation plane controlled continuously by topFlatness (0 = crag peak, 1 = flat sky-island plateau)
        const capHeight = deckH * (1.04 - 0.34 * topFlatness);
        const capSlope = (1.0 - topFlatness) * 0.38;
        const capN = vnorm([rng.signed() * capSlope, 1.0, rng.signed() * capSlope]);
        const nextTop = clipPolyByHalfSpace(deckPoly, capN, capHeight * capN[1], 0);
        if (nextTop) deckPoly = nextTop;
    }

    const deckPrim = compilePlainPrimitive({
        poly: deckPoly,
        R: worldR,
        pivot: floatPivot,
        roundness,
        sharpBase: false,
        sqWeight: roundness > 0.45 ? (roundness - 0.45) * 1.1 : 0.0,
        sqRx: deckRx * 0.94, sqRy: deckRy * 0.90, sqRz: deckRz * 0.94, sqCy: deckCy,
        sqExpH: 0.92, sqExpV: 0.68,
        primOp: -1.0,
        baseY: -deckOverlap,
        refH: deckH + deckOverlap,
        taper: -(0.12 + rimOverhang * 0.35),
        skewX: style === 'hover_ledge' ? 0.24 : keelSkew * 0.20
    });
    if (!deckPrim) return null;
    parts.push(deckPrim);

    // 2. Inverted Multi-Faceted Stalactite / Torn Bedrock Root Keel (local y in [-keelH .. +keelOverlap])
    const keelOverlap = Math.min(deckH * 0.45, keelH * 0.28);
    const kBound = Math.max(rx, rz, keelH) * 1.8;
    let keelPoly = createBoxPoly(-kBound, -keelH, -kBound, kBound, keelOverlap, kBound, 0);

    const tipSkewX = keelSkew * rx * 0.78;
    const tipSkewZ = rng.signed() * rz * 0.14;
    const keelSides = 8;
    for (let s = 0; s < keelSides; s++)
    {
        const ang = rot0 + 0.2 + (s * Math.PI * 2) / keelSides + rng.signed() * 0.09;
        const dx = Math.cos(ang), dz = Math.sin(ang);
        const rTop = Math.hypot(rx * dx, rz * dz) * rng.range(0.86, 1.04);
        const rBot = rTop * Math.max(0.04, 1.0 - keelSharp * 0.95);
        const uBot = (dx * tipSkewX + dz * tipSkewZ) + rBot;
        const du = rTop - uBot;
        const L = Math.max(1e-4, Math.hypot(keelH, du));
        const n = [(dx * keelH) / L, -du / L, (dz * keelH) / L];
        const d = (rTop * keelH) / L;
        const next = clipPolyByHalfSpace(keelPoly, n, d, 0);
        if (next) keelPoly = next;
    }

    const keelBlend = Math.min(rx, h) * (0.045 + 0.10 * roundness);
    const keelPrim = compilePlainPrimitive({
        poly: keelPoly,
        R: worldR,
        pivot: floatPivot,
        roundness: roundness * 0.75,
        sharpBase: false,
        primOp: keelBlend,
        baseY: -keelH,
        refH: keelH + keelOverlap
    });
    if (keelPrim) parts.push(keelPrim);

    // 3. Secondary Asymmetric Hanging Root Spurs around the underbelly
    for (let sp = 0; sp < keelSpurs; sp++)
    {
        const sAng = rot0 + 0.55 + (sp * Math.PI * 2) / Math.max(1, keelSpurs) + rng.signed() * 0.22;
        const dx = Math.cos(sAng), dz = Math.sin(sAng);
        const radialDist = 0.46 + rng.float() * 0.18;
        const ox = dx * rx * radialDist;
        const oz = dz * rz * radialDist;
        const spurDepth = keelH * (0.44 + ((sp * 29) % 3) * 0.11 + rng.float() * 0.08);
        const spurTop = keelOverlap * 0.75;
        const spurRx = rx * rng.range(0.28, 0.38);
        const spurRz = rz * rng.range(0.28, 0.38);

        const sb = Math.max(spurRx, spurRz, spurDepth) * 1.8;
        let sPoly = createBoxPoly(ox - sb, -spurDepth, oz - sb, ox + sb, spurTop, oz + sb, 0);
        const tipX = ox + keelSkew * spurRx * 0.9;
        const tipZ = oz;
        for (let k = 0; k < 6; k++)
        {
            const a = sAng + (k * Math.PI * 2) / 6;
            const nx0 = Math.cos(a), nz0 = Math.sin(a);
            const rt = Math.hypot(spurRx * nx0, spurRz * nz0);
            const rb = rt * Math.max(0.08, 1.0 - keelSharp * 0.88);
            const uTop = nx0 * ox + nz0 * oz + rt;
            const uBot = nx0 * tipX + nz0 * tipZ + rb;
            const du = uTop - uBot;
            const L = Math.max(1e-4, Math.hypot(spurDepth, du));
            const n = [(nx0 * spurDepth) / L, -du / L, (nz0 * spurDepth) / L];
            const d = (uTop * spurDepth) / L;
            const next = clipPolyByHalfSpace(sPoly, n, d, 0);
            if (next) sPoly = next;
        }

        const spurPrim = compilePlainPrimitive({
            poly: sPoly,
            R: worldR,
            pivot: floatPivot,
            roundness: roundness * 0.80,
            sharpBase: false,
            primOp: keelBlend * 0.85,
            baseY: -spurDepth,
            refH: spurDepth + spurTop
        });
        if (spurPrim) parts.push(spurPrim);
    }

    return parts;
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
        case 'hoodoo_column':      return buildSingleHoodooColumn(p, worldR, worldPivot, rng);
        case 'pedestal_rock':      return buildSinglePedestalRock(p, worldR, worldPivot, rng);
        case 'floating_rock':      return buildSingleFloatingRock(p, worldR, worldPivot, rng);
        case 'rock_slab':          return buildSingleRockSlab(p, worldR, worldPivot, rng);
        case 'crystal_prism':      return buildSingleCrystalPrism(p, worldR, worldPivot, rng);
        case 'linear_dike':        return buildSingleLinearDike(p, worldR, worldPivot, rng);
        case 'custom_sculpt_rock': return buildSingleCustomSculptRock(p, worldR, worldPivot, rng);
        case 'big_boulder':
        default:                   return buildSingleBigBoulder(p, worldR, worldPivot, rng);
    }
}

/*==========================================================================================================================================
               7. PURE-SDF KINEMATIC SPLIT & FAULT MODIFIER ENGINE + LAYER-STACK COMPILER
==========================================================================================================================================*/

export function computeRock2FaultBasis(strikeYawDeg = 0.0, dipPitchDeg = 68.0)
{
    const psi = (strikeYawDeg || 0.0) * DEG2RAD;
    const del = clamp(dipPitchDeg ?? 68.0, 0.0, 90.0) * DEG2RAD;
    const sp = Math.sin(psi), cp = Math.cos(psi);
    const sd = Math.sin(del), cd = Math.cos(del);
    return {
        strike: [sp, 0.0, cp],
        normal: [sd * cp, cd, -sd * sp],
        dip:    [cd * cp, -sd, -cd * sp]
    };
}

export function rock2FaultNormalToStrikeDip(nx, ny, nz)
{
    const len = Math.hypot(nx, ny, nz) || 1.0;
    let ux = nx / len, uy = ny / len, uz = nz / len;
    if (uy < 0.0)
    {
        ux = -ux; uy = -uy; uz = -uz;
    }
    const dipPitchDeg = Math.acos(clamp(uy, 0.0, 1.0)) / DEG2RAD;
    const horiz = Math.hypot(ux, uz);
    const strikeYawDeg = horiz > 1e-4 ? (Math.atan2(-uz, ux) / DEG2RAD) : 0.0;
    return {
        strikeYawDeg: Number(strikeYawDeg.toFixed(1)),
        dipPitchDeg: Number(clamp(dipPitchDeg, 0.0, 90.0).toFixed(1))
    };
}

function buildRodriguesMat3(ax, ay, az, angleRad)
{
    const len = Math.hypot(ax, ay, az);
    if (len < 1e-6 || Math.abs(angleRad) < 1e-6)
    {
        return [1, 0, 0, 0, 1, 0, 0, 0, 1];
    }
    const ux = ax / len, uy = ay / len, uz = az / len;
    const c = Math.cos(angleRad), s = Math.sin(angleRad), t = 1.0 - c;
    return [
        t * ux * ux + c,      t * ux * uy - s * uz, t * ux * uz + s * uy,
        t * ux * uy + s * uz, t * uy * uy + c,      t * uy * uz - s * ux,
        t * ux * uz - s * uy, t * uy * uz + s * ux, t * uz * uz + c
    ];
}

function buildRigidFaultBlock(hingePivot, axisVec, tiltRad, transVec, f0 = 0, f1 = 0, f2 = 0, f3 = 0)
{
    const R = buildRodriguesMat3(axisVec[0], axisVec[1], axisVec[2], tiltRad);
    const invR = [
        R[0], R[3], R[6],
        R[1], R[4], R[7],
        R[2], R[5], R[8]
    ];
    const hx = hingePivot[0], hy = hingePivot[1], hz = hingePivot[2];
    const tx = transVec[0], ty = transVec[1], tz = transVec[2];

    // Forward: p = h + R*(q - h) + t = R*q + (h - R*h + t)
    const rHx = R[0] * hx + R[1] * hy + R[2] * hz;
    const rHy = R[3] * hx + R[4] * hy + R[5] * hz;
    const rHz = R[6] * hx + R[7] * hy + R[8] * hz;
    const fwdT = [hx - rHx + tx, hy - rHy + ty, hz - rHz + tz];

    // Inverse pullback: q = h + R^T*(p - (h + t)) = R^T*p + (h - R^T*(h + t))
    const cx = hx + tx, cy = hy + ty, cz = hz + tz;
    const rtCx = invR[0] * cx + invR[1] * cy + invR[2] * cz;
    const rtCy = invR[3] * cx + invR[4] * cy + invR[5] * cz;
    const rtCz = invR[6] * cx + invR[7] * cy + invR[8] * cz;
    const invT = [hx - rtCx, hy - rtCy, hz - rtCz];

    return {
        fwdR: R,
        fwdT,
        invR,
        invT,
        f0, f1, f2, f3
    };
}

function hashFault2D01(ix, iy, seedU)
{
    let h = (Math.imul(ix | 0, 374761393) + Math.imul(iy | 0, 668265263) + Math.imul(seedU | 0, 1442695041)) >>> 0;
    h = Math.imul(h ^ (h >>> 13), 1274126177) >>> 0;
    return ((h ^ (h >>> 16)) >>> 0) / 4294967296.0;
}

function faultValueNoise2DSigned(x, y, seedU)
{
    const ix = Math.floor(x) | 0;
    const iy = Math.floor(y) | 0;
    const fx = x - ix;
    const fy = y - iy;
    const ux = fx * fx * (3.0 - 2.0 * fx);
    const uy = fy * fy * (3.0 - 2.0 * fy);

    const n00 = hashFault2D01(ix, iy, seedU);
    const n10 = hashFault2D01(ix + 1, iy, seedU);
    const n01 = hashFault2D01(ix, iy + 1, seedU);
    const n11 = hashFault2D01(ix + 1, iy + 1, seedU);

    const nx0 = n00 + (n10 - n00) * ux;
    const nx1 = n01 + (n11 - n01) * ux;
    return (nx0 + (nx1 - nx0) * uy) * 2.0 - 1.0;
}

export function evalRock2FaultJigsawRoughness(u, v, planeSeed, mod)
{
    const rtA = mod.rtAniso;
    let su = u / rtA;
    let sv = v * rtA;
    let disp = 0.0;

    if (mod.macroWavinessAmp > 1e-5)
    {
        const invW = mod.invMacroWavelength;
        disp += mod.macroWavinessAmp * faultValueNoise2DSigned(su * invW, sv * invW, planeSeed | 0);
    }

    if (mod.microJaggednessAmp > 1e-5)
    {
        let freq = mod.microFrequency;
        let amp = mod.microJaggednessAmp;
        let qu = su, qv = sv;
        for (let oct = 0; oct < mod.octaves; oct++)
        {
            const sOct = (planeSeed + (oct + 1) * 7919) | 0;
            const n = faultValueNoise2DSigned(qu * freq, qv * freq, sOct);
            const ridged = 1.0 - 2.0 * Math.abs(n);
            disp += amp * (0.55 * n + 0.45 * ridged);
            const ru = qu * 0.80 - qv * 0.60;
            const rv = qu * 0.60 + qv * 0.80;
            qu = ru;
            qv = rv;
            freq *= 2.03;
            amp *= 0.50;
        }
    }
    return disp;
}

function evalRock2FaultPlaneSdf(plane, mod, qx, qy, qz)
{
    const rx = qx - plane.ox;
    const ry = qy - plane.oy;
    const rz = qz - plane.oz;
    const nDist = rx * plane.nx + ry * plane.ny + rz * plane.nz - plane.offset;
    if (!mod.hasRoughness) return nDist;
    const u = rx * plane.sx + ry * plane.sy + rz * plane.sz;
    const v = rx * plane.dx + ry * plane.dy + rz * plane.dz;
    return (nDist + evalRock2FaultJigsawRoughness(u, v, plane.seed, mod)) * mod.roughPenalty;
}

function rotateVecByMat3(M, v)
{
    return [
        M[0] * v[0] + M[1] * v[1] + M[2] * v[2],
        M[3] * v[0] + M[4] * v[1] + M[5] * v[2],
        M[6] * v[0] + M[7] * v[1] + M[8] * v[2]
    ];
}

export function compileRock2SplitFaultModifier(lyr, srcBounds)
{
    const p = { ...DEFAULT_ROCK2_SPLIT_FAULT_PARAMS, ...(lyr.params || {}) };
    const splitMode = p.splitMode || 'single_plane';
    const splitModeCode = splitMode === 'parallel_family' ? 1
        : (splitMode === 'conjugate_pair' ? 2 : (splitMode === 'radial_wedge' ? 3 : 0));

    const ox = Number.isFinite(p.originX) ? p.originX : 0.0;
    const oy = Number.isFinite(p.originY) ? p.originY : 1.8;
    const oz = Number.isFinite(p.originZ) ? p.originZ : 0.0;
    const origin = [ox, oy, oz];

    const strikeYawDeg = p.strikeYawDeg ?? 24.0;
    const dipPitchDeg = clamp(p.dipPitchDeg ?? 68.0, 0.0, 90.0);
    const basis = computeRock2FaultBasis(strikeYawDeg, dipPitchDeg);
    const s0 = basis.strike, d0 = basis.dip, n0 = basis.normal;

    const baseSeed = Math.max(1, Math.round(p.seed ?? 1409)) | 0;
    const rng = createRng(baseSeed * 2654435761);

    const macroWavinessAmp = clamp(p.macroWavinessAmp ?? 0.18, 0.0, 2.0);
    const macroWavelength = clamp(p.macroWavelength ?? 2.40, 0.25, 14.0);
    const microJaggednessAmp = clamp(p.microJaggednessAmp ?? 0.045, 0.0, 0.75);
    const microFrequency = clamp(p.microFrequency ?? 3.20, 0.35, 16.0);
    const octaves = clamp(Math.round(p.octaves ?? 3), 1, 4);
    const anisotropyStretch = clamp(p.anisotropyStretch ?? 1.45, 0.20, 5.0);
    const gradEst = Math.min(0.88, (macroWavinessAmp / macroWavelength) * 1.6 + microJaggednessAmp * microFrequency * 0.65);
    const roughPenalty = 1.0 / Math.sqrt(1.0 + gradEst * gradEst);

    const crackGapWidth = clamp(p.crackGapWidth ?? 0.06, 0.0, 1.80);
    const lipChamfer = clamp(p.lipChamfer ?? 0.045, 0.0, 0.60);
    const vTaperDepth = clamp(p.vTaperDepth ?? 0.0, 0.0, 10.0);
    const tipSharpness = clamp(p.tipSharpness ?? 1.25, 0.25, 4.0);

    const hN = clamp(p.normalHeave ?? 0.14, -1.5, 4.0);
    const hS = clamp(p.strikeSlip ?? 0.32, -5.0, 5.0);
    const hD = clamp(p.dipSlip ?? 0.22, -5.0, 5.0);
    const hingeDeg = clamp(p.hingeTiltDeg ?? 5.0, -75.0, 75.0);
    const gravSettle = clamp(p.gravitySettle ?? 0.08, -3.0, 3.5);

    const bMinY = Number.isFinite(srcBounds?.minY) ? srcBounds.minY : 0.0;
    const baseHingeY = Math.min(oy, bMinY + 0.35);

    const planes = [];
    const blocks = [];

    if (splitMode === 'single_plane')
    {
        planes.push({
            ox, oy, oz, offset: 0.0,
            nx: n0[0], ny: n0[1], nz: n0[2],
            sx: s0[0], sy: s0[1], sz: s0[2],
            dx: d0[0], dy: d0[1], dz: d0[2],
            seed: baseSeed
        });

        // Block 0: Footwall (stationary reference, Phi_0 <= 0 -> +1)
        blocks.push(buildRigidFaultBlock([ox, baseHingeY, oz], s0, 0.0, [0, 0, 0], +1, 0, 0, 0));

        // Block 1: Hanging Wall (T^-1(p) rigid heave + strike/dip slip + hinge tilt + gravity settle, Phi_0 >= 0 -> -1)
        const totTiltRad = (hingeDeg + gravSettle * 7.5) * DEG2RAD;
        const trans = [
            hN * n0[0] + hS * s0[0] + hD * d0[0],
            hN * n0[1] + hS * s0[1] + hD * d0[1] - gravSettle,
            hN * n0[2] + hS * s0[2] + hD * d0[2]
        ];
        blocks.push(buildRigidFaultBlock([ox, baseHingeY, oz], s0, totTiltRad, trans, -1, 0, 0, 0));
    }
    else if (splitMode === 'parallel_family')
    {
        const K = clamp(Math.round(p.planeCount ?? 3), 1, 16);
        const spacing = clamp(p.spacing ?? 1.35, 0.20, 10.0);
        const spJit = clamp(p.spacingJitter ?? 0.18, 0.0, 1.0);
        const dipJit = clamp(p.dipJitterDeg ?? 2.5, 0.0, 30.0);

        const offsets = [];
        for (let i = 0; i < K; i++)
        {
            const baseOff = (i - (K - 1) * 0.5) * spacing;
            offsets.push(baseOff + rng.signed() * 0.42 * spJit * spacing);
        }
        offsets.sort((a, b) => a - b);

        for (let i = 0; i < K; i++)
        {
            const bJit = computeRock2FaultBasis(
                strikeYawDeg + rng.signed() * dipJit * 0.55,
                clamp(dipPitchDeg + rng.signed() * dipJit, 0.0, 90.0)
            );
            planes.push({
                ox, oy, oz, offset: offsets[i],
                nx: bJit.normal[0], ny: bJit.normal[1], nz: bJit.normal[2],
                sx: bJit.strike[0], sy: bJit.strike[1], sz: bJit.strike[2],
                dx: bJit.dip[0],    dy: bJit.dip[1],    dz: bJit.dip[2],
                seed: (baseSeed + (i + 1) * 4099) | 0
            });
        }

        const denom = Math.max(1.0, K * 0.5);
        for (let m = 0; m <= K; m++)
        {
            const rel = (m - K * 0.5) / denom;
            const absRel = Math.abs(rel);
            const cMid = m === 0
                ? offsets[0] - spacing * 0.5
                : (m === K ? offsets[K - 1] + spacing * 0.5 : 0.5 * (offsets[m - 1] + offsets[m]));
            const pivotM = [
                ox + n0[0] * cMid,
                baseHingeY,
                oz + n0[2] * cMid
            ];
            const tiltRad = (rel * hingeDeg + (rel >= 0 ? 1 : -1) * absRel * gravSettle * 6.0) * DEG2RAD;
            const trans = [
                rel * (hN * n0[0] + hS * s0[0] + hD * d0[0]),
                rel * (hN * n0[1] + hS * s0[1] + hD * d0[1]) - absRel * gravSettle,
                rel * (hN * n0[2] + hS * s0[2] + hD * d0[2])
            ];
            const fLow = m > 0 ? -m : 0;           // -( (m-1) + 1 ) -> Phi_{m-1} >= 0
            const fHigh = m < K ? +(m + 1) : 0;    // +( m + 1 )     -> Phi_m <= 0
            blocks.push(buildRigidFaultBlock(pivotM, s0, tiltRad, trans, fLow, fHigh, 0, 0));
        }
    }
    else if (splitMode === 'conjugate_pair')
    {
        const totalPlanes = clamp(Math.round(p.planeCount ?? 2), 2, 16);
        const conjDeg = clamp(p.conjugateAngleDeg ?? 62.0, 20.0, 150.0);
        const halfRad = 0.5 * conjDeg * DEG2RAD;
        const spacing = clamp(p.spacing ?? 1.35, 0.25, 10.0);
        const spJit = clamp(p.spacingJitter ?? 0.18, 0.0, 1.0);
        const dipJit = clamp(p.dipJitterDeg ?? 2.5, 0.0, 30.0);

        // totalPlanes = 2..16 maps directly to (nA + nB) = totalPlanes (max 8x8 -> 9x9 = 81 cells)
        const nA = Math.ceil(totalPlanes * 0.5);
        const nB = Math.floor(totalPlanes * 0.5);

        const rotA = buildRodriguesMat3(s0[0], s0[1], s0[2], +halfRad);
        const rotB = buildRodriguesMat3(s0[0], s0[1], s0[2], -halfRad);
        const normA0 = rotateVecByMat3(rotA, n0), dipA0 = rotateVecByMat3(rotA, d0);
        const normB0 = rotateVecByMat3(rotB, n0), dipB0 = rotateVecByMat3(rotB, d0);

        const offA = [], offB = [];
        for (let i = 0; i < nA; i++) offA.push(nA === 1 ? 0.0 : ((i - (nA - 1) * 0.5) * spacing + rng.signed() * 0.38 * spJit * spacing));
        for (let j = 0; j < nB; j++) offB.push(nB === 1 ? 0.0 : ((j - (nB - 1) * 0.5) * spacing + rng.signed() * 0.38 * spJit * spacing));
        offA.sort((a, b) => a - b);
        offB.sort((a, b) => a - b);

        for (let i = 0; i < nA; i++)
        {
            const rJ = buildRodriguesMat3(s0[0], s0[1], s0[2], rng.signed() * dipJit * DEG2RAD);
            const nJ = rotateVecByMat3(rJ, normA0);
            const dJ = rotateVecByMat3(rJ, dipA0);
            planes.push({
                ox, oy, oz, offset: offA[i],
                nx: nJ[0], ny: nJ[1], nz: nJ[2],
                sx: s0[0], sy: s0[1], sz: s0[2],
                dx: dJ[0], dy: dJ[1], dz: dJ[2],
                seed: (baseSeed + (i + 1) * 3571) | 0
            });
        }
        for (let j = 0; j < nB; j++)
        {
            const rJ = buildRodriguesMat3(s0[0], s0[1], s0[2], rng.signed() * dipJit * DEG2RAD);
            const nJ = rotateVecByMat3(rJ, normB0);
            const dJ = rotateVecByMat3(rJ, dipB0);
            planes.push({
                ox, oy, oz, offset: offB[j],
                nx: nJ[0], ny: nJ[1], nz: nJ[2],
                sx: s0[0], sy: s0[1], sz: s0[2],
                dx: dJ[0], dy: dJ[1], dz: dJ[2],
                seed: (baseSeed + (nA + j + 1) * 5113) | 0
            });
        }

        const denA = Math.max(0.5, nA * 0.5);
        const denB = Math.max(0.5, nB * 0.5);
        for (let ia = 0; ia <= nA; ia++)
        {
            const ua = (ia - nA * 0.5) / denA;
            const fALow = ia > 0 ? -ia : 0;
            const fAHigh = ia < nA ? +(ia + 1) : 0;

            for (let ib = 0; ib <= nB; ib++)
            {
                const ub = (ib - nB * 0.5) / denB;
                const fBLow = ib > 0 ? -(nA + ib) : 0;
                const fBHigh = ib < nB ? +(nA + ib + 1) : 0;

                // Conjugate X-shear displacement: Set A dextral (+ua), Set B sinistral (-ub), radial dilation + throw
                const slipS = (ua - ub) * 0.5 * hS;
                const tiltRad = ((ua - ub) * 0.5 * hingeDeg + (Math.abs(ua) + Math.abs(ub)) * 0.35 * gravSettle * 5.0) * DEG2RAD;
                const trans = [
                    0.5 * hN * (ua * normA0[0] + ub * normB0[0]) + slipS * s0[0] + 0.5 * hD * (ua * dipA0[0] + ub * dipB0[0]),
                    0.5 * hN * (ua * normA0[1] + ub * normB0[1]) + slipS * s0[1] + 0.5 * hD * (ua * dipA0[1] + ub * dipB0[1])
                        - 0.5 * (Math.abs(ua) + Math.abs(ub)) * gravSettle,
                    0.5 * hN * (ua * normA0[2] + ub * normB0[2]) + slipS * s0[2] + 0.5 * hD * (ua * dipA0[2] + ub * dipB0[2])
                ];
                blocks.push(buildRigidFaultBlock([ox, baseHingeY, oz], s0, tiltRad, trans, fALow, fAHigh, fBLow, fBHigh));
            }
        }
    }
    else
    {
        // 'radial_wedge': W convex wedges fanning out around hub axis `n0` on disc `(s0, d0)`
        const W = clamp(Math.round(p.planeCount ?? 4), 3, 16);
        const spJit = clamp(p.spacingJitter ?? 0.18, 0.0, 1.0);
        const dipJit = clamp(p.dipJitterDeg ?? 2.5, 0.0, 25.0);
        const stepAng = (Math.PI * 2.0) / W;

        const angles = [];
        for (let k = 0; k < W; k++)
        {
            angles.push(k * stepAng + rng.signed() * 0.32 * spJit * stepAng);
        }

        for (let k = 0; k < W; k++)
        {
            const th = angles[k];
            const ct = Math.cos(th), st = Math.sin(th);
            // Ray outward in disc `(s0, d0)`
            const ray = [
                ct * s0[0] + st * d0[0],
                ct * s0[1] + st * d0[1],
                ct * s0[2] + st * d0[2]
            ];
            // Circumferential normal (+theta side)
            let mVec = [
                -st * s0[0] + ct * d0[0],
                -st * s0[1] + ct * d0[1],
                -st * s0[2] + ct * d0[2]
            ];
            let hubJ = n0;
            if (dipJit > 1e-3)
            {
                const rTilt = buildRodriguesMat3(ray[0], ray[1], ray[2], rng.signed() * dipJit * DEG2RAD);
                mVec = rotateVecByMat3(rTilt, mVec);
                hubJ = rotateVecByMat3(rTilt, n0);
            }
            planes.push({
                ox, oy, oz, offset: 0.0,
                nx: mVec[0], ny: mVec[1], nz: mVec[2],
                sx: ray[0],  sy: ray[1],  sz: ray[2],
                dx: hubJ[0], dy: hubJ[1], dz: hubJ[2],
                seed: (baseSeed + (k + 1) * 6151) | 0
            });
        }

        for (let k = 0; k < W; k++)
        {
            const nextK = (k + 1) % W;
            const a0 = angles[k];
            const a1 = nextK === 0 ? angles[0] + Math.PI * 2.0 : angles[nextK];
            const mid = 0.5 * (a0 + a1);
            const cm = Math.cos(mid), sm = Math.sin(mid);

            // Outward bisector & tangential shear unit vectors
            const bis = [
                cm * s0[0] + sm * d0[0],
                cm * s0[1] + sm * d0[1],
                cm * s0[2] + sm * d0[2]
            ];
            const tang = [
                -sm * s0[0] + cm * d0[0],
                -sm * s0[1] + cm * d0[1],
                -sm * s0[2] + cm * d0[2]
            ];
            const axialAlt = Math.sin(mid + (k % 2 === 0 ? 0.45 : -0.45));
            const hingePivotW = [ox + bis[0] * 0.85, baseHingeY, oz + bis[2] * 0.85];
            const tiltRad = -(hingeDeg + gravSettle * 9.0) * DEG2RAD;
            const trans = [
                hN * bis[0] + hS * tang[0] + hD * axialAlt * n0[0],
                hN * bis[1] + hS * tang[1] + hD * axialAlt * n0[1] - gravSettle,
                hN * bis[2] + hS * tang[2] + hD * axialAlt * n0[2]
            ];
            // Sector k lies where Phi_k >= 0 (-(k+1)) AND Phi_{nextK} <= 0 (+(nextK+1))!
            blocks.push(buildRigidFaultBlock(hingePivotW, tang, tiltRad, trans, -(k + 1), +(nextK + 1), 0, 0));
        }
    }

    // Compute exact post-fault expanded bounds & per-block conservative AABBs for fast CPU culling
    const sb = srcBounds || { minX: -3, minY: 0, minZ: -3, maxX: 3, maxY: 4, maxZ: 3 };
    const srcCorners = [
        [sb.minX, sb.minY, sb.minZ], [sb.maxX, sb.minY, sb.minZ],
        [sb.maxX, sb.minY, sb.maxZ], [sb.minX, sb.minY, sb.maxZ],
        [sb.minX, sb.maxY, sb.minZ], [sb.maxX, sb.maxY, sb.minZ],
        [sb.maxX, sb.maxY, sb.maxZ], [sb.minX, sb.maxY, sb.maxZ]
    ];
    const srcCx = 0.5 * (sb.minX + sb.maxX);
    const srcCy = 0.5 * (sb.minY + sb.maxY);
    const srcCz = 0.5 * (sb.minZ + sb.maxZ);

    let outMinX = sb.minX, outMinY = sb.minY, outMinZ = sb.minZ;
    let outMaxX = sb.maxX, outMaxY = sb.maxY, outMaxZ = sb.maxZ;
    let maxDisp = 0.0;

    for (let bi = 0; bi < blocks.length; bi++)
    {
        const blk = blocks[bi];
        const R = blk.fwdR, T = blk.fwdT;
        let bMinX = Infinity, bMinY = Infinity, bMinZ = Infinity;
        let bMaxX = -Infinity, bMaxY = -Infinity, bMaxZ = -Infinity;

        for (let ci = 0; ci < 8; ci++)
        {
            const c = srcCorners[ci];
            const wx = R[0] * c[0] + R[1] * c[1] + R[2] * c[2] + T[0];
            const wy = R[3] * c[0] + R[4] * c[1] + R[5] * c[2] + T[1];
            const wz = R[6] * c[0] + R[7] * c[1] + R[8] * c[2] + T[2];
            if (wx < bMinX) bMinX = wx; if (wx > bMaxX) bMaxX = wx;
            if (wy < bMinY) bMinY = wy; if (wy > bMaxY) bMaxY = wy;
            if (wz < bMinZ) bMinZ = wz; if (wz > bMaxZ) bMaxZ = wz;
        }

        blk.minX = bMinX; blk.minY = bMinY; blk.minZ = bMinZ;
        blk.maxX = bMaxX; blk.maxY = bMaxY; blk.maxZ = bMaxZ;

        if (bMinX < outMinX) outMinX = bMinX; if (bMaxX > outMaxX) outMaxX = bMaxX;
        if (bMinY < outMinY) outMinY = bMinY; if (bMaxY > outMaxY) outMaxY = bMaxY;
        if (bMinZ < outMinZ) outMinZ = bMinZ; if (bMaxZ > outMaxZ) outMaxZ = bMaxZ;

        const mCx = R[0] * srcCx + R[1] * srcCy + R[2] * srcCz + T[0];
        const mCy = R[3] * srcCx + R[4] * srcCy + R[5] * srcCz + T[1];
        const mCz = R[6] * srcCx + R[7] * srcCy + R[8] * srcCz + T[2];
        const cornerShift = Math.hypot(mCx - srcCx, mCy - srcCy, mCz - srcCz)
            + 0.5 * Math.hypot(bMaxX - bMinX - (sb.maxX - sb.minX), bMaxY - bMinY - (sb.maxY - sb.minY), bMaxZ - bMinZ - (sb.maxZ - sb.minZ));
        if (cornerShift > maxDisp) maxDisp = cornerShift;
    }

    const useCustomTone = !!(p.stoneTone && p.stoneTone !== 'inherit' && ROCK2_STONE_TONES[p.stoneTone]);
    const faceTone = useCustomTone ? ROCK2_STONE_TONES[p.stoneTone].rgb : [0.52, 0.50, 0.48];

    return {
        id: lyr.id,
        name: lyr.name,
        type: 'split_fault',
        targetScope: p.targetScope === 'single_layer' ? 'single_layer' : 'stack_below',
        targetLayerId: p.targetLayerId || '',
        splitMode,
        splitModeCode,
        origin,
        strikeYawDeg,
        dipPitchDeg,
        conjugateAngleDeg: clamp(p.conjugateAngleDeg ?? 62.0, 20.0, 150.0),
        basis,
        macroWavinessAmp,
        macroWavelength,
        invMacroWavelength: 1.0 / Math.max(0.25, macroWavelength),
        microJaggednessAmp,
        microFrequency,
        octaves,
        anisotropyStretch,
        rtAniso: Math.sqrt(anisotropyStretch),
        hasRoughness: (macroWavinessAmp > 1e-5 || microJaggednessAmp > 1e-5),
        roughPenalty,
        crackGapWidth,
        lipChamfer,
        vTaperDepth,
        tipSharpness,
        useCustomTone: useCustomTone ? 1 : 0,
        faceColor: faceTone,
        planes,
        blocks,
        maxDisplacement: maxDisp + macroWavinessAmp + microJaggednessAmp + 0.35,
        outBounds: {
            minX: outMinX, minY: outMinY, minZ: outMinZ,
            maxX: outMaxX, maxY: outMaxY, maxZ: outMaxZ,
            cx: 0.5 * (outMinX + outMaxX),
            cy: 0.5 * (outMinY + outMaxY),
            cz: 0.5 * (outMinZ + outMaxZ)
        }
    };
}

export function evalRock2SplitFaultModifier(mod, evalUpstreamFn, wx, wy, wz, outColor = null)
{
    const blocks = mod.blocks;
    const planes = mod.planes;
    const halfGap = 0.5 * mod.crackGapWidth;
    const lipChamfer = mod.lipChamfer;
    const isBlind = mod.vTaperDepth > 1e-3;

    let slipWeight = 1.0;
    if (isBlind)
    {
        const dBase0 = evalUpstreamFn(wx, wy, wz, null);
        const depth0 = Math.max(0.0, -dBase0);
        const uSlip = clamp(1.0 - depth0 / (mod.vTaperDepth * 1.35), 0.0, 1.0);
        slipWeight = uSlip * uSlip;
    }

    let bestD = 1e9;
    let bestBlockIdx = 0;
    let bestQ = [wx, wy, wz];
    let bestNearSeam = 0.0;

    for (let bi = 0; bi < blocks.length; bi++)
    {
        const blk = blocks[bi];
        // Fast conservative distance prune against block AABB
        const dxB = Math.max(0.0, blk.minX - wx, wx - blk.maxX);
        const dyB = Math.max(0.0, blk.minY - wy, wy - blk.maxY);
        const dzB = Math.max(0.0, blk.minZ - wz, wz - blk.maxZ);
        if (dxB + dyB + dzB > bestD + 1.5) continue;

        const invR = blk.invR, invT = blk.invT;
        let qx = invR[0] * wx + invR[1] * wy + invR[2] * wz + invT[0];
        let qy = invR[3] * wx + invR[4] * wy + invR[5] * wz + invT[1];
        let qz = invR[6] * wx + invR[7] * wy + invR[8] * wz + invT[2];

        if (isBlind && slipWeight < 0.9999)
        {
            qx = wx + (qx - wx) * slipWeight;
            qy = wy + (qy - wy) * slipWeight;
            qz = wz + (qz - wz) * slipWeight;
        }

        const dRock = evalUpstreamFn(qx, qy, qz, null);
        let dBlk = dRock;
        let seamMask = 0.0;

        const fCodes = [blk.f0, blk.f1, blk.f2, blk.f3];
        if (!isBlind)
        {
            for (let fi = 0; fi < 4; fi++)
            {
                const fc = fCodes[fi];
                if (fc === 0) continue;
                const pIdx = Math.abs(fc) - 1;
                const sign = fc > 0 ? 1.0 : -1.0;
                const phi = evalRock2FaultPlaneSdf(planes[pIdx], mod, qx, qy, qz);
                const dCut = sign * phi + halfGap;
                const prev = dBlk;
                dBlk = lipChamfer > 1e-4 ? smaxPoly(dBlk, dCut, lipChamfer) : Math.max(dBlk, dCut);
                if (Math.abs(dCut - dRock) < lipChamfer + 0.08 || dBlk > prev + 1e-4)
                {
                    seamMask = Math.max(seamMask, clamp(1.0 - Math.abs(dCut) / Math.max(0.06, lipChamfer + 0.06), 0.0, 1.0));
                }
            }
        }
        else
        {
            const depth = Math.max(0.0, -dRock);
            const u = depth / mod.vTaperDepth;
            const taper = u < 1.0 ? Math.pow(1.0 - u, mod.tipSharpness) : 0.0;
            const aEff = Math.max(0.012, halfGap) * taper;
            const rEff = lipChamfer * clamp(1.0 - u, 0.0, 1.0);
            const tipFloor = depth - mod.vTaperDepth;

            for (let fi = 0; fi < 4; fi++)
            {
                const fc = fCodes[fi];
                if (fc === 0) continue;
                const pIdx = Math.abs(fc) - 1;
                const sign = fc > 0 ? 1.0 : -1.0;
                const phi = evalRock2FaultPlaneSdf(planes[pIdx], mod, qx, qy, qz);
                const dSlot = Math.max(-sign * phi - aEff, tipFloor);
                const dCut = -dSlot;
                const prev = dBlk;
                dBlk = rEff > 1e-4 ? smaxPoly(dBlk, dCut, rEff) : Math.max(dBlk, dCut);
                if (u < 1.0 && (Math.abs(dCut - dRock) < rEff + 0.06 || dBlk > prev + 1e-4))
                {
                    seamMask = Math.max(seamMask, clamp(1.0 - Math.abs(dCut) / Math.max(0.06, rEff + 0.06), 0.0, 1.0));
                }
            }
        }

        if (dBlk < bestD)
        {
            bestD = dBlk;
            bestBlockIdx = bi;
            bestQ[0] = qx; bestQ[1] = qy; bestQ[2] = qz;
            bestNearSeam = seamMask;
        }
    }

    if (outColor)
    {
        evalUpstreamFn(bestQ[0], bestQ[1], bestQ[2], outColor);
        if (bestNearSeam > 1e-3)
        {
            const tr = mod.useCustomTone ? mod.faceColor[0] : clamp(outColor.r * 0.84 + 0.04, 0.0, 1.0);
            const tg = mod.useCustomTone ? mod.faceColor[1] : clamp(outColor.g * 0.83 + 0.035, 0.0, 1.0);
            const tb = mod.useCustomTone ? mod.faceColor[2] : clamp(outColor.b * 0.82 + 0.03, 0.0, 1.0);
            const w = bestNearSeam * (mod.useCustomTone ? 0.88 : 0.65);
            outColor.r = lerp(outColor.r, tr, w);
            outColor.g = lerp(outColor.g, tg, w);
            outColor.b = lerp(outColor.b, tb, w);
        }
        outColor._pullbackQ = bestQ;
        outColor._blockIdx = bestBlockIdx;
    }
    return bestD;
}

/*==========================================================================================================================================
               7B. PURE-SDF 3D BRUSH SCULPTING MODIFIER (`sculpt_layer` — ADD / CARVE / FLATTEN / SMOOTH / INFLATE / CREASE)
==========================================================================================================================================*/

export function appendRock2SculptDab(sculptLayer, dabInput)
{
    if (!sculptLayer || sculptLayer.type !== 'sculpt_layer') return 0;
    if (!sculptLayer.params) sculptLayer.params = { ...DEFAULT_ROCK2_SCULPT_LAYER_PARAMS, dabs: [] };
    if (!Array.isArray(sculptLayer.params.dabs)) sculptLayer.params.dabs = [];

    const p = sculptLayer.params;
    const op = dabInput.op || dabInput.opOverride || resolveRock2EffectiveSculptOp(p.brushOp || 'add', p.brushPolarity || 'add', false, false);
    const r = clamp(dabInput.r ?? p.brushRadius ?? 0.78, 0.08, 8.0);
    const s = clamp(dabInput.s ?? p.brushStrength ?? 0.58, 0.02, 1.0);
    const h = clamp(dabInput.h ?? p.brushHardness ?? 0.50, 0.0, 1.0);
    const tone = dabInput.tone ?? p.stoneTone ?? 'inherit';
    const strokeId = dabInput.strokeId ?? 1;
    const sym = dabInput.symmetryMode ?? p.symmetryMode ?? 'none';

    const nNorm = vnorm([dabInput.nx ?? 0.0, dabInput.ny ?? 1.0, dabInput.nz ?? 0.0]);
    const tLen = Math.hypot(dabInput.tx ?? 1.0, dabInput.tz ?? 0.0) || 1.0;
    const tx0 = (dabInput.tx ?? 1.0) / tLen;
    const tz0 = (dabInput.tz ?? 0.0) / tLen;

    const mirrors = [[1, 1]];
    if (sym === 'x' || sym === 'xz') mirrors.push([-1, 1]);
    if (sym === 'z' || sym === 'xz') mirrors.push([1, -1]);
    if (sym === 'xz') mirrors.push([-1, -1]);

    let added = 0;
    for (let mi = 0; mi < mirrors.length; mi++)
    {
        const [mx, mz] = mirrors[mi];
        if (mi > 0)
        {
            if (mx === -1 && Math.abs(dabInput.x) < r * 0.12 && mz === 1) continue;
            if (mz === -1 && Math.abs(dabInput.z) < r * 0.12 && mx === 1) continue;
            if (mx === -1 && mz === -1 && Math.hypot(dabInput.x, dabInput.z) < r * 0.12) continue;
        }
        p.dabs.push({
            x: Number((dabInput.x * mx).toFixed(4)),
            y: Number(dabInput.y.toFixed(4)),
            z: Number((dabInput.z * mz).toFixed(4)),
            nx: Number((nNorm[0] * mx).toFixed(4)),
            ny: Number(nNorm[1].toFixed(4)),
            nz: Number((nNorm[2] * mz).toFixed(4)),
            tx: Number((tx0 * mx).toFixed(4)),
            tz: Number((tz0 * mz).toFixed(4)),
            r: Number(r.toFixed(3)),
            s: Number(s.toFixed(3)),
            h: Number(h.toFixed(3)),
            op,
            tone,
            strokeId
        });
        added++;
    }

    if (p.dabs.length > 1024)
    {
        p.dabs.splice(0, p.dabs.length - 1024);
    }
    return added;
}

export function undoLastRock2SculptStroke(sculptLayer)
{
    const dabs = sculptLayer?.params?.dabs;
    if (!Array.isArray(dabs) || dabs.length === 0) return false;
    const lastStrokeId = dabs[dabs.length - 1].strokeId;
    if (lastStrokeId !== undefined)
    {
        while (dabs.length > 0 && dabs[dabs.length - 1].strokeId === lastStrokeId)
        {
            dabs.pop();
        }
    }
    else
    {
        dabs.pop();
    }
    return true;
}

export function clearRock2SculptLayer(sculptLayer)
{
    if (sculptLayer?.params && Array.isArray(sculptLayer.params.dabs))
    {
        sculptLayer.params.dabs.length = 0;
    }
}

export function compileRock2SculptModifier(lyr, srcBounds)
{
    const p = { ...DEFAULT_ROCK2_SCULPT_LAYER_PARAMS, ...(lyr.params || {}) };
    const rawDabs = Array.isArray(p.dabs) ? p.dabs : [];
    const sb = srcBounds || { minX: -3, minY: 0, minZ: -3, maxX: 3, maxY: 4, maxZ: 3 };

    let outMinX = sb.minX, outMinY = sb.minY, outMinZ = sb.minZ;
    let outMaxX = sb.maxX, outMaxY = sb.maxY, outMaxZ = sb.maxZ;
    let maxDisp = 0.0;

    const dabs = [];
    const startIdx = Math.max(0, rawDabs.length - 1024);
    for (let i = startIdx; i < rawDabs.length; i++)
    {
        const raw = rawDabs[i];
        if (!raw || !Number.isFinite(raw.x) || !Number.isFinite(raw.y) || !Number.isFinite(raw.z)) continue;
        const r = clamp(raw.r ?? 0.78, 0.08, 8.0);
        const s = clamp(raw.s ?? 0.58, 0.02, 1.0);
        const h = clamp(raw.h ?? 0.50, 0.0, 1.0);
        const opCode = ROCK2_SCULPT_OP_CODE[raw.op] ?? 0;
        const nVec = vnorm([raw.nx ?? 0.0, raw.ny ?? 1.0, raw.nz ?? 0.0]);
        const tLen = Math.hypot(raw.tx ?? 1.0, raw.tz ?? 0.0) || 1.0;
        const tx = (raw.tx ?? 1.0) / tLen;
        const tz = (raw.tz ?? 0.0) / tLen;

        const toneSpec = (raw.tone && raw.tone !== 'inherit') ? ROCK2_STONE_TONES[raw.tone] : null;
        const useTone = toneSpec ? 1 : 0;
        const col = toneSpec ? toneSpec.rgb : [0.58, 0.57, 0.55];

        dabs.push({
            cx: raw.x, cy: raw.y, cz: raw.z, r,
            nx: nVec[0], ny: nVec[1], nz: nVec[2], s,
            tx, tz, opCode, h,
            colR: col[0], colG: col[1], colB: col[2], useTone
        });

        if (opCode === 0 || opCode === 4 || opCode === 8)
        {
            const pad = r * 1.45;
            if (raw.x - pad < outMinX) outMinX = raw.x - pad;
            if (raw.x + pad > outMaxX) outMaxX = raw.x + pad;
            if (raw.y - pad < outMinY) outMinY = raw.y - pad;
            if (raw.y + pad > outMaxY) outMaxY = raw.y + pad;
            if (raw.z - pad < outMinZ) outMinZ = raw.z - pad;
            if (raw.z + pad > outMaxZ) outMaxZ = raw.z + pad;
            if (pad > maxDisp) maxDisp = pad;
        }
    }

    return {
        id: lyr.id,
        name: lyr.name,
        type: 'sculpt_layer',
        targetScope: p.targetScope === 'single_layer' ? 'single_layer' : 'stack_below',
        targetLayerId: p.targetLayerId || '',
        dabs,
        maxDisplacement: maxDisp,
        outBounds: {
            minX: outMinX, minY: outMinY, minZ: outMinZ,
            maxX: outMaxX, maxY: outMaxY, maxZ: outMaxZ,
            cx: 0.5 * (outMinX + outMaxX),
            cy: 0.5 * (outMinY + outMaxY),
            cz: 0.5 * (outMinZ + outMaxZ)
        }
    };
}

export function evalRock2SculptModifier(mod, evalUpstreamFn, wx, wy, wz, outColor = null)
{
    const col = outColor || null;
    let dAcc = evalUpstreamFn(wx, wy, wz, col);
    const dabs = mod.dabs;
    if (!dabs || dabs.length === 0) return dAcc;

    for (let i = 0; i < dabs.length; i++)
    {
        const db = dabs[i];
        const r = db.r;
        const s = db.s;
        const h = db.h;
        const op = db.opCode;

        const vx = wx - db.cx;
        const vy = wy - db.cy;
        const vz = wz - db.cz;
        const dist2 = vx * vx + vy * vy + vz * vz;
        const dist = Math.sqrt(dist2);
        const blendK = r * (0.14 + 0.48 * (1.0 - h));

        if (op === 0)
        {
            // Clay Buildup / Add (+): oblate stone-clay pad aligned with surface normal
            const sink = -0.18 * r * (1.0 - 0.5 * s);
            const lb = dist - (r + Math.abs(sink));
            if (lb > dAcc + blendK) continue;

            const qx = vx - db.nx * sink;
            const qy = vy - db.ny * sink;
            const qz = vz - db.nz * sink;
            const vn = qx * db.nx + qy * db.ny + qz * db.nz;
            const tang2 = Math.max(0.0, qx * qx + qy * qy + qz * qz - vn * vn);
            const tau = clamp(0.34 + 0.54 * s, 0.34, 0.88);
            const ry = r * tau;
            const fEllip = Math.sqrt(tang2 / (r * r) + (vn * vn) / (ry * ry));
            const dDab = (fEllip - 1.0) * ry;

            const wCol = blendK > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dDab) / blendK, 0.0, 1.0) : (dDab < dAcc ? 1.0 : 0.0);
            dAcc = sminPoly(dAcc, dDab, blendK);
            if (col && db.useTone && wCol > 1e-3)
            {
                col.r = lerp(col.r, db.colR, wCol * 0.88);
                col.g = lerp(col.g, db.colG, wCol * 0.88);
                col.b = lerp(col.b, db.colB, wCol * 0.88);
            }
        }
        else if (op === 1)
        {
            // Carve / Chisel Gouge (-): deep zero-rind scoop sunk slightly into the rock so thin walls never leave outer crusts
            const sinkIn = -0.06 * r * s;
            const lb = dist - (r + Math.abs(sinkIn));
            if (lb > -dAcc + blendK) continue;

            const qx = vx - db.nx * sinkIn;
            const qy = vy - db.ny * sinkIn;
            const qz = vz - db.nz * sinkIn;
            const vn = qx * db.nx + qy * db.ny + qz * db.nz;
            const tang2 = Math.max(0.0, qx * qx + qy * qy + qz * qz - vn * vn);
            const tau = clamp(0.74 + 0.26 * s, 0.74, 1.0);
            const ry = r * tau;
            const fEllip = Math.sqrt(tang2 / (r * r) + (vn * vn) / (ry * ry));
            const dDab = (fEllip - 1.0) * ry;

            const prev = dAcc;
            dAcc = smaxPoly(dAcc, -dDab, blendK);
            if (col && dAcc > prev + 1e-4)
            {
                const cutW = smoothstep(0.0, Math.max(0.02, blendK + 0.04), dAcc - prev);
                const tr = db.useTone ? db.colR : clamp(col.r * 0.86 + 0.03, 0.0, 1.0);
                const tg = db.useTone ? db.colG : clamp(col.g * 0.85 + 0.03, 0.0, 1.0);
                const tb = db.useTone ? db.colB : clamp(col.b * 0.84 + 0.025, 0.0, 1.0);
                col.r = lerp(col.r, tr, cutW * (db.useTone ? 0.85 : 0.55));
                col.g = lerp(col.g, tg, cutW * (db.useTone ? 0.85 : 0.55));
                col.b = lerp(col.b, tb, cutW * (db.useTone ? 0.85 : 0.55));
            }
        }
        else if (op === 6)
        {
            // Erase SDF (Volumetric 3D Sphere Eraser): completely deletes solid & floating SDFs inside sphere of radius rEff
            const rEff = r * (0.72 + 0.36 * s);
            const feather = r * 0.08 * (1.0 - h);
            const dErase = dist - rEff;
            if (dErase > -dAcc + feather) continue;
            dAcc = smaxPoly(dAcc, -dErase, feather);
        }
        else
        {
            if (dist >= r) continue;
            const uRad = dist / r;
            const fall = 1.0 - smoothstep(h * 0.58, 1.0, uRad);
            const w = fall * fall;
            if (w <= 1e-5) continue;

            if (op === 2)
            {
                // Flatten / Planar Facet Scrape (`TrimDynamic` rock plane shave)
                const scrapeDepth = 0.14 * r * s;
                const dPlane = (vx * db.nx + vy * db.ny + vz * db.nz) + scrapeDepth;
                const target = smaxPoly(dAcc, dPlane, 0.04 * r * (1.0 - h));
                dAcc = lerp(dAcc, target, clamp(s * 1.15, 0.05, 1.0) * w);
            }
            else if (op === 3)
            {
                // Smooth / Polish: 6-tap 3D volumetric Laplacian relaxation
                const stepH = clamp(r * 0.24, 0.04, 0.42);
                const dAvg = (
                    evalUpstreamFn(wx + stepH, wy, wz, null) +
                    evalUpstreamFn(wx - stepH, wy, wz, null) +
                    evalUpstreamFn(wx, wy + stepH, wz, null) +
                    evalUpstreamFn(wx, wy - stepH, wz, null) +
                    evalUpstreamFn(wx, wy, wz + stepH, null) +
                    evalUpstreamFn(wx, wy, wz - stepH, null)
                ) / 6.0;
                dAcc = lerp(dAcc, dAvg, clamp(s * 0.92, 0.05, 0.95) * w);
            }
            else if (op === 4)
            {
                // Inflate / Bulge (+): expands rock volume outward along existing SDF
                dAcc -= (0.34 * r * s) * w;
            }
            else if (op === 7)
            {
                // Deflate / Shrink (-): erodes & melts thin rock shells / slivers inward
                dAcc += (0.44 * r * s) * w;
            }
            else if (op === 5 || op === 8)
            {
                // Crease (- Groove, op=5) / Chisel Ridge (+ Vein, op=8)
                let bx = db.ny * db.tz - db.nz * 0.0;
                let by = db.nz * db.tx - db.nx * db.tz;
                let bz = db.nx * 0.0 - db.ny * db.tx;
                const bLen = Math.hypot(bx, by, bz) || 1.0;
                bx /= bLen; by /= bLen; bz /= bLen;
                const crossCoord = Math.abs(vx * bx + vy * by + vz * bz);
                const grooveDepth = 0.36 * r * s * w;
                const vSlope = 0.75 + 1.35 * h;
                const vCut = Math.max(0.0, grooveDepth - crossCoord * vSlope);
                const sign = op === 8 ? -1.0 : 1.0;
                dAcc += sign * vCut * Math.exp(-Math.max(0.0, Math.abs(dAcc)) * 2.5 / r);
            }

            if (col && db.useTone && w > 1e-3)
            {
                col.r = lerp(col.r, db.colR, w * s * 0.75);
                col.g = lerp(col.g, db.colG, w * s * 0.75);
                col.b = lerp(col.b, db.colB, w * s * 0.75);
            }
        }
    }

    return dAcc;
}

export function evalRock2AnyModifier(mod, evalUpstreamFn, wx, wy, wz, outColor = null)
{
    if (!mod) return evalUpstreamFn(wx, wy, wz, outColor);
    if (mod.type === 'sculpt_layer')
    {
        return evalRock2SculptModifier(mod, evalUpstreamFn, wx, wy, wz, outColor);
    }
    return evalRock2SplitFaultModifier(mod, evalUpstreamFn, wx, wy, wz, outColor);
}

/**
 * Sphere-traces `(ro, rd)` against the full Pure-SDF stack for live 3D Brush Sculpting
 * and returns `{ hitPoint: [x, y, z], normal: [nx, ny, nz], distance, isProximityHit }`!
 * When `options.proximityRadius > 0` (used by `Erase` and `-` Carve/Deflate brushes), any floating
 * SDF fragment passing inside the 3D brush cylinder around `(ro, rd)` is captured automatically!
 */
export function pickRock2SculptHitAtRay(compiledStack, ro, rd, options = {})
{
    if (!compiledStack || !compiledStack.compiledLayers || compiledStack.compiledLayers.length === 0)
    {
        return null;
    }

    const proxR = Math.max(0.0, Number(options.proximityRadius) || 0.0);
    const b = compiledStack.bounds;
    const invDx = 1.0 / (Math.abs(rd[0]) > 1e-8 ? rd[0] : 1e-8);
    const invDy = 1.0 / (Math.abs(rd[1]) > 1e-8 ? rd[1] : 1e-8);
    const invDz = 1.0 / (Math.abs(rd[2]) > 1e-8 ? rd[2] : 1e-8);

    const pad = proxR > 0 ? proxR : 0.0;
    const tx1 = (b.minX - pad - ro[0]) * invDx, tx2 = (b.maxX + pad - ro[0]) * invDx;
    const ty1 = (b.minY - pad - ro[1]) * invDy, ty2 = (b.maxY + pad - ro[1]) * invDy;
    const tz1 = (b.minZ - pad - ro[2]) * invDz, tz2 = (b.maxZ + pad - ro[2]) * invDz;

    const tEnter = Math.max(0.05, Math.min(tx1, tx2), Math.min(ty1, ty2), Math.min(tz1, tz2));
    const tExit  = Math.min(160.0, Math.max(tx1, tx2), Math.max(ty1, ty2), Math.max(tz1, tz2));
    if (tExit < tEnter) return null;

    let t = tEnter;
    let hit = false;
    let bestD = 1e9;
    let bestT = tEnter;
    const maxSteps = proxR > 0 ? 156 : 92;

    for (let step = 0; step < maxSteps && t <= tExit; step++)
    {
        const d = evalRock2StackSdf(compiledStack, ro[0] + rd[0] * t, ro[1] + rd[1] * t, ro[2] + rd[2] * t);
        if (d < bestD)
        {
            bestD = d;
            bestT = t;
        }
        if (d < 0.015)
        {
            hit = true;
            break;
        }
        if (proxR > 0 && d < proxR * 1.6)
        {
            // Fine step near brush cylinder so thin floating SDF slivers/crumbs are never stepped over
            t += Math.min(Math.max(0.045, proxR * 0.22), Math.max(0.025, d * 0.65));
        }
        else
        {
            t += Math.max(0.014, d * 0.82);
        }
    }

    if (hit)
    {
        const hx = ro[0] + rd[0] * t;
        const hy = ro[1] + rd[1] * t;
        const hz = ro[2] + rd[2] * t;
        const eps = 0.025;
        const gx = evalRock2StackSdf(compiledStack, hx + eps, hy, hz) - evalRock2StackSdf(compiledStack, hx - eps, hy, hz);
        const gy = evalRock2StackSdf(compiledStack, hx, hy + eps, hz) - evalRock2StackSdf(compiledStack, hx, hy - eps, hz);
        const gz = evalRock2StackSdf(compiledStack, hx, hy, hz + eps) - evalRock2StackSdf(compiledStack, hx, hy, hz - eps);
        const nVec = vnorm([gx, gy, gz]);

        return {
            hitPoint: [hx, hy, hz],
            normal: nVec,
            distance: t,
            isProximityHit: false
        };
    }

    // Brush-Cylinder Floating-SDF Capture: snap onto floating SDF crumbs inside `proxR` of the cursor ray!
    if (proxR > 0 && bestD < proxR * 0.94)
    {
        const rx = ro[0] + rd[0] * bestT;
        const ry = ro[1] + rd[1] * bestT;
        const rz = ro[2] + rd[2] * bestT;
        const eps = 0.035;
        const gx = evalRock2StackSdf(compiledStack, rx + eps, ry, rz) - evalRock2StackSdf(compiledStack, rx - eps, ry, rz);
        const gy = evalRock2StackSdf(compiledStack, rx, ry + eps, rz) - evalRock2StackSdf(compiledStack, rx, ry - eps, rz);
        const gz = evalRock2StackSdf(compiledStack, rx, ry, rz + eps) - evalRock2StackSdf(compiledStack, rx, ry, rz - eps);
        const grad = vnorm([gx, gy, gz]);

        // Pull from the ray point along -grad onto the nearest floating SDF surface/crumb!
        const pull = Math.max(0.0, bestD);
        const cx = rx - grad[0] * pull;
        const cy = ry - grad[1] * pull;
        const cz = rz - grad[2] * pull;
        const dAtCrumb = evalRock2StackSdf(compiledStack, cx, cy, cz);
        if (dAtCrumb < Math.max(0.22, proxR * 0.42))
        {
            return {
                hitPoint: [cx, cy, cz],
                normal: [-rd[0], -rd[1], -rd[2]],
                distance: bestT,
                isProximityHit: true
            };
        }
    }

    return null;
}

/**
 * 100% Pure-SDF Connected-Component Floating Island Cleaner (`0` Mesh -> SDF!).
 * Scans the compiled 3D SDF volume, finds all disconnected floating SDF crumbs / slivers / islands
 * detached from the grounded rock (or main rock body), and appends exact Pure-SDF `'erase'` spheres
 * onto a `sculpt_layer` so they vanish cleanly in both GPU Raymarch and QEF Dual-Contour Mesh!
 */
export function removeFloatingRock2SdfIslands(doc, compiledStack = null, options = {})
{
    if (!doc || !Array.isArray(doc.layers) || doc.layers.length === 0)
    {
        return { removedIslands: 0, addedDabs: 0, sculptLayerId: null };
    }

    const comp = compiledStack || compileRock2Stack(doc);
    if (!comp || !comp.compiledLayers || comp.compiledLayers.length === 0)
    {
        return { removedIslands: 0, addedDabs: 0, sculptLayerId: null };
    }

    const b = comp.bounds;
    const res = clamp(Math.round(options.resolution ?? 68), 36, 96);
    const sx = (b.maxX - b.minX) / res;
    const sy = (b.maxY - b.minY) / res;
    const sz = (b.maxZ - b.minZ) / res;
    const voxelDiag = Math.hypot(sx, sy, sz);
    // Generous sub-voxel capture threshold so paper-thin crescent slivers in 112^3 raymarch are also caught!
    const isoThresh = voxelDiag * 0.48;

    const total = res * res * res;
    const solid = new Uint8Array(total);
    const visited = new Uint8Array(total);

    let ptr = 0;
    for (let iz = 0; iz < res; iz++)
    {
        const wz = b.minZ + (iz + 0.5) * sz;
        for (let iy = 0; iy < res; iy++)
        {
            const wy = b.minY + (iy + 0.5) * sy;
            for (let ix = 0; ix < res; ix++)
            {
                const wx = b.minX + (ix + 0.5) * sx;
                if (evalRock2StackSdf(comp, wx, wy, wz) <= isoThresh)
                {
                    solid[ptr] = 1;
                }
                ptr++;
            }
        }
    }

    const components = [];
    const queue = new Int32Array(total);
    const strideY = res;
    const strideZ = res * res;

    for (let seedIdx = 0; seedIdx < total; seedIdx++)
    {
        if (!solid[seedIdx] || visited[seedIdx]) continue;
        visited[seedIdx] = 1;
        let qHead = 0, qTail = 0;
        queue[qTail++] = seedIdx;

        let minIx = res, maxIx = 0;
        let minIy = res, maxIy = 0;
        let minIz = res, maxIz = 0;
        const cells = [];

        while (qHead < qTail)
        {
            const cur = queue[qHead++];
            cells.push(cur);
            const iz = (cur / strideZ) | 0;
            const rem = cur - iz * strideZ;
            const iy = (rem / strideY) | 0;
            const ix = rem - iy * strideY;

            if (ix < minIx) minIx = ix; if (ix > maxIx) maxIx = ix;
            if (iy < minIy) minIy = iy; if (iy > maxIy) maxIy = iy;
            if (iz < minIz) minIz = iz; if (iz > maxIz) maxIz = iz;

            // 26-connected neighborhood so diagonal rock bridges stay unified
            for (let dz = -1; dz <= 1; dz++)
            {
                const nz = iz + dz;
                if (nz < 0 || nz >= res) continue;
                for (let dy = -1; dy <= 1; dy++)
                {
                    const ny = iy + dy;
                    if (ny < 0 || ny >= res) continue;
                    for (let dx = -1; dx <= 1; dx++)
                    {
                        if (dx === 0 && dy === 0 && dz === 0) continue;
                        const nx = ix + dx;
                        if (nx < 0 || nx >= res) continue;
                        const nIdx = nz * strideZ + ny * strideY + nx;
                        if (solid[nIdx] && !visited[nIdx])
                        {
                            visited[nIdx] = 1;
                            queue[qTail++] = nIdx;
                        }
                    }
                }
            }
        }

        const minYWorld = b.minY + minIy * sy;
        components.push({
            count: cells.length,
            cells,
            minIx, maxIx, minIy, maxIy, minIz, maxIz,
            minYWorld
        });
    }

    if (components.length === 0)
    {
        return { removedIslands: 0, addedDabs: 0, sculptLayerId: null };
    }

    components.sort((aComp, bComp) => bComp.count - aComp.count);
    const largest = components[0];
    const groundYLimit = 0.28;
    const anyGroundedLarge = components.some(c => c.minYWorld <= groundYLimit && c.count >= Math.max(16, largest.count * 0.08));

    const floatingDebris = [];
    for (let ci = 0; ci < components.length; ci++)
    {
        const c = components[ci];
        const isGrounded = c.minYWorld <= groundYLimit;
        if (anyGroundedLarge)
        {
            // Keep grounded rocks of meaningful volume; delete any ungrounded floating SDFs & tiny crumbs!
            const keep = isGrounded && c.count >= Math.max(14, largest.count * 0.015);
            if (!keep) floatingDebris.push(c);
        }
        else
        {
            // Elevated Floating Island scene: keep largest body, delete all smaller detached floating crumbs/shards!
            if (ci > 0) floatingDebris.push(c);
        }
    }

    if (floatingDebris.length === 0)
    {
        return { removedIslands: 0, addedDabs: 0, sculptLayerId: null };
    }

    // Find or create top-level `sculpt_layer`
    let sculptLayer = null;
    for (let i = doc.layers.length - 1; i >= 0; i--)
    {
        const l = doc.layers[i];
        if (l.type === 'sculpt_layer' && l.enabled && (l.params?.targetScope || 'stack_below') === 'stack_below')
        {
            sculptLayer = l;
            break;
        }
    }
    if (!sculptLayer)
    {
        sculptLayer = createRock2Layer('sculpt_layer', { name: '3D Sculpt & Cleanup Layer' });
        doc.layers.push(sculptLayer);
    }

    const existingDabs = sculptLayer.params?.dabs || [];
    let maxStroke = 0;
    for (let i = 0; i < existingDabs.length; i++)
    {
        if ((existingDabs[i].strokeId ?? 0) > maxStroke) maxStroke = existingDabs[i].strokeId;
    }
    const cleanStrokeId = maxStroke + 1;

    let addedDabs = 0;
    for (let di = 0; di < floatingDebris.length; di++)
    {
        const compDeb = floatingDebris[di];
        const cMinX = b.minX + compDeb.minIx * sx, cMaxX = b.minX + (compDeb.maxIx + 1) * sx;
        const cMinY = b.minY + compDeb.minIy * sy, cMaxY = b.minY + (compDeb.maxIy + 1) * sy;
        const cMinZ = b.minZ + compDeb.minIz * sz, cMaxZ = b.minZ + (compDeb.maxIz + 1) * sz;
        const halfDiag = 0.5 * Math.hypot(cMaxX - cMinX, cMaxY - cMinY, cMaxZ - cMinZ);

        if (halfDiag <= 1.35)
        {
            addedDabs += appendRock2SculptDab(sculptLayer, {
                x: 0.5 * (cMinX + cMaxX),
                y: 0.5 * (cMinY + cMaxY),
                z: 0.5 * (cMinZ + cMaxZ),
                nx: 0, ny: 1, nz: 0,
                r: clamp(halfDiag + voxelDiag * 0.95, 0.16, 4.5),
                s: 1.0,
                h: 0.95,
                op: 'erase',
                symmetryMode: 'none',
                strokeId: cleanStrokeId
            });
        }
        else
        {
            // Cover elongated floating sliver arcs with tight local spheres so nearby grounded rock is untouched
            const coverR = clamp(voxelDiag * 2.6, 0.35, 1.35);
            const killR2 = (coverR * 0.72) * (coverR * 0.72);
            const covered = new Uint8Array(compDeb.cells.length);
            for (let k = 0; k < compDeb.cells.length; k++)
            {
                if (covered[k]) continue;
                const cell = compDeb.cells[k];
                const iz = (cell / strideZ) | 0;
                const rem = cell - iz * strideZ;
                const iy = (rem / strideY) | 0;
                const ix = rem - iy * strideY;
                const cx = b.minX + (ix + 0.5) * sx;
                const cy = b.minY + (iy + 0.5) * sy;
                const cz = b.minZ + (iz + 0.5) * sz;

                addedDabs += appendRock2SculptDab(sculptLayer, {
                    x: cx, y: cy, z: cz,
                    nx: 0, ny: 1, nz: 0,
                    r: coverR + voxelDiag * 0.65,
                    s: 1.0,
                    h: 0.95,
                    op: 'erase',
                    symmetryMode: 'none',
                    strokeId: cleanStrokeId
                });

                for (let j = k; j < compDeb.cells.length; j++)
                {
                    if (covered[j]) continue;
                    const c2 = compDeb.cells[j];
                    const jz = (c2 / strideZ) | 0;
                    const jrem = c2 - jz * strideZ;
                    const jy = (jrem / strideY) | 0;
                    const jx = jrem - jy * strideY;
                    const dx = (b.minX + (jx + 0.5) * sx) - cx;
                    const dy = (b.minY + (jy + 0.5) * sy) - cy;
                    const dz = (b.minZ + (jz + 0.5) * sz) - cz;
                    if (dx * dx + dy * dy + dz * dz <= killR2) covered[j] = 1;
                }
            }
        }
    }

    return {
        removedIslands: floatingDebris.length,
        addedDabs,
        sculptLayerId: sculptLayer.id
    };
}

export function compileRock2Stack(doc)
{
    const layers = doc?.layers || [];
    const hasSoloRock = layers.some(l => l.enabled && l.solo && !isRock2ModifierType(l.type));
    const hasSoloMod = !hasSoloRock && layers.some(l => l.enabled && l.solo && isRock2ModifierType(l.type));

    const compiledLayers = [];
    const compiledModifiers = [];
    const executionSteps = [];
    const allPrimitives = [];
    let totalPlanes = 0;

    let minX = Infinity, minY = Infinity, minZ = Infinity;
    let maxX = -Infinity, maxY = -Infinity, maxZ = -Infinity;
    let maxModifierDisplacement = 0.0;

    const MAX_STACK_PRIMITIVES = 256;
    const pendingSingleLayerModSpecs = [];

    for (let li = 0; li < layers.length; li++)
    {
        const lyr = layers[li];
        if (!lyr.enabled) continue;

        if (isRock2ModifierType(lyr.type))
        {
            if (hasSoloMod && !lyr.solo) continue;
            if (hasSoloRock && !lyr.solo && lyr.params?.targetScope !== 'single_layer') continue;

            const scope = lyr.params?.targetScope === 'single_layer' ? 'single_layer' : 'stack_below';
            if (scope === 'single_layer' && lyr.params?.targetLayerId)
            {
                pendingSingleLayerModSpecs.push(lyr);
            }
            else if (compiledLayers.length > 0)
            {
                const curStackBounds = Number.isFinite(minX)
                    ? { minX, minY, minZ, maxX, maxY, maxZ, cx: 0.5 * (minX + maxX), cy: 0.5 * (minY + maxY), cz: 0.5 * (minZ + maxZ) }
                    : { minX: -3, minY: 0, minZ: -3, maxX: 3, maxY: 4, maxZ: 3, cx: 0, cy: 2, cz: 0 };
                const mod = lyr.type === 'sculpt_layer'
                    ? compileRock2SculptModifier(lyr, curStackBounds)
                    : compileRock2SplitFaultModifier(lyr, curStackBounds);
                const modIdx = compiledModifiers.length;
                compiledModifiers.push(mod);
                executionSteps.push({ kind: 'stack_modifier', modIndex: modIdx });

                if (mod.outBounds.minX < minX) minX = mod.outBounds.minX;
                if (mod.outBounds.maxX > maxX) maxX = mod.outBounds.maxX;
                if (mod.outBounds.minY < minY) minY = mod.outBounds.minY;
                if (mod.outBounds.maxY > maxY) maxY = mod.outBounds.maxY;
                if (mod.outBounds.minZ < minZ) minZ = mod.outBounds.minZ;
                if (mod.outBounds.maxZ > maxZ) maxZ = mod.outBounds.maxZ;
                maxModifierDisplacement += mod.maxDisplacement;
            }
            continue;
        }

        if (hasSoloRock && !lyr.solo) continue;

        const spec = getRock2NodeSpec(lyr.type);
        const p = { ...spec.defaults, ...(lyr.params || {}), _sourceType: spec.type };
        const layerR = buildRock2EulerMatrix3(p.yawDeg ?? 0, p.pitchDeg ?? 0, p.rollDeg ?? 0);
        const layerPivot = [p.posX ?? 0.0, p.posY ?? 0.0, p.posZ ?? 0.0];

        const { intraBlend, instances, rawCount } = evaluateRock2GeneratorInstances(lyr.generator, p, spec.type);
        const room = Math.max(0, MAX_STACK_PRIMITIVES - allPrimitives.length);
        if (room <= 0) break;

        const lSx = clamp(p.scaleX ?? 1.0, 0.12, 8.0);
        const lSy = clamp(p.scaleY ?? 1.0, 0.12, 8.0);
        const lSz = clamp(p.scaleZ ?? 1.0, 0.12, 8.0);
        const pScaled = (lSx === 1.0 && lSy === 1.0 && lSz === 1.0)
            ? p
            : {
                ...p,
                width: (p.width ?? 4.0) * lSx,
                height: (p.height ?? 3.5) * lSy,
                length: (p.length ?? 4.0) * lSz,
                hoverHeight: p.hoverHeight !== undefined ? p.hoverHeight * lSy : undefined
            };

        const layerPrims = [];
        const instanceBounds = {};
        let builtInstances = 0;
        for (let k = 0; k < instances.length && layerPrims.length < room; k++)
        {
            const inst = instances[k];
            const srcIdx = inst.sourceIndex ?? k;
            const localInstR = buildRock2EulerMatrix3(inst.yawDeg, inst.pitchDeg, inst.rollDeg);
            const worldR = mulMat3(layerR, localInstR);
            const worldPivot = forwardRock2Point(
                layerR,
                layerPivot,
                inst.pos[0] * lSx,
                inst.pos[1] * lSy,
                inst.pos[2] * lSz
            );

            const instRockType = inst.rockType || spec.type;
            const instParams = buildMutatedInstanceParams(instRockType, pScaled, inst);

            const built = buildAtomicBaseRockPrimitive(instRockType, instParams, worldR, worldPivot);
            if (!built) continue;

            const parts = Array.isArray(built) ? built.filter(Boolean) : [built];
            if (parts.length === 0 || layerPrims.length + parts.length > MAX_STACK_PRIMITIVES) break;

            parts[0].primOp = -1.0;
            let iMinX = Infinity, iMinY = Infinity, iMinZ = Infinity;
            let iMaxX = -Infinity, iMaxY = -Infinity, iMaxZ = -Infinity;
            for (let pi = 0; pi < parts.length; pi++)
            {
                const pr = parts[pi];
                pr.instanceIndex = srcIdx;
                layerPrims.push(pr);
                if (pr.primOp > -1.5)
                {
                    if (pr.minX < iMinX) iMinX = pr.minX;
                    if (pr.maxX > iMaxX) iMaxX = pr.maxX;
                    if (pr.minY < iMinY) iMinY = pr.minY;
                    if (pr.maxY > iMaxY) iMaxY = pr.maxY;
                    if (pr.minZ < iMinZ) iMinZ = pr.minZ;
                    if (pr.maxZ > iMaxZ) iMaxZ = pr.maxZ;
                }
            }

            if (Number.isFinite(iMinX))
            {
                instanceBounds[srcIdx] = {
                    minX: iMinX, minY: iMinY, minZ: iMinZ,
                    maxX: iMaxX, maxY: iMaxY, maxZ: iMaxZ,
                    cx: (iMinX + iMaxX) * 0.5,
                    cy: (iMinY + iMaxY) * 0.5,
                    cz: (iMinZ + iMaxZ) * 0.5,
                    worldR,
                    worldPivot,
                    rawPos: inst.rawPos || [inst.pos[0], inst.pos[1], inst.pos[2]],
                    rawEuler: inst.rawEuler || [inst.yawDeg, inst.pitchDeg, inst.rollDeg],
                    rockType: instRockType
                };
            }
            builtInstances++;
        }

        if (layerPrims.length === 0) continue;

        const blockStart = allPrimitives.length;
        let lMinX = Infinity, lMinY = Infinity, lMinZ = Infinity;
        let lMaxX = -Infinity, lMaxY = -Infinity, lMaxZ = -Infinity;
        for (let k = 0; k < layerPrims.length; k++)
        {
            const pr = layerPrims[k];
            allPrimitives.push(pr);
            totalPlanes += pr.numPlanes;

            if (pr.primOp > -1.5)
            {
                if (pr.minX < lMinX) lMinX = pr.minX;
                if (pr.maxX > lMaxX) lMaxX = pr.maxX;
                if (pr.minY < lMinY) lMinY = pr.minY;
                if (pr.maxY > lMaxY) lMaxY = pr.maxY;
                if (pr.minZ < lMinZ) lMinZ = pr.minZ;
                if (pr.maxZ > lMaxZ) lMaxZ = pr.maxZ;
            }

            const isBoolSub = lyr.combineMode === 'subtract' || lyr.combineMode === 'chamfer_subtract';
            if ((!isBoolSub || compiledLayers.length === 0 || lyr.isBoolean) && pr.primOp > -1.5)
            {
                if (pr.minX < minX) minX = pr.minX;
                if (pr.maxX > maxX) maxX = pr.maxX;
                if (pr.minY < minY) minY = pr.minY;
                if (pr.maxY > maxY) maxY = pr.maxY;
                if (pr.minZ < minZ) minZ = pr.minZ;
                if (pr.maxZ > maxZ) maxZ = pr.maxZ;
            }
        }

        const lBounds = Number.isFinite(lMinX)
            ? {
                minX: lMinX, minY: lMinY, minZ: lMinZ,
                maxX: lMaxX, maxY: lMaxY, maxZ: lMaxZ,
                cx: (lMinX + lMaxX) * 0.5,
                cy: (lMinY + lMaxY) * 0.5,
                cz: (lMinZ + lMaxZ) * 0.5
            }
            : {
                minX: layerPivot[0] - 1, minY: layerPivot[1], minZ: layerPivot[2] - 1,
                maxX: layerPivot[0] + 1, maxY: layerPivot[1] + 2, maxZ: layerPivot[2] + 1,
                cx: layerPivot[0], cy: layerPivot[1] + 1, cz: layerPivot[2]
            };

        const tone = ROCK2_STONE_TONES[p.stoneTone] || ROCK2_STONE_TONES.clay;
        const cLayerIdx = compiledLayers.length;
        const isBoolLayer = isRock2BooleanLayer(lyr);
        compiledLayers.push({
            id: lyr.id,
            name: lyr.name,
            type: spec.type,
            isBoolean: isBoolLayer,
            generatorType: lyr.generator?.type || 'none',
            instanceCount: builtInstances,
            rawInstanceCount: rawCount ?? builtInstances,
            combineOp: ROCK2_COMBINE_OP_CODE[lyr.combineMode] ?? 0,
            blendRadius: clamp(lyr.blendRadius ?? 0.35, 0.0, 4.0),
            intraBlend: clamp(intraBlend ?? 0.0, 0.0, 3.0),
            color: tone.rgb,
            bounds: lBounds,
            instanceBounds,
            blockStart,
            blockCount: layerPrims.length,
            attachedModifierIndices: []
        });
        executionSteps.push({ kind: 'rock_layer', layerIndex: cLayerIdx });
    }

    for (let mi = 0; mi < pendingSingleLayerModSpecs.length; mi++)
    {
        const mLyr = pendingSingleLayerModSpecs[mi];
        const targetCL = compiledLayers.find(cl => cl.id === mLyr.params?.targetLayerId);
        if (!targetCL) continue;
        const mod = mLyr.type === 'sculpt_layer'
            ? compileRock2SculptModifier(mLyr, targetCL.bounds)
            : compileRock2SplitFaultModifier(mLyr, targetCL.bounds);
        const modIdx = compiledModifiers.length;
        compiledModifiers.push(mod);
        targetCL.attachedModifierIndices.push(modIdx);
        targetCL.bounds = mod.outBounds;
        if (mod.outBounds.minX < minX) minX = mod.outBounds.minX;
        if (mod.outBounds.maxX > maxX) maxX = mod.outBounds.maxX;
        if (mod.outBounds.minY < minY) minY = mod.outBounds.minY;
        if (mod.outBounds.maxY > maxY) maxY = mod.outBounds.maxY;
        if (mod.outBounds.minZ < minZ) minZ = mod.outBounds.minZ;
        if (mod.outBounds.maxZ > maxZ) maxZ = mod.outBounds.maxZ;
        if (mod.maxDisplacement > maxModifierDisplacement) maxModifierDisplacement = mod.maxDisplacement;
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
        minY: minY > 0.15 ? (minY - margin) : Math.min(0.0, minY - margin * 0.5),
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
        compiledModifiers,
        executionSteps,
        maxModifierDisplacement,
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

function chamferSubtract(a, b, r)
{
    const m = Math.max(a, -b);
    if (r <= 1e-6) return m;
    return Math.max(m, (a - b + r) * 0.70710678);
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
        const pH = prim.pinchHeightNorm ?? 0.34;
        const pRel = (h - pH) / 0.22;
        const sPinch = 1.0 - prim.pinch * Math.exp(-pRel * pRel);
        let sRing = 1.0;
        if ((prim.ringAmp ?? 0.0) > 1e-4 && (prim.ringFreq ?? 0.0) > 0.05)
        {
            const hc = clamp(h, 0.0, 1.0);
            sRing = 1.0 + prim.ringAmp * Math.sin(hc * prim.ringFreq * 6.283185307179586 + (prim.ringPhase ?? 0.0));
        }
        const s = clamp(sTap * sPinch * sRing, 0.14, 2.50);
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

function evalRock2LayerRawPrimitives(lyr, primitives, wx, wy, wz)
{
    let dLyr = 1e9;
    let dInst = 1e9;
    let hasInst = false;
    const ib = lyr.intraBlend;

    for (let k = 0; k < lyr.blockCount; k++)
    {
        const prim = primitives[lyr.blockStart + k];
        const pOp = prim.primOp ?? -1.0;
        const lb = Math.hypot(wx - prim.cx, wy - prim.cy, wz - prim.cz) - prim.radius;
        const lbCons = lb > 0.0 ? lb * 0.25 : lb;

        if (pOp > -1.5 && pOp < -0.5)
        {
            if (hasInst)
            {
                dLyr = dLyr > 1e8 ? dInst : (ib > 1e-4 ? sminPoly(dLyr, dInst, ib) : Math.min(dLyr, dInst));
            }
            hasInst = true;
            if (lbCons > dLyr + ib + 0.25)
            {
                dInst = lbCons;
            }
            else
            {
                dInst = evalRock2PrimitiveSdf(prim, wx, wy, wz);
            }
        }
        else if (pOp >= 0.0)
        {
            const subB = pOp;
            if (lbCons > Math.min(dInst, dLyr + ib) + subB + 0.25) continue;
            const dP = evalRock2PrimitiveSdf(prim, wx, wy, wz);
            dInst = dInst > 1e8 ? dP : (subB > 1e-4 ? sminPoly(dInst, dP, subB) : Math.min(dInst, dP));
            hasInst = true;
        }
        else
        {
            const cutB = Math.max(0.0, -pOp - 2.0);
            if (!hasInst || dInst > dLyr + ib + 0.25 || lbCons > Math.max(0.0, -dInst) + cutB + 0.25) continue;
            const dP = evalRock2PrimitiveSdf(prim, wx, wy, wz);
            dInst = smaxPoly(dInst, -dP, cutB);
        }
    }

    if (hasInst)
    {
        dLyr = dLyr > 1e8 ? dInst : (ib > 1e-4 ? sminPoly(dLyr, dInst, ib) : Math.min(dLyr, dInst));
    }
    return dLyr;
}

function evalRock2LayerWithAttachedMods(compiledStack, layerIndex, wx, wy, wz, outColor = null)
{
    const lyr = compiledStack.compiledLayers[layerIndex];
    const primitives = compiledStack.primitives;
    const mods = compiledStack.compiledModifiers || [];
    const att = lyr.attachedModifierIndices;

    const evalBase = (qx, qy, qz, col) =>
    {
        if (col)
        {
            col.r = lyr.color[0];
            col.g = lyr.color[1];
            col.b = lyr.color[2];
        }
        return evalRock2LayerRawPrimitives(lyr, primitives, qx, qy, qz);
    };

    if (!att || att.length === 0)
    {
        return evalBase(wx, wy, wz, outColor);
    }

    const evalChain = (mCursor, qx, qy, qz, col) =>
    {
        if (mCursor < 0) return evalBase(qx, qy, qz, col);
        const mod = mods[att[mCursor]];
        return evalRock2AnyModifier(mod, (ux, uy, uz, uCol) => evalChain(mCursor - 1, ux, uy, uz, uCol), qx, qy, qz, col);
    };
    return evalChain(att.length - 1, wx, wy, wz, outColor);
}

function combineRock2LayerSdf(dAcc, colAcc, dLyr, lr, lg, lbCol, op, br)
{
    if (dAcc > 1e8)
    {
        if (op === 3 || op === 5) return dAcc;
        colAcc.r = lr; colAcc.g = lg; colAcc.b = lbCol;
        return dLyr;
    }

    if (op === 0)
    {
        if (dLyr < dAcc) { colAcc.r = lr; colAcc.g = lg; colAcc.b = lbCol; return dLyr; }
        return dAcc;
    }
    if (op === 1)
    {
        const w = br > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / br, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
        colAcc.r = lerp(colAcc.r, lr, w); colAcc.g = lerp(colAcc.g, lg, w); colAcc.b = lerp(colAcc.b, lbCol, w);
        return sminPoly(dAcc, dLyr, br);
    }
    if (op === 2)
    {
        const w = br > 1e-4 ? clamp(0.5 + 0.5 * (dAcc - dLyr) / br, 0.0, 1.0) : (dLyr < dAcc ? 1.0 : 0.0);
        colAcc.r = lerp(colAcc.r, lr, w); colAcc.g = lerp(colAcc.g, lg, w); colAcc.b = lerp(colAcc.b, lbCol, w);
        return chamferMin(dAcc, dLyr, br);
    }
    if (op === 3)
    {
        const prev = dAcc;
        const next = smaxPoly(dAcc, -dLyr, br);
        const cutW = smoothstep(0.0, Math.max(0.02, br + 0.04), next - prev);
        colAcc.r = lerp(colAcc.r, lr, cutW * 0.65); colAcc.g = lerp(colAcc.g, lg, cutW * 0.65); colAcc.b = lerp(colAcc.b, lbCol, cutW * 0.65);
        return next;
    }
    if (op === 4)
    {
        const w = dLyr > dAcc ? 1.0 : 0.0;
        colAcc.r = lerp(colAcc.r, lr, w * 0.5); colAcc.g = lerp(colAcc.g, lg, w * 0.5); colAcc.b = lerp(colAcc.b, lbCol, w * 0.5);
        return smaxPoly(dAcc, dLyr, br);
    }
    if (op === 5)
    {
        const prev = dAcc;
        const next = chamferSubtract(dAcc, dLyr, br);
        const cutW = smoothstep(0.0, Math.max(0.02, br + 0.04), next - prev);
        colAcc.r = lerp(colAcc.r, lr, cutW * 0.70); colAcc.g = lerp(colAcc.g, lg, cutW * 0.70); colAcc.b = lerp(colAcc.b, lbCol, cutW * 0.70);
        return next;
    }
    return dAcc;
}

export function evalRock2StackSdf(compiledStack, wx, wy, wz, outColor = null)
{
    const { compiledLayers, compiledModifiers, executionSteps, primitives } = compiledStack;
    if (compiledLayers.length === 0)
    {
        if (outColor) { outColor.r = 0.58; outColor.g = 0.57; outColor.b = 0.55; }
        return 10.0;
    }

    if (!compiledModifiers || compiledModifiers.length === 0)
    {
        const col = { r: 0.58, g: 0.57, b: 0.55 };
        let dAcc = 1e9;
        for (let li = 0; li < compiledLayers.length; li++)
        {
            const lyr = compiledLayers[li];
            const dLyr = evalRock2LayerRawPrimitives(lyr, primitives, wx, wy, wz);
            dAcc = combineRock2LayerSdf(dAcc, col, dLyr, lyr.color[0], lyr.color[1], lyr.color[2], lyr.combineOp, lyr.blendRadius);
        }
        if (outColor) { outColor.r = col.r; outColor.g = col.g; outColor.b = col.b; }
        return dAcc;
    }

    const steps = executionSteps || [];
    const evalUpToStep = (stepEnd, px, py, pz, colOut) =>
    {
        let lastModStep = -1;
        for (let s = stepEnd; s >= 0; s--)
        {
            if (steps[s].kind === 'stack_modifier')
            {
                lastModStep = s;
                break;
            }
        }

        const accCol = { r: 0.58, g: 0.57, b: 0.55 };
        let dAcc = 1e9;
        let startIdx = 0;

        if (lastModStep >= 0)
        {
            const mod = compiledModifiers[steps[lastModStep].modIndex];
            dAcc = evalRock2AnyModifier(
                mod,
                (qx, qy, qz, uCol) => evalUpToStep(lastModStep - 1, qx, qy, qz, uCol),
                px, py, pz,
                accCol
            );
            startIdx = lastModStep + 1;
        }

        const lCol = { r: 0.58, g: 0.57, b: 0.55 };
        for (let s = startIdx; s <= stepEnd; s++)
        {
            const st = steps[s];
            if (st.kind !== 'rock_layer') continue;
            const lyr = compiledLayers[st.layerIndex];
            const dLyr = evalRock2LayerWithAttachedMods(compiledStack, st.layerIndex, px, py, pz, lCol);
            dAcc = combineRock2LayerSdf(dAcc, accCol, dLyr, lCol.r, lCol.g, lCol.b, lyr.combineOp, lyr.blendRadius);
        }

        if (colOut)
        {
            colOut.r = accCol.r;
            colOut.g = accCol.g;
            colOut.b = accCol.b;
            if (accCol._pullbackQ) colOut._pullbackQ = accCol._pullbackQ;
        }
        return dAcc;
    };

    return evalUpToStep(steps.length - 1, wx, wy, wz, outColor);
}

/**
 * Evaluates the exact Pure-SDF distance of a single compiled layer `layerIndex` at `(wx, wy, wz)`.
 */
export function evalRock2SingleLayerSdf(compiledStack, layerIndex, wx, wy, wz)
{
    const lyr = compiledStack?.compiledLayers?.[layerIndex];
    if (!lyr) return 1e9;
    return evalRock2LayerWithAttachedMods(compiledStack, layerIndex, wx, wy, wz, null);
}

/**
 * Sphere-traces a 3D viewport camera ray `(ro, rd)` against the compiled Pure-SDF stack
 * and returns the exact layer `{ layerId, layerIndex, hitPoint, distance }` whose surface was clicked!
 */
export function pickRock2LayerAtRay(compiledStack, ro, rd, options = {})
{
    if (!compiledStack || !compiledStack.compiledLayers || compiledStack.compiledLayers.length === 0)
    {
        return null;
    }

    const b = compiledStack.bounds;
    const invDx = 1.0 / (Math.abs(rd[0]) > 1e-8 ? rd[0] : 1e-8);
    const invDy = 1.0 / (Math.abs(rd[1]) > 1e-8 ? rd[1] : 1e-8);
    const invDz = 1.0 / (Math.abs(rd[2]) > 1e-8 ? rd[2] : 1e-8);

    const tx1 = (b.minX - ro[0]) * invDx, tx2 = (b.maxX - ro[0]) * invDx;
    const ty1 = (b.minY - ro[1]) * invDy, ty2 = (b.maxY - ro[1]) * invDy;
    const tz1 = (b.minZ - ro[2]) * invDz, tz2 = (b.maxZ - ro[2]) * invDz;

    const tEnter = Math.max(0.05, Math.min(tx1, tx2), Math.min(ty1, ty2), Math.min(tz1, tz2));
    const tExit  = Math.min(160.0, Math.max(tx1, tx2), Math.max(ty1, ty2), Math.max(tz1, tz2));
    if (tExit < tEnter) return null;

    let t = tEnter;
    let hit = false;
    for (let step = 0; step < 84 && t <= tExit; step++)
    {
        const wx = ro[0] + rd[0] * t;
        const wy = ro[1] + rd[1] * t;
        const wz = ro[2] + rd[2] * t;
        const d = evalRock2StackSdf(compiledStack, wx, wy, wz);
        if (d < 0.018)
        {
            hit = true;
            break;
        }
        t += Math.max(0.016, d * 0.85);
    }

    // Optional: allow clicking visible Semi-Transparent Red Boolean Ghost Volumes directly in the 3D Viewport!
    let boolGhostHit = null;
    if (options.pickBooleanGhosts)
    {
        const boolIndices = [];
        for (let li = 0; li < compiledStack.compiledLayers.length; li++)
        {
            if (compiledStack.compiledLayers[li].isBoolean) boolIndices.push(li);
        }
        if (boolIndices.length > 0)
        {
            const maxTBool = hit ? (t + 0.06) : tExit;
            let tB = tEnter;
            let hitBoolIdx = -1;
            for (let step = 0; step < 84 && tB <= maxTBool; step++)
            {
                const wx = ro[0] + rd[0] * tB;
                const wy = ro[1] + rd[1] * tB;
                const wz = ro[2] + rd[2] * tB;
                let minDB = 1e9;
                let bestBLi = -1;
                for (let k = 0; k < boolIndices.length; k++)
                {
                    const li = boolIndices[k];
                    const dL = evalRock2LayerRawPrimitives(compiledStack.compiledLayers[li], compiledStack.primitives, wx, wy, wz);
                    if (dL < minDB) { minDB = dL; bestBLi = li; }
                }
                if (minDB < 0.018)
                {
                    hitBoolIdx = bestBLi;
                    break;
                }
                tB += Math.max(0.016, minDB * 0.85);
            }

            if (hitBoolIdx >= 0 && (!hit || tB <= t + 0.04))
            {
                const bhx = ro[0] + rd[0] * tB;
                const bhy = ro[1] + rd[1] * tB;
                const bhz = ro[2] + rd[2] * tB;
                const bLayer = compiledStack.compiledLayers[hitBoolIdx];
                const primitives = compiledStack.primitives;
                let bInstIdx = 0;
                let bInstScore = 1e9;
                let curI = 0, curD = 1e9, hasC = false;
                const flushC = () =>
                {
                    if (!hasC) return;
                    if (Math.abs(curD) < bInstScore) { bInstScore = Math.abs(curD); bInstIdx = curI; }
                };
                for (let k = 0; k < bLayer.blockCount; k++)
                {
                    const prim = primitives[bLayer.blockStart + k];
                    const pOp = prim.primOp ?? -1.0;
                    if (pOp > -1.5 && pOp < -0.5)
                    {
                        flushC(); hasC = true; curI = prim.instanceIndex ?? 0;
                        curD = evalRock2PrimitiveSdf(prim, bhx, bhy, bhz);
                    }
                    else if (pOp >= 0.0)
                    {
                        const dP = evalRock2PrimitiveSdf(prim, bhx, bhy, bhz);
                        curD = curD > 1e8 ? dP : (pOp > 1e-4 ? sminPoly(curD, dP, pOp) : Math.min(curD, dP));
                        hasC = true;
                    }
                    else if (hasC)
                    {
                        const cutB = Math.max(0.0, -pOp - 2.0);
                        curD = smaxPoly(curD, -evalRock2PrimitiveSdf(prim, bhx, bhy, bhz), cutB);
                    }
                }
                flushC();
                boolGhostHit = {
                    layerId: bLayer.id,
                    layerIndex: hitBoolIdx,
                    instanceIndex: bInstIdx,
                    rawInstanceCount: bLayer.rawInstanceCount ?? bLayer.instanceCount ?? 1,
                    isBooleanGhost: true,
                    hitPoint: [bhx, bhy, bhz],
                    distance: tB
                };
            }
        }
    }

    if (boolGhostHit) return boolGhostHit;
    if (!hit) return null;

    const hx = ro[0] + rd[0] * t;
    const hy = ro[1] + rd[1] * t;
    const hz = ro[2] + rd[2] * t;

    // Pull back `[hx, hy, hz]` through any active stack modifiers so displaced blocks still map to their true source rock!
    let qx = hx, qy = hy, qz = hz;
    if (compiledStack.compiledModifiers && compiledStack.compiledModifiers.length > 0)
    {
        const probeCol = { r: 0.5, g: 0.5, b: 0.5 };
        evalRock2StackSdf(compiledStack, hx, hy, hz, probeCol);
        if (probeCol._pullbackQ)
        {
            qx = probeCol._pullbackQ[0];
            qy = probeCol._pullbackQ[1];
            qz = probeCol._pullbackQ[2];
        }
    }

    let bestIdx = 0;
    let bestScore = 1e9;
    for (let li = 0; li < compiledStack.compiledLayers.length; li++)
    {
        const lyr = compiledStack.compiledLayers[li];
        const dRaw = evalRock2LayerRawPrimitives(lyr, compiledStack.primitives, qx, qy, qz);
        const surfDist = Math.abs(dRaw);
        if (surfDist < bestScore)
        {
            bestScore = surfDist;
            bestIdx = li;
        }
    }

    // Identify the exact individual rock instance (`sourceIndex`) hit within the winning layer!
    const winLayer = compiledStack.compiledLayers[bestIdx];
    const primitives = compiledStack.primitives;
    let bestInstIdx = 0;
    let bestInstScore = 1e9;
    let curInstIdx = 0;
    let curInstD = 1e9;
    let hasCur = false;

    const flushCurInst = () =>
    {
        if (!hasCur) return;
        const sc = Math.abs(curInstD);
        if (sc < bestInstScore)
        {
            bestInstScore = sc;
            bestInstIdx = curInstIdx;
        }
    };

    for (let k = 0; k < winLayer.blockCount; k++)
    {
        const prim = primitives[winLayer.blockStart + k];
        const pOp = prim.primOp ?? -1.0;
        if (pOp > -1.5 && pOp < -0.5)
        {
            flushCurInst();
            hasCur = true;
            curInstIdx = prim.instanceIndex ?? 0;
            curInstD = evalRock2PrimitiveSdf(prim, qx, qy, qz);
        }
        else if (pOp >= 0.0)
        {
            const dP = evalRock2PrimitiveSdf(prim, qx, qy, qz);
            curInstD = curInstD > 1e8 ? dP : (pOp > 1e-4 ? sminPoly(curInstD, dP, pOp) : Math.min(curInstD, dP));
            hasCur = true;
        }
        else if (hasCur)
        {
            const cutB = Math.max(0.0, -pOp - 2.0);
            const dP = evalRock2PrimitiveSdf(prim, qx, qy, qz);
            curInstD = smaxPoly(curInstD, -dP, cutB);
        }
    }
    flushCurInst();

    return {
        layerId: winLayer.id,
        layerIndex: bestIdx,
        instanceIndex: bestInstIdx,
        rawInstanceCount: winLayer.rawInstanceCount ?? winLayer.instanceCount ?? 1,
        hitPoint: [hx, hy, hz],
        distance: t
    };
}

/*==========================================================================================================================================
                               8. GPU TEXTURE PACKER & QEF DUAL-CONTOUR MESH BAKER
==========================================================================================================================================*/

export function packRock2StackGpuBuffers(compiledStack, options = {})
{
    const { compiledLayers, compiledModifiers = [], executionSteps = [], primitives } = compiledStack;
    const selectedLayerId = options.selectedLayerId || '';
    const numLayers = Math.max(1, compiledLayers.length);
    const numBlocks = Math.max(1, primitives.length);
    const totalPlanes = Math.max(1, compiledStack.totalPlanes);

    const layerTexData = new Float32Array(numLayers * 2 * 4);
    const blockTexData = new Float32Array(numBlocks * 11 * 4);
    const planeTexData = new Float32Array(totalPlanes * 4);

    const boolLayers = [];
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

        if (lyr.isBoolean) boolLayers.push(lyr);
    }

    const boolLayerTexHeight = Math.max(1, boolLayers.length);
    const boolLayerTexData = new Float32Array(boolLayerTexHeight * 2 * 4);
    for (let bi = 0; bi < boolLayers.length; bi++)
    {
        const bl = boolLayers[bi];
        const bOff = bi * 8;
        boolLayerTexData[bOff + 0] = bl.blockStart;
        boolLayerTexData[bOff + 1] = bl.blockCount;
        boolLayerTexData[bOff + 2] = bl.combineOp;
        boolLayerTexData[bOff + 3] = bl.id === selectedLayerId ? 1.0 : 0.0;
        boolLayerTexData[bOff + 4] = bl.color[0];
        boolLayerTexData[bOff + 5] = bl.color[1];
        boolLayerTexData[bOff + 6] = bl.color[2];
        boolLayerTexData[bOff + 7] = bl.intraBlend;
    }

    let planeCursor = 0;
    for (let i = 0; i < primitives.length; i++)
    {
        const b = primitives[i];
        const off = i * 44;

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
        blockTexData[off + 27] = b.primOp ?? -1.0;

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

        blockTexData[off + 40] = b.ringFreq ?? 0.0;
        blockTexData[off + 41] = b.ringAmp ?? 0.0;
        blockTexData[off + 42] = b.ringPhase ?? 0.0;
        blockTexData[off + 43] = b.pinchHeightNorm ?? 0.34;

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

    const numMods = compiledModifiers.length;
    const faultMods = [];
    const sculptMods = [];
    const modGpuEntry = new Array(numMods);

    let totalFaultPlanes = 0;
    let totalFaultBlocks = 0;
    let totalSculptDabs = 0;

    for (let mi = 0; mi < numMods; mi++)
    {
        const m = compiledModifiers[mi];
        if (m.type === 'sculpt_layer')
        {
            const sIdx = sculptMods.length;
            sculptMods.push(m);
            totalSculptDabs += (m.dabs?.length || 0);
            modGpuEntry[mi] = { kind: 'sculpt_layer', sculptIndex: sIdx, dabs: m.dabs?.length || 0 };
        }
        else
        {
            const fIdx = faultMods.length;
            faultMods.push(m);
            totalFaultPlanes += (m.planes?.length || 0);
            totalFaultBlocks += (m.blocks?.length || 0);
            modGpuEntry[mi] = { kind: 'split_fault', modIndex: fIdx };
        }
    }

    const modMetaHeight = Math.max(1, faultMods.length);
    const faultPlaneHeight = Math.max(1, totalFaultPlanes);
    const faultBlockHeight = Math.max(1, totalFaultBlocks);

    const modMetaTexData = new Float32Array(modMetaHeight * 5 * 4);
    const faultPlaneTexData = new Float32Array(faultPlaneHeight * 4 * 4);
    const faultBlockTexData = new Float32Array(faultBlockHeight * 4 * 4);

    let fpCursor = 0;
    let fbCursor = 0;
    for (let fi = 0; fi < faultMods.length; fi++)
    {
        const m = faultMods[fi];
        const mOff = fi * 20;

        modMetaTexData[mOff + 0] = fpCursor;
        modMetaTexData[mOff + 1] = m.planes.length;
        modMetaTexData[mOff + 2] = fbCursor;
        modMetaTexData[mOff + 3] = m.blocks.length;

        modMetaTexData[mOff + 4] = m.macroWavinessAmp;
        modMetaTexData[mOff + 5] = m.macroWavelength;
        modMetaTexData[mOff + 6] = m.microJaggednessAmp;
        modMetaTexData[mOff + 7] = m.microFrequency;

        modMetaTexData[mOff + 8]  = m.octaves;
        modMetaTexData[mOff + 9]  = m.anisotropyStretch;
        modMetaTexData[mOff + 10] = m.roughPenalty;
        modMetaTexData[mOff + 11] = m.crackGapWidth;

        modMetaTexData[mOff + 12] = m.lipChamfer;
        modMetaTexData[mOff + 13] = m.vTaperDepth;
        modMetaTexData[mOff + 14] = m.tipSharpness;
        modMetaTexData[mOff + 15] = m.useCustomTone;

        modMetaTexData[mOff + 16] = m.faceColor[0];
        modMetaTexData[mOff + 17] = m.faceColor[1];
        modMetaTexData[mOff + 18] = m.faceColor[2];
        modMetaTexData[mOff + 19] = m.splitModeCode;

        for (let pi = 0; pi < m.planes.length; pi++)
        {
            const pl = m.planes[pi];
            const pOff = (fpCursor + pi) * 16;
            faultPlaneTexData[pOff + 0]  = pl.ox; faultPlaneTexData[pOff + 1]  = pl.oy; faultPlaneTexData[pOff + 2]  = pl.oz; faultPlaneTexData[pOff + 3]  = pl.offset;
            faultPlaneTexData[pOff + 4]  = pl.nx; faultPlaneTexData[pOff + 5]  = pl.ny; faultPlaneTexData[pOff + 6]  = pl.nz; faultPlaneTexData[pOff + 7]  = pl.seed;
            faultPlaneTexData[pOff + 8]  = pl.sx; faultPlaneTexData[pOff + 9]  = pl.sy; faultPlaneTexData[pOff + 10] = pl.sz; faultPlaneTexData[pOff + 11] = 0.0;
            faultPlaneTexData[pOff + 12] = pl.dx; faultPlaneTexData[pOff + 13] = pl.dy; faultPlaneTexData[pOff + 14] = pl.dz; faultPlaneTexData[pOff + 15] = 0.0;
        }
        fpCursor += m.planes.length;

        for (let bi = 0; bi < m.blocks.length; bi++)
        {
            const blk = m.blocks[bi];
            const bOff = (fbCursor + bi) * 16;
            faultBlockTexData[bOff + 0]  = blk.invR[0]; faultBlockTexData[bOff + 1]  = blk.invR[1]; faultBlockTexData[bOff + 2]  = blk.invR[2]; faultBlockTexData[bOff + 3]  = blk.invT[0];
            faultBlockTexData[bOff + 4]  = blk.invR[3]; faultBlockTexData[bOff + 5]  = blk.invR[4]; faultBlockTexData[bOff + 6]  = blk.invR[5]; faultBlockTexData[bOff + 7]  = blk.invT[1];
            faultBlockTexData[bOff + 8]  = blk.invR[6]; faultBlockTexData[bOff + 9]  = blk.invR[7]; faultBlockTexData[bOff + 10] = blk.invR[8]; faultBlockTexData[bOff + 11] = blk.invT[2];
            faultBlockTexData[bOff + 12] = blk.f0;      faultBlockTexData[bOff + 13] = blk.f1;      faultBlockTexData[bOff + 14] = blk.f2;      faultBlockTexData[bOff + 15] = blk.f3;
        }
        fbCursor += m.blocks.length;
    }

    const sculptMetaHeight = Math.max(1, sculptMods.length);
    const sculptDabHeight = Math.max(1, totalSculptDabs);
    const sculptMetaTexData = new Float32Array(sculptMetaHeight * 4);
    const sculptDabTexData = new Float32Array(sculptDabHeight * 4 * 4);

    let sdCursor = 0;
    for (let si = 0; si < sculptMods.length; si++)
    {
        const sm = sculptMods[si];
        const dabs = sm.dabs || [];
        sculptMetaTexData[si * 4 + 0] = sdCursor;
        sculptMetaTexData[si * 4 + 1] = dabs.length;
        sculptMetaTexData[si * 4 + 2] = 0.0;
        sculptMetaTexData[si * 4 + 3] = 0.0;

        for (let di = 0; di < dabs.length; di++)
        {
            const db = dabs[di];
            const dOff = (sdCursor + di) * 16;
            sculptDabTexData[dOff + 0]  = db.cx;   sculptDabTexData[dOff + 1]  = db.cy;   sculptDabTexData[dOff + 2]  = db.cz;   sculptDabTexData[dOff + 3]  = db.r;
            sculptDabTexData[dOff + 4]  = db.nx;   sculptDabTexData[dOff + 5]  = db.ny;   sculptDabTexData[dOff + 6]  = db.nz;   sculptDabTexData[dOff + 7]  = db.s;
            sculptDabTexData[dOff + 8]  = db.tx;   sculptDabTexData[dOff + 9]  = db.tz;   sculptDabTexData[dOff + 10] = db.opCode; sculptDabTexData[dOff + 11] = db.h;
            sculptDabTexData[dOff + 12] = db.colR; sculptDabTexData[dOff + 13] = db.colG; sculptDabTexData[dOff + 14] = db.colB; sculptDabTexData[dOff + 15] = db.useTone;
        }
        sdCursor += dabs.length;
    }

    const gpuPasses = [];
    if (numMods === 0)
    {
        gpuPasses.push({ kind: 'plain', layerStart: 0, layerCount: compiledLayers.length, hasPrevVol: false });
    }
    else
    {
        let hasMainVol = false;
        let runStart = -1;
        let runCount = 0;

        const flushPlainRun = () =>
        {
            if (runCount <= 0) return;
            gpuPasses.push({ kind: 'plain', layerStart: runStart, layerCount: runCount, hasPrevVol: hasMainVol });
            hasMainVol = true;
            runStart = -1;
            runCount = 0;
        };

        for (let s = 0; s < executionSteps.length; s++)
        {
            const st = executionSteps[s];
            if (st.kind === 'stack_modifier')
            {
                const entry = modGpuEntry[st.modIndex];
                if (!entry || (entry.kind === 'sculpt_layer' && entry.dabs === 0)) continue;
                flushPlainRun();
                if (hasMainVol)
                {
                    if (entry.kind === 'sculpt_layer')
                    {
                        gpuPasses.push({ kind: 'sculpt_layer', sculptIndex: entry.sculptIndex });
                    }
                    else
                    {
                        gpuPasses.push({ kind: 'split_fault', modIndex: entry.modIndex });
                    }
                }
            }
            else if (st.kind === 'rock_layer')
            {
                const cl = compiledLayers[st.layerIndex];
                if (cl.attachedModifierIndices && cl.attachedModifierIndices.length > 0)
                {
                    const modSteps = cl.attachedModifierIndices
                        .map(idx => modGpuEntry[idx])
                        .filter(e => e && !(e.kind === 'sculpt_layer' && e.dabs === 0));
                    if (modSteps.length > 0)
                    {
                        flushPlainRun();
                        gpuPasses.push({
                            kind: 'single_layer_mod',
                            layerIndex: st.layerIndex,
                            modSteps,
                            modIndices: modSteps.filter(e => e.kind === 'split_fault').map(e => e.modIndex),
                            combineOp: cl.combineOp,
                            blendRadius: cl.blendRadius,
                            hasPrevVol: hasMainVol
                        });
                        hasMainVol = true;
                    }
                    else
                    {
                        if (runCount === 0) runStart = st.layerIndex;
                        runCount++;
                    }
                }
                else
                {
                    if (runCount === 0) runStart = st.layerIndex;
                    runCount++;
                }
            }
        }
        flushPlainRun();
    }

    return {
        numLayers: compiledLayers.length,
        numBlocks: primitives.length,
        totalPlanes: compiledStack.totalPlanes,
        layerTexWidth: 2,
        layerTexHeight: numLayers,
        layerTexData,
        blockTexWidth: 11,
        blockTexHeight: numBlocks,
        blockTexData,
        planeTexWidth: 1,
        planeTexHeight: totalPlanes,
        planeTexData,
        hasBooleanLayers: boolLayers.length > 0,
        numBooleanLayers: boolLayers.length,
        boolLayerTexWidth: 2,
        boolLayerTexHeight,
        boolLayerTexData,
        numModifiers: numMods,
        modMetaWidth: 5,
        modMetaHeight,
        modMetaTexWidth: 5,
        modMetaTexHeight: modMetaHeight,
        modMetaTexData,
        faultPlaneWidth: 4,
        faultPlaneHeight,
        faultPlaneTexWidth: 4,
        faultPlaneTexHeight: faultPlaneHeight,
        faultPlaneTexData,
        faultBlockWidth: 4,
        faultBlockHeight,
        faultBlockTexWidth: 4,
        faultBlockTexHeight: faultBlockHeight,
        faultBlockTexData,
        sculptMetaTexWidth: 1,
        sculptMetaTexHeight: sculptMetaHeight,
        sculptMetaTexData,
        sculptDabTexWidth: 4,
        sculptDabTexHeight: sculptDabHeight,
        sculptDabTexData,
        gpuPasses
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
    const modMargin = compiledStack.maxModifierDisplacement ?? 0.0;

    const sdf = evaluateSparseSdf(bounds, voxelSize, {
        positiveDisplacementBound: dispBound,
        negativeDisplacementBound: dispBound,
        fineDisplacementBound: dispBound,
        buildBrickContext(cx, cy, cz, cullRadius)
        {
            const margin = cullRadius + 0.45 + modMargin;
            for (let i = 0; i < primitives.length; i++)
            {
                const b = primitives[i];
                if (b.primOp <= -2.0) continue;
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
