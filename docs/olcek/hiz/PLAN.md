# PLAN · sahne 7 "Ölçek", ikinci seçenek: "Hız"

> Tek kaynak plan. 2026-10-05 akşamı Özcan'la uzun bir konuşmayla yazıldı (Ölçek'in ilk seçeneği bittikten hemen sonra).
> Durum ve ölçümler `docs/olcek/hiz/DURUM.md`'de. Özcan'ın referans ekran görüntüleri (Alan Becker, *Animation vs.
> Physics*; yalnız referans, telifli, git dışında) `assets/olcek/referans/`: `zaman-genislemesi.png`, `gamma-egrisi.png`,
> `uzay-kisalmasi.png`, `kuasar-galaksi.png`. Bağlam sıkıştırılırsa önce bu dosyayı, sonra Ölçek'in `docs/olcek/PLAN.md`
> §4-§7'sini (zaman, görsel dil, motor), hafızada `genel/feedback_gpu_ve_grafik.md` ve `grafik/feedback_grafik_netlik.md`'yi
> oku.
>
> **Yetki (Özcan):** *"Her zamanki gibi tamamen özgürsün, detayları da konuştuk ve gerçekten iyi bir şey bekliyorum. Hiç bu
> kadar fizik fizik yapmamıştık. [...] En iyi sonuca ulaşmak için her şey mübah yani her şey serbest ve yetkilisin."*
> Plan bir başlangıç; daha iyisi bulunursa değişir, değişiklik ve gerekçesi `DURUM.md`'ye yazılır.

## 1 · Görev ve Özcan'ın sözleri

Ölçek'in ilk seçeneği (Adres) ölçeği gösteriyordu: kamera yalnız uzaklaşıp yakınlaşıyor, evren bugünde donuk. **İkinci
seçenek hızı ve uzay-zamanı gösterir:** bir roket, üstünde temsili bir insan (manken), Dünya'dan Proxima Centauri'ye,
oradan Andromeda'ya gerçekten gider. Özel göreliliğin söylediği her şey, tam formülleriyle, canlı.

- *"Aklımdaki sahne hız ve uzay zaman ilişkisi üzerine [...] Bir roket var, üzerinde temsili insan var [...] Sadece W ve
  S tuşları çalışıyor [...] W hızlanıyor, S yavaşlıyor [...] Biz hızlandıkça 3 farklı grafik ile bunu matematiksel olarak
  gösteriyoruz, zaman bizim için hızıma uygun şekilde değişiyor. Uzay zaman dokusunu göstereceğiz yani. Gerçek fiziğe
  uygun olacak."*
- *"Işık hızının %80iyle giderken neredeyse durmuş gibi çok yavaş gidiyor. Ölçek algısını muazzam besleyen bir şey. Kamera
  yakından deli gibi anormal hızlı, uzay ölçeğine bi alıyorsun aslında gitmiyorsun bile o kadar hızlıyken."*
- *"Evet doppler efekt de dahil fizik ne derse o etkilerinin hepsini ekleyelim. Yani amaç o fiziği simüle etmek zaten."*
- *"Gerçek zaman, korkutucu ve hani yüce olan o his zaten. Anormal hızlısın saçma bir hız ama 4 yıl sürecek o kadar
  uzak. Uzayın o büyüklüğünü muazzam verir. [...] Bu daha çok kozmik korku gibi. Akıl almaz büyüklüğü anlatıyor."*
- *"Ama işin daha etkileyici kısmı şu olsun, harbiden gidelim. Yani çok saçma biliyorum ama bir manyak bilgisayarını 4
  yıl açık bırakacak olsa ulaşabilsin anlatabildim mi. Starfield pngsi gibi olmasın. Zaten fiziği simüle etmek o demek.
  Orada gerçekten galaksi var ve gerçekten gidiyoruz ama çok uzakta."*
