> 🗄️ **ARŞİV (2026-10-03).** Sahne 4 "Laboratuvar"a döndü (sıvı ve tanecik fiziği, ağır çekim); insan yolu fikir
> olarak kaldı. Kod `tanecik-insan-arsiv` etiketinde. Güncel sahne: `docs/laboratuvar/`.

# PLAN · sahne 4: "Tanecik" (tanecik fiziği ve insan fiziği)

> Tek kaynak plan. 2026-10-02'de Özcan'la konuşularak yazıldı, Özcan onayladı. Durum ve ölçümler
> `docs/tanecik/DURUM.md`'de. Bağlam sıkıştırılırsa önce bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md`
> §9-16'yı oku (özellikle §15: ekran kartında XPBD'nin ölçülerek bulunmuş kuralları).

## 1 · Görev ve Özcan'ın sözleri

Sahneler bir sıra izliyor: 1 ışığın matematiksel doğruluğu, 2 sanat, 3 katı cisim fiziği (yıkım). Sahne 4 bunun
devamı: **yumuşak cisim ve sıvı**, her şey tanecik. Özcan (2026-10-02), birebir:

- Fikir: *"4.Sahne için de 2.sahnedeki gibi bir devam mantığında sahne düşündüm. Bunun için de parçacık ve
  parçacıklardan oluşan insan fiziği düşündüm. Sahne şöyle: Ortada bir insan var, bina gibi. Elimizde de 5 çeşit silah
  var. Tabanca, hafif hasar. Tüfek tek noktaya yoğun hasar. Pompalı: Yayılan geniş hasar. Bomba: Patlıyor. Kılıç,
  kesiyor. İnsan da çok fazla sayıda noktacıktan oluşacak. Vurulunca gerçek fizikle o noktalar kanı temsil edecek. İnsan
  vücudunun dışı da o noktaları içeride tutan bir deriye sahip olacak. İç organ gibi detaylara hiç gerek yok. Temel
  iskelet sistemi yani kemik olursa fizik daha güzel olur ama aşırı detaylı değil, kolda birkaç kemik bacakda birkaç
  kemik gibi. Yapısal bütünlüğü sağlayan ana kemik yapısı yani. Ragdoll fiziği deniyor sanırım o olacak özünde.
  Parçacık fiziği + ragdoll. Bu işin 1.kısmı. 2.Kısım olarak da aynı parçacık fiziğini dökülme, bir kapta yayılma,
  tsunami gibi serbest dağılma vs. Tanecik fiziğiyle yapılabilecek temel şeyler yani. Bir de bu adamın üzerine küp
  ağırlık düşürme olsun. Top gibi, gücünü biz seçeriz. Çok ağır olunca adam patlayarak ezilir böyle."*
- Odak: *"Zor kısımlarla çok uğraşmayalım. Odaklanmak istediğim şey parçacık fiziği. İnsan bunun için güzel bir araç
  olduğundan seçtim."*
- Denge: *"Ayakta durmayı aşırı gerçekçi yapman şart değil ama mesela tabanca gibi hafif bir darbede mantıklı şekilde
  düşmesi veya düşmemesi lazım. Ondan hani normal insan da böyle yere basar ve dengede durur ya, dengede durmak için
  çaba gösterir. Öyle olursa güzel olur. Aşırı gerçekçi olmasına gerek yok ama mantıklı durmasını tercih ederim."*
- Doku: *"Jöle gibi durmasını pek istemem, daha gerçekçi insan fiziği olmasını tercih ederim."*
- Görünüm ve ton: *"Gerçekçi insan derisi ve yüz ise tamamen önemsiz, bununla hiç uğraşma. Deri renk olarak falan
  gerçekçi olabilir ama yüzü olmasa bile olur [...] Amaç gerçekçi insan yapmak değil hatta çok gerçekçi insan olursa bu
  gore a bile dönebilir hoş değil, amaç adam doğramak değil parçacık fiziğini ve insan fiziğini düzgün yapmak."*
- Ölçek: *"aşırı çok sudan ziyade parçacık sayısının çok fazla olup o performansı miktara değil, detaya ve fiziğe
  harcamayı tercih ederim."*
