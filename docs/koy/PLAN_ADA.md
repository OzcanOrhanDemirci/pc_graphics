# PLAN · sahne 5, üçüncü bölüm: "Ada" (iskele, amforalar, batık ve maviye inen halat)

> 🏁 **Bitti (2026-10-04 akşamı): B0-B6 yapıldı.** Sonuç, ölçüler ve NE DOĞRULANMADI `docs/koy/DURUM.md` başında.
> Yerleşim uygulamada plandan şu kadar ayrıldı: iskele ucu alçak beton T başlığı (Özcan'ın iskele altı fotoğrafı),
> şamandıra z ≈ −160 (zemin ~47 m), duba hatları z = +24 → −160.

> Tek kaynak plan. 2026-10-04 öğleden sonra Özcan'la konuşularak kesinleşti; uygulama **tam otonom**. Koy'un K0-K1'i, üç
> ara durağı ve Amfora'nın A0-A7'si bitti (durum ve ölçümler `docs/koy/DURUM.md`). Mimari, veri ve ölçü düzeneği için
> `PLAN.md` ve `PLAN_AMFORA.md` başvuru olarak durur. Bağlam sıkıştırılırsa önce bu dosyayı, sonra DURUM.md'nin başını,
> sonra hafızada `genel/feedback_gpu_ve_grafik.md` §13, §19-§23'ü oku.

## 1 · Görev, yetki ve Özcan'ın sözleri

Amfora'dan sonra Özcan (2026-10-04 öğlen): *"amfora beni tatmin etmedi [...] beklentim daha yüksekti"*, ama: *"Netlik ve
stabilite konusunda çok iyi iş çıkardın. Emek boşa gitmedi. Pürüzsüze yakın bir görüntü [...] god of war gibi ps5
oyunlarında gördüğüm bir netlik. Yani o konu çözüldü. [...] bunun üzerine inşa edelim."*

Tarifi (birebir özet): küçük bir ada (detaylı değil: amaç kara değil), **iskele**: ahşap, ayakları beton, ayakların etrafı
midye ve yosun. İskeleden suya, kum var, çok alçak. İskele yönünde iki yanda **dubalar**: görüntüyü tek yere odaklayan
yatay sınır, mantıklı bir sebebi olsun diye. Adanın etrafı ve duba dışı her yer **kum, sabit, yavaş yavaş doğal şekilde
hep derinleşiyor**; bir noktada ışık fizik olarak inmediği için zemin görünmüyor, **mutlak mavi yücelik** doğal oluşuyor,
aşağı dalınca görünür. Daha derine inmeye çalışınca **basınçtan mantıklı bir şeyle ölelim, sahne sıfırlansın**: *"Oyun gibi
aslında ama oyunlaşmasını istemiyorum, bunu gerçek fizikle mantıklı yaparsan süper olur."* Koridorda sırasıyla: **sığ kum**
(deniz taşları, deniz yıldızı gibi ufak süslemeler, rastgele, her yerde olabilir, seyrek) → biraz derinde **amforalar,
taşların arasında birikmiş** (mevcut tasarım ve kemerin taşı), öncesinde ve sonrasında tek tük amfora → ileride **ahşap
yelkenli batık, Kara İnci tarzı korsan gemisi, biraz büyük, içine girilebilir** → en ileride dubalar dikeyi de kapsıyor,
ortada bir **şamandıra**, ipi derinlere uzanıyor, **halatın üzerinde midyeler**, halat takip edilebilir; burası dikey sınır,
ötesine geçilebilir ama içerik yok. *"Minimum fizik yine, amaç netlik ve stabilite, o sahne odağı devam ediyor ama tutarlı
bir dünyada içerik var."*

Sonra (karar): *"Ada ve su üstü sadece suyun altından görünsün. İskelenin altını gezebilelim. [...] suyun üstüne çıkmak
yok. O hesaplama işlerine girmeyip dar alanda kalalım. Her sahnede dar alanda en iyi sonuca odaklandık hedef kayması
olmasın. Suyun yüzeyine çıkış yok, boğulunca suyun içinde iskele merdiveninde başlar. [...] Yarı yıkık batık uygun, biraz
büyük olsun yani bayağı keşfedilebilir bir şey olsun. Ayrıca netlik ve stabil görüntü hedefini ve gerçek zamanlı ışık
hedeflerini iyi zorlayacak bir şey batık. Full otonom git. [...] Paralel agent kullanma her şeyi kendin yap."*

Ve hedef: *"Şimdi altın saat + fener yapıyoruz aslında. Fenerde full sanat yapmıştık. Altın saat de full teknik. [...]
Fenerle altın saatin güçlerini birleştirmek. Yani teknik + sanat. Sanat kısmını da bu plan sağlayacak çünkü tekniği
yaptık. [...] Görüntüye bakınca gönlüm açılsın istiyorum, etkilenmek istiyorum. [...] Tamamen özgürsün. [...] o sonuca
giden yolda her şey serbest."*

Konuşmada sunduğum ve itiraz edilmeyen kararlar: ölüm = havadaki oksijenin kısmi basıncı (§6); derinlik sarhoşluğu
çizilmez; hava bitmez; zemin eğimi kıyıda birkaç dereceden başlayıp derinde 20-25°'ye çıkar (kumun ~32°'lik durma açısının
altında); şamandıra halatı dipteki bir beton bloğa; balık yok.

