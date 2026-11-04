# Sahne 13 · Dağ · durum

## Özcan'ın bakacağı yer

`build\pc_graphics.exe` → galeride **13 Dağ** → Enter. (Doğrudan: `build\pc_graphics.exe --scene dag`.) Veri `cache/dag`'de hazır, yükleme yarım dakika.

1. **Açılış:** korkuluğa birkaç adım, karşıda güneş. Karadeniz'de güneş yolu, katman katman sırtlar.
2. **Dürbün:** sağ tık basılı, tekerlek yaklaştırır (3x-30x). Panel bakılan yerin adını ve uzaklığını yazar. Önce güneş yoluna bak: dalgalar çıkar. Sonra tuş **2**, yön 20°: **Elbruz**, 299 km, iki tepesi ve karı. Tuş **3**, yön 114°: **Ağrı**, 295 km, gökten zor ayrılır.
3. **Tuş 3 ile doğuya dön:** Kaçkar'ın kendi gölgesi havada, ufka kadar üçgen.
4. **Korkuluğa dayan, aşağı bak.** Kaya, iri taşlar, altta vadi.
5. Soru: görüntü gönlünü açıyor mu? Dürbünde 10-30 km sırtlar yeterli mi? Korkuluk fazla mı parlak?

Tuşlar: fare bakar · W A S D · Shift hızlı · Boşluk zıplar · C eğilir · sağ tık dürbün · Tab imleç · 1-5 bakışlar · R başa · ↑↓ pozlama · F5 ayar ve hava tabloları (`scenes/dag.cfg`).

## Ölçümler (2026-11-04, RTX 4080, 2560×1440)

| Ne | Değer | Nasıl |
|---|---|---|
| Kare, pencerede, 16 örnek | Açılış 60,2 · bakışlar 60,7-67,3 FPS · dürbün 19x 42,4 · korkulukta 59,0 | `tools/dag_olc.py --pencere` |
| Kare, 32 ve 64 örnek | 36,3 ve 13,1 FPS | aynı |
| Kart belleği | 14,2 / 16 GB (işletim sistemi dahil), kapanışta geri döner | aynı |
| Üçgen | toplam 146 milyon: uzak ağ 51,8 M (1 yay saniyesi, LOD yok), yakın ve orta halka 94 M | `--headless` çıktısı |
| En uzak çizilen yer | **387 km**, yön 339,7°, denizin üstünden Batı Kafkasya | `--menzil`: tüm çember, her örneğin çarptığı en uzak zemin; panel bunu her yüklemede ölçer |
| En uzak görülebilen yer | **380-390 km bandı**: siluetin göğe karşı kontrastı ortanca %3,7. Eşik ölçümden önce yazıldı: %2 (Koschmieder). Görüntü eğrisi öncesi, doğrusal ışıkta; zar zor | `--menzil`, 1,5° görüşle tüm çember |
| Bilinen zirveler | Zirvesine nişanlı ışın, harita uzaklığına karşı: Elbruz 299,2/299,0 km · Şhara 289,8/289,7 · Uşba 283,6/283,5 · Ağrı 294,6/294,5 · Aragats 258,2/258,1 · Palandöken 112,1/112,1. Kazbek ve Tebulosmta görünmüyor: önlerinde sırt var | `--menzil` |
| Denizin ufku | 239,7 km | Etkin yarıçap 7.323 km, göz 3.923 m |
| Kontrast, uzaklığa göre | 100 km %36, 200 km %24, 300 km %11, 350 km %5 (bant ortancası) | `--menzil` |
| Delik | 3.004.560 ışın, köşelerden ve zıplarken, 0'ı yerin içinden geçti. Sınama: aşırı budanmış ağda (`far.tolerance=-0.003`) 163.222 | `--delik` |
| Kararlılık, dönüş 0,35 rad/sn | piksel kıpırtısı 0,00238, blok 0,00057, FLIP 0,0113, titreme oranı 1,01 | `tools/kararlilik.py`, `tools/titreme.py`, 16 kare, 16 örnek, 64 örnek referans |
| Kararlılık, ileri 1,3 m/sn | piksel kıpırtısı 0,00166, blok 0,00030, FLIP 0,0130, titreme oranı 1,01 | aynı |
| Karşılaştırma tabanı | Fener TAA 0,0053-0,0061 · Ada 0,00076-0,0014 (0,5 m/sn yüzüş). Aynı ölçü, başka yol ve hız | `docs/koy/DURUM.md` |
| Yürürken uzak bant | ikinci fark 0,00000, p99,9 0,0004. Burası neredeyse durduğu için aracı zorlamaz, tek başına kanıt sayılmaz | `tools/dag_titreme.py` |
| Sonlu olmayan örnek | Önce 1-3, kök neden: korkuluğun Fresnel'inde `powf(1 - x, 5)`, x 1'i aşınca negatif taban. Düzeltmeden sonra 10 kamerada 0 | `tools/dag_olc.py`, yol `dag: sonlu olmayan ornek` satırında |

