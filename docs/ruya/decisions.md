# Rüya kararları

## 1 · 2026-11-07 · seçim · Bir dünya dosyası yerine tekrar üretilebilir tarif

Her adanın yerleşimi koordinatından çıkar. On altı ortak şekil bir üretim kalıbı gibi bellekte kalır. Kalıpları, her adaya ayrı ağ ürettirmekten daha az veriyle ayrıntılı geometri çizmek için seçtim. Temel biçimlerin tekrarı hâlâ fark edilebilir. Her hücreye ayrı ağ vermek, üretim ve hızlandırma yapısının ömrünü yeniden tasarlamayı gerektirir.

## 2 · 2026-11-07 · seçim · Işık için geçici yerleşim tablosu

Kamera konisinin dışındaki bir kemer kadrajda gölge veya yansıma yapabilir. Işık taşıma için çevredeki kalıpların yerleşimleri her karede geçici tabloya giriyor. Kamera ışınlarının erişimi kendi maskesiyle sınırlı. İzleme bitince tablo ve hızlandırma verisi sıfırlanıyor. Bu karar, “bakılan dünya”yı doğrudan görüntünün yanı sıra görüntüye ışık taşıyabilecek çevreyi de kapsayacak biçimde yorumluyor. Çevre adaylarının kısa ömürlü de olsa yerleşim verisi var. Bu veri çıkarılırsa gölge, yansıma ve dolaylı ışık değişir.

İlk makro ada vekilinden ayrıntı kaybı yüzünden, her ışının tam kalıbı ayrı aradığı denemeden ise yakın kadrajdaki maliyeti ve referans farkı yüzünden döndüm.

## 3 · 2026-11-07 · seçim · Duran ve uçan kamera aynı görüntü yolunda

Her karede aynı örnekleri ve geçmiş kullanmayan gürültü gidericiyi seçtim. Böylece kamera durunca birikim görüntüyü değiştirmiyor. Hareket vektörüyle geçmiş taşımak düşük örnekle daha çok ayrıntı toplayabilirdi. Bu yolu seçmek hareket kılavuzlarını eklemeyi ve uçuş karşılaştırmalarını yenilemeyi gerektirir.

## 4 · 2026-11-07 · seçim · Adaları ayrı parçalardan kur

Özcan'ın uçuşunda bütün adayı oluşturan eski kalıpların tekrarı belirgindi. Bölgelere kaya kemeri, katmanlı sahanlık, parçalanmış taş ve koruluk gibi farklı siluetler verdim. Konumdan hesaplanan tarif, kaya ve yapı parçalarını ayrı ayrı ölçeklendirip yerleştiriyor. Her adaya tamamen ayrı üçgen ağı vermek daha geniş çeşitlilik sağlayabilir, ama her uçuşta yeni bir ağ ve hızlandırma yapısı üretir. Bu tercih değiştirilirse üretim, GPU yerleşimi ve çarpışma önbelleği birlikte ele alınmalı.

## 5 · 2026-11-07 · seçim · Teması çizilen yüzeyden al

Kameranın çevresine 27 cm yarıçaplı bir küre koydum. Her hareket boyunca kürenin gerçek üçgenlere ilk temasını arayıp kalan hareketi yüzeye paralel yönlendiriyorum. Basit kutular daha ucuzdu. Ancak kemerdeki boşluğu kapatabilir veya kaya çıkıntısının içinden geçirebilirdi. Sadece bu çarpışma modülünü değiştirerek başka bir çözüm sınanabilir.

## 6 · 2026-11-07 · seçim · Taşın mikro ayrıntısını üret ve piksel alanına göre süz

Taş için gözenek, ton, pürüzlülük ve kabartma eğimini sahne açılırken 4096²'lik bir haritaya ürettim. Yakın dokunun rengini gürültü giderme sonrasında yeniden uyguluyorum. Her ışında aynı ayrıntıyı baştan hesaplamak veya hep tam çözünürlüğü okumak seçeneklerdi. Süzgeçsiz denemede yakın rotada fazla değişim yükseldi. Harita tarifi ve filtre tersine çevrilebilir. Bunun bedeli, yakın temas ve hareket dizilerini yeniden karşılaştırmak.

