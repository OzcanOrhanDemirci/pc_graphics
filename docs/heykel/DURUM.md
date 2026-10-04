# DURUM · sahne 6 "Heykel" (geometri)

> Tek kaynak durum panosu. Plan `docs/heykel/PLAN.md`. En yeni en üstte.

## 🛑 Durak 1 · Özcan'ın bakacağı yer (2026-10-05 gecesi, G0 etüt taslağı)

**Açmak:** `build\pc_graphics.exe` → galeride **06 Heykel** (Enter). Yükleme bir iki saniye. Sol alttaki panelde **Aday**
düğmeleri (ya da 1-5 tuşları): Şehir · Katedral · Yıldız · Tohum · Dantel.

**Senden istenen:** taslak olarak bak ve sanat kararını ver. Hangi aday (ya da adaylar) "geometrik olarak şok" yolunda,
hangisi değil; genel görünüm (malzeme, ışık, kaide, karanlık oda) doğru yönde mi. Ayrıntı (keskinlik, 10.000 kat,
katların tasarımı, malzemenin ışık geçirmesi) bilerek yok; o işler senin seçtiğin aday üstünde G1-G3'te.

**Nasıl bakılır:**
- **Dur ve bekle:** kıpırdamadığın sürece görüntü birikir: ilk görünümde 1-3 saniyede, yakında birkaç saniyede netleşir
  (sağ üstte "Örnek" sayısı artar).
  Hareket ederken görüntü daha kaba bir önizlemedir: yargıyı durunca ver.
- **Tekerlek:** imlecin gösterdiği yere yaklaşır (o nokta ekranda yerinde kalır). Bu taslakta en fazla 1.000 kat.
  En ilginç yerler 10 ve 100 kat civarında.
- **Sol tık:** ışık tıkladığın yere döner (ölçeği o anki bakışa göre). **Sol tıkla sürükle:** ışık o noktanın çevresinde
  döner; yatık ışıkta kabartma okunur.
- **Sağ tıkla sürükle:** heykeli (bakışı, en son yaklaştığın noktanın çevresinde) çevirir.
- **R:** ilk görünüm · **↑ / ↓:** pozlama · **H:** arayüzü gizler.

**Adaylar (her biri farklı bir karakter):**
| | Aday | Fraktal | Uzaktan | Yaklaşınca |
|---|---|---|---|---|
| 1 | **Şehir** | Mandelbox, ölçek 3 | yüzleri bina bloklarına oyulmuş bir küp | sonu gelmeyen bloklar, avlular, kuleler |
| 2 | **Katedral** | sözde Klein (Knighty), bir bloğa kesilmiş | kemer sıraları ve kum saati sütunlar | daire daire dantel, iç içe kubbe ağları |
| 3 | **Yıldız** | Menger süngeri, üst köşesi merkezden kesilmiş | kesitte altı köşeli yıldızlar | kare boşluklu sonsuz bir yapı, galeriler |
| 4 | **Tohum** | Mandelbulb (üs 8), eğik | karnabahar gibi organik bir kütle | çiçek gibi ışınsal halkalar, tomurcuklar |
| 5 | **Dantel** | Apollonius küre paketi, bir küreye kesilmiş | tabaklar, küreler, delikler | küre küre içe doğru sonsuz delikler |

**Ölçümler (2560×1440, RTX 4080, autotest):** hareket ederken önizleme 1-16 ms (aday ve yakınlığa göre), uygulama
28-44 fps. Durunca yol izleme örneği 3-13 ms (ilk görünüm) / 40-72 ms (10 kat yakında; böyle bir örnek birkaç kareye
satır satır bölünür, ekran donmaz). 1.024 örnekte durur. Kart belleği 2,0 GB.

