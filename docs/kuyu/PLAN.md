# PLAN · Sahne 10 "Kuyu"

> Sürüm 1 · 2026-10-07 öğle · Özcan'la konuşulup yazıldı. Ayna'nın kardeşi. Otonom bir oturumda uygulanır; bu plan yön
> verir, daha iyi bir yol görülürse değişir, değişiklik DURUM.md'ye gerekçesiyle yazılır.
> **Yetki (Özcan):** tasarım bütünüyle Claude'da (*"o ciddi tasarımı, dizaynı sana bırakıyorum"*); her sahnedeki gibi tam
> yetki: *"Önemli olan en iyi sonuca ulaşmak. O sonuca ulaşmak için gerekenleri yap."* Ayna gecesinin kuralları geçerli
> (§7): ajan yok, gereksiz kontrol yok, haftalık kota durma sebebi değil.

## 0 · Tek amaç

**Sonsuzluk hissi: içinde sonsuz yansıma olan kapalı dünyaları dışarıdan izleyen bir gözlemci olmak.** Özcan: *"Benim
görmek istediğim o Interstellar'daki his + kendi içinde yansıma, dışarıdan gözlemci."* · *"Sonsuz odalar arasında aşağı
düşüyoruz [...] her odada farklı konumlarda aynalar, farklı bir ışık ve kendi içerisinde yansıması var. Yansıma odadan
dışarı çıkmıyor. [...] biz aynalarla dolu dünyaları gözlemleyen dış gözlemciyiz, tesseract içinde gibi."* · *"Ciddi bir
atmosfer olsun. Yani oyuncak gibi olmasın."*

**İç içe sonsuzluk:** sonsuz bir oda kafesi × her odanın içindeki sonsuz yansıma. Ayna'da labirentin içindeydik
(sonsuzluk görüntüydü, biz içindeydik); burada kapalı ayna dünyalarının dışındayız, aralarında düşüyoruz.

