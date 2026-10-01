# Gösteri — "Altın saatte cam ve taş"

## Açmak
`C:\dev\stress_test_pc\build\stress.exe` — çift tıkla ya da terminalden çalıştır. Birincil monitörde kenarlıksız tam
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
