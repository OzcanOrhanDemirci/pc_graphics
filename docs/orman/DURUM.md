# DURUM · Sahne 11 "Orman"

> Plan `docs/orman/PLAN.md`; varlıkların kaynağı ve lisansı `docs/orman/VARLIK.md`. Özcan'la etkileşimli ilerleniyor
> (2026-10-27: *"Sahnede ilerledikçe [...] 11. sahneyi açıp fikirlerimi belirtebilirim"*).

## Özcan: nasıl açılır, ne denenir

- Varlıklar git'te değil: önce `python assets/orman/fetch.py` (indirir, her dosyayı özetiyle denetler; 50 varlık, diskte ~1,2 GB + zipler 0,4 GB).
  DLSS için `third_party/fetch.sh` SDK'yı getirmiş olmalı (v310.9.1); yoksa sahne sabit örneklerle derlenir.
- `build\pc_graphics.exe` → galeride **0**, sonra **aşağı ok** (11. sahnenin sayı tuşu yok) → Enter. İlk yükleme
  ~13 sn (taramalar 7 sn, ışık 4 sn).
- Fare tıklamadan bakar (Tab imleci bırakır), **W A S D** yürür, **Shift** koşar, **Boşluk** zıplar, **C** eğilir.
  **1 2 3 4** bakışlar (dere boyunca, devrik gövde, yukarı yapraklara, yer), **R** başa, **↑ ↓** pozlama. Panelde
  **Örnek 1 / 4 / 8 / 16 / DLSS** (varsayılan DLSS; sabit örnekler kıyas için, kurulamazsa 4'e döner).
- Yoğunluk `scenes/orman.cfg`'de katman katman: `dense.floor / shrub / sapling / debris / litter`, `young.trees`.

## Şu an (2026-10-27 akşam): iskelet + titremesiz yürüyüş + katmanlı yoğunluk

Plandaki taşlar sırayla değil, ilk bakışı en çok değiştirenden başlayarak yürüdü:

- **Varlık hattı (O0):** `assets/orman/fetch.py` + `manifest.json` (Poly Haven ve ambientCG, her dosya özetiyle);
  glTF'i doğrudan okuyan yükleyici (cgltf, MIT): her düğüm (varyant) bir prototip, her primitif onun GAS'ında bir
  yapı girdisi; Poly Haven'ın JPG temel rengi alfasız geldiği için alfa haritası yanındaki PNG'den, adından eşlenir.
  Alfa haritasının mip zinciri kapsamayı korur (Castaño): uzakta yaprak incelmez. Albedo mip'leri doğrusal ışıkta.
  Kaba LOD düğümleri (`_LOD1+`) atlanır. Varlıkların seçimi ayrı bir ajanın işi, sürüyor.
- **Görüntü ışın izlemeyle (O1, §3.1):** OptiX 9.1. Varsayılan yol **DLSS Ray Reconstruction** (NGX'in CUDA yolu,
  DLAA, büyütme yok): her karede piksel başına tek ışın, 16 adımlık Halton dizisinin sıradaki noktasına; kareleri
  bir önceki karenin gözünden hareket vektörleriyle DLSS toplar. Işın üretimi onun okuduğu kılavuzları yazar (renk,
  dağınık ve ayna albedo, normal, pürüzlülük, derinlik, hareket, derenin yansıma uzunluğu). Güneş diski noktası, pus
  adımı ve alfa eşiği diziyle döner, piksel başına 4×4 Bayer karosuyla kaydırılır. Sabit yol hâlâ panelde: piksel başına
  sabit desenli N ışın, yeniden yapılandırma süzgecinin (Gauss, σ 0,42 px) dağılımından. Doku seviyesi ışın konisinden
  (Akenine-Möller 2019), DLSS'te bir seviye ince.
- **Titreme düzeltmesi:** güneşin gölge ışını yaprağı açık kalan payı kadar geçirir (hep ya da hiç değil; koni
  örneğin izinden başlar, güneşin yarıçapıyla genişler); alfa haritasının 0'ın üstündeki seviyeleri kapsama. Gözün
  örnekleri de pikselin erimindeki kapsamayı alır, her biri kendi eşiğiyle (alpha to coverage). Normal haritası
  ortalama normalinin boyunu korur (Toksvig): uzak parlama genişler.
- **Pişmiş ışık (O4'ün ilk hâli):** 30 cm'lik yoklama ızgarası, her yoklama L1 SH (192 sabit yön, 2 geçiş: gök +
  güneş, sonra bir sekme), içeride kalan yoklama arka yüz payıyla geçersiz sayılır; okuma 8 yoklama, geçerlilik ve
  yüzeyin önünde olma ağırlığıyla. Zeminin kendi kapanması köşe başına (bitkilerin altı, 0,6 m, 48 ışın). Yaprağın
  arkasından gelen ışık aynı ızgaradan ters normalle.
- **Güneş canlı:** her örnek güneş diskinde kendi sabit noktasına bir gölge ışını (plan güneşi ızgarada pişirmeyi
  söylüyordu, aşağıda "Kararlar").
- **Pus ve huzmeler:** yerde yoğun, yukarı incelen pus; güneşin havadaki görünürlüğü 10 cm'lik bir hacimde pişmiş
  (voksel başına 4 gölge ışını), Henyey-Greenstein g 0,7. Adımlar göze yakın sıklaşır, pikselin örnekleri adımların
  içinde ayrı yerlere düşer.
- **Dere (O2'nin ilk hâli):** yatak zeminde oyulmuş, su yüzeyi yatak boyunca bir şerit; akışla kayan dalgacıklar,
  Fresnel payıyla yansıma ışını, Snell penceresinden kırılma ışını ve çay rengi soğurma, güneşin pırıltısı. Su gölge
  yapmaz (görünürlük maskesi).
- **Islaklık:** pürüzlü yüzeyler parlar (`wetness`), parlaklık ızgaranın ayna yönündeki ışığından.
- **Yerleşim (O3'ün kaba hâli):** sebepli kurallar: devrik gövde derenin üstünde kıyılara dayanır, kayalar kıyıda yarı
  gömülü, taşlar yatakta, ağaçlar ortadaki aralığın dışında, eğreltiler nemli kıyıda yoğun, geniş yapraklılar
  öbekler hâlinde, yosun yatağın ıslak kenarında halı. Gövdeler ve kayalar yürüyüşte engel.
- **Kenar gizlendi (vadi):** 44 m'lik ayrıntılı zeminin çevresinde 120 m'lik, 25 cm aralıklı ikinci bir zemin; 44 m'nin
  dışında yamaçlar dengesiz bir eğimle 28 m boyunca 9 m'ye kalkar (dere yatağının üstünde kalkmaz), dere parçanın dışında
  iki ucunda ters yönlere kıvrılıp gözden çıkar (su şeridi de onunla uzar). Parça birebir aynı kaldı: halkanın kopyaları
  parçadan sonra yerleşir. Halkada 20 dev ağaç (parçayla güneş arasındaki 35°'lik dilimde yok: ışığın geldiği aralık),
  yamaçlarda genç ağaçlar, yürünen alanın kenarından (15 m) dışarı göz hizasında bir çalı duvarı (90 cm'yi geçen alçak
  bitkiler, biraz büyütülmüş: bugün yalnız pachira). Izgaranın dışındaki yüzey ızgaranın en yakın yüzünün ışığını alır.
- **Katmanlı yoğunluk:** her kat kendi sebebiyle. Eğrelti ve küçük ot her yerde, bitkinin kendi genişliğiyle aralıklı,
  nemli kıyıda sık; geniş yapraklılar öbekler hâlinde. Pachira eğreltilerden ayrıldı: göz hizasında kendi çalı katı,
  aralarında geçit. Fidanlar diz boyundan küçük ağaca; 10 genç jakaranda orta kat (ışığın aralığında yok). Kabuk ve
  kuru dal ayrı kat. Bakışların yerinde 0,8 m açıklık: göz bitkilerin arasında durur, birinin içinde değil.
- **Yere düşmüş yapraklar:** ambientCG `LeafSet011` (kuru, kahverengi), `022` ve `023` (taze, sararan) atlaslarından;
  her yaprağın yeri opaklık haritasının adalarından, elle çizilen yok. Parçanın üstünde tek bir ağ: her yaprak zeminin
  kendi üçgenlerine yatan bükük bir dörtgen, kenarı kıvrık; gövde ve kayanın önüne yığılır, birkaçı yatakta batık;
  zeminin pişmiş kapanmasını alır, yeşiller sarıya boyalı. Kendi rastgele dizisi var: büyük parçalar (gövde, kaya,
  ağaç) yerinden oynamadı.

### Ölçümler (2026-10-27, RTX 4080)

| | |
|---|---|
| Varlık / prototip / malzeme | 23 / 80 / 30 (o anki indirme) |
| Kopya | 55.228; kopyalanmış üçgen ~400-418 milyon |
| Bellek | doku 2,0 GB, GAS 251 MB, geometri 341 MB, ızgara 121 MB, güneş hacmi 51 MB; uygulamada kart 7,8 / 16 GB |
| Yükleme | taramalar 7,4 sn, dünya 0,5 sn, yapılar 0,9 sn, zemin AO 16 ms, ızgara 4,1 sn, güneş hacmi 0,1 sn |
| 1440p, bakış 0, `--bench 30` | 1 örnek 5,8 ms · **4 örnek 24,2 ms** · 8 örnek 59 ms · 16 örnek 116 ms; pussuz 4 örnek 21,4 ms |
| Uygulamada (`--autotest-only orman`) | 46,7 FPS, kare 21,3 ms |
| Vadi ve halka (aynı oturumda `world.outer=44` ile kıyas, 1440p, 4 örnek, `--bench 30`) | bakış 0: 26,6 → 27,9 ms · bakış 1: 17,9 → 19,8 ms; kopya 55.228 → 64.457; yükleme ~+1,3 sn (ızgara 3,3 → 4,4 sn) |
| Titreme (bakış 1, 0,06 m/s yana yürüyüş, 1440p, 24 kare; `tools/titreme.py`, `tools/kararlilik.py`) | 4 örnek, düzeltmeden sonra: titreme 0,0106 (önce 0,0100; güneş beneği ısı haritasından gitti), kare ~25 ms (önce 19,8) · **DLSS: titreme 0,0047** (16 örnekli referans 0,0080), kıpırtı 0,0096 (4 örnek 0,0143), blok 0,0015 (0,0024); kare 13,4 ms (4 örnek 24,8) |
| Yoğunluk (önce → sonra, 1440p, DLSS, `--bench 30`) | kopya 64.457 → 119.822 + 191.917 yerde yaprak; kopyalanmış üçgen 3.481 M → 4.330 M · bakış 0-3: 15,6 / 13,2 / 15,8 / 15,4 → 17,0 / 16,0 / 18,8 / 18,5 ms · ızgara 6,3 → 6,8 sn · uygulamada 50,5 FPS, kart 7,2 GB · titreme 0,0035 (referans 0,0069, oran önceki gibi) |

### Kararlar (plandan sapmalar)

- **Güneş ışınla, ızgarayla değil:** plan güneşin görünürlüğünü 3B ızgarada pişirmeyi söylüyordu (ışın yok). Gölge
  ışını yaprak alfasıyla birlikte örnek başına bir ışın: yakın yaprağın keskin gölgesi ızgaranın çözemeyeceği kadar
  ince (30 cm'deki yaprağın yarı gölgesi ~3 mm), maliyeti ölçüde küçük. Izgara yalnız pusun huzmeleri için.
- **Su erken geldi:** plandaki gibi ışınla, ama O2'yi beklemeden, ilk bakış için.
- **Sonlu olmayan örnek sayılır ve atılır:** taramalarda sıfır alanlı üçgenler var; birinin NaN'ı ışıltı
  piramidinde siyah bir kare olarak yayılıyordu. Normal sıfırsa geometriden, o da sıfırsa ışından alınır.
- **Kareler arası bellek geldi (DLSS-RR):** ilk yol "her kare aynı, komşu piksel belleği yok"du. Alfanın süzülmesi
  güneş beneğini çözdü ama titreme 4 örnekte yerinde kaldı (0,0106): dal ve yaprakçık pikselden ince geometri, dört sabit
  örnek göz hareket ederken onu tutamaz. 16 örnek 116 ms; DLSS-RR tek ışınla 13,4 ms'de 16 örnekten daha sakin.
  NGX akışın sırasında çalışmadığı için akış önce ve sonra eşitlenir (eşitlemesiz görüntü bir kare geride kalıyordu).
- **Yaprak döküntüsü kopya değil tek ağ:** kopyalar zemine bir noktadan oturur, yamaçta havada kalır ya da gömülür;
  tek ağda her yaprak zeminin kendi üçgenine yatar. Kendi rastgele dizisi, başka hiçbir şeyi oynatmasın diye.

## Açık işler (sıra Özcan'ın bakışına göre değişir)

1. **Tropikal varlıklar:** Poly Haven'da yağmur ormanı ağacı, palmiye, sarmaşık, monstera, muz, eğrelti ağacı yok;
   bugünkü taç jakaranda ve küçük ağaçlarla "orman" okunuyor, "yağmur ormanı" değil. Sketchfab (CC-BY) için Özcan'ın
   anahtarı gerekiyor (PLAN §6).
2. **Özcan'ın yoğunluğa ve titremeye bakışı:** son istek "çok daha yoğun" ve "titremesin"di (*"%70 olmuş"*); ikisi
   de ölçüyle yapıldı, gözle bakışı bekleniyor. Yoğunluk katsayıları cfg'de, kare süresinde pay var (≤ 18,8 ms).
3. **Pachira yapraklarının altı çok koyu** (eski sorun; çalı katı göz hizasına çıkınca daha çok görünüyor).
4. Zemin karışımı (çamur, çakıl), kuru dal yığını (`StickSet001` indirildi, kullanılmadı), kompozisyon (devrik dev,
   payanda köklü ağaç), yaprak kenarının üç yolu, kör test, elde kamera kipi.

(Kapananlar, 2026-10-27: dünyanın kenarı vadiyle; güneş beneği greni alfanın kapsamasıyla; yürürken titreme DLSS-RR ile.)

## NE DOĞRULANMADI

- Yürüyüş, çarpışma ve bakışlar elle canlı denenmedi; yalnız otomatik test (açılış, panel, yardım) ve komut satırı
  kareleri.
- Titreme ve kıpırtı yalnız bakış 1'de, tek bir yavaş yana yürüyüşte ölçüldü; koşu, dönüş ve öbür bakışlar ölçülmedi.
  DLSS'in hızlı dönüşte iz bırakıp bırakmadığı (ghosting) bilinmiyor.
- DLSS SDK'sı olmadan derleme yolunun (sabit örneklere dönüş) bu son hâliyle sınandığına dair kayıt yok.
- Bakış 3'te tek bir sonlu olmayan örnek hâlâ var (atılıyor, görüntüde iz yok); kaynağı bulunmadı.
- Normal haritasının yeşil kanalının yönü (glTF'in v'si aşağı) mantıkla seçildi, ışık açısıyla sınanmadı.
