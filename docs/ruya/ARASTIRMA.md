# Rüya araştırma özeti · 2026-11-07

Üç bağımsız alt ajan ışık taşıma, görünür dünya/malzeme ve ölçüm altyapısını araştırdı. Bulgular uygulama öncesi değerlendirildi; kaynakların örnek performansları bu bilgisayarın ölçümü sayılmadı.

## Seçilen yol ve sınırı

Opak üçgenler, sıkıştırılmış prototip GAS'ları ve görünür hücre IAS'ı, RTX 4080'in donanım kesişim yolunu kullanır. Büyük bir özel AABB'nin içine bütün dünyayı koymak, ayrıntılı kesişimi RT çekirdeklerine taşımaz. [NVIDIA, Creating Optimal Meshes for Ray Tracing](https://developer.nvidia.com/blog/creating-optimal-meshes-for-ray-tracing/) boşluklu büyük yapıların ve ince uzun üçgenlerin maliyetini açıklar. [OptiX mühendisi açıklaması](https://forums.developer.nvidia.com/t/customize-intersection-method-for-built-in-triangle/234924) özel kesişimin programda çalıştığını ayırır.

Görüş dışını silmenin ışığa etkisi birincil kaynaktan doğrulandı. “Some ray-traced effects, like reflections or shadows, require support for out-of-frustum objects for rendering accuracy.” İlk vekil sorgu görüş dışı kemer ve bitki ayrıntısını kaybediyordu. Son yol, aynı üçgen kalıpları ışığa kısa ömürlü yerleşimler olarak sunuyor. `--full-world` ile kamera maskesi de kaldırılabiliyor ve aynı dünyada görünürlük karşılaştırılabiliyor. [NVIDIA, Practical Tips for Optimizing Ray Tracing](https://developer.nvidia.com/blog/practical-tips-for-optimizing-ray-tracing/)

Prosedürel malzeme sınırsız frekans demek değildir. PBRT tanımı: “One of the most crucial properties of a practical noise function is that it be band limited with a known maximum frequency.” [PBRT, Noise](https://www.pbr-book.org/3ed-2018/Texture/Noise). Yüzeydeki ince ayrıntı ışının piksel ayak izi büyüdükçe bastırılır; eğik ve hareketli kamera ayrıca sınanır.

## Şimdilik seçilmeyen adaylar

- Eski displacement micro-mesh örnekleri [NVIDIA deposunda arşivlenmiş](https://github.com/nvpro-samples/vk_displacement_micromaps). Güncel [RTX Mega Geometry](https://github.com/NVIDIA-RTX/RTXMG) örneğinin CLAS yolu bu OptiX projesine bir ayar eklemek değildir. API/donanım desteği ve aynı geometride kazanç ölçülmeden “4080 hızlanması” denmez.
- ReSTIR PT, çok güçlü ama ayrı bir zamansal örnek tekrar kullanım sistemi. [RTXDI ReSTIR PT kılavuzu](https://github.com/NVIDIA-RTX/RTXDI/blob/main/Doc/RestirPT.md) Ray Reconstruction için zamansal korelasyonsuz gürültü gereğini açıklıyor. Her iki karmaşık sistemi başlangıçta birlikte eklemek ölçümü ve hata ayırmayı güçleştirir. Rüya ilk adayda geçmiş kullanmayan gürültü giderme ve aynı örneklerle çalışır.
- [Neural Radiance Caching](https://research.nvidia.com/publication/2021-06_real-time-neural-radiance-caching-path-tracing) öğrenilen ışık önbelleğidir; yeni beliren dünyanın hemen doğru aydınlanması ve durunca değişmeme hedefiyle doğrudan eşit değildir.

## Dış ölçü ve içerideki sayaç

[CUDA Best Practices, GPU timers](https://docs.nvidia.com/cuda/cuda-c-best-practices-guide/index.html#using-cuda-gpu-timers) event aralığının GPU işinin zamanı olduğunu tanımlar. Bu yüzden GPU izleme/giderme/ton eşleme aralıkları ile CPU duvar saati ayrı kaydedilir. Headless ölçüm GL sunumunu ve monitörü içermez. [FLIP](https://github.com/NVlabs/flip) görüntü farkını inceler; sanat veya fotogerçekçilik onayı değildir. Tekrar üretim, bellek, görünürlük ve örnek yakınsaması ayrı kapılardır. Eşikler PLAN.md içinde ölçümden önce yazıldı.

Ham araştırma notları bu görevin çalışma klasöründe tutulur. Depodaki bu özet uygulamanın dayandığı yöntem ve bilinen sınırların kalıcı kaydıdır.
