#!/usr/bin/env python3
"""Analytic pine LOD meshes for the packed-foliage engine regression/demo. No external art."""
from pathlib import Path
import math,json,hashlib
root=Path(__file__).resolve().parents[2]/'Projects/MapWorkshop/Assets'
def tree(name,segments,levels):
    triangles=[]
    def add(a,b,c):triangles.append((a,b,c))
    def cone(y,height,radius):
        top=(0,y+height,0);center=(0,y,0)
        for i in range(segments):
            a=2*math.pi*i/segments;b=2*math.pi*(i+1)/segments
            p=(radius*math.cos(a),y,radius*math.sin(a));q=(radius*math.cos(b),y,radius*math.sin(b))
            add(p,top,q);add(center,p,q)
    # A narrow lower cone forms the trunk; upper tiers form the crown.
    cone(0,1.5,.18)
    for i in range(levels):cone(.8+i*.65,2.5-i*.45,1.2-i*.22)
    text=['# deterministic engine LOD fixture, metres']
    for tri in triangles:
        a,b,c=tri;u=[b[i]-a[i] for i in range(3)];v=[c[i]-a[i] for i in range(3)]
        n=(u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]);length=math.sqrt(sum(x*x for x in n));n=tuple(x/length for x in n)
        for point in tri:text.append('v '+' '.join(f'{x:.7f}' for x in point))
        text.append('vn '+' '.join(f'{x:.7f}' for x in n))
    for i in range(len(triangles)):text.append(f'f {i*3+1}//{i+1} {i*3+2}//{i+1} {i*3+3}//{i+1}')
    relative=f'Models/Foliage/{name}.obj';p=root/relative;p.parent.mkdir(parents=True,exist_ok=True);p.write_text('\n'.join(text)+'\n')
    p.with_suffix('.obj.zmeta').write_text(json.dumps({'UUID':hashlib.sha1(('MapWorkshop/'+relative).encode()).hexdigest()[:16],'Type':'Model','Import':{}},indent=4)+'\n')
    print(name,len(triangles),'triangles')
if __name__=='__main__':tree('pine-near',12,3);tree('pine-far',4,1)
