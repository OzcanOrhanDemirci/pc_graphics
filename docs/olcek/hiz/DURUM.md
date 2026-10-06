# DURUM · Ölçek'in ikinci seçeneği: "Hız"

> Tek kaynak durum panosu. Plan `docs/olcek/hiz/PLAN.md`. En yeni en üstte.

## 10-06 sabahı (2) · Özcan'ın iki revizesi: savrulan etiketler, yana bakınca bembeyaz ekran

Özcan'ın yolculuğu: 21 dokuz, Proxima'nın 156.000 ışık yılı ötesi, görüş 120°, enerji gözü açık.

**1) "Yüksek hızlarda yazılar sağa sola, yukarı aşağı savruluyor, okunmuyor."**

- **Sebep:** gözün dünyası galaksinin "şimdi"siyle yerleşiyordu. 21 dokuzda bir karede Dünya'da 11,7 yıl geçer: Dünya,
  Ay ve Proxima b her kare yörüngelerinin rastgele bir yerine atlıyordu. Roketin arkasındaki tekil nokta bu kaymayı
  büyütür: 156.000 ışık yılından 1 AU 1e-10 rad'dır, 21 dokuzda ise göğün 47°'si. Bir etiket zıplayınca çakışma kuralı
  yüzünden ötekiler de yanıp sönüyordu.
- **Düzeltme (fizik):** göz her sistemi ışığının yola çıktığı anda görür (gecikmeli zaman: şimdi eksi uzaklık bölü c).
  Işık hızına yakın uzaklaşan roket Dünya'nın sonraki ışığını geride bırakır: arkadaki Dünya kalkıştan kısa süre
  sonraki hâlinde donar. Önde Proxima b hızlı oynar ama küçülmüş görünür. Panolardaki "görünen Dünya" saati zaten
  böyleydi, göz de artık aynı (`renderEye`, `Scale::hizClocks(t, tProxima)`).
- **Düzeltme (okunurluk):** bir etiketin sonraki karede ekranda ne kadar kayacağı önceden hesaplanır: roketin kendi
  yolu, itiş ya da frenin değiştirdiği sapınç ve cismin yörüngesi (kalkıştan önce dünya durur, yörünge sayılmaz). Bir
  karede 2-5 pikselden hızlı kayan etiket yumuşakça söner. Örneğin Proxima'yı 21 dokuzda yeni geçmişken arkadaki
  etiketler çizgi gibi kayar, okunmaz.
- **Ek:** etiketler grafik panellerinin ve enerji anahtarının altına girmez. Biri bir panelin camının altında yarım
  okunuyordu.
- **Ölçüm:** Özcan'ın kaydının bir kopyasıyla canlı test (`--autotest-only olcek/hizkayit --set hiz.test.kayit=<dosya>`),
  0,1 sn arayla dört kare: "Proxima b" yerinde, yalnız uzaklığı 159 binden 160 bin ışık yılına ilerliyor. Aynı test
  eski saatle: etiket 0,1 sn'de ~66 piksel sıçrıyor, bazı karelerde kayboluyor. Sınama: bir kare sonra göze görünen
  Dünya 233 m kayar; eski "şimdi" onu 1,76 × 10¹¹ m (1,2 AU) atardı.

**2) "Parlak bir şeyin biraz yanına bakınca ekran bembeyaz oluyor, daha çevirince tak diye geçiyor."**

- **Sebep:** göz pozlamayı sabit 75°'lik orta pencereden ölçüyordu, Özcan'ın görüşü 120°'ydi. Parlak nokta (21 dokuzda
  öndeki tek nokta) pencerenin dışında ama ekranın içinde kalınca ölçüye girmiyordu. Parıltısı karanlık alanın
  pozlamasıyla bütün ekranı boğuyordu. Ekranın kenarından çıkınca parıltı sert kesildiği için beyazlık bir anda
  geçiyordu. Başsız tarama: bakış 0,70-1,04 rad arasında ekranın %100'ü beyaz, 1,10'da siyah.