## 2 · Öncelik sırası (çatışırsa)

1. **Göz bir bakışta ne görüyor?** Her istasyonun bir kahraman görüntüsü (§3) ve onu taşıyan ışık, renk, kompozisyon.
   Amfora'nın dersi: ölçüler tutup göz etkilenmediyse iş bitmemiştir ([[gpu-ve-grafik]] §13).
2. **Netlik ve kararlılık bir ALT SINIR:** sabit örnekleme, zamansal tahmin yok, bugünkü düzey korunur (Fener'in yarısından
   iyi). Yeni ölçüm yalnız yeni zorluklarda (iskele altı çizgileri, batığın içi, halat): ölçü bütçeyi yutmaz.
3. **Gerçek zamanlı ışık:** güneşin gölgesi, iskele aralıklarından ve batığın deliklerinden giren ışık hüzmeleri, dalgayla
   salınan çizgiler; hepsi fizikten.
4. **Fizik:** ışık, derinlik ve basınç gerçek; sanat için sapılan her yer kodda ve DURUM'da adıyla işaretlenir.
5. **Kare hızı:** varsayılan ayarlarla 30'un altına inilmez; hedef 41.

## 3 · Sanat yönetimi: istasyonlar ve kahraman görüntüler

**Güneş adanın üstünden** (dalgıç açığa bakarken güneş arkada ve biraz yanda): koridora bakınca her şey önden-yandan
aydınlık (amforaların ve batığın biçimi, renkleri), ışık sütunları ileride derinlikte birleşir; dönüp iskeleye bakınca
güneşe karşı: iskele ayakları siluet, tahta aralıklarından ışık perdeleri, Snell penceresinde iskelenin kırılmış görüntüsü.

| İstasyon | Derinlik | Göz bir bakışta ne görüyor |
|---|---|---|
| **İskele ve ada** | 0,5-4 m | Güneşe karşı: midyeli beton ayaklar siluet, tahta aralıklarından dalgayla salınan ışık perdeleri, kumda dans eden kostik ağı, adanın yosunlu kayaları. Renk: turkuaz su, açık bej kum, sıcak |
| **Sığ kum** | 4-9 m | Bembeyaz kumda keskin kostik ağı, dalga izleri; seyrek kırmızı deniz yıldızları, taşlar, deniz hıyarı, yelpaze midyesi kabukları; iki yanda duba hatları yüzeyde ve dibe inen ipleri, aradaki yol ileride maviye gömülüyor |
| **Amforalar** | ~12 m | Kayaların arasına birikmiş amfora yığını, çevresinde tek tük amforalar; yeşilimsi mavi; ileride sisin içinde bir geminin kıç kasarası beliriyor |
| **Batık** | 18-26 m | Maviden çıkan yarı yıkık bir kalyon: yüksek kıç kasarası, top lumbaraları, kırık direkler; çökmüş yanında kaburgalar açıkta. İçinde lumbaralardan ve güverte deliklerinden giren ışık hüzmeleri, toplar, karanlık |
| **Şamandıra ve halat** | 0 → 55 m | Yüzeyde parlak bir şamandıra, midyeli halatı dümdüz aşağı, mutlak maviye; ışınlar derinlikte birleşiyor; dalınca 35-40 m'de dip beliriyor, halat bir beton bloğa bağlı; ötesinde kum karanlığa iniyor |

Sırayla görünürlük: suyun görüş mesafesi ~40 m; istasyonlar arası ~40-60 m. Hiçbir istasyon ötekini ezmez, her biri bir
öncekinden ayrılırken sisten çıkar (yolculuk hissi).

