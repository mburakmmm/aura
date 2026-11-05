# ADR-020 iOS host, scene ve UI-thread modeli

## Context

iOS preview host'u `UIWindowScene`, `UIWindow`, custom `AuraCanvas` ve
`CADisplayLink` kullanıyor. Nox entry ayrı worker'da çalışırken UIKit çağrıları
main thread'e taşınıyor. Scene lifecycle olayları typed kuyruğa bağlandı, ancak
display link background'da açık bırakılırsa gereksiz wakeup oluşabilir. İlk
paket manifesti bir scene destekliyor; çoklu scene/window mapping henüz yok.

## Decision

UIKit nesneleri yalnız main thread'de oluşturulur, değiştirilir ve bırakılır.
`aura_host_*` çağrıları main thread dışından gelirse dar `on_main` kapısından
geçer. Nox runtime worker'ı hiçbir UIKit nesnesini doğrudan kullanmaz; host
callback'leri yalnız ortak shell/event API'siyle haberleşir.

Bir `AuraCanvas` bir Aura window slot'una ve bir monoton surface generation'a
karşılık gelir. `didMoveToWindow` create/destroy, `layoutSubviews` change olayı
üretir. UIKit pointer/metrics/lifecycle callback'leri queued typed event yazar;
Widget/Element/RenderObject callback stack'inde mutate edilmez.

`CADisplayLink` scene background/resign sırasında pause edilir, foreground'da
resume edilir ve canvas kapanışında invalidate edilir. Frame timestamp native
monotonic/display callback'inden gelir; animation kendi sistem saati okumaz.

v0 mobil host açıkça tek `UIWindowScene` destekler. App process termination ile
scene inactive/background/disconnect olayları ayrı tutulur. Scene disconnect
runtime stop ister; configuration benzeri geçici state değişimleri yalnız
lifecycle/surface olayları üretir. iPad multi-window ancak scene -> Binding
registry ve ownership testi ekleyen ayrı ADR ile açılabilir.

## Alternatives

UIKit'i Nox worker thread'inden çağırmak platform sözleşmesini ihlal eder.
Display link'i her lifecycle geçişinde tamamen yeniden yaratmak duplicate
callback ve retain-cycle riskini artırır; pause/resume identity'yi korur.
Scene event'inden doğrudan Nox observer çağırmak queue ordering ve UI-thread
invariant'ını bozar. Çoklu scene'i eksik global window mapping'iyle sessizce
etkinleştirmek yanlış surface/input routing üretir.

## Risks

`dispatch_sync` yalnız main-thread kontrolünden sonra kullanılmalıdır; aksi
halde deadlock olur. Scene disconnect sonrası gecikmiş display callback'i
gelebilir; invalidated/paused link ve generation kontrolü bunu zararsız
kılmalıdır. Tek-scene kısıtı destek matrisinde görünür olmalıdır.

## Consequences

iOS adapter kaynakları UIKit thread ownership'ini dar bir katmanda tutar.
Lifecycle ve surface fixture'ları ortak C state machine'de test edilir;
Objective-C target syntax derlemesi adapter sözleşmesini korur. Fiziksel cihaz
background/foreground ve external-display kabul testleri yine gereklidir.

## Migration ve rollback

Display link pause/resume additive olarak eklenir. Sorunda lifecycle'a bağlı
pause kapatılabilir fakat event queue ve main-thread kapısı korunur. Çoklu scene
çalışması başladığında tek-scene manifesti kaldırılmadan önce registry,
restoration ve route testleri tamamlanmalıdır.
