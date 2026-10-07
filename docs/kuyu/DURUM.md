# DURUM · Sahne 10 "Kuyu"

> Otonom oturum, 2026-10-07 öğleden sonra (plan `docs/kuyu/PLAN.md`). Bu dosya çalışırken yazılıyor; sonuç bölümü
> sahne bitince başa gelecek.

## Şu ana kadar (çalışma günlüğü)

### Kurulan (K0-K2, K4'ün çekirdeği, K6'nın ilk turu)

- **Kafes:** hücre 8 m, her hücrenin ortasında 3,6 m'lik oda (iç yüzü gümüş filmli, yarım inç gri cam). Odanın 12
  kenarı sonsuza uzanan **demetler**: her eksende oda sırası başına dört çizgi. Üç aile birbirine değmez: a ekseni
  boyunca uzanan demet a+1 ekseninde odanın dışında, a+2 ekseninde odanın açıklığının içinde durur (her köşede bir
  fırıldak). Böylece her yüzün iki karşı kenarında birer demet yatar, demetler kesişmez (iç içe geçen çıta yok).
- **Kuyu:** ayrı bir boş sütun gerekmedi: demetler arasında kendiliğinden sonsuz dikey kanallar kalıyor. Düştüğümüz
  kanal iki oda sütununun arası (x ∈ (−1,8, 1,8), z ∈ (2,44, 5,56)): iki yanda 2,2 m ötede odalar geçer.
- **İzleyici (CUDA, analitik):** hücreden hücreye 3B yürüyüş; her hücrede oda kutusu ve 12 demet (kare prizma). Demet
  yüzünde çıtaların kabartması kesin yürünür (eksen boyunca sabit yükseklik alanı), çıta başına 5 iplik.
- **Duvar (Ö2):** gümüş film 45 nm (Johnson-Christy 1972), Airy formülüyle; gri cam 12,7 mm, GGI "Tinted Float" veri
  sayfası: gri 1/2" görünür geçirgenlik %18; dışta yansıma önleyici %0,5. Dik gelişte (550 nm) içeriden **%94,5**
  yansıtır, dışarıdan **%4,1**, geçirgenlik **%0,71** (iki yönde aynı). İnce gümüş maviyi daha çok geçirir, kırmızıyı
  daha çok yansıtır: derin yansımalar kehribara kayar (fizik, ayarı yok).
- **Oda içi:** ampuller (2 cm cam küre) ışının konisiyle (Ayna'nın disk örtüşmesi), askı telleri koniyle, krom ve bakır
  küreler, serbest iki yüzlü aynalar, sarkaç (T = 2π√(l/g), büyük salınım düzeltmesiyle). 20 oda türü (yıldızlar,
  tek lamba, ay ışığı, küre, sarkaç, eğik ayna, köşe yansıtıcı, sıra, karanlık, mum), her oda 48 küp simetrisinden
  biriyle döndürülmüş: komşular benzemez.
- **Yapının ışığı (K4):** demetleri yalnız odaların camdan sızan ışığı aydınlatır. Kafes dönemli olduğundan bir hücrenin
  demet yüzleri üzerinde bir kez tablo kurulur (2 cm × 2,5 cm texel, 460 bin texel): texel başına 4096 kosinüs ağırlıklı
  ışın (Fibonacci, rastgele sayı yok), her ışın çarptığı oda yüzüne yazılır; 27 komşu oda yüzü ayrı (en büyük 16),
  ötesi ve pus ayrı. Odaların rengi kare çizilirken çarpılır (oda türleri dönemli değil). Kurulum ~0,3 s.

### Ölçümler (şimdilik)

| # | Sonuç |
| --- | --- |
| Ö1 | **0,00013 px** (30. görüntüye kadar 16 görüntü; hedef < 0,1). `--test o1` |
| Ö2 | Bağımsız hesap (Abelès matrisi, Macleod) ↔ tablolar: **< 1e-6** (hedef ±0,005); soğurma ≥ 0; karşılıklılık 2e-15. `--test o2` + `python tools/kuyu_o2.py` |
| Ö4 | Tablonun makinesi (aynı yayıcı modelle kaba kuvvet): **0,989**. Fiziğe karşı (odaların içi tam derinlikte izlenerek): **0,934, tutmadı** (aşağıda). `--test o4` |
| Ö6 | 1000 rastgele poz temiz, bozuk piksel 0; dört bakış üç ayrı süreçte bit bit aynı (md5) |

**Ö4 neden tutmadı:** tablo bir odanın her yüzünü tekdüze bir yayıcı sayar (oda türünün o yüzden çıkan ortalama
ışınımı × ortak açı biçimi). Gerçekte camdan çıkan ışık yüze yayılmış değil: ampullerin kendisi ve ilk görüntüleri
camın bir yerini daha parlak yapar. Cama yakın demet yüzlerinde fark %10-28. Oda türünün yöne bağlı parıltısını
kullanmak düzeltmedi (0,93: hata açısal değil mekânsal). Düzeltmek her gölgelendirmede ampul başına gölge ışını ister
(ilk deneme: 27 odayı piksel başına hesaplamak 70 ms tutuyordu). Görünür etkisi: ampule yakın demetlerde yumuşak
parlak lekeler eksik.

### Kararlar ve plandan sapmalar

1. **Demetler odaların kenarlarından çıkar** (planda hücre kenarlarındaydı): tesseract'ın mantığı (çizgiler odanın
   kendi uzantısı), daha yoğun kafes, kuyu kendiliğinden.
