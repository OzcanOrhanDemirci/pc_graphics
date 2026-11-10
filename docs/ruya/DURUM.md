# Rüya · Sahne 14

## Yarım kalan bulgular · 2026-11-10

**Görsel hatalar (P1).** v4'te dik duvarlardaki sert kenarlı koyu üçgen ve elmasların nedeni:
güneşe sırtını dönen kaba üçgen, yumuşak normali ışığa baksa da tümden gölgede sayılıyordu.
Güneşe bakma kararı artık yumuşak normale göre veriliyor; gölge ışını normal ile üçgen arasındaki
eğim kadar (en çok 2,5 m × eğimin sinüsü) yukarıdan çıkıyor. Elmas içi parlaklık 0,29'dan
0,39–0,43'e çıktı (çevre 0,49); kenarda ince bir şerit kaldı.

**Duvarda çimen.** v4'teki büyük yama 45°'den düz yüzde (hangi şekil olduğu belirlenmedi),
kurala uygun olduğu için kaldı. Asıl kusur, köşedeki ortalama normalin çimeni dik yüze taşımasıydı;
çimen artık üçgenin kendi eğimiyle de sınırlı; 60°'den dik yüzde çimen pikseli 14 228'den 0'a indi.

**Kaya dokusu.** v5 sütununda 3,2 m kaya dokusunun dikey tekrarı: döndürülmüş kopyanın maskesi
0,045'ten 0,16'ya sıklaştı (0,11 etkisizdi), tekrar zirvesi 0,156'dan 0,025'e indi.
Rota kare süresi p50 +0,2 ms, p95 +0,35 ms (24,2 → 24,4; 31,5 → 31,8).

**Hız ve titreme (P2).** Dokular iki set aynı anda çözülerek açılıyor: 5,9–6,3 s → 3,5–3,7 s,
en yüksek çalışma belleği 1,39–1,44 → 1,64–1,66 GB. Yaprak alfasını ayrı R8 haritaya taşımak
ölçülebilir kazanç vermedi (taç bakışı −0,04 ms, rota 0), geri alındı.

Taç bakışının yüksek süresi (`--eye 93.6927,-4.0545,511.998 --yaw 2.7222 --pitch -0.2763`,
p50 34,4 ms, bütçe dışı) alfadan ya da dokudan gelmiyor: düz malzemeyle de 34,4 ms,
spp 1'de 3,2 ms; maliyet örnek başına, büyük olasılıkla iç içe kartlarda ışın dolaşımında (ölçülmedi).

Normal haritası için Toksvig denendi: rota (a) kıpırtısı 0,00386 → 0,00379, (b) ve (c) değişmedi,
görünüm hafifçe değişti; alınmadı. Seçenekler varsayılan değil: `--set per_sample_shading=1`
rota p95 38,2 ms (+6,5), kıpırtı (a) 0,00333, (b) 0,00061, (c) 0,00513;
`--set spp=16` p95 44,0 ms, (a) 0,00367, (c) 0,00815 (kötüleşti);
`--set ambient_reach=6` p95 −0,4 ms, kıpırtıya etkisi yok, görünüm değişiyor, varsayılan 10 m kaldı.
Varsayılanla P2 görüntüleri f66cb27 ile bayt bayt aynı.

Kod testleri her iki parçada `--test` geçti (başarısız 0 / toplam 51), `--lifecycle` geçti; ölçümler RTX 4080, 1440p, 12 örnek, iki işçi GPU'yu paylaşırken ardışık A/B.

## Yerleşim düzeltmesi · yapı çakışması ve kule tekrarı

**Yapı ayak izleri.** Kök neden: `planCell` yapıları (ana bina, kule, galeri, bahçe) yalnız tepe
yüzeyinin içinde kalıyor mu diye sınıyordu; birbirlerine göre hiç sınanmıyordu (yönler ve
uzaklıklar sabit, küçük adada ana bina, galeri ve kule aynı alana düşüyordu). 32x32 hücrede (önceki yerleşim) 216 yapı çiftinden
67'si ayak izinde çakışıyordu (11-12 courtyard+bahçe 26, 8/9/16/19/20 ile 13 galerisi 33, kule
içerenler 8). Şimdi her yapı, katı (yapraksız) üçgenlerinin 0,5 m ızgarada işaretlenen ayak izi
ile öncekilerinkine 1 m açıklık bırakarak yerleşiyor; engellenen yön 0,4 ve 0,8 rad yana çevriliyor,
yer bulunamazsa yapı konmuyor (494 yerine 456 yapı). Test `yapi-ayak-izleri-cakismaz` eski yerleşimde
67 ile kırmızı, şimdi 0. **v2 bakışı:** kulenin arkasındaki ince duvar ve saçak çakışma değil;
19 numaralı kemer duvarı kuleden ~9 m geride, kameraya sıyırarak bakınca kulenin arkasında
hizalanıyor. Ayak izi çakışmasından değil bakış hizasından; v2 karesinde duruyor (önce/sonra
`captures/ruya/yerlesim/v2_once.png`, `v2_sonra.png`).

