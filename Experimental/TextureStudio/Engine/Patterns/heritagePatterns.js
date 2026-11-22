// Original, editable vector studies inspired by named decorative traditions.
// These are not historic reproductions, ceremonial symbols, or calligraphic text.
// No raster artwork, external resource, random seed, or palette variant is a card.
export const islamicPatternCatalog = [
  [
    "Zellige Eight-Point Stars",
    "zellige-eight-point-stars",
    "Moroccan zellige-inspired eight-point stars, lozenge joints and fine glaze outlines.",
  ],
  [
    "Girih Decagon Rosettes",
    "girih-decagon-rosettes",
    "Persian girih-inspired decagonal rosettes with rhombic petals and bow-tie connectors; an original periodic study, not a historical five-tile reconstruction.",
  ],
  [
    "Persian Twelve-Point Rosettes",
    "persian-twelve-point-rosettes",
    "A twelvefold Persian-inspired central rosette, triangular interstices and quarter rosettes at the repeat corners.",
  ],
  [
    "Seljuk Star-and-Cross",
    "seljuk-star-and-cross",
    "Seljuk-inspired eight-point stars alternating with angular cross-shaped infill on a connected square grid.",
  ],
  [
    "Andalusian Interlaced Squares",
    "andalusian-interlaced-squares",
    "Andalusian-inspired square and diagonal strap circuits with explicitly separated over-and-under crossings.",
  ],
  [
    "Maghrebi Honeycomb Stars",
    "maghrebi-honeycomb-stars",
    "Maghrebi-inspired sixfold star flowers on a regular hexagonal lattice; the tile aspect preserves the hexagonal geometry.",
  ],
  [
    "Mamluk Compass Medallions",
    "mamluk-compass-medallions",
    "Mamluk-inspired sixteenfold compass medallions with alternating radial panels, satellite stars and nested polygon borders.",
  ],
  [
    "Ottoman Rumi Arabesque",
    "ottoman-rumi-arabesque",
    "Ottoman rumi-inspired mirrored curling stems, split leaves and palmettes; original non-figurative floral ornament.",
  ],
  [
    "Moroccan Laced Diamonds",
    "moroccan-laced-diamonds",
    "Moroccan-inspired connected diamond straps, paired inner lozenges and small star knots at their junctions.",
  ],
  [
    "Kufic-Inspired Square Maze",
    "kufic-inspired-square-maze",
    "An abstract square-Kufic-inspired orthogonal maze. Decorative geometry only: no readable lettering, quotation or religious text.",
  ],
].map(([name, id, description]) => ({
  name,
  id,
  description,
  group: "Islamic geometry",
}));

export const africanPatternCatalog = [
  [
    "Asante Kente Blocks",
    "asante-kente-blocks",
    "Asante, Ghana — kente-inspired joined strip blocks, supplementary-weft bars and alternating stepped lozenges.",
  ],
  [
    "Ewe Kente Zigzags",
    "ewe-kente-zigzags",
    "Ewe, Ghana and Togo — strip-weaving-inspired stepped zigzags, narrow warp guards and contrasting weft ladders.",
  ],
  [
    "Bamana Bogolanfini Dashes",
    "bamana-bogolanfini-dashes",
    "Bamana, Mali — bogolanfini mudcloth-inspired contrasting panels of dashes, combs and dotted geometric marks; no assigned symbolic meanings.",
  ],
  [
    "Kuba Raffia Steps",
    "kuba-raffia-steps",
    "Kuba, Democratic Republic of the Congo — raffia-inspired stepped lozenges, bent hooks and paired offset stitch lines.",
  ],
  [
    "Ndebele Painted Chevrons",
    "ndebele-painted-chevrons",
    "Ndebele, South Africa — painted architectural geometry-inspired chevrons, bold outlines and inset colour panels.",
  ],
  [
    "Yoruba Adire Rings",
    "yoruba-adire-rings",
    "Yoruba, Nigeria — adire indigo-resist-inspired concentric rings, dotted halos and small opposing crescent marks.",
  ],
  [
    "Yoruba Aso-Oke Stripes",
    "yoruba-aso-oke-stripes",
    "Yoruba, Nigeria — aso-oke strip-weaving-inspired long warp bands, fine transverse ribs and supplementary lozenge accents.",
  ],
  [
    "Cameroonian Ndop Lattice",
    "cameroonian-ndop-lattice",
    "Grassfields, Cameroon — ndop indigo-resist-inspired linked square cells, diagonal hourglasses and dotted line details.",
  ],
  [
    "Ethiopian Tibeb Border",
    "ethiopian-tibeb-border",
    "Ethiopia and Eritrea — tibeb embroidery-inspired nested diamonds and stepped coloured bands; repeats horizontally as a border.",
  ],
  [
    "Swahili Kanga Rosettes",
    "swahili-kanga-rosettes",
    "Swahili coast, East Africa — kanga-inspired printed floral-geometric fields with contrasting frame bands; no proverb or text is reproduced.",
  ],
].map(([name, id, description]) => ({
  name,
  id,
  description,
  group: "African traditional",
}));

