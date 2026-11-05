# ADR-018 Mobil native runtime ownership ve shutdown

## Context

iOS ve Android preview host'ları Nox app'i ayrı kod yollarında oluşturuyor,
`nox_app_start` sonrasında app'i destroy etmiyor ve pending exception nesnesinin
private object layout'undan mesaj okumaya çalışıyor. Android worker thread'i
detach edildiği için Activity kapanışıyla runtime sahipliği arasında açık bir
ilişki de yok. Create/start failure, erken stop ve normal return aynı cleanup
yolundan geçmiyor.

Nox embed sözleşmesine göre config içindeki `NoxEmbedSlot` destroy tamamlanana
kadar yaşamalı, pending exception `nox_exception_take` sonrasında host
tarafından `nox_exception_release` ile bırakılmalı ve app tam bir kez destroy
edilmelidir. UI thread shutdown sırasında worker join ederek bloke edilmemeli.

## Decision

`native/mobile/app_runtime` process başına tek Nox app'in ownership state
machine'idir. Durumları `idle`, `starting`, `running`, `stopping`, `stopped` ve
`failed` olur. iOS ve Android yalnız start/request-stop çağrısı ile platform log
callback'i sağlar; create/bind/start/exception-release/destroy zinciri ortak
modülde tutulur.

Runtime kendi joinable olmayan worker thread'ini sahiplenir. UI lifecycle
callback'i yalnız stop flag'i koyar, varsa `nox_app_request_stop` çağırır ve
ortak shell pencerelerine close isteği gönderip frame wait condition'larını
uyandırır. Böylece Aura `run_app` döngüsü doğal olarak kapanır. Worker,
`nox_app_start` hangi sonuçla dönerse dönsün exception nesnesini güvenli public
API ile bırakır ve app'i tam bir kez destroy eder. UI thread worker join etmez.

Start öncesi stop, create failure, start failure, normal return ve pending
exception aynı terminal cleanup yoluyla test edilir. Log callback'ine private
Nox object pointer/layout bilgisi verilmez; güvenli message API bulunana kadar
yalnız hata sınıfı raporlanır.

`NoxHostConfig` içindeki storage path string'leri ve `NoxEmbedSlot`, destroy
bitene kadar ortak runtime tarafından yaşatılır. Process `chdir` kaldırıldı.
Asset ve depolama kökleri ADR-033 ile ayrılır.

## Alternatives

Her adapter'da ayrı cleanup kodu sürdürmek failure davranışını ve test
kapsamını ayrıştırır. Activity/scene destroy içinde thread join etmek UI thread'i
belirsiz süre bloke eder. App pointer'ını lifecycle callback'inden doğrudan
destroy etmek çalışan Nox entry ile use-after-free yarışına yol açar. Exception
mesajını object offset'iyle okumak ABI dışıdır.

## Consequences

İki mobil build ortak `app_runtime.c` nesnesini link eder. Host callback'leri
Widget/Element/RenderObject'a erişmez. Shell close isteği idempotenttir ve
yalnız pencere state/condition'ını değiştirir. Detached worker terminal cleanup
tamamlanınca condition ile test edilebilir; process çıkışı cleanup'ın yerine
geçmez.

Tek process/tek app kararı iPad multi-scene veya Android çoklu Activity desteği
öncesinde yeniden ADR gerektirir. Depolama kökleri ADR-033 ile ayrıldı.

## Migration ve rollback

Önce ortak state machine fake Nox embed API ile ASan/UBSan testine alınır;
sonra Android ve iOS duplicate start kodları kaldırılır. Sorunda adapter'lar
eski start yoluna dönebilir, ancak private exception layout okuması ve eksik
destroy geri getirilemez. Ortak shell/event ABI additive kalır.
