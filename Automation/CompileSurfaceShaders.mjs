//============================================================================================================================================
//                                                        COMPILESURFACESHADERS.MJS
//============================================================================================================================================
// 🧩 Compile the authored Slang surface entries through a supplied portable compiler; persist reflection alongside SPIR-V.

import Fs from 'node:fs';
import Path from 'node:path';
import Crypto from 'node:crypto';
import {pathToFileURL, fileURLToPath} from 'node:url';
import Assert from 'node:assert/strict';

const [CompilerPath, Destination] = process.argv.slice(2);
Assert(CompilerPath && Destination,'Expected Slang WASM JavaScript entry path and output directory');
const InitializeCompiler = (await import(pathToFileURL(Path.resolve(CompilerPath)).href)).default;
const Compiler = await InitializeCompiler();
const Global = Compiler.createGlobalSession();
const Target = Compiler.getCompileTargets().find(Choice=>Choice.name==='SPIRV');
Assert(Global && Target,'Portable compiler must support SPIR-V');
const Root = Path.resolve(Path.dirname(fileURLToPath(import.meta.url)),'..');
Fs.mkdirSync(Destination,{recursive:true});
const ShaderDirectory=Path.join(Root,'Engine/Render/Shader');
const MountedDirectory='/frontier-surface-shaders';
Compiler.FS.mkdirTree(MountedDirectory);
const SourceDependencies=[];
for (const File of Fs.readdirSync(ShaderDirectory).filter(Name=>Name.endsWith('.slang')).sort())
{
    const Contents=Fs.readFileSync(Path.join(ShaderDirectory,File));
    Compiler.FS.writeFile(MountedDirectory+'/'+File,Contents);
    SourceDependencies.push({file:'Engine/Render/Shader/'+File,sha256:Crypto.createHash('sha256').update(Contents).digest('hex')});
}
const CompilerSource=Fs.readFileSync(Path.resolve(CompilerPath));
const CompilerBinary=Fs.readFileSync(Path.join(Path.dirname(Path.resolve(CompilerPath)),'slang-wasm.wasm'));
const Manifest = {compiler:Compiler.getVersionString(),target:'SPIRV',
    compiler_javascript_sha256:Crypto.createHash('sha256').update(CompilerSource).digest('hex'),
    compiler_wasm_sha256:Crypto.createHash('sha256').update(CompilerBinary).digest('hex'),sources:SourceDependencies,entries:[]};