- **Düzeltme:**
  - Ölçü penceresi ekrandan dar olamaz: geniş görüşte ekranın tamamı ölçülür. Dürbünle daralınca pencere 75°'de kalır.
  - Büyük cisimlerin ışığı merkez noktasıyla değil, diskinin pencereye düşen payıyla sayılır (dört halka). Kalkışta
    Dünya'nın yalnız ince kenarı ekranda kalınca da ölçülür; önce gece göğüne uyup beyaz bir şerit oluyordu. Galaksilerde
    disk, ışığın çoğunu taşıyan iç bölge (görünür yarıçapın 0,35'i): düzgün disk sayılınca ortadaki Andromeda eksik
    ölçülürdü.
  - Parıltı ekranın kenarına yaklaşırken son %6'da söner (güneşlik gibi), sert kesilmez. Kenarda sıfır: bir yıldızın
    parıltısı çizilen noktasından gelir, noktalar kenardan birkaç piksel sonra biter.
  - Göz uyumu: parlak bir şey görüş alanına girince göz bir anda en çok 3 kademe fazlasına iner ve o kare yeniden
    çizilir (beyaz kare yok). Sonra 0,3 sn'de ışığa, 1,2 sn'de karanlığa uyar. Kural tek yerde: `hz::eyeAdapt`, sahne
    ve yolculuk sınaması aynısını kullanır (sınama kareyi yeniden çizmez, yani sahneden katıdır).
  - Bakılan yüze uyumda Doppler eksikti: roketin arkasındaki kızıla kaymış bir hayaletin yüzü durgunmuş gibi
    sayılıyor, 21 dokuzda pozlamayı EV -10'a çekiyordu. Artık yüzün parlaklığı D⁴ katı, görünür payı D·T'de. Göz en
    çok 10⁵'e uyar, ötesinde ölçü yerinde durur (yüz kamaştırır). Duruşta bile 10⁵'i aşan yüz (Güneş) hiç ölçülmez,
    eskisi gibi.
- **Bağımsız inceleme** (salt okuma, ayrı ajan) bir gerçek hata buldu: ilk düzeltmem eski sert eşiği Doppler'li
  parlaklığa uyguluyordu. Proxima'ya otomatik varışta öne bakarken yüz 0,45 c'de ölçüden düşüyor, 0,11 c'de geri
  giriyordu: tek karede 7,8 kademe. Tavanla düzeltildi; sınaması eklendi ve hatalı sürümde KALDI (7,79 kademe, β 0,106),
  düzeltilmişte en büyük adım 0,51 kademe. Üç küçük bulgu da düzeltildi: yıldız parıltısının kenarı, itişte sapınç
  değişimi ve kalkıştan önce duran dünya etiket kestiriminde, okunamayan test kaydı artık söylenir.
- **Ölçüm:** aynı tarama düzeltmeden sonra beyaz piksel yok (parlak piksel en çok %0,8), kenardan çıkış yumuşak.
  Görünür gözde, 75°'de, kalkışta Dünya'nın çevresinde ve Proxima durağında da tarandı: beyaz en çok %1,1 (parlak
  bulutlar). Canlı testte R ile öne dönerken parlak nokta ekrana giriyor: 24 kare (0,05 sn arayla), beyaz piksel en
  çok %0,06, ortalama 20,9'a çıkıp (kamaşma) 0,3 sn'de 8'e oturuyor.
- **Eski/yeni kıyası** (başsız, 11 durum × 2 göz; HEAD ile bu değişiklik): 17 kare bit bit aynı. Değişenler: kalkışta
  arkaya bakış (%0,9 piksel, en çok 37 seviye: Dünya 0,14 sn önceki hâliyle, ~0,2 piksel); Proxima durağı (%0,001: b'nin
  ışık zamanı, parlama saati); 21 dokuzda görünür göz (EV -1,18 → 12,29: eski kare simsiyahtı, göz arkadaki hayalet
  Dünya'ya uyuyordu; yenisinde tasarımdaki gibi öndeki tek nokta görünüyor).
- **Ek:** Proxima'nın parlamaları float saatle 8 saniyelik adımlarla atlıyordu (4 yılda float'ın adımı). Saat
  70.000 sn'de sarılır, artık akıcı.

**Sınamalar:** `--test olcek-hiz` 54/54 GEÇTİ, dördü yeni: "geniş görüş ekrandaki parlak noktayı ölçer", "ekranda kalan
parlak bir kenar ölçülür", "Proxima'ya varışta öne bakan gözün ölçüsü yumuşak", "arkadaki Dünya ve b ışıklarının
zamanında durur". Tam yolculuk (`--test olcek-hiz-yolculuk`): GEÇTİ: Andromeda'ya varıldı, 3.682 göz karesi, ortalama 11,0 ms (en uzun 21,7),
sonlu olmayan kare 0, iki kare arası ışık en çok %1,14 (önce %1,02).

## 10-06 sabahı · Özcan'ın ilk denemesi: "direk ışık hızına yakın başladı"

- **Sebep:** fizik değil, kayıt. Sahne dünkü yolculuğu `hiz_yolculuk.txt`'ten kaldığı yerden açtı.
  - O yolculuk eski ivmeyle (0,4 c/s) uçulmuştu: Proxima ayağı 148 sn sürmüş. Yeni ivmede 913 sn.
  - Kayıt sabahın ikinci açılışında Andromeda yolunda φ 21,27'ydi (18 dokuz). İlk açılışta da yolculuk ışık hızına
    yakındı: 59 saniyelik ilk oturumda yeni ivme 21,27'ye ancak φ ≥ 8,85'ten (7 dokuz) çıkabilir. Andromeda ayağının
    t − x/c kaydı da eski ivmenin izini taşıyor (bağımsız inceleme, dosya zamanlarıyla).
- **Düzeltme:**
  - Kayıt biçimine sürüm geldi (`hz::kSaveVersion` = 2: H2'nin ivmesi). Başka sürümün yolculuğu sürdürülmez,
    yolculuk Dünya'nın yanından başlar. Yola çıkmış eski bir yolculuğun dosyası `hiz_yolculuk.txt.eski-surum`
    olarak saklanır (`.yedek` her oturum döner, orada kalmaz).
  - Hız'a ilk girişte ne olduğu söylenir:
    - sürdürülen bir yolculukta "Kaldığın yerden: <ayak>, <hız>. Baştan başlatmak için: panelde 'Yolculuğu baştan
      başlat'". Hız duruyorsa "duruyorsun", yavaşsa "β 0,88", hızlıysa "18,2 dokuz". Panelde de ilk W, S ya da Boşluk'a
      (en çok 20 sn) kadar **"Kaldığın yerden"** işareti kalır: bildirim 3 sn'de söner, kaçabilir.
    - sürdürülmeyen bir yolculukta nedeni: eski sürüm (ivme değişti), daha yeni sürüm ya da okunamayan kayıt.
  - Sınama: aynı kayıt "hiz.surum = 1" ile reddedilir.
- **Doğrulama:** iki bağımsız inceleme (teşhisi çürütme, düzeltmeyi kırma). Normal bir açılışta hızlı başlamanın başka
  yolu bulunmadı (test düzenekleri yalnız `--autotest`'te, kayda dokunmuyor). Özcan düzeltilmiş derlemeyi 06:50'de
  açtı: eski kayıt reddedildi, yolculuk Dünya'dan başladı, otomatik varışla Proxima'ya uçtu.

## 🏁 Özet (10-06 sabaha karşı): H0-H6 bitti, otonom gece

**Özcan'ın bakacağı yer:** uygulamayı aç, Ölçek, panelde **Seçenek: Hız**, ardından **Boşluk** (otomatik varış).
Yolculuk gerçek zamanda geçer: Proxima 15 dk, Andromeda 72 dk. Ne zaman neye bakılacağı aşağıda.

| An | Roketin gözünde | Yandan |
|---|---|---|
| Kalkış | önde Samanyolu; arkada (sürükle) Dünya, bugünün bulutları | roket, Dünya ve Ay (2) |
| %1-60 c (1-4 dk) | gök öne toplanır, Macellan Bulutları görüş alanına girer | ortam noktaları çizgi olur |
| %60 c'den sonra | **arkadaki Güneş öne gelir**: mavi-beyaz, göz kamaştırır | Güneş Sistemi (3) |
| ~5 dokuz | **önde kırmızı bir güneş doğar**: kozmik fon, sonra turuncu, beyaz, mavi | yakın yıldızlar (4) |
| Proxima | lekeli turuncu disk, b, Alfa Erboğa A/B, Kraliçe'de Güneş (etiketli) | Proxima diski |
| 21 dokuz | önde tek nokta (enerji gözünde mor-beyaz gama) | Samanyolu (5), Yerel Grup (6) |
| Fren | Andromeda küçük mavi elipsten 70°'lik altın sarmala | iki galaksi arasında roket |

**E** enerji gözünü kapatıp açar. Kapalıyken gerçek göz kalır: ışık görünür banttan çıkınca kararır.

**Kararlar** (Özcan bıraktı, benim seçimim): gözde aracın parçası yok. Enerji gözü fonun duruştaki parıltısını
çıkarır. Göz bakılan yüze uyar, Güneş'e uyamaz. Güneşlik: parlama yalnız görüş alanından. Proxima b 60° ötede. Yandan
haritada Proxima'nın rengi tavanla korunur.

## 2026-10-06 sabaha karşı · H6: bütünleme

- **Ayrıntılar** (galeri): Hız için bir başlık, kaynaklarıyla (ESO atfı dahil). Etiketlere "Özel görelilik". F1
  yardımına **E**. İlk denemede üç başlık sayfaya sığmadı, birinci seçeneğin başlıkları kesildi. Hız tek başlıkta
  toplandı. Uygulamaya bir güvence eklendi: sığmayan metinde gövde yazısı %85'e kadar küçülür, hiçbir sahnede başlık
  kesilmez. Otomatik testin `galeri_ayrintilar_7` karesinde altı başlık iki sütunda tam.
- **README:** "The second option: Speed" bölümü, altı resim (`docs/images/olcek-hiz-*.jpg`: enerji gözü, ilk ışık,
  Andromeda'ya fren, Proxima, roket, Samanyolu haritası).
- **GOSTERI / DEMO:** Hız'ın tuşları ve "Dene" (iki dil).
- **Otomatik test:** `olcek/hiz` kalkışta E ile gerçek gözün karesini de alır (`olcek_hiz_gercek_goz`).
- **Tam yolculuk sınaması** (`--test olcek-hiz-yolculuk`, başsız):
  - otomatik varışla Proxima'ya, dönüp Andromeda'ya; fizik kare kare (60/sn);
  - göz her 10 saniyede, bir kare arayla iki kez, pozlaması sahnedeki gibi uyumlu; her 60 saniyede yandan üç ölçek.
  - Sonuç: **GEÇTİ**. Yolcunun saati 87,6 dk, Dünya'da 2,395 milyon yıl. Göz 3.682 kare: ortalama 11,0 ms, en uzun
    21,8 ms (2560×1440). Yandan 264 kare, en uzun 4,4 ms. Sonlu olmayan piksel yok (ton eğrisinden önceki HDR
    arabellek). İki ardışık kare arasında ışık en çok %1,02 değişiyor (Andromeda freninin başında): titreme yok.
- **Bulunan ve düzeltilen (bu sınamayla):** %60 c'de sapınç Güneş'i görüş alanının hemen dışına (39°) taşıyordu. Göz
  daha geniş bir koniye uyduğundan görünmeyen Güneş'e uyuyor, kare kapkara oluyordu. Parlaması da kenardan sızıyordu.
  Şimdi göz görüş alanının dikdörtgenine uyar (dürbünsüz hâli), parlama ekranda görünenden gelir. Güneş kareye girene
  kadar yıldızlar görünür. Girince mavi-beyaz bir yıldız olarak merkeze kayar.
- **Bedel panosu** değişmedi (ince ayar gerekmedi).

### NE DOĞRULANMADI (H6)
- **Özcan'ın gözü:** hiçbiri. Kendisi uyurken yapıldı.
- **Canlı uygulamada tam yolculuk:** başsız sınama fizik ve çizimi kare kare koştu. Gerçek pencerede, gerçek zamanda
  baştan sona bir yolculuk izlenmedi. Otomatik test kalkışı, bir dakikayı, Proxima varışını ve 21 dokuzu kapsıyor.
- **Canlı kare hızı:** başsız ortalama 11 ms. Pencerede arayüzle ölçüm yalnız otomatik testin anlarında.
- **İngilizce Ayrıntılar** sayfası ve README'nin GitHub'daki görünüşü gözle görülmedi.

## 2026-10-06 gecesi · H5: duraklar

**Özcan'ın bakacağı yer.**
- **Kalkış:** sürükleyip arkana bak. Dünya 17° geniş (yer eşzamanlı yörünge uzaklığı). Ölçek'in fotogerçekçi Dünya'sı,
  üstünde bugünün bulutları ve havanın ince mavi kenarı; %74'ü aydınlık (Güneş burnun 61° yanında).
- **Proxima:** önde 1,69°'lik turuncu bir disk. Üstünde büyük lekeler, kenarı kararıyor, 70 saniyede bir parlıyor
  (bir parlama yıldızı). Bakışını çevir:
  - Alfa Erboğa A ve B parlak bir çift yıldız (−6,8 ve −5,5 kadir);
  - Proxima b solda, 60° ötede, dördüncü kadirden bir nokta (dürbünle 60° evrede bir disk);
  - arkada Güneş: Kraliçe takımyıldızında bir yıldız.
  Hepsinin üstünde adları ve uzaklıkları yazıyor.

**Gözde gök cisimleri** (`Scale::eyeBodies`):
- Ölçek'in toplarıyla çizilir: Güneş, gezegenler, Ay; yeni olarak Proxima ve b. Çekirdek topları pikselin durgun
  çerçevedeki ışınıyla keser (sapınç) ve ışıklarını renk sıcaklıklarında kara cisim gibi kaydırır (Doppler).
  Küçükken nokta olurlar.
- Güneş ve Proxima'nın ışığını katalogdaki yıldızları taşır (pozlama toplamı ve nokta). Topları büyüdükçe noktaları
  söner (iki kez sayılmaz).
- Dünya'nın havası yandan görünüşün kodundan (`Scale::earthAir`, tek kaynak). Haritalar artık iki seçenekte de aynı
  işlevle kuruluyor (`setMaps`); önce gözde yer tutucu kıtalar çıkıyordu.
- **Proxima Centauri:** 0,1542 R☉ (Boyajian 2012), 3.042 K, görünür ışığı V 11,13'ten (M_V 15,56). Rengi gözün CIE
  tablosundan (dünyanın üç dalga boylu kaba kara cismi turuncuyu vermiyordu).
  - Yüzü (`dwarfSurface`): soğuk yıldızın güçlü kenar kararması, yüzün beşte biri kadar koyu leke (~2.700 K), piksel
    altında sönen ince granülasyon.
  - Her 70 saniyede bir parlama: iki saniyede yükselen mavi-beyaz küçük bir leke, bir dakikada söner. Yeri parlamanın
    numarasından, göze dönük yüzde.
- **Proxima b:** 1,07 M⊕, ~1,02 R⊕ kayalıksa; bir temsil (resmi yok). Proxima'nın ışığıyla aydınlanır. Yörüngesi
  (a 0,04857 AU, P 11,1868 gün; Faria 2022) Güneş-Proxima çizgisinden geçen düzlemde. Otomatik varışın anında durağın
  60° ötesinde.
  - Neden 60°: daha yakında Proxima'nın ışığında kaybolur. Ona uymuş bir göz yanında gezegen görmez. Bakınca Proxima
    uyum konisinin dışında kalır, b karanlık gökte bir nokta olur.
- **Alfa Erboğa A ve B** ölçülen yerlerinde. J2000 konumları ve öz hareketleri (Kervella 2016), Akeson 2021
  paralaksı (750,81 mas), dikine hızları, astropy ile 2026-10-05'e taşındı. HYG ikisini 1,3248 pc'de tutuyordu.
- **Etiketler** (`drawEyeLabels`, arayüzle): Güneş (Proxima yakınında "Kraliçe takımyıldızında"), Dünya, Ay,
  Proxima Centauri, Proxima b, Alfa Erboğa A ve B (tek nokta iken tek etiket), Andromeda, Üçgen, Samanyolu'nun
  merkezi. Konumları ışıklarının sapınçlı yönü. Çakışan ve panellerin altında kalan atlanır.

**Göz** (sanat yönetimi fiziğin araçlarıyla):
- **Bakılan yüze uyum:** göz, bakışın ortasındaki bir cismin yüzüne uyar. Koşul: 0,2-1°'den geniş olması (dürbünde
  büyütülmüş hâli). Yüzünün en parlak yeri ekranın ~0,5'ine gelir, rengi doygunluğunu korur.
  - Sınırı 10⁵ W/m²/sr: bir lambanın yüzüne uyulur, Güneş'inkine (2 × 10⁷) uyulmaz, kamaştırır.
  - Önce: göz alanın ortalamasına uyuyor, Dünya'nın bulutları bembeyaz patlıyordu, Proxima sönük kahverengi bir
    diskti.
- **Güneşlik:** parlama yalnız görüş alanındaki kaynaklardan (bir objektifin başlığı gibi). Kalkışta Güneş burnun
  61° yanında. İnsan gözünün saçılmasıyla bütün göğü beyaz bir perdeyle örterdi.
- **Dönüşten sonra** (153°, 8 sn) göz Proxima'dan karanlığa birkaç saniyede uyar. O arada gök siyah (otomatik testin
  `olcek_hiz_donus` karesi o an).

**Yandan harita:** Proxima ve b orada da cisim (katalogdaki Proxima yıldızı yandan çizilmez, iki kez olmasın).
Derin uzayın pozlamasında (EV 23,5) beyaz bir leke olacaklardı. Ekrandaki en parlak yerleri bir tavana çekilir (tek
çarpan: lekeler ve kenar kalır). Kızıl cüce haritada turuncu bir disk.

**Ölçümler:**
- Kalkışta arkaya bakınca göz EV −1,18 (gece göğünde 17,50). Bulutlar ~0,5.
- Durakta Proxima 1,691°. Göz ona bakınca EV −7,9. Ölçü 7,7 × 10⁴, alanın ortalaması 238.
- Alfa Erboğa Proxima'dan: A 12.016 AU, −6,79 kadir; B 12.010 AU, −5,43 (HYG'nin B parlaklığıyla).
- b Proxima'dan 60,0°, duraktan 0,0486 AU.
- Kare (2560×1440, başsız): görüş alanında top varken 10,7-11,5 ms (piksel başına dört örnek), yokken 6-7 ms.
- Birinci seçenek: s 1,0 / 2,5 / 7,2 / 9,0 / 12,0 / 16,5'te H5 öncesinin kareleriyle **bit bit aynı** (önceki commit
  stash ile derlenip karşılaştırıldı).