## 2026-11-08 · Canlıda yol izleme yerine sabit gölgelendirme (Claude)

Hareket ederken titremenin kaynağı her karede yeniden gürültüden arındırılan 3 örnekli
yol izlemeydi. Projenin genel kuralı da canlıda rastgele sayıyı yasaklıyor. Diğer seçenek
yol izlemeyi koruyup zamansal birikim eklemekti; bu, hareket ve durgunluğun tek dünya
olması kuralını bozar ve hayalet bırakır. Sabit gölgelendirme 4 kat hızlı, kazanılan
süre 12 örneklik süper örneklemeye gitti. Geri almak `scenes/ruya.cfg`'de
`bounces = 3`, `spp = 3` yazmak kadar ucuz; yol izleyici referans için duruyor.

## 2026-11-08 · Adalar kapalı fonksiyon, çarpışma mesafe tahmini (Claude)

Ada ağları hücre başına üretilip ışık çevresinde 90 kare tutuluyor. Çarpışma aynı
fonksiyonun mesafe tahminini 20 cm adımlarla yürüyor. Diğer seçenek her adanın ağından
CPU BVH kurmaktı: aynı yüzey iki kez saklanırdı. Ağ köşelerinin fonksiyona uzaklığı
testte 5 cm altında.

## 2026-11-09 · Adalar: tasarlanmış silüet, masif düzeni, sık bitki (Claude, revizyon 1/3)

Adalar ızgarada rastgele dağılmış yuvarlak somunlar gibiydi ve üstleri boştu. Plan artık
dışbükey sütunların birleşimi (eklem, oyuk, kırık başlık taşı); duvarlar ortak katmanlanmayı
izliyor. Hücreler beş hücrelik masiflerde bir ana sütunun çevresinde toplanıyor, aralarda
kıvrılan boş koridorlar var. Diğer seçenek eski tek fonksiyonlu biçime yalnız gürültü eklemekti:
rastgele görünümü değiştirmezdi. Geri dönüş bedeli: `ruya_layout.cpp` ve `ruya_island.cpp`
birlikte, çarpışma ve yerleşim testleri yeniden.

## 2026-11-10 · Malzeme: her yüzeye CC0 tarama (Claude, revizyon 2/3)

Yalnız kaya ve çimen taranmıştı; gövde, taç, çatı, ahşap ve bronz düz renkti. On bir Poly Haven
seti (8K kaya/zemin/kesme taş, 4K kabuk/çatı/ahşap/metal/iki yaprak atlası), ışın izine göre
anizotrop süzme, karışım bölgelerinde yükseklik maskesiyle tek tarama seçimi; taçlar alfa
kesmeli dal kartları. Diğer seçenek prosedürel dokuyu keskinleştirmekti: "gerçek malzeme"
isteğini karşılamazdı. Geri dönüş bedeli: `fetch.py` ve doku kümeleri, yaprak kartları.

## 2026-11-10 · Titreme: referans kaba kuvvet, ölçü onun farkı (Claude, revizyon 3/3)

