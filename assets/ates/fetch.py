"""Ateş's open-licence assets: downloads them, checks every file, puts them where the scene reads them (assets/ates/cc0/).

  python assets/ates/fetch.py                  every asset in manifest.json; re-runnable (skips what is there and correct)
  python assets/ates/fetch.py --pin ph:fern_02 adds a Poly Haven asset (model or texture, 2K) to the manifest
  python assets/ates/fetch.py --pin acg:LeafSet010   adds an ambientCG asset (its 2K-JPG zip; 2K-PNG if it has no JPG)
  python assets/ates/fetch.py --only fern_02   just these ids

The files are not committed (large); this script, manifest.json and docs/ates/VARLIK.md are. Layout, per asset id:
  Poly Haven model   cc0/<id>/<id>_2k.gltf, <id>.bin, textures/... as Poly Haven ships the glTF; the alpha map (the glTF's
                     base colour is a JPG without one) as textures/<id>_alpha_2k.png, or per material for multi-material
                     plants (textures/<id>_leaves_alpha_2k.png: pachira, jacaranda, tree_small_02), or _opacity_2k
  Poly Haven texture cc0/<id>/<id>_<map>_2k.<ext> (diff, nor_gl, rough, ao, disp, arm: what it has)
  ambientCG          cc0/<id>/<the zip's members> (<id>_2K-JPG_Color.jpg, _NormalGL, _Roughness, _Opacity, ...)
Poly Haven gives every file's md5 (pinned with --pin); ambientCG gives none: the zip's SHA-256 is pinned on --pin's download.
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, 'cc0')
DL = os.path.join(HERE, '_dl')
MANIFEST = os.path.join(HERE, 'manifest.json')
UA = {'User-Agent': 'pc_graphics-ates-fetch'}
RES = '2k'


def get_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
        return json.loads(r.read().decode('utf-8'))


def digest(path, algo):
    h = hashlib.new(algo)
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 22), b''):
            h.update(block)
    return h.hexdigest()


def good(path, entry):
    if not os.path.exists(path):
        return False
    if 'sha256' in entry:
        return digest(path, 'sha256') == entry['sha256']
    if 'md5' in entry:
        return digest(path, 'md5') == entry['md5']
    return os.path.getsize(path) == entry.get('size', -1)


def download(url, path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=900) as r, open(path + '.part', 'wb') as f:
        shutil.copyfileobj(r, f, 1 << 22)
    os.replace(path + '.part', path)


def fetch_entry(path, entry):
    """one file to path, checked; an interrupted copy never leaves a short file behind (via .part)"""
    if good(path, entry):
        return False
    download(entry['url'], path)
    if not good(path, entry):
        sys.exit(f'OZET TUTMADI: {path}\n  {entry}')
    return True


def load_manifest():
    if not os.path.exists(MANIFEST):
        return {'assets': []}
    with open(MANIFEST, encoding='utf-8') as f:
        return json.load(f)


def save_manifest(m):
    m['assets'].sort(key=lambda a: a['id'].lower())
    with open(MANIFEST, 'w', encoding='utf-8', newline='\n') as f:
        json.dump(m, f, indent=1, ensure_ascii=False)
        f.write('\n')


def pin_polyhaven(aid):
    info = get_json(f'https://api.polyhaven.com/info/{aid}')
    files = get_json(f'https://api.polyhaven.com/files/{aid}')
    kind = {0: 'hdri', 1: 'texture', 2: 'model'}[info['type']]
    out = []
    if kind == 'model':
        g = files['gltf'][RES]['gltf']
        out.append({'path': f'{aid}/{os.path.basename(g["url"])}', 'url': g['url'], 'md5': g['md5'], 'size': g['size']})
        for rel, v in g['include'].items():
            out.append({'path': f'{aid}/{rel}', 'url': v['url'], 'md5': v['md5'], 'size': v['size']})
        # the alpha map(s): 'Alpha', or one per material ('leaves_alpha' on the multi-material trees), or 'opacity'
        for key in sorted(files):
            k = key.lower()
            if (k == 'alpha' or k.endswith('_alpha') or k == 'opacity') and RES in files[key]:
                a = files[key][RES].get('png') or files[key][RES]['jpg']
                out.append({'path': f'{aid}/textures/{os.path.basename(a["url"])}', 'url': a['url'], 'md5': a['md5'], 'size': a['size']})
    elif kind == 'texture':
        for key, name in (('Diffuse', 'diff'), ('nor_gl', 'nor_gl'), ('Rough', 'rough'), ('AO', 'ao'), ('Displacement', 'disp'),
                          ('arm', 'arm'), ('Alpha', 'alpha')):
            if key not in files or RES not in files[key]:
                continue
            fmts = files[key][RES]
            v = fmts['png'] if key in ('Displacement', 'Alpha') and 'png' in fmts else fmts.get('jpg') or fmts.get('png')
            out.append({'path': f'{aid}/{os.path.basename(v["url"])}', 'url': v['url'], 'md5': v['md5'], 'size': v['size']})
    else:
        h = files['hdri'][RES]['hdr']
        out.append({'path': f'{aid}/{os.path.basename(h["url"])}', 'url': h['url'], 'md5': h['md5'], 'size': h['size']})
    return {'id': aid, 'source': 'polyhaven', 'kind': kind, 'name': info['name'], 'page': f'https://polyhaven.com/a/{aid}',
            'license': 'CC0-1.0', 'authors': sorted(info.get('authors', {}).keys()), 'dimensions_mm': info.get('dimensions'),
            'polycount': info.get('polycount'), 'files': out}


def pin_ambientcg(aid):
    q = get_json(f'https://ambientcg.com/api/v2/full_json?id={aid}&include=downloadData')
    a = q['foundAssets'][0]
    downloads = a['downloadFolders']['default']['downloadFiletypeCategories']['zip']['downloads']
    by_attr = {d['attribute']: d for d in downloads}
    d = by_attr.get('2K-JPG') or by_attr.get('2K-PNG')
    if not d:
        sys.exit(f'{aid}: 2K zip yok ({sorted(by_attr)})')
    zpath = os.path.join(DL, d['fileName'])
    if not (os.path.exists(zpath) and os.path.getsize(zpath) == d['size']):
        download(d['fullDownloadPath'], zpath)
    return {'id': aid, 'source': 'ambientcg', 'kind': a['dataType'].lower(), 'name': a['displayName'],
            'page': f'https://ambientcg.com/a/{aid}', 'license': 'CC0-1.0', 'authors': ['Lennart Demes (ambientCG)'],
            'files': [{'zip': d['fileName'], 'url': d['fullDownloadPath'], 'sha256': digest(zpath, 'sha256'), 'size': d['size']}]}


def fetch_asset(a):
    got = 0
    for e in a['files']:
        if 'zip' in e:
            zpath = os.path.join(DL, e['zip'])
            got += fetch_entry(zpath, e)
            dest = os.path.join(OUT, a['id'])
            with zipfile.ZipFile(zpath) as z:
                for member in z.namelist():
                    p = os.path.join(dest, os.path.basename(member))
                    if member.endswith('/') or os.path.exists(p):
                        continue
                    os.makedirs(dest, exist_ok=True)
                    with z.open(member) as src, open(p + '.part', 'wb') as dst:
                        shutil.copyfileobj(src, dst)
                    os.replace(p + '.part', p)
        else:
            got += fetch_entry(os.path.join(OUT, e['path']), e)
    return got


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pin', nargs='*', default=[])
    ap.add_argument('--only', nargs='*', default=[])
    args = ap.parse_args()
    m = load_manifest()
    if args.pin:
        for spec in args.pin:
            src, aid = spec.split(':', 1)
            entry = pin_polyhaven(aid) if src == 'ph' else pin_ambientcg(aid) if src == 'acg' else sys.exit(f'kaynak? {spec}')
            m['assets'] = [x for x in m['assets'] if x['id'] != aid] + [entry]
            print(f'sabitlendi: {aid} ({entry["kind"]}, {len(entry["files"])} dosya)')
        save_manifest(m)
    total = 0
    for a in m['assets']:
        if args.only and a['id'] not in args.only:
            continue
        n = fetch_asset(a)
        total += n
        if n:
            print(f'indirildi: {a["id"]} ({n} dosya)')
    print(f'tamam: {len(m["assets"])} varlik, {total} yeni dosya')


if __name__ == '__main__':
    main()