**Sınamalar** (4 yeni, hepsi GEÇTİ): Alfa Erboğa'nın uzaklık ve kadirleri; Proxima'nın açısı ve b'nin yeri; Proxima'ya
bakan gözün onun yüzüne uyması; kalkışta Dünya topu ve gündüze uyan göz.

### NE DOĞRULANMADI (H5)
- **Özcan'ın gözü:** hiçbir durak karesine bakmadı.
- **Parlamalar** hiçbir karede yakalanmadı (zamanlamaları 70 sn). Biçimleri yalnız koddan.
- **Etiketler** yalnız otomatik testin Proxima varışında görüldü (Proxima ve b). Kalkışta, Alfa Erboğa'da ve
  Andromeda'da ekranda görülmedi.
- **Canlı kare hızı** "yalnız alanda top varken dört örnek" değişikliğinden sonra ölçülmedi (önce Proxima'da 69 fps).
- **Yörüngeye oturma:** roket iki durakta da durur. b'nin yörüngesinde dolanma gösterilmedi (11 günde bir tur:
  dakikalarda fark edilmez).
- Proxima b'nin görünüşü, yörünge düzlemi ve evresi temsil. Haritadaki tavan ve gözün "bakılan yüze uyum" eşikleri
  tasarım.

## 2026-10-06 gecesi · H4: galaksiler roketin gözünde

**Özcan'ın bakacağı yer.** Andromeda yolunun sonu. Frenin son ~6 dakikasında (φ 5'ten 0'a) Andromeda önce küçük, mavi-beyaz
bir elips olarak belirir (önde sıkışmış, D⁴ ile parlamış). Hız düştükçe büyür, sapınçla bükülür, rengi kendi altınına
döner. Varışta (merkezin 100.000 ışık yılı önü) gökyüzünün ~70°'sini kaplar, solunda Üçgen (M33).

**Ne yapıldı:**
- Ölçek'in galaksi modelleri (Samanyolu, M31, M33; ince düzlem: kollar, çubuk, toz, kümeler, HII bölgeleri) tek işleve
  taşındı (`Scale::galaxyModels`). Birinci seçenek ve göz aynı kaynaktan.
  - Birinci seçenek değişmedi: dört ölçekte kareler öncesiyle karşılaştırıldı. İkisi bit bit aynı, ikisinde 1 ve 8
    pikselde 1/255. Aynı kare iki koşuda da 1/255 oynuyor.
- **Gözde:** her piksel için durgun çerçevedeki yön (sapınç), model o yönde değerlendirilir. Pikselin izi D kat büyük.
  Işığı galaksinin renk sıcaklığındaki kara cisim gibi kayar: Samanyolu 4.800 K, M31 4.500 K, M33 5.800 K.
- **Küçükken nokta:** galaksinin ekrandaki yarıçapı (durgun açısı / D) 1,5-4 pikselin altına inince model söner, ışığı
  nokta olur. Büyük galaksiler pozlama toplamına hep nokta olarak girer.
  - Görünür parlaklıklar: Samanyolu modelin kendi 5,1 × 10¹⁰ L☉'si, M31 M_V −21,5 (3,4 × 10¹⁰), M33 M_V −18,8
    (2,8 × 10⁹).
- **2MRS'in 43 bin galaksisi** gözde nokta (2,5 Mpc'nin ötesi; Yerel Grup sahnenin kendi modelleri, iki kez sayılmaz).
- **Samanyolu:** panorama 300 ışık yılından 3.000'e söner, model onun yerini alır. Modelin kümeleri ve HII bölgeleri
  gözde 0,35 kat küçük (ışıkları korunarak). Uzaktan bakış için kuruldular; birkaç bin ışık yılından derecelerce
  geniş pembe lekeler oluyorlardı. Birinci seçenekte 1 kalır.
- **Enerji gözü değişti: fonun duruştaki parıltısı çıkarılıyor.** Göz yolculuğun başında sıfırlanmış bir dedektör:
  her yöndeki fon ışıması σT0⁴/π sıfır sayılır (gökbilimcinin gök çıkarması). Kalan, hareketin önde kattığı. Arkada
  sıfırın altı siyah.
  - Neden: varışta fon, Andromeda'nın bütün ışığının 10 katı. Enerji gözünün (açık başlar) finali bordo bir perdenin
    arkasında soluk bir galaksiydi. Duruşta da her yeri kırmızıya boyuyordu.
  - Şimdi: duruşta enerji gözü gerçek göğe yakın (yıldızların kızılötesi ve morötesi eklenir; Proxima kırmızı bir
    nokta). φ 1,5'teki kırmızı sis ve doğan güneşler aynı. Açıklamanın alt yazısı: *"fonun duruştaki parıltısı
    çıkarılmış"*.

**Ölçümler** (gerçek göz, duruşta, 75°):

