"""Branching lightning, volumetric ground fog and a transparent water glass."""


def lightning_art():
    import random
    rng=random.Random(43)
    # Unequal segment lengths and small correlated deviations avoid a regular zigzag.
    def channel(anchors):
        pts=[anchors[0]]
        for (x,y),(xx,yy) in zip(anchors,anchors[1:]):
            n=max(2,int(((xx-x)**2+(yy-y)**2)**.5/4))
            for i in range(1,n):
                t=i/n
                pts.append((x+(xx-x)*t+rng.uniform(-1.3,1.3),y+(yy-y)*t+rng.uniform(-.7,.7)))
            pts.append((xx,yy))
        path=f'M{pts[0][0]} {pts[0][1]}'
        for i in range(1,len(pts)-1):
            x,y=pts[i];xx,yy=pts[i+1]
            path+=f'Q{x:.2f} {y:.2f} {(x+xx)/2:.2f} {(y+yy)/2:.2f}'
        return path+f'L{pts[-1][0]} {pts[-1][1]}'
    trunk=[(143,24),(140,43),(127,58),(127,72),(116,78),(113,100),(119,113),(109,123),(108,148),(99,157),(95,178),(84,190),(82,214),(73,231)]
    forks=[
      ([(127,58),(150,67),(162,83),(164,98),(180,113),(184,135)],1.05),
      ([(113,100),(95,106),(88,121),(69,134),(63,150),(47,169)],1.2),
      ([(109,123),(131,131),(134,146),(154,158),(161,180),(177,196)],1.1),
      ([(95,178),(111,183),(117,200),(132,211),(135,228)],.8),
      ([(150,67),(171,66),(182,76),(201,85)],.6),
      ([(164,98),(183,101),(201,118),(210,138)],.55),
      ([(88,121),(75,109),(70,96),(55,88)],.6),
      ([(69,134),(47,137),(35,151)],.55),
      ([(154,158),(175,158),(185,168),(204,175)],.65),
      ([(134,146),(130,161),(140,179),(139,196)],.55),
      ([(84,190),(62,198),(54,215),(43,224)],.75),
      ([(127,72),(107,68),(100,53),(84,44)],.65),
      ([(63,150),(69,164),(60,184)],.45),
    ]
    main=channel(trunk)
    defs='''<filter id="lightning-bloom" x="-50%" y="-15%" width="200%" height="130%"><feGaussianBlur stdDeviation="3.2"/></filter><filter id="lightning-corona" x="-30%" y="-15%" width="160%" height="130%"><feGaussianBlur stdDeviation=".8"/></filter>'''
    body=f'<g fill="none" stroke-linecap="round" stroke-linejoin="round"><path d="{main}" stroke="#7e9ef9" stroke-width="9" opacity=".4" filter="url(#lightning-bloom)"/>'
    for i,(anchors,width) in enumerate(forks):
        x,y=anchors[0];xx,yy=anchors[-1];path=channel(anchors)
        defs+=f'<linearGradient id="lightning-tip-{i}" gradientUnits="userSpaceOnUse" x1="{x}" y1="{y}" x2="{xx}" y2="{yy}"><stop stop-color="#d8e7ff"/><stop offset=".65" stop-color="#b3c9f4" stop-opacity=".8"/><stop offset="1" stop-color="#829edb" stop-opacity=".1"/></linearGradient>'
        body+=f'<path d="{path}" stroke="#8dabfa" stroke-width="{width+1.8}" opacity=".3" filter="url(#lightning-corona)"/><path d="{path}" stroke="url(#lightning-tip-{i})" stroke-width="{width}"/>'
    body+=f'<path d="{main}" stroke="#acc5ff" stroke-width="4" opacity=".65" filter="url(#lightning-corona)"/><path d="{main}" stroke="#f5faff" stroke-width="1.7"/></g>'
    return defs,body


