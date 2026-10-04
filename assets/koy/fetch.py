"""Koy's CC0 scans: downloads them, checks every file's SHA-256, puts them where the scene reads them (assets/koy/cc0/).

  python assets/koy/fetch.py            re-runnable: skips what is there and correct

The files are not committed (CC0, but large); this script and LICENSES.md are. Sources and why each was chosen:
docs/koy/VERI.md. Every hash was pinned by the K0 research pass (2026-10-03) and checked against the provider's own md5.
"""
import hashlib
import io
import json
import os
import shutil
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
    # the high-resolution set (2026-10-03 ara durak): Poly Haven's md5 checked when pinned; ambientCG gives none (pinned on
    # first download)
    'boulder_01_diff_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Models/jpg/8k/boulder_01/boulder_01_diff_8k.jpg',
                               '36e3f8b181ccd3630b4a821c8e29af000d6cfa8f0ab80c23e7fc78bc790e24da'),
    'boulder_01_nor_gl_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Models/jpg/8k/boulder_01/boulder_01_nor_gl_8k.jpg',
                                 'b1f693b926d9af8f280591a0ced1bfb90be8b5e02b65a09be6bf4f00d2f49b97'),
    'rock_boulder_dry_diff_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/rock_boulder_dry/rock_boulder_dry_diff_4k.jpg',
                                     '2787cfdf899ea12442e67e337e32712d6e25f4a0763ab3df63fd9306a02e156f'),
    'rock_boulder_dry_nor_gl_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/rock_boulder_dry/rock_boulder_dry_nor_gl_4k.jpg',
                                       '440e81881c0681f9dc7efbc5bcf97c9585801bcbdc95b7bd74d6d18657e46f20'),
    'Ground054_4K-JPG.zip': ('https://ambientcg.com/get?file=Ground054_4K-JPG.zip',
                             '13b6964cc9afb04e577f18204fd4656c2786e0197cadf34552e1ef9b177a67e6'),
    # everything at 8K (2026-10-03 gece): Poly Haven md5 checked; the ambientCG zip's size equals its API's (no md5 there)
    'rock_boulder_dry_diff_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/rock_boulder_dry/rock_boulder_dry_diff_8k.jpg',
                                     '8626225ff30359db35c8b858cff3b48b986ca168b6cec3642437e524200eedae'),
    'rock_boulder_dry_nor_gl_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/rock_boulder_dry/rock_boulder_dry_nor_gl_8k.jpg',
                                       '9c83e90fa5215e828fa9d9368263b4365d35bba3c86a948c9af63f7f002bd473'),
    'Ground054_8K-JPG.zip': ('https://ambientcg.com/get?file=Ground054_8K-JPG.zip',
                             'e4a7478a126025cf7a1e92abbead028debaa47c88563a5727fb55e704f1f2ce8'),
    # the 16K set, panel "Ultra" (2026-10-03 gece): Poly Haven md5 checked; the ambientCG zip's size equals its API's.
    # Both were tested to be real 16K, not enlarged 8K (docs/koy/DURUM.md, "Ara durak 2")
    'rock_boulder_dry_diff_16k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/16k/rock_boulder_dry/rock_boulder_dry_diff_16k.jpg',
                                      'e5af191c0c546aa331b17eb45a812cdc11315eb6bcf00bdf251f5e9785444c2f'),
    'rock_boulder_dry_nor_gl_16k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/16k/rock_boulder_dry/rock_boulder_dry_nor_gl_16k.jpg',
                                        'b3866a4a280e19070d3cd5ebe714184508fae00520582a5f7e1066a12b322051'),
    'Ground054_16K-JPG.zip': ('https://ambientcg.com/get?file=Ground054_16K-JPG.zip',
                              'accbf0efe105003082a40348745f4336d909a6c21fa2abe479ffbe328eacec7e'),
    # Amfora (2026-10-04 gecesi, docs/koy/AMFORA_KAYNAK.md §3-4): the jars' fired clay (Poly Haven clay_plaster, 2.00 m)
    # and the concretion over them (coral_fort_wall_01, 1.36 m); Poly Haven md5 checked when pinned
    'clay_plaster_diff_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/clay_plaster/clay_plaster_diff_8k.jpg',
                                 '29fc36a48909bd5d4c743ccf8deb98a7886f9427845427ddcf986424a6e8d4a5'),
    'clay_plaster_nor_gl_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/clay_plaster/clay_plaster_nor_gl_8k.jpg',
                                   '0a78d8d9c01591e0b71961ce48749df5891f66c88e2337e921ebd9a8e6bbcd82'),
    'coral_fort_wall_01_diff_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/coral_fort_wall_01/coral_fort_wall_01_diff_8k.jpg',
                                       '14204ac93866d4ebeaeb4d7c22bd5cbb8072a3c1c2052afecc50773f6b82fdec'),
    'coral_fort_wall_01_nor_gl_8k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/8k/coral_fort_wall_01/coral_fort_wall_01_nor_gl_8k.jpg',
                                         '2ccb8e22c14248adc0d8bd16800f04358cf0bc759562fe4142d17f08e2906140'),
    # Ada (2026-10-04, docs/koy/PLAN_ADA.md): the pier's and the wreck's wood (Poly Haven rough_wood, 0.5 m: one board's
    # weathered grain, the boards are geometry) and the pier's concrete piles (concrete_wall_008, 2.7 m); 4K is past the
    # screen at their distances; Poly Haven md5 checked when pinned
    'rough_wood_diff_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/rough_wood/rough_wood_diff_4k.jpg',
                               '2a7714192151b892e1fc3850e0d83beac385b933e7615b356d5537aa42ad3410'),
    'rough_wood_nor_gl_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/rough_wood/rough_wood_nor_gl_4k.jpg',
                                 '6d01bfa129d42d90a6b58bdc1dcf358a37bbbeca9422ac6e974ed2457ff01c05'),
    'concrete_wall_008_diff_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/concrete_wall_008/concrete_wall_008_diff_4k.jpg',
                                      '9452c58ce2256afe9e96f04952712a1c22c012b61dc048ad10a18e62f6c26c63'),
    'concrete_wall_008_nor_gl_4k.jpg': ('https://dl.polyhaven.org/file/ph-assets/Textures/jpg/4k/concrete_wall_008/concrete_wall_008_nor_gl_4k.jpg',
                                        '52de09b25a87b1c21435b8cae0d946d9a29141188cac827f240c9392466f853d'),
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