| Yer | gökyüzü (görünür, W/m²) | göz EV | görüntü |
|---|---|---|---|
| Güneş'in yanında | 7,0 × 10⁻⁶ | 16,95 | Samanyolu |
| Güneş'ten 3.000 ışık yılı, arkaya | 2,2 × 10⁻⁵ | 18,47 | diskin altı: kollar, kümeler |
| 30.000 ışık yılı, arkaya | 7,4 × 10⁻⁶ | 16,01 | Samanyolu'nun yarısı, çubuk |
| 100.000 ışık yılı, arkaya | 1,3 × 10⁻⁶ | 17,26 | bütün sarmal, alttan |
| yolun ortası (1,2 milyon ışık yılı) | 2,6 × 10⁻⁸ | 20,92 | önde M31 (~9°) ve M33; arkada Samanyolu |
| varış | 1,17 × 10⁻⁶ | 17,35 | M31 ~70° |

- Fren eğrisinde (`--set hiz.test.fren=1`): φ 5'te M31 0,6° (EV 18,05), φ 2'de büyük mavi-beyaz kubbe (16,18), φ 0,5'te
  kendi renginde (16,42).
- Kare 6-7 ms (2560×1440).

**Sınamalar** (2 yeni, hepsi GEÇTİ):
- Enerji gözünde fon: duruşta yalnız dipolün izi (1,5 × 10⁻⁸ W/m²). Hızda bütün göğün kazancı
  4σT0⁴(γ²(1 + β²/3) − 1) kadar ya da biraz fazlası (arkadaki eksik sıfırda kesildiği için).
- Varışta göğün ışığı M31'in L/4πd²'si (%1 içinde). Model varışta ve yolun ortasında açık, 21 dokuzda bir nokta.

### NE DOĞRULANMADI (H4)
- **Özcan'ın gözü** ve **hareketle izleme:** kareler başsız, sabit φ'lerde.
- **Galaksinin içinden görünüş:** ince düzlem modeli 3.000-20.000 ışık yılında diskin altını bir "zemin" gibi
  gösteriyor. PLAN'ın hacimsel Samanyolu'su yapılmadı. Normal yolculukta bu bölge 21 dokuzda geçilir (gözde yalnız
  önde bir nokta); yalnız orada durulursa görünür.
- **Panoramayla modelin örtüşmesi** (1.000 ışık yılında %16 model) ayarlanmadı: modelin ufuk çizgisi kuşağın altında
  ince bir çizgi olarak görünüyor.
- **Galaksilerin parlaklıkları** yayımlanmış mutlak kadirlerden. Renk sıcaklıkları seçildi. Doppler kayması her
  galaksiye tek sıcaklıkla; bileşenlerin (yaşlı disk, genç kollar) ayrı kayması yok.
- M31'in uyduları (M32, M110), Macellan Bulutları dışarıdan görünüşte yok.

## 2026-10-06 gecesi · H3: roketin gözünde gökyüzü

**Özcan'ın bakacağı yer.** Hız → roketin gözü (açılış görüntüsü). **Enerji gözü açık başlar** (panelde Açık | Kapalı,
ya da **E**; seçim kayıtta kalır). Üst ortada adı ve bantların renk şeridi. W ile hızlan: önce Samanyolu'nun kuşağı öne
toplanır, %99 c'den sonra küçük bir yıldız yumağına döner. **~5 dokuzda (φ 6-7,5, yaklaşık 10 sn) evrenin ilk ışığı
önde kırmızı bir güneş olarak doğar**, turuncuya, altına, beyaza döner. Sonra mavi; enerji gözünde mor, en sonda
gama ışınının beyaz-moru.

**Gökyüzü** (`eyeSky`, çekirdekte piksel başına; `skySums`, pozlama için durgun çerçevenin ızgarasında):
- **Samanyolu'nun yaygın ışığı:** ESO/S. Brunier'in bütün gök panoraması (eso0932a, CC BY 4.0, künye
  `assets/olcek/KAYNAKLAR.md`). `tools/olcek_veri.py samanyolu`:
  - yıldızları 9 piksellik medyanla silindi (katalog onları nokta olarak çiziyor, iki kez sayılmasın);
  - her piksele renginden bir renk sıcaklığı (McCamy);
  - ölçek: en parlak birkaç derece (Yay bulutu) 20,5 V kadir/yay saniyesi²;
  - galaktik eşdikdörtgen, 2048×1024, yarım duyarlıklı.
  Ölçülen: panorama gece göğünün görünür ışığının %39'u. Yıldızlarla birlikte bir düzleme ~1,6 × 10⁻⁴ lux (aysız
  gecenin yıldız ışığı ~10⁻⁴ lux).
- **Hızda:** panoramanın her pikseli kendi sıcaklığındaki kara cisim gibi kayar. Rengi o kara cismin renginin döndüğü
  kadar döner, parlaklığı D⁴ × görünür payın değişimi.
- **Pırıltı yok:** panoramanın seviyeleri var (her biri yarısı). Pikselin durgun gökteki izi D kat büyüdüğünden (sapınç
  şekli korur), her piksel izinin genişliğindeki seviyeyi okur. Hız arttıkça sıkışan gök kareden kareye kıpırdamaz.
- **Kozmik fon ışıması:** T0 = 2,72548 K. Güneş'in ona göre hareketinden gelen dipol (Planck 2018: 3,362 mK, l 264,02°,
  b 48,25°). Her pikselde T0 × D, parlaklık σT⁴/π. Duruşta görünmez; φ 6,25'te önde koyu kırmızı belirir.
- **Örnekleme:** gökyüzü piksel başına dört örnek (kale deseni). Öndeki parlak başlık görme sınırında birkaç piksel.
- **Hesap:** çift duyarlık yalnız sapınçta (başlık e^−φ genişliğinde). Işık log₁₀ uzayında tek duyarlıkla toplanır.
  İlk sürüm her şeyi çift duyarlıkla yapıyordu: kare 15-25 ms. Şimdi **5-7 ms** (2560×1440, 109 bin yıldızla).
  Canlıda gözde 86 fps (kare 9,2 ms; ana işlemcide yıldızların hazırlığı 3-4 ms), yandan 160 fps.

**Göz** (sanat yönetimi fiziğin araçlarıyla):
- **Uyum:** karanlıkta ışığın iki katına yarım EV (parlak gök parlak görünür). Gündüz düzeyinde tam uyum (Weber: göz
  ışığı değil karşıtlığı tutar). Geçiş gece göğünün 2¹⁶ katında (`hiz.gun`), yumuşak.
  - Önce: φ 9'da önde gökyüzü 108 W/m² (Güneş'in 1/13'ü) ve yarım uyumla bütün kare mavi-beyaz bir sise boğuluyordu.
  - Şimdi: φ 7-16 arası aynı parlaklıkta bir güneş ve hâlesi.
