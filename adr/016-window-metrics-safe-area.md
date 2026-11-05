# ADR-016 WindowMetrics, safe area ve klavye inset'i

## Context

İlk mobil preview safe-area sol/üst değerini pointer koordinatından çıkarır ve
klavye yüksekliğini pencere yüksekliğinden düşürür. Bu yaklaşım sağ/alt safe
area'yı, cutout'u, tablet/foldable resize'ı ve uygulamanın inset'i bilinçli
kullanmasını ifade edemez. Pointer koordinat sistemi ile içerik padding'i de
birbirine karışır.

Aura constraint layout'u logical pixel ile çalışır. Mobil ortam bilgisi
global singleton veya platform tipi olamaz; yalnız bu değerlere bağımlı
Element'lar değişimde dirty olmalıdır.

## Decision

Aura additive public API olarak snapshot semantiğine sahip `WindowMetrics`
value object'i sunacaktır. Nox alanları yazılabilir olsa da Binding yayınlanmış
bir instance'ı yerinde mutate etmez; her metrics değişiminde yeni bir snapshot
oluşturur. Binding/scene başına en az şu alanlar bulunur:

- logical ve physical surface size;
- device pixel ratio ve refresh rate;
- `view_padding` safe-area değerleri;
- `view_insets` yazılım klavyesi ve geçici kapatıcı inset'ler;
- system-gesture inset ve display-cutout bilgisi;
- orientation;
- font/text scale, platform brightness ve high-contrast;
- locale listesi, text direction, reduce-motion ve erişilebilir gezinme
  tercihleri;
- monoton metrics revision.

Native host fiziksel/platform değerlerini tek kez logical pixel'e normalize
eder. Aura surface koordinat sisteminin başlangıcı her zaman tam yüzeyin
sol-üst köşesidir. Safe-area pointer koordinatından çıkarılmaz; padding olarak
metrics içinde taşınır. Böylece hit testing, paint ve platform semantics aynı
koordinat sistemini kullanır.

Metrics event'i frame input fazında Binding'e uygulanır. `window_metrics_of`
benzeri BuildContext lookup dependency kaydeder. Eşit snapshot, revision farklı
olsa bile yeni rebuild üretmez; değişen değer yalnız dependent Element'ları
dirty eder. Widget platform handle tutmaz; yayınlanmış snapshot uygulama kodu
tarafından mutate edilmemelidir.

`SafeArea`, `view_padding` değerlerinden seçilen kenarlara typed padding
uygulayan bir ComponentWidget'tır; RenderObject veya platform handle üretmez.
Klavye yüksekliği surface size'ı değiştirmez ve `view_insets.bottom` içinde
taşınır. Root uygulama varsayılan olarak keyboard avoidance isteyebilir;
Binding content constraint'ini `view_insets` ile daraltır, fakat tam metrics
uygulamaya açık kalır. Bu politika explicit bir app/window configuration ile
kapatılabilir.

Metrics değişimleri aynı frame'de son değer kazanacak şekilde coalesce
edilebilir. Orientation ayrı bir mutable global değildir; size ve metrics ile
aynı revision'da güncellenir. Frame sırasında reentrant layout yapılmaz;
değişim layout'u normal dirty queue üzerinden bir sonraki uygun layout fazına
alır.

## Alternatives

Her ekranda sabit `Padding` kullanmak notch, tablet ve rotation davranışını
uygulama koduna bırakırdı. Klavyeyi doğrudan pencere yüksekliğinden düşürmek
tam yüzey ölçüsünü kaybettirir ve overlay/semantics koordinatını bozar. Global
`MediaQuery` benzeri mutable singleton çoklu pencere/scene desteğini ve
fine-grained dependency tracking'i kapatırdı.

## Consequences

Metrics public API'ye additive olarak girer; 1.0 isimlerini kırmaz. Hostların
safe-area, keyboard, scale, locale ve accessibility tercihlerini ortak event
payload'ına çevirmesi gerekir. Layout, hit-test ve semantics aynı logical
koordinatı paylaşır. `SafeArea` widget testi gerçek pencere gerektirmez.

## Migration

Önce immutable value object, Binding storage ve headless dependency testleri
eklenir. Native preview eski resize olayını üretmeye devam ederken yeni metrics
alanları varsayılan sıfır inset ve mevcut scale ile doldurulur. Sonra iOS ve
Android host dört kenar/inset değerlerini gönderir. Son kapıda pointer'dan
safe-area çıkaran eski normalization ve klavye nedeniyle surface height
küçültme kaldırılır.

## Amendment

Snapshot'a şu alanlar eklenir. Constructor yalnız depo içinden çağrılır;
1.0 isimleri değişmez.

- `rotation_degrees`: portrait'tan saat yönünde 0/90/180/270. Android
  `Display.getRotation() * 90` ile aynıdır. iOS LandscapeRight 90,
  LandscapeLeft 270 değerini üretir.
- `bold_text`: 0 veya 1.
- `cutouts`: en fazla 8 logical `CutoutRect`. Fazlası `cutout_truncated` ile
  bildirilir. iOS'ta public cutout dikdörtgen listesi yoktur; notch
  `view_padding` içinde kalır ve liste boş gider.
- `keyboard_duration_s` ve `keyboard_curve`: 0 unspecified, 1 linear,
  2 ease-in, 3 ease-out, 4 ease-in-out. UIViewAnimationCurve bu tabloya
  çevrilir. Süre ve eğri metrics event'indedir, environment event'inde değil.
- `locale_list`: satır sonuyla ayrılmış BCP-47 listesi. `locale` ilk satırdır.
- `frame_budget_ms` saklanan alan değildir. `1000 / refresh_rate` döner;
  hız 0 veya negatifse 60 Hz bütçesi kullanılır.

Environment event'i coalesce edilmez. Host imzayı ancak shell olayı kuyruğa
kabul ettikten sonra hatırlar; pencere henüz hazır değilse bir sonraki
yayın yeniden dener. Android `windowSoftInputMode` `adjustNothing`dır ve API
30+ `setDecorFitsSystemWindows(false)` ile kenardan kenara çizer. Klavye
yüzeyi küçültmez; yükseklik `view_insets` olarak gelir.

Odaklanan metin alanının imleci inset'in altında kalırsa kök child, inset
kadar yukarı kayar. Kayma her layout'ta güncel inset'ten hesaplanır; host
animasyonun ara değerlerini metrics olarak gönderdikçe imleç görünür bölgede
kalır. Inset kapanınca kayma sıfırlanır. Yüzey boyutu değişmez.

`SafeArea` yalnız `view_padding` ve verilen minimumu kullanır. `view_insets`
safe area padding'ine eklenmez; klavye home indicator'ın yerini padding olarak
büyütmez. `KeyboardInset` seçilen kenarlara `view_insets` kadar padding uygular
ve varsayılan olarak yalnız alt kenarı kaplar. Negatif inset sıfırlanır. İkisi
de ComponentWidget'tır.

Pencere sınıfı logical genişlikten gelir: 600'den dar compact, 840'den dar
regular, kalanı wide. Eşikler platform size-class API'si değildir. Orientation
metrics snapshot'ını değiştirir; StatefulWidget state'i bu yüzden yeniden
kurulmaz.

Debug modda 44 logical pikselin altındaki button ve toggle bir kez tanı basar.
Bu tanı boyutu büyütmez ve release modda susar.
