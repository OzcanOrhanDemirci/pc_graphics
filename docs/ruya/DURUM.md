# Rüya · Sahne 14

## Revizyon · 2026-11-10 (Özcan'ın 3 maddesi)

**Özcan'ın bakacağı yer:** önce `python assets/ruya/fetch.py` (CC0 Poly Haven taramaları,
11 set, 4K ve 8K), sonra `build\pc_graphics.exe --scene ruya`. 1–6 altı bakış, Shift hızlı uçuş.

**Üç parça ne değiştirdi** (sahnenin fikri, ışığı ve sisi aynı):

1. **Adalar** (155b498): ızgaraya serpilmiş yuvarlak somunlar yerine eklemli, oyuklu,
   kırık başlıklı sütun birleşimleri; beş hücrelik masifler bir ana sütunun çevresinde,
   aralarında kıvrılan boş koridorlar; tepe, sahanlık ve duvarda sık bitki.
2. **Gerçek malzeme** (576f169): her yüzey CC0 Poly Haven taraması (kaya, katman, zemin,
   kesme taş 8K; kabuk, çatı, ahşap, metal, iki yaprak atlası 4K), ışın izine göre anizotrop
   süzme; taçlar alfa kesmeli dal kartları.
3. **Titreme ve takılma** (bu commit):
   - Yaprak alfası pikselin 12 örneğinde (0, 1) aralığına eşit dağıtılmış eşiklerle sınanıyor
     (sabit, kareden kareye değişmez); yaprak alfası ve dokusu örneğin kendi payında süzülüyor.
   - Yüzey pikselin her çeyreğinde bir kez değerlendiriliyor (önceden piksel başına bir);
     sisin kontrastın %45'inden fazlasını aldığı yerde tek değerlendirme ve tek ortam ışını.
   - Ortam ışını örnek başına 3 → 2; doku anizotropisi 2 → 16.
   - Ada ağları sekiz işçi iş parçacığında ışık çevresinin 240 m ötesine kadar önceden
     üretiliyor, GPU'ya ada ada karede en çok ~2 ms bütçeyle yükleniyor, ön ısıtılmış bellek
     havuzundan (2 GiB). Işık çevresindeki hücre hazır değilse kare bekler (yalnız ilk kare
     ve 1–6 sıçramaları); görüntü zamanlamaya bağlı değil.

