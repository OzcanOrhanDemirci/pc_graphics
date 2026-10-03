# PLAN · sahne 5, ikinci bölüm: "Amfora" (batık bir geminin yükü ve uzakta doğal kemer)

> Tek kaynak plan. 2026-10-04 gecesi Özcan'la konuşularak kesinleşti. Koy'un K0-K1'i ve üç ara durağı bitti (durum ve
> bütün ölçümler `docs/koy/DURUM.md`). Bu plan Koy'un K2'sinin (koyun kendisi) yerini alır; eski plan `PLAN.md` mimari,
> veri ve ölçü bölümleri için başvuru olarak durur. Bağlam sıkıştırılırsa önce bu dosyayı, sonra DURUM.md'nin başını,
> sonra hafızada `genel/feedback_gpu_ve_grafik.md` §14, §19, §20, §21'i oku.

## 1 · Görev ve yetki

Özcan (2026-10-04 gecesi):
- *"Amfora yapacağız, aşırı gerçekçi, çok yüksek kaliteli doku ve netlik hedefi aynen geçerli, yücelik hissi için
  ışıklandırmayı ayarlayacaksın. Kemer de bunu destekleyecek."*
- *"yeni oturumda grafik devamdan sonra artık bana bir şey sormadan bu sahneyi, tüm görevleri bitirene kadar otonom devam
  et. En iyi sonucu alana kadar devam etmekte özgürsün. Eğer yolda bir engelle karşılaşırsan amforadan vazgeçmekte bile
  özgürsün [...] amaç senin sınırlarını görüp yapabileceğin en iyiyi görmek."*
- *"Ben yarın uygun olunca bitmiş haline bakarım."* Kota: haftalığın tamamı kullanılabilir (sabah yenilenir); 5 saatlik
  kota %95 civarında durulur, yenilenince devam edilir (§9).
- *"Çok fazla paralel agent kullanma. Her şeyi kendin yap. Çok lazım olursa 1 tane paralel agent kullanabilirsin."*
- Kostik: *"Hüzme yöntemiyle kendi en iyi bildiğin şekilde yapmak daha sağlıklı olur."*
- Daha önce (10-03): *"Ancak asıl odağı ve max kaliteyi amfora tarlasına harcarsın."* · Kemere *"gidilebilir ama uzakta
  olduğu için biraz yüzmek gerekir. Dibine gidince de 8k dokulu kemer olur."*

Durak yok: baştan sona kendi başıma, kararlar bende; her karar gerekçesiyle DURUM'a yazılır. Özcan sonunda uygulamanın
içinden canlı bakar (fotoğraf ya da video değil).

## 2 · Öncelik sırası (çatışırsa)

1. **Netlik ve kararlılık** (değişmedi): sabit örnekleme, zamansal tahmin yok, her yolda Fener'in yarısından iyi. Kostik
   gürültüsü artık ölçünün içinde (§7).
2. **Amfora tarlasında hiper gerçekçilik:** gerçek bir batık fotoğrafıyla yan yana durabilmek.
3. **Yücelik:** ölçek, ters ışık, ışık sütunları, maviye kaybolan derinlik.
4. **Fizik:** ışık ve temas gerçek; sanat için fizikten sapılan her yer kodda ve DURUM'da adıyla işaretlenir.
5. **Kare hızı:** varsayılan ayarlarla 41 fps; 30'un altına inilmez.

## 3 · Sahnenin kendisi (benim kararlarım; ölçü ve etütle değişebilir)

