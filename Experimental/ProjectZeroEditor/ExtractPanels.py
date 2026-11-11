#!/usr/bin/env python3
"""Retain native property declarations for the HTML editor, without compiling or changing engine code."""
from pathlib import Path
import json,re,hashlib
Root=Path(__file__).resolve().parents[2]
Source=Root/'Engine/Host/CelestialSequence.cpp'
Text=Source.read_text()
def Body(Name):
    Start=Text.index('{',Text.index('::'+Name+'(')); Depth=1; End=Start+1
    while Depth:
        Depth+=(Text[End]=='{')-(Text[End]=='}'); End+=1
    return Text[Start+1:End-1]
def Arguments(Text,Start):
    Depth=1; Quote=False; Escape=False; End=Start; Parts=[]; Last=Start
    while Depth:
        C=Text[End]
        if Quote:
            if Escape: Escape=False
            elif C=='\\': Escape=True
            elif C=='"': Quote=False
        elif C=='"': Quote=True
        elif C in '({[': Depth+=1
        elif C in ')}]': Depth-=1
        elif C==',' and Depth==1: Parts.append(Text[Last:End].strip()); Last=End+1
        End+=1
    Parts.append(Text[Last:End-1].strip()); return Parts
Constants={'SunColourTemperature::MinimumKelvin':1000,'SunColourTemperature::MaximumKelvin':40000,'Cloud.CeilingMetres':12000}
def Number(S,Default):
    if S in Constants:return Constants[S]
    S=re.sub(r'(?<=\d)[fu]\b','',S)
    try:return float(S)
    except ValueError:return Default
Defaults={'Angular Diameter':.533,'Temperature':6500,'Intensity':1,'Direct':2.5,'Local Hours':10.2,'Day duration':24,'Latitude':-26.2,'Longitude':28.3,'Day of Month':4,'Month':10,'Brightness':1,'Point Size':1,'Limiting magnitude':5.5,'Twinkle depth':30,'Twinkle rate':1.2,'Celestial rotation':0,'Coverage':.55,'Density':1,'Feature Scale':1,'Anisotropy':.6,'Base':2000,'Thickness':1800,'Ceiling':12000,'Speed':4.2,'Bearing':225,'Shear':.15,'Veer':10,'Gust':.3,'Turbulence':.2,'Steadiness':.7,'Particle Size':1,'Wind Drift':.8,'Accumulation':.2,'Width':1,'Secondary':.35,'Minimum Path':50,'Rayleigh':1,'Mie':1,'Mie Anisotropy':.8,'Ozone':1,'Rayleigh Scale H':8000,'Mie Scale H':1200,'Atmosphere':80000,'Sky Brightness':1,'Horizon Glow':1,'White Line':1,'Falloff Height':391,'Sun Scatter':1,'Start':50,'Mie Blend':.5,'Ghosts':6,'Halo Radius':.5,'Chromatic':.25,'Aperture Blades':6,'Streak gain':1,'Spread':1,'Rotation':0,'Ray pairs':6,'Ghost brightness':1,'Ghost spacing':1,'Halo brightness':1,'Halo width':.035,'Preview X':.28,'Preview Y':.43}
def Extract(Code):
    Code=re.sub(r'//[^\n]*','',Code)
    Options={M[1]:re.findall(r'"([^"\n]+)"',M[2]) for M in re.finditer(r'const\s+char\*\s*const\s+(\w+)\[\]\s*=\s*\{([^}]+)\}',Code)}
    Groups={}; Fields=[]
    for M in re.finditer(r'(?:auto&|EditorPropertyGroup&)\s*(\w+)\s*=\s*OpenGroup\(Sheet,\s*"([^"]+)"',Code):Groups[M[1]]=M[2]
    for M in re.finditer(r'Push\(\s*(\w+)\s*,\s*Make(Slider|Switch|Select|Colour|Axes|Readout)\(',Code):
        A=Arguments(Code,M.end()); Label=A[0].strip('"')
        if not A[0].startswith('"'):continue
        F={'Label':Label,'Control':M[2],'Group':Groups.get(M[1],M[1]),'NativeArguments':A[1:]}
        if M[2]=='Slider':
            F.update(Minimum=Number(A[1],0),Maximum=Number(A[2],100),Decimals=int(Number(A[4],2)),Unit=A[5].strip('"'))
            F['Default']=max(F['Minimum'],min(F['Maximum'],Number(A[3],Defaults.get(Label,1))))
        elif M[2]=='Switch': F['Default']= A[1]!='false' and Label not in ['Animate','Fetch Baked Dome']
        elif M[2]=='Select': F.update(Options=Options.get(A[1],['Default']),Default=0)
        elif M[2]=='Colour': F['Default']='#ffffff' if Label!='Ground Albedo' else '#796e60'
        elif M[2]=='Axes': F['Default']=[100,100,100] if Label=='Half Size' else [0,0,0]
        else:F['Default']='—'
        if not any(X['Label']==Label for X in Fields):Fields.append(F)
    return Fields
