# ADR-010 Gezgin

## Context

Aura 0.1 tek pencerede sayfa yığını ister. URL yönlendiricisi, derin bağlantı ve çoklu pencere bu sürümün dışındadır. Alt sayfanın state'i `pop` ile yeniden kurulmamalıdır. Yalnız üstteki sayfa yerleşir, boyanır ve hit-test edilir.

## Decision

`Navigator` bir inherited değerdir. `Route` adı ve sayfa widget'ını taşır. Yığın elementin üzerindedir; widget yeniden üretilince yığın silinmez. `push`, `pop`, `replace` ve `can_pop` vardır. `pop` tek sayfa varken no-op'tur.

Her sayfa anahtarlı bir `RouteHost` olarak monte kalır. `RenderNavigator` çok çocukludur ve yalnız aktif indeksi layout, paint ve hit-test eder. `Navigator.of` build sırasında bağımlılık kaydeder. Olay işleyicileri bağımlılık kaydetmeden tutamaç alır.

## Alternatives

Her geçişte alt sayfayı unmount etmek state'i sıfırlardı. URL yönlendiricisini 0.1'e almak platform derin bağlantısı olmadan yarım bir API bırakırdı. Üst üste bütün sayfaları boyamak gizli sayfanın tıklanmasına yol açardı.

## Consequences

Tema ve diğer inherited değerler alttaki sayfada durur. Görünmeyen sayfa layout almaz. Yığın widget alanında değil elementte yaşar.

## Migration

Derin bağlantı gelirse yığın aynı `Route` kayıtlarından üretilir. `push` ve `pop` imzası değişmez.
