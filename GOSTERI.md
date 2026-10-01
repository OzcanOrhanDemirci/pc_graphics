# Gösteri

> **Üç sahne, üç exe** (`C:\dev\pc_graphics\build\`):
> - `pc_graphics.exe`: sahne 1, "Altın saatte cam ve taş" (bu sayfa).
> - `fener.exe`: sahne 2, Inside tarzı deniz feneri. Tuşlar: **Boşluk** A'dan B'ye orman içinden yürü (yolda basarsan
>   geri döner) · **B** öbür bakışa atla · **P** zamanı durdur · **← / →** zaman ∓1 sn · **↑ / ↓** pozlama · **S** ekran
>   görüntüsü · **H** gösterge · **F5** `scenes/fener.cfg` ve gölgelendiricileri yeniden yükle · **Esc** çıkış.
>   **Tab** oyuncu kipi (merak, dünya bunun için kurulmadı): 1. Tab kendi adanda A'da, 2. Tab fener adasında kulenin
>   dibinde, 3. Tab sahneye dönüş. Oyuncuyken **WASD** yürü · **fare** bak · **Shift** koş · **Boşluk** zıpla ·
>   **F12** ekran görüntüsü. Ağaç gövdelerine, kuleye ve kulübeye çarparsın; derin suda yüzersin.
> - `yikim.exe`: sahne 3, betonarme apartmanın depremle ya da senin elinle yıkılması (odak fizik). Açılınca bina iki
>   saniye görünmeden oturur, sonra tam ekran pencere gelir. Tuşlar: **sol tık** tıkladığın yeri patlat (yakındaki
>   parçalar yok olur, çevresi savrulur; **Ctrl + sol tık** büyük patlama) · **sağ tık + fare** bak · **WASD** uç, **E/Q**
>   yukarı/aşağı, **Shift** hızlı · **tekerlek** yakınlaş · **Boşluk** depremi başlat/durdur · **1-5** kayıt (1 Pazarcık
>   2023, 2 Antakya 2023, 3 Yarımca 1999, 4 İzmit 1999, 5 Bayraklı 2020) · **+ / -** şiddet ¼ adımla · **T** ağır çekim
>   (×1, ×0,25, ×0,1) · **P** zamanı durdur · **B** donatı demirlerini göster/gizle · **R** binayı baştan kur · **F12**
>   ekran görüntüsü · **H** yazıları gizle · **Esc** çıkış. Dene: Antakya ×1 (bina göçer), Pazarcık ×1 (eşikte: ağır
>   hasarla ayakta kalır, korkuluklar ve duvarlar dökülür; **+** ile ×1,25'te göçer), Yarımca ×1 ile ×1,5 (aynı bina,
>   iki son); zemin kat kolonlarını tek tek patlat; çöküşü **T** ile ağır çekimde izle, kırık kolon uçlarına uçup
>   demirlere yakından bak.

# Sahne 1 — "Altın saatte cam ve taş"

## Açmak
`C:\dev\pc_graphics\build\pc_graphics.exe` — çift tıkla ya da terminalden çalıştır. Birincil monitörde kenarlıksız tam
ekran açılır (2560×1440), **durağan modda** başlar. İlk kare "gökkuşağı": karanlık odaya 5 yarıktan giren güneş,
huzmelere asılı 14 kristal prizma, betona saçılan tayflar. Görüntü saniyede ~8 örnek biriktirerek netleşir.

- İlk saniyeler: yapay zekâ gürültü gidericisiyle okunur bir görüntü.
- ~30 saniye: temiz.
- birkaç dakika: referansa çok yakın (8192 örnekten sonra gürültü giderici devreden çıkar, ham görüntü kalır).

Bekle ve bak: ışık bıçağı, prizmanın içindeki tayf, perde duvardaki gökkuşağı, köpüklerin ince film renkleri, toz
zerreleri. Hepsi hesaplanıyor; hiçbiri boyanmış doku değil.

## Tuşlar
| Tuş | Ne yapar |
|---|---|
| **Boşluk** | durağan ↔ **canlı** (canlı: su dalgalanır, köpükler ve toz süzülür, ~24 fps) |
| **C** | sonraki kamera (0 gökkuşağı, 1 prizma natürmortu, 2 alçak geniş, 3 orta, 4 köşe, 5 havuz, 6 plan) |
| **H** | gösterge aç/kapa (sol altta: fps, ms, örnek, W, sıcaklık) |
| **S** | ekran görüntüsü → `captures/shot_*.png` + `.exr` |
| **↑ / ↓** | pozlama ±¼ durak |
| **← / →** | odak uzaklığı |
| **D** | gürültü giderici aç/kapa (kapatınca ham yol izleme gürültüsü görünür) |
| **P / V / N** | fotonlar (kostikler) / pus / doğrudan ışık örneklemesi aç-kapa: ne neyi yapıyor, gör |
| **1-5** | görünüm: güzel, albedo, normal, yalnız kostikler, maliyet ısı haritası |
| **R** | birikimi sıfırla · **F5** gölgelendiricileri ve `scene.cfg`'yi yeniden yükle · **Esc** çıkış |

## Dene
- **P**'ye bas: gökkuşağı, prizmanın tayfı ve su desenleri kaybolur. Onlar ayrı bir foton hesabından geliyor.
- **D**'yi kapat, 1 dakika bekle: gürültü giderici olmadan da aynı görüntüye yakınsıyor (fiziksel doğruluk).
- **Boşluk** ile canlıya geç: 720p çizilir, yapay zekâ 1440p'ye büyütür. Durağanla farkı prizmada görülür.

## Ayarlar
`scene.cfg` (F5 ile anında): güneş yüksekliği/yönü, pus yoğunluğu, beyaz dengesi, pozlama, kamera konumları
(`camera.0.pos = x y z`, `camera.0.target`, `camera.0.focal`, `camera.0.fnumber`), parlama gücü.
