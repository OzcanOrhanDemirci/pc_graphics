# Gösteri

*İngilizcesi: [DEMO.md](DEMO.md)*

## Açmak
`build\pc_graphics.exe` (deponun içinden) — çift tıkla. Birincil monitörde kenarlıksız tam ekran **galeri** açılır:
solda on üç sahnenin numaralı listesi, sağda seçilenin resmi ve kısa açıklaması. Ada, Ölçek, Orman, Ateş ve Dağ
depoda olmayan açık veriler okur; onları açmadan önce README'deki "Derleme" bölümündeki betikleri bir kez çalıştır.

- **↑ / ↓** (ya da **W / S**, ya da fareyle tıkla) seç, **Enter** (ya da **Sahneyi aç** düğmesi) aç. İlk dokuz sahnenin
  sayı tuşu var (**1-9**), onuncunun **0**; 11-13'e oklarla ya da fareyle gidilir.
- **A** (ya da **Ayrıntılar** düğmesi): sahnenin nasıl yapıldığı, resmin üstünde, başlık başlık; **A** ya da **Esc** kapatır.
- **L** (ya da sağ üstte **TR | EN**): uygulamanın dili, Türkçe ↔ İngilizce; seçim hatırlanır. Komut satırında
  `--lang en` ya da `--lang tr` de olur.
- Açılan sahne bir yükleme ekranının arkasında hazırlanır: GPU programları derlenir, dünyası kurulur. **Esc** yüklemeyi
  bırakır. Denizaltı ilk açılışta ışığını pişirir, bu birkaç dakika sürer; sonraki açılışlar önbellekten.
- Her sahnede: **Esc** geri (yardım → sahne → galeri; galeride çıkış) · **F1** o sahnenin tuşları · **H** arayüzü gizle ·
  **F12** ekran görüntüsü (`captures/`) · **F5** ayarları ve GPU programlarını yeniden yükle (Yıkım dışında her sahnede).
