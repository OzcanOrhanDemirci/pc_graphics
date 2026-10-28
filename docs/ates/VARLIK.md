# VARLIK · sahne 12 "Ateş", açık lisanslı varlıklar

> Tek kaynak: sahneye giren her hazır varlığın adresi, yazarı, lisansı ve ne için kullanıldığı. Plan `PLAN.md` §3.10
> (Poly Haven, CC0). Ateşin kendisi (gaz, odun, kor, kıvılcım, kömür deseni, çatlaklar) varlık değil, koddan çıkar.

## 0 · Lisans ve indirme

- **Lisans kuralı (Özcan, Orman'dan):** CC0 ve CC BY (künyeyle) evet; CC BY-SA yalnız başka yol yoksa; NC ve ND hayır.
  Buradaki beş varlığın hepsi **CC0** (Poly Haven): künye zorunlu değil, yine de sahnenin "Teşekkür" satırı bu dosyayı
  gösterir.
- **İndirme:** `PYTHONIOENCODING=utf-8 python -X utf8 assets/ates/fetch.py` (yeniden çalıştırılabilir; her dosya Poly
  Haven'ın md5 özetiyle denetlenir). Dosyalar git'e girmez (`.gitignore`: `assets/ates/cc0/`, `assets/ates/_dl/`);
  `fetch.py`, `manifest.json` ve bu dosya girer.

## 1 · Varlıklar (`assets/ates/manifest.json`, 2K)

| Kimlik | Tür | Yazar | Lisans | Sahnede | Boyut |
|---|---|---|---|---|---|
| [rock_07](https://polyhaven.com/a/rock_07) | model | Jenelle van Heerden | CC0 | ocağın taşları | 5 dosya, 6,0 MB |
| [namaqualand_stones_01](https://polyhaven.com/a/namaqualand_stones_01) | model | Greg Zaal, Jenelle van Heerden | CC0 | ocağın taşları (çeşit) | 5 dosya, 10,2 MB |
| [burned_ground_01](https://polyhaven.com/a/burned_ground_01) | doku | Rob Tuytel | CC0 | ocağın içi ve çevresi: yanık toprak | 6 dosya, 17,4 MB |
| [brown_mud_dry](https://polyhaven.com/a/brown_mud_dry) | doku | Rob Tuytel | CC0 | ocağın ötesi: kuru toprak | 6 dosya, 20,4 MB |
| [bark_brown_02](https://polyhaven.com/a/bark_brown_02) | doku | Rob Tuytel | CC0 | kütüklerin kabuğu (yanmamış yüzey) | 6 dosya, 15,9 MB |

Boyutlar manifestteki dosya boyutlarının toplamı. `bark_brown_02` yoksa kabuk koddan düz bir kahverengiye düşer
(`ates_world.cpp`, `loadAssets`).
