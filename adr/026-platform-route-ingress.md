# ADR-026 Typed platform route ingress

## Context

Android App Link/custom intent ve iOS universal link/custom URL uygulama henüz
ilk frame'i üretmeden veya çalışırken gelebilir. Native adapter'ın URI'yi
sayfaya çevirmesi Widget katmanını platforma taşır; global son Navigator'a
push etmek multi-window ve nested Navigator izolasyonunu bozar. URI'yi sessizce
string callback olarak geçirmek de kaynak türünü ve handled sonucunu kaybeder.

## Decision

Platform adapter `AURA_PLATFORM_ROUTE` event'i üretir. Event UTF-8 location ve
typed source taşır: app-link, universal-link veya intent. Native shell pencere
oluşmadan gelen route event'lerini slot başına bounded pending queue'da saklar
ve ilgili window açıldığında sırayla event queue'ya aktarır.

Nox tarafında `PlatformRouteRequest(location, source)` immutable request'tir.
Navigator varsayılan olarak route ingress tüketmez. Uygulama
`Navigator.with_platform_routes(resolver)` ile request'i `Widget | None`
sonucuna dönüştürür. `None` unhandled demektir; Widget dönerse Navigator URI'yi
route adı olarak kullanıp normal keyed retained stack'e tek bir batched push
yapar.

Dispatch current Binding root'u ile sınırlıdır. Element ağacında yalnız aktif
route yolu aranır; en derin Navigator ilk fırsatı alır, unhandled ise ancestor
Navigator denenir. Native adapter Navigator registry'sini veya Widget type'ını
görmez. Overlay route ingress'i saklamaz; resolver kararı uygulamaya aittir.

Android initial/onNewIntent ve iOS scene openURL/continueUserActivity aynı ABI
sözleşmesine girer. Masaüstü kabuklar aynı kuyruğa `aura_post_platform_route`
ile yazar; Binding bu olayı input fazında Navigator'a verir. Boş location
kuyruğa girmez. URI parsing, authentication ve domain allowlist uygulama
resolver'ının sorumluluğudur; framework location'ı değiştirmez.

## Alternatives

Native route→screen tablosu katman sınırını bozar. Global Navigator registry'si
yanlış window veya gizli retained route'u seçebilir. URI'yi framework içinde
genel router'a çevirmek M2 kapsamını büyütür ve uygulama URL şemasını core'a
bağlar. Window açılmadan gelen route'u düşürmek cold-start deep link'i bozar.

## Risks

Resolver kötü niyetli veya doğrulanmamış URI'yi kabul edebilir; framework bunu
güvenli saymaz ve dokümantasyon allowlist ister. Pending queue bounded olmalı,
overflow mevcut queue diagnostic politikasına uymalıdır. Multi-window adapter
doğru slotu seçmelidir.

## Consequences

Route stack'in tek sahibi Navigator kalır. Headless testler native platform
olmadan request routing ve batching'i doğrular. Platform adapter testleri cold
start ve warm intent/URL girişini ayrı sınar.

## Migration ve rollback

Event kind ABI v1'de ayrılmıştır; adapter callback'leri additive'dir. Resolver
eklenmemiş Navigator event'i unhandled bırakır. Native callback kaldırılırsa
programmatic Navigator API değişmeden kalır.
