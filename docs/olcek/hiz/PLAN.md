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
| Zaman | **Gerçek zaman.** Hızlandırma yok, zaman çarpanı yok: bir yolcu saniyesi bir ekran saniyesi. 10-06'da yeniden: *"Zaman hızlandırma olmasın. Bu tüm mantığımıza ters. Sürecin uzun sürmesi proxima ve bir galaksinin çok uzakta olmasıyla ilgili hissi verir."* |
| Hız tavanı | **21 dokuz** (%99,9999999999999999999, γ ≈ 2,2 × 10¹⁰): Andromeda yolcunun saatiyle ~1 saat. 17 dokuz (4 gün) konuşuldu, Özcan 1 saati seçti |
| Duraklar | **Üç:** kalkış Dünya'nın yörüngesi; ara durak Proxima Centauri (4,25 ışık yılı, yanında Proxima b); uzak durak Andromeda (diskinin 100 bin ışık yılı önü). Dar alan: uzaya ve üç durağa özen |
| Rota | Dünya → Proxima → (153° dönüş) → Güneş sisteminin 1,9 ışık yılı yanından geri → Andromeda. Düz çizgiler, gerçek konumlar |
| Kontrol | **W** itiş, **S** fren (aynı güç), **Space** otomatik varış (doğru anda frenleyip yörüngeye oturur). A, D, geri vites yok. Bırakınca o hızda süzülürsün. **İvme 10-06'da değişti** (aşağıda): sabit değil, 1 g'den başlayıp hızla artan |
| Görüntüler | **Yandan 2,5B** (roket, manken, grafikler) ve **FPS** (roketin gözü: 3B uzay, bütün optik etkiler). Roket ve manken yalnız 2,5B'de, okunur ve sade; özen uzaya. **10-06:** yandan görünüm **Dünya'nın çerçevesi** (evrenin haritası), roketin çerçevesi ("ölçülen dünya") söküldü; iki görüntü de kalır, FPS'e **enerji gözü** eklenir (aşağıda) |
| İnsan | **Manken** (çöp adam değil), sahnelerin üslubuna uygun |
| Grafikler | Ekran kadar önemli. Hız ölçeği, uzay-zaman dokusu, yol kısalması, saatler, **bedeller**; Özcan'ın görüntülerindeki dilde (ince çizgi, aralıklı başlık), kendi özenimizle |
| Fizik | **Özel görelilik, tam formüllerle, her etki.** İdealize edilen yalnız roketin mühendisliği (enerji, ivme, radyasyon), bedel panosu bunu açıkça gösterir. "Bükülme" özel görelilikte düz uzay-zamanın dilimlenmesidir (Minkowski), kütleçekimi değil |

### Kararlar (Özcan, 2026-10-05 gecesi ve 10-06, H1'e bakınca)
H1'den sonra Özcan ilk kez baktı: *"şuan fizik olarak doğru, grafik olarak büyük oranda istediğim gibi ama görüntü olarak
hiç aklımdakine uymayan bir durum var"*. Konuşarak şunlar kararlaştı:

