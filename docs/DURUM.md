# DURUM — pc_graphics · sahne 1: "Altın saatte cam ve taş"

> Tek kaynak durum panosu. Her faz sonunda güncellenir; bağlam sıkıştırılınca BASLA.md ile birlikte okunur.

## Şu an
- **2026-10-01, Özcan'ın geri bildirimi:** "teknik olarak etkileyici ama görsel olarak değil: ışık var, küp var". Kısa tur
  (motor aynı): karanlık oda, 5 yarık, huzmelere asılı 14 kristal prizma (`crystal.count`), sıcak beyaz dengesi, güçlü
  parlama. Yeni varsayılan kamera 0 "gökkuşağı" (`captures/FINAL_gokkusagi_dn.png`). Canlı 24 fps, durağan ~8 örnek/s.
- **2026-10-01 · F0-F5 benim tarafımda bitti; Özcan'ın gözü bekleniyor** (F5 kapısı: "şaşırdım" ya da düzeltme listesi).
- Gösteri: `build\pc_graphics.exe` → kahraman kare (kamera 0, prizma natürmortu) durağan modda açılır. Tuşlar `GOSTERI.md`.
- Gönderilenler: F1_kose (F1), sheet_cv (F3 ara), hero_q4_dn (F5 aday), FINAL_prizma_dn (son kare, 8192 örnek).
- Özcan'dan yanıt yok (oturum boyunca); kararlar kendi yargımla verildi, aşağıda gerekçeli.
- Açık kalanlar: canlı modda prizma tayfı karıncalı (durağanın kalitesine yetişmiyor); gök modelinde çoklu saçılım yok;
  kostiğin 2'den derin dolaylı sekmeleri toplanmıyor (~%0,7 eksik); güneş canlıda hareket etmiyor.

## Nasıl çalıştırılır
- Derleme: `cmd /c C:\dev\pc_graphics\build.bat` (CMake + Ninja, VS 2026, `third_party/cuda`). PowerShell'de
  `cd` cmd'ye geçmez: build.bat'ı **tam yoluyla** çağır.
- Pencere: `build\pc_graphics.exe` (kenarlıksız tam ekran, birincil monitör). Tuşlar main.cpp başında.
- Başsız: `build\pc_graphics.exe --headless --width 1280 --height 720 --spp 64 --out captures/ad` → PNG + EXR,
  güneş/gök tanı satırı, ms/başlatma, NaN sayacı.
- Testler: `--selftest` (spektral), `--furnace` (beyaz fırın), `--autotest` (pencere: H, F5, S, Esc zaman çizelgesi).
- Ekran yakalama (monitörde gerçekte görünen): `tools/screengrab.ps1 -Out <mutlak yol> -DelaySeconds 2.5`.
- Sahne ayarları `scene.cfg` (F5 yeniden okur). Cihaz kodu `src/device/*.cu|h`, F5'te NVRTC ile yeniden derlenir.

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| F0 İskelet | ✅ 10-01 | pencere 2560×1440 @165 Hz RTX 4080'de (GL_RENDERER doğrulandı), F5, S (PNG+EXR), HUD, NVML |
| F1 Işık doğru | ✅ 10-01 | fırın %0,03 · D65 beyazı ±0,001 · yakınsama 1/√n · güneş ~3480 K · 3 yarık + tavan yarığı + köşe kamerası |
| F2 Malzeme ve detay | ✅ 10-01 | Sellmeier F/d/C, Newton ince film dizisi (seçim testinde); özel küreler (giriş+çıkış); terrazzo, beton, sıva |
| F3 Kostik ve hacim | ✅ 10-01 | spektral fotonlar + PPM; foton ışınları (hacimsel kostik); huzme aralık örneklemesi; 5° güneşle PT'ye karşı oran 0,991 |
| F4 Canlı mod | ✅ 10-01 | 720p + TEMPORAL_UPSCALE2X → 1440p, 34-35 fps (RTSS de 35 diyor); titreşim 0,73/255; Özcan'ın gözü bekliyor |
| F5 Sanat ve zirve | ✅ benim tarafım | natürmort, perde duvar, serin gök penceresi, parlama, tayf bölme, FLIP referans; Özcan'ın gözü bekliyor |

