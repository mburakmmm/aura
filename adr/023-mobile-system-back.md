# ADR-023 Mobil system back ve route mutation

## Context

Android system back ve predictive-back tamamlanması platform event loop'undan
gelir. Aura'da overlay ve Navigator ayrı retained ağaçlardır; native adapter'ın
route state'ini bilmesi veya doğrudan Widget çağırması katman sınırını bozar.
Kök rotada tüketilmeyen back ise Android'e bildirilmelidir ki Activity normal
sistem kapanışına dönebilsin.

## Decision

Native adapter `AURA_SYSTEM_BACK` sürümlü platform event'i üretir. Event,
`start/update/cancel/commit` fazını ve normalize edilmiş `0...1` ilerlemeyi
taşır. Ardışık update event'leri bounded queue'da coalesce edilir. Yalnız
commit fazı mutation yapar. FramePump aktif pointer sequence'lerini iptal
ettikten sonra aynı mounted root altında şu öncelikle tek bir hedef seçer:

1. entry taşıyan en derin Overlay,
2. pop edebilen en derin Navigator,
3. unhandled platform sonucu.

Overlay ve route mutation yalnız `mark_dirty()` yapar; event dispatch sırasında
recursive rebuild yapılmaz. Reconciliation sonraki build fazında gerçekleşir.
Dismissible overlay entry kendi back callback'ini taşır; Dialog back ile
kapanınca `DialogResult` iptal sonucu yazar.

Hedef araması global registry sırasına göre değil Element ağacında sanal
dispatch ile yapılır. `NavigatorElement` yalnız aktif route child'ına iner;
retained fakat görünmeyen eski sayfalardaki nested Navigator/Overlay back
olayını tüketemez. Böylece aynı süreçteki ayrı root'lar ve gizli route state'i
izole kalır.

Nox sonucu ortak shell ABI üzerinden native host'a döndürür. Android UI thread
handled sonucu hiçbir sistem işlemi yapmadan bırakır; unhandled sonucu Activity
kapanışına yönlendirir. API 34 `OnBackAnimationCallback`, API 33
`OnBackInvokedCallback`, eski API'ler `onBackPressed` adaptörü kullanır.
Predictive progress FramePump'ta tutulur; route mutation'ın tek commit noktası
tamamlanma olayıdır.

## Alternatives

Native tarafta route sayısı tutmak retained Nox state'ini iki kaynağa böler.
Her back'te Activity'yi kapatmak overlay ve nested route'ları atlar. Event
callback içinde anında rebuild reentrant build/layout riski yaratır. Global son
kayıtlı Navigator'ı seçmek multi-window/root izolasyonunu bozar.

## Risks

Predictive progress henüz route transition animasyonuna görsel olarak bağlı
değildir; bu entegrasyon transition altyapısıyla birlikte eklenmelidir.
Unhandled sonucu native UI thread'e asenkron döndüğü için Activity o kısa
aralıkta ikinci back olayını deduplicate etmelidir.

## Consequences

Desktop ve headless back event'i aynı root-scoped kararı test edebilir. Widget
ve RenderObject platform sınıfı görmez. Overlay kapanışı route pop'tan önce
gelir ve bütün mutation'lar frame batching invariant'ını korur.

## Migration ve rollback

Event kind zaten ABI v1'de ayrılmıştır. Native callback additive semboldür;
desktop backend'ler no-op compatibility stub sunar. Sorunda Android callback
kaydı kaldırılabilir, Nox root-scoped back davranışı headless API olarak kalır.
