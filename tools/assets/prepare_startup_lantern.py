"""Offline conversion of Poly Haven Wooden Lantern 01's glTF into Stack's mesh.

Usage: python tools/assets/prepare_startup_lantern.py source.gltf Assets/Startup/lantern.mesh
Only translation-only nodes and triangle primitives are accepted.
The runtime does not load glTF or need this script. UVs retain glTF's top-down convention.
"""
import json
import struct
import sys
from pathlib import Path

source, output = map(Path, sys.argv[1:])
doc = json.loads(source.read_text())
buffers = [(source.parent / b['uri']).read_bytes() for b in doc['buffers']]

def accessor(i):
    a = doc['accessors'][i]
    v = doc['bufferViews'][a['bufferView']]
    fmt = {5126: 'f', 5123: 'H', 5125: 'I'}[a['componentType']]
    n = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
    size = struct.calcsize('<'+fmt*n)
    start = v.get('byteOffset', 0)+a.get('byteOffset', 0)
    return [struct.unpack_from('<'+fmt*n, buffers[v['buffer']], start+j*v.get('byteStride',size))
            for j in range(a['count'])]

vertices = []
bounds = [[], []]
for node in doc['nodes']:
    assert not any(k in node for k in ('matrix', 'rotation', 'scale', 'children'))
    part = int('handle' in node['name'])
    trans = node.get('translation', [0, 0, 0])
    for p in doc['meshes'][node['mesh']]['primitives']:
        assert p.get('mode', 4) == 4
        attrs = p['attributes']
        pos, norm, uv = [accessor(attrs[k]) for k in ('POSITION','NORMAL','TEXCOORD_0')]
        for (i,) in accessor(p['indices']):
            xyz = tuple(pos[i][j]+trans[j] for j in range(3))
            bounds[part].append(xyz)
            vertices.append((*xyz, *norm[i], *uv[i], float(part), float(p['material'])))
with output.open('wb') as f:
    f.write(struct.pack('<8sI', b'STKLANT1', len(vertices)))
    for v in vertices: f.write(struct.pack('<10f', *v))
print(f'{len(vertices)//3} triangles, {output.stat().st_size} bytes')
for i, points in enumerate(bounds):
    print('handle' if i else 'body', [(min(p[j] for p in points),max(p[j] for p in points)) for j in range(3)])
