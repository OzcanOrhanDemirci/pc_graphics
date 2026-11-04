"""Sahne 13 Dağ: bütün ölçümler tek koşuda, her sayı DURUM'daki tablonun kaynağı. Exe'nin kopyasından koşar (derleme sürerken de).

  python tools/dag_olc.py [--pencere]     --pencere: ek olarak gerçek pencerede autotest (pencere açılır, Özcan makinedeyken koşma)

Çıktı: captures/dag/olcum.txt ve ekran. Eşikler ölçümden önce yazıldı (docs/dag/PLAN.md, Bitti tanımı):
  en uzak çizilen >= 300 km; delik 0; aşırı budanmış ağda delik > 0 (araç konuşuyor mu); dönüşte ve yürürken piksel kıpırtısı < 0,0053
  (Fener'in TAA tabanı); titreme oranı canlı/referans <= 1,10; sonlu olmayan örnek 0; bellek <= 15 GB, kare >= 30 FPS (pencere).
"""
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, 'build', 'pc_graphics.exe')
COPY = os.path.join(ROOT, 'build', 'dag_olc.exe')
OUT = os.path.join(ROOT, 'captures', 'dag', 'olcum.txt')
lines = []


def say(s):
    print(s, flush=True)
    lines.append(s)


def run(args, timeout):
    p = subprocess.run([COPY] + args, cwd=ROOT, capture_output=True, text=True, errors='replace', timeout=timeout)
    return p.returncode, p.stdout + p.stderr


def check(name, ok, value):
    say(f"{'GECTI ' if ok else 'KALDI '} {name}: {value}")
    return ok


shutil.copy2(EXE, COPY)
good = True

code, o = run(['--scene', 'dag', '--headless', '--menzil'], 300)
far = float(re.search(r'en uzak cizilen yer ([\d.]+) km, yon ([\d.]+)', o).group(1))
good &= check('en uzak cizilen yer (>= 300 km)', far >= 300.0, f'{far} km')
m = re.search(r'gorunen en uzak .*: (\d+) km', o)
say(f'       gorunen en uzak (ortanca kontrast >= %2): {m.group(1)} km')
for ln in o.splitlines():
    if ln.startswith('dag: yer:') and any(k in ln for k in ('Elbrus', 'Ararat', 'Shkhara', 'Ushba', 'Aragats', 'Kazbek', 'Paland')):
        say('       ' + ln[9:].strip())
    if ln.startswith('dag: kontrast') and re.search(r' (100|200|300|350|380)-', ln):
        say('       ' + ln[5:].strip())

code, o = run(['--scene', 'dag', '--headless', '--delik', '--w', '1280', '--h', '720'], 300)
m = re.search(r'delik: (\d+) isin, (\d+) yerin', o)
good &= check('delik (0)', int(m.group(2)) == 0, f'{m.group(2)} / {m.group(1)} isin')

code, o = run(['--scene', 'dag', '--headless', '--delik', '--w', '1280', '--h', '720', '--set', 'far.tolerance=-0.003', '--set', 'far.eyeRing=0'], 300)
m = re.search(r'delik: (\d+) isin, (\d+) yerin', o)
good &= check('delik araci konusuyor mu (bozuk agda > 0)', int(m.group(2)) > 0, f'{m.group(2)} / {m.group(1)} isin')
bad = os.path.join(ROOT, 'cache', 'dag', 'far_1_1_1_2000_3000_30000_-300_0.bin')
if os.path.exists(bad):
    os.remove(bad)

# the cameras that once gave samples that were not finite, and the views
code, o = run(['--scene', 'dag', '--headless', '--out', 'captures/dag/olc', '--bench', '20', '--cam', '0 0 0 118 -6 6', '--cam', '0 0 0 299 -2.3 5', '--cam', '-4 0 -2 299 -2 45',
               '--cam', '0 0 0 30 -3 1.5', '--cam', '0 0 0 299 -70 60', '--view', '0', '--view', '1', '--view', '2', '--view', '3', '--view', '4'], 300)
bads = [int(x) for x in re.findall(r'sonlu olmayan (\d+)\)', o)]
good &= check('sonlu olmayan ornek (0)', sum(bads) == 0, f'{sum(bads)} ({len(bads)} kamera)')
fps = [float(x) for x in re.findall(r'\(([\d.]+) FPS\)', o)]
say(f'       --bench FPS (pencere bedeli yok): en az {min(fps):.1f}, en cok {max(fps):.1f}')

for name, args in (('don', ['--yol', 'don', '--hiz', '0.35', '--cam', '0 0 0 0 -3 45']), ('ileri', ['--yol', 'ileri', '--cam', '-1 0 -0.5 299 -3 45'])):
    d = f'captures/dag/stab_{name}'
    shutil.rmtree(os.path.join(ROOT, d), ignore_errors=True)
    run(['--scene', 'dag', '--headless', '--out', d, '--kararlilik', '16'] + args, 300)
    k = subprocess.run([sys.executable, 'tools/kararlilik.py', d], cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    j = json.loads(k[k.index('{'):])
    t = subprocess.run([sys.executable, 'tools/titreme.py', d], cwd=ROOT, capture_output=True, text=True, errors='replace').stdout
    oran = float(re.search(r'oran ([\d.]+)', t).group(1))
    px = j['kipirti_piksel']['ort']
    good &= check(f'{name}: piksel kipirtisi (< 0,0053)', px < 0.0053, f"{px:.5f}, blok {j['kipirti_blok']['ort']:.5f}, FLIP {j['netlik_flip']['ort']:.4f}")
    good &= check(f'{name}: titreme orani (<= 1,10)', oran <= 1.10, f'{oran:.2f}')

if '--pencere' in sys.argv:
    p = subprocess.run([COPY, '--autotest-only', 'dag'], cwd=ROOT, capture_output=True, text=True, errors='replace', timeout=300)
    o = p.stdout + p.stderr
    rows = re.findall(r'autotest (dag[^:]*): ([\d.]+) fps, kare ([\d.]+) ms, kart bellegi ([\d.]+) / ', o)
    for r in rows:
        say(f'       {r[0]}: {r[1]} FPS, kart {r[3]} GB')
    sixteen = [float(r[1]) for r in rows if '32' not in r[0] and '64' not in r[0]]
    good &= check('pencere FPS, 16 ornek (>= 30)', min(sixteen) >= 30.0, f'en az {min(sixteen)}')
    mem = max(float(r[3]) for r in rows)
    good &= check('bellek (<= 15 GB)', mem <= 15.0, f'{mem} GB')
    good &= check('korkuluk: hep icerde', 'icerde 0' not in o, f"{o.count('icerde 1')} olcum")
    good &= check('otomasyon acilis yonu 299', 'yon 299.0' in o, 'autotest')

say('SONUC: ' + ('hepsi gecti' if good else 'KALAN VAR'))
with open(OUT, 'w', encoding='utf8') as f:
    f.write('\n'.join(lines) + '\n')
os.remove(COPY)
sys.exit(0 if good else 1)
