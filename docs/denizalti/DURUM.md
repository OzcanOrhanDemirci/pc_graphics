# DURUM · sahne 8 "Denizaltı"

> Tek kaynak durum panosu. Plan `docs/denizalti/PLAN.md`, kaynaklar `docs/denizalti/REFERANS.md`. En yeni en üstte.
> Otonom görev (Özcan, 2026-10-06 sabahı): soru sormadan, bitene kadar; kareler SendUserFile ile.

**Sıradaki taş: D4 · Malzeme ve ayrıntı** (kadran yazıları, etiket levhaları, aşınma, cam), sonra D5 (pus, yoklamalar,
ışık dengesi) ve teçhizatın ikinci geçişi.

## D3 (1. geçiş) · Oda ve teçhizat (2026-10-06 öğle)

- `dz_room.*`: gerçek ölçülerle kabuk (REFERANS §0): 8,23 m, gövde ekseni güvertenin 12 cm üstünde, sacın içi 2,421 m,
  **1" mantar** (FEAT_CORK: boyanın yuvarlattığı taneler), 11 T posta (6×4, 76,2 cm), iki perde ve **oval su geçmez
  kapılar** (51×97 cm, eşik 48 cm, eksenin 1,1 m sancağı; döküm çerçeve, 8 köpek, çift topuzlu kol, pirinç çerçeveli
  pencere), yeşil boyalı güverte levhaları (gömme vidalı), tavanda 6 kablo demeti (örgü yüzey, kuşaklar), 24 cm
  havalandırma kanalı (flanş, askı), borular; 3 kırmızı gece lambası (buhar geçirmez, kafesli küre), harita lambası
  (emaye abajur, tek beyaz kaynak).
- `dz_equip.*`: dalış istasyonu (eğik pano, iki 16" ve bir 8,5" derinlik göstergesi, eğim göstergesi, dümen açısı
  göstergeleri, 70 cm 8 kollu çıplak çelik çarklar, değiştirme valfi kolları, turkuaz vinil oturaklar), Christmas tree
  (9×7 kırmızı/yeşil mercek; iki açıklık hâlâ kırmızı), 6 kollu ana vent manifoldu, hidrolik yağ tankı, trim manifoldu
  (2×8 kırmızı el çarkı + gösterge panosu), telsiz odası bölmesi (alüminyum, sürgülü kapı), harita masası (çekmeceler,
  harita, pergel, kupa), dümen standı (kırmızı bantlı çark, gyro tekrarlayıcı, dümen açısı göstergesi), kule merdiveni ve
  oval kaporta boğazı, iki periskop kuyusu, HP hava manifoldu (18 gösterge, 3 kat valf), elektrik panoları.
- **Hata (bulundu, düzeltildi): oda aynaydı.** "x sancak, y yukarı, z baş" adlandırması baş tarafa bakan biri için sol
  elli: sağ elli dünyada +z'ye bakınca +x solda kalır. Dalış istasyonu (iskele) karede sağda çıktı. Bütün dünya x'te
  aynalanıp üçgen sırası çevrildi (`mirrorX`); bitmiş dünyada x iskeleye.
- **Ölçüm:** 4,69 milyon taban üçgeni, 2,49 milyon köşe, kurulum 1,1 sn. Atlas 8192×13156, **86,8 milyon teksel**;
  pişirme 256 örnek 144 sn, 1024 örnek ~10 dk. Kare 1440p 4x High: 51,5 ms → **küme ayıklaması** (0,5 m hücreler,
  çerçeve dışı kümeler çizilmez, komşu aralıklar birleşir) + kontrol aşamasında arkası dönük yamalar: **36,0 ms**.

## D1 (1. kısım) · Fırın: yönlü ışık haritası (2026-10-06 öğle)

- `dz_atlas.*`: her ızgara bir chart; chart'ın kendi yüzey koordinatları (metre) atlas dikdörtgenine; rafa dizme;
  küçük parçalar (< 6 cm) iki kat ince. Teksel-yüzey eşlemesi: teksel merkezi üçgenin içinde ya da 0,75 teksel
  yakınında (chart kenarı da aydınlanır), sonra boş kenar teksellerine komşu ortalaması (padding).
- `dz_bake.*` + `device/dz_bake.cu` (OptiX, NVRTC): teksel başına N yol; lambalara doğrudan örnekleme (küre ışık,
  gölge ışını: kafes gölgesi düşer), sekmelerde yeniden doğrudan örnekleme, Rus ruleti; gelen ışık L1 küresel
  harmoniğe (miktar + yön). Ayrıca dört ana lambanın teksel başına görünürlüğü (parlamanın pişmiş gölgesi: 2010'ların
  "sabit ışık" yolu; canlıda yalnız okunur). Kısa başlatmalar (~250 ms, sürücünün 2 sn bekçisinin çok altında).
  Önbellek `cache/denizalti/isik.bin` (git dışı), anahtar: geometri + lambalar + ayarlar.
- Gölgelendirici: yaygın ışık L1'den doğrusal olmayan kurulumla (Geomerics: negatif lob yok), ortam yansıması
  harmoniklerden (yoklamalar gelene kadar), lamba parlaması küre ışık (Karis) × pişmiş görünürlük.
