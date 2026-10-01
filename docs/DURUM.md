# DURUM — stress_test_pc

> Tek kaynak durum panosu. Her faz sonunda güncellenir; bağlam sıkıştırılınca BASLA.md ile birlikte okunur.

## Şu an
- **2026-10-01 · F1 ışık doğru bitti** (kapılar aşağıda). ⏭️ **F2 · Malzeme ve detay** (BASLA.md §7).
- F1 karesi Özcan'a gönderildi: `captures/F1_kose.png` (köşe kamerası, 2048 örnek).

## Nasıl çalıştırılır
- Derleme: `cmd /c C:\dev\stress_test_pc\build.bat` (CMake + Ninja, VS 2026, `third_party/cuda`). PowerShell'de
  `cd` cmd'ye geçmez: build.bat'ı **tam yoluyla** çağır.
- Pencere: `build\stress.exe` (kenarlıksız tam ekran, birincil monitör). Tuşlar main.cpp başında.
- Başsız: `build\stress.exe --headless --width 1280 --height 720 --spp 64 --out captures/ad` → PNG + EXR,
  güneş/gök tanı satırı, ms/başlatma, NaN sayacı.
- Testler: `--selftest` (spektral), `--furnace` (beyaz fırın), `--autotest` (pencere: H, F5, S, Esc zaman çizelgesi).
- Ekran yakalama (monitörde gerçekte görünen): `tools/screengrab.ps1 -Out <mutlak yol> -DelaySeconds 2.5`.
- Sahne ayarları `scene.cfg` (F5 yeniden okur). Cihaz kodu `src/device/*.cu|h`, F5'te NVRTC ile yeniden derlenir.

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| F0 İskelet | ✅ 10-01 | pencere 2560×1440 @165 Hz RTX 4080'de (GL_RENDERER doğrulandı), F5, S (PNG+EXR), HUD, NVML |
| F1 Işık doğru | ✅ 10-01 | fırın %0,03 · D65 beyazı ±0,001 · yakınsama 1/√n · güneş ~3480 K · 3 yarık + tavan yarığı + köşe kamerası |
| F2 Malzeme ve detay | bekliyor | |
| F3 Kostik ve hacim | bekliyor | |
| F4 Canlı mod | bekliyor | |
| F5 Sanat ve zirve | bekliyor | |

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