def file_sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            h.update(block)
    return h.hexdigest()


def fetch_file(name):
    """fetch() for the big files (the 16K set, up to 2 GB): streamed to disk and hashed in blocks; returns the path"""
    url, want = FILES[name]
    path = os.path.join(DL, name)
    if os.path.exists(path) and file_sha256(path) == want:
        return path
    os.makedirs(DL, exist_ok=True)
    req = urllib.request.Request(url, headers={'User-Agent': 'pc_graphics-koy-fetch'})
    with urllib.request.urlopen(req, timeout=900) as r, open(path + '.part', 'wb') as f:
        shutil.copyfileobj(r, f, 1 << 22)
    got = file_sha256(path + '.part')
    if got != want:
        sys.exit(f'HASH TUTMADI: {name}\n  beklenen {want}\n  gelen    {got}')
    os.replace(path + '.part', path)
    print(f'indirildi: {name} ({os.path.getsize(path) / 1e6:.1f} MB)')
    return path


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
    # the high-resolution set: the same boulder at 8K, a 4K rock detail layer (Poly Haven rock_boulder_dry, 1.8 m a
    # tile) laid over it in world space, the sand at 4K
    sets = (('kaya_model_8k', (('boulder_01_diff_8k.jpg', 'albedo.jpg'), ('boulder_01_nor_gl_8k.jpg', 'normal.jpg'))),
            ('kaya_ayrinti', (('rock_boulder_dry_diff_4k.jpg', 'albedo.jpg'), ('rock_boulder_dry_nor_gl_4k.jpg', 'normal.jpg'))),
            ('kaya_ayrinti_8k', (('rock_boulder_dry_diff_8k.jpg', 'albedo.jpg'), ('rock_boulder_dry_nor_gl_8k.jpg', 'normal.jpg'))),
            ('amfora_kil_8k', (('clay_plaster_diff_8k.jpg', 'albedo.jpg'), ('clay_plaster_nor_gl_8k.jpg', 'normal.jpg'))),
            ('kabuk_8k', (('coral_fort_wall_01_diff_8k.jpg', 'albedo.jpg'), ('coral_fort_wall_01_nor_gl_8k.jpg', 'normal.jpg'))),
            ('ahsap_4k', (('rough_wood_diff_4k.jpg', 'albedo.jpg'), ('rough_wood_nor_gl_4k.jpg', 'normal.jpg'))),
            ('beton_4k', (('concrete_wall_008_diff_4k.jpg', 'albedo.jpg'), ('concrete_wall_008_nor_gl_4k.jpg', 'normal.jpg'))))
    for folder, files in sets:
        d = os.path.join(OUT, folder)
        os.makedirs(d, exist_ok=True)
        for src, dst in files:
            p = os.path.join(d, dst)
            if not os.path.exists(p):
                open(p, 'wb').write(fetch(src))
    kum4 = os.path.join(OUT, 'kum_4k')
    os.makedirs(kum4, exist_ok=True)
    if not os.path.exists(os.path.join(kum4, 'normal.jpg')):
        z = zipfile.ZipFile(io.BytesIO(fetch('Ground054_4K-JPG.zip')))
        names = z.namelist()
        for key, dst in (('_Color.jpg', 'albedo.jpg'), ('_NormalGL.jpg', 'normal.jpg'), ('_Roughness.jpg', 'rough.jpg')):
            member = [n for n in names if n.endswith(key)][0]
            open(os.path.join(kum4, dst), 'wb').write(z.read(member))
        print('kum: ambientCG Ground054 (4K)')
    kum8 = os.path.join(OUT, 'kum_8k')
    os.makedirs(kum8, exist_ok=True)
    if not os.path.exists(os.path.join(kum8, 'normal.jpg')):
        z = zipfile.ZipFile(io.BytesIO(fetch('Ground054_8K-JPG.zip')))
        names = z.namelist()
        for key, dst in (('_Color.jpg', 'albedo.jpg'), ('_NormalGL.jpg', 'normal.jpg')):   # roughness unused: not extracted
            member = [n for n in names if n.endswith(key)][0]
            open(os.path.join(kum8, dst), 'wb').write(z.read(member))
        print('kum: ambientCG Ground054 (8K)')
    # the 16K set (panel "Ultra"): the sand and the rock detail layer; the boulder's own scan ends at 8K
    ayr16 = os.path.join(OUT, 'kaya_ayrinti_16k')
    os.makedirs(ayr16, exist_ok=True)
    for src, dst in (('rock_boulder_dry_diff_16k.jpg', 'albedo.jpg'), ('rock_boulder_dry_nor_gl_16k.jpg', 'normal.jpg')):
        p = os.path.join(ayr16, dst)
        if not os.path.exists(p):   # via .part: an interrupted copy never leaves a short file that later runs skip
            shutil.copyfile(fetch_file(src), p + '.part')
            os.replace(p + '.part', p)
    kum16 = os.path.join(OUT, 'kum_16k')
    os.makedirs(kum16, exist_ok=True)
    if not os.path.exists(os.path.join(kum16, 'normal.jpg')):
        with zipfile.ZipFile(fetch_file('Ground054_16K-JPG.zip')) as z:
            names = z.namelist()
            for key, dst in (('_Color.jpg', 'albedo.jpg'), ('_NormalGL.jpg', 'normal.jpg')):
                member = [n for n in names if n.endswith(key)][0]
                with z.open(member) as s, open(os.path.join(kum16, dst) + '.part', 'wb') as d:
                    shutil.copyfileobj(s, d, 1 << 22)
                os.replace(os.path.join(kum16, dst) + '.part', os.path.join(kum16, dst))
        print('kum: ambientCG Ground054 (16K)')
    print('tamam:', OUT)


if __name__ == '__main__':
    main()
