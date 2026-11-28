import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import { join } from "node:path";
import { WgslReflect } from "wgsl_reflect/wgsl_reflect.module.js";
import { DecodeControlVehicleTemplate } from "../Source/VehicleFaceDecoder.js";
import { BuildLobbyWorlds } from "../Source/LobbyWorlds.js";
import { SurfaceMaterials } from "../Source/ProceduralLobbySurfaces.js";
import { SpawnSurfaceSurfels } from "../Source/SurfelAllocator.js";
import { BuildCompressedBvh4, TraceBvh4 } from "../Source/BVH4Builder.js";
import { ParseLm63, ResamplePhotometry } from "../Source/PhotometricProfiles.js";

const shaderFiles=["PrimaryVisibility.wgsl","SurfelRadiosity.wgsl","EmissiveShadowsAndDirect.wgsl","SurfelScreenGather.wgsl","CompositePresentation.wgsl"];
const forbidden=new Set(["asset","cache","probe","map","node","tree","pass","pipeline","data","info","state","value","flags","config","emitter","mesh","facet","studio","and"]);
const codeFiles=[];
async function gather(directory){for(const entry of await readdir(directory,{withFileTypes:true})){const path=join(directory,entry.name);if(entry.isDirectory())await gather(path);else if(/\.(js|mjs|wgsl|css|html|py)$/.test(entry.name))codeFiles.push(path);}}
await gather("Source");await gather("Shaders");await gather("Styles");codeFiles.push("index.html","Serve.py");
for(const path of codeFiles){let source=await readFile(path,"utf8");source=source.replace(/\/\*[\s\S]*?\*\//g,"").replace(/\/\/[^\n]*/g,"").replace(/"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|`(?:\\.|[^`\\])*`/g,"");if(path==="index.html")source=source.replace(/\bvalue\s*=\s*/g,"").replace(/>[^<]*</g,"><");const tokens=[];for(const match of source.matchAll(/(?<!\.)[A-Za-z_$][A-Za-z0-9_$]*/g))tokens.push(match[0]);for(const token of tokens){if(forbidden.has(token.toLowerCase()))assert.fail(`${path}: forbidden identifier ${token}`);if(/^(is|has|can|should)[A-Z]/.test(token))assert.fail(`${path}: boolean prefix ${token}`);}}
for(const file of shaderFiles){const source=await readFile(join("Shaders",file),"utf8");const reflection=new WgslReflect(source);assert(reflection.entry.vertex?.length||reflection.entry.compute?.length,`${file}: no executable entry point`);const seen=new Set();for(const resource of [...reflection.uniforms,...reflection.storage,...reflection.textures,...reflection.samplers]){if(resource.group===undefined||resource.binding===undefined)continue;const key=`${resource.group}:${resource.binding}`;assert(!seen.has(key),`${file}: duplicate resource location ${key}`);seen.add(key);}}
const readFace=async name=>{const bytes=await readFile(`Resources/ControlVehicle/ControlVehicle${name}.face`);return bytes.buffer.slice(bytes.byteOffset,bytes.byteOffset+bytes.byteLength);};
const vehicle=DecodeControlVehicleTemplate(await readFace("Body"),await readFace("Rim"),await readFace("Tyre"));assert.equal(vehicle.TriangleCount,25068,"vehicle triangle count");assert.equal(SurfaceMaterials.length,34,"surface finish count");
const worlds=BuildLobbyWorlds(vehicle);assert.equal(worlds.length,3,"lobby count");const vehicleCounts=[];for(const world of worlds)vehicleCounts.push(world.vehicles);assert.deepEqual(vehicleCounts,[3,4,4],"vehicle count per lobby");const lightCounts=[];for(const world of worlds)lightCounts.push(world.lights.length);assert.deepEqual(lightCounts,[5,5,4],"lighting source count per lobby");assert(worlds.every(world=>world.surfaces.length>10),"procedural surfaces present");assert(!worlds.some(world=>world.surfaces.some(surface=>/sky.?dome/i.test(surface.name))),"outdoor sky must not block sunlight");
for(const world of worlds){const surfels=SpawnSurfaceSurfels(world,{density:1,minimumPerSurface:1,minimumThin:2});assert(surfels.count>=world.surfaces.length,`${world.key}: per-surface surfel minimum`);assert(surfels.origins.every(Number.isFinite),`${world.key}: finite surfel origins`);}
let positionLength=0;for(const surface of worlds[0].surfaces)positionLength+=surface.geometry.positions.length;const positions=new Float32Array(positionLength);let positionOffset=0;for(const surface of worlds[0].surfaces){positions.set(surface.geometry.positions,positionOffset);positionOffset+=surface.geometry.positions.length;}const bvh=BuildCompressedBvh4(positions);assert.equal(bvh.triangleCount,positionLength/9);assert(bvh.entryCount>1,"four-wide BVH entries");assert(TraceBvh4(bvh,[0,0,5],[0,0,-1],100).triangleId>=0,"BVH4 scene ray hit");
for(const name of ["CarParkDownlight","LowBeamHeadlamp","TailLamp"]){const source=await readFile(`Resources/Photometry/${name}.ies`,"utf8");const profile=ResamplePhotometry(ParseLm63(source,name));assert(profile.peakCandela>0,`${name}: candela peak`);assert.equal(profile.pixels.length,4096);}
console.log(`Validation passed: ${worlds.length} lobbies, ${vehicle.TriangleCount.toLocaleString()} vehicle triangles, ${shaderFiles.length} WGSL sources, BVH4 ray test, LM-63 photometry.`);
