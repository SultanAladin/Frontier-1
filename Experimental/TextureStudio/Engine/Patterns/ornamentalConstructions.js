import { ornamentalInfill } from "./ornamentDrawing.js";
import { referenceBuilder } from "./referencePatterns.js";

// Each row specifies a different panel graph/connection rule. No color, scale,
// seed, density or rotation participates in the construction identity.
const constructionRows = [
  [
    "Shield and Spear Inlay",
    "African inlay",
    "shield",
    "split",
    "Stepped shields meet paired spear-shaped corner panels.",
  ],
  [
    "Four Gate Marquetry",
    "African inlay",
    "gates",
    "nested",
    "Four opposed gates enclose a square central chamber.",
  ],
  [
    "Angular Knot Windows",
    "African inlay",
    "knot",
    "hook",
    "Bent arms interlock around inset square windows.",
  ],
  [
    "Cut-Pile Windmill Panels",
    "African inlay",
    "windmill",
    "quartered",
    "Four long blades spiral around a central square.",
  ],
  [
    "Raffia Basket Chambers",
    "African inlay",
    "basket",
    "labyrinth",
    "Alternating paired planks terminate at square junctions.",
  ],
  [
    "Crossed Ladder Applique",
    "African inlay",
    "cross",
    "ladder",
    "A connected cross spine separates four corner chambers.",
  ],
  [
    "Broken Octagon Patchwork",
    "African inlay",
    "octagon",
    "hook",
    "Octagonal chambers meet small square junctions.",
  ],
  [
    "Split Hourglass Tapestry",
    "African inlay",
    "hourglass",
    "nested",
    "Opposing trapezoids meet at an inset diamond.",
  ],
  [
    "Spearhead Courtyards",
    "African inlay",
    "spear",
    "ladder",
    "Paired pointed pentagons leave a four-sided center court.",
  ],
  [
    "Inlaid Arrow Crossroads",
    "African inlay",
    "arrows",
    "quartered",
    "Four arrowheads converge on a diamond crossing.",
  ],
  [
    "Raffia Offset Portals",
    "African inlay",
    "portals",
    "labyrinth",
    "Alternating wide and narrow lintels form staggered portals.",
  ],
  [
    "Nested Shield Compartments",
    "African inlay",
    "chambers",
    "hook",
    "Large shield cells are separated by a network of small lozenges.",
  ],
  [
    "Woven Diagonal Planks",
    "African inlay",
    "diagonal",
    "ladder",
    "Offset diagonal parallelograms join alternating triangular ends.",
  ],
  [
    "Stepped Diamond Saddlework",
    "African inlay",
    "stepped",
    "split",
    "Stair-stepped central shields share toothed edge compartments.",
  ],
  [
    "Raffia Nine-Room Quilt",
    "African inlay",
    "nine-room",
    "labyrinth",
    "A central court, four corridors and four outer square rooms.",
  ],
  [
    "Crossed Shuttle Weft",
    "African inlay",
    "shuttle",
    "nested",
    "Interleaved long shuttles cross between diamond junctions.",
  ],
  [
    "Double-Lintel Panelwork",
    "African inlay",
    "lintel",
    "quartered",
    "Nested rectangular gateways alternate with transverse short lintels.",
  ],
  [
    "Pinched Hexagon Applique",
    "African inlay",
    "pinched",
    "hook",
    "Waisted hexagons join paired triangular side panels.",
  ],
  [
    "Joined Elbow Labyrinth",
    "African inlay",
    "elbows",
    "labyrinth",
    "Opposed L-shaped bands terminate in square turning chambers.",
  ],
  [
    "Triangular Shield Fans",
    "African inlay",
    "fan",
    "split",
    "Six triangular compartments spread around an off-center hub.",
  ],
  [
    "Woven Diamond Bridges",
    "African inlay",
    "bridges",
    "ladder",
    "Diamond centers connect through narrow bridge corridors.",
  ],
  [
    "Octagon Square Palmette Network",
    "Islamic networks",
    "octagon",
    "palmette",
    "Eight-sided floral chambers alternate with four-sided junctions.",
  ],
  [
    "Sixfold Hexagonal Vinework",
    "Islamic networks",
    "hexagon",
    "vine",
    "A honeycomb field carries sixfold branching vine interiors.",
  ],
  [
    "Trihexagonal Rosette Network",
    "Islamic networks",
    "trihex",
    "rosette",
    "Hexagonal medallions link through triangular interstices.",
  ],
  [
    "Twelvefold Rosette and Kitework",
    "Islamic networks",
    "dodecagon",
    "rosette",
    "Twelve-sided rosettes surround four triangular corner kites.",
  ],
  [
    "Square Octagonal Strapwork",
    "Islamic networks",
    "octagon",
    "strap",
    "Crossed square straps intersect within octagonal tile boundaries.",
  ],
  [
    "Diamond Bowtie Arabesque",
    "Islamic networks",
    "bowtie",
    "vine",
    "Large diamonds are joined by pinched bowtie compartments.",
  ],
  [
    "Eightfold Kite Assembly",
    "Islamic networks",
    "kites",
    "palmette",
    "Eight kite panels assemble into a connected radial crossing.",
  ],
  [
    "Interlocked Cross Cartouches",
    "Islamic networks",
    "cross",
    "vine",
    "Foliate cross corridors interlock around square corner cartouches.",
  ],
  [
    "Pentagonal Gate Arabesques",
    "Islamic networks",
    "gates",
    "palmette",
    "Opposed pentagonal niches meet a square floral central court.",
  ],
  [
    "Hexagonal Triangle Strapwork",
    "Islamic networks",
    "trihex",
    "strap",
    "Strap rosettes occupy hexagons, with contrasting triangular knots.",
  ],
  [
    "Eightfold Square Interlace",
    "Islamic networks",
    "square",
    "strap",
    "Two square circuits weave through an eightfold angular star.",
  ],
  [
    "Twelvefold Petal Cartouches",
    "Islamic networks",
    "petal12",
    "palmette",
    "Twelve almond-shaped compartments radiate between corner pockets.",
  ],
  [
    "Sixfold Rhombic Starwork",
    "Islamic networks",
    "rhombi",
    "rosette",
    "Three rhombic orientations meet at sixfold star junctions.",
  ],
  [
    "Linked Quatrefoil Arabesque",
    "Islamic networks",
    "quatrefoil",
    "vine",
    "Four lobed chambers share central and corner diamond knots.",
  ],
  [
    "Woven Octagonal Crossings",
    "Islamic networks",
    "chambers",
    "strap",
    "Octagonal rings are connected through narrow crossed bridge pieces.",
  ],
  [
    "Pointed Ogive Tilework",
    "Islamic networks",
    "ogive",
    "palmette",
    "Pointed lens compartments nest in alternating upright and inverted rows.",
  ],
  [
    "Stellated Honeycomb Inlay",
    "Islamic networks",
    "hexagon",
    "strap",
    "Six-lobed strap networks meet at the vertices of a honeycomb.",
  ],
  [
    "Floral Fan Tilework",
    "Islamic networks",
    "fan",
    "vine",
    "Asymmetric triangular fan panels enclose clipped trailing vines.",
  ],
  [
    "Eightfold Petal Inlay",
    "Islamic networks",
    "petal8",
    "rosette",
    "Eight pointed petal chambers surround a central rosette.",
  ],
  [
    "Cross and Shield Tilework",
    "Islamic networks",
    "shield",
    "palmette",
    "Central octagonal shields join cross-like split corner compartments.",
  ],
  [
    "Woven Diamond Star Junctions",
    "Islamic networks",
    "bridges",
    "strap",
    "Diamond star centers join through paired over-under bridge channels.",
  ],
  [
    "Four Gardens and Waterways",
    "Ornamental carpets",
    "four-gardens",
    "tree",
    "Four walled gardens separated by a continuous crossed waterway.",
  ],
  [
    "Seven Niche Courtyard",
    "Ornamental carpets",
    "seven-niches",
    "vase",
    "Seven unequal arched niches surround a transverse central court.",
  ],
  [
    "Tree and Vase Terrace",
    "Ornamental carpets",
    "terraces",
    "tree",
    "Terraced long plots alternate with upright tree and vase compartments.",
  ],
  [
    "Lobed Medallion and Pendants",
    "Ornamental carpets",
    "pendants",
    "vine",
    "A large lobed center is flanked by linked pointed pendants and spandrels.",
  ],
  [
    "Twelve Garden Satellites",
    "Ornamental carpets",
    "satellites",
    "vase",
    "A central garden is surrounded by twelve separately bounded satellites.",
  ],
  [
    "Mirrored Cypress Aisles",
    "Ornamental carpets",
    "aisles",
    "tree",
    "Opposed cypress aisles terminate at a central transversal.",
  ],
  [
    "Paired Prayer Niches",
    "Ornamental carpets",
    "paired-niches",
    "vase",
    "Two facing arched niches are joined by a narrow decorated waist.",
  ],
  [
    "Nine Court Palmette Garden",
    "Ornamental carpets",
    "nine-room",
    "palmette",
    "Nine linked courts alternate long rectangular and square enclosures.",
  ],
  [
    "Staircase Garden Terraces",
    "Ornamental carpets",
    "stair-garden",
    "tree",
    "Nested stepped terraces frame a tall central tree garden.",
  ],
  [
    "Compass Rose Compartment Rug",
    "Ornamental carpets",
    "compass",
    "vine",
    "Eight radial compartments meet a center, four kite spandrels and a frame.",
  ],
  [
    "Three Connected Floral Shields",
    "Ornamental carpets",
    "three-shields",
    "palmette",
    "Three large overlapping shield boundaries share narrow foliate connecting bands.",
  ],
  [
    "Vase Arcade and Spandrels",
    "Ornamental carpets",
    "arcade",
    "vase",
    "A two-level arcade separates arched vase panels from triangular spandrels.",
  ],
  [
    "Fourfold Garden Labyrinth",
    "Ornamental carpets",
    "garden-maze",
    "tree",
    "Bent garden corridors wind around four separately enclosed flower beds.",
  ],
  [
    "Central Cypress and Rosettes",
    "Ornamental carpets",
    "cypress-court",
    "tree",
    "A tall central tree court is flanked by paired rosette compartments.",
  ],
  [
    "Diagonal Palmette Galleries",
    "Ornamental carpets",
    "galleries",
    "palmette",
    "Diagonal galleries are interrupted by an upright central floral chamber.",
  ],
  [
    "Star Pavilion and Corner Gardens",
    "Ornamental carpets",
    "pavilion",
    "vase",
    "A stellated pavilion is surrounded by four square walled gardens.",
  ],
  [
    "Chain of Foliate Cartouches",
    "Ornamental carpets",
    "cartouche-chain",
    "vine",
    "Alternating long and short linked cartouches share a continuous spine.",
  ],
  [
    "Twin Lotus Pools",
    "Ornamental carpets",
    "lotus-pools",
    "rosette",
    "Two circular lotus pools link through a square island and triangular shore plots.",
  ],
  [
    "Cypress River Delta",
    "Ornamental carpets",
    "delta",
    "tree",
    "A branching three-way waterway separates three tree gardens and inlet plots.",
  ],
  [
    "Five Chamber Floral Screen",
    "Ornamental carpets",
    "five-chambers",
    "vase",
    "A wide central garden is surrounded by four unequal outer galleries.",
  ],
  [
    "Octagonal Fountain Court",
    "Ornamental carpets",
    "fountain",
    "palmette",
    "An octagonal fountain meets four radial garden corridors and corner beds.",
  ],
];
export const ornamentalCatalog = constructionRows.map(
  ([name, group, plan, craft, description]) => ({
    name,
    group,
    plan,
    craft,
    description,
    id: name.toLowerCase().replaceAll(" ", "-"),
    structureKey: `panelgraph:${plan}/interior:${craft}`,
    detailed: true,
  }),
);