def register_weather_material_icons(icon):
    defs='''<linearGradient id="fluid-glass-wall"><stop stop-color="#c6e9f4" stop-opacity=".5"/><stop offset=".13" stop-color="#d1f1ff" stop-opacity=".09"/><stop offset=".55" stop-color="#7ab5d1" stop-opacity=".025"/><stop offset=".88" stop-color="#d4f6ff" stop-opacity=".23"/><stop offset="1" stop-color="#97d2e7" stop-opacity=".55"/></linearGradient><linearGradient id="fluid-glass-water" x2=".2" y2="1"><stop stop-color="#b8e4ed" stop-opacity=".7"/><stop offset=".45" stop-color="#62b4d3" stop-opacity=".5"/><stop offset="1" stop-color="#2b7caa" stop-opacity=".78"/></linearGradient><linearGradient id="fluid-glass-surface" x2=".8" y2="1"><stop stop-color="#ebffff" stop-opacity=".85"/><stop offset=".5" stop-color="#8dcfe4" stop-opacity=".55"/><stop offset="1" stop-color="#3994bf" stop-opacity=".8"/></linearGradient><linearGradient id="fluid-glass-base"><stop stop-color="#d7f5ff" stop-opacity=".5"/><stop offset=".5" stop-color="#6cabbf" stop-opacity=".2"/><stop offset="1" stop-color="#c5f0ff" stop-opacity=".65"/></linearGradient>'''
    body='''<path d="M64 56 78 200c2 20 98 20 100 0l14-144" fill="url(#fluid-glass-wall)" stroke="#b3d9e6" stroke-opacity=".55" stroke-width="1.5"/><path d="m72 112 10 82c2 17 90 17 92 0l10-82Z" fill="url(#fluid-glass-water)"/><path d="M72 112c7-14 41-10 58-6 19 5 42 5 54 6-3 13-105 17-112 0Z" fill="url(#fluid-glass-surface)" stroke="#dbf8ff" stroke-opacity=".65" stroke-width="1.3"/><path d="M77 113q24-8 47-3t50 4" stroke="#f0ffff" stroke-opacity=".6" stroke-width="1.2"/><path d="M64 56a64 16 0 1 0 128 0 64 16 0 1 0-128 0Z" fill="url(#fluid-glass-wall)" stroke="#d1e8ee" stroke-width="2"/><ellipse cx="128" cy="56" rx="57" ry="11" stroke="#96bfce" stroke-width="1" stroke-opacity=".45"/><path d="M69 62 82 194" stroke="#f1fcff" stroke-width="3" stroke-opacity=".7" stroke-linecap="round"/><path d="m182 72-10 113" stroke="#c9f1fb" stroke-opacity=".45" stroke-width="5" stroke-linecap="round"/><path d="M80 198c8 17 87 17 96 0v6c-10 18-86 18-96 0Z" fill="url(#fluid-glass-base)"/><path d="M87 201q41 12 81 0" stroke="#ddf9ff" stroke-width="2" stroke-opacity=".7"/><path d="M101 184q24 8 48 0" stroke="#b6ecff" stroke-opacity=".3" stroke-width="2" stroke-linecap="round"/>'''
    icon('fluid-glass','Fluid · water glass','Objects','Clear water in a tapered glass, with a visible meniscus, transparent walls, pale-blue refraction and a thick reflective base. Native SVG.',defs,body)

    defs='''<radialGradient id="fog-volume" cx="44%" cy="38%" r="62%"><stop stop-color="#e2e8e8" stop-opacity=".64"/><stop offset=".38" stop-color="#c2d0d6" stop-opacity=".48"/><stop offset=".75" stop-color="#91a7b8" stop-opacity=".2"/><stop offset="1" stop-color="#819bab" stop-opacity="0"/></radialGradient><filter id="fog-drift" x="-20%" y="-35%" width="140%" height="170%"><feTurbulence type="fractalNoise" baseFrequency=".027 .046" numOctaves="3" seed="32" result="noise"/><feDisplacementMap in="SourceGraphic" in2="noise" scale="15" result="cloud"/><feColorMatrix in="noise" type="matrix" values="0 0 0 0 1 0 0 0 0 1 0 0 0 0 1 .5 .25 .25 0 .08" result="density"/><feComposite in="cloud" in2="density" operator="in"/><feGaussianBlur stdDeviation="2"/></filter><filter id="fog-wisp" x="-25%" y="-50%" width="150%" height="200%"><feGaussianBlur stdDeviation="4"/></filter>'''
    body='<g filter="url(#fog-drift)">'
    # Interlocking volumes occupy a single shallow bank, not separated shelves.
    for x,y,rx,ry,opacity in [(126,151,104,49,.8),(67,145,51,31,.8),(107,129,62,43,.68),(166,149,67,37,.9),(193,153,40,26,.6),(109,167,70,30,.7),(148,134,42,33,.5)]:
        body+=f'<circle r="{rx}" transform="translate({x} {y}) scale(1 {ry/rx})" fill="url(#fog-volume)" opacity="{opacity}"/>'
    body+='</g>'
    body+='''<g fill="none" stroke="#d2dfe2" stroke-linecap="round" filter="url(#fog-wisp)"><path d="M35 151c24 8 38-8 66-5 20 2 29 12 55 7 19-4 33-3 53 1" stroke-width="7" opacity=".16"/><path d="M66 135c20-19 51-19 72-9 12 6 29 13 45 9" stroke-width="5" opacity=".15"/><path d="M45 176c39-11 73 9 109-1 19-6 43-5 59 0" stroke-width="5" opacity=".12"/></g>'''
    icon('fog','Fog','Environment','A continuous low-lying bank of mist with interlocking diffuse volumes, uneven density and softly curling wisps. No stacked bands or hard cloud outlines. Native SVG.',defs,body)
