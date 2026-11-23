/*==========================================================================================================================================
                                                         REFERENCERENDERER.JS
==========================================================================================================================================*/
// 🧩 Reuse the pinned reference HTML's WebGL2 EON/GGX renderer, actuator geometry and depth-tested drafting contours.
// 📝 Source: 658fac7378f2b38309966fab72c007e98075e538 / Documentation/Prototypes/ProjectBrowser.html
// 📝 Original function/shader identifiers are retained for source traceability; browser adaptation follows below.
'use strict';

(() => {
const SceneSurfaceDefaults = [
    { Title: "Actuator housing", Albedo: "#87b2a3", Specular: .5, Metallic: .65, Roughness: .3, Description: "Machined housing and radial cooling fins." },
    { Title: "Copper windings", Albedo: "#d99565", Specular: .5, Metallic: 1, Roughness: .24, Description: "Exposed copper winding rings and terminals." },
    { Title: "Shaft & fasteners", Albedo: "#c5cdd2", Specular: .5, Metallic: 1, Roughness: .18, Description: "Steel shaft, flange bolts and tie rods." },
    { Title: "Graphite seals", Albedo: "#282c2b", Specular: .5, Metallic: 0, Roughness: .7, Description: "Insulating gaskets and recessed bearing seals." },
    { Title: "Mounting plate", Albedo: "#747f82", Specular: .5, Metallic: .85, Roughness: .4, Description: "Mounting plate and structural support brackets." }
];

const SceneVertexSource = `#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 Position;
layout(location=1) in vec3 Normal;
layout(location=2) in float Surface;
layout(location=3) in vec3 Separation;
uniform mat4 ViewProjection;
uniform float Explode;
out vec3 WorldPosition;
out vec3 WorldNormal;
flat out int SurfaceIndex;
void main()
{
    WorldPosition = Position + Separation * Explode;
    WorldNormal = Normal;
    SurfaceIndex = int(Surface + .5);
    gl_Position = ViewProjection * vec4(WorldPosition, 1.0);
}`;

const SceneFragmentSource = `#version 300 es
precision highp float;
precision highp int;
in vec3 WorldPosition;
in vec3 WorldNormal;
flat in int SurfaceIndex;
uniform vec4 AlbedoMetallic[7];
uniform vec2 SpecularRoughness[7];
uniform vec3 Eye;
uniform sampler2D EonLookup;
uniform int Channel;
uniform bool GridEnabled;
out vec4 RadianceOutput;
const float Pi = 3.14159265359;
float Fifth(float X) { float X2=X*X; return X2*X2*X; }
vec3 Fresnel(vec3 F0, float Cosine) { return F0+(1.0-F0)*Fifth(1.0-Cosine); }
vec2 EnvironmentIntegral(float NoV, float Roughness)
{
    vec4 Fit = Roughness*vec4(-1.0,-.0275,-.572,.022)+vec4(1.0,.0425,1.04,-.04);
    float A = min(Fit.x*Fit.x, exp2(-9.28*NoV))*Fit.x+Fit.y;
    return vec2(-1.04,1.04)*A+Fit.zw;
}
float EonMissing(float Cosine, float Roughness)
{
    return texture(EonLookup, vec2((Cosine*63.0+.5)/64.0,(Roughness*31.0+.5)/32.0)).r*.25;
}
vec3 DiffuseEon(vec3 Albedo, float Roughness, float NoV, float NoL, float VoL)
{
    float Correlation = VoL-NoV*NoL;
    float Angular = Correlation > 0.0 ? Correlation/max(NoV,NoL) : Correlation;
    float A = 1.0/(1.0+(.5-2.0/(3.0*Pi))*Roughness);
    float Average = A*(1.0+(2.0/3.0-28.0/(15.0*Pi))*Roughness);
    vec3 Multiple = Albedo*Albedo*Average/(1.0-Albedo*max(0.0,1.0-Average));
    return (Albedo*A*(1.0+Roughness*Angular) + Multiple*EonMissing(NoV,Roughness)*EonMissing(NoL,Roughness)/max(1e-5,1.0-Average))/Pi;
}
vec3 EvaluateLight(vec3 Normal, vec3 View, vec3 Light, vec3 Energy, vec3 Albedo, vec3 F0, float Metallic, float Roughness, vec3 Compensation)
{
    float NoL = max(dot(Normal,Light),0.0);
    if (NoL <= 0.0) return vec3(0.0);
    float NoV = max(dot(Normal,View),.0001);
    vec3 HalfDirection = normalize(View+Light);
    float NoH = max(dot(Normal,HalfDirection),0.0);
    float VoH = max(dot(View,HalfDirection),0.0);
    float Alpha = Roughness*Roughness, Alpha2 = Alpha*Alpha;
    float Denominator = NoH*NoH*(Alpha2-1.0)+1.0;
    float Distribution = Alpha2/(Pi*Denominator*Denominator);
    float Visibility = .5/max(NoL*sqrt(NoV*NoV*(1.0-Alpha2)+Alpha2)+NoV*sqrt(NoL*NoL*(1.0-Alpha2)+Alpha2),.00001);
    vec3 F = Fresnel(F0,VoH);
    vec3 Diffuse = DiffuseEon(Albedo,Roughness,NoV,NoL,dot(View,Light))*(1.0-Metallic)*(1.0-F);
    return (Diffuse + Distribution*Visibility*F*Compensation)*Energy*NoL;
}
vec3 ToSrgb(vec3 Linear)
{
    return mix(12.92*Linear,1.055*pow(max(Linear,vec3(0.0)),vec3(1.0/2.4))-.055,step(vec3(.0031308),Linear));
}
void main()
{
    if (Channel == 5)
    {
        // Opaque black depth coverage with optional faint grid; no material lighting.
        if (SurfaceIndex == 6 || (SurfaceIndex == 5 && !GridEnabled)) discard;
        float GridInk = SurfaceIndex == 5 ? .055*(1.0-smoothstep(4.0,9.0,length(WorldPosition.xz))) : 0.0;
        RadianceOutput = vec4(vec3(GridInk),1.0);
        return;
    }
    if (SurfaceIndex == 5 && !GridEnabled) discard;
    if (SurfaceIndex >= 5)
    {
        float Fade = 1.0-smoothstep(4.0,9.0,length(WorldPosition.xz));
        float Grey = SurfaceIndex == 5 ? .085*Fade : .008;
        RadianceOutput = vec4(vec3(Grey),1.0);
        return;
    }
    vec3 Albedo = AlbedoMetallic[SurfaceIndex].rgb;
    float Metallic = AlbedoMetallic[SurfaceIndex].a;
    float Specular = SpecularRoughness[SurfaceIndex].x;
    float Roughness = max(.04,SpecularRoughness[SurfaceIndex].y);
    vec3 F0 = mix(vec3(.16*Specular*Specular),Albedo,Metallic);
    if (Channel != 0)
    {
        vec3 Display = Channel==1 ? ToSrgb(Albedo) : Channel==2 ? ToSrgb(F0) : vec3(Channel==3 ? Metallic : Roughness);
        RadianceOutput = vec4(Display,1.0); return;
    }
    vec3 Normal = normalize(WorldNormal), View = normalize(Eye-WorldPosition);
    if (dot(Normal,View) < 0.0) Normal = -Normal;
    float NoV = max(dot(Normal,View),.0001);
    vec2 Dfg = EnvironmentIntegral(NoV,Roughness);
    vec3 Compensation = 1.0+F0*(1.0/max(Dfg.x+Dfg.y,.001)-1.0);
    vec3 Radiance = EvaluateLight(Normal,View,normalize(vec3(2,5,3)),vec3(3.6,3.9,4.2),Albedo,F0,Metallic,Roughness,Compensation);
    Radiance += EvaluateLight(Normal,View,normalize(vec3(-3,2,-4)),vec3(2.5,1.9,1.3),Albedo,F0,Metallic,Roughness,Compensation);
    // Analytic studio fill, not image-based lighting or a physical environment convolution.
    vec3 Reflection = reflect(-View,Normal);
    float Softbox = pow(max(dot(Reflection,normalize(vec3(-.5,1.0,1.0))),0.0),mix(100.0,2.0,Roughness));
    vec3 Studio = mix(vec3(.06,.07,.08),vec3(.65,.72,.78),smoothstep(-.2,.9,Reflection.y)) + vec3(1.8)*Softbox;
    Radiance += Studio*(F0*Dfg.x+Dfg.y)*Compensation;
    Radiance += Albedo*(1.0-Metallic)*(1.0-Fresnel(F0,NoV))*(.08+.2*max(Normal.y,0.0));
    // A single display transform. This standalone canvas is not the engine HDR radiance target.
    vec3 Mapped = clamp((Radiance*(2.51*Radiance+.03))/(Radiance*(2.43*Radiance+.59)+.14),0.0,1.0);
    RadianceOutput = vec4(ToSrgb(Mapped),1.0);
}`;

const DraftingVertexSource = `#version 300 es
precision highp float;
precision highp int;
layout(location=0) in vec3 Position;
layout(location=1) in vec3 Separation;
layout(location=2) in vec3 NormalA;
layout(location=3) in vec3 NormalB;
layout(location=4) in float Crease;
uniform mat4 ViewProjection;
uniform vec3 ViewDirection;
uniform float Explode;
void main()
{
    float FacingA=dot(NormalA,ViewDirection), FacingB=dot(NormalB,ViewDirection);
    bool Silhouette=FacingA*FacingB<=0.0 && abs(FacingA-FacingB)>.00001;
    // Orthographic facing is constant along an edge: both endpoints make the same decision.
    gl_Position=Crease>.5 || Silhouette ? ViewProjection*vec4(Position+Separation*Explode,1.0) : vec4(2.0,2.0,2.0,1.0);
}`;

const DraftingFragmentSource = `#version 300 es
precision highp float;
precision highp int;
uniform float Ink;
out vec4 ContourColour;
void main() { ContourColour=vec4(vec3(Ink),1.0); }
`;

function SceneUnitVector(Vector)
{
    const Length = Math.hypot(...Vector) || 1;
    return Vector.map(Component => Component / Length);
}

function SceneCrossProduct(Left, Right)
{
    return [Left[1]*Right[2]-Left[2]*Right[1], Left[2]*Right[0]-Left[0]*Right[2], Left[0]*Right[1]-Left[1]*Right[0]];
}

function SceneDotProduct(Left, Right)
{
    return Left.reduce((Sum, Component, Index) => Sum + Component * Right[Index], 0);
}

// Positions and normals are baked once into a single interleaved GPU allocation.
// The final three floats are an assembly separation vector, evaluated in the vertex shader.

function GenerateActuatorTriangles()
{
    const Vertices = [];
    function Triangle(Corners, Normals, Surface, Separation)
    {
        for (let Index = 0; Index < 3; Index++) Vertices.push(...Corners[Index], ...Normals[Index], Surface, ...Separation);
    }
    function Quad(Corners, Normals, Surface, Separation)
    {
        Triangle([Corners[0], Corners[1], Corners[2]], [Normals[0], Normals[1], Normals[2]], Surface, Separation);
        Triangle([Corners[0], Corners[2], Corners[3]], [Normals[0], Normals[2], Normals[3]], Surface, Separation);
    }
    function Box(Centre, Span, Surface, Separation = [0, 0, 0])
    {
        for (let Axis = 0; Axis < 3; Axis++) for (const Sign of [-1, 1])
        {
            const Tangent = (Axis + 1) % 3, Bitangent = (Axis + 2) % 3;
            const Normal = [0, 0, 0]; Normal[Axis] = Sign;
            const Corners = [[-1,-1],[1,-1],[1,1],[-1,1]].map(Pair => {
                const Point = [...Centre];
                Point[Axis] += Sign * Span[Axis] / 2;
                Point[Tangent] += Pair[0] * Span[Tangent] / 2;
                Point[Bitangent] += Pair[1] * Span[Bitangent] / 2;
                return Point;
            });
            Quad(Corners, [Normal, Normal, Normal, Normal], Surface, Separation);
        }
    }
    function Tube(Centre, Radius, Bore, Length, Surface, Separation = [0, 0, 0], Axis = 0, Segments = 48)
    {
        const Tangent = (Axis + 1) % 3, Bitangent = (Axis + 2) % 3;
        function Point(Angle, Radial, Depth)
        {
            const Position = [...Centre];
            Position[Axis] += Depth;
            Position[Tangent] += Math.cos(Angle) * Radial;
            Position[Bitangent] += Math.sin(Angle) * Radial;
            return Position;
        }
        function RadialNormal(Angle, Sign)
        {
            const Normal = [0,0,0]; Normal[Tangent] = Math.cos(Angle)*Sign; Normal[Bitangent] = Math.sin(Angle)*Sign; return Normal;
        }
        for (let Segment = 0; Segment < Segments; Segment++)
        {
            const A = Segment * Math.PI * 2 / Segments, B = (Segment + 1) * Math.PI * 2 / Segments;
            for (const [Radial, Sign] of [[Radius, 1], [Bore, -1]])
            {
                if (!Radial) continue;
                Quad([Point(A,Radial,-Length/2),Point(B,Radial,-Length/2),Point(B,Radial,Length/2),Point(A,Radial,Length/2)],
                    [RadialNormal(A,Sign),RadialNormal(B,Sign),RadialNormal(B,Sign),RadialNormal(A,Sign)],Surface,Separation);
            }
            for (const Sign of [-1,1])
            {
                const Normal = [0,0,0]; Normal[Axis] = Sign;
                Quad([Point(A,Bore,Sign*Length/2),Point(B,Bore,Sign*Length/2),Point(B,Radius,Sign*Length/2),Point(A,Radius,Sign*Length/2)],
                    [Normal,Normal,Normal,Normal],Surface,Separation);
            }
        }
    }
    Box([0,-.06,0], [16,.08,16], 6);
    for (let Line = -8; Line <= 8; Line++)
    {
        Box([Line,0,0],[.012,.004,16],5);
        Box([0,0,Line],[16,.004,.012],5);
    }
    Box([0,.2,0], [6.2,.32,3.2],4,[0,-.15,0]);
    for (const X of [-1.65,1.65])
    {
        Box([X,.73,0],[.58,1.05,1.8],4,[0,-.2,0]);
        Box([X,.46,0],[1,.18,2.7],4,[0,-.2,0]);
        for (const Z of [-1.15,1.15]) Tube([X,.59,Z],.13,.045,.12,2,[0,-.2,0],1,6);
    }
    const Height = 1.8;
    Tube([0,Height,0],.89,.3,2.7,0);
    Tube([-.25,Height,0],.93,.89,1.8,3);
    for (let Index = 0; Index < 12; Index++) Tube([-.98+Index*.14,Height,0],1.03,.88,.055,0);
    for (const X of [-1.28,1.28])
    {
        Tube([X,Height,0],.98,.3,.19,1,[Math.sign(X)*.45,0,0]);
        Tube([X*1.17,Height,0],1.08,.3,.17,0,[Math.sign(X)*.8,0,0]);
        Tube([X*1.29,Height,0],.69,.25,.08,3,[Math.sign(X)*1.0,0,0]);
        Tube([X*1.34,Height,0],.48,.25,.12,2,[Math.sign(X)*1.1,0,0]);
        for (let Bolt = 0; Bolt < 8; Bolt++)
        {
            const Angle = Bolt*Math.PI/4;
            Tube([X*1.27,Height+.86*Math.cos(Angle),.86*Math.sin(Angle)],.105,.035,.13,2,[Math.sign(X)*1.15,0,0],0,6);
        }
    }
    Tube([2,Height,0],.25,0,1.5,2,[1.7,0,0]);
    Tube([2.75,Height,0],.34,.12,.18,2,[1.9,0,0]);
    Box([2.25,Height+.26,0],[.6,.06,.11],3,[1.7,0,0]);
    Tube([-1.95,Height,0],.3,0,.55,2,[-1.6,0,0]);
    for (const Angle of [Math.PI/4,3*Math.PI/4,5*Math.PI/4,7*Math.PI/4])
        Tube([0,Height+.79*Math.cos(Angle),.79*Math.sin(Angle)],.045,0,3.2,2);
    Box([.5,2.92,0],[.9,.38,.8],3,[0,.7,0]);
    Box([.5,3.14,0],[1.02,.09,.92],0,[0,1,0]);
    for (const X of [.25,.75]) Tube([X,3.29,0],.12,.045,.24,1,[0,1.2,0],1,16);
    for (const X of [-2.65,2.65]) for (const Z of [-1.25,1.25])
        Tube([X,.41,Z],.13,.05,.1,2,[0,-.15,0],1,6);
    return new Float32Array(Vertices);
}

function GenerateEonLookup()
{
    const Pixels = new Uint8Array(64*32*4);
    for (let Row = 0; Row < 32; Row++) for (let Column = 0; Column < 64; Column++)
    {
        const Roughness = Row / 31, Cosine = Math.max(.0001, Column / 63);
        const Sine = Math.sqrt(Math.max(0, 1-Cosine*Cosine));
        const A = 1/(1+(.5-2/(3*Math.PI))*Roughness);
        const G = Sine*(Math.acos(Cosine)-Sine*Cosine) + 2*((Sine/Cosine)*(1-Sine*Sine*Sine)-Sine)/3;
        const Missing = Math.max(0,1-(A+Roughness*A*G/Math.PI));
        const Offset = (Row*64+Column)*4;
        Pixels[Offset] = Math.round(Math.min(1,Missing*4)*255);
        Pixels[Offset+3] = 255;
    }
    return Pixels;
}

function GenerateDraftingContours(Vertices)
{
    const Adjacency = new Map();
    const Minimum = [Infinity,Infinity,Infinity], Maximum = [-Infinity,-Infinity,-Infinity];
    function CoordinateKey(Position)
    {
        return Position.map(Coordinate => Math.round(Coordinate*100000)).join(",");
    }
    for (let Offset = 0; Offset < Vertices.length; Offset += 30)
    {
        if (Vertices[Offset+6] >= 5) continue;
        const Corners = [0,10,20].map(Shift => Array.from(Vertices.slice(Offset+Shift,Offset+Shift+3)));
        for (const Position of Corners) for (let Axis=0; Axis<3; Axis++)
        {
            Minimum[Axis] = Math.min(Minimum[Axis],Position[Axis]);
            Maximum[Axis] = Math.max(Maximum[Axis],Position[Axis]);
        }
        const EdgeA = Corners[1].map((Coordinate,Axis) => Coordinate-Corners[0][Axis]);
        const EdgeB = Corners[2].map((Coordinate,Axis) => Coordinate-Corners[0][Axis]);
        const Cross = SceneCrossProduct(EdgeA,EdgeB);
        if (Math.hypot(...Cross) < 1e-9) continue;
        let Normal = SceneUnitVector(Cross);
        const AuthoredNormal = [0,1,2].map(Axis => Vertices[Offset+3+Axis]+Vertices[Offset+13+Axis]+Vertices[Offset+23+Axis]);
        if (SceneDotProduct(Normal,AuthoredNormal)<0) Normal=Normal.map(Coordinate => -Coordinate);
        const Separation = Array.from(Vertices.slice(Offset+7,Offset+10));
        const Group = Vertices[Offset+6]+":"+CoordinateKey(Separation)+":";
        for (let Edge=0; Edge<3; Edge++)
        {
            const Start=Corners[Edge], End=Corners[(Edge+1)%3];
            const Keys=[CoordinateKey(Start),CoordinateKey(End)].sort();
            const Token=Group+Keys.join("/");
            const Existing=Adjacency.get(Token);
            if (Existing) Existing.Normals.push(Normal);
            else Adjacency.set(Token,{Start,End,Separation,Normals:[Normal]});
        }
    }
    const Contours=[];
    for (const Edge of Adjacency.values())
    {
        const NormalA=Edge.Normals[0], NormalB=Edge.Normals[1] || NormalA;
        const Cosine=SceneDotProduct(NormalA,NormalB);
        if (Edge.Normals.length===2 && Cosine>.9999) continue;
        const Crease=Edge.Normals.length!==2 || Cosine<Math.cos(25*Math.PI/180) ? 1 : 0;
        for (const Position of [Edge.Start,Edge.End])
            Contours.push(...Position,...Edge.Separation,...NormalA,...NormalB,Crease);
    }
    return new Float32Array(Contours);
}

function CompileSceneProgram(Gl, VertexSource, FragmentSource)
{
    const Shaders=[];
    const Program=Gl.createProgram();
    try
    {
        for (const [Source,Type] of [[VertexSource,Gl.VERTEX_SHADER],[FragmentSource,Gl.FRAGMENT_SHADER]])
        {
            const Shader=Gl.createShader(Type); Shaders.push(Shader);
            Gl.shaderSource(Shader,Source); Gl.compileShader(Shader);
            if (!Gl.getShaderParameter(Shader,Gl.COMPILE_STATUS)) throw new Error(Gl.getShaderInfoLog(Shader));
            Gl.attachShader(Program,Shader);
        }
        Gl.linkProgram(Program);
        if (!Gl.getProgramParameter(Program,Gl.LINK_STATUS)) throw new Error(Gl.getProgramInfoLog(Program));
        return Program;
    }
    catch (Fault) { Gl.deleteProgram(Program); throw Fault; }
    finally { for (const Shader of Shaders) Gl.deleteShader(Shader); }
}

function AllocateDraftingContours(Device)
{
    const Gl=Device.Gl;
    const Program=CompileSceneProgram(Gl,DraftingVertexSource,DraftingFragmentSource);
    const Contours=GenerateDraftingContours(Device.Vertices);
    const Layout=Gl.createVertexArray(); Gl.bindVertexArray(Layout);
    const Allocation=Gl.createBuffer(); Gl.bindBuffer(Gl.ARRAY_BUFFER,Allocation);
    Gl.bufferData(Gl.ARRAY_BUFFER,Contours,Gl.STATIC_DRAW);
    for (const [Location,Width,Offset] of [[0,3,0],[1,3,12],[2,3,24],[3,3,36],[4,1,48]])
    {
        Gl.enableVertexAttribArray(Location); Gl.vertexAttribPointer(Location,Width,Gl.FLOAT,false,52,Offset);
    }
    const Uniforms={};
    for (const Name of ["ViewProjection","ViewDirection","Explode","Ink"]) Uniforms[Name]=Gl.getUniformLocation(Program,Name);
    Gl.bindVertexArray(null);
    return { Program,Layout,Allocation,Uniforms,VertexCount:Contours.length/13 };
}

//------------------------------------------------------------------------------------------------------------------------
//                                                     SHARED PREVIEW ADAPTATION
//------------------------------------------------------------------------------------------------------------------------

const OriginalTriangles = GenerateActuatorTriangles();
function GeneratePreviewTriangles(Category, Revision)
{
    const Sequence = [];
    for (let Offset = 0; Offset < OriginalTriangles.length; Offset += 30)
    {
        const Surface = OriginalTriangles[Offset + 6];
        if (Revision >= 1 && Surface === 1) continue;
        if (Revision >= 2 && Surface === 2) continue;
        if (Category === 'geometry' && Surface < 5 && Surface !== 0) continue;
        if (Category === 'geometry' && Surface < 5 && Revision > 0)
        {
            const Scale = 1 - Revision * .12;
            for (const Shift of [0,10,20])
                Sequence.push(OriginalTriangles[Offset+Shift]*Scale,
                    1.8+(OriginalTriangles[Offset+Shift+1]-1.8)*Scale,
                    OriginalTriangles[Offset+Shift+2]*Scale,
                    ...OriginalTriangles.subarray(Offset+Shift+3,Offset+Shift+10));
        }
        else Sequence.push(...OriginalTriangles.subarray(Offset, Offset + 30));
    }
    if (Category === 'project')
    {
        // 📝 A small studio around the inherited actuator specimen, using the same ten-float vertex layout and shaders.
        function Box(Centre, Span, Surface)
        {
            for (let Axis = 0; Axis < 3; ++Axis) for (const Sign of [-1, 1])
            {
                const Tangent = (Axis + 1) % 3, Bitangent = (Axis + 2) % 3;
                const Normal = [0, 0, 0]; Normal[Axis] = Sign;
                const Corners = [[-1,-1],[1,-1],[1,1],[-1,1]].map(Pair => {
                    const Point = [...Centre]; Point[Axis] += Sign * Span[Axis] / 2;
                    Point[Tangent] += Pair[0] * Span[Tangent] / 2;
                    Point[Bitangent] += Pair[1] * Span[Bitangent] / 2;
                    return Point;
                });
                for (const Index of [0, 1, 2, 0, 2, 3]) Sequence.push(...Corners[Index], ...Normal, Surface, 0, 0, 0);
            }
        }
        Box([0,-.2,0], [11,.2,9], 4);
        for (const X of [-4.8,4.8])
        {
            Box([X,1.7,-3.8], [.28,3.7,.28], 2);
            Box([X,1.7,3.8], [.28,3.7,.28], 2);
        }
        Box([0,3.5,-3.8], [9.8,.24,.25], 0);
        Box([0,3.5,3.8], [9.8,.24,.25], 0);
        for (const X of [-4.8,4.8]) Box([X,3.5,0], [.25,.24,7.8], 0);
        if (Revision < 2)
        {
            Box([-3.8,.5,-2.2], [1.5,1,1.5], 0);
            Box([3.7,.35,2.2], [1.6,.7,1.2], 1);
        }
    }
    return new Float32Array(Sequence);
}

class ReferencePreview
{
    constructor(Canvas)
    {
        this.Canvas = Canvas;
        this.Camera = {Azimuth:.85, Elevation:.43, Distance:11, Target:[0,1.1,0]};
        this.Configuration = {};
        this.Signature = '';
        this.Gl = Canvas.getContext('webgl2', {alpha:false, antialias:true, depth:true, preserveDrawingBuffer:true, powerPreference:'low-power'});
        if (!this.Gl) throw new Error('WebGL 2 is unavailable. No rendered thumbnail can be produced.');
        const Gl = this.Gl;
        this.Program = CompileSceneProgram(Gl, SceneVertexSource, SceneFragmentSource);
        this.TriangleLayout = Gl.createVertexArray();
        Gl.bindVertexArray(this.TriangleLayout);
        this.Allocation = Gl.createBuffer(); Gl.bindBuffer(Gl.ARRAY_BUFFER, this.Allocation);
        for (const [Location, Width, Offset] of [[0,3,0],[1,3,12],[2,1,24],[3,3,28]])
        {
            Gl.enableVertexAttribArray(Location); Gl.vertexAttribPointer(Location, Width, Gl.FLOAT, false, 40, Offset);
        }
        this.Lookup = Gl.createTexture(); Gl.bindTexture(Gl.TEXTURE_2D,this.Lookup);
        Gl.texImage2D(Gl.TEXTURE_2D,0,Gl.RGBA,64,32,0,Gl.RGBA,Gl.UNSIGNED_BYTE,GenerateEonLookup());
        Gl.texParameteri(Gl.TEXTURE_2D,Gl.TEXTURE_MIN_FILTER,Gl.LINEAR); Gl.texParameteri(Gl.TEXTURE_2D,Gl.TEXTURE_MAG_FILTER,Gl.LINEAR);
        Gl.texParameteri(Gl.TEXTURE_2D,Gl.TEXTURE_WRAP_S,Gl.CLAMP_TO_EDGE); Gl.texParameteri(Gl.TEXTURE_2D,Gl.TEXTURE_WRAP_T,Gl.CLAMP_TO_EDGE);
        this.Uniforms = {};
        for (const Name of ['ViewProjection','Explode','Eye','EonLookup','Channel','GridEnabled','AlbedoMetallic','SpecularRoughness'])
            this.Uniforms[Name] = Gl.getUniformLocation(this.Program,['AlbedoMetallic','SpecularRoughness'].includes(Name) ? Name+'[0]' : Name);
        Gl.useProgram(this.Program); Gl.uniform1i(this.Uniforms.EonLookup,0);
        Canvas.addEventListener('webglcontextlost', Event => { Event.preventDefault();this.Lost=true;this.OnFailure?.('Graphics context lost. Reload the preview to recreate its renderer.'); });
    }

    Configure(Configuration)
    {
        this.Configuration = {...Configuration};
        const Signature = Configuration.Category+':'+Configuration.Revision;
        if (Signature !== this.Signature)
        {
            this.Signature = Signature;
            const Gl = this.Gl;
            this.Vertices = GeneratePreviewTriangles(Configuration.Category, Configuration.Revision || 0);
            Gl.bindVertexArray(this.TriangleLayout); Gl.bindBuffer(Gl.ARRAY_BUFFER,this.Allocation);
            Gl.bufferData(Gl.ARRAY_BUFFER,this.Vertices,Gl.STATIC_DRAW);
            if (this.Contours)
            {
                Gl.deleteProgram(this.Contours.Program); Gl.deleteVertexArray(this.Contours.Layout); Gl.deleteBuffer(this.Contours.Allocation);
            }
            this.Contours = null;
        }
        const View = Configuration.View || 0;
        this.Camera = {Azimuth:[.85,-.65,1.6][View%3],Elevation:Configuration.Category==='project' ? .55 : .43,
                       Distance:Configuration.Category==='project' ? 17 : 10.5,Target:[0,1.3,0]};
        if (Configuration.Sketch) this.Camera = {Azimuth:View%2 ? Math.PI/2 : 0,Elevation:0,Distance:10.5,Target:[0,1.65,0]};
    }

    QueryStatistics()
    {
        const Minimum = [Infinity,Infinity,Infinity], Maximum = [-Infinity,-Infinity,-Infinity];
        let Triangles = 0;
        for (let Offset = 0; Offset < this.Vertices.length; Offset += 30)
        {
            if (this.Vertices[Offset+6] >= 5) continue;
            Triangles++;
            for (const Shift of [0,10,20]) for (let Axis=0; Axis<3; ++Axis)
            {
                Minimum[Axis] = Math.min(Minimum[Axis],this.Vertices[Offset+Shift+Axis]);
                Maximum[Axis] = Math.max(Maximum[Axis],this.Vertices[Offset+Shift+Axis]);
            }
        }
        return {Triangles, Extents:Maximum.map((Coordinate,Axis) => +(Coordinate-Minimum[Axis]).toFixed(3)),
                SurfaceChannels:['Albedo','Specular','Metallic','Roughness'],Renderer:'Reference WebGL 2 · EON diffuse / compensated GGX'};
    }

    ConstructProjection(Aspect, Drafting)
    {
        const Camera = this.Camera;
        const Eye = Camera.Target.map((Coordinate,Axis) => Coordinate + Camera.Distance * [Math.cos(Camera.Elevation)*Math.sin(Camera.Azimuth),Math.sin(Camera.Elevation),Math.cos(Camera.Elevation)*Math.cos(Camera.Azimuth)][Axis]);
        const Z = SceneUnitVector(Eye.map((Coordinate,Axis) => Coordinate-Camera.Target[Axis]));
        const X = SceneUnitVector(SceneCrossProduct([0,1,0],Z)), Y = SceneCrossProduct(Z,X);
        const View = [X[0],Y[0],Z[0],0,X[1],Y[1],Z[1],0,X[2],Y[2],Z[2],0,-SceneDotProduct(X,Eye),-SceneDotProduct(Y,Eye),-SceneDotProduct(Z,Eye),1];
        const Focal = 1/Math.tan(Math.PI/8), Near=.1, Far=100, Span=Camera.Distance/Focal/Math.min(1,Aspect);
        const Projection = Drafting ? [1/(Span*Aspect),0,0,0,0,1/Span,0,0,0,0,-2/(Far-Near),0,0,-.15,-(Far+Near)/(Far-Near),1]
            : [Focal/Aspect,0,0,0,0,Focal,0,0,0,0,(Far+Near)/(Near-Far),-1,0,0,2*Far*Near/(Near-Far),0];
        const Product = new Float32Array(16);
        for (let Column=0;Column<4;++Column) for (let Row=0;Row<4;++Row) for (let Axis=0;Axis<4;++Axis)
            Product[Column*4+Row] += Projection[Axis*4+Row]*View[Column*4+Axis];
        return {Eye,Product};
    }

    Rasterize(Width, Height)
    {
        if (this.Lost) throw new Error('Graphics context lost. Reload the preview.');
        const Gl = this.Gl, Uniforms=this.Uniforms;
        Width=Math.max(1,Math.min(1600,Math.round(Width)));Height=Math.max(1,Math.min(1200,Math.round(Height)));
        if (this.Canvas.width!==Width || this.Canvas.height!==Height) {this.Canvas.width=Width;this.Canvas.height=Height;}
        const Channel = this.Configuration.Channel ?? (this.Configuration.Category==='cad' ? 5 : 0);
        const Drafting = Channel===5;
        Gl.enable(Gl.DEPTH_TEST);Gl.depthMask(true);Gl.depthFunc(Gl.LESS);Gl.clearColor(0,0,0,1);
        Gl.viewport(0,0,Width,Height);Gl.clear(Gl.COLOR_BUFFER_BIT|Gl.DEPTH_BUFFER_BIT);
        Gl.useProgram(this.Program);Gl.bindVertexArray(this.TriangleLayout);
        const {Eye,Product}=this.ConstructProjection(Width/Height,Drafting);
        Gl.uniformMatrix4fv(Uniforms.ViewProjection,false,Product);Gl.uniform3fv(Uniforms.Eye,Eye);
        Gl.uniform1f(Uniforms.Explode,this.Configuration.Explode || 0);Gl.uniform1i(Uniforms.Channel,Channel);Gl.uniform1i(Uniforms.GridEnabled,!Drafting);
        const Colours=[],Scalars=[];
        for (const Surface of SceneSurfaceDefaults)
        {
            for (const Offset of [1,3,5])
            {
                const Srgb=parseInt(Surface.Albedo.slice(Offset,Offset+2),16)/255;
                Colours.push(Srgb<=.04045 ? Srgb/12.92 : ((Srgb+.055)/1.055)**2.4);
            }
            Colours.push(Surface.Metallic);Scalars.push(Surface.Specular,Surface.Roughness);
        }
        Colours.push(0,0,0,0,0,0,0,0);Scalars.push(0,1,0,1);
        Gl.uniform4fv(Uniforms.AlbedoMetallic,Colours);Gl.uniform2fv(Uniforms.SpecularRoughness,Scalars);
        if (Drafting) {Gl.enable(Gl.POLYGON_OFFSET_FILL);Gl.polygonOffset(1,1);}
        Gl.drawArrays(Gl.TRIANGLES,0,this.Vertices.length/10);Gl.disable(Gl.POLYGON_OFFSET_FILL);
        if (Drafting)
        {
            if (!this.Contours) this.Contours=AllocateDraftingContours(this);
            const Contours=this.Contours;
            Gl.useProgram(Contours.Program);Gl.bindVertexArray(Contours.Layout);
            Gl.uniformMatrix4fv(Contours.Uniforms.ViewProjection,false,Product);
            Gl.uniform3fv(Contours.Uniforms.ViewDirection,SceneUnitVector(Eye.map((Coordinate,Axis)=>Coordinate-this.Camera.Target[Axis])));
            Gl.uniform1f(Contours.Uniforms.Explode,this.Configuration.Explode || 0);Gl.depthMask(false);
            if (this.Configuration.HiddenEdges)
            {
                Gl.depthFunc(Gl.GREATER);Gl.uniform1f(Contours.Uniforms.Ink,.2);Gl.drawArrays(Gl.LINES,0,Contours.VertexCount);
            }
            Gl.depthFunc(Gl.LEQUAL);Gl.uniform1f(Contours.Uniforms.Ink,.85);Gl.drawArrays(Gl.LINES,0,Contours.VertexCount);
            Gl.depthMask(true);Gl.depthFunc(Gl.LESS);
        }
        const Error = Gl.getError();
        if (Error!==Gl.NO_ERROR) throw new Error('WebGL preview error '+Error);
        return this.Canvas;
    }

    Capture(Width=384, Height=246)
    {
        this.Rasterize(Width,Height);
        return this.Canvas.toDataURL('image/png');
    }

    ConstructUV(Width=600, Height=380)
    {
        const Canvas=document.createElement('canvas');Canvas.width=Width;Canvas.height=Height;
        const Context=Canvas.getContext('2d');Context.fillStyle='#0a1211';Context.fillRect(0,0,Width,Height);
        Context.strokeStyle='#30443b';Context.lineWidth=.5;
        for (let Index=0;Index<=10;++Index) {Context.beginPath();Context.moveTo(Index*Width/10,0);Context.lineTo(Index*Width/10,Height);Context.moveTo(0,Index*Height/10);Context.lineTo(Width,Index*Height/10);Context.stroke();}
        // 📝 Generated cylindrical coordinates from actual preview vertices, not authored UV records from a file.
        Context.strokeStyle='#b2c8bb70';Context.lineWidth=.6;
        for (let Offset=0;Offset<this.Vertices.length;Offset+=30)
        {
            if (this.Vertices[Offset+6]>=5) continue;
            Context.beginPath();
            for (let Corner=0;Corner<3;++Corner)
            {
                const Shift=Offset+Corner*10;
                const U=(Math.atan2(this.Vertices[Shift+1]-1.8,this.Vertices[Shift+2])+Math.PI)/(2*Math.PI);
                const V=(this.Vertices[Shift]+3.2)/6.4;
                const X=20+U*(Width-40),Y=20+V*(Height-40);
                if (!Corner) Context.moveTo(X,Y);else Context.lineTo(X,Y);
            }
            Context.closePath();Context.stroke();
        }
        return Canvas;
    }

    AttachNavigation(OnChange)
    {
        let Pointer=null;
        const Canvas=this.Canvas;
        Canvas.tabIndex=0;
        Canvas.addEventListener('pointerdown',Event=>{if(Event.button>1)return;Pointer={Token:Event.pointerId,X:Event.clientX,Y:Event.clientY,Pan:Event.shiftKey||Event.button===1};Canvas.setPointerCapture(Event.pointerId);Canvas.focus();});
        Canvas.addEventListener('pointermove',Event=>{
            if(!Pointer||Pointer.Token!==Event.pointerId)return;
            const X=Event.clientX-Pointer.X,Y=Event.clientY-Pointer.Y;Pointer.X=Event.clientX;Pointer.Y=Event.clientY;
            if(Pointer.Pan||Event.shiftKey){this.Camera.Target[0]-=X*this.Camera.Distance*.0015;this.Camera.Target[1]+=Y*this.Camera.Distance*.0015;}
            else{this.Camera.Azimuth-=X*.008;this.Camera.Elevation=Math.max(-1.4,Math.min(1.4,this.Camera.Elevation+Y*.006));}
            this.Refresh();
        });
        for(const Name of ['pointerup','pointercancel','lostpointercapture'])Canvas.addEventListener(Name,()=>{if(Pointer){Pointer=null;OnChange?.();}});
        Canvas.addEventListener('wheel',Event=>{Event.preventDefault();this.Camera.Distance=Math.max(4,Math.min(28,this.Camera.Distance*Math.exp(Math.max(-100,Math.min(100,Event.deltaY))*.001)));this.Refresh();OnChange?.();},{passive:false});
        Canvas.addEventListener('keydown',Event=>{if(Event.key==='Home'){Event.preventDefault();this.Configure(this.Configuration);this.Refresh();OnChange?.();}});
        this.Observer=new ResizeObserver(()=>this.Refresh());this.Observer.observe(Canvas);
    }

    Refresh()
    {
        if (!this.Canvas.isConnected || this.Canvas.closest('[hidden]')) return;
        const Bounds=this.Canvas.getBoundingClientRect();
        if(Bounds.width<1||Bounds.height<1)return;
        try {this.Rasterize(Bounds.width*Math.min(devicePixelRatio||1,2),Bounds.height*Math.min(devicePixelRatio||1,2));}
        catch(Failure){this.OnFailure?.(Failure.message);}
    }
}
window.ReferencePreview = ReferencePreview;
})();