Panels={}
for Key,Method in [('sun','BuildSunSheet'),('stars','BuildStarsSheet'),('wind','BuildWindSheet'),('precipitation','BuildPrecipitationSheet'),('rainbow','BuildRainbowSheet'),('flare','BuildFlareSheet'),('atmosphere','BuildAtmosphereSkySheet')]:Panels[Key]=Extract(Body(Method))
Panels['precipitation']+=Extract(Body('BuildPrecipitationBehaviour'))
Cloud=Body('BuildCloudSheet'); Split=Cloud.index('return;')
Panels['local-cloud']=Extract(Cloud[:Split]); Panels['clouds']=Extract(Cloud[Split:])+Extract(Body('BuildCloudShadowSheet'))
Fog=Body('BuildFogSheet')
for Key,Case in [('height-fog','HeightFog'),('aerial-fog','AtmosphericFog'),('local-fog','LocalFog')]:
    Section=Fog.split('case CelestialEntity::'+Case+':')[1].split('break;')[0];Panels[Key]=Extract(Section)
    if Key=='height-fog':next(F for F in Panels[Key] if F['Label']=='Density')['Default']=.002
    if Key=='local-fog':next(F for F in Panels[Key] if F['Label']=='Feature Scale')['Default']=100
for Key in ['clouds','local-cloud','height-fog','aerial-fog','local-fog']:
    Panels[Key]+=[{'Label':'Wind source','Control':'Select','Group':'Wind binding','Options':['Global wind','Cloud wind','Local cloud wind','Local fog wind','Height fog wind','Aerial fog wind'],'Default':0}, {'Label':'Owned wind component','Control':'Switch','Group':'Wind binding','Default':False}]
def Slider(Label,Minimum,Maximum,Default,Unit='',Group='Properties',Decimals=2):return dict(Label=Label,Control='Slider',Minimum=Minimum,Maximum=Maximum,Default=Default,Unit=Unit,Group=Group,Decimals=Decimals)
Panels['camera']=[Slider('Focal Length',1,500,35,'mm'),Slider('Sensor Width',16,70,36,'mm'),Slider('Aperture',1.4,22,2.8,'f/'),Slider('Subject Distance',1,100,10,'m')]
Panels['moon']=[Slider('Phase',0,1,.62),Slider('Bright',0,6,1.6,'x'),Slider('Glow',0,3,.8,'x'),Slider('Size',.1,180,.52,'deg'),Slider('Azimuth',0,360,300,'deg'),Slider('Elevation',-90,90,28,'deg'),Slider('Roll',0,360,0,'deg'),Slider('Pitch',-180,180,0,'deg'),dict(Label='Visible',Control='Switch',Default=True,Group='Moon'),dict(Label='Follow Sky',Control='Switch',Default=False,Group='Moon'),dict(Label='Preset',Control='Select',Options=['Luna','Ember','Glacier','Sulfur','Shroud','Shard'],Default=0,Group='Moon')]
Panels['post']=[Slider('EV Compensation',-4,4,0,'EV','Exposure'),Slider('Saturation',0,2,1,Group='Tone Mapping'),Slider('Contrast',.5,2,1,Group='Tone Mapping'),Slider('Bloom Intensity',0,1,.05,Group='Lens Effects'),Slider('Vignette',0,1,.15,Group='Lens Effects')]
Panels['geometry']=[dict(Label='Position',Control='Axes',Default=[0,0,0],Group='Transform',ReadOnly=True),dict(Label='Rotation',Control='Readout',Default='+0° about Z',Group='Transform'),dict(Label='Albedo',Control='Colour',Default='#c06bbf',Group='Surface'),Slider('Emission',0,64,0,'lx','Surface',1),Slider('Roughness',0,1,.4,Group='Surface'),dict(Label='Metallic',Control='Readout',Default='0.00',Group='Surface')]
Panels['light']=[Slider('Intensity',0,64,32,'lx','Light',1),dict(Label='Colour',Control='Colour',Default='#ffffff',Group='Light'),dict(Label='Direction',Control='Readout',Default='−Z (nadir)',Group='Aim')]
Panels['group']=[dict(Label='Contents',Control='Readout',Default='5 direct · 14 total',Group='Group'),dict(Label='Tint',Control='Colour',Default='#9bacb4',Group='Group')]
# Literal staging in Prepare takes precedence over authoring defaults. Keep the expressions for provenance.
Prepared={}
DefaultHeaders={
 'Observation':('CelestialSolver.h','CelestialObservation'),
 'Clock':('../Host/CelestialSequence.h','CelestialClock'),
 'Medium':('AtmosphereModel.h','AtmosphereMedium'),
 'Light':('AtmosphereModel.h','AtmosphereLight'),
 'Twilight':('AtmosphereModel.h','TwilightSettings'),
 'Wind':('WindField.h','WindSettings'),
 'Cloud':('VolumetricMedia.h','CloudLayerSettings'),
 'LocalCloud':('VolumetricMedia.h','LocalVolumeSettings'),
 'LocalFog':('VolumetricMedia.h','LocalVolumeSettings'),
 'Fog':('VolumetricMedia.h','FogSettings'),
 'Precip':('Precipitation.h','PrecipitationSettings'),
 'Rainbow':('AtmosphericOptics.h','RainbowSettings'),
 'Flare':('AtmosphericOptics.h','LensFlareSettings')}