// All cell paths below are geometry; colorways do not affect them.
const sq = (x, y, w, h = w) => [
  [x, y],
  [x + w, y],
  [x + w, y + h],
  [x, y + h],
];
const regular = (n, x, y, r, phase = -Math.PI / 2) =>
  Array.from({ length: n }, (_, i) => [
    x + r * Math.cos(phase + (i * 2 * Math.PI) / n),
    y + r * Math.sin(phase + (i * 2 * Math.PI) / n),
  ]);
function compartmentGraph(plan) {
  const cells = [],
    put = (p) => cells.push(p),
    box = (x, y, w, h = w) => put(sq(x, y, w, h));
  const ring = (n, r = 0.33, cx = 0.5, cy = 0.5) => {
    const inner = regular(n, cx, cy, r),
      outer = regular(n, cx, cy, 0.72);
    put(inner);
    for (let i = 0; i < n; i++)
      put([inner[i], outer[i], outer[(i + 1) % n], inner[(i + 1) % n]]);
  };
  const rotate = (p, a) =>
    p.map(([x, y]) => [
      0.5 + (x - 0.5) * Math.cos(a) - (y - 0.5) * Math.sin(a),
      0.5 + (x - 0.5) * Math.sin(a) + (y - 0.5) * Math.cos(a),
    ]);
  switch (plan) {
    case "square":
      box(0, 0, 1);
      break;
    case "octagon": {
      const k = 0.29;
      put([
        [k, 0],
        [1 - k, 0],
        [1, k],
        [1, 1 - k],
        [1 - k, 1],
        [k, 1],
        [0, 1 - k],
        [0, k],
      ]);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [k, 0],
              [0, k],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    }
    case "dodecagon":
      ring(12, 0.41);
      break;
    case "hexagon":
      put(regular(6, 0.5, 0.5, 0.57, Math.PI / 6));
      break;
    case "trihex":
      put(regular(6, 0.5, 0.5, 0.35, Math.PI / 6));
      for (let i = 0; i < 6; i++) {
        const a = (i * Math.PI) / 3 + Math.PI / 6;
        put([
          [0.5 + Math.cos(a) * 0.35, 0.5 + Math.sin(a) * 0.35],
          [
            0.5 + Math.cos(a + Math.PI / 3) * 0.35,
            0.5 + Math.sin(a + Math.PI / 3) * 0.35,
          ],
          [
            0.5 + Math.cos(a + Math.PI / 6) * 0.7,
            0.5 + Math.sin(a + Math.PI / 6) * 0.7,
          ],
        ]);
      }
      break;
    case "rhombi":
      for (let i = 0; i < 3; i++)
        put(
          rotate(
            [
              [0.5, 0.5],
              [0.5, 0],
              [0.933, 0.25],
              [0.933, 0.75],
            ],
            (i * Math.PI * 2) / 3,
          ),
        );
      break;
    case "shield":
      put([
        [0.2, 0.2],
        [0.5, 0],
        [0.8, 0.2],
        [1, 0.5],
        [0.8, 0.8],
        [0.5, 1],
        [0.2, 0.8],
        [0, 0.5],
      ]);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.5, 0],
              [0.2, 0.2],
              [0, 0.5],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "gates":
      box(0.32, 0.32, 0.36);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [1, 0],
              [0.68, 0.32],
              [0.32, 0.32],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "knot":
      box(0.34, 0.34, 0.32);
      for (let i = 0; i < 4; i++) {
        put(rotate(sq(0, 0, 0.66, 0.18), (i * Math.PI) / 2));
        put(rotate(sq(0.18, 0.18, 0.48, 0.16), (i * Math.PI) / 2));
      }
      break;
    case "windmill":
      box(0.33, 0.33, 0.34);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.67, 0],
              [0.67, 0.33],
              [0.33, 0.33],
              [0.33, 0.67],
              [0, 0.67],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "basket":
      box(0, 0, 0.5, 0.25);
      box(0, 0.25, 0.5, 0.25);
      box(0.5, 0, 0.25, 0.5);
      box(0.75, 0, 0.25, 0.5);
      box(0, 0.5, 0.25, 0.5);
      box(0.25, 0.5, 0.25, 0.5);
      box(0.5, 0.5, 0.5, 0.25);
      box(0.5, 0.75, 0.5, 0.25);
      break;
    case "cross":
      box(0.35, 0, 0.3, 1);
      box(0, 0.35, 0.35, 0.3);
      box(0.65, 0.35, 0.35, 0.3);
      for (let x of [0, 0.65]) for (let y of [0, 0.65]) box(x, y, 0.35);
      break;
    case "hourglass":
      put([
        [0, 0],
        [1, 0],
        [0.65, 0.5],
        [0.35, 0.5],
      ]);
      put([
        [0, 1],
        [1, 1],
        [0.65, 0.5],
        [0.35, 0.5],
      ]);
      put([
        [0, 0],
        [0.35, 0.5],
        [0, 1],
      ]);
      put([
        [1, 0],
        [0.65, 0.5],
        [1, 1],
      ]);
      break;
    case "spear":
      put([
        [0, 0],
        [0.5, 0.18],
        [0.5, 0.82],
        [0, 1],
        [0.2, 0.5],
      ]);
      put([
        [1, 0],
        [0.5, 0.18],
        [0.5, 0.82],
        [1, 1],
        [0.8, 0.5],
      ]);
      put([
        [0, 0],
        [1, 0],
        [0.5, 0.18],
      ]);
      put([
        [0, 1],
        [1, 1],
        [0.5, 0.82],
      ]);
      break;
    case "arrows":
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [1, 0],
              [0.5, 0.5],
              [0.5, 0.22],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "portals":
      box(0, 0, 1, 0.18);
      box(0, 0.18, 0.18, 0.82);
      box(0.82, 0.18, 0.18, 0.82);
      box(0.18, 0.18, 0.64, 0.62);
      box(0.18, 0.8, 0.64, 0.2);
      break;
    case "chambers":
      ring(8, 0.33);
      break;
    case "diagonal":
      put([
        [0, 0],
        [0.35, 0],
        [1, 0.65],
        [1, 1],
      ]);
      put([
        [0, 0],
        [1, 1],
        [0.65, 1],
        [0, 0.35],
      ]);
      put([
        [0.35, 0],
        [1, 0],
        [1, 0.65],
      ]);
      put([
        [0, 0.35],
        [0.65, 1],
        [0, 1],
      ]);
      break;
    case "stepped": {
      const p = [
        [0.35, 0],
        [0.65, 0],
        [0.65, 0.18],
        [0.82, 0.18],
        [0.82, 0.35],
        [1, 0.35],
        [1, 0.65],
        [0.82, 0.65],
        [0.82, 0.82],
        [0.65, 0.82],
        [0.65, 1],
        [0.35, 1],
        [0.35, 0.82],
        [0.18, 0.82],
        [0.18, 0.65],
        [0, 0.65],
        [0, 0.35],
        [0.18, 0.35],
        [0.18, 0.18],
        [0.35, 0.18],
      ];
      put(p);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.35, 0],
              [0.35, 0.18],
              [0.18, 0.18],
              [0.18, 0.35],
              [0, 0.35],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    }
    case "nine-room":
      for (let j = 0; j < 3; j++)
        for (let i = 0; i < 3; i++) {
          const cuts = [0, 0.25, 0.75, 1];
          box(cuts[i], cuts[j], cuts[i + 1] - cuts[i], cuts[j + 1] - cuts[j]);
        }
      break;
    case "shuttle":
      put([
        [0, 0.25],
        [0.25, 0],
        [1, 0.75],
        [0.75, 1],
      ]);
      put([
        [0.75, 0],
        [1, 0.25],
        [0.25, 1],
        [0, 0.75],
      ]);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.25, 0],
              [0, 0.25],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "lintel":
      for (let j = 0; j < 3; j++) {
        const y = j / 3;
        box(j % 2 ? 0 : 0.2, y, 0.8, 1 / 3);
        box(j % 2 ? 0.8 : 0, y, 0.2, 1 / 3);
      }
      break;
    case "pinched":
      put([
        [0, 0],
        [1, 0],
        [0.75, 0.5],
        [1, 1],
        [0, 1],
        [0.25, 0.5],
      ]);
      put([
        [0, 0],
        [0.25, 0.5],
        [0, 1],
      ]);
      put([
        [1, 0],
        [0.75, 0.5],
        [1, 1],
      ]);
      break;
    case "elbows":
      put([
        [0, 0],
        [1, 0],
        [1, 0.25],
        [0.25, 0.25],
        [0.25, 1],
        [0, 1],
      ]);
      box(0.25, 0.25, 0.5);
      put([
        [0.75, 0.25],
        [1, 0.25],
        [1, 1],
        [0.25, 1],
        [0.25, 0.75],
        [0.75, 0.75],
      ]);
      break;
    case "fan": {
      const edge = [
        [0, 0],
        [0.5, 0],
        [1, 0],
        [1, 1],
        [0.5, 1],
        [0, 1],
      ];
      for (let i = 0; i < 6; i++)
        put([[0.38, 0.5], edge[i], edge[(i + 1) % 6]]);
      break;
    }
    case "bridges":
      put([
        [0.5, 0.15],
        [0.85, 0.5],
        [0.5, 0.85],
        [0.15, 0.5],
      ]);
      box(0.4, 0, 0.2, 0.15);
      box(0.4, 0.85, 0.2, 0.15);
      box(0, 0.4, 0.15, 0.2);
      box(0.85, 0.4, 0.15, 0.2);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.4, 0],
              [0.4, 0.25],
              [0.25, 0.4],
              [0, 0.4],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "bowtie":
      put([
        [0.5, 0],
        [1, 0.5],
        [0.5, 1],
        [0, 0.5],
      ]);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.5, 0],
              [0, 0.5],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "kites":
      for (let i = 0; i < 8; i++)
        put(
          rotate(
            [
              [0.5, 0.5],
              [0.35, 0.15],
              [0.5, 0],
              [0.65, 0.15],
            ],
            (i * Math.PI) / 4,
          ),
        );
      break;
    case "petal8":
    case "petal12": {
      const n = plan === "petal8" ? 8 : 12;
      for (let i = 0; i < n; i++) {
        const a = (i * 2 * Math.PI) / n;
        put(
          rotate(
            [
              [0.5, 0.5],
              [0.37, 0.22],
              [0.5, 0],
              [0.63, 0.22],
            ],
            a,
          ),
        );
      }
      break;
    }
    case "quatrefoil":
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0.5, 0.5],
              [0.2, 0.35],
              [0.15, 0.15],
              [0.35, 0.2],
            ],
            (i * Math.PI) / 2,
          ),
        );
      put(regular(4, 0.5, 0.5, 0.16));
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.5, 0],
              [0.35, 0.2],
              [0.15, 0.15],
              [0.2, 0.35],
              [0, 0.5],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    case "ogive":
      put([
        [0.5, 0],
        [0.82, 0.25],
        [1, 0.5],
        [0.82, 0.75],
        [0.5, 1],
        [0.18, 0.75],
        [0, 0.5],
        [0.18, 0.25],
      ]);
      for (let i = 0; i < 4; i++)
        put(
          rotate(
            [
              [0, 0],
              [0.5, 0],
              [0.18, 0.25],
              [0, 0.5],
            ],
            (i * Math.PI) / 2,
          ),
        );
      break;
    default:
      throw new Error("Unknown compartment graph: " + plan);
  }
  return cells;
}