- *"Manken ve roketi 3D görmeyelim 2.5D görelim. 3D kamera direk fps olsun. Yani roketin gözü varmış gibi. [...] Asıl
  işte süper olması gereken uzay. [...] Burada ise uzayda yolculuk yapıyoruz. Yani galaksinin, yıldızın harbiden öyle
  görünmesi lazım ve görüntü etkileyici, estetik ve gerçekçi olmalı."*
- *"Diyagram çizgileri de basit duruyor ama buna özenmeni istiyorum. Canlı grafiklerle o uzay zamandaki yol kısalmasını,
  bükülme hissini, hız ölçeğini ve saat geçmesini gerçekten güzel anlatalım. Grafikler de ekran kadar önemli yani."*
- *"O zaman andromeda 1 saat olsun. Yine çok uzun ama bilgisayar başında oturup yapılabilir. Muhtemelen ben yapabilirim
  :D. O bedelleri de grafik yapacağız nasıl olsa, onlarla beraber gösterelim aynı tasarım dilinde."*
- *"no mans sky gibi oyunlar ışınlanma yapıyor biz fizik yapıyoruz. [...] Bu etkiyi açıkçası düzgünce doğru şekilde,
  estetik şekilde simüle eden hiçbir şey görmedim."*

### Kararlar (Özcan, 2026-10-05 akşamı)
| Konu | Karar |
|---|---|
| Yer | Ölçek sahnesinin ikinci seçeneği (panelde Ölçek \| Hız). Aynı evren, aynı veri, aynı motor |
| Ekranın saati | **Roketin saati (B):** ekranın bir saniyesi yolcunun bir saniyesi. Dünya saati γ kat hızlı akar. (A, Dünya'nın saati, konuşuldu: ışık hızının yavaşlığı birebir yaşanır ama yolcunun saati donar; seçilmedi) |
| Zaman | **Gerçek zaman.** Hızlandırma yok, zaman çarpanı yok: bir yolcu saniyesi bir ekran saniyesi |
| Hız tavanı | **21 dokuz** (%99,9999999999999999999, γ ≈ 2,2 × 10¹⁰): Andromeda yolcunun saatiyle ~1 saat. 17 dokuz (4 gün) konuşuldu, Özcan 1 saati seçti |
| Duraklar | **Üç:** kalkış Dünya'nın yörüngesi; ara durak Proxima Centauri (4,25 ışık yılı, yanında Proxima b); uzak durak Andromeda (diskinin 100 bin ışık yılı önü). Dar alan: uzaya ve üç durağa özen |
| Rota | Dünya → Proxima → (153° dönüş) → Güneş sisteminin 1,9 ışık yılı yanından geri → Andromeda. Düz çizgiler, gerçek konumlar |
| Kontrol | **W** itiş (sabit öz-ivme), **S** fren (aynı güç), **Space** otomatik varış (doğru anda frenleyip yörüngeye oturur). A, D, geri vites yok. Bırakınca o hızda süzülürsün |
| Görüntüler | **Yandan 2,5B** (roket, manken, ölçülen dünya, grafikler) ve **FPS** (roketin gözü: 3B uzay, bütün optik etkiler). Roket ve manken yalnız 2,5B'de, okunur ve sade; özen uzaya |
| İnsan | **Manken** (çöp adam değil), sahnelerin üslubuna uygun |
| Grafikler | Ekran kadar önemli. Hız ölçeği, uzay-zaman dokusu, yol kısalması, saatler, **bedeller**; Özcan'ın görüntülerindeki dilde (ince çizgi, aralıklı başlık), kendi özenimizle |
| Fizik | **Özel görelilik, tam formüllerle, her etki.** İdealize edilen yalnız roketin mühendisliği (enerji, ivme, radyasyon), bedel panosu bunu açıkça gösterir. "Bükülme" özel görelilikte düz uzay-zamanın dilimlenmesidir (Minkowski), kütleçekimi değil |

## 2 · Fizik: özel görelilik, eksiksiz

**Durum** (yolcunun saatiyle ilerler): hız açısı φ (rapidite), öz zaman τ, Dünya'nın zamanı t (durgun çerçeve:
Güneş-Proxima-Andromeda, aralarındaki yüzlerce km/s yok sayılabilir; belgede yazılı), ayağın başından konum x (durgun
çerçevede, metre), yön (ayağın birim vektörü).

