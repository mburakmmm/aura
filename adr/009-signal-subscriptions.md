# ADR-009 Signal abonelikleri

## Context

ADR-003 state invalidation'ını kilitler ve Signal aboneliğinin ömrünü ayrı bir karara bırakır. Nox `weakref` sunmaz. `dict[int, Element]` döngü toplayıcısını süreç çıkışında askıda bırakır. Signal, Computed ve Effect widget ağacının yerine geçmez; yalnız okuyan elementi kirletir.

## Decision

`Signal` tamsayı, `TextSignal` metin tutar. Nox generic sınıfı alan tipi olarak kabul etmediği için bu somut tipler kullanılır. `set` aynı değerde bildirim üretmez. `get`, build sırasında aktif elementin kimliğini kaydeder. Sinyal element nesnesi saklamaz. Abonelik, framework modülünde sinyal kimliği ile element kimliğinin paralel listesidir. `BuildOwner` monte elementleri paralel kimlik ve nesne listelerinde tutar, bildirimi bu listeden kirli kuyruğa çevirir. Unmount aynı listedeki kimliği siler; sinyal modülüne geri çağrı yoktur.

`Computed` okuduğu sinyallere abone olur. Değerlendirme yığınında aynı kimlik görünürse `cyclic computed dependency` hatası verilir. Element unmount, kendi kimliğini sinyallerden siler. Build kapsamı iç içe rebuild için bir yığındır ve rebuild bitince kalkar.

Sayaç `State.invalidate` çağırmaz. Düğme sinyal yazar. Yalnız sinyali okuyan builder yeniden kurulur.

## Alternatives

Element nesnesini sinyalde tutmak unmount sonrası sarkan referans bırakırdı. Global rebuild, statik başlığı da yeniden kurardı. `weakref` dilde yoktur.

## Consequences

Tek UI iş parçacığı varsayımı sürer. Sinyal kaydı framework build kapsamına bağlıdır. Olay içinden `get` abonelik eklemez.

## Migration

Abonelik kimliği değişirse `BuildOwner` araması ve unmount temizliği birlikte güncellenir. `State.invalidate` durur.
