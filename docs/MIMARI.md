# MİMARİ · pc_graphics

> Uygulamanın nasıl kurulduğu ve yeni bir sahnenin nasıl ekleneceği. Durum ve ölçümler `docs/DURUM.md`'de; her sahnenin
> kendi panosu `docs/<sahne>/`.

## Bir bakışta

Tek program, `build\pc_graphics.exe`. Açılınca **galeri**: sahneler numaralı bir listede, seçilenin resmi büyük bir kartta.
Enter ile sahne açılır: **yükleme ekranı** sahnenin adımlarını gösterirken sahne bir işçi iş parçacığında yüklenir
(derleme, dünya kurma; pencere bu sırada da canlı kalır). Sahne çalışırken üstünde **ortak arayüz** durur; Esc galeriye
döndürür, sahne bütün ekran kartı kaynaklarını bırakır.

```
src/
  app/        uygulama: pencere, galeri, yükleme, sahne üstü arayüz, yardım, bildirimler, sahne kaydı
    scene.h       sahne sözleşmesi (app::Scene) — her sahne bunu uygular
    registry.cpp  sahnelerin listesi (galeri sırası)
    app.cpp       pencere, döngü, kipler (galeri / yükleme / sahne / hata), girdi, --autotest
    overlay.cpp   ekranların çizimi
    theme.h       renkler, yazı biçimleri, ölçek
    main.cpp      giriş: uygulama ya da bir sahnenin komut satırı araçları
  ui/         arayüz kiti: yuvarlak köşeli paneller, gölgeler, resimler, IBM Plex metin (OpenGL 2 + bir GLSL programı)
  core/       sahnelerin ortak parçaları: ayar dosyası (Config), resim okuma/yazma, GPU ölçümleri (NVML), ışıltı piramidi
  shared/     konak ve ekran kartı kodunun ortak başlıkları: vektörler, ton eşleme, tayf
  scenes/
    altinsaat/  sahne 1 · Altın Saat (spektral yol izleyici)
    fener/      sahne 2 · Fener (gerçek zamanlı hacimsel ışık)
    yikim/      sahne 3 · Yıkım (ekran kartında fizik çözücü)
      <sahne>_app.h     bir çalıştırmanın durumu (renderer, ayarlar, dünya)
      <sahne>_app.cpp   komut satırı araçları (pencere olmadan kareler, testler)
      <sahne>_live.cpp  uygulamadaki canlı sahne: app::Scene
      device/           OptiX programları; çalışma anında NVRTC ile derlenir (F5 yeniden derler)
scenes/<sahne>.cfg   sahnelerin ayarları (anahtar = değer; --set anahtar=değer ile ezilir)
assets/
  fonts/      IBM Plex Sans, Serif, Mono (SIL Open Font License, LICENSE.txt)
  gallery/    galeri resimleri, <sahne>.jpg
  deprem/     Yıkım'ın deprem kayıtları (ESM, AFAD TADAS)
docs/         Türkçe çalışma günlüğü: bu dosya, DURUM.md, sahne klasörleri (PLAN, DURUM), images/ (README görselleri)
tools/        Python araçları: EXR istatistikleri, FLIP karşılaştırması, temas tabakaları, değer etütleri
```

## Sahne sözleşmesi (`src/app/scene.h`)

