# ADR-005 Platform soyutlaması

## Context

Aura önce tek masaüstü platformunda görünür olmalı, fakat pencere, olay, pano ve erişilebilirlik çağrıları widget koduna dağılmamalıdır. İlk platform macOS'tur.

## Decision

Platform yüzeyi `create_window`, `destroy_window`, `poll_events`, `request_frame` (`wait_frame`), imleç ve metin ölçümü etrafında toplanır. Bu dilimde çalışan kısım pencere, olay yoklama, kare bekleme, çizim komutları ve metin ölçümüdür. Pano, IME ve macOS AX köprüsü yoktur.

`SemanticsNode` rol, etiket, değer, etkinlik, seçili durum, eylem ve sınır taşır. `RenderObject` bu düğümü doldurur. Platform AX köprüsü sonraki dilimdedir.

Platform koşulu framework boyunca `if macos` olarak yazılmaz. Yeni bir işletim sistemi yeni bir dylib ve `aura/platform` sarmalayıcısı ekler.

## Alternatives

Doğrudan AppKit çağrılarını widget'lara koymak ilk sayacı kısaltırdı ve Linux ile Windows kapılarını kapatırdı. Ayrı bir sanal `PlatformBackend` sınıfı bu dilimde tek uygulaması olacağı için C sınırı Nox sarmalayıcılarında tutuldu.

## Consequences

Pencere tutamacı tamsayıdır. Sıfır, oluşturma hatasıdır. Kapatma idempotenttir. Kapatma sonrası yoklama ve boyut sorgusu `use after close` üretir. En fazla 32 pencere vardır.

## Migration

İkinci platform eklendiğinde sarmalayıcı aynı Nox fonksiyon adlarını uygular. Widget ve yerleşim modülleri değişmez.
