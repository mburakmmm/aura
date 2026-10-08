# ADR-014 Plugin köprüsü

## Context

Plugin API, Nox derleyicisine dinamik modül yükleme eklemeden uygulama görünümlerini ve yerli çocuk yüzeyleri bağlamalıdır. Native kod Nox fonksiyonu çağırmaz.

## Decision

İki kapı vardır.

Nox plugin’i, uygulamanın import edip `register_view(name, factory)` ile kaydettiği bir `() -> Widget` fabrikasıdır. Çekirdek dizin taramaz. Kayıt yoksa `FrameworkError`. Fabrika, kaydı tutan modülün içinden çağrılır.

Native plugin, platform katmanının yol ile açtığı bir kütüphanedir. Üç sembol zorunludur: `aura_plugin_probe_frame`, `aura_plugin_probe_event`, `aura_plugin_probe_close`. Biri eksikse kütüphane kapanır ve yükleme başarısız olur. Kabuk `plugin_frame` ve `plugin_event` çağırır.

`PlatformView` bir `RenderObjectWidget`’tır. Dikdörtgeni `aura_plugin_place` ile kabuğa bildirir. Kabuk, Skia pixmap’inin üstüne bir çocuk yüzeyi koyar. Yeni tuval komutu yoktur.

## Alternatives

Çalışma anında `.nox` yüklemek derleyici değişikliği ister. Plugin’in Nox’a fonksiyon işaretçisi vermesi geri çağrı yasağını bozar.

## Consequences

Tutamak tamsayıdır. `close` idempotenttir. Kapatma sonrası `frame` `use after close` üretir. Testler tam probe, eksik probe ve çift kapatmayı kapsar.

## Migration

Mevcut widget’lar plugin kaydı olmadan çalışır. Yerleştirme, karenin sonunda kabuk tarafındadır.
