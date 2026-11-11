// Run after npm run build. Verify relocation without regenerating approved assets.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
const web = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const repo = path.resolve(web, '../..');
for (const page of ['index.html', 'icons.html', 'collection-icon-options.html']) {
  assert.ok(fs.existsSync(path.join(web, page)), page);
  assert.ok(!fs.existsSync(path.join(repo, page)), `Old root entry remains: ${page}`);
  assert.ok(fs.existsSync(path.join(web, 'dist', page)), `Build omitted ${page}`);
}
for (const page of ['icons.html', 'collection-icon-options.html']) {
  const source = fs.readFileSync(path.join(web, page), 'utf8');
  assert.equal(fs.readFileSync(path.join(web, 'dist', page), 'utf8'), source);
  for (const [, url] of source.matchAll(/(?:src|href)="([^"]+)"/g)) {
    if (/^(?:[a-z]+:|#|\/\/)/i.test(url)) continue;
    for (const root of [web, path.join(web, 'dist')]) {
      assert.ok(fs.existsSync(path.resolve(root, url)), `${page}: missing ${url}`);
    }
  }
}
const icons = JSON.parse(fs.readFileSync(path.join(repo, 'EngineContent/Icons/Manifest.json'), 'utf8'));
for (const entry of icons) {
  assert.ok(entry.reference.startsWith('Experimental/FrontierEditor/'), entry.reference);
  const approved = fs.readFileSync(path.join(repo, entry.reference), 'utf8');
  const compact = ['folder-scene.svg', 'folder-world.svg', 'folder-materials.svg'].includes(entry.file);
  const expected = compact ? approved.replace(/<text\b[^>]*>[\s\S]*?<\/text>/g, '').replace(/<title\b[^>]*>[\s\S]*?<\/title>/g, '').replace(/\saria-labelledby="[^"]*"/g, '') : approved;
  assert.equal(fs.readFileSync(path.join(repo, 'EngineContent/Icons', entry.file), 'utf8'), expected, entry.file);
}
console.log(`PASS: three relocated pages, production gallery links/assets, and ${icons.length} native SVG authorities.`);
