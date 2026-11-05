# ADR-019 Android host, Activity ve surface lifecycle

## Context

Android preview host'u bir `Activity` içinde custom `View` ve raster `Bitmap`
sunum hedefi kullanıyor. `onSizeChanged` yalnız metrics yayımlıyor; view attach,
detach ve backing target nesli ortak runtime tarafından bilinmiyor. Bu yüzden
surface yokken Nox frame döngüsü paint/present çalıştırabilir ve configuration
change sırasında eski Activity global ref'i ile yeni Activity/runtime sahipliği
karışabilir.

Aura renderer ileride `SurfaceView`, `ANativeWindow` veya GPU swapchain'e
geçebilir. Widget ve rendering katmanı Android tipi görmemeli; surface kaybı
normal platform olayı olarak ele alınmalıdır.

## Decision

Ortak `AuraPlatformEvent` içindeki `SURFACE_CREATED`, `SURFACE_CHANGED` ve
`SURFACE_DESTROYED` olayları sunum hedefinin lifecycle sözleşmesidir. Surface
kimliği platform handle değildir; process içi monoton generation ile birlikte
yalnız ordering/stale-event reddi için kullanılan opaque bir sayıdır. Payload
physical width/height, format ve generation taşır.

Android `AuraView` attach olduğunda generation artırır ve create, non-zero size
geldiğinde aynı generation ile change, detach olduğunda destroy yayımlar.
Geçiş kararı `aura_android_surface_*` makinesindedir; View yalnızca sonucu
yayımlar. Sıfır veya negatif boyut change üretmez. Detach, attach yokken
olay üretmez. Attach zaten bağlıyken önce eski generation için destroy, sonra
yeni generation için create üretir. Makine headless test edilir. Mevcut raster
`Bitmap` hedefi de bu sözleşmeye tabidir. Gelecekte GPU surface aynı olayları
üretir; framework API'si değişmez.

Native shell en son surface snapshot'ını pencere create edilmeden önce de
tutar. Nox penceresi daha sonra açılırsa mevcut surface için sentetik create
snapshot'ı kuyruğa eklenir. Yanlış generation ile change/destroy reddedilir.
Native callback yalnız event kuyruğuna yazar; Widget/Element/RenderObject
doğrudan mutate edilmez.

`Binding` surface availability ve generation'ı per-window tutar. Destroy
olayında aktif pointer sequence iptal edilir, ticker/focus/IME askıya alınır ve
paint/present/semantics submission atlanır. Create/change sonrasında yalnız app
lifecycle `resumed` ise scheduler/focus devam eder. Surface state app lifecycle
state'inin yerine geçmez.

`Choreographer` zinciri yalnız resumed Activity'de tektir; pause/destroy
callback'i kaldırır. `isChangingConfigurations()` true ise Activity JNI ref'i
serbest bırakılır fakat process-level Nox runtime durdurulmaz. Yeni Activity
aynı runtime'a bind olur ve yeni surface generation yayımlar. Gerçek process
shutdown yolunda runtime stop istenir.

## Alternatives

Surface kaybını yalnız zero-size resize olarak modellemek stale GPU target ve
generation yarışlarını ayırt edemez. Android `Surface`/`View` nesnesini public
Nox API'sine geçirmek platform sınırını bozar. Her Activity recreation'da Nox
runtime'ı yeniden başlatmak state restoration'ı zorunlu kılar ve config change'i
process death ile yanlış biçimde eşler. Surface yokken boş frame çizmek CPU ve
batarya tüketir.

## Risks

View attach olayı Nox penceresinden önce gelebilir; shell snapshot replay'i bu
sırayı kapsamalıdır. Detach ve Activity destroy yakın aralıkta gelebilir;
generation kontrolü duplicate/stale destroy'u zararsız yapmalıdır. Mevcut
raster backend ile gerçek Android buffer ownership'i test edilemez; GPU backend
ADR'si ayrıca gereklidir.

## Consequences

Surface accessor sembolleri bütün desktop backend'lerde uyumluluk stub'ı olarak
bulunur. Headless native test create-before-window, normal create/change/destroy,
stale generation ve recreate dizilerini doğrular. Android adapter Java/JNI
derleme kapısı ve contract checker ortak shell kullanımını zorunlu kılar.

## Migration ve rollback

Önce ortak shell state machine ve headless test, sonra Android View callback'leri
ve Binding paint gate'i eklenir. Sorunda adapter'ın surface event üretimi
kapatılabilir; typed ABI additive kalır ve desktop availability stub'ı mevcut
davranışı korur. GPU surface geldiğinde yalnız native adapter/renderer değişir.