for Scope,(File,Struct) in DefaultHeaders.items():
    Header=re.sub(r'//[^\n]*','',(Root/'Engine/DisplayPresentation'/File).read_text())
    Found=re.search(r'struct\s+'+Struct+r'\s*\{',Header)
    if not Found:continue
    Start=Found.end();End=Start;Depth=1
    while Depth:
        Depth+=(Header[End]=='{')-(Header[End]=='}');End+=1
    Block=Header[Start:End-1]
    for Match in re.finditer(r'(?:float|bool|uint32_t)\s+(\w+)\s*=\s*(-?[0-9.eE+-]+[fu]?|true|false)\s*;',Block):
        Value=Match[2];Prepared[Scope+'.'+Match[1]]=Value=='true' if Value in ['true','false'] else Number(Value,0)
for Match in re.finditer(r'(\w+\.\w+(?:\[\d\])?)\s*=\s*(-?[0-9.]+[fu]?|true|false)\s*;',re.sub(r'//[^\n]*','',Body('Prepare'))):
    Value=Match[2]
    Prepared[Match[1]]=Value=='true' if Value in ['true','false'] else Number(Value,0)
for Key,Sheet in Panels.items():
    for Field in Sheet:
        Arguments=Field.get('NativeArguments',[])
        Expression=Arguments[2] if Field['Control']=='Slider' and len(Arguments)>2 else Arguments[0] if Arguments else ''
        Expression=Expression.replace('SourceWind.','Wind.')
        if Expression in Prepared:Field['Default']=Prepared[Expression]
        Comparison=re.fullmatch(r'(\w+\.\w+)\s*>\s*([0-9.]+)f?',Expression)
        if Comparison and Comparison[1] in Prepared:Field['Default']=Prepared[Comparison[1]]>float(Comparison[2])
        if Field['Control']=='Axes' and Expression+'[0]' in Prepared:Field['Default']=[Prepared[Expression+'['+str(I)+']'] for I in range(3)]
for Field in Panels['sun']:
    if Field['Label']=='Day duration':Field['Default']=3
    if Field['Label']=='Speed':Field['Default']=1
Result={'SourceCommit':'9864969','Panels':Panels,'SourceFiles':{str(P.relative_to(Root)):hashlib.sha256(P.read_bytes()).hexdigest() for P in [Source,*[Root/'Engine/DisplayPresentation'/File for File,_ in DefaultHeaders.values()],*sorted((Root/'Engine/Editor').glob('*InspectorPanel.cpp')),Root/'Engine/Editor/EditorStyleSpecification.h',Root/'Engine/DisplayPresentation/ControlCentreHost.h']}}
(Path(__file__).parent/'NativePanels.json').write_text(json.dumps(Result,indent=2)+'\n')
print('Extracted',len(Panels),'native property sheets;',sum(map(len,Panels.values())),'controls')
