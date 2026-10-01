# DURUM — stress_test_pc

> Tek kaynak durum panosu. Her faz sonunda güncellenir; bağlam sıkıştırılınca BASLA.md ile birlikte okunur.

## Şu an
- **2026-10-01 · F0 iskelet bitti.** ⏭️ **F1 · Işık doğru** (BASLA.md §7).

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
| F1 Işık doğru | sürüyor | spektral çekirdek, güneş+gök, NEE+MIS, pus tek saçılım yazıldı; oda ışığı tasarımı |
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

## Ölçümler
| Tarih | Ne | Değer |
|---|---|---|
| 10-01 | 720p, 1 spp/başlatma, ilk sahne | 13,5 ms/başlatma, ~68 M yol/s |
| 10-01 | 1440p pencere | ~41 ms/başlatma |
| 10-01 | ilk başlatma (OptiX tembel derleme) | ~4,5 s tek sefer; ölçümden hariç tutuluyor |
| 10-01 | güneş merkez parlaklığı 450/550/650 nm | 2898 / 6795 / 9378 W/(m²·sr·nm): alçak güneşin sıcak tayfı |
| 10-01 | `--selftest` | D65 xy (0,31271; 0,32907), sRGB beyazı ±0,001, Sellmeier F/d/C ±6e-5, ince film, sigmoid: hepsi OK |
