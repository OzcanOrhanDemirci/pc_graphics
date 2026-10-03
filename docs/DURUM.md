# DURUM · pc_graphics uygulaması

> Uygulamanın tek kaynak durum panosu. Mimari ve yeni sahne ekleme: `docs/MIMARI.md`. Sahnelerin kendi panoları:
> `docs/altinsaat/`, `docs/fener/`, `docs/yikim/`, `docs/laboratuvar/`, `docs/koy/` (PLAN.md ve DURUM.md); `docs/tanecik/` arşiv.

## Şu an
- **2026-10-03 (akşam) · Sahne 5 "Koy" planlandı** (su altı: Akdeniz kayalık koyu, 3-15 m, birinci şahıs tüplü dalgıç).
  Özcan'la uzun konuşmayla: **netlik ve kararlılık her şeyin önünde** (kaba kuvvet süper örnekleme, zamansal tahmin yok,
  Fener taban), ışık + temas gerçek fizik, değişmeyen ışık fırınlanır, 41 fps sabit, su üstü yok. Tek kaynak
  `docs/koy/PLAN.md`; uygulama K0'dan başlar, ilk durak (netlik) Özcan'ın canlı bakışı.
- **2026-10-03 (gece, Özcan uyurken otonom) · Sahne 4 artık "Laboratuvar"** (ağır çekim fotoğraf masası). İnsan / manken
  yolu kapandı (Özcan: fikir olarak kalsın), kodu `tanecik-insan-arsiv` etiketinde, belgeleri `docs/tanecik/` (arşiv).
  Beş deney: su dökme, bal, kum, su balonu, karpuz; hızlı anlar arka planda ince hesaplanan **kayıt** (video değil,
  fizik saklanır), bal ve kum canlı. T1 çözücüsü, sıvı yüzeyi ve silah modülü sahneye taşındı (`src/scenes/laboratuvar/`).
  Galeride 04 (deniz yeşili). Plan, ölçümler, kararlar ve **NE DOĞRULANMADI**: `docs/laboratuvar/DURUM.md`.
  Ajan dalları: `tanecik-suzgec` (gürültü süzgeci) eski çiziciye bağlıydı, birleştirilmedi; Laboratuvar OptiX'in
  yapay zekâ gürültü gidericisini (AOV) kullanıyor. `tanecik-denge` insan için, birleştirilmedi (dal duruyor).
- **2026-10-02 (gece) · Sahne 4 "Tanecik" başladı** (tanecik fiziği ve insan fiziği). Plan Özcan'la konuşuldu ve
  onaylandı: `docs/tanecik/PLAN.md`. T0 (görünüm) bitti, galeride 04 (vurgu rengi deniz yeşili `#6FC2B4`). Galeri
  başlığındaki sahne sayısı artık kayıttan ("dört sahne"), liste satırları sahne sayısına göre kısalır (beşinci sahnede
  taşmasın). Otomatik test tek sahneye daraltılabilir: `--autotest-only <id>`. Ayrıntı `docs/tanecik/DURUM.md`.
- **🏁 2026-10-02 (gece) · Yıkım tamamlandı; sıradaki yeni bir sahne tasarımı (yeni oturumda).** Açık kalanlar
  `docs/yikim/DURUM.md`'nin başında.
- **2026-10-02 (gece, kapanış) · Yıkım'da kamera W ile baktığı yöne uçar** (yukarı bakınca yükselir). `docs/yikim/DURUM.md`.
- **2026-10-02 (gece, en en son) · Yıkım'da dördüncü yapı: sismik izolasyonlu kule** (Özcan: *"Sallandıkça sallanıyor
  ama yıkılmıyor"*). Üç kademeli cam kule ve iğne, kauçuk izolatörler üstünde; çizicide yansımalı cam. 7'de neredeyse
  hasarsız, 8'de birkaç cam, 9'da boşluklarla ayakta. `docs/yikim/DURUM.md`.
- **2026-10-02 (gece, en son) · Yıkım topunun sağ ucu bir göktaşı** (Özcan: *"Son hali cidden abartı olsun"*): 5 m,
  520 t, 110 m/s; tek atışta gökdeleni de apartmanı da yıkar. Ölçümler `docs/yikim/DURUM.md`.