**Referans his (Özcan'ın üç film karesi, masaüstünde; depoya konmadı):** Interstellar'ın tesseract'ı. Kopyalanmaz, his
alınır: üç yöne sonsuza giden dik kafes · aşağı bakışta tek kaçış noktası, derin kuyu perspektifi · her şey eksenler
boyunca gerilmiş ince çizgilere dönmüş (kitap sırtları sonsuz ipliklere) · koyu ceviz/kahve ve siyah, sıcak amber
ışıklar arasında soğuk beyaz-camgöbeği vurgular · derinde karanlığın içinde küçük aydınlık odalar · insanın küçüklüğü.

## 1 · Kararlar (Özcan, 2026-10-07)

1. **Yeni sahne, galeride 10.** Ayna'nın seçeneği değil, kardeşi: başka dar alan (sonsuzluğu dışarıdan izlemek), aynı
   aile (kesin ayna optiği, ölçülmüş sabitler, koni süzme, gren yok).
2. **Sonsuz kafes, aşağı düşüyoruz.** Her hücrede kapalı bir oda; odaların içi aynalarla dolu, **her oda farklı** (ayna
   dizilimi, ışık, nesne). **Yansıma odadan dışarı çıkmaz.** Biz dışarıdayız.
3. **Ciddi atmosfer, oyuncak değil.** Sanat yönü Claude'da (§5).
4. Ayna'dan gelen kalıcı kurallar: **ayar paneli yok** (2560×1440, 30-40 FPS, kaliteyi Claude ayarlar) · **fizik ne
   derse o** · **gren ve pır pır yok** ([[grafik-netlik-kurali]]: canlıda rastgele örnek yok, deterministik) ·
   **Denizaltı kontrol sözleşmesi** (fare tıklamadan bakar, ters değil; A/D = görüntünün sağ vektörü).

## 2 · "Fizik ne derse o": tek yönlü duvar

"Yansıma odadan dışarı çıkmıyor" gerçek bir fiziğe oturur (karakol sorgu camı; Kusama'nın Sonsuzluk Odaları):
- **Duvar katmanları (içten dışa):** ince gümüş film (ilk yüzey, odaya bakar) · cam · koyu (nötr gri) cam · dışta
  yansıma önleyici kaplama. İçeriden: gümüş film ≈ %90 ayna → oda içinde ışık sonsuza dek gidip gelir. Dışarıdan:
  koyu cam filmi iki kez geçerek söndürür → dış yansıma ≈ %1-5; geçirgenlik ≈ %0,5-2.
- **Fizik bunu böyle ister:** geçirgenlik iki yönde aynıdır (karşılıklılık); yansıtma soğurma sayesinde iki yönde
  farklıdır. "Tek yönlülük" içerisi parlak, dışarısı karanlık olduğu için görünür. Dış yüzde komşu odaların zayıf bir
  parıltısı kalır (fizik bu kadarını ister).
- **Değerler hesaplanır, uydurulmaz:** gümüş filmin R/T/A'sı film kalınlığından aktarım matrisiyle (gümüşün kompleks
  kırılma indisi: Johnson ve Christy 1972, Ayna'da var), açıya göre, kanal başına tablo (Ayna'nın `Optics` tabloları
  gibi). Koyu cam geçirgenliği bir üretici veri sayfasından (kaynağıyla DURUM'a). Film kalınlığı ve cam koyuluğu
  bizim seçimimizdir (dünya kurmak); sonrası hesap.
- **Oda içi:** ayna duvarlar + iç aynalar (düzlem, gümüş) + cilalı metal nesneler (pirinç/bronz/krom: ölçülmüş n, k) +
  ışık kaynakları. **İçeride dağıtıcı (mat) yüzey yok ya da siyah:** Kusama odaları gibi içerik = ışık + ayna + metal.
  Böylece içerideki her yol kesin: yansır, yansır, bir ışığa ya da karanlığa varır. Hüzme ağacına gerek yok.
- **Sekme sınırı:** yol ağırlığı 1/255'in altına inene dek; tünelin sonunda kara duvar olmaz (Ö7).

## 3 · Mimari

- **Kendi izleyicimiz (CUDA, Ayna gibi, OptiX değil).** Dünya üç yönde periyodik bir küp kafes: ışın boşlukta **3B
  hücre yürüyüşüyle** (DDA) ilerler, her hücrede yalnız o hücrenin kirişleri, iplik demetleri ve odası sınanır. Kesin
  düzlem/kutu/küre kesişimleri; öteleme yok.
- **Oda dışı:** kutunun 6 yüzü (tek yönlü duvar tabloları) → dış yansıma dalı (zayıf, boşlukta sürer) + geçen dal
  (odaya girer). Geçen dal içeride en yakın yüzeyi bulur: duvar (iç ayna), iç ayna, metal nesne, ışık. İçeriden duvara
  geçen sızıntı karanlık boşluğa çıkar (katkısı < 1/255: kesilir, ölçülerek).
- **Işıklar ve küçük parlak noktalar:** Ayna'nın koni-disk örtüşmesi (derin yansımadaki küçük ışık titremez, kesin
  görünen payı kadar toplanır).
- **Uzak oda:** oda birkaç pikselden küçükse içini izlemek hem boşa hem titrer → odanın dışarıdan görünen **ortalama
  parıltısı** (oda türü başına önceden, deterministik hesaplanır) ışının izdüşümüyle yumuşakça karışır (sıçrama yok).
- **Yapının ışığı:** kirişleri ve iplikleri odaların parlayan yüzleri (alan ışıkları, poligonun kesin biçim katsayısı
  formülüyle) ve uzaktaki tek bir soğuk ışık (kuyunun dibinden, referanslardaki camgöbeği-beyaz vurgu) aydınlatır.
  Gölge: ölçerek (önce görünür yüzler, sonra gerekirse pişirilmiş görünürlük).
- **Pus:** boşlukta ince, sıcak; derinliği verir, sonsuzluğu kesmeden söndürür. Kapalı biçim (Ayna'nın pus kodu),
  odalar boyutlu ışık olarak.
- **Netlik:** ışın konisi (Ayna), yordamsal desenler (iplikler) izdüşümle süzülür; piksel başına sabit örnek (6 kale
  deseni) + `recon`; birikim yok, gürültü giderici yok.
- **Kesinlik:** sonsuza düşüş: kamera konumu kafes periyoduna göre sarılır (koordinat büyümez); oda türleri mutlak
  hücre dizininden (64 bit) karılır.
- **Ayna gecesinin dersi:** çok dağınık warp'larla çalışan liste çekirdeklerinden kaçın (derleyicinin dağınık warp'ta
  yanlış kod üretmesinden şüphelenildi); belirlilik **aynı kare N süreçte md5** ile sınanır (Ö6).

## 4 · Bitti tanımı: ölçümler (her biri DURUM.md'de sayıyla)

| # | Ölçüm | Hedef |
| --- | --- | --- |
| Ö1 | **Doğruluk:** bir odada lambanın n'inci görüntüsü analitik yerinde | < 0,1 px (n = 30'a kadar) |
| Ö2 | **Tek yönlü duvar:** film tablosu ↔ bağımsız hesap; R + T + A = 1; T iki yönde aynı; içeri/dışarı yansıma oranı | ±0,005 |
| Ö3 | **Kararlılık:** düşüş yolu, `tools/kararlilik.py` | Ayna düzeyi ya da altı (16'lık referansa ≤ 0,0014; 64'lük de raporlanır) |
| Ö4 | **Yapının ışığı:** hesap ↔ çevrimdışı kaba kuvvet referansı | ≥ 0,99 |
| Ö5 | **Hız:** 2560×1440 yerel, büyütmesiz | 30-40 FPS, en kötü bakışta da |
| Ö6 | Bozuk piksel, rastgele poz taraması, aynı kare N süreçte bit bit aynı | 0 / temiz / aynı |
| Ö7 | **Sonsuzluk kesilmez:** iç tünelin sonunda kalan enerji; boşluğun derinliği | < 1/255; görüş pusta söner, kara duvar ya da kesik yok |

