# PLAN · Ada'yı zenginleştir (dubaların içi dolu, dışı serpiştirilmiş, uzağı sakin)

> Tek kaynak plan. 2026-10-04 akşamı Özcan'ın emriyle, **tam otonom, ajansız**. Önceki: `PLAN_ADA.md` (🏁), durum
> `DURUM.md`. Bağlam sıkışırsa önce bu dosya, sonra DURUM'un başı, sonra hafızada `genel/feedback_gpu_ve_grafik.md` §24.

## 1 · Özcan'ın sözü (özet; birebiri hafızada `grafik/project_ada_zengin_gorevi.md`)

Sahneyi malzeme olarak zenginleştir: daha fazla kaya, deniz yıldızı, tabanda yeni 8K dokular. İskeleden gemiye kadar ve
geminin çevresi dahil daha dolu, gerçek su altı zenginliği. Alan küçük (dubaların içi): ekstra özen. Gerçekçilik için görüş
alanına giren her yere serpiştir: dubanın dışına bakınca da bir sürü şey olsun (her yer duba içi gibiymiş ilüzyonu);
uzaklara gidince daha boş ve sakin olabilir. **Amaç inandırıcılık.** Tamamen özgür.

## 2 · Sınırlar (değişmez)

- Hareketli bitki/hayvan yok (*"yosun fiziği bu sahnenin amacına ters"*). Her şey durağan.
- Netlik ve kararlılık alt sınır; pencerede **≥ 30 fps** (başlangıçta 30-32: her ekleme ölçülür, gerekirse başka yerden
  kazanılır). Bellek kartta 13,6 / 16 GB: yeni 8K set ~0,7 GB.
- Taramalar en az 8K. Gerçek türler, gerçek yerleşim mantığı (kaya üstünde ne yaşar, kumda ne yaşar, derinlikle ne değişir).

## 3 · Bölgeler (z kıyıdan açığa eksi; koridor x ∈ [−15, 15])

| Bölge | z | Derinlik | Ne |
|---|---|---|---|
| A · sığ kum | 0 → −38 | 3,5-9 m | dalgacıklı kum + kabuk kırıntısı yamaları; çakıl öbekleri; koridor kenarlarında alçak kayalık yamaları (yosun örtüsü, süngerler, Padina, kestaneler); kırmızı ve turuncu deniz yıldızları, kum yıldızları (Astropecten), deniz hıyarları, yelpaze midyeleri, boş kabuklar |
| B · amforalar | −38 → −66 | 9-14 m | var olan kaya yuvası + yosun ve süngerler; ahtapot yuvası (kabuk yığını); tek tük amfora ve parçaları |
| C · batığa iniş | −66 → −86 | 14-20 m | seyrek taşlar, tarak kabukları; geminin enkaz izi (tahta, fıçı çemberi, gülle) yamaçtan aşağı batığa |
| D · batığın çevresi | −86 → −116 | 18-25 m | enkaz alanı: tahtalar, fıçı ve çemberleri, gülle yığınları, safra taşları, kırık çömlek, makara; enkaz üstünde sünger ve yelpaze; gövdenin dibinde kum oyuntusu |
| E · şamandıraya | −116 → −160 | 25-47 m | seyrek: taşlar, tüp anemonları (Cerianthus), kum yıldızları; derin, sakin |
| Dış | koridorun ±15-45 m dışı | — | aynı bölgelerin seyreltilmişi (kaya kümeleri, yamalar), uzaklaştıkça azalır; ötesi sakin kum |

## 4 · Sıra

1. **Taban çeşitliliği:** ikinci kum taraması (8K, kabuklu/iri taneli) ve yükleme anında hesaplanan malzeme haritası
   (gürültü + kayaların ve batığın çevresi): yamalar, kayaların dibinde çakıl, batığın çevresinde koyu silt.
2. **Kayalar:** koridor kenarlarında ve dışında kaya kümeleri (uzaklık alanı + yüzey ağı, kireçtaşı ayrıntı katmanı,
   kabuk ve canlı lekeler), üstlerinde deniz kestaneleri, süngerler, Padina.
3. **Kum canlıları ve kabuklar:** daha çok ve çeşitli deniz yıldızı, kabuk türleri, ahtapot yuvası, tüp anemonları.
4. **Batığın enkaz alanı.**
5. **Dış serpiştirme** (uzaklıkla seyrelen).
6. Ölçüler (fps, kararlılık, bellek), DURUM, hasat.

Her adımda kareler telefona, commit + push. Kare göstermeden büyük öğe bitirilmez.