- **Ölçüm:** test duvarı 8192×1953 atlas, 8,96 milyon teksel (%56 dolu); **64 örnek 2,7 sn, 1024 örnek 16,3 sn**.
  Kare 1440p 4x High: 16,6 ms (D0'ın geçici ışığıyla 13,5 ms).
- **Bulunan hata:** ilk pişirmede duvarda tek tük parlak teksel. Sebep: yarım küreyi kosinüsle örnekleyip RADYANSI
  harmoniğe izdüşürürken ağırlık π/cos sıyırma açısında patlıyor. Düzgün yarım küre örneklemesi (ağırlık 2π, sınırlı):
  benekler gitti. Kalan: 1024 örnekte ince lekelenme → son pişirme daha çok örnekle.
- Hata: `dilate` 16 kanallık geçici diziyle 20 kanalı yazıyordu → yığın taşması (0xC0000409, çıktısız). Düzeltildi.

## D0 · Hat ve test duvarı (2026-10-06 öğle)

**Yapılan:**
- Sahne sözleşmesine `texture()` eklendi: sahne kendi çizdiği GL dokusunu verir (`src/app/scene.h`, `app.cpp`).
  Öteki sahneler değişmedi (`image()` yolu aynen).
- `dz_gl.*`: OpenGL 4.6 yükleyici (Khronos `glext.h`, `third_party/fetch.sh` indirir).
- `dz_geom.*`: parça kiti. Her kenar yuvarlatılmış; ızgara yamaları; tüp, dirsek, halka, yuvarlak silindir, altıgen,
  yuvarlak kutu, simit, kubbe, küre, gövde duvarı, torna (lathe), ekstrüzyon. `finishMesh`: aynı konumdaki köşelerin
  normalleri ortalanır (yer değiştirme normali, çatlak olmasın), teğetler.
- `dz_parts.*`: boru hattı (dirsekli), flanş çifti + saplama ve somunlar, el çarkı, glob vana, gösterge, gövde duvarı,
  T kesitli posta + kaynak dikişi, tread güverte levhası.
- `dz_world.*`: test duvarı (iskele tarafı gövde, 4 posta, 5 vanalı manifold, flanşlar, gösterge panosu, 3" boru).
- `dz_render.*` + `glsl/`: tessellation (kenar çarpanı, kenarın uzunluğu bölü hedef boyun o uzaklıktaki karşılığı;
  yalnız kenarın iki ucundan hesaplanır), Phong tessellation (paylaşılan yer değiştirme normaliyle), analitik yüzey
  ayrıntısı (sac dalgası + portakal kabuğu boya, kaynak dalgası, diş, tread, döküm) hem yer değiştirmede hem normalde
  aynı işlevle; çok örnekli hedefte örnek başına gölgelendirme (gerçek SSAA); ton sıkıştırmalı çözümleme; AgX + sabit
  titreşim; pencereye Catmull-Rom/çadır süzgeçli uydurma. Ters derinlik.
- Canlı sahne: galeride 08, panelde Çözünürlük / SSAA / Tessellation / Grafik / Işık / Efekt; WASD, Tab bakış, sol
  sürükle bakış, sağ tık dürbün (görüş 4 kat dar), Shift yavaş, R baş.
- Işık D0'da **geçici** (üç lamba, gölgesiz): yalnız hattı ölçmek için. Özcan'a bu kareler gönderilmedi.

**Ölçümler (RTX 4080, sürücü 617.14):**
- GL 4.6; çoklu örnek renk ve derinlikte **32'ye kadar**: 16x SSAA donanımın kendi 16 örneğiyle (2×2 ölçek gerekmedi).
  Tessellation çarpanı en çok 64.
- Test duvarı: 601 bin taban üçgeni, 324 bin köşe, kurulum 104 ms. En ince üçgen 2,2 mm/64... (taban kenarı 12 mm'den).
- **Kare süresi, 1440p** (GPU zaman damgası, 8 karenin ortancası), ilk sürüm → analitik türev sonrası:

| Tess | 1x | 2x | 4x | 8x | 16x |
|---|---|---|---|---|---|
| Low | 2,1 → **1,0** | 2,4 → 1,2 | 5,5 → **2,2** | 11,0 → 4,9 | 20,2 → **8,2** |
| Medium | 3,2 → 1,7 | 3,4 → 1,9 | 7,6 → **3,4** | 16,3 → 7,7 | 30,5 → **13,3** |
| High | 7,9 → 4,9 | 10,3 → 7,0 | 24,0 → **13,5** | 53,7 → 30,4 | 109,4 → **57,0** |

  Maliyet örnek sayısıyla doğrusal: örnek başına gölgelendirme baskın. İlk sürümde normal ayrıntısı merkezi farkla beş
  kez gürültü hesaplıyordu; analitik türevle bir kez → yarıya indi. Çözümleme 16x'te 0,3 ms.
- **Çatlak testi** (`--test catlak`, arka yüzler macenta, ayıklama kapalı): 4 kamera × 3 seviye × 1x/4x. Bulgu: milyon
  pikselde birkaç macenta. Bölme kapalıyken (`--bolmesiz`) kayboluyor, yer değiştirmeden bağımsız. Yakından bakınca:
  biri borunun **siluetinde** (ayıklama kapalı görünümde ön ve arka yüzün eşit derinlik çakışması), öteki somun
  kapağının ortasında tek pikseller. Köşeleri konuma göre sabit sırayla toplamak (TES) değiştirmedi. Normal çizimde
  (arka yüz ayıklı) görünür bir delik gözlenmedi, ama bu **ölçülmedi**: test, oda kapalı kabuk olunca normal çizimle
  yeniden kurulacak (D3).
