# Gösteri

## Açmak
`C:\dev\pc_graphics\build\pc_graphics.exe` — çift tıkla. Birincil monitörde kenarlıksız tam ekran **galeri** açılır:
solda beş sahne, sağda seçilenin resmi ve açıklaması.

- **↑ / ↓** (ya da **1-5**, ya da fare) seç, **Enter** (ya da **Sahneyi aç** düğmesi) aç.
- **A** (ya da **Ayrıntılar** düğmesi): resmin üstünde sahnenin nasıl yapıldığı, teknik başlıklarla; **Esc** kapatır.
- **L** (ya da sağ üstte **TR | EN**): uygulamanın dili, Türkçe ↔ İngilizce; seçim hatırlanır. İngilizce rehber: `DEMO.md`.
- Açılan sahne birkaç saniye yükleme ekranında hazırlanır (sahne 1'in programları ilk açılışta daha uzun derlenir).
- Her yerde: **Esc** geri (yardım → sahne → galeri → çıkış) · **F1** o sahnenin tuşları · **H** arayüzü gizle ·
  **F12** ekran görüntüsü (`captures/`) · **F5** ayarları ve programları yeniden yükle (sahne 1 ve 2).
- Sahne üstündeki arayüz: sol altta sahnenin adı ve durumu (ve varsa fareyle kullanılan denetimler: Yıkım'da
  yapı, top gücü, büyüklük, süre çubuğu, başlat / durdur; ilerleme çubuğu), sağ üstte hız ve süreler, sağ altta tuş ipuçları.

## 01 · Altın Saat — cam ve taş, tayfına ayrılan güneş
Karanlık odaya 5 yarıktan giren güneş, huzmelere asılı 14 kristal prizma, betona saçılan tayflar. **Durağan** başlar:
görüntü örnek biriktirdikçe netleşir (sol alttaki çubuk 8.192 örneğe ilerler; ondan sonra gürültü giderici çekilir,
ham görüntü kalır).

| Tuş | Ne yapar |
|---|---|
| **Boşluk** | durağan ↔ **canlı** (canlı: su dalgalanır, köpükler ve toz süzülür, ~22 fps) |
| **C** | sonraki kamera (gökkuşağı, prizma natürmortu, alçak geniş, orta, köşe, havuz, plan) |
| **↑ / ↓** | pozlama ±¼ durak · **← / →** odak uzaklığı · **R** birikimi sıfırla |
| **P / V / N** | fotonlar (kostikler) / pus / doğrudan ışık örneklemesi aç-kapa: ne neyi yapıyor, gör |
| **D** | gürültü giderici aç/kapa · **G** güneş lekesi bağlantısı |
| **1-5** | görünüm: güzel, albedo, normal, yalnız kostikler, maliyet ısı haritası |

**Dene:** **P**'ye bas: gökkuşağı, prizmanın tayfı ve su desenleri kaybolur (ayrı bir foton hesabından geliyorlar).
**D**'yi kapat, bir dakika bekle: gürültü giderici olmadan da aynı görüntüye yakınsıyor. **Boşluk** ile canlıya geç:
720p çizilir, yapay zekâ 1440p'ye büyütür; farkı prizmada görülür. **F12** PNG'nin yanına doğrusal EXR de yazar.
Ayarlar `scenes/altinsaat.cfg` (F5 ile anında): güneş, pus, beyaz dengesi, pozlama, kameralar, parlama.

## 02 · Fener — siste dönen bir huzme, gece ve orman
Inside tarzı deniz feneri. A "Pencere" bakışında açılır.

| Tuş | Ne yapar |
|---|---|
| **Boşluk** | A'dan B'ye orman içinden yürü (778 m, ~70 sn; yolda basarsan geri döner; sol altta ilerleme) |
| **B** | öbür bakışa atla · **P** zamanı durdur · **← / →** zaman ∓1 sn · **↑ / ↓** pozlama |
| **1-5** | görünüm: son, yalnız yüzey, yalnız hacim, derinlik, normal |
| **Tab** | oyuncu kipi (merak, dünya bunun için kurulmadı): 1. Tab kendi adanda A'da, 2. Tab fener adasında kulenin dibinde, 3. Tab sahneye dönüş |
| oyuncuyken | **WASD** yürü · **fare** bak · **Shift** koş · **Boşluk** zıpla; ağaçlara, kuleye, kulübeye çarparsın, derin suda yüzersin |

Ayarlar `scenes/fener.cfg` (F5 ile anında).

## 03 · Yıkım — betonarme yapılar, deprem ve kırılma
Panelin en üstünde **Yapı**: **Apartman** (5 kat, zemin katta dükkânlar), **Gökdelen** (30 kat, 97 m, beton çekirdek,
camlı lobi), **Ev** (tek kat, beşik çatı, baca), **Sismik** (26 kat ve iğne, taban izolasyonlu, cam cephe: 9'da bile ayakta). Seçince yapı yeniden kurulur ve kamera ona döner. Açılırken yapı iki
saniye görünmeden kendi ağırlığıyla oturur. Odak fizik.

| Tuş | Ne yapar |
|---|---|
| **3-9** | depremin büyüklüğü (sol alttaki panelde fareyle de tıklanır) |
| **← / →** | süre: kısalt / uzat. Panelde kaydırma çubuğu; çentik büyüklüğün kendi süresi (varsayılan), en sağ **∞**: durdurana kadar |
| **Boşluk** | depremi başlat / durdur (paneldeki düğme de); durdurunca zemin 1,5 sn'de diner |
| **sol tık** | tıkladığın yere bir **yıkım topu** gelir (döküm çelik), saniyede en çok bir. Kırılıp devrilmeye fizik karar verir |
| **↑ / ↓** | topun gücü (paneldeki **Top** çubuğu): sol uç hafif ve yavaş (0,5 t, kolonu kıpırdatmaz), çentik dengeli (4,2 t), sağ uç **göktaşı** (5 m çap, 520 t, saatte 396 km: tek atışta gökdeleni yıkar) |
| **sağ tık + fare** | bak · **WASD** uç (**W** baktığın yöne: yukarı bakınca yükselir) · **E / Q** yukarı / aşağı · **Shift** hızlı · **tekerlek** yakınlaş |
| **T** | ağır çekim (×1, ×0,25, ×0,1) · **P** zamanı durdur · **R** binayı baştan kur |
| **B** | donatı demirlerini göster / gizle · **G** gürültü giderici (hızlı süzgeç → yapay zekâ → ham) |

| Büyüklük | Apartman | Gökdelen | Ev |
|---|---|---|---|
| 3-5 | sallanır, çatlar; bir şey düşmez | sallanır | sallanır |
| 6 | balkon korkulukları ve zemin kat dolgusunun bir kısmı dökülür | kılcal çatlaklar | iki çatlak |
| 7 | ağır hasar: korkuluklar ve zemin kat dolgusu dökülür, çerçeve ayakta | lobi katı gider, kule bütün olarak devrilir | altı çatlak |
| 8 | yumuşak kat göçmesi: zemin kat gider, bina yan yatıp çöker | (ölçülmedi) | göçer |
| 9 | daha uzun ve sert sarsıntı, sonunda bina tamamen çöker | göçer | göçer |

**Dene:** gökdeleni 6 ile 7'de karşılaştır (6'da sallanır, 7'de devrilir; devrilişi **T** ile ağır çekimde izle); evin
çatısına top at; 6 ile 8'i apartmanda art arda (**R** ile baştan kurup) karşılaştır; 7'yi **∞** ile başlatıp uzun sarsıntıda hasarın nasıl
biriktiğini izle; zemin kat kolonlarına top at, gücü değiştirerek (ölçüldü: dengeli top köşe ve kenar kolonu kırar, en hafifi
kıpırdatmaz, göktaşı apartmanı da gökdeleni de tek atışta yıkar); çöküşü **T** ile ağır çekimde izle, kırık kolon uçlarına uçup demirlere yakından bak.
Ayarlar `scenes/yikim.cfg` (bina, malzemeler, çözücü, kamera, güneş).

## 04 · Laboratuvar — ağır çekim fotoğraf masası: su, bal, kum, su balonu, karpuz, jel, süt damlası, küp
Koyu set, arkada yumuşak ışık paneli. Panelin en üstünde **Deney** (ya da **1-8**). Her şey tanecik, fizik ne derse o.
**Hızlı anlar:** tetik kaba dünyada hemen oynar (sol üstte **Kaba hesap**); istersen **İnce hesap yap** (**I**) ile ince
dünya aynı girdileri arka planda hesaplar (**İnce hesap %…**, alttaki çubuk). **Zaman** çubuğuna tıklayınca hesaplanan
kısım hemen izlenir; bitince bir kez gerçek hızda oynar, sonra serbest inceleme. **R** ince kayıttan kaba hesaba döner
(kayıt zaman çubuğunda kalır). Video yok: saklanan fiziktir, her açıdan canlı çizilir.

| Deney | Ne yap |
|---|---|
| **Su** | **Yükseklik** çubuğu (12-45 cm; bırakınca beher yeniden kurulur), **Boşluk** ya da **Dök**: beher bardağa dökülür, yüksekten sıçrar ve taşar |
| **Bal** | canlı; **sol tıkla sürükle**: kaşık imleci izler · **Akış**, **Yükseklik** çubukları · **Boşluk** akıt / durdur |
| **Kum** | canlı; **sol tıkla sürükle**: huni imleci izler (behere doldur ya da masaya yığ) · **Akış**, **Yükseklik** çubukları · **Boşluk** dök / durdur |
| **Su balonu** | balona **sol tık** (tıkladığın yere iğne) ya da **Boşluk** (önden) |
| **Karpuz** | **Silah**: tabanca (karpuzu açar, kaideden düşürür) ya da tüfek (dağıtır) · **Mermi** çubuğu (tüfekte 5,56 → 7,62×51 → .50 BMG, gerçek yükler), karpuza **sol tık** (kameradan ateş) ya da **Boşluk** (yandan, ortadan) |
| **Süt damlası** | **Yükseklik** çubuğu (5-50 cm), **Boşluk** ya da **Damla bırak**: taçı ×1/100'de izle |
| **Jel** | **Mermi** çubuğu (tabanca: .22 → 9 mm → .50 AE), bloğa **sol tık** ya da **Boşluk** (yandan, boylamasına), **saniyede bir, istediğin kadar**: yol ve geçici boşluk saydam bloğun içinde; ince hesap bütün atışları sırayla yeniden hesaplar (aralarındaki 0,8 sn'den uzun bekleme 0,8 sn'ye kısalır) |
| **Küp** | **Küp sayısı** çubuğu (64 → 1 milyon: 4³ … 100³; bırakınca yeniden kurulur), **Şiddet** çubuğu (3 µg → 2 g TNT; altında yüzün ortasından çıkış hızı), **Boşluk**, **Patlat** ya da küpe **sol tık**. Ağır çekimde izle; 64³ ve üstü canlıda yavaş: **İnce hesap yap** ile hesapla (1 milyonda ~6 dk; bu arada kaba küpler bekler, kamerayı oynatmazsan daha çabuk), sonra gerçek hızda ve ×1/100'de izle |

| Tuş | Ne yapar |
|---|---|
| **T** | hız: ×1, ×1/10, ×1/100 (canlıda da, kayıtta da; panelde **Hız**). Canlıda ağır çekim aynı fiziktir, yalnız yavaş |
| **I** | ince hesap yap / durdur |
| **P** | durdur / oynat (kayıt bitmişse baştan oynatır) · **← / →** kayıtta 10 ms geri / ileri (**Shift** ile 100 ms) |
| **sağ tık + fare** | bak · **WASD** baktığın yöne git · **E / Q** yukarı / aşağı · **Shift** hızlı · **tekerlek** ileri / geri · **C** kamerayı deneye döndür |
| **R** | yeniden kur (ince kayıttayken kaba hesaba dön) |

**Dene:** balonu patlat, **İnce hesap yap**, kayıt bitince **Zaman**'ı başa çek, **×1/100** seç ve **P**: lastik 20-30 ms'de soyulurken su bir
an balon biçiminde durur. Suyu en yüksekten (45 cm) dök, sıçramayı ×1/10'da dondurup etrafında dolaş. Kumu bir yere
yığ (koni ~32°), sonra huniyi behere götür. Jele altı yedi kez farklı yerlerden ateş et. Karpuzu ×1/100'de izle: önce şişer, sonra kabuk açılır.
Ayarlar `scenes/laboratuvar.cfg` (F5 ile).

## 05 · Ada — bir adanın iskelesinden maviye: amforalar, bir kalyon batığı, derine inen halat
İskelenin beton başlığının altında, merdivenin dibinde başlarsın: karanlık bir salon, kabuk bağlamış sütunlar, kırık
deliklerden inen güneş. Dubaların çizdiği yoldan açığa (adanın tersine) yüz: sığ kum, ~12 m'de amforalar, ~22 m'de
batık, en uçta şamandıra ve dibe inen halatı. Yüzeye çıkılmaz; ppO2 1,6 bar'ı (~66 m) aşarsan görüş daralır, kararır,
merdivende yeniden başlarsın.

| Tuş | Ne yapar |
|---|---|
| **W A S D** | yüz (baktığın yöne) · **Boşluk** yukarı · **C** aşağı · **Shift** güçlü palet vuruşu |
| **Tab** | fareyle bak (aç/kapa) · **sağ tık** basılı tutup bak |
| **F** | dalgıç feneri (batığın içi, salonun karanlığı; yakında gerçek renkler: kızıl yelpaze mercanları, turuncu süngerler) |
| **P** | zamanı durdur (durunca örnekler birikir) · **K** kostik aç/kapa · **E** kenar pikselleri · **↑ / ↓** pozlama |

Panel (sağ üst): derinlik, basınç, oksijenin kısmi basıncı (ppO2), geçişlerin süreleri. **Dene:** başlığın altında
deliklerin altına git, ışığın kumda salınışını izle; batığın sancak tarafındaki kırıktan içeri gir, feneri aç; şamandıra
halatını izleyerek in, 35-40 m'de dibin belirişine bak. Ayarlar `scenes/koy.cfg` (F5 ile).

## 06 · Heykel — karanlık bir odada, kaidede kesilmiş beyaz bir Menger süngeri; on bin kat yakına
Geometrinin sahnesi: ayrıntı dokuda değil gerçek geometride ve yaklaştıkça bitmiyor. Kıpırdamadığın sürece görüntü
birikir ve netleşir; sağ üstte yakınlaşma ve ekranın o an kaç milimetre genişliğinde olduğu.

| Tuş | Ne yapar |
|---|---|
| **Tekerlek** | imlecin gösterdiği yere yaklaş, uzaklaş (o nokta ekranda yerinde kalır; 10.000 kata kadar, düz kesik yüzlerde 30) |
| **T** | dalış turu: dört noktaya 10.000 kata kadar, her katta durup netleşir (bir tuş ya da fare durdurur) |
| **Sol tık** | ışığı tıklanan yere çevir · **sürükle:** ışığı o noktanın çevresinde döndür |
| **Sağ tık** | sürükle: heykeli çevir · **R** ilk görünüm |
| **F** | netlik alanı: hepsi net (varsayılan) / makro · **↑ / ↓** pozlama |

**Dene:** önce T ile turu izle. Sonra bir yıldızın kenarına tekerlekle in ve her onda bir katta dur; ışığı yatık açıya
sürükle. Ayarlar `scenes/heykel.cfg` (F5 ile).
