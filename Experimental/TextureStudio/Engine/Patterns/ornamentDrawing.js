// Shared hand-authored cubic leaf, vine, palmette and inlay vocabulary.
export function ornamentalInfill(b, craft, cx, cy, r, z = 5) {
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
  const tr = ([x, y]) => [cx + x * r, cy + y * r];
  const polygon = (k, p, stage = z) => b.add(stage, k, p.map(tr));
  const line = (k, p, w = 0.025, stage = z) =>
    b.line(stage, k, p.map(tr), w * r);
  const cubic = (p0, p1, p2, p3, n = 16) =>
    Array.from({ length: n + 1 }, (_, i) => {
      const t = i / n,
        u = 1 - t;
      return [
        p0[0] * u * u * u +
          3 * p1[0] * u * u * t +
          3 * p2[0] * u * t * t +
          p3[0] * t * t * t,
        p0[1] * u * u * u +
          3 * p1[1] * u * u * t +
          3 * p2[1] * u * t * t +
          p3[1] * t * t * t,
      ];
    });
  function leaf(x, y, length, angle, k = 4) {
    const p = [
      ...cubic(
        [0, 0],
        [length * 0.3, -length * 0.45],
        [length * 0.85, -length * 0.24],
        [length, 0],
        7,
      ),
      ...cubic(
        [length, 0],
        [length * 0.72, length * 0.4],
        [length * 0.2, length * 0.2],
        [0, 0],
        7,
      ),
    ];
    polygon(
      3,
      p.map(([u, v]) => [
        x + u * Math.cos(angle) - v * Math.sin(angle),
        y + u * Math.sin(angle) + v * Math.cos(angle),
      ]),
      z + 1,
    );
    polygon(
      k,
      p.map(([u, v]) => [
        x + u * 0.83 * Math.cos(angle) - v * 0.73 * Math.sin(angle),
        y + u * 0.83 * Math.sin(angle) + v * 0.73 * Math.cos(angle),
      ]),
      z + 2,
    );
  }
  if (
    ["split", "nested", "hook", "quartered", "ladder", "labyrinth"].includes(
      craft,
    )
  ) {
    const base =
      craft === "split"
        ? [
            [0, -1],
            [1, 0],
            [0, 1],
            [-1, 0],
          ]
        : sq(-0.98, -0.98, 1.96);
    for (let i = 0; i < 9; i++)
      polygon(
        [0, 1, 4, 1, 0, 3, 0, 5, 2][i],
        base.map(([x, y]) => [x * (1 - i * 0.1), y * (1 - i * 0.1)]),
        z + i,
      );
    for (let side = 0; side < 4; side++)
      for (let q = 0; q < 9; q++) {
        const a = (side * Math.PI) / 2,
          x = -0.8 + q * 0.2,
          y = -0.92;
        const px = x * Math.cos(a) - y * Math.sin(a),
          py = x * Math.sin(a) + y * Math.cos(a);
        polygon(
          q % 2 ? 0 : 1,
          [
            [px - 0.035, py],
            [px, py - 0.035],
            [px + 0.035, py],
            [px, py + 0.035],
          ],
          z + 9,
        );
      }
    if (craft === "labyrinth")
      for (let q = 0; q < 4; q++) {
        const a = (q * Math.PI) / 2,
          p = [
            [-0.78, -0.78],
            [0.78, -0.78],
            [0.78, 0.78],
            [-0.5, 0.78],
            [-0.5, -0.5],
            [0.5, -0.5],
            [0.5, 0.5],
            [-0.2, 0.5],
            [-0.2, -0.2],
            [0.2, -0.2],
          ];
        line(
          0,
          p.map(([x, y]) => [
            x * Math.cos(a) - y * Math.sin(a),
            x * Math.sin(a) + y * Math.cos(a),
          ]),
          0.11,
          z + 9,
        );
      }
    else if (craft === "hook" || craft === "ladder")
      for (let q = 0; q < 4; q++)
        for (let j = 0; j < 4; j++) {
          const a = (q * Math.PI) / 2,
            y = -0.92 + j * 0.17;
          line(
            1,
            [
              [-0.7, y],
              [-0.4, y],
              [-0.4, y + 0.12],
            ].map(([x, y]) => [
              x * Math.cos(a) - y * Math.sin(a),
              x * Math.sin(a) + y * Math.cos(a),
            ]),
            0.045,
            z + 9,
          );
        }
    else
      for (let q = 0; q < 4; q++) {
        const a = (q * Math.PI) / 2;
        polygon(
          q % 2 ? 1 : 0,
          [
            [0, 0],
            [0.35 * Math.cos(a), 0.35 * Math.sin(a)],
            [
              0.35 * Math.cos(a + Math.PI / 2),
              0.35 * Math.sin(a + Math.PI / 2),
            ],
          ],
          z + 9,
        );
      }
    return;
  }
  if (craft === "strap") {
    for (let a of [0, Math.PI / 4])
      for (let rr of [0.92, 0.57]) {
        const p = regular(4, 0, 0, rr, a);
        line(0, [...p, p[0]], 0.12, z);
        line(3, [...p, p[0]], 0.065, z + 1);
        line(1, [...p, p[0]], 0.015, z + 2);
      }
    for (let q = 0; q < 8; q++) {
      const a = (q * Math.PI) / 4;
      line(
        3,
        [
          [-1.05 * Math.cos(a), -1.05 * Math.sin(a)],
          [-0.7 * Math.cos(a + 0.3), -0.7 * Math.sin(a + 0.3)],
          [-0.55 * Math.cos(a), -0.55 * Math.sin(a)],
        ],
        0.04,
        z + 2,
      );
      leaf(0.6 * Math.cos(a), 0.6 * Math.sin(a), 0.24, a + 0.6, 4);
      b.flower(
        z + 3,
        2,
        cx + 0.76 * r * Math.cos(a),
        cy + 0.76 * r * Math.sin(a),
        r * 0.085,
        6,
      );
    }
    b.star(z + 3, 1, cx, cy, r * 0.3, 8, 0.65);
    b.star(z + 4, 2, cx, cy, r * 0.2, 8, 0.7);
    return;
  }
  if (craft === "tree" || craft === "vase") {
    if (craft === "vase") {
      polygon(
        3,
        [
          [-0.33, 0.5],
          [0.33, 0.5],
          [0.18, 0.82],
          [-0.18, 0.82],
        ],
        z + 1,
      );
      polygon(
        2,
        [
          [-0.27, 0.53],
          [0.27, 0.53],
          [0.14, 0.74],
          [-0.14, 0.74],
        ],
        z + 2,
      );
    }
    line(
      3,
      [
        [0, 0.75],
        [0, -0.94],
      ],
      0.045,
    );
    for (let j = 0; j < 6; j++)
      for (const s of [-1, 1]) {
        const y = 0.45 - j * 0.23;
        line(
          3,
          cubic(
            [0, y],
            [0.3 * s, y + 0.2],
            [0.7 * s, y - 0.15],
            [0.45 * s, y - 0.35],
          ),
          0.025,
        );
        leaf(0.13 * s, y, 0.48, j % 2 ? -0.7 * s : -1.1 * s, 4);
        b.flower(z + 3, 2, cx + 0.5 * s * r, cy + (y - 0.32) * r, 0.08 * r, 6);
      }
  } else {
    const n = craft === "palmette" ? 4 : craft === "rosette" ? 8 : 6;
    for (let j = 0; j < n; j++) {
      const a = (j * Math.PI * 2) / n,
        rot = ([x, y]) => [
          x * Math.cos(a) - y * Math.sin(a),
          x * Math.sin(a) + y * Math.cos(a),
        ];
      line(
        3,
        cubic([0, 0], [0.75, -0.5], [1.3, 0.3], [0.65, 0.55]).map(rot),
        0.024,
      );
      for (let t of [0.32, 0.58, 0.82]) {
        const p = rot([t, -0.25 * Math.sin(t * 3)]);
        leaf(...p, 0.31, a - 0.9, 4);
        leaf(...p, 0.23, a + 0.9, 5);
      }
      if (craft === "palmette")
        for (let q = -2; q <= 2; q++) {
          const p = rot([0.7, 0.12]);
          leaf(...p, 0.32, a + q * 0.35, q % 2 ? 1 : 2);
        }
      else {
        const p = tr(rot([0.75, 0.45]));
        b.flower(z + 3, j % 2 ? 1 : 2, ...p, r * 0.13, 7);
      }
    }
  }
  b.flower(z + 3, 1, cx, cy, r * 0.17, 8);
  b.disk(z + 4, 2, cx, cy, r * 0.06);
}