- Cam görünüm: *"Ben jöle adam istemiyorum dedim ama içindeki sıvı görünür fikri mantıklı geldi. Amaç parçacık fiziği
  olduğu için jöle adam mantıklı gibi."* Sonra: *"Görünümden emin değilim ama parçacık fiziğini en iyi bu verir dediğin
  için [...] taslak yaparsın ben test ederim, beğenmezsek değiştiririz."*
- Silahlar: *"Silah gücü uygun, biz seçelim ama gerçekçi momentumda kalsın. Yani en güçlü tabanca desert eagle tarzı
  olsun. Gidip roket atar gibi de uçurmasın. En güçlü tüfek çok güçlü tüfekler gibi olsun. En güçlü pompalı da aynı
  mantık. Gerçekçi sınırlar içinde en güçlü-güçsüz ayarı olsun. Yoksa tabanca bomba gibi olmasın yani en son
  seviyede."*
- Malzemeler: *"Su/kan (kandan kastım insan içindeki sıvı anlamında). Kum, bal gibi koyu sıvı. Yeterli olur."*
- El aracı: *"Olur el de ekleyelim."*
- Gore'un anlamı (T0'dan sonra): *"Gore derken ben onu çok gerçekçi beden yaparsak diye dedim. Kanla dolu olması güzel
  olur. [...] Gerçekte de kanla dolar çünkü. Benim gore dememin nedeni hypergerçekçi bir yüze ve vücuda sahip bir şeye
  bunları yapmak doğru olmaz dedim. Burada adam manken zaten."* → beden kanla dolu çizilir.
- Onay ve çerçeve: *"Aynen zor kısımları optimize etmekte özgürsün illa şöyle böyle olacak değil, yeterince iyi
  seviyedeyse okey deriz. Onaylıyorum. Başlayabilirsin."*

→ Ölçüt: **tanecik fiziği ve insan fiziği düzgün; davranış mantıklı; görüntü sade.** Zor kısımlar "yeterince iyi"
olunca kapanır; her birinin B planı aşağıda.

## 2 · Fizik modeli

Tek çözücüde her şey tanecik (NVIDIA FleX'in yolu: Macklin, Müller, Chentanez, Kim 2014, "Unified Particle Physics for
Real-Time Applications"; sıvı: Macklin ve Müller 2013, "Position Based Fluids"; katılık için çok alt adım: Macklin vd.
2019, "Small Steps in Physics Simulation"). Ekran kartında, Yıkım'ın XPBD deneyimiyle (hafıza §15: kapasite toplam
çarpana, sabit noktalı toplamlarla tekrarlanabilirlik, momentumu koruyan Jacobi ortalaması, yük yavaş verilir).