- **Yaygın ışık kazancı** (karanlığa uyumlu gözün alan toplaması, Riccò): ×16, gündüzde 1'e iner.
- **Parlama** (gözün kendi saçılması, Stiles-Holladay: L = 10 E/θ², θ derece, 0,1°'den): öndeki parlak başlığın
  çevresinde (burnun 5 derecesindeki yaygın ışıktan) ve en parlak yıldızlarda. Ölçülen: φ 10'da fon ışımasının
  %99'dan fazlası o 5 derecede.

**Enerji gözü** (Özcan 10-06: *"göremediğimizi mantıklı şekilde görünür kılıp bu da varmış bu hızda diye öğretici
olur. Ama gerçek görüntüyü isteyen kapatır"*):
- Görünür pay yerine **bütün bantların ışığı**: yıldızlar, panorama ve fon ışıması. Rengi o ışığın tepe yaptığı bant
  (Wien):
  - koyu kırmızı: mikrodalga;
  - kırmızı-turuncu: kızılötesi;
  - görünür bant: kendi rengi;
  - mavi: morötesi;
  - mor: X ışını;
  - beyaz-mor: gama.
- ~~Göz bütün bu ışığa uyar. Duruşta gece göğünün ışığının %60'ı fon ışıması: enerji gözünde her yer hafif kırmızı,
  Samanyolu gerçek gözdekinden 1,6 EV sönük.~~ *(H4'te değişti: fonun duruştaki parıltısı çıkarılıyor, yukarıda.)*
- **φ 1,5'te** (β 0,905): önde kırmızı bir sis, içinde Samanyolu'nun kuşağı ve iki Macellan Bulutu. Fon ışıması önde
  D⁴ = 403 kat parlak.
- **Açıklama** (`drawEnergyKey`): üst ortada, arayüzle görünür. Altı bant eşit genişlikte. İki üçgen gösterir: öndeki
  fon ışımasının ve Güneş gibi bir yıldızın ışığının o an tepe yaptığı bant. Duruşta mikrodalga ve görünür; 21 dokuzda
  gama ve gamanın ötesi.

**Ölçümler** (başsız kareler `--hiz --phi … --set hiz.enerji=0|1`, Proxima yolunun ortası, 75°):

| φ | dokuz | göz EV | önde görünür (W/m²) | bütün bantlar (W/m²) | görüntü (gerçek göz) |
|---|---|---|---|---|---|
| 0 | 0 | 16,95 | 7,0 × 10⁻⁶ | 2,1 × 10⁻⁵ | Samanyolu, toz şeritleri |
| 1,5 | 1,0 | 15,38 | 1,8 × 10⁻⁵ | 1,7 × 10⁻⁴ | kuşak parlıyor, öne toplanıyor |
| 3 | 2,3 | 15,55 | 1,4 × 10⁻⁵ | 3,2 × 10⁻³ | kuşak ve yıldızlar ortada |
| 6 | 4,9 | 16,65 | 3,0 × 10⁻⁶ | 1,3 | küçük beyaz yumak |
| 6,5 | 5,3 | 11,64 | 3,0 × 10⁻³ | 3,5 | kırmızı güneş doğuyor |
| 7 | 5,8 | 6,70 | 0,40 | 9,6 | turuncu güneş, hâle |
| 9 | 7,5 | −1,28 | 108 | 523 | mavi-beyaz güneş |
| 16 | 13,6 | 4,55 | 1,9 | 6,3 × 10⁸ | mavi, sönmeye başlıyor |
| 24,52 | 21 | 12,70 | 7,1 × 10⁻⁴ | 1,6 × 10¹⁶ | küçük mavi nokta (ışık gamada) |

**Sınamalar** (`--test olcek-hiz`, 5 yeni, hepsi GEÇTİ):
- Fon ışımasının roketteki toplamı, gökyüzü toplamlarının kendi ızgarasıyla: 4σT0⁴γ²(1 + β²/3). Duruştan 21 dokuza
  her hızda en çok 1,5 × 10⁻⁴ farklı (dipol ve ızgara).
- φ 10'da fon ışımasının %99'dan fazlası burnun 5 derecesinde.
- Gece göğü yıldız ışığı mertebesinde.
- Panorama gece göğünün bir parçası; renk sıcaklıkları 2.000-15.000 K.
- Göz: duruşta `hiz.ev`. Karanlıkta iki kat ışığa −0,500 EV, gündüzde −1,000 EV.
- Enerji gözünün renkleri: parlaklık 1 (1,7 × 10⁻⁷), sıçramasız.

**Benim seçimim (PLAN §3.1, Özcan bıraktı): gözde aracın parçası yok.** Pencere kenarı ya da burun, öndeki tek ışık
noktasının çevresini kapatır ve görüş alanını daraltır. Roket yandan görünüşte zaten var. Göz serbest bir göz.

### NE DOĞRULANMADI (H3)
- **Özcan'ın gözü:** roketin gözünün hiçbir karesine bakmadı. Enerji gözünün renk dili ve duruştaki kırmızı ton onun
  beğenisine kaldı.
- **Hareketle:** kareler başsız ve sabit φ'lerde. Canlı yolculukta hızlanırken kıpırtı olmadığı (seviyeler, dört
  örnek) yalnız tasarımdan; gözle izlenmedi.
- **Panoramanın parlaklık ölçeği** (Yay bulutu 20,5 kadir/yay saniyesi²) yayımlanmış bir yüzey parlaklığına yaslanan
  bir yaklaşık. Gece göğünün toplamı yalnız mertebe olarak sınandı.
- **Parlamanın** 0,1°'lik çekirdeği ve kaynakları (5°'lik koni, en parlak 7 yıldız) tasarım. Stiles-Holladay 1°'nin
  altında geçerli değil.
- **Güneş'ten uzakta:** panorama 300 ışık yılından 3.000'e söner. Galaksiden çıkışın ışığı (Samanyolu'nun hacimsel
  modeli) H4'te. Arada gözde yalnız katalog yıldızları ve fon ışıması var.
- **Fon ışımasının anizotropileri** (Planck haritası) yok; yalnız dipol.
- README ve galerinin "Ayrıntılar"ına ESO atfı H6'da girecek (künye şimdilik `KAYNAKLAR.md`'de).

## 2026-10-06 gecesi · H2 (1. kısım): yeni ivme ve yandan harita

**Özcan'ın bakacağı yer.** Hız → **Tab** (yandan). Roket ortada, kamera onu izliyor. **1-6** ölçek durakları: roket ·
Dünya ve Ay · Güneş Sistemi · yakın yıldızlar · Samanyolu · Yerel Grup. W'ye basınca motorun ışığı yanar; hızlandıkça
yanından akan ortam noktaları çizgiye döner.

**İvme (PLAN §2, Özcan'ın takvimi).** Öz-ivme hızın bir işlevi: duruşta 1 g, hep artar, tavanda 11,1 milyon g.
- Tasarım: on katlık her hız artışının süresi f(log₁₀ φ), 11 sn'den 155 sn'ye bir lojistik. Dört sayısı takvime
  oturtuldu.
- 1.638 ince dilimde sabit (komşular %1 farklı). Her dilimde sabit ivmenin kapalı biçimleri, fizik her karede tam.
- Fren itişin aynası. Durma mesafesi D(φ), otomatik varışın tepesi D(φ*) = (r + D(φ))/2'den, dilim içinde kapalı ters.
- W ile duruştan:
  | Hız | Süre | İvme |
  |---|---|---|
  | 300 km/sa | 7,5 sn | 1,8 g |
  | ses hızı | 14 sn | 7 g |
  | kaçış hızı (11,2 km/s) | 31 sn | 238 g |
  | %1 c | **60,0 sn** | 36.700 g |
  | %10 c | **120,0 sn** | 59.300 g |
  | %99 c | **330,0 sn** | 1,2 milyon g |
  | 21 dokuz | **480,0 sn** | 11,1 milyon g |
- Yolculuklar: **Proxima 912,89 sn (15,2 dk)**, en çok 14,7 dokuz, Dünya'da 4,2457 yıl. **Andromeda 72,24 dk** (8 dk itiş,
  56,2 dk 21 dokuzda süzülme, 8 dk fren), Dünya'da 2,3951 milyon yıl.
- **Bağımsız kâhin** (mpmath, 50 basamak, dilim tablosundan kendi integraliyle; C++ koduna bakmadan): T, C, D dokuz
  noktada, tavan ve iki tepe **1,5 × 10⁻¹⁵ içinde aynı**. Takvim 60,0001 / 120,002 / 330,004 / 480,004 sn. Kâhinin tek
  işareti: kare kare uçuşta yolcunun saati 72 dakikada 7,9 ns (10⁻¹² göreli) farklı, 260 bin karede double olarak biriken
  saatin yuvarlaması. Fizik değil; testin eşiği 1 µs.
- Canlı: W ile 5 sn'de 52 m/s, 10 sn'de 154 m/s; otomatik varışla bir dakikada %3,7 c.

**Yandan: Dünya'nın çerçevesi (evrenin haritası).** Özcan: *"Yalnız 1 olsun"*.
- H0'ın roket çerçevesi ("ölçülen dünya": noktaların kısaltılması, ışın gerdirmesi `stretchX`, γ ile solan galaksiler)
  kodun her yerinden söküldü.
- Galaksi ölçeklerinde kamera hareket ekseni etrafında galaksi düzleminin 32° üstüne çıkar (`s` 16,5 → 19). Diskler eğik
  görünür (referanstaki gibi). Kenardan bakınca Samanyolu sönük bir çizgiydi; içinden bakınca gri bir duvardı.
- Yerel Grup ölçeğinde kameranın odağı ayağın ortası (`hiz.focus`): iki galaksi aynı karede, roket aralarında sürünür.
- Pozlama (`sideEv`): disk ışığının ekranı doldurduğu 18,5-20,5'te göz uyumu −2,5 EV; galaksiler arasında +2,5 EV.
- **2B katman** (`olcek_hiz_yan.*`, arayüz takımıyla; H ile gizlenmez, resmin parçası):
  - **Ortam noktaları:** durgun çerçevede sabit, 10^k m kafeslerinde, ekran ölçeğine en yakın iki kafes karışarak. Roketin
    yeri (çift-double) kafesin periyoduna göre kesin alınır: 10²² m'de de noktalar yerinde durur. Gözün kapağı 1/60 sn
    (yolcunun saati); bu sürede alınan yol γβc/60 kadar çizgiye çekilir. Çizgi ekranı aşınca hücrelerin noktaları sabit
    sıralara toplanır, sıralar kararlı ışık izleri olur: hiçbir hızda kareden kareye titreme yok.
  - **Yol:** iki durak arası ince ışıklı çizgi; duraklar ekrandaysa halka, ad ve uzaklık; değilse ortadaki boş alanda
    okla. Uzak uçlar izdüşümden önce yol üstünde yakına alınır (10¹⁶ m'deki bir noktaya 30 m'den bakınca 10⁻¹⁶'lık eksen
    hatası metre olur).
  - **Roket:** 14 m'lik foton roketi. Cam kubbe ve altında oturan, yüzsüz bir yolcu, arkada parabolik ayna, odağında
    ışık kaynağı. Hareket yönünde Dünya'nın ölçtüğü gibi γ kat kısa. Frende döner (ışık öne bakmalı), 0,9 sn'lik bir yan
    dönüşle çizilir. Uzakta bir nokta.
  - **Motor ışığı:** aynanın genişliğinde yumuşak bir altın şerit; parlaklığı ivmenin logaritması (1 g'de sönük, tavanda
    tam). Bir harita öğesi: ışığın gittiği yer.
  - **Hız yazısı:** v, β ve "senin bir saniyende X" (γβc · 1 sn: 21 dokuzda ~700 ışık yılı).
