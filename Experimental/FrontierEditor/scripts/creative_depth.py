"""Geometry/lighting corrections for the nine creative icons (not global effects).
Explicit edge thickness, local occlusion and one upper-left studio light.
"""
import copy
import xml.etree.ElementTree as ET


def refine_depth(key, defs, body):
    root=ET.fromstring('<g>'+body+'</g>')
    extra=[]
    def linear(name, stops, x2='.7', y2='1'):
        ident=key+'-depth-'+name
        extra.append(f'<linearGradient id="{ident}" x1="0" y1="0" x2="{x2}" y2="{y2}">'+''.join(f'<stop offset="{p}" stop-color="{c}"/>' for p,c in stops)+'</linearGradient>')
        return f'url(#{ident})'
    def radial(name, stops, cx='30%',cy='20%',r='85%'):
        ident=key+'-depth-'+name
        extra.append(f'<radialGradient id="{ident}" cx="{cx}" cy="{cy}" r="{r}">'+''.join(f'<stop offset="{p}" stop-color="{c}"/>' for p,c in stops)+'</radialGradient>')
        return f'url(#{ident})'
    def node(text):return ET.fromstring(text)
    def append(parent,text):parent.append(node(text))
    def parents():return {c:p for p in root.iter() for c in p}
    def find_attr(attr,value):return [e for e in root.iter() if e.get(attr)==value]
    def edge(e, color, dx,dy, highlight='#ffffff', opacity='.25'):
        # A physical backing face, not a drop shadow around the entire icon.
        p=parents()[e]; back=copy.deepcopy(e)
        back.set('transform',f'translate({dx} {dy}) '+back.get('transform',''))
        back.set('fill',color);back.attrib.pop('stroke',None)
        p.insert(list(p).index(e),back)
        e.set('stroke',highlight);e.set('stroke-opacity',opacity);e.set('stroke-width','.9')
        return back
    def contact(e,dx=2,dy=3,opacity='.3'):
        p=parents()[e];shadow=copy.deepcopy(e)
        shadow.set('transform',f'translate({dx} {dy}) '+shadow.get('transform',''))
        shadow.set('fill','#080f18');shadow.set('opacity',opacity)
        shadow.set('filter',f'url(#{key}-depth-contact)');shadow.attrib.pop('stroke',None)
        p.insert(list(p).index(e),shadow)
    extra.append(f'<filter id="{key}-depth-contact" x="-20%" y="-20%" width="140%" height="145%"><feGaussianBlur stdDeviation="1.2"/></filter>')
    if key=='speaker':
        front=radial('cabinet',[(0,'#6f8790'),('.5','#48616e'),(1,'#253c4b')],r='110%')
        for e in find_attr('fill','url(#speaker-face)'):
            edge(e,'#203541',3,2,'#a3b9bf','.24');e.set('fill',front)
        cone=radial('cone',[(0,'#243d4c'),('.27','#425863'),('.33','#889a9e'),('.72','#cbd2ca'),('.94','#929f9f'),(1,'#e6e9df')],cx='50%',cy='48%',r='52%')
        for e in find_attr('fill','url(#speaker-cone)'):e.set('fill',cone)
        surround=radial('suspension',[(0,'#0b202d'),('.7','#142c3a'),('.84','#607582'),('.92','#263f4e'),(1,'#0a1d28')],cx='45%',cy='40%',r='60%')
        for e in find_attr('fill','url(#speaker-rubber)'):e.set('fill',surround)
        for e in find_attr('fill','url(#speaker-cap)'):
            contact(e,1,2,'.6');e.set('stroke','#99adb5');e.set('stroke-opacity','.15');e.set('stroke-width','.8')
    if key in ['film-reel','cinema']:
        prefix='film-reel' if key=='film-reel' else 'cinema-reel'
        plate=linear('reel-metal',[(0,'#f2f3ed'),('.18','#cbd2d5'),('.46','#9ca8af'),('.72','#d0d5d4'),(1,'#73828e')])
        for e in find_attr('fill',f'url(#{prefix}-plate)'):
            if e.tag=='circle':edge(e,'#5b6b78',2.8,1.8,'#f3f6f2','.65');e.set('fill',plate)
        for i,e in enumerate(find_attr('fill',f'url(#{prefix}-hole)')):
            x=float(e.get('cx','0'));y=float(e.get('cy','0'));r=float(e.get('r'))
            p=parents()[e];j=list(p).index(e);ident=f'{key}-depth-hole-{i}'
            extra.append(f'<clipPath id="{ident}"><circle cx="{x}" cy="{y}" r="{r}"/></clipPath>')
            # Deep interior, offset sidewall and lower rim produce a recess, not a bead.
            e.set('fill','#080f16')
            p.insert(j+1,node(f'<g clip-path="url(#{ident})"><circle cx="{x+5}" cy="{y+2}" r="{r}" fill="#303a43"/><path d="M{x-r} {y}a{r} {r} 0 0 1 {r*2} 0" fill="none" stroke="#00070e" stroke-width="3" opacity=".55"/></g>'))
            p.insert(j+2,node(f'<circle cx="{x}" cy="{y}" r="{r+.6}" fill="none" stroke="#edf3ee" stroke-opacity=".38" stroke-width=".8"/>'))
        # Fine edge lighting makes the film visibly thin rather than a thick ramp.
        for e in root.iter('path'):
            if e.get('d','').startswith('M165 145'):
                e.set('stroke','#afbcc5');e.set('stroke-width','.8');e.set('stroke-opacity','.42')
    if key=='document-bundle':
        kraft=radial('kraft',[(0,'#fbe0a9'),('.6','#e6bd7b'),(1,'#bf8541')],r='120%')
        for e in find_attr('fill','url(#document-bundle-folder)'):
            if e.get('d','').startswith('M44'):
                contact(e,4,4,'.32');edge(e,'#9f692f',3,4,'#ffe9bb','.6');e.set('fill',kraft)
        for e in find_attr('fill','url(#document-bundle-paper)'):
            contact(e,1,3,'.18');edge(e,'#b2aea4',1,2,'#ffffff','.55')
        clip=linear('clip',[(0,'#b8dfe0'),('.17','#79b3bc'),('.6','#4a8c9b'),(1,'#26586c')])
        for e in find_attr('fill','url(#document-bundle-clip)'):
            contact(e,2,3,'.4');edge(e,'#285567',1,3,'#d8f4ef','.55');e.set('fill',clip)
        # Retain the existing wire outline and give it a small, local cast shadow.
        for e in list(root.iter('path')):
            if e.get('d','').startswith('M83 69'):
                p=parents()[e];sh=copy.deepcopy(e);sh.set('stroke','#715c43');sh.set('opacity','.5');sh.set('transform','translate(1.5 1.5)');p.insert(list(p).index(e),sh)
    if key=='file-folder':
        gold=radial('gold',[(0,'#ffe894'),('.4','#f9c32c'),('.75','#e6a60b'),(1,'#b2780c')],cx='18%',cy='12%',r='125%')
        for e in find_attr('fill','url(#file-folder-gold)'):
            contact(e,3,2,'.25');edge(e,'#9f6b0d',4,4,'#fff0ae','.6');e.set('fill',gold)
        for e in find_attr('fill','url(#file-folder-paper)'):
            contact(e,2,3,'.23');edge(e,'#bdb4a1',1.5,2.5,'#fffdf5','.6')
        # Rolled spine: a real dark interior surrounded by the folded card edge.
        p=list(root)[0]
        append(p,'<path d="M37 109v77q0 22 12 21" stroke="#865305" stroke-width="4" stroke-linecap="round" fill="none"/>')
        append(p,'<path d="M34 106v83q0 25 15 24" stroke="#ffdc63" stroke-width="2.3" stroke-linecap="round" fill="none"/>')
    if key=='retro-tv':
        case=radial('case',[(0,'#8495a2'),('.4','#586d7e'),(1,'#2a3e50')],r='120%')
        for e in find_attr('fill','url(#retro-tv-body)'):
            if e.tag=='path' and e.get('d','').startswith('M32'):
                edge(e,'#1e3142',3,2,'#b4c4ce','.22');e.set('fill',case)
        # CRT inner surround: bevel -> cavity -> curved glass, all on the same plane.
        for e in list(root.iter('rect')):
            if e.get('x')=='40' and e.get('y')=='85':
                e.set('fill','#0f2638');e.set('stroke','#96aebf');e.set('stroke-opacity','.45');e.set('stroke-width','1.2')
        for e in root.iter('g'):
            if e.get('clip-path')=='url(#retro-tv-screen)':
                # A perimeter-only glass shading layer; preserve vivid test colors.
                ident=key+'-depth-glass-vignette'
                extra.append(f'<radialGradient id="{ident}" cx="45%" cy="43%" r="70%"><stop offset=".5" stop-color="#071927" stop-opacity="0"/><stop offset="1" stop-color="#071927" stop-opacity=".55"/></radialGradient>')
                append(e,f'<rect x="45" y="90" width="110" height="105" fill="url(#{ident})"/>')
        for e in list(root.iter('circle')):
            if e.get('cx') in ['171','172']:
                contact(e,2,3,'.5');e.set('fill',radial('knob-'+e.get('cx'),[(0,'#8e9fab'),('.6','#596e80'),(1,'#263b4e')]))
                e.set('stroke','#b4c8d5');e.set('stroke-opacity','.23');e.set('stroke-width','.8')
    if key in ['cinema','video-slate']:
        prefix='cinema-slate' if key=='cinema' else 'video-slate'
        bodyfill=radial('slate',[(0,'#69717a'),('.38','#434e5a'),(1,'#202d3b')],r='115%')
        for e in find_attr('fill',f'url(#{prefix}-body)'):
            edge(e,'#111e2c',4,5,'#a4b3bd','.28');e.set('fill',bodyfill)
        # Raised clapper arms have their own bevels and thickness, independent of slate.
        for e in find_attr('fill',f'url(#{prefix}-bar)'):
            if e.tag=='rect':edge(e,'#172432',2,3,'#c4cdd0','.35')
        if key=='video-slate':
            for e in find_attr('fill','url(#video-slate-red)'):
                if e.tag=='path':
                    contact(e,4,4,'.4');edge(e,'#ad6c07',3,2,'#fff1b2','.6')
            for e in find_attr('fill','url(#video-slate-white)'):
                if e.tag=='rect' and e.get('y')=='195':edge(e,'#77858c',2,3,'#fffdf3','.6')
    if key=='paint-palette':
        porcelain=radial('porcelain',[(0,'#fff9f1'),('.5','#e8dde8'),('.8','#c4b7d2'),(1,'#968dac')],cx='25%',cy='20%',r='110%')
        for e in find_attr('fill','url(#paint-palette-base)'):
            # Preserve the true thumb-hole while adding a curved rim to the top face.
            e.set('fill',porcelain);e.set('stroke','#fffbf2');e.set('stroke-opacity','.45');e.set('stroke-width','1')
        for e in list(root.iter('circle')):
            fill=e.get('fill','')
            if fill.startswith('url(#paint-palette-'):
                p=parents()[e];idx=list(p).index(e)
                p.insert(idx,node('<circle r="20.6" cy=".8" fill="#fcf7f3" opacity=".8"/>'))
                e.set('stroke','#68566e');e.set('stroke-opacity','.45');e.set('stroke-width','1.5')
                # Upper well wall is concave, while the lower lip catches the light.
                append(p,'<path d="M-19 0a19 19 0 0 1 38 0" fill="none" stroke="#5e4b69" stroke-width="2.5" stroke-opacity=".28"/>')
        handle=linear('brush-handle',[(0,'#927039'),('.23','#e3bd70'),('.42','#f9dc93'),('.65','#c49a52'),(1,'#826131')],x2='1',y2='.25')
        for e in find_attr('fill','url(#paint-palette-handle)'):e.set('fill',handle)
        for e in find_attr('fill','url(#paint-palette-metal)'):
            e.set('stroke','#f5f3f3');e.set('stroke-opacity','.3');e.set('stroke-width','.8')
        # Fine, tapered bristle strands curve over the tuft, rather than a flat blob.
        g=list(root)[0]
        for d in ['M184 46q-9-18 13-32','M188 48q-4-11 9-24','M193 51q10-12 8-25']:
            append(g,f'<path d="{d}" fill="none" stroke="#dfb77c" stroke-opacity=".24" stroke-width=".65"/>')
    if key=='drawing-compass':
        pink=linear('anodized-pink',[(0,'#ffd4e8'),('.15','#ffa2cf'),('.43','#f87eb9'),('.8','#d74b93'),(1,'#a72a6c')],x2='1',y2='.2')
        for e in find_attr('fill','url(#drawing-compass-pink)'):
            edge(e,'#9c306b',4,2,'#ffd2e7','.6');e.set('fill',pink)
        for e in find_attr('fill','url(#drawing-compass-yellow)'):
            if e.tag=='circle':
                edge(e,'#a17510',4,1,'#fff6a2','.6')
            else:edge(e,'#b78311',2,2,'#fff59b','.4')
        for e in find_attr('fill','url(#drawing-compass-metal)'):
            if e.tag=='circle':
                contact(e,1,1,'.4');e.set('stroke','#fffbed');e.set('stroke-opacity','.5');e.set('stroke-width','.6')
        # Machine the adjustment barrel: narrow sidewall + grooved circumference.
        for e in root.iter('g'):
            if e.get('transform')=='translate(128 130) scale(.65 1)':
                append(e,'<path d="M-10-15q-13 15 0 30m4-32q-13 17 0 34" stroke="#968292" stroke-opacity=".35" stroke-width="1.1" fill="none"/>')
    # A shallow rounded edge is lit from the same upper-left direction. This
    # derives a normal from each face's alpha, rather than faking depth with glow.
    bevel_id=key+'-depth-bevel'
    extra.append(f'''<filter id="{bevel_id}" x="-10%" y="-10%" width="120%" height="120%" color-interpolation-filters="sRGB"><feGaussianBlur in="SourceAlpha" stdDeviation="1.7" result="bump"/><feSpecularLighting in="bump" surfaceScale="3" specularConstant=".48" specularExponent="24" lighting-color="#ffffff" result="light"><feDistantLight azimuth="225" elevation="48"/></feSpecularLighting><feComposite in="light" in2="SourceAlpha" operator="in" result="shine"/><feBlend in="SourceGraphic" in2="shine" mode="screen"/></filter>''')
    for e in root.iter():
        fill=e.get('fill','')
        if (fill.startswith('url(#'+key+'-depth-') and any(t in fill for t in ['cabinet','reel-metal','kraft','gold','case','slate','anodized-pink','porcelain'])) or fill in ['url(#paint-palette-metal)','url(#speaker-top)']:
            if e.tag in ['path','rect','circle']:e.set('filter',f'url(#{bevel_id})')
    return defs+''.join(extra),''.join(ET.tostring(c,encoding='unicode') for c in root)
