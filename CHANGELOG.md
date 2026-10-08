# Değişiklikler

Bu dosya yayımlanan sürümleri tutar. Sürüm numarası `nox.json` ile, git etiketi `v` önekiyle aynıdır. Sonraki teslimatların sırası [roadmap.md](roadmap.md) dosyasındadır.

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
