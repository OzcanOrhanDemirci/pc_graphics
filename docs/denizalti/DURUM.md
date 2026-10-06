# DURUM · sahne 8 "Denizaltı"

> Tek kaynak durum panosu. Plan `docs/denizalti/PLAN.md`, kaynaklar `docs/denizalti/REFERANS.md`. En yeni en üstte.
> Otonom görev (Özcan, 2026-10-06 sabahı): soru sormadan, bitene kadar; kareler SendUserFile ile.

**Sıradaki taş: D1 · Fırın** (UV2 açılımı, OptiX yol izleme ile yönlü ışık haritası, yoklamalar, pus hacmi).

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