## Kararlar
- **Yapı:** tüm OptiX programları tek `programs.cu` (NVRTC, OptiX-IR); raygen içinde `optixTraverse` +
  `optixReorder` (SER, malzeme türüne göre) + gölgelendirme. Kapalı-isabet programı yok. Gölge ışını `optixTrace`,
  yalnız küreler için any-hit (sabun filmi geçirgenliği).
- **Spektral:** kahraman dalga boyu ×4, pbrt-v4 görünür dalga boyu pdf'i 360-830 nm, CIE 1931 Wyman-Sloan-Shirley
  çok loblu fit, yansıtırlık Jakob-Hanika sigmoid (konakta Gauss-Newton ile uydurulur).
- **Güneş:** 5778 K kara cisim, kenar kararması u=0,6, atmosfer geçirgenliği gök ön hesabıyla aynı modelden (aynı kaynak).
- **Gök:** küresel atmosfer, tek saçılım (Rayleigh + Mie AOD 0,15 + ozon Chappuis), 512×256×48 spektral harita,
  ufukta sıkışık yükseklik eşlemesi. Çoklu saçılım YOK (bilinen eksik).
- **Gök NEE:** açıklıklar (portal) üzerinden; MIS ile BSDF örneklemesi.
- **Duvar 25 cm, yarık 20 cm, güneş yanal açısı 20°:** 40 cm duvar + 12 cm yarık + 28° güneşi tamamen kesiyordu
  (yanal kayma 0,40·tan28° = 21 cm > 12 cm). Ölçülerek bulundu, sahne geometri hesabı DURUM'a not.

## F1'de öğrenilenler (ölçülerek)
- **Fırın testi kapalı odada Rus ruleti ile yakınsamaz** (yollar ~100 sekme): fırın kipinde RR kapalı. İkinci hata:
  pus kapalıyken gölge ışınları pus geçirgenliğiyle çarpılıyordu (%4,7 kayıp). Düzeltildi: tek `hazeActive()`.
- **Güneş lekesi bağlantısı** (güneşli yüzeyi ikincil ışık gibi örnekleme, 3 yönlü MIS): yansız (fark %-0,14),
  yalnız güneşte gürültüyü 0,89 → 0,29 düşürüyor ama eşit sürede bütün sahnede düz yol izleme kazanıyor
  (0,319 vs 0,383). Kodda, varsayılan KAPALI (`path.sun_patch`).
- **Gök haritası önem örneklemesi** (hâle): eşit sürede kazanmıyor (0,352 vs 0,319). Kodda, varsayılan kapalı
  (`sky.map_fraction`).
- **Prosedürel doku süresin 2/3'üydü:** 3B fbm yerine pişirilmiş 2B gürültü dokusu (aynı `gnoise2` fonksiyonundan,
  açılışta) → 7,9 → 3,4 ms (960×540, 1 spp). SER kapalıyken 6 kat yavaş: yeniden sıralama şart.
- Yol izleyici artık ~150 M yol/s (~2-3 G ışın/s): donanım tavanına yakın. Sonraki kaldıraçlar: örnekleme deseni
  (düşük tutarsızlık / mavi gürültü) ve gürültü giderici.
- Hatanın %99,5'i köpüklerdeki alt-piksel güneş parıltılarında: gerçek, yavaş yakınsıyor, ton eşlemede kırpılıyor.

## F2-F5'te öğrenilenler (ölçülerek)
- **OptiX yerleşik küresi içeriden vurulmuyor** (deneyle: içeriden vuruş sayısı 0) → su dolu küre siyahtı, köpük tek filmdi.
  Küreler özel ilkel (`__intersection__sphere`, iki kök).
- **Su yön için dağılımsız** (Δn ~0,01): her havuz ışınında ikincil dalga boylarını öldürmek renk karıncası yapıyordu.
  Fotonlar suda tam dağıtıcı (her foton kendi dalga boyunda, gürültü bedeli yok).
- **Gökkuşağının yeri hesaplandı** (`tools/prism_plan.py`): çoğu açıda prizmada tam iç yansıma; tüm renklerin çıktığı
  pencere ~78-84°. 82° seçildi.