| Konu | Karar |
|---|---|
| Ad | Galeride **"05 Amfora"** (EN "Amphorae"); kodda `koy` kalır (`--scene koy`), ad değiştirmek boşa iş |
| Hikâye | MÖ 1. yy, bir Roma ticaret gemisinin şarap yükü: gövde çürümüş, amforalar düştükleri düzende bir tümsek |
| Amfora tarlası | ~10 × 6 m, **~12 m derinlikte** kum düzlüğünde: 40-80 amfora, 3-4 tip (Dressel 1B ağırlıkta), sıra izi bozulmuş, bir kısmı yarıya kadar kuma gömülü, ağızlar dışarıda, birkaçı kırık, çevrede kırık parçalar |
| Kemer | **Doğal kireçtaşı kemeri** (Akdeniz karstı; Kaş, Malta gibi): ~30 m ötede, zemin oraya doğru ~20 m'ye iner, kemer ~14 m yüksek, ~8-10 m açıklık, tepesi yüzeye ~5 m. Yüzülerek gidilir, dibinde 8K |
| Işık | Güneş kemerin arkasından ve biraz yandan: kemer ters ışıkta siluet, ışık sütunları açıklıktan ve kenarlardan tarlaya doğru akar. Derinlik (12 → 20 m) renkleri kendiliğinden maviye çeker |
| Canlılık | Az: Posidonia çayırı yalnız kenarlarda (Akdeniz batıklarının gerçek çevresi), amforalarda deniz kabuğu ve tortu; balık yok |
| Dalgıç feneri | **Aday (A5'te karar):** amforanın gerçek pişmiş toprak rengini yakında gösterir, tayfsal ışık yolu zaten var; bedeli ölçülür |
| Doku | Kum 16K (Ultra), dalgacıklar açık; amforalar ve kemer 8K (gerçek ölçekte ekran sınırı); 16K yalnız büyük alanlı taramalarda |
| Kapsam dışı | Gece, yakamoz, kabarcık, yelek fiziği, Bühlmann, balık sürüleri, su üstü (eski planın K3-K4'ü): dar alanda azami kalite için |

**Bir bakışta:** önde kumdan taşan, kabuk bağlamış amforalar; arkada aşağı inen kum yamacı ve maviye gömülü dev bir kaya
kemeri; açıklığından süzülen ışık sütunları tarlanın üstüne iniyor; tepede dalgacıklı Snell penceresi.

## 4 · Kaynaklar ve malzeme (A0)

- **Amfora geometrisi:** önce gerçek taramalar (müze ve arkeoloji taramaları: Sketchfab, Smithsonian 3D, Scan the World;
  CC0 ya da CC-BY, en az 4K doku, gerçek ölçek). Uygun yoksa ya da yetmezse: yayımlanmış tipoloji çizimlerinden (Dressel 1B,
  Dressel 2-4, Lamboglia 2) **döner yüzey** + kulplar, kodla kırık ve aşınma. Lisans `assets/koy/LICENSES.md`'ye.
- **Malzeme:** 8K CC0 pişmiş toprak ve kil taramaları (Poly Haven, ambientCG; seçim ölçüyle: fotoğraflanan alan / 8192 ≤ 0,2
  mm); üstüne kodla katmanlar: deniz kabuğu (serpulid boruları, bryozoa, kireçli kırmızı alg), tortu dolgusu, kum birikimi
  (yukarı bakan yüzlerde), kırık kenarda temiz kil rengi.
- **Kemer:** geometri imzalı uzaklık alanından (gürültüyle şekillenmiş kireçtaşı), ağa dönüştürülür; malzeme triplanar 8K
  kaya taramaları + ayrıntı katmanı (rock_boulder_dry gibi), aşağıda tortu ve kabuk.
- **Başvuru:** gerçek Akdeniz amfora batığı ve kaya kemeri fotoğrafları `docs/koy/BASVURU.md`'ye (bağlantı + not).

## 5 · Teknik iş paketleri

1. **Kostik hüzme yöntemi (gürültüsüz).** Şimdiki yöntem dokuya ~1 ışın düşürüp sayıyor; kostik bu yüzden kaynıyor (16 kat
   ışınlı referansa karşı kıpırtı 0,0113, olağanın ~35 katı). Yeni yöntem: yüzey ızgarasının her hücresi bir ışık hüzmesi;
   her derinlik diliminde dört köşe ışınının vardığı yerden hücrenin alanı, ışık o alana yayılır (alan küçüldükçe parlar).
   Sayma yok, gürültü yok; odak çizgileri doğal olarak keskin. Güneş diski bulanıklığı aynen kalır. Enerji testi (`--test
   kostik`) yeni yönteme uyarlanır. Eski yol yedek olarak ayarla seçilir.
2. **Derin sahne:** zemin 12 → 20 m yamaç; ışık alanı ve tablolar yeni derinliğe (tablolar 40 m'ye kadar zaten var; ışık
   alanının dip derinliği yamaca göre, gerekirse iki derinlik), kostik dilimleri 0,5-25 m, güneş açısı ve yönü.
3. **Amforalar:** ağ (tarama ya da döner yüzey), örnekleme (instancing) ve çeşitleme (dönüş, gömülme, kırık, kabuk
   maskesi tohumu), yerleşim (gemi gövdesinin izini süren tümsek), kum içine gömülme (kesişen kum çizgisi + koyu temas
   tortusu), çarpışma (26 yönlü yoklama zaten var).
4. **Kemer:** SDF → ağ, uzak/yakın ayrıntı, malzeme, çarpışma; tarlaya bakışta siluet okuması.
5. **Işık ve yücelik:** güneşin yeri, sütunların kemerden geçişi, pozlama uyumu, (feneri ölç ve karar ver).
6. **Bellek:** şimdi kartta 10,5 GB (16K setler + üç doku kipi birlikte). Amforalar ve kemer eklenince BC7/BC5 sıkıştırma
   ve disk önbelleği; gerekirse Standart ve Yüksek kipleri belleğe yalnız seçilince yüklenir. Hedef ≤ 14 GB.
7. **Galeri ve arayüz:** ad, açıklama, galeri resmi, tuşlar; panelde gereksizleşen düğmeler (kostik ışını) kaldırılır
   ya da sadeleşir.

## 6 · Fazlar (durak yok)

| Faz | İş | Bitiş ölçütü |
|---|---|---|
| A0 | Kaynak araştırması ve seçim (§4), başvuru panosu, kompozisyon etüdü: gri kutularla tarla + kemer + güneş, kameradan bakış | Bakışta okuma sırası doğru (tarla, sütunlar, kemer) |
| A1 | Kostik hüzme yöntemi + yeni kostik gürültü ölçüsü | Gürültü kıpırtısı ≤ 0,003; kostik süresi ≤ ~6 ms |
| A2 | Derin zemin, ışık alanı, güneş | Testler geçer; ilk bakış ölçüleri |
| A3 | Amforalar (tek amforayı kusursuz yap, sonra tarla) | Yakında tekstel ≤ 0,2 mm; tekrar göze batmıyor |
| A4 | Kemer | Uzaktan siluet, dibinde 8K, çarpışma |
| A5 | Işık ve yücelik, fener kararı | Kendi sanat turum: "en kötü 3 şey" listesi boşalana kadar |
| A6 | Netlik, kararlılık, hız, bellek turu (bütün yollar) | §7'nin tamamı |
| A7 | Bitiş: galeri, belgeler, otomatik test, tek bir bağımsız gözden geçirme ajanı, hasat | DURUM'da "Özcan'ın bakacağı yer" |

Bir faz kötü giderse sıra değişebilir; amfora olmazsa (kaynak, bellek, görüntü) başka bir özneye geçmek dahil her karar
serbest, gerekçesiyle yazılır.

## 7 · Başarı kartı (ölçülür)

- **Kararlılık ve netlik:** yollar: `sabit`, `yuz`, `yukari`, `kum`, yeni `amfora` (tarlanın üstünde 0,5-1 m'den süzülme),
  yeni `kemer` (kemere yüzüş). Her ölçüde Fener'in yarısından iyi (Fener: piksel 0,0053-0,0061, blok 0,0011-0,0026,
  bağıl 0,45-1,24, FLIP 0,026-0,038).
- **Kostik gürültüsü:** canlı = varsayılan, referans = çok ışınlı kostik (`kararlilik.py`): piksel kıpırtısı ≤ 0,003
  (şimdi 0,0113).
- **Hız:** varsayılan ayarlarla (4 örnek + kenar 16, Ultra, dalgacık) her yolda 41 fps; en kötü %1 kare yazılır.
- **Doku:** amforada 30 cm'den tekstel ≤ 0,2 mm (`--doku` yöntemiyle), örnekler arasında göze batan tekrar yok.
- **Bellek:** kartta ≤ 14 GB (masaüstü dahil).
- **Fizik testleri:** snell, optik, kostik, dalga ve eklenenler geçer.
- **Görünüş:** başvuru panosundaki gerçek fotoğraflarla kendi kıyasım; "en kötü 3 şey" listesi DURUM'da.

## 8 · Ölçü araçları (hazır)

`tools/kararlilik.py` (kıpırtı + FLIP), `tools/doku_kiyas.py` (FLIP Özcan'ın ekranıyla, en ince oktav, kırpıntı;
`--kipler`), CLI: `--kararlilik --yol ...`, `--doku`, `--dalgacik`, `--yokla`, `--zaman N`, `--headless` (+
`display.exposure_fixed`), `--test [optik|kostik|snell|dalga]`, `--autotest-only koy`. Özcan'ın ekranı: 32,5 inç,
2560×1440, yakından bakıyor (FLIP 50 cm, 31,1 ppd). FLIP kalibrasyonu (Özcan'ın gözü): ~0,29 "gece-gündüz", ~0,13 "net
fark"; yerel değişimler ortalamada sulanır, kırpıntıya da bak.

## 9 · Otonom çalışma kuralları

- **"grafik devam" gelince:** `git pull` (iki depo) → bu dosya → DURUM.md "Şu an" → kaldığın yerden. Özcan'a soru yok.
- **Kota:** her ~30 dakikada ve her büyük işten önce `cat ~/.claude/kota-durumu.json` (yaşı "yazildi" alanında; 15
  dakikadan eskiyse bayat). 5 saatlik `used_percentage` ≥ 93 ise: elindeki küçük adımı bitir, hafıza ve proje commit +
  push, sonra **CronCreate** tek seferlik (`recurring: false`) `resets_at` + 3 dakikaya (yerel saat: `date -d @<resets_at>`;
  :00 ve :30'a düşürme) şu istemle: *"grafik devam: 5 saatlik kota yenilendi, docs/koy/PLAN_AMFORA.md ve DURUM.md 'Şu
  an'dan devam"*. Sonra turu bitir (oturum boşta kalmalı ki tetiklensin). Haftalık kota tamamen kullanılabilir; ≥ 97 ise
  aynısı haftalık `resets_at`'e.
- **Ajan:** paralel ajan en çok 1, yalnız gerçekten gerekirse (örneğin sondaki bağımsız gözden geçirme). Gerisi kendim.
- **Commit:** ~30-60 dakikada bir; sıra hafıza → `claude_memory` push → proje push. Türkçe mesaj, atıf satırları.
- **Doğrulama:** her görsel karar canlı yoldan, hareket hâlinde ölçülür; kendi önceki sonucum kaynak değildir; bir sayıya
  göre güç ayarlıyorsam sayıyı alandan ölçerim (dalgacık dersi). Ekran kartı boş: tam güç kullanılabilir.
- **Bitince:** DURUM'un başına "Özcan'ın bakacağı yer" (nasıl açılır, nereye bakılır, panel), hasat (hafızaya kalıcı
  dersler), son commit; yeni uyandırma kurulmaz. Özet "NE DOĞRULANMADI" ve "hafıza itildi" ile biter. Uzun çizgi yok.
