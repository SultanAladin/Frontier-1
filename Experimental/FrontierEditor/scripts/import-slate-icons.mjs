/** Render the pinned, reviewed SVG component definitions only. No upstream app runs. */
import fs from 'node:fs';
import vm from 'node:vm';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {transformSync} from 'esbuild';
import React from 'react';
import {renderToStaticMarkup} from 'react-dom/server';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const dir=path.join(root,'vendor/slate-new-icons');
const source=fs.readFileSync(path.join(dir,'App.tsx'),'utf8');
const entries=[...source.matchAll(/<IconWrapper name="([^"]+)"><(\w+)\s*\/><\/IconWrapper>/g)].map(m=>({name:m[1],component:m[2]}));
if(entries.length!==102)throw new Error('Unexpected upstream gallery inventory; review source before importing.');
const components=source.slice(source.indexOf('const DocumentWidget'),source.indexOf('const IconWrapper'));
// Upstream component bodies are SVG JSX, numeric array maps and styles only.
// Fail closed if future snapshots introduce external or executable content.
if(/\b(?:fetch|eval|require|import|process|globalThis|window|document|navigator)\b|<(?:script|image|foreignObject)\b|\bon[A-Z]\w*\s*=/.test(components.replace(/\{\/\*[\s\S]*?\*\/\}/g,'')))throw new Error('Source requires review: not static SVG definitions.');
const js=transformSync(components+'\nglobalThis.assets = ['+entries.map(e=>e.component).join(',')+'];',{loader:'tsx',jsx:'transform',jsxFactory:'React.createElement',jsxFragment:'React.Fragment',target:'es2020'}).code;
const context=vm.createContext({React});
vm.runInContext(js,context,{timeout:3000});
const assets=entries.map((entry,i)=>{
  const svg=renderToStaticMarkup(React.createElement(context.assets[i]));
  if(!svg.startsWith('<svg ')||!svg.endsWith('</svg>'))throw new Error('Non-SVG component '+entry.name);
  return {...entry,id:'slate-'+entry.name.toLowerCase().replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,''),svg};
});
if(new Set(assets.map(a=>a.id)).size!==assets.length)throw new Error('Duplicate names');
fs.writeFileSync(path.join(dir,'icons.json'),JSON.stringify(assets,null,2)+'\n');
console.log(`Rendered ${assets.length} upstream SVG components, preserving React SVG attribute casing and dynamic geometry.`);
