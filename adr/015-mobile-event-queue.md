# ADR-015 Mobil olay kuyruğu ve ABI

## Context

Masaüstü kabukları olay türünü, iki koordinatı, bir tuş alanını, bir düğme
alanını ve isteğe bağlı metni kuyrukta tutar. İlk mobil preview aynı numaraları
kullanır. Bu yüzey tek parmak için yeterlidir; fakat pointer kimliği, platform
timestamp'i, lifecycle, memory pressure, surface kaybı ve metrics değişimini
ifade edemez. Kuyruk okunurken platform callback'leri yeni olay ekleyebilir.

Widget, Element ve RenderObject platform callback'inden doğrudan mutate
edilemez. Nox FFI callback'i framework lifecycle'ının temel mekanizması
olamaz. Mobil host callback'leri yalnız native kuyruğa yazmalı, tek mantıksal
UI akışı frame'in input fazında sabit bir batch okumalıdır.

## Decision

Aura native host sözleşmesi sürümlü bir `AuraPlatformEvent` yapısı kullanır.
İlk sürüm en az şu alanları taşır:

- `size`, `abi_version`, `kind` ve monoton `sequence`;
- platform monotonic saatinden `timestamp_ns`;
- `device_id`, `pointer_id`, pointer kind, buttons ve modifiers;
- logical-pixel `x/y`, scroll veya hareket deltaları, pressure ve tilt;
- event türüne göre metrics, lifecycle veya text payload kimliği.

Mevcut 1–15 event numaraları geriye dönük fixture'lar için korunur. Yeni
pointer/metrics/lifecycle türleri 16 ve sonrasından eklenir. Alanlar masaüstü
`x/y/key/button` slotlarına semantik olarak sıkıştırılmaz. Nox bridge native
struct layout'ını public API yapmaz; `aura_window_event_*` erişim fonksiyonları
ve typed Aura sarmalayıcıları üzerinden okur.

Platform callback'i event payload'ının ownership'ini tutmaz. Host text ve
variable payload'ı kuyruğa kopyalar; batch clear, window destroy ve partial
initialization cleanup yolları bunları tam bir kez serbest bırakır. String
UTF-8'dir ve açık byte length taşır.

`poll_events` o input fazı için immutable bir batch snapshot'ı açar. Snapshot
açıldıktan sonra gelen olaylar sonraki batch'e gider. Böylece count ve alan
erişimleri sırasında index anlamı değişmez. Her pencere/scene kendi kuyruğuna,
sequence alanına ve aktif-pointer tablosuna sahiptir.

Yalnız ardışık, aynı pointer'a ait move/hover ve aynı pencereye ait bekleyen
metrics değişimleri coalesce edilebilir. Down, up, cancel, text/IME,
lifecycle, semantics action ve memory-pressure olayları coalesce edilmez.
Queue sınırı aşıldığında sessiz düşürme yapılmaz: coalescible kayıtlar önce
birleştirilir; yer kalmazsa overflow counter/diagnostic set edilir, aktif
pointer dizileri cancel edilir ve bu durum sonraki poll'da framework'e
bildirilir.

Pointer, tuş, lifecycle, metrics, environment, memory pressure, surface,
metin düzenleme ve platform route olayları platform callback'inin monoton
saatini taşır. iOS `CACurrentMediaTime`, Android `SystemClock.uptimeNanos`
kullanır. Host bu değeri callback girişinde alır; kuyruk, pencere açılışında
yeniden yayınlanan surface ve bekleyen route aynı değeri korur. Sıfır gelen
değer shell girişinde `CLOCK_MONOTONIC` ile doldurulur, pencere açılışında
yeniden damgalanmaz.

Frame sırası değişmez:

1. platform callback'leri kuyruğa yazar;
2. frame input fazı batch'i açıp sırayla uygular;
3. state mutation yalnız dirty mark üretir;
4. build/layout/paint/composite aynı frame scheduler kurallarını izler.

Lifecycle veya paint callback'i reentrant build/layout başlatmaz. Platform
timestamp'i gesture velocity ve ordering içindir; animation yalnız vsync frame
timestamp'ini kullanır.

## Alternatives

Her platform callback'inden Nox/UI koduna doğrudan dönmek daha az native kod
gerektirirdi; callback reentrancy, thread ownership ve batched rebuild
sözleşmesini bozardı. Bütün alanları eski `x/y/key/button` slotlarına koymak C
ABI'yi kısa tutardı fakat tür hatalarını runtime'a taşırdı. Platform başına
farklı event sınıfları widget katmanına UIKit/JNI ayrımı sızdırırdı.

## Consequences

Native shell event storage ve erişim yüzeyi genişler. Runtime input dispatch
typed olaylara geçerken eski masaüstü fixture'ları korunabilir. Pointer move
yükü coalescing ile sınırlanır; kritik olay kaybı diagnostic olmadan mümkün
değildir. Concurrent producer/read/clear, overflow, UTF-8 ownership ve window
destroy sanitizer testleri zorunludur.

## Migration

Önce yeni struct ve erişim fonksiyonları native/mobile fixture'ında eklenir.
Masaüstü hostlar aynı fixture'ı uygulamadan public Nox sarmalayıcı yeni alana
bağlanmaz. Sonra FramePump typed dispatch'e geçirilir. Eski erişim fonksiyonları
bir uyumluluk dilimi boyunca yeni snapshot'tan değer okur; bütün hostlar ve
testler taşındıktan sonra internal olarak kaldırılabilir.
