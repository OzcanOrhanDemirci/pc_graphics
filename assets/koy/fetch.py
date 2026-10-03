"""Koy's CC0 scans: downloads them, checks every file's SHA-256, puts them where the scene reads them (assets/koy/cc0/).

  python assets/koy/fetch.py            re-runnable: skips what is there and correct

The files are not committed (CC0, but large); this script and LICENSES.md are. Sources and why each was chosen:
docs/koy/VERI.md. Every hash was pinned by the K0 research pass (2026-10-03) and checked against the provider's own md5.
"""
import hashlib
import io
import json
import os
import struct
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'cc0')
DL = os.path.join(HERE, '_dl')

# name -> (url, sha256)
FILES = {
    'Ground054_2K-PNG.zip': ('https://ambientcg.com/get?file=Ground054_2K-PNG.zip',
                             '9d400b304ff2c9b18cf1a9abddfd98d33dba7a7beb5f76acca4da81f37476032'),
    'boulder_01_2k.gltf': ('https://dl.polyhaven.org/file/ph-assets/Models/gltf/2k/boulder_01/boulder_01_2k.gltf',
                           '52d118b56748a18509e4ec0075d07b254d72a0401793f22363c0bb3a682e7361'),
    'boulder_01.bin': ('https://dl.polyhaven.org/file/ph-assets/Models/gltf/8k/boulder_01/boulder_01.bin',
                       '7f5b06503d62bd95bfe9c6b8a354a5cc8ff04a4d271a8cf87a3e3125db4232d7'),
    'boulder_01_diff_2k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Models/jpg/2k/boulder_01/boulder_01_diff_2k.jpg',
                               '90bbaa17c1fe0254d2b0b6e5148264fa834953a72724d566f14af029eebe3ca3'),
    'boulder_01_nor_gl_2k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Models/jpg/2k/boulder_01/boulder_01_nor_gl_2k.jpg',
                                 '5174b318712ac7725cbcb55d422607f9c66d7f8d035c5412ab897b5939e2db6b'),
}
# members of the sand zip, and their hashes
SAND = {
    'Ground054_2K-PNG_Color.png': ('albedo.png', '1444e8119c95277a6342f9888834663f0c3ca90b63e7a791aee29006eeac5c6a'),
    'Ground054_2K-PNG_NormalGL.png': ('normal.png', 'b77d2d445fa547a508802d3fa8be89df4ef3f02d9cb1c895558e1b649c793a64'),
    'Ground054_2K-PNG_Roughness.png': ('rough.png', '157087fe0e73ea28e665cb204c5cc1caa98c7cb2625d54398ef432901cee3f17'),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def fetch(name):
    url, want = FILES[name]
    path = os.path.join(DL, name)
    if os.path.exists(path):
        data = open(path, 'rb').read()
        if sha256(data) == want:
            return data
    req = urllib.request.Request(url, headers={'User-Agent': 'pc_graphics-koy-fetch'})
    data = urllib.request.urlopen(req, timeout=120).read()
    got = sha256(data)
    if got != want:
        sys.exit(f'HASH TUTMADI: {name}\n  beklenen {want}\n  gelen    {got}')
    os.makedirs(DL, exist_ok=True)
    open(path, 'wb').write(data)
    print(f'indirildi: {name} ({len(data) / 1e6:.1f} MB)')
    return data


def gltf_to_mesh(gltf_bytes, bin_bytes, out_path):
    """The glTF's one mesh as raw arrays: 'KOYM', nv, nt, positions, normals, uvs (float32), indices (uint32)."""
    g = json.loads(gltf_bytes)
    prim = g['meshes'][0]['primitives'][0]

    def acc(i):
        a = g['accessors'][i]
        bv = g['bufferViews'][a['bufferView']]
        off = bv.get('byteOffset', 0) + a.get('byteOffset', 0)
        comps = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[a['type']]
        fmt = {5126: 'f', 5125: 'I', 5123: 'H'}[a['componentType']]
        size = struct.calcsize(fmt)
        stride = bv.get('byteStride', size * comps)
        vals = []
        for k in range(a['count']):
            vals.append(struct.unpack_from('<' + fmt * comps, bin_bytes, off + k * stride))
        return vals

    pos = acc(prim['attributes']['POSITION'])
    nrm = acc(prim['attributes']['NORMAL'])
    uv = acc(prim['attributes']['TEXCOORD_0'])
    idx = [v[0] for v in acc(prim['indices'])]
    nv, nt = len(pos), len(idx) // 3
    with open(out_path, 'wb') as f:
        f.write(b'KOYM' + struct.pack('<II', nv, nt))
        for p in pos:
            f.write(struct.pack('<3f', *p))
        for n in nrm:
            f.write(struct.pack('<3f', *n))
        for t in uv:
            f.write(struct.pack('<2f', *t))
        f.write(struct.pack('<%dI' % len(idx), *idx))
    return nv, nt


def main():
    os.makedirs(OUT, exist_ok=True)
    # the sand: ambientCG Ground054 (fine even sand, about 3.5 m a tile)
    kum = os.path.join(OUT, 'kum')
    os.makedirs(kum, exist_ok=True)
    if not all(os.path.exists(os.path.join(kum, v[0])) for v in SAND.values()):
        z = zipfile.ZipFile(io.BytesIO(fetch('Ground054_2K-PNG.zip')))
        for member, (dst, want) in SAND.items():
            data = z.read(member)
            if sha256(data) != want:
                sys.exit(f'HASH TUTMADI: {member}')
            open(os.path.join(kum, dst), 'wb').write(data)
        print('kum: ambientCG Ground054 (2K)')
    # the boulder: Poly Haven boulder_01 (photogrammetry), its mesh as raw arrays and its textures
    kaya = os.path.join(OUT, 'kaya_model')
    os.makedirs(kaya, exist_ok=True)
    if not os.path.exists(os.path.join(kaya, 'mesh.bin')):
        nv, nt = gltf_to_mesh(fetch('boulder_01_2k.gltf'), fetch('boulder_01.bin'), os.path.join(kaya, 'mesh.bin'))
        print(f'kaya: Poly Haven boulder_01, {nv} kose, {nt} ucgen')
    for src, dst in (('boulder_01_diff_2k.jpg', 'albedo.jpg'), ('boulder_01_nor_gl_2k.jpg', 'normal.jpg')):
        p = os.path.join(kaya, dst)
        if not os.path.exists(p):
            open(p, 'wb').write(fetch(src))
    print('tamam:', OUT)


if __name__ == '__main__':
    main()