- **Hızda yıldızlar:** kamera roketle senin bir saniyende γβc yol alır (21 dokuzda 709 ışık yılı). Ölçek motorunun
  yıldız ve galaksi noktaları, gözün kapağında aldıkları piksel yoluna bölünmüş ışıkla çizilir (`hiz.shutterMove`).
  Işık çizgi boyunca dağılır; uzun çizgi çeken yakın yıldızlar söner, kısa çekenler yumuşak kayar. Kareden kareye
  sıçrayıp titreyen yıldız yok.
- **21 dokuzda** (düzenek `olcek/hizyuksek --set hiz.test.phi=24.5237`):
  - roket 2,2 × 10¹⁰ kat kısa, ince bir çizgi;
  - ortam noktaları kararlı ışık izleri;
  - frende roket dönmüş, ışığı öne bakıyor;
  - Samanyolu ölçeğinde galaksi ve roket.
  Düzenek, 21 dokuzda süzülürken saniyeler içinde Proxima'yı binlerce ışık yılı geçiyor: iki durak da arkada kalınca
  etiketleri alt alta dizilir.
- Uygulama: `Overlay.hud` (sahnenin 2B resmi her zaman, grafikler yalnız arayüzle). Arayüz takımına dolu dışbükey çokgen
  (`ui::fill`).
- Otomatik test yeniden yazıldı. `olcek/hiz`: yandan duruşta ve itişte, 2-6 durakları, göz, Boşluk, bir dakika sonra.
  `olcek/hizvaris --set hiz.test.varis=1`: yalnız test koşusunda Proxima'ya bir dakika kala %1 c'de fren eğrisinden
  başlar; vardı (1:00), 153° döndü.

### NE DOĞRULANMADI (H2 1. kısım)
- **Özcan'ın gözü:** yandan haritanın hiçbir karesine bakmadı.
- **Kenar durumları:** frendeki dönüş çizimi ve 21 dokuzdaki görünüm (ortam çizgileri, γ kat kısa roket) karede
  görülmedi; yalnız %3,7 c'ye kadar.
- **F12:** sahnenin kendi resmi (F12) 2B katmanı içermez; ekranın görüntüsü (otomatik test) içerir.
- **Pozlama ayarları** (−2,5 / +2,5 EV ve 32° eğim) gözle seçildi.
- **Ölçek'in birinci seçeneği** bu taşta yeniden ölçülmedi. Dokunulan her yol, `hiz.on` kapalıyken yapı gereği aynı
  (×1,0, kimlik, yalnız Hız dalı); kareler bit bit karşılaştırılmadı.

## 2026-10-05 gece · H1: grafikler

**Özcan'ın bakacağı yer.** Hız'da beş grafik panosu resmin üstünde, iki görüntüde de. Panelde **Grafikler: Açık |
Kapalı** (ya da **G**).
- Sol üstte HIZ ve YOL, sağda UZAY-ZAMAN ve SAATLER, altta BEDEL.
- Panolar uygulamanın panelleri arasındaki boşluğa yerleşir: sağ üstte ölçümler, sol altta sahne paneli, sağ altta
  tuşlar. Sığmazlarsa birlikte küçülürler. 2560×1440 için çizildiler.

**Panolar** (`src/scenes/olcek/olcek_hiz_pano.h/.cpp`):
1. **HIZ:**
   - Işık duvarına dayanan hız çubuğu.
   - γ(β) eğrisi ve üstünde nokta (logaritmik; ekseni kendini büyütür).
   - Dokuz sayısı başlıkta.
2. **UZAY-ZAMAN** (400×400, Özcan'ın referansı `assets/olcek/referans/uzay-kisalmasi.png`):
   - Dünya'nın eksenleri: x (uzay) ve t (zaman).
   - Roketin 5×5'lik ızgarası, kenarları x′ ve t′ boyunca. Hızlandıkça ikisi de ışık çizgisine doğru yatar (eksenden
     atan β kadar) ve ızgara bir çizgiye döner.
   - Yalnız bu katlanma: uzak zaman ve uzay senin için bükülüp kısalır. Sayıları öteki panolar veriyor.
3. **YOL:**
   - İki cetvel: Dünya'nın ölçtüğü ve senin ölçtüğün (γ kat kısa).
   - Durma mesafesi: altın köşeli çizgi ve yazı.
   - Sıkışan uzay ızgarası.
4. **SAATLER:**
   - İki kadran. Dünya'nınki γ kat hızlı; ibreleri gözün kapağı açıkken taradığı yelpaze, bir tam turda disk.
   - "Dünya'da şimdi (senin için)": t − β(r·m)/c.
   - "Gördüğün Dünya": t − d/c, Doppler çarpanıyla.
5. **BEDEL** (1 tonluk roket):
   - Hareket enerjisi.
   - Foton roketinin yaktığı, "durmak için de yakarsa" ve kütle oranı (durmakla).
   - İvme.
   - Önden gelen ışıma: öne bakan kara plakaya düşen fon ışıması (kapalı biçim).
   - Öndeki fon ışımasının sıcaklığı, rengi ve bandı.
   - Her hidrojen atomunun enerjisi.
   - Dipnot: düşünce deneyi.

**Altyapı:**
- **Sahne sözleşmesine `overlay()`.** Sahne resmin üstüne arayüz takımıyla çizer: uygulamanın panellerinin altında, H ile
  gizlenir. Uygulamanın panel kutuları da (`o.panels`) gelir; grafikler onlardan uzak durur.
- **Yazı yedeği.** Mono'da ve Serif'te olmayan harf (γ, β), aynı kalınlıktaki Sans'tan çizilir. Ekranda "?" çıkmıyor.
- **Kayan denetim tıklama almaz (çift tıklama tuzağı).**
  - Ölçek'te "Hız"a çift tıklama, yerine gelen "Otomatik varış"a basıp yolculuğu başlatıyordu.
  - 2560×1440'ta Ölçek'teki "Hız"ın ortası (244, 1226), Hız'daki "Otomatik varış"ın (161-385 × 1210-1263) içinde.
  - Duran imlecin altına kendiliğinden gelen denetim, yarım saniye geçene ya da fare kıpırdayana kadar tıklama almıyor.
    Bütün sahneler için geçerli. Otomatik test artık "Hız"a çift tıklıyor.
  - Bulunuşu: tam ekran otomatik testin birinde roket kalkış karesinde itiyordu, yani otomatik varış Boşluk'tan önce
    açıktı. Sonra Boşluk onu kapattı; Proxima'ya varılmadı.
  - İkinci tıklamanın kaynağı bulunamadı. Otomatik test o koşuda tek tıklama yaptı; en olası açıklama, bilgisayarın
    başında yapılmış gerçek bir tıklama.
- **Sayı yazımı tek kaynak:** panelin ve panoların sayıları `olcek_hiz_pano.cpp`'den.
  - 1'in altı üç anlamlı basamak.
  - 0,01'in altı 10'un kuvveti.
  - Tam sıfır "0".

