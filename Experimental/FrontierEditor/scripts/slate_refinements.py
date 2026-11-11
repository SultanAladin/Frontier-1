"""Explicit user-requested fixture adaptations; original vendor source is untouched."""
import xml.etree.ElementTree as ET


def append_markup(parent, markup):
    parent.extend(list(ET.fromstring('<g>'+markup+'</g>')))


def refine_light(svg, key):
    if key not in {'slate-spotlight','slate-l-e-d-panel','slate-lantern'}:
        return
    scene=next(e for e in svg if e.tag=='g')
    main=next(e for e in scene if e.tag=='g')
    defs=next(e for e in svg if e.tag=='defs')
    if key=='slate-l-e-d-panel':
        for n in main.iter():
            if n.get('transform','').startswith('rotate('):n.attrib.pop('transform')
            if n.get('stroke') in ['#222','#333']:n.set('stroke','#555b62')
            if n.get('stroke')=='rgba(255,255,255,0.15)':n.set('stroke','#73787c')
            if n.get('fill')=='#1f1f1f':n.set('fill','#2b2e32')
            if n.get('fill')=='#fef08a':n.set('fill','#fff1c8')
            if n.get('stroke-width')=='4':n.set('stroke-width','3')
            if n.tag=='line' and n.get('y2')=='70':n.set('y2','48')
        return
    if key=='slate-spotlight':
        # The approved upright light core and beam; no changes to their source gradients.
        beam=next(n for n in main.iter() if n.get('fill')=='url(#spotBeam)')
        beam.set('d','M -22 20 L 22 20 L 48 75 L -48 75 Z')
        aperture=[n for n in main.iter() if n.tag=='ellipse']
        for n in aperture:
            if n.get('rx')=='30':
                n.set('rx','27');n.set('ry','7');n.set('stroke','#3b454d');n.set('stroke-width','.7')
            if n.get('rx')=='22':n.set('fill','#fff1ba')
        append_markup(defs,'''<linearGradient id="spotBarrel"><stop stop-color="#20262b"/><stop offset=".2" stop-color="#657077"/><stop offset=".36" stop-color="#434b52"/><stop offset=".7" stop-color="#2c3238"/><stop offset="1" stop-color="#161c22"/></linearGradient><linearGradient id="spotMount" x2=".8" y2="1"><stop stop-color="#939ba0"/><stop offset=".5" stop-color="#4c555d"/><stop offset="1" stop-color="#303940"/></linearGradient>''')
        for n in list(main):main.remove(n)
        append_markup(main,'''<path d="M-41-16h6V2q0 12 12 12h46q12 0 12-12v-18h6V2q0 18-18 18h-46q-18 0-18-18Z" fill="url(#spotMount)" stroke="#89969e" stroke-width=".7"/>
<path d="M-39-13V2q0 16 16 16h46" stroke="#b8c1c4" stroke-opacity=".4" stroke-width=".9" fill="none"/>
<path d="M-37-12h9m56 0h9" stroke="#687780" stroke-width="5"/>
<path d="M-25-29a25 12 0 0 1 50 0l3 49q-28 9-56 0Z" fill="url(#spotBarrel)" stroke="#626e76" stroke-width="1"/>
<path d="M-23-29a23 10 0 0 1 46 0" stroke="#a0acb1" stroke-opacity=".5" stroke-width=".9" fill="none"/>
<path d="M-20-24l-1 8m5-10-1 9m5-11-1 10m5-11v10" stroke="#1b252d" stroke-width="2" stroke-linecap="round"/>
<path d="M-19-9l-1 20" stroke="#a5b1b8" stroke-opacity=".3" stroke-width="1.6" stroke-linecap="round"/>
<path d="M-29 20a29 9 0 1 0 58 0 29 9 0 1 0-58 0Z" fill="url(#spotMount)" stroke="#7c8a94" stroke-width=".7"/>
<circle cx="-29" cy="-12" r="4.5" fill="url(#spotMount)" stroke="#87949c" stroke-width=".7"/><circle cx="29" cy="-12" r="4.5" fill="url(#spotMount)" stroke="#87949c" stroke-width=".7"/><path d="M-31-12h4M29-14v4" stroke="#27343b" stroke-width="1.1" fill="none"/>''')
        main.extend(aperture);main.append(beam)
        return
    # Reconstruct the latest connected lantern, preserving the approved tank geometry.
    append_markup(defs,'''<linearGradient id="lanternRedMetal"><stop stop-color="#351d25"/><stop offset=".18" stop-color="#8a4e4b"/><stop offset=".34" stop-color="#b17666"/><stop offset=".59" stop-color="#773d3c"/><stop offset="1" stop-color="#351e27"/></linearGradient>
<linearGradient id="lanternTube"><stop stop-color="#673539"/><stop offset=".3" stop-color="#bd8873"/><stop offset=".6" stop-color="#884a45"/><stop offset="1" stop-color="#3e252c"/></linearGradient>
<linearGradient id="lanternTankTop" x2=".3" y2="1"><stop stop-color="#b17e6d"/><stop offset=".4" stop-color="#8a5550"/><stop offset="1" stop-color="#472831"/></linearGradient>''')
    for n in list(main):main.remove(n)
    append_markup(main,'''<path d="M-32-14v-28c0-39 64-39 64 0v28" fill="none" stroke="#7a8080" stroke-width="2.3" stroke-linecap="round"/>
<path d="M-32-14v-28c0-18 8-28 20-29" fill="none" stroke="#c0c4bb" stroke-opacity=".35" stroke-width=".7"/>
<path d="M-25 51c-9-11-13-26-12-48l1-25q1-14 14-17M25 51c9-11 13-26 12-48l-1-25q-1-14-14-17" fill="none" stroke="#3c242b" stroke-width="8" stroke-linecap="round"/>
<path d="M-25 51c-9-11-13-26-12-48l1-25q1-14 14-17M25 51c9-11 13-26 12-48l-1-25q-1-14-14-17" fill="none" stroke="url(#lanternTube)" stroke-width="5.5" stroke-linecap="round"/>
<path d="M-18-23c0 21-11 38-8 55 2 13 12 18 26 18s24-5 26-18c3-17-8-34-8-55Z" fill="url(#lanternGlass)" stroke="#aeb9b7" stroke-opacity=".55" stroke-width=".9"/>
<circle cx="0" cy="22" r="26" fill="url(#lanternGlow)"/>
<path d="M-14-15c-1 23-10 36-6 48" fill="none" stroke="#e8f0e8" stroke-opacity=".46" stroke-width="1.6" stroke-linecap="round"/>
<path d="M20 4q8 23 0 34" fill="none" stroke="#c7d8d2" stroke-opacity=".25" stroke-width="1.2"/>
<path d="M-27 43q27 8 54 0l7 13v10q0 7-34 8-34-1-34-8V56Z" fill="url(#lanternRedMetal)" stroke="#815b59" stroke-width=".8"/>
<path d="M-27 43c0-6 54-6 54 0s-54 8-54 0Z" fill="url(#lanternTankTop)"/>
<path d="M-34 65q34 10 68 0v3q-34 12-68 0Z" fill="#38232c" stroke="#895e5a" stroke-width=".7"/>
<path d="M-29 66q11 4 25 4" fill="none" stroke="#c79b82" stroke-opacity=".5" stroke-width="1" stroke-linecap="round"/>
<path d="M-24 53v7" stroke="#d2a28a" stroke-opacity=".38" stroke-width="2" stroke-linecap="round"/>
<path d="M17 47l5-2 7 3-5 3Z" fill="#b99a78"/><path d="M17 47v3l7 3 5-3v-2l-5 3Z" fill="#705a48"/>
<path d="M-11 40q11-5 22 0v4q-11 3-22 0Z" fill="#57504a"/><rect x="-2" y="31" width="4" height="9" rx="1" fill="#302b29"/>
<path d="M1 1c-3 10-10 16-10 24 0 12 18 12 19 0C11 17 4 8 1 1Z" fill="#ffe4a0"/><path d="M0 17c-3 5-4 10-2 13 4 4 8 0 6-5Z" fill="#fffdec"/>
<path d="M-25-28c2-15 12-25 25-25s23 10 25 25l3 5q-28 8-56 0Z" fill="url(#lanternRedMetal)" stroke="#94645d" stroke-width=".8"/>
<path d="M-28-25q28 7 56 0v3q-28 8-56 0Z" fill="#502a32" stroke="#9d7063" stroke-width=".7"/>
<path d="M-18-38q4-9 14-11" stroke="#c6977f" stroke-opacity=".5" stroke-width=".9" fill="none"/>
<path d="M-12-36v4m8-7v4m8-4v4m8-1v4" stroke="#2b1c23" stroke-width="1.8" stroke-linecap="round"/>
<path d="M-21-30q5 3 11 3" stroke="#c48d74" stroke-opacity=".42" stroke-width="1.2" stroke-linecap="round" fill="none"/>
<path d="M-27 21q-1 12 4 18" stroke="#c59577" stroke-opacity=".35" stroke-width="1" stroke-linecap="round" fill="none"/>''')
    for n in svg.iter():
        if n.get('stop-color')=='#fca5a5':n.set('stop-color','#ffd291')