### Bu taslakta ne yapıldı
- Yeni sahne `src/scenes/heykel/`: fraktallar mesafe kestirimiyle (`heykel_de.h`, ekran kartı ve ana işlemci aynı kod:
  imlecin altını seçmek ve heykeli kaideye oturtmak da onunla), CUDA çekirdekleri (`heykel_kernels.cu`): önizleme (doğrudan
  ışık, tahmini yumuşak gölge, kapalılık; gürültüsüz) ve durunca biriken yol izleme (alan ışığının gerçek gölgesi, üç
  sekme, piksel altı örnekler); önizlemeden birikime ilk 6 örnekte yumuşak geçiş; AgX ton, sabit titreşim.
- **Piksel kuralı** baştan: yüzey eşiği pikselin o uzaklıktaki izinin yarısı (`detail = 0,5`), normal aynı ölçekte.
- **Oturtma:** her aday yüklenirken uzaktan merkeze 1.500 ışınla dış yüzeyi bulunur: yarıçap, yükseklik, alt nokta ve
  kütle ortası; heykel 42 cm boyuna ölçeklenir, ortası kaidenin ortasına gelir, altı yüksekliğin %1-8'i kadar düz kesilir
  (kaideye oturur).
- **Oda:** koyu, ufuk çizgisi görünmeyen bir boşluk; dolgu ışığı yalnız yukarıdan (yumuşak tavan ışığı gibi). Tek spot:
  boyutu, uzaklığı ve aydınlattığı daire o anki bakışa göre (yakında da aynı ölçekte görünür).
- Ayarlar `scenes/heykel.cfg` (F5); her adayın her düğmesi `aday<i>.<düğme>` ile ezilir (etüt bunlarla yapıldı).
- Komut satırı: `pc_graphics.exe --scene heykel --headless --aday all --zoom 1,10,100 --spp 128 --w 1280 --h 720`
  adayların ilk görünümünü ve yakınlaşma basamaklarını PNG'ye yazar (`captures/heykel_etut/`).
- Uygulamaya genel ek: `--autotest` betiğinde **tekerlek** adımı (`wheel(n)`); Heykel'in sınaması yakınlaşma, ışık
  tıklama ve sürükleme, çevirme ve beş adayı dolaşır.

### Etütte elenenler (benim kararım, kareye bakarak)
- **Mandelbox ölçek 2 ve −1,5, Julia biçimleri:** dışarıdan düz bir küp (güzelliği içte). Küreye ya da kutuya kesilmiş
  hâli ("Oyma"): çoğu yüzü düz bir top. Ölçek 3 kaldı (Şehir): yüzleri gerçekten oyulmuş.
- **Sierpinski dörtyüzlüsü** (dönmeli ve dönmesiz): ders kitabı görünümü, yakında çarpık; zayıf.
- **Dönmeli Menger** ("Kristal"): kırık tuğla yığını. Dönmesiz Menger'in köşegen kesiti (Yıldız) çok daha güçlü.
- Sözde Klein'ın ince ipliksi ayarları (boşlukta asılı iplikler) ve tek sütun hâli: blok hâli (Katedral) en güçlüsü.

### NE DOĞRULANMADI
- **Senin gözün:** hiçbir aday senin canlı bakışından geçmedi; kararlar karelere benim bakmamla.
- **Fareyle gerçek kullanım:** yakınlaşma, ışık ve çevirme yalnız autotest'in sanal faresiyle denendi. Sürükleme hızları
  (ışık 0,003, çevirme 0,0025 rad/piksel) tahmin; elde fazla ya da az gelebilir.
- **Kıpraşma ölçülmedi:** taslakta kenar yumuşatma yalnız birikimden gelir; hareket sırasındaki önizleme tek örneklidir
  (kenarlar tırtıklı, ince ayrıntı kıpraşır). Netlik işi G2'de.
- **1.000 katın ötesi** denenmedi; tek hassasiyetin nerede bozulduğu ölçülmedi.
- Bazı yakın bakışlarda (Tohum, 10 kat) 1.024 örnekte bile hafif gren kalabilir; ölçülmedi.