- **Tanecik boyu tek:** bütün tanecikler aynı yarıçapta (5-7 mm); T1'de ölçerek seçilir.
- **Beden (~1,78 m, 70 kg, ~70 L):** 7 mm aralıkla ~200 bin, 5 mm aralıkla ~560 bin tanecik.
  - **Kemik:** 17 parça: kafa, boyun, göğüs (kaburgalarla), bel, leğen; kolda üst kol, ön kol, el; bacakta uyluk,
    baldır, ayak. Katı (tanecik kümesi ya da katı cisim, T2'de seçilir); eklemler hareket sınırlı (diz tek yöne);
    yeterli darbede kırılır, kılıç keser.
  - **Doku:** bedeni dolduran tanecikler; komşularına ve en yakın kemiğe bağlı (kopabilen bağlar). Yalnız aynı ya da
    eklemle komşu kemiğin tanecikleri birbirine bağlanır (iki uyluk birbirine yapışmaz).
  - **İç sıvı ("kan"):** bağı kopan tanecik serbest kalır ve sıvı gibi akar (sudan ~3-4 kat koyu). Yaranın çevresindeki
    bağlar bir süre yavaşça zayıflar: yara sızar.
  - **Deri:** en dış tanecik katmanı, örülü ince bir zar; gerilince yırtılır.
  - **Sertlik:** jöle değil, gerçek doku. Önce bir **balistik jel** bloğunda (insan dokusunun standart taklidi) merminin
    açtığı geçici boşlukla kalibre edilir, sonra bedene konur. Karın ve uylukta yalnız küçük ikincil sallantı.
- **Denge (çaba gösteren, yürümeyen adam):**
  - Kaslar duruşu tutar (eklemlerde gücü sınırlı motorlar), küçük ve sınırlı görünmez bir yardım.
  - Küçük itki: ayak bileği ve kalçadan salınır, toparlanır. Orta itki: düşmemek için basması gereken noktayı hesaplar
    (yakalama noktası, Pratt vd. 2006), bir-iki toparlanma adımı atar. Büyük itki: düşer, kolları düştüğü yöne uzanır,
    kasları yavaşça gevşer. Yürüme yok.
  - Hasar mantığı: bir uzvun kas gücü o uzuvdaki sağlam doku ve kemik oranına bağlı (kemiği kırılan bacak taşımaz);
    gövdede, kafada ya da omurgada ağır hasar bütün kasları gevşetir, adam yığılır. Gerçekte de insanlar vurulunca bu
    yüzden düşer, merminin itkisiyle değil.
- **Sıvılar ve kum (kısım 2):** aynı tanecikler, bağsız. Su (yoğunluk kısıtı, yüzey gerilimi, ıslatma, viskozite),
  iç sıvı, kum (sürtünmeli tanecik), bal gibi koyu sıvı. Yoğunluğa göre kendiliğinden katmanlaşma, kaldırma kuvveti.
  Ölçek tezgâh/akvaryum (deniz yok: tanecik boyu ölçeği belirler, 2 cm aralıkla 1 milyon tanecik yalnız ~8 m³).

## 3 · Araçlar

Her silahın güç çubuğu var, **gerçekçi sınırlar içinde**; çentik yaygın kalibre. Sayılar üretici yük tablolarının tipik
değerleri, yaklaşık; T4'te teyit edilir. Son sütun: mermi bütün momentumunu 70 kg'a bıraksa verilecek hız (üst sınır).

| Araç | En hafif | Çentik | En güçlü | En güçlüde 70 kg'a en çok |
|---|---|---|---|---|
| Tabanca | .22 LR (2,6 g, ~330 m/s) | 9 mm (8 g, ~360 m/s) | .50 AE, Desert Eagle (19 g, ~450 m/s) | 0,12 m/s |
| Tüfek | 5,56 mm (4 g, ~940 m/s) | 7,62×51 (9,5 g, ~840 m/s) | .50 BMG (42 g, ~900 m/s) | 0,54 m/s |
| Pompalı | .410 (14 g, ~370 m/s) | 12 kalibre, 9 saçma (31 g, ~400 m/s) | 12 kalibre magnum, 18 saçma (60 g, ~370 m/s) | 0,31 m/s |

- Merminin yolu bir ışın: dokuda sürüklenmeyle enerji kaybeder; kaybettiği momentum yolun çevresindeki taneciklere geçer,
  ince tüpte bağlar kopar; giriş ve çıkış deliği, geçici boşluk kendiliğinden. Işın olduğu için hiçbir hız bedenden
  "atlamaz". Pompalı: koni içinde saçmalar.
- **Bomba:** tıklanan yere düşer, kısa fitilden sonra patlar. 50 g'dan 2 kg TNT eşdeğerine; çentik el bombası (~180 g
  patlayıcı). Basınç dalgası uzaklık ve kütleyle gerçek ölçekleme tablolarından. Şarapnel yok.
- **Kılıç:** fareyle ekrana çizilen çizgi; vuruş hızı fare hızından, üst sınır usta bir vuruş. Çizgiden geçen düzlemde
  bağlar ve kemikler kesilir; kemiği ancak güçlü vuruş keser.
- **Küp:** 10 kg - 5 t ağırlık çubuğu, adamın üstüne düşer. Ezilme fizikten çıkar (doku sıkışır, bağlar kopar, sıvı
  dışarı iter, deri yırtılır).
