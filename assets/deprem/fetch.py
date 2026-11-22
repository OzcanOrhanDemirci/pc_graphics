"""Yikim's earthquake records: downloads them, checks every file, puts them where the scene reads them (assets/deprem/).

  python assets/deprem/fetch.py              the four ESM records (12 files); re-runnable (skips what is there and correct),
                                             then checks the Bayrakli record and says how to get it if it is missing
  python assets/deprem/fetch.py --zip F.zip  takes the Bayrakli record out of the zip TADAS gives (nested zips too)

The files are not committed (their redistribution licence is not stated); this script is. Sources and credit (NOTICE.md):
  ESM, the Engineering Strong-Motion database: Mascandola C., Felicetta C., Russo E., Luzi L., Lanzano G., Sgobba S.,
    Brunelli G., Ramadan F., Pacor F., ORFEUS Strong-Motion SMC (2026). Engineering Strong Motion Database (ESM),
    version 3.0. INGV. https://doi.org/10.13127/esm.3 . Processed (MP) records, its public web service, no account.
  AFAD TADAS, Disaster and Emergency Management Presidency of Turkiye (https://tadas.afad.gov.tr).
  Original network TK: Turkish National Strong Motion Network, AFAD, https://doi.org/10.7914/SN/TK .

Checks. Every file is pinned to the SHA-256 of the copy the scene was tuned with. ESM writes the moment of each download
into one header line (DATA_TIMESTAMP_YYYYMMDD_HHMMSS:), so no two downloads are byte-identical; for ESM files the hash is
taken with that one line left out, and every other byte, the samples included, must match. The scene's parser
(src/scenes/yikim/quake.cpp) skips every line with a colon, so that line never reaches the numbers. The Bayrakli files
are pinned whole.

Bayrakli (2020 Samos earthquake, station TK.3513) is in AFAD TADAS only: ESM has that earthquake but no TK record of it.
TADAS gives it after a guest login in the browser, so it is a step done by hand (this script does not log in):
  1. https://tadas.afad.gov.tr , "Misafir Girisi" (Guest Login)
  2. the earthquake of 2020-10-30 11:51:24 (Mw 6.6, Samos; TADAS event 10614), station 3513 (Izmir Bayrakli),
     record 20201030115124_3513
  3. download it processed: data type "mp", "Acc" (acceleration), format "asc2"
  4. python assets/deprem/fetch.py --zip <the downloaded zip>
     (or unzip it and put 20201030115124_3513_mp_Acc_E.asc, _N.asc, _U.asc in assets/deprem/ yourself)
Without it the scene opens and every magnitude but 3 and 4 (whose waveform it is) works.
"""
import argparse
import hashlib
import io
import os
import sys
import urllib.error
import urllib.request
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
UA = {'User-Agent': 'pc_graphics-deprem-fetch'}
ESM = 'https://esm-db.eu/esmws/eventdata/1/query?eventid={event}&network={net}&station={sta}&format=ascii&processing-type=MP&data-type=ACC'
STAMP = b'DATA_TIMESTAMP_YYYYMMDD_HHMMSS:'

