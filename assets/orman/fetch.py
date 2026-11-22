"""Orman's open-licence assets: downloads them, checks every file, puts them where the scene reads them (assets/orman/cc0/).

  python assets/orman/fetch.py                 every asset in manifest.json; re-runnable (skips what is there and correct)
  python assets/orman/fetch.py --check         only checks what is there, downloads nothing
  python assets/orman/fetch.py --pin ph:fern_02 adds a Poly Haven asset (model or texture, 2K) to the manifest
  python assets/orman/fetch.py --pin acg:LeafSet010   adds an ambientCG asset (its 2K-JPG zip; 2K-PNG if it has no JPG)
  python assets/orman/fetch.py --only fern_02  just these ids
  --lang tr|en                                 the language of the messages (default: the application's, from user.cfg)

The files are not committed (large); this script, manifest.json and docs/orman/VARLIK.md are. Layout, per asset id:
  Poly Haven model   cc0/<id>/<id>_2k.gltf, <id>.bin, textures/... as Poly Haven ships the glTF; the alpha map (the glTF's
                     base colour is a JPG without one) as textures/<id>_alpha_2k.png, or per material for multi-material
                     plants (textures/<id>_leaves_alpha_2k.png: pachira, jacaranda, tree_small_02), or _opacity_2k
  Poly Haven texture cc0/<id>/<id>_<map>_2k.<ext> (diff, nor_gl, rough, ao, disp, arm: what it has)
  ambientCG          cc0/<id>/<the zip's members> (<id>_2K-JPG_Color.jpg, _NormalGL, _Roughness, _Opacity, ...)
Poly Haven gives every file's md5 (pinned with --pin); ambientCG gives none: the zip's SHA-256 is pinned on --pin's download.
The messages follow the application's language (tools/dil.py).
"""
import argparse
import hashlib
import json
import os
import shutil
import sys
import urllib.error
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import dil  # noqa: E402
from dil import say  # noqa: E402

SCENE_TR = 'Orman'
HELP_TR = f"""{SCENE_TR} sahnesinin açık lisanslı varlıkları: indirir, her dosyayı denetler, sahnenin okuduğu yere koyar
(assets/{os.path.basename(HERE)}/cc0/).

  python assets/{os.path.basename(HERE)}/fetch.py                  manifest.json'daki her varlık; yeniden çalıştırılabilir
                                               (yerinde ve doğru olanı atlar)
  python assets/{os.path.basename(HERE)}/fetch.py --check          yalnız yerinde olanı denetler, hiçbir şey indirmez
  python assets/{os.path.basename(HERE)}/fetch.py --pin ph:fern_02 manifest'e bir Poly Haven varlığı ekler (model ya da doku, 2K)
  python assets/{os.path.basename(HERE)}/fetch.py --pin acg:LeafSet010   bir ambientCG varlığı ekler (2K-JPG zip'i; JPG yoksa 2K-PNG)
  python assets/{os.path.basename(HERE)}/fetch.py --only fern_02   yalnız bu kimlikler
  --lang tr|en                                 iletilerin dili (öntanımlı: uygulamanınki, user.cfg'den)

Dosyalar depoya işlenmez (büyükler); bu betik, manifest.json ve docs/{os.path.basename(HERE)}/VARLIK.md işlenir. Poly Haven her
dosyanın md5'ini verir (--pin ile sabitlenir); ambientCG vermez: zip'in SHA-256'sı --pin'in indirmesinde sabitlenir.
"""
OUT = os.path.join(HERE, 'cc0')
DL = os.path.join(HERE, '_dl')
MANIFEST = os.path.join(HERE, 'manifest.json')
UA = {'User-Agent': f'pc_graphics-{os.path.basename(HERE)}-fetch'}
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
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=900) as r, open(path + '.part', 'wb') as f:
            shutil.copyfileobj(r, f, 1 << 22)
    except (urllib.error.URLError, OSError) as e:
        sys.exit(say(f'İNDİRİLEMEDİ: {url}\n  {e}\n  Bağlantı var mı? Sonra yeniden çalıştırın: indirilmiş olan atlanır.',
                     f'COULD NOT DOWNLOAD: {url}\n  {e}\n  Is the connection up? Then run this again: what is there is skipped.'))
    os.replace(path + '.part', path)


