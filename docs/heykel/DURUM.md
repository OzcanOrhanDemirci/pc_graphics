# DURUM · sahne 6 "Heykel" (geometri)

> Tek kaynak durum panosu. Plan `docs/heykel/PLAN.md`. En yeni en üstte.

## 🔁 Revize 2 · Tek dünya: her kare aynı (2026-10-05 öğleden sonra) · Özcan'ın bakacağı yer

**Özcan:** *"netlik oldu gerçekten. Şuan tek sorun ışığın hesaplanırken kıpraşması [...] rtxin olayı nedir, ışık oradadır
[...] Şuanki hissiyat sen yakınlaştın ben bi ayarlıyım kendimi dur diyormuş gibi [...] Yakınlaştırıp uzaklaştırırken
kötü, sabit durunca güzel."*

**Sebep (üç parça):** (1) hareket ederken kare başına 1-4 örnek, durunca 128'e kadar birikim: görüntü durduktan sonra
"kendini ayarlıyordu"; (2) yakınlaşma bitince lamba yeniden hedefleniyordu, yakınlaşırken yolu kapanınca bir karede başka
yöne atlıyordu (kıpırtı); (3) hareket ederken her kare ağır, durunca iş bitince kart boşta: fps iniş çıkışı.

**Yapılanlar:**
- **Her kare aynı:** `heykelFrame`: piksel başına 4 sabit örnek (`frame.base`), birikim yok; durunca görüntü değişmez,
  kart yalnız bir şey değiştiğinde çizer. Kenara aynı karede ek örnek (`frame.edge`) denendi: Menger'de neredeyse her yer
  kenar, kareyi 2-3 kat uzatıyor (warp'ta farklı sayıda örnek de verimsiz): kapalı.
- **Işık atlamaz:** yakınlaşırken lambanın hedefi yaklaştığın noktaya yumuşakça kayar (`followLight`, `light.follow` 4/sn),
  uzaklığı, boyu ve ışık havuzu bakışla birlikte; yolu kapanınca yeni yön aranır ama lamba oraya bir karede değil yumuşakça
  döner (`turnLampTowardsGoal`). Yakınlaşma sonundaki yeniden hedefleme kalktı. (Bu olmadan hedef havada kalıyordu, derin
  görünümler düz gri oldu: autotest karesinde görüldü, düzeltildi.)
- **Hız:** çapa hesabı yalnız gereken düzeylerde: son 10 düzey düz float (`deMengerPert`); derinde örnek 13,2 → 6,5 ms,
  hata 10.000 katta 0,017 piksel (`--test pert`). Yörüngeler sabit bellekte (az kazanç). Aşırı gevşetilmiş yürüyüş
  (Keinert) denendi: kazanç yok, geri alındı.

**Ölçümler (2560×1440):** kare 13-36 ms (yüzeyde ~15, derinde 20-36; tek tük bakışta 50), yani hareket ederken 28-65 fps.
Kıpırtı (`--kararlilik`, canlı kare = pencerenin karesi): yakınlaşma 0,0137 / %99 0,11, ışık sürükleme **0,0017** / 0,03.
Durunca değişen bir şey yok (birikim yok), dolayısıyla "ayarlanma" yok.

**NE DOĞRULANMADI:** canlı bakışın; lambanın yumuşak dönüşünün hızı (4/sn) gözde doğru mu; 4 sabit örnekle kenar
kalitesinin dururken eski 128 örnekli görüntüden ne kadar geride kaldığı (FLIP 0,036). 50 ms'ye çıkan bakışlar (uzun
galeriler boyunca) 20 fps.

## 🔁 Revize 1 · Yıldız, deterministik çizici (2026-10-05 öğlen)

**Özcan'ın ilk canlı bakışı (Katedral):** *"genel görüntü kalitesi çok kötü. Işık hareket edince pır pır titriyor,
yakınlaşınca görüntü çok tanecikli ve bulanık [...] Ada da 1 er örnekle bile bunu başarmıştın"*; sonra *"dokunun kendisi
yani geometrinin kendisi çok noktacıklı [...] matematiksel sonsuz diyince ben vektörlde olduğu gibi aşırı net bir şey
beklemiştim"*; *"en net ve temiz olup incelenebilecek olana geç"*.

