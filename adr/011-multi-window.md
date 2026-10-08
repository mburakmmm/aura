# ADR-011 Çoklu pencere

## Context

Aura 0.1 tek `Binding` ve tek `FramePump` ile yaşar. Aktif pencere tutamacı da tektir; IME ve pano bu tutamaca yazılır. Native tarafta en fazla 32 pencere vardır, fakat Nox tarafı ikinci bir pencereyi kare döngüsüne almaz. 0.2 birden fazla masaüstü penceresi ister. Skia ve ikinci işletim sistemi bu kararın dışında kalır.

## Decision

Her pencere kendi `Binding` nesnesini, dolayısıyla kendi element ağacını, odak yöneticisini ve kare damgasını taşır. `AppHost` bu bağlamaları tamsayı kimliklerle tutar. Odaklanan pencere, IME ve pano için aktif tutamaç olur. Kapanan pencere host’tan düşer. Son pencere kapanınca `run_app` döngüsü biter.

`run_app` tek pencereli kolay yol olarak kalır. İkinci pencere `WindowController.open` ile açılır. Pencereler arasında widget durumu paylaşılmaz. Platform olayı yine pencerenin kendi kuyruğundadır; Nox’a geri çağrı girmez.

## Alternatives

Tek bir `FramePump` içinde iki kök tutmak, odak ve kirli kuyruğu pencereler arasında karıştırırdı. İşletim sisteminin belge penceresini widget ağacına sızdırmak da katman yönünü bozardı.

## Consequences

Bir penceredeki işaretçi olayı diğer pencerenin state’ini kirletmez. VSync beklemesi hayattaki ilk pencerenin display link’idir; aynı ekrandaki pencereler aynı karede boyanır. Widget katmanı pencere tutamacı görmez.

## Migration

Mevcut `run_app(widget)` çağrıları tek pencere açmaya devam eder. İkinci pencere isteyen kod host kurulduktan sonra `WindowController` kullanır.