## 5 · Sahne ve sanat yönü (Claude'un; "ciddi, oyuncak değil")

- **Kafes:** hücre 8 m; ortada 3,6 m'lik küp oda; hücre kenarları boyunca kirişler (odalar arasında kalır). Bir hücre
  sütununda oda yok: **kuyu** (düştüğümüz boşluk, ~8 m genişlik). Kuyunun dört duvarı odalardan; kirişlerin arasından
  derinde başka odalar ve kafes sonsuza gider.
- **İplikler (sahnenin imzası):** kirişler, eksen boyunca gerilmiş ince çıta demetleri: farklı kalınlık ve tonlarda
  (eski kitap sırtı renkleri: koyu ceviz, is, kemik, soluk bordo, pirinç), her biri sonsuz uzun. Referanslardaki
  gerilmiş çizgi dokusu buradan gelir. Arada ince parlak tel/çizgi aralıkları (ışık sızar).
- **Odalar:** koyu cam küpler, ince bronz kenar çerçevesi. Dışarıdan: siyaha yakın camın içinde derin, parıltılı bir
  dünya. Her oda bir "dünya" (hücre dizininden seçilir, komşular benzemez):
  - **Mum:** ortada tek sıcak ışık → üç boyutlu sonsuz ışık kafesi, derinde kararır.
  - **Tünel:** karşılıklı iki duvar tam ayna, ötekiler loş → tek yönde sonsuz tünel.
  - **Köşe:** üç dik iç ayna → ışık geldiği yere döner.
  - **Eğik:** 45° bir iç ayna → görüntü kafesi döner, kırılır.
  - **Açılı çift:** iki iç ayna arasında 72° → görüntüler bir çember üstünde (ölçülü, tek renk).
  - **Küre:** ortada cilalı krom küre, ışık yanında → her görüntüde bütün oda bir kürede.
  - **Sarkaç:** ağır pirinç bir küre ince tele asılı, yavaş salınır (tek hareketli dünya, zamanın işlevi: kesin).
  - **Karanlık:** ışık sönük; yalnız komşuların sızan parıltısı.
- **Işıklar:** çoğu sıcak (1900-3000 K: mum, gaz lambası), birkaçı soğuk (ay ışığı ~4100 K); doygun renk yok. Kuyunun
  dibinden gelen tek soğuk ışık kirişlerin kenarlarını çizer.