**Yapılanlar:**
- **Rastgele sayı yok** (Ada'nın yolu): yol izleyici kalktı. Her piksel her karede aynı sabit örnekleri alır (R4 dizisi:
  alt piksel konumu ve spotun diskindeki nokta, Blackman-Harris 1,5 piksel); hareket ederken ~25 ms'ye sığan kadar (1-4),
  durunca aynı desenden 128'e kadar eklenir: kenarlar ve spotun yarı gölgesi olgunlaşır, gren hiç olmaz. Spotun gölgesi
  ışın boyunca uzaklıklardan tahmin (ışık sürüklenince kenar piksel piksel atlamaz). Netlik alanı varsayılan kapalı.
- **Heykel artık Yıldız** (Menger süngeri, köşegeninden ortadan kesilmiş, kesik yüzü izleyiciye dönük, altıgenin düz
  kenarı üstünde duran bir madalyon). Katedral'in noktacıkları geometrinin kendisiydi: yüzeyi her ölçekte piksel boyu
  dairelerden oluşur, tekrar azaltmak onu köpüğe dağıtıyordu (kareyle denendi). Menger'in yüzleri düz, kenarları keskin.
- **Nokta nokta piksel kuralı:** her noktada yalnız en az **4 piksel** büyüklüğündeki delikler çizilir (`lod.pixels`);
  daha küçükleri gren olurdu. Yaklaştıkça bir sonraki düzeyin delikleri **gerçek kare tüneller olarak merkezlerinden
  genişleyerek** açılır (iki düzeyin mesafesini karıştırmak yıldızları üçgene çeviriyor, delikleri çukurla dolduruyordu;
  çözünürlük değişince bile biçim değişiyordu).
- **10.000 kat Menger'de de pertürbasyonla:** katlamalar (mutlak değer, üç sıralama takası, ölçek, z adımı) parça parça
  doğrusal; her dalın fark formülü kesin, dal kararları çift hassasiyetteki farklardan (`MStep`). `--test pert`: 10.000
  katta en fazla **0,0006 piksel** (düz float 0,24).
- **Işık:** ilk görünümde spot soldan yatık (yıldızlar beyaz, deliklerin içinde gölge), kenarı yumuşak (sert kenar
  kaidede halka çiziyordu); pozlama ilk görünümde +0,4 EV, 10 kattan itibaren eski düzey.
- **Tur:** yeni 16 rastgele dalıştan seçilmiş dört nokta (yıldızlar baştan sona · bir köşe ve galeriler · yıldız, üçgen,
  yıldız · yıldızlardan tünellere); duraklar 2,5-4,5 sn (gren yok, beklenecek şey yalnız kenarlar).

**Ölçümler (2560×1440, `--kararlilik`, `tools/kararlilik.py`, piksel kıpırtısı ort / %99):**

| | Yakınlaşma (50 → 70 kat, kare başı %2) | Işık sürükleme (1.000 kat) |
|---|---|---|
| Katedral, eski çizici (yol izleme) | 0,0588 / 0,37 | 0,0125 / 0,26 |
| Katedral, deterministik | 0,0588 / 0,37 | 0,0022 / 0,05 |
| **Yıldız, deterministik, 4 piksel** | **0,0137 / 0,11** | **0,0027 / 0,06** |

