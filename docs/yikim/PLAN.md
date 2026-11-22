# PLAN · sahne 3: "Yıkım" (deprem ve betonarme apartmanın çöküşü)

> Tek kaynak plan. 2026-10-01'de Özcan'la konuşularak yazıldı. Durum ve ölçümler `docs/yikim/DURUM.md`'de (açılınca).
> Bağlam sıkıştırılırsa önce bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md` §9-14'ü oku.

## 1 · Görev ve Özcan'ın sözleri

Sahne 1 matematiksel gerçeklik, sahne 2 estetik, sahne 3 **fizik**. Her oturumun dar bir odağı var; bunun odağı
gerçekçi hissettiren bir binanın gerçekçi şekilde yıkılması. Özcan (2026-10-01):

- *"Bu sefer basit ama matematiksel olarak yine de düzgün atmosfer + fizik yapacağız ve odağımız fizik olacak. Bu
  simülasyonun amacı fiziğin sınırlarını zorlamak."*
- *"Önceki 2 oturumun ilkinde matematiksel gerçekliğe odaklandık. 2. oturumda estetiğe odaklandık. Bu oturumda fiziğe
  odaklanacağız. Her oturumun aslında dar alanda odaklandığı bir hedef var. Buradaki hedef de gerçekçi hissettiren bir
  binanın gerçekçi şekilde yıkılması. Fiziğin çok iyi olması. Matematiksel kesin-mutlak doğru değil. Film gerçekçiliği
  tarzında estetik gerçekçilik diyebiliriz."*
- Işık: *"RTX ışıklı normal güneşli hava. Yani özel bir atmosfer kurmana, uzun uzun uğraşmana gerek yok."*
- Fikir aşamasından (hafıza `project_grafik_fikirler.md` (hafıza arşivinde)): mühendislik doğruluğu değil *"makul gerçeklik"*;
  *"gerçekçi görünen dedim, gerçekçi değil [...] gerçekçi hissettiriyordur"*; çıta BeamNG ve Red Faction Guerrilla.
- **Çerçeve (plan sırasında, önemli):** *"Aşırı gerçekçilik için çok araştırma yapmana gerek yok. Ben bunu hobi projesi
  olarak, sen ne kadar iyi fizik yapabilirsin onu görmek için yapıyorum. O nedenle gerçekçi yıkım = iyi yıkım
  olmayabilir. Amaç eğlence. Oyun gibi. [...] Fiziği iyi kur yeterli. [...] Gerekli verileri alıp geliştirmeye odaklan."*
  → Ölçüt: **sağlam, kararlı, eğlenceli fizik**. Mühendislik ayrıntısı ve kaynak taraması yok; sayılar makul
  varsayımlar, gerekirse ayar.

### Özcan'ın kararları (2026-10-01, AskUserQuestion)
| Soru | Karar |
|---|---|
| Ne yıkılıyor | **Betonarme apartman** (4-5 katlı, Türkiye'nin tanıdık yapısı) |
| Nasıl | **Deprem** (gerçek kayıt, şiddet ayarı) **+ kolon / duvar kırma** (fareyle) |
| Çıktı | **Yalnız canlı**, etkileşimli pencere; video ürünü yok (geliştirme sırasında Özcan'a kısa klipler gönderilir) |
| Işık | **Sade güneşli gün**, RTX ışığı; atmosfer basit ama doğru; emek fiziğe |

Varsayılan (Özcan'a söylendi, itiraz gelmedi): sahnede insan yok.

### Faz kararları
| Ne | Karar |
|---|---|
| Y0 kapanışı (2026-10-01) | Özcan: *"fizik yeterince iyi duruyor ya hız ve hissiyat olarak. Devam edebilirsin."* |
| Çatı (Y1) | **Düz çatı + su depoları + güneş kollektörleri**, binaya bağlı değil (kiremit çatı seçilmedi) |
| Y1 kapanışı (2026-10-01) | Özcan: *"Çok iyi duruyor. Devam edebilirsin."* |
| Depremler (2026-10-02) | Özcan: *"Türkiyedeki depremleri kaldır, onun yerine onlardan ilham alarak 3 şiddetinde 4 5 6 7 8 9 şeklinde rihter ölçeğine göre depremleri listele."* → büyüklük 3-9, her biri gerçek bir kaydın dalgasından (`docs/yikim/DURUM.md`) |
| Kamera (2026-10-02) | Özcan: *"fare ve klavye hareketleri ters, onları düz yap yani sağa-sağa, sol-sola gitsin"* → düzeltildi, test `kontrol` |
| Tek uygulama (2026-10-02) | Üç sahne `pc_graphics.exe`'de, ortak arayüzle; `yikim.exe` kalktı (`docs/DURUM.md`) |
| Y2 kapanışı ve sahnenin kapanışı (2026-10-01) | Özcan: *"Tamam. Video yine iyi duruyor. Artık son eklemeleri, düzenlemeleri yapıp tamamlayabilirsin."* Kapanış işi: Y6 canlı pencere (hız), Y3'ten donatı görünümü; Y4 toz ve Y5 görüntü yapılmadı (odak fizikti, ışık kararı "sade güneşli gün"), Y7'nin yerini her fazda Özcan'ın klip yorumları tuttu |

## 2 · Veri

Özcan'ın isteğiyle araştırma yalnız gerekli veriye indirildi: gerçek deprem ivme kayıtları (kaynak ve lisansıyla,
`assets/deprem/fetch.py` ve DURUM'da). Malzeme ve kesit sayıları makul varsayım (beton ~C16, düz donatı, 30×50 kolon,
12 cm döşeme, 3 m kat); eğlence ve kararlılık için ayarlanabilir, `scenes/yikim.cfg`.

## 3 · Hedef: göz neye inanır?

Sahne 2'de soru "göz bir bakışta ne görüyor?" idi. Burada: **göz hangi ipucuyla "bu gerçek" der, hangisiyle "maket" ya
da "oyun" der?** Sıralama, en ucuz bozulandan:

1. **Zaman ölçeği.** 3 m kat duruştan 0,78 sn'de düşer (√(2h/g)); 15 m bina 1,75 sn. Yanlışsa maket (hızlı) ya da su
   altı (yavaş). İlk ölçülecek şey.
2. **Kütle ve rijitlik.** Beton bina jöle gibi sallanmaz, lastik gibi sekmez. Sağlam bina depremde santimetrelerle
   oynar (birinci doğal periyot ~0,5 sn), sonra bir yerden teslim olur. Çözücünün yumuşaklığı en sık görülen oyun izi.
3. **İlerleyen göçme.** Önce çatlak, bir an tutunma, sarkma, sonra hızlanan çöküş. Bir kolon gidince yük komşulara
   geçer; ya taşırlar ya zincir başlar. Hepsi bir anda kopmaz.
4. **Malzemenin kimliği.** Betondan eğri demir çıkar, döşeme parçası donatısına asılı kalır; tuğla dolgu çapraz
   çatlar ve dışarı devrilir; sıva kabuk kabuk dökülür; cam tuzla buz olur; kiremit kayar.
5. **Toz.** Çöküşün asıl görüntüsü. Ağır, yere yayılan bulut; katlar üst üste binerken pencerelerden fışkıran jetler;
   güneşte kendi gölgesi; yavaş çöken kuyruk.
6. **Kuyruk.** Ana çöküşten sonra yuvarlanan parçalar, sallanan sarkık döşeme, ufalanan kenarlar, yavaşça çöken toz.

## 4 · Bina

1990'ların tipik İzmir/Kocaeli apartmanı (sayılar araştırmadan, kaynakla birlikte §2'ye işlenecek):
- Zemin kat dükkânlar: geniş camlı vitrinler, kepenk, az dolgu duvar → **yumuşak kat** (gerçek depremlerde en sık
  göçme biçimi). Üstte 4 normal kat, kat başına 2 daire.
- Betonarme çerçeve: kolonlar, kirişler, döşemeler; zayıf beton (sınıfı araştırmadan), düz donatı, seyrek etriye.
- Tuğla dolgu duvarlar (dış ve iç), sıva ve boya; PVC çerçeveli pencereler; konsol balkonlar ve korkulukları.
- Çatı: düz beton teras, alçak korkuluk duvarı; üstünde binaya bağlı olmayan su depoları ve güneş kollektörleri
  (Özcan'ın seçimi, 2026-10-01).
- Çevre sade: sokak, kaldırım, karşı tarafta basit sabit komşu cepheler (ışık sekmesi ve ölçek için).

## 5 · Teknik yol

Kendi kodumuz (Özcan: "her şeyi sen yap"). Sahne 1 ve 2'ye dokunulmaz: ayrı modül `src/yikim/`, ayrı `yikim.exe`.

**Fizik çekirdeği (ekran kartında, CUDA):**
- **Katı elemanlar + lif arayüzleri** (Uygulamalı Eleman Yöntemi, AEM'den esinli): her yapı elemanı (kolon, kiriş,
  döşeme, dolgu duvar) birkaç katı parçaya bölünür. Komşu parçaların ortak yüzü bir **arayüz**dür; arayüz kesit
  üzerine dağılmış birkaç **lif** taşır (beton + donatı). Her lif normal ve kayma yönünde bir kısıt. Malzeme yasası
  liflerde yaşar: beton çekmede çatlar (sıfıra iner), basınçta ezilir; donatı akar (plastik uzama), kopma uzamasında
  kopar. Böylece moment kapasitesi, plastik mafsal, eksenel yük-moment etkileşimi ayrı ayrı kodlanmadan kesitten çıkar.
- **Çözücü: XPBD, küçük adımlar** (Müller ve diğ. 2020, Macklin 2019): kare başına çok alt adım, rijitlik uyum
  (compliance) ile, alt adımda patlamaz. Plastiklik: kısıt kuvveti kapasiteyi aşınca kırpılır ve dinlenme hâli kayar.
  Arayüzler renklendirilir (graf boyama) ve renk renk Gauss-Seidel ile çözülür (uzun rijit zincirler için).
- **Temas:** her parça dışbükey çokyüzlü (kutu ya da Voronoi hücresi). Bir parçanın yüzey noktaları ötekinin
  düzlemlerine karşı sınanır; geniş faz ızgara. Statik ve dinamik sürtünme, çarpma katsayısı.
- **Zemin:** hareketli kinematik düzlem; depremde kaydın yer değiştirmesini izler, binanın temeli ona bağlı.

**Kırılma görünümü:**
- Mafsal bölgesi (kolon ve kiriş uçları) ezilince önceden hesaplanmış Voronoi kırıntılarına ayrılır; içinden
  **donatı kafesi** görünür: OptiX eğrileri, iki ucun hâlinden bükülmüş, basınçta burkulmuş.
- Kırık yüzeyler düz kesik görünmesin: kırık yüzü gürültüyle kabartılır; iki komşu aynı gürültüyü kullandığı için
  bağlıyken birebir oturur.
- Sıva ve boya ince kabuk; cam kırıkları; kiremit ya da su deposu.

**Toz:** ekran kartında ızgara akışkan (yarı-Lagrange taşıma, girdap koruma, basınç çözümü), düşen parçalar akışkanı
iter (katlar kapanırken hava pencerelerden fışkırır), kırılma, ezilme ve çarpmalar toz salar. Tozun kendi gölgesi ve
sokağa düşen gölgesi.

**Deprem girişi:** AFAD / ESM ivme kayıtları (kaynak ve lisansıyla, §2), taban çizgisi düzeltmesi ve süzgeçle yer
değiştirmeye çevrilir; şiddet çarpanı ayarlanabilir. Bina sönümü %5 civarı (ölçülür).

**Görüntü:** güneş + fiziksel gök (sahne 1'in tek saçılımlı atmosferi, RGB'ye indirgenmiş) + gölge + kısa yol izleme
(dolaylı ışık) + zamansal gürültü giderme. Her parça bir OptiX örneği; dönüşümler çözücüden doğrudan ekran kartında
yazılır, sahne her kare yeniden kurulur.

**Etkileşim:** serbest kamera (WASD + fare), nişangâh; sol tık hedefteki kolonu ya da duvarı kırar. Deprem başlat,
kayıt seç, şiddet artır/azalt, ağır çekim, durdur, baştan kur (R).

## 6 · Ölçümler (fizik testleri, `yikim.exe --test`)

Az ve öz: fiziğin sağlam olduğunu gösteren sayılar. Eşik testten önce yazılır.
1. Serbest düşme: 3 m'den 0,782 sn (analitik). Zaman ölçeği.
2. Kutu kulesi ve piramit: 30 sn'de kayma ve titreme yok (en büyük hız eşiği). Kararlılık.
3. Eğik düzlem: tan θ < μ ise durur, büyükse kayar. Sürtünme.
4. Konsol kiriş: uç sehim ve titreşim frekansı analitiğe yakın. Jöle olmama.
5. Enerji: girdi yokken kinetik enerji kendiliğinden artmaz. Patlama dedektörü.
6. Bina öz ağırlıkla durur, sünmez; kolon kaldırınca çöker; deprem şiddeti artınca hasar artar.
7. Performans: kare başına fizik ve görüntü süresi.

## 7 · Fazlar

| Faz | İş | Özcan'a gösterilen |
|---|---|---|
| Y0 | Fizik çekirdeği: çözücü, lif arayüzü, temas, zemin; testler 1-5; sade görüntü (gri parçalar, güneş gölgesi) | 2 kısa klip: basit çerçevede kolon kaldırma ve sarsıntı; "ağırlık hissi" |
| Y1 | Bina üreticisi: apartman, elemanlar, arayüzler, kapasiteler; kesit görünümü (bağ dayanımı ısı haritası); test 6 | kaba bina, kolon kaldırma klibi; çatı tipi seçimi |
| Y2 | Deprem: kayıtlar, süzgeç, kinematik zemin, sönüm | aynı bina, iki şiddet |
| Y3 | Kırılma görünümü: kırıntılar, donatı, pürüzlü yüzey, sıva, cam, çatı ögeleri | yakın plan kareler |
| Y4 | Toz: akışkan, salım, hava fışkırması, gölge | çöküş klibi |
| Y5 | Görüntü: gök, dolaylı ışık, malzemeler, sokak | son kare adayı |
| Y6 | Canlı: kamera, kırma, ağır çekim, HUD, performans | canlı pencere |
| Y7 | Fizik turları: Özcan'ın gözü ("ağırlık hissi") | |

Tahmin: 8-12 oturum. Her faz sonunda DURUM güncellenir; faz Özcan "tamam" demeden kapanmaz.

## 8 · Çalışma biçimi

- Fizik kararlarının hakemi ölçüm; "ağırlık hissi"nin hakemi Özcan. Ben hareketi göremem: kare dizisi + sayılar;
  Özcan'a kısa klip (`SendUserFile`), görsel seçeneklerde `AskUserQuestion`.
- Fizik dışı (sanat) ayarlar adıyla işaretlenir (kodda ve DURUM'da).
- Commit kuralı: doğrulama ile commit aynı komut satırında durmaz. Commit mesajları Türkçe, oturumun atıf satırlarıyla.
  Sıra: hafıza → `claude_memory` commit/push → proje commit/push (GitHub `pc_graphics`).
- İş özetleri "NE DOĞRULANMADI" ve "Hafıza itildi: evet/hayır" ile biter. Uzun çizgi kullanma.
