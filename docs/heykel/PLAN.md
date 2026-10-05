# PLAN · sahne 6: "Heykel" (çalışma adı; geometri)

> Tek kaynak plan. 2026-10-05 gecesi Özcan'la bir konuşmayla yazıldı; konuşmanın tamamı hafızada
> `grafik/project_grafik_fikirler.md` sonundaki "Sahne görev çerçevesi" ve ardından gelen bloklarda. Durum ve ölçümler
> `docs/heykel/DURUM.md`'de. Bağlam sıkıştırılırsa önce bu dosyayı, sonra hafızada `genel/feedback_gpu_ve_grafik.md`
> §6 (SDF), §13 (teknik ≠ görsel), §19 ve §21'i (kararlılık, netlik) oku.
>
> **2026-10-05 sabahı: G0-G4 otonom görevde yapıldı** (Özcan seçimi bana bıraktı: Katedral). Plandan ayrılanlar ve
> ölçümler `DURUM.md`'nin başında: katlar formülle değil yer seçimi ve ışıkla; dalışlar her yerde 10.000 kat (düz kesik
> yüzlerde 30); 10.000 katta ekranın genişliği 0,15 mm (ilk görünüm planın varsaydığından geniş); porselen geçirgenliği
> ve son oda bilerek yok.

## 1 · Görev ve Özcan'ın sözleri

Her sahnenin bir görevi var: **tek dar odakta en iyisi olmak.** 1 Altın Saat doğru ışık, 2 Fener gerçek zamanlı ışık ve
sanat yönetimiyle dünya, 3 Yıkım yıkım fiziği, 4 Laboratuvar tanecik fiziği, 5 Ada zor bir ortamda saf netlik ve
kararlılık. **6'nın görevi geometri:** ayrıntının dokuda değil gerçek geometride olduğu, yakınlaştıkça bitmeyen bir yüzey.

- *"Geometriyi çevresiz saf odağa alsak. Tüm amaç geometri. Boş sahne, ortada geometriyi yapacağımız şey var. Serbest
  değil sabit kamera. Sadece yakınlaşma uzaklaşma var. Hareket ve fizik hiç yok sıfır. [...] Bütün hesaplama gücünü
  tamamen geometrik detay ve ona bağlı şeylere ayırıyoruz. Dar alanda en yüksek kalite hedefi."*
- *"Adobe flash döneminden kalma veya windows media player ses görüntüsü tarzında olmasını istemem. Gerçekten 3D olarak
  muazzam olmalı."*
- *"Amaç geometrik olarak şok olmak ve etkilenmek."* · *"Kararları verdik ancak kararları değiştirmekte de özgürsün."*

### Kararlar (Özcan ya da onun bıraktığı yetkiyle ben, 2026-10-05)
| Konu | Karar |
|---|---|
| Nesne | **Fraktal, müzede duran imkânsız bir heykel gibi.** Tek gerçek malzeme, renk yalnız ışıktan |
| Malzeme | Beyaz mat (alçı ya da sırsız porselen). Porselenin ince duvardan ışık geçirmesi G3'te A/B |
| Derinlik | **10.000 kat**, sonsuz değil. Her yerde ~100 kat serbest, seçilmiş 3-5 noktada tasarlanmış dalış |
| Kamera | Sabit; yalnız yakınlaşma (imlecin gösterdiği noktaya), kaide elle döner (sürükle, bırakınca netleşir) |
| Işık | **İmlecin baktığı yere tıklayınca ışık orayı aydınlatır**; sürükleyince o noktanın çevresinde döner |
| Hareket, fizik | Sıfır. Sahne durağan: durunca görüntü birikerek referans kalitesine yakınsar |
| Gösterge | Yok (Ada kuralı). Dalış girişleri ışık ve açıklıkla davet eder. Panel bilgi için kalır |
| Netlik | Ada'nın çıtası taban: kıpraşma yok, gürültü yok |
| Yasak | Gökkuşağı renk, abartılı parlama, şekil değiştiren animasyon, nabız, siyah üstüne neon, ortalanmış kaleydoskop |

## 2 · Görsel hedef: göz bir bakışta ne görüyor

Koyu bir boşlukta, bir kaidenin üstünde beyaz, mat, 40 cm'lik bir heykel. Tek güçlü ışık onu yandan, yatık açıyla
yakalar; gölgeler derin, ışık oyuklara seke seke girer ve derinde söner. Yaklaştıkça yüzeyin içinden yeni bir mimari
açılır: kemerler, tüneller, kuleler. Her yakınlaşma basamağı (1, 10, 100, 1.000, 10.000 kat) **tek başına asılabilecek
bir fotoğraf gibi** durmalı. 10.000 katta ekranın genişliği bir saç teli kalınlığı (~70 µm) ve orada da yapı bitmemeli.

