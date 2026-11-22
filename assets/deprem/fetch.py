"""Yikim's earthquake records: downloads them, checks every file, puts them where the scene reads them (assets/deprem/).

  python assets/deprem/fetch.py              the four ESM records (12 files); re-runnable (skips what is there and correct),
                                             then checks the Bayrakli record and says how to get it if it is missing
  python assets/deprem/fetch.py --zip F.zip  takes the Bayrakli record out of the zip TADAS gives (nested zips too)
  python assets/deprem/fetch.py --check      only checks what is there, downloads nothing
  --lang tr|en                               the language of the messages (default: the application's, from user.cfg)

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
  1. https://tadas.afad.gov.tr , "Misafir Girişi" (Guest Login)
  2. the earthquake of 2020-10-30 11:51:24 (Mw 6.6, Samos; TADAS event 10614), station 3513 (Izmir Bayrakli),
     record 20201030115124_3513
  3. download it processed: data type "mp", "Acc" (acceleration), format "asc2"
  4. python assets/deprem/fetch.py --zip <the downloaded zip>
     (or unzip it and put 20201030115124_3513_mp_Acc_E.asc, _N.asc, _U.asc in assets/deprem/ yourself)
Without it the scene opens and every magnitude but 3 and 4 (whose waveform it is) works.

The messages follow the application's language (tools/dil.py).
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
sys.path.insert(0, os.path.join(HERE, '..', '..', 'tools'))
import dil  # noqa: E402
from dil import say  # noqa: E402

HELP_TR = """Yıkım'ın deprem kayıtları: indirir, her dosyayı denetler, sahnenin okuduğu yere koyar (assets/deprem/).

  python assets/deprem/fetch.py              dört ESM kaydı (12 dosya); yeniden çalıştırılabilir (yerinde ve doğru olanı
                                             atlar), sonra Bayraklı kaydını denetler, yoksa nasıl alınacağını yazar
  python assets/deprem/fetch.py --zip F.zip  Bayraklı kaydını TADAS'ın verdiği zip'ten çıkarır (iç içe zip'ler de olur)
  python assets/deprem/fetch.py --check      yalnız yerinde olanı denetler, hiçbir şey indirmez
  --lang tr|en                               iletilerin dili (öntanımlı: uygulamanınki, user.cfg'den)

Dosyalar depoya işlenmez (yeniden dağıtım lisansları belirtilmemiş); bu betik işlenir. Kaynaklar ve atıf: NOTICE.md.
Her dosya, sahnenin ayarlandığı kopyanın SHA-256 özetine sabitlenir (ESM dosyalarında indirme anını yazan tek başlık
satırı dışarıda bırakılarak).

Bayraklı (2020 Sisam depremi, istasyon TK.3513) yalnız AFAD TADAS'ta var. TADAS onu tarayıcıda misafir girişinden sonra
verir, bu yüzden elle yapılan bir adımdır (bu betik giriş yapmaz):
  1. https://tadas.afad.gov.tr , "Misafir Girişi"
  2. 30.10.2020 11:51:24 depremi (Mw 6,6, Sisam; TADAS olay 10614), istasyon 3513 (İzmir Bayraklı),
     kayıt 20201030115124_3513
  3. işlenmiş olarak indirin: veri tipi "mp", "Acc" (ivme), biçim "asc2"
  4. python assets/deprem/fetch.py --zip <indirilen zip>
     (ya da zip'i açıp 20201030115124_3513_mp_Acc_E.asc, _N.asc, _U.asc dosyalarını assets/deprem/ içine kendiniz koyun)
Bu kayıt olmadan sahne açılır; dalgası onun olan 3 ve 4 dışındaki bütün büyüklükler çalışır.
"""
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
        sys.exit(say(f'İNDİRİLEMEDİ: {rec["name"]}\n  {url}\n  {e}\n  ESM (esm-db.eu) erişilebilir mi? Sonra yeniden çalıştırın.',
                     f'COULD NOT DOWNLOAD: {rec["name"]}\n  {url}\n  {e}\n  Is ESM (esm-db.eu) reachable? Then run this again.'))
    try:
        z = zipfile.ZipFile(io.BytesIO(body))
    except zipfile.BadZipFile:
        sys.exit(say(f'ESM zip vermedi: {rec["name"]}\n  {url}\n  yanıt: {body[:300]!r}',
                     f'ESM did not return a zip: {rec["name"]}\n  {url}\n  reply: {body[:300]!r}'))
    names = set(z.namelist())
    for n, pin in rec['files'].items():
        if n not in names:
            sys.exit(say(f'ESM zip\'inde yok: {n}\n  {url}\n  zip: {sorted(names)}',
                         f'not in the ESM zip: {n}\n  {url}\n  zip: {sorted(names)}'))
        data = z.read(n)
        if esm_digest(data) != pin:
            sys.exit(say(f'ÖZET TUTMADI: {n}\n  {url}\n  ESM kaydı değişmiş olabilir; sahne bu kayıtla ayarlandı, denetlemeden kullanmayın.',
                         f'HASH MISMATCH: {n}\n  {url}\n  The ESM record may have changed; the scene was tuned with this one, do not use it unchecked.'))
        write(n, data)
    return len(rec['files'])


def check_esm():
    """--check: which ESM files are missing or not the pinned ones; nothing is downloaded"""
    wrong = 0
    for rec in ESM_RECORDS:
        for n, pin in rec['files'].items():
            if not os.path.exists(os.path.join(HERE, n)):
                print(say(f'  yok: {n}', f'  missing: {n}'))
                wrong += 1
            elif not good(n, pin, esm_digest):
                print(say(f'  özet tutmadı: {n}', f'  hash mismatch: {n}'))
                wrong += 1
    total = sum(len(r['files']) for r in ESM_RECORDS)
    print(say(f'ESM: {total} dosyadan {total - wrong} tanesi yerinde ve doğru' + ('' if not wrong else '; indirmek için: python assets/deprem/fetch.py'),
              f'ESM: {total - wrong} of {total} files present and correct' + ('' if not wrong else '; to fetch them: python assets/deprem/fetch.py')))
    return wrong == 0


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
            sys.exit(say(f'zip\'te yok: {n}\n  {path}\n  TADAS\'tan kayıt 20201030115124_3513, "mp", "Acc", "asc2" olarak mı indirildi?',
                         f'not in the zip: {n}\n  {path}\n  Was record 20201030115124_3513 downloaded from TADAS as "mp", "Acc", "asc2"?'))
        if whole_digest(found[n]) != pin:
            sys.exit(say(f'ÖZET TUTMADI: {n} ({path})\n  sahnenin ayarlandığı kayıt değil (başka bir işlem tipi ya da biçim mi?)',
                         f'HASH MISMATCH: {n} ({path})\n  not the record the scene was tuned with (another processing type or format?)'))
        write(n, found[n])
    print(say(f'alındı: Bayraklı (TK.3513), {len(BAYRAKLI)} dosya', f'taken: Bayrakli (TK.3513), {len(BAYRAKLI)} files'))


def check_bayrakli():
    bad = [n for n, pin in BAYRAKLI.items() if os.path.exists(os.path.join(HERE, n)) and not good(n, pin, whole_digest)]
    missing = [n for n in BAYRAKLI if not os.path.exists(os.path.join(HERE, n))]
    if not bad and not missing:
        print(say('Bayraklı (TK.3513): yerinde ve doğru', 'Bayrakli (TK.3513): present and correct'))
        return True
    tr = ('\nBayraklı kaydı (2020 Sisam depremi, TK.3513; büyüklük 3 ve 4 bunu kullanır) elle indirilir:\n'
          + ''.join(f'  yok: {n}\n' for n in missing) + ''.join(f'  özet tutmadı: {n}\n' for n in bad) +
          '  1. https://tadas.afad.gov.tr , "Misafir Girişi"\n'
          '  2. 30.10.2020 11:51:24 depremi (Mw 6,6, Sisam; TADAS olay 10614), istasyon 3513 (İzmir Bayraklı),\n'
          '     kayıt 20201030115124_3513\n'
          '  3. işlenmiş olarak indirin: veri tipi "mp", "Acc" (ivme), biçim "asc2"\n'
          '  4. python assets/deprem/fetch.py --zip <indirilen zip>\n'
          '     (ya da zip\'i açıp 20201030115124_3513_mp_Acc_E.asc, _N.asc, _U.asc dosyalarını assets/deprem/ içine koyun)\n'
          '  Bu kayıt olmadan sahne açılır; 3 ve 4 dışındaki bütün büyüklükler çalışır.')
    en = ('\nThe Bayrakli record (2020 Samos earthquake, TK.3513; magnitudes 3 and 4 use it) is downloaded by hand:\n'
          + ''.join(f'  missing: {n}\n' for n in missing) + ''.join(f'  hash mismatch: {n}\n' for n in bad) +
          '  1. https://tadas.afad.gov.tr , "Misafir Girişi" (Guest Login)\n'
          '  2. the earthquake of 2020-10-30 11:51:24 (Mw 6.6, Samos; TADAS event 10614), station 3513 (Izmir Bayrakli),\n'
          '     record 20201030115124_3513\n'
          '  3. download it processed: data type "mp", "Acc" (acceleration), format "asc2"\n'
          '  4. python assets/deprem/fetch.py --zip <the downloaded zip>\n'
          '     (or unzip it and put 20201030115124_3513_mp_Acc_E.asc, _N.asc, _U.asc in assets/deprem/ yourself)\n'
          '  Without it the scene opens and every magnitude but 3 and 4 works.')
    print(say(tr, en))
    return False


def main():
    dil.choose(dil.argv_lang())   # before the parser: --help is given in the chosen language too
    ap = argparse.ArgumentParser(description=say(HELP_TR, __doc__), formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--zip', help=say('TADAS\'ın 20201030115124_3513 kaydı için verdiği zip', 'the zip TADAS gave for record 20201030115124_3513'))
    ap.add_argument('--check', action='store_true', help=say('yalnız denetle, indirme', 'only check, download nothing'))
    ap.add_argument('--lang', choices=('tr', 'en'), help=say('iletilerin dili (öntanımlı: uygulamanınki, user.cfg\'den)',
                                                           'the language of the messages (default: the application\'s, from user.cfg)'))
    args = ap.parse_args()
    if args.check:
        ok = check_esm()
        sys.exit(0 if check_bayrakli() and ok else 1)
    if args.zip:
        take_zip(args.zip)
    total = 0
    for rec in ESM_RECORDS:
        n = fetch_esm(rec)
        total += n
        if n:
            print(say(f'indirildi: {rec["name"]} ({n} dosya)', f'downloaded: {rec["name"]} ({n} files)'))
    print(say(f'tamam: ESM, {len(ESM_RECORDS)} kayıt, {total} yeni dosya', f'done: ESM, {len(ESM_RECORDS)} records, {total} new files'))
    if not check_bayrakli():
        sys.exit(1)


if __name__ == '__main__':
    main()
