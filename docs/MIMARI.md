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
| `info()` | Galerinin ve arayüzün bildiği her şey: kimlik (`--scene <id>`), numara, ad, alt başlık, bir-iki cümlelik açıklama, galerideki "Ayrıntılar" (`details`: başlık + paragraf, nasıl yapıldığı), etiketler, galeri resmi, vurgu rengi, tuş grupları (F1 yardımı bunlardan çizilir), F5 desteği. |
| `load(host, state)` | Adım adım yükleme. Her çağrı bir adım yapar, `state.label` bir sonraki adımı anlatır, `state.fraction` ilerlemedir. İşçi iş parçacığında çalışır: **OpenGL yok**, CUDA/OptiX serbest. Hata: `state.error`. |
| `frame(host, input)` | Bir kare: girdi (bu karede basılan tuşlar, basılı tuşlar, fare, tekerlek, sahnenin ekrandaki yeri, panel denetimlerine yapılanlar `events`) → sahne ilerler ve kendi resmini çizer. |
| `image(w, h)` | Karenin resmi: RGBA8, üst satır önce, ana bellekte (sahnenin kilitli belleği). Uygulama dokuya yükleyip ekrana sığdırır. |
| `status(s)` | Arayüz şeridinin içeriği: durum etiketleri (`chips`), sağ üstteki ölçümler (`metrics`), panelin denetimleri (`controls`), bir ilerleme çubuğu (ya da sonu olmayan, `progressEndless`). |
| `screenshot(host)` | F12: sahnenin kendi resmi `captures/<id>_<tarih>` (sahne 1 EXR de yazar). |
| `reload(host)` | F5 (isteğe bağlı): ayarlar ve programlar yeniden. |
| `unload()` | Ekran kartı kaynaklarını bırakır. Uygulama ardından `cudaDeviceReset` yapar: bir sahnenin unuttuğu bellek bir sonrakine geçmez. |

`Host` (uygulamanın sahneye sunduğu): bildirim (`toast`), fareyi yakalama (birinci şahıs bakış), proje kökü, ekran görüntüsü
yolu, `--set` ezmeleri.

**Uygulamanın tuşları** sahneye ulaşmaz: Esc (galeri; yardım açıkken yardımı kapatır), F1 (tuşlar), H (arayüzü gizle),
F12 (ekran görüntüsü), F5 (yeniden yükle). Sahneler bunları kullanmamalı. Galeride A ayrıntıları açar.

**Panel denetimleri (`app::Control`).** Bir sahne panelinde tıklanacak bir şey isterse onu her karede `status`'ta tarif
eder; çizmez, fareye bakmaz:

| Tür | Gösterir | Sahneye dönen olay (`ControlEvent`, `id` ile) |
| --- | --- | --- |
| `Choice` | yan yana seçenekler (`items`, seçili `active`) | `item`: tıklanan |
| `Slider` | çubuk (`value` 0..1), çentik (`mark`: varsayılan), sağında değer (`text`) | `value`: sürüklenen yer, düğme basılıyken her kare; bırakınca `released` |
| `Button` | düğme (`text`), aynı işi yapan tuş (`keys`), vurgulu (`strong`), kapalı (`enabled = false`) | tıklandı |

Uygulama başlıkları tek sütunda hizalar, satırları aynı genişliğe getirir, altına `detail` satırını yazar; üstüne
gelinince el imleci ve vurgu. Panellere (sağ üst ve sağ alt dahil) basılan fare sahneye geçmez: Yıkım'da düğmeye
tıklamak top atmaz. Durumun sahibi sahnedir: çubuğu bırakılan yere oturtmak (yuvarlamak, çentiğe yapıştırmak) onun işi,
panel bir sonraki karede sahnenin söylediğini çizer. Tıklama yerleri bir önceki karede çizilenden gelir.

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
   `captures/arayuz/` altına yazar, kapanışta boş ekran kartı belleğini yazdırır (her turda aynı kalmalı). Betik
   panel denetimlerine kimlikleriyle tıklayabilir (`app.cpp`, adım 11-12), konumu bilmeden.

## Tasarım dili

- **Koyu, sessiz bir çerçeve, parlak resimler.** Arayüz nötr gri tonlarda; her sahne tek bir vurgu rengi getirir:
  01 altın (`#E6B566`), 02 soğuk mavi (`#86B3DA`), 03 güvenlik turuncusu (`#E5764A`). Galeri düğmesi, seçili öğe,
  seçici ve ilerleme çubukları o rengi alır.
- **Tek yazı ailesi, üç ses:** IBM Plex Serif adlar ve başlıklar, Plex Sans metin, Plex Mono sayılar ve tuş kapakları.
  Küçük başlıklar büyük harf ve harf aralıklı (Türkçe büyük harf: i → İ).
- **Cam paneller:** yarı saydam koyu dolgu, ince açık kenar, yumuşak gölge, 16-20 piksellik köşeler.
- **Yerleşim 2560×1440 için çizilir, pencereye ölçeklenir** (en küçük ölçek 0,7). Sol üst köşe boş bırakılır: orada
  RivaTuner'ın kare hızı göstergesi durur.
- **Hareket:** ekranlar 0,5-0,7 sn'de belirir; galeri seçimi kayarak geçer, resimler çapraz geçişle değişir; ayrıntılar
  paneli resmin üstüne 0,3 sn'de iner, resmin kendi yazıları o sırada çekilir; bildirimler 3 sn sonra söner.
- **Galeride iki ses:** resmin altında ne olduğu (kısa açıklama), "Ayrıntılar"da nasıl yapıldığı (teknik). Stil ya da
  esin adı açıklamada değil, gerekirse ayrıntılarda.
- **Yazı atlası:** IBM Plex'ten ASCII, Latin-1, Latin Genişletilmiş-A (Türkçe), tireler ve tırnaklar, oklar, eksi ve ∞
  pişirilir (`src/ui/ui.cpp`, `kRanges`). Başka bir simge önce oraya eklenir; yoksa ekranda "?" çıkar.