**Görünüm değişikliği:** yer bulamayan yapı artık konmadığı için onun düzleştirilmiş alanı da açılmıyor; v4 adasının tepesi ağaçlı düz platform yerine basamaklı bir tepe ve daha az ağaçla görünüyor (son doğrulama, `C:/Temp/ruya_son_dogrulama/kiyas_v4_tam.jpg`). Hata değil, yerleşim düzeltmesinin sonucu.

**Kule tekrarı.** Tek kule ağı (prototip 10) var; yeni varlık eklenmedi. Boy (y ölçeği) artık
(x + 2z) mod 7 sınıfına göre 0,80–1,24 arası yedi basamaktan seçiliyor; komşu hücrelerin sınıfı
hep farklı. Sivri adadaki kulenin genişliği de (önceden sabit 0,5) 0,42–0,62 arası değişiyor. Ölçü (`kule-komsuda-tekrarlanmaz`, 32x32 hücre, 161 kule, 8 komşuluk): genişlik ve
boyu birbirinin %5'i içinde kalan komşu çift oranı 19/98 (%19,4) → 1/96 (%1,0); ikizi olan
kule 35 → 2. Aynı ağ olduğu için ağ düzeyinde tekrar değişmedi.

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

**Hareket telafili kıpırtı** (`tools/kararlilik.py --hareket-telafili`; `ruya_titreme.py` kendiliğinden ekler).
Büyük kaydırmada yukarıdaki ölçü canlı/referans durağan farkını da saydığından eski tabanla kıyas zor;
bu ölçü önceki kareyi derinlik ve kamera pozuyla yeniden izdüşürür (çift doğrusal), derinlik uyuşmazlığı
(%1,5), ekran dışı ve gökyüzü/yüzey karışımı pikselleri maskeler, kalanda
`(L_t - W[L_t-1]) - (R_t - W[R_t-1])` ortalamasını alar. Koşu: `python tools/ruya_titreme.py --out KLASOR -- ROTA`
(canlı koşu `pc_graphics.exe --scene ruya --headless --dump-dir ... --dump-depth` ile kare başına
`depth_XXXX.bin`: kamera + görüş derinliği, 1440p'de 14,7 MB/kare, yalnız ölçüm yakalamada; çizim aynı).
Elde: `python tools/kararlilik.py KLASOR --hareket-telafili`. Çıktıya `kipirti_hareket_telafili` eklenir
(ort, p95, p99, `maske_orani`, `referans_mc_degisim_ort`, aynı maskede telafisiz `referans_ham_degisim_maskeli_ort`);
mevcut anahtarlar değişmez. Sınama (GPU'suz, sentetik): `python tools/test_kararlilik_hareket.py`.

| f66cb27, 1440p, 24 kare (ölçülen) | eski piksel ort | yeni (hareket telafili) | maske | referansın değişimi: telafisiz → telafili |
| --- | ---: | ---: | ---: | ---: |
| (a) rota | 0,00386 | **0,00183** | %8,7 | 0,0393 → 0,0031 |
| (b) alçak uçuş | 0,00081 | **0,00054** | %1,5 | 0,0018 → 0,0009 |
| (c) yanal kayma | 0,00798 | **0,00309** | %5,1 | 0,0313 → 0,0040 |

Doğrulama (6 kare): durağan kamera 0,00000 (eski de 0); gerçek sahnede canlı 64 örnek, referans 128 örnek
(gürültüsüz sayılır), rota (a): eski 0,00133, yeni 0,00090 (kalan 64 ile 128 örnek farkı ve 8 bit);
rota (a) canlı kareye ±0,02 düzgün gürültü eklenince 0,00165 → 0,01194 (sentetik sahnede beklenen 0,0133
çevresinde). Yeniden izdüşümün doğruluğu: referansın kendi değişimi aynı maskede 0,039 → 0,003.
Ölçü ~0 ama tam 0 değil: canlı ve referansın örnekleme farkı (ince doku) çift doğrusal örneklemeyle
tam silinmez. Hedef sonucu değişmedi: (a) 0,00183 de eski tabanın (0,00121) üstünde, ama aradaki fark
artık iki kat daha küçük.

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