- **Hız açısı üzerinden:** β = tanh φ, γ = cosh φ, γβ = sinh φ. **1 − β = 2 / (e^(2φ) + 1)** doğrudan: 21 dokuzda
  bile basamak kaybı yok (0,999...9'u double saklayamaz, 1'e yuvarlar). Sayılar yazılırken de 1 − β'dan.
- **İtiş:** W basılıyken öz-ivme a = 0,4 c/s (φ̇ = 0,4 /s; ~1,2 × 10⁷ g). Sabit öz-ivmede kapalı biçimler (her karede
  tam, adım hatası yok): dt = cosh φ dτ, dx = c sinh φ dτ ⇒ bir adımda φ₀ → φ₁: Δt = (c/a)(sinh φ₁ − sinh φ₀),
  Δx = (c²/a)(cosh φ₁ − cosh φ₀). Süzülürken (itiş yok) Δt = cosh φ Δτ, Δx = c sinh φ Δτ.
- **Fren:** S aynı güçte, φ azalır. Sabit ivme olduğu için dokuzlar doğrusal düşer, γ katlanarak (Özcan'ın "katlanarak
  yavaşlama" isteği fizikte kendiliğinden böyle).
- **Tavan:** φ_max = arcosh(γ_max) = 24,52 (21 dokuz): W ile **61 saniye**. Bu arada 1.771 ışık yılı yol alınır.
- **Konum kesinliği:** ayağın başından uzaklık Andromeda ayağında 2,3 × 10²² m'ye çıkar; metre ölçeğinde yakınlaşırken
  roketin yanındaki tanecikler için çift-double (Ölçek'teki DD) ya da roketin kendi çapası (ağaçta bir düğüm, konumu
  ayağın başından DD). Kamera hep rokete göre.
- **Otomatik varış (Space):** durma mesafesi d_dur = (c²/a)(cosh φ − 1) (durgun çerçevede). Space basılınca roket kalan
  yol d_dur'a inene kadar süzülür, sonra aynı güçle frenler, hedefte **yörüngeye** oturur (Proxima'da Proxima b'nin
  yörüngesi, 0,0486 AU, ~47 km/s; Andromeda'da galaksiye göre durgun). Panelde **d_dur** ve **kalan** canlı: kendin de
  frenleyebilirsin. Geçersen durduktan sonra W hedefe geri götürür (roket döner; geri vites değil, yön).
- **Dönüş:** bir ayaktan ötekine geçerken roket yeni yöne döner (Proxima'da 153°), FPS'de gökyüzü yavaşça döner (~8 s).
- **Yolculuk** (roketin saatiyle, kusursuz sürüşte):

| Ayak | Roketin saati | Dünya'da geçen | En yüksek hız |
|---|---|---|---|
| Dünya → Proxima (4,2465 ışık yılı, Gaia) | **89 s** (44,5 W + 44,5 fren) | 4,25 yıl | rapidite 17,8 (γ 2,7 × 10⁷, ~15 dokuz) |
| Proxima → Andromeda (2,395 milyon ışık yılı) | **58,3 dk** (1 dk itiş, 56,3 dk seyir, 1 dk fren) | 2,395 milyon yıl | 21 dokuz |

- **Etkiler** (hepsi):
  1. **Zaman genişlemesi:** iki saat.
  2. **Yol kısalması:** roketin çerçevesinde gidiş yönündeki uzaklıklar γ kat kısa. Yandan bakışta evren basıklaşır.
  3. **Eşzamanlılığın göreliliği:** uzay-zaman diyagramında roketin "şimdi" çizgisi eğilir. Dünya'da "şu an"ın ne
     olduğu hıza göre değişir.
  4. **Sapınç:** tan(θ′/2) = e^(−φ) tan(θ/2) (her φ'de kararlı biçim). Gökyüzü öne toplanır.
  5. **Doppler:** D = 1 / (γ(1 − β cos θ)) (ileri yönde e^φ'ye kadar). Kara cisim T sıcaklıkta görünürse D·T
     sıcaklığında kara cisim olarak görünür: yıldızlar, güneşler, kozmik fon ışıması kesin.
  6. **Parlaklık:** yüzey parlaklığı (özgül şiddet, bolometrik) D⁴, nokta kaynağın akısı D² (I_ν/ν³ değişmez, katı açı
     D⁻²). İkisi de birim sınamayla doğrulanacak: hareket eden gözlemcide eşyönlü bir kara cismin enerji yoğunluğu
     γ²(1 + β²/3)u çıkmalı.
  7. **Terrell dönmesi** ve görülen biçimler: FPS'nin göreli ışın izlemesinden kendiliğinden (aşağıda).
  8. **Kozmik fon ışıması:** önde T′ = D · 2,725 K. ~γ 500-1000'de önde **görünür bir disk** olur, sonra morötesine, X ve
     gama ışınına geçer. Güneş'in fona göre 370 km/s'lik dipolü (3,4 mK) dahil.
  9. **Işığın gecikmesi:** Dünya'nın saatinin iki yüzü (§4): "şu an" (eşzamanlılık) ve "görülen" (gecikmeli, Doppler'li).

## 3 · Görüntüler

### 3.1 Roketin gözü (FPS): asıl iş

Mankenin yerinden bakış; fare sürükleyince baş döner (roket dümdüz gider), tekerlek görüş açısını daraltır (dürbün),
Tab ile yandan bakışa geçilir.

- **Göreli ışın izleme:** her pikselin yönü roketin çerçevesinde θ′. Ters sapınçla (tan(θ/2) = e^φ tan(θ′/2)) durgun
  çerçevede θ'ya çevrilir, sahne orada izlenir (durgun, gerçek konumlar), dönen ışığın tayfı D ile kaydırılır, parlaklığı
  D'nin kuvvetiyle çarpılır. Sapınç, Doppler, parlama, Terrell hep bu tek dönüşümden çıkar; ayrı ayrı "efekt" yok.
- **Renk:** her ışık kaynağının tayfı (kara cisim ya da birkaç kara cismin toplamı) D ile kaydırılır, CIE 1931 renk
  eşleme fonksiyonlarından sRGB'ye. log T üzerinde hazır tablo (10 K - 10¹² K; renk ve görünür ışığın payı). Görünür
  bandın dışına kayan ışık **görünmez olur** (dürüst: çok dokuzda pencere kararır).
- **Gökyüzü katmanları:**
  1. *Yakın uzay (Güneş'ten ~100 ışık yılı içi):* gerçek tüm gökyüzü fotoğrafı (ESO, Serge Brunier'in Samanyolu
     panoraması, CC BY 4.0; lisans H3'te doğrulanır) yaygın ışık için. Parlak yıldızlar fotoğraftan katalog konumlarıyla
     silinir, yerlerine katalog yıldızları nokta olarak konur (roketin yerinden doğru açıyla, Proxima'da yakın yıldızlar
     yer değiştirir, kuşak aynı kalır: gerçekte de öyle).
  2. *Galaksiden çıkış (yüzlerce - binlerce ışık yılı):* fotoğraf geçerliliğini yitirir. Samanyolu'nun **hacimsel 3B
     modeli** (ince ve kalın disk, çubuk, şişkinlik, kollar, toz katmanı: Ölçek'in modelinin 3B'si) devralır; geçiş
     Güneş'ten uzaklıkla, fotoğrafla model aynı yerden bakınca örtüşecek biçimde ayarlanır.
  3. *Galaksiler arası:* arkada Samanyolu (dışarıdan), önde Andromeda ve Üçgen (M33) hacimsel modelleri, uzakta 2MRS
     galaksileri, her yerde kozmik fon ışıması (isteğe bağlı Planck haritasının anizotropileri).
- **Pozlama:** tasarlanmış, görüntüden okunmaz (kıpraşma yok). Gökyüzünün toplam ışığı φ'nin ve yerin bilinen bir
  işlevi (fon ışımasının γ²'si, yıldızların toplamı): göz uyumu bu hesaptan, yumuşak. AgX ton eşleme.
- **Kalite:** gren yok, kıpraşma yok; nokta kaynaklar ışığını koruyarak (Ölçek'in noktaları gibi), deterministik.

### 3.2 Yandan 2,5B: ölçülen dünya ve hikâye

- **Ölçülen dünya, roketin çerçevesinde:** roketin "şimdi"sinde cetvelle ölçülen konumlar. Evren gidiş yönünde γ kat
  basıklaşır, roket durur, evren akar. Yıldızlar, Proxima, Andromeda basık; çok dokuzda yol üstündeki yıldızlar roketin
  önünde ince bir tabakaya sıkışır. Optik değil (göz bunu görmez), **ölçümdür**: grafiklerdeki dil.
- **Roket ve manken:** sade, temiz, okunur 2B çizim (vektör üslubu, hafif ışık). Roket bir foton roketi: itişte arkasında
  ince bir ışık huzmesi. Mankenin yüzü yok, oturuyor.
- **Hız hissi:** yıldızlararası ortamın tanecikleri (gaz ve toz; yoğunluk temsili, hız gerçek) roketin yanından geçer.
  Gözün kapağı bir kare (1/60 s): %99,9'da bir karede 5.000 km. 10 m'den bakınca çizgi yağmuru, Dünya boyunda kısa
  çizgiler, AU'da nokta, ışık yılında donmuş. Ölçek motorunun tekerleği aynen: 10 m'den 10²³ m'ye.
- **Hız oku:** roketle akan ince ok, üstünde v (m/s) ve β (Özcan'ın son görüntüsündeki gibi). Durak işaretleri: ad ve
  uzaklık (iki çerçevede).

### 3.3 Duraklar

1. **Kalkış, Dünya'nın yörüngesi:** Ölçek'in fotogerçekçi Dünya'sı, Ay ve Güneş bugünkü yerlerinde. FPS'de gökyüzünde
   Andromeda soluk bir leke: gideceğin yer en başından görünür.
2. **Proxima Centauri:**
   - Kızıl cüce (3.042 K, 0,154 R☉), Proxima b'nin yörüngesinden **1,69°** (Güneş'in Dünya'dan görünüşünün 3,2 katı).
     Yüzeyinde ışık-gölge (granülasyon, lekeler) ve parlamalar: o bir parlama yıldızı.
   - Proxima b (1,07 M⊕, gelgit kilitli olabilir) temsil: görüntüsü yok.
   - Gökyüzünde Alfa Erboğa A ve B (13.000 AU, **−6,6 ve −5,3 kadir**: Venüs'ten parlak).
   - **Güneş'imiz Kraliçe (Cassiopeia) takımyıldızında 0,4 kadirlik bir yıldız.**
3. **Andromeda:** diskinin 100 bin ışık yılı önünde, galaksiye göre durgun. Disk gökyüzünde **~70°**; arkada Samanyolu
   soluk bir sarmal. Dünya saati: 2,4 milyon yıl.

## 4 · Grafikler: ekran kadar önemli

Dil: Özcan'ın görüntülerindeki gibi ince beyaz çizgiler, aralıklı büyük harf başlıklar (IBM Plex), sakin, yumuşak
canlanma. Yolcunun saati **beyaz**, Dünya'nınki **kırmızı** (görüntülerdeki gibi). Her pano kendi kendini ölçekler
(logaritmik eksenler), 21 dokuzda da anlamlı kalır. Kenarlara yerleşir, iki görüntüde de; panelden açılıp kapanır.

1. **HIZ:**
   - "Kaç dokuz" merdiveni: ibre ışık hızı duvarına dayanır, dokuzlar tırmanmaya devam eder.
   - γ eğrisi, üstünde kayan nokta (eksen kendini büyütür).
   - v (m/s), β (bütün dokuzlarıyla), γ, rapidite.
2. **UZAY-ZAMAN DOKUSU:**
   - Minkowski diyagramı: Dünya'nın ızgarası, roketin t′ ve x′ eksenleri ve ızgarası, hızlandıkça ışık çizgisine doğru
     katlanır, sıkışır ("bükülme hissi").
   - Işık konisi, roketin dünya çizgisi (itişte hiperbol).
   - Roketin "şimdi" çizgisinin Dünya'nın zamanında nereyi kestiği: eşzamanlılık.
3. **YOL:**
   - Hedefe iki cetvel: Dünya'nın ölçtüğü (sabit) ve senin ölçtüğün (eriyen).
   - Durma mesafesi ile kalan mesafe (frenleme anı).
   - Sıkışan bir uzay ızgarası (Özcan'ın "SPACE DILATION" görüntüsü gibi).
4. **SAATLER:**
   - İki kadranlı saat. Senin saatin saniye saniye.
   - Dünya'nınki γ kat hızlı: çok dokuzda ibreleri bir diske bulanır (gözün kapağı), tarih sayacı yıllar, milyon yıllar.
   - İkinci yüz: "görülen Dünya saati", gecikmeli ve Doppler'li. Uzaklaşırken yavaş görünür.
5. **BEDEL:**
   - Harcanan enerji (1 tonluk roket için "Güneş'in X saniyesi"; 21 dokuzda **Güneş'in 1,5 saati**).
   - Foton roketi kütle oranı (hızlanmak için e^φ: 21 dokuzda **4,5 × 10¹⁰**, durmakla 2 × 10²¹).
   - Çekilen ivme (**12 milyon g**).
   - Önden gelen radyasyon ("Güneş ışığının X katı": 17 dokuzda 6 × 10⁸, 21 dokuzda **6 × 10¹²**), öndeki fon
     ışımasının sıcaklığı ve rengi.
   - Her hidrojen atomunun çarpma enerjisi (21 dokuzda 2 × 10¹⁹ eV: gözlenen en enerjik kozmik ışınlar düzeyi).
   - Bu bir düşünce deneyi: hareketin fiziği gerçek, idealize edilen roketin mühendisliği. Pano bunu söyler.

Çizim: arayüz şu an çizgi çizmiyor. Kenar yumuşatmalı ince çizgi, yay, eğri, kesikli çizgi altyapısı (GPU'da mesafe
alanıyla ya da arayüzün OpenGL'inde gölgelendiriciyle) H1'in ilk işi.

## 5 · Teknik yol

- **Yer:** `src/scenes/olcek/` içinde `olcek_hiz_*` dosyaları (fizik `olcek_hiz.h/.cpp`, FPS ve yandan çizim çekirdekleri,
  grafikler). Ölçek'in motoru (çapa ağacı, s tekerleği, katalog yükleyiciler, parçacık çizici, AgX) paylaşılır.
- **Seçenek:** panelde Ölçek | Hız (Laboratuvar'daki malzeme seçimi gibi). Hız açılınca kalkış durumu: Dünya'nın
  yörüngesi, φ = 0, τ = t = 0, "bugün" (2026-10-05 10:00 UTC).
- **Veri:**
  - Var olan: HYG (yıldızlar), 2MRS, Samanyolu, M31, M33 modelleri, Dünya, Ay haritaları.
  - Yeni: ESO panoraması; Proxima'nın Gaia konumu (4,2465 ışık yılı); Alfa Erboğa A/B (HYG'de var); CIE 1931 tabloları;
    isteğe bağlı Planck fon haritası.
- **Kalıcılık:** yolculuk durumu çıkışta kaydedilir (τ, t, φ, ayak, konum). Açınca kaldığı yerden sürer, kapalıyken
  geçen zaman sayılmaz.
- **Ses yok** (öteki sahneler gibi).

## 6 · Ölçüm ve sınama

- **Fizik birim sınamaları** (`olcek-hiz`):
  - kapalı biçimler adım adım entegrasyonla örtüşür;
  - 21 dokuzda 1 − β uzun-kesinlikli kestirimle;
  - Proxima kusursuz sürüşte 89,0 s / 4,25 yıl, Andromeda 58,3 dk / 2,395 milyon yıl;
  - sapınç ve Doppler özdeşlikleri (ters dönüşüm, D(θ)·D′(θ′) = 1);
  - eşyönlü kara cismin enerji yoğunluğu γ²(1 + β²/3)u.
- **Renk:** 5.778 K'lik bir yıldız D = 2'de 11.556 K'lik kara cisim gibi görünür; fon ışıması önde görünür banda
  girdiği γ'da disk olur.
- **Süreklilik:** Ölçek'in `olcek-sure`'si gibi bir tarama: hızlanma ve tekerlek boyunca sıçrama yok (parçacık ölçüsü
  dahil). Kare ≤ 16 ms (2560×1440).
- **Tam yolculuk:** hızlandırılmış bir sınama koşusu bütün yolculuğu sayılarla doğrular. Gerçek bir saatlik koşu
  Özcan'ın (*"Muhtemelen ben yapabilirim :D"*).

## 7 · Kilometre taşları

- **H0 · Fizik ve iskelet:** seçenek, roket durumu, W/S/Space, otomatik varış, ayaklar ve dönüş, kalıcılık, fizik
  sınamaları, paneldeki sayılar. Görüntü en basit hâliyle: FPS'de katalog yıldızları sapınç ve Doppler'le (CIE tablosu
  dahil), yandan bir nokta.
- **H1 · Grafikler:** çizgi altyapısı, beş pano, canlı. Özcan'ın gözüne erken gösterilir.
- **H2 · Yandan 2,5B:** roket ve manken çizimi, ölçülen dünya (kısalma), yıldızlararası ortamın çizgileri, tekerlek, hız
  oku, durak işaretleri.
- **H3 · FPS gökyüzü:** göreli ışın izleme, ESO panoraması ve yıldız silme, kozmik fon ışıması, pozlama.
- **H4 · Galaksiler 3B:** Samanyolu'nun hacimsel modeli (içeriden ve dışarıdan, fotoğrafla örtüşen), M31, M33, 2MRS.
- **H5 · Duraklar:** Dünya'nın yörüngesi, Proxima (yıldız, b, Alfa Erboğa, Güneş'imiz Kraliçe'de), Andromeda'ya varış,
  yörüngeye oturma, dönüş.
- **H6 · Bütünleme:** bedel panosunun ince ayarı, galeri ve "Ayrıntılar", README (iki dil), GOSTERI/DEMO, autotest,
  tam yolculuk sınaması, performans.

## 8 · Riskler ve dürüst sınırlar

- **Galaksinin içeriden ve dışarıdan gerçekçiliği** en büyük iş. Fotoğrafla modelin örtüşmesi, H4'te ölçülerek.
- **Panodan yıldız silmek:** parlak yıldızlar silinmezse katalogla çift görünür. Silme izi kalmamalı.
- **Dinamik aralık:** önde parlaklık D² ile 10²⁰'ye çıkar; tasarlanmış pozlama ve AgX ile.
- **Temsiller açıkça yazılır:**
  - yıldızlararası ortamın yoğunluğu (çizgiler için);
  - Proxima b'nin görünüşü;
  - roketin mühendisliği (bedel panosu);
  - Samanyolu'nun ölçülmeyen yarısı (Ölçek'teki gibi).
- **Fizik notu:** "bükülme" burada Minkowski uzay-zamanının dilimlenmesidir; gerçek eğrilik (kütleçekimi) bu planın
  dışında.

## 9 · İleride (bu planın dışında)

- Genel görelilik: bir kara deliğin yanından geçiş (merceklenme, zaman genişlemesi).
- Daha uzak duraklar: Virgo kümesi, bir kuasar, kozmik olay ufku (~16 milyar ışık yılı: ötesine ışık bile varamaz).
- İkizler: Dünya'ya dönüş ve karşılaştırma.
- Ekranın saati olarak Dünya'nınki (A): "bilgisayarı 4,25 yıl açık bırakan Proxima'ya varır".