**Ölçüm yöntemi.** `python tools/ruya_titreme.py --out KLASOR -- ROTA`: canlı (12 örnek) ile aynı
gölgelendirmenin 128 örnekli kaba kuvvet referansı (`--reference`: her örnek kendi yüzeyini
kendi payında gölgeler), sonra `tools/kararlilik.py --isi`. 1440p, 24 kare. Rotalar:
(a) `--route`; (b) alçak uçuş sütunlar arası `--view 1 --eye 858.79,20,-105.96 --pitch 0 --drift 12,0,0,0`;
(c) taç ve çimen önünde yanal kayma `--view 3 --eye 84.6,1.5,493.4 --drift 0,3,0,0`.
Kayıtlar `captures/ruya/revize_titreme/` (once_*, amb_* = son hâl; `isi_once_sonra_*.jpg`,
`kirp_*_once_sonra_ref.png`). Taban (0,00121) eski sahnede ölçülmüştü; yeni sahne çok daha
sık bitki ve doku taşıdığından aynı rotada referansın kendi değişimi iki katı (0,042'ye karşı 0,020).
Büyük kaydırmada bu ölçü canlıyla referans arasındaki durağan farkı (ör. bulanıklık) da sayar.

| Kıpırtı (ölçülen) | piksel ort | p99 | 8×8 blok | FLIP | yeşil (taç/çimen) |
| --- | ---: | ---: | ---: | ---: | ---: |
| (a) rota, önce → sonra | 0,00681 → **0,00386** | 0,080 → 0,038 | 0,00291 → 0,00112 | 0,0237 → 0,0138 | 0,0200 → 0,0082 |
| (b) alçak uçuş | 0,00091 → **0,00081** | 0,0101 → 0,0078 | 0,00016 → 0,00013 | 0,0063 → 0,0058 | 0,0146 → 0,0087 |
| (c) yanal kayma | 0,01202 → **0,00798** | 0,101 → 0,055 | 0,00226 → 0,00125 | 0,0367 → 0,0281 | 0,0294 → 0,0110 |

"Yeşil": referansta yeşili baskın pikseller (yaklaşık taç/çimen maskesi). FLIP her rotada
düştü: kararlılık bulanıklaşarak gelmedi (kırpımlar da aynı).
**Hedef tutmadı:** rota (a) 0,00386, tabanın (0,00121) üstünde. 12 örnekte her yüzeyi ayrı
gölgelemek bile 0,00323'te kalıyor (+7 ms); kalan fark taç kenarı, ince korkuluk ve yaprak
katmanlarında 12 örneğin kendisi.

| Kare süresi, RTX 4080, 1440p, 12 örnek (ölçülen) | p50 | p95 | en uzun | upload_ms en uzun |
| --- | ---: | ---: | ---: | ---: |
| (a) `--route --frames 200 --warmup 5`, önce (576f169, 2 koşu) | 25,8–26,0 | 33,6–33,9 | 49,5–50,6 | 21,7–22,7 |
| (a) sonra (3 koşu) | **24,2–24,3** | **31,6–31,7** | 34,5–36,0 | **2,3–2,4** |
| (b) alçak uçuş 200 kare, önce → sonra | 25,5 → 23,7 | 27,1 → 25,1 | 59,1 → 27,0 | 34,4 → 2,3 |
| 36 m/s ileri 300 kare, önce → sonra | 18,8 → 16,6 | 28,3 → 20,2 | 50,8 → 23,1 | 35,6 → 2,5 |

Sonra: hiçbir karede 5 ms üstü yükleme yok, hiçbir uçuş karesi hücre beklemedi. GPU tahsisi
6,7–6,9 GB (havuzun boş kısmı hariç; havuz 2 GiB, en yüksek kullanım 1,44 GiB).
`--scene ruya --test`, `--lifecycle` ve gizli `--test-live` geçti; yeni test `ada-akisi-dogrudan-ile-ayni`
iş parçacığında üretilen ağların doğrudan üretimle bayt bayt aynı olduğunu sınıyor (bozuk
üreticiyle 30 farkla kaldı).

**Yapılmayan:** normal haritası için Toksvig/LEAN (ısı haritasında parlama odağı görülmedi,
ölçülmedi); ince geometri (korkuluk) ve gölge kenarı için ayrı düzeltme; örnek sayısını artırma.
Piksel başına döndürülmüş alt piksel deseni denendi, etkisizdi (0,00435 → 0,00438), geri alındı.

**NE DOĞRULANMADI:** canlı ekran açılmadı; gerçek fare hissi, monitör sunumu ve 1–6 sıçramasındaki
bekleme süresi gözle görülmedi. Bir ağacın içine uçulduğunda (ekranı dolduran yaprak) kare ~38 ms
ölçüldü; bütçe dışı. Kare süreleri koşudan koşuya ~1 ms oynuyor.

## Önceki sürüm · 2026-11-08 (Claude, Özcan'ın 6 maddesi)

- Canlıda yol izleme ve gürültü giderici kalktı: sabit 12 alt piksel örneği, güneşe tek gölge
  ışını, sabit ortam yönleri. 720p kıpırtı 0,00121 (eski yol izleyici 0,00205).
- Her hücre bulut denizinden yükselen tek parça kireçtaşı sütunu; tohum (x, z) / (−x, −z)
  ayna kopyası hatası düzeltildi. Sis iki katmanlı, uzak çizim 640 m.
- Gizli CUDA-GL 120 kare p50 14,84 ms (o sürümün dokusuz hâli).

## Önceki sürüm · 2026-11-07 (Codex)

- İlk sahne: 27 ortak prototip, koordinattan yerleşim, kare başına geçici OptiX yerleşim
  tablosu (kamera ve ışık maskeleri ayrı), 3 örnek + 3 sekme + OptiX gürültü giderici.
- Uçuş 200 kare p50 23,80 ms, p95 26,29 ms; en ağır sabit bakış p50 ~30,2 ms.
- Kamera 27 cm yarıçaplı küre, çizilen üçgenlere karşı çarpışır; 20 boşaltma/yeniden üretme
  döngüsünde HDR baytları aynı.
- Ayrıntılı kayıtlar ve deneyler git geçmişinde (`docs/ruya/DURUM.md`, 576f169 ve öncesi).