function courtGraph(plan) {
  const cells = [],
    put = (p) => cells.push(p),
    box = (x, y, w, h = w) => put(sq(x, y, w, h));
  const diamond = (x, y, rx, ry = rx) =>
    put([
      [x, y - ry],
      [x + rx, y],
      [x, y + ry],
      [x - rx, y],
    ]);
  const niche = (x, y, w, h) =>
    put([
      [x, y + h],
      [x, y + h * 0.32],
      [x + w * 0.5, y],
      [x + w, y + h * 0.32],
      [x + w, y + h],
    ]);
  switch (plan) {
    case "four-gardens":
      for (let x of [0.04, 0.54]) for (let y of [0.04, 0.54]) box(x, y, 0.42);
      box(0.46, 0, 0.08, 1);
      box(0, 0.46, 0.46, 0.08);
      box(0.54, 0.46, 0.46, 0.08);
      break;
    case "seven-niches":
      for (let i = 0; i < 3; i++) {
        niche(i / 3, 0, 1 / 3, 0.38);
        niche(i / 3, 0.62, 1 / 3, 0.38);
      }
      niche(0, 0.38, 1, 0.24);
      break;
    case "terraces":
      for (let i = 0; i < 4; i++) {
        box(0, i * 0.25, 0.3, 0.25);
        box(0.3, i * 0.25, 0.4, 0.25);
        box(0.7, i * 0.25, 0.3, 0.25);
      }
      break;
    case "pendants":
      put(regular(12, 0.5, 0.5, 0.32));
      diamond(0.5, 0.1, 0.11, 0.1);
      diamond(0.5, 0.9, 0.11, 0.1);
      for (let x of [0.15, 0.85])
        for (let y of [0.2, 0.8]) niche(x - 0.13, y - 0.18, 0.26, 0.36);
      break;
    case "satellites":
      put(regular(12, 0.5, 0.5, 0.21));
      for (let i = 0; i < 12; i++) {
        const a = (i * Math.PI) / 6;
        put(
          regular(6, 0.5 + 0.36 * Math.cos(a), 0.5 + 0.36 * Math.sin(a), 0.12),
        );
      }
      break;
    case "aisles":
      for (let j = 0; j < 3; j++) {
        niche(j / 3, 0, 1 / 3, 0.43);
        niche(j / 3, 0.57, 1 / 3, 0.43);
      }
      box(0, 0.43, 1, 0.14);
      break;
    case "paired-niches":
      niche(0.04, 0.02, 0.92, 0.43);
      put([
        [0.04, 0.98],
        [0.04, 0.71],
        [0.5, 0.55],
        [0.96, 0.71],
        [0.96, 0.98],
      ]);
      diamond(0.5, 0.5, 0.18, 0.05);
      break;
    case "nine-room":
      return compartmentGraph("nine-room");
    case "stair-garden":
      for (let i = 0; i < 4; i++) {
        box(i * 0.12, i * 0.12, 0.12, 1 - i * 0.24);
        box(0.88 - i * 0.12, i * 0.12, 0.12, 1 - i * 0.24);
      }
      break;
    case "compass": {
      const pts = regular(8, 0.5, 0.5, 0.72);
      put(regular(8, 0.5, 0.5, 0.2));
      for (let i = 0; i < 8; i++) put([[0.5, 0.5], pts[i], pts[(i + 1) % 8]]);
      break;
    }
    case "three-shields":
      for (let j = 0; j < 3; j++) diamond(0.5, 0.17 + j * 0.33, 0.43, 0.17);
      for (let x of [0.1, 0.9])
        for (let y of [0.33, 0.66]) diamond(x, y, 0.1, 0.2);
      break;
    case "arcade":
      for (let j = 0; j < 2; j++)
        for (let i = 0; i < 4; i++) {
          niche(i * 0.25, j * 0.5, 0.25, 0.5);
          put([
            [i * 0.25, j * 0.5],
            [i * 0.25 + 0.125, j * 0.5],
            [i * 0.25, j * 0.5 + 0.16],
          ]);
        }
      break;
    case "garden-maze":
      return compartmentGraph("elbows").flatMap((p) => [
        p.map(([x, y]) => [x * 0.5, y * 0.5]),
        p.map(([x, y]) => [0.5 + x * 0.5, 0.5 + y * 0.5]),
        p.map(([x, y]) => [0.5 + x * 0.5, y * 0.5]),
        p.map(([x, y]) => [x * 0.5, 0.5 + y * 0.5]),
      ]);
    case "cypress-court":
      niche(0.3, 0, 0.4, 1);
      for (let x of [0.15, 0.85])
        for (let y of [0.25, 0.75]) put(regular(8, x, y, 0.145));
      break;
    case "galleries":
      diamond(0.5, 0.5, 0.25, 0.4);
      for (let x of [0, 0.75])
        for (let y of [0, 0.5])
          put([
            [x, y],
            [x + 0.25, y + 0.1],
            [x + 0.25, y + 0.5],
            [x, y + 0.4],
          ]);
      break;
    case "pavilion":
      put(regular(8, 0.5, 0.5, 0.34));
      for (let x of [0, 0.78]) for (let y of [0, 0.78]) box(x, y, 0.22);
      for (let i = 0; i < 4; i++) {
        const a = (i * Math.PI) / 2;
        diamond(0.5 + 0.4 * Math.cos(a), 0.5 + 0.4 * Math.sin(a), 0.1, 0.1);
      }
      break;
    case "cartouche-chain":
      for (let j = 0; j < 5; j++) {
        const r = j % 2 ? 0.12 : 0.3;
        put(
          regular(8, 0.5, 0.1 + j * 0.2, r, 0).map(([x, y]) => [
            x,
            0.1 + j * 0.2 + (y - (0.1 + j * 0.2)) * 0.33,
          ]),
        );
      }
      for (let x of [0, 0.8]) box(x, 0, 0.2, 1);
      break;
    case "lotus-pools":
      for (let y of [0.25, 0.75]) put(regular(16, 0.5, y, 0.25));
      box(0.42, 0.42, 0.16);
      for (let x of [0, 0.82]) for (let y of [0, 0.5]) niche(x, y, 0.18, 0.5);
      break;
    case "delta":
      put([
        [0.5, 0.5],
        [0, 0],
        [1, 0],
      ]);
      put([
        [0.5, 0.5],
        [1, 0],
        [1, 1],
      ]);
      put([
        [0.5, 0.5],
        [0, 1],
        [0, 0],
      ]);
      put([
        [0, 1],
        [0.5, 0.5],
        [1, 1],
      ]);
      break;
    case "five-chambers":
      box(0.22, 0.22, 0.56);
      box(0, 0, 1, 0.22);
      box(0, 0.78, 1, 0.22);
      box(0, 0.22, 0.22, 0.56);
      box(0.78, 0.22, 0.22, 0.56);
      break;
    case "fountain":
      put(regular(8, 0.5, 0.5, 0.23, Math.PI / 8));
      for (let x of [0, 0.75]) for (let y of [0, 0.75]) box(x, y, 0.25);
      box(0.4, 0, 0.2, 0.27);
      box(0.4, 0.73, 0.2, 0.27);
      box(0, 0.4, 0.27, 0.2);
      box(0.73, 0.4, 0.27, 0.2);
      break;
    default:
      throw new Error("Unknown garden graph: " + plan);
  }
  return cells;
}