Neyin ucuz göründüğü ve karşılığı (konuşmada sayıldı): renk matematikten değil malzemeden · ışık sahte değil (gölge,
seken ışık) · ölçek ipucu (makro alan derinliği, kaide, temas gölgesi, paralaks) · ayrıntı her yerde eşit değil
(hiyerarşi, dinlenme yüzeyleri) · görüntü hatası yok.

## 3 · Teknik yol

- **Geometri: mesafe kestirimiyle ışın yürütme** (fraktalın formülü her ölçekte kesin; üçgenleme gerekmez). G0'da
  CUDA çekirdeği. Hız yetmezse (ölçüyle) görünüme göre üçgenleyip donanım ışın izlemesi G2'de denenir.
- **Piksel kuralı:** yürüyüşün yüzey eşiği pikselin o uzaklıktaki izine bağlı (ε = uzaklık × piksel açısı × k).
  Pikselden ince yapı geometri olarak çizilmez, yüzeye katılır; normal de aynı ölçekte alınır. Yakınlaşırken ayrıntı
  aniden belirmemeli ya da kaybolmamalı (kapı ölçüsü).
- **Işık:** hareket sürerken deterministik önizleme (doğrudan ışık + yumuşak gölge + kapalılık; gürültüsüz), durunca
  yol izleme birikir (seken ışık, alan ışığının gerçek gölgesi, süper örnekleme). Önizlemeden birikime yumuşak geçiş.
- **Yakınlaşma:** kamera imlecin altındaki noktaya göre ölçeklenir (nokta ekranda yerinde kalır), üstel ve yumuşak.
- **Sayısal derinlik:** tek hassasiyet kabaca birkaç yüz kat taşır; 10.000 için G1'de kameraya göreli koordinat ve
  gerekirse çift-float.
- **Kendi kendini sınayan araçlar:** pencere olmadan aday ve basamak kareleri (`--scene heykel --headless`), kat taslağı,
  aynı yoldan kararlılık ölçüsü (`tools/kararlilik.py`).

## 4 · Fazlar

| Faz | İş | Çıkış |
|---|---|---|
| **G0 · Etüt taslağı** | 4-6 fraktal adayı uygulamada canlı: beyaz mat malzeme, tek ışık + dolgu, kaide, imlece yakınlaşma, tıklanan ışık, durunca birikim. Detaya inilmez | 🛑 **Durak 1:** Özcan canlı bakar, adayı ve genel görünümü seçer |
| G1 · Katlar ve derinlik | Seçilen fraktalın dalış yolları; her kata kendi mimarisi (yinelemeye göre formül ya da ayar: hibrit); son odalar; 10.000 kata sayısal derinlik; kat kat kare taslağı | Her basamak tek başına fotoğraf |
| G2 · Netlik ve kararlılık | Piksel kuralının ölçüsü; sabit örnekli süper örnekleme + kenarda 16-32 (Ada düzeni); yakınlaşma yolunda kıpırtı ölçüsü | Ada çıtası |
| G3 · Işık ve malzeme | Seken ışık, yumuşak gölge, lambanın ölçeği; porselen geçirgenliği A/B; makro alan derinliği; ton | Gerçek fotoğrafla yan yana |
| G4 · Tur ve cila | Otomatik dalış turu (gösteri + ölçüm), galeri kartı, belgeler | 🛑 **Durak 2:** Özcan canlı |

## 5 · G0 adayları

Her biri farklı bir karakter. Ölçüt: hiyerarşi (büyük kütle, orta yapı, ince ayrıntı), dinlenme yüzeyleri, ışığın
geçtiği boşluklar, yakınlaştıkça yeni yapı. Gerekirse fraktal bir kesme biçimiyle (küre, blok) kesişir: kesik yüzeyler
düz dinlenme alanı olur, kesitte yapı görünür.

1. **Kutu** (Mandelbox): şehir ve kale mimarisi, keskin köşeler.
2. **Katedral** (sözde Klein): kemerler, sütunlar, gotik salonlar.
3. **Dantel** (Apollonius, küre evirme): gözenekli, kemik ve mercan gibi.
4. **Kristal** (dönmeli Menger): keskin, kristal kuleler.
5. **Tohum** (Mandelbulb): en bilinen organik fraktal; kıyas için.
6. **Hibrit**: etütte seçilir (iki katlamanın sırayla uygulanması gibi).

## 6 · Riskler ve dürüst sınırlar

- **Klişe görünüm:** her şey sanat yönüne bağlı; son hakem Özcan'ın canlı bakışı.
- **Hız:** fraktal yürüyüşü + seken ışık pahalı; birikimin kaç saniyede temizlendiği ölçülecek.
- **Kat tasarımının kontrolü:** yinelemeye göre ayar değiştirmenin ne kadar yönetilebildiği G1'de görülür; az çıkarsa
  dalış sayısı azalır, katlar yer seçimiyle taşınır.
- **Hareketi izleyemem,** yalnız kare görürüm: yakınlaşmanın akıcılığı ve "wow" Özcan'da.
