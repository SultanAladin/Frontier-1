/*============================================================================================================================================
                                                        COMPILEVECTORCOVERAGE.MJS
============================================================================================================================================*/
// 🧩 Compile and reflect the shared Slang GPU coverage ABI using an explicitly provisioned compiler.

import Fs from "node:fs";
import Path from "node:path";
import Assert from "node:assert/strict";
import Crypto from "node:crypto";
import { pathToFileURL } from "node:url";
const [CompilerPath, Destination] = process.argv.slice(2);
Assert(CompilerPath && Destination, "Expected Slang WASM entry and output directory.");
const Compiler = await (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default();
const Digest = Bytes => Crypto.createHash("sha256").update(Bytes).digest("hex");
const Manifest = { sources: [{ file: "Engine/Render/Shader/VectorCoverage.slang",
    sha256: Digest(Fs.readFileSync("Engine/Render/Shader/VectorCoverage.slang")) }], entries: [],
    compiler: Compiler.getVersionString(), compiler_javascript_sha256: Digest(Fs.readFileSync(CompilerPath)),
    compiler_wasm_sha256: Digest(Fs.readFileSync(Path.join(Path.dirname(CompilerPath), "slang-wasm.wasm"))) };
const Global = Compiler.createGlobalSession();
const Session = Global.createSession(Compiler.getCompileTargets().find(Target => Target.name === "SPIRV").value);
try
{
    const Library = Session.loadModuleFromSource(Fs.readFileSync("Engine/Render/Shader/VectorCoverage.slang", "utf8"), "VectorCoverage", "/VectorCoverage.slang");
    Assert(Library, JSON.stringify(Compiler.getLastError()));
    for (const EntryCaption of ["VectorCoverageCompute", "VectorSelectionCompute"])
    {
        const Entry = Library.findEntryPointByName(EntryCaption);
        Assert(Entry, JSON.stringify(Compiler.getLastError()));
        const Combined = Session.createCompositeComponentType([Library, Entry]);
        Assert(Combined, JSON.stringify(Compiler.getLastError()));
        const Linked = Combined.link(); Assert(Linked, JSON.stringify(Compiler.getLastError()));
        const Blob = Linked.getEntryPointCodeBlob(0, 0); Assert(Blob, JSON.stringify(Compiler.getLastError()));
        const Reflection = Linked.getLayout(0).toJsonObject();
        const Spirv = Buffer.from(Blob);
        let ComputeEntry = false;
        for (let Offset = 20; Offset < Spirv.length;)
        {
            const Header = Spirv.readUInt32LE(Offset), Words = Header >>> 16;
            Assert(Words > 0 && Offset + Words * 4 <= Spirv.length, "Malformed compiled SPIR-V instruction.");
            if ((Header & 65535) === 15 && Words >= 4 && Spirv.readUInt32LE(Offset + 4) === 5)
                ComputeEntry ||= Spirv.subarray(Offset + 12, Offset + Words * 4).toString().split("\0")[0] === "main";
            Offset += Words * 4;
        }
        Assert(ComputeEntry, "The Vulkan archive must expose the compute entry named main.");
        for (const [Name, Size, Binding] of [["Instructions", 112, 0], ["Segments", 32, 2]])
        {
            const Parameter = Reflection.parameters.find(Choice => Choice.name === Name);
            Assert.equal(Parameter.type.resultType.sizes.find(Entry => Entry.kind === "uniform").value, Size);
            Assert.equal(Parameter.binding.index, Binding);
        }
        // 📝 CoverageProjection grew from 4 uints (16 bytes) to 6 — Width/Height/Count/Root plus the CPU
        // tile-binning fields TileSize/TilesPerRow — when the coverage/selection shaders gained per-tile
        // leaf-gating. 24 bytes is the current, intentional ABI; keep this in sync with
        // Engine/Render/Shader/VectorCoverage.slang's `struct CoverageProjection` and the matching
        // `Resident->PushConstantBytes` branch in Engine/Render/VectorCoverageRasterization.cpp.
        Assert.equal(Reflection.parameters.find(Parameter => Parameter.name === "Projection").type.elementType.sizes.find(Entry => Entry.kind === "uniform").value, 24);

        Fs.mkdirSync(Destination, { recursive: true });
        Fs.writeFileSync(Path.join(Destination, EntryCaption + ".spv"), Buffer.from(Blob));
        Fs.writeFileSync(Path.join(Destination, EntryCaption + ".json"), JSON.stringify(Reflection, null, 2));
        Manifest.entries.push({ entry: EntryCaption, spirv_sha256: Digest(Buffer.from(Blob)) });
        console.log(Compiler.getVersionString(), EntryCaption + " compiled; instruction/segment ABI 112/32 bytes.");
    }
    Fs.writeFileSync(Path.join(Destination, "VectorCompilation.json"), JSON.stringify(Manifest, null, 2));
}
finally { Session.delete(); Global.delete(); }
