# ADR-022 Nested scroll delta tüketimi

## Context

Bir pointer hit-test path'inde birden fazla scrollable viewport bulunabilir.
Önceki arena yalnız ilk `has_scroll` RenderBox'ını saklıyor ve `scroll_by()`
çağrısının ne kadar delta tükettiğini bilmiyordu. İç viewport sınırdayken drag
kayboluyor, parent viewport'a aktarılmıyor ve hangi host'un fling alacağı
belirsiz kalıyordu.

## Decision

Hit-test path'indeki scroll hedefleri leaf-to-root sırayla bir pointer
sequence'inde tutulur. RenderBox additive `consume_scroll(delta) -> remaining`
sözleşmesi sunar. En iç hedef önce tüketir; sınır nedeniyle kalan delta sıradaki
ancestor scroll hedeflerine aktarılır. `scroll_by()` compatibility için korunur
ve tüketim bilgisi olmayan eski/custom RenderBox varsayılan olarak deltanın
tamamını tüketmiş sayılır.

`RenderViewport`, `ScrollController` pozisyonunun clamp öncesi/sonrası farkını
kullanarak gerçek remainder üretir. Fling yalnız son delta tüketen host'a
bağlanır ve host sınırda remainder ürettiğinde durur. Yön tersine döndüğünde
her örnek yeniden leaf'ten başlar; böylece iç viewport tekrar tüketebilir.
Fling decay ve yer değiştirmesi sürekli üstel modelin frame aralığındaki tam
integraliyle hesaplanır; aynı başlangıç hızı 60/90/120 Hz'de aynı mesafeyi
üretir. Kayan nokta remainder artıkları ortak logical-pixel epsilon ile sıfıra
normalize edilir.

TextField içindeki yatay-dominant drag selection'a, dikey-dominant drag ise
ancestor scroll'a verilir. Button tap slop aşılınca reddedilir ve scroll path'i
kazanır. Bu seçimler event sayısına değil logical mesafe ve platform monotonic
timestamp'ine dayanır.

## Alternatives

Yalnız en iç scroll'u seçmek parent'a aktarımı engeller. Yalnız en dış scroll'u
seçmek nested viewport'u kullanılamaz yapar. Scroll pozisyonunu gesture
katmanından okumak rendering/layout sınırını bozar. Bütün scroll'ları aynı
deltayla hareket ettirmek delta'yı çoğaltır.

## Risks

Custom scroll RenderBox'ları remainder üretmek istiyorsa `consume_scroll`u
override etmelidir; yalnız `scroll_by` kullanan eski implementasyon tam tüketim
semantiğinde kalır. İki eksenli ve overscroll/bounce davranışı bu dikey v1
sözleşmesinin dışındadır. Fling'in nested parent'a momentum devri ileride ayrı
ölçüm ve ADR gerektirebilir.

## Consequences

Nested drag toplam deltayı çoğaltmadan içten dışa aktarır. Pointer sequence tek
winner olarak kalır; yalnız tüketici host değişebilir. Widget/Element katmanı
scroll pozisyonunu veya platform tipini görmez.

## Migration ve rollback

Mevcut `scroll_by` override'ları değişmeden derlenir. Boundary-aware custom
scroll'lar kademeli olarak `consume_scroll` ekleyebilir. Sorunda gesture arena
yalnız ilk hedefi çağıracak şekilde geri alınabilir; additive RenderBox metodu
ABI/API uyumluluğunu bozmaz.
