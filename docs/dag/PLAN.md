# Sahne 13 · Dağ · plan

**Görev (tek):** uzak çizim mesafesinde dar alanda en iyilerin üzerine çıkmak.

**Özcan'ın brief'i (2026-11-04, birebir):**

> 13.sahne: Bir dağın tepesindeyiz, güzel manzaralı bir atmosfer var. Günbatımı. Dağdan aşağıya patika yok. Korkuluklar var ve küçük bir alandan ayrılamıyoruz. w a s d ile hareket ediyoruz. Dar alan hedefi: Uzak çizim mesafesi. Ufka kadar her yeri görebilmek. Doğa manzarası. Uzaklaştıkça doğal şekilde görüntü kayboluyor ama çizim mesafesi aşırı uzak. Dar alanda en iyi olma hedefi, uzak çizim mesafesini en iyi yapabilmek.

## Kilit fikir

Oyunlar uzağı ayrıntı seviyesiyle (LOD, HLOD, impostor) çizer, çünkü göz her yere gidebilir. Burada göz küçük bir terastan çıkamıyor. Bu kısıt her şeyi değiştirir:

1. **Ayrıntı seviyesi yok.** Terastan görülebilen her hücre önceden bulunur (8 göz konumundan görüş taraması) ve üçgen olur. Bir kez kurulur, hiç değişmez: belirme, geçiş, atlama olamaz.
2. **Gerçek yer.** Uzakta en zayıf halka gürültüyle üretilmiş arazidir; gerçek yükseklik verisi (Copernicus 30 m) her ölçekte gerçek görünür. Yer: **Kaçkar Dağı doruğu** (3.921 m modelde). Batıda Karadeniz'e batan güneş ve denizde güneş yolu, kuzeyde Kafkaslar (Elbruz 299 km), güneydoğuda Ağrı (295 km).
3. **Dürbün kanıttır.** Sağ tıkla 30 kata kadar: 300 km ötedeki Elbruz'un iki tepesi seçilir, panel adını ve uzaklığını söyler. LOD'lu bir sahnede dürbün hileyi açığa çıkarır; burada aynı üçgenler.
4. **Solma fizik.** Rayleigh, Mie, ozon, çoklu saçılma (Hillaire 2020), dağların havadaki gölgesi (gölge yüksekliği haritası). Dünya'nın eğriliği geometride (etkin yarıçap 7.323 km, k = 0,13).

## Teknik

| Parça | Nasıl | Neden |
|---|---|---|
| Yükseklik | Copernicus GLO-30; herkese açık olmayan 16 karoda (Ermenistan, Azerbaycan ve sınır karoları, Ağrı dahil) SRTM | GLO-90 yerine: Ağrı 90 m'de kalıyordu |
| Uzak zemin | 1 yay saniyesi modelin görünen hücreleri, derece başına GAS + tek IAS, OptiX | Üçgen çok az çıktı (görünen pay küçük): 1 yay saniyesi her yere sığdı, 300 km ötede de |
| Yakın zemin | İki kutupsal ağ (göz çevresinde, hücre uzaklıkla büyür): 1,5 km'ye ve 30 km'ye | 30 m model yakında bulanık. Üstüne eklemli kaya (blok, çatlak), iri taş alanı, teras taşları |
| Renk | Sentinel-2 cloudless 2016 (CC BY 4.0), 3 katman, uydunun kendi güneşi çıkarılır | Sonraki yıllar NC, kullanılamaz |
| Işık | Güneş ufku haritası (doğrusal yay çizgileriyle tam çözüm), penumbra analitik; yakın ve orta halkada gölge ışını; gök payı fırın | Güneş alçak: en güzel şey uzun gölgeler |
| Hava | Bir kez tabloya: gökyüzü (4096×1536), froxel (1024×768×48) | Göz sabit: tablo kareye maliyetsiz, gürültüsüz |
| Görüntü | 16 sabit örnek, ton eşleme sonrası ağırlıklı toplama, AgX | Birikim yok, gren yok |
| Deniz | Örneğin seçebildiği dalga yüzeyi eğer, seçemediği eğimleri genişletir (Beckmann) | Yakınlaştıkça parıltı dalgaya dönüşür, enerji aynı |

## Karşı görüşler (kendime)

- **"Skybox gibi olmaz mı?"** Hayır: her piksel gerçek ışın, yürüyünce paralaks gerçek. Yine de ufuk paralaksı doğal olarak sıfıra yakın.
- **"Gerçek yer, 'sanat dizaynı' değil."** Kompozisyonu güneş azimutu kurar (299°, temmuz batışı, denize). Yer seçimi kompozisyon kararıdır.
- **Satır dışı hile:** güneş yüksekliği ufuk haritasıyla serbest, azimutu sabit. Şimdilik tek an.

## Bitti tanımı

1. Galeriden açılır, W A S D ile korkuluk içinde yürünür, dışarı çıkılmaz. Kontrol sözleşmesi tutar.
2. Görünen en uzak yer ölçülür (`--menzil`), bilinen zirveler doğru yönde ve yükseklikte durur.
3. Canlı görüntü kararlı: `tools/kararlilik.py` piksel kıpırtısı dönüşte ve yürürken Fener'in TAA tabanının (0,0053-0,0061) altında, 16 örnek 64 örnek referansa göre `tools/titreme.py` oranı en çok 1,10. (İlk yazım "uzak bantta 0,001" diyordu: uzak bant yürürken durduğu için ölçüm boştu; düzeltildi.)
4. 1440p'de 30 FPS üstü.
5. Son hakem: Özcan'ın gözü, canlı.