export const heritagePatternCatalog = [
  ...islamicPatternCatalog,
  ...africanPatternCatalog,
];
const heritageNumber = (n) => Number((n / 5.12).toFixed(4));
const heritageXY = (p) => p.map(heritageNumber).join(" ");
const heritagePolyline = (p) =>
  p.map((point, i) => (i ? "L" : "M") + heritageXY(point)).join("");
const heritagePolygon = (p) => heritagePolyline(p) + "Z";
const heritageRect = (x, y, w, h) =>
  heritagePolygon([
    [x, y],
    [x + w, y],
    [x + w, y + h],
    [x, y + h],
  ]);
const heritageCircle = (x, y, r) =>
  `M${heritageXY([x - r, y])}a${heritageXY([r, r])} 0 1 0 ${heritageXY([2 * r, 0])}a${heritageXY([r, r])} 0 1 0 ${heritageXY([-2 * r, 0])}Z`;
const heritageRegularPoints = (
  x,
  y,
  r,
  count,
  angle = -Math.PI / 2,
  scaleY = 1,
) =>
  Array.from({ length: count }, (_, i) => [
    x + Math.cos(angle + (i * Math.PI * 2) / count) * r,
    y + Math.sin(angle + (i * Math.PI * 2) / count) * r * scaleY,
  ]);
const heritageStar = (
  x,
  y,
  r,
  count,
  inner = 0.6,
  angle = -Math.PI / 2,
  scaleY = 1,
) =>
  heritagePolygon(
    Array.from({ length: count * 2 }, (_, i) => {
      const a = angle + (i * Math.PI) / count,
        radius = i % 2 ? r * inner : r;
      return [x + Math.cos(a) * radius, y + Math.sin(a) * radius * scaleY];
    }),
  );
const heritageDiamond = (x, y, rx, ry = rx) =>
  heritagePolygon([
    [x, y - ry],
    [x + rx, y],
    [x, y + ry],
    [x - rx, y],
  ]);
const heritageRotate = (points, x, y, angle) =>
  points.map(([u, v]) => [
    x + u * Math.cos(angle) - v * Math.sin(angle),
    y + u * Math.sin(angle) + v * Math.cos(angle),
  ]);
const heritageSteppedDiamond = (x, y, r, steps = 4) => {
  const edge = [[0, -r]],
    s = r / steps;
  for (let i = 0; i < steps; i++)
    edge.push([(i + 1) * s, -r + i * s], [(i + 1) * s, -r + (i + 1) * s]);
  return heritagePolygon(
    Array.from({ length: 4 }, (_, i) =>
      heritageRotate(edge.slice(0, -1), x, y, (i * Math.PI) / 2),
    ).flat(),
  );
};
const heritageLeaf = (x, y, w, h, angle = 0) => {
  const p = (u, v) => heritageXY(heritageRotate([[u, v]], x, y, angle)[0]);
  return `M${p(0, -h / 2)}C${p(w, -h / 3)} ${p(w * 0.8, h / 3)} ${p(0, h / 2)}C${p(-w * 0.8, h / 3)} ${p(-w, -h / 3)} ${p(0, -h / 2)}Z`;
};

