# DURUM · Sahne 9 "Isı" (TASLAK)

> **2026-10-06 akşam · kaba 3B taslak hazır, Özcan'ın bakması bekleniyor.** Galeride **09 Isı**.

## Ne bu

Bilgisayarın içi, bir manzara boyunda: biz bir balkondayız. Ekran kartı üç dev fanıyla sıcak bir duvar, RAM kuleler
şehri, işlemcinin pompası ışık halkalı bir tapınak, güç kaynağı uzakta karanlık bir kütle, kasanın delikli paneli
yıldızlı bir gök. Sahnenin ileride asıl işi bilgisayarı en yüksek yüke sokmak; görüntü onun temsili ("veri gerçek,
görüntü sanat").

**Neden taslak (Özcan, 2026-10-06):** *"Sen aklındaki tasarımı taslak olarak yap. 3D render olsun. [...] w a s d ile
yürüyeyim ve o manzaraya bakayım. Taslak olduğu için her şey kaba ve detaysız olur. Ama kompozisyonu görürüz. [...]
Beğenirsek ya da biraz revizeyle içime sinecekse gerçekten o sahneyi detaylarıyla kurmaya geçersin. Beğenmezsek iptal
ederiz."* Bütün konuşma ve kararlar hafızada (`claude_memory`, `grafik/project_isi_gorevi.md`).

## Nasıl bakılır

- **1 2 3 4**: bestelenmiş bakışlar (Balkon, Duvarın dibi, Kulenin tepesi, Pompa); kamera 2,6 sn'de yavaşça uçar. **R** balkona döner.
- **W A S D** serbest uçuş, **Boşluk** yüksel, **C** alçal, **Shift** hızlı (90 m/sn). Çarpışma yok (taslak).
- **Sol tık sürükle** ya da **Tab**: etrafa bak. **↑ ↓** pozlama.
- **5 6 7** ya da panel: İşlemci, RAM, SSD. **Taslakta yalnız görüntü, yük üretmez**: pompa ve hortumları, MOSFET
  bloğunun ayağı, kule pencereleri, devre yollarındaki darbeler, SSD'nin ışık sırası uyanır.
- Panelde **Harcanan enerji** sürgüsü: dünyanın soğuk maviden kehribara dönüşünü önizler (ileride gerçek enerjiyle).
- Konsoldaki yedi kadranın ilki ekran kartının **kendi ölçtüğü** güç (NVML); diğerleri düğmelere bağlı yer tutucu.
- Komut satırı: `pc_graphics.exe --scene isi --headless --out captures/isi [--view 0..3] [--w --h] [--cpu --ram --ssd
  --energy 0..1] [--cam "x y z yaw pitch fov"] [--set anahtar=değer]`. Ayarlar `scenes/isi.cfg`, F5 yeniden yükler.

## Ne var, ne bilerek yok

- **Var:** mesafe alanıyla (SDF) tanımlı kaba kütleler, dönen fan kanatları (ağır, gerçek devirle değil), fanların
  ardında kızıl kanat demeti ve ısı boruları, duvarın tepesinde sekiz ısı borusu, iki sıra sekizer kule (her birinde
  kırmızı uyarı lambası), lav boruları (üstlerinde akan bir kor yarığı), insan boyunda lambalar (ölçek için), yıldızlı
  delikli gök ve ondan inen yumuşak huzmeler, alçak pus.
- **Işık:** rastgele sayı yok; pikselde iki sabit örnek. Pus, sıcak parçaların pustaki parıltısı (atan) ve huzmeler
  (erf) ışın boyunca kapalı biçimde hesaplanır, adım adım örneklenmez.
- **Bilerek yok:** ayrıntı, gerçek yük (CPU/RAM/SSD yükleri, sensör programı, priz ölçümü), iç dünyalar, çarpışma,
  güç kaynağının "dağ" biçimi (şimdilik koyu bir kutu ve fan halkası).

## Ölçümler (2026-10-06)

| | |
| --- | --- |
| Canlı (1920×1080, 2 örnek) | autotest **44,6 fps, kare 22,7 ms**; kart 2,5 / 16 GB; o sırada ekran kartı **219 / 320 W, 63 °C** |
| Fotoğraf (2560×1440, kenarda 4 ek örnek) | 112 ms; kenar ölçütü 1440p'de kareyi 40 ms'den 112 ms'ye çıkarıyor, canlıda kapalı |
| Bozuk piksel (NaN) | 0 (dört bakış, uyanık ve uyuyan) |
| Sahne kapanınca | kart belleği boşaldı (14.793 / 16.376 MB boş) |

Yolda bulunan iki hata: (1) düz bir yüzeyde normal sıfır vektör çıkınca NaN, parlama piramidi onu ekranın yarısına
yayıyordu (kararma) → normal korundu ve her NaN sıfırlanıp sayılıyor. (2) Zemindeki küçük parçaların mesafesi kutusu
olmayan hücrede 5,6 m'lik görünmez bir döşeme üretiyordu; lav boruları ve lamba ayakları altında kalmıştı.

## NE DOĞRULANMADI

- **Özcan canlı görmedi;** karar onun: beğenilirse ayrıntılı kurulum, beğenilmezse iptal.
- Hareket hâlinde kıpırtı ölçülmedi (`tools/kararlilik.py`). Canlıda kenar örnekleri kapalı: ince ayrıntıda (kanat
  demeti, uzak devre yolları, gök delikleri) hareketle kıpırtı olabilir.
- Serbest uçuşta kameranın cisimlerin içine girmesi engellenmiyor; içeriden bakınca görüntü bozuk görünür.
- Galeri resmi `assets/gallery/isi.jpg` balkondan, üç parça uyanık, enerji %20.
