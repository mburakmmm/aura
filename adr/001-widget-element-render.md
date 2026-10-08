# ADR-001 Widget, Element ve RenderObject

## Context

Aura, deklaratif bir widget ağacını uzun ömürlü bir runtime kimliği ve ayrı bir yerleşim/çizim ağacıyla bağlamak zorundadır. Nox 2.0'da `isinstance` yoktur ve somutlaşmış bir jenerik sınıf başka bir sınıfın tabanı olamaz. Çocuk listeleri protocol listesi olarak tutulamaz.

## Decision

Üç ağaç ayrıdır. Widget yalnızca konfigürasyondur. Element kimlik, lifecycle, kirli durum ve reconciliation sahibidir. RenderObject yerleşim, çizim, hit-test ve geometri sahibidir.

Kimlik, her widget'ın `__init__` içinde verdiği sabit `type_id` ile kurulur. Reconciliation aynı `type_id` ve uyumlu `Key` ile yapılır; anahtar yoksa kardeş sırası kullanılır. Algoritma önek taraması, sonek taraması, anahtarlı orta bölüm ve anahtarsız konum yedeğidir.

Konfigürasyon sanal metotlarla render nesnesine yazılır (`set_paragraph`, `set_button`, `set_sized`, `set_padding`, `set_color`, `set_flex_style`). `ValueKey` metin tutar; tam sayı anahtarı `IntValueKey`'dir. `ObjectKey` ve `GlobalKey` süreç içi tamsayı kimlik kullanır.

## Alternatives

Çalışma zamanı tip testi ve jenerik `State[T]` tabanı, Nox 2.0'ın sınıf modeli içinde ifade edilemiyordu. Widget ile RenderObject'i tek sınıfta birleştirmek yerleşim durumunu konfigürasyona taşırdı.

## Consequences

Yeni bir widget ailesi kendi `type_id` değerini alır ve gerekirse yeni bir sanal setter ekler. `State` jenerik değildir; sayaç gibi veriler somut state alanlarında durur. GlobalKey kaydı `dict[int, Element]` ile tutulmaz; döngü toplayıcının bu şekilde kapanan sözlüklerde tamamlanmadığı görüldüğü için paralel listeler kullanılır.

## Migration

Bu ayrım değişirse yeni bir ADR yazılır. Mevcut `type_id` değerleri süreç içinde `alloc_type_id()` ile verilir ve diske serileştirilmez.