Bağıl kıpırtı 0,15 (Fener 0,45-1,24, Ada 0,01-0,02). Hareket hâlinde 4 yerine 7 örnek ölçüyü değiştirmedi (0,0136): kalan,
hızlı yakınlaşmada geometrinin kendisinin akması. Bir örnek tam ekranda 2-14 ms (Katedral'in 3-10 katı hızlı); uygulama
hareket ederken 30-60 fps, durunca 162.

**NE DOĞRULANMADI:** Yıldız'a senin canlı bakışın; hızlı yakınlaşmadaki kalan kıpırtının gözde nasıl durduğu; Katedral
ve diğer adaylar artık yalnız komut satırında (`heykel = yildiz`, `scenes/heykel.cfg`).

## Önceki hâl: otonom görevin sonu (Katedral, 2026-10-05 sabahı)

Özcan G0 taslağına bakmadan seçimi bana bıraktı ve sahneyi tamamlanana kadar otonom sürdürmemi istedi (*"Hangisini en
iyi yapabileceksen seç. Sahneyi tamamlayana kadar otonom şekilde devam et [...] Paralel agent kullanma, kendin çalış"*).
Seçimim **Katedral** (sözde Klein): ilk görünümü gerçek bir müze fotoğrafı gibi, yakında her katta yeni daire dantelleri.

**Açmak:** `build\pc_graphics.exe` → galeride **06 Heykel** (Enter). Bir iki saniyede açılır.

**Nasıl bakılır:**
- **Dur ve bekle:** kıpırdamadığın sürece görüntü birikir ve netleşir (sağ üstte "Örnek"). Hareket ederken gördüğün
  yarım çözünürlüklü, yumuşak bir önizleme; parlaklığı birikmiş görüntüyle ölçülerek eşlendi, durunca sıçramaz.
- **Tekerlek:** imlecin gösterdiği yere yaklaşır (o nokta ekranda yerinde kalır), **10.000 kata kadar.** Sağ üstte
  yakınlaşma ve **ekranın genişliği** (ilk görünümde ~1,5 m, 10.000 katta 0,15 mm). Bloğun düz kesik yüzlerinde ve
  kaidede derinlik yok (orada yalnız düzlem kalır): oralarda 30 katta durur.
- **T ya da panelde "Dalış turu":** dört seçilmiş noktaya 10.000 kata kadar iner, 10, 100, 1.000 ve 10.000 katta durup
  netleşir, çıkar, sonrakine geçer. Bir tuş ya da fare durdurur.
- **Sol tık:** ışık tıkladığın yere döner · **sol tıkla sürükle:** ışık o noktanın çevresinde döner (yatık ışıkta
  dantel okunur) · **sağ tıkla sürükle:** heykeli çevir · **R:** ilk görünüm · **F** ya da panel: netlik alanı
  (makro / hepsi net) · **↑ / ↓:** pozlama · **H:** arayüzü gizle.

**Ölçümler (2560×1440, RTX 4080):**
- **Uygulama** (`--autotest`, altı sahne birden: hepsi yüklendi, çıkış 0, her sahne kapanınca boş kart belleği aynı
  düzeye döndü, 14,8-15,0 GB). **Akıcılık** (`--autotest-only heykel`): 28-44 fps; hareket ederken önizleme 5-33 ms (derinde ışın sayısı
  kendini düşürür). Kart belleği 2,0 GB.
- **Durunca netleşme** (komut satırı, 2560×1440, iki dalış; yüzde: 1.024 örnekli referanstan 8 gri seviyesinden fazla
  ayrılan piksel):

  | | 64 örnek | 128 örnek | 256 örnek |
  |---|---|---|---|
  | İlk görünüm | 0,5 sn · %1 | 0,9 sn · %0,3 | 1,7 sn · %0,05 |
  | 1.000 kat | 6-9 sn · %4-11 | 10-17 sn · %1-3 | 15-33 sn · %0,2-0,6 |
  | 10.000 kat | 8-9 sn · %6-11 | 14-16 sn · %2-4 | 23-29 sn · %0,3-0,7 |

  Derinde bir örnek tam ekranda ~190-225 ms (uyarlamalı örneklemede sonrakiler ucuzlar). Tur bu yüzden 1.000 ve 10.000
  katta 12 saniyeye kadar bekler.
- **Sayısal derinlik** (`--test pert`): 10.000 katta en fazla 0,003 piksel; düz float 0,24-0,49.

### Görev boyunca verdiğim kararlar (her biri bir ölçümle ya da kareye bakarak)
- **10.000 kat, pertürbasyonla.** Orada bir piksel konumun birkaç float basamağı: düz float'ta hata 0,24-0,49 piksel,
  yüzey eşiği yarım piksel. Derin Mandelbrot yakınlaşmalarının yolu: çapa noktasının yörüngesi ana işlemcide çift
  hassasiyetle (`setAnchor`), her piksel ondan float bir fark olarak izlenir. Katlama ve evirme parça parça rasyonel:
  her dalın kesin bir fark formülü var, dal sınırları çift hassasiyetteki uzaklıklardan (`heykel_de.h`, `KStep`). Ölçü
  (`--test pert`, 12 çapa, 2.000+ nokta): 10.000 katta en fazla 0,003 piksel. 1.000 kattan önce düz float (fark görünmez,
  pertürbasyon 2-3 kat pahalı).
- **Tekrar sayısı yakınlaşmayla artar** (`itersNow`: 10'dan başlar, onda bir katta 2 artar, 18'de durur). 10 tekrarda
  10.000 kattaki dantel boşlukta yüzen halka bulutuna dağıldı (formül bitmişti); 14-18 tekrarda dantel sürüyor. Piksel
  kuralının formüle uygulanmış hâli: pikselden ince yapı zaten çizilmiyor.
- **Katlar formülle ayrı tasarlanamadı.** Belli bir tekrardan sonra kutu ya da evirme ayarını değiştirmek (`split`, `k2`,
  `a2`) derin katlarda yalnız ince fark verdi. Plandaki geri çekilme yolu: katların karakteri **yer seçimi** (16 rastgele
  dalıştan 4'ü, her durakta dengeli olanlar) ve **ışıkla** taşınıyor. Kod duruyor, kullanılmıyor (`split` varsayılan 99).
- **Dalışlar dünya noktası:** ilk sürüm pikseldi; 2560×1440'ta yüzey eşiği daha ince olduğu için yol başka bir deliğe
  sapıp yoğun kafesin içine daldı. Şimdi her dalış, 10.000 kata inilen noktanın çift hassasiyetli konumu
  (`--hedef` turun gördüğünü çizer).
- **Işık üç parça:** (1) **spot:** tıkladığın yere döner; yaklaşırken kamerayla birlikte gelir (makro fotoğrafçının
  lambası gibi, aynı noktaya göre ölçeklenir), hattını bir kemer kapatırsa en yakın açık açıyı arar (15-60°, her karede
  tek ışınla kontrol). Önceki hâlinde derin katların çoğu kararıyordu. (2) **göz ışığı:** merceğin üstünde bir halka ışık;
  kameranın gördüğü her nokta onu da görür, gölge testi gerekmez, gürültüsüz, önizlemeyle birebir. Uzaklıkla düşüşü
  merceğe yakın yumuşatıldı (**sanat ayarı, adıyla**: `eye.soft`); düz ters kare yasasında yakın bir kemer görüntünün
  %4,6'sını beyaza patlatıyordu. Kameranın arkasına konmuş ilk sürümü, arkadaki kemerlerin gölgesine giriyordu. (3)
  tavandan sönük bir dolgu (oda).
- **Önizleme:** yarım çözünürlük, önizleme pikseli başına 4 ışın (döndürülmüş ızgara): tek ışınlı tam çözünürlük kadar
  pahalı ama süzülmüş (ince dantel tek ışında kıpraşıyordu). Yavaşlarsa (derinde) ışın sayısı 2'ye, 1'e iner. Parlaklık
  birikmiş görüntüyle ölçülerek eşlendi (oran 0,93-1,07, 16 piksellik bloklarda ortalama fark 4-10 gri seviyesi).
- **Birikim:** düşük tutarsızlıklı diziler (Kronecker, asal kökler), Gauss yeniden kurma (σ 0,45 piksel), uyarlamalı
  örnekleme (32 örnekten sonra ortalaması %1,2 kesinleşen piksel durur), yüzeyde 2, 100 kattan derinde 1 sekme
  (sekme yolun yarısı kadar pahalı, katkısı az), adım çarpanı 0,9 (0,8'e göre %15 hızlı, 10.000 katta delik ya da benek
  yok). Bir örnek bir kareye sığmazsa satır satır kareler arasına bölünür: ekran donmaz.
- **Makro alan derinliği** (yol izlemede ince mercek, açıklık odak uzaklığının %0,3'ü; açısal bulanıklık her yakınlıkta
  aynı): 100 katta öndeki dantel perdesi yumuşayıp ön plan olarak okunuyor. İlk görünümde etkisi yok. F kapatır.
- **Ölçek:** plan "10.000 katta ekranın genişliği bir saç teli (~70 µm)" diyordu; ilk görünüm heykeli kaideyle ve payla
  çerçevelediği için (genişlik ~1,5 m) 10.000 katta ölçülen 0,15 mm, iki saç teli. Özcan'ın sayısı (10.000 kat) korundu.
- **Diğer dört aday çıktı:** tek dar odak; Katedral dışındakiler float'la 1.000 kata, tur ve ışık ayarı yalnız onda.
  Komut satırı etüdünde duruyorlar (`--aday 0..4`).

### Bilerek yapılmayanlar
- **Porselenin ince duvardan ışık geçirmesi:** bu fraktalın duvarlarının gerçek bir kalınlığı yok (sözde Klein'ın
  yüzeyleri sıfır kalınlıkta disklerin görüntüleri; görünen kalınlığı piksel kuralı veriyor). Geçirgenlik yakınlaşmaya
  göre değişen, fiziksel olmayan bir şey olurdu.
- **Son oda** (dalışın sonunda tasarlanmış bir öge): 10.000 kattaki dantelin kendisi varış; eklenen bir nesne o anı
  ucuzlatırdı.
- **Yapay zekâ gürültü gidericisi:** ince geometriyi yumuşatma riski bu sahnenin amacına ters; birikim yeterince hızlı.

### NE DOĞRULANMADI
- **Senin gözün:** hiçbir şey senin canlı bakışından geçmedi. Işığın, sanat ayarlarının (göz ışığı, alan derinliği),
  turun hızı ve duraklarının yargısı karelere benim bakmamla.
- **Gerçek fareyle kullanım:** yakınlaşma, ışık tıklama ve sürükleme, çevirme yalnız otomatik sınamanın sanal faresiyle.
  Sürükleme hızları tahmin.
- **Hareket hâlindeki kıpraşma ölçülmedi:** yakınlaşırken görülen önizleme süzülmüş ama tek kare başına (zamansal birikim
  yok); kararlılık ölçüsü (`tools/kararlilik.py`) bu sahneye bağlanmadı. Durağan görüntü ölçüldü (yukarıda).
- **10.000 katın ötesi:** yakınlaşma orada durur; ötesi denenmedi.
- **Çok yoğun kafes bakışları:** kameranın hemen önünde ince bir dantel tabakası varken görüntü gri bir örtüye döner
  (gerçek geometri, cepheden gelen göz ışığıyla düz); turun durakları bunlardan kaçınıyor, serbest gezintide çıkabilir.

## G0 · Etüt taslağı (2026-10-05 gecesi; Özcan bakmadan seçimi bana bıraktı, aşağıdaki görev)

**Açmak:** `build\pc_graphics.exe` → galeride **06 Heykel** (Enter). Yükleme bir iki saniye. Sol alttaki panelde **Aday**
düğmeleri (ya da 1-5 tuşları): Şehir · Katedral · Yıldız · Tohum · Dantel.

**Senden istenen:** taslak olarak bak ve sanat kararını ver. Hangi aday (ya da adaylar) "geometrik olarak şok" yolunda,
hangisi değil; genel görünüm (malzeme, ışık, kaide, karanlık oda) doğru yönde mi. Ayrıntı (keskinlik, 10.000 kat,
katların tasarımı, malzemenin ışık geçirmesi) bilerek yok; o işler senin seçtiğin aday üstünde G1-G3'te.

**Nasıl bakılır:**
- **Dur ve bekle:** kıpırdamadığın sürece görüntü birikir: ilk görünümde 1-3 saniyede, yakında birkaç saniyede netleşir
  (sağ üstte "Örnek" sayısı artar).
  Hareket ederken görüntü daha kaba bir önizlemedir: yargıyı durunca ver.
- **Tekerlek:** imlecin gösterdiği yere yaklaşır (o nokta ekranda yerinde kalır). Bu taslakta en fazla 1.000 kat.
  En ilginç yerler 10 ve 100 kat civarında.
- **Sol tık:** ışık tıkladığın yere döner (ölçeği o anki bakışa göre). **Sol tıkla sürükle:** ışık o noktanın çevresinde
  döner; yatık ışıkta kabartma okunur.
- **Sağ tıkla sürükle:** heykeli (bakışı, en son yaklaştığın noktanın çevresinde) çevirir.
- **R:** ilk görünüm · **↑ / ↓:** pozlama · **H:** arayüzü gizler.

**Adaylar (her biri farklı bir karakter):**
| | Aday | Fraktal | Uzaktan | Yaklaşınca |
|---|---|---|---|---|
| 1 | **Şehir** | Mandelbox, ölçek 3 | yüzleri bina bloklarına oyulmuş bir küp | sonu gelmeyen bloklar, avlular, kuleler |
| 2 | **Katedral** | sözde Klein (Knighty), bir bloğa kesilmiş | kemer sıraları ve kum saati sütunlar | daire daire dantel, iç içe kubbe ağları |
| 3 | **Yıldız** | Menger süngeri, üst köşesi merkezden kesilmiş | kesitte altı köşeli yıldızlar | kare boşluklu sonsuz bir yapı, galeriler |
| 4 | **Tohum** | Mandelbulb (üs 8), eğik | karnabahar gibi organik bir kütle | çiçek gibi ışınsal halkalar, tomurcuklar |
| 5 | **Dantel** | Apollonius küre paketi, bir küreye kesilmiş | tabaklar, küreler, delikler | küre küre içe doğru sonsuz delikler |

**Ölçümler (2560×1440, RTX 4080, autotest):** hareket ederken önizleme 1-16 ms (aday ve yakınlığa göre), uygulama
28-44 fps. Durunca yol izleme örneği 3-13 ms (ilk görünüm) / 40-72 ms (10 kat yakında; böyle bir örnek birkaç kareye
satır satır bölünür, ekran donmaz). 1.024 örnekte durur. Kart belleği 2,0 GB.

### Bu taslakta ne yapıldı
- Yeni sahne `src/scenes/heykel/`: fraktallar mesafe kestirimiyle (`heykel_de.h`, ekran kartı ve ana işlemci aynı kod:
  imlecin altını seçmek ve heykeli kaideye oturtmak da onunla), CUDA çekirdekleri (`heykel_kernels.cu`): önizleme (doğrudan
  ışık, tahmini yumuşak gölge, kapalılık; gürültüsüz) ve durunca biriken yol izleme (alan ışığının gerçek gölgesi, üç
  sekme, piksel altı örnekler); önizlemeden birikime ilk 6 örnekte yumuşak geçiş; AgX ton, sabit titreşim.
- **Piksel kuralı** baştan: yüzey eşiği pikselin o uzaklıktaki izinin yarısı (`detail = 0,5`), normal aynı ölçekte.
- **Oturtma:** her aday yüklenirken uzaktan merkeze 1.500 ışınla dış yüzeyi bulunur: yarıçap, yükseklik, alt nokta ve
  kütle ortası; heykel 42 cm boyuna ölçeklenir, ortası kaidenin ortasına gelir, altı yüksekliğin %1-8'i kadar düz kesilir
  (kaideye oturur).
- **Oda:** koyu, ufuk çizgisi görünmeyen bir boşluk; dolgu ışığı yalnız yukarıdan (yumuşak tavan ışığı gibi). Tek spot:
  boyutu, uzaklığı ve aydınlattığı daire o anki bakışa göre (yakında da aynı ölçekte görünür).
- Ayarlar `scenes/heykel.cfg` (F5); her adayın her düğmesi `aday<i>.<düğme>` ile ezilir (etüt bunlarla yapıldı).
- Komut satırı: `pc_graphics.exe --scene heykel --headless --aday all --zoom 1,10,100 --spp 128 --w 1280 --h 720`
  adayların ilk görünümünü ve yakınlaşma basamaklarını PNG'ye yazar (`captures/heykel_etut/`).
- Uygulamaya genel ek: `--autotest` betiğinde **tekerlek** adımı (`wheel(n)`); Heykel'in sınaması yakınlaşma, ışık
  tıklama ve sürükleme, çevirme ve beş adayı dolaşır.

### Etütte elenenler (benim kararım, kareye bakarak)
- **Mandelbox ölçek 2 ve −1,5, Julia biçimleri:** dışarıdan düz bir küp (güzelliği içte). Küreye ya da kutuya kesilmiş
  hâli ("Oyma"): çoğu yüzü düz bir top. Ölçek 3 kaldı (Şehir): yüzleri gerçekten oyulmuş.
- **Sierpinski dörtyüzlüsü** (dönmeli ve dönmesiz): ders kitabı görünümü, yakında çarpık; zayıf.
- **Dönmeli Menger** ("Kristal"): kırık tuğla yığını. Dönmesiz Menger'in köşegen kesiti (Yıldız) çok daha güçlü.
- Sözde Klein'ın ince ipliksi ayarları (boşlukta asılı iplikler) ve tek sütun hâli: blok hâli (Katedral) en güçlüsü.

### NE DOĞRULANMADI
- **Senin gözün:** hiçbir aday senin canlı bakışından geçmedi; kararlar karelere benim bakmamla.
- **Fareyle gerçek kullanım:** yakınlaşma, ışık ve çevirme yalnız autotest'in sanal faresiyle denendi. Sürükleme hızları
  (ışık 0,003, çevirme 0,0025 rad/piksel) tahmin; elde fazla ya da az gelebilir.
- **Kıpraşma ölçülmedi:** taslakta kenar yumuşatma yalnız birikimden gelir; hareket sırasındaki önizleme tek örneklidir
  (kenarlar tırtıklı, ince ayrıntı kıpraşır). Netlik işi G2'de.
- **1.000 katın ötesi** denenmedi; tek hassasiyetin nerede bozulduğu ölçülmedi.
- Bazı yakın bakışlarda (Tohum, 10 kat) 1.024 örnekte bile hafif gren kalabilir; ölçülmedi.