| | |
| --- | --- |
| `info()` | Galerinin ve arayüzün bildiği her şey: kimlik (`--scene <id>`), numara, ad, alt başlık, açıklama, etiketler, galeri resmi, vurgu rengi, tuş grupları (F1 yardımı bunlardan çizilir), F5 desteği. |
| `load(host, state)` | Adım adım yükleme. Her çağrı bir adım yapar, `state.label` bir sonraki adımı anlatır, `state.fraction` ilerlemedir. İşçi iş parçacığında çalışır: **OpenGL yok**, CUDA/OptiX serbest. Hata: `state.error`. |
| `frame(host, input)` | Bir kare: girdi (bu karede basılan tuşlar, basılı tuşlar, fare, tekerlek, sahnenin ekrandaki yeri) → sahne ilerler ve kendi resmini çizer. |
| `image(w, h)` | Karenin resmi: RGBA8, üst satır önce, ana bellekte (sahnenin kilitli belleği). Uygulama dokuya yükleyip ekrana sığdırır. |
| `status(s)` | Arayüz şeridinin içeriği: durum etiketleri (`chips`), sağ üstteki ölçümler (`metrics`), bir seçici (Yıkım'ın büyüklükleri), bir ilerleme çubuğu. |
| `screenshot(host)` | F12: sahnenin kendi resmi `captures/<id>_<tarih>` (sahne 1 EXR de yazar). |
| `reload(host)` | F5 (isteğe bağlı): ayarlar ve programlar yeniden. |
| `unload()` | Ekran kartı kaynaklarını bırakır. Uygulama ardından `cudaDeviceReset` yapar: bir sahnenin unuttuğu bellek bir sonrakine geçmez. |

`Host` (uygulamanın sahneye sunduğu): bildirim (`toast`), fareyi yakalama (birinci şahıs bakış), proje kökü, ekran görüntüsü
yolu, `--set` ezmeleri.

**Uygulamanın tuşları** sahneye ulaşmaz: Esc (galeri; yardım açıkken yardımı kapatır), F1 (tuşlar), H (arayüzü gizle),
F12 (ekran görüntüsü), F5 (yeniden yükle). Sahneler bunları kullanmamalı.

## Yeni bir sahne eklemek

1. `src/scenes/<id>/` klasörü: renderer'ı, `device/` altında OptiX programları, durum (`<id>_app.h`), canlı sahne
   (`<id>_live.cpp`), gerekiyorsa komut satırı araçları (`<id>_app.cpp`).
2. `<id>_live.cpp` içinde `app::Scene`'i uygula ve sahnenin isim alanında üç işlev ver:
   ```cpp
   namespace <id> {
   app::SceneInfo info();
   std::unique_ptr<app::Scene> make();
   int cli(int argc, char** argv);   // komut satırı aracı yoksa registry'de nullptr
   }
   ```
3. `src/app/registry.cpp`'ye bir satır (galeri sırası buradan), `CMakeLists.txt`'ye dosyalar.
4. `scenes/<id>.cfg` (ayarlar), `assets/gallery/<id>.jpg` (16:9, en az 1920 piksel genişlik).
5. Sına: `build\pc_graphics.exe --autotest` galeriyi ve her sahneyi kendiliğinden dolaşır, ekranları
   `captures/arayuz/` altına yazar, kapanışta boş ekran kartı belleğini yazdırır (her turda aynı kalmalı).

## Tasarım dili

- **Koyu, sessiz bir çerçeve, parlak resimler.** Arayüz nötr gri tonlarda; her sahne tek bir vurgu rengi getirir:
  01 altın (`#E6B566`), 02 soğuk mavi (`#86B3DA`), 03 güvenlik turuncusu (`#E5764A`). Galeri düğmesi, seçili öğe,
  seçici ve ilerleme çubukları o rengi alır.
- **Tek yazı ailesi, üç ses:** IBM Plex Serif adlar ve başlıklar, Plex Sans metin, Plex Mono sayılar ve tuş kapakları.
  Küçük başlıklar büyük harf ve harf aralıklı (Türkçe büyük harf: i → İ).
- **Cam paneller:** yarı saydam koyu dolgu, ince açık kenar, yumuşak gölge, 16-20 piksellik köşeler.
- **Yerleşim 2560×1440 için çizilir, pencereye ölçeklenir** (en küçük ölçek 0,7). Sol üst köşe boş bırakılır: orada
  RivaTuner'ın kare hızı göstergesi durur.
- **Hareket:** ekranlar 0,5-0,7 sn'de belirir; galeri seçimi kayarak geçer, resimler çapraz geçişle değişir; bildirimler
  3 sn sonra söner.
