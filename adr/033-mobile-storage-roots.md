# ADR-033 Mobil depolama kökleri ve asset çözümlemesi

ADR-MOB-AURA-013.

## Context

Mobil runtime tek bir kökü hem Nox `documents` hem `support`, `cache` ve
`temp` alanına yazıyor, asset aramasını da process `chdir` ile bundle dizinine
bağlıyordu. `chdir` process genelidir; aynı süreçteki başka işin göreli
dosyasını kaydırır. Cache ile kullanıcı belgesi aynı dizinde olursa bellek
baskısında belge silinebilir ve cache yedeklenir.

Route stack formatı ADR-027'dedir. Bu karar o formatın nereye yazılacağını
seçer. Geri kurulum, uygulama `with_restoration` resolver'ı verdiğinde
framework'ün okuduğu bu dosyadan yapılır; Widget serialize edilmez.

## Decision

Beş kök vardır: `documents`, `support`, `cache`, `temp`, `assets`. İlk dördü
`NoxHostConfig` alanlarına ayrı ayrı gider. `assets` Nox config'inde yoktur;
Aura dosya açmadan önce göreli yolu bu köke bağlar. Mutlak yol olduğu gibi
kalır. `..`, ters bölü ve `:` içeren göreli yol reddedilir. Kök boşsa runtime
başlamaz. Process `chdir` yapılmaz.

`documents` ve `support` yedeklenir. `cache`, `temp` ve `assets` yedeklenmez.
iOS cache ve temp dizinine backup exclusion işareti konur. Android cache,
platformun yedeklemediği cache dizinidir.

Kritik bellek baskısı cache kökünün çocuklarını siler. Kökün kendisi, `/`,
`.` ve documents/support/assets ile aynı yol silinmez. Sembolik link izlenmez.

Android asset çıkarması sürüm damgasına bakar. Damga `AURA_VERSION` ile aynıysa
çıkarma tekrarlanmaz. Değilse staging dizinine çıkarılır, damga yazılır ve
dizin asset kökünün yerine adlandırılır. iOS asset kökü bundle'dır; çıkarma
yoktur.

Route snapshot yalnız `support/aura-route-stack` dosyasına yazılır ve yalnız
`AURA-RS:1:` ile başlayan baytı kabul eder. Hata logu `aura <sürüm> embed-abi <n>:`
önekini taşır.

## Alternatives

Tek köke `chdir` göreli asset'i çalıştırır ama process genelidir. Cache'i
documents altına koymak yedek ve silme sınırını kaldırır. Asset kökünü Nox
compiler'ına yeni bir config alanı olarak eklemek bu deponun dışındadır.
Widget ağacına dosya yolu vermek platform handle'ını UI katmanına taşır.

## Consequences

Masaüstünde asset kökü kurulmaz; göreli görüntü yolu çalışma dizinine göre
kalır. Mobil görüntü açılışı bundle veya çıkarılmış asset köküne bağlanır.
Pause, hidden ve terminated geçişlerinde restoration açılmış kök Navigator
stack'i `support/aura-route-stack` dosyasına yazılır. Sonraki açılış dosyayı
okur ve uygulama resolver'ı ile sayfaları yeniden kurar. Resolver bilinmeyen
bir ad döndürürse stack değişmez.

## Migration ve rollback

Host'lar tek kök ve `chdir` yerine beş kök verir. Eski tek-kök çağrı derlenmez.
Sorunda çağrı geri alınabilir; `chdir` ve cache ile belgenin aynı dizinde
silinmesi geri getirilemez.

## Risk

Asset kökü yanlış dizine kurulursa göreli dosya açılışı boş döner. Damga sürümü
manifest ile `build_identity.h` üzerinden denetlenir. Purge, cache yolu mutlak
değilse hiç silmez.
