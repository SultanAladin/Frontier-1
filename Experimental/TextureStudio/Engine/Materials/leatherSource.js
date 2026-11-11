// One vector height-source atlas: fine nappa / full grain / bull grain / belly.
// Authored curves are regenerated deterministically; no downloaded photography.
// Every path is wrapped at its patch boundary BEFORE the four patches are packed.
export function leatherSourceSVG() {
  let seed = 3917;
  const rnd = () => {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    return seed / 4294967296;
  };
  const S = 512;
  let defs = "",
    body = "";
  for (let patch = 0; patch < 4; patch++) {
    const ox = (patch % 2) * S,
      oy = Math.floor(patch / 2) * S;
    let marks = [],
      crowns = [];
    const add = (d, width, gray) =>
      marks.push(
        `<path d="${d}" fill="none" stroke="rgb(${gray},${gray},${gray})" stroke-width="${width}" stroke-linecap="round"/>`,
      );
    if (patch === 3) {
      const rows = 9,
        cols = 6;
      const ys = Array.from(
        { length: rows },
        (_, i) => (i * S) / rows + (rnd() - 0.5) * 13,
      );
      for (let j = 0; j < rows; j++) {
        const y = ys[j],
          next = j === rows - 1 ? ys[0] + S : ys[j + 1];
        add(`M-20 ${y} C120 ${y - 8} 350 ${y + 9} 532 ${y}`, 3.6, 76);
        for (let i = 0; i < cols; i++) {
          const x = (i * S) / cols + (rnd() - 0.5) * 23;
          add(
            `M${x} ${y} C${x - 8} ${y + 16} ${x + 7} ${next - 12} ${x + 3} ${next}`,
            2.6 + rnd(),
            92,
          );
          for (let k = 0; k < 4; k++) {
            const t = y + 5 + rnd() * (next - y - 10);
            add(
              `M${x - 2} ${t} q${-7 - rnd() * 7} ${rnd() * 4 - 2} ${-14 - rnd() * 10} ${rnd() * 9 - 4}`,
              0.5 + rnd() * 0.5,
              139,
            );
          }
        }
      }
    } else {
      // A periodic, irregular three-way furrow network. Polygon clipping is
      // performed once to author vector curves; no F1/F2 cellular shading.
      const n = [27, 18, 11][patch],
        step = S / n;
      const nodes = Array.from({ length: n }, () =>
        Array.from({ length: n }, () => [
          (rnd() - 0.5) * step * 0.92,
          (rnd() - 0.5) * step * 0.92,
        ]),
      );
      const site = (x, y) => {
        const a = nodes[((y % n) + n) % n][((x % n) + n) % n];
        return [x * step + a[0], y * step + a[1]];
      };
      for (let y = 0; y < n; y++)
        for (let x = 0; x < n; x++) {
          const a = site(x, y);
          crowns.push(
            `<circle cx="${a[0]}" cy="${a[1]}" r="${step * 0.7}" fill="url(#crown${patch})"/>`,
          );
          let polygon = [
            [a[0] - step * 2, a[1] - step * 2],
            [a[0] + step * 2, a[1] - step * 2],
            [a[0] + step * 2, a[1] + step * 2],
            [a[0] - step * 2, a[1] + step * 2],
          ];
          for (let dy = -2; dy <= 2; dy++)
            for (let dx = -2; dx <= 2; dx++) {
              if (!dx && !dy) continue;
              const b = site(x + dx, y + dy),
                nx = b[0] - a[0],
                ny = b[1] - a[1],
                c =
                  (b[0] * b[0] + b[1] * b[1] - a[0] * a[0] - a[1] * a[1]) * 0.5;
              const out = [];
              for (let j = 0; j < polygon.length; j++) {
                const p = polygon[j],
                  q = polygon[(j + 1) % polygon.length],
                  dp = p[0] * nx + p[1] * ny - c,
                  dq = q[0] * nx + q[1] * ny - c;
                if (dp <= 0) out.push(p);
                if (dp < 0 !== dq < 0) {
                  const t = dp / (dp - dq);
                  out.push([
                    p[0] + (q[0] - p[0]) * t,
                    p[1] + (q[1] - p[1]) * t,
                  ]);
                }
              }
              polygon = out;
            }
          for (let j = 0; j < polygon.length; j++) {
            const p = polygon[j],
              q = polygon[(j + 1) % polygon.length];
            const mx = (p[0] + q[0]) * 0.5,
              my = (p[1] + q[1]) * 0.5;
            const bend =
              Math.sin(mx * 0.07 + my * 0.12) * 0.08 * (p[0] < q[0] ? 1 : -1);
            const cx = mx + (q[1] - p[1]) * bend,
              cy = my - (q[0] - p[0]) * bend;
            const d = `M${p[0].toFixed(3)} ${p[1].toFixed(3)} Q${cx.toFixed(3)} ${cy.toFixed(3)} ${q[0].toFixed(3)} ${q[1].toFixed(3)}`;
            const width = [0.8, 1.25, 2.2][patch];
            add(d, width * 3.1, 172);
            add(d, width, [142, 126, 110][patch]);
          }
        }
    }
    for (let i = 0; i < (patch === 3 ? 0 : 950); i++) {
      const x = rnd() * S,
        y = rnd() * S,
        r = 0.35 + rnd() * 0.65;
      marks.push(
        `<ellipse cx="${x}" cy="${y}" rx="${r}" ry="${r * 0.58}" fill="#999"/>`,
      );
    }
    defs += `<radialGradient id="crown${patch}"><stop offset="0" stop-color="#dedede" stop-opacity=".65"/><stop offset="1" stop-color="#bebebe" stop-opacity="0"/></radialGradient><filter id="soft${patch}" x="-10%" y="-10%" width="120%" height="120%" color-interpolation-filters="sRGB"><feGaussianBlur stdDeviation="${patch === 3 ? 1.5 : patch === 2 ? 0.7 : 0.45}"/></filter><clipPath id="hide${patch}"><rect x="${ox}" y="${oy}" width="512" height="512"/></clipPath><g id="grain${patch}">${crowns.join("")}${marks.join("")}</g>`;
    body += `<g clip-path="url(#hide${patch})"><rect x="${ox}" y="${oy}" width="512" height="512" fill="#bebebe"/>`;
    for (let y = -1; y <= 1; y++)
      for (let x = -1; x <= 1; x++)
        body += `<use href="#grain${patch}" filter="url(#soft${patch})" transform="translate(${ox + x * S} ${oy + y * S})"/>`;
    body += "</g>";
  }
  return `<svg xmlns="http://www.w3.org/2000/svg" width="2048" height="2048" viewBox="0 0 1024 1024"><defs>${defs}</defs>${body}</svg>`;
}
