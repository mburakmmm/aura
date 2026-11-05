# ADR-017 Mobil lifecycle, scheduler ve restoration

## Context

iOS scene ve Android Activity callback'leri ilk preview'da yalnız log üretir.
Aura `Scheduler` her `FramePump.pump` çağrısında bütün ticker'ları çalıştırır;
background/resume sınırı bilinmez. Uzun süre background'da kalan bir
animasyon resume frame'inde çok büyük delta görebilir. Aktif pointer, focus ve
IME state'i de surface görünmez olduğunda açık kalabilir.

Mobil işletim sistemleri terminate callback'ini garanti etmez. State save'i
yalnız terminate anına bağlamak veri kaybına yol açar. Lifecycle global bir
singleton olursa çoklu scene/window modeli birbirini yanlışlıkla etkiler.

## Decision

Ortak event ABI aşağıdaki scene/window durumlarını taşır:

- `detached`: native surface/scene bağlı değil;
- `inactive`: görünür olabilir fakat input/animation aktif değildir;
- `resumed`: foreground, input ve ticker çalışabilir;
- `paused`: background'a geçmiştir;
- `hidden`: görünür değildir fakat process yaşayabilir;
- `terminated`: yalnız alınabildiğinde son bildirimdir.

Her `Binding` kendi `LifecycleOwner` instance'ına sahiptir. Owner public
observer kaydı, mevcut state ve restoration-save callback'i sunar. Global
mutable lifecycle state bulunmaz. Aynı state tekrar gelirse observer çağrısı
üretilmez. Observer dispatch sırasında reentrant lifecycle transition açık
`FrameworkError` üretir.

`resumed` dışındaki durumlarda `FramePump` aktif pointer sequence'ini cancel
eder, scheduler'ı suspend eder ve platform text input'u kapatır. Mantıksal
focus korunabilir; resume olduğunda halen mounted ve enabled focus node için
IME yeniden açılır. App task'ları, network veya disk işi framework tarafından
zorla iptal edilmez. Async sonuçlar mevcut UI-thread/state lifecycle
sözleşmesine uymaya devam eder.

Scheduler suspend sırasında ticker callback'i üretmez. Resume frame timestamp'i
ile pause başlangıcı arasındaki süre her ticker'ın zaman tabanına eklenir;
böylece `AnimationController.start_s` ve scroll fling `last_time` background
süresini animasyon deltası olarak görmez. Animation hâlâ frame timestamp'i
kullanır; lifecycle callback'i sistem saatinden ayrı animation tick üretmez.

`paused`, `hidden` ve `terminated` geçişleri observer'lara state değişiminden
sonra save isteği gönderir. Bu hook hızlı, idempotent ve non-blocking olmalıdır;
kalıcı disk yazımı uygulama tarafından async yapılır. Terminate gelmeyebileceği
için save yalnız terminate'a bırakılmaz. Aura'da henüz post-frame callback
kuyruğu yoktur; eklendiğinde suspended durumda çalıştırılmayacak ve resume
frame'ine taşınacaktır.

Native callback yalnız `AuraPlatformEvent` kuyruğuna typed lifecycle payload
yazar. Widget/Element/RenderObject callback thread'inden mutate edilmez.
Transition `Binding._drain` input fazında uygulanır.

Pipeline fazı `idle`, `build`, `layout`, `paint` ve `composite` sırasını izler.
Paint veya composite sırasında layout, layout sırasında paint, ve aynı fazın
yeniden girmesi `illegal pipeline transition` tanısı üretir. Paint sırasında
`mark_needs_layout` bir sonraki frame'e ertelenir.

## Alternatives

Activity/scene callback'inden doğrudan Nox observer çağırmak daha kısa olurdu;
thread ownership, reentrancy ve event ordering invariant'larını bozardı.
Ticker'ları pause sırasında listeden silmek controller ownership'ini kaybeder
ve resume davranışını uygulama koduna iterdi. Resume'da yalnız delta'yı clamp
etmek gerçek animation süresini platforma göre değişken kılar ve fling state'ini
çözmezdi. Process-global lifecycle singleton'ı multi-window/scene için yanlış
olurdu.

## Consequences

Scheduler ve ticker taban sınıfına suspend/rebase contract'ı eklenir.
AnimationController ve fling bu contract'ı uygular. FocusManager IME'yi
suspend/resume edebilir. iOS ve Android host aynı lifecycle durum numaralarını
üretir; masaüstü backend'ler sembol uyumluluğu için accessor sağlar fakat event
üretmek zorunda değildir.

Observer'lar callback içinde blocking I/O yapmamalıdır. Background frame
gelmese bile save hook'u lifecycle input batch'i işlendiği anda çalışır.

## Migration ve rollback

Önce headless LifecycleOwner/Scheduler testleri, sonra ortak C accessor ve
mobil adapter event'leri eklenir. Binding varsayılan olarak `resumed` başlar;
bu masaüstü davranışını korur. Sorun halinde adapter event üretimi kapatılabilir;
typed enum ve owner API additive kalır, scheduler varsayılan active davranışına
döner. Eski log-only callback'ler tüm acceptance testleri geçtikten sonra
kaldırılır.

## Amendment

Bellek baskısı lifecycle durumundan ayrı bir sinyaldir. Seviyeler 1 low,
2 moderate ve 3 critical'dir. 0 host sinyali değildir. `LifecycleOwner`
geçersiz seviye ve observer dispatch sırasında yeniden giren bildirimi
`FrameworkError` ile reddeder. iOS memory warning critical sayılır. Android
`TRIM_MEMORY_COMPLETE` ve `RUNNING_CRITICAL` critical, `TRIM_MEMORY_MODERATE`
ve `RUNNING_LOW` moderate, diğer 5 ve üzeri low olur. Bu bildirim cache
tahliye politikası uygulamaz; observer kendi kaynağını bırakır.

`WindowMetrics.reduce_motion` açıldığında `Scheduler` oynayan
`AnimationController`'ı hedefine sıçratır ve fling'i yerinde durdurur.
Geri sarma yapılmaz. Kapanınca süren animasyonlar kaldığı yerden devam eder.