# ESM records: one request gives a zip of the three components, named as the scene reads them
ESM_RECORDS = [
    {'name': '2023 Kahramanmaras, Pazarcik (TK.4614)', 'event': 'INT-20230206_0000008', 'net': 'TK', 'sta': '4614', 'files': {
        'TK.4614..HNE.D.INT-20230206_0000008.ACC.MP.ASC': '31e89aeb669ad7cb2e57a55965f3817f234cba283b053c8254979c44f43545c1',
        'TK.4614..HNN.D.INT-20230206_0000008.ACC.MP.ASC': 'bb7fe067fbd31a781bb55904d2dc55df342451c81b27a131bd2e1970bbc8fb92',
        'TK.4614..HNZ.D.INT-20230206_0000008.ACC.MP.ASC': 'c4b38c0b8a776fcee5ff5a76c555e349576ebf235e999bc00458c7bf85d7a2ee'}},
    {'name': '2023 Kahramanmaras, Antakya (TK.3123)', 'event': 'INT-20230206_0000008', 'net': 'TK', 'sta': '3123', 'files': {
        'TK.3123..HNE.D.INT-20230206_0000008.ACC.MP.ASC': 'cb26973fe04156caddeec32dbdc140d1871a84bdec0f6a9067f092e8f1be464f',
        'TK.3123..HNN.D.INT-20230206_0000008.ACC.MP.ASC': '18aa2fd12c6b6c3f8b6f7694fb8cb2ee9c26aded091bfd2fbcf3dcfdd8779ec8',
        'TK.3123..HNZ.D.INT-20230206_0000008.ACC.MP.ASC': 'f655d02e080350cc03f66db7de0539b8a415accd4f6ac150122967f322da37ba'}},
    {'name': '1999 Kocaeli, Yarimca (YPT)', 'event': 'TK-1999-0077', 'net': 'A', 'sta': 'YPT', 'files': {
        'A.YPT.00.HNE.D.TK-1999-0077.ACC.MP.ASC': '7557b972ede4d7e1369bac79d4ecc9585bea0e853ce6ffd0987afe229098fb94',
        'A.YPT.00.HNN.D.TK-1999-0077.ACC.MP.ASC': '988b5ac2cbfdc700450ecc9a70557e00cb2d3d392827162683f4e9b136802dd4',
        'A.YPT.00.HNZ.D.TK-1999-0077.ACC.MP.ASC': 'b05b56435e609a30929723efe2c5fbd0a48b79567658316bbcd63cad50104bc4'}},
    {'name': '1999 Kocaeli, Izmit (TK.4101)', 'event': 'TK-1999-0077', 'net': 'TK', 'sta': '4101', 'files': {
        'TK.4101..HNE.D.TK-1999-0077.ACC.MP.ASC': '70af51ad9a4736b686fb8d95e64d6d9f652b95bc0ead909aadfd6a009042b540',
        'TK.4101..HNN.D.TK-1999-0077.ACC.MP.ASC': '03d82d234fbdd3af90717d15ad5853ce8256ae039c2f3d3b10665c685125a498',
        'TK.4101..HNZ.D.TK-1999-0077.ACC.MP.ASC': '5f23fc7d9aeb5e5c9f6ce2d6cef6c2dac4320b76c9b81075bcb36ec750b66a1e'}},
]

# AFAD TADAS record, by hand (see above); pinned whole
BAYRAKLI = {
    '20201030115124_3513_mp_Acc_E.asc': '6963e94cd925feeab124e87ff845d67f18c3b1ca3fe36836373cb5b3042434b8',
    '20201030115124_3513_mp_Acc_N.asc': '07fc52d07ffa0c2db34db5d621bf1efb0dae77ab61d7e777e9dbd9af690eb83d',
    '20201030115124_3513_mp_Acc_U.asc': '03f6fbb9a2b9811b01bd285ecb96672318745123e4bef5ad1841d4e3a061f8be',
}


def esm_digest(data):
    """SHA-256 of an ESM file without its download-time line"""
    return hashlib.sha256(b''.join(l for l in data.splitlines(True) if not l.startswith(STAMP))).hexdigest()


def whole_digest(data):
    return hashlib.sha256(data).hexdigest()


def good(name, pin, digest):
    path = os.path.join(HERE, name)
    if not os.path.exists(path):
        return False
    with open(path, 'rb') as f:
        return digest(f.read()) == pin


def write(name, data):
    """an interrupted write never leaves a short file behind (via .part)"""
    path = os.path.join(HERE, name)
    with open(path + '.part', 'wb') as f:
        f.write(data)
    os.replace(path + '.part', path)


