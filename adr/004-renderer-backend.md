# ADR-004 Çizim backend'i

## Context

Aura grafik tipleri bir üreticinin tiplerini taşımaz. İlk prototipte yazılım rasterizer yazılmaz. Renderer seçimi bu ADR ile kilitlenir. Nox `extern def` `-framework` bağlayamaz; yalnız kütüphane adı veya `.dylib` yolu bağlar.

## Decision

İlk backend platform tuvalidir. macOS penceresi Cocoa, çizim CoreGraphics, metin ölçümü ve çizimi Core Text'tir. Bu üçü ve CoreVideo, `native/macos/libaura_macos.dylib` içine bağlanır. Nox bu dylib'i yol olarak bağlar.

Aura tarafındaki kayıt yüzeyi `Canvas` ve `Scene`'dir. `RecordingCanvas` yalnız testlerde komut kaydı tutar; üretim çizimi CoreGraphics'tir. Skia tipleri çekirdeğe girmez. İleride Skia aynı `Canvas` yüzeyine oturabilir.

## Alternatives

Skia ilk günde daha taşınabilir bir rasterizer verirdi fakat platform penceresi, olay kuyruğu ve metin için ayrıca bir backend isterdi. Yazılım rasterizer framework'ün ilk görsel kilometre taşını renderer icat etmeye çevirirdi. SDL aynı soyutlamaya sonra bağlanabilir.

## Consequences

Widget katmanında `extern def` yoktur. FFI sınırı `aura/platform` ve metin ölçüm köprüsündedir. Pencere yokken birim testler `RecordingCanvas` kullanır. Dylib göreli yolu süreç çalışma dizininden çözülür.

## Migration

Backend değişirse `Canvas` metotları korunur ve yeni dylib aynı C sembollerini ya da yeni bir Nox sarmalayıcısını uygular. Çekirdek widget'lar yeniden yazılmaz.