| Konu | Karar |
|---|---|
| **İvme** | *"Işık hızının %1 ine çıkmamız 1 dk falan sürsün. Sonra %10 civarına tekrar 1 dk. Kalan %90 ise 3-4 dk [...] %99 dan sonraki 9 larda ise birkaç dk içerisinde 21 dokuza ulaşırız [...] 7 8 dk falan [...] bu sayede hız, olayın sahnenin kendisi olur [...] 21 dokuz gibi absürt hızlar pratikte daha nadir ulaşılır. Hem %1 ışık hızı bile anca ulaşılan çok yüksek bir hız hissini korur."* İvme 1 g'den başlar, hep artar. Yolculukların uzaması bilerek kabul edildi (Proxima ~14 dk, Andromeda ~1 sa 12 dk tahmini; §2) |
| **Hız hissi** | *"araba oyunlarında 300 km hızla gidersin ve çok hızlıyım dersin [...] O hissi sağlayacak bir şeyler. Gerçekçiliği bozmadan."* Kaynakları: yakın cisimler (kalkışta Dünya ve Ay), gökyüzünün kendisi (sapınç, Doppler, fon), yandan akan **yıldızlararası ortam noktaları (hep açık, ayrı düğme yok)**, motorun ışığı |
| **Yandan görünüm** | *"Yalnız 1 olsun"*: **Dünya'nın çerçevesi** (evrenin haritası; `referans/kuasar-galaksi.png` gibi). Kamera roketi izler; tekerlek: yakın = hız, uzak = ölçek ve küçüklük. *"estetik, yüce ve kozmik olarak wow dedirtmesi lazım [...] uzayın büyüklüğündeki küçüklüğümüz ve yavaşlığımız hissi"* |
| **İki görüntü** | FPS'i bırakmayı sordu, kaybı duyunca: *"FPS i bozmayalım. 2 görüntümüz de olsun. Motor ışığı ve diğer her türlü fizik detayı güzel olur [...] %1 den ışık hızına iyice yaklaştığımız süreç ve ışık hızına neredeyse ulaşma seviyesinde o uzayı fizikle ve estetik şekilde görmek istiyorum. Planı kaybedeceğimiz şeyler olduğu için küçültmeyelim."* |
| **Sanat yönetimi** | *"gerçeklik kadar estetik yani gerçek estetiğe önem veriyorum. Altın saati ders olarak yazmıştık, gerçekti ama estetik değildi."* İlke: **fizik bükülmez; sanat yönetimi fiziğin kendi araçlarıyla** (pozlama ve göz uyumu, gözün optiği: parlama ve kırınım, dürbün, kadraj, zamanlama) |
| **Enerji gözü** | FPS'te görünmeyen ışığı (morötesi, X, gama) açıkça etiketli renklerle gösteren dedektör. *"fizik açısından daha doğru [...] öğretici olur. Ama gerçek görüntüyü isteyen kapatır."* Açık başlar |
| **Grafikler** | H1 revizesi: UZAY-ZAMAN sade (eksenler ve katlanan ızgara), HIZ'da dokuz merdiveni yok. *"grafikler istediğim gibi olmuş"* |
| **Yetki** | *"Tam otonom şekilde bitirene kadar devam et"* (10-06 ~00:07). Taşların sırası ve FPS'te aracın bir parçasının görünmesi Claude'un kararı |

## 2 · Fizik: özel görelilik, eksiksiz

**Durum** (yolcunun saatiyle ilerler): hız açısı φ (rapidite), öz zaman τ, Dünya'nın zamanı t (durgun çerçeve:
Güneş-Proxima-Andromeda, aralarındaki yüzlerce km/s yok sayılabilir; belgede yazılı), ayağın başından konum x (durgun
çerçevede, metre), yön (ayağın birim vektörü).

- **Hız açısı üzerinden:** β = tanh φ, γ = cosh φ, γβ = sinh φ. **1 − β = 2 / (e^(2φ) + 1)** doğrudan: 21 dokuzda
  bile basamak kaybı yok (0,999...9'u double saklayamaz, 1'e yuvarlar). Sayılar yazılırken de 1 − β'dan.