Hepsi tek komutla yeniden: `python tools/dag_olc.py --pencere` (çıktı `captures/dag/olcum.txt`, eşikler betiğin başında, pencere açar).

## Ana oturumun ölçümünden düzelenler

- Panelde "en uzak yer 394 km": pişirme listesinin sayısıydı. Şimdi "en uzak çizilen yer 387 km", her yüklemede resimden ölçülür.
- Eski kıyas ("uzak bant 0,00000, Ada 0,001") geçersizdi: yan yürürken uzak bant duruyor, Ada'nın sayısı başka ölçü. Yukarıdaki tablo aynı ölçüyle.
- Eski "60-84 FPS"in üst ucu otomasyonun sanal faresinden, kamera göğe sıçrıyordu. Sahne ilk üç karenin fare farkını almıyor, autotest artık gerçek açılışı ölçüyor.
- **Önceki DURUM yanlıştı:** "GLO-90 Ağrı'yı etkilemiyor" demiştim, Ağrı tam o karodaydı. 16 karo artık SRTM (Terrain Tiles, zoom 12, ~29 m): en yüksek 5.110 m Ağrı, GLO-90'a göre ortalama fark ±3 m. Düz su hücreleri GLO-90'dan kaldı.

## Kararlar

- **Yer: Kaçkar doruğu.** Temmuz batışı (azimut 299°) denize. Ağrı, Elbruz, Kafkasya aynı çemberde. Panelde zirvenin yayımlanan yüksekliği, ölçüler modelden.
- **Renk: Sentinel-2 cloudless 2016** (CC BY 4.0). 2017 daha turuncu çıktı, 2018 sonrası NC.
- **Güneş +1,5°:** Daha alçakta zemin kömür, daha yüksekte gün batımı azalır.
- **Hava temiz:** Mie 1,5e-6. 3e-6'da 300 km ötesi tek renk. Denizin üstünde ince alçak pus: ufuk kesik durmasın.
- **Dürbünde görüntü karşıtlığı artar** (`zoom.contrast`, en çok 1,45 kat, yalnız parlaklıkta): göz orada kalan az karşıtlığa alışır. Fizik değil, algı.
- **Yüksek bulut ufka 1,5-5° yaklaşınca söner:** o açıda dürbünde koyu çizik oluyordu.
- **Gri kayadaki yaprak ve filiz yüklemede griye çekilir** (`gray_rocks` taraması orman tabanı).
- **Korkuluk çinko kaplı çelik:** boya sarı yanıyordu.
- **Bellek:** `aerial_rocks_04` 4K'ya indirildi: yalnız uzaktan okunuyor, ince seviyesi hiç görünmüyor.
- **Güneş diski yassı** (`sun.flat` 0,88), ışığı yakınlaşmanın karesiyle bölünür: dürbünde kenar parıltıda boğuluyordu. Çizim gerçek, parıltı sanat ayarı.

## NE DOĞRULANMADI

- **Özcan'ın gözü, canlı his.** Yürüme, fare, dürbün hızı, deniz dalgalarının hareketi. Ters kontrol render vektörüyle ve ana oturumun gerçek girdisiyle doğru; yine de ilk denemede sor (kural).
- **Gerçek fotoğrafla siluet kıyası yok.** Yön, uzaklık ve yükseklik haritayla tutuyor.
- **Göller:** kodda ayna var ama yalnız modelde düz olan su için. Doruğun 2 km güneyindeki buzul gölü modelde düz değil: su olarak işlenmiyor, uydunun koyu rengiyle duruyor.
- **Ada ve Fener kıyası** aynı ölçü ama başka yol ve hız. Aynı yol olamaz.
- **Bellek payı dar.** Başka kart kullanan program varsa sayfalama riski.
- **Kontrast ölçüsü** ortanca, doğrusal ışıkta, göğe karşı siluette: görüntü eğrisi sonrası ana oturum 387 km kenarında %2,8 ölçmüştü. Sınıra yakın, kesin sayı sayma.
- **Denizin hareketi:** dalga deseni yavaş kayıyor. Karede güzel; akışı canlı görmedim, kıpırtısı dürbünde ölçülmedi.