def fetch_esm(rec):
    if all(good(n, pin, esm_digest) for n, pin in rec['files'].items()):
        return 0
    url = ESM.format(**rec)
    try:
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=300) as r:
            body = r.read()
    except (urllib.error.URLError, OSError) as e:
        sys.exit(f'INDIRILEMEDI: {rec["name"]}\n  {url}\n  {e}\n  ESM (esm-db.eu) erisilebilir mi? Sonra yeniden calistirin.')
    try:
        z = zipfile.ZipFile(io.BytesIO(body))
    except zipfile.BadZipFile:
        sys.exit(f'ESM zip vermedi: {rec["name"]}\n  {url}\n  yanit: {body[:300]!r}')
    names = set(z.namelist())
    for n, pin in rec['files'].items():
        if n not in names:
            sys.exit(f'ESM zip\'inde yok: {n}\n  {url}\n  zip: {sorted(names)}')
        data = z.read(n)
        if esm_digest(data) != pin:
            sys.exit(f'OZET TUTMADI: {n}\n  {url}\n  ESM kaydi degismis olabilir; sahne bu kayitla ayarlandi, denetlemeden kullanmayin.')
        write(n, data)
    return len(rec['files'])


def members(z):
    """(basename, bytes) of every file in a zip, nested zips opened"""
    for n in z.namelist():
        if n.endswith('/'):
            continue
        data = z.read(n)
        if n.lower().endswith('.zip'):
            yield from members(zipfile.ZipFile(io.BytesIO(data)))
        else:
            yield os.path.basename(n), data


def take_zip(path):
    with zipfile.ZipFile(path) as z:
        found = {n: d for n, d in members(z) if n in BAYRAKLI}
    for n, pin in BAYRAKLI.items():
        if n not in found:
            sys.exit(f'zip\'te yok: {n}\n  {path}\n  TADAS\'tan kayit 20201030115124_3513, "mp", "Acc", "asc2" olarak mi indirildi?')
        if whole_digest(found[n]) != pin:
            sys.exit(f'OZET TUTMADI: {n} ({path})\n  sahnenin ayarlandigi kayit degil (baska bir islem tipi ya da bicim mi?)')
        write(n, found[n])
    print(f'alindi: Bayrakli (TK.3513), {len(BAYRAKLI)} dosya')


def check_bayrakli():
    bad = [n for n, pin in BAYRAKLI.items() if os.path.exists(os.path.join(HERE, n)) and not good(n, pin, whole_digest)]
    missing = [n for n in BAYRAKLI if not os.path.exists(os.path.join(HERE, n))]
    if not bad and not missing:
        return True
    print('\nBayrakli kaydi (2020 Sisam depremi, TK.3513; buyukluk 3 ve 4 bunu kullanir) elle indirilir:')
    for n in missing:
        print(f'  yok: {n}')
    for n in bad:
        print(f'  ozet tutmadi: {n}')
    print('  1. https://tadas.afad.gov.tr , "Misafir Girisi"\n'
          '  2. 30.10.2020 11:51:24 depremi (Mw 6,6, Sisam; TADAS olay 10614), istasyon 3513 (Izmir Bayrakli),\n'
          '     kayit 20201030115124_3513\n'
          '  3. islenmis olarak indirin: "mp", "Acc" (ivme), bicim "asc2"\n'
          '  4. python assets/deprem/fetch.py --zip <indirilen zip>\n'
          '  Bu kayit olmadan sahne acilir; 3 ve 4 disindaki butun buyuklukler calisir.')
    return False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--zip', help='the zip TADAS gave for record 20201030115124_3513')
    args = ap.parse_args()
    if args.zip:
        take_zip(args.zip)
    total = 0
    for rec in ESM_RECORDS:
        n = fetch_esm(rec)
        total += n
        if n:
            print(f'indirildi: {rec["name"]} ({n} dosya)')
    print(f'tamam: ESM, {len(ESM_RECORDS)} kayit, {total} yeni dosya')
    if not check_bayrakli():
        sys.exit(1)


if __name__ == '__main__':
    main()
