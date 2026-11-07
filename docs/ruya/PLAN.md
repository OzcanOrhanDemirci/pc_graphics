# Rüya

## Görev ve yetki · 2026-11-07

Özcan'ın brief'i:

> pc_graphics projem için **Rüya** adında yeni bir sahne geliştir. Amaç, RTX 4080'imin tüm potansiyelini kullanarak gerçek zamanlı, tamamen prosedürel, olağanüstü yoğun ve fotogerçekçi bir dünya oluşturmak. Oyuncu WASD ve fareyle serbestçe uçabilecek; görüş açısı ve kamera dönüş hızı sınırlı olacak, yalnızca bakılan dünya üretilecek, görünür kaldığı sürece sabit kalacak, görüş dışına çıktığında bellekten silinecek ve tekrar bakıldığında yeniden üretilebilecek. Kalıcı dünya, fiziksel etkileşim ve genel amaçlı oyun sistemleri gerekmiyor. **Rüya konseptinin sanat yönetiminde tamamen özgürsün; yaratıcı ol, görsel ve teknik sınırları zorla.** Hedef 1440p'de yaklaşık 30–40 FPS ile mümkün olan en yüksek geometri, doku, ışık ve path tracing kalitesine ulaşmak. Yalnızca benim bilgisayarıma özel optimizasyon yap; başka donanımları düşünme. Algoritmaları, mimariyi ve yöntemleri sana bırakıyorum. Önce alt ajanlardan yararlanarak alanın en iyi yöntemlerini ve sınırlarını araştır, bunları aşmayı hedefleyen bir plan oluştur, ardından onay beklemeden tamamen otonom şekilde uygula, test et ve optimize et. Daha iyi sonuç bulduğunda planı ve mimariyi değiştirmekte özgürsün. İlk çalışan sonuçla yetinme; bütün performans ve doğruluk iddialarını bağımsız ölçümler, karşılaştırmalar ve kayıtlarla kanıtla. **Bu, senin araştırma, yaratıcılık, mühendislik ve otonom çalışma kapasitenin sınırlarını ölçen ağır bir test. Elinden gelenin mutlak en iyisini yap; ortaya çıkan sonuç, bu görevde ulaşabildiğin en yüksek seviye olarak değerlendirilecek. (Yıkım sahnesini optimize edebildiğini gösterdin. Sıra bu sahnede.)**

## Görsel hedef

Sis denizinden yükselen, altı aşınmış kireçtaşı adalar. Üstlerinde kat kat açık kemer galerileri, bronz ince işçilik, küçük su aynaları ve taşın arasından büyüyen bahçeler. Düşük güneş taşın sıcaklığını, soğuk gök ıslak yüzeyleri ve boşluğu ayırır. Uçarken yakın yaprağın ve taş gözeneklerinin arasından kilometrelik tekrarsız yerleşime geçilir. Dünya gerçek bir yer iddiası değildir.

Öneri: RT çekirdeklerinin opak üçgen kesişimi, paylaşılan yoğun prototipler ve kamera konisine göre yerleşim. Karşı görüş: her ışında bütün dünyayı SDF ile aramak, geometri belleğini azaltır ama karmaşık kesişimi SM'lere taşır; bu bilgisayarın RT birimlerini kullanmaz. CLAS/Mega Geometry ve zamansal rezervuarlar ilk çalışan ölçümden önce varsayılan yapılmayacak.

## Araştırmanın değiştirdiği karar

Bu bölüm ilk hipotezi kaydeder. Uygulama sırasındaki deneyler ve seçilen
geçici ışık tablosu [kararlar](decisions.md) ve [ölçümler](DURUM.md) içindedir.

Yalnız birincil kamera konisinde üçgen tutmak, kamera dışındaki gölge ve yansımayı tam hesaplamakla genel halde çelişir. Ayrıntılı dünya hücreleri yalnız konservatif görüş konisinde tutulacak. İkincil ışınlar için görüş dışı makro adalar aynı koordinat/tohum tarifinden belleksiz sorgulanacak. Bu vekil, ayrıntılı kemer ve bitkilerle birebir aynı değildir. Tam çevre üçgen referansıyla farkı ölçülmeden fiziksel doğruluk iddiası kurulmaz.