- **İtiş (10-06'dan):** öz-ivme hızın bir işlevi, a(φ): 1 g'den başlar, hep artar, tepede milyonlarca g. Hedef takvim
  (duruştan W basılı): **%1 c ~1 dk, %10 c ~2 dk, %99 c ~5,5 dk, 21 dokuz ~8 dk.** a(φ) ince dilimlerde sabittir (komşu
  dilimler %1'den az farklı: hissedilmez); her dilimde sabit öz-ivmenin kapalı biçimleri geçerli, böylece fizik yine her
  karede tam: dτ = c dφ/a, Δt = (c/a)(sinh φ₁ − sinh φ₀), Δx = (c²/a)(cosh φ₁ − cosh φ₀). Süzülürken Δt = cosh φ Δτ,
  Δx = c sinh φ Δτ. (H0'daki sabit 0,4 c/s'nin yerini aldı: Özcan'a *"hızlanmıyoruz, direk ışık hızına yaklaşıyoruz"*.)
- **Fren:** S aynı güçte, φ azalır. Sabit ivme olduğu için dokuzlar doğrusal düşer, γ katlanarak (Özcan'ın "katlanarak
  yavaşlama" isteği fizikte kendiliğinden böyle).
- **Tavan:** φ_max = arcosh(γ_max) = 24,5237 (21 dokuz; "24,52"ye yuvarlanmış hâli 20 dokuz verir): yeni ivmeyle
  W ile ~8 dk (H0'da 61,3 s).
- **Konum kesinliği:** ayağın başından uzaklık Andromeda ayağında 2,3 × 10²² m'ye çıkar; metre ölçeğinde yakınlaşırken
  roketin yanındaki tanecikler için çift-double (Ölçek'teki DD) ya da roketin kendi çapası (ağaçta bir düğüm, konumu
  ayağın başından DD). Kamera hep rokete göre.
- **Otomatik varış (Space):** durma mesafesi d_dur(φ) = D(φ), duruştan φ'ye itişte alınan yol (yasa yalnız φ'ye bağlı
  olduğu için fren itişin aynası: aynı yol, aynı süre; sabit ivmede (2c²/a) sinh²(φ/2)). Space bulunduğun her durumdan
  **en kısa yoldan** götürür (H0'da değişti, gerekçe DURUM'da): durabileceği en yüksek hız φ* (D(φ*) = (r + D(φ))/2)
  daha yukarıdaysa hızlanır, tavanda süzülür,
  kalan yol d_dur'a inince aynı güçle frenler, hedefte durur. Zaten o kadar hızlıysan yalnız süzülür ve doğru anda
  frenler. **W ya da S'ye basmak** kumandayı geri alır; elle hedefin binde biri kadar yakınında durursan son kısmı otomatik
  varış uçar (ışınlanma yok). Hedefte **yörüngeye** oturma H5'in işi (Proxima'da Proxima b'nin yörüngesi, 0,0486 AU,
  ~47 km/s; Andromeda'da galaksiye göre durgun); H0'da roket hedefte durgun çerçevede durur. Panelde **d_dur** ve **kalan**
  canlı: kendin de frenleyebilirsin. Geçersen durduktan sonra W hedefe geri götürür (roket döner; geri vites değil, yön).
- **Dönüş:** bir ayaktan ötekine geçerken roket yeni yöne döner (Proxima'da 153°), FPS'de gökyüzü yavaşça döner (~8 s).
- **Yolculuk** (roketin saatiyle, kusursuz sürüşte; **H0'ın sabit ivmesiyle**, yeni ivmenin ölçümleri DURUM'da: tahmin
  Proxima ~14 dk ve en çok ~14 dokuz, Andromeda ~1 sa 12 dk):

| Ayak | Roketin saati | Dünya'da geçen | En yüksek hız |
|---|---|---|---|
| Dünya → Proxima (4,2457 ışık yılı, Gaia DR3 bugüne taşınmış) | **89,0 s** (44,49 W + 44,49 fren) | 4,25 yıl | rapidite 17,8 (γ 2,7 × 10⁷, ~15 dokuz) |
| Proxima → Andromeda (2,3951 milyon ışık yılı) | **58,3 dk** (61,3 s itiş, 56,25 dk seyir, 61,3 s fren) | 2,395 milyon yıl | 21 dokuz |

- **Etkiler** (hepsi):
  1. **Zaman genişlemesi:** iki saat.
  2. **Yol kısalması:** roketin çerçevesinde gidiş yönündeki uzaklıklar γ kat kısa. Yandan bakışta evren basıklaşır.
  3. **Eşzamanlılığın göreliliği:** uzay-zaman diyagramında roketin "şimdi" çizgisi eğilir. Dünya'da "şu an"ın ne
     olduğu hıza göre değişir.
  4. **Sapınç:** tan(θ′/2) = e^(−φ) tan(θ/2) (her φ'de kararlı biçim). Gökyüzü öne toplanır.
  5. **Doppler:** D = γ(1 + β cos θ) (θ durgun çerçevede, kaynağa doğru) = 1 / (γ(1 − β cos θ′)) (θ′ roketin
     gördüğü açı); ileri yönde e^φ'ye kadar. (Planın ilk hâli ikinci biçimi durgun çerçevenin θ'sıyla yazıyordu: 90°'de γ
     yerine 1/γ verir. Kâhin buldu, H0.) Kara cisim T sıcaklıkta görünürse D·T sıcaklığında kara cisim olarak görünür:
     yıldızlar, güneşler, kozmik fon ışıması kesin.
  6. **Parlaklık:** yüzey parlaklığı (özgül şiddet, bolometrik) D⁴, nokta kaynağın akısı D² (I_ν/ν³ değişmez, katı açı
     D⁻²). İkisi de birim sınamayla doğrulanacak: hareket eden gözlemcide eşyönlü bir kara cismin enerji yoğunluğu
     γ²(1 + β²/3)u çıkmalı.
  7. **Terrell dönmesi** ve görülen biçimler: FPS'nin göreli ışın izlemesinden kendiliğinden (aşağıda).
  8. **Kozmik fon ışıması:** önde T′ = D · 2,725 K. Önü γ ≈ 150'de kızarmaya başlar (798 K), γ ≈ 370'te 2.000 K,
     γ ≈ 550'de 3.000 K; sonra morötesine, X ve gama ışınına geçer. **Büyük bir disk değil, küçük bir nokta:** T_f'den sıcak
     bölgenin açısal yarıçapı en çok asin(T0/T_f) (1.000 K için 0,16°), en büyüğüne γ = T_f/T0'da varır, sonra
     √(2T0/(γT_f)) ile küçülür: gittikçe ısınan ve küçülen bir nokta (planın ilk hâli "γ 500-1000'de görünür bir disk"
     diyordu; kâhinin hesabı, H0). Güneş'in fona göre 370 km/s'lik dipolü (3,36 mK, Planck 2018) dahil.
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
     panoraması, CC BY 4.0, doğrulandı) yaygın ışık için. Fotoğrafın yıldızları silinir (H3: 9 piksellik medyan),
     yerlerine katalog yıldızları nokta olarak konur (roketin yerinden doğru açıyla, Proxima'da yakın yıldızlar yer
     değiştirir, kuşak aynı kalır: gerçekte de öyle).
  2. *Galaksiden çıkış (yüzlerce - binlerce ışık yılı):* fotoğraf geçerliliğini yitirir. Samanyolu'nun **hacimsel 3B
     modeli** (ince ve kalın disk, çubuk, şişkinlik, kollar, toz katmanı: Ölçek'in modelinin 3B'si) devralır; geçiş
     Güneş'ten uzaklıkla, fotoğrafla model aynı yerden bakınca örtüşecek biçimde ayarlanır.
  3. *Galaksiler arası:* arkada Samanyolu (dışarıdan), önde Andromeda ve Üçgen (M33) hacimsel modelleri, uzakta 2MRS
     galaksileri, her yerde kozmik fon ışıması (isteğe bağlı Planck haritasının anizotropileri).
- **Enerji gözü (10-06):** görünmeyen ışığı da sayan dedektör: her yönden gelen enerji (bolometrik), bandına göre
  açıkça etiketli renklerle (radyo/mikrodalga, kızılötesi, görünür, morötesi, X, gama). Çok dokuzda gözle kararan evren
  onunla önde yanan bir noktaya döner. Açık başlar; kapatınca saf göz. **H4:** yolculuğun başında sıfırlanmış: fon
  ışımasının duruştaki eşit parıltısı çıkarılır, hareketin kattığı görünür (yoksa her yer kırmızı bir perde).
- **Sanat yönetimi (10-06):** fizik bükülmez. Araçlar: pozlama ve göz uyumu, gözün optiği (parlak noktanın çevresinde
  parlama ve kırınım halkaları), dürbün, kadraj, zamanlama. Aracın bir parçası (pencere kenarı, burun) **görünmez**
  (H3 kararı: öndeki tek ışığın çevresini kapatır, roket yandan görünüşte var).
- **Göz uyumu (H3):** karanlıkta yarım uyum (parlak gök parlak görünür), gündüz düzeyinde tam uyum (Weber); yaygın
  ışığın alan toplaması kazancı gündüzde söner. Önde doğan "güneş" φ 7'den 16'ya aynı parlaklıkta kalır.
- **Pozlama:** tasarlanmış, görüntüden okunmaz (kıpraşma yok). Gökyüzünün toplam ışığı φ'nin ve yerin bilinen bir
  işlevi (fon ışımasının γ²'si, yıldızların toplamı): göz uyumu bu hesaptan, yumuşak. AgX ton eşleme.
- **Kalite:** gren yok, kıpraşma yok; nokta kaynaklar ışığını koruyarak (Ölçek'in noktaları gibi), deterministik.

### 3.2 Yandan 2,5B: evrenin haritası ve hikâye

- **Dünya'nın çerçevesi (10-06, Özcan: "Yalnız 1 olsun"):** evren kendi durgun çerçevesinde, galaksiler gerçek şekilleri
  ve uzaklıklarıyla (`referans/kuasar-galaksi.png` gibi). Kamera roketi izler; ekranın saati yine roketin saati. Yakından
  çevre akar (hız), uzaktan roket zor kıpırdar (ölçek, küçüklük). Bir **harita**dır, fotoğraf değil: tutarlı olmak için
  roket de Dünya'nın ölçtüğü gibi çizilir, hareket yönünde γ kat kısa (%90'da yarı, %99'da 1/7, ötesi ince bir disk).
  (Planın ilk hâlindeki "ölçülen dünya", roketin çerçevesi, H0'da yapılmıştı; H2'de söküldü.)
- **Roket ve manken:** sade, temiz, okunur 2B çizim (vektör üslubu, hafif ışık). Roket bir foton roketi: itişte arkasında
  ince bir ışık huzmesi. Mankenin yüzü yok, oturuyor.
- **Hız hissi:** yıldızlararası ortamın tanecikleri (gaz ve toz; yoğunluk temsili, hız gerçek) roketin yanından geçer.
  Gözün kapağı bir kare (1/60 s): %99,9'da bir karede 5.000 km. 10 m'den bakınca çizgi yağmuru, Dünya boyunda kısa
  çizgiler, AU'da nokta, ışık yılında donmuş. Ölçek motorunun tekerleği aynen: 10 m'den 10²³ m'ye.
- **Hız oku:** roketle akan ince ok, üstünde v (m/s) ve β (Özcan'ın son görüntüsündeki gibi). Durak işaretleri: ad ve
  uzaklık. Yol, iki durak arasında ince ışıklı bir çizgi (referanstaki gibi).
- **Ölçek durakları (10-06):** tekerleğin yanında yumuşak geçişli hazır ölçekler: Roket · Dünya ve Ay · Güneş Sistemi ·
  Yakın yıldızlar · Samanyolu · Yerel Grup (Samanyolu ve Andromeda birlikte: referans görüntü).

### 3.3 Duraklar

1. **Kalkış, Dünya'nın yörüngesi:** Ölçek'in fotogerçekçi Dünya'sı, Ay ve Güneş bugünkü yerlerinde. FPS'de gökyüzünde
   Andromeda soluk bir leke: gideceğin yer en başından görünür.
2. **Proxima Centauri:**
   - Kızıl cüce (3.042 K, 0,154 R☉), Proxima b'nin yörüngesinden **1,69°** (Güneş'in Dünya'dan görünüşünün 3,2 katı).
     Yüzeyinde ışık-gölge (granülasyon, lekeler) ve parlamalar: o bir parlama yıldızı.
   - Proxima b (1,07 M⊕, gelgit kilitli olabilir) temsil: görüntüsü yok.
   - Gökyüzünde Alfa Erboğa A ve B (**12.060 AU**, **−6,8 ve −5,5 kadir**: Venüs'ten parlak; Akeson 2021 paralaksı +
     Gaia DR3 Proxima. Planın ilk hâlindeki 13.000 AU, −6,6/−5,3 eski paralaksla. HYG ikisini 1,3248 pc'ye koyuyor:
     11.360 AU; H5'te gerçek uzaklıklarına taşınır).
   - **Güneş'imiz Kraliçe (Cassiopeia) takımyıldızında 0,4 kadirlik bir yıldız.**
3. **Andromeda:** diskinin 100 bin ışık yılı önünde, galaksiye göre durgun. Disk gökyüzünde **~70°** (büyük eksen 71°;
   77,5° eğik diski bu uzaklıktan perspektifle görmek: küçük eksen 33°, yakın kenar 27°, uzak kenar 5°); arkada Samanyolu
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
   - Harcanan enerji (1 tonluk roket için "Güneş'in X saniyesi"; 21 dokuzda kinetik enerji **Güneş'in 1,46 saati**;
     foton roketinin bunun için yaktığı (e^φ − 1)mc² **2,9 saat**, durmak için de yakarsa **Güneş'in 15 milyon yılı**:
     hangisi gösterildiği yazılır).
   - Foton roketi kütle oranı (hızlanmak için e^φ: 21 dokuzda **4,5 × 10¹⁰**, durmakla 2 × 10²¹).
   - Çekilen ivme (H0'da sabit **12 milyon g**; 10-06'dan hıza bağlı: duruşta 1 g, tavanda **11 milyon g**).
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
  - Yeni: ESO panoraması; Proxima'nın Gaia konumu (bugüne taşınmış, 4,2457 ışık yılı; H0'da var); Alfa Erboğa A/B
    (HYG'de var, uzaklıkları Akeson 2021'e taşınacak); CIE 1931 tabloları (H0'da var, `tools/olcek_veri.py cie`);
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
- **Renk:** 5.772 K'lik bir yıldız D = 2'de 11.544 K'lik kara cisim gibi görünür; fon ışıması önde görünür banda
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
- **H2 · İvme ve yandan harita (10-06'da genişledi):** yeni ivme yasası (fizik, sınamalar, kâhin); yandan Dünya'nın
  çerçevesi (ölçülen dünya sökülür), takip eden kamera, roket ve manken çizimi, motor ışığı, yıldızlararası ortam
  noktaları, tekerlek ve ölçek durakları, yol çizgisi, hız oku, durak işaretleri; kalkışta Dünya ve Ay.
- **H3 · FPS gökyüzü:** göreli ışın izleme, ESO panoraması ve yıldız silme, kozmik fon ışıması, pozlama, enerji gözü,
  gözün optiği. **Bitti (10-06 gecesi)**, ayrıntı DURUM'da.
- **H4 · Galaksiler 3B:** Samanyolu'nun hacimsel modeli (içeriden ve dışarıdan, fotoğrafla örtüşen), M31, M33, 2MRS.
  **Bitti (10-06 gecesi), bir sınırla:** Ölçek'in ince düzlem modelleri gözde (sapınç ve Doppler'le), küçükken nokta;
  2MRS noktaları. Hacimsel Samanyolu yapılmadı (içinden görünüş "zemin" gibi; normal yolculukta 21 dokuzda geçilir).
  Enerji gözü fonun duruştaki parıltısını çıkarır (DURUM H4).
- **H5 · Duraklar:** Dünya'nın yörüngesi, Proxima (yıldız, b, Alfa Erboğa, Güneş'imiz Kraliçe'de), Andromeda'ya varış,
  yörüngeye oturma, dönüş. **Bitti (10-06 gecesi):** gözde gök cisimleri (Dünya, Ay, Güneş; Proxima'nın lekeli
  yüzü ve parlamaları; b), Alfa Erboğa ölçülen yerlerinde, etiketler, bakılan yüze uyum, güneşlik. Yörüngede
  dolanma yok (roket durur). Ayrıntı DURUM'da.
- **H6 · Bütünleme:** bedel panosunun ince ayarı, galeri ve "Ayrıntılar", README (iki dil), GOSTERI/DEMO, autotest,
  tam yolculuk sınaması, performans.

## 8 · Riskler ve dürüst sınırlar

- **Galaksinin içeriden ve dışarıdan gerçekçiliği** en büyük iş. Fotoğrafla modelin örtüşmesi, H4'te ölçülerek.
- **Panodan yıldız silmek:** parlak yıldızlar silinmezse katalogla çift görünür. Silme izi kalmamalı.
- **Dinamik aralık:** önde parlaklık D² ile 2 × 10²¹'e çıkar (D⁴ 4 × 10⁴²: float taşar, çift hassasiyette ya da
  logaritmada hesaplanır); tasarlanmış pozlama ve AgX ile.
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
