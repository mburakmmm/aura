# ADR-012 Skia boyama ve metin ölçümü

## Context

ADR-004 ilk boyamayı CoreGraphics ve Core Text olarak kilitledi. Üç masaüstü kabuğu aynı pikseli üretmek için ortak bir oynatıcı ister. Widget ağacı ve tuval metotları değişmeden kalmalıdır. Nox, Skia tiplerini görmez.

## Decision

Boyama ve metin ölçümü Skia raster yüzeyi üzerindedir. Dylib Skia C++ çağırır. Nox yalnız mevcut C sembollerini görür: `clear`, `fill_rect`, `fill_text`, `push_opacity`, `pop_opacity`, `draw_image`, `measure_width`, `measure_height`. HarfBuzz Skia’nın içindedir. Şekillendirme elle yazılmaz.

Metin, depodaki OFL Noto Sans dosyasıyla ölçülür ve çizilir. İstenen aile bulunamazsa bu font kullanılır. PNG ve JPEG Skia codec’i ile çözülür. Tutamak tamsayıdır.

macOS penceresi pixmap’i CoreGraphics ile yapıştırır. Metal bu kararda yoktur. Kabuk, mantıksal noktayı pencere ölçeğiyle piksele çevirir.

## Alternatives

Üç ayrı tuval (CoreGraphics, Direct2D, Cairo) aynı komut listesini üç metin motoruyla çizerdi. Resmi `sk_*` C başlıkları metin ve görüntü için yetersizdi. GPU yüzeyi pencere yapıştırmasını kabuğa özel bir ikinci yol yapardı.

## Consequences

`native/libaura_host.dylib` Skia statik kütüphanesiyle bağlanır. Penceresiz testler `RecordingCanvas` kullanmaya devam eder. Metin ölçümü dylib ve font dosyası ister. Çalışma dizini depo köküdür.

## Migration

ADR-004 ve ADR-008’in fonksiyon adları durur. Core Text ve ImageIO çizim yolu kabuktan çıkar. Eski `libaura_macos.dylib` yolu `libaura_host.dylib` olur.
