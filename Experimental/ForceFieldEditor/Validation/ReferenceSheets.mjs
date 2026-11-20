#!/usr/bin/env node
/*============================================================================================================================================
                                                     REFERENCE SHEETS
============================================================================================================================================*/
// 🧩 Headless proof sheets: renders the field visualisation for each axis and a contact sheet of every generator to plain PPM files using the same
// rasteriser the studio runs in the browser. Run with `node Experimental/ForceFieldEditor/Validation/ReferenceSheets.mjs [outDir]`.

import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import * as M from '../Engine/FieldMath.js';
import { FORCE_LAWS } from '../Engine/ForceFieldCatalog.js';
import { PrepareFrame, DefaultDocument, NewSource, SanitizeDocument } from '../Engine/FieldEvaluator.js';
import { CreateSurface, SectionProjection, SectionHeatmap, SectionSources, Disc, Line, SurfaceToPpm, RenderSectionSurface } from '../Viewport/SectionRasterizer.js';
import { SuggestMaximum, QuantityAt } from '../Engine/FieldEvaluator.js';

const here = path.dirname(fileURLToPath(import.meta.url));
// Default to a scratch directory outside the repository: proof sheets are generated artifacts, not sources.
const outDir = path.resolve(process.argv[2] || process.env.FORCEFIELD_SHEETS_DIR || '/tmp/frontier-forcefield-sheets');
fs.mkdirSync(outDir, { recursive: true });

const write = (name, surface) => {
  const file = path.join(outDir, name);
  fs.writeFileSync(file, SurfaceToPpm(surface));
  return file;
};

// 1 — the full composite for each sectional axis of the default document.
const doc = DefaultDocument();
const runtime = PrepareFrame(doc.Sources, 1.6, doc.Domain);
for (const axis of ['XY', 'XZ', 'YZ']) {
  const { Surface } = RenderSectionSurface(doc, runtime, 1.6, axis, { Width: 720, Height: 470, Probe: [2.4, 1.8, 1.2], Selected: doc.Sources[0].Id });
  write(`section-${axis}.ppm`, Surface);
}

// 2 — one thumbnail per generator: its own field rendered on a neutral envelope, in the domain frame.
const thumb = 132;
const columns = 5;
const rows = Math.ceil(FORCE_LAWS.length / columns);
const sheet = CreateSurface(thumb * columns, thumb * rows + 6);
sheet.Fill(13, 14, 17, 255);
FORCE_LAWS.forEach((law, index) => {
  const col = index % columns;
  const row = Math.floor(index / columns);
  const single = SanitizeDocument({
    Domain: { Min: [-10, -10, -6], Max: [10, 10, 6] },
    Sources: [NewSource(law.Id, { Position: [0, 0, 0] })],
    Overlay: { Quantity: law.Channel === 'acceleration' ? 'Acceleration' : 'Speed', Colormap: 'Engineering' },
  });
  const cellRuntime = PrepareFrame(single.Sources, 1.1, single.Domain);
  const cell = CreateSurface(thumb - 4, thumb - 4);
  cell.Fill(9, 10, 12, 255);
  const projection = SectionProjection(cell, 'XZ', single.Domain, 4);
  const peak = law.Id === 'Drag' ? 1 : SuggestMaximum(cellRuntime, single.Domain, 1.1, 'Speed');
  SectionHeatmap(cell, projection, cellRuntime, 1.1, {
    Quantity: single.Overlay.Quantity,
    Colormap: 'Engineering',
    Maximum: peak,
    Offset: 0,
    Opacity: 0.9,
    Domain: single.Domain,
    Samples: 52,
  });
  SectionSources(cell, projection, single, 1.1, { Offset: 0, ShowEnvelopes: false });
  // frame key and ordinal tick
  Line(cell, 2, 2, cell.Width - 2, 2, [60, 64, 72], 0.6, 1);
  for (let x = 0; x < cell.Width; x++) for (let y = 0; y < cell.Height; y++) {
    if (x < 2 || y < 2 || x > cell.Width - 3 || y > cell.Height - 3) sheet.Blend(col * thumb + 2 + x, row * thumb + 2 + y, 40, 44, 52, 1);
  }
  for (let x = 0; x < cell.Width; x++) for (let y = 0; y < cell.Height; y++) {
    const i = (y * cell.Width + x) * 4;
    sheet.Blend(col * thumb + 2 + x, row * thumb + 2 + y, cell.Data[i], cell.Data[i + 1], cell.Data[i + 2], 1);
  }
  Disc(sheet, col * thumb + 8, row * thumb + 8, 3, index === 0 ? [255, 178, 56] : [200, 206, 216], 0.95);
});

const files = [
  ...['XY', 'XZ', 'YZ'].map((axis) => path.join(outDir, `section-${axis}.ppm`)),
  write('generator-contact-sheet.ppm', sheet),
];
console.log(`Rendered ${files.length} reference sheets into ${outDir}`);
for (const file of files) console.log(`  · ${path.basename(file)} (${(fs.statSync(file).size / 1024).toFixed(0)} KiB)`);