def fetch_entry(path, entry):
    """one file to path, checked; an interrupted copy never leaves a short file behind (via .part)"""
    if good(path, entry):
        return False
    download(entry['url'], path)
    if not good(path, entry):
        sys.exit(say(f'ÖZET TUTMADI: {path}\n  {entry}', f'HASH MISMATCH: {path}\n  {entry}'))
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
        sys.exit(say(f'{aid}: 2K zip yok ({sorted(by_attr)})', f'{aid}: no 2K zip ({sorted(by_attr)})'))
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


def check_asset(a):
    """--check: the files of one asset that are missing or not the pinned ones (a zip's members: present)"""
    wrong = []
    for e in a['files']:
        if 'zip' in e:
            dest = os.path.join(OUT, a['id'])
            if not (os.path.isdir(dest) and os.listdir(dest)) and not good(os.path.join(DL, e['zip']), e):
                wrong.append(e['zip'])
        elif not good(os.path.join(OUT, e['path']), e):
            wrong.append(e['path'])
    return wrong


def main():
    dil.choose(dil.argv_lang())   # before the parser: --help is given in the chosen language too
    ap = argparse.ArgumentParser(description=say(HELP_TR, __doc__), formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--check', action='store_true', help=say('yalnız denetle, indirme', 'only check, download nothing'))
    ap.add_argument('--pin', nargs='*', default=[], help=say('manifest\'e eklenecek varlıklar (ph:<kimlik>, acg:<kimlik>)',
                                                             'assets to add to the manifest (ph:<id>, acg:<id>)'))
    ap.add_argument('--only', nargs='*', default=[], help=say('yalnız bu kimlikler', 'just these ids'))
    ap.add_argument('--lang', choices=('tr', 'en'), help=say('iletilerin dili (öntanımlı: uygulamanınki, user.cfg\'den)',
                                                           'the language of the messages (default: the application\'s, from user.cfg)'))
    args = ap.parse_args()
    m = load_manifest()
    rel = os.path.basename(HERE)
    if args.check:
        bad = 0
        for a in m['assets']:
            if args.only and a['id'] not in args.only:
                continue
            for w in check_asset(a):
                print(say(f'  eksik ya da özeti tutmuyor: {a["id"]}: {w}', f'  missing or hash mismatch: {a["id"]}: {w}'))
                bad += 1
        print(say(f'{len(m["assets"])} varlık; ' + (f'{bad} dosya eksik, indirmek için: python assets/{rel}/fetch.py' if bad else 'hepsi yerinde ve doğru'),
                  f'{len(m["assets"])} assets; ' + (f'{bad} files missing, to fetch them: python assets/{rel}/fetch.py' if bad else 'all present and correct')))
        sys.exit(1 if bad else 0)
    if args.pin:
        for spec in args.pin:
            src, _, aid = spec.partition(':')
            if src not in ('ph', 'acg') or not aid:
                sys.exit(say(f'kaynak? {spec} (ph:<kimlik> ya da acg:<kimlik>)', f'source? {spec} (ph:<id> or acg:<id>)'))
            entry = pin_polyhaven(aid) if src == 'ph' else pin_ambientcg(aid)
            m['assets'] = [x for x in m['assets'] if x['id'] != aid] + [entry]
            print(say(f'sabitlendi: {aid} ({entry["kind"]}, {len(entry["files"])} dosya)', f'pinned: {aid} ({entry["kind"]}, {len(entry["files"])} files)'))
        save_manifest(m)
    total = 0
    for a in m['assets']:
        if args.only and a['id'] not in args.only:
            continue
        n = fetch_asset(a)
        total += n
        if n:
            print(say(f'indirildi: {a["id"]} ({n} dosya)', f'downloaded: {a["id"]} ({n} files)'))
    print(say(f'tamam: {len(m["assets"])} varlık, {total} yeni dosya', f'done: {len(m["assets"])} assets, {total} new files'))


if __name__ == '__main__':
    main()