- **2026-10-02 (gece, son) · Gökdelenin ortası donatısız beton.** Özcan: *"Merkez çok bağımsız kalıyordu"* (toplarla
  çevresi yıkılınca ortadaki çekirdek tek başına ayakta kalıyordu). Depremde davranış ve hız aynı; merkez zayıfladı ama
  orta açıklığın donatılı kolonları onu yine taşıyabiliyor (kısmi, Özcan: *"Biraz bile iyileşse yeterli"*).
  `docs/yikim/DURUM.md`.
- **2026-10-02 (gece) · Gökdelenin göçüşü hızlandı.** Özcan: *"gökdelende [...] ağır çekimde gibi yıkılıyor. Bunu
  optimize edip düzeltir misin."* Temas çözümü iki kat hızlı; göçüşte fizik adımı 40-50 → 20-23 ms, pencerede dünya
  gerçeğin ~%35-40'ından %66-73'üne. Sürtünme çapası tablosundaki bir yarış (gökdelen her koşuda farklı yıkılıyordu)
  düzeltildi; 24. sn sonrası için bir kaynak açık. `--autotest` gökdeleni 7'de göçertip dünya saatini yazar. Ayrıntı ve
  ölçümler `docs/yikim/DURUM.md`.
- **2026-10-02 (akşam) · İngilizce, üç yapı.** Özcan: *"Şimdi projeme ingilizce dil desteği ekle. Tüm proje ingilizce de
  olabilsin. Ayrıca yıkım moduna 3 çeşit bina ekleyelim. [...] Diğeri gökdelen olsun. Çok upuzun bir bina. Gerçekçi
  upuzun. [...] Bir de yer evi olsun. [...] Üçgen çatılı falan."*
  - **Dil:** uygulamadaki her yazı iki dilde (`L("Türkçe", "English")`, `src/app/lang.h`); galeride sağ üstte TR | EN
    ya da **L**; seçim `user.cfg`'de (git dışı), `--lang tr|en`. Sahnelerin adları İngilizcede Golden Hour, Lighthouse,
    Collapse. Büyük harf ve sayılar dile göre (İngilizcede "I" ve ondalık nokta). İngilizce gösteri rehberi `DEMO.md`.
    Belgeler (`docs/`) Türkçe çalışma günlüğü olarak kaldı.
  - **Yıkım'da üç yapı:** apartman, 30 katlı gökdelen, beşik çatılı ev; panelin en üstünden seçilir. Ölçümler ve
    çözücüde bulunan hata (temas payı) `docs/yikim/DURUM.md`.
  - Sahne panelinde seçenek düğmeleri artık yazılarına göre genişliyor (rakam: kare; sözcük: sözcük kadar).
  - `--autotest` iki dilde koşuldu (`--lang tr`, `--lang en`); test, seçilen dili kaydetmiyor.
- **2026-10-02 (gündüz) · Revizeler.** Özcan: *"Gayet güzel olmuş. Küçük revizeler yapalım. Bu sahnelerin açıklama
  metinlerini şu tarzı bu tarzı demek yerine kısaca özetle. Bir de ayrıntılar diye tıklayınca açılan bir alan yap. Orada
  teknik detaylardan bahset. [...] Yıkımda depremlerin süresi varsayılan şuanki gibi olsun ama biz değiştirebilelim.
  Sonsuza kadar da sürdürebilelim. Durduruna kadar yani. Deprem büyüklüklerine mouse ile tıklanabilsin. Süresi kaydırma
  çubuğu şeklinde ayarlanabilsin. Bir de mouse ile binaya tıklayınca kolon pıt diye yok oluyor ya. Gerçek bir darbe gibi
  hissettirsin. [...] Biri vurmuş gibi kırılıp devrilsin."*
  - **Galeri:** açıklamalar bir-iki cümlelik özet (stil adları kalktı); **Ayrıntılar** (düğme ya da A) resmin üstünde
    "nasıl yapıldı" panelini açar: sahne başına altı başlıkta teknik özet, iki sütunda, yükseklikçe dengeli.
  - **Sahne paneli artık tıklanır:** sözleşmede `Control` (seçenek sırası, kaydırma çubuğu, düğme). Uygulama çizer, fareyi
    alır (üstünde el imleci; panele tıklama sahneye geçmez), olanı bir sonraki karede sahneye `events` olarak verir; durum
    sahnede kalır. Ayrıntı `docs/MIMARI.md`.
  - **Yıkım:** büyüklükler tıklanır; **süre çubuğu** (çentik: büyüklüğün kendi süresi, varsayılan; uç: ∞ durdurana kadar;
    ← → ile de); **Depremi başlat / durdur** düğmesi; durdurunca zemin 1,5 sn'de diner (eskiden bir anda donuyordu: sarsıntı).
    Tıklama artık **yıkım topu** atar (patlatma kalktı). Ayrıntı ve ölçümler `docs/yikim/DURUM.md`.