**Revize (Özcan'ın ilk bakışı, aynı gece):**
- *"Uzay zaman grafiği [...] Sağda kocaman. Orada bir eğri çizmeye gerek yok. [...] ızgara bükülüp çizgileşiyor. O sade
  his yeterli."* Minkowski diyagramı (Dünya'nın ızgarası, dünya çizgileri, ışık konisi, noktalar) söküldü; yerine
  referanstaki gibi eksenler ve katlanan ızgara. Küçüldü: 520×560'tan 400×400'e.
- *"Kaç tane dokuz olduğunun sayısına bence gerek yok [...] sayısal detay olarak yazıyor zaten."* Dokuz merdiveni
  söküldü.
- Dünya çizgisinin izi (`Trace`) ve diyagramın ölçeği kodun her yerinden çıktı.
- Görüntü üzerine söyledikleri (hız hissi, ivme, yandan bakış) bir sonraki taşın konusu; konuşuluyor.

### Ölçümler
- **`--test olcek-hiz`, hepsi GEÇTİ.** Yeni üç sınama var.
  - BEDEL, 21 dokuzda plandaki sayılar:
    - hareket enerjisi Güneş'in 1,458 saati;
    - foton roketi 2,917 saat, durmakla 14,88 milyon yıl;
    - kütle oranı 4,472 × 10¹⁰ (durmakla 2,000 × 10²¹);
    - ivme 12,23 milyon g;
    - H atomu 2,10 × 10¹⁹ eV;
    - önde fon 1,22 × 10¹¹ K.
  - Önden gelen ışıma Güneş ışığının 17 dokuzda 6,13 × 10⁸ katı, 21 dokuzda 6,13 × 10¹² katı (plan: 6 × 10⁸ ve
    6 × 10¹²).
  - Plaka formülü başka bir yoldan sınandı. Ön yarıküre eksi arka yarıküre, izotrop ışımanın gerilim-enerji tensörünün
    dönüşümüne eşit olmalı: (16/3)γ²βσT₀⁴.
    - En büyük fark 1,7 × 10⁻¹¹.
    - Seriden kapalı biçime geçişteki sıçrama 8,3 × 10⁻⁶.
- **Canlı, tam ekran 2560×1440** (`--autotest-only olcek/hiz`):
  - 160-162 fps (ekran 165 Hz), kare 5,7-6,4 ms;
  - çift tıklamadan sonra Hız 0 m/s;
  - Proxima'ya "senin saatinle 1:39, Dünya'da 4,25 yıl";
  - 153° dönüş.
- **Panoların değerleri elle hesapla tuttu:**
  - γ 33'te önden gelen ışıma 1,34 × 10⁻⁵ (ekranda 1,33 × 10⁻⁵); fon 180 K, Wien tepesi 16 µm, kızılötesi.
  - γ 111'de durma mesafesi 82,5 milyon km (ekranda 82,6).
  - Proxima'da Dünya'da şimdi 3 Ocak 2031. Görülen Dünya kalkış günü, çünkü ışık 4,25 yıl gecikmeli.
- Kareler: `captures/arayuz/olcek_hiz_*.png` (git dışı).

### NE DOĞRULANMADI
- **Özcan'ın gözü:** panolar onun gönderdiği görüntülerin diliyle yan yana konmadı. Renk, yoğunluk ve yerleşim onayını
  bekliyor.
- **Küçük pencere:** 1280×720'de panolar sığmak için küçülüyor; yazılar ~5 piksel, okunmuyor. Küçük pencerede pano
  azaltmak ya da büyütmek düşünülmedi.
- **Çift tıklama düzeltmesinin testi** kırmızıya döndürülerek sınanmadı: düzeltme kapatılıp test koşulmadı. Kanıt,
  hatanın görüldüğü koşu ve düğmelerin ölçülen yerleri.
- **Öteki sahneler:** yazı yedeği ve tıklama koruması bütün sahneleri etkiliyor, ama onların otomatik testleri yeniden
  koşulmadı. Yazıda yalnız eksik harfler değişir (önceden "?"). Tıklamada yalnız duran imlecin altına kayan denetim
  bekler.
- **Minkowski diyagramı:** çok dokuzda roketin ızgarası neredeyse görünmüyor; sıkıştıkça seyreltiliyor. Bu doğru mu,
  yoksa daha görünür mü olmalı: Özcan'ın gözü karar verecek.
- **Bildirimler** (alt orta), sahne paneli genişlediğinde BEDEL'in sağ ucuna kısa bir süre değebiliyor.
- **Fon ışımasının renk lekesi:** 600 K'nin altında saydam (görünür ışık yok). Üstünde en parlak kanala göre
  normalleşiyor, bu yüzden 606 K'de koyu kırmızı değil parlak kırmızı. Leke parlaklığı değil yalnız renk tonunu
  gösteriyor.

## 2026-10-05 gece · H0: fizik ve iskelet

**Özcan'ın bakacağı yer.** Galeride 07 Ölçek → panelde **Seçenek: Ölçek | Hız** → Hız. Sahne her zaman Ölçek'te açılır;
yolculuk Hız'da kaldığı yerde bekler.
- **W** itiş (0,4 c/s, 12 milyon g), **S** fren (aynı güçle, geri vites yok), **Boşluk** otomatik varış.
- **Tab** roketin gözü ↔ yandan. Sürükle: başını çevir. Tekerlek: dürbün (75°'den 10⁻¹¹ radyana) ya da yandan ölçek
  (10 m - 10²³ m). **R** ileri bak / rokete dön. F1'de "Hız" tuş grubu var.
- Panel: yol, hız (m/s; çok dokuzda "c − x m/s"), β (bütün dokuzlarıyla), dokuz sayısı, γ, hız açısı φ, senin saatin,
  Dünya'da geçen, Dünya'da tarih, kalan (Dünya'nın ve senin ölçtüğün), durma mesafesi, görüş açısı / ekranın genişliği.
- "Yolculuğu baştan başlat" iki kez basınca.

**Fizik** (`src/scenes/olcek/olcek_hiz.h/.cpp`). Her şey hız açısı φ'den: β = tanh φ, γ = cosh φ, ve
1 − β = 2/(e^(2φ) + 1); 0,999...9'u double saklayamaz, bu biçim her φ'de basamak kaybetmez. İtişte, frende ve süzülmede
durgun çerçevenin zamanı ve yolu kapalı biçimlerle ilerler; adım hatası yok. Kare içindeki olaylar karede tam yerinde
bölünür: tavana varış, frene geçiş, duruş, dönüşün sonu. Konum ayağın hedefinden çift-double, 10²² m'de metrenin çok
altında kesin. Tavan 21 dokuz (φ = 24,5237).

**Rota:**
- Dünya'nın yanından (bugünkü yeri, yer eşzamanlı yarıçapta) Proxima'ya: Gaia DR3, astropy ile 5 Ekim 2026'ya taşınmış.
  1,301729 pc = **4,2457 ışık yılı**. Roket Proxima b'nin yörünge yarıçapında (0,0486 AU) durur.
- Proxima'da **153,4°** dönüş. 2. ayak Güneş'in **1,90 ışık yılı** yanından geçer, Andromeda'nın 100 bin ışık yılı önünde
  biter: **2,3951 milyon ışık yılı**.
- Kataloğun Proxima'sı (HYG, Hipparcos 2007) gerçeğinden 1.282 AU öndeydi: Gaia'nın yerine taşındı. Roketin durduğu yıldız
  gördüğü yıldız. Ölçek'te de artık orada.

**Roketin gözü** (H0'ın en basit görüntüsü: siyah üstünde yıldızlar):
- 109.400 HYG yıldızı ve Güneş, her biri durgun çerçevedeki yönüyle sapınca uğrar (tan(θ′/2) = e^(−φ) tan(θ/2)).
- Doppler D = γ(1 + β cos θ). Yıldız D·T sıcaklığında kara cisim olarak görünür; akısı D² ile çarpılır.
- Rengi ve görünür payı CIE 1931 2° **resmî tablosundan** (CIE 2019, CC BY-SA 4.0, `tools/olcek_veri.py cie`, sha256
  sabitli): log T üzerinde 10 K - 10¹² K tablo, üstünde Rayleigh-Jeans uzantısı.
- Yıldız dosyası artık sıcaklık taşıyor ("OLS2"; eski "OLST" okunur, sıcaklık renkten geri çözülür).
- Ana işlemcide, trigonometrisiz (yarım açının tanjantı), 64 parçada paralel ve sabit sırayla: her koşu aynı.

**Göz uyumu tasarlanmış:** görüş alanındaki görünür ışıktan (tüm gökyüzü öyle olsaydı), gece göğüne göre yarım uyum,
~1,2 sn'de. Görüntüden okunmaz. Tabanı `hiz.ev = 17,5` (dinginken ~7. kadir görünürlük sınırında), `hiz.uyum = 0,5`.
Karanlık gökte en çok 6 EV açılır.

**Yandan: ölçülen dünya, H0'dan.** Ölçek motoru roketin yanında. Kamera roketin tam yanından bakar: hareket ekranın
sağına, Galaksi'nin kuzeyi yukarı. Dünya roketin çerçevesinde ölçülür: hareket yönünde uzunluklar γ kat kısa, roketin
etrafında.
- Noktaların (yıldızlar, galaksiler, gezegen noktaları) yeri ana işlemcide kısaltılır, ışığı gerçek uzaklığından gelir.
- Işınla çizilenler (küreler, hava, galaksiler) çekirdekte ışın gerdirmesiyle aynı dönüşümü görür (`stretchX`).
- Roket bir nokta. Dünya'nın saatleri durgun çerçevenin zamanından; Galaksi'nin dönüşü tutulur (rota durgun çerçevede).
  Kalkıştan önce bekleyen roket Dünya'yla gider: dünya "bugün"den ilk itişte başlar.
- Sahil, çekirdek, kuşaklar, eve dönüş, tur kapalı.

**Kalıcılık:** proje kökünde `hiz_yolculuk.txt` (git dışı). Her 2 sn'de, varışta, kip değişince ve kapanırken yazılır.
Yazma geçici dosya + ad değiştirme; açılışta bir `.yedek`. NaN diske yazılmaz, sürüm satırı var. Otomatik test kullanıcının
yolculuğuna dokunmaz (`Host::testing`).

### Plandan değişenler (gerekçeleriyle)
1. **Otomatik varış her durumdan en kısa yoldan.** Plan "Space basılınca süzülür, d_dur'da frenler" diyordu; yavaşken
   basılınca yüzyıllarca süzülürdü (φ 0,01'de Proxima'ya ~425 yıl). Şimdi:
   - durabileceği en yüksek hız φ* daha yukarıdaysa hızlanır, tavanda süzülür, durma eğrisinde frenler;
   - zaten o kadar hızlıysan plandaki gibi yalnız süzülüp doğru anda frenler;
   - **W ya da S'ye basmak** kumandayı geri alır; basılı tutmak almaz (W basılıyken Boşluk'a basılabilir);
   - elle hedefin binde biri kadar yakınında durursan son kısmı otomatik varış uçar. Hedefe yalnız fren eğrisinin ucunda
     varılır: ışınlanma yok.
2. **Yandan görünüm H0'da ölçülen dünya.** Plan H0'da en basit hâlini, H2'de ölçülen dünyayı koyuyordu. Durgun
   çerçevede dünya karede c·sinh φ akar (tavanda karede 11,8 ışık yılı): yıldızlar bir karede ekranı geçer, pır pır.
   Ölçülen dünyada en çok c ile akar (planın "%99,9'da karede 5.000 km"si zaten roketin çerçevesiydi).
3. **Göz görüş alanına uyar, bütün gökyüzüne değil.** Kalkışta Güneş burundan 61° uzakta. Bütün göğe uysaydı yıldızlar
   kalkışta kaybolurdu; planın "kalkışta Andromeda soluk bir leke" isteği bunu gerektirir.
4. **Proxima bugünkü yerinde** (4,2457 ışık yılı; plan 2016'nın 4,2465'ini yazıyordu). Kodun ilk hâlinde yanlışlıkla
   SIMBAD'ın J2000 koordinatları vardı; veri denetimi buldu.
5. **Planın fizik metni düzeltildi** (bağımsız kâhin, mpmath 50 basamak):
   - §2.5 Doppler: 1/(γ(1 − β cos θ)) roketin açısıyla yazılır;
   - §2.8 fon ışıması büyük bir disk değil, gittikçe ısınan ve küçülen bir nokta (en çok 0,16°, 1.000 K);
   - §8 D² en çok 2 × 10²¹;
   - §6 Güneş 5.772 K;
   - §3.3 α Erboğa −6,8/−5,5 kadir, 12.060 AU; M31'in perspektifi;
   - §4.5 bedelde kinetik enerji 1,46 saat, foton roketinin yaktığı 2,9 saat.

### Bağımsız inceleme (4 inceleyici, her bulguya ayrı bir şüpheci) ve düzeltmeler
Hepsi düzeltildi:
- **Ölçek'e gerileme:** Planck köpüğü (s < −33,9) kabul edilmiş kareden 3,5 milyon pikselde farklıydı. Tek ifadede iki
  `r.u()` çağrısının sırası C++'ta belirsiz; dosya yeniden derlenince derleyici sırayı değiştirmişti. Üç ifade (köpük,
  çekirdeğin nükleonları, protonun denizi) ayrı satırlara kabul edilmiş karelerin sırasıyla sabitlendi. `readme2`'nin 8
  karesi bit bit aynı. Yer tutucu yıldız ve galaksilerin `v3(rng…)` argüman sıraları gerçek veri yüklüyken görünmüyor;
  sabitlenmedi.
- **Göz:**
  - Dürbün 10⁻⁸ radyanın altına inince uyum konisinin gök payı kosinüs yuvarlamasından 0 oluyordu; 0/0 NaN pozlama, göz
    kalıcı siyah (kayda da geçerdi). Koni artık açıdan ve sinüsten hesaplanıyor.
  - Uyum yakınlaştırmadan bağımsız: noktalar dürbünde parlamıyor, pozlama da değişmiyor (hep 75°'lik alana uyar).
    NaN asla tutulmaz.
- **Yandan:**
  - Hava, Dünya'dan uzakta da kuruluyordu (koşulum yanlıştı); float taşması yarım ekranı karartıyordu.
  - Uzak noktaların derinlik sınamasında |p|² taşıyordu; galaksiler küçük ölçekte kayboluyordu.
  - Top/nokta geçişi kısalmayı saymıyordu; ince şerit sabit örneklerin arasında yanıp sönüyordu.
  - Kuşaklar ilk itişte bir karede kayboluyordu: Hız'da kapalı.
- **Fizik ve panel:**
  - "Bu hızla durulamaz" bildirimi her karede tekrarlanıyordu: bir kez.
  - Dönüş bildirimi hep 153° diyordu (hedefi geçip dönünce gerçek dönüş 27°).
  - Hedefin yakınında S ya da Boşluk ile devralınca yakalama kumandayı geri alıyordu: artık el bir daha itene ya da bölge
    dışına çıkana kadar yakalanmaz.
  - "Dokuz −0,00".
  - "↔" yazı atlasında yoktu.
  - Baş açısı sarılmıyordu (R birkaç tur geri dönüyordu).
  - Bilimsel gösterimde "10,00 × 10⁵" yuvarlaması.
  - Otomatik testin Proxima kareleri dönüşten sonra çekiliyordu.

### Ölçümler (`--test olcek-hiz`, hepsi GEÇTİ)
- Kapalı biçimler / Runge-Kutta (20.000 adım): en büyük göreli fark 1,7e-13.
- 1 − β çift-double hesaba karşı ≤ 2e-16; tavanda 1,000e-21 (21,000000 dokuz).
- Kusursuz sürüş (formülden):
  - Proxima **88,98468 s / 4,24568 yıl**;
  - Andromeda **58,29701 dk / 2,395100 milyon yıl**.
- Otomatik varış, üç kare düzeninde (60 kare/s, 1/300-1/12 sn düzensiz, 0,5 sn): fark ≤ 1,8e-8 s, Dünya saati ≤ 1e-16.
  İki varış da tam hedefte. Fren eğrisine oturtmanın en büyük düzeltmesi ~7 km; o anda roket karede ~10¹⁷ m gidiyor.
- Bağımsız kâhin aynı sayıları buldu: 88,9856082479 s (2016 Proxima'sıyla), 3497,8204 s. Tavan 24,5237170667174523.
- Elle:
  - 46 sn W sonra Boşluk: geçer, durur, döner, tam hedefte durur;
  - 100 sn W'de φ tavanda kalır; S durur, geri gitmez;
  - bir kare W sonra Boşluk: 89,000 sn'de varır;
  - 20 AU kala elle durunca otomatik varış yakalar ve uçar, hiçbir adım hareketin izin verdiğinden uzun değil.
- Sapınç tersi 4e-16. D(θ) = D′(θ′) 7e-16. Trigonometrisiz biçim trigonometrikle 7e-16 (yön 3e-16 rad). Tam arkada girdinin
  1e-16'lık yuvarlaması e^φ kat büyür (koşulsuz nokta, fizikte hiçbir şey).
- Eşyönlü ışımanın enerji yoğunluğu γ²(1 + β²/3)u: 21 dokuzda dahil 1e-10'un altında.
- Planck eğrisi yayımlanan kromatikliklerle (1.000 K: 0,6528 / 0,3445). D³B(ν/D, T) = B(ν, DT): 9e-16.
  Çok sıcakta görünür pay T⁻³.
- Kayıt dosyası bit bit geri gelir, aynı sürer.
- Ölçek'in sınamaları değişmedi: `olcek-hassasiyet` en kötü 9,5e-5 piksel, `olcek-zaman` GEÇTİ, `olcek-sure` kabul
  edilmiş taramayla (`captures/olcek/sure_r2.txt`) sıçrama ve parçacık sayılarında birebir aynı, en uzun kare 15,78 ms
  (bütçe 16; referans 15,71). `readme2`'nin 8 karesi bit bit aynı.

### Kareler ve canlı yol
- Komut satırı: `--headless --hiz --phi 0,2,5,12,24.5237 [--ayak 1 --yer 0.5 --yan 12,16]` → `captures/olcek_hiz/`.
- Göz, yolun ortasında:
  - φ 2'de gökyüzü öne toplanır ve maviye kayar;
  - φ 5'te bütün gök ~20°'lik bir koniye girer;
  - φ 12'de bir nokta;
  - 21 dokuzda pencere kararır (yıldızların görünür ışığı 1/D ile söner); en parlak şey arkadaki Güneş'in öne gelmiş
    görüntüsü.
- Kalkışta hızlanırken Güneş sapınçla öne gelir (φ 2'de burundan 9°), maviye kayar ve gözü kamaştırır (EV 1-3). Güneş
  uzaklaşınca (~20-30 sn) yıldızlar görünür: fizik.
- Yandan, φ 5'te yıldız alanı hareket yönünde 74 kat basılıp dikey bir banda döner; 21 dokuzda bir çizgi.
- Canlı: `--autotest-only olcek/hiz` (pencerede). Hız'ı seçer, 2 × 5 sn W, Tab, tekerlek, sürükleme, R, Boşluk. Proxima'ya
  varır: "senin saatinle 1:38, Dünya'da 4,25 yıl geçti", "Roket Andromeda'ya dönüyor (153°)". Panelde Dünya'da tarih
  **3 Ocak 2031**. Kare 1,3-4,1 ms (pencerede 127-162 fps).

### NE DOĞRULANMADI
- **Özcan'ın gözü:** canlı pencerede bakılmadı; görüntüler komut satırı ve 1280×720 otomatik test kareleri.
- Roketin gözü H0'ın en basit hâli: Güneş ve Proxima yalnız nokta (gerçek diskleri H5). Samanyolu'nun yaygın ışığı,
  galaksiler ve fon ışıması yok (H3-H4). Kalkışta Andromeda "soluk leke" henüz yok.
- Yandan görünümde aşırı γ'da galaksi hacimleri soluyor. Kamera bir galaksinin genişliği kadar yakındayken basık
  galaksinin içinde kalıp "duvar" görüyordu. Ölçülen dünyanın çerçevelemesi H2'nin işi: ortografik bir harita olabilir.
  Noktalar duruyor.
- Pozlama tabanı (17,5) ve uyum payı (0,5) gözle seçildi, ölçülmedi.
- Bir yıldızın görünür akısı V kadirinden ve B−V sıcaklığından, kara cisim varsayımıyla; gerçek tayflar (çizgiler, bant
  farkı) yok.
- Durgun çerçevenin ihmal ettiği hızlar: Proxima'nın 32 km/s'si, M31'in 300 km/s'si (2. ayakta 2.400 ışık yılı = %0,1).
  Roket hedefte durgun çerçevede durur; yörüngeye oturma H5.
- Pencere küçültülünce kareler gelmez: o süre yolculuğa sayılmaz (kare başına en çok 2 sn). Özcan'a sorulacak.