- Sahne üstündeki arayüz: sol altta sahnenin adı, durumu ve varsa fareyle kullanılan denetimleri (Yıkım'da yapı, top,
  büyüklük, süre; Laboratuvar'da deney ve zaman çubuğu; birkaç sahnede örnek sayısı), sağ üstte kare hızı ve süreler,
  sağ altta **F1**, **H** ve **Esc** ipuçları.

## 01 · Altın Saat — cam ve taş, tayfına ayrılan güneş
Beton bir oda, beş dar yarıktan giren alçak güneş, huzmelere asılı on dört kristal prizma ve betona saçılan tayflar.
**Durağan** başlar: görüntü örnek biriktikçe netleşir. Gürültü giderici 8.192 örneğe kadar çalışır, sonra çekilir ve
ham görüntü kalır.

| Tuş | Ne yapar |
|---|---|
| *Kip ve kamera* | |
| **Boşluk** | durağan ↔ **canlı** kip (canlıda su dalgalanır, toz ve köpükler süzülür) |
| **C** | sonraki kamera (gökkuşağı, prizma natürmortu, alçak, orta, köşe, havuz, plan) |
| **← / →** | odağı yaklaştır / uzaklaştır |
| **↑ / ↓** | pozlamayı artır / azalt (¼ durak) |
| **R** | birikimi baştan başlat |
| *Katmanlar* | |
| **P** | fotonları aç / kapat (kostik) |
| **V** | pusu aç / kapat |
| **N** | doğrudan ışık örneklemesini aç / kapat |
| **G** | güneşli yüzeyleri ışık say (aç / kapat) |
| **D** | gürültü gidericiyi aç / kapat |
| *Görünüm* | |
| **1** | son görüntü |
| **2 / 3** | albedo / normaller |
| **4** | yalnız kostikler |
| **5** | maliyet ısı haritası |
| **6** | ham görüntü, gürültü giderilmeden |

**Dene:** **P**'ye bas: gökkuşakları, prizmanın tayfı ve su desenleri kaybolur; hepsi ayrı bir foton hesabından gelir.
**D** ile gürültü gidericiyi kapat, bir dakika bekle: onsuz da aynı görüntüye varılır. **Boşluk** ile canlıya geç:
1280×720 çizilir, yapay zekâ 2560×1440'a büyütür, saniyede yaklaşık 24 kare; farkı en çok prizmada görürsün. **F12**
burada PNG'nin yanına doğrusal bir EXR de yazar. Ayarlar `scenes/altinsaat.cfg`'de (güneş, pus, beyaz dengesi,
pozlama, parlama; **F5** ile anında).

## 02 · Fener — gece sisinde dönen bir fener huzmesi
Playdead'in *Inside*'ı tarzında bir gece: sisli denizde bir fener döner, huzmeleri sisi yarar, kara köknarlar ve tek
sıcak pencere onlara karşı kararır. "Pencere" bakışında (A) açılır; öbürü "Yücelik" (B).

| Tuş | Ne yapar |
|---|---|
| *Sahne* | |
| **Boşluk** | A ↔ B arasında ormanın içinden yürü (778 m, ~70 sn); yoldayken basarsan geri döner |
| **B** | öbür bakışa atla |
| **P** | zamanı durdur / sürdür |
| **← / →** | zamanı 1 sn geri / ileri al |
| **↑ / ↓** | pozlama ±0,25 EV |
| **1-5** | son görüntü, yüzey, sis, derinlik, normal |
| *Oyuncu* | |
| **Tab** | sahne → oyuncu (kendi adanda) → fener adası (kulenin dibinde) → sahne |
| **W A S D** | yürü |
| **Fare** | etrafa bak |
| **Shift** | basılı tutup koş |
| **Boşluk** | zıpla |

Oyuncu kipi bir merak: dünya onun için kurulmadı, ama ağaçlara, kuleye ve kulübeye çarparsın, derin suda yüzersin.
**Dene:** **Boşluk** ile ormana gir: göz karanlığa 1,5 saniyede yarı yarıya uyar, orman karanlık kalır ama okunur. **2** ve
**3** ile yüzeyi ve sisi ayrı ayrı gör. Ayarlar `scenes/fener.cfg`'de (**F5** ile anında).

## 03 · Yıkım — betonarme yapılar, deprem ve kırılma
Panelin en üstünde **Yapı**: **Apartman** (5 kat, zemin katta dükkânlar), **Gökdelen** (30 kat, 97 m, camlı lobi),
**Ev** (tek kat, beşik çatı, baca), **Sismik** (26 kat ve çelik iğne, ~123 m, kauçuk izolatörlerin üstünde: sallanır,
süzülür ama 9'da bile ayakta kalır). Seçince yapı yeniden kurulur ve kamera ona döner. Yapı yüklenirken kendi
ağırlığıyla oturur. Odak fizik.

| Tuş | Ne yapar |
|---|---|
| *Deprem* | |
| **3-9** | büyüklüğü seç (paneldeki **Büyüklük**'te fareyle de) |
| **← / →** | süreyi kısalt / uzat; panelde **Süre** çubuğu, çentik büyüklüğün kendi süresi, en sağ ucu **∞**: durdurana kadar |
| **Boşluk** | depremi başlat / durdur (paneldeki düğme de); durdurunca zemin 1,5 sn'de diner |
| **T** | ağır çekim: ×1, ×0,25, ×0,1 |
| **P** | zamanı durdur / sürdür |
| *Top ve bina* | |
| **Sol tık** | tıkladığın yere döküm çelik bir yıkım topu at (saniyede en çok bir); neyin kırılıp devrileceğine fizik karar verir |
| **↑ / ↓** | topun gücü (paneldeki **Top** çubuğu): sol uç 0,5 t ve saatte 29 km, çentik dengeli 4,2 t, sağ uç bilerek abartılmış bir **göktaşı** (5 m çap, 514 t, saatte 396 km) |
| **R** | binayı yeniden kur |
| *Kamera* | |
| **Sağ tık** + **Fare** | basılı tutup etrafa bak |
| **W A S D** | uç; **W** baktığın yöne gider (yukarı bakınca yükselirsin) |
| **E / Q** | yüksel / alçal |
| **Shift** | basılı tutunca hızlı |
| **Tekerlek** | yakınlaş / uzaklaş |
| *Görüntü* | |
| **B** | donatıyı göster / gizle |
| **G** | gürültü giderme: kendi süzgecimiz → yapay zekâ → ham görüntü |

| Büyüklük | Apartman | Gökdelen | Ev |
|---|---|---|---|
| 3-5 | sallanır, çatlar; bir şey düşmez | sallanır | sallanır |
| 6 | balkon korkulukları ve zemin kat dolgusunun bir kısmı dökülür | kılcal çatlaklar | iki çatlak |
| 7 | ağır hasar: korkuluklar ve zemin kat dolgusu dökülür, çerçeve ayakta | lobi katı gider, kule bütün olarak devrilir | altı çatlak |
| 8 | yumuşak kat göçmesi: zemin kat gider, bina yan yatıp çöker | (ölçülmedi) | göçer |
| 9 | daha uzun ve sert sarsıntı, sonunda bina tamamen çöker | göçer | göçer |

**Dene:** gökdeleni 6 ile 7'de karşılaştır: 6'da sallanır, 7'de devrilir; devrilişi **T** ile ağır çekimde izle. Evin
çatısına top at. Apartmanda 6 ile 8'i art arda dene (arada **R** ile baştan kur). 7'yi **∞** ile başlat, uzun
sarsıntıda hasarın nasıl biriktiğini izle. Zemin kat kolonlarına gücü değiştirerek top at: dengeli top köşe ve kenar
kolonunu kırar, en hafifi kıpırdatmaz, göktaşı apartmanı da gökdeleni de tek atışta yıkar. Bir çöküşten sonra kırık
kolon uçlarına uç, **B** ile demirlere yakından bak. Ayarlar `scenes/yikim.cfg`'de (bina, malzemeler, çözücü, kamera,
güneş); bu sahnede **F5** yok, ayarlar sahne yeniden açılınca okunur.

## 04 · Laboratuvar — ağır çekimde sıvı, kum ve kırılan cisimler
Ağır çekim fotoğraf masası: koyu bir set, arkada yumuşak bir ışık paneli. Panelin en üstünde **Deney** (ya da **1-8**).
Her şey tanecik, fizik ne derse o. Tetik önce kaba dünyada hemen oynar (panelde **Kaba hesap** yazar). İstersen
**İnce hesap yap** (**I**) aynı girdileri arka planda ince bir dünyada yeniden hesaplar (**İnce hesap %…** ve alttaki
çubuk). **Zaman** çubuğuna tıklayınca hesaplanan kısım hemen izlenir; bitince kayıt bir kez gerçek hızda oynar, sonra
serbestçe incelenir. Video yok: saklanan fiziktir, her açıdan canlı çizilir.

| Deney | Ne yap |
|---|---|
| **Su** | **Yükseklik** çubuğu (12-45 cm; bırakınca beher yeniden kurulur), **Boşluk** ya da **Dök**: beher bardağa dökülür, yüksekten sıçrar ve taşar |
| **Bal** | canlı; **sol tıkla sürükle**: kaşık imleci izler · **Akış** ve **Yükseklik** çubukları · **Boşluk** ya da **Akıt / Durdur** |
| **Kum** | canlı; **sol tıkla sürükle**: huni imleci izler (behere doldur ya da masaya yığ) · **Akış** ve **Yükseklik** çubukları · **Boşluk** ya da **Dök / Durdur** |
| **Su balonu** | balona **sol tık** (tıkladığın yere iğne) ya da **Boşluk** / **İğne batır** (önden) |
| **Karpuz** | **Silah**: tabanca (karpuzu açar, kaideden düşürür) ya da tüfek (dağıtır) · **Mermi** çubuğu (tüfekte 5,56 → 7,62×51 → .50 BMG, gerçek yükler) · karpuza **sol tık** (kameradan ateş) ya da **Boşluk** (yandan, ortadan) |
| **Jel** | **Mermi** çubuğu (tabanca: .22 → 9 mm → .50 AE) · bloğa **sol tık** ya da **Boşluk** (yandan, boylamasına), saniyede bir, istediğin kadar: yol ve geçici boşluk saydam bloğun içinde görünür. İnce hesap bütün atışları sırayla yeniden hesaplar (aralarındaki 0,8 sn'den uzun bekleme 0,8 sn'ye kısalır) |
| **Süt damlası** | **Yükseklik** çubuğu (5-50 cm), **Boşluk** ya da **Damla bırak**: tacı ×1/100'de izle |
| **Küp** | **Küp sayısı** çubuğu (64 → 1 milyon: 4³ … 100³; bırakınca yeniden kurulur), **Şiddet** çubuğu (3 µg → 2 g TNT; altında bir yüzün ortasının çıkış hızı), **Boşluk**, **Patlat** ya da küpe **sol tık**. 64³ ve üstü canlıda yavaş: **İnce hesap yap** ile hesapla (1 milyonda ~6 dk; bu arada kaba küpler bekler), sonra gerçek hızda ve ×1/100'de izle |

| Tuş | Ne yapar |
|---|---|
| *Deney* | |
| **1-8** | deneyi seç (paneldeki sırayla) |
| **Boşluk** | başlat: dök, ateş et, patlat |
| **Sol tık** | hedefe tıkla, sürükleyip akışı gezdir |
| **I** | ince hesabı başlat / durdur |
| **R** | yeniden kur ya da ince kayıttan çıkıp kaba hesaba dön (kayıt zaman çubuğunda kalır) |
| *Zaman* | |
| **T** | hız: ×1, ×1/10, ×1/100 (panelde **Hız**); canlıda ağır çekim aynı fiziktir, yalnız yavaş |
| **P** | durdur / oynat (bitmiş kayıt baştan oynar) |
| **← / →** | kayıtta 10 ms geri / ileri |
| **Shift** + **← / →** | kayıtta 100 ms geri / ileri |
| *Kamera* | |
| **Sağ tık** + **Fare** | basılı tutup bak |
| **W A S D** | hareket et |
| **Q / E** | aşağı / yukarı |
| **Tekerlek** | ileri / geri |
| **Shift** | basılı tutup hızlı git |
| **C** | kamerayı deneye döndür |

**Dene:** balonu patlat, **İnce hesap yap**; kayıt bitince **Zaman**'ı başa çek, **×1/100** seç ve **P**: lastik
20-30 ms'de soyulurken su bir an balon biçiminde durur. Suyu en yüksekten (45 cm) dök, sıçramayı ×1/10'da dondurup
etrafında dolaş. Kumu bir yere yığ (koni yaklaşık 32°), sonra huniyi behere götür. Jele altı yedi kez farklı yerlerden
ateş et. Karpuzu ×1/100'de izle: önce şişer, sonra kabuk açılır. Ayarlar `scenes/laboratuvar.cfg`'de (**F5** ile).

## 05 · Ada — bir adanın iskelesinden maviye
İskelenin beton başlığının altında, merdivenin dibinde başlarsın: karanlık bir salon, kabuk bağlamış sütunlar, kırık
deliklerden inen güneş. Dubaların çizdiği yoldan açığa, adanın tersine yüz: sığ kum, ~12 m'de amforalar, ~22 m'de
batık, en uçta şamandıra ve dibe inen halatı. Yüzeye çıkılmaz; oksijenin kısmi basıncı (ppO₂) 1,6 bar'ı (~66 m)
aşarsan görüş daralır, kararır, merdivende yeniden başlarsın.

| Tuş | Ne yapar |
|---|---|
| *Dalgıç* | |
| **W A S D** | yüz (baktığın yöne) |
| **Boşluk** | yukarı çık |
| **C** | aşağı in |
| **Shift** | basılı tutup güçlü palet vuruşu |
| **Tab** | fareyle bakışı aç / kapat |
| **Sağ tık** | basılı tutup bak |
| *Sahne* | |
| **P** | zamanı durdur / sürdür (durunca örnekler birikir) |
| **E** | kenar piksellerini göster / gizle |
| **K** | kostiği aç / kapat |
| **↑ / ↓** | pozlamayı artır / azalt |

Sağ üstte derinlik, basınç, ppO₂ ve geçişlerin süreleri; sol altta piksel ve kenar örneği ile doku kalitesi seçilir.
**Dene:** başlığın altında deliklerin altına git, ışığın kumda salınışını izle. Batığın çökmüş sancak tarafından içeri
gir. Şamandıra halatını izleyerek in: dip 35-40 m'de belirir. Ayarlar `scenes/koy.cfg`'de (**F5** ile).

## 06 · Heykel — kesilmiş bir Menger süngeri, on bin kat yakın
Karanlık bir odada, kaidenin üstünde ortasından kesilmiş beyaz bir küp; kesitte altı köşeli yıldızlar. Ayrıntı dokuda
değil gerçek geometride ve yaklaştıkça bitmiyor. Her kare aynı sabit örneklerle çizilir: hareket ederken gördüğün,
durunca gördüğünle aynıdır. Sağ üstte yakınlaşma ve ekranın o an kaç milimetre genişliğinde olduğu.

| Tuş | Ne yapar |
|---|---|
| *Bakış* | |
| **Tekerlek** | imlece doğru yaklaş, geri uzaklaş (o nokta ekranda yerinde kalır; 10.000 kata kadar, düz kesik yüzlerde ve kaidede 30) |
| **Sağ tık** | basılı sürükle: heykeli çevir |
| **R** | ilk görünüme dön |
| **T** | dalış turunu başlat / durdur: dört dalış, her biri 10, 100, 1.000 ve 10.000 katta durur (bir tuş ya da fare de durdurur) |
| *Işık* | |
| **Sol tık** | ışığı tıklanan yere çevir |
| **Sol tık** | sürükle: ışığı o noktanın çevresinde gezdir |
| **↑ / ↓** | pozlama ±0,25 EV |
| **F** | netlik alanı: makro / hepsi net |

**Dene:** önce **T** ile turu izle. Sonra bir yıldızın kenarına tekerlekle in, her onda bir katta dur; ışığı yatık bir
açıya sürükle. On bin katta ekran 0,15 mm genişliğindedir, yapı orada da sürer. Ayarlar `scenes/heykel.cfg`'de
(**F5** ile).

## 07 · Ölçek — bir sahilden evrenin kenarına ve Planck boyuna
Ölçeğin sahnesi: 62 onluk, yalnız tekerlekle, kesintisiz. Çandarlı'da (İzmir, Dikili) kalenin önündeki kumsaldan
başlarsın. Her şey 5 Ekim 2026 öğlesindeki gerçek yerinde ve gerçek boyunda, aradaki boşluklar da gerçek uzunluğunda.
Zaman her ölçekte kendi hızında akar (dışarı hızlanır, içeri yavaşlar) ama hiçbir şeyi değiştirmez: sahile hep aynı
öğlede dönersin. Sağ üstte bakılan cisim ve ekranın genişliği.

| Tuş | Ne yapar |
|---|---|
| *Ölçek* | |
| **Tekerlek** | uzaklaş / yaklaş (bir tık ×1,5; hızlı çevirince saniyede 20 onluğa kadar) |
| **R** | sahile dön |
| **T** | turu başlat / durdur: evrenin kenarına, sahile, Planck'a, sahile (saniyede yarım onluk; tekerlek de durdurur) |

**Dene:**
- Sahilden yavaşça uzaklaş: kalenin önündeki kumsal (Sentinel-2), Çandarlı Körfezi, Ege, o günün bulutlarıyla Dünya.
- Dünya'dan Güneş'e geç: gezegenler yörüngelerinde birer nokta, asteroit kuşağı, Jüpiter'in Truvalıları, Kuiper
  kuşağı. Samanyolu'nu tepeden gör, sonra Andromeda'yı. En dışta 2MRS'in gerçek galaksileri ve kozmik ağ.
- **R** ile sahile dön ve içeri in: kum taneleri, kristal basamakları, atomların peteği, kristalin yoğunluk haritası.
  Sonra atomun elektronları (yanından geçerler), boş atomun ortasında çekirdeğin 28 nükleonu, proton ve üç kuarkı.
- Daha da in: kuarkın çevresinde her ölçekte sanal çift halkaları. En dipte köpük baloncukları ve altın renkli bir
  sicim; ikisi de kuram.

### İkinci seçenek: Hız
Panelde **Seçenek: Hız**. Bir roketle Dünya'dan Proxima'ya ya da Andromeda'ya; özel görelilik tam formüllerle,
ışınlanma yok, zaman hep gerçek. Proxima senin saatinle 15 dakika (Dünya'da 4,25 yıl), Andromeda 72 dakika (2,4
milyon yıl). Hedefi panelden istediğin an seçersin (**Hedef: Proxima | Andromeda**, başta Proxima); roket durağansa
hemen ona döner. Geçip gitsen de seçili hedef hep oradadır.

| Tuş | Ne yapar |
|---|---|
| *Hız · uçuş* | |
| **W** | basılı tutup hızlan: 1 g'den başlar, hızlandıkça artar (bir dakikada ışık hızının %1'i, sekiz dakikada 21 dokuz) |
| **S** | basılı tutup frenle |
| **Boşluk** | seçili hedefe otomatik varışı aç / kapat: doğru anda frenleyip orada durur (başka yöne gidiyorsan önce durur, döner); **W** ya da **S** kumandayı geri alır |
| *Hız · görüntü* | |
| **Tab** | roketin gözü ↔ yandan bakış |
| **G** | grafikleri aç / kapat |
| **E** | enerji gözünü aç / kapat (açık başlar) |
| **Sol tık** | basılı tutup sürükle, etrafa bak |
| **Tekerlek** | gözde yakınlaş (dürbün), yandan ölçeği değiştir |
| **1-6** | yandan ölçek durakları: roket, Dünya ve Ay, Güneş Sistemi, yakın yıldızlar, Samanyolu, Yerel Grup |
| **R** | öne bak / rokete dön |

**Dene:**
- Kalkışta sürükleyip arkana bak: o günün bulutlarıyla Dünya. Sonra **Boşluk** ve izle; acele etme, yolculuğun
  uzunluğu uzaklığın kendisi.
- Hızlandıkça gök öne toplanır. Işık hızının yarısını geçince arkada bıraktığın Güneş sapınçla öne gelir: mavi-beyaz,
  göz kamaştıran bir yıldız. Beş dokuz civarında önde kırmızı bir güneş doğar: evrenin ilk ışığı, kozmik fon. **E**
  ile enerji gözünü kapatıp aç: ışık görünür banttan çıkınca gerçek göz kararır, enerji gözü onu gösterir.
- **Tab** ile yandan bak, **1-6** ile ölçeği değiştir.
- Proxima'da etrafına bak: lekeli kızıl cüce, Alfa Erboğa A ve B, gezegeni b ve arkada Kraliçe takımyıldızında Güneş.
  Sonra panelde **Hedef: Andromeda** ve **Boşluk**. Andromeda'ya frenle yaklaşırken öne bak.

Plan ve durum `docs/olcek/`'te, Hız'ınki `docs/olcek/hiz/`'de. Ayarlar `scenes/olcek.cfg`'de (**F5** ile).

## 08 · Denizaltı — gece kırmızısında bir kontrol odası
Tench sınıfı bir filo denizaltısının kontrol odası, üç kırmızı gece lambası ve harita masasının lambasıyla. Atmosferin
sahnesi: ışık, pus ve yansımalar önceden pişirilir, ekran kartı yalnız görüntüyü çizer. İlk açılışta ışık pişirilir
(yükleme ekranında ilerlemesiyle, yaklaşık 5 dakika); sonraki açılışlar önbellekten.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **W A S D** | yürü |
| **Shift** | basılı tutup yavaş yürü (yakından incelemek için) |
| **Boşluk** | zıpla |
| **C** | basılı tutup eğil |
| **Fare** | etrafa bak (tıklamadan) |
| **Sağ tık** | basılı tutup dürbünle bak |
| **Tab** | imleci panel için bırak / geri al |
| **Sol tık** | imleç serbestken sürükleyip bak |
| **R** | başa dön |
| *Görüntü* | |
| **↑ / ↓** | pozlamayı artır / azalt |

Panelde **Çözünürlük** (720p-8K), **SSAA** (1x-16x), **Tessellation**, **Grafik**, **Işık**, **Efekt** (Low, Medium,
High). **F12** burada paneldeki çözünürlük ve SSAA ile bir fotoğraf çeker (8K 16x dahil), "Fotoğraf hesaplanıyor"
kartıyla, 1920 × 1080'lik parçalar halinde. 8K 16x yalnız fotoğrafta çizilir; canlı görüntü o ayarda 8K 4x'tir.

**Dene:**
- Başlangıçta öne yürü: iskelede dalış istasyonu (çarklar, 16" derinlik göstergeleri), ortada harita lambasının beyaz
  havuzu.
- Harita masasına eğil: Ege seyir haritası, nöbetçinin kurşun kalemle çizdiği rota ve mevkileri, pusula gülü, paralel
  cetvel, pergel.
- Sancaktaki panolara **Shift** ile yaklaş: her döner şalterin altında devresinin adı, krom kalkanında OFF/ON çizgileri.
- Bir göstergeye dürbünle bak: kadranın yazısı, camında odanın yansıması.
- Tessellation'ı Low ile High arasında çevir; bir somunun dişine, mantar kaplamaya, oyma pirinç levhaya bak.
- SSAA'yı 1x ile 16x arasında çevir; 720p 16x'i 1440p 4x'le karşılaştır (aynı örnek sayısı).
- **F12** ile 8K 16x fotoğraf al ve dosyada yaklaş.

Plan ve durum `docs/denizalti/`'da. Ayarlar `scenes/denizalti.cfg`'de (**F5** ile).

## 09 · Ayna — ayna labirentinde yansımanın yansıması
Endülüs kemerli bir ayna labirentinde süzülen krom bir küresin; göz kürenin ortasında, kendini yalnız aynalarda
görürsün. Aynalar ölçülmüş değerlerle hesaplanır (6 mm float cam ve arkasında gümüş); yansıma, doğrudan bakış kadar
keskindir.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **Fare** | etrafa bak (tıklamadan) |
| **W A S D** | ileri, geri, yanlara |
| **Boşluk** | yüksel |
| **C** | alçal |
| **Shift** | basılı tutunca hızlı |
| **Tab** | imleci bırak / geri al |
| **1-4** | bakışlar: koridor, ayna, salon, zemin |
| **R** | başa dön |
| *Işık* | |
| **Q / E** | ışığın rengi: sıcak beyaz, gün ışığı, kırmızı, turuncu, yeşil, camgöbeği, mavi, mor |
| **Tekerlek** | ışığın gücü (8 kademe) |
| **F** | ışığı aç / kapat |
| **Sol tık** / **G** | önüne lamba bırak, o anki renk ve güçte (en çok 8) |
| **X** | lambaları topla |
| **↑ / ↓** | pozlama |

Ayar paneli yok: kalite 2560 × 1440 için ayarlı, saniyede 30-44 kare.

**Dene:**
- Başlangıçta koridor boyunca bak: kemerler, sütunlar ve fenerler aynalarda sonsuza gider.
- **2** ile aynaya dön: karşında küremiz ve yarığındaki ışık; iki paralel aynanın tünelinde kopyaları küçülerek
  kararır ve camgöbeği-yeşile döner (camın demirinden).
- Işığı bir aynaya yatık tut: yansıyan koni öbür duvara ve pusa düşer.
- Farklı renkte üç lamba bırakıp uzaklaş, sonra aynalara bak: her biri her yöne çoğalır.
- Bir kapı aralığından yavaşça geç: ışığın pervaza düşüşü yakından da keskin.

Plan, ölçümler ve durum `docs/ayna/`'da. Ayarlar `scenes/ayna.cfg`'de (**F5** ile).

## 10 · Kuyu — aynalı odalardan bir kafeste sonsuz düşüş
Görünmez bir gözlemcisin; iki oda sütunu arasındaki bir kuyudan saniyede 1,5 m düşersin. Her oda koyu bir cam küp:
içinde ışığı sonsuza dek dönen kapalı bir dünya. Odaların kenarları sonsuza uzanan iplik demetleri olarak sürer.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **Fare** | etrafa bak (tıklamadan) |
| **W A S D** | yatay kay (odalara ve demetlere çarparsın) |
| **Boşluk** | yüksel |
| **C** | alçal |
| **Q / E** | düşüşü yavaşlat / hızlandır (dur, 0,5, 1,5, 4, 10 m/s) |
| **Shift** | basılı tutup gittikçe hızlan: hız her 1,3 sn'de e katına çıkar, tavan 12.000 m/s; bırakınca birkaç saniyede normale döner |
| **Tab** | imleci bırak / geri al |
| *Bakış* | |
| **1-4** | kuyu, oda (önünde durursun), kafes, yukarı |
| **R** | başa dön |
| **↑ / ↓** | pozlama |

Ayar paneli yok: kalite 2560 × 1440 için ayarlı, saniyede 33-42 kare.

**Dene:**
- Başta dosdoğru aşağı bak: kuyunun kaçış noktası, kat kat geçen odalar.
- **2**: bir odanın camının önünde durursun. İçeride lambanın görüntüleri üç boyutlu bir kafeste sonsuza gider; derinde
  kehribara döner (ince gümüş kırmızıyı maviden çok yansıtır).
- **W** ile bir odanın camına yaklaş: dışarıdan cam yalnız %4 yansıtır, içerisi koyu camın ardında parlar.
- **Q** ile dur, fareyi yavaşça gezdir: uzaktaki odaların küçük ışıkları titremeden kayar.
- **E E** ile hızlan: odalar sessizce yukarı akar.
- Düşerken **Shift**'i basılı tut: önce yavaş, sonra giderek hızlanırsın. Belli bir hızın üstünde kafes iki kare
  arasında yarım hücreden fazla atlar: iskelet durur, titrer ya da geri gidiyor gibi görünür, odalar her karede başka
  (geri dönen araba tekerleği ilüzyonu).

Plan, ölçümler ve durum `docs/kuyu/`'da. Ayarlar `scenes/kuyu.cfg`'de (**F5** ile).

## 11 · Orman — yağmurdan sonra sabah, sık bir yağmur ormanı
Bir derenin kıyısında, her yanı bitkiyle dolu bir yağmur ormanında yürürsün: eğrelti, kök, yosun, taş, derenin
üstünde devrik bir gövde. Güneş yaprakların arasından süzülür, yerde pus durur. Her bitki ve taş açık lisanslı gerçek
bir taramadır. Orman bir duvarla bitmez: dümdüz yürürsen 396 m sonra dereye öbür yandan dönersin. İlk yükleme yarım
dakika kadar sürer.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **Fare** | etrafa bak (tıklamadan) |
| **W A S D** | yürü |
| **Shift** | basılı tutup koş |
| **Boşluk** | zıpla |
| **C** | basılı tutup eğil |
| **Tab** | imleci panel için bırak / geri al |
| **R** | başa dön |
| *Bakış* | |
| **1-4** | hazır bakışlar: dere boyunca ışığa, devrik gövde, yukarı yapraklara, yere yakın |
| **↑ / ↓** | pozlamayı artır / azalt |

Panelde **Örnek**: **DLSS** (varsayılan: karede piksel başına tek ışın, NVIDIA DLSS Ray Reconstruction kareleri
birleştirir) ya da kıyas için pikselde 1, 4, 8, 16 sabit ışın. Sol altta kopya ve üçgen sayısı.

**Dene:**
- **1**'de dere boyunca ışığa bak: pustaki güneş huzmeleri, ıslak taşlar.
- **2** ile devrik gövdeye git, **3** ile tepe örtüsüne bak: yapraklar kat kat göğe dek.
- **4**'te eğil, yere yakından bak: düşmüş yapraklar zeminin üstüne tek tek yatırılmış.
- Yürürken panelde **DLSS** ile **4** arasında geç: sabit örneklerde pikselden ince dallar kıpırdar, DLSS'te durur.
- Bir yöne dümdüz yürü: orman bitmez, bir süre sonra aynı dereye varırsın.

Plan, ölçümler ve durum `docs/orman/`'da. Ayarlar `scenes/orman.cfg`'de (**F5** ile).

## 12 · Ateş — gecenin karanlığında taş ocakta yanan ateş
Aysız bir gecede, açık havada taş bir ocakta yanan odun ateşi. Alevin biçimi, rengi, ışığı ve kıvılcımları elle
çizilmez, yanmanın fiziğinden gerçek zamanda hesaplanır: gaz akar, odun ısınıp ayrışır, kömür kor olur. Ateşin
ulaşmadığı gök tam siyahtır.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **Fare** | etrafa bak (tıklamadan) |
| **W A S D** | ocağın çevresinde yürü |
| **C** | basılı tutup çömel |
| **Tab** | imleci bırak / geri al |
| *Sahne* | |
| **1-4** | hazır bakışlar: ayakta, ocağın yanına çömelmiş, alçaktan göğe karşı, közlere tepeden |
| **R** | ateşi baştan yak |
| **↑ / ↓** | pozlamayı artır / azalt |

Sol altta ateşin ısı gücü, kalan odun ve kömür, en sıcak yüzeyin sıcaklığı.

**Dene:**
- **1**'de alevin ardındaki taşlara bak: sıcak hava ışığı kırar, taşlar titrer.
- **2** ile ocağın yanına çömel: alevin dibindeki mavi, kızgın közlerin üstünde.
- **3** ile alçaktan bak: gecenin önünde dipleri mavi alevler.
- **4** ile közlere bak: kütüklerin arasında kor, üstünü örten kül.
- Kıvılcımları izle: odunun içinde bir cep patlayınca fırlarlar, gaz onları yukarı taşır.
- **R** ile ateşi baştan yak, yeniden büyüyüşünü izle.

Plan, ölçümler ve durum `docs/ates/`'te. Ayarlar `scenes/ates.cfg`'de (**F5** ile).

## 13 · Dağ — Kaçkar'da gün batımı, ufka kadar gerçek yeryüzü
Kaçkar'ın 3.937 metrelik doruğunda, korkuluklu küçük bir terastasın. Önünde Karadeniz'e batan güneş, sağında
Kafkaslar, arkanda Ağrı. Gördüğün her tepe gerçek, en uzağı yaklaşık 390 km ötede. Açılış bakışında korkuluk açıktır:
oradan yürüyüp dağa inebilirsin.

| Tuş | Ne yapar |
|---|---|
| *Hareket* | |
| **Fare** | etrafa bak (tıklamadan) |
| **W A S D** | yürü |
| **Shift** | basılı tutup koş |
| **Alt** | basılı tutup on kat hızlı git |
| **Boşluk** | zıpla |
| **C** | basılı tutup eğil |
| **Tab** | imleci panel için bırak / geri al |
| **R** | başa dön |
| *Bakış* | |
| **Sağ tık** | basılı tutup dürbünle bak |
| **Sağ tık** + **Tekerlek** | dürbünün büyütmesi (3x-30x) |
| **1-5** | hazır bakış yönleri |
| **↑ / ↓** | pozlamayı artır / azalt |

Panelde **Örnek**: pikselde 4, 8, 16, 32 ya da 64 ışın (16'da saniyede yaklaşık 60 kare). Dürbünle bakarken panel
bakılan yerin adını ve uzaklığını yazar.

**Dene:**
- Açılışta güneş karşında: denizde güneş yolu, katman katman sırtlar. Önce dürbünle güneş yoluna bak: dalgalar çıkar.
- **2**, sonra dürbünle yön 20°: Elbruz, 299 km, iki tepesi ve karı. **3**, yön 114°: Ağrı, 295 km.
- **3** ile doğuya dön: Kaçkar'ın kendi gölgesi havada, ufka kadar bir üçgen.
- Korkuluğa dayan, aşağı bak: kaya, iri taşlar, altta vadi. Sonra korkuluğun açıklığından dağa in.

Plan, ölçümler ve durum `docs/dag/`'da. Ayarlar `scenes/dag.cfg`'de (**F5** ile).