- **Yasak listesi (Heykel'den ve "oyuncak değil"den):** neon, gökkuşağı, doygun renk, nabız, morph, kaleydoskop
  görüntüsü, bloom abartısı, ışık gösterisi. Simetri ve tekrar mimariden gelir, süsten değil.
- **Biz:** görünmez bir gözlemci (insan yok; tesseract'taki gibi küçük ve sessiz). Kamera, kuyuda yavaş düşer.
- **Ad:** "Kuyu" (EN "The Well"), çalışma adı; galeride 10. Vurgu rengi amber.

## 6 · Aşamalar (sıra = öncelik; kota biterse eldeki en önemli kısım bitmiş olsun)

| | İş | Kapı / ölçüm |
| --- | --- | --- |
| **K0** | İskelet: `src/scenes/kuyu/`, galeri 10, `--headless`/`--cam`/`--probe`, autotest adımı; Ayna'dan ortaklar: koni ve parıltı, optik tablo kurucusu, çözümleme (AgX, bloom, `recon`, titreşimsiz dither), CUDA-GL ortak doku | derleme, autotest |
| **K1** | Kafes + 3B hücre yürüyüşü + kirişler + oda kutuları (dışı) + kuyu; düşen kamera; kaba ışık | **Ö6** |
| **K2** | Tek yönlü duvar (film tabloları) + oda içi sekme döngüsü + ilk iki dünya (Mum, Tünel) | **Ö1, Ö2** |
| **K3** | Dünyaların hepsi, ışıklar (koni-disk), metal nesneler, sarkaç | kareler |
| **K4** | Yapının ışığı (odaların yüzleri + dipteki soğuk ışık) + pus | **Ö4** |
| **K5** | Kalite: uzak oda ortalaması, koni süzme, örnek bütçesi, sekme sınırı | **Ö3, Ö5, Ö7** |
| **K6** | Sanat: iplik demetleri, malzemeler, renk, kompozisyon; referans karelerle "his" yan yana | Özcan'a kareler |
| **K7** | Kontrol, düşüş, çarpışma, autotest bakışları | autotest |
| **K8** | Kendi gözden geçirmem (ajan yok): kod ve beş bakış karesi baştan, Ö1-Ö7 son hâlde yeniden | |
| **K9** | DURUM.md başında sonuç + NE DOĞRULANMADI, galeri resmi, README sahne bölümü, hafıza hasadı, commit/push | |

İkinci oturuma (Özcan isterse): bir odanın içine girmek (Inception'ın bir kat aşağısı), odalar arasında zaman farkı
(tesseract'taki gibi her oda başka bir an).

## 7 · Çalışma kuralları (otonom oturum)

- Önce oku: [[gpu-ve-grafik]] (§32 Ayna dersleri başta), [[grafik-netlik-kurali]], bu plan, `docs/ayna/DURUM.md`.
- Her aşamada ölç, sayıyı DURUM.md'ye yaz. Görüntü değişikliği kareye bakılmadan bitmiş sayılmaz. Bir iyileştirme
  farklı desenli ya da daha yakınsamış referansla da ölçülür (Ayna'nın "ölçüyü kandıran kazanç" dersi).
- Önemli kareleri SendUserFile ile gönder: kısa, Türkçe, ne gördüğünü söyle. Az ve anlamlı.
- **Ajan yok, kendin çalış.** Ö1-Ö7 dışında ek denetim katmanı kurma.
- Kota: 5 saatlik %90+ → sıfırlanmaya dek bekle, sonra sürdür. Haftalık kota bu görevde durma sebebi değil.
- Sıra: hafıza → `claude_memory` commit/push → proje commit/push; her aşama sonunda commit.
- Ters bölü/`$` içeren yama: Edit ya da Write ile .py (heredoc tuzağı). Derlemeyi `cmd.exe //c "C:\dev\pc_graphics\build.bat"`
  ile çalıştır, çıkış kodunu ve exe saatini oku.
- Bitince: DURUM.md başı = Özcan'ın bakacağı yer (nasıl açılır, tuşlar, ölçümler, NE DOĞRULANMADI).

## 8 · Başlangıç varsayılanları (ölçünce değişebilir)

- **Yeniden kullanılacak kod (Ayna):** `ayna_kernels.cu` (koni, `glowObjects`'in disk-disk örtüşmesi, pus kapalı biçimi,
  `kResolve`, `kHaze` ve bilateral büyütme, 6 kale deseni), `ayna_app.cpp` (Fresnel, optik tablolar, `saveReference`,
  testler ve fuzz), `ayna_live.cpp` (canlı sahne, CUDA-GL), `tools/kararlilik.py`.
- **Tuşlar:** fare bakış (tıklamadan, Tab bırakır) · **W/S** düşüş hızı (dur, yavaş, hızlı; varsayılan ~1,5 m/s) ·
  **A/D, Boşluk/C** kuyuda yana ve öne kayma (odaya yaklaşmak için; kirişlere ve odalara çarpar) · **Shift** hızlı ·
  **1-4** bestelenmiş bakışlar · **R** başa dön · **↑ ↓** pozlama.
- **Ölçüler:** hücre 8 m, oda 3,6 m (iç), duvar 12 mm, bronz kenar 4 cm; kiriş demeti 0,6-1,0 m kesit, 12-30 çıta.
- **Duvar (ilk hâl):** gümüş film ~30 nm (R ≈ %90, T ≈ %3-5, hesapla kesinleşir), koyu cam geçirgenliği ~%10-20 (veri
  sayfasından), yansıma önleyici dış kaplama ~%0,5.
- **Örnek bütçesi:** 6 sabit örnek + `recon` 0,12; kare ≤ 30 ms; aşılırsa önce uzak oda ortalamasının eşiği, sonra iç
  sekme sınırı ölçülerek.
- **Autotest bakışları:** kuyudan aşağı (kaçış noktası), bir odanın camına yakın (iç sonsuzluk), yana kafes boyunca,
  yukarı, bir Tünel odasının ekseninden; her biri ekran görüntüsü + kare süresi.