## 4 · Yerleşim (metre; z kıyıdan açığa doğru eksi, x yan)

| Öğe | Yer |
|---|---|
| Ada | merkezi (0, +60), su hattında ~35 m yarıçap, ~12 m yüksek kaya; su altında kayalık etek 2-4 m'ye kadar, sonra kum |
| İskele | adanın kuzey kıyısından açığa, x ∈ [−1,5, +1,5], z ∈ [0, +28]; güverte suyun 1,3 m üstü; beton ayaklar 2 sıra, 3 m arayla; merdiven ucun sağında |
| Başlangıç | merdivenin dibinde, ~1,2 m derinlikte, açığa (−z) bakış |
| Duba hatları | x = ±15, z = +24 → −150; açık uçta (z = −150) iki hattı birleştiren hat ve ortada büyük şamandıra |
| Sığ kum | z 0 → −35, 3,5 → 8 m |
| Amforalar | merkez z ≈ −50, ~12 m; kayalar arasında yığın, ±15 m tek tük |
| Batık | merkez z ≈ −95, zemin ~22 m; ekseni koridora ~35° açılı, kıç kasarası kıyıya (yamaç yukarı), baş açığa |
| Şamandıra | (0, 0, −150); altında zemin ~55 m, halat beton bloğa |
| Ötesi | kum eğimi sürer: 66 m ~z −175, 100 m ~z −250 |

