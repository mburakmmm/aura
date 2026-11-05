# ADR-029 Text range geometry snapshot

## Context

iOS `UITextInput`, yalnız aktif caret rectangle'ını değil herhangi bir
`UITextPosition` için caret rectangle, bir range'in ilk rectangle'ı ve bir
ekran noktasına en yakın text position'ı senkron olarak sorar. Android cursor
anchor API'si de IME isterse character bounds yayınlayabilir.

Aura'nın renderer'ı gerçek satır kırılımı ve glyph ölçümünün tek sahibidir.
Native adapter'ın editable bounds boyunca lineer tahmin yapması; proportional
font, emoji, birleşik grapheme ve multiline alanlarda yanlış selection/candidate
konumu üretir. UIKit/CoreText veya Android Paint ile metni ikinci kez ölçmek de
renderer ile platform arasında iki farklı layout gerçeği yaratır.

## Decision

Focused `RenderTextField`, mevcut caret/editable rectangle mesajına ek olarak
layout-doğru bir boundary snapshot yayınlar. Snapshot kayıtları:

- Unicode codepoint offset,
- editable alanına göre local caret x/y,
- line height

taşır. Kayıtlar extended-grapheme sınırlarında ve offset sırasındadır. Native
adapter platformun UTF-16 position modeline yalnız kendi sınırında dönüştürür.

ABI üç aşamalıdır:

1. `aura_text_geometry_begin(handle, target, revision, count)`
2. her kayıt için `aura_text_geometry_boundary(...)`
3. `aura_text_geometry_end(handle, target, revision)`

Common mobile shell pending snapshot'ı mutex altında kurar. Eksik, sıra dışı,
NaN/sonsuz, negatif offset veya stale end çağrısı committed snapshot'ı
değiştirmez. Yalnız tamamen doğrulanmış snapshot atomik olarak publish edilir.
Count bounded'dır ve allocation overflow kontrol edilir.

`target + revision` aynıysa `begin` sıfır döndürür ve RenderTextField yeniden
ölçüm yapmaz. Text, wrapping width veya line layout değişince render object
revision artırır. Alanın ekrandaki origin'i animasyon/scroll sırasında değişse
bile local boundary snapshot değişmez; mevcut ucuz `aura_text_geometry` çağrısı
origin ve editable bounds'u her frame günceller.

iOS adapter snapshot'ı senkron UIKit sorguları için retained local değerler
olarak tutar. `caretRectForPosition`, `firstRectForRange`,
`closestPositionToPoint` ve selection rectangle'ları yalnız bu snapshot'tan
türetilir. Snapshot henüz yoksa yalnız mevcut caret için güvenli fallback
kullanılır; editable-width lineer tahmin yapılmaz.

Android adapter aynı snapshot'ı `CursorAnchorInfo` character bounds filtreleri
için kullanabilir. Platform adapter metin ölçmez ve Widget/Element katmanları
platform tipi görmez.

## Alternatives

Native font API'siyle yeniden ölçüm renderer font fallback/shaping sonucuyla
ayrışır. Her frame bütün boundary'leri göndermek uzun alanlarda gereksiz FFI ve
ölçüm maliyeti yaratır. Ham pointer/array ownership'ini Nox public API'sine
taşımak ownership invariant'ını bozar. Yalnız current caret göndermek UIKit
range sözleşmesini karşılamaz.

## Risks

Mevcut Skia ölçüm primitive'i prefix ölçümü yaptığı için snapshot üretimi uzun
metinde pahalı olabilir; revision cache bunu her-frame maliyet olmaktan çıkarır.
Text shaping backend'i glyph cluster konum API'si sunduğunda aynı ABI daha hızlı
üretimle beslenebilir. RTL/bidi için logical offset sırası ile visual x sırası
aynı olmayabilir; kayıtlar offset'e göre saklanır, point lookup visual rect
üzerinden yapılır.

## Consequences

IME candidate window, selection ve point-to-position davranışı Aura'nın çizdiği
layout ile aynı olur. Snapshot common shell ownership'inde olduğundan iOS veya
Android lifecycle sırasında dangling pointer oluşmaz. Desktop backend'ler mobil
range snapshot istemediğini `begin == 0` ile açıkça bildirir.

## Test strategy

- common shell: stale/partial/invalid snapshot reddi, atomik commit ve teardown,
- Nox render test: multiline ve emoji/grapheme boundary offset/rect fixture,
- iOS compile contract: tam `UITextInput` geometri yöntemleri,
- iOS simulator: caret/range/closest point acceptance,
- Android: cursor-anchor character bounds ve UTF-16 offset fixture.

## Rollback

Üç yeni ABI sembolü additive'dir. Adapter snapshot callback'ini kapatırsa mevcut
current-caret geometry yolu çalışmaya devam eder; public Widget API değişmez.