2. **Dipteki soğuk ışık yok:** sonsuz kafeste sonsuzdaki bir ışık hiçbir yere ulaşamaz (her doğrultu bir odaya ya da
   demete çarpar, yalnız eksenler boyunca açık kanallar kalır); bizimle düşen sahte bir ışık olurdu. Soğuk vurgular
   ay ışıklı odalardan (5200-6500 K) ve ince gümüşün maviyi geçirmesinden geliyor.
3. **Oda içi kesme: görünmezlik sınırıyla.** Kalan yolun bir ampulden ekrana katabileceği en çok ışık
   w·I/(π·max(r, R)²) (R ayak izi, yalnız büyür) pozlanmış görüntüde AgX'in en dik yerinde yarım kod değerinin altına
   inince yol biter. "Girişin 1/255'i" kuralı görünür fark veriyordu (oda bakışında piksellerin %28'i 2 koddan fazla):
   ampuller çok parlak, derin görüntüleri de görünüyor.
4. **Kuyruk denendi, bırakıldı:** ilk N sekmeden sonrasını odanın o yöndeki ortalama ışınımıyla kapatmak noktaların
   arasındaki karanlığı turuncu bir sisle dolduruyordu (gerçekte derin görüntüler seyrek noktalar).
5. **Bir pikselin aynı oda yüzüne giren örnekleri oda içini paylaşır:** 6 örnek dışarıyı (kenarlar) izler, oda içi
   pikselin merkezine en yakın örnekten tek ışınla (izdüşümü √(6) ^ 0,5 genişler), küre ya da ayna olan odalarda 3
   ışınla. Oda bakışında 187 → 25 ms. Ortalama parlaklık korunuyor; noktalar 6 örnektekinden biraz yumuşak.
6. **Paylaşımlı bellek:** oda türleri, yüz parlaklıkları ve simetri tablosu blok başında paylaşımlı belleğe; global
   bellekten okununca birbirine bağlı yükleme zinciri (oda → tür → parlaklık) kareyi 40 ms'ye çıkarıyordu (şimdi 14).
7. **32 bitlik karma:** hücre dizinleri 64 bit, karma 32 bit (dört milyar hücrede bir sarar).
8. **Referanslardan (Özcan'ın üç tesseract karesi):** çizgiler ince ve çok, aralarında parlak (krem) ve koyu iplikler;
   çoğu ışığı yansıtıyor; renk nötr-sıcak. Bu yüzden: çıta başına 5 iplik (kemik, ceviz, siyah lake, gri bez, ten
   deri, bordo, zeytin, altın varak), cilalı iplikler Fresnel (n 1,5) ile, altın varak altının ölçülmüş sabitleriyle
   yansıtır (yansıyan ışın odaya çarparsa odanın ortalama parıltısı). Işıklar 3300-4300 K, çerçeve krom (bakır,
   aynalarda sonsuza tekrarlanınca her şeyi turuncuya boyuyordu).

## NE DOĞRULANMADI (şimdilik)

- Canlı uygulamada kontroller insan elinde denenmedi.
- Ö3 (kararlılık), Ö5 (canlı FPS), Ö7 henüz ölçülmedi.
