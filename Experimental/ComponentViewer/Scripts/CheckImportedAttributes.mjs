import assert from 'node:assert/strict';
import { readObj, readGltf } from '../../Shared/GeometryCompile/Engine/ImportFormats.js';
const obj = readObj('v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\nvt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\nusemtl Paint\nf 1/1 2/2 3/3 4/4\n');
assert.deepEqual([...obj.cornerUvs], [0,0,1,0,1,1,0,0,1,1,0,1]);
assert.equal(obj.slotNames[0], 'Paint');
assert.equal(readObj('v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n').cornerUvs, null);
const pos = new Float32Array([0,0,0,1,0,0,0,1,0]);
const uv = new Uint8Array([0,0,255,0,0,255]);
const bytes = new Uint8Array(pos.byteLength + uv.byteLength); bytes.set(new Uint8Array(pos.buffer)); bytes.set(uv,pos.byteLength);
const gltf = {
  buffers: [{ uri: `data:application/octet-stream;base64,${Buffer.from(bytes).toString('base64')}`, byteLength: bytes.length }],
  bufferViews: [{buffer:0,byteOffset:0,byteLength:pos.byteLength}, {buffer:0,byteOffset:pos.byteLength,byteLength:uv.byteLength}],
  accessors: [{bufferView:0,componentType:5126,count:3,type:'VEC3'}, {bufferView:1,componentType:5121,normalized:true,count:3,type:'VEC2'}],
  meshes:[{primitives:[{attributes:{POSITION:0,TEXCOORD_0:1},material:0}]}], nodes:[{mesh:0}], scenes:[{nodes:[0]}], scene:0,
  materials:[{name:'Paint',pbrMetallicRoughness:{baseColorFactor:[0.2,0.3,0.4,1],roughnessFactor:0.45,metallicFactor:0.6}}],
};
const imported = readGltf(gltf, null);
assert.deepEqual([...imported.cornerUvs], [0,0,1,0,0,1]);
assert.equal(imported.palette[0].name, 'Paint');
assert.equal(imported.palette[0].roughness, 0.45);
assert.equal(imported.palette[0].metalness, 0.6);
console.log('Imported OBJ UVs, normalized glTF UVs and glTF material factors: OK');