- **2026-10-02 (öğleden sonra) · Topun gücü, bekleme.** Özcan: *"Bir top atınca 1 saniyeliğine tekrar top atılamasın [...]
  topun ağırlığını yani gücünü ayarlayabilelim. Kaydırma çubuğuyla [...] Mermi gibi yani."* Panelde en üstte **Top**
  çubuğu (boy ve hız birlikte, çentik dengeli atış), saniyede en çok bir top. Sözleşmeye `Control::section` (bir
  denetim grubunun üstüne ince çizgi). Ölçümler `docs/yikim/DURUM.md`.
- **2026-10-02 · Üç sahne tek uygulamada (gece, Özcan uyurken otonom).** Özcan'ın istekleri, birebir:
  1. *"Yıkım simülasyonunda fare ve klavye hareketleri ters, onları düz yap yani sağa-sağa, sol-sola gitsin."*
  2. *"Yıkım için Türkiyedeki depremleri kaldır, onun yerine onlardan ilham alarak 3 şiddetinde 4 5 6 7 8 9 şeklinde
     rihter ölçeğine göre depremleri listele."*
  3. *"3 Tane sahnem oldu. Bu sahnelerin hepsini tek bir uygulamada birleştir. Bu uygulamaya ortak bir arayüz geliştir.
     [...] Sahneleri değiştirme, arayüzü değiştir. [...] Sıkıcı düz yazılar yerine estetik bir arayüz tasarla. [...]
     İleride başka başka sahneler, grafikler vs eklenebilir. Buna uygun şekilde ölçeklenebilir olsun."*
  4. *"Bu 3 güncelleme sonucu ortaya çıkacak yapılması zorunlu veya kritik olan başka bir şeyler olduğunu düşünüyorsan
     yapmakta [...] özgürsün. [...] Dosya düzenini ve githubı profesyonel ve düzgünce yönet."*
- Yapılanlar:
  - **Kamera** (Yıkım): görüntünün sağı `ileri × yukarı`; uçuş kodu tersini kullanıyordu, fare sapması da ters
    artıyordu. Tek yerde (`fly_camera.h`), `--scene yikim --test kontrol` yönleri görüntüye karşı sınar.
  - **Depremler büyüklükle (3-9)**; gerçek kayıtların adları arayüzden kalktı, dalgaları kaldı. Ayrıntı ve ölçümler
    `docs/yikim/DURUM.md`.
  - **Tek uygulama:** `build\pc_graphics.exe` galeriyle açılır; `fener.exe` ve `yikim.exe` kalktı. Galeri, yükleme ekranı,
    her sahnenin üstünde aynı arayüz (kimlik ve durum, seçici, ilerleme, hız paneli, tuş ipuçları), F1 yardım, bildirimler.
    IBM Plex yazı ailesi, sahne başına bir vurgu rengi. Tasarım dili `docs/MIMARI.md`.
  - **Dosya düzeni:** her sahne kendi klasöründe (`src/scenes/<id>/`, ekran kartı programları `device/` altında,
    ayarlar `scenes/<id>.cfg`, belgeler `docs/<id>/`); ortak parçalar `src/core`, arayüz kiti `src/ui`, uygulama `src/app`.
  - **Kritik bulunan:** sahnelerin kapanış yolları eksikti (tek başına çalışırken önemsizdi; aç-kapa ile Yıkım tur başına
    yüzlerce MB sızdırırdı). Sahneler kaynaklarını bırakıyor, uygulama ardından ekran kartını sıfırlıyor.
- Açmak: `build\pc_graphics.exe` (çift tık). Tuşlar `GOSTERI.md`, ekranda F1.
- İş `unified-app` dalında yapılıp doğrulandı, `main`'e birleştirildi.

