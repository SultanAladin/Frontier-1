// Native MoonAtlasPreset order and original 2K sRGB surface assets.
export const MoonBodies = [
 {Name:'Luna',Key:'luna',Size:25,Tilt:6.7,Haze:0,Gamma:1},
 {Name:'Ember',Key:'ember',Size:1.6,Tilt:25,Haze:.08,Gamma:1},
 {Name:'Glacier',Key:'glacier',Size:1.2,Tilt:3,Haze:.05,Gamma:1},
 {Name:'Sulfur',Key:'sulfur',Size:1.1,Tilt:2,Haze:0,Gamma:1},
 {Name:'Shroud',Key:'shroud',Size:2.4,Tilt:27,Haze:1,Gamma:1},
 {Name:'Shard',Key:'shard',Size:.6,Tilt:0,Haze:0,Gamma:.9}
];
export async function ConstructCelestialAtlas(Exchange) {
 const Response=await fetch('Assets/Celestial/MoonAtlas.rgba.gz');
 if(!Response.ok) throw new Error('Native Moon atlas could not be loaded');
 // Also works in the Node/Dawn harness: no browser-only image decoder or placeholder texture.
 const Stream=new Blob([await Response.arrayBuffer()]).stream().pipeThrough(new DecompressionStream('gzip'));
 const Pixels=new Uint8Array(await new ResponseConstructor(Stream).arrayBuffer());
 if(Pixels.length!==2048*1024*4*6) throw new Error('Invalid native Moon atlas byte count');
 const Texture=Exchange.ConstructTexture({label:'Native six-body lunar atlas · 2K sRGB',size:[2048,1024,6],format:'rgba8unorm-srgb',usage:GPUTextureUsage.TEXTURE_BINDING|GPUTextureUsage.COPY_DST});
 Exchange.Device.queue.writeTexture({texture:Texture},Pixels,{bytesPerRow:2048*4,rowsPerImage:1024},[2048,1024,6]);
 return {Texture,View:Texture.createView({dimension:'2d-array'}),Ready:true};
}
const ResponseConstructor=globalThis.Response;
