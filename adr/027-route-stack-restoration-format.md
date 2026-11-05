# ADR-027 Route stack restoration format v1

## Context

Mobil process death ve scene restoration için Navigator stack kimlikleri kalıcı
bir forma ihtiyaç duyar. Widget, State veya closure serialize edilemez; bunları
reflection ile kaydetmek lifecycle ve ownership invariant'larını bozar. Format
erken sürümlerde değişebileceği için versiyonsuz JSON/string listesi kalıcı
contract olamaz.

## Decision

Aura Route Stack Snapshot v1 yalnız sıralı route adlarını saklar. Magic ve
version zorunludur. Her string Unicode karakter sayısına dayalı length-prefix
ile yazılır; böylece URI, `:`, newline ve boşluk escape kuralına ihtiyaç duymaz:

`AURA-RS:<version>:<count>:<len>:<route>...`

Decoder exact tüketim ister; trailing data, eksik payload, negatif/çok büyük
count, boş route adı, desteklenmeyen version ve limit üstü ad deterministic
`FrameworkError` üretir. v1 limitleri 256 route ve route başına 4096
karakterdir. Snapshot Widget/State verisi veya auth token içermez.

`NavigatorHandle.snapshot()` aktif Navigator'ın retained entry adlarını bu
typed snapshot'a kopyalar. Bu milestone yalnız formatı ve encode/decode
contract'ını sabitler. Restore aşamasında uygulama route adı→Widget resolver'ı
sağlayacak; framework serialized Widget yaratmayacaktır.

## Alternatives

Widget graph serialization reflection ve closure capture gerektirir. Ayraçlı
kaçışsız string formatı URI/newline ile bozulur. Platform-specific Bundle veya
NSUserActivity'yi canonical format yapmak Android/iOS state'ini ayrıştırır.
Versiyonsuz JSON gelecekteki migration'ı belirsiz bırakır.

## Risks

Unicode `len` semantiği codec'in iki tarafında aynı Nox string modeliyle
çalışmalıdır. Native storage bu payload'ı byte olarak taşıyacaksa UTF-8
dönüşümünü değiştirmemelidir. Route adları hassas veri içermemelidir.

## Consequences

Format platformdan bağımsız, bounded ve fuzz edilebilirdir. `Navigator.with_restoration`
aynı version header'ını `support/aura-route-stack` dosyasından okur. Resolver
yalnız route adından Widget üretir; kök ad eşleşmezse veya bir ad çözülemezse
mevcut stack korunur. iOS scene activity yalnız `AURA-RS` sürüm 1 adını taşır; yığın baytını taşımaz. Başka bir sürüm veya biçim kaydı dosyadaki snapshot'ı siler. Activity yoksa dosya durur.

## Migration ve rollback

Yeni decoder ileride version'a göre ayrı parser seçer. v1 encoder sabit kalır.
Format kullanımı opt-in snapshot API'sidir; kaldırılması runtime Navigator
stack'ini etkilemez.
