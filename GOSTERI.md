# Gösteri

## Açmak
`C:\dev\pc_graphics\build\pc_graphics.exe` — çift tıkla. Birincil monitörde kenarlıksız tam ekran **galeri** açılır:
solda üç sahne, sağda seçilenin resmi ve açıklaması.

- **↑ / ↓** (ya da **1-3**, ya da fare) seç, **Enter** (ya da **Sahneyi aç** düğmesi) aç.
- Açılan sahne birkaç saniye yükleme ekranında hazırlanır (sahne 1'in programları ilk açılışta daha uzun derlenir).
- Her yerde: **Esc** geri (yardım → sahne → galeri → çıkış) · **F1** o sahnenin tuşları · **H** arayüzü gizle ·
  **F12** ekran görüntüsü (`captures/`) · **F5** ayarları ve programları yeniden yükle (sahne 1 ve 2).
- Sahne üstündeki arayüz: sol altta sahnenin adı ve durumu (ve varsa seçici, ilerleme çubuğu), sağ üstte hız ve
  süreler, sağ altta tuş ipuçları.

## 01 · Altın Saat — cam ve taş, tayfına ayrılan güneş
Karanlık odaya 5 yarıktan giren güneş, huzmelere asılı 14 kristal prizma, betona saçılan tayflar. **Durağan** başlar:
görüntü örnek biriktirdikçe netleşir (sol alttaki çubuk 8.192 örneğe ilerler; ondan sonra gürültü giderici çekilir,
ham görüntü kalır).

| Tuş | Ne yapar |
|---|---|
| **Boşluk** | durağan ↔ **canlı** (canlı: su dalgalanır, köpükler ve toz süzülür, ~22 fps) |
| **C** | sonraki kamera (gökkuşağı, prizma natürmortu, alçak geniş, orta, köşe, havuz, plan) |
| **↑ / ↓** | pozlama ±¼ durak · **← / →** odak uzaklığı · **R** birikimi sıfırla |
| **P / V / N** | fotonlar (kostikler) / pus / doğrudan ışık örneklemesi aç-kapa: ne neyi yapıyor, gör |
| **D** | gürültü giderici aç/kapa · **G** güneş lekesi bağlantısı |
| **1-5** | görünüm: güzel, albedo, normal, yalnız kostikler, maliyet ısı haritası |

**Dene:** **P**'ye bas: gökkuşağı, prizmanın tayfı ve su desenleri kaybolur (ayrı bir foton hesabından geliyorlar).
**D**'yi kapat, bir dakika bekle: gürültü giderici olmadan da aynı görüntüye yakınsıyor. **Boşluk** ile canlıya geç:
720p çizilir, yapay zekâ 1440p'ye büyütür; farkı prizmada görülür. **F12** PNG'nin yanına doğrusal EXR de yazar.
Ayarlar `scenes/altinsaat.cfg` (F5 ile anında): güneş, pus, beyaz dengesi, pozlama, kameralar, parlama.

## 02 · Fener — siste dönen bir huzme, gece ve orman
Inside tarzı deniz feneri. A "Pencere" bakışında açılır.

| Tuş | Ne yapar |
|---|---|
| **Boşluk** | A'dan B'ye orman içinden yürü (778 m, ~70 sn; yolda basarsan geri döner; sol altta ilerleme) |
| **B** | öbür bakışa atla · **P** zamanı durdur · **← / →** zaman ∓1 sn · **↑ / ↓** pozlama |
| **1-5** | görünüm: son, yalnız yüzey, yalnız hacim, derinlik, normal |
| **Tab** | oyuncu kipi (merak, dünya bunun için kurulmadı): 1. Tab kendi adanda A'da, 2. Tab fener adasında kulenin dibinde, 3. Tab sahneye dönüş |
| oyuncuyken | **WASD** yürü · **fare** bak · **Shift** koş · **Boşluk** zıpla; ağaçlara, kuleye, kulübeye çarparsın, derin suda yüzersin |

Ayarlar `scenes/fener.cfg` (F5 ile anında).

## 03 · Yıkım — betonarme bir apartman, deprem ve kırılma
Açılırken bina iki saniye görünmeden kendi ağırlığıyla oturur. Odak fizik.

| Tuş | Ne yapar |
|---|---|
| **3-9** | depremin büyüklüğü (sol altta seçili olan, tepe ivmesi ve süresiyle); **Boşluk** başlat / durdur |
| **sol tık** | tıkladığın yeri patlat (yakındaki parçalar yok olur, çevresi savrulur); **Ctrl + sol tık** büyük patlama |
| **sağ tık + fare** | bak · **WASD** uç · **E / Q** yukarı / aşağı · **Shift** hızlı · **tekerlek** yakınlaş |
| **T** | ağır çekim (×1, ×0,25, ×0,1) · **P** zamanı durdur · **R** binayı baştan kur |
| **B** | donatı demirlerini göster / gizle · **G** gürültü giderici (hızlı süzgeç → yapay zekâ → ham) |

| Büyüklük | Bina |
|---|---|
| 3-5 | sallanır, çatlar; bir şey düşmez |
| 6 | balkon korkulukları ve zemin kat dolgusunun bir kısmı dökülür |
| 7 | ağır hasar: korkuluklar ve zemin kat dolgusu dökülür, çerçeve ayakta kalır |
| 8 | yumuşak kat göçmesi: zemin kat gider, bina yan yatıp çöker |
| 9 | daha uzun ve sert sarsıntı; en güçlü kısım daha geç gelir, sonunda bina tamamen çöker |

**Dene:** 6 ile 8'i art arda (**R** ile baştan kurup) karşılaştır; zemin kat kolonlarını tek tek patlat; çöküşü **T** ile
ağır çekimde izle, kırık kolon uçlarına uçup demirlere yakından bak.
Ayarlar `scenes/yikim.cfg` (bina, malzemeler, çözücü, kamera, güneş).
