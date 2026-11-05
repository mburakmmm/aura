# ADR-024 Focus traversal ve hardware keyboard normalizasyonu

## Context

Aura'nın `FocusManager` ağacı lifecycle ile askıya alınabiliyor fakat odak
düğümleri arasında deterministik gezinme, yönsel seçim ve platformlar arası
ortak key/modifier sözleşmesi yoktur. Mevcut desktop host'ları native key
değerlerini doğrudan geçirir; mobil event ABI'sindeki ayrı `modifiers` alanı da
Nox binding tarafından okunmayıp pointer `buttons` alanıyla karıştırılır.

Text input ile hardware key aynı veri değildir. Key event'ten karakter üretmek
IME composition, klavye düzeni ve dead-key davranışını bozar. Focus kararını
RenderObject'a veya native view'a taşımak da bağımsız focus tree invariant'ını
bozar.

## Decision

Aura aşağıdaki sözleşmeyi kullanır:

1. `aura.input.keyboard` platformdan bağımsız logical key ve modifier
   sabitlerinin tek kaynağıdır. Mobil adapter'lar Android/iOS değerlerini bu
   sözleşmeye normalize eder; metin yalnız `TEXT`/`IME_*` event'leriyle akar.
2. Mobile event ABI'deki `buttons` ve `modifiers` ayrı accessor'larla Nox'a
   taşınır. Pointer dispatch `buttons`, keyboard dispatch `modifiers` kullanır.
3. Focus traversal aktif Element ağacının layout sonrası depth-first sırasından
   üretilen kararlı `traversal_order` ile çalışır. Tab ileri, Shift-Tab geri
   gider ve uçlarda sarar. Disabled, detach edilmiş veya retained fakat gizli
   route altındaki düğüm aday değildir.
4. Directional focus, focus node üzerinde tutulan son layout bounds snapshot'ı
   ile en yakın uygun merkezi seçer. Önce ana eksen mesafesi, sonra çapraz eksen
   sapması değerlendirilir. Text-editing düğümlerinde oklar caret hareketine
   ayrılır; yönsel traversal yalnız text olmayan focus hedeflerinde çalışır.
5. Focus tree rendering katmanını import etmez. Element, layout tamamlandıktan
   sonra bağlı RenderBox'ın global bounds'unu FocusNode'a kopyalar. Böylece
   focus bağımsız tree kalır ve yalnız immutable sayısal snapshot görür.
6. Lifecycle/surface askıdayken traversal ve key dispatch tüketilmez. Focus
   identity korunur; resume sonrasında aynı düğüm devam eder.

## Alternatives

Native view'ların kendi focus sırasını kullanması retained Element identity ve
multi-scene izolasyonunu iki state kaynağına böler. RenderObject referansını
FocusNode'a koymak input→rendering bağımlılığı yaratır. KeyDown'dan Unicode
üretmek IME'yi atlar. Yönsel gezinmeyi yalnız kayıt sırası yapmak görsel grid ve
yeniden akışlarda beklenmeyen hedef seçer.

## Risks

Bounds snapshot bir layout tamamlanana kadar geçersiz olabilir; bu durumda
yönsel traversal kararlı sıra fallback'ini kullanır. Açık traversal group ve
policy API'si bu ilk sözleşmenin kapsamı dışındadır ve ayrı bir ADR gerektirir.
Desktop adapter'ların eski raw key değerleri geçiş döneminde compatibility
eşlemeleriyle kabul edilir; yeni mobil adapter'lar yalnız normalize değer
üretir.

## Consequences

Widget/Element/RenderObject sınırı korunur. Headless testler native pencere
olmadan sıra ve yön seçimini doğrulayabilir. Android ve iOS hardware keyboard
aynı logical key/modifier değerlerini üretir; IME composition bağımsız kalır.

## Migration ve rollback

Accessor ve native key callback'leri additive'dir. Mobil key callback kayıtları
kaldırıldığında touch/IME davranışı değişmeden kalır. FocusNode'un yeni alanları
default değerlerle eski düğümleri destekler; traversal çağrıları kaldırılırsa
mevcut doğrudan focus davranışı korunur.