- **El:** fareyle tut, it, çek, sürükle; kuvveti gerçek bir el gibi sınırlı. Dengeyi sınamanın aracı.
- **Genel:** R yeni adam · ağır çekim · dondur-dolaş (zaman durur, kamera döner) · cam / deri görünümü.
- **Malzeme kipi (kısım 2):** su, iç sıvı, kum, koyu sıvı · fareyle dök · cam akvaryum · baraj kapağı (tezgâh
  ölçeğinde tsunami) · küp at (yüzer ya da batar) · adam bu kipte de durabilir, üstüne az miktar dökülür.

## 4 · Görünüm (T0 taslağı, Özcan sınar)

- Beyaz sonsuz stüdyo fonu (yumuşak köşeli zemin-duvar), stüdyo ışığı.
- **Cam insan:** saydam, ten rengi ince bir deri (müzelerdeki şeffaf anatomi modelleri gibi); içinde fildişi kemikler ve
  tanecikler. Fizik saydam değil, gerçek doku sertliğinde; saydam olan yalnız görüntü. Darbede yüklenen taneciklerin
  parlaması bir **görselleştirmedir** (fizik değil), öyle etiketlenir. Bağı kopan tanecik koyu kırmızı sıvı.
- **Opak deri** tek tuşla: gerçekçi ten rengi, yüzsüz pürüzsüz manken başı.
- **Zemin tuval olur:** yere oturan sıvı zemine leke olarak işlenir, fizikten çıkar (performans + zamanla biriken resim).
- Gore'dan uzak: kesik yeri et değil, kırılmış bir cam figür ve dökülen sıvı gibi görünür.

## 5 · Ölçülecekler

| Konu | Ölçü |
|---|---|
| Zaman ölçeği | Serbest düşüş süresi (√(2h/g)) |
| Su | Baraj yıkılmasında cephe hızı, Martin ve Moyce (1952) deneyiyle kıyas |
| Kum | Yığılma açısı ~30-35° |
| Doku | Balistik jelde geçici boşluk (ağır çekim kayıtları ve yayınlar) |
| Sıvı | Hacim korunumu (yoğunluk sapması) |
| Silahlar | Adama geçen momentum mermininkini aşmıyor (korunum) |
| Denge | Aynı noktaya artan itkiler: salınma / adım / düşme eşikleri; hakem Özcan |
| Bütçe | 5 / 6 / 7 mm'de tanecik sayısı ve adım süresi |

## 6 · Fazlar

| Faz | İş | Çıktı |
|---|---|---|
| T0 | Görünüm: sahne modülü, stüdyo, manken (işaretli uzaklık alanı), tanecik dolgusu, kemikler; cam ve deri | Özcan'a kareler |
| T1 | Tanecik çekirdeği: komşu ızgarası, sıvı yoğunluk kısıtı, XPBD bağları, alt adım; sıvı yüzeyi çizimi; su balonu (zar + sıvı: vur, sızdır, küp düşür, patlat); baraj yıkılması ölçümü; bütçe ölçümü | Klip + ölçümler |
| T2 | Beden: kemikler (eklem sınırlı, kırılır), doku bağları, deri zarı; jel bloğu kalibrasyonu; gevşek düşüş | Klip |
| T3 | Denge ve El | Klip + itki merdiveni |
| T4 | Silahlar, bomba, kılıç, küp; ağır çekim, dondur-dolaş, zemin tuvali | Klip |
| T5 | Malzeme kipi | Klip + ölçümler |
| T6 | Cila, galeri, README, kapanış | Sahne biter |

## 7 · B planları (zor kısımlar, "yeterince iyi" olunca kapanır)

- Toparlanma adımı tutmazsa adam adım atmadan salınır ya da düşer.
- Bal kıvamı çok zor çıkarsa şurup kıvamı.
- Cam görünüm jöle gibi okunursa varsayılan opak deri olur.
- Kum taneleri 5-7 mm'de çakıl gibi görünür: kum bir yüzey olarak çizilir, ince taneler yalnız görüntüde.

**Bilerek yok:** yürüme, iç organ, yüz ayrıntısı, deniz ölçeğinde su, şarapnel, ses.