- **Foton ışınları pahalı** (kutular üst üste biniyor): 16 parçaya bölme + kare başına ışınların ~1/8'i (akı ölçekli, yansız).
- **Toz 21 ms yiyordu**: ayrı GAS + görünürlük maskesi 2 (yalnız kamera ışınları) → ~0 ms.
- **Gürültü giderici:** kostik ayrı kalmalı (denendi: gökkuşağını bulanıklaştırdı). Canlıda AOV modeli düz sıvada "fırça izi"
  uyduruyordu; TEMPORAL_AOV + kamera uzayı normal düzeltti. Ateşböceği → blok; canlıda örnek kırpması.
- **Renk:** zeytin-kahve ton, serin gök dolgusunun yokluğundandı; güneşe sırtı dönük sağ duvar açıklığı dengeyi kurdu.
  Gök modelinde çoklu saçılım yok (gök gerçekte daha parlak/mavi): bilinen eksik.

- **Tabakalı tayf bölme:** prizmayı gören pikseller aynı rastgele sayılarla 4 kez izlenir, camda her seferinde başka
  dalga boyu sürer (ortalama yansız). Durağanda +%24 süre, prizmanın renk gürültüsü belirgin düşük. "16 örnek ver"
  denemesi aynı iş parçacığında sıralı çalıştığı için başlatmayı bekletti (11 → 48 ms): sayı değil dağılım önemli.
- **Fırın testi bir tutarsızlık daha yakaladı** (+%0,15): fırında köpükler ışın için opak, gölge ışını için geçirgendi.
  Yalnız fırına özgü; düzeldi (%0,02-0,03).

## Referansa uzaklık (LDR-FLIP, kahraman kare, 16.384 örnek referans)
| Kip | Süre | FLIP | Not |
|---|---|---|---|
| durağan 16 örnek + gg | 0,7 s | 0,077 | |
| durağan 256 örnek + gg | 11,6 s | 0,061 | |
| durağan 1024 örnek + gg | 45 s | 0,041 | ham 1024: 0,055 |
| canlı 80. kare (donuk) | 25,9 fps | 0,115 | en çok prizmada ayrışıyor (canlıda karıncalı tayf) |

## Ölçümler
| Tarih | Ne | Değer |
|---|---|---|
| 10-01 | 720p, 1 spp/başlatma, ilk sahne | 13,5 ms/başlatma, ~68 M yol/s |
| 10-01 | 1440p pencere | ~41 ms/başlatma |
| 10-01 | ilk başlatma (OptiX tembel derleme) | ~4,5 s tek sefer; ölçümden hariç tutuluyor |
| 10-01 | güneş merkez parlaklığı 450/550/650 nm | 2898 / 6795 / 9378 W/(m²·sr·nm): alçak güneşin sıcak tayfı |
| 10-01 | `--selftest` | D65 xy (0,31271; 0,32907), sRGB beyazı ±0,001, Sellmeier F/d/C ±6e-5, ince film, sigmoid: hepsi OK |
| 10-01 | fırın testi (640×360, 256 spp) | ortalama 1,00028, medyan 0,9999, renk E beyazı (1,20/0,95/0,91) |
| 10-01 | 960×540 1 spp (dokular pişmiş) | 3,4 ms, ~150 M yol/s; 1440p 2048 spp 49 s |
| 10-01 | yakınsama (ort. göreli piksel hatası, 16k referansa) | 16: 0,62 · 64: 0,35 · 256: 0,19 · 1024: 0,11 |
| 10-01 | güneş rengi 10° | xy 0,4122 0,4063 · CCT ~3480 K |
| 10-01 | durağan 1440p (fotonlar+ışınlar+toz) | ~43-47 ms/örnek, ~20 örnek/s; 900 örnek 45 sn |
| 10-01 | canlı 1440p (720p + büyütme) | 28-29 ms/kare: yol 10 + foton 1,3 + gg 14-15 + ton 0,3 ms |
| 10-01 | güç (pencere, durağan) | 262 W / 370 W, 2880 MHz, 54 °C |
| 10-01 | güneş lekesi / gök haritası örneklemesi | yansız ama eşit sürede kazanmıyor: varsayılan kapalı |
