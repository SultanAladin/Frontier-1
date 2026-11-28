import { ConstructAimedAreaQuadLight, ConstructAimedPhotometricSpotLight, ConvertYUpPositionToWorldZUp, SrgbHexToLinearRgb } from "./CoordinateConversion.js";
import { InstantiateVehicleSurface } from "./VehicleFaceDecoder.js";
import { SurfaceFinish as F, SurfaceMaterials, BuildCycloramaSurface, BuildTiledFloorSurface, BuildCeilingTrussSurface, BuildTripodSurface, BuildCinemaCameraSurface, BuildLightFixtureSurface, BuildWorkstationSurface, BuildForestBackdropSurface, BuildWindFanSurface, BuildGarageShellSurface, BuildCarLiftSurface, BuildGaragePropsSurface, BuildGarageCablesSurface, BuildRaceTrackSurface, TransformSurface, JoinSurfaces } from "./ProceduralLobbySurfaces.js";

function lobby(key,title) { return {key,title,surfaces:[],lights:[],cameras:[],vehicles:0,materials:SurfaceMaterials,atmosphere:{sky:[.018,.022,.03],sun:[0,0,0],sunDirection:[-.25,.45,.85]},sunOcclusion:[],bounces:3}; }
function put(world,name,geometry,position=[0,0,0],yaw=0,options={}) {const surfaceEntry={name,geometry:TransformSurface(geometry,position,yaw,options.scale??1),position,yaw,thin:!!options.thin,occludesSun:options.occludesSun!==false,emissive:!!options.emissive,category:options.category??"prop"};world.surfaces.push(surfaceEntry);return surfaceEntry;}
function area(world,name,at,target,width,height,radiance,kelvin=5600) {const source=ConstructAimedAreaQuadLight(at,target,width,height,radiance,false,name);world.lights.push({...source,name,kelvin,kind:"area",enabled:true});const g=BuildLightFixtureSurface({width,height,depth:.16});put(world,`${name} diffuser`,g,at,0,{emissive:true,category:"luminaire"});}
function spot(world,name,at,target,power,range,profile=0,kelvin=4100) {world.lights.push({...ConstructAimedPhotometricSpotLight(at,target,power,range,profile,name),kind:"photometric",kelvin,enabled:true});}
function preset(key,label,eye,target,fov=36) {return {key,label,eye:ConvertYUpPositionToWorldZUp(...eye),target:ConvertYUpPositionToWorldZUp(...target),fov};}
function vehiclePaint(hex,metallic=.65,roughness=.16) {return [...SrgbHexToLinearRgb(hex),metallic,roughness];}
function addVehicle(world,template,name,at,yaw,scale,paint,brake=[.7,.05,.04],lift=0) {if(!template)return;const decoded=InstantiateVehicleSurface(template,{VehicleBrakeCaliper:2,VehicleRimAlloy:4,VehicleGlossCanopy:9,VehicleGlass:9,VehicleIntakePolymer:3,VehicleTireRubber:17,VehicleTrimMetal:18,VehicleHeadlightOptic:F.VehicleHeadlightOptic,VehicleTaillightOptic:F.VehicleTaillightOptic},1,2,{UniformScale:scale,YawDegrees:yaw,PitchDegrees:0,RollDegrees:0},[at[0],at[1],at[2]+lift]);const g={positions:decoded.Positions,normals:decoded.Normals,slots:decoded.Slots};const surfaceEntry=put(world,name,g,[0,0,0],0,{category:"vehicle"});surfaceEntry.paint=SrgbHexToLinearRgb(paint);surfaceEntry.brake=brake;world.vehicles++;}

