"""Colored GPU/RAM symbols, a dimensional GPU and geographic Earth SVG."""
import math


def fan(x,y,dim=False):
    rim='url(#gpu3-ring)' if dim else '#152b3d'
    blade='url(#gpu3-blade)' if dim else '#8ca8b8'
    s=f'<circle cx="{x}" cy="{y}" r="32" fill="{rim}"/><circle cx="{x}" cy="{y}" r="27.5" fill="#0c1b28"/>'
    # Swept, tapered vanes rather than the old petal-shaped blades.
    for a in range(0,360,40):
        s+=f'<path d="M{x-3} {y-8}C{x-18} {y-23} {x-20} {y-16} {x-22} {y-8}Q{x-12} {y-14} {x-6} {y+2}Z" transform="rotate({a} {x} {y})" fill="{blade}"/>'
    s+=f'<circle cx="{x}" cy="{y}" r="8" fill="'+('url(#gpu3-hub)' if dim else '#3e6278')+'"/>'
    if dim:s+=f'<circle cx="{x}" cy="{y}" r="7.5" fill="none" stroke="#a4bccb" stroke-opacity=".24" stroke-width=".65"/>'
    return s


def register_compute_icons(icon):
    # Compact clipped shroud and swept blades, without returning to tiny details.
    flat='''<path d="M30 72h17v9h-7v105H30Z" fill="#aebcc6"/><path d="M43 163h161v15H43Z" fill="#4f8678"/><path d="M80 178h31v12H80Zm39 0h66v12h-66Z" fill="#d6b26a"/><path d="M43 84h158l22 19v53l-15 15H43Z" fill="#37576e"/><path d="M49 91h147l18 15v45l-12 12H49Z" fill="#4d7087"/>
<path d="M155 101h35l14 12v32l-9 9h-40Z" fill="#2a4355"/><circle cx="100" cy="127" r="34" fill="#203340"/><circle cx="100" cy="127" r="29" fill="#132731"/>'''
    for angle in range(0,360,60):
        flat+=f'<path d="M97 119C90 115 85 106 90 100Q102 98 110 106L107 118Z" transform="rotate({angle} 100 127)" fill="#9bb7c6"/>'
    flat+='<circle cx="100" cy="127" r="9" fill="#476b81"/>'
    for y in [115,127,139]:flat+=f'<path d="M165 {y}h25" stroke="#89a9bb" stroke-width="4" stroke-linecap="round"/>'
    flat+='<path d="M49 93h17m74 0h48" stroke="#8ebbc8" stroke-width="3" stroke-linecap="round"/>'
    icon('graphics-card-2d','Graphics card · 2D','Objects','A compact flat GPU with clipped shroud corners, six swept fan blades, a recessed vent panel and a keyed gold edge connector. Restrained solid colors; no gradients or fine circuit details.','',flat)

    defs='''<linearGradient id="gpu3-shroud" x2=".6" y2="1"><stop stop-color="#a7b4bd"/><stop offset=".15" stop-color="#687e8e"/><stop offset=".5" stop-color="#384e60"/><stop offset="1" stop-color="#1b2c3e"/></linearGradient><linearGradient id="gpu3-edge" x2=".5" y2="1"><stop stop-color="#516577"/><stop offset="1" stop-color="#142535"/></linearGradient><linearGradient id="gpu3-blade" x2=".7" y2="1"><stop stop-color="#8b9eaa"/><stop offset=".36" stop-color="#4f6779"/><stop offset="1" stop-color="#1a3042"/></linearGradient><radialGradient id="gpu3-ring"><stop offset=".78" stop-color="#071422"/><stop offset=".86" stop-color="#091c2c"/><stop offset=".93" stop-color="#8c9daa"/><stop offset="1" stop-color="#213b4e"/></radialGradient><radialGradient id="gpu3-hub" cx="30%" cy="20%"><stop stop-color="#8c9fac"/><stop offset=".55" stop-color="#3c5367"/><stop offset="1" stop-color="#14293d"/></radialGradient><linearGradient id="gpu3-metal" x2="1" y2=".3"><stop stop-color="#637686"/><stop offset=".3" stop-color="#dde6e8"/><stop offset=".65" stop-color="#95a8b6"/><stop offset="1" stop-color="#556c7e"/></linearGradient><linearGradient id="gpu3-gold" x2="0" y2="1"><stop stop-color="#ffe5a1"/><stop offset="1" stop-color="#bd843d"/></linearGradient>'''
    body='''<g transform="matrix(.96 -.18 0 1 7 25)"><path d="M40 75h172l18 13v94H58l-18-14Z" fill="url(#gpu3-edge)"/><path d="m40 75 17-9h159l14 22-18-5Z" fill="url(#gpu3-metal)"/><path d="M48 167h159v10H48Z" fill="#285044"/><path d="M87 177h34v11H87Zm39 0h52v11h-52Z" fill="url(#gpu3-gold)"/>'''
    for x in range(90,178,5):
        if x not in [120,125]:body+=f'<path d="M{x} 180v7" stroke="#815d31" stroke-width="1"/>'
    body+='<path d="M215 92h12v77h-12Z" fill="#0f2332"/>'
    for y in range(97,166,5):body+=f'<path d="M216 {y}h10" stroke="#7b939f" stroke-width="1.3"/>'
    body+='''<path d="M40 79h169q7 0 7 8v72q0 8-7 8H40Z" fill="url(#gpu3-shroud)" stroke="#aebec7" stroke-opacity=".4" stroke-width=".8"/><path d="m48 81 46 0-10 8H48Zm107 0h53v8h-44Z" fill="#77ccdb"/><path d="m48 158 44 0 9 7H48Zm112 0h47v7h-55Z" fill="#224457"/><path d="M116 90h8v66h-8Z" fill="#172d3e"/>'''
    body+=fan(82,124,True)+fan(174,124,True)
    body+='''<path d="M34 66h8v122h-8Zm-9 0h18v7H25Z" fill="url(#gpu3-metal)"/><path d="M36 91v22m0 8v22" stroke="#2b4356" stroke-width="2"/><path d="M48 80h161" stroke="#eff7f6" stroke-opacity=".5" stroke-width=".7"/><path d="M220 147v13" stroke="#6be2d0" stroke-width="2"/></g>'''
    icon('graphics-card-3d','Graphics card · 3D','Objects','Reworked GPU with bevelled blue-metal shroud, recessed swept-blade fans, exposed heatsink fins, layered PCB and keyed gold contacts.',defs,body)

    # Solid memory board, three chips and grouped contacts; no pin/trace clutter.
    body='''<path d="M34 91h188v23h-6v14h6v37H34v-37h6v-14h-6Z" fill="#548f77"/><path d="M46 165h88v16H46Zm99 0h65v16h-65Z" fill="#d8b76c"/>'''
    for x in [57,105,153]:
        body+=f'<rect x="{x}" y="107" width="36" height="42" rx="4" fill="#263d40"/>'
    for x in [58,72,86,100,114,158,172,186,200]:
        body+=f'<path d="M{x} 168v10" stroke="#548f77" stroke-width="2"/>'
    icon('ram-2d','RAM · 2D','Objects','A simplified flat memory module with three broad chips, a green notched board and keyed gold contacts. No chip pins, circuit traces or mounting details.','',body)

    # Orthographic geographic projection, centered on Africa/Europe (20° E).
    # Coastlines are simplified lon/lat polygons, not arbitrary decorative blobs.
    def project(lon,lat):
        a=math.radians(lon-20);b=math.radians(lat);t=math.radians(10)
        x=math.cos(b)*math.sin(a);y=math.cos(t)*math.sin(b)-math.sin(t)*math.cos(b)*math.cos(a)
        z=math.sin(t)*math.sin(b)+math.cos(t)*math.cos(b)*math.cos(a)
        if z<0:
            n=math.hypot(x,y);x/=n;y/=n
        return 128+84*x,128-84*y
    def polygon(points):
        dense=[]
        for i,(a,b) in enumerate(points):
            c,d=points[(i+1)%len(points)]
            for j in range(4):dense.append(project(a+(c-a)*j/4,b+(d-b)*j/4))
        return ' '.join(('M' if i==0 else 'L')+f'{x:.2f} {y:.2f}' for i,(x,y) in enumerate(dense))+'Z'
    continents=[
      [(-17,15),(-17,22),(-13,28),(-9,31),(-6,36),(2,37),(10,37),(12,33),(20,32),(25,32),(32,31),(35,25),(36,19),(43,12),(51,11),(49,5),(42,-1),(40,-10),(35,-18),(33,-26),(27,-33),(18,-35),(14,-28),(12,-18),(12,-6),(9,3),(2,5),(-5,5),(-10,7)],
      [(-10,36),(-9,43),(-2,44),(-5,48),(1,51),(6,54),(9,55),(11,58),(8,58),(5,61),(7,67),(17,71),(29,71),(32,65),(26,60),(30,58),(39,60),(45,55),(37,49),(32,45),(29,41),(26,40),(24,37),(21,37),(20,40),(16,42),(16,39),(13,42),(9,44),(3,42),(0,38)],
      [(30,71),(55,73),(85,75),(110,70),(140,60),(150,50),(130,42),(122,32),(115,22),(109,19),(107,10),(103,3),(100,8),(98,17),(92,22),(88,21),(80,8),(76,10),(72,20),(67,24),(59,25),(55,26),(50,30),(45,30),(39,35),(35,36),(30,41),(37,46),(43,52),(36,59)],
      [(35,30),(43,30),(49,24),(56,23),(52,17),(45,12),(42,15),(38,23)],
      [(-81,10),(-72,12),(-63,10),(-53,5),(-50,0),(-35,-5),(-38,-15),(-43,-23),(-49,-29),(-54,-36),(-63,-47),(-68,-55),(-73,-50),(-74,-33),(-80,-5)],
      [(-8,50),(-6,54),(-8,58),(-4,59),(-2,55),(1,52)],
      [(-52,60),(-44,61),(-22,70),(-25,80),(-45,83),(-62,76)],
      [(49,-13),(51,-16),(48,-25),(44,-25),(44,-20)],
    ]
    defs='''<radialGradient id="earth-ocean" cx="28%" cy="24%" r="85%"><stop stop-color="#66c1e4"/><stop offset=".35" stop-color="#2789bd"/><stop offset=".7" stop-color="#165786"/><stop offset="1" stop-color="#09253f"/></radialGradient><linearGradient id="earth-land" x2=".4" y2="1"><stop stop-color="#a9c49c"/><stop offset=".3" stop-color="#80a88c"/><stop offset=".48" stop-color="#c1b47c"/><stop offset=".64" stop-color="#638f6a"/><stop offset="1" stop-color="#7f9b79"/></linearGradient><radialGradient id="earth-shade" cx="30%" cy="25%" r="77%"><stop offset=".3" stop-color="#081b36" stop-opacity="0"/><stop offset=".7" stop-color="#081b36" stop-opacity=".1"/><stop offset="1" stop-color="#020d25" stop-opacity=".8"/></radialGradient><radialGradient id="earth-limb"><stop offset=".91" stop-color="#89dfff" stop-opacity="0"/><stop offset=".98" stop-color="#8cddf6" stop-opacity=".2"/><stop offset="1" stop-color="#c2f1fd" stop-opacity=".55"/></radialGradient><clipPath id="earth-disc"><circle cx="128" cy="128" r="84"/></clipPath>'''
    body='<circle cx="128" cy="128" r="84" fill="url(#earth-ocean)"/><g clip-path="url(#earth-disc)">'
    for points in continents:body+=f'<path d="{polygon(points)}" fill="url(#earth-land)" stroke="#d0d5a5" stroke-opacity=".23" stroke-width=".6"/>'
    body+='''<path d="M58 93q11-25 41-28m-47 91q13-13 29-5m68-89q37 0 47 24m-62 104q20 3 39-10m-63-70q21-4 39 5" fill="none" stroke="#eef9fb" stroke-opacity=".48" stroke-width="3.2" stroke-linecap="round"/><path d="M61 95q12-26 39-25m48-5q31 1 45 23m-56 105q19 1 34-9" fill="none" stroke="#e4f7fa" stroke-opacity=".3" stroke-width="1.3"/><circle cx="128" cy="128" r="84" fill="url(#earth-shade)"/><circle cx="128" cy="128" r="84" fill="url(#earth-limb)"/></g>'''
    icon('earth','World / Earth','Celestial','A shaded blue Earth with geographically projected, simplified continents, fine cloud bands and a restrained atmospheric rim.',defs,body)
