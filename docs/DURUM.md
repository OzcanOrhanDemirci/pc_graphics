# DURUM — stress_test_pc

> Tek kaynak durum panosu. Her faz sonunda güncellenir; bağlam sıkıştırılınca BASLA.md ile birlikte okunur.

## Şu an
- **2026-10-01 · F(-1) hazırlık bitti.** Araç zinciri kuruldu ve `tools/smoke` ile doğrulandı (`SMOKE OK`): CUDA 13.4.2
  redist + VS 2026, OptiX 9.1 (RT core v30), OptiX-IR modül/boru hattı/raygen, AI gürültü giderici modelleri
  (HDR, AOV, TEMPORAL, TEMPORAL_AOV, UPSCALE2X), NVML. NVIDIA FLIP (Python) kurulu.
- ⏭️ **F0 · İskelet** (BASLA.md §7).

## Fazlar
| Faz | Durum | Not |
|---|---|---|
| F0 İskelet | bekliyor | |
| F1 Işık doğru | bekliyor | |
| F2 Malzeme ve detay | bekliyor | |
| F3 Kostik ve hacim | bekliyor | |
| F4 Canlı mod | bekliyor | |
| F5 Sanat ve zirve | bekliyor | |

## Ölçümler
*(her faz sonunda: geçiş başına ms, spp/s, W, FLIP/RMSE, titreşim)*