for (const [File,Entries] of [['CadSurface.slang',['CadSurfaceVertex','CadSurfaceFragment']],
                             ['CadEdge.slang',['CadEdgeVertex','CadEdgeFragment']],
                             ['MatCapSurface.slang',['MatCapSurfaceVertex','MatCapSurfaceFragment']],
                             ['PreviewPbrSurface.slang',['PreviewPbrSurfaceVertex','PreviewPbrSurfaceFragment']],
                             ['MaterialSurface.slang',['MaterialSurfaceVertex','MaterialSurfaceFragment']],
                             ['BrowserPreview.slang',['BrowserPreviewVertex','BrowserPreviewFragment']],
                             ['BrowserDrafting.slang',['BrowserDraftingVertex','BrowserDraftingFragment']]])
{
    const SourcePath=Path.join(Root,'Engine/Render/Shader',File);
    const Source=Fs.readFileSync(SourcePath,'utf8');
    for(const Name of Entries)
    {
        const Session=Global.createSession(Target.value);
        Assert(Session,JSON.stringify(Compiler.getLastError()));
        try
        {
            const Library=Session.loadModuleFromSource(Source,File.replace('.slang',''),MountedDirectory+'/'+File);
            Assert(Library,JSON.stringify(Compiler.getLastError()));
            const Entry=Library.findEntryPointByName(Name);
            Assert(Entry,JSON.stringify(Compiler.getLastError()));
            const Combined=Session.createCompositeComponentType([Library,Entry]);
            Assert(Combined,JSON.stringify(Compiler.getLastError()));
            const Linked=Combined.link();
            Assert(Linked,JSON.stringify(Compiler.getLastError()));
            const Blob=Linked.getEntryPointCodeBlob(0,0);
            Assert(Blob,JSON.stringify(Compiler.getLastError()));
            const Bytes=Buffer.from(Blob);
            Assert(Bytes.length>=20 && Bytes.readUInt32LE(0)===0x07230203,'Expected SPIR-V bytes');
            const Reflection=Linked.getLayout(0).toJsonObject();
            const Push=Reflection.parameters.find(Parameter=>Parameter.name==='Projection');
            Assert.equal(Push?.binding.kind,'pushConstantBuffer');
            const MaterialEnabled=Name.startsWith('MaterialSurface');
            const PbrEnabled=Name.startsWith('PreviewPbr');
            const MatCapEnabled=Name.startsWith('MatCap') || PbrEnabled || MaterialEnabled;
            const BrowserEnabled=Name.startsWith('Browser');
            const PushBytes=MatCapEnabled ? 128 : 112;
            Assert.deepEqual(Push.type.elementType.fields.map(Field=>[Field.name,Field.binding.offset,Field.binding.size]),
                MatCapEnabled ? [['ClipFromSurface',0,64],['ViewNormalRow0',64,16],['ViewNormalRow1',80,16],
                                 ['ViewNormalRow2',96,16],['SurfaceTint',112,16]] :
                BrowserEnabled ? [['ClipFromSurface',0,64],['EyeChannel',64,16],['RasterOptions',80,16],['ViewDirection',96,16]] :
                                [['ClipFromSurface',0,64],['SurfaceColour',64,16],['EdgeColour',80,16],['DisplayStroke',96,16]]);
            Assert.equal(Push.type.elementType.sizes.find(Size=>Size.kind==='uniform').value,PushBytes);
            if(BrowserEnabled && Name==='BrowserPreviewFragment')
            {
                for(const [Resource,Index] of [['EonTexture',0],['EonSampler',1]])
                {
                    const Parameter=Reflection.parameters.find(Parameter=>Parameter.name===Resource);
                    Assert.equal(Parameter?.binding.kind,'descriptorTableSlot');
                    Assert.equal(Parameter?.binding.index,Index);
                    Assert.equal(Parameter?.type.kind,Index===0 ? 'resource' : 'samplerState');
                    if(Index===0) Assert.equal(Parameter.type.baseShape,'texture2D');
                    Assert.equal(Parameter?.binding.space ?? 0,0);
                }
            }
            if(MatCapEnabled && !MaterialEnabled)
            {
                for(const [Resource,Index] of [[PbrEnabled ? 'AlbedoTexture' : 'MatCapTexture',0],[PbrEnabled ? 'SurfaceSampler' : 'MatCapSampler',1]])
                {
                    const Parameter=Reflection.parameters.find(Parameter=>Parameter.name===Resource);
                    Assert.equal(Parameter?.binding.kind,'descriptorTableSlot');
                    Assert.equal(Parameter?.binding.index,Index);
                    Assert.equal(Parameter?.type.kind,Index===0 ? 'resource' : 'samplerState');
                    if(Index===0) Assert.equal(Parameter.type.baseShape,'texture2D');
                    Assert.equal(Parameter?.binding.space ?? 0,0);
                }
            }
            if(PbrEnabled)
            {
                const Uniform=Reflection.parameters.find(Parameter=>Parameter.name==='Scattering');
                Assert.equal(Uniform.binding.index,0);
                Assert.equal(Uniform.binding.space,1);
                Assert.equal(Uniform.type.elementType.sizes.find(Size=>Size.kind==='uniform').value,928);
                const Strata=Uniform.type.elementType.fields.find(Field=>Field.name==='Strata');
                Assert.equal(Strata.binding.offset,160);
                Assert.equal(Strata.type.elementCount,8);
                Assert.equal(Strata.type.elementType.sizes.find(Size=>Size.kind==='uniform').value,96);
            }
            if(MaterialEnabled)
            {
                const Uniform=Reflection.parameters.find(Parameter=>Parameter.name==='Material');
                Assert.equal(Uniform.binding.index,0);
                Assert.equal(Uniform.binding.space,1);
                Assert.equal(Uniform.type.elementType.sizes.find(Size=>Size.kind==='uniform').value,1760);
                Assert.deepEqual(Uniform.type.elementType.fields.map(Field=>[Field.name,Field.binding.offset]),
                    [['Illumination',0],['UvScaleOffset',928],['RadianceNormal',944],['TextureUse',960],
                     ['StratumUv',1088],['StratumNormal',1216],['DirectionalOptics',1344],['Paint',1472],['PreviewConfiguration',1744]]);
                const Paint=Uniform.type.elementType.fields.find(Field=>Field.name==='Paint');
                Assert.equal(Paint.type.sizes.find(Size=>Size.kind==='uniform').value,272);
                Assert.deepEqual(Paint.type.fields.map(Field=>[Field.name,Field.binding.offset]),
                    [['Configuration',0],['Distribution',16],['Finish',32],['Relief',48],['Palette',64],['AverageTint',256]]);
                const Images=Reflection.parameters.find(Parameter=>Parameter.name==='MaterialTextures');
                Assert.equal(Images.binding.index,0);
                Assert.equal(Images.binding.space ?? 0,0);
                Assert.equal(Images.type.elementCount,4);
                Assert.equal(Images.type.elementType.baseShape,'texture2D');
                Assert.equal(Images.type.elementType.array,true);
                const Sampler=Reflection.parameters.find(Parameter=>Parameter.name==='MaterialSampler');
                Assert.equal(Sampler.binding.index,1);
                const AuthoringMaps=Reflection.parameters.find(Parameter=>Parameter.name==='AuthoringMaps');
                Assert.equal(AuthoringMaps?.binding.index,2);
                Assert.equal(AuthoringMaps?.type.elementCount,6);
                Assert.equal(AuthoringMaps?.type.elementType.baseShape,'texture2D');
                Assert.equal(AuthoringMaps?.type.elementType.array,true);
            }
            if(Name.endsWith('Vertex'))
            {
                const Attributes=Reflection.entryPoints[0].parameters[0].type.fields;
                Assert.deepEqual(Attributes.map(Attribute=>Attribute.binding.index),
                    MaterialEnabled ? [0,1,2,3] : BrowserEnabled ? (Name.startsWith('BrowserDrafting') ? [0,1,2,3,4] : [0,1,2,3]) : [0,1]);
            }
            const Capabilities=[];
            const SpirvEntries=[];
            for(let Offset=20;Offset<Bytes.length;)
            {
                const Instruction=Bytes.readUInt32LE(Offset);
                const Words=Instruction>>>16;
                Assert(Words>0 && Offset+Words*4<=Bytes.length,'Malformed SPIR-V instruction');
                if((Instruction & 65535)===17) Capabilities.push(Bytes.readUInt32LE(Offset+4));
                if((Instruction & 65535)===15)
                    SpirvEntries.push(Bytes.subarray(Offset+12,Offset+Words*4).toString('utf8').split('\0')[0]);
                Offset+=Words*4;
            }
            Assert.deepEqual(Capabilities,[1],'Surface shaders must need only the core Shader capability, not optional features');
            Assert.deepEqual(SpirvEntries,['main']);
            Fs.writeFileSync(Path.join(Destination,Name+'.spv'),Bytes);
            Fs.writeFileSync(Path.join(Destination,Name+'.json'),JSON.stringify(Reflection,null,2)+'\n');
            Manifest.entries.push({source:'Engine/Render/Shader/'+File,entry:Name,bytes:Bytes.length,
                spirv_version:Bytes.readUInt32LE(4),spirv_entry:'main',capabilities:Capabilities,push_constant_bytes:PushBytes,source_sha256:Crypto.createHash('sha256').update(Source).digest('hex'),
                spirv_sha256:Crypto.createHash('sha256').update(Bytes).digest('hex')});
            console.log(Name,Bytes.length,'bytes');
        }
        finally {Session.delete();}
    }
}
Global.delete();
Fs.writeFileSync(Path.join(Destination,'ShaderCompilation.json'),JSON.stringify(Manifest,null,2)+'\n');