// Curved botanical drawing vocabulary. Cubic curves and tapered lancet leaves
// replace the isolated star/dot stamps used by older patterns.

export function ornamentalPattern(name) {
  const spec = ornamentalCatalog.find((p) => p.name === name || p.id === name);
  if (!spec) return null;
  const african = spec.group === "African inlay",
    court = spec.group === "Ornamental carpets";
  const colors = african
    ? ["#171b21", "#f0e6ca", "#c73328", "#edb837", "#224b83", "#198766"]
    : ["#132d40", "#f0e3ba", "#a52e33", "#cfac57", "#1b746d", "#456d3e"];
  const b = referenceBuilder(512, 512, colors);
  b.rect(0, 0, 0, 0, 512, 512);
  // A fine connected ground network occupies interstices not covered by panels.
  for (let y = 16; y < 500; y += 32)
    for (let x = 16; x < 500; x += 32) {
      b.line(
        1,
        3,
        [
          [x - 16, y],
          [x, y - 16],
          [x + 16, y],
          [x, y + 16],
          [x - 16, y],
        ],
        0.55,
      );
      b.star(1, african ? 4 : 5, x, y, 7, 4, 0.45);
    }
  const graph = court ? courtGraph(spec.plan) : compartmentGraph(spec.plan);
  const rows = court ? 1 : 3,
    cols = court ? 1 : 3,
    edge = 26,
    span = 460;
  for (let row = 0; row < rows; row++)
    for (let col = 0; col < cols; col++)
      for (let idx = 0; idx < graph.length; idx++) {
        const polygon = graph[idx].map(([x, y]) => [
          edge + ((col + x) * span) / cols,
          edge + ((row + y) * span) / rows,
        ]);
        const cx = polygon.reduce((a, p) => a + p[0], 0) / polygon.length,
          cy = polygon.reduce((a, p) => a + p[1], 0) / polygon.length;
        const minx = Math.min(...polygon.map((p) => p[0])),
          maxx = Math.max(...polygon.map((p) => p[0])),
          miny = Math.min(...polygon.map((p) => p[1])),
          maxy = Math.max(...polygon.map((p) => p[1]));
        const r = Math.min(maxx - minx, maxy - miny) * 0.43;
        if (r < 2) continue;
        const inset = polygon.map(([x, y]) => [
          cx + (x - cx) * 0.95,
          cy + (y - cy) * 0.95,
        ]);
        b.add(2, 3, polygon);
        b.add(3, idx % 3 === 0 ? 2 : idx % 3 === 1 ? 0 : 4, inset);
        b.outline(4, 1, inset, 0.65);
        // Clip both concave and convex compartment ornament to its actual boundary.
        const draw = () => {
          // Fit the ornament to the compartment, rather than leaving a tiny stamp
          // stranded in the center of long corridors and wedge-shaped panels.
          const sx = ((maxx - minx) * 0.47) / r,
            sy = ((maxy - miny) * 0.47) / r;
          b.transform(
            ([x, y]) => [cx + (x - cx) * sx, cy + (y - cy) * sy],
            () => ornamentalInfill(b, spec.craft, cx, cy, r, 5),
          );
        };
        b.clip(inset, () => {
          if (!african && spec.craft !== "strap") {
            const spacing = court ? 26 : 22;
            for (let fy = miny + spacing * 0.5; fy < maxy; fy += spacing)
              for (let fx = minx + spacing * 0.5; fx < maxx; fx += spacing) {
                const pts = Array.from({ length: 10 }, (_, i) => [
                  fx + Math.sin(i * 0.4) * 3,
                  fy - 5 + i,
                ]);
                b.line(4.1, 3, pts, 0.6);
                for (let j = 0; j < 3; j++)
                  b.leaf(
                    4.2,
                    5,
                    fx + (j % 2 ? 2 : -2),
                    fy - 3 + j * 3,
                    2.4,
                    j % 2 ? 0.5 : 2.5,
                  );
                b.flower(4.3, 1, fx, fy - 5, 1.6, 5);
              }
          }
          draw();
        });
        if (court && r > 65)
          for (let q = 0; q < polygon.length; q++) {
            const p = polygon[q];
            ornamentalInfill(
              b,
              "vine",
              cx + (p[0] - cx) * 0.58,
              cy + (p[1] - cy) * 0.58,
              r * 0.2,
              5,
            );
          }
      }
  // Ornamented borders mask field spill and remain individually editable.
  for (const [inset, width, k] of [
    [0, 6, 0],
    [6, 2, 3],
    [8, 2, 1],
    [10, 12, 0],
    [22, 2, 3],
    [24, 2, 1],
  ]) {
    b.rect(20, k, inset, inset, 512 - inset * 2, width);
    b.rect(20, k, inset, 512 - inset - width, 512 - inset * 2, width);
    b.rect(20, k, inset, inset, width, 512 - inset * 2);
    b.rect(20, k, 512 - inset - width, inset, width, 512 - inset * 2);
  }
  for (let q = 28; q < 490; q += 18)
    for (const [x, y] of [
      [q, 16],
      [496, q],
      [512 - q, 496],
      [16, 512 - q],
    ]) {
      b.diamond(21, 3, x, y, 7, 5);
      b.diamond(22, 2, x, y, 4, 2);
    }
  return {
    schema: "alloy.pattern.v1",
    name: spec.name,
    collection: spec.group,
    construction: spec.id,
    presentation: "rug",
    designAspect: 1,
    tileAxes: "none",
    background: colors[0],
    repeat: "straight",
    repeats: 1,
    mapping: "uv",
    layers: b.layers(african ? "cotton" : "wool"),
  };
}
