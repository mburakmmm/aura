# ADR-003 State ve Signal

## Context

Aura'da state değişimi aynı karede birden fazla rebuild üretmemelidir. Nox, somutlaşmış jenerik sınıfı taban olarak kabul etmez ve `weakref` sunmaz. Signal ve Computed bu dilimin dışında bırakılmıştır; sayaç yine de çalışan bir state modeli ister.

## Decision

`State` jenerik değildir. Widget alanları `create_state` sırasında state kurucusuna kopyalanır. `invalidate` elementi kirli kuyruğa ekler. Aynı element aynı temizlemede bir kez durur. Temizleme sıra ebeveyn derinliği artan yöndedir. Üç `invalidate` tek rebuild üretir.

`dispose` iki kez çağrılırsa `dispose called twice` hatasıdır. Dispose veya unmount sonrası `invalidate`, `set_state after dispose` hatasıdır. Dinleyici listeleri unmount sırasında açıkça silinir. `BuildContext` saklanmaz; her build için kısa ömürlü bir görünüm üretilir, böylece Element ile Context arasında döngü kurulmaz.

Signal, Computed ve Effect sonraki bir dilimdedir. Bağımlılık kaydı o zaman zayıf değil, unmount ile silinen açık abonelik olacaktır.

## Alternatives

Jenerik `State[T]` derleyici sınırına takıldı. Anında özyinelemeli rebuild, art arda gelen üç atamada üç kez ağaç kurardı. Global mutable UI durumu framework boyunca dağılırdı.

## Consequences

Sayaç verisi `CounterState.count` alanındadır. `did_update_widget` eski widget'ı `Widget` olarak alır. Kirli temizleme 64 turdan sonra `cyclic build dependency` verir.

## Migration

Signal eklendiğinde bu ADR'nin kararını bozmaz; yeni bir ADR abonelik ömrünü yazar. `State` jenerik yapılmaz.
