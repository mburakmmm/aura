# ADR-021 Multi-pointer normalization ve gesture arena

## Context

Aura'nın mevcut mobil adapter'ları transitional pointer helper kullanıyor;
pointer/device kimliği ve gerçek platform timestamp'i sıfır kalıyor. Android
yalnız action index yerine çoğu olayı tek pointer gibi ele alıyor, iOS `UITouch`
identity'sini taşımıyor. Nox `GestureArena` da bütün pointer'lar için tek mutable
state kullanıyor; ikinci parmak ilk sequence'in tap/scroll/velocity state'ini
ezebilir.

## Decision

Platform adapter her fiziksel pointer sequence'i stable `pointer_id` ve
`device_id` ile `AuraPlatformEvent` kuyruğuna yazar. Koordinatlar view-local
physical pixel olarak tek native normalizer'a girer ve yalnız DPR ile logical
pixel'e çevrilir; safe-area çıkarılmaz. Platform monotonic event timestamp'i
nanosecond alanına taşınır.

Android DOWN/POINTER_DOWN ve UP/POINTER_UP için action index'i, MOVE için bütün
pointer'ları işler. Historical move örnekleri timestamp sırasıyla current
sample'dan önce kuyruğa girer. CANCEL bütün event pointer'larına uygulanır.
Tool type, pressure, tilt, buttons ve device ID normalize edilir.

iOS her `UITouch` için canvas-scope monoton ID üretir; pointer sona erince map
girdisi bırakılır. Aynı callback'teki touch'lar pointer ID ile deterministik
sıralanır. Touch timestamp, type ve normalized force taşınır. Coalesced/predicted
touch politikası ayrı performans ölçümü sonrası açılır; predicted sample v1'de
kuyruğa yazılmaz.

Ortak FFI pointer/device/timestamp alanlarını Binding'e açar. `GestureArena`
her pointer ID için ayrı `GestureSequence` tutar. Tap/scroll/text/drag state'i,
slop, velocity ve winner pointer sequence'e aittir. Tek pointer cancel yalnız
ilgili sequence'i; lifecycle/surface/overflow cancel bütün sequence'leri
temizler. Fling yalnız biten sequence'in sonucundan başlatılır.

Scale recognizer aynı active sequence registry üzerinde kurulur. Aynı
`RenderBox` hedefinde, iki primary pointer henüz başka bir recognizer'a
kaybetmemişken ikinci pointer gelirse scale team kurulur; iki sequence'in tap
üyeleri reddedilir ve başlangıç mesafesine göre boyutsuz scale oranı üretilir.
Bir pointer kalkınca takım biter, kalan pointer aynı sequence içinde yeniden
tap/pan/scroll kazanamaz. Başlamış pan, scroll, text selection veya başka bir
single-pointer winner ikinci pointer tarafından çalınmaz. Rotation ayrı bir
recognizer ve ayrı bir ölçüm/ürün ihtiyacı olarak ertelenir.

## Alternatives

Platform pointer ID'sini yok sayıp action index kullanmak parmaklar yer
değiştirdiğinde identity'yi bozar. `UITouch` adresini doğrudan public ID yapmak
reuse ve log sızıntısı riski taşır. Global tek arena state'ini kilitlemek state
ezilmesini çözmez. Safe-area'yı koordinattan çıkarmak hit-test ile layout
coordinate space'ini ayırır.

## Risks

Historical sample sayısı büyük olabilir; queue bound/coalescing halen geçerli
olmalıdır. İki sequence farklı scroll target'larını eşzamanlı sürükleyebilir;
aynı hedefte scale yalnız single-pointer winner oluşmadan önce öncelik alır.
iOS touch map cleanup cancel/end yollarının tamamında test edilmelidir.

## Consequences

Desktop backend'ler yeni accessor sembollerine sıfır dönen compatibility stub
ekler; mevcut mouse testleri pointer ID `0` ile değişmeden çalışır. Mobile
adapter kaynakları transitional pointer API'yi bırakır. Headless test iki
eşzamanlı pointer'ın state ve tap sonucunu birbirinden ayırdığını doğrular.

## Migration ve rollback

Önce accessor ve platform normalization fixture'ları, sonra per-pointer arena
eklenir. Sorunda adapter pointer ID'si `0` gönderebilir; ABI additive kalır.
Tek-state arena geri getirilemez çünkü multi-touch identity public event
sözleşmesinin parçası olur.