## Nasıl çalıştırılır
- Derleme: `cmd /c C:\dev\pc_graphics\build.bat` → `build\pc_graphics.exe` (tek program).
- Uygulama: `build\pc_graphics.exe` · `--scene <id>` doğrudan sahneye (Esc galeriye) · `--windowed` 1280×720 pencere ·
  `--set anahtar=değer` sahne ayarı · `--autotest` bütün uygulamayı dolaşır, ekranlar `captures/arayuz/`.
- Sahnelerin araçları (pencere olmadan): `--scene altinsaat --headless ...` (eskisi gibi `--scene`'siz de),
  `--scene fener --headless --frames N ...`, `--scene yikim --test`, `--scene yikim --clip sarsinti --set deprem.magnitude=7`.
  Her birinin listesi kendi `*_app.cpp` başında.

## Ölçümler (RTX 4080, 2560×1440, `--autotest`)
| Ne | Değer |
| --- | --- |
| Sahne 1, durağan | ~10 fps (kare başına ~30 ms örnek + ~94 ms yol izleme başlatması: önceden de böyleydi) |
| Sahne 1, canlı | 22 fps |
| Sahne 2 | 80-95 fps (yolculukta da) |
| Sahne 3 | 48-54 fps (deprem sırasında da) |
| Sahne kapandıktan sonra boş ekran kartı belleği | 14 869 / 14 869 / 14 860 MB (üç sahne, iki tam tur: sızıntı yok) |
| Taşımadan ve birleştirmeden sonra | üç sahnenin komut satırı kareleri önceki koda göre **bit bit aynı** (640×360, sabit tohum: fark 0) |
| Testler | Yıkım fizik 24/26 (bilinen iki: konsol), kontrol 4/4, deprem döngüsü 4/4 (10-02 gündüz), sahne 1 spektral öz-sınama OK |
| Revizeden sonra (10-02 gündüz) | Sahne 3 55 fps, sonsuz depremde 49, büyüklük 7'de 50; sahne 1 ve 2 değişmedi; kapanışta boş bellek yine sabit |

## Öğrenilenler (ölçülerek)
- **Taşıma sırasında davranışı kilitle:** üç sahneden sabit ayarla kareler, önce kendileriyle tekrar edilebilirliği
  sınandı (fark 0), sonra her adımdan sonra karşılaştırıldı. Büyük yeniden düzenleme bu sayede güvenle yapıldı.
- **Tek başına çalışan programın kapanışı ile bir uygulamanın içindeki sahnenin kapanışı ayrı şeylerdir:** üç sahnenin
  `shutdown`'ı yalnız OptiX nesnelerini bırakıyordu. Uygulamada sahne her kapandığında `cudaDeviceReset` (garanti) +
  sahnelerin kendi bırakmaları (düzen); ölçü kapanıştaki boş bellek.
- **Uzun yükleme ana iş parçacığında pencereyi "yanıt vermiyor"a düşürür:** sahne 1'in derlemesi önbellek boşken 42 sn
  (dosya yolu değişince OptiX önbelleği ıskaladı). Yükleme işçi iş parçacığında; sahneler yüklemede OpenGL kullanmıyor.
- **Bir yazı tipinde olan harf atlasta olmayabilir:** "∞" ekranda "?" çıktı; IBM Plex'te var, atlasa pişirilen aralıklarda
  yoktu (eksi işaretiyle birlikte eklendi). Yeni bir simge kullanmadan önce atlas aralıklarına bak (`src/ui/ui.cpp`).
- **Arayüz ekranlarını görmeden tasarlamak kör uçuştur:** `--autotest` bileşik ekranı (`glReadPixels`) PNG'ye yazıyor;
  ilk turda dört kusur buradan yakalandı (arka plan karartmasında dikey dikiş, önceki sahnenin bildirimi yükleme
  ekranında, Türkçe büyük harfte "â" ve İngilizce adda "İ").

## Bilinen sınırlar / doğrulanmadı
- Fare otomatik testte sanal: betik imleci koyar ve tıklar (galeri seçimi, iki düğme, Yıkım'da bakış, top, büyüklük,
  süre çubuğu, başlat/durdur). Gerçek farenin sürüklemesi (çubuğu tutup kaydırmak) ve el imleci gözle denenmedi;
  Fener'in oyuncu kipinde fareyle bakış hiç sınanmadı.
- 1440'tan farklı ekranlarda yerleşim yalnız ölçekle uyarlanıyor (1080p ve 4K gözle denenmedi).
