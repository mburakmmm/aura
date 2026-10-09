# Değişiklikler

Bu dosya yayımlanan sürümleri tutar. Sürüm numarası `nox.json` ile, git etiketi `v` önekiyle aynıdır. Sonraki teslimatların sırası [roadmap.md](roadmap.md) dosyasındadır.

## 0.5.0

Notes. Tek belge, düz metin. Paragraflar satır sonuyla ayrılır. Zengin metin, çift yön ve emoji yoktur. Kapı `scripts/certify.sh` ve GitHub Actions’tır: macOS, gizli Win32, X11 ve Wayland.

- Satır haritası her görünen satırın kaynak aralığını tutar. Sığmayan kelime karakterden bölünür. `wrap_text` bu satırların metnini döndürür.
- Seçim, kestiği her satıra ayrı dikdörtgen basar. İmleç o satırın dikey konumuna iner. Yukarı ve aşağı ok aynı yatay konumda bir görsel satır kayar.
- IME kompozisyonu imlecin olduğu aralıkta görünür. Commit mevcut ekleme yoluna girer. Karakter sanal tuştan üretilmez.
- `examples/notes` self-check’i sarılmış cümleyi seçer, panoya kopyalar, ikinci paragrafa yapıştırır ve IME commit’ini imlecin durduğu yere yazar.
- Sertifika önceki beş self-check’in ardından Notes’u gizli pencerede koşturur. Pencere açık kalmaz.

## 0.4.0

Kabuk sertifikası. Aynı Nox programı macOS, Windows ve Linux’ta geçer. Kapı `scripts/certify.sh` ve GitHub Actions’tır: `macos-latest`, gizli Win32, `AURA_LINUX_SHELL=x11` ile Xvfb, `AURA_LINUX_SHELL=wayland` ile headless Weston.

- Üç kabuk probe dosyasını `native/plugins/libaura_probe.dylib` adıyla üretir. Konak kütüphane adı `native/libaura_host.dylib` kalır.
- Windows kabuğu Skia’nın statik CRT arşiviyle bağlanır. Pikseller BGRA olarak `StretchDIBits` ile basılır.
- Linux kabuğu seçimi `create_window` anında kalır. X11 ve Wayland pixmap’i BGRA tutar. `aura_sample` vurgu rengini bu tampondan okur.
- Pano, imleç, dosya bırakma, IME kuyruğu, erişilebilirlik listesi ve plugin çerçevesi aynı Nox testleriyle zorlanır. Ayrı bir Windows veya Linux testi yoktur.
- Sertifika `./scripts/test.sh` ardından sayaç, dikdörtgen, todo, hesap makinesi ve `examples/proof` self-check’lerini gizli pencerede koşturur.

## 0.3.0

Masaüstü çerçevesinin üçüncü dilimi. macOS’ta doğrulandı. Windows ve Linux kabuk kaynakları ağaçtadır. Üç kabukta aynı self-check 0.4 kapısıdır.

- Skia raster, tuval komutlarını oynatır. macOS’ta CoreGraphics pixmap’i pencereye basar.
- Metin, depodaki OFL Noto Sans ile ölçülür ve çizilir. PNG ve JPEG Skia codec’i ile çözülür.
- Tek kabuk köprüsü `native/libaura_host.dylib` yolunu bağlar. `aura/platform/macos.nox` bu köprünün yeniden dışa aktarımıdır.
- Windows kabuğu Win32 olay kuyruğu, IME, Unicode pano, dosya bırakma ve UI Automation içerir.
- Linux kabuğu `WAYLAND_DISPLAY` varsa Wayland, yoksa X11 kullanır. Metin girişi, pano, bırakma, imleç ve AT-SPI aynı Nox sarmalayıcılarına bağlanır.
- Debug modu yerleşim sınırını ve kirlenen boyama kutusunu çizer. Profil faz sürelerini ve kurulum sayaçlarını tutar. Anlam ağacı değişmediyse yeniden yayınlanmaz.
- `register_view` bir Nox görünümü kaydeder. `PlatformView` yerli çocuk yüzeyi kabuğa yerleştirir.
- Örnekler: sayaç, dikdörtgen, todo, hesap makinesi ve `examples/proof`.
- Sekiz benchmark `benchmarks/suite.nox` altındadır.

## 0.2.0

Çoklu pencere, diyalog, menü, metin seçimi, pano, I-beam imleç, görüntü, sürükle-bırak, kaydırma çubuğu, macOS erişilebilirlik köprüsü ve todo uygulaması.

## 0.1.0

Widget, Element, RenderObject, kısıt tabanlı yerleşim, metin, düğme, durum, signal, animasyon, sanal liste, tema ve gezgin. Referans uygulamalar sayaç ve tododur.
