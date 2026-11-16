"""Convert attributed GLB/OBJ assets to bounded native triangle/texture packages.

Build-time tool only. Requires numpy and Pillow. Never modifies tracking poses.
"""
import argparse, io, json, struct
from pathlib import Path
import numpy as np
from PIL import Image

def glb(path):
    data=Path(path).read_bytes()
    magic,version,length=struct.unpack_from('<III',data)
    assert magic==0x46546c67 and version==2 and length==len(data)
    size,kind=struct.unpack_from('<II',data,12)
    doc=json.loads(data[20:20+size]); off=20+size
    size,kind=struct.unpack_from('<II',data,off); blob=data[off+8:off+8+size]
    def accessor(index):
        a=doc['accessors'][index]; v=doc['bufferViews'][a['bufferView']]
        dtype={5121:'u1',5123:'<u2',5125:'<u4',5126:'<f4'}[a['componentType']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        item=np.dtype(dtype).itemsize
        return np.ndarray((a['count'],width),dtype,blob,offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',width*item),item)).copy()
    def transform(n):
        if 'matrix' in n:return np.array(n['matrix']).reshape(4,4,order='F')
        x,y,z,w=n.get('rotation',[0,0,0,1]); m=np.eye(4)
        m[:3,:3]=np.array([[1-2*y*y-2*z*z,2*x*y-2*z*w,2*x*z+2*y*w],[2*x*y+2*z*w,1-2*x*x-2*z*z,2*y*z-2*x*w],[2*x*z-2*y*w,2*y*z+2*x*w,1-2*x*x-2*y*y]])@np.diag(n.get('scale',[1,1,1]))
        m[:3,3]=n.get('translation',[0,0,0]); return m
    batches=[]
    def walk(index,parent):
        node=doc['nodes'][index]; world=parent@transform(node)
        if 'mesh' in node:
            for p in doc['meshes'][node['mesh']]['primitives']:
                assert p.get('mode',4)==4
                attrs=p['attributes']; positions=accessor(attrs['POSITION'])
                positions=(np.c_[positions,np.ones(len(positions))]@world.T)[:,:3]
                uv=accessor(attrs['TEXCOORD_0']) if 'TEXCOORD_0' in attrs else np.zeros((len(positions),2))
                indices=accessor(p['indices']).ravel() if 'indices' in p else np.arange(len(positions))
                mat=doc.get('materials',[{}])[p.get('material',0)].get('pbrMetallicRoughness',{})
                color=np.tile(np.array(mat.get('baseColorFactor',[1,1,1,1]),dtype=float),(len(positions),1))
                # Mild baked surface shading keeps shape readable in the unlit diagnostic renderer.
                if 'NORMAL' in attrs:
                    normal=accessor(attrs['NORMAL'])@np.linalg.inv(world[:3,:3])
                    normal/=np.maximum(np.linalg.norm(normal,axis=1,keepdims=True),1e-10)
                    color[:,:3]*=(.7+.3*np.maximum(0,normal@np.array([.3,.8,.5])/np.sqrt(.98)))[:,None]
                texture=b''
                if 'baseColorTexture' in mat:
                    image=doc['images'][doc['textures'][mat['baseColorTexture']['index']]['source']]
                    v=doc['bufferViews'][image['bufferView']]; start=v.get('byteOffset',0)
                    output=io.BytesIO(); Image.open(io.BytesIO(blob[start:start+v['byteLength']])).convert('RGBA').save(output,format='PNG'); texture=output.getvalue()
                batches.append((np.c_[positions,color,uv][indices].astype('<f4'),texture))
        for child in node.get('children',[]):walk(child,world)
    for root in doc['scenes'][doc.get('scene',0)]['nodes']:walk(root,np.eye(4))
    return batches

def obj(path):
    path=Path(path); points=[]; uv=[]; vertices=[]
    for line in path.read_text().splitlines():
        fields=line.split()
        if not fields:continue
        if fields[0]=='v':points.append(list(map(float,fields[1:4])))
        elif fields[0]=='vt':uv.append(list(map(float,fields[1:3])))
        elif fields[0]=='f':
            for k in range(2,len(fields)-1):
                for token in (fields[1],fields[k],fields[k+1]):
                    ids=token.split('/'); t=uv[int(ids[1])-1] if len(ids)>1 and ids[1] else [0,0]
                    vertices.append(points[int(ids[0])-1]+[1,1,1,1]+[t[0],1-t[1]])
    texture=path.with_name('vr_tracker_vive_3_0_diff.png').read_bytes()
    return [(np.array(vertices,dtype='<f4'),texture)]

def write(path,batches):
    path=Path(path); path.parent.mkdir(parents=True,exist_ok=True)
    with path.open('wb') as f:
        f.write(struct.pack('<4sII',b'VDM1',1,len(batches)))
        for vertices,texture in batches:
            assert len(vertices)<=100000 and np.isfinite(vertices).all()
            f.write(struct.pack('<II',len(vertices),len(texture))); f.write(vertices.tobytes()); f.write(texture)

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source');p.add_argument('output');args=p.parse_args()
    batches=glb(args.source) if args.source.endswith('.glb') else obj(args.source)
    write(args.output,batches)
    print(f'{args.output}: {sum(len(v)//3 for v,t in batches)} triangles, {len(batches)} materials')
