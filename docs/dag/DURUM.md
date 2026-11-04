# Sahne 13 · Dağ · durum

## Özcan'ın bakacağı yer

`build\pc_graphics.exe` → galeride **13 Dağ** → Enter. (Doğrudan: `build\pc_graphics.exe --scene dag`.) İlk açılışta veri `cache/dag`'de hazır, yükleme yarım dakika.

1. **Korkuluğa yürü** (W), güneşe bak: Karadeniz'de güneş yolu, katman katman sırtlar.
2. **Sağ tık basılı: dürbün**, tekerlek yaklaştırır (3x-30x). Panel bakılan yeri ve uzaklığı yazar. Tuş **2** kuzeydoğu: yön 20°, dürbünü aç: **Elbruz**, 299 km, iki tepesi seçilir. Tuş **3** doğu: yön 114°: **Ağrı**, 295 km.
3. **Doğuya dön:** Kaçkar'ın kendi gölgesi havada, ufka kadar.
4. Soru: görüntü gönlünü açıyor mu? Yakın kayalar (10-100 m) inandırıcı mı? Korkuluk fazla mı parlak?

Tuşlar: fare bakar · W A S D · Shift hızlı · Boşluk zıplar · C eğilir · sağ tık dürbün · Tab imleç · 1-5 bakışlar · R başa · ↑↓ pozlama · F5 ayar ve hava tabloları (dağ.cfg).

## Ölçümler (2026-11-04, RTX 4080, 2560×1440, 16 örnek)

| Ne | Değer | Nasıl |
|---|---|---|
| Kare (pencere, GPU zaman damgası) | 16 örnek: 60-84 FPS, dürbün 19x 40 FPS. 32 örnek 40 FPS, 64 örnek 17 FPS | `--autotest-only dag` |
| Üçgen | uzak 51,8 M (1 yay saniyesi, LOD yok) + yakın 55 M + orta 21 M | `--headless` çıktısı |
| Kart belleği | 14,1 / 16 GB (işletim sistemi dahil), kapanışta geri döner | NVML |
| En uzak çizilen yer | **387 km**, yön 339,7° (Batı Kafkasya, denizin üstünden) | `--menzil`: tüm çember, her örneğin çarptığı en uzak zemin |
| Bilinen zirveler | Zirvesine nişanlı ışın, harita uzaklığına karşı: Elbruz 299,2 / 299,0 km · Şhara 289,8 / 289,7 · Uşba 283,6 / 283,5 · Ağrı 294,6 / 294,5 · Aragats 258,2 / 258,2 · Palandöken 112,1 / 112,1. Kazbek ve Tebulosmta görünmüyor: önlerinde sırt var (ışın 87 ve 46 km) | `--menzil` |
| Denizin ufku | 239,7 km | Etkin yarıçap 7.323 km, göz 3.923 m |
| Uzak bant titremesi | ortalama 0,00000, p99,9 0,0014 (kıyas: Ada 0,001, Fener 0,006); 16 örnek ile 64 örnek referans aynı | `tools/dag_titreme.py`, 1,3 m/s yan yürüyüş |
| Korkuluk | 9 sn W, 6 sn D, 7 sn Shift+S: hep içeride, korkulukta duruyor | autotest günlüğü `dag: ayak` |
| A/D yönü | Yön 299°'ye bakarken D: x artıyor, z azalıyor (görüntünün sağı) | autotest günlüğü, render vektörüyle aynı formül |

## Kararlar (Özcan'a değil, kayda)

- **Yer: Kaçkar doruğu.** Temmuz batışı (azimut 299°) denize. Ağrı, Elbruz, Kafkasya aynı çemberde.
- **Renk: Sentinel-2 cloudless 2016** (CC BY 4.0). 2017 daha turuncu çıktı, 2018 sonrası NC.
- **Güneş +1,5°:** Daha alçakta zemin kömür, daha yüksekte gün batımı azalır (A/B: `captures/dag/air_sheet.png`).
- **Hava temiz:** Mie 1,5e-6. 3e-6'da 300 km ötesi tek renk.
- **Yüksek bulut var** (ince sirrüs, tabaka): gün batımının rengini gök taşır. Uzak görüşü kesmez, hesap kolay.
- **Pus sıcak beyaz dengesi `white`:** gölgeler aşırı maviydi.

## NE DOĞRULANMADI

- **Özcan'ın gözü, canlı his.** Hareketi ve hissi göremem. Yürüme sarsıntısı, fare hızı, dürbün sensi.
- **Ters kontrol:** render vektörüyle kanıtlandı, yine de ilk denemede sor (kural).
- **Gerçek manzarayla kıyas yok:** Kaçkar'dan çekilmiş bir fotoğrafla siluet karşılaştırması yapılmadı. Yön ve uzaklık coğrafi hesapla tutuyor.
- **Bellek payı dar:** 14,1 / 16 GB. Arkada başka kart kullanan program varsa sayfalama riski.
- **Göller:** kodda ayna var, karede henüz yakından bakılmadı.
- **GLO-90 karoları** (Ermenistan, Azerbaycan) daha yumuşak: Ağrı'nın kuzeyi bu bölgede kalmıyor ama Aragats ve Sevan öyle.