**Zemin profili:** koridor ekseni boyunca düzgün bir eğri (birinci türevi sürekli), istasyonlarda hafif düzlükler, aradaki
eğim en çok ~25°; ada çevresinde kıyıya doğru yükselir. Koridor dışı da aynı eğri (x'e bağlı hafif dalgalanma).
Kuşbakışı harita ürünle aynı kaynaktan (`--map`), sayılar oradan doğrulanır.

## 5 · Teknik işler

1. **Büyük dünya:** zemin ağı koridorda ince (8-10 cm), dışarıda büyüyen; dip haritası dikdörtgen; dalgacık normal haritası
   karolanır ve derinlikle söner (dalga izi derinde yok).
2. **Işık alanı 120 m'ye:** `AMB_ZMAX` 40 → 120, alan sayısı 4 → 8 (dip derinlikleri istasyonlara göre); kostik 0,5-30 m,
   ötesinde odaklama 1'e söner (güneş diskinin bulanıklığı zaten siler).
3. **Su üstü yalnız alttan:** Snell penceresinden çıkan ışın havada izlenir: iskele güvertesi, ayakların su üstü, ada, şamandıralar. Havada gölgelendirme (güneş + gök + sudan yansıyan ışık), `pathTab`'a hava satırları.
4. **Güneş gölgesi:** yüzeyler gölge ışınını yüzeyden sonra havada sürdürür (iskele, ada gölge düşürür); ışının yönü
   yüzeyin o noktadaki dalga eğimiyle kırılır (gölge çizgileri dalgayla salınır). Işık sütunları için 3B güneş kutusu
   yerine **güneş derinlik haritaları** (iskele 5 mm, batık 2 cm): her adımda kırık güneş yönünde ilk engelin uzaklığı,
   2×2 süzme, aynı dalga salınımı.
5. **Örnekleme ve ağlar:** `KOY_MAX_MESHES` 32 → 128; küçük öğeler (midye, taş, deniz yıldızı) örnekleme; ortam kararması
   ağ başına menzille (batığın içi için uzun menzil).
6. **Basınç:** derinlik, basınç, ppO₂ panelde (dalış bilgisayarı); ppO₂ ≥ 1,6 bar'da belirtiler, birkaç saniyede kasılma,
   kararma, merdivende yeniden başlama (§6).

## 6 · Basınç ve ölüm (fizik)

Deniz suyunda basınç P = 1,013 + 0,1005 · d bar (ρg = 1025 · 9,81). Tüplü dalgıcın havasında oksijen %20,95: ppO₂ = 0,2095 · P.
- 1,4 bar (~57 m): rekreasyonel dalışın kabul ettiği üst sınır.
- **1,6 bar (~66 m): merkezi sinir sistemi oksijen zehirlenmesi.** Gerçekte belirtiler (görüşün tünelleşmesi, kas seğirmesi)
  kasılmaya ve bilinç kaybına gider; regülatör ağızdan çıkar, dalgıç boğulur. Başlangıç süresi kişiye ve süreye bağlı
  (NOAA sınırı 1,6 bar'da 45 dk); burada **ppO₂ 1,6'yı aşınca birikimli bir doz** sayılır, hızı ppO₂ ile üstel artar
  (1,6'da ~8 sn, 1,8'de ~3 sn): belirtiler doza göre görünür, sığa çıkınca doz söner, dolunca kararma ve merdiven.
  Sanat sapması (adıyla): gerçek gecikme dakikalar olabilirdi; süre sahne için kısaltıldı.
- Derinlik sarhoşluğu (30 m+) zihinsel bir etki, görüntüyü bozmaz: çizilmez. Hava bitmez (tek bilerek kolaylık).

## 7 · Fazlar

| Faz | İş | Sonu |
|---|---|---|
| B0 ✅ | Bu plan, ayar dosyası, `--map` yeni yerleşimle | harita |
| B1 ✅ | Büyük dünya: zemin profili ve ağ, dip haritası, ışık alanı 120 m, kostik derinliği, dalgacıklar, başlangıç ve tavan, basınç ve ölüm, panel | koridordan sığ → mutlak mavi kareleri |
| B2 ✅ | Ada (su altı kayalık etek + su üstü kaya), iskele (beton ayaklar, kirişler, güverte, merdiven), havada izleme, güneş derinlik haritası ve dalga salınımı | iskele altı kahraman karesi |
| B3 ✅ | Midye ve yosun (ayaklar, halat), duba hatları (yüzen şamandıralar dalgayla iner kalkar, dibe inen ipler, bloklar), büyük şamandıra, halat ve blok | halat karesi |
| B4 ✅ | Sığ kum süsleri (taş, deniz yıldızı, kabuk, deniz hıyarı, kestane), amforaların yeni yeri kayalar arasında, tek tük amforalar | sığ ve amfora kareleri |
| B5 ✅ | Batık: gövde çizgileri, postalar, kaplama, güverteler, kıç kasarası ve pencereler, baş kasarası, direkler, toplar, çapa, dümen; hasar; ahşap ve demir malzeme, kabuk; içerde ışık | batık kareleri (dış, iç) |
| B6 ✅ | Sanat turu (her istasyon, güneş, pozlama, renk, başlangıç bakışı), kararlılık ve hız ölçüleri, galeri adı ve resmi, README, GOSTERI, DURUM, kendi kod gözden geçirmem | Özcan'ın bakacağı yer |

Her fazın sonunda kareler bakılarak eleştirilir (taze gözle: "göz ne görüyor, gerçek bir fotoğrafla yan yana durur mu"),
commit + push. Kare göstermeden büyük öğe bitirilmez (kemer dersi): Özcan yokken bu kural kendi gözüm için geçerli, her
büyük öğeden önce kaba bir kare, sonra ayrıntı.

## 8 · Otonom kurallar

- Özcan'a soru yok; kararlar bende, gerekçesiyle DURUM'a. **Ajan yok** (ne paralel ne sıralı).
- Kota: ~30 dk'da bir ve büyük işten önce oku (`~/.claude/kota-durumu.json`). 5 saatlik ≥ %93 → adımı bitir, commit + push
  (hafıza → proje), CronCreate tek seferlik `resets_at` + 3 dk, istem: *"grafik devam: 5 saatlik kota yenilendi,
  docs/koy/PLAN_ADA.md ve DURUM.md 'Şu an'dan devam"*; turu bitir. Haftalık ≥ %97 ise aynısı haftalık sıfırlanmaya.
- ~30-60 dk'da commit + push; DURUM "Şu an" her commit'te güncel; NE DOĞRULANMADI.
- Bitince: DURUM başına "Özcan'ın bakacağı yer", hasat (kalıcı dersler hafızaya), son commit, hafızada görevi 🏁 işaretle.

## 9 · Kaynaklar (eklendikçe)

- Basınç ve oksijen: NOAA Diving Manual (CNS oksijen sınırları, 1,6 bar'da 45 dk); ppO₂ = F_O2 · P.
- Kalyon biçimi: 17. yy savaş gemisi oranları (Vasa 1628: boy 47,5 m, en 11,7 m; tipik kalyon boy/en 3,5-4), tumblehome,
  kıç kasarası; ayrıntı B5'te `docs/koy/BATIK_KAYNAK.md`.
- Akdeniz sığ dip canlıları: Echinaster sepositus (kırmızı deniz yıldızı), Holothuria tubulosa (deniz hıyarı), Pinna
  nobilis (yelpaze midyesi), Paracentrotus lividus (mor deniz kestanesi), Mytilus galloprovincialis (Akdeniz midyesi).
