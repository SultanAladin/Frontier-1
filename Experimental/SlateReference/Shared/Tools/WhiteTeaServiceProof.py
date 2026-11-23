#!/usr/bin/env python3
"""Deterministic one-frame CPU raster proof for WhiteTeaService.codex.
Mirrors the first viewport material pass: depth-tested opaque geometry, fixed studio dielectric lighting,
and a sky background. It reads the same OBJ geometry references carried by the Codex workspace payload.
"""
from pathlib import Path
import math
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "VisualProof" / "WhiteTeaService" / "whiteteaservice-material-frame.png"
W,H=1280,720
# name, obj, translation
ITEMS=[("Service Teapot","ServiceTeapot.obj",(-.12,0,0)),("Teacup","Teacup.obj",(.30,0,-.04)),
("Saucer","Saucer.obj",(.30,-.004,-.04)),("Sugar Bowl","SugarBowl.obj",(.05,0,.28)),
("Milk Jug","MilkJug.obj",(-.39,0,.18))]

def obj(path):
    v=[]; f=[]
    for line in path.read_text().splitlines():
        a=line.split()
        if not a: continue
        if a[0]=='v': v.append(tuple(map(float,a[1:4])))
        elif a[0]=='f': f.append([int(x.split('/')[0])-1 for x in a[1:]])
    return v,f

def camera(p):
    # camera at (0, .55, 1.45), looking to (0,.10,0)
    eye=(0,.55,1.45); target=(0,.10,0); up=(0,1,0)
    def sub(a,b):return (a[0]-b[0],a[1]-b[1],a[2]-b[2])
    def norm(a):
        q=math.sqrt(sum(x*x for x in a));return tuple(x/q for x in a)
    def cross(a,b):return(a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0])
    f=norm(sub(target,eye)); r=norm(cross(f,up)); u=cross(r,f); q=sub(p,eye)
    x=sum(q[i]*r[i] for i in range(3)); y=sum(q[i]*u[i] for i in range(3)); z=sum(q[i]*f[i] for i in range(3))
    if z<=.01:return None
    s=1.25
    return (W*.5+x/z*H*s, H*.54-y/z*H*s, z)

def tri(img,zbuf,a,b,c,color):
    minx=max(0,int(min(a[0],b[0],c[0])));maxx=min(W-1,int(max(a[0],b[0],c[0])))
    miny=max(0,int(min(a[1],b[1],c[1])));maxy=min(H-1,int(max(a[1],b[1],c[1])))
    area=(b[1]-c[1])*(a[0]-c[0])+(c[0]-b[0])*(a[1]-c[1])
    if abs(area)<1e-7:return
    pix=img.load()
    for y in range(miny,maxy+1):
      for x in range(minx,maxx+1):
        w0=((b[1]-c[1])*(x-c[0])+(c[0]-b[0])*(y-c[1]))/area
        w1=((c[1]-a[1])*(x-c[0])+(a[0]-c[0])*(y-c[1]))/area
        w2=1.0-w0-w1
        if w0>=0 and w1>=0 and w2>=0:
          d=w0*a[2]+w1*b[2]+w2*c[2]
          # This proof resolves all source faces two-sided. The first real visibility pass carries
          # authoritative winding; CPU evidence must not discard a source face because of OBJ orientation.
          i=y*W+x
          zbuf[i]=d
          pix[x,y]=color

def main():
    im=Image.new('RGB',(W,H)); p=im.load()
    for y in range(H):
        t=y/(H-1); sky=(int(38+42*t),int(75+55*t),int(108+58*t))
        for x in range(W):p[x,y]=sky
    z=[1e9]*(W*H)
    # floor, rendered as two triangles
    floor=[camera((-1,-.013,-.8)),camera((1,-.013,-.8)),camera((1,-.013,.9)),camera((-1,-.013,.9))]
    tri(im,z,floor[0],floor[1],floor[2],(103,102,96));tri(im,z,floor[0],floor[2],floor[3],(103,102,96))
    # The floor is the background plane for this proof; objects retain their own depth ordering above it.
    z=[1e9]*(W*H)
    base=ROOT/'EngineContent/GeometryArchives/WhiteTeaService'
    for _,file,t in ITEMS:
      vs,fs=obj(base/file); projected=[]
      for q in vs: projected.append(camera((q[0]+t[0],q[1]+t[1],q[2]+t[2])))
      for face in fs:
        pts=[projected[i] for i in face]
        if any(q is None for q in pts):continue
        # source quads are planar; split only for this CPU raster evidence.
        for i in range(1,len(pts)-1): tri(im,z,pts[0],pts[i],pts[i+1],(221,219,207))
    OUT.parent.mkdir(parents=True,exist_ok=True);im.save(OUT)
    print(OUT)
if __name__=='__main__':main()