export function BuildLobbyWorlds(vehicleTemplate=null) {
    const worlds=[];
    const one=lobby("lobby-1","Cyclorama · Automotive Portraits");
    put(one,"Cyclorama sweep",BuildCycloramaSurface({width:18,depth:13.5,height:7}),[0,.8,0]);
    put(one,"Glossy tiled floor",BuildTiledFloorSurface({width:48,depth:40,material:F.Concrete}),[0,0,0]);
    put(one,"Ribbed ceiling truss",BuildCeilingTrussSurface({width:34,depth:28,height:7.25}),[0,.5,0]);
    addVehicle(one,vehicleTemplate,"Hero · obsidian coupe",[0,.15,.026],-16,.76,0x111319,[.72,.055,.06]);
    addVehicle(one,vehicleTemplate,"Alpine GT · outer left",[-12.2,-4.8,0],-24,.74,0xeceff3,[.72,.055,.06]);
    addVehicle(one,vehicleTemplate,"Liquid tourer · outer right",[12.2,-4.8,0],-156,.74,0x9ca5b4,[.82,.42,.05]);
    area(one,"Overhead softbox",[0,.25,3.98],[0,.25,.4],7.2,4.0,[.95,1.0,1.08],5600);
    put(one,"Key Fresnel tripod",BuildTripodSurface({height:1.58}),[-3.65,-1.55,0],-.18,{thin:true});
    put(one,"Barn-door Fresnel",BuildLightFixtureSurface({width:.48,height:.48,depth:.42}),[-3.65,-1.55,2.35],-.18,{thin:true});
    spot(one,"Key Fresnel IES",[-3.65,-1.55,2.72],[0,.15,.88],[16.5,15.2,13.4],22,1,5400);
    put(one,"Umbrella fill tripod",BuildTripodSurface({height:1.56}),[3.45,-1.85,0],.21,{thin:true});
    area(one,"Umbrella bounce",[3.45,-1.85,2.2],[0,.15,.88],1.55,1.55,[1.45,1.55,1.72],5800);
    put(one,"Back-left rim tripod",BuildTripodSurface({height:1.72}),[-4.35,2.65,0],-.24,{thin:true});
    area(one,"Rim LED panel",[-4.35,2.65,2.2],[0,.15,.88],1.15,.68,[2.5,2.85,3.45],6300);
    put(one,"Back-right accent tripod",BuildTripodSurface({height:1.66}),[4.55,2.4,0],.24,{thin:true});
    put(one,"Octa softbox strobe",BuildLightFixtureSurface({width:.96,height:.96,depth:.42}),[4.55,2.4,1.95],.24);
    spot(one,"Accent strobe IES",[4.55,2.4,2.35],[0,.15,.88],[12.5,13,14.5],22,1,5600);
    const cameraRigs=[[-3.25,-5.75,1.38],[2.55,-5.65,1.24]];
    for(let i=0;i<cameraRigs.length;i++){const p=cameraRigs[i];put(one,`Cinema tripod ${i+1}`,BuildTripodSurface({height:p[2]}),[p[0],p[1],0],0,{thin:true});put(one,`Cinema camera ${i+1}`,BuildCinemaCameraSurface(),[p[0],p[1],p[2]+.15],0);}
    put(one,"Tether desk",BuildWorkstationSurface({}),[5.9,-6.45,0],-.49);
    put(one,"Grip cases",BuildWorkstationSurface({width:1,depth:.55}),[-6.4,-6.25,0],.42);
    one.cameras=[preset("hero-34","Hero 3/4",[5.75,1.62,6.45],[.05,.78,-.15],35),preset("low-front","Low Front",[3.85,.85,5.15],[-.1,.72,0],32),preset("rear-quarter","Rear Quarter",[-5.45,1.54,5.45],[.15,.76,-.25],36),preset("profile-65","Profile 65mm",[.15,1.22,8.25],[0,.78,-.15],25),preset("overhead-crane","Overhead Crane",[5.4,4.45,8.1],[0,.7,-.4],40),preset("crew-bts","Crew BTS",[-7.4,1.92,8.85],[.25,1.12,-.4],42)];
    one.atmosphere.sky=[.014,.018,.026]; worlds.push(one);

    const two=lobby("lobby-2","Forest Soundstage · Backlit Volume");
    put(two,"Forest floor",BuildTiledFloorSurface({width:46,depth:40,material:F.Concrete}),[0,-1.5,0]);
    put(two,"Ceiling truss",BuildCeilingTrussSurface({width:26,depth:22,height:7.25}),[0,.8,0]);
    put(two,"Forest LED volume",BuildForestBackdropSurface({width:12.4,height:6.2}),[0,5.65,0],0,{emissive:true,category:"backdrop"});
    for(const x of [-7.2,7.2])put(two,"Black V-flag",BuildGaragePropsSurface({kind:"flag"}),[x,4,2.3],x<0?-.4:.4,{thin:true});
    addVehicle(two,vehicleTemplate,"Hero · forest sage",[0,.35,0],-22,.78,0x7ea689);
    addVehicle(two,vehicleTemplate,"Obsidian coupe · left",[-11.8,-4.2,0],-24,.74,0x111319);
    addVehicle(two,vehicleTemplate,"Alpine GT · right",[11.8,-4.2,0],-156,.74,0xeceff3);
    addVehicle(two,vehicleTemplate,"Silver reserve",[10.8,-9.4,0],-138,.74,0x9ca5b4);
    put(two,"Wind fan",BuildWindFanSurface({}),[-5.25,-1.65,0],.28,{thin:true});
    area(two,"Overhead silk",[0,.45,4.18],[0,.35,.9],6.4,3.8,[1.1,1.15,1.22]);
    spot(two,"Woodland Fresnel",[-4.75,-1.45,2.7],[0,.35,1],[19,14,8.5],24,1,3450);
    area(two,"Umbrella fill",[4.85,-2.15,2.35],[0,.35,.8],1.55,1.55,[1.4,1.55,1.65]);
    area(two,"Rim panel",[-4.95,2.55,2.75],[0,.35,1],1.05,.62,[2.2,2.5,3]);
    spot(two,"Accent Fresnel",[5.05,2.25,2.65],[0,.35,.8],[16,11.2,6.8],22,1,3250);
    put(two,"Tether desk",BuildWorkstationSurface({}),[6.4,-6.85,0],-.49);
    put(two,"Grip cases",BuildWorkstationSurface({width:1,depth:.55}),[-6.8,-6.55,0],.38);
    two.cameras=[preset("hero-34","Hero 3/4 · Forest",[5.45,2.05,7.15],[0,1.1,-.35])];two.atmosphere.sky=[.018,.024,.022];worlds.push(two);

    const three=lobby("lobby-3","Racing Pit Garage · Afternoon Sun");
    put(three,"150 m race circuit and grandstands",BuildRaceTrackSurface({length:150}),[0,0,0],0,{category:"circuit"});
    put(three,"Pit garage shell and LEDs",BuildGarageShellSurface({width:22,depth:18,height:7.15}),[0,0,0],0,{category:"garage",thin:true});
    put(three,"Polished pit floor",BuildTiledFloorSurface({width:44,depth:24.8,material:F.Concrete}),[0,-6.2,.015]);
    put(three,"Four post lift",BuildCarLiftSurface({height:2.12,width:3.85}),[-5.55,1.55,0],0,{thin:true});
    addVehicle(three,vehicleTemplate,"Hero GT · signal orange",[.35,-.25,0],-146,.78,0xe86512);
    addVehicle(three,vehicleTemplate,"Alpine GT · right bay",[5.45,-8.65,0],-142,.75,0xeceff3);
    addVehicle(three,vehicleTemplate,"Midnight cobalt · paddock",[-5.35,-8.75,0],-36,.75,0x13294b);
    addVehicle(three,vehicleTemplate,"Coupe · hydraulic lift",[-5.55,1.55,0],90,.74,0x111319,undefined,2.12);
    const props=[["Tool chest",7.1,2.4,"tool-chest"],["Rolling tool cart",5.35,-2.2,"tool-cart"],["Mechanic workbench",9.75,.35,"workbench"],["Air compressor",-9.65,-3.85,"compressor"],["Slick tire rack",4.05,4.55,"tire-rack"],["Telemetry cabinet",-1.55,4.65,"cabinet"]];
    props.forEach(([n,x,y,k])=>put(three,n,BuildGaragePropsSurface({kind:k}),[x,y,0],0));put(three,"Air hoses and cable ramp",BuildGarageCablesSurface({length:11.5}),[0,0,0],0,{thin:true});
    area(three,"Ceiling LED bank",[0,-.35,6.05],[0,-.35,.2],12,8,[1.15,1.2,1.28],5850);
    spot(three,"Afternoon sunlight",[-9.4,13.5,8.2],[.35,-.35,.88],[26,21,14],36,1,4300);
    area(three,"Open pit daylight",[-4.2,6.3,3.1],[.35,-.35,.88],7.5,3.6,[1.3,1.38,1.54]);
    spot(three,"Mechanic bay lamp",[5.2,-1.4,5.4],[.35,-.35,.88],[18,13,8.2],22,1,3400);
    three.sunOcclusion.push({direction:[-.45,.65,.6],irradiance:[3.2,2.8,2.3]});
    three.atmosphere={sky:[.18,.24,.34],sun:[3.2,2.8,2.3],sunDirection:[-.45,.65,.6]};
    three.cameras=[preset("hero-34","Hero 3/4 · Pit Garage",[4.95,1.52,5.85],[.35,.88,.35])];worlds.push(three);
    return worlds;
}