Canlı kare, aynı gölgelendirmenin 128 örnekli kaba kuvvet referansıyla karşılaştırılıyor:
referansta her örnek kendi yüzeyini kendi piksel payında (1/√128) gölgeler. 2. parçanın
"piksel başına bir yüzey" önbelleği bu referansta yok; o yüzden önbelleğin bulanıklığı da
kıpırtı olarak görünür. Diğer seçenek referansı da önbellekli çizmekti: doku ve alfa
kaynaklı titremeyi ölçüden silerdi. Bedel: büyük kaydırmalarda ölçü durağan farkı da sayar
(DURUM'da yazılı).

## 2026-11-10 · Yaprak alfası: örnekler arası kademeli eşik (Claude)

Sabit 0,5 eşiği (uzakta 0,2'ye kayan) yaprak kenarı bir örneği geçince pikseli sıçratıyordu.
Pikselin 12 örneği eşiği (0, 1) aralığına eşit dağıtılmış değerlerle sınıyor; mip ortalama
alfayı tuttuğu için örneklerin kartta durma payı kapsamaya eşit. Kaydırma pikselde sabit,
kareden kareye değişmez: rastgele sayı yok. Alfa ve yaprak dokusu örneğin kendi payında
süzülüyor. Rota (a) kıpırtısı 0,00681 → 0,00542 (eşik), → 0,00442 (örnek payı). Diğer seçenek
alfa mip'ini kapsama koruyarak yeniden üretmekti: tek eşikli örnek yine sıçrardı. Geri dönüş:
`cardOpaque` ve canlı döngüdeki `cut`.

## 2026-11-10 · Yüzey önbelleği çeyrek pikselde, siste tek (Claude)

Piksel başına tek değerlendirme, kaba kuvvet referansına göre bulanık ve kaydırmada
kıpırtı veriyordu; her örneği ayrı değerlendirmek rotada +7 ms. Yüzey pikselin her çeyreğinde
bir kez, çeyrek ayak iziyle değerlendiriliyor; sis kontrastın %45'inden fazlasını almışsa
tek değerlendirme. Rota (a) 0,00435 → 0,00386, rota (c) 0,00982 → 0,00798. İki yarım piksel
(+2,6 ms, 0,00424) ve her yerde çeyrek (p95 33,84 ms, bütçe dışı) denendi. Geri dönüş:
`quarters = 1`.

## 2026-11-10 · Doku süzgecinde anizotropi 2 → 16 (Claude)

Yatık bakılan duvar ve çimende 2 kat anizotropi dokuyu referansa göre bulanıklaştırıyordu.
16 kat rota (a) kıpırtısını 0,00442 → 0,00428 indirdi, bedeli ~0,35 ms. Geri dönüş:
`ruya_material.cu` `maxAnisotropy`.

## 2026-11-10 · Ortam ışını örnek başına 3 → 2 (Claude)

Kare süresinin en büyük kalemi ortam ışınlarıydı (3 → 1 rotada −11 ms). 2 ışın rota (a)
p50'yi 32,4 → 22,7 ms indirdi, kıpırtı 0,00428 → 0,00435, FLIP 0,0146 → 0,0150. 1 ışın
FLIP'i 0,0181'e çıkardı (ortam gölgesi basamaklandı). Sisin kontrastın %45'inden fazlasını
aldığı yüzeyde örnek başına 1 ışın: rota (a) p95 32,7 → 31,6 ms, kıpırtı değişmedi (0,00386).
Geri dönüş: `kAmbientRays` ve `shadeAmbient` çağrısı.

## 2026-11-10 · Ada ağları arka planda, kare beklemez (Claude)

Yeni hücre ışık çevresine girince ağı karede üretiliyordu (10–23 ms sıçrama). Sekiz işçi
iş parçacığı, ışık çevresinin 240 m dışına kadar hücreleri en yakından başlayarak üretiyor;
GPU yüklemesi ada ada, karede 2 ms bütçeyle. Ön ısıtılmış bellek havuzu: `cudaMalloc`
büyürken 32 ms bekletiyordu. Görüntü zamanlamaya bağlı değil: ışık çevresindeki hücre hazır
değilse kare onu bekler (ilk kare ve 1–6 sıçramaları), o kare tüm ileri halkayı da tamamlar.
Diğer seçenek hazır olmayanı o kare çizmemekti: aynı kamerada farklı görüntü verirdi.
Geri dönüş bedeli: `ruya_streaming.*`, `streamCells`; bellek ~2 GiB havuz.
