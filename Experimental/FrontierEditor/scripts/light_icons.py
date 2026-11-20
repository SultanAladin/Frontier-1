"""Paired editor/HMI light symbols and dimensional fixture icons, native SVG."""
import math


def register_light_icons(icon):
    def pair(slug, name, description, flat, dimensional, color='#e9c67b'):
        key='light-'+slug
        icon(key+'-2d',name+' · 2D','Lights',description+' Flat, static symbol for editor or HMI controls.','',f'<g stroke="{color}" stroke-width="6" stroke-linecap="round" stroke-linejoin="round">{flat}</g>')
        defs=f'''<linearGradient id="{key}-metal" x2=".8" y2="1"><stop stop-color="#939ca7"/><stop offset=".25" stop-color="#535f6d"/><stop offset=".7" stop-color="#252f3d"/><stop offset="1" stop-color="#141d29"/></linearGradient><linearGradient id="{key}-silver" x2="1" y2=".3"><stop stop-color="#4a5661"/><stop offset=".27" stop-color="#d9dfe0"/><stop offset=".48" stop-color="#9aa8b4"/><stop offset=".76" stop-color="#e3e8e7"/><stop offset="1" stop-color="#556572"/></linearGradient><radialGradient id="{key}-glass" cx="32%" cy="24%" r="78%"><stop stop-color="#fffdf0"/><stop offset=".4" stop-color="{color}"/><stop offset=".77" stop-color="#8196a1"/><stop offset="1" stop-color="#34495e"/></radialGradient><linearGradient id="{key}-white" x2=".6" y2="1"><stop stop-color="#fffef4"/><stop offset=".45" stop-color="#d9e2e5"/><stop offset="1" stop-color="#8298ab"/></linearGradient><radialGradient id="{key}-glow"><stop stop-color="{color}" stop-opacity=".33"/><stop offset="1" stop-color="{color}" stop-opacity="0"/></radialGradient>'''
        # Symbol-specific strings use short local references; namespace every SVG.
        for token in ['metal','silver','glass','white','glow']:
            dimensional=dimensional.replace('url(#'+token+')','url(#'+key+'-'+token+')')
        icon(key+'-3d',name+' · 3D','Lights',description+' Dimensional companion with shaded native SVG surfaces.',defs,dimensional)

    bulb='M104 170v-12c-1-16-27-28-27-59a51 51 0 0 1 102 0c0 31-26 43-27 59v12Z'
    flat=f'<path d="{bulb}"/><path d="M105 183h46m-41 13h36m-29 14h22M116 168v-44l-12-14m36 58v-44l12-14m-36 14h24"/>'
    dim=f'''<circle cx="128" cy="103" r="76" fill="url(#glow)"/><path d="{bulb}" fill="url(#glass)" stroke="#f5dfb0" stroke-opacity=".55"/><path d="M94 102q-3-24 20-36" stroke="#fff9de" stroke-opacity=".65" stroke-width="5" stroke-linecap="round"/><path d="M116 168v-44l-10-14m34 58v-44l10-14m-34 14h24" stroke="#a06627" stroke-width="2"/><path d="M117 123h22" stroke="#fff5c2" stroke-width="2.4"/><path d="M103 169h50v27q0 8-25 8t-25-8Z" fill="url(#silver)"/><path d="m105 176 46-5m-46 14 46-5m-46 14 46-5" stroke="#354656" stroke-opacity=".6" stroke-width="3"/><path d="M116 203h24l-4 9h-16Z" fill="url(#metal)"/>'''
    pair('incandescent','Incandescent bulb','Warm filament bulb with a screw base.',flat,dim)

    flat='<path d="M77 109a51 51 0 0 1 102 0Z"/><path d="M82 121q8 20 23 37v14h46v-14q15-17 23-37M107 185h42m-35 13h28m-23 13h18M113 135v17m15-17v17m15-17v17"/>'
    dim='''<path d="M76 111a52 52 0 0 1 104 0Z" fill="url(#white)" stroke="#eef9fa" stroke-opacity=".3"/><path d="M78 111h100q-5 28-26 48v15h-48v-15q-21-20-26-48Z" fill="url(#white)"/><path d="M83 113h90" stroke="#647e92" stroke-opacity=".35"/><path d="m106 137 7 21m15-21v23m22-23-7 21" stroke="#7f98ac" stroke-opacity=".45" stroke-width="3" stroke-linecap="round"/><path d="M104 172h48v27q-23 12-48 0Z" fill="url(#silver)"/><path d="m106 180 44-4m-44 13 44-4m-44 13 44-4" stroke="#526572" stroke-width="2.7"/><path d="M117 204h22l-4 8h-14Z" fill="url(#metal)"/>'''
    pair('led-bulb','LED bulb','Frosted LED bulb with a ceramic heat-dissipating neck.',flat,dim,'#b5dbea')

    flat='<rect x="42" y="99" width="172" height="58" rx="13"/>'+''.join(f'<rect x="{x}" y="116" width="14" height="24" rx="3" fill="#bde4ee" stroke="none"/>' for x in [57,87,117,147,177])+'<path d="M59 79v-9m35 9V65m34 14V62m34 17V65m35 14v-9"/>'
    dim='''<g transform="rotate(-12 128 128)"><rect x="35" y="108" width="186" height="58" rx="15" fill="#14202b"/><rect x="35" y="98" width="186" height="58" rx="15" fill="url(#metal)" stroke="#a4b6c2" stroke-opacity=".4"/><rect x="43" y="107" width="170" height="39" rx="9" fill="#162b38"/>'''
    for x in [60,94,128,162,196]:
        dim+=f'<circle cx="{x}" cy="126" r="18" fill="url(#glow)"/><rect x="{x-9}" y="112" width="18" height="28" rx="5" fill="url(#glass)"/><rect x="{x-5}" y="115" width="10" height="20" rx="3" fill="#f5ffff" opacity=".7"/>'
    dim+='<path d="M46 102h162" stroke="#dfedf4" stroke-opacity=".35" stroke-width="1.3"/></g>'
    pair('led-strip','LED / DRL strip','A five-cell LED light bar for vehicle DRL or linear lighting.',flat,dim,'#bde4ee')

    rays=''
    for angle in range(0,360,45):
        rays+=f'<path d="M128 54v-17" transform="rotate({angle} 128 128)"/>'
    flat='<circle cx="128" cy="128" r="38"/>'+rays
    dim='<circle cx="128" cy="128" r="70" fill="url(#glow)"/><circle cx="128" cy="128" r="39" fill="url(#glass)" stroke="#fff3cc" stroke-opacity=".6"/>'
    for angle in range(0,360,45):
        dim+=f'<path d="M128 55V40" transform="rotate({angle} 128 128)" stroke="#d6b66e" stroke-width="6" stroke-linecap="round"/><path d="M127 54V41" transform="rotate({angle} 128 128)" stroke="#fff2b6" stroke-width="1.5" stroke-linecap="round"/>'
    pair('point','Point light','Omnidirectional editor light represented by a central emitter and radial rays.',flat,dim)

    flat='<path d="M49 87h42m-12-12 12 12-12 12M49 128h42m-12-12 12 12-12 12M49 169h42m-12-12 12 12-12 12"/><circle cx="162" cy="128" r="38"/><path d="M162 69V56m0 144v-13m59-59h-13m-12-41 10-10m-10 92 10 10"/>'
    dim='<circle cx="162" cy="128" r="67" fill="url(#glow)"/><circle cx="162" cy="128" r="39" fill="url(#glass)"/>'
    for y in [83,128,173]:
        dim+=f'<path d="M41 {y-4}h38v-9l18 13-18 13v-9H41Z" fill="url(#silver)" stroke="#cedbe2" stroke-opacity=".25"/>'
    dim+='<path d="M162 69V57m0 142v-12m59-59h-12m-13-41 9-9m-9 91 9 9" stroke="#dbbb7b" stroke-width="5" stroke-linecap="round"/>'
    pair('directional','Directional light','Parallel light rays for a sun-like directional editor light.',flat,dim)

    flat='<rect x="51" y="50" width="154" height="103" rx="8"/><path d="M72 134V70h112v64M128 153v42m-34 12h68M76 170l-15 24m119-24 15 24"/>'
    dim='''<path d="M122 153h12v48h-12Z" fill="url(#metal)"/><path d="M94 204q34-12 68 0v8H94Z" fill="url(#metal)"/><path d="m46 58 161-14 10 11v107L55 176l-9-10Z" fill="#17232e"/><path d="m46 58 161-14v108L46 166Z" fill="url(#metal)" stroke="#9badb8" stroke-opacity=".4"/><path d="m55 67 143-13v89L55 157Z" fill="url(#white)"/><path d="m64 74 126-11v73L64 148Z" fill="#f4f2df" opacity=".8"/><path d="m56 68 142-13" stroke="#fff" stroke-opacity=".6"/><path d="m85 67-10 78m44-81-10 78m44-81-10 78m44-81-10 78" stroke="#9aafb5" stroke-opacity=".1"/>'''
    pair('area','Area light','Rectangular soft-light panel for editor area lighting.',flat,dim,'#e6dba6')

    flat='<path d="m89 71 77 40-28 56-77-40Z"/><path d="m76 137-9 23 39 19 9-24m-28 16-17 36m-26 0h53m76-94 29-5m-34 25 33 9m-43 9 20 21"/>'
    dim='''<path d="m157 124 66-15-28 95-45-37Z" fill="url(#glow)"/><path d="m76 140-11 27 40 19 17-36" stroke="#283747" stroke-width="10" stroke-linejoin="round"/><path d="m76 140-11 27 40 19 17-36" stroke="#778a9a" stroke-opacity=".45" stroke-width="2"/><path d="m86 181-13 29H48v6h53v-6H84l12-24Z" fill="url(#metal)"/><path d="m76 64 86 40q18 9 4 38t-30 21l-87-41q-14-6-2-33t29-25Z" fill="url(#metal)"/><g transform="translate(151 133) rotate(26) scale(.48 1)"><circle r="34" fill="#16232e" stroke="#8ba3b6" stroke-width="3"/><circle r="27" fill="url(#glass)"/><circle r="20" fill="url(#white)" opacity=".65"/></g><path d="m67 78 63 30m-70-17 63 30m-70-17 63 30" stroke="#15222e" stroke-opacity=".5" stroke-width="3"/><circle cx="73" cy="156" r="6" fill="url(#silver)"/>'''
    pair('spot','Spotlight','A focused spotlight with an adjustable yoke and a recessed reflector.',flat,dim,'#e9d6a0')

    for kind,name,color in [('low-beam','Low beam','#71d29d'),('high-beam','High beam','#73aaf5'),('fog','Front fog light','#82c69b')]:
        flat='<path d="M144 77v102q60-3 60-51t-60-51Z"/>'
        dim='<path d="M144 72v112q70-1 70-55t-70-57Z" fill="url(#metal)" stroke="#9cabb4" stroke-opacity=".4"/><path d="M151 82v92q53-2 53-45t-53-47Z" fill="url(#glass)"/><path d="M158 90q36 5 39 32" stroke="#f0faff" stroke-opacity=".62" stroke-width="3" stroke-linecap="round"/>'
        for i in range(4):
            y=85+i*28
            dy=0 if kind=='high-beam' else 18
            flat+=f'<path d="M54 {y+dy} 120 {y}"/>'
            dim+=f'<path d="M53 {y+dy} 120 {y}" stroke="{color}" stroke-width="5" stroke-linecap="round"/><path d="M54 {y+dy-1} 119 {y-1}" stroke="#ebffff" stroke-opacity=".4" stroke-width="1" stroke-linecap="round"/>'
        for x in [164,176,188]:
            dim+=f'<path d="M{x} 96q10 31 0 64" stroke="#496578" stroke-opacity=".3" stroke-width="1"/>'
        if kind=='fog':
            # Broken beam / fog crossing is deliberately distinct from dipped beam.
            wave='M87 68c-19 20 19 20 0 40s19 20 0 40 19 20 0 40'
            flat+=f'<path d="{wave}" stroke="#182c25" stroke-width="12"/><path d="{wave}"/>'
            dim+=f'<path d="{wave}" stroke="#162a22" stroke-width="10"/><path d="{wave}" stroke="{color}" stroke-width="4"/>'
        pair(kind,name,{'low-beam':'Green dipped-headlamp symbol with downward-sloping beams.','high-beam':'Blue high-beam headlamp symbol with parallel horizontal beams.','fog':'Front fog-lamp symbol with sloping rays crossed by a fog line.'}[kind],flat,dim,color)