export function heritagePattern(name) {
  const spec = heritagePatternCatalog.find(
    (p) => p.name === name || p.id === name,
  );
  if (!spec) return null;
  const islamic = spec.group === "Islamic geometry",
    finish = islamic ? "ceramic" : "cotton";
  const d = {
    schema: "alloy.pattern.v1",
    name: spec.name,
    collection: spec.group,
    construction: spec.id,
    library: { family: spec.name, palette: "Original" },
    background: "#f1e3c7",
    repeat: "straight",
    repeats: 1,
    tileAxes: "xy",
    mapping: "uv",
    layers: [],
  };
  let c = ["#f1e3c7", "#175b64", "#c59a43", "#a34c3b", "#203b49", "#e3ce94"];
  const add = (label, colour, paths, props = {}) => {
    const path = Array.isArray(paths) ? paths.join("") : paths;
    if (!path) return;
    d.layers.push({
      kind: "path",
      name: label,
      x: 256,
      y: 256,
      width: 512,
      height: 512,
      path,
      color: c[colour],
      stroke: c[colour],
      paint: true,
      fillEnabled: true,
      strokeWidth: 0,
      finish,
      relief: islamic ? 0.04 : 0.025,
      ...props,
    });
  };
  const line = (label, colour, paths, width = 3, props = {}) =>
    add(label, colour, paths, {
      fillEnabled: false,
      strokeWidth: width,
      strokeLinecap: "round",
      strokeLinejoin: "round",
      ...props,
    });
  const ground = (palette = c) => {
    c = palette;
    d.background = c[0];
    add(
      islamic ? "Glaze ground" : "Cloth ground",
      0,
      heritageRect(0, 0, 512, 512),
      { relief: 0 },
    );
  };
  const cells = (count, draw) => {
    const step = 512 / count;
    for (let y = 0; y < count; y++)
      for (let x = 0; x < count; x++)
        draw(x * step + step / 2, y * step + step / 2, x, y, step);
  };

  switch (spec.id) {
    case "zellige-eight-point-stars": {
      ground();
      const stars = [],
        inner = [],
        centres = [],
        joints = [];
      cells(2, (x, y) => {
        stars.push(heritageStar(x, y, 118, 8, Math.sqrt(2 - Math.SQRT2)));
        inner.push(heritageStar(x, y, 96, 8, Math.sqrt(2 - Math.SQRT2)));
        centres.push(heritageStar(x, y, 42, 8, 0.6));
      });
      for (let x = 0; x <= 512; x += 256)
        for (let y = 0; y <= 512; y += 256)
          joints.push(heritageDiamond(x, y, 78));
      add("Gold star rims", 2, stars);
      add("Turquoise star faces", 1, inner);
      add("Terracotta star centres", 3, centres);
      add("Cobalt lozenge joints", 4, joints);
      line("Fine glaze tracery", 0, [...inner, ...joints], 2);
      break;
    }
    case "girih-decagon-rosettes": {
      ground([
        "#183c52",
        "#ecdfbe",
        "#c8a25c",
        "#478b88",
        "#a94e44",
        "#2b6076",
      ]);
      const decagons = [],
        petals = [],
        cores = [],
        knots = [],
        outlines = [];
      cells(2, (x, y) => {
        decagons.push(heritagePolygon(heritageRegularPoints(x, y, 115, 10)));
        cores.push(heritageStar(x, y, 55, 10, 0.55));
        for (let i = 0; i < 10; i++) {
          const a = -Math.PI / 2 + (i * Math.PI) / 5;
          const p = heritageRotate(
            [
              [25, 0],
              [64, -19],
              [106, 0],
              [64, 19],
            ],
            x,
            y,
            a,
          );
          petals.push(heritagePolygon(p));
          outlines.push(heritagePolygon(p));
        }
      });
      for (let y = 0; y <= 512; y += 256)
        for (let x = 0; x <= 512; x += 256)
          knots.push(
            heritagePolygon([
              [x - 52, y - 29],
              [x, y - 10],
              [x + 52, y - 29],
              [x + 34, y],
              [x + 52, y + 29],
              [x, y + 10],
              [x - 52, y + 29],
              [x - 34, y],
            ]),
          );
      add("Decagonal tile faces", 5, decagons);
      add("Rhombic rosette petals", 2, petals);
      add("Ten-point insets", 3, cores);
      add("Bow-tie junctions", 4, knots);
      line(
        "Ivory girih straps",
        1,
        [...decagons, ...outlines, ...cores, ...knots],
        2.5,
      );
      break;
    }
    case "persian-twelve-point-rosettes": {
      ground([
        "#eee1c4",
        "#164b62",
        "#d3ab60",
        "#527f83",
        "#a75045",
        "#f8f0dd",
      ]);
      const rims = [],
        stars = [],
        petals = [],
        triangles = [];
      for (const [x, y, r] of [
        [256, 256, 205],
        [0, 0, 134],
        [512, 0, 134],
        [0, 512, 134],
        [512, 512, 134],
      ]) {
        rims.push(heritageStar(x, y, r, 12, 0.73));
        stars.push(heritageStar(x, y, r * 0.83, 12, 0.69));
        for (let i = 0; i < 12; i++)
          petals.push(
            heritageLeaf(
              x + Math.cos((i * Math.PI) / 6) * r * 0.48,
              y + Math.sin((i * Math.PI) / 6) * r * 0.48,
              r * 0.065,
              r * 0.36,
              (i * Math.PI) / 6 + Math.PI / 2,
            ),
          );
      }
      for (let i = 0; i < 4; i++)
        triangles.push(
          heritagePolygon(
            heritageRotate(
              [
                [214, -37],
                [245, 0],
                [214, 37],
              ],
              256,
              256,
              (i * Math.PI) / 2,
            ),
          ),
        );
      add("Twelvefold gold borders", 2, rims);
      add("Deep blue rosette fields", 1, stars);
      add("Radial ivory petals", 5, petals);
      add("Terracotta triangular interstices", 4, triangles);
      add("Turquoise centre seal", 3, heritageStar(256, 256, 45, 12, 0.6));
      line("Inset polygon edging", 0, stars, 2);
      break;
    }
    case "seljuk-star-and-cross": {
      ground([
        "#dec8a4",
        "#1d6670",
        "#f5ead6",
        "#bd7746",
        "#254858",
        "#ceb787",
      ]);
      const stars = [],
        centres = [],
        crosses = [];
      cells(4, (x, y) => {
        stars.push(heritageStar(x, y, 59, 8, Math.sqrt(2 - Math.SQRT2)));
        centres.push(heritageStar(x, y, 26, 8, 0.68));
      });
      for (let y = 0; y <= 512; y += 128)
        for (let x = 0; x <= 512; x += 128)
          crosses.push(
            heritagePolygon([
              [x - 16, y - 49],
              [x + 16, y - 49],
              [x + 16, y - 16],
              [x + 49, y - 16],
              [x + 49, y + 16],
              [x + 16, y + 16],
              [x + 16, y + 49],
              [x - 16, y + 49],
              [x - 16, y + 16],
              [x - 49, y + 16],
              [x - 49, y - 16],
              [x - 16, y - 16],
            ]),
          );
      add("Ivory cross tiles", 2, crosses);
      add("Turquoise star tiles", 1, stars);
      add("Ochre inner stars", 3, centres);
      line("Dark tile joints", 4, [...crosses, ...stars], 2.5);
      break;
    }
    case "andalusian-interlaced-squares": {
      ground([
        "#193f45",
        "#ebd9ac",
        "#c79248",
        "#3c7e75",
        "#a94937",
        "#f4e9cb",
      ]);
      const squares = [],
        diamonds = [],
        gaps = [],
        over = [],
        centres = [];
      cells(2, (x, y) => {
        squares.push(heritageRect(x - 83, y - 83, 166, 166));
        diamonds.push(heritageDiamond(x, y, 117));
        centres.push(heritageStar(x, y, 34, 8, 0.6));
        // Cut the lower strap at alternate crossings, then redraw only the upper span.
        for (const [u, v, vertical] of [
          [83, 34, true],
          [-83, -34, true],
          [34, -83, false],
          [-34, 83, false],
        ]) {
          gaps.push(heritageRect(x + u - 10, y + v - 10, 20, 20));
          over.push(
            heritagePolyline(
              vertical
                ? [
                    [x + u, y + v - 16],
                    [x + u, y + v + 16],
                  ]
                : [
                    [x + u - 16, y + v],
                    [x + u + 16, y + v],
                  ],
            ),
          );
        }
      });
      line("Square strap shadows", 2, squares, 12);
      line("Diagonal strap shadows", 2, diamonds, 12);
      line("Ivory square straps", 1, squares, 6);
      line("Ivory diagonal straps", 1, diamonds, 6);
      add("Alternating underpass cutouts", 0, gaps);
      line("Upper crossing spans", 2, over, 12);
      line("Upper crossing highlights", 1, over, 6);
      add("Vermilion star knots", 4, centres);
      break;
    }
    case "maghrebi-honeycomb-stars": {
      ground([
        "#f0e1bb",
        "#286b64",
        "#bc7d42",
        "#e9c776",
        "#264b56",
        "#f7efdb",
      ]);
      d.designAspect = 2 / Math.sqrt(3);
      const cellsPath = [],
        stars = [],
        insets = [],
        pollen = [],
        sy = 2 / Math.sqrt(3),
        r = 256 / 3;
      for (let col = -1; col <= 4; col++)
        for (let row = -1; row <= 3; row++) {
          const x = col * 128,
            y = (row + (Math.abs(col) % 2) / 2) * (512 / 3);
          cellsPath.push(
            heritagePolygon(heritageRegularPoints(x, y, r * 0.94, 6, 0, sy)),
          );
          stars.push(heritageStar(x, y, r * 0.73, 6, 0.5, 0, sy));
          insets.push(heritageStar(x, y, r * 0.4, 6, 0.62, 0, sy));
          pollen.push(heritageCircle(x, y, 7));
        }
      add("Terracotta honeycomb faces", 2, cellsPath);
      add("Ivory six-point flowers", 5, stars);
      add("Green star insets", 1, insets);
      add("Gold centres", 3, pollen);
      line("Hexagonal glaze seams", 4, cellsPath, 2);
      break;
    }
    case "mamluk-compass-medallions": {
      ground([
        "#233f3f",
        "#eadbb9",
        "#bd974a",
        "#8f473e",
        "#4c7972",
        "#f1e8d3",
      ]);
      const facets = [[], []],
        rings = [],
        satellites = [];
      for (let i = 0; i < 16; i++) {
        const a = -Math.PI / 2 + (i * Math.PI) / 8;
        facets[i % 2].push(
          heritagePolygon(
            heritageRotate(
              [
                [45, 0],
                [143, -29],
                [213, 0],
                [143, 29],
              ],
              256,
              256,
              a,
            ),
          ),
        );
      }
      for (const r of [223, 182, 97])
        rings.push(heritagePolygon(heritageRegularPoints(256, 256, r, 16)));
      for (const x of [0, 512])
        for (const y of [0, 512])
          satellites.push(heritageStar(x, y, 103, 8, 0.63));
      add("Crimson compass panels", 3, facets[0]);
      add("Green compass panels", 4, facets[1]);
      line("Gold sixteen-sided frames", 2, rings, 5);
      line("Ivory radial joints", 1, [...facets[0], ...facets[1]], 2);
      add("Corner satellite stars", 2, satellites);
      add("Ivory central medallion", 5, heritageStar(256, 256, 68, 16, 0.6));
      add("Green central flower", 4, heritageStar(256, 256, 39, 8, 0.55));
      break;
    }
    case "ottoman-rumi-arabesque": {
      ground([
        "#eae1c9",
        "#226b68",
        "#bd9647",
        "#a44739",
        "#284a54",
        "#f6edda",
      ]);
      const stems = [],
        leaves = [],
        red = [],
        buds = [];
      for (const cx of [128, 384]) {
        const p = (x, y) => heritageXY([cx + x, y]);
        for (const sign of [-1, 1])
          stems.push(
            `M${p(0, 0)}C${p(sign * 112, 85)} ${p(sign * 112, 171)} ${p(0, 256)}C${p(-sign * 112, 341)} ${p(-sign * 112, 427)} ${p(0, 512)}`,
          );
        for (let j = 0; j < 4; j++)
          for (const sign of [-1, 1]) {
            const y = 64 + j * 128,
              x = cx + sign * (j % 2 ? -57 : 57);
            leaves.push(heritageLeaf(x, y, 20, 80, (sign * Math.PI) / 3));
            buds.push(heritageLeaf(x, y, 8, 45, (sign * Math.PI) / 3));
          }
        for (const y of [0, 256, 512]) {
          red.push(heritageLeaf(cx, y, 27, 104));
          for (const sign of [-1, 1])
            red.push(heritageLeaf(cx + sign * 23, y + 10, 18, 73, sign * 0.5));
        }
      }
      line("Gold scrolling stem outlines", 2, stems, 8);
      line("Green scrolling stems", 1, stems, 3);
      add("Split green leaves", 1, leaves);
      add("Ivory leaf veins", 5, buds);
      add("Red palmettes", 3, red);
      line("Palmette fine edging", 2, red, 1.5);
      break;
    }
    case "moroccan-laced-diamonds": {
      ground([
        "#efe3c7",
        "#1d6770",
        "#c99b48",
        "#ae5341",
        "#254d58",
        "#f7efd9",
      ]);
      const weave = [],
        centres = [],
        inner = [],
        stars = [];
      for (let row = -1; row <= 4; row++)
        for (let col = -1; col <= 4; col++) {
          const x = col * 128 + (Math.abs(row) % 2) * 64,
            y = row * 128;
          weave.push(heritageDiamond(x, y, 64, 128));
          centres.push(heritageDiamond(x, y, 38, 85));
          inner.push(heritageDiamond(x, y, 22, 52));
          stars.push(heritageStar(x, y, 12, 8, 0.6));
        }
      line("Gold lattice straps", 2, weave, 9);
      line("Teal lattice straps", 1, weave, 4);
      add("Ochre lozenge insets", 2, centres);
      add("Ivory lace windows", 5, inner);
      add("Terracotta junction stars", 3, stars);
      line("Lozenge pinlines", 4, centres, 1.5);
      break;
    }
    case "kufic-inspired-square-maze": {
      ground([
        "#f0e5ce",
        "#184c57",
        "#c39443",
        "#a95140",
        "#e0c99b",
        "#f7efdf",
      ]);
      const mazes = [],
        seals = [],
        guards = [];
      cells(2, (x, y) => {
        for (let q = 0; q < 4; q++)
          mazes.push(
            heritagePolyline(
              heritageRotate(
                [
                  [-104, -104],
                  [88, -104],
                  [88, -72],
                  [-72, -72],
                  [-72, -40],
                  [56, -40],
                  [56, -8],
                  [-8, -8],
                  [-8, -24],
                  [24, -24],
                ],
                x,
                y,
                (q * Math.PI) / 2,
              ),
            ),
          );
        seals.push(heritageRect(x - 16, y - 16, 32, 32));
      });
      for (let k = 0; k <= 512; k += 256)
        guards.push(
          heritagePolyline([
            [k, 0],
            [k, 512],
          ]),
          heritagePolyline([
            [0, k],
            [512, k],
          ]),
        );
      line("Angular maze circuits", 1, mazes, 10, {
        strokeLinecap: "square",
        strokeLinejoin: "miter",
      });
      line("Gold cell guards", 2, guards, 4, { strokeLinecap: "square" });
      add("Terracotta square seals", 3, seals);
      break;
    }
    case "asante-kente-blocks": {
      ground([
        "#e0ad32",
        "#242622",
        "#a8392e",
        "#287357",
        "#eee1b5",
        "#b87331",
      ]);
      const red = [],
        green = [],
        bars = [],
        lozenges = [],
        trim = [],
        weft = [];
      cells(4, (x, y, col, row) => {
        const type = (col + row) % 3;
        if (type === 0) {
          red.push(heritageRect(x - 49, y - 53, 98, 106));
          for (let k = -2; k <= 2; k++)
            bars.push(heritageRect(x - 38, y + k * 16 - 3, 76, 6));
        } else if (type === 1) {
          green.push(heritageRect(x - 49, y - 53, 98, 106));
          lozenges.push(heritageSteppedDiamond(x, y, 43));
        } else
          for (let k = -2; k <= 2; k++)
            bars.push(heritageRect(x - 49, y + k * 19 - 4, 98, 8));
      });
      for (let x = 0; x <= 512; x += 128)
        trim.push(heritageRect(x - 7, 0, 14, 512));
      for (let y = 0; y <= 512; y += 16)
        weft.push(
          heritagePolyline([
            [0, y],
            [512, y],
          ]),
        );
      add("Red supplementary-weft blocks", 2, red);
      add("Green strip blocks", 3, green);
      add("Ivory weft bars", 4, bars);
      add("Gold stepped lozenges", 0, lozenges);
      add("Black strip joins", 1, trim);
      line("Fine loom ribs", 5, weft, 0.7, { opacity: 0.45 });
      break;
    }
    case "ewe-kente-zigzags": {
      ground([
        "#1b424b",
        "#ebd59d",
        "#c79432",
        "#a44436",
        "#498572",
        "#14272c",
      ]);
      const bands = [],
        zig = [],
        gold = [],
        guards = [],
        ladders = [];
      for (let y = 0; y < 512; y += 128) {
        bands.push(
          heritageRect(0, y + 18, 512, 28),
          heritageRect(0, y + 87, 512, 24),
        );
        for (let x = -128; x <= 512; x += 128) {
          zig.push(
            heritagePolyline([
              [x, y + 77],
              [x + 32, y + 77],
              [x + 32, y + 57],
              [x + 64, y + 57],
              [x + 64, y + 77],
              [x + 96, y + 77],
              [x + 96, y + 97],
              [x + 128, y + 97],
            ]),
          );
          gold.push(heritageDiamond(x + 64, y + 31, 15, 10));
        }
      }
      for (let x = 0; x <= 512; x += 128) {
        guards.push(heritageRect(x - 5, 0, 10, 512));
        for (let y = 6; y < 512; y += 13)
          ladders.push(
            heritagePolyline([
              [x - 10, y],
              [x + 10, y],
            ]),
          );
      }
      add("Ivory transverse bands", 1, bands);
      line("Red stepped weft zigzags", 3, zig, 6, {
        strokeLinecap: "square",
        strokeLinejoin: "miter",
      });
      add("Gold weft diamonds", 2, gold);
      add("Dark warp guards", 5, guards);
      line("Green guard ladders", 4, ladders, 2);
      break;
    }
    case "bamana-bogolanfini-dashes": {
      ground([
        "#e4d6b4",
        "#302c24",
        "#f2e6c9",
        "#8f6042",
        "#bf9870",
        "#514031",
      ]);
      const panels = [],
        dashes = [],
        combs = [],
        dots = [],
        joins = [];
      cells(4, (x, y, col, row) => {
        if ((col + row) % 2 === 0) {
          panels.push(heritageRect(x - 57, y - 57, 114, 114));
          for (let j = -1; j <= 1; j++)
            for (let i = -1; i <= 1; i++)
              dashes.push(
                heritagePolyline([
                  [x + i * 30 - 7, y + j * 31 - 8],
                  [x + i * 30 + 7, y + j * 31 + 8],
                ]),
              );
        } else {
          for (let k = -2; k <= 2; k++)
            combs.push(
              heritagePolyline([
                [x - 34, y + k * 18],
                [x + 34, y + k * 18],
              ]),
            );
          for (const u of [-43, 43])
            for (let j = -2; j <= 2; j++)
              dots.push(heritageCircle(x + u, y + j * 19, 3));
        }
      });
      for (let n = 0; n <= 512; n += 128)
        joins.push(
          heritagePolyline([
            [n, 0],
            [n, 512],
          ]),
          heritagePolyline([
            [0, n],
            [512, n],
          ]),
        );
      add("Dark mud-dyed panels", 1, panels);
      line("Light resist dashes", 2, dashes, 5);
      line("Brown comb marks", 3, combs, 5);
      add("Dark dotted edges", 1, dots);
      line("Warm panel joins", 4, joins, 4);
      break;
    }
    case "kuba-raffia-steps": {
      ground([
        "#cbb48b",
        "#423329",
        "#efe0ba",
        "#95643c",
        "#ad8959",
        "#302a24",
      ]);
      const dark = [],
        light = [],
        hooks = [],
        double = [];
      cells(2, (x, y) => {
        dark.push(heritageSteppedDiamond(x, y, 112));
        light.push(heritageSteppedDiamond(x, y, 73));
        for (let k = 0; k < 4; k++) {
          hooks.push(
            heritagePolyline(
              heritageRotate(
                [
                  [99, 30],
                  [99, 99],
                  [43, 99],
                  [43, 77],
                  [77, 77],
                  [77, 43],
                ],
                x,
                y,
                (k * Math.PI) / 2,
              ),
            ),
          );
          double.push(
            heritagePolyline(
              heritageRotate(
                [
                  [104, 25],
                  [104, 104],
                  [38, 104],
                ],
                x,
                y,
                (k * Math.PI) / 2,
              ),
            ),
          );
        }
      });
      add("Dark stepped raffia lozenges", 1, dark);
      add("Cream inset stepwork", 2, light);
      line("Brown bent hook bands", 3, hooks, 8, {
        strokeLinecap: "square",
        strokeLinejoin: "miter",
      });
      line("Fine paired raffia stitches", 1, double, 2, {
        strokeLinecap: "square",
      });
      break;
    }
    case "ndebele-painted-chevrons": {
      ground([
        "#e9e3d2",
        "#202e31",
        "#d89b28",
        "#c54c3a",
        "#277b8a",
        "#6b854a",
      ]);
      const bands = [[], [], [], []],
        windows = [],
        joins = [];
      for (let row = -1; row <= 3; row++)
        for (let col = 0; col < 2; col++) {
          const x = col * 256,
            y = row * 192;
          for (let k = 0; k < 4; k++)
            bands[k].push(
              heritagePolygon([
                [x, y + k * 26],
                [x + 128, y + 84 + k * 26],
                [x + 256, y + k * 26],
                [x + 256, y + 22 + k * 26],
                [x + 128, y + 106 + k * 26],
                [x, y + 22 + k * 26],
              ]),
            );
          windows.push(heritageRect(x + 111, y + 12, 34, 38));
        }
      // Three equal chevron courses make the y-period exactly 512 units.
      const shrinkY = (path) =>
        path.replace(
          /([ML])([-\d.]+) ([-\d.]+)/g,
          (_, cmd, x, y) =>
            `${cmd}${x} ${Number(((Number(y) * 8) / 9).toFixed(4))}`,
        );
      for (let k = 0; k < 4; k++) {
        const p = bands[k].map(shrinkY);
        add(
          ["Golden", "Red", "Blue", "Green"][k] + " painted chevrons",
          k + 2,
          p,
        );
        line("Dark outline " + (k + 1), 1, p, 3);
      }
      add("Cream inset windows", 0, windows.map(shrinkY));
      line("Window outlines", 1, windows.map(shrinkY), 3);
      for (let x = 0; x <= 512; x += 256)
        joins.push(
          heritagePolyline([
            [x, 0],
            [x, 512],
          ]),
        );
      line("Bold panel divisions", 1, joins, 6);
      break;
    }
    case "yoruba-adire-rings": {
      ground([
        "#244969",
        "#e0dfc5",
        "#75919e",
        "#42687c",
        "#eef0d9",
        "#173750",
      ]);
      const outer = [],
        inner = [],
        dots = [],
        marks = [];
      cells(4, (x, y, col, row) => {
        outer.push(heritageCircle(x, y, 48), heritageCircle(x, y, 34));
        inner.push(heritageCircle(x, y, 18));
        for (let j = 0; j < 16; j++)
          dots.push(
            heritageCircle(
              x + Math.cos((j * Math.PI) / 8) * 41,
              y + Math.sin((j * Math.PI) / 8) * 41,
              2.5,
            ),
          );
        const side = (col + row) % 2 ? -1 : 1;
        marks.push(
          heritagePolyline([
            [x - side * 10, y - 8],
            [x + side * 2, y - 8],
            [x + side * 8, y],
            [x + side * 2, y + 8],
            [x - side * 10, y + 8],
          ]),
        );
      });
      line("Soft indigo resist halos", 2, outer, 5);
      line("Ivory concentric rings", 1, [...outer, ...inner], 2.5);
      add("Dotted resist halos", 4, dots);
      line("Opposing resist centre marks", 1, marks, 3);
      break;
    }
    case "yoruba-aso-oke-stripes": {
      ground([
        "#ded4b4",
        "#452c32",
        "#9e4e43",
        "#b69348",
        "#f2e8cf",
        "#71654f",
      ]);
      const wide = [],
        red = [],
        guard = [],
        ribs = [],
        diamonds = [];
      for (let x = 0; x < 512; x += 128) {
        wide.push(heritageRect(x + 34, 0, 44, 512));
        red.push(heritageRect(x + 87, 0, 14, 512));
        guard.push(
          heritageRect(x + 15, 0, 3, 512),
          heritageRect(x + 107, 0, 4, 512),
        );
        for (let y = 8; y < 512; y += 16)
          ribs.push(
            heritagePolyline([
              [x + 34, y],
              [x + 78, y],
            ]),
          );
        for (let y = 64; y < 512; y += 128)
          diamonds.push(heritageDiamond(x + 56, y, 14, 26));
      }
      add("Plum warp bands", 1, wide);
      add("Rust warp stripes", 2, red);
      add("Gold thread guards", 3, guard);
      line("Ivory transverse weave ribs", 4, ribs, 1.2);
      add("Gold supplementary lozenges", 3, diamonds);
      break;
    }
    case "cameroonian-ndop-lattice": {
      ground([
        "#193c59",
        "#e4dfc2",
        "#849ba2",
        "#345971",
        "#f2ecd5",
        "#567689",
      ]);
      const grid = [],
        triangles = [],
        centres = [],
        dots = [];
      for (let n = 0; n <= 512; n += 128)
        grid.push(
          heritagePolyline([
            [n, 0],
            [n, 512],
          ]),
          heritagePolyline([
            [0, n],
            [512, n],
          ]),
        );
      cells(4, (x, y, col, row) => {
        const a = (col + row) % 2 ? Math.PI / 2 : 0;
        triangles.push(
          heritagePolygon(
            heritageRotate(
              [
                [-43, -44],
                [43, -44],
                [0, -6],
              ],
              x,
              y,
              a,
            ),
          ),
          heritagePolygon(
            heritageRotate(
              [
                [-43, 44],
                [43, 44],
                [0, 6],
              ],
              x,
              y,
              a,
            ),
          ),
        );
        centres.push(heritageDiamond(x, y, 13));
        for (let k = -2; k <= 2; k++)
          for (const sign of [-1, 1])
            dots.push(heritageCircle(x + sign * 52, y + k * 20, 2.3));
      });
      line("Soft resist cell boundaries", 2, grid, 6);
      line("Ivory linked lattice", 1, grid, 2);
      line("Alternating hourglass panels", 1, triangles, 3);
      add("Cream central diamonds", 4, centres);
      add("Resist-dot lines", 1, dots);
      break;
    }
    case "ethiopian-tibeb-border": {
      ground([
        "#eee5ce",
        "#29474a",
        "#a84a42",
        "#d2a646",
        "#4b7c67",
        "#ede1b7",
      ]);
      d.tileAxes = "x";
      const bars = [],
        diamonds = [],
        inner = [],
        stitches = [],
        gold = [];
      for (const y of [141, 371]) bars.push(heritageRect(0, y - 4, 512, 8));
      for (let x = -128; x <= 640; x += 128) {
        diamonds.push(heritageSteppedDiamond(x + 64, 256, 90, 5));
        inner.push(heritageSteppedDiamond(x + 64, 256, 62, 5));
        gold.push(heritageDiamond(x + 64, 256, 34));
        for (let j = 0; j < 8; j++)
          stitches.push(
            heritagePolyline([
              [x + j * 16, 127],
              [x + j * 16 + 5, 135],
            ]),
            heritagePolyline([
              [x + j * 16, 377],
              [x + j * 16 + 5, 385],
            ]),
          );
      }
      add("Green embroidered edge bands", 4, bars);
      add("Red stepped border diamonds", 2, diamonds);
      add("Ivory stepped centres", 0, inner);
      add("Golden lozenge cores", 3, gold);
      line("Dark diamond stitching", 1, diamonds, 2);
      line("Gold selvedge stitches", 3, stitches, 3);
      line("Inner green embroidery", 4, inner, 2);
      break;
    }
    case "swahili-kanga-rosettes": {
      ground([
        "#ead9b3",
        "#a44239",
        "#285d54",
        "#d6a44c",
        "#f3e6c9",
        "#4b4230",
      ]);
      const rims = [],
        flowers = [],
        leaves = [],
        dots = [],
        guards = [];
      cells(2, (x, y) => {
        rims.push(heritageCircle(x, y, 97));
        for (let j = 0; j < 8; j++)
          flowers.push(
            heritageLeaf(
              x + Math.cos((j * Math.PI) / 4) * 39,
              y + Math.sin((j * Math.PI) / 4) * 39,
              16,
              57,
              (j * Math.PI) / 4 + Math.PI / 2,
            ),
          );
        for (let j = 0; j < 4; j++)
          leaves.push(
            heritageLeaf(
              x + Math.cos((j * Math.PI) / 2 + Math.PI / 4) * 104,
              y + Math.sin((j * Math.PI) / 2 + Math.PI / 4) * 104,
              11,
              38,
              (j * Math.PI) / 2 + Math.PI / 4 + Math.PI / 2,
            ),
          );
        dots.push(heritageCircle(x, y, 13));
      });
      for (let n = 0; n <= 512; n += 256)
        guards.push(
          heritageRect(n - 8, 0, 16, 512),
          heritageRect(0, n - 8, 512, 16),
        );
      add("Red printed medallion fields", 1, rims);
      add("Ivory eight-petal flowers", 4, flowers);
      add("Green outer leaves", 2, leaves);
      add("Golden flower centres", 3, dots);
      add("Green frame bands", 2, guards);
      line("Gold medallion outlines", 3, rims, 3);
      break;
    }
  }
  return d;
}
