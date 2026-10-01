# DURUM · sahne 2 "Fener"

> Tek kaynak durum panosu. Plan ve Özcan'ın tarifi `docs/fener/PLAN.md`'de.

## Şu an
- **2026-10-01 · F0 değer etüdü bitti, Özcan'ın seçimi bekleniyor.** Üç kompozisyon + "huzme kamerada" anı:
  `docs/fener/etud/` (A pencere, B yücelik, C yakın; A'nın tepe anı). Üretici: `python tools/fener_etud.py --variant all --w 1600`
  (`--phi 2` huzmeyi kameraya çevirir). Taslak başına ~1 dk (1600 px, numpy).

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| F0 Değer etüdü | ✅ benim tarafım | 3 kompozisyon + tepe anı; Özcan seçecek |
| F1 Altyapı (sahne seçimi) | ⏳ | |
| F2 Kaba 3D | ⏳ | |
| F3 Işık | ⏳ | |
| F4 Su | ⏳ | |
| F5 Son işlem | ⏳ | |
| F6 Hareket, döngü, video | ⏳ | |
| F7 Sanat turları | ⏳ | |

## Etüdün sayıları (seçilen taslak 3D'ye bunlarla taşınır)
| | A Pencere | B Yücelik | C Yakın |
|---|---|---|---|
| kamera yüksekliği / bakış yukarı / dikey FOV | 5 m / 5° / 38° | 4 m / 10° / 44° | 7 m / 4° / 34° |
| adaya uzaklık / tepe yüksekliği | 470 m / 44 m | 900 m / 46 m | 330 m / 40 m |
| ön plan | iki yakın köknar + ince ölü gövde | tek köknar solda, sağ açık | iki köknar + ince gövde |

Ortak: fener kulesi 36 m (taban yarıçapı 4,4 m, tepe 3,1 m), lamba ~tepe + 39 m; kulübe fenerin 74 m sağında,
22 m önde, sundurmalı ve hafif yamuk; fiyort duvarları 6 / 10 / 16 km'de, 0,25-5 km yüksekliğinde; ay sol üstte.

## Etüt sırasında öğrenilenler (ölçülerek)
- **Kör ayar döngüsü pahalı:** tepe anı dört deneme boyunca "her yer beyaz" kaldı; bileşenleri tek tek ölçünce suçlu
  sis değil, benim eklediğim parlama yıldızının geniş halkasıydı (sisin ~20 katı). İlk iş ölçmek olmalıydı.
- **Gerçek fener huzmesi kameraya çarpmaz:** lamba ~82 m yüksekte, 470 m'de kamera eksenin ~9,7° altında. Yatay ve dar
  bir huzme hep üstten geçer. Çözüm (sanat): huzme gerçek gibi neredeyse yatay kalır ve kameranın üstünden geçer
  (tepede lambaya yakınsayan ışık sütunu); "kameraya vuran ışık" = huzmenin yönü kameraya döndüğünde lambanın
  göz kamaştırması.
- **Kameranın içinde olduğu huzme bütün sisi yakar** (ileri saçılım fiziksel olarak ~100 kat). Gerçekçi ama düz ve
  güzel değil; ön/yan oranı ~5'e indirildi.
- **Değer ağı:** dalgalar değer gürültüsüyle (ızgara) yapılınca blok ve yatay şerit verdi; rastgele yönlü 40 dalganın
  toplamı (2. harmonikle sivri tepe) temiz kırık yansıma verdi.
- **Fiyort duvarları ölçekle görünür oldu:** 5-14 km'de 0,2-2,6 km yükseklik, adanın ormanının arkasında kalıyordu;
  6-16 km'de 0,25-5 km'ye çıkınca ayın altında beliriyor.

## Bilerek fizik dışı (sanat) ayarları
Etütte (3D'ye taşınırsa aynı adla işaretlenecek):
- huzme saçılımında ön/yan oranı ~5 (gerçek sis ~100), ve huzmenin sisteki parlaklığına yumuşak tavan (`beamCap`);
- huzmeye ek zayıf, geniş, aşağı eğik yayılma lobu (`spill`): ormanı ve denizi süpürsün diye;
- göz kamaştırma yıldızı huzmenin **yönüne** bağlı, kameradaki gerçek şiddete değil (`face()`);
- ağaç tepelerinin huzmeden aldığı ışığa yumuşak tavan; ay diski büyütülmüş (0,55°, gerçek 0,26°).
