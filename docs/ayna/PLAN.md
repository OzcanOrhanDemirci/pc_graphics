# PLAN · Sahne 9 "Ayna"

> Sürüm 1 · 2026-10-07 gece · Özcan'la konuşulup yazıldı. Bir sonraki oturumda Özcan "grafik devam" yazınca **otonom**
> çalışılır, sahne bitene kadar (Özcan: *"Yeni oturumda grafik devam yazarım ve sahneyi bitirene kadar çalışırsın. Ben
> de sabah uyanınca test ederim."*). Bu plan yön verir; daha iyi bir yol görülürse değişir, değişiklik DURUM.md'ye yazılır.
> **Yetki (Özcan, aynı gece):** *"her sahnede her zaman söylediğim gibi tamamen özgürsün ve tam yetkilisin. Eğer planda
> bir sorun görürsen planı değiştirebilirsin. İstersen bir şeyler ekle-çıkar yapabilirsin. Önemli olan en iyi sonuca
> ulaşmak. O sonuca ulaşmak için gerekenleri yap."* · *"Paralel agent kullanma, kendin çalış. Gereksiz kontrollere de
> gerek yok."* · Zaman: sabah 9'a kadar 8 saatten fazla. *"Haftalık kotayı dert etme [...] görevi bitirene kadar devam et."*

## 0 · Tek amaç

**Yansımanın yansımasının yansımasını tam kalitede göstermek.** Özcan: *"Yansımanın yansımasının yansımasını tam kalitede
görmek sahnemizin dar alandaki başarısı olacak."* · *"Oyunlarda yansımalar hep çok detaysız ve özensiz [...] rtx'li
oyunlarda bile yansıma genelde gerçek ama düşük kalite. Bizim tek odağımız yansıma olacağı için onu mükemmel
yapmalıyız. Çözünürlük ve netlik, stabilite olarak normal görüntü seviyesinde olmalı."*

**Ölçüt: yansıma = doğrudan görüntü kalitesi.** Labirentin büyüklüğü, ayrıntı zenginliği, oyun mekaniği ikincil.

## 1 · Kararlar (Özcan, 2026-10-06/07)

1. **Ayna labirenti**, Elhamra tarzı (Luzern'deki 1896 ayna labirenti gibi; 60° üçgen ızgara). **Kompakt**, aynaların
   konumları düzgün ayarlı (tam paralel; gerçekteki hafif kıvrılan tünel bilerek yok). *"Labirenti büyük yapmaya
   harcayacağın eforu yansıma kalitesine harcarsın."*
2. **Biz yansıtan bir küreyiz** (krom). **Kamera kürenin gözünden (FPS):** kendimizi yalnız aynalarda görürüz. İnsan yok.
3. **Renkli ve farklı şiddette ışıklar**, tam gerçek zamanlı; **ışık bırakabiliriz**.
4. **Havada pus var** (huzmeler görünsün).
5. **"Fizik ne derse o":** aynalara sanat ayarı yok. *"Aynaları kendi haline bıraksak [...] Sen labirenti kurarsın.
   Yansımalar zaten öyle olması gerektiği için öyle olur."*
6. **Ayar paneli YOK** (çözünürlük, yumuşatma, grafik kalitesi). **2K (2560×1440), hedef 30-40 FPS**, kaliteyi Claude
   ayarlar. Derin tünelde fazladan sabit örnek (Özcan: *"o kaliteyi sağlayabileceğine ben de inanıyorum"*).
7. **Galerideki "09 Isı" taslağı silinir, yerine bu sahne gelir** (geri alma değil, çalışmanın parçası).
8. Kırılma (§6, A7): Özcan sordu; plana **en sona, kapılı** kondu. Çekirdek ölçütler (§4) tutmadan başlanmaz.

## 2 · "Fizik ne derse o"nun anlamı

Işın izleme çekirdekleri yalnız çarpışmayı bulur; sekmeyi, yansıtma oranını, ışığın yolunu biz yazarız. Kural: **gerçek
değerlerle yaz, sanat için hile yapma.** Fizikten gelenler (ayar değil):
- **Ayna = arkası sırlı cam:** ön yüzde cam Fresnel'i (dik ~%4, yatıkta artar), içte gümüş (~%95, hafif sıcak), camın
  içinden iki kez geçiş (demir yüzünden yeşil soğurma). Sonuç: gerçek aynanın ~%85-90'ı ve **sonsuz tünelin kararıp
  yeşile dönmesi** kendiliğinden. Değerler bir kaynaktan alınır ve DURUM.md'ye kaynağıyla yazılır.
- **Hayalet görüntü:** ön cam yüzeyinin zayıf ikinci yansıması, cam kalınlığı kadar kaymış. Dal, katkısı 1/255'in altına
  inene dek izlenir (pratikte ilk 1-2 sekme).
- **Zemin:** cilalı koyu taş, dielektrik Fresnel (dik az, yatık çok), keskin.
- **Sekme sınırı:** yolun ağırlığı 1/255'in altına inince (≈ 50 sekme). Tünelin sonunda kara duvar olmaz.
- **Krom küre:** metal Fresnel (krom ya da gümüş, karar DURUM'a), dışbükey: yansıma kendiliğinden sıkışır.
Bizim seçimlerimiz (dünya kurmak, hile değil): labirent, ışıkların yeri/rengi/şiddeti, pusun yoğunluğu, pozlama.

## 3 · Mimari

- **OptiX + RT çekirdekleri, üçgenler** (aynalar tam düzlem, kesin normal). SDF ile ışın yürütme DEĞİL: her sekmede
  yürütme hem yavaş hem yatık açıda kesinliği bozar. Küre: OptiX'in yerleşik küresi ya da analitik kesişim.
  Altyapı örnekleri: `fener` (canlı OptiX, NVRTC), `yikim` (her karede değişen geometri + XPBD katı cisim).
- **Yol:** kameradan ışın → ayna/küre/zeminde kesin sekme (rastgelelik yok) → ilk dağıtıcı yüzeyde tam gölgelendirme.
  Aynalar ışıklandırılmaz, yalnız yansıtır: maliyet sekme başına bir ışın.
- **Işık aynadan duvara (en zor kısım):** yol izleme nokta ışıkta bu yolu bulamaz. **Görüntü kaynak / hüzme ağacı**
  (akustikte ve Heckbert-Hanrahan 1984'te): her ışığın her aynadaki görüntüsü, o aynanın çerçevesinden geçen bir hüzme
  içinde geçerli; ışık kıpırdayınca ağaç yeniden kurulur (önce 1. derece, ölçerek 2.'ye). Gölgelendirme noktası yalnız
  içinde bulunduğu hüzmelerin görüntü ışıklarına bakar + o yolun açık olduğunu sınar. Kesin ve grensiz.
  **Doğrulama:** çevrimdışı kaba kuvvet ışık izleme (çok foton, yakınsamış) referansına karşı (Altın Saat'in fotonları
  yol izlemeye karşı 0,991'di; hedef ≥ 0,99).
- **Pus:** her ışın parçası boyunca kapalı biçim (Isı taslağının `atan` parıltısı): doğrudan ışıklar + koni sınırı
  (ışın ∩ koni aralığı) + **ayna görüntü ışıkları kendi hüzmeleri içinde** → aynadan aynaya zikzak çizen huzmeler. Her
  sekmenin parçasında da (yansıyan dünyada pus eksik olmaz).
- **Netlik ve doku:** her ışında **ışın konisi** (Akenine-Möller vd. 2019) taşınır; düz aynada genişlik toplam yolla
  büyür, kürede eğrilikle açılır. Yordamsal desenler koni genişliğiyle süzülür → aynanın arkasında doku, doğrudan
  bakıldığı kadar keskin ve kıpırtısız.
- **Örnekleme:** her pikselde sabit desen (döndürülmüş ızgara, 4); koni bir ayna kenarı/desen ayrıntısından genişse
  ya da sekme derinse o piksele ek sabit örnek (tavan ölçülerek, ör. 16-32). **Gürültü giderici ve geçmiş kare yok.**
- **Kendi kendini kesme:** sekme noktası ötelemesi Wächter-Binder (Ray Tracing Gems 2019, "A Fast and Robust Method
  for Avoiding Self-Intersection"). 50 sekmede nokta/delik çıkmamalı.

## 4 · Bitti tanımı: ölçümler (her biri DURUM.md'de sayıyla)

| # | Ölçüm | Hedef |
| --- | --- | --- |
| Ö1 | **Netlik:** bir nesne doğrudan 6 m'den ↔ aynadan toplam 6 m yolla (yansıtma oranıyla düzeltilmiş) | piksel piksel aynı (FLIP ≈ 0) |
| Ö2 | **Doğruluk:** iki paralel aynada tünelin n'inci kopyası analitik yerinde | < 0,1 px (n = 40'a kadar) |
| Ö3 | **Kararlılık:** hareketle kıpırtı, `tools/kararlilik.py` | Ada/Denizaltı seviyesi (~0,001) |
| Ö4 | **Aynadan ışık:** görüntü kaynak yöntemi ↔ çevrimdışı referans | ≥ 0,99 |
| Ö5 | **Hız:** 2560×1440 yerel, büyütme yok | 30-40 FPS (kare ≤ 33 ms), en kötü bakışta da |
| Ö6 | Bozuk piksel (NaN), sekme noktası lekesi | 0 |

## 5 · Sahne

- **Labirent:** 60° üçgen ızgara, ~1,2 m'lik ayna panoları, 2,4 m yükseklik, kabaca 5-6 hücrelik bir alan (~40-60 pano),
  ortada küçük bir boşluk. Panoların arasında ve üstünde Endülüs kemerleri (ahşap/alçı, yaldızlı kenar), tavanda koyu
  ahşap ya da muqarnas'ın kaba hâli. Desenler İslam geometrisinden (yıldız), kodla.
- **Biz:** ~0,35 m çaplı krom küre, ~1,3 m yükseklikte süzülür. Kamera merkezde; kendi küremiz birincil ışında görünmez,
  aynada görünür. Önünde bir yarık: el feneri konisi (yarığın biçimi aynadaki kürede okunur).
- **Hareket:** W A S D, Boşluk/C yüksel-alçal, Shift hızlı; aynalara ve çerçevelere çarpar (labirentte duvardan
  geçilmez). Fare bakış (Denizaltı'nın kontrol sözleşmesi: tıklamadan bakış, ters değil).
- **Işıklar:** kendi ışığımız (renk paleti bir tuşla: sıcak beyaz, kırmızı, yeşil, mavi, mor, ...; şiddet tekerlekle) ·
  **ışık bırakmak** (bir tuşla bulunduğun yere o anki renkte küçük cam lamba; en çok 8; bir tuşla hepsini topla) ·
  kemerlerde birkaç sönük pirinç fener (labirent tamamen karanlık olmasın; asıl ışık bizim).
- **Pus:** hafif ve düzgün; huzmeleri gösterir, yansımayı bulandırmaz (yoğunluk ölçülerek).
- **Ad:** "Ayna" (EN "The Mirror"), galeride 09, Isı'nın yerine. Vurgu rengi DURUM'da seçilir.

## 6 · Aşamalar (sıra = öncelik; kota biterse eldeki en önemli kısım bitmiş olsun)

| | İş | Kapı / ölçüm |
| --- | --- | --- |
| **A0** | Isı taslağını sil (kod, cfg, galeri resmi, kayıt, autotest adımı, docs/isi); "ayna" iskeleti, OptiX hattı, `--headless` kare aracı + `--cam` | derleme, autotest |
| **A1** | Kaba labirent + ayna malzemesi (fizik) + sabit örnekli sekme döngüsü + öteleme | **Ö2, Ö6** |
| **A2** | Işıklar: doğrudan + görüntü kaynak/hüzme ağacı, gölgeler; referans aracı | **Ö4** |
| **A3** | Pus: her parçada kapalı biçim, koni ve hüzme içindeki görüntü ışıkları | zikzak huzme karesi |
| **A4** | Küre (biz): krom, kamera, hareket ve çarpışma, ışık denetimi, ışık bırakma | autotest adımları |
| **A5** | Kalite: ışın konisi süzme, derin tünel ek örnek, hayalet görüntü, bütçe | **Ö1, Ö3, Ö5** |
| **A6** | Sanat: kemerler, desenler, zemin, fenerler, renk; Denizaltı ölçüsü | Özcan'a kareler |
| **A7** | **Kırılma (kapılı: Ö1-Ö6 tutmadan başlanmaz):** hızla çarpınca **çatlama** (darbe noktasından örümcek ağı çatlak, her parça hafif farklı açıda: yansıma parça parça kayar; çatlak kenarında camın yeşil parıltısı); çatlamış aynaya ikinci darbede **dökülme** (7-8 keskin parça Yıkım'ın katı cisim çözücüsüyle düşer, yerde her biri kendi açısıyla yansıtır; arkada çerçevenin tahtası görünür); R ile onarım | Ö1-Ö6 kırık hâlde de |
| **A8** | Kendi gözden geçirmem (ajan yok): kodu ve dört-beş bakış karesini baştan bir kez, Ö1-Ö6'yı son hâlde yeniden koş | |
| **A9** | DURUM.md başında sonuç + NE DOĞRULANMADI, galeri resmi, autotest, README sahne bölümü, hafıza hasadı, commit/push | |

Denizaltı dersi (Özcan 10-06): emeğin %30-40'ı doğrulamaya ve son %20'ye gitti. Burada **doğrulama = Ö1-Ö6 ölçümleri**;
**ajan yok** (Özcan: "paralel agent kullanma, kendin çalış"), gereksiz kontrol yok. Kalite yansımaya harcanır; labirent
süsünde azalan verime girilmez.

## 7 · Çalışma kuralları (otonom oturum)

- Önce oku: [[gpu-ve-grafik]] (§6, §7, §13-14, §19, §27, §30, §31), [[grafik-netlik-kurali]], bu plan.
- Her aşamada ölç, sayıyı DURUM.md'ye yaz; "temiz görünüyor" yetmez. Görüntü değişikliği kareye bakılmadan bitmiş sayılmaz.
- Önemli kareleri SendUserFile ile gönder (Özcan uyuyor; sabah bakar): kısa, Türkçe, ne gördüğünü söyle. Az ve anlamlı.
- **Ajan yok, kendin çalış.** Ölçümler Ö1-Ö6 dışında ek denetim katmanı kurma.
- Kota: 5 saatlik %90+ → sıfırlanmaya dek bekle (arka planda bekleme, sıfırlanma + 1 dk), sonra kaldığın yerden sürdür.
  **Haftalık kota bu görevde durma sebebi DEĞİL** (Özcan 10-07: "Haftalık kotayı dert etme [...] görevi bitirene kadar
  devam et"). Bitene kadar çalış.
- Sıra: hafıza → `claude_memory` commit/push → proje commit/push; her aşama sonunda commit.
- Ters bölü/`$` içeren yama: Edit aracı ya da Write ile .py (heredoc tuzağı). Derlemenin çıkış kodunu oku.
- Bitince: DURUM.md başı = Özcan'ın sabah bakacağı yer (nasıl açılır, tuşlar, ölçümler, NE DOĞRULANMADI).

## 8 · Başlangıç varsayılanları (oturumu hızlandırmak için; ölçünce değişebilir)

- **Yeniden kullanılacak kod:** kapalı biçimli pus ve parıltı (`fogDepth`, `lightGlow`, huzme `erf`), sabit örnek deseni,
  `--headless`/`--cam` kare aracı, NaN bekçisi ve "bozuk piksel" sayacı: Isı taslağında, silindikten sonra da git'te
  (`git show 9ef627e:src/scenes/isi/isi_kernels.cu`, `isi_app.cpp`). Canlı OptiX hattı ve NVRTC: `src/scenes/fener/`.
  Her karede değişen geometri ve katı cisim: `src/scenes/yikim/` (A7).
- **Tuşlar:** W A S D, Boşluk/C, Shift; fare bakış (Tab bırakır) · **Q / E** ışığımızın rengi (palet: sıcak beyaz 2700 K,
  gün ışığı 6500 K, kırmızı, turuncu, yeşil, camgöbeği, mavi, mor) · **tekerlek** şiddet (log ölçek, ~8 kademe) · **F**
  ışığımız aç/kapa · **Sol tık** ya da **G** buraya lamba bırak (o anki renk ve şiddet) · **X** lambaları topla · **R**
  başa dön (A7'den sonra aynaları da onarır) · F12 ekran görüntüsü (uygulamanın).
- **Panel:** ayar yok (karar 6). Yalnız durum: ışığımızın rengi/şiddeti, bırakılan lamba sayısı, kare süresi; kısa bir
  "nasıl" satırı. Ölçüm paneli yok.
- **Labirent ölçüleri (ilk hâl):** pano 1,2 m × 2,4 m, cam 6 mm, panolar arası 2 cm ahşap çıta; kemerler panoların
  üstünde 2,4-3,0 m; tavan 3,2 m; zemin cilalı koyu taş. 60° ızgarada 5-6 hücre, bir giriş, ortada bir boşluk.
- **Örnek bütçesi (ilk hâl):** pikselde 4 sabit örnek; koni genişliği > yerel ayrıntı ya da sekme > 8 ise 16'ya kadar.
  Kare bütçesi 2K'da ≤ 30 ms; aşılırsa önce ek örnek eşiği, sonra hüzme ağacının derecesi ölçülerek kısılır.
- **Ayna sabitleri:** gümüş, cam ve demir soğurması için değerler bir kaynaktan (cam üreticisi ya da optik tablosu)
  alınıp DURUM.md'ye kaynağıyla yazılır; tahminle değer girilmez.
- **Autotest adımları:** sahneyi aç, ileri uç, ışık rengini değiştir, iki lamba bırak, en derin tünel bakışı, pus
  huzmesi bakışı; her biri ekran görüntüsü + kare süresi.
