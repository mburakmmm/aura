# ADR-025 iOS interactive swipe-back ve Navigator politikası

## Context

iOS, Android gibi uygulama geneline yayılan bir system-back callback'i sunmaz.
`UINavigationController` interactive-pop davranışı sağlayabilir; Aura ise kendi
retained Navigator/RenderNavigator ağacına ve platformdan bağımsız scene'ine
sahiptir. Native navigation controller ile Aura route stack'ini birlikte
tutmak iki state kaynağı, lifecycle sapması ve custom renderer üzerinde UIKit
view hiyerarşisi zorunluluğu yaratır.

Edge swipe; yatay scroll, text selection, Button tap, Overlay ve nested
Navigator ile yarışır. Gesture ilerlerken route'u hemen silmek cancel durumunda
state'i geri kurmayı ve frame batching invariant'ını bozar.

## Decision

iOS interactive back Aura gesture/navigation katmanlarında uygulanacaktır;
native adapter yalnız normalize pointer event'lerini ve WindowMetrics içindeki
system-gesture inset'lerini üretmeye devam eder. UIKit
`UINavigationController` veya `UIScreenEdgePanGestureRecognizer` route
state'inin sahibi olmaz.

### Adaylık ve arena önceliği

- Root binding, iOS navigation policy etkinse sol edge için bir
  `InteractiveBackRecognizer` kaydeder. RTL yönünde aday edge sağdır.
- Başlangıç bölgesi `max(system_gesture_edge, 20 logical px)` olur ve hiçbir
  zaman safe-area çıkarımıyla koordinat değiştirilmez.
- Recognizer yalnız aktif Element yolundaki en derin Navigator `can_pop`
  diyorsa adaydır. Dismissible Overlay varsa önce Overlay back politikası
  çalışır; route preview başlamaz.
- Touch slop aşılmadan hiçbir yarışmacı kazanmaz. Edge'den içeri doğru hareket
  ana eksende baskınsa interactive-back, yatay scroll/pan yarışmacılarından
  önce kazanır. Dikey niyet scroll'a bırakılır. Kazanınca aynı pointer
  sequence'indeki Button/text-selection adayları cancel edilir.
- İkinci pointer interactive route gesture'ına katılmaz ve başlamış winner'ı
  değiştirmez.

### Navigator transaction

Navigator üç root-scoped sanal operation sunacaktır:

1. `begin_interactive_pop()` aktif ve önceki entry kimliklerini dondurur,
2. `update_interactive_pop(progress)` yalnız render progress'i dirty eder,
3. `finish_interactive_pop(commit)` cancel'da preview'ı sıfırlar, commit'te
   route mutation'ını bir kez yapıp Element'i sonraki build için dirty eder.

Progress logical delta / viewport width olarak `0...1` aralığına clamp edilir.
Commit kararı varsayılan olarak progress `>= 0.5` veya içeri yönlü logical
velocity eşiğiyle verilir. Eşikler gesture metrics sözleşmesinde tutulur.

Gesture sırasında entry listesi değişmez. `RenderNavigator` önceki ve aktif
child'ı aynı constraints ile layout eder; önceki route'u altta, aktif route'u
progress'e bağlı translation ile üstte boyar. Hit-test, focus, semantics ve
input aktif route'ta kalır. Commit sonrası normal reconciliation aktif route'u
değiştirir; cancel hiçbir Widget/Element identity'sini değiştirmez.

Push/replace, lifecycle pause, surface loss, queue overflow ve başka system
navigation olayı aktif transaction'ı önce cancel eder. Transaction tek UI
thread'indedir; native callback route listesine erişmez.

## Alternatives

`UINavigationController` kullanmak retained route state'ini UIKit ile
çoğaltır. Native screen-edge recognizer'ın doğrudan `pop` çağırması gesture
arena ve batched rebuild kurallarını atlar. Gesture başında entry'yi silip
cancel'da yeniden eklemek State ve Element identity'sini kaybedebilir. Bütün
yatay drag'leri back saymak carousel, text selection ve nested scroll'u bozar.

## Risks

İki route'un gesture süresince layout/paint edilmesi kısa süreli ek maliyet
yaratır; önceki route retained olduğu için yeniden build gerekmez. Çok pahalı
route'larda RepaintBoundary/layer reuse ölçülmelidir. RTL edge ve VoiceOver
gesture çatışmaları cihaz test matrisiyle doğrulanmadan production kapısı
geçilmez.

## Consequences

Navigator route stack'in tek sahibi kalır. iOS davranışı custom renderer ile
uyumludur ve headless gesture transaction test edilebilir. Android predictive
back ile aynı `start/update/cancel/commit` kavramı paylaşılır fakat Android
platform callback'i, iOS ise Aura edge recognizer'ı tarafından sürülür.

## Test ve kabul planı

- Headless: progress route'u mutate etmez; cancel identity'yi korur; commit
  yalnız bir pop ve bir batched rebuild üretir.
- Gesture: edge/back ile horizontal scroll, Button, text selection ve ikinci
  pointer çatışmaları deterministiktir.
- Render: 0/0.5/1 progress'te iki route layout ve translation değerleri.
- Lifecycle: pause, surface loss ve queue overflow transaction'ı cancel eder.
- iOS cihaz: 60/120 Hz, LTR/RTL, VoiceOver, nested Navigator ve edge scroll.

## Migration ve rollback

Policy platform backend capability bit'i ile yalnız iOS'ta açılır. Transaction operation'ları additive'dir;
recognizer kaydı kaldırılırsa mevcut programmatic/system route pop davranışı
değişmeden kalır. UIKit navigation state'i yaratılmadığı için rollback veri
migrasyonu gerektirmez.
