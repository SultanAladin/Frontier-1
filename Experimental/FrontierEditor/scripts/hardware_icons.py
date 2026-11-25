"""Dimensional hardware and audio assets; explicit depth, pure vector surfaces."""
import math

def register_hardware_icons(icon):
    def make(key,name,description,body,extra='',flat=False):
        defs='' if flat else f'''<linearGradient id="{key}-edge" x2=".8" y2="1"><stop stop-color="#52616e"/><stop offset=".5" stop-color="#263441"/><stop offset="1" stop-color="#15202b"/></linearGradient><linearGradient id="{key}-face" x2=".8" y2="1"><stop stop-color="#899aa5"/><stop offset=".45" stop-color="#536673"/><stop offset="1" stop-color="#374956"/></linearGradient><linearGradient id="{key}-silver" x2="1" y2=".7"><stop stop-color="#f0f4ee"/><stop offset=".3" stop-color="#acbac3"/><stop offset=".54" stop-color="#e3e9e7"/><stop offset=".8" stop-color="#8798a5"/><stop offset="1" stop-color="#c0cbd0"/></linearGradient><radialGradient id="{key}-black" cx="32%" cy="25%" r="80%"><stop stop-color="#657480"/><stop offset=".45" stop-color="#303e4b"/><stop offset="1" stop-color="#0e1923"/></radialGradient><linearGradient id="{key}-gold" x2=".7" y2="1"><stop stop-color="#ffe4a1"/><stop offset=".4" stop-color="#eec278"/><stop offset="1" stop-color="#a4652e"/></linearGradient><filter id="{key}-contact" x="-25%" y="-25%" width="150%" height="160%"><feGaussianBlur stdDeviation="1.4"/></filter>'''
        defs+=extra
        for token in ['edge','face','silver','black','gold','contact']:
            body=body.replace('url(#'+token+')',f'url(#{key}-{token})')
        icon(key,name,'Objects',description,defs,body)

    # Local square top plane: the same affine projection is used for chassis,
    # platter, arm and screws. Thickness is an actual lower translated face.
    body='''<g transform="translate(0 22)"><g transform="matrix(.9 .53 -.67 .65 113 -8)"><rect x="22" y="24" width="170" height="196" rx="16" fill="url(#edge)"/></g></g><g transform="matrix(.9 .53 -.67 .65 113 -8)"><rect x="22" y="24" width="170" height="196" rx="16" fill="url(#face)" stroke="#c9d4d8" stroke-opacity=".4" stroke-width="1"/><rect x="34" y="37" width="146" height="169" rx="13" fill="#101f2c" stroke="#a8bcc7" stroke-opacity=".35"/>
<circle cx="107" cy="111" r="65" fill="#050e18"/><circle cx="107" cy="106" r="62" fill="#596c7b"/><circle cx="107" cy="103" r="62" fill="url(#silver)" stroke="#e3ecee" stroke-width="1.1"/><circle cx="107" cy="104" r="57" fill="none" stroke="#dce4e6" stroke-opacity=".35" stroke-width=".55"/><circle cx="107" cy="105" r="24" fill="#152430"/><circle cx="107" cy="100" r="23" fill="url(#face)" stroke="#e0e8e8" stroke-opacity=".6" stroke-width=".8"/><circle cx="107" cy="100" r="7" fill="#162a39" stroke="#c9d7da" stroke-width="1.4"/>'''
    for a in [0,90,180,270]:
        x=107+15*math.cos(math.radians(a));y=100+15*math.sin(math.radians(a))
        body+=f'<circle cx="{x}" cy="{y}" r="3.2" fill="#0c1c29" stroke="#cad9dd" stroke-width=".7"/>'
    body+='''<path d="M71 177q-14 3-17-10-4-16 8-23l40-23q7-3 9 3l-26 43q-5 8-14 10Z" fill="#020a12" opacity=".65" filter="url(#contact)"/><path d="M69 175q-14 3-17-10-4-16 8-23l40-23q7-3 9 3l-26 43q-5 8-14 10Z" fill="#935022"/><path d="M69 171q-14 3-17-10-4-16 8-23l40-23q7-3 9 3l-26 43q-5 8-14 10Z" fill="url(#gold)" stroke="#fff2bf" stroke-opacity=".8" stroke-width="1"/><circle cx="66" cy="153" r="7" fill="#162a38"/><circle cx="66" cy="151" r="6" fill="url(#face)" stroke="#cbdadd" stroke-width="1"/><rect x="123" y="174" width="32" height="24" rx="4" fill="#0d1a25"/><rect x="121" y="172" width="32" height="24" rx="4" fill="url(#black)"/>'''
    for x,y in [(31,40),(183,40),(31,204),(183,204)]:
        body+=f'<circle cx="{x}" cy="{y+2}" r="6" fill="#172630"/><circle cx="{x}" cy="{y}" r="5.5" fill="url(#face)" stroke="#d2dce1" stroke-opacity=".65" stroke-width=".8"/>'
    body+='''</g><g transform="matrix(.9 .53 0 1 -34 140)"><rect x="105" y="0" width="44" height="14" rx="3" fill="#0c1720" stroke="#80919c" stroke-opacity=".5"/><path d="M112 4h9v6h-9Zm16 0h9v6h-9Z" fill="url(#gold)"/></g>'''
    # Sweep the rounded casing edge vertically, closing the sidewalls between
    # the top and bottom faces instead of leaving two floating flat plates.
    back='<g transform="translate(0 22)"><g transform="matrix(.9 .53 -.67 .65 113 -8)"><rect x="22" y="24" width="170" height="196" rx="16" fill="url(#edge)"/></g></g>'
    sides=''.join(f'<g transform="translate(0 {depth})"><g transform="matrix(.9 .53 -.67 .65 113 -8)"><rect x="22" y="24" width="170" height="196" rx="16" fill="#283b4a"/></g></g>' for depth in range(22,0,-1))
    body=body.replace(back,sides)
    make('hard-drive','Hard drive','An open hard drive with a brushed platter, recessed spindle, raised gold actuator and a thick graphite enclosure.', '<g transform="translate(32 8) scale(.8)">'+body+'</g>')

    extra='''<linearGradient id="arcade-joystick-purple" x2=".7" y2="1"><stop stop-color="#8476ad"/><stop offset=".5" stop-color="#58457f"/><stop offset="1" stop-color="#34204e"/></linearGradient><radialGradient id="arcade-joystick-pink" cx="30%" cy="23%" r="78%"><stop stop-color="#ff9bc9"/><stop offset=".3" stop-color="#ff429e"/><stop offset=".72" stop-color="#d81376"/><stop offset="1" stop-color="#8f0d53"/></radialGradient><radialGradient id="arcade-joystick-boot" cx="30%" cy="20%"><stop stop-color="#86769f"/><stop offset=".5" stop-color="#52376d"/><stop offset="1" stop-color="#291938"/></radialGradient>'''
    body='''<path d="M35 144v43q0 10 10 15l67 32q15 7 28 0l69-32q10-5 10-16v-43Z" fill="url(#arcade-joystick-purple)"/><path d="m42 132 71-34q14-7 28 0l71 34q14 7 0 15l-73 35q-12 6-24 0l-73-35q-14-7 0-15Z" fill="url(#arcade-joystick-purple)" stroke="#bda4df" stroke-opacity=".3" stroke-width="1.2"/><path d="M36 147v40q0 9 11 14l68 32q5 2 10 3v-50Z" fill="#21122d" opacity=".2"/><g transform="translate(128 140) scale(1 .46)"><circle r="38" fill="#251432" opacity=".45" filter="url(#contact)"/></g><path d="M91 138q3-38 37-38t37 38q-37 19-74 0Z" fill="url(#arcade-joystick-boot)"/><path d="M122 73h13v42q-6 5-13 0Z" fill="url(#silver)"/><circle cx="128" cy="58" r="34" fill="url(#arcade-joystick-pink)"/><g transform="translate(64 145) scale(1 .55)"><circle cy="2" r="10" fill="#291735"/><circle r="9" fill="url(#arcade-joystick-pink)"/></g>'''
    make('arcade-joystick','Arcade joystick','A new purple arcade controller with a hot-pink spherical grip, steel shaft, sculpted rubber boot and one pink button.',body,extra)

    extra='''<linearGradient id="microphone-shell" x2="1" y2=".12"><stop stop-color="#18212b"/><stop offset=".27" stop-color="#758087"/><stop offset=".44" stop-color="#3b4751"/><stop offset=".7" stop-color="#141d27"/><stop offset=".87" stop-color="#3b4e60"/><stop offset="1" stop-color="#0a141e"/></linearGradient><clipPath id="microphone-grille"><path d="M91 97V63q0-38 37-38t37 38v34Z"/></clipPath>'''
    body='''<g transform="translate(128 222) scale(1 .28)"><circle cy="15" r="58" fill="url(#edge)"/><circle r="58" fill="url(#face)" stroke="#b5c3cc" stroke-opacity=".45" stroke-width="1"/></g><path d="M117 171h22v42q-11 5-22 0Z" fill="url(#microphone-shell)"/><g transform="translate(128 214) scale(1 .33)"><circle r="15" fill="url(#silver)"/><circle cy="-3" r="12" fill="url(#face)"/></g><path d="M80 117v23q0 49 48 49t48-49v-23" fill="none" stroke="#0b1621" stroke-width="9"/><path d="M79 117v23q0 47 49 47t49-47v-23" fill="none" stroke="url(#silver)" stroke-width="3"/>
<path d="M91 91h74v44q0 40-37 40t-37-40Z" fill="url(#microphone-shell)"/><path d="M91 97V63q0-38 37-38t37 38v34Z" fill="url(#black)"/><g clip-path="url(#microphone-grille)">'''
    for x in range(88,170,3):
        body+=f'<path d="M{x} 24q-8 32-3 75" stroke="#a6b0b4" stroke-opacity=".38" stroke-width=".65"/>'
    for y in range(27,100,3):
        body+=f'<path d="M88 {y}q40-9 81 0" stroke="#a9b1b4" stroke-opacity=".33" stroke-width=".65"/>'
    body+='''</g><path d="M90 95q38-7 76 0v12q-38-6-76 0Z" fill="url(#microphone-shell)" stroke="#b8c4ca" stroke-opacity=".25" stroke-width=".7"/><g transform="translate(82 121) scale(.55 1)"><circle r="11" fill="#08141f"/><circle cx="-3" r="10" fill="url(#microphone-shell)"/></g><g transform="translate(172 121) scale(.55 1)"><circle r="11" fill="#08141f"/><circle cx="3" r="10" fill="url(#face)" stroke="#9dafbd" stroke-opacity=".4" stroke-width=".7"/></g>'''
    make('microphone','Microphone','A dark studio microphone with a curved woven grille, metal capsule, pivoting yoke, cylindrical stem and weighted circular base.',body,extra)

    from compute_icons import register_compute_icons
    register_compute_icons(icon)
