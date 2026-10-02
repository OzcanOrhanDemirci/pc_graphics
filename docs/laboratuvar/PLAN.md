# PLAN · sahne 4: "Laboratuvar" (sıvı ve tanecik fiziği, ağır çekim)

> Tek kaynak plan. 2026-10-03 gecesi, Özcan'ın otonom görev emriyle yazıldı (hafızada
> `grafik/project_laboratuvar_gorevi.md`). Durum ve ölçümler `docs/laboratuvar/DURUM.md`'de. Bağlam sıkıştırılırsa önce
> bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md` §9-16'yı oku. Kararları gerekçesiz bozma; bozarsan
> gerekçesini DURUM.md'ye yaz.

## 1 · Görev ve Özcan'ın sözleri (2026-10-03)

Sahne 4 önce "Tanecik" (tanecik fiziği + insan) olarak başladı (T0-T4). Özcan exe'yi denedi: kan 7 mm boncuk, top gibi
sekiyor, kemik kırılmıyor → "olmadı". İnsan yolu (organlar, gerçekçi parçalanma) konuşulunca kapsam şişti; Özcan asıl
merakının parçacık ve akışkan fiziği olduğunu söyledi: insan **fikir olarak kaldı** (kodu `tanecik-insan-arsiv`
etiketinde). Yeni sahne: **ağır çekim laboratuvarı**.

- *"Amaç senin ne kadar iyi işler yapabileceğini, işin sınırlarını görmek. O nedenle özgür bırakmak istiyorum."*
  *"Benim istediğim wow diyebilmek."* Hobi, ileride portfolyo (depo en sonda herkese açılacak: dış veri ancak izin
  verici lisansla).
- *"Olabildiğince gerçekçi, fizik ne derse o şeklinde ve açıkçası görüntü olarak da etkileyici olacak biçimde özgürce
  nasıl istersen öyle yap."* *"gerekirse bu aldığımız kararları bile esnetebilirsin."*
- *"aşırı takıntılı şekilde bir şeye takılma, yeterince iyi yada %90 hedefimiz geçerli."*
- 🔒 *"abartılı değil, fizik ne derse o. Abartıyı silah gücüyle sağlarız [...] Yoksa cıvır ve oyunlaşır. Ben oyun
  yapmıyorum, fizik simülasyonu kuruyorum."*
- 🔒 Boncuk yok: *"boncuklara ayrılırsa komik derecede kötü olur."* Sıvı düzgün yüzeyle (T1'in su klipleri gerçekçiydi,
  çünkü tane görünmüyordu). Katı parçalar kırık yüzeyli parça.
- Karpuz: *"aşama aşama katı-sıvı-akış"*: kabuk ayrılır, kırmızı kısım katıdan sıvıya geçer, sonra akar.
- Bal: cama yapışır, yapıştığı yerden akmaya devam eder, serbest kısım hızlı, yayılıp birikir. Kum: yapışmaz, birbirini
  iteleyerek dolar. Su dökme: *"yüksekten dökünce bardağın dışına da dökülür"*, sıçramaları ağır çekimde ya da
  dondurup görmek. Ölçüt: Yıkım'daki *"bakınca evet ya mantıklı"*, internetteki etkileyici fizik videolarına render
  ile yaklaşmak.

## 2 · Kayıt ve canlı (🔒)

İlke: **ağır çekimde incelenecek hızlı ayrıntı → kayıt; akışın kendisi sahne → canlı.**

- **Kayıt deneyleri** (su dökme, su balonu, karpuz): tetik (dök, iğne, ateş) **kaba dünyada anında canlı oynar**
  (etki-tepki şart: *"e ne oldu şimdi hissi oluşur"*). Aynı girdiler (deneyin ayarları, tetiğin anı ve yeri) saklanır;
  **ince dünya** (daha küçük tanecik) aynı girdilerle arka planda hesaplanır ve sık aralıkla (≥ 1/600 s) kaydedilir.
  Hesaplanan kısım **hemen izlenir** (video yüklenmesi gibi, zaman çubuğunda dolan kısım). Bitince **tam kalitede
  gerçek hızda bir kez** oynar; sonra serbest inceleme: WASD kamera, zaman çubuğu, hız (×1, ×1/10, ×1/100), dondur.
  Oynatma yalnız çizimdir (fizik yok): kaydedilen anlar arası doğrusal ara doldurma. Ekranda kaba önizlemenin kaba
  olduğu yazar.
- **Canlı deneyler** (bal, kum): **ince hesap yok** (*"bahane değil sahnenin kendisi"*), *"performansın yettiği ölçüde
  canlı"*; dondur ve dolaş var.
- 🔒 **Video dışa aktarma yok** (*"amacımıza ters"*). Komut satırının kare aracı yalnız benim doğrulamam içindir.

## 3 · Fizik modeli

Tek çözücü: T1'de kurulan, testli **tanecik çözücüsü** (FleX yolu: PBF sıvı + XPBD bağlar + şekil eşleme + sıvılaşma +
kinematik tanecikler; baraj Martin-Moyce'a RMS 0,069; `particles.*`). Eklenecekler malzemeye göre:

| Malzeme | Model | Ölçülecek |
|---|---|---|
| Su | PBF (yoğunluk kısıtı, diverjans geçişi), XSPH, yüzey gerilimi (Akinci) | hacim, baraj (eski test), sıçrama hissi |
| Bal | PBF + çok yüksek viskozite (alt adım başına XSPH, akış hızına karşı ölçülür), cama yapışma (Akinci yapışması) | ip kıvrılması (Ribe 2004 ölçeklemesi), yayılma |
| Kum | sürtünmeli tanecikler (FleX'in granüler fazı: kendi grubuyla çarpışan, statik/dinamik sürtünmeli), yapışma yok | yığılma açısı 30-35° |
| Lastik (balon) | önceden gerili XPBD zar (dinlenme boyu şişik boyun altında: lastik gerilimi suyun basıncını taşır), yırtılınca kendi gerilimiyle çekilir | patlamada suyun bir an balon şeklinde kalması |
| Karpuz | kabuk: sert, kırılgan bağlı katı (kopunca parçalar); et: zayıf bağlı, sıvılaşan katı (parçalanır, suyuna döner); su: PBF | katı → sıvı → akış sırası |

Kaplar (beher, bardak, tabak, huni) **kinematik tanecik duvarları** (iki kat, sıvının yoğunluğuna Akinci ψ ile girer)
+ görüntüde düzgün cam ağ. Kaba / ince dünya aynı kurulum işlevinden, yalnız tanecik aralığı farklı (kaba 2,5-3 mm,
ince 1,2-1,8 mm; ölçerek seçilir).

**Çözücü kıyası (karar 6):** MPM (MLS-MPM: kum Drucker-Prager, koyu sıvı, kırılan yumuşak katı) ile eşit sürede kıyas
planlandı. Bu gecenin süresi (8 saat, kota) iki çözücüye yetmez; önce elde testli olanla her deneyi kurup kareye
bakılır, bir malzeme PBF'te "yeterince iyi"ye varamazsa o malzeme için MPM denenir. Gerekçe ve sonuç DURUM.md'de.

## 4 · Görüntü

- **Ağır çekim fotoğraf stüdyosu:** koyu, sessiz bir set; deneyin arkasında büyük yumuşak bir ışık paneli (sıvı ve cam
  arkadan aydınlanınca kırılan kenarlarıyla okunur: sıvı fotoğrafçılığının klasik kurulumu), önden-yandan ana ışık,
  yanda şerit ışıklar (parlak kenar). Masa: mat koyu taş.
- **Sıvılar** her karede taneciklerinden yüzey ağı (Yu-Turk anizotropik çekirdek, `surface.*`); su ve bal berrak
  (kırılma, tam Fresnel, yolu boyunca soğurma: bal kehribar), karpuz suyu yarı saydam kırmızı.
- **Kum:** fizik tanesi birkaç mm; gerçek boy ince taneler görüntü katmanı (her fizik tanesine bağlı, onunla dönen küçük
  küreler) + altta yüzey (Özcan onayladı).
- **Katı parçalar** (karpuz kabuğu ve eti, lastik) tanecik değil yüzey: parçanın taneciklerinden kurulan ağ, malzeme
  rengiyle; lastik zarı kendi üçgenleriyle.
- **Cam kaplar:** kalınlıklı gerçek cam (iki yüzey, n = 1,5).
- Canlıda az örnek + birikim + gürültü giderici; durunca birikir.

## 5 · Deneyler (sıra: su → bal → kum → balon → karpuz; ekleme/çıkarma serbest)

1. **Su dökme** (kayıt): beher eğilir, su bardağa dökülür; yükseklik çubuğu; yüksekten dökünce sıçrar, taşar, masada
   birikinti.
2. **Bal** (canlı): kaşık/huniden ince bal ipi cam tabağa; kıvrılır, yığılır, yayılır, kenardan sarkar. Akış ve
   yükseklik çubuğu, fareyle ipi gezdirme.
3. **Kum** (canlı): huniden cam behere ve masaya; birbirini iteleyerek dolar, koni yığılır; çelik bilye bırakma.
4. **Su balonu** (kayıt): asılı su dolu balon, iğneyle tıklanan yerden patlar; lastik çekilir, su bir an balon şeklinde
   kalır, sonra dağılır.
5. **Karpuz** (kayıt): kaidede küçük karpuz, tüfekle (gerçek yükler, `weapons.*`) vurulur: kabuk parçalanır, et kopar,
   suyuna döner, masaya akar.

## 6 · Fazlar

| Faz | İş |
|---|---|
| L0 | Sahne iskeleti: klasör, galeri, set ve ışık, cam kaplar, sıvı yüzeyleri çoklu malzeme; su dökme canlı (kaba) |
| L1 | Kayıt düzeni: girdiler, ince dünya arka planda, kare deposu, oynatma (zaman çubuğu, hız, dondur, WASD) |
| L2 | Bal (viskozite, yapışma) |
| L3 | Kum (granüler faz, ince tane katmanı) |
| L4 | Su balonu, karpuz |
| L5 | Cila: gürültü giderici, galeri resmi, tuşlar, README EN + TR, testler, sabah raporu |

## 7 · B planları

- Bal kıvrılmazsa: kıvrılmayan ama yapışan ve yayılan koyu şurup; dürüstçe yazılır.
- Kum PBF sürtünmesiyle doğru açıyı vermezse: MPM Drucker-Prager denenir; o da yetişmezse açı ölçümüyle birlikte
  yazılır.
- İnce hesap gerçek zamandan çok yavaşsa: kaydın süresi kısaltılır (olayın ilk 1-2 saniyesi), ince tanecik büyür.
- Karpuz yetişmezse: önce su balonu biter (aynı kayıt düzeni), karpuz yarım kalırsa DURUM.md'de.

**Bilerek yok:** insan (fikir), video dışa aktarma, ses.
