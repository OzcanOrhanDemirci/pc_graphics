# DURUM · sahne 7 "Ölçek"

> Tek kaynak durum panosu. Plan `docs/olcek/PLAN.md`. En yeni en üstte.

## 2026-10-05 öğleden sonra · Ö0 bitti: iskelet ve ölçek motoru

Galeride **07 Ölçek** (vurgu rengi gül `#E08BAE`, geçici galeri resmi evrenin kenarından). 62,0 onluğun hepsi
kaydırılabiliyor: `s` = log10(ekranın genişliği, m), alt uç Planck uzunluğu (s = -34,79), üst uç gözlemlenebilir evrenin
ekranın yüksekliğine sığdığı yer (s = 27,25). Cisimler **yer tutucu** (Özcan sordu: *"tamamen temsili mi olacak"*;
cevap: dışarısı Ö1-Ö3'te gerçek veriyle gerçek görünecek, ışığın dalga boyunun altı ister istemez temsili).

### Ne yapıldı
- **"Bugün"** (`tools/olcek_bugun.py`, astropy 7 + JPL DE440s → `src/scenes/olcek/olcek_bugun.h`): sahildeki noktada
  gerçek güneş öğlesi **12:58:56 TSİ** (09:58:56 UTC). Gezegenler, Ay, Güneş (Dünya'dan), Dünya'nın dönüş matrisi,
  Samanyolu'nun kutbu ve merkezi o anın gerçek yerinde. Planın iddiaları ölçüldü: başucu galaktik kuzey kutba **11,6°**
  (plan ~11°), gezegenlerin düzlemi **51,0°** eğik (plan ~50°), Güneş'in yüksekliği 46,8°.
- **Çapa ağacı** (`olcek_world.*`): Laniakea → Yerel Grup → Galaksi merkezi → Güneş → Dünya → bugünkü sahil noktası →
  sahil noktası → çekirdek → proton → kuark, yanlarda Ay, gezegenler, M31, M33, Macellan bulutları. İki çapanın arası
  yalnız ortak atalarına kadar toplanır. Kamera her an odağın çapasına göre tutulur, GPU'ya D (odak uzaklığı) biriminde
  float gider.
- **Odağın yolu** (PLAN §3): kuark ↔ proton ↔ çekirdek ↔ sahil ↔ Dünya ↔ Güneş ↔ Galaksi merkezi ↔ Yerel Grup ↔ Laniakea
  ↔ biz; her geçiş W'ye doğrusal karışımla (ekran düzgün hızla kayar). Kamera yalnız başucu boyunca uzaklaşır; yön
  yıldızlara göre sabit (kuzey yukarı, doğu sağ).
- **Zaman** (PLAN §4): R(s) = W^0,68 (W ≥ 1 m), W^1,24 (W ≤ 1 m); katman saatleri (Dünya'nın dönüşü ve yılı, Ay,
  gezegenler, galaksinin dönüşü ve kolları, örgü titreşimi, çölün oktavları). **Gözün kapağı**: Gauss pozlama
  (σ = 0,15 sn); hızlı yörünge halkaya döner (ışığı korunarak, örnekler zaman eşit aralıklı: yavaş yerde parlak), hızlı
  dönen Dünya'nın deseni boylamda ortalanır, örgü titreşimi analitik olarak (her kipin genliği e^(-ω²σ²/2) kadar kalır).
- **Eve dönüş**: kamera 10^6,6 m'nin altında yerle birlikte döner (sahil hep öğle, yer kaymaz); oradan 10^7,8'e kadar
  Dünya kendi hızına kavuşur. Dönerken yakınlaşmayla orantılı olarak bugünkü dönüşe çekilir (kısa yoldan). Yıl için aynısı
  10^11,9-10^12,9 arasında: odak Dünya'dan Güneş'e geçmeden Dünya yörüngede yürümez (yoksa kamera yörüngeyle birlikte
  bütün görüntüyü savururdu).
- **Çizici** (`olcek_kernels.cu`): piksel başına 4 sabit örnek (R2, ilki merkez, Blackman-Harris), birikim yok, rastgele
  sayı yok. Küre kesişimi kameranın yüksekliğinden (çift hassasiyetle hesaplanan) kararlı formülle: ekranın 10^12 katı
  bir küre yüzeyinin hemen üstünden de doğru. Noktalar ekranda Gauss lekesi, toplamları akıları: küçülen cisim ışığını
  koruyarak noktaya döner (1-2,5 piksel arası ikisi birlikte). Bulutlar (Gauss) analitik sütun, galaksi diskleri analitik.
  Rengin bittiği eşik: 10^-6 → 10^-7 m arasında renk söner.
- **Panel**: "Bakılan" (PLAN §2'nin tablosu, iki dil) ve "Ekranın genişliği" (fm, pm, nm, µm, mm, cm, m, km, milyon /
  milyar km, ışık yılı, bin / milyon / milyar ışık yılı; 10^-15 m'nin altında 10'un kuvveti). Yazı atlasına üst simgeler
  eklendi; IBM Plex'te üst simge eksi (⁻) yok: eksi işaretinden üst simge boyunda çiziliyor (`src/ui/ui.cpp`).
- **Tuşlar**: tekerlek (tık ×1,5, en hızlı saniyede 20 onluk), R sahile dön, T tur (evrenin kenarı, sahil, Planck,
  sahil; saniyede yarım onluk). `--autotest-only olcek` bütün yolu dolaşıyor.

### Ölçümler
- `--test olcek-hassasiyet` (her 0,02 onlukta görünen çapalar ve yanlarındaki bir nokta; float ile double-double):
  **en kötü 1,1 × 10⁻⁴ piksel**, 27.855 ölçüm, GEÇTİ (eşik 0,01). Sınama iki hata yakaladı: (1) çekirdeğin titreşimi
  zaman oranına bölünüyordu (çarpılmalıydı), titreşim hep sıfırdı; (2) bugünkü sahil noktası Dünya'nın merkezine
  bağlıydı, 10^-10 m'de proton 6.400 km'lik toplamın içinde kayboluyordu (0,014 px; titreşim açılınca ekranlarca olurdu).
  Şimdi sahil noktası bugünkü noktaya asılı.
- `--test olcek-zaman` (140 yolculuk: 7 uzaklığa çık, 5 bekleme, 4 dönüş hızı): sahile varışta **Dünya'nın dönüşü 0 rad,
  yılı 0 sn, sahil noktası 0 m** sapma, GEÇTİ. Bedeli: dönüş bölgesinde Dünya kendi hızından hızlı döner: saniyede 0,5
  onlukla inerken en çok **1,1 kat**, 2 onlukla 2,6, 8 onlukla 10, 40 onlukla 63 kat (planın ≤ %30'u yavaş inişte tuttu,
  hızlı inişte tutmuyor; Özcan canlıda görüp karar verecek).
- `--test olcek-sure` (üst uçtan alta ve geri, 0,01 onluk adımla 12.408 kare, zaman donuk): kare **her yerde ≤ 6,5 ms**,
  bir yer hariç: s 6-7'de 23,7 ms (yer tutucu Dünya deseninin dönüş ortalaması: 12 dokunuşta 6 oktav gürültü; Ö1'de gerçek
  dokuyla gidecek). Ana işlemci ≤ 2 ms. Sıçrama ölçüsü (bir kare, öncekinin yakınlaşma kadar büyütülmüşüyle; 2 piksel
  hoşgörü, kenar blokları hariç, eşik 0,04): ilk koşu 285 kare → düzeltmelerden sonra 127 kare. Düzeltilenler:
  - **s ≈ 2,9 (0,36):** Dünya'yı merkezinden ortalama yarıçaplı bir küre çiziyordum; sahil noktası elipsoidin üstünde,
    yerin merkezine 6.369,9 km, düşeyi merkezden 0,19° sapar: kamera 1 km yükseklikte kürenin yüzeyinin altına
    düşüyordu. Dünya küresi artık sahil noktasından geçer, merkezi yerel düşey üstünde.
  - **s ≈ -10,3 (0,18):** 0,25 mm'lik kum tanesinin küre kesişimi 0,1 nm'den bakınca float'ta bütün basamaklarını
    kaybediyordu (atom bir görünüp bir kayboluyordu). Ev tanesi için kesişim kameranın yüksekliğinden.
  - **s ≈ -10,5:** zemin atomun içine geçerken birden kalkıyordu; artık bir onlukta söner.
  - Ev kadrajda: Yerel Grup geçişinde Güneş kenara taşıyordu (1,08); geçiş 22,0-23,0'a kaydırıldı.
  - Kalanlar (yer tutucuya ait): s ≈ 7 köşelerde Dünya deseninin dönüş ortalamasında dokunuş sayısının tam sayı adımları
    (Ö1); s ≈ 26-26,6 kameranın kozmik ağın içinden geçerken yakınındaki galaksiler (yakın sönümü eklendi, Ö3'te ağ
    yeniden); s ≈ 11,5 Güneş'in alt kenarda derinlik farkı (ölçünün odak düzlemi varsayımı, sıçrama değil).
- `--autotest-only olcek`: 162 fps (ekranın 165 Hz'i), Dünya'da 40 fps (yer tutucu desen), kart belleği 2,2 GB.

### Plandan ayrılanlar (gerekçeleriyle)
- **Görüş açısı yatay 40°** (planda sayı yoktu): sahilde 1,4 m'den 1 m'lik bakış; evren dikeyde sığsın diye üst uç 27,25.
- **Yılın serbest bırakılması 10^11,9'dan sonra** (plan "s 8 → 5 arası dönme ve yörünge çekilir" diyordu): odak Dünya'dan
  Güneş'e geçerken Dünya yörüngede yürürse kamera onu izler, Güneş saniyede bir tur atar. 10^11-10^11,9 arasında Dünya
  yörüngede durur (o ölçekte bir nokta), öteki gezegenler yürür.
- **Pozlama eğrisi** (`scenes/olcek.cfg`, `ev`): gezegenlerin halkaları ve yıldızlar uzun pozlamayla görünür (s 13,5-22,6
  EV +23), galaksiler nokta olunca onlukta ~6,6 EV açılır (ışıkları uzaklığın karesiyle söner). Ö2-Ö3'te gerçek
  içerikle yeniden ayarlanacak.
- **Sahil noktası** Özcan'ın isteğiyle **Çandarlı**'ya (Dikili; kalenin önündeki kumsal) taşınıyor: Ö1'in ilk işi.

### NE DOĞRULANMADI
- Canlı pencerede elle tekerlek: yalnız `--autotest` betiğinin sanal tekerleği çevirdi; hızlı çevirmenin hissi, R'nin ve
  turun akışı gözle izlenmedi. Özcan eve gelince.
- Gözün kapağının (σ = 0,15 sn) halkaları ve Dünya'nın dönüş ortalamasını hareket hâlinde gözle görmedim; yalnız tek
  kareler. Ay'ın ve gezegenlerin halkaya dönüşü kare olarak var, akışı canlıda bakılmadı.
- Kararlılık (`tools/kararlilik.py`) bu sahnede koşulmadı: sabit örnek deseni ve birikimsizlik Heykel'inkiyle aynı, ama
  ölçülmedi.
- Yıldızlar, galaksiler, kozmik ağ, atom altı: hepsi yer tutucu; boyutlar ve yerler gerçek, sayıları ve görünüşleri değil.

### Sıradaki: Ö1 · Ev
Veri hazır (`tools/olcek_veri.py`, `assets/olcek/KAYNAKLAR.md`): Sentinel-2 (2025-09-23, 10 m, 82 km), Blue Marble NG Ekim
(dünya 21600×10800 ve Ege 500 m), Ay (LRO). Bugünün gerçek bulutları için NASA GIBS VIIRS (5 Ekim 2026; Avrupa geçişi
akşama tamamlanır). Sıra: sahil noktasını Çandarlı'ya taşı → dokulu Dünya ve Ay → bulutlar → atmosfer → sahil ve kum
gerçek kıyı çizgisine oturur.

## 2026-10-05 gecesi · Plan yazıldı

Özcan'la uzun bir konuşmada karar verildi: saf adres (Powers of Ten gibi), İzmir'de bir sahilden dışarı gözlemlenebilir
evrene, içeri Planck uzunluğuna, ~61,7 onluk, yalnız tekerlekle, kesintisiz. Zaman akıyormuş gibi akar (dışarı onlukta
×4,8, içeri ÷17), etkisi yok, sahil hep öğle. Görüntüde yazı yok; panelde bakılan cismin adı ve ekranın genişliği.
Sıradaki: **Ö0 · iskelet ve ölçek motoru** (bütün onluklar yer tutucularla, süreklilik ve hassasiyet sınamaları).
