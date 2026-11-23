//============================================================================================================================================
//                                                      COMPILEVECTORSURFACE.MJS
//============================================================================================================================================
// 🧩 Compile and authenticate the typed Float32 vector-surface compute entry point.

import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {fileURLToPath, pathToFileURL} from 'node:url';
import Assert from 'node:assert/strict';

const [CompilerPath, Destination] = process.argv.slice(2);
Assert(CompilerPath && Destination, 'Expected Slang WASM entry path and archive destination');
const Compiler = await (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default();
const Global = Compiler.createGlobalSession();
const Target = Compiler.getCompileTargets().find(Choice => Choice.name === 'SPIRV');
Assert(Global && Target, 'Expected portable SPIR-V compiler');
const Root = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)), '..');
const SourcePath = Path.join(Root, 'Engine/Render/Shader/VectorSurface.slang');
const Source = Fs.readFileSync(SourcePath, 'utf8');
const Mount = '/frontier-vector-surface';
Compiler.FS.mkdirTree(Mount);
Compiler.FS.writeFile(Mount + '/VectorSurface.slang', Source);
const Dependencies = [];
for (const Name of ['VectorCoverage.slang'])
{
    const Bytes = Fs.readFileSync(Path.join(Root, 'Engine/Render/Shader', Name));
    Compiler.FS.writeFile(Mount + '/' + Name, Bytes);
    Dependencies.push({file: 'Engine/Render/Shader/' + Name, sha256: Crypto.createHash('sha256').update(Bytes).digest('hex')});
}
const Session = Global.createSession(Target.value);
const Module = Session.loadModuleFromSource(Source, 'VectorSurface', Mount + '/VectorSurface.slang');
Assert(Module, JSON.stringify(Compiler.getLastError()));
const Entry = Module.findEntryPointByName('VectorSurfaceCompute');
Assert(Entry, JSON.stringify(Compiler.getLastError()));
const Linked = Session.createCompositeComponentType([Module, Entry]).link();
Assert(Linked, JSON.stringify(Compiler.getLastError()));
const Bytes = Buffer.from(Linked.getEntryPointCodeBlob(0, 0));
Assert(Bytes.length >= 20 && Bytes.readUInt32LE(0) === 0x07230203, 'Expected valid SPIR-V');
const Reflection = Linked.getLayout(0).toJsonObject();
const Projection = Reflection.parameters.find(Parameter => Parameter.name === 'Projection');
// 📝 VectorSurface's own CoverageProjection grew from 6 uints (24 bytes) to 8 — the shared
// Width/Height/Count/Root/TileSize/TilesPerRow fields plus the surface-only Channel/Contributions
// fields appended last — when the coverage/selection shaders gained per-tile leaf-gating. 32 bytes
// is the current, intentional ABI; keep this in sync with Engine/Render/Shader/VectorSurface.slang's
// `struct CoverageProjection` and the `OutputFormat == VK_FORMAT_R32G32B32A32_SFLOAT ? 32u : 24u`
// branch in Engine/Render/VectorCoverageRasterization.cpp.
Assert.equal(Projection?.type.elementType.sizes.find(Size => Size.kind === 'uniform').value, 32,
    'Float32 surface compute push ABI must contain 32 bytes.');
let StorageImageFormat = 0;
for (let Offset = 20; Offset < Bytes.length;)
{
    const Header = Bytes.readUInt32LE(Offset), Words = Header >>> 16;
    Assert(Words > 0 && Offset + Words * 4 <= Bytes.length, 'Malformed compiled Float32 surface SPIR-V.');
    if ((Header & 65535) === 25 && Words >= 9 && Bytes.readUInt32LE(Offset + 28) === 2)
    {
        const Format = Bytes.readUInt32LE(Offset + 32);
        Assert.equal(Format, 1, 'VectorSurface storage image must declare Rgba32f.');
        StorageImageFormat = Format;
    }
    Offset += Words * 4;
}
Assert.equal(StorageImageFormat, 1, 'Float32 shader archive has no typed Rgba32f storage image.');
Fs.mkdirSync(Destination, {recursive: true});
Fs.writeFileSync(Path.join(Destination, 'VectorSurfaceCompute.spv'), Bytes);
const Manifest = {compiler: Compiler.getVersionString(), entry: 'VectorSurfaceCompute', target: 'SPIRV',
    output_format: 'rgba32f', push_constant_bytes: 32,
    source_sha256: Crypto.createHash('sha256').update(Source).digest('hex'), dependencies: Dependencies,
    binary_sha256: Crypto.createHash('sha256').update(Bytes).digest('hex')};
Fs.writeFileSync(Path.join(Destination, 'VectorSurfaceCompilation.json'), JSON.stringify(Manifest, null, 2) + '\n');
console.log('PASS: VectorSurfaceCompute emits typed RGBA32F storage.');