Ortak prototipler ve malzeme fonksiyonları üretim tarifidir; dünya hücresi değildir. Bunların kalıcı GPU belleği ile etkin hücre verisi ayrı raporlanacak. Konservatif hücre kümesi gerçek örtülmeyen piksel kümesi değildir: bir parçanın kadraja girebilmesi bütün hücreyi aday yapar. Dünya diske kaydedilmez, kamera veya kare numarası geometrinin girdisi olmaz.

## Uygulama sırası

1. Deterministik hücre tarifleri, opak ve yoğun prosedürel prototipler; bağımsız geometri, görünürlük ve kontrol testleri. Galeriye yeni sahne; mevcut sahneler değişmez.
2. Aynı canlı/headless renderer üzerinde CUDA/OptiX, çok sıçramalı diffuse/GGX yolları, doğrudan güneş örneklemesi, filtreli prosedürel malzemeler. Başlangıçta geçmiş kullanmayan OptiX gürültü giderici. Duran ve hareketli kamerada aynı kalite.
3. Görsel inceleme, altı kamera, hareket rotası, maliyet ayrıştırma; eşit kamera/kalitede A/B/A. Kaliteyi ölçülen kare bütçesine göre artırma. Ham yüksek örnek referans, görüş dışı vekil A/B, tekrarlı üretim ve bellek döngüsü. Bağımsız ajan ham kanıtı inceleyecek.

## Ölçümden önce kabul eşikleri

- Çözünürlük: giriş ve çıktı tam 2560×1440; yükseltme yok. Ana performans hedefi toplam headless kare medyanı ≤33,33 ms, hareketli yol p95 ≤40 ms. Sunum/ekran FPS'i ayrı ve görünür pencere açılmadan doğrulanmamış kalır. Sıcak koşulda en az 300 kare ve üç ayrı süreç tekrarı.
- Sonlu olmayan örnek sayısı: 0. Geometri indekslerinin ve normallerin geçerliliği; pozitif alanlı üçgenler. Hiçbir görünür aday hücre konservatif sınırından taşmaz.
- A→B→A: aynı hücre tarifi ve prototip parmak izi tam eşit. Aynı girdili sabit kamera çıktısı üç ayrı süreçte aynı. Etkin dünya verisi bakışın dışında kalmaz; boşaltma testinde etkin hücre sayısı 0.
- Bellek: başlangıç ve 20 gidiş/dönüş sonrası etkin veri farkı 0 bayt; kapasite havuzu ayrıca raporlanır. Kaynak bırakıldıktan sonra bağımsız kart kullanım farkı ≤64 MiB; sürücü önbelleği etkisi ayrıca kaydedilir. Tahsis öncesi boş GPU belleği sorgulanır, en az 1 GiB pay bırakılır.
- Kontrol: yaw 0'da W −Z, D +X; fare sağa → bakış sağa. Dönüşün iki eksenli toplamı ≤0,85 rad/s, FOV 36–48°, pitch ±1,45 rad. Dünya hücreleri görünür kaldıkça transform/topoloji değiştirmez.
- Kalite: aynı kamerada ham 64/256 spp referans yakınsaması, varsayılan kalite/ham/giderilmiş karşılaştırması. Görüş dışı vekil ve tüm çevre referansı farkı sayısal ve görsel raporlanır; düşük hata eşiği sonuç görüldükten sonra başarıya göre uydurulmaz. Ön hedef lineer normalize RMSE ≤0,10; bu eşik sanat kalitesini veya fotogerçekçiliği kanıtlamaz.
- Ölçü araçları negatif kontrolle kırmızı sonuç vermeli: bozuk hücre kimliği, siyah kare, yanlış kamera yönü veya geçersiz geometri. Sadece PASS yazısı kanıt sayılmaz.

## Doğrulama dozu ve sınır

Hobi için hafif dozun üstüne, Özcan'ın bu görevde istediği bağımsız araştırma ve ağır ölçüm eklenir. Pencere, ses ve odak çalma yok; testler headless. Kullanıcının gerçek fare ve sanat değerlendirmesi otomatik testle kapatılamaz. “Sektörün en iyisi”, “fotogerçekçilik kanıtlandı” veya “4080'in tüm potansiyeli kullanıldı” tek uygulama ölçümünden çıkarılmayacak.
