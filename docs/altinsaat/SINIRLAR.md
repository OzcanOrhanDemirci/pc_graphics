# SINIRLAR — Claude'un bu projede zorlandığı yerler

> Özcan'ın isteği: *"Senin sınırlarını görmek."* Dürüst ve kısa: nerede zorlandım, hangi deneme başarısız oldu, neyi
> göremedim ya da ölçemedim, neyi Özcan'ın gözü düzeltti. Proje sonunda Özcan'a anlatılır.

## Baştan bilinenler (planlarken)
- Hareketi izleyemiyorum; yalnız kareleri görüyorum. Titreşimi sayıyla ölçerim, son yargı Özcan'ın gözü.
- Sanat yönetimi: üretebilirim, ama "bu kare neden çarpıcı değil" sorusunda Özcan'ın bir cümlesi birkaç denememe bedel.
- İçerik (insan, karakter, taranmış varlık) üretemem; bu yüzden sahne ışık ve malzeme üzerine kurulu.

## Proje boyunca
- **Geometride ilk hesabım yanlıştı:** 40 cm duvar, 12 cm yarık ve 28° yan açılı güneşte ışık hiç girmiyordu
  (yanal kayma 21 cm). İlk kare simsiyah çıktı; kare bakmadan fark edemezdim.
- **Gürültünün kaynağını iki kez yanlış tahmin ettim.** Önce "güneş lekelerinden dolaylı ışık" dedim, onu çözen
  bir strateji yazdım: doğruydu ama eşit sürede kazanmadı. Sonra "gökteki hâle" dedim, onu da yazdım: o da kazanmadı.
  Asıl kaldıraç ölçünce çıktı: dokular sürenin 2/3'üymüş. Sezgim değil kapat-ölç tablosu buldu.
- **Fırın testini ilk kurduğumda test yanlıştı** (Rus ruleti kapalı odada sonsuz kuyruk), ikinci denemede gerçek bir
  hata yakaladı (pus geçirgenliği tutarsızlığı, %4,7).
- **Sanat:** ilk düzenlemeler "tek renk turuncu-bej" çıktı; beyaz dengesi ve kamera açısı (ışığa karşı) gerekti.
  F1 karesi hâlâ sütlü ve düz; neyin "çarpıcı" olduğuna dair yargım burada en zayıf yerim.
- **Bir API davranışını varsaydım:** OptiX yerleşik küresinin içeriden de vurulduğunu sandım; su dolu küre siyah çıkınca
  deneyle (içeriden vuruşları boyayan bayrak) ölçtüm. İlk deney tasarımım da hatalıydı (işaret tek dalga boyundaydı).
- **Kabuk alıntılaması** birkaç kez düzenleme betiklerimi bozdu (ters bölü-n kaçışı satır sonuna dönüştü); betikleri
  dosyaya yazarak çözdüm.
- **Gökkuşağının nereye düşeceğini önce tahminle aradım** (açı taraması); prizmanın çoğu açıda tam iç yansıma yaptığını
  görünce 2B ışın izleme betiği yazıp hesapladım.
- **Hareketi göremiyorum:** canlı modun titreşimini ardışık kare farkıyla ölçtüm; gözle onay Özcan'da.
- **Sanat yargısı:** "düz ve zeytin" görüntünün sebebini bulmam birkaç tur sürdü (önce pusa, sonra pozlamaya baktım; asıl
  sebep serin dolgu ışığının yokluğuydu). Kahraman kompozisyonu (natürmort) altıncı denemede çıktı.
- **"Daha çok örnek ver" sezgisi yanıltıcıydı:** prizma piksellerine 16 örnek verdim, kare süresi 11'den 48 ms'ye çıktı;
  örnekler aynı iş parçacığında sıralı koşuyordu (GPU'da kuyruk gecikmesi). Örnek sayısını değil tabakalamayı artırmak
  (aynı yolu 4 dalga boyuyla) işe yaradı.
- **Fırın testi bir kez daha kendi tutarsızlığımı yakaladı** (+%0,15): fırın kipinde köpükleri ışınlar için opak, gölge
  ışınları için geçirgen bırakmıştım. Sayı küçüktü, eşik içindeydi; yine de iki tohumda tutarlı çıkınca kovaladım.

## Sonuç: sınırlarım nerede çıktı
1. **Fizik ve doğrulama güçlü yanım:** spektral ışık taşıma, foton akısı, MIS ağırlıkları, beyaz fırın, Sellmeier ve ince
   film testleri ilk ya da ikinci denemede doğru çıktı; hataları ölçerek buldum (fırın, FLIP, yol izlemeye karşı foton oranı).
2. **Sezgim performansta ve gürültüde yanılıyor; ölçüm düzeltiyor.** Üç "akıllı" fikrim (ışık lekesi örneklemesi, gök
   haritası, prizma piksellerine çok örnek) ölçünce kaybetti; kazananlar sıkıcıydı (dokuyu pişirmek, toza görünürlük maskesi).
3. **API'leri varsayıyorum;** yerleşik kürenin içeriden vurulmaması gibi bir davranışı ancak siyah kareyi görünce deneyle
   doğruladım.
4. **Sanat yönetimi en zayıf yerim.** Teknik olarak doğru ama "düz" kareleri uzun süre ürettim; sebebi (serin dolgu ışığı
   yok, kahraman öge küçük) bulmam birçok tur sürdü ve hâlâ kendi gözümle "şaşırtıcı mı" diyemiyorum. Bunu Özcan söyleyecek.
5. **Hareketi göremiyorum:** canlı modu yalnız sayılarla (fps, kare farkı, FLIP) ve tek tek karelerle değerlendirdim.
6. **Canlı mod durağanın kalitesine yetişmiyor:** en çok prizmada (canlıda tayf karıncalı). 1 örnek/kare + yapay zekâyla
   büyütme bu sahnede tavan.
